/*
 * LEGAL NOTICE
 *
 * Copyright (C) 2012-2015 InventIt Inc. All rights reserved.
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
#include "firmware/firmware_updater.h"

#define TAG	"firmware"
#define TRACE_ENTER() MOAT_LOG_TRACE(TAG, "== enter =>");
#define TRACE_LEAVE() MOAT_LOG_TRACE(TAG, "<= leave ==");
#define LOG_ERROR(format, ...)  MOAT_LOG_ERROR(TAG, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) MOAT_LOG_INFO(TAG, format, ##__VA_ARGS__)
#define LOG_DEBUG(format, ...)  MOAT_LOG_DEBUG(TAG, format, ##__VA_ARGS__)

sse_int
moat_app_main(sse_int argc, sse_char *argv[])
{
  Moat moat;
  TFirmwareUpdater updater;
  sse_int err = SSE_E_OK;

  err = moat_init(argv[0], &moat);
  if (err != SSE_E_OK) {
    LOG_ERROR("failed to moat_init().");
    goto error_exit;
  }
  err = TFirmwareUpdater_Initialize(&updater, moat);
  if (err != SSE_E_OK) {
    LOG_ERROR("failed to TFirmwareUpdater_Initialize().");
    goto error_exit;
  }
  err = TFirmwareUpdater_Start(&updater);
  if (err != SSE_E_OK) {
    LOG_ERROR("failed to TFirmwareUpdater_Start().");
    TFirmwareUpdater_Finalize(&updater);
    goto error_exit;
  }
  moat_run(moat);
  TFirmwareUpdater_Stop(&updater);
  TFirmwareUpdater_Finalize(&updater);

error_exit:
  return err;
}
