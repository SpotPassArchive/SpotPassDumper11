#include <upload.h>

#define SOC_BUFSIZE 0x30000

#define SERVER_URL "https://bossarchive.raregamingdump.ca/api"

#define UL_CONNECT_TIMEOUT (10 * 1000 * 1000 * 1000LL)
#define UL_BUFSIZE (512 * 1024)
#define UL_CHUNK_TIMEOUT (15 * 1000 * 1000 * 1000LL)

static CURLcode curl_res = 0;
static u32 *soc_buf = NULL;
static bool soc_initialized = false;
static bool curl_initialized = false;

Result upload_init() {
	Result res = 0xE7E3FFFF;
	if (R_FAILED((soc_buf = (u32 *)memalign(0x1000, SOC_BUFSIZE)) ? 0 : UL_RES(RD_OUT_OF_MEMORY)))
		return res;
	if (R_FAILED(res = socInit(soc_buf, SOC_BUFSIZE)))
		goto exit0;
	soc_initialized = true;
	if (R_FAILED(((curl_res = curl_global_init(CURL_GLOBAL_ALL)) == CURLE_OK) ? 0 : UL_RES(UCURL_INIT_FAIL)))
		goto exit1;
	curl_initialized = true;
	return 0;
exit1:
	socExit();
exit0:
	free(soc_buf);
	return res;
}

void upload_exit() {
	if (curl_initialized) curl_global_cleanup();
	if (soc_initialized) socExit();
	if (soc_buf) { free(soc_buf); soc_buf = NULL; }
}
Result upload_connect() {
	Result res = 0xE7E3FFFF;
	Handle connect_event = 0;
	acuConfig config;
	u32 status = -1;

	TR(svcCreateEvent(&connect_event, RESET_ONESHOT), "Failed creating wifi connect event handle");

	TRE(acInit(), "Failed initializing AC", err);
	TRE(ACU_GetStatus(&status), "Failed querying wifi status", err);

	if (status == 3) return 0; /* we seem to be connected already */

	TRE(ACU_CreateDefaultConfig(&config), "Failed creating wifi default config", err);
	TRE(ACU_SetNetworkArea(&config, 2), "Failed setting wifi network area", err);
	TRE(ACU_SetAllowApType(&config, BIT(0) | BIT(1) | BIT(2)), "Failed setting allowed slot types", err);
	TRE(ACU_SetRequestEulaVersion(&config), "Failed setting request eula version", err);
	TRE(ACU_ConnectAsync(&config, connect_event), "Failed starting wifi connect process", err);

	svcWaitSynchronization(connect_event, UL_CONNECT_TIMEOUT);
	svcCloseHandle(status); /* cleanup */

	TR(ACU_GetStatus(&status), "Failed querying wifi status");

	return status == 3 ? 0 : UL_RES(UWIFI_CONN_FAIL);
err:
	svcCloseHandle(connect_event);
	return res;
}

CURLcode upload_get_err() { return curl_res; }

size_t dummy_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
	return nmemb * size;
}

size_t dl_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
	struct download_data *dldata = (struct download_data *)userdata;
	size_t siz = size * nmemb;

	if (dldata->offset == dldata->size - 1) return nmemb;

	memcpy(&dldata->buffer[dldata->offset], ptr, siz);
	dldata->offset += siz;

	return siz;
}

Result upload_conn_test(bool disable_ssl_verify) {
	CURL *c = curl_easy_init();
	if (!c) return UL_RES(UCURL_INIT_FAIL);

	struct curl_blob ca;
	ca.len = (size_t)cert_bin_size;
	ca.data = (void *)cert_bin;

	curl_easy_setopt(c, CURLOPT_HTTPGET, 1);
	if (disable_ssl_verify)
		curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0);
	else
		curl_easy_setopt(c, CURLOPT_CAINFO_BLOB, &ca);
	curl_easy_setopt(c, CURLOPT_URL, SERVER_URL "/stats/ctr");
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, dummy_cb);

	/* should get the error with upload_get_err in case of ssl failures */
	if ((curl_res = curl_easy_perform(c)) != CURLE_OK)	
		return UL_RES(UCURL_ERROR);

	long statuscode;

	if ((curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &statuscode)) != CURLE_OK)
		return UL_RES(UCURL_ERROR);

	curl_easy_cleanup(c);

	return statuscode == 200 ? 0 : UL_RES(UCONNTEST_FAIL);
}

size_t upload_chunk_cb(char *buffer, size_t size, size_t nitems, void *userdata) {
	struct upload_data *dt = (struct upload_data *)userdata;
	size_t total_size = size * nitems;
	size_t to_write = MIN(dt->size - dt->offset, total_size);
	if (!to_write) return 0;
	u32 read;
	dt->read_res = FSFILE_Read(dt->file, &read, dt->offset, buffer, to_write);
	if (R_FAILED(dt->read_res))
		return CURL_READFUNC_ABORT;
	dt->offset += read;
	return read;
}

int prog_cb(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
	bool *fin_prog = (bool *)clientp;
	if (!ultotal) return 0;
	gspWaitForVBlank();
	gfxSwapBuffers();
	if (!*fin_prog) printf("\rUploading... (" CONSOLE_YELLOW "%.02f%%" CONSOLE_RESET ")", PERCENTAGE(ulnow, ultotal));
	if (ultotal == ulnow && !*fin_prog) {
		putchar('\n');
		*fin_prog = true;
	}
	return 0;
}

Result upload_send_partition_a(Handle file, bool disable_ssl_verify) {
	struct upload_data data;
	struct download_data dldata;
	Result res = 0xE7E3FFFF;

	dldata.size = UL_BUFSIZE + 1;
	dldata.offset = 0;
	dldata.buffer = (char *)malloc(UL_BUFSIZE + 1);
	if (!dldata.buffer) return UL_RES(RD_OUT_OF_MEMORY);

	if (R_FAILED(res = FSFILE_GetSize(file, &data.size))) {
		free(dldata.buffer);
		return res;
	}

	data.file = file;
	data.offset = 0;

	CURL *c = curl_easy_init();
	if (!c) {
		free(dldata.buffer);
		return UL_RES(UCURL_INIT_FAIL);
	}

	struct curl_blob ca;
	ca.len = (size_t)cert_bin_size;
	ca.data = (void *)cert_bin;

	bool fin_prog = false;

	curl_easy_setopt(c, CURLOPT_POST, 1);
	curl_easy_setopt(c, CURLOPT_READDATA, &data);
	curl_easy_setopt(c, CURLOPT_READFUNCTION, upload_chunk_cb);
	if (disable_ssl_verify)
		curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0);
	else
		curl_easy_setopt(c, CURLOPT_CAINFO_BLOB, &ca);
	curl_easy_setopt(c, CURLOPT_URL, SERVER_URL "/upload/ctr/partition-a");
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, dl_cb);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &dldata);
	curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)data.size);
	curl_easy_setopt(c, CURLOPT_BUFFERSIZE, UL_BUFSIZE);
	curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0);
	curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, prog_cb);
	curl_easy_setopt(c, CURLOPT_XFERINFODATA, &fin_prog);
	
	curl_res = curl_easy_perform(c);
	putchar('\n');

	if (curl_res != CURLE_OK) {
		free(dldata.buffer);
		return R_FAILED(data.read_res) ? data.read_res : UL_RES(UCURL_ERROR);
	}

	long statuscode;

	if ((curl_res = curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &statuscode)) != CURLE_OK) {
		free(dldata.buffer);
		return UL_RES(UCURL_ERROR);
	}

	dldata.buffer[dldata.offset] = 0;

	if (statuscode != 200)
		printf("Server response (" CONSOLE_MAGENTA "%ld" CONSOLE_RESET "):\n\n" CONSOLE_YELLOW "%s" CONSOLE_RESET "\n", statuscode, dldata.buffer);

	free(dldata.buffer);
	return statuscode == 200 ? 0 : UL_RES(UL_FAILED);
}

#undef TR
#undef TRE
#undef ERR_RET
#undef ERR_EXIT
