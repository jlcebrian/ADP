#include <ddb_test.h>

#if HAS_TESTMODE

#include <ddb.h>
#include <ddb_vid.h>
#include <os_file.h>
#include <os_lib.h>
#include <os_mem.h>

static const char* input = 0;
static const char* inputBegin = 0;
static const char* inputEnd = 0;
static DDB_ScreenshotCallback screenshotCallback = 0;
static bool inputError = false;
static const char* inputErrorMessage = 0;
static bool interactiveInput = false;
static bool inputStopped = false;
static bool inputBarrier = false;
static int partSelection = 0;
static char partScreenshotFile[FILE_MAX_PATH] = { 0 };
enum WaitKey { WaitKey_None, WaitKey_Explicit, WaitKey_Implicit };
static WaitKey waitKey = WaitKey_None;

static bool IsDirective(const char* directive)
{
	size_t length = StrLen(directive);
	return input != 0 && input + length <= inputEnd &&
		MemComp((void*)input, directive, length) == 0 &&
		(input + length == inputEnd || input[length] == '\r' || input[length] == '\n');
}

static bool IsKeyDirective()
{
	return IsDirective("@key");
}

static void ConsumeLine()
{
	while (input < inputEnd && *input != '\r' && *input != '\n') input++;
	if (input < inputEnd && *input == '\r') input++;
	if (input < inputEnd && *input == '\n') input++;
}

static void ConsumeStopDirective()
{
	ConsumeLine();
	interactiveInput = true;
	inputStopped = true;
}

static bool IsCaptureDirective()
{
	return (input + 9 <= inputEnd && MemComp((void*)input, (void*)"@capture ", 9) == 0) ||
	       (input + 6 <= inputEnd && MemComp((void*)input, (void*)"@save ", 6) == 0);
}

// allowCaptures is false while a finite (animation-driving) PAUSE is running, so a
// pending @capture/@save is deferred until the game reaches its next settled input
// point (command prompt or infinite PAUSE) — the screenshot then shows the final
// frame instead of a mid-animation one.
static void ProcessDirectives(bool allowCaptures)
{
	while (input != 0 && input < inputEnd)
	{
		if (IsDirective("@exit"))
		{
			// End the run cleanly at this input point: take no further input,
			// let the player loop exit via exitGame. Used by headless screenshot
			// tests to bail out after a capture without finishing (or hanging in)
			// the game.
			ConsumeLine();
			input = inputEnd;
			VID_Quit();
			return;
		}
		if (IsDirective("@interactive") || IsDirective("@stop"))
		{
			ConsumeStopDirective();
			return;
		}

		if (!IsCaptureDirective())
			return;
		if (!allowCaptures)
			return;

		const char* prefix =
			(input + 9 <= inputEnd && MemComp((void*)input, (void*)"@capture ", 9) == 0)
				? "@capture " : "@save ";

		const char* name = input + StrLen(prefix);
		const char* end = name;
		while (end < inputEnd && *end != '\r' && *end != '\n') end++;
		char fileName[FILE_MAX_PATH];
		size_t length = (size_t)(end - name);
		if (screenshotCallback == 0 || length == 0 || length >= sizeof(fileName))
		{
			inputError = true;
			inputErrorMessage = "invalid scripted screenshot directive";
			VID_Quit();
			return;
		}
		MemCopy(fileName, name, length);
		fileName[length] = 0;
		input = end;
		ConsumeLine();
		if (!screenshotCallback(fileName))
		{
			inputError = true;
			inputErrorMessage = "scripted screenshot failed";
			VID_Quit();
			return;
		}
		inputBarrier = true;
		return;
	}
}

bool DDB_TestIsActive()
{
	return input != 0;
}

bool DDB_TestGetKey(uint8_t* key, uint8_t* ext, uint8_t* mod)
{
	if (!DDB_TestIsActive())
		return false;
	if (waitKey != WaitKey_None)
	{
		WaitKey keyType = waitKey;
		waitKey = WaitKey_None;
		if (keyType == WaitKey_Explicit)
			ConsumeLine();
		if (key) *key = 0x0D;
		if (ext) *ext = 0;
		if (mod) *mod = 0;
		return true;
	}
	if (input < inputEnd && *input == '\r') input++;
	ProcessDirectives(true);
	if (inputStopped)
	{
		VID_GetKey(key, ext, mod);
		return true;
	}
	if (IsKeyDirective())
	{
		ConsumeLine();
		if (key) *key = 0x0D;
		if (ext) *ext = 0;
		if (mod) *mod = 0;
		return true;
	}
	if (input < inputEnd)
	{
		uint8_t value = *input++;
		if (value == '\n')
		{
			value = 0x0D;
			inputBarrier = true;
		}
		if (key) *key = value;
		if (ext) *ext = 0;
		if (mod) *mod = 0;
		return true;
	}
	if (!interactiveInput)
	{
		if (key) *key = 0;
		if (ext) *ext = 0;
		if (mod) *mod = 0;
		return true;
	}
	VID_GetKey(key, ext, mod);
	return true;
}

bool DDB_TestAnyKey()
{
	if (!DDB_TestIsActive())
		return false;
	if (inputStopped)
		return VID_AnyKey();
	if (inputBarrier)
	{
		inputBarrier = false;
		return false;
	}
	ProcessDirectives(true);
	if (inputBarrier)
	{
		inputBarrier = false;
		return false;
	}
	if (IsKeyDirective() || input < inputEnd)
		return true;
	// Scripted input exhausted: in interactive mode hand off to live input so the
	// player regains keyboard control once the script finishes (matching
	// DDB_TestGetKey and DDB_TestAnyKeyForWait).
	return interactiveInput ? VID_AnyKey() : false;
}

bool DDB_TestAnyKeyForWait(bool allowCaptures)
{
	if (!DDB_TestIsActive())
		return false;
	ProcessDirectives(allowCaptures);
	if (inputStopped)
		return VID_AnyKey();
	if (inputBarrier)
	{
		inputBarrier = false;
		return false;
	}
	if (input >= inputEnd)
		return interactiveInput ? VID_AnyKey() : false;
	// A capture was deferred (finite pause in progress): report no key so the
	// pause runs to its end rather than being consumed as a keypress.
	if (!allowCaptures && IsCaptureDirective())
		return false;
	if (IsKeyDirective() || *input == '\r' || *input == '\n')
	{
		waitKey = WaitKey_Explicit;
		return true;
	}
	waitKey = WaitKey_Implicit;
	return true;
}

bool DDB_TestHasScriptedInput()
{
	return !inputStopped && input != 0 && input < inputEnd;
}

void DDB_TestSetScreenshotCallback(DDB_ScreenshotCallback callback)
{
	screenshotCallback = callback;
	inputError = false;
	inputErrorMessage = 0;
}

void DDB_TestEnableInteractiveInput()
{
	interactiveInput = true;
}

void DDB_TestSetPartSelection(int part, const char* screenshotFileName)
{
	partSelection = part;
	partScreenshotFile[0] = 0;
	if (screenshotFileName != 0)
		StrCopy(partScreenshotFile, sizeof(partScreenshotFile), screenshotFileName);
}

int DDB_TestGetPartSelection()
{
	return partSelection;
}

bool DDB_TestCapturePartSelector()
{
	if (partScreenshotFile[0] == 0)
		return true;
	if (screenshotCallback == 0)
	{
		inputError = true;
		inputErrorMessage = "no screenshot callback for part selector capture";
		return false;
	}
	if (!screenshotCallback(partScreenshotFile))
	{
		inputError = true;
		inputErrorMessage = "part selector screenshot failed";
		return false;
	}
	return true;
}

bool DDB_TestHasError()
{
	return inputError;
}

const char* DDB_TestGetError()
{
	return inputErrorMessage;
}

void DDB_TestLoadInput(const char* fileName)
{
	inputError = false;
	inputErrorMessage = 0;
	interactiveInput = false;
	inputStopped = false;
	inputBarrier = false;
	waitKey = WaitKey_None;
	if (inputBegin != 0) Free((void*)inputBegin);
	input = inputBegin = inputEnd = 0;
	File* file = File_Open(fileName, ReadOnly);
	if (file == 0) return;
	uint64_t fileSize = File_GetSize(file);
	inputBegin = input = (const char*)AllocateBlock("Test input", (size_t)fileSize, false);
	if (inputBegin != 0)
		inputEnd = inputBegin + File_Read(file, (void*)input, fileSize);
	File_Close(file);
	if (inputBegin == 0) input = inputEnd = 0;
}

#endif
