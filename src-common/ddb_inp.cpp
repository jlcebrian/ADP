#include <ddb.h>
#include <ddb_scr.h>
#include <ddb_data.h>
#include <ddb_vid.h>
#include <os_char.h>
#include <os_file.h>
#include <os_lib.h>

#define DDB_WRAPAROUND_HISTORY 0

// TODO: Move this to an OS-specific location

enum UndoMode
{
	Undo_Coalesce = 1,
	Undo_Keep = 2,
};

static void DumpUndoBuffer(DDB_Interpreter* i);
static void AddInputHistoryLine (DDB_Interpreter* i);
static void UpdateLastInputHistoryLine (DDB_Interpreter* i);

// ------------------------------ Message-based cursor (PAWS) ------------------------------

#if HAS_PAWS

static int GetPAWSCursorWidth(DDB_Interpreter* i)
{
	uint8_t buffer[16];
	int length = 0;
	DDB_GetMessage(i->ddb, DDB_SYSMSG, 34, (char *)buffer, 16);
	for (int n = 0; buffer[n] != 0; n++) {
		if (buffer[n] >= 16 && buffer[n] <= 20) {
			n++;
		} else if (buffer[n] >= 32) {
			length += charWidth[buffer[n]];
		}
	}
	return length;
}

static void DrawPAWSCursor(DDB_Interpreter* i, int x, int y)
{
	DDB_Window* w = DDB_GetInputWindow(i);
	uint8_t charset = i->ddb->curCharset;

	int px = w->posX;
	int py = w->posY;
	w->posX = x;
	w->posY = y;
	DDB_OutputMessageToWindow(i, DDB_SYSMSG, 34, w);
	DDB_FlushWindow(i, w);
	w->posX = px;
	w->posY = py;
}

#endif

// ------------------------------ Start/End ------------------------------

DDB_Window* DDB_GetInputWindow(DDB_Interpreter* i)
{
	#if HAS_PAWS
	if (i->ddb->version == DDB_VERSION_PAWS)
		return &i->win;
	#endif

	int inputWindow = i->flags[Flag_InputStream] & 0x07;
	return
		inputWindow == 0 ||
		inputWindow == i->curwin ? &i->win : &i->windef[inputWindow];
}

void DDB_StartInput(DDB_Interpreter* i, bool withPrompt)
{
	DDB_Window *iw = DDB_GetInputWindow(i);

	if (withPrompt)
		DDB_OutputUserPrompt(i);

	DDB_Flush(i);
	DDB_ResetScrollCounts(i);
	DDB_OutputInputPrompt(i);
	DDB_FlushWindow(i, iw);
	AddInputHistoryLine(i);
	i->inputCursorX = 0;
	i->inputBufferPtr = 0;
	i->inputBufferLength = 0;
	i->inputCompletionX = 0;
	i->quotedString = 0;
	i->undoBufferLength = 3;
	i->undoBufferCurrentEntry = 3;
	i->undoBuffer[0] = Undo_Keep;
	i->undoBuffer[1] = 0xFF;
	i->undoBuffer[2] = 0;
	i->state = DDB_INPUT;
	SCR_SetTextInputMode(true);
	DumpUndoBuffer(i);
}

void DDB_FinishInput(DDB_Interpreter * i, bool timeout)
{
	DDB_Window* iw = DDB_GetInputWindow(i);
	int echoFlag = timeout ? Input_PrintAfterTimeout : Input_PrintAfterInput;

	UpdateLastInputHistoryLine(i);

	#if HAS_PAWS
	if (i->ddb->version == DDB_VERSION_PAWS)
	{
		iw->ink = i->ddb->defaultInk;
		iw->paper = i->ddb->defaultPaper;
	}
	#endif

	if (i->inputFlags & Input_ClearWindow)
		DDB_ClearWindow(i, iw);
	else if (!timeout)
		DDB_NextLineAtWindow(i, iw);
	else
		iw->posX += i->inputCursorX * columnWidth;

	if ((i->inputFlags & echoFlag) != 0 && i->inputBufferLength > 0 && i->curwin != (i->flags[Flag_InputStream] & 7))
	{
		char buffer[64] = { 0, 0 };

		DDB_Flush(i);
		#if REPRODUCE_ECHO_BUG
		if (i->ddb->target == DDB_MACHINE_ATARIST)
		{
			int newlineCount = 0;
			const char* end = DDB_GetMessage(i->ddb, DDB_SYSMSG, 33, buffer, 64);
			for (int n = 0; buffer+n < end; n++)
				if (buffer[n] == '\r')
					newlineCount++;
			if (newlineCount == 0)
			{
				for (int m = 0; m < i->inputBufferLength; m++)
					DDB_OutputChar(i, i->inputBuffer[m]);
			}
			for (int n = 0; n < 64 && buffer+n < end; n++)
			{
				DDB_OutputChar(i, buffer[n]);
				if (buffer[n] == '\r')
				{
					if (--newlineCount == 0)
					{
						for (int m = 0; m < i->inputBufferLength; m++)
							DDB_OutputChar(i, i->inputBuffer[m]);
					}
				}
			}
			DDB_Flush(i);
			DDB_NewLine(i);
		}
		else
		#endif
		{
			DDB_Flush(i);
			DDB_OutputMessage(i, DDB_SYSMSG, 33);
			for (int n = 0; n < i->inputBufferLength; n++)
				DDB_OutputChar(i, i->inputBuffer[n]);
			DDB_Flush(i);
			DDB_NewLine(i);
		}
	}

	i->inputCursorX = 0;
	SCR_SetTextInputMode(false);
	DDB_ResetScrollCounts(i);
}

void DDB_ResolveInputEnd(DDB_Interpreter* i)
{
	char buffer[3];

	DDB_GetMessage(i->ddb, DDB_SYSMSG, 30, buffer, 3);
	if (ToUpper(i->inputBuffer[0]) == ToUpper(buffer[0]))
	{
		DDB_NewText(i);

		if (i->ddb->version < 2)
		{
			DDB_Reset(i);
			DDB_ResetWindows(i);
			VID_Clear(0, 0, 320, 200, 0);
			VID_SetDefaultPalette();
		}
		else
		{
			i->flags[Flag_Locno] = 0;
			i->procstackptr = 0;
			i->doall = false;
			i->procstack[0].entry = 0;
			i->procstack[0].process = 0;
			i->procstack[0].offset = 0;
		}
		i->state = DDB_RUNNING;
	}
	else
	{
		// If there is a 'goodbye!' sysmess 14, delay the quit op
		DDB_GetMessage(i->ddb, DDB_SYSMSG, 14, buffer, 3);
		if (buffer[0] != 0 && buffer[1] != 0)
		{
			DDB_OutputMessage(i, DDB_SYSMSG, 14);
			DDB_Flush(i);
			SCR_GetMilliseconds(&i->quitStart);
		}

		i->state = DDB_QUIT;
	}
}

void DDB_ResolveInputQuit(DDB_Interpreter* i)
{
	char buffer[2];

	DDB_GetMessage(i->ddb, DDB_SYSMSG, 30, buffer, 2);
	if (ToUpper(i->inputBuffer[0]) == ToUpper(buffer[0]))
	{
		i->done = true;
		i->state = DDB_RUNNING;
	}
	else
	{
		// Jump to the next entry
		i->procstack[i->procstackptr].offset = 0;
		i->procstack[i->procstackptr].entry++;
		i->state = DDB_RUNNING;
	}
	i->inputBufferLength = 0;
}

void DDB_ResolveInputLoad(DDB_Interpreter* i)
{
	if (i->inputBufferLength == 0)
	{
		i->state = DDB_RUNNING;
		return;
	}

	i->inputBuffer[i->inputBufferLength] = 0;

	bool success = false;
	File* file = File_Open((const char*)i->inputBuffer, ReadOnly);
	if (file != 0)
	{
		success = File_Read(file, i->buffer, i->saveStateSize) == i->saveStateSize;
		File_Close(file);
	}

	DDB_NewText(i);

	if (!success)
	{
		DDB_OutputMessage(i, DDB_SYSMSG, 57);	// I/O Error
		DDB_Flush(i);
		DDB_NewLine(i);
		i->state = DDB_WAITING_FOR_KEY;

		// Jump to the next entry
		i->procstack[i->procstackptr].offset = 0;
		i->procstack[i->procstackptr].entry++;
	}
	else
	{
		i->state = DDB_RUNNING;
	}
}

void DDB_ResolveInputSave(DDB_Interpreter* i)
{
	if (i->inputBufferLength == 0)
	{
		i->state = DDB_RUNNING;
		return;
	}

	i->inputBuffer[i->inputBufferLength] = 0;

	bool success = false;
	File* file = File_Create((const char*)i->inputBuffer);
	if (file != 0)
	{
		success = File_Write(file, i->buffer, i->saveStateSize) == i->saveStateSize;
		File_Close(file);
		OSSyncFS();
	}

	DDB_NewText(i);

	if (!success)
	{
		DDB_OutputMessage(i, DDB_SYSMSG, 57);	// I/O Error
		DDB_Flush(i);
		DDB_NewLine(i);

		// Jump to the next entry
		i->procstack[i->procstackptr].offset = 0;
		i->procstack[i->procstackptr].entry++;
		i->state = DDB_RUNNING;
	}
	else
	{
		i->state = DDB_RUNNING;
	}
}

// ------------------------------ History ------------------------------

static void RemoveFirstInputHistoryLine (DDB_Interpreter* i)
{
	int n = 0;

	while (n < i->inputHistoryLength && i->inputHistory[n] != 0)
		n++;
	if (n < i->inputHistoryLength)
	{
		MemMove(i->inputHistory, i->inputHistory + n + 1, i->inputHistoryLength - n - 1);
		i->inputHistoryLength -= n + 1;
		i->inputHistoryCurrentEntry -= n + 1;
		i->inputHistoryLastEntry -= n + 1;
	}
}

static void AddInputHistoryLine (DDB_Interpreter* i)
{
	if (i->inputHistoryLength >= HISTORY_SIZE)
		RemoveFirstInputHistoryLine(i);
	if (i->inputHistoryLength < HISTORY_SIZE)
	{
		i->inputHistoryLastEntry = i->inputHistoryLength;
		i->inputHistoryCurrentEntry = i->inputHistoryLength;
		i->inputHistory[i->inputHistoryLength++] = 0;
	}
}

static void UpdateLastInputHistoryLine (DDB_Interpreter* i)
{
	int length = i->inputBufferLength;
	int current = i->inputHistoryLength - i->inputHistoryLastEntry;
	int required = length - current;

	if (i->inputHistoryLength + required > HISTORY_SIZE)
	{
		RemoveFirstInputHistoryLine(i);
		if (i->inputHistoryLength + required > HISTORY_SIZE)
			return;
	}
	MemCopy((char *)i->inputHistory + i->inputHistoryLastEntry,
	       i->inputBuffer, length);
	i->inputHistoryLength = i->inputHistoryLastEntry + length + 1;
	i->inputHistory[i->inputHistoryLength - 1] = 0;
	i->inputHistoryCurrentEntry = i->inputHistoryLastEntry;
	i->inputCompletionX = 0;
}

static bool HistoryCompletion (DDB_Interpreter* i)
{
	const uint8_t* ptr = i->inputHistory + i->inputHistoryCurrentEntry;
	const uint8_t* start = ptr;
	uint8_t size = i->inputBufferLength;
	if (i->inputHistoryLastEntry == 0)
		return false;
	if (i->inputCompletionX > 0 && i->inputCompletionX < size)
		size = i->inputCompletionX;

	for (;;)
	{
		if (ptr == i->inputHistory)
		{
			ptr = i->inputHistory + i->inputHistoryLastEntry;
			if (ptr == start)
				return false;
		}
		ptr--;
		if (ptr == start)
			return false;
		if (ptr == i->inputHistory || ptr[-1] == 0)
		{
			if (StrComp(ptr, i->inputBuffer, size) == 0)
			{
				StrCopy(i->inputBuffer, sizeof(i->inputBuffer), ptr);
				i->inputHistoryCurrentEntry = ptr - i->inputHistory;
				i->inputCursorX = i->inputBufferLength = StrLen(i->inputBuffer);
				i->inputCompletionX = size;
				return true;
			}
		}
	}
}

static bool NavigateHistory (DDB_Interpreter* i, int direction)
{
	const uint8_t* ptr = i->inputHistory + i->inputHistoryCurrentEntry;
	if (i->inputHistoryLastEntry == 0)
		return false;
	if (direction > 0) {
		while (*ptr != 0 && ptr < i->inputHistory + i->inputHistoryLength)
			ptr++;
		if (*ptr == 0 && ptr < i->inputHistory + i->inputHistoryLength)
			ptr++;
		if (ptr == i->inputHistory + i->inputHistoryLength)
		{
#			if DDB_WRAPAROUND_HISTORY
			ptr = i->inputHistory;
#			else
			return false;
#			endif
		}
	} else {
		if (ptr == i->inputHistory)
		{
#			if DDB_WRAPAROUND_HISTORY
			ptr = i->inputHistory + i->inputHistoryLastEntry;
#			else
			return false;
#			endif
		}
		else while (ptr > i->inputHistory) {
			ptr--;
			if (ptr > i->inputHistory && ptr[-1] == 0)
				break;
		}
	}
	StrCopy(i->inputBuffer, sizeof(i->inputBuffer), ptr);
	i->inputHistoryCurrentEntry = ptr - i->inputHistory;
	i->inputCursorX = i->inputBufferLength = StrLen(i->inputBuffer);
	return true;
}

// ------------------------------ Undo ------------------------------

static void DumpUndoBuffer(DDB_Interpreter* i)
{
#if DEBUG_UNDO
	const char* ptr = i->undoBuffer;
	const char* end = i->undoBuffer + i->undoBufferLength;
	fprintf(stderr, "Undo buffer: (%04X length, current entry %04X)\n", i->undoBufferLength, i->undoBufferCurrentEntry);
	while (ptr < end)
	{
		fprintf(stderr, "%04X %X %s %02X %s\n", (int)(ptr - i->undoBuffer), (uint8_t)ptr[0],
			i->undoBufferCurrentEntry == ptr - i->undoBuffer ? ">>" : "  ", (uint8_t)ptr[1], ptr+2);
		ptr++;
		while (*ptr != 0 && ptr < end) ptr++;
		if (*ptr == 0 && ptr < end) ptr++;
	}
	fprintf(stderr, "%04X   %s ---\n", (int)(ptr - i->undoBuffer), i->undoBufferCurrentEntry == ptr - i->undoBuffer ? ">>" : "  ");
	fprintf(stderr, "\n");
#endif
}

static void CheckUndoBufferSize(DDB_Interpreter* i, int size)
{
	while (i->undoBufferCurrentEntry + size  >= UNDO_BUFFER_SIZE)
	{
		const uint8_t* ptr = i->undoBuffer;
		const uint8_t* end = i->undoBuffer + i->undoBufferLength;
		while (*ptr != 0 && ptr < end) ptr++;
		if (ptr < end) ptr++;
		MemMove(i->undoBuffer, ptr, end - ptr);
		i->undoBufferCurrentEntry -= ptr - i->undoBuffer;
		i->undoBufferLength -= ptr - i->undoBuffer;
	}
}

static void SaveUndoState(DDB_Interpreter* i, UndoMode mode)
{
	int size = i->inputBufferLength;
	int prev = i->undoBufferCurrentEntry;

	// Coalesce small edits
	if (prev == i->undoBufferLength && prev > 0 && mode == Undo_Coalesce)
	{
		do prev--; while (prev > 0 && i->undoBuffer[prev-1] != 0);
		if (i->undoBuffer[prev] == Undo_Coalesce)
		{
			int prevCursor = i->undoBuffer[prev+1] == 255 ? 0 : i->undoBuffer[prev+1];
			int distance = prevCursor - i->inputCursorX;
			if (distance < 0) distance = -distance;
			if (distance <= 1)
			{
				i->undoBufferCurrentEntry = prev;
				i->undoBufferLength = prev;
			}
		}
	}

	CheckUndoBufferSize(i, size + 3);

	i->undoBuffer[i->undoBufferCurrentEntry] = mode;
	if (i->inputCursorX == 0)
		i->undoBuffer[i->undoBufferCurrentEntry+1] = 255;
	else
		i->undoBuffer[i->undoBufferCurrentEntry+1] = i->inputCursorX;
	MemCopy(i->undoBuffer + i->undoBufferCurrentEntry + 2, i->inputBuffer, size);
	i->undoBuffer[i->undoBufferCurrentEntry + size + 2] = 0;

	i->undoBufferLength = i->undoBufferCurrentEntry + size + 3;
	i->undoBufferCurrentEntry = i->undoBufferLength;

	DumpUndoBuffer(i);
}

static void Undo(DDB_Interpreter* i)
{
	int pos = i->undoBufferCurrentEntry, prev;
	if (pos == 0)
		return;
	do pos--; while (pos > 0 && i->undoBuffer[pos-1] != 0);
	if (pos == 0)
		return;
	prev = pos;
	do prev--; while (prev > 0 && i->undoBuffer[prev-1] != 0);

	i->undoBufferCurrentEntry = pos;

	i->inputCursorX = i->undoBuffer[prev+1];
	if (i->inputCursorX == 255)
		i->inputCursorX = 0;
	MemCopy(i->inputBuffer, i->undoBuffer + prev + 2, pos - prev - 3);
	i->inputBufferLength = pos - prev - 3;
	if (i->inputCursorX > i->inputBufferLength)
	{
		// fprintf(stderr, "Undo: cursor position %d > buffer length %d\n", i->inputCursorX, i->inputBufferLength);
		i->inputCursorX = i->inputBufferLength;
	}

	DumpUndoBuffer(i);
}

static void Redo(DDB_Interpreter* i)
{
	int pos = i->undoBufferCurrentEntry;
	if (pos >= i->undoBufferLength)
		return;

	int next = pos;
	do next++; while (next < i->undoBufferLength && i->undoBuffer[next] != 0);
	if (next < i->undoBufferLength)
		next++;

	i->undoBufferCurrentEntry = next;
	i->inputCursorX = i->undoBuffer[pos+1];
	if (i->inputCursorX == 255)
		i->inputCursorX = 0;
	MemCopy(i->inputBuffer, i->undoBuffer + pos + 2, next - pos - 3);
	i->inputBufferLength = next - pos - 3;

	DumpUndoBuffer(i);
}

// ------------------------------ Visuals ------------------------------

void DDB_PrintInputLine(DDB_Interpreter* i, bool withCursor)
{
	DDB_Window* w = DDB_GetInputWindow(i);
	uint8_t cellX = i->cellX;
	uint8_t cellW = i->cellW;
	int x = w->posX;
	int y = w->posY;
	int cursorX = -1;
	int first = 0;
	int width;
	int maxX;
	int n;

	uint8_t ink, paper;
	DDB_GetCurrentColors(i->ddb, w, &ink, &paper);

	// ST/Amiga have originally some weird combination here,
	// but PC uses just the text colors and it looks better
	int cursorPaper = paper;
	int cursorInk = ink;
	int cursorWidth = 0;

	if (w != &i->win)
		DDB_CalculateCells(i, w, &cellX, &cellW);

	width = cellX*8 + cellW*8 - x;
	maxX = x + width;

	if (withCursor)
	{
		int totalWidth = 0;
		#if HAS_PAWS
		if (i->ddb->version == DDB_VERSION_PAWS)
			cursorWidth = GetPAWSCursorWidth(i);
		#endif
		for (n = 0; n < i->inputBufferLength; n++)
		{
			if (n == i->inputCursorX)
				cursorX = totalWidth;
			totalWidth += charWidth[i->inputBuffer[n]];
		}
		if (cursorX == -1)
		{
			cursorX = totalWidth;
			totalWidth += charWidth['_'];
		}
		if (totalWidth > width - cursorWidth)
		{
			for (n = 0; n < i->inputBufferLength; n++)
			{
				if (cursorX < charWidth[i->inputBuffer[n]])
					break;
				first++;
				cursorX -= charWidth[i->inputBuffer[n]];
				if (cursorX < width - charWidth['_'])
					break;
			}
		}
	}

	for (n = first; n < i->inputBufferLength; n++)
	{
		if (x + charWidth[i->inputBuffer[n]] > maxX)
			break;
		if (n == i->inputCursorX && withCursor)
		{
			#if HAS_PAWS
			if (i->ddb->version == DDB_VERSION_PAWS)
			{
				DrawPAWSCursor(i, x, y);
				x += cursorWidth;
				DDB_GetCurrentColors(i->ddb, w, &ink, &paper);
				SCR_DrawCharacter(x, y, i->inputBuffer[n], cursorInk, cursorPaper);
			}
			else
			#endif
			{
				SCR_DrawCharacter(x, y, i->inputBuffer[n], cursorInk, cursorPaper);
				SCR_DrawCharacter(x, y, '_', cursorInk, 255);
			}
		}
		else
		{
			SCR_DrawCharacter(x, y, i->inputBuffer[n], ink, paper);
		}
		x += charWidth[i->inputBuffer[n]];
	}
	if (n == i->inputCursorX && withCursor)
	{
		#if HAS_PAWS
		if (i->ddb->version == DDB_VERSION_PAWS)
		{
			DrawPAWSCursor(i, x, y);
			x += cursorWidth;
		}
		else
		#endif
		{
			SCR_DrawCharacter(x, y, '_', cursorInk, cursorPaper);
			x += charWidth['_'];
		}
	}

	if (maxX > x)
		SCR_Clear(x, y, maxX-x, lineHeight, paper);
}

void DDB_PlayClick(DDB_Interpreter* i, bool allowRepeats)
{
	uint32_t time = 0;
	SCR_GetMilliseconds(&time);
#ifndef NO_SAMPLES
	if (allowRepeats || i->lastClick < time - 250)
	{
		if (i->keyClick == 2)
		{
			SCR_PlaySampleBuffer(clickSample, clickSampleSize,
			                     RandInt(29000, 31000),
			                     RandInt(48, 80));
		}
		else if (i->keyClick == 1)
		{
			SCR_PlaySampleBuffer(beepSample, beepSampleSize,
			                     30000, RandInt(48, 80));
		}
	}
#endif
	i->lastClick = time;
}

// ------------------------------ Main entry point ------------------------------

void DDB_ProcessInputFrame()
{
	DDB_Interpreter* i = interpreter;
	uint8_t key = 0, ext = 0, mod = 0;

	if (!SCR_AnyKey())
		return;

	SCR_GetKey(&key, &ext, &mod);

	if (key == 0x7F)
		key = 0;
	if (key != 0)
		ext = 0;

	if (ext == 0x73) // Alternative Left
		ext = 0x4B;
	if (ext == 0x74) // Alternative Right
		ext = 0x4D;

	switch (key | (ext << 8))
	{
		case 0x5300: // Delete
			if (i->inputCursorX < i->inputBufferLength)
			{
				if (mod & SCR_KEYMOD_CTRL)
				{
					while (i->inputCursorX < i->inputBufferLength && i->inputBuffer[i->inputCursorX] != ' ')
					{
						MemMove(i->inputBuffer + i->inputCursorX, i->inputBuffer + i->inputCursorX + 1, i->inputBufferLength - i->inputCursorX - 1);
						i->inputBufferLength--;
					}
					while (i->inputCursorX < i->inputBufferLength && i->inputBuffer[i->inputCursorX] == ' ')
					{
						MemMove(i->inputBuffer + i->inputCursorX, i->inputBuffer + i->inputCursorX + 1, i->inputBufferLength - i->inputCursorX - 1);
						i->inputBufferLength--;
					}
					SaveUndoState(i, Undo_Keep);
				}
				else
				{
					MemMove(i->inputBuffer + i->inputCursorX, i->inputBuffer + i->inputCursorX + 1, i->inputBufferLength - i->inputCursorX - 1);
					i->inputBufferLength--;
					SaveUndoState(i, Undo_Coalesce);
				}
				DDB_PrintInputLine(i, true);
				if (i->state == DDB_INPUT)
					UpdateLastInputHistoryLine(i);
			}
			break;

		case 0x4700: // Home
			i->inputCursorX = 0;
			DDB_PrintInputLine(i, true);
			break;

		case 0x6100: // End
		case 0x4F00: // End
			i->inputCursorX = i->inputBufferLength;
			DDB_PrintInputLine(i, true);
			break;

		case 0x4B00: // Left
			if (i->inputCursorX > 0)
			{
				int startPos = i->inputCursorX;
				i->inputCursorX--;
				if (mod & SCR_KEYMOD_CTRL)
				{
					while (i->inputCursorX > 0 && i->inputBuffer[i->inputCursorX] == ' ')
						i->inputCursorX--;
					if (i->inputBuffer[i->inputCursorX] != ' ')
					{
						while (i->inputCursorX > 0 && i->inputBuffer[i->inputCursorX] != ' ')
							i->inputCursorX--;
						if (i->inputCursorX < startPos-1 && i->inputBuffer[i->inputCursorX] == ' ')
							i->inputCursorX++;
					}
				}
				DDB_PrintInputLine(i, true);
			}
			break;

		case 0x4D00: // Right
			if (i->inputCursorX < i->inputBufferLength)
			{
				if (mod & SCR_KEYMOD_CTRL)
				{
					while (i->inputCursorX < i->inputBufferLength && i->inputBuffer[i->inputCursorX] != ' ')
						i->inputCursorX++;
					while (i->inputCursorX < i->inputBufferLength && i->inputBuffer[i->inputCursorX] == ' ')
						i->inputCursorX++;
				}
				else
				{
					i->inputCursorX++;
				}
				DDB_PrintInputLine(i, true);
			}
			break;

		case 0x4200: // F8
			if (i->state == DDB_INPUT)
			{
				if (HistoryCompletion(i))
				{
					SaveUndoState(i, Undo_Coalesce);
					DDB_PrintInputLine(i, true);
				}
			}
			break;

		case 0x4800: // Up
			if (i->state == DDB_INPUT)
			{
				if (NavigateHistory(i, -1))
				{
					SaveUndoState(i, Undo_Coalesce);
					DDB_PrintInputLine(i, true);
				}
			}
			break;

		case 0x5000: // Down
			if (i->state == DDB_INPUT)
			{
				if (NavigateHistory(i, 1))
				{
					SaveUndoState(i, Undo_Coalesce);
					DDB_PrintInputLine(i, true);
				}
			}
			break;

		case 0x001B: // Escape
			if (i->inputBufferLength > 0)
			{
				i->inputBufferLength = 0;
				i->inputCursorX = 0;
				SaveUndoState(i, Undo_Keep);
				DDB_PrintInputLine(i, true);
				if (i->state == DDB_INPUT)
					UpdateLastInputHistoryLine(i);
			}
			break;

		case 0x0008: // Backspace
			if (i->inputCursorX > 0)
			{
				if (mod & SCR_KEYMOD_CTRL)
				{
					while (i->inputCursorX > 0 && i->inputBuffer[i->inputCursorX-1] == ' ')
					{
						MemMove(i->inputBuffer + i->inputCursorX - 1, i->inputBuffer + i->inputCursorX, i->inputBufferLength - i->inputCursorX);
						i->inputCursorX--;
						i->inputBufferLength--;
					}
					while (i->inputCursorX > 0 && i->inputBuffer[i->inputCursorX-1] != ' ')
					{
						MemMove(i->inputBuffer + i->inputCursorX - 1, i->inputBuffer + i->inputCursorX, i->inputBufferLength - i->inputCursorX);
						i->inputCursorX--;
						i->inputBufferLength--;
					}
					SaveUndoState(i, Undo_Keep);
				}
				else
				{
					MemMove(i->inputBuffer + i->inputCursorX - 1, i->inputBuffer + i->inputCursorX, i->inputBufferLength - i->inputCursorX);
					i->inputCursorX--;
					i->inputBufferLength--;
					SaveUndoState(i, Undo_Coalesce);
				}
				DDB_PrintInputLine(i, true);
				if (i->state == DDB_INPUT)
					UpdateLastInputHistoryLine(i);
			}
			break;

		case 0x1C00: // Enter
		case 0x000D:
			if (i->inputBufferLength == 0)
				break;
			//Delay(50);
			DDB_PrintInputLine(i, false);
			DDB_FinishInput(i, false);
			switch (i->state)
			{
				case DDB_INPUT_QUIT:    DDB_ResolveInputQuit(i); break;
				case DDB_INPUT_END:	    DDB_ResolveInputEnd(i); break;
				case DDB_INPUT_SAVE:    DDB_ResolveInputSave(i); break;
				case DDB_INPUT_LOAD:    DDB_ResolveInputLoad(i); break;

				default:
					if (i->ddb->oldMainLoop)
						i->state = DDB_FINISHED;
					else
						i->state = DDB_RUNNING;
			}
			break;

		default:
			// fprintf(stderr, "Key: %04X\n", key | (ext << 8));

			if ((mod & SCR_KEYMOD_CTRL) != 0)
			{
				#ifdef HAS_CLIPBOARD
				if (key == 'v' || key == 'V')
				{
					uint32_t size = 0;
					if (VID_HasClipboardText(&size))
					{
						if (i->inputBufferLength >= sizeof(i->inputBuffer) - 1)
							break;
						if (size > sizeof(i->inputBuffer) - i->inputBufferLength - 1)
							size = sizeof(i->inputBuffer) - i->inputBufferLength - 1;
						if (size == 0)
							break;

						MemMove(i->inputBuffer + i->inputCursorX + size, i->inputBuffer + i->inputCursorX, i->inputBufferLength - i->inputCursorX);
						VID_GetClipboardText(i->inputBuffer + i->inputCursorX, size);
						i->inputBufferLength += size;
						i->inputCursorX += size;
						SaveUndoState(i, Undo_Keep);
						DDB_PrintInputLine(i, true);
						if (i->state == DDB_INPUT)
							UpdateLastInputHistoryLine(i);
					}
					break;
				}
				if (key == 'c' || key == 'C')
				{
					VID_SetClipboardText(i->inputBuffer, i->inputBufferLength);
					break;
				}
				#endif
				if (key == 'z' || key == 'Z')
				{
					Undo(i);
					DDB_PrintInputLine(i, true);
					if (i->state == DDB_INPUT)
						UpdateLastInputHistoryLine(i);
					break;
				}
				if (key == 'y' || key == 'Y')
				{
					Redo(i);
					DDB_PrintInputLine(i, true);
					if (i->state == DDB_INPUT)
						UpdateLastInputHistoryLine(i);
					break;
				}
			}
			if (ext == 0 && key >= 16 && key <= 127 && i->inputBufferLength < sizeof(i->inputBuffer)-1)
			{
				if (i->ddb->version == DDB_VERSION_PAWS)
					key = ToUpper(key);
				MemMove(i->inputBuffer + i->inputCursorX + 1, i->inputBuffer + i->inputCursorX, i->inputBufferLength - i->inputCursorX);
				i->inputBuffer[i->inputCursorX] = key;
				i->inputBufferLength++;
				i->inputCursorX++;
				SaveUndoState(i, Undo_Coalesce);
				DDB_PrintInputLine(i, true);
				if (i->state == DDB_INPUT)
					UpdateLastInputHistoryLine(i);
			}
			break;
	}

	DDB_PlayClick(i, true);
}