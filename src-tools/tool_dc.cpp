#include <dc.h>

#include <stdio.h>

static void ShowHelp()
{
	printf("DC DAAD Compiler " VERSION_STR "\n\n");
	printf("Compiles .SCE files to .DDB databases.\n\n");
	printf("Usage: dc <input.sce> [<output.ddb>]\n");
}

int main (int argc, char *argv[])
{
	if (argc < 2)
	{
		ShowHelp();
		return 0;
	}

	DC_CompilerOptions opts;
	opts.version = 2;
	opts.target = DDB_MACHINE_IBMPC;
	opts.language = DDB_SPANISH;
	
	DC_Program* program = DC_Compile(argv[1], &opts);
	if (program == 0)
	{
		fprintf(stderr, "%s: %s\n", argv[1], DC_GetErrorString());
		return 1;
	}
	return 0;
}