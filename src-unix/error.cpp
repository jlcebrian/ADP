#ifdef _UNIX

#include <os_lib.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WEB
#include <emscripten.h>
#include <stdio.h>
#endif

void OSInit()
{
#ifdef _WEB
	EM_ASM(
		FS.mkdir("/saves"); 
		FS.mount(IDBFS, {}, "/saves");
		FS.chdir("/saves");
	);
	EM_ASM(FS.syncfs(true, (err) => {
		if (!err)
			console.log("Filesystem synchronized");
		else
			console.log("Error synchronizing filesystem:", err);
	}));
#endif
}

void OSSyncFS()
{
#ifdef _WEB
	EM_ASM(FS.syncfs(false, (err) => {
		if (!err)
			console.log("Filesystem synchronized");
		else
			console.log("Error synchronizing filesystem:", err);
	}));
#endif
}

void OSError (const char* message)
{
	fputs(message, stderr);
	fputs("\n", stderr);
	exit(0);
}

#endif
