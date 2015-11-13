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
#ifndef __FIRMWARE_PACKAGE__
#define __FIRMWARE_PACKAGE__

SSE_BEGIN_C_DECLS

#include <sseutils.h>

typedef struct TFirmwarePackage_ TFirmwarePackage;

typedef sse_int (*FirmwarePackage_CommandCallback)(TFirmwarePackage *package, sse_int in_err, sse_char *in_err_info, sse_pointer in_user_data);

struct TFirmwarePackage_ {
  sse_int fState;
  sse_char *fPackageFilePath;
  sse_char *fPackageDirPath;
  TSseUtilShellCommand *fCurrentCommand;
  FirmwarePackage_CommandCallback fCommandCallback;
  sse_pointer fCommandUserData;
};

TFirmwarePackage * FirmwarePackage_New(void);
void TFirmwarePackage_Delete(TFirmwarePackage *self);
sse_int TFirmwarePackage_Extract(TFirmwarePackage *self, FirmwarePackage_CommandCallback in_callback, sse_pointer in_user_data);
sse_bool TFirmwarePackage_Verify(TFirmwarePackage *self);
sse_int TFirmwarePackage_InvokeUpdate(TFirmwarePackage *self);
sse_int TFirmwarePackage_CheckResult(TFirmwarePackage *self, FirmwarePackage_CommandCallback in_callback, sse_pointer in_user_data);

sse_char * FirmwarePackage_GetPackageFilePath(void);
sse_char * FirmwarePackage_GetPackageDirPath(void);

SSE_END_C_DECLS

#endif /* __FIRMWARE_PACKAGE__ */
