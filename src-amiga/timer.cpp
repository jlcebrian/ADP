#include <os_types.h>
#include <os_lib.h>


#include <devices/timer.h>
#include <exec/exec.h>
#include <exec/devices.h>
#include <exec/interrupts.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "gcc8_c_support.h"

static bool timerOpened = false;

static timerequest* req;
static MsgPort* port;

void OpenTimer()
{
	if (timerOpened)
		return;
	
		if (SysBase->SoftVer >= 39)
		{
			port = CreateMsgPort();
			if (port == 0)
			{
				DebugPrintf("Error creating keyboard message port\n");
				return;
			}

			req = (timerequest*)CreateIORequest(port, sizeof(timerequest));
			if (req == 0)
			{
				DebugPrintf("Error creating keyboard IO request\n");
				return;
			}
		}
		else
		{
			req = (struct timerequest*)AllocMem(sizeof(timerequest), MEMF_ANY | MEMF_PUBLIC);
			if (req == 0)
			{
				DebugPrintf("Error allocating keyboard IO request\n");
				return;
			}
			memset(req, 0, sizeof(timerequest));
		}
		
	if (OpenDevice("timer.device", UNIT_MICROHZ, (IORequest *)req, 0) != 0)
	{
		DebugPrintf("Error opening timer.device: %ld\n", IoErr());
		Delay(50);
		Exit(0);
	}

}

uint32_t GetMilliseconds()
{
	if (timerOpened == false)
		OpenTimer();
	
	req->tr_node.io_Command = TR_GETSYSTIME;
	DoIO(&req->tr_node);
	
	return req->tr_time.tv_secs * 1000 + (req->tr_time.tv_micro / 1000);
}

void CloseTimer()
{
	if (timerOpened == false)
		return;
		
	CloseDevice((IORequest *)&req);

	if (SysBase->SoftVer >= 39)
	{
		DeleteIORequest(req);
		DeleteMsgPort(port);
	}
	else
	{
		FreeMem(req, sizeof(timerequest));
	}

	timerOpened = false;
}