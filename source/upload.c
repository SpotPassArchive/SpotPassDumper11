#include <upload.h>


#define SERVER_URL "http://bossarchive.raregamingdump.ca/api"

#define UL_CONNECT_TIMEOUT (10 * 1000 * 1000 * 1000LL)
#define UL_BUFSIZE (512 * 1024)
#define UL_CHUNK_TIMEOUT (15 * 1000 * 1000 * 1000LL)

Result upload_connect() {
	Result res = 0xE7E3FFFF;
	Handle connect_event = 0;
	acuConfig config;
	u32 status = -1;

	TR(svcCreateEvent(&connect_event, RESET_ONESHOT), "Failed creating wifi connect event handle");

	TRE(acInit(), "Failed initializing AC", err);
	TRE(ACU_GetStatus(&status), "Failed querying wifi status", err);

	if (status == 3) return 0; // we seem to be connected already

	TRE(ACU_CreateDefaultConfig(&config), "Failed creating wifi default config", err);
	TRE(ACU_SetNetworkArea(&config, 2), "Failed setting wifi network area", err);
	TRE(ACU_SetAllowApType(&config, BIT(0) | BIT(1) | BIT(2)), "Failed setting allowed slot types", err);
	TRE(ACU_SetRequestEulaVersion(&config), "Failed setting request eula version", err);
	TRE(ACU_ConnectAsync(&config, connect_event), "Failed starting wifi connect process", err);

	svcWaitSynchronization(connect_event, UL_CONNECT_TIMEOUT);
	svcCloseHandle(status); // cleanup

	TR(ACU_GetStatus(&status), "Failed querying wifi status");

	return status == 3 ? 0 : UWIFI_CONNECT_FAIL;
err:
	svcCloseHandle(connect_event);
	return res;
}

Result upload_init() {
	// not even sure what a good size would be
	return httpcInit(0x8000);
}

void upload_exit() {
	httpcExit();
}

// use the ctr stats endpoint to check if we are *actually* connected to the internet
Result upload_conn_test() {
	Result res = 0xE7E3FFFF;
	httpcContext ctx;
	u32 code = -1;

	TR(httpcOpenContext(&ctx, HTTPC_METHOD_GET, SERVER_URL "/stats/ctr", 1), "Failed opening http:c context");
	TRE(httpcBeginRequest(&ctx), "Failed begnning HTTP request", err);
	TRE(httpcGetResponseStatusCode(&ctx, &code), "Failed getting HTTP status code", err);
	res = code == 200 ? 0 : UCONNTEST_FAIL;
err:
	httpcCancelConnection(&ctx);
	httpcCloseContext(&ctx);
	return res;
}

Result upload_partition_a(Handle file) {
	u8 *upload_buffer = NULL;
	Result res = 0xE7E3FFFF;
	char contentlength[128];
	httpcContext ctx;
	u64 size = -1;
	u32 _rd = 0;

	TR(FSFILE_GetSize(file, &size), "Failed getting file size");
	snprintf(contentlength, 128, "%ld", (u32)size);

	TR((upload_buffer = (u8 *)malloc(UL_BUFSIZE)) ? 0 : UOOM, "Out of memory"); // this should *not* happen

	TRE(httpcOpenContext(&ctx, HTTPC_METHOD_POST, SERVER_URL "/upload/ctr/partition-a", 1), "Failed opening http:c context", err2);
	TRE(httpcAddRequestHeaderField(&ctx, "content-length", contentlength), "Failed setting content-length", err1);
	TRE(httpcSetPostDataType(&ctx, 2), "Failed setting POST data type", err1);
	TRE(httpcBeginRequest(&ctx), "Failed beginning HTTP request", err1);

	u32 sent = 0, remaining = (u32)size, to_send = MIN((u32)size, UL_BUFSIZE);

	printf("Uploading partitionA.bin... (%.02f%%)", (float)sent / (u32)size * 100.0f);

	while (remaining) {
		TRE(FSFILE_Read(file, &_rd, sent, upload_buffer, to_send), "\nFailed reading file into memory", err2);
		// someone should be able to upload 512KiB within 15 seconds.
		TRE(httpcSendPostDataTimeout(&ctx, to_send, upload_buffer, UL_CHUNK_TIMEOUT), "\nFailed uploading data chunk", err0);
		
		remaining -= to_send;
		sent += to_send;
		to_send = MIN(remaining, 512 * 1024);
		gspWaitForVBlank();
		gfxSwapBuffers();
		printf("\rUploading partitionA.bin... (%.02f%%)", (float)sent / (u32)size * 100.0f);
	}

	printf("\n");

	TRE(httpcNotifyFinishSendPostData(&ctx), "Failed finalizing POST request", err0);
	TRE(httpcGetResponseStatusCode(&ctx, &_rd), "Failed getting HTTP status code", err0);
	TRE(_rd == 200 ? 0 : UPLOAD_FAIL, "Server did not return status code 200", err0);

	httpcCloseContext(&ctx);
	free(upload_buffer);
	return res;
err0:
	httpcCancelConnection(&ctx);
err1:
	httpcCloseContext(&ctx);
err2:
	free(upload_buffer);
	return res;
}

#undef MIN
#undef TR
#undef TRE
#undef ERR_RET
#undef ERR_EXIT
