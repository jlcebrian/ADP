#include <dc.h>

#include <os_file.h>
#include <os_lib.h>
#include <os_mem.h>

#include <stdio.h>

static const char* kADPCVersion = VERSION_STR;

struct ADPC_ArgList
{
	const char** items;
	size_t count;
	size_t capacity;
};

static bool AddArg(ADPC_ArgList* list, const char* value)
{
	if (list->count == list->capacity)
	{
		size_t newCapacity = list->capacity == 0 ? 8 : list->capacity * 2;
		const char** items = Allocate<const char*>("adpc args", (unsigned)newCapacity, false);
		if (items == 0)
			return false;
		if (list->items != 0 && list->count != 0)
			MemCopy(items, list->items, sizeof(const char*) * list->count);
		if (list->items != 0)
			Free((void*)list->items);
		list->items = items;
		list->capacity = newCapacity;
	}
	list->items[list->count++] = value;
	return true;
}

static void FreeArgList(ADPC_ArgList* list)
{
	if (list->items != 0)
		Free((void*)list->items);
	list->items = 0;
	list->count = 0;
	list->capacity = 0;
}

static void ShowHelp()
{
	printf("ADP Compiler %s\n\n", kADPCVersion);
	printf("Compiles .SCE files to .DDB databases.\n\n");
	printf("Usage: adpc [options] <input.sce> [<output.ddb>]\n\n");
	printf("Defaults: version=v1, target=amiga, language=spanish, charset=auto\n\n");
	printf("Options:\n");
	printf("  --version v1|v2|v3     Target DAAD version (only v1 is implemented today)\n");
	printf("  --target <machine>     ibmpc, spectrum, c64, cpc, msx, atarist, amiga, pcw, plus4, msx2\n");
	printf("  --language <lang>      english or spanish\n");
	printf("  --charset <mode>       auto, utf8, cp437\n");
	printf("  -DNAME[=VALUE]         Predefine a symbol for preprocessing\n");
	printf("  -I <path>              Add include search path\n");
	printf("  --dump-preprocessed    Print preprocessed source to stdout\n");
	printf("  --strict               Treat unsupported directives as errors\n");
}

static bool WriteFile(const char* fileName, const uint8_t* data, size_t size)
{
	File* file = File_Create(fileName);
	if (file == 0)
		return false;
	bool ok = File_Write(file, data, size) == size;
	File_Close(file);
	return ok;
}

static bool ParseVersion(const char* text, DDB_Version* version)
{
	if (StrIComp(text, "v1") == 0 || StrIComp(text, "1") == 0) { *version = DDB_VERSION_1; return true; }
	if (StrIComp(text, "v2") == 0 || StrIComp(text, "2") == 0) { *version = DDB_VERSION_2; return true; }
	if (StrIComp(text, "v3") == 0 || StrIComp(text, "3") == 0) { *version = DDB_VERSION_3; return true; }
	return false;
}

static bool ParseLanguage(const char* text, DDB_Language* language)
{
	if (StrIComp(text, "english") == 0 || StrIComp(text, "en") == 0) { *language = DDB_ENGLISH; return true; }
	if (StrIComp(text, "spanish") == 0 || StrIComp(text, "es") == 0) { *language = DDB_SPANISH; return true; }
	return false;
}

static bool ParseCharset(const char* text, DC_Charset* charset)
{
	if (StrIComp(text, "auto") == 0) { *charset = DCCharset_Auto; return true; }
	if (StrIComp(text, "utf8") == 0 || StrIComp(text, "utf-8") == 0) { *charset = DCCharset_UTF8; return true; }
	if (StrIComp(text, "cp437") == 0 || StrIComp(text, "ibm437") == 0 || StrIComp(text, "dos") == 0) { *charset = DCCharset_CP437; return true; }
	return false;
}

static bool ParseTarget(const char* text, DDB_Machine* machine)
{
	if (StrIComp(text, "ibmpc") == 0 || StrIComp(text, "pc") == 0) { *machine = DDB_MACHINE_IBMPC; return true; }
	if (StrIComp(text, "spectrum") == 0 || StrIComp(text, "zx") == 0) { *machine = DDB_MACHINE_SPECTRUM; return true; }
	if (StrIComp(text, "c64") == 0) { *machine = DDB_MACHINE_C64; return true; }
	if (StrIComp(text, "cpc") == 0) { *machine = DDB_MACHINE_CPC; return true; }
	if (StrIComp(text, "msx") == 0) { *machine = DDB_MACHINE_MSX; return true; }
	if (StrIComp(text, "atarist") == 0 || StrIComp(text, "st") == 0) { *machine = DDB_MACHINE_ATARIST; return true; }
	if (StrIComp(text, "amiga") == 0) { *machine = DDB_MACHINE_AMIGA; return true; }
	if (StrIComp(text, "pcw") == 0) { *machine = DDB_MACHINE_PCW; return true; }
	if (StrIComp(text, "plus4") == 0) { *machine = DDB_MACHINE_PLUS4; return true; }
	if (StrIComp(text, "msx2") == 0) { *machine = DDB_MACHINE_MSX2; return true; }
	return false;
}

static bool StartsWithText(const char* text, const char* prefix)
{
	size_t prefixLen = StrLen(prefix);
	return StrComp(text, prefix, prefixLen) == 0;
}

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		ShowHelp();
		return 0;
	}

	DC_CompilerOptions opts = {};
	opts.version = DDB_VERSION_1;
	opts.target = DDB_MACHINE_AMIGA;
	opts.language = DDB_SPANISH;
	opts.sourceCharset = DCCharset_Auto;

	ADPC_ArgList includePaths = {};
	ADPC_ArgList defines = {};
	const char* inputFileName = 0;
	const char* outputFileName = 0;

	for (int i = 1; i < argc; ++i)
	{
		const char* arg = argv[i];
		if (StrComp(arg, "--help") == 0 || StrComp(arg, "-h") == 0)
		{
			ShowHelp();
			FreeArgList(&includePaths);
			FreeArgList(&defines);
			return 0;
		}
		if (StrComp(arg, "--dump-preprocessed") == 0)
		{
			opts.dumpPreprocessed = true;
			continue;
		}
		if (StrComp(arg, "--strict") == 0)
		{
			opts.strict = true;
			continue;
		}
		if (StrComp(arg, "--version") == 0 && i + 1 < argc)
		{
			if (!ParseVersion(argv[++i], &opts.version))
			{
				fprintf(stderr, "Invalid version: %s\n", argv[i]);
				return 1;
			}
			continue;
		}
		if (StrComp(arg, "--target") == 0 && i + 1 < argc)
		{
			if (!ParseTarget(argv[++i], &opts.target))
			{
				fprintf(stderr, "Invalid target: %s\n", argv[i]);
				return 1;
			}
			continue;
		}
		if (StrComp(arg, "--language") == 0 && i + 1 < argc)
		{
			if (!ParseLanguage(argv[++i], &opts.language))
			{
				fprintf(stderr, "Invalid language: %s\n", argv[i]);
				return 1;
			}
			continue;
		}
		if (StrComp(arg, "--charset") == 0 && i + 1 < argc)
		{
			if (!ParseCharset(argv[++i], &opts.sourceCharset))
			{
				fprintf(stderr, "Invalid charset: %s\n", argv[i]);
				return 1;
			}
			continue;
		}
		if (StrComp(arg, "-I") == 0 && i + 1 < argc)
		{
			AddArg(&includePaths, argv[++i]);
			continue;
		}
		if (StartsWithText(arg, "-D"))
		{
			AddArg(&defines, arg + 2);
			continue;
		}
		if (inputFileName == 0)
		{
			inputFileName = arg;
			continue;
		}
		if (outputFileName == 0)
		{
			outputFileName = arg;
			continue;
		}
		fprintf(stderr, "Unexpected argument: %s\n", arg);
		return 1;
	}

	if (inputFileName == 0)
	{
		ShowHelp();
		return 1;
	}
	if (outputFileName == 0)
		outputFileName = ChangeExtension(inputFileName, ".ddb");

	opts.includePaths = includePaths.items;
	opts.includePathCount = includePaths.count;
	opts.defines = defines.items;
	opts.defineCount = defines.count;

	const DC_Compilation* compilation = DC_Compile(inputFileName, &opts);
	if (compilation == 0)
	{
		const char* details = DC_GetErrorDetails();
		int errorCount = DC_GetErrorCount();
		if (details != 0 && details[0] != 0)
		{
			if (errorCount > 1)
				fprintf(stderr, "Compilation failed with %d errors:\n%s\n", errorCount, details);
			else
				fprintf(stderr, "%s: %s\n", DC_GetErrorString(), details);
		}
		else
			fprintf(stderr, "%s: %s\n", inputFileName, DC_GetErrorString());
		FreeArgList(&includePaths);
		FreeArgList(&defines);
		return 1;
	}

	if (!WriteFile(outputFileName, compilation->data, compilation->size))
	{
		fprintf(stderr, "Unable to write %s\n", outputFileName);
		DC_FreeCompilation(compilation);
		FreeArgList(&includePaths);
		FreeArgList(&defines);
		return 1;
	}

	printf("DDB file written to '%s' (%u bytes)\n", outputFileName, (unsigned)compilation->size);
	DC_FreeCompilation(compilation);
	FreeArgList(&includePaths);
	FreeArgList(&defines);
	return 0;
}
