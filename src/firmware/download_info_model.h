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
#ifndef __DOWNLOAD_INFO_MODEL__
#define __DOWNLOAD_INFO_MODEL__

SSE_BEGIN_C_DECLS

#define DOWNLOAD_INFO_MODEL_NAME  "DownloadInfo"
#define DOWNLOAD_INFO_MODEL_FIELD_URL  "url"
#define DOWNLOAD_INFO_MODEL_FIELD_NAME  "name"
#define DOWNLOAD_INFO_MODEL_FIELD_VERSION  "name"
#define DOWNLOAD_INFO_MODEL_FIELD_STATUS  "status"
#define DOWNLOAD_INFO_MODEL_FIELD_ERROR_INFO  "errorInfo"

typedef struct TDownloadInfoModel_ TDownloadInfoModel;

typedef sse_int (*DownloadInfoModel_DownloadAndUpdateCommandCallback)(TDownloadInfoModel *model, sse_char *in_key, sse_pointer in_user_data);
struct TDownloadInfoModel_ {
  Moat fMoat;
  MoatObject *fCurrentInfo;
  DownloadInfoModel_DownloadAndUpdateCommandCallback fCommandCallback;
  sse_pointer fCommandUserData;
};

sse_int TDownloadInfoModel_Initialize(TDownloadInfoModel *self, Moat in_moat);
void TDownloadInfoModel_Finalize(TDownloadInfoModel *self);
sse_int TDownloadInfoModel_Start(TDownloadInfoModel *self);
void TDownloadInfoModel_Stop(TDownloadInfoModel *self);
void TDownloadInfoModel_SetDownloadAndUpdateCommandCallback(TDownloadInfoModel *self, DownloadInfoModel_DownloadAndUpdateCommandCallback in_callback, sse_pointer in_user_data);
MoatObject * TDownloadInfoModel_GetModelObject(TDownloadInfoModel *self);
sse_int TDownloadInfoModel_NotifyResult(TDownloadInfoModel *self, sse_char *in_key, sse_int in_err_code, sse_char *in_err_info);
void TDownloadInfoModel_Clear(TDownloadInfoModel *self);

SSE_END_C_DECLS

#endif /* __DOWNLOAD_INFO_MODEL__ */
