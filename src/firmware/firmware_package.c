/*
 * LEGAL NOTICE
 *
 * Copyright (C) 2015 InventIt Inc. All rights reserved.
 *
 * This source code, product and/or document is protected under licenses
 * restricting its use, copying, distribution, and decompilation.
 * No part of this source code, product or document may be reproduced in
 * any form by any means without prior written authorization of InventIt Inc.
 * and its licensors, if any.
 *
 * InventIt Inc.
 * 9F, Kojimachi 4-4-7, Chiyoda-ku, Tokyo 102-0083
 * JAPAN
 * http://www.yourinventit.com/
 */
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <dirent.h>

#include <servicesync/moat.h>

#include "firmware_package.h"

#define TAG "FirmwarePackage"

#define PATH_DELIMITER_CHR '/'
#define FWPKG_FILE_NAME  "fwpackage.bin"
#define FWPKG_DIR_NAME  "fwpackage"

enum {
  FWPKG_STATE_DORMANT = 0,
  FWPKG_STATE_EXTRACTING,
  FWPKG_STATE_EXTRACTED,
  FWPKG_STATE_EXTRACT_FAILED,
  FWPKG_STATES
};

static sse_char *
FirmwarePackage_MakeFullPath(sse_char *in_base_path, sse_char *in_path)
{
  sse_char cwd[PATH_MAX];
  sse_int len;
  sse_char *p;

  if (in_base_path == NULL) {
    p = getcwd(cwd, sizeof(cwd));
    if (p == NULL) {
      int err_no = errno;
      MOAT_LOG_ERROR(TAG, "failed to getcwd(). err=[%s]", strerror(err_no));
      return NULL;
    }
    in_base_path = cwd;
  }
  len = sse_strlen(in_base_path);
  len++;
  len += sse_strlen(in_path);
  p = sse_malloc(len + 1);
  if (p == NULL) {
    MOAT_LOG_ERROR(TAG, "failed to sse_malloc().");
    return NULL;
  }
  snprintf(p, len + 1, "%s%c%s", in_base_path, PATH_DELIMITER_CHR, in_path);
  return p;
}

static void
FirmwarePackage_RemoveDir(sse_char *in_dir_path)
{
  DIR *dir;
  struct dirent *entry;
  struct stat st;
  sse_char *org_wd = NULL;
  sse_char *p;
  sse_char *path = NULL;

  dir = opendir(in_dir_path);
  if (dir == NULL) {
    return;
  }
  org_wd = sse_malloc(PATH_MAX);
  if (org_wd == NULL) {
    return;
  }
  p = getcwd(org_wd, PATH_MAX);
  if (p == NULL) {
    sse_free(org_wd);
    return;
  }
  chdir(in_dir_path);
  entry = readdir(dir);
  while (entry != NULL) {
    lstat(entry->d_name, &st);
    if (S_ISDIR(st.st_mode)) {
      if (sse_strcmp(".", entry->d_name) == 0 || sse_strcmp("..", entry->d_name) == 0) {
        entry = readdir(dir);
        continue;
      }
      path = FirmwarePackage_MakeFullPath(in_dir_path, entry->d_name);
      FirmwarePackage_RemoveDir(path);
      sse_free(path);
      chdir(in_dir_path);
    } else {
      unlink(entry->d_name);
    }
    entry = readdir(dir);
  }
  closedir(dir);
  remove(in_dir_path);
  chdir(org_wd);
  sse_free(org_wd);
}

static sse_int
TFirmwarePackage_HandleExtractResult(TFirmwarePackage *self, sse_int in_err, sse_char *in_err_info)
{
  sse_int err;
  if (self->fExtractCallback == NULL) {
    return SSE_E_INVAL;
  }
  if (self->fState != FWPKG_STATE_EXTRACTING) {

    return SSE_E_INVAL;
  }
  if (in_err == SSE_E_OK) {
    MOAT_LOG_DEBUG(TAG, "Extracted");
    self->fState = FWPKG_STATE_EXTRACTED;
  } else {
    MOAT_LOG_DEBUG(TAG, "Extract Failed:%d", in_err);
    self->fState = FWPKG_STATE_EXTRACT_FAILED;
  }
  err = (*self->fExtractCallback)(self, in_err, in_err_info, self->fExtractUserData);
  MOAT_LOG_DEBUG(TAG, "Callback result=%d", err);
  return err;
}

static void
FirmwarePackage_OnExtractCompleted(TSseUtilShellCommand* self, sse_pointer in_user_data, sse_int in_result)
{
  int err;
  sse_char *err_info = NULL;
  if (in_result == 0) {
      err = SSE_E_OK;
  } else {
    err = SSE_E_GENERIC;
    err_info = "Failed to extract command";
  }
  MOAT_LOG_DEBUG(TAG, "Extract Completed: result=%d", in_result);
  TFirmwarePackage_HandleExtractResult((TFirmwarePackage *)in_user_data, err, err_info);
}

static void
FirmwarePackage_OnExtractError(TSseUtilShellCommand* self, sse_pointer in_user_data, sse_int in_error_code, const sse_char* in_message)
{
  MOAT_LOG_ERROR(TAG, "Extract Error: err=%d", in_error_code);
  TFirmwarePackage_HandleExtractResult((TFirmwarePackage *)in_user_data, in_error_code, (sse_char *)in_message);
}

sse_int
TFirmwarePackage_Extract(TFirmwarePackage *self, FirmwarePackage_ExtractCallback in_callback, sse_pointer in_user_data)
{
  MoatValue *dir_path = NULL;
  TSseUtilShellCommand *command = NULL;
  sse_int err;

  dir_path = moat_value_new_string(self->fPackageDirPath, 0, sse_true);
  if (dir_path == NULL) {
    MOAT_LOG_ERROR(TAG, "failed to create moat_value for dir_path.");
    err = SSE_E_NOMEM;
    goto error_exit;
  }
  FirmwarePackage_RemoveDir(self->fPackageDirPath);
  err = SseUtilFile_MakeDirectory(dir_path);
  if (err != SSE_E_OK) {
    MOAT_LOG_ERROR(TAG, "failed to SseUtilFile_MakeDirectory(). path=[%s], err=%d", self->fPackageDirPath, err);
    goto error_exit;
  }
  command = SseUtilShellCommand_New();
  if (command == NULL) {
    MOAT_LOG_ERROR(TAG, "failed to SseUtilShellCommand_New().");
    return SSE_E_NOMEM;
  }
  {
    sse_char pp[PATH_MAX];
    getcwd(pp, sizeof(pp));
    MOAT_LOG_ERROR(TAG, "CWD:%s", pp);
  }
  TSseUtilShellCommand_SetOnComplatedCallback(command, FirmwarePackage_OnExtractCompleted, self);
  TSseUtilShellCommand_SetOnErrorCallback(command, FirmwarePackage_OnExtractError, self);
  err = TSseUtilShellCommand_SetShellCommand(command, "unzip");
  if (err != SSE_E_OK) {
    MOAT_LOG_ERROR(TAG, "failed to TSseUtilShellCommand_SetShellCommand(). err=%d", err);
    goto error_exit;
  }
  err = TSseUtilShellCommand_AddArgument(command, FWPKG_FILE_NAME);
  if (err != SSE_E_OK) {
    MOAT_LOG_ERROR(TAG, "failed to TSseUtilShellCommand_AddArgument(). err=%d", err);
    goto error_exit;
  }
  err = TSseUtilShellCommand_AddArgument(command, "-d");
  if (err != SSE_E_OK) {
    MOAT_LOG_ERROR(TAG, "failed to TSseUtilShellCommand_AddArgument(). err=%d", err);
    goto error_exit;
  }
  err = TSseUtilShellCommand_AddArgument(command, FWPKG_DIR_NAME);
  if (err != SSE_E_OK) {
    MOAT_LOG_ERROR(TAG, "failed to TSseUtilShellCommand_AddArgument(). err=%d", err);
    goto error_exit;
  }
  err = TSseUtilShellCommand_AddArgument(command, "-o");
  if (err != SSE_E_OK) {
    MOAT_LOG_ERROR(TAG, "failed to TSseUtilShellCommand_AddArgument(). err=%d", err);
    goto error_exit;
  }
  err = TSseUtilShellCommand_Execute(command);
  if (err != SSE_E_OK) {
    MOAT_LOG_ERROR(TAG, "failed to TSseUtilShellCommand_Execute(). err=%d", err);
    goto error_exit;
  }
  moat_value_free(dir_path);
  self->fExtractCallback = in_callback;
  self->fExtractUserData = in_user_data;
  self->fState = FWPKG_STATE_EXTRACTING;
  return SSE_E_OK;

error_exit:
  if (dir_path != NULL) {
    moat_value_free(dir_path);
  }
  if (command != NULL) {
    TSseUtilShellCommand_Delete(command);
  }
  return err;
}

TFirmwarePackage *
FirmwarePackage_New(void)
{
  TFirmwarePackage *package = NULL;
  sse_char *file_path = NULL;
  sse_char *dir_path = NULL;
  int err = 0;

  package = sse_zeroalloc(sizeof(TFirmwarePackage));
  file_path = FirmwarePackage_GetPackageFilePath();
  if (file_path == NULL) {
    goto error_exit;
  }
  dir_path = FirmwarePackage_GetPackageDirPath();
  if (dir_path == NULL) {
    goto error_exit;
  }
  err = TSseUtilShellCommand_Initialize(&package->fExtractCommand);
  if (err != SSE_E_OK) {
    goto error_exit;
  }
  package->fState = FWPKG_STATE_DORMANT;
  package->fPackageFilePath = file_path;
  package->fPackageDirPath = dir_path;
  return package;

error_exit:
  if (package != NULL) {
    sse_free(package);
  }
  return NULL;
}

void
TFirmwarePackage_Delete(TFirmwarePackage *self)
{
  TSseUtilShellCommand_Finalize(&self->fExtractCommand);
  if (self->fPackageDirPath != NULL) {
//    FirmwarePackage_RemoveDir(package->fPackageDirPath);
    sse_free(self->fPackageDirPath);
  }
  if (self->fPackageFilePath != NULL) {
//    SseUtilFile_DeleteFile(package->fPackageFilePath);
    sse_free(self->fPackageFilePath);
  }
  sse_free(self);
}

sse_char *
FirmwarePackage_GetPackageFilePath(void)
{
  return FirmwarePackage_MakeFullPath(NULL, FWPKG_FILE_NAME);
}

sse_char *
FirmwarePackage_GetPackageDirPath(void)
{
  return FirmwarePackage_MakeFullPath(NULL, FWPKG_DIR_NAME);
}
