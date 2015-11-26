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

#include <servicesync/moat.h>
#include "download_info_model.h"

#define TAG "DownloadInfoModel"

#define TRACE_ENTER() MOAT_LOG_TRACE(TAG, "== enter =>");
#define TRACE_LEAVE() MOAT_LOG_TRACE(TAG, "<= leave ==");
#define LOG_ERROR(format, ...)  MOAT_LOG_ERROR(TAG, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) MOAT_LOG_INFO(TAG, format, ##__VA_ARGS__)
#define LOG_DEBUG(format, ...)  MOAT_LOG_DEBUG(TAG, format, ##__VA_ARGS__)

/* DownloadInfoModel private */

static sse_int
DownloadInfoModel_OnDownloadAndUpdate(Moat in_moat, sse_char *in_uid, sse_char *in_key, MoatValue *in_data, sse_pointer in_model_context)
{
  TDownloadInfoModel *model= (TDownloadInfoModel *)in_model_context;
  sse_int err;

  TRACE_ENTER();
  if (in_key == NULL) {
    LOG_ERROR("async key is nil.");
    return SSE_E_INVAL;
  }
  if (model->fCurrentInfo == NULL || model->fCommandCallback == NULL) {
    LOG_ERROR("invalid model state");
    return SSE_E_INVAL;
  }
  err = (*model->fCommandCallback)(model, in_key, model->fCommandUserData);
  if (err != SSE_E_OK) {
    LOG_ERROR("failed to CommandCallback:%s", sse_get_error_string(err));
    return err;
  }
  TRACE_LEAVE();
	return SSE_E_OK;
}

static sse_int
TDownloadInfoModel_UpdateCurrent(TDownloadInfoModel *self, sse_char *in_uid, MoatObject *in_object)
{
  MoatObject *obj = NULL;
  sse_char *p;
  sse_uint len;
  sse_int err;

  TRACE_ENTER();
  if (in_object == NULL) {
    LOG_ERROR("in_object is nil.");
    return SSE_E_INVAL;
  }
  err = moat_object_get_string_value(in_object, DOWNLOAD_INFO_MODEL_FIELD_URL, &p, &len);
  if (err != SSE_E_OK) {
    LOG_ERROR("failed to moat_object_get_string_value(%s).", DOWNLOAD_INFO_MODEL_FIELD_URL);
    return SSE_E_INVAL;
  }

  obj = moat_object_clone(in_object);
  if (obj == NULL) {
    LOG_ERROR("failed to moat_object_clone().");
    err = SSE_E_NOMEM;
    goto error_exit;
  }
  TDownloadInfoModel_Clear(self);
  self->fCurrentInfo = obj;
  TRACE_LEAVE();
  return SSE_E_OK;

error_exit:
  if (obj != NULL) {
    moat_object_free(obj);
  }
  return err;
}

/* MOAT Mapper impl */
static sse_int
DownloadInfoModel_OnUpdate(Moat in_moat, sse_char *in_uid, MoatObject *in_object, sse_pointer in_model_context)
{
  TDownloadInfoModel *model = (TDownloadInfoModel *)in_model_context;
  sse_int err;

  TRACE_ENTER();
  err = TDownloadInfoModel_UpdateCurrent(model, in_uid, in_object);
  TRACE_LEAVE();
  return err;
}

/* MOAT Command */
sse_int
DownloadInfo_downloadAndUpdate(Moat in_moat, sse_char *in_uid, sse_char *in_key, MoatValue *in_data, sse_pointer in_model_context)
{
  TDownloadInfoModel *model = (TDownloadInfoModel *)in_model_context;
  sse_int err;

  TRACE_ENTER();
  if (model->fCommandCallback == NULL) {
    LOG_ERROR("Command Callback is nil.");
    return SSE_E_INVAL;
  }
  err = moat_start_async_command(in_moat, in_uid, in_key, in_data, DownloadInfoModel_OnDownloadAndUpdate, in_model_context);
  if (err) {
    LOG_ERROR("failed to moat_start_async_command(). err=%s", sse_get_error_string(err));
    return err;
  }
  TRACE_LEAVE();
  return SSE_E_INPROGRESS;
}

/* DownloadInfoModel public */
void
TDownloadInfoModel_Clear(TDownloadInfoModel *self)
{
  TRACE_ENTER();
  if (self->fCurrentInfo != NULL) {
    moat_object_free(self->fCurrentInfo);
    self->fCurrentInfo = NULL;
  }
  TRACE_LEAVE();
}

sse_int
TDownloadInfoModel_NotifyResult(TDownloadInfoModel *self, sse_char *in_key, sse_int in_err_code, sse_char *in_err_info)
{
  sse_char *service_id = NULL;
  MoatObject *info;
  sse_char *status;
  sse_char *err_info = NULL;
  sse_int err;
  sse_int req_id;

  TRACE_ENTER();
  LOG_DEBUG("err=%s, info=%s", sse_get_error_string(in_err_code), (in_err_info == NULL) ? "" : in_err_info);
  if (self->fCurrentInfo == NULL) {
    LOG_ERROR("Current object is nil.");
    return SSE_E_INVAL;
  }
  service_id = moat_create_notification_id_with_moat(self->fMoat, "update-result", "1.0");
  info = self->fCurrentInfo;
  if (in_err_code != SSE_E_OK) {
    status = "ERROR";
    if (in_err_info == NULL) {
      err_info = (sse_char *)sse_get_error_string(in_err_code);
    } else {
      err_info = in_err_info;
    }
  } else {
    status = "UPDATED";
  }
  err = moat_object_add_string_value(info, DOWNLOAD_INFO_MODEL_FIELD_STATUS, status, 0, sse_true, sse_true);
  if (err != SSE_E_OK) {
    LOG_ERROR("failed to moat_object_add_string_value(%s).", DOWNLOAD_INFO_MODEL_FIELD_STATUS);
    goto error_exit;
  }
  if (err_info != NULL) {
    err = moat_object_add_string_value(info, DOWNLOAD_INFO_MODEL_FIELD_ERROR_INFO, err_info, 0, sse_true, sse_true);
    if (err != SSE_E_OK) {
      LOG_ERROR("failed to moat_object_add_string_value(%s).", DOWNLOAD_INFO_MODEL_FIELD_ERROR_INFO);
      goto error_exit;
    }
  }
  /* for reduce value size : set url value "" */
  err = moat_object_add_string_value(info, DOWNLOAD_INFO_MODEL_FIELD_URL, "", 0, sse_true, sse_true);
  if (err != SSE_E_OK) {
    LOG_ERROR("failed to moat_object_add_string_value(%s).", DOWNLOAD_INFO_MODEL_FIELD_URL);
    goto error_exit;
  }
  req_id = moat_send_notification(self->fMoat, service_id, in_key, DOWNLOAD_INFO_MODEL_NAME, info, NULL, NULL);
  if (req_id < 0) {
    err = req_id;
    LOG_ERROR("failed to moat_send_notification(%s). err=%s", DOWNLOAD_INFO_MODEL_NAME, sse_get_error_string(err));
    goto error_exit;
  }
  sse_free(service_id);
  TRACE_LEAVE();
  return SSE_E_OK;

error_exit:
  if (service_id != NULL) {
    sse_free(service_id);
  }
  return err;
}

MoatObject *
TDownloadInfoModel_GetModelObject(TDownloadInfoModel *self)
{
  TRACE_ENTER();
  TRACE_LEAVE();
  return self->fCurrentInfo;
}

sse_int
TDownloadInfoModel_SetModelObject(TDownloadInfoModel *self, MoatObject *in_obj)
{
  MoatObject *obj = NULL;
  sse_char *p;
  sse_uint len;
  sse_int err;

  TRACE_ENTER();
  err = moat_object_get_string_value(in_obj, DOWNLOAD_INFO_MODEL_FIELD_URL, &p, &len);
  if (err != SSE_E_OK) {
    LOG_ERROR("%s is missing.", DOWNLOAD_INFO_MODEL_FIELD_URL);
    return err;
  }
  obj = moat_object_clone(in_obj);
  if (obj == NULL) {
    LOG_ERROR("failed to moat_object_new().");
    return SSE_E_NOMEM;
  }
  if (self->fCurrentInfo != NULL) {
    moat_object_free(self->fCurrentInfo);
  }
  self->fCurrentInfo = obj;
  TRACE_LEAVE();
  return SSE_E_OK;
}

void
TDownloadInfoModel_SetDownloadAndUpdateCommandCallback(TDownloadInfoModel *self, DownloadInfoModel_DownloadAndUpdateCommandCallback in_callback, sse_pointer in_user_data)
{
  TRACE_ENTER();
  self->fCommandCallback = in_callback;
  self->fCommandUserData = in_user_data;
  TRACE_LEAVE();
}

sse_int
TDownloadInfoModel_Start(TDownloadInfoModel *self)
{
  ModelMapper mapper;
  sse_int err;

  TRACE_ENTER();
  mapper.AddProc = NULL;
  mapper.RemoveProc = NULL;
  mapper.UpdateProc = DownloadInfoModel_OnUpdate;
  mapper.UpdateFieldsProc = DownloadInfoModel_OnUpdate;
  mapper.FindAllUidsProc = NULL;
  mapper.FindByUidProc = NULL;
  mapper.CountProc = NULL;
  LOG_DEBUG("register_model %s", DOWNLOAD_INFO_MODEL_NAME);
  err = moat_register_model(self->fMoat, DOWNLOAD_INFO_MODEL_NAME, &mapper, self);
  if (err != SSE_E_OK) {
    LOG_ERROR("failed to register model. err=%d", err);
    return err;
  }
  LOG_DEBUG("%s model has been registered.", DOWNLOAD_INFO_MODEL_NAME);
  TRACE_LEAVE();
  return SSE_E_OK;
}

void
TDownloadInfoModel_Stop(TDownloadInfoModel *self)
{
  TRACE_ENTER();
  moat_unregister_model(self->fMoat, DOWNLOAD_INFO_MODEL_NAME);
  TDownloadInfoModel_Clear(self);
  TRACE_LEAVE();
}

sse_int
TDownloadInfoModel_Initialize(TDownloadInfoModel *self, Moat in_moat)
{
  TRACE_ENTER();
  sse_memset(self, 0, sizeof(TDownloadInfoModel));
  self->fMoat = in_moat;
  TRACE_LEAVE();
  return SSE_E_OK;
}

void
TDownloadInfoModel_Finalize(TDownloadInfoModel *self)
{
  TRACE_ENTER();
  TDownloadInfoModel_Clear(self);
  TRACE_LEAVE();
}
