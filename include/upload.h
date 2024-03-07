#ifndef sd11_upload_h
#define sd11_upload_h

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include <curl/curl.h>
#include <3ds.h>

#include <util.h>

#include "cert_bin.h"

enum {
	UCURL_ERROR     = 1,
	UWIFI_CONN_FAIL = 2,
	UCONNTEST_FAIL  = 3,
	USOC_INIT_FAIL  = 4,
	UL_FAILED       = 5,
};

struct upload_data {
	u64 offset;
	u64 size;
	Result read_res;
	Handle file;
};

#define UL_RES(val) MAKERESULT(RL_PERMANENT,RS_INVALIDSTATE,RM_APPLICATION,val)

Result upload_init();
void upload_exit();

Result upload_connect();
Result upload_conn_test(bool disable_ssl_verify);
Result upload_send_partition_a(Handle file, bool disable_ssl_verify);
CURLcode upload_get_err();

#endif
