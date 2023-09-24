#ifdef _DOS

#include <ddb.h>
#include <ddb_vid.h>
#include <os_mem.h>

#include <stdio.h>
#include <direct.h>
#include <string.h>

static void error(const char* message)
{
	VID_Finish();

	printf("Error: %s\n", message);
	exit(1);
}

extern "C" int main (int argc, char**argv)
{
	if (!DDB_RunPlayer())
		error(DDB_GetErrorString());
	return 1;
}

#endif