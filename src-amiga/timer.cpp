#include <os_types.h>
#include <os_lib.h>
#include <devices/timer.h>
#include <exec/devices.h>
#include <exec/interrupts.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "gcc8_c_support.h"

static bool timerOpened = false;

static timerequest req;

void OpenTimer()
{
	if (timerOpened)
		return;
		
	if (OpenDevice("timer.device", UNIT_MICROHZ, (IORequest *)&req, 0) != 0)
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
	
	req.tr_node.io_Command = TR_GETSYSTIME;
	DoIO(&req.tr_node);
	
	return req.tr_time.tv_secs * 1000 + (req.tr_time.tv_micro / 1000);
}

void CloseTimer()
{
	if (timerOpened == false)
		return;
		
	CloseDevice((IORequest *)&req);
	timerOpened = false;
}