#include <httpc_extension.h>

Result httpcSetPostDataType(httpcContext *ctx, u8 type) {
	u32 *cmdbuf = getThreadCommandBuffer();
	cmdbuf[0] = IPC_MakeHeader(0x15, 2, 0);
	cmdbuf[1] = ctx->httphandle;
	cmdbuf[2] = type;

	Result res = svcSendSyncRequest(ctx->servhandle);
	if (R_FAILED(res)) return res;

	return cmdbuf[1];
}

Result httpcNotifyFinishSendPostData(httpcContext *ctx) {
	u32 *cmdbuf = getThreadCommandBuffer();
	cmdbuf[0] = IPC_MakeHeader(0x1D, 1, 0);
	cmdbuf[1] = ctx->httphandle;

	Result res = svcSendSyncRequest(ctx->servhandle);
	if (R_FAILED(res)) return res;

	return cmdbuf[1];
}

Result httpcSendPostDataTimeout(httpcContext *ctx, u32 bufsize, void *buf, u64 timeout) {
	u32 *cmdbuf = getThreadCommandBuffer();

	cmdbuf[0] = IPC_MakeHeader(0x001B, 4, 2);
	cmdbuf[1] = ctx->httphandle;
	cmdbuf[2] = bufsize;
	cmdbuf[3] = (u32)(timeout & 0xFFFFFFFF);
	cmdbuf[4] = (u32)((timeout >> 32) & 0xFFFFFFFF);
	cmdbuf[5] = IPC_Desc_Buffer(bufsize, IPC_BUFFER_R);
	cmdbuf[6] = (u32)buf;

	Result res = svcSendSyncRequest(ctx->servhandle);
	if (R_FAILED(res)) return res;

	return cmdbuf[1];
}
