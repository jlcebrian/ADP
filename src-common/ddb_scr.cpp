#include <ddb_scr.h>
#include <ddb_vid.h>
#include <os_mem.h>
#include <os_file.h>

#if !defined(NO_BUFFERING)

static bool smoothScrolling = true;

#define COMMAND_BUFFER_BLOCKS

#define MAX_BLOCKS 		   256
#define COMMANDS_PER_BLOCK 256

SCR_CommandData  firstCommandBlock[COMMANDS_PER_BLOCK];
SCR_CommandData	*commandBufferBlocks[MAX_BLOCKS] = { firstCommandBlock };
size_t			 commandBufferIndex = 0;
size_t			 commandBufferReadIndex = 0;

const char*      inputFile = 0;
const char*      inputFileBegin = 0;
const char*      inputFileEnd = 0;

static inline int IndexBlock (size_t index)
{
	return index / COMMANDS_PER_BLOCK;
}
static inline int IndexOffset (size_t index)
{
	return index % COMMANDS_PER_BLOCK;
}

static void SCR_ConsumeFullBuffer()
{
	while(commandBufferReadIndex != commandBufferIndex)
	{
		SCR_ConsumeBuffer();
		if (waitingForKey)
		{
			VID_WaitForKey();
			waitingForKey = false;
		}
	}
	commandBufferReadIndex = 0;
	commandBufferIndex = 0;
}

static SCR_CommandData* SCR_AddCommandToBuffer()
{
	if(commandBufferIndex == commandBufferReadIndex)
	{
		commandBufferReadIndex = 0;
		commandBufferIndex = 0;
	}

	int block = IndexBlock(commandBufferIndex);
	if (commandBufferBlocks[block] == 0)
	{
		DebugPrintf("Allocating command buffer block %ld\n", (long)block);
		commandBufferBlocks[block] = Allocate<SCR_CommandData>("Command buffer", COMMANDS_PER_BLOCK);
		if (commandBufferBlocks[block] == 0)
		{
			// PANIC! Unable to allocate command
			SCR_ConsumeFullBuffer();
			block = IndexBlock(commandBufferIndex);
			if (commandBufferBlocks[block] == 0)
			{
				// This shouldn't happen
				return 0;}
		}
	}

	int offset = IndexOffset(commandBufferIndex++);
	return &commandBufferBlocks[block][offset];
}

void SCR_WaitForKey()
{
	if (buffering)
	{
		SCR_CommandData* c = SCR_AddCommandToBuffer();
		c->type = SCR_COMMAND_WAITFORKEY;
	}
	else
	{
		waitingForKey = true;
		buffering = true;
	}
}

void SCR_DrawCharacter(int x, int y, uint8_t ch, uint8_t ink, uint8_t paper)
{
	if (buffering)
	{
		SCR_CommandData* c = SCR_AddCommandToBuffer();
		c->type = SCR_COMMAND_DRAWCHARACTER;
		c->x = x;
		c->y = y;
		c->n = ch;
		c->ink = ink;
		c->paper = paper;
	}
	else
	{
		VID_DrawCharacter(x, y, ch, ink, paper);
	}
}

bool SCR_SampleExists(uint8_t sample)
{
	return VID_SampleExists(sample);
}

bool SCR_LoadPicture(uint8_t picno, DDB_ScreenMode screenMode)
{
	if (buffering)
	{
		SCR_CommandData* c = SCR_AddCommandToBuffer();
		c->type = SCR_COMMAND_LOADPICTURE;
		c->n = picno;
		c->x = screenMode;
	}
	else
	{
		VID_LoadPicture(picno, screenMode);
	}

	return VID_PictureExists(picno);
}

void SCR_DisplayPicture(int x, int y, int w, int h, DDB_ScreenMode mode)
{
	if (buffering)
	{
		SCR_CommandData* c = SCR_AddCommandToBuffer();
		c->type = SCR_COMMAND_DISPLAYPICTURE;
		c->x = x;
		c->y = y;
		c->w = w;
		c->h = h;
		c->n = mode;
	}
	else
	{
		VID_DisplayPicture(x, y, w, h, mode);
	}
}

bool SCR_PictureExists(uint8_t picno)
{
	return VID_PictureExists(picno);
}

void SCR_GetPictureInfo(bool* fixed, int16_t* x, int16_t* y, int16_t* w, int16_t* h)
{
	VID_GetPictureInfo(fixed, x, y, w, h);
}

void SCR_Clear(int x, int y, int w, int h, uint8_t color)
{
	if (buffering)
	{
		SCR_CommandData* c = SCR_AddCommandToBuffer();
		c->type = SCR_COMMAND_CLEAR;
		c->x = x;
		c->y = y;
		c->w = w;
		c->h = h;
		c->n = color;
	}
	else
	{
		VID_Clear(x, y, w, h, color);
	}
}

void SCR_Scroll(int x, int y, int w, int h, int lines, uint8_t paper, bool smooth)
{
	if(smoothScrolling && smooth)
		buffering = true;
	if (buffering)
	{
		SCR_CommandData* c = SCR_AddCommandToBuffer();
		c->type = SCR_COMMAND_SCROLL;
		c->x = x;
		c->y = y;
		c->w = w;
		c->h = h;
		c->n = lines;
		c->paper = paper;
	}
	else
	{
		VID_Scroll(x, y, w, h, lines, paper);
	}
}

void SCR_ConsumeBuffer()
{
	bool scrollPerformed = false;

	while(commandBufferReadIndex != commandBufferIndex)
	{
		int block = IndexBlock(commandBufferReadIndex);
		int offset = IndexOffset(commandBufferReadIndex);
		commandBufferReadIndex++;

		SCR_CommandData* c = &commandBufferBlocks[block][offset];
		switch(c->type)
		{
			case SCR_COMMAND_WAITFORKEY:
				waitingForKey = true;
				return;
			case SCR_COMMAND_DRAWCHARACTER:
				VID_DrawCharacter(c->x, c->y, c->n, c->ink, c->paper);
				break;
			case SCR_COMMAND_LOADPICTURE:
				VID_LoadPicture(c->n,(DDB_ScreenMode)c->x);
				break;
			case SCR_COMMAND_DISPLAYPICTURE:
				VID_DisplayPicture(c->x, c->y, c->w, c->h,(DDB_ScreenMode)c->n);
				break;
			case SCR_COMMAND_CLEAR:
				VID_Clear(c->x, c->y, c->w, c->h, c->n);
				break;
			case SCR_COMMAND_SCROLL:
				if(scrollPerformed && smoothScrolling) {
					commandBufferReadIndex--;
					return;
				}
				VID_Scroll(c->x, c->y, c->w, c->h, c->n, c->paper);
				scrollPerformed = true;
				break;
			case SCR_COMMAND_SAVE:
				VID_SaveScreen();
				break;
			case SCR_COMMAND_RESTORE:
				VID_RestoreScreen();
				break;
			case SCR_COMMAND_SWAP:
				VID_SwapScreen();
				break;
			case SCR_COMMAND_SETOPBUFFER:
				VID_SetOpBuffer((SCR_Operation)c->n, c->x);
				break;
			case SCR_COMMAND_CLEARBUFFER:
				VID_ClearBuffer(c->x);
				break;
		}
	}
	buffering = false;
}

bool SCR_Synchronized ()
{
	return !buffering && !waitingForKey;
}

void SCR_GetKey(uint8_t* key, uint8_t* ext, uint8_t* mod)
{
    if (inputFile && inputFile < inputFileEnd && *inputFile == '\r')
        inputFile++;
    if (inputFile && inputFile < inputFileEnd)
    {
        if (key) {
            *key = *inputFile++;
            if (*key == '\n') *key = 0x0D;
        }
        if (ext)
            *ext = 0;
        if (mod)
            *mod = 0;
        return;
    }

	VID_GetKey(key, ext, mod);
}

bool SCR_AnyKey()
{
    if (inputFile && inputFile < inputFileEnd)
        return true;

	return VID_AnyKey();
}

void SCR_GetPaletteColor(uint8_t color, uint8_t* r, uint8_t* g, uint8_t* b)
{
	// Should this be buffered ???
	VID_GetPaletteColor(color, r, g, b);
}

void SCR_SetPaletteColor(uint8_t color, uint8_t r, uint8_t g, uint8_t b)
{
	// Should this be buffered ???
	VID_SetPaletteColor(color, r, g, b);
}

void SCR_SaveScreen()
{
	if (buffering)
	{
		SCR_CommandData* c = SCR_AddCommandToBuffer();
		c->type = SCR_COMMAND_SAVE;
	}
	else
	{
		VID_SaveScreen();
	}
}

void SCR_RestoreScreen()
{
	if (buffering)
	{
		SCR_CommandData* c = SCR_AddCommandToBuffer();
		c->type = SCR_COMMAND_RESTORE;
	}
	else
	{
		VID_RestoreScreen();
	}
}

void SCR_SwapScreen()
{
	if (buffering)
	{
		SCR_CommandData* c = SCR_AddCommandToBuffer();
		c->type = SCR_COMMAND_SWAP;
	}
	else
	{
		VID_SwapScreen();
	}
}

void SCR_SetOpBuffer(SCR_Operation op, bool front)
{
	if (buffering)
	{
		SCR_CommandData* c = SCR_AddCommandToBuffer();
		c->type = SCR_COMMAND_SETOPBUFFER;
		c->n = op;
		c->x = front;
	}
	else
	{
		VID_SetOpBuffer(op, front);
	}
}

void SCR_ClearBuffer(bool front)
{
	if (buffering)
	{
		SCR_CommandData* c = SCR_AddCommandToBuffer();
		c->type = SCR_COMMAND_CLEARBUFFER;
		c->x = front;
	}
	else
	{
		VID_ClearBuffer(front);
	}
}

void SCR_PlaySample(uint8_t sample, int* duration)
{
	VID_PlaySample(sample, duration);
}

void SCR_PlaySampleBuffer(void* buffer, int duration, int hz, int volume)
{
	VID_PlaySampleBuffer(buffer, duration, hz, volume);
}

void SCR_GetMilliseconds(uint32_t* time)
{
	VID_GetMilliseconds(time);
}

void SCR_Quit()
{
	VID_Quit();
}

void SCR_MainLoop(DDB_Interpreter* i, MainLoopCallback callback)
{
	VID_MainLoop(i, callback);
}

void SCR_OpenFileDialog(bool existing, char* filename, size_t bufferSize)
{
	VID_OpenFileDialog(existing, filename, bufferSize);
}

void SCR_SetTextInputMode(bool enabled)
{
	VID_SetTextInputMode(enabled);
}

void SCR_UseInputFile(const char* filename)
{
    File* file = File_Open(filename, ReadOnly);
    if (file == 0)
    {
        inputFile = 0;
        inputFileBegin = 0;
        inputFileEnd = 0;
        return;
    }

    uint64_t fileSize = File_GetSize(file);
    inputFileBegin = inputFile = (const char*)OSAlloc(fileSize);
    inputFileEnd = inputFileBegin + File_Read(file, (uint8_t*)inputFile, fileSize);
    File_Close(file);

    inputFile = inputFileBegin;
}

#endif