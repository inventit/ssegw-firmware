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

#define TRACE_ENTER() MOAT_LOG_TRACE(TAG, "== enter =>");
#define TRACE_LEAVE() MOAT_LOG_TRACE(TAG, "<= leave ==");
#define LOG_ERROR(format, ...)  MOAT_LOG_ERROR(TAG, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) MOAT_LOG_INFO(TAG, format, ##__VA_ARGS__)
#define LOG_DEBUG(format, ...)  MOAT_LOG_DEBUG(TAG, format, ##__VA_ARGS__)

#define PATH_DELIMITER_CHR '/'
#define FWPKG_FILE_NAME  "fwpackage.bin"
#define FWPKG_DIR_NAME  "fwpackage"
#define FWPKG_UPGRADE_SCRIPT_PATH "fw/fw_upgrade.sh"
#define FWPKG_CHECK_SCRIPT_PATH "fw/check_result.sh"

#define USE_SYSTEM_TO_EXTRACT

enum {
  FWPKG_STATE_DORMANT = 0,
  FWPKG_STATE_EXTRACTING,
  FWPKG_STATE_EXTRACTED,
  FWPKG_STATE_VERIFIED,
  FWPKG_STATE_EXTRACT_FAILED,
  FWPKG_STATE_UPDATING,
  FWPKG_STATE_CHECKING,
  FWPKG_STATES
};

static sse_char *
FirmwarePackage_MakeFullPath(sse_char *in_base_path, sse_char *in_path)
{
  sse_char cwd[PATH_MAX];
  sse_int len;
  sse_char *p;

  TRACE_ENTER();
  if (in_base_path == NULL) {
    p = getcwd(cwd, sizeof(cwd));
    if (p == NULL) {
      int err_no = errno;
      LOG_ERROR("failed to getcwd(). err=[%s]", strerror(err_no));
      return NULL;
    }
    in_base_path = cwd;
  }
  len = sse_strlen(in_base_path);
  len++;
  len += sse_strlen(in_path);
  p = sse_malloc(len + 1);
  if (p == NULL) {
    LOG_ERROR("failed to sse_malloc().");
    return NULL;
  }
  snprintf(p, len + 1, "%s%c%s", in_base_path, PATH_DELIMITER_CHR, in_path);
  TRACE_LEAVE();
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
  sse_int result;

  TRACE_ENTER();
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
  result = chdir(in_dir_path);
  LOG_DEBUG("chdir(%s):result=%d", in_dir_path, result);
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
      result = chdir(in_dir_path);
      LOG_DEBUG("chdir(%s):result=%d", in_dir_path, result);
    } else {
      unlink(entry->d_name);
    }
    entry = readdir(dir);
  }
  closedir(dir);
  remove(in_dir_path);
  result = chdir(org_wd);
  LOG_DEBUG("chdir(%s):result=%d", org_wd, result);
  sse_free(org_wd);
  TRACE_LEAVE();
}

static sse_int
TFirmwarePackage_HandleExtractResult(TFirmwarePackage *self, sse_int in_err, sse_char *in_err_info)
{
  sse_int err;

  TRACE_ENTER();
  if (self->fCommandCallback == NULL) {
    LOG_ERROR("CommandCallback is nil.");
    return SSE_E_INVAL;
  }
  if (in_err == SSE_E_OK) {
    LOG_DEBUG("Extracted");
    self->fState = FWPKG_STATE_EXTRACTED;
  } else {
    LOG_ERROR("Extract Failed:%s", sse_get_error_string(in_err));
    self->fState = FWPKG_STATE_EXTRACT_FAILED;
  }
  err = (*self->fCommandCallback)(self, in_err, in_err_info, self->fCommandUserData);
  LOG_DEBUG("Callback result=%s", sse_get_error_string(err));
  TRACE_LEAVE();
  return err;
}

static sse_int
TFirmwarePackage_HandleCheckResultResult(TFirmwarePackage *self, sse_int in_err, sse_char *in_err_info)
{
  sse_int err;

  TRACE_ENTER();
  if (self->fCommandCallback == NULL) {
    LOG_ERROR("CommandCallback is nil.");
    return SSE_E_INVAL;
  }
  self->fState = FWPKG_STATE_DORMANT;
  err = (*self->fCommandCallback)(self, in_err, in_err_info, self->fCommandUserData);
  LOG_DEBUG("Callback result=%s", sse_get_error_string(err));
  TRACE_LEAVE();
  return err;
}

static sse_int
TFirmwarePackage_HandleCommandResult(TFirmwarePackage *self, sse_int in_err, sse_char *in_err_info)
{
  sse_int err = SSE_E_OK;

  TRACE_ENTER();
  if (self->fCurrentCommand != NULL) {
    TSseUtilShellCommand_Delete(self->fCurrentCommand);
    self->fCurrentCommand = NULL;
  }
  switch (self->fState) {
  case FWPKG_STATE_EXTRACTING:
    err = TFirmwarePackage_HandleExtractResult(self, in_err, in_err_info);
    break;
  case FWPKG_STATE_CHECKING:
    err = TFirmwarePackage_HandleCheckResultResult(self, in_err, in_err_info);
    break;
  default:
    err = SSE_E_INVAL;
    break;
  }
  LOG_DEBUG("err=%s", sse_get_error_string(err));
  TRACE_LEAVE();
  return err;
}

static void
FirmwarePackage_OnCommandCompleted(TSseUtilShellCommand* self, sse_pointer in_user_data, sse_int in_result)
{
  TFirmwarePackage *package = (TFirmwarePackage *)in_user_data;
  sse_int err;
  sse_char *err_info = NULL;

  TRACE_ENTER();
  if (in_result == 0) {
      err = SSE_E_OK;
  } else {
    err = SSE_E_GENERIC;
    if (package->fState == FWPKG_STATE_EXTRACTING) {
      err_info = "Failed to extract command.";
    } else if (package->fState == FWPKG_STATE_CHECKING) {
      err_info = "Failed to check result command.";
    }
  }
  LOG_DEBUG("Command Completed: result=%d", in_result);
  TFirmwarePackage_HandleCommandResult(package, err, err_info);
  TRACE_LEAVE();
}

static void
FirmwarePackage_OnCommandError(TSseUtilShellCommand* self, sse_pointer in_user_data, sse_int in_error_code, const sse_char* in_message)
{
  TRACE_ENTER();
  LOG_ERROR("Command Error: err=%s", sse_get_error_string(in_error_code));
  TFirmwarePackage_HandleCommandResult((TFirmwarePackage *)in_user_data, in_error_code, (sse_char *)in_message);
  TRACE_LEAVE();
}

sse_int
TFirmwarePackage_InvokeUpdate(TFirmwarePackage *self)
{
  sse_char *path = NULL;
  int result;
  int err;

  TRACE_ENTER();
  path = FirmwarePackage_MakeFullPath(self->fPackageDirPath, FWPKG_UPGRADE_SCRIPT_PATH);
  if (path == NULL) {
    LOG_ERROR("failed to FirmwarePackage_MakeFullPath(%s).", FWPKG_UPGRADE_SCRIPT_PATH);
    return SSE_E_NOMEM;
  }
  self->fState = FWPKG_STATE_UPDATING;
  result = system(path);
  if (result != 0) {
    LOG_ERROR("command failed:[%s]. result=%d", path, result);
    err = SSE_E_GENERIC;
  } else {
    LOG_DEBUG("update command was successful:[%s].", path);
    err = SSE_E_OK;
  }
  sse_free(path);
  TRACE_LEAVE();
  return err;
}

sse_bool
TFirmwarePackage_Verify(TFirmwarePackage *self)
{
  sse_char *path = NULL;
  MoatValue *path_value = NULL;
  sse_bool ok;

  TRACE_ENTER();
  path = FirmwarePackage_MakeFullPath(self->fPackageDirPath, FWPKG_UPGRADE_SCRIPT_PATH);
  if (path == NULL) {
    LOG_ERROR("failed to FirmwarePackage_MakeFullPath(%s).", FWPKG_UPGRADE_SCRIPT_PATH);
    goto error_exit;
  }
  path_value = moat_value_new_string(path, 0, sse_true);
  if (path_value == NULL) {
    LOG_ERROR("failed to moat_value_new_string(%s).", path);
    goto error_exit;
  }
  ok = SseUtilFile_IsFile(path_value);
  if (!ok) {
    LOG_ERROR("file [%s] is not exists or is not a file.", path);
    goto error_exit;
  }
  moat_value_free(path_value);
  sse_free(path);
  path = NULL;
  path_value = NULL;
  path = FirmwarePackage_MakeFullPath(self->fPackageDirPath, FWPKG_CHECK_SCRIPT_PATH);
  if (path == NULL) {
    LOG_ERROR("failed to FirmwarePackage_MakeFullPath(%s).", FWPKG_CHECK_SCRIPT_PATH);
    goto error_exit;
  }
  path_value = moat_value_new_string(path, 0, sse_true);
  if (path_value == NULL) {
    LOG_ERROR("failed to moat_value_new_string(%s).", path);
    goto error_exit;
  }
  ok = SseUtilFile_IsFile(path_value);
  if (!ok) {
    LOG_ERROR("file [%s] is not exists or is not a file.", path);
    goto error_exit;
  }
  moat_value_free(path_value);
  sse_free(path);
  TRACE_LEAVE();
  return sse_true;

error_exit:
  if (path_value != NULL) {
    moat_value_free(path_value);
  }
  if (path != NULL) {
    sse_free(path);
  }
  return sse_false;
}

sse_int
TFirmwarePackage_Extract(TFirmwarePackage *self, FirmwarePackage_CommandCallback in_callback, sse_pointer in_user_data)
{
  MoatValue *dir_path = NULL;
#ifndef USE_SYSTEM_TO_EXTRACT
  TSseUtilShellCommand *command = NULL;
#else /* USE_SYSTEM_TO_EXTRACT */
  const char * command = "unzip " FWPKG_FILE_NAME " -d ./" FWPKG_DIR_NAME "/";
  int result;
#endif /* USE_SYSTEM_TO_EXTRACT */
  sse_int err;

  TRACE_ENTER();
  if (self->fCurrentCommand != NULL) {
    LOG_ERROR("current command is not nil.");
    return SSE_E_INVAL;
  }
  dir_path = moat_value_new_string(self->fPackageDirPath, 0, sse_true);
  if (dir_path == NULL) {
    LOG_ERROR("failed to create moat_value for dir_path.");
    err = SSE_E_NOMEM;
    goto error_exit;
  }
  FirmwarePackage_RemoveDir(self->fPackageDirPath);
  err = SseUtilFile_MakeDirectory(dir_path);
  if (err != SSE_E_OK) {
    LOG_ERROR("failed to SseUtilFile_MakeDirectory(). path=[%s], err=%d", self->fPackageDirPath, err);
    goto error_exit;
  }

#ifndef USE_SYSTEM_TO_EXTRACT
  command = SseUtilShellCommand_New();
  if (command == NULL) {
    LOG_ERROR("failed to SseUtilShellCommand_New().");
    return SSE_E_NOMEM;
  }
  TSseUtilShellCommand_SetOnComplatedCallback(command, FirmwarePackage_OnCommandCompleted, self);
  TSseUtilShellCommand_SetOnErrorCallback(command, FirmwarePackage_OnCommandError, self);
  err = TSseUtilShellCommand_SetShellCommand(command, "unzip");
  if (err != SSE_E_OK) {
    LOG_ERROR("failed to TSseUtilShellCommand_SetShellCommand(). err=%d", err);
    goto error_exit;
  }
  err = TSseUtilShellCommand_AddArgument(command, FWPKG_FILE_NAME);
  if (err != SSE_E_OK) {
    LOG_ERROR("failed to TSseUtilShellCommand_AddArgument(). err=%d", err);
    goto error_exit;
  }
  err = TSseUtilShellCommand_AddArgument(command, "-d");
  if (err != SSE_E_OK) {
    LOG_ERROR("failed to TSseUtilShellCommand_AddArgument(). err=%d", err);
    goto error_exit;
  }
  err = TSseUtilShellCommand_AddArgument(command, "./" FWPKG_DIR_NAME "/");
  if (err != SSE_E_OK) {
    LOG_ERROR("failed to TSseUtilShellCommand_AddArgument(). err=%d", err);
    goto error_exit;
  }
  err = TSseUtilShellCommand_Execute(command);
  if (err != SSE_E_OK) {
    LOG_ERROR("failed to TSseUtilShellCommand_Execute(). err=%d", err);
    goto error_exit;
  }
  self->fCurrentCommand = command;
  self->fCommandCallback = in_callback;
  self->fCommandUserData = in_user_data;
  self->fState = FWPKG_STATE_EXTRACTING;
#else /* USE_SYSTEM_TO_EXTRACT */
  result = system(command);

  LOG_DEBUG("command:[%s]. result=%d", command, result);
  self->fCommandCallback = in_callback;
  self->fCommandUserData = in_user_data;
  self->fState = FWPKG_STATE_EXTRACTING;
  FirmwarePackage_OnCommandCompleted(NULL, self, result);
#endif /* USE_SYSTEM_TO_EXTRACT */
  moat_value_free(dir_path);
  TRACE_LEAVE();
  return SSE_E_OK;

error_exit:
  if (dir_path != NULL) {
    moat_value_free(dir_path);
  }
#ifndef USE_SYSTEM_TO_EXTRACT
  if (command != NULL) {
    TSseUtilShellCommand_Delete(command);
  }
#endif /* USE_SYSTEM_TO_EXTRACT */
  return err;
}

sse_int
TFirmwarePackage_CheckResult(TFirmwarePackage *self, FirmwarePackage_CommandCallback in_callback, sse_pointer in_user_data)
{
  sse_char *cmd_path = NULL;
  TSseUtilShellCommand *command = NULL;
  sse_int err;

  TRACE_ENTER();
  if (self->fCurrentCommand != NULL) {
    LOG_ERROR("current command is not nil.");
    return SSE_E_INVAL;
  }
  cmd_path = FirmwarePackage_MakeFullPath(self->fPackageDirPath, FWPKG_CHECK_SCRIPT_PATH);
  if (cmd_path == NULL) {
    LOG_ERROR("failed to FirmwarePackage_MakeFullPath().");
    return SSE_E_NOMEM;
  }
  command = SseUtilShellCommand_New();
  if (command == NULL) {
    LOG_ERROR("failed to SseUtilShellCommand_New().");
    return SSE_E_NOMEM;
  }
  TSseUtilShellCommand_SetOnComplatedCallback(command, FirmwarePackage_OnCommandCompleted, self);
  TSseUtilShellCommand_SetOnErrorCallback(command, FirmwarePackage_OnCommandError, self);
  err = TSseUtilShellCommand_SetShellCommand(command, cmd_path);
  if (err != SSE_E_OK) {
    LOG_ERROR("failed to TSseUtilShellCommand_SetShellCommand(). err=%d", err);
    goto error_exit;
  }
  err = TSseUtilShellCommand_Execute(command);
  if (err != SSE_E_OK) {
    LOG_ERROR("failed to TSseUtilShellCommand_Execute(). err=%d", err);
    goto error_exit;
  }
  sse_free(cmd_path);
  self->fCurrentCommand = command;
  self->fCommandCallback = in_callback;
  self->fCommandUserData = in_user_data;
  self->fState = FWPKG_STATE_CHECKING;
  TRACE_LEAVE();
  return SSE_E_OK;

error_exit:
  if (cmd_path != NULL) {
    sse_free(cmd_path);
  }
  return err;
}

void TFirmwarePackage_RemovePackage(TFirmwarePackage *self)
{
  TRACE_ENTER();
  if (self->fPackageDirPath != NULL) {
    FirmwarePackage_RemoveDir(self->fPackageDirPath);
  }
  if (self->fPackageFilePath != NULL) {
    unlink(self->fPackageFilePath);
  }
  TRACE_LEAVE();
}

TFirmwarePackage *
FirmwarePackage_New(void)
{
  TFirmwarePackage *package = NULL;
  sse_char *file_path = NULL;
  sse_char *dir_path = NULL;

  TRACE_ENTER();
  package = sse_zeroalloc(sizeof(TFirmwarePackage));
  file_path = FirmwarePackage_GetPackageFilePath();
  if (file_path == NULL) {
    LOG_ERROR("failed to FirmwarePackage_GetPackageFilePath().");
    goto error_exit;
  }
  dir_path = FirmwarePackage_GetPackageDirPath();
  if (dir_path == NULL) {
    LOG_ERROR("failed to FirmwarePackage_GetPackageDirPath().");
    goto error_exit;
  }
  package->fState = FWPKG_STATE_DORMANT;
  package->fPackageFilePath = file_path;
  package->fPackageDirPath = dir_path;
  TRACE_LEAVE();
  return package;

error_exit:
  if (dir_path != NULL) {
    sse_free(dir_path);
  }
  if (file_path != NULL) {
    sse_free(file_path);
  }
  if (package != NULL) {
    sse_free(package);
  }
  return NULL;
}

void
TFirmwarePackage_Delete(TFirmwarePackage *self)
{
  TRACE_ENTER();
  if (self->fPackageDirPath != NULL) {
    sse_free(self->fPackageDirPath);
  }
  if (self->fPackageFilePath != NULL) {
    sse_free(self->fPackageFilePath);
  }
  sse_free(self);
  TRACE_LEAVE();
}

sse_char *
FirmwarePackage_GetPackageFilePath(void)
{
  TRACE_ENTER();
  TRACE_LEAVE();
  return FirmwarePackage_MakeFullPath(NULL, FWPKG_FILE_NAME);
}

sse_char *
FirmwarePackage_GetPackageDirPath(void)
{
  TRACE_ENTER();
  TRACE_LEAVE();
  return FirmwarePackage_MakeFullPath(NULL, FWPKG_DIR_NAME);
}
