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
#ifndef __FIRMWARE_UPDATER__
#define __FIRMWARE_UPDATER__

SSE_BEGIN_C_DECLS

typedef struct TFirmwareUpdater_ TFirmwareUpdater;

struct TFirmwareUpdater_ {
  Moat fMoat;
  TDownloadInfoModel fInfo;
  sse_char *fAsyncKey;
  MoatDownloader *fDownloader;
};

sse_int TFirmwareUpdater_Initialize(TFirmwareUpdater *self, Moat in_moat);
void TFirmwareUpdater_Finalize(TFirmwareUpdater *self);
sse_int TFirmwareUpdater_Start(TFirmwareUpdater *self);
void TFirmwareUpdater_Stop(TFirmwareUpdater *self);

SSE_END_C_DECLS

#endif /* __FIRMWARE_UPDATER__ */
