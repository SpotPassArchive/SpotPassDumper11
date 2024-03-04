#ifndef sd11_upload_h
#define sd11_upload_h

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <httpc_extension.h>
#include <util.h>

enum {
	UWIFI_CONNECT_FAIL = MAKERESULT(RL_PERMANENT, RS_INVALIDSTATE, RM_APPLICATION, 1),
	UCONNTEST_FAIL = MAKERESULT(RL_PERMANENT, RS_INVALIDSTATE, RM_APPLICATION, 2),
	UPLOAD_FAIL = MAKERESULT(RL_PERMANENT, RS_INVALIDSTATE, RM_APPLICATION, 3),
	UOOM = MAKERESULT(RL_PERMANENT, RS_INVALIDSTATE, RM_APPLICATION, 4),
};

Result upload_init();
void upload_exit();

Result upload_connect();
Result upload_conn_test();
Result upload_partition_a(Handle file);

#endif
