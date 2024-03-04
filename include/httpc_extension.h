#ifndef sd11_httpc_extension_h
#define sd11_httpc_extension_h

#include <3ds.h>

Result httpcSetPostDataType(httpcContext *ctx, u8 type);
Result httpcNotifyFinishSendPostData(httpcContext *ctx);
Result httpcSendPostDataTimeout(httpcContext *ctx, u32 bufsize, void *buf, u64 timeout);

#endif
