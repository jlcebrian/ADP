#pragma once

#include <exec/io.h>
#include <exec/ports.h>

bool OldExec_CreateIORequest(struct MsgPort** outPort, struct IORequest** outReq, uint32_t size, void (*printError)(const char*), const char* portError, const char* requestError);
void OldExec_DeleteIORequest(struct MsgPort* port, struct IORequest* req, uint32_t size);
void OldExec_BeginIO(struct IORequest* req);
