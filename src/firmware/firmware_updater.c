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
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <servicesync/moat.h>

#include "firmware_updater.h"

#define TAG "FirmwareUpdater"

/* FirmwareUpdater private */

static void
TFirmwareUpdater_Clear(TFirmwareUpdater *self)
{
  if (self->fDownloader != NULL) {
    moat_downloader_free(self->fDownloader);
    self->fDownloader = NULL;
  }
  if (self->fAsyncKey != NULL) {
    sse_free(self->fAsyncKey);
    self->fAsyncKey = NULL;
  }
  if (self->fPackage != NULL) {
    TFirmwarePackage_Delete(self->fPackage);
    self->fPackage = NULL;
  }
}

static sse_int
TFirmwareUpdater_HandleExtractResult(TFirmwareUpdater *self, sse_int in_err, sse_char *in_err_info)
{
  sse_int err = in_err;

  if (self->fAsyncKey == NULL) {
    return SSE_E_INVAL;
  }
  if (in_err != SSE_E_OK) {
    TDownloadInfoModel_NotifyResult(&self->fInfo, self->fAsyncKey, err, in_err_info);
    TFirmwareUpdater_Clear(self);
  } else {
    // TODO!
    TDownloadInfoModel_NotifyResult(&self->fInfo, self->fAsyncKey, err, in_err_info);
    TFirmwareUpdater_Clear(self);
  }
  return err;
}

static sse_int
FirmwareUpdater_OnExtracted(TFirmwarePackage *package, sse_int in_err, sse_char *in_err_info, sse_pointer in_user_data)
{
  int err;
  err = TFirmwareUpdater_HandleExtractResult((TFirmwareUpdater *)in_user_data, in_err, in_err_info);
  MOAT_LOG_DEBUG(TAG, "in_err=%d, in_err_info=%s, err=%d", in_err, in_err_info, err);
  return err;
}

static sse_int
TFirmwareUpdater_ExtractPackage(TFirmwareUpdater *self)
{
  TFirmwarePackage *package = NULL;
  sse_int err;

  package = FirmwarePackage_New();
  if (package == NULL) {
    MOAT_LOG_DEBUG(TAG, "failed to FirmwarePackage_New().");
    return SSE_E_NOMEM;
  }
  err = TFirmwarePackage_Extract(package, FirmwareUpdater_OnExtracted, self);
  if (err != SSE_E_OK) {
    goto error_exit;
  }
  self->fPackage = package;
  return SSE_E_OK;

error_exit:
  if (package != NULL) {
    TFirmwarePackage_Delete(package);
  }
  return err;
}

static sse_int
TFirmwareUpdater_HandleDownloadResult(TFirmwareUpdater *self, sse_int in_err)
{
  sse_int err = in_err;
  sse_char *err_info = "";
  if (self->fAsyncKey == NULL) {
    return SSE_E_INVAL;
  }
  if (self->fDownloader != NULL) {
    moat_downloader_free(self->fDownloader);
    self->fDownloader = NULL;
  }
  if (err != SSE_E_OK) {
    err_info = "Failed to download package";
  } else {
    err = TFirmwareUpdater_ExtractPackage(self);
    if (err != SSE_E_OK) {
      err_info = "Failed to extract package.";
    }
  }
  if (err != SSE_E_OK) {
    TDownloadInfoModel_NotifyResult(&self->fInfo, self->fAsyncKey, err, err_info);
    TFirmwareUpdater_Clear(self);
  }
  return err;
}

static void
FirmwareUpdater_OnDownloaded(MoatDownloader *in_dl, sse_bool in_canceled, sse_pointer in_user_data)
{
  int err;
  int result = in_canceled ? SSE_E_INTR : SSE_E_OK;
  err = TFirmwareUpdater_HandleDownloadResult((TFirmwareUpdater *)in_user_data, result);
  MOAT_LOG_DEBUG(TAG, "err=%d", err);
}

static void
FirmwareUpdater_OnDownloadError(MoatDownloader *in_dl, sse_int in_err_code, sse_pointer in_user_data)
{
  int err;
  err = TFirmwareUpdater_HandleDownloadResult((TFirmwareUpdater *)in_user_data, in_err_code);
  MOAT_LOG_DEBUG(TAG, "err=%d", err);
}

static sse_int
FirmwareUpdater_OnDownloadAndUpdate(TDownloadInfoModel *in_info, sse_char *in_key, sse_pointer in_user_data)
{
  TFirmwareUpdater *updater = (TFirmwareUpdater *)in_user_data;
  MoatObject *info_obj = NULL;
  MoatDownloader *downloader = NULL;
  sse_char *key;
  sse_char *url;
  sse_uint url_len;
  sse_char *file_path = NULL;
  sse_int err = SSE_E_INVAL;

  key = sse_strdup(in_key);
  if (key == NULL) {
    MOAT_LOG_ERROR(TAG, "failed to duplicate key.");
    err = SSE_E_NOMEM;
    goto error_exit;
  }
  info_obj = TDownloadInfoModel_GetModelObject(in_info);
  if (info_obj == NULL) {
    MOAT_LOG_ERROR(TAG, "failed to TDownloadInfoModel_GetModelObject()");
    err = SSE_E_INVAL;
    goto error_exit;
  }
  err = moat_object_get_string_value(info_obj, "url", &url, &url_len);
  if (err) {
    MOAT_LOG_ERROR(TAG, "failed to get url.");
    goto error_exit;
  }
  file_path = FirmwarePackage_GetPackageFilePath();
  if (file_path == NULL) {
    MOAT_LOG_ERROR(TAG, "failed to download path.");
    goto error_exit;
  }
  downloader = moat_downloader_new();
  if (downloader == NULL) {
    err = SSE_E_NOMEM;
    goto error_exit;
  }
  moat_downloader_set_callbacks(downloader, FirmwareUpdater_OnDownloaded, FirmwareUpdater_OnDownloadError, updater);
  unlink(file_path);
  err = moat_downloader_download(downloader, url, url_len, file_path);
  if (err) {
    goto error_exit;
  }
  updater->fAsyncKey = key;
  updater->fDownloader = downloader;
  return SSE_E_OK;

error_exit:
  if (downloader != NULL) {
    moat_downloader_free(downloader);
  }
  if (file_path != NULL) {
    sse_free(file_path);
  }
  TFirmwareUpdater_Clear(updater);
  TDownloadInfoModel_Clear(&updater->fInfo);
  err = TFirmwareUpdater_HandleDownloadResult(updater, err);
  return err;
}

/* FirmwareUpdater public */

sse_int
TFirmwareUpdater_Start(TFirmwareUpdater *self)
{
  sse_int err;
  err = TDownloadInfoModel_Start(&self->fInfo);
  if (err != SSE_E_OK) {
    return err;
  }
  TDownloadInfoModel_SetDownloadAndUpdateCommandCallback(&self->fInfo,
      FirmwareUpdater_OnDownloadAndUpdate, self);
  return SSE_E_OK;
}

void
TFirmwareUpdater_Stop(TFirmwareUpdater *self)
{
  TDownloadInfoModel_SetDownloadAndUpdateCommandCallback(&self->fInfo, NULL, NULL);
  TDownloadInfoModel_Stop(&self->fInfo);
}

sse_int
TFirmwareUpdater_Initialize(TFirmwareUpdater *self, Moat in_moat)
{
  sse_memset(self, 0, sizeof(TFirmwareUpdater));
  self->fMoat = in_moat;
  TDownloadInfoModel_Initialize(&self->fInfo, in_moat);
  return SSE_E_OK;
}

void
TFirmwareUpdater_Finalize(TFirmwareUpdater *self)
{
  TDownloadInfoModel_Finalize(&self->fInfo);
  TFirmwareUpdater_Clear(self);
}
