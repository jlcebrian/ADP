#include <ddb.h>
#include <ddb_vid.h>
#include <os_mem.h>

#include <stdio.h>
#include <string.h>

void TracePrintf(const char* format, ...)
{
}

static void error(const char* message)
{
	VID_Finish();

	printf("Error: %s\n", message);
	exit(1);
}

int main (int argc, char**argv)
{
	if (!DDB_RunPlayer())
		error(DDB_GetErrorString());
	return 1;
}


