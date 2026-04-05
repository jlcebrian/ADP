#include <os_types.h>

#ifdef _AMIGA

#include "old_exec.h"

#include <exec/tasks.h>
#include <inline/macros.h>
#include <proto/exec.h>

bool OldExec_CreateIORequest(struct MsgPort** outPort, struct IORequest** outReq, uint32_t size, void (*printError)(const char*), const char* portError, const char* requestError)
{
	if (outPort == 0 || outReq == 0)
		return false;

	*outPort = 0;
	*outReq = 0;

	MsgPort* port = (MsgPort*)AllocMem(sizeof(MsgPort), MEMF_ANY | MEMF_PUBLIC | MEMF_CLEAR);
	if (port == 0)
	{
		if (printError != 0 && portError != 0)
			printError(portError);
		return false;
	}

	BYTE sig = AllocSignal(-1);
	if (sig == -1)
	{
		FreeMem(port, sizeof(MsgPort));
		if (printError != 0 && portError != 0)
			printError(portError);
		return false;
	}

	port->mp_Node.ln_Type = NT_MSGPORT;
	port->mp_Node.ln_Pri = 0;
	port->mp_Node.ln_Name = 0;
	port->mp_Flags = PA_SIGNAL;
	port->mp_SigBit = sig;
	port->mp_SigTask = FindTask(0);
	port->mp_MsgList.lh_Head = (Node*)&port->mp_MsgList.lh_Tail;
	port->mp_MsgList.lh_Tail = 0;
	port->mp_MsgList.lh_TailPred = (Node*)&port->mp_MsgList.lh_Head;

	IORequest* req = (IORequest*)AllocMem(size, MEMF_ANY | MEMF_PUBLIC | MEMF_CLEAR);
	if (req == 0)
	{
		FreeSignal(sig);
		FreeMem(port, sizeof(MsgPort));
		if (printError != 0 && requestError != 0)
			printError(requestError);
		return false;
	}

	req->io_Message.mn_ReplyPort = port;
	req->io_Message.mn_Length = size;
	req->io_Message.mn_Node.ln_Type = NT_REPLYMSG;
	req->io_Message.mn_Node.ln_Pri = 0;
	req->io_Message.mn_Node.ln_Name = 0;

	*outPort = port;
	*outReq = req;
	return true;
}

void OldExec_DeleteIORequest(struct MsgPort* port, struct IORequest* req, uint32_t size)
{
	if (req != 0)
		FreeMem(req, size);

	if (port != 0)
	{
		if (port->mp_SigBit != (UBYTE)-1)
			FreeSignal((BYTE)port->mp_SigBit);
		FreeMem(port, sizeof(MsgPort));
	}
}

void OldExec_BeginIO(struct IORequest* req)
{
	LP1NR(30, OldExec_BeginIO, struct IORequest*, req, a1, , req->io_Device);
}

#endif
