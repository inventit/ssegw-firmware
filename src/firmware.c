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
 * 9F KOJIMACHI CP BUILDING
 * 4-4-7 Kojimachi, Chiyoda-ku, Tokyo 102-0083
 * JAPAN
 * http://www.yourinventit.com/
 */

/*!
 * @file	firmware.c
 * @brief	ファームウェア更新 MOATアプリケーション
 *
 *			サーバからのファームウェア更新要求を受け、ファームウェアをダウンロードし、ファームウェア更新を実行するMOATアプリケーション。
 */

#include <servicesync/moat.h>

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <ev.h>

/*! ファームウェアファイルの一時保存先 */
#define FW_DOWNLOAD_FILE			"/tmp/sse_fota.firm"
/*! リブートまでの待ち時間(秒) */
#define REBOOT_WAIT_SEC				(1)
/*! 非同期結果通知キーのフィールド名(結果保存時に使用) */
#define KEY_FIELD_NAME				"@asyncKey"
/*! ファームウェア更新結果情報のキー */
#define FIRMWARE_UPDATE_STATUS_KEY	"DowonloadInfo"

#if defined(SSE_LOG_USE_SYSLOG)
#define LOG_ERROR(format, ...)				SSE_SYSLOG_ERROR("Firmware", format, ##__VA_ARGS__)
#define LOG_DEBUG(format, ...)				SSE_SYSLOG_DEBUG("Firmware", format, ##__VA_ARGS__)
#else
#define LOG_PRINT(type, tag, format, ...)	printf("[" type "]" tag " %s:%s():L%d " format "\n", __FILE__, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define LOG_ERROR(format, ...)				LOG_PRINT("**ERROR**", "Firmware", format, ##__VA_ARGS__)
#define LOG_DEBUG(format, ...)				LOG_PRINT("DEBUG", "Firmware", format, ##__VA_ARGS__)
#endif

/*! ファームウェア更新コンテキスト */
typedef struct FirmwareContext_ FirmwareContext;
struct FirmwareContext_ {
	Moat Moat;					/**< MOATハンドル(MOAT API呼び出し時に使用) */
	sse_char *Urn;				/**< 自パッケージのURN(結果通知のID作成時に使用) */
	MoatObject *CurrentInfo;	/**< 最新のダウンロード情報 */
	MoatDownloader *Downloader;
};

/**
 * @brief	ファームウェア更新コンテキストクリア
 *　			ファームウェア更新コンテキストの一時データを解放する。
 *
 * @param	in_ctx ファームウェア更新コンテキスト
 */
static void
clear_context(FirmwareContext *in_ctx)
{
	if (in_ctx->Downloader != NULL) {
		moat_downloader_free(in_ctx->Downloader);
		in_ctx->Downloader = NULL;
	}
	if (in_ctx->CurrentInfo != NULL) {
		moat_object_free(in_ctx->CurrentInfo);
		in_ctx->CurrentInfo = NULL;
	}
}

/**
 * @brief	結果通知
 *			ファームウェア更新結果をサーバに通知する。
 *
 * @param	in_err_code エラーコード
 * @param	in_err_info エラー情報文字列
 * @param	in_ctx ファームウェア更新コンテキスト
 * @return	処理結果
 */
static sse_int
notify_result(sse_int in_err_code, sse_char *in_err_info, FirmwareContext *in_ctx)
{
	sse_char *service_id = NULL;
	MoatObject *info;
	sse_char *status;
	sse_char code_str[32];
	sse_char *err_info = NULL;
	sse_char *p;
	sse_char *key = NULL;
	sse_uint len;
	sse_int err;
	sse_int req_id;

	if (in_ctx->CurrentInfo == NULL) {
		return SSE_E_INVAL;
	}
	service_id = moat_create_notification_id_with_moat(in_ctx->Moat, "update-result", "1.0");
	info = in_ctx->CurrentInfo;
	if (in_err_code != SSE_E_OK) {
		status = "ERROR";
		if (in_err_info == NULL) {
			sse_itoa(in_err_code, code_str);
			err_info = code_str;
		} else {
			err_info = in_err_info;
		}
	} else {
		status = "UPDATED";
	}
	err = moat_object_add_string_value(info, "status", status, 0, sse_true,
	        sse_true);
	if (err) {
		goto error_exit;
	}
	if (err_info != NULL) {
		err = moat_object_add_string_value(info, "errorInfo", err_info, 0,
		        sse_true, sse_true);
		if (err) {
			goto error_exit;
		}
	}
	/* for reduce value size : set url value "" */
	err = moat_object_add_string_value(info, "url", "", 0, sse_true, sse_true);
	if (err) {
		goto error_exit;
	}
	err = moat_object_get_string_value(info, KEY_FIELD_NAME, &p, &len);
	if (err) {
		goto error_exit;
	}
	key = sse_strdup(p);
	if (key == NULL) {
		goto error_exit;
	}
	moat_object_remove_value(info, KEY_FIELD_NAME);
	req_id = moat_send_notification(in_ctx->Moat, service_id, key, "DownloadInfo", info, NULL, NULL);
	if (req_id < 0) {
		err = req_id;
	} else {
		err = SSE_E_OK;
	}
	sse_free(key);
	sse_free(service_id);
	clear_context(in_ctx);
	return err;

error_exit:
	clear_context(in_ctx);
	if (key != NULL) {
		sse_free(key);
	}
	if (service_id != NULL) {
		sse_free(service_id);
	}
	return err;
}

/**
 * @brief	ファームウェア検証/更新実行
 *			ダウンロードしたファームウェアファイルを firm コマンドで検証し、OKなら更新を要求する。
 *
 * @param	in_ctx ファームウェア更新コンテキスト
 * @return	処理結果
 */
static sse_int
verify_and_update_request(FirmwareContext *in_ctx)
{
	/* firm -n FirmFile -m {update | check | version} [-d] [-w RebootWaitTime] [-v] [-h] */
	sse_int result;
	sse_char command[1024];

	/* ファームウェアチェック */
	snprintf(command, sizeof(command), "firm -n %s -m check", FW_DOWNLOAD_FILE);
	LOG_DEBUG("exec :[%s]", command);
	result = system(command);
	if (result != 0) {
		LOG_ERROR("failed to check a firmware file. result=[%d]", result);
		return result;
	}
	LOG_DEBUG("exec :[%s]", command);
	/* チェック結果保存 */
	result = moat_datastore_save_object(in_ctx->Moat, FIRMWARE_UPDATE_STATUS_KEY, in_ctx->CurrentInfo);
	if (result) {
		LOG_ERROR("failed to save an object. result=[%d]", result);
		return result;
	}
	/* ファームウェア更新要求 */
	snprintf(command, sizeof(command), "firm -n %s -m update -w %d", FW_DOWNLOAD_FILE, REBOOT_WAIT_SEC);
	result = system(command);
	if (result != 0) {
		LOG_ERROR("failed to update a firmware file. result=[%d]", result);
		moat_datastore_remove_object(in_ctx->Moat, FIRMWARE_UPDATE_STATUS_KEY);
		return result;
	}
	LOG_DEBUG("firm has been updated.");
	return SSE_E_OK;
}

/**
 * @brief	ファームウェアダウンロード完了時処理
 *			ダウンロード成功時は、ファームウェアの検証/更新を行い、失敗時は、エラーをサーバに通知する。
 *
 * @param	in_err ダウンロード結果
 * @param	in_ctx ファームウェア更新コンテキスト
 * @return	処理結果
 */
static sse_int
done_download_firmware(sse_int in_err, FirmwareContext *in_ctx)
{
	sse_int err;

	if (in_err) {
		goto error_exit;
	}
	LOG_DEBUG("firmware file has been downloaded successfully.");
	err = verify_and_update_request(in_ctx);
	if (err) {
		in_err = err;
		goto error_exit;
	}
	return SSE_E_OK;

error_exit:
	/* エラー時は、ファームウェアファイル削除後、サーバに結果通知 */
	unlink(FW_DOWNLOAD_FILE);
	return notify_result(in_err, NULL, in_ctx);
}

static void
on_downloaded(MoatDownloader *in_dl, sse_bool in_canceled, sse_pointer in_user_data)
{
	int result = in_canceled ? SSE_E_INTR : SSE_E_OK;
	int err = done_download_firmware(result, (FirmwareContext *)in_user_data);
	LOG_DEBUG("err=%d", err);
}

static void on_error(MoatDownloader *in_dl, sse_int in_err_code, sse_pointer in_user_data)
{
	int err = done_download_firmware(in_err_code, (FirmwareContext *)in_user_data);
	LOG_DEBUG("err=%d", err);
}

/**
 * @brief	FWダウンロード/FW更新処理開始
 *			FWダウンロードの準備を行い、ダウンロードを開始する。
 *
 * @param	in_moat MOATハンドル
 * @param	in_uid UUID
 * @param	in_key 非同期実行キー
 * @param	in_data コマンドパラメータ
 * @param	in_model_context モデルコンテキスト
 * @param	処理結果
 */
static sse_int
downloadinfo_download_and_update_proc(Moat in_moat, sse_char *in_uid, sse_char *in_key, MoatValue *in_data, sse_pointer in_model_context)
{
	FirmwareContext *ctx = (FirmwareContext *)in_model_context;
	MoatObject *info = ctx->CurrentInfo;
	MoatDownloader *downloader = NULL;
	sse_char *p;
	sse_uint len;
	sse_int err = SSE_E_INVAL;

	if (in_key == NULL || info == NULL) {
		goto error_exit;
	}
	/* ダウンロード情報に非同期キーを追加 */
	err = moat_object_add_string_value(info, KEY_FIELD_NAME, in_key, 0, sse_true, sse_true);
	if (err) {
		goto error_exit;
	}
	downloader = moat_downloader_new();
	if (downloader == NULL) {
		err = SSE_E_NOMEM;
		goto error_exit;
	}
	moat_downloader_set_callbacks(downloader, on_downloaded, on_error, ctx);
	/* ダウンロード情報からURL取得 */
	err = moat_object_get_string_value(info, "url", &p, &len);
	if (err) {
		goto error_exit;
	}
	err = moat_downloader_download(downloader, p, len, FW_DOWNLOAD_FILE);
	if (err) {
		goto error_exit;
	}
	ctx->Downloader = downloader;
	return SSE_E_OK;

error_exit:

	if (downloader != NULL) {
		moat_downloader_free(downloader);
	}
	moat_object_free(info);
	ctx->CurrentInfo = NULL;
	err = done_download_firmware(err, ctx);
	return err;
}


/**
 * @brief	必須フィールドチェック
 *			更新されたDownloadInfoモデルデータに必須フィールドが含まれているかチェックする。
 *
 * @param	in_object DownloadInfoモデルデータ
 * @return	チェック結果
 */
static sse_bool
validate_mandatory_fields(MoatObject *in_object)
{
	sse_char *p;
	sse_uint len;
	sse_int err;

	err = moat_object_get_string_value(in_object, "url", &p, &len);
	if (err) {
		return sse_false;
	}
	return sse_true;
}

/**
 * @brief	ダウンロード情報更新
 *			DownloadInfoモデルデータを更新する。
 *
 * @param	in_ctx ファームウェア更新コンテキスト
 * @param	in_uid UUID(未使用)
 * @param	in_object DownloadInfoモデルデータ
 * @return	チェック結果
 */
static sse_int
update_current_info(FirmwareContext *in_ctx, sse_char *in_uid,
        MoatObject *in_object)
{
	sse_bool is_valid_obj;
	MoatObject *obj = NULL;
	sse_int err;

	if (in_object == NULL || in_ctx->CurrentInfo != NULL) {
		return SSE_E_INVAL;
	}
	is_valid_obj = validate_mandatory_fields(in_object);
	if (!is_valid_obj) {
		return SSE_E_INVAL;
	}
	obj = moat_object_clone(in_object);
	if (obj == NULL) {
		err = SSE_E_NOMEM;
		goto error_exit;
	}
	in_ctx->CurrentInfo = obj;
	return SSE_E_OK;

error_exit:
	if (obj != NULL) {
		moat_object_free(obj);
	}
	return err;
}

/**
 * @brief	DownloadInfoモデルデータ更新処理
 *			DownloadInfoモデルのUpdateメソッドの実装。
 *
 * @param	in_moat MOATハンドル
 * @param	in_uid UUID
 * @param	in_object 更新モデルデータ
 * @param	in_model_context モデルコンテキスト
 * @return	処理結果
 */
static sse_int
DownloadInfo_UpdateProc(Moat in_moat, sse_char *in_uid, MoatObject *in_object,
        sse_pointer in_model_context)
{
	FirmwareContext *ctx = (FirmwareContext *)in_model_context;
	sse_int err;

	err = update_current_info(ctx, in_uid, in_object);
	return err;
}

/**
 * @brief	DownloadInfoモデルデータ部分更新処理
 *			DownloadInfoモデルのUpdateFieldsメソッドの実装。
 *
 * @param	in_moat MOATハンドル
 * @param	in_uid UUID
 * @param	in_object 更新モデルデータ
 * @param	in_model_context モデルコンテキスト
 * @return	処理結果
 */
static sse_int
DownloadInfo_UpdateFieldsProc(Moat in_moat, sse_char *in_uid, MoatObject *in_object, sse_pointer in_model_context)
{
	FirmwareContext *ctx = (FirmwareContext *)in_model_context;
	sse_int err;

	err = update_current_info(ctx, in_uid, in_object);
	return err;
}

/**
 * @brief	ファームウェア更新コマンド処理
 *　			DownloadInfoモデルのdownloadAndUpdateコマンドの実装。非同期コマンド処理を開始する。
 *
 * @param	in_moat MOATハンドル
 * @param	in_uid UUID
 * @param	in_key 非同期実行キー
 * @param	in_data コマンドパラメータ
 * @param	in_model_context モデルコンテキスト
 * @return	正常終了時は、SSE_E_INPROGRESSを返す。SSE_E_INPROGRESS以外はエラー
 */
sse_int
DownloadInfo_downloadAndUpdate(Moat in_moat, sse_char *in_uid, sse_char *in_key, MoatValue *in_data, sse_pointer in_model_context)
{
	sse_int err;
	err = moat_start_async_command(in_moat, in_uid, in_key, in_data, downloadinfo_download_and_update_proc, in_model_context);
	if (err) {
		return err;
	}
	return SSE_E_INPROGRESS;
}

/**
 * @brief	ファームウェア更新アプリエントリポイント
 *　			ファームウェア更新アプリのメイン関数。
 *
 * @param	argc 引数の数
 * @param	引数
 * @return	終了コード
 */
sse_int
moat_app_main(sse_int argc, sse_char *argv[])
{
	Moat moat;
	ModelMapper mapper;
	FirmwareContext *ctx;
	MoatObject *status = NULL;
	sse_int err;

	err = moat_init(argv[0], &moat);
	if (err != SSE_E_OK) {
		LOG_ERROR("failed to initialize.");
		return EXIT_FAILURE;
	}
	/* ファームウェア更新コンテキスト取得 */
	ctx = sse_malloc(sizeof(FirmwareContext));
	if (ctx == NULL) {
		return EXIT_FAILURE;
	}
	sse_memset(ctx, 0, sizeof(FirmwareContext));
	ctx->Moat = moat;
	ctx->Urn = argv[0];
	/* DownloadInfoモデルのMapperメソッド設定、モデル登録 */
	mapper.AddProc = NULL;
	mapper.RemoveProc = NULL;
	mapper.UpdateProc = DownloadInfo_UpdateProc;
	mapper.UpdateFieldsProc = DownloadInfo_UpdateFieldsProc;
	mapper.FindAllUidsProc = NULL;
	mapper.FindByUidProc = NULL;
	mapper.CountProc = NULL;
	LOG_DEBUG("DO register_model [DownloadInfo]");
	err = moat_register_model(moat, "DownloadInfo", &mapper, ctx);
	LOG_DEBUG("DONE register_model err=[%d]", err);
	if (err != SSE_E_OK) {
		LOG_ERROR("failed to register model.");
		return EXIT_FAILURE;
	}
	/* ファームウェア更新結果取得 */
	err = moat_datastore_load_object(moat, FIRMWARE_UPDATE_STATUS_KEY, &status);
	if (err == SSE_E_OK) {
		/* ファームウェア更新結果が存在すれば、結果を削除し、サーバに結果通知 */
		ctx->CurrentInfo = status;
		moat_datastore_remove_object(moat, FIRMWARE_UPDATE_STATUS_KEY);
		err = notify_result(0, NULL, ctx);
	}
	/* イベントループ実行 */
	moat_run(moat);
	moat_unregister_model(moat, "DownloadInfo");
	clear_context(ctx);
	sse_free(ctx);
	moat_destroy(moat);
	return EXIT_SUCCESS;
}
