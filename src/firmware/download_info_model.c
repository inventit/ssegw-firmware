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

/* DownloadInfoModel private */
static sse_int
DownloadInfoModel_OnDownloadAndUpdate(Moat in_moat, sse_char *in_uid, sse_char *in_key, MoatValue *in_data, sse_pointer in_model_context)
{
  TDownloadInfoModel *model= (TDownloadInfoModel *)in_model_context;
  sse_int err;

  if (in_key == NULL) {
    MOAT_LOG_ERROR(TAG, "async key is nil.");
    return SSE_E_INVAL;
  }
  if (model->fCurrentInfo == NULL || model->fCommandCallback == NULL) {
    MOAT_LOG_ERROR(TAG, "invalid model state");
    return SSE_E_INVAL;
  }
  err = (*model->fCommandCallback)(model, in_key, model->fCommandUserData);
	return err;
}

static sse_int
TDownloadInfoModel_UpdateCurrent(TDownloadInfoModel *self, sse_char *in_uid, MoatObject *in_object)
{
  MoatObject *obj = NULL;
  sse_char *p;
  sse_uint len;
  sse_int err;

  if (in_object == NULL) {
    return SSE_E_INVAL;
  }
  err = moat_object_get_string_value(in_object, DOWNLOAD_INFO_MODEL_FIELD_URL, &p, &len);
  if (err) {
    return SSE_E_INVAL;
  }

  obj = moat_object_clone(in_object);
  if (obj == NULL) {
    err = SSE_E_NOMEM;
    goto error_exit;
  }
  TDownloadInfoModel_Clear(self);
  self->fCurrentInfo = obj;
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

  err = TDownloadInfoModel_UpdateCurrent(model, in_uid, in_object);
  return err;
}

/* MOAT Command */
sse_int
DownloadInfo_downloadAndUpdate(Moat in_moat, sse_char *in_uid, sse_char *in_key, MoatValue *in_data, sse_pointer in_model_context)
{
  TDownloadInfoModel *model = (TDownloadInfoModel *)in_model_context;
  sse_int err;

  if (model->fCommandCallback == NULL) {
    return SSE_E_INVAL;
  }
  err = moat_start_async_command(in_moat, in_uid, in_key, in_data, DownloadInfoModel_OnDownloadAndUpdate, in_model_context);
  if (err) {
    return err;
  }
  return SSE_E_INPROGRESS;
}

/* DownloadInfoModel public */
void
TDownloadInfoModel_Clear(TDownloadInfoModel *self)
{
  if (self->fCurrentInfo != NULL) {
    moat_object_free(self->fCurrentInfo);
    self->fCurrentInfo = NULL;
  }
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

  if (self->fCurrentInfo == NULL) {
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
  err = moat_object_add_string_value(info, "status", status, 0, sse_true, sse_true);
  if (err) {
    goto error_exit;
  }
  if (err_info != NULL) {
    err = moat_object_add_string_value(info, "errorInfo", err_info, 0, sse_true, sse_true);
    if (err) {
      goto error_exit;
    }
  }
  /* for reduce value size : set url value "" */
  err = moat_object_add_string_value(info, "url", "", 0, sse_true, sse_true);
  if (err) {
    goto error_exit;
  }
  req_id = moat_send_notification(self->fMoat, service_id, in_key, "DownloadInfo", info, NULL, NULL);
  if (req_id < 0) {
    err = req_id;
  } else {
    err = SSE_E_OK;
  }
  sse_free(service_id);
  return err;

error_exit:
  if (service_id != NULL) {
    sse_free(service_id);
  }
  return err;
}

MoatObject *
TDownloadInfoModel_GetModelObject(TDownloadInfoModel *self)
{
  return self->fCurrentInfo;
}

void
TDownloadInfoModel_SetDownloadAndUpdateCommandCallback(TDownloadInfoModel *self, DownloadInfoModel_DownloadAndUpdateCommandCallback in_callback, sse_pointer in_user_data)
{
  self->fCommandCallback = in_callback;
  self->fCommandUserData = in_user_data;
}

sse_int
TDownloadInfoModel_Start(TDownloadInfoModel *self)
{
  ModelMapper mapper;
  sse_int err;

  mapper.AddProc = NULL;
  mapper.RemoveProc = NULL;
  mapper.UpdateProc = DownloadInfoModel_OnUpdate;
  mapper.UpdateFieldsProc = DownloadInfoModel_OnUpdate;
  mapper.FindAllUidsProc = NULL;
  mapper.FindByUidProc = NULL;
  mapper.CountProc = NULL;
  MOAT_LOG_DEBUG(TAG, "register_model %s", DOWNLOAD_INFO_MODEL_NAME);
  err = moat_register_model(self->fMoat, DOWNLOAD_INFO_MODEL_NAME, &mapper, self);
  if (err != SSE_E_OK) {
    MOAT_LOG_ERROR(TAG, "failed to register model. err=%d", err);
    return err;
  }
  MOAT_LOG_DEBUG(TAG, "%s model has been registered.", DOWNLOAD_INFO_MODEL_NAME);
  return SSE_E_OK;
}

void
TDownloadInfoModel_Stop(TDownloadInfoModel *self)
{
  moat_unregister_model(self->fMoat, DOWNLOAD_INFO_MODEL_NAME);
  TDownloadInfoModel_Clear(self);
}

sse_int
TDownloadInfoModel_Initialize(TDownloadInfoModel *self, Moat in_moat)
{
  sse_memset(self, 0, sizeof(TDownloadInfoModel));
  self->fMoat = in_moat;
  return SSE_E_OK;
}

void
TDownloadInfoModel_Finalize(TDownloadInfoModel *self)
{
  TDownloadInfoModel_Clear(self);
}
