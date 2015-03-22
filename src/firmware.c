/*
 * LEGAL NOTICE
 *
 * Copyright (C) 2012-2013 InventIt Inc. All rights reserved.
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
	sse_char *StatusFile;		/**< 結果情報ファイル(未使用) */
	MoatHttpClient *Http;			/**< HTTPクライアント */
};

static sse_int handle_io_ready_proc(sse_int in_event_id, sse_pointer in_data, sse_uint in_data_length, sse_pointer in_user_data);

/**
 * @brief	ファームウェア更新コンテキストクリア
 *　			ファームウェア更新コンテキストの一時データを解放する。
 *
 * @param	in_ctx ファームウェア更新コンテキスト
 */
static void
clear_context(FirmwareContext *in_ctx)
{
	if (in_ctx->Http != NULL) {
		moat_httpc_free(in_ctx->Http);
		in_ctx->Http = NULL;
	}
	if (in_ctx->CurrentInfo != NULL) {
		moat_object_free(in_ctx->CurrentInfo);
		in_ctx->CurrentInfo = NULL;
	}
}

/**
 * @brief	通知IDの生成
 *			URNとサービスIDから通知IDを生成する。
 *			通知IDのフォーマット：
 *			"urn:moat:" + ${APP_ID} + ":" + ${PACKAGE_ID} + ":" + ${SERVICE_NAME} + ":" + ${VERSION}
 *
 * @param	in_urn URN (${APP_ID} + ":" + ${PACKAGE_ID})
 * @param	in_service_name サービス名
 * @return	通知ID（呼び出し側で解放すること）
 */
static sse_char *
create_notification_id(sse_char *in_urn, sse_char *in_service_name)
{
	sse_char *prefix = "urn:moat:";
	sse_uint prefix_len;
	sse_char *suffix = ":1.0";
	sse_uint suffix_len;
	sse_uint urn_len;
	sse_uint service_len;
	sse_char *noti_id = NULL;
	sse_char *p;

	prefix_len = sse_strlen(prefix);
	urn_len = sse_strlen(in_urn);
	service_len = sse_strlen(in_service_name);
	suffix_len = sse_strlen(suffix);
	noti_id = sse_malloc(prefix_len + urn_len + 1 + service_len + suffix_len + 1);
	if (noti_id == NULL) {
		return NULL;
	}
	p = noti_id;
	sse_memcpy(p, prefix, prefix_len);
	p += prefix_len;
	sse_memcpy(p, in_urn, urn_len);
	p += urn_len;
	*p = ':';
	p++;
	sse_memcpy(p, in_service_name, service_len);
	p += service_len;
	sse_memcpy(p, suffix, suffix_len);
	p += suffix_len;
	*p = '\0';
	return noti_id;
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
	service_id = create_notification_id(in_ctx->Urn, "update-result");
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
	MoatEventService *es;
	sse_int err;

	es = moat_event_service_get_instance();
	moat_event_service_unsubscribe(es, MOAT_EVENT_IO_READY, handle_io_ready_proc);
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

/**
 * @brief	IO_READYイベントハンドラ
 *			HTTP送受信処理を継続する。送受信完了時は、ダウンロード完了時処理を実行する。
 *
 * @param	in_event_id イベントID
 * @param	in_data イベントデータ
 * @param	in_data_length イベントデータ長
 * @param	in_user_data ユーザデータ
 * @return	処理結果
 */
static sse_int
handle_io_ready_proc(sse_int in_event_id, sse_pointer in_data,
        sse_uint in_data_length, sse_pointer in_user_data)
{
	FirmwareContext *ctx = (FirmwareContext *)in_user_data;
	MoatHttpClient *http = ctx->Http;
	MoatHttpResponse *res = NULL;
	sse_int status_code;
	sse_int state;
	sse_bool complete;
	sse_int err = SSE_E_OK;

	state = moat_httpc_get_state(http);
	if (state == MOAT_HTTP_STATE_CONNECTING || state == MOAT_HTTP_STATE_SENDING) {
		err = moat_httpc_do_send(http, &complete);
		if (err != SSE_E_OK && err != SSE_E_AGAIN) {
			LOG_ERROR("failed to moat_httpc_do_send():err=[%d]", err);
			goto done;
		}
		if (complete) {
			LOG_DEBUG("do recv");
			err = moat_httpc_recv_response(http);
			if (err != SSE_E_OK && err != SSE_E_AGAIN) {
				LOG_ERROR("failed to moat_httpc_recv_response()");
				goto done;
			}
		}
	} else if (state == MOAT_HTTP_STATE_RECEIVING || state == MOAT_HTTP_STATE_RECEIVED) {
		err = moat_httpc_do_recv(http, &complete);
		if (err != SSE_E_OK && err != SSE_E_AGAIN) {
			LOG_ERROR("failed to moat_httpc_do_recv():err=[%d]", err);
			goto done;
		}
		if (complete) {
			res = moat_httpc_get_response(http);
			if (res == NULL) {
				err = SSE_E_GENERIC;
				goto done;
			}
			err = moat_httpres_get_status_code(res, &status_code);
			if (err) {
				LOG_ERROR("failed to moat_httpres_get_status_code() [%d]", err);
				goto done;
			}
			if (status_code != 200) {
				err = status_code;
			}
			goto done;
		}
	} else {
		LOG_ERROR("unhandled http state. state=[%d]", state);
		err = SSE_E_INVAL;
		goto done;
	}
	return SSE_E_OK;

done:
	err = done_download_firmware(err, ctx);
	return SSE_E_OK;
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
	MoatHttpClient *http = NULL;
	MoatHttpRequest *req;
	MoatEventService *es;
	sse_char *p;
	sse_uint len;
	sse_bool opt;
	sse_int err = SSE_E_INVAL;

	if (in_key == NULL || info == NULL) {
		goto error_exit;
	}
	/* ダウンロード情報に非同期キーを追加 */
	err = moat_object_add_string_value(info, KEY_FIELD_NAME, in_key, 0, sse_true, sse_true);
	if (err) {
		goto error_exit;
	}
	/* HTTPクライアント設定 */
	http = moat_httpc_new();
	if (http == NULL) {
		err = SSE_E_NOMEM;
		goto error_exit;
	}
	/* ダウンロードコンテンツの圧縮を許容 */
	opt = sse_true;
	err = moat_httpc_set_option(http, MOAT_HTTP_OPT_ACCEPT_COMPRESSED, &opt, sizeof(sse_bool));
	if (err) {
		goto error_exit;
	}
	/* ダウンロード先のパス設定 */
	err = moat_httpc_set_download_file_path(http, FW_DOWNLOAD_FILE, sse_strlen(FW_DOWNLOAD_FILE));
	if (err) {
		goto error_exit;
	}
	/* ダウンロード情報からURL取得 */
	err = moat_object_get_string_value(info, "url", &p, &len);
	if (err) {
		goto error_exit;
	}
	/* 取得したURLへのGETリクエスト生成 */
	req = moat_httpc_create_request(http, MOAT_HTTP_METHOD_GET, p, len);
	if (req == NULL) {
		err = SSE_E_GENERIC;
		goto error_exit;
	}
	/* GETリクエスト送信 */
	err = moat_httpc_send_request(http, req);
	if (err) {
		goto error_exit;
	}
	/* IO_READYイベント購読開始 */
	es = moat_event_service_get_instance();
	err = moat_event_service_subscribe(es, MOAT_EVENT_IO_READY, handle_io_ready_proc, ctx);
	if (err) {
		goto error_exit;
	}
	ctx->Http = http;
	return SSE_E_OK;

error_exit:

	if (http != NULL) {
		moat_httpc_free(http);
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
