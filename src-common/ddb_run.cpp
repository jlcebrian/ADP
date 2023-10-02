#include <ddb.h>
#include <ddb_scr.h>
#include <os_char.h>
#include <os_mem.h>
#include <os_lib.h>

#define PAUSE_ON_INKEY 0
#define DEBUG_UNDO 0

#ifndef TRACE_ON
#define TRACE_ON 0
#endif

#if TRACE_ON
extern void TracePrintf(const char* format, ...);
#define TRACE TracePrintf
#else
#define TRACE(...)
#endif

static void MarkWindowOutput();

static uint8_t objNameBuffer[256];

static uint8_t ToUpper(uint8_t ch);
static uint8_t ToLower(uint8_t ch);

#if TRACE_ON
static const char* DDB_MessageTypeNames[] = {
	"MSG",
	"SYSMSG",
	"OBJNAME",
	"LOCDESC"
};
static const char* DDB_StateNames[] = {
	"DDB_RUNNING",
	"DDB_PAUSED",
	"DDB_FINISHED",
	"DDB_FATAL_ERROR",
	"DDB_QUIT",
	"DDB_WAITING_FOR_KEY",
	"DDB_CHECKING_KEY",
	"DDB_VSYNC",
	"DDB_INPUT",
	"DDB_INPUT_QUIT",
	"DDB_INPUT_END",
	"DDB_INPUT_SAVE",
	"DDB_INPUT_LOAD"
};
static const char* DDB_FlowNames[] = {
	"FLOW_DESC",
	"FLOW_AFTER_TURN",
	"FLOW_INPUT",
	"FLOW_RESPONSES",
};
#endif

static const char* TranslateChar(uint8_t c)
{
	static char buffer[2] = { 32, 0 };

	if (c == '\n')
		return "\\n";
	else if (c == '\r')
		return "\\r";
	else if (c == '\t')
		return "\\t";
	else if (c == '\b')
		return "\\b";
	else if (c == '\x0E')
		return "\\g";
	else if (c == '\x0F')
		return "\\t";
		
	buffer[0] = c;
	return buffer;
}

void DDB_ResetWindows (DDB_Interpreter* i)
{
	for (int n = 0; n < 8; n++)
	{
		DDB_Window* w = &i->windef[n];
		w->x = 0;
		w->y = 0;
		w->width = screenWidth;
		w->height = screenHeight;
		w->ink = 1;
		w->paper = 0;
		w->posX = w->x;
		w->posY = w->y;
		w->scrollCount = 0;
	}

	i->win = i->windef[0];
	DDB_CalculateCells(i, &i->win, &i->cellX, &i->cellW);
}

void DDB_Reset (DDB_Interpreter* i)
{
	int n;
	uint8_t topFlags[10];
	for (n = 0xF9; n <= 0xFF; n++)
		topFlags[n-0xF9] = i->flags[n];

	i->state = DDB_RUNNING;
	i->doall = false;
	i->procstackptr = 0;
	i->procstack[0].entry = 0;
	i->procstack[0].process = 0;
	i->procstack[0].offset = 0;
	i->inputBufferLength = 0;
	i->inputBuffer[0] = 0;
	i->inputBufferPtr = 0;
	i->inputCompletionX = 0;
	MemClear(i->buffer, i->saveStateSize);

	for (n = 0xF9; n <= 0xFF; n++)
		i->flags[n] = topFlags[n-0xF9];

	i->flags[Flag_CPAdjective] = 255;
	i->flags[Flag_CPNoun] = 255;
	i->flags[Flag_GraphicFlags] = 128;
	i->flags[Flag_MaxCarried] = 4;
	i->flags[Flag_Strength] = 10;

	if (i->ddb->version > 1)
	{
		i->flags[Flag_ScreenMode] = i->screenMode;
	}

	if (i->ddb->oldMainLoop)
	{
		i->oldMainLoopState = FLOW_DESC;
		i->procstack[0].process = 1;
	}

	for (n = 0; n < i->ddb->numObjects; n++)
	{
		i->objloc[n] = i->ddb->objLocTable[n];
		if (i->objloc[n] == Loc_Carried)
			i->flags[Flag_NumCarried]++;
	}

	// SCR_Clear(0, 0, screenWidth, screenHeight, 0);
}

// --------------------
//  Internal functions
// --------------------

static void TraceVocabularyWord (DDB* ddb, uint8_t type, uint8_t index)
{
	#if TRACE_ON
	uint8_t* ptr = ddb->vocabulary;

	while (*ptr != 0)
	{
		if (ptr[5] == index && ptr[6] == type)
		{
			for (int n = 0; n < 5; n++)
				TRACE("%c", DDB_Char2ISO[(ptr[n] ^ 0xFF) & 0x7F]);
			return;
		}
		ptr += 7;
	}
	if (type == WordType_Verb && index < 40)
		TraceVocabularyWord(ddb, WordType_Noun, index);
	else
		TRACE("%-5d", index);
	#endif
}

static int CalculateWeight (DDB_Interpreter* i, uint8_t objno, int depth)
{
	int value = 0;

	if (objno >= i->ddb->numObjects)
		return 0;

	uint8_t attr = i->ddb->objAttrTable[objno];
	value += attr & Obj_Weight;
	if ((attr & Obj_Container) != 0 && depth < 10)
	{
		// Container
		for (int n = 0; n < i->ddb->numObjects; n++)
		{
			if (i->objloc[n] == objno)
				value += CalculateWeight(i, n, depth + 1);
		}
	}

	return value > 255 ? 255 : value;
}

static int CalculateCarriedWeight(DDB_Interpreter* i)
{
	int value = 0;
	for (int n = 0; n < i->ddb->numObjects; n++)
	{
		if (i->objloc[n] == Loc_Carried || i->objloc[n] == Loc_Worn)
			value += CalculateWeight(i, n, 0);
	}
	return value > 255 ? 255 : value;
}

void DDB_CalculateCells (DDB_Interpreter* i, DDB_Window* w, uint8_t* cellX, uint8_t* cellW)
{
	static int inc[] = { 0,1,2,3,1,2,3,3,1,2,2,3,1,1,2,3 };
	int charsX = w->x / 6;
	int charsW = w->width / 6 - 1;
	int cellsX = 3*(charsX >> 2) + ((charsX & 3) != 0 ? (charsX & 3)-1 : 0);
	int cellsW = inc[((charsX & 3) << 2) | (charsW & 3)] + 3*(charsW >> 2);

	//if ((w->x & 0x07) == 0) cellsX = w->x / 8;

	// This adjustment is necessary because the original interpreter
	// calculates this using the unclipped window width
	if (w->x + w->width >= screenWidth)
		cellsW = (screenWidth - cellsX*8) / 8;
	if (cellsW == 0)
		cellsW = 1;

	// fprintf(stderr, "Adjusting x=%d width=%d -> %d width %d\n", x, width, cellsX*8, cellsW*8);

	*cellX = cellsX;
	*cellW = cellsW;
}	

static void ShowMorePrompt (DDB_Interpreter* i)
{
	static char more[128];

	DDB_Window* w = &i->win;
	int x = w->x;
	int maxX = i->cellX*8 + i->cellW*8;
	int ink = inkMap[w->ink];
	int paper = inkMap[w->paper];

	DDB_GetMessage(i->ddb, DDB_SYSMSG, 32, more, sizeof(more));
	SCR_Clear(w->posX, w->posY, maxX - w->posX, lineHeight, paper);
	for (const char* ptr = more; *ptr; ptr++) {
		if (x + charWidth[(uint8_t)*ptr] > maxX)
			break;
		SCR_DrawCharacter(x, w->posY, *ptr, ink, paper);
		x += charWidth[(uint8_t)*ptr];
	}
	SCR_WaitForKey();
	SCR_Clear(w->posX, w->posY, maxX - w->posX, lineHeight, paper);

	if (i->flags[Flag_TimeoutFlags] & Timeout_MorePrompt)
	{
		i->timeout = true;
		i->timeoutRemainingMs = i->flags[Flag_Timeout] * 1000;
	}
	i->flags[Flag_TimeoutFlags] &= ~Timeout_LastFrame;
}

bool DDB_NextLineAtWindow (DDB_Interpreter* i, DDB_Window* w)
{
	int maxY = w->y + w->height - lineHeight;
	int paper = inkMap[w->paper];

	w->posX = w->x;
	w->posY += lineHeight;
	if (w->posY > maxY)
	{
		uint8_t cellX = i->cellX, cellW = i->cellW;
		int scroll = w->posY - maxY;
		if (w != &i->win) 
			DDB_CalculateCells(i, w, &cellX, &cellW);
		SCR_Scroll(cellX*8, w->y, cellW*8, w->height, scroll, paper, (w->flags & Win_NoMorePrompt) ? false : w->smooth);
		w->posY -= scroll;
	}
	w->scrollCount++;

	if ((w->flags & Win_NoMorePrompt) == 0)
	{
		int scrollLines = (w->height / lineHeight) - 1;
		if (scrollLines > 2 && w->scrollCount >= scrollLines) {
			ShowMorePrompt(i);
			w->scrollCount = 0;
			w->smooth = 1;
		}
	}

	return false;
}

bool DDB_NextLine (DDB_Interpreter* i)
{
	return DDB_NextLineAtWindow(i, &i->win);
}

bool DDB_NewLineAtWindow (DDB_Interpreter* i, DDB_Window* w)
{
	int maxX = w->x + w->width;
	uint8_t cellX, cellW;
	if (w == &i->win)
	{
		cellX = i->cellX;
		cellW = i->cellW;
	}
	else
	{
		DDB_CalculateCells(i, w, &cellX, &cellW);
	}
	maxX = cellX * 8 + cellW * 8;
	if (w->posX < maxX)
	{
		int paper = inkMap[w->paper];
		// fprintf(stderr, "NewLine clearing up to %d (posX:%d cellX:%d cellW:%d)\n", maxX, w->posX, cellX, cellW);
		SCR_Clear(w->posX, w->posY, maxX - w->posX, lineHeight, paper);
	}

	return DDB_NextLineAtWindow(i, w);
}

bool DDB_NewLine (DDB_Interpreter* i)
{
	return DDB_NewLineAtWindow(i, &i->win);
}

void DDB_ClearWindow (DDB_Interpreter* i, DDB_Window* w)
{
	int x, width;
	if (w != &i->win)
	{
		uint8_t cellX, cellW;
		DDB_CalculateCells(i, w, &cellX, &cellW);
		x = cellX * 8;
		width = cellW * 8;
	}
	else
	{
		x = i->cellX * 8;
		width = i->cellW * 8;
	}

	// Hack: check how it is done in the originals, because it's not clearing
	// the right window pixels in Espacial's menus or Chichen's cutscenes
	//if (x + width - 2 < w->x + w->width)
	//	width = w->x + w->width - x - 2;

	// int index = w == &i->win ? i->curwin : w - i->windef;
	// fprintf(stderr, "Clearing window %d: %d,%d %dx%d\n", index, x, w->y, width, w->height);
	SCR_Clear(x, w->y, width, w->height, inkMap[w->paper & 0x0F]);

	w->posX = w->x;
	w->posY = w->y;
	w->scrollCount = 0;
	w->smooth = 0;
}

static bool BufferPicture (DDB_Interpreter* i, uint8_t picno)
{
	i->currentPicture = picno;
	return SCR_LoadPicture(picno, i->screenMode);
}

static inline int AdjustX (int x, int columnWidth)
{
	int    cells = x / columnWidth;
	return cells * columnWidth;
}

static void DrawBufferedPicture (DDB_Interpreter* i)
{
	DDB_Window* w = &i->win;
	int16_t x, width;

	bool fixed = false;
	int16_t picx = 0;
	int16_t picy = 0;
	int16_t picw = 0;
	int16_t pich = 0;
	SCR_GetPictureInfo(&fixed, &picx, &picy, &picw, &pich);

	if (fixed) 
	{
		x             = picx & ~7;
		width         = (picw + 7) & ~7;
		i->cellX	  = picx / 8;
		i->cellW	  = (width + 7) / 8;
		i->win.width  = AdjustX(picw, columnWidth);
		i->win.height = pich;
		i->win.x      = AdjustX(x + 3, columnWidth);
		i->win.y      = picy;
		i->win.posX   = x;
		i->win.posY   = picy;

		//fprintf(stderr, "Drawing fixed picture %d: %d,%d %dx%d (window set to %d,%d %dx%d)\n", i->currentPicture, picx, picy, picw, pich, i->win.x, i->win.y, i->win.width, i->win.height);
	} 
	else 
	{
		x  	          = i->cellX * 8;
		width 		  = i->cellW * 8;
		i->win.posX   = i->win.x;
		i->win.posY   = i->win.y;

		//fprintf(stderr, "Drawing floating picture %d: %d,%d %dx%d\n", i->currentPicture, i->cellX*8, i->win.y, i->cellW*8, i->win.height);
	}

	SCR_DisplayPicture(x, w->y, width, w->height, i->screenMode);
}

void DDB_FlushWindow (DDB_Interpreter* i, DDB_Window* w)
{
	int maxX = i->cellX*8 + i->cellW*8;
	bool forceGraphics = (w->flags & Win_ForceGraphics) != 0;
	
	if (w != &i->win)
	{
		uint8_t cellX, cellW;
		DDB_CalculateCells(i, w, &cellX, &cellW);
		maxX = cellX*8 + cellW*8;
	}

	if (w->posX >= w->x)
	{
		int wordWidth = 0;
		for (int n = 0; n < i->pendingPtr ; n++)
		{
			uint8_t ch = i->pending[n];
			uint8_t width = charWidth[ch];
			if (i->pending[n] == ' ')
				break;
			wordWidth += width;
		}
		if (w->posX + wordWidth >= maxX)
			DDB_NewLineAtWindow(i, w);
	}

	for (int n = 0; n < i->pendingPtr ; n++)
	{
		uint8_t ch = i->pending[n];
		if (ch < 16)
		{
			switch (ch)
			{
				case 0x0E:		// Graphics on ('\g')
					w->graphics = true; 
					break;

				case 0x0F:		// Graphics off ('\t')
					w->graphics = false; 
					break;
			}
			continue;
		}
		if (w->graphics || forceGraphics) ch |= 0x80;

		int width = charWidth[ch];
		if (w->posX + width > maxX)
		{
			if (ch == ' ' || ch == 0xA0) {
				w->posX = maxX;
				continue;
			}
			DDB_NewLineAtWindow(i, w);
		}
		SCR_DrawCharacter(w->posX, w->posY, ch, inkMap[w->ink], inkMap[w->paper]);
		w->posX += width;
	}
	i->pendingPtr = 0;

	i->keyChecked = false;
}

void DDB_Flush (DDB_Interpreter* i)
{
	DDB_FlushWindow(i, &i->win);
}

void DDB_ResetSmoothScrollFlags(DDB_Interpreter* i)
{
	i->win.smooth = 0;
	for (int n = 0; n < 8; n++)
		i->windef[n].smooth = false;
}

void DDB_ResetScrollCounts(DDB_Interpreter* i)
{
	i->win.scrollCount = 0;
	for (int n = 0; n < 8; n++)
		i->windef[n].scrollCount = 0;
}

static void OutputCharToWindow (DDB_Interpreter* i, DDB_Window* w, char c)
{
	switch(c)
	{
		case '\x0D':		// Newline ('\n') 
			DDB_FlushWindow(i, w);
			DDB_NewLineAtWindow(i, w);
			return;

		case '_':
		case '@':
		{
			int firstChar = 0;
			DDB_GetMessage(i->ddb, DDB_OBJNAME, i->flags[Flag_Objno], (char *)objNameBuffer, sizeof(objNameBuffer));
			if (!objNameBuffer[0])
				return;
			if (i->ddb->language == DDB_SPANISH)
			{
				if (objNameBuffer[1] == 'n') {
					if (objNameBuffer[0] == 'u' || objNameBuffer[0] == 'U') {
						if (objNameBuffer[3] == 's' && (objNameBuffer[2] == 'a' || objNameBuffer[2] == 'o')) {
							firstChar++;
						} else if (objNameBuffer[2] == 'a') {
							firstChar++;
							objNameBuffer[2] = 'a';
						} else {
							objNameBuffer[0] = 'e';
						}
						objNameBuffer[1] = 'l';
					}
				}
			}
			if (c == '@')
				objNameBuffer[firstChar] = ToUpper(objNameBuffer[firstChar]);
			else
				objNameBuffer[firstChar] = ToLower(objNameBuffer[firstChar]);
			for (uint8_t* ptr = objNameBuffer + firstChar; *ptr != 0; ptr++) {
				if (*ptr == '.') break;
				OutputCharToWindow(i, w, *ptr);
			}
			return;
		}

		case '\x0B':		// Clear screen ('\b')
			DDB_FlushWindow(i, w);
			DDB_ClearWindow(i, w);
			return;

		case '\x0C':		// Wait for a keypress ('\k')
			DDB_FlushWindow(i, w);
			SCR_WaitForKey();
			DDB_ResetScrollCounts(i);
			w->smooth = 1;
			return;
	}

	if (i->pendingPtr >= sizeof(i->pending)) {
		//fputs("Output buffer overflow\n", stderr);
		DDB_FlushWindow(i, w);
	}

	if (i->pendingPtr > 0 && i->pending[i->pendingPtr - 1] == ' ' && c != ' ')
		DDB_FlushWindow(i, w);

	i->pending[i->pendingPtr++] = c;
}

static void OutputChar (DDB_Interpreter* i, char c)
{
	OutputCharToWindow(i, &i->win, c);
}

static void OutputTextToWindow (DDB_Interpreter* i, const char* text, DDB_Window* w)
{
	while (*text != 0)
		OutputCharToWindow(i, w, (uint8_t)*text++);
}

void DDB_OutputText (DDB_Interpreter* i, const char* text)
{
	OutputTextToWindow(i, text, &i->win);
}

bool DDB_OutputMessageWin (DDB_Interpreter* i, DDB_MsgType type, uint8_t msgId, DDB_Window* w)
{
	DDB* ddb = i->ddb;

	uint16_t* table;
	uint8_t entries;

	switch (type)
	{
		case DDB_MSG:
			table = ddb->msgTable;
			entries = ddb->numMessages;
			break;
		case DDB_SYSMSG:
			table = ddb->sysMsgTable;
			entries = ddb->numSystemMessages;
			break;
		case DDB_OBJNAME:
			table = ddb->objNamTable;
			entries = ddb->numObjects;
			break;
		case DDB_LOCDESC:
			table = ddb->locDescTable;
			entries = ddb->numLocations;
			break;
		default:
			DDB_Warning("Invalid message type %d", type);
			return false;
	}

	if (msgId >= entries)
		return false;
	uint8_t* ptr = ddb->data + table[msgId];
	if (ptr <= ddb->data || ptr >= ddb->data + ddb->dataSize)
		return false;

	TRACE("%s%d: \"", DDB_MessageTypeNames[type], msgId);

	while (true)
	{
		uint8_t c = *ptr++ ^ 0xFF;
		if (c == 0x0A)
			break;
		if (c >= 128)
		{
			if (!ddb->hasTokens)
			{
				DDB_Warning("Message contains token 0x%02X but DDB has no tokens!", c);
				continue;
			}
			uint8_t* token = ddb->tokensPtr[c - 128];
			if (token == 0)
			{
				DDB_Warning("Message contains token 0x%02X but it's not defined in the DDB!", c);
				continue;
			}
			for (;;)
			{
				OutputCharToWindow(i, w, *token & 0x7F);
				TRACE(TranslateChar(*token & 0x7F));
				if (*token >= 128)
					break;
				token++;
			}
		}
		else
		{
			OutputCharToWindow(i, w, c);
			TRACE(TranslateChar(c));
		}
	}
	TRACE("\" ");
	return true;
}

bool DDB_OutputMessage (DDB_Interpreter* i, DDB_MsgType type, uint8_t index)
{
	return DDB_OutputMessageWin(i, type, index, &i->win);
}

void DDB_OutputInputPrompt(DDB_Interpreter* i)
{
	DDB_Window *iw = i->inputWindow == 0 || i->inputWindow == i->curwin ? &i->win : &i->windef[i->inputWindow];
	DDB_OutputMessageWin(i, DDB_SYSMSG, 33, iw);
}

static void PrintAt (DDB_Interpreter* i, DDB_Window* w, int line, int col)
{
	DDB_Flush(i);
	w->posY = w->y + line * lineHeight;
	w->posX = w->x + col * columnWidth;
	if (w->posX < w->x || w->posX > w->x + w->width ||
		w->posY < w->y || w->posY > w->y + w->height - lineHeight)
	{
		w->posX = w->x;
		w->posY = w->y;
	}
	w->scrollCount = 0;
	w->smooth = 0;
}

static void WinAt (DDB_Interpreter* i, int line, int col)
{
	DDB_Window* w = &i->windef[i->curwin];

	w->posY -= w->y;
	w->posX -= w->x;
	w->y = line * lineHeight;
	w->x = col * columnWidth;

	if (w->y >= screenHeight - lineHeight)
		w->y = screenHeight - lineHeight;
	if (w->x >= screenWidth - columnWidth)
		w->x = screenWidth - columnWidth;

	if (w->height + w->y > screenHeight)
		w->height = w->y >= screenHeight ? 0 : screenHeight - w->y;
	if (w->width + w->x > screenWidth)
		w->width = w->x >= screenWidth ? 0 : screenWidth - w->x;

	// Make sure printing position stays between limits
	// WARNING: this resets the scroll line counter
	PrintAt(i, w, w->posY, w->posX);

	i->win.posX = w->posX;
	i->win.posY = w->posY;
	i->win.width = w->width;
	i->win.height = w->height;
	i->win.x = w->x;
	i->win.y = w->y;

	DDB_CalculateCells(i, w, &i->cellX, &i->cellW);

	// fprintf(stderr, "Window %d repositioned: %d,%d %dx%d\n", i->curwin, w->x, w->y, w->width, w->height);
}

static void CenterWindow (DDB_Interpreter* i, DDB_Window* w)
{
	w->posX -= w->x;
	if (w->width >= screenWidth)
	{
		w->x = 0;
		w->width = screenWidth;
	}
	else 
	{
		w->x = (screenWidth - w->width) / 2;
		w->x -= w->x % columnWidth;
	}
	w->posX += w->x;

	DDB_CalculateCells(i, w, &i->cellX, &i->cellW);
}

static void WinSize (DDB_Interpreter* i, int lines, int columns)
{
	DDB_Window* w = &i->windef[i->curwin];
	
	columns++;

	w->height = lines * lineHeight;
	w->width = columns * columnWidth;
	if (w->height + w->y > screenHeight)
		w->height = w->y >= screenHeight ? 0 : screenHeight - w->y;
	if (w->width + w->x > screenWidth)
		w->width = w->x >= screenWidth ? 0 : screenWidth - w->x;

	// Make sure printing position stays between limits
	// WARNING: this resets the scroll line counter
	PrintAt(i, w, w->posY, w->posX);

	i->win.posX = w->posX;
	i->win.posY = w->posY;
	i->win.width = w->width;
	i->win.height = w->height;
	i->win.x = w->x;
	i->win.y = w->y;

	DDB_CalculateCells(i, w, &i->cellX, &i->cellW);

	// fprintf(stderr, "Window %d resized: %d,%d %dx%d\n", i->curwin, w->x, w->y, w->width, w->height);
}

static int CountObjectsAt (DDB_Interpreter* i, uint8_t locno)
{
	int count = 0;
	if (locno == 255)
		locno = i->flags[Flag_Locno];
	for (int n = 0; n < i->ddb->numObjects; n++)
	{
		if (i->objloc[n] == locno)
			count++;
	}
	return count;
}

static void ListObjectsAt (DDB_Interpreter* i, int locno)
{
	if (locno == 255) locno = i->flags[Flag_Locno];

	int count = CountObjectsAt(i, locno);
	
	i->flags[Flag_ListFlags] &= ~0x80;
	for (int n = 0; n < i->ddb->numObjects; n++)
	{
		if (i->objloc[n] == locno)
		{
			i->flags[Flag_ListFlags] |= ~0x80;
			DDB_GetMessage(i->ddb, DDB_OBJNAME, n, (char *)objNameBuffer, sizeof(objNameBuffer));
			if (i->flags[Flag_ListFlags] & 0x40)		// Continuous listing
			{
				objNameBuffer[0] = ToLower(objNameBuffer[0]);
				for (const char* ptr = (const char*)objNameBuffer; *ptr != 0 && *ptr != '.'; ptr++)
					OutputCharToWindow(i, &i->win, (uint8_t)*ptr);
				if (count == 1)
					DDB_OutputMessage(i, DDB_SYSMSG, 48);		// .
				else if (count == 2)
					DDB_OutputMessage(i, DDB_SYSMSG, 47);		// and
				else
					DDB_OutputMessage(i, DDB_SYSMSG, 46);		// ,
			}
			else
			{
				DDB_OutputText(i, (const char *)objNameBuffer);
				OutputChar(i, '\r');
			}
			count--;
		}
	}
}

static void SetObjno (DDB_Interpreter* i, uint8_t objno)
{
	i->flags[Flag_Objno] = objno;
	i->flags[Flag_ObjLocno] = 0;
	i->flags[Flag_ObjWeight] = 0;
	i->flags[Flag_ObjContainer] = 0;
	i->flags[Flag_ObjWearable] = 0;

	if (i->ddb->version > 1)
	{
		i->flags[Flag_ObjAttributes] = 0;
		i->flags[Flag_ObjAttributes + 1] = 0;
	}

	if (objno != 255 && objno < i->ddb->numObjects)
	{
		uint8_t attr = i->ddb->objAttrTable[objno];
		i->flags[Flag_ObjLocno] = i->objloc[objno];
		i->flags[Flag_ObjWeight] = attr & Obj_Weight;
		i->flags[Flag_ObjContainer] = (attr & Obj_Container) ? 128 : 0;
		i->flags[Flag_ObjWearable] = (attr & Obj_Wearable) ? 128 : 0;
		if (attr & Obj_Container)
			i->flags[Flag_ObjWeight] = CalculateWeight(i, objno, 0);
		if (i->ddb->objExAttrTable)
		{
			uint16_t ex = i->ddb->objExAttrTable[objno];
			i->flags[Flag_ObjAttributes] = ex & 0xFF;
			i->flags[Flag_ObjAttributes + 1] = ex >> 8;
		}
	}
}

static uint8_t WhatoAt (DDB_Interpreter* i, uint8_t locno)
{
	uint8_t objno = 255;
	
	for (int n = 0; n < i->ddb->numObjects; n++)
	{
		uint8_t noun = i->ddb->objWordsTable[n * 2];
		uint8_t adjective = i->ddb->objWordsTable[n * 2 + 1];

		if (noun == 255) 
			continue;
		if (i->flags[Flag_Noun1] == noun && (i->flags[Flag_Adjective1] == 255 || i->flags[Flag_Adjective1] == adjective))
		{
			if (locno == 255 || i->objloc[n] == locno)
				objno = n;
		}
	}

	// Enhancement: allow using a single adjective to refer to an object
	// but only if no unknown words where present in the sentence.
	
	if (objno == 255 && locno != 255 && 
		i->flags[Flag_Noun1] == 255  && 
		i->flags[Flag_Adjective1] != 255 && 
		!(i->sentenceFlags & SentenceFlag_UnknownWord))
	{
		for (int n = 0; n < i->ddb->numObjects; n++)
		{
			uint8_t noun = i->ddb->objWordsTable[n * 2];
			uint8_t adjective = i->ddb->objWordsTable[n * 2 + 1];

			if (i->objloc[n] == locno && i->flags[Flag_Adjective1] == adjective)
			{
				i->flags[Flag_Noun1] = noun;
				objno = n;
				break;
			}
		}
	}

	return objno;
}

static uint8_t Whato (DDB_Interpreter* i)
{
	int objno = WhatoAt(i, Loc_Carried);
	if (objno == 255)
		objno = WhatoAt(i, Loc_Worn);
	if (objno == 255)
		objno = WhatoAt(i, i->flags[Flag_Locno]);
	if (objno == 255)
		objno = WhatoAt(i, 255);
	return objno;
}

static bool DoAll (DDB_Interpreter* i, uint8_t locno, bool start)
{
	int n = i->flags[Flag_DoAllObjNo] + 1;

	if (start)
	{
		i->doallDepth = 0;
		i->doallLocno = locno;
		n = 0;
	}

	for (; n < i->ddb->numObjects; n++)
	{
		if (i->objloc[n] == locno)
		{
			uint8_t noun = i->ddb->objWordsTable[2*n];
			uint8_t adjective = i->ddb->objWordsTable[2 * n + 1];
			if (i->flags[Flag_Noun2] != 255 && i->flags[Flag_Noun2] == noun)
			{
				if (i->flags[Flag_Adjective2] == 255 || i->flags[Flag_Adjective2] != adjective)
					continue;
			}
			i->flags[Flag_Noun1] = i->ddb->objWordsTable[2*n];
			i->flags[Flag_Adjective1] = i->ddb->objWordsTable[2*n + 1];
			i->flags[Flag_DoAllObjNo] = n;

			// TODO: Verify this step, since it is not documented
			SetObjno(i, n);

			i->doall = true;
			return true;
		}
	}
	i->doall = false;
	i->flags[Flag_DoAllObjNo] = 255;
	return false;
}

static bool Present(DDB_Interpreter* i, uint8_t objno)
{
	return objno < i->ddb->numObjects && (
		i->objloc[objno] == i->flags[Flag_Locno] ||
		i->objloc[objno] == Loc_Worn ||
		i->objloc[objno] == Loc_Carried);
}

static bool Absent(DDB_Interpreter* i, uint8_t objno)
{
	return objno >= i->ddb->numObjects || (
		i->objloc[objno] != i->flags[Flag_Locno] &&
		i->objloc[objno] != Loc_Carried &&
		i->objloc[objno] != Loc_Worn);
}

void DDB_Desc (DDB_Interpreter* i, uint8_t locno)
{
	if (i->ddb->version == 2)
	{
		DDB_OutputMessage(i, DDB_LOCDESC, locno == 255 ? i->flags[Flag_Locno] : locno);	
		return;
	}

	if (i->flags[2] > 0)
		i->flags[2]--;
	if (i->flags[3] > 0 && i->flags[Flag_Darkness] != 0)
		i->flags[3]--;
		
	if (i->flags[Flag_Darkness] != 0 && Absent(i, 0))
	{
		if (i->ddb->version == 1 && i->flags[4] > 0)
			i->flags[4]--;


		if (i->ddb->version == 1)
		{
			if ((i->flags[Flag_GraphicFlags] & Graphics_NoClsBeforeDesc) == 0)
			{
				DDB_SetWindow(i, 1);
				DDB_ClearWindow(i, &i->win);
			}
		}
		DDB_SetWindow(i, 1);
		DDB_OutputMessage(i, DDB_SYSMSG, 0);
	}
	else
	{
		if (SCR_PictureExists(i->flags[Flag_Locno]))
		{
			DDB_SetWindow(i, 0);
			BufferPicture(i, i->flags[Flag_Locno]);
			DrawBufferedPicture(i);
		}
		DDB_SetWindow(i, 1);
		if ((i->flags[Flag_GraphicFlags] & Graphics_NoClsBeforeDesc) == 0)
		{
			DDB_Flush(i);
			DDB_ClearWindow(i, &i->win);
		}

		DDB_OutputMessage(i, DDB_LOCDESC, locno == 255 ? i->flags[Flag_Locno] : locno);	
	}
	
	i->procstack[0].entry   = 0;
	i->procstack[0].process = 1;
	i->procstack[0].offset  = 0;
	i->procstackptr = 0;
	i->doall = false;
	i->oldMainLoopState = FLOW_DESC;
	i->state = DDB_RUNNING;
}

void DDB_NewText(DDB_Interpreter* i)
{
	i->inputBufferPtr = 0;
	i->inputBuffer[0] = 0;
	i->inputBufferLength = 0;
	i->inputCompletionX = 0;
}

void DDB_SetWindow(DDB_Interpreter* i, int winno)
{
	i->windef[i->curwin] = i->win;
	i->curwin = winno & 7;
	i->win = i->windef[i->curwin];
	i->flags[Flag_Window] = winno;

	DDB_CalculateCells(i, &i->win, &i->cellX, &i->cellW);
}

static void UpdatePos (DDB_Interpreter* i, int process, int entry, int offset)
{
	i->procstack[i->procstackptr].process = process;
	i->procstack[i->procstackptr].entry = entry;
	i->procstack[i->procstackptr].offset = offset;
}

static bool MovePlayer (DDB_Interpreter* i, uint8_t flag)
{
	uint8_t locno = i->flags[Flag_Locno];
	if (locno >= i->ddb->numLocations)
		return false;
	uint16_t offset = i->ddb->connections[locno];
	if (offset == 0)
		return false;
	uint8_t* ptr = i->ddb->data + offset;
	uint8_t* end = i->ddb->data + i->ddb->dataSize;
	while (*ptr != 0xFF && ptr < end)
	{
		if (ptr[0] == i->flags[Flag_Verb])
		{
			i->flags[flag] = ptr[1];
			return true;
		}
		ptr += 2;
	}
	return false;
}

static bool FindWord (DDB_Interpreter* i, const uint8_t** textPointer, const uint8_t* end, uint8_t* type, uint8_t* code)
{
	uint8_t* word = i->ddb->vocabulary;
	const uint8_t* ptr = *textPointer;

	// TODO: Sort vocabulary in memory and use binary search

	while (*word != 0)
	{
		bool found = true;
		for (int n = 0; n < 5; n++)
		{
			uint8_t ch = word[n] ^ 0xFF;
			uint8_t cp = ptr + n < end ? ptr[n] : ' ';
			if (ch != cp)
			{
				if (ch == ToUpper(cp))
					continue;
				if (ch != ' ' || IsAlphaNumeric(ptr[n]))
					found = false;
				break;
			}
			if (ch == ' ')
			{
				found = true;
				break;
			}
		}
		if (found)
		{
			while (ptr < end && IsAlphaNumeric(*ptr)) 
				ptr++;
			*textPointer = ptr;
			*type = word[6];
			*code = word[5];
			return true;
		}
		word += 7;
	}
	return false;
}

static bool EndsWithPronoun (const char* word, int len)
{
	if (len < 2)
		return false;
	if (ToUpper(word[len-2]) == 'L' && (ToUpper(word[len-1]) == 'A' || ToUpper(word[len-1]) == 'O'))
		return true;
	if (len < 3 || ToUpper(word[len-1]) != 'S')
		return false;
	if (ToUpper(word[len-3]) == 'L' && (ToUpper(word[len-2]) == 'A' || ToUpper(word[len-2]) == 'O'))	
		return true;
	return false;
}

static bool Parse (DDB_Interpreter* i, bool quoted)
{
	const uint8_t* ptr;
	const uint8_t* end;
	uint8_t code;
	uint8_t type;
	uint8_t previousVerb = 255;
	int wordsFound = 0;

	if (quoted && i->quotedString) {
		ptr = i->quotedString;
		end = i->quotedString + i->quotedStringLength;
	} else if (quoted && i->inputBufferPtr >= i->inputBufferLength) {
		return false;
	} else {
		ptr = i->inputBuffer + i->inputBufferPtr;
		end = i->inputBuffer + i->inputBufferLength;
		quoted = false;
	}

	if (i->inputBufferPtr != 0)
		previousVerb = i->flags[Flag_Verb];

	i->flags[Flag_Verb]        = 255;
	i->flags[Flag_Noun1]       = 255;
	i->flags[Flag_Noun2]       = 255;
	i->flags[Flag_Adjective1]  = 255;
	i->flags[Flag_Adjective2]  = 255;
	i->flags[Flag_Preposition] = 255;
	i->flags[Flag_Adverb]      = 255;
	i->flags[Flag_Preposition] = 255;
	i->sentenceFlags           = 0;

	while (ptr < end)
	{
		const uint8_t* word;

		if (*ptr == ' ' || *ptr == '\t')
		{
			ptr++;
			continue;
		}
		if (!quoted && !i->quotedString && (*ptr == '"' || *ptr == '\'')) {
			char quote = *ptr++;
			i->quotedString = (uint8_t*)ptr;
			while (ptr < end && *ptr != quote)
				ptr++;
			i->quotedStringLength = ptr - i->quotedString;
			if (*ptr == quote) ptr++;
			continue;
		}
		if (!IsAlphaNumeric(*ptr) && !IsDelimiter(*ptr))
		{
			ptr++;
			continue;
		}
		if (IsDelimiter(*ptr))
		{
			if (*ptr == ':')
				i->sentenceFlags |= SentenceFlag_Colon;
			else if (*ptr == '?')
				i->sentenceFlags |= SentenceFlag_Question;
			ptr++;
			if (wordsFound == 0)
				continue;
			break;
		}

		word = ptr;
		if (FindWord(i, &ptr, end, &type, &code))
		{
			if (type == WordType_Conjunction)
			{
				if (wordsFound == 0)
					continue;
				break;
			}

			wordsFound++;

			// TODO: Check phrase structure, following the documentation

			switch (type)
			{
				case WordType_Verb:
					if (i->flags[Flag_Verb] == 255)	
					{
						i->flags[Flag_Verb] = code;
						if (EndsWithPronoun((const char*)word, ptr - word) && i->flags[Flag_Noun1] == 255 && i->flags[Flag_CPNoun] != 255)
						{
							i->flags[Flag_Noun1] = i->flags[Flag_CPNoun];
							i->flags[Flag_Adjective1] = i->flags[Flag_CPAdjective];
						}
					}
					break;
				case WordType_Noun:
					if (i->flags[Flag_Noun1] == 255)
						i->flags[Flag_Noun1] = code;
					else if (i->flags[Flag_Noun2] == 255)
						i->flags[Flag_Noun2] = code;
					break;
				case WordType_Adjective:
					if (i->flags[Flag_Adjective1] == 255 && i->flags[Flag_Noun2] == 255)
						i->flags[Flag_Adjective1] = code;
					else if (i->flags[Flag_Adjective2] == 255)
						i->flags[Flag_Adjective2] = code;
					break;
				case WordType_Preposition:
					if (i->flags[Flag_Preposition] == 255)
						i->flags[Flag_Preposition] = code;
					break;
				case WordType_Adverb:
					if (i->flags[Flag_Adverb] == 255)
						i->flags[Flag_Adverb] = code;
					break;
				default:
					break;
			}
		}
		else
		{
			i->sentenceFlags |= SentenceFlag_UnknownWord;
			while(ptr < end && IsAlphaNumeric(*ptr)) 
				ptr++;
			continue;
		}
	}

	if (quoted) {
		i->quotedString = 0;
		i->quotedStringLength = 0;
	} else {
		i->inputBufferPtr = ptr - i->inputBuffer;
	}

	if (i->flags[Flag_Verb] == 255 && i->flags[Flag_Noun1] < 40)
		i->flags[Flag_Verb] = i->flags[Flag_Noun1];
	else if (i->flags[Flag_Verb] == 255 && previousVerb)
		i->flags[Flag_Verb] = previousVerb;

	if (i->flags[Flag_Noun1] != 255 && i->flags[Flag_Noun1] >= 50)
	{
		i->flags[Flag_CPNoun] = i->flags[Flag_Noun1];
		i->flags[Flag_CPAdjective] = i->flags[Flag_Adjective1];
	}
	else if (i->flags[Flag_Noun2] != 255 && i->flags[Flag_Noun2] >= 50)
	{
		i->flags[Flag_CPNoun] = i->flags[Flag_Noun2];
		i->flags[Flag_CPAdjective] = i->flags[Flag_Adjective2];
	}

	return wordsFound > 0;
}

static struct
{
	int x;
	int y;
	int width;
	int height;
	bool hasOutput;
}
windowClears[16];
static int windowClearCount = 0;
static const int maxWindowClears = sizeof(windowClears) / sizeof(windowClears[0]);

static void MarkWindowOutput()
{
	if (windowClearCount > 0)
		windowClears[windowClearCount - 1].hasOutput = true;
}

static bool AnyWindowOverlapsCurrent (DDB_Interpreter* i)
{
	for (int n = 0; n < windowClearCount; n++)
	{
		if (   windowClears[n].x < i->win.x + i->win.width       
			&& windowClears[n].x + windowClears[n].width > i->win.x 
			&& windowClears[n].y < i->win.y + i->win.height      
			&& windowClears[n].y + windowClears[n].height > i->win.y
			&& windowClears[n].hasOutput)
		{
			// fprintf(stderr, "Window %d (%d,%d %dx%d) overlaps previously cleared window %d (%d,%d %dx%d)\n", i->curwin,
			// 	i->win.x, i->win.y, i->win.width, i->win.height,
			// 	n, windowClears[n].x, windowClears[n].y, windowClears[n].width, windowClears[n].height);
			return true;
		}
	}
	if (windowClearCount < maxWindowClears)
	{
		windowClears[windowClearCount].x = i->win.x;
		windowClears[windowClearCount].y = i->win.y;
		windowClears[windowClearCount].width = i->win.width;
		windowClears[windowClearCount].height = i->win.height;
		windowClears[windowClearCount].hasOutput = i->win.paper != 0;
		windowClearCount++;
	}
	return false;
}

// --------------------
//   Public functions
// --------------------

void DDB_Step (DDB_Interpreter* i, int stepCount)
{
	char output[16];
	uint16_t paletteChanges = 0;
	bool repeatingDisplay = false;

	uint8_t  process  = i->procstack[i->procstackptr].process;
	uint16_t entry    = i->procstack[i->procstackptr].entry;
	uint16_t offset   = i->procstack[i->procstackptr].offset;
	uint8_t* entryPtr = i->ddb->data + i->ddb->processTable[process] + entry * 4;
	uint8_t* code     = i->ddb->data + *(uint16_t *)(entryPtr + 2) + offset;

	uint8_t  value, locno;

	if (i->state == DDB_PAUSED)
		i->state = DDB_RUNNING;
	else if (i->state != DDB_RUNNING)
		return;

	windowClearCount = 0;

	while (stepCount-- > 0)
	{
		if (offset == 0)
		{
			if (entry == 0)
			{
				TRACE("\nEntering process %d\n\n", process);
			}
			if (*entryPtr == 0)
			{
				if (i->doall)
				{
					if (i->doallDepth > 0)
						i->doallDepth--;
					else if (DoAll(i, i->doallLocno, false))
					{
						TRACE("Performing next DoAll: process %d, entry %d, offset %d\n", i->doallProcess, i->doallEntry, i->doallOffset);
						entry    = i->doallEntry;
						offset   = i->doallOffset;
						process  = i->doallProcess;
						entryPtr = i->ddb->data + i->ddb->processTable[process] + entry * 4;
						code     = i->ddb->data + *(uint16_t *)(entryPtr + 2) + offset;
						continue;						
					}
					else
					{
						TRACE("DoAll finished\n");
					}
				}
				if (i->procstackptr == 0)
				{
					DDB_Flush(i);
					i->state = DDB_FINISHED;
					return;
				}
				
				TRACE("\nLeaving process %d\n\n", process);

				i->procstackptr--;
				process  = i->procstack[i->procstackptr].process;
				entry    = i->procstack[i->procstackptr].entry;
				offset   = i->procstack[i->procstackptr].offset;
				entryPtr = i->ddb->data + i->ddb->processTable[process] + entry * 4;
				code     = i->ddb->data + *(uint16_t *)(entryPtr + 2) + offset;

				TRACE("Resuming process %d, entry %d, offset %d\n\n", process, entry, offset);
				continue;
			}
			else
			{
				uint8_t verb = entryPtr[0];
				uint8_t noun = entryPtr[1];

				if ((verb != 255 && verb != i->flags[Flag_Verb]) ||
				    (noun != 255 && noun != i->flags[Flag_Noun1]))
				{
					entry++;
					offset = 0;
					entryPtr += 4;
					continue;
				}
				else
				{
					code = i->ddb->data + *(uint16_t *)(entryPtr + 2);
				}

				if (verb == 255)
					TRACE("_    ");
				else
					TraceVocabularyWord(i->ddb, WordType_Verb, verb);
				TRACE("       ");
				if (noun == 255)
					TRACE("_    ");
				else
					TraceVocabularyWord(i->ddb, WordType_Noun, noun);
				TRACE("       ");
			}
		}
		else
		{
			TRACE("%-24s", "");
		}

		if (*code == 0xFF) // End of entry
		{
			entry++;
			offset = 0;
			entryPtr += 4;
			TRACE("\n");
			continue;
		}

		uint8_t condactIndex = *code & 0x7F;
		uint8_t condact = i->ddb->condactMap[condactIndex].condact;
		uint8_t params = i->ddb->condactMap[condactIndex].parameters;
		uint8_t param0 = params > 0 ? code[1] : 0;
		uint8_t param1 = params > 1 ? code[2] : 0;
		if (*code & 0x80)
			param0 = i->flags[param0];

		#if TRACE_ON
		TRACE("%-12s", DDB_GetCondactName((DDB_Condact)condact));
		if (params > 0)
		{
			if (*code & 0x80)
				TRACE("[%d]%s", code[1], code[1] < 10 ? "  " : code[1] < 100 ? " " : "");
			else
				TRACE("%-5d", param0);
			if (params > 1)
				TRACE(" %-3d", param1);
			else
				TRACE("    ");
		}
		else
		{
			TRACE("         ");
		}
		if (*code & 0x80)
			TRACE("    | ([%d] = %d)  ", code[1], i->flags[code[1]]);
		else
			TRACE("    | ");
		#endif

		bool finished  = false;
		bool ok = true;				// If !ok, jump to next entry

		switch (condact)
		{
			// Generic conditions

			case CONDACT_AT:
				ok = i->flags[Flag_Locno] == param0;
				TRACE("%sPlayer is at %d", ok ? "":"[Failed] ", i->flags[Flag_Locno]);
				break;
			case CONDACT_NOTAT:
				ok = i->flags[Flag_Locno] != param0;
				TRACE("%sPlayer is at %d", ok ? "":"[Failed] ", i->flags[Flag_Locno]);
				break;
			case CONDACT_ATGT:
				ok = i->flags[Flag_Locno] > param0;
				TRACE("%sPlayer is at %d", ok ? "":"[Failed] ", i->flags[Flag_Locno]);
				break;
			case CONDACT_ATLT:
				ok = i->flags[Flag_Locno] < param0;
				TRACE("%sPlayer is at %d", ok ? "":"[Failed] ", i->flags[Flag_Locno]);
				break;
			case CONDACT_PRESENT:
				ok = Present(i, param0);
				TRACE("%sObj#%d \"%s\" is in %d", ok ? "":"[Failed] ", param0, DDB_GetDebugMessage(i->ddb, DDB_OBJNAME, param0), i->objloc[param0]);
				break;
			case CONDACT_ABSENT:
				ok = Absent(i, param0);
				TRACE("%sObj#%d \"%s\" is in %d", ok ? "":"[Failed] ", param0, DDB_GetDebugMessage(i->ddb, DDB_OBJNAME, param0), i->objloc[param0]);
				break;
			case CONDACT_WORN:
				ok = param0 < i->ddb->numObjects && i->objloc[param0] == Loc_Worn;
				TRACE("%sObj#%d \"%s\" is in %d", ok ? "":"[Failed] ", param0, DDB_GetDebugMessage(i->ddb, DDB_OBJNAME, param0), i->objloc[param0]);
				break;
			case CONDACT_NOTWORN:
				ok = param0 < i->ddb->numObjects && i->objloc[param0] != Loc_Worn;
				TRACE("%sObj#%d \"%s\" is in %d", ok ? "":"[Failed] ", param0, DDB_GetDebugMessage(i->ddb, DDB_OBJNAME, param0), i->objloc[param0]);
				break;
			case CONDACT_CARRIED:
				ok = param0 < i->ddb->numObjects && i->objloc[param0] == Loc_Carried;
				TRACE("%sObj#%d \"%s\" is in %d", ok ? "":"[Failed] ", param0, DDB_GetDebugMessage(i->ddb, DDB_OBJNAME, param0), i->objloc[param0]);
				break;
			case CONDACT_NOTCARR:
				ok = param0 < i->ddb->numObjects && i->objloc[param0] != Loc_Carried;
				TRACE("%sObj#%d \"%s\" is in %d", ok ? "":"[Failed] ", param0, DDB_GetDebugMessage(i->ddb, DDB_OBJNAME, param0), i->objloc[param0]);
				break;
			case CONDACT_ISAT:
				if (param1 == 255) param1 = i->flags[Flag_Locno];
				ok = param0 < i->ddb->numObjects && i->objloc[param0] == param1;
				TRACE("%sObj#%d \"%s\" is in %d", ok ? "":"[Failed] ", param0, DDB_GetDebugMessage(i->ddb, DDB_OBJNAME, param0), i->objloc[param0]);
				break;
			case CONDACT_ISNOTAT:
				if (param1 == 255) param1 = i->flags[Flag_Locno];
				ok = param0 < i->ddb->numObjects && i->objloc[param0] != param1;
				TRACE("%sObj#%d \"%s\" is in %d", ok ? "":"[Failed] ", param0, DDB_GetDebugMessage(i->ddb, DDB_OBJNAME, param0), i->objloc[param0]);
				break;
			case CONDACT_HASAT:
			case CONDACT_HASNAT:
			{
				int n = 59 - param0/8;
				int m = (1 << (param0 & 7));
				ok = (i->flags[n] & m) != 0;
				if (condact == CONDACT_HASNAT)
					ok = !ok;
				TRACE("%sFlag %d = %d ($%02X mask $%02X)", ok ? "":"[Failed] ", n, i->flags[n], i->flags[n], m);
				break;
			}
			case CONDACT_ZERO:
				ok = i->flags[param0] == 0;
				TRACE("%sFlag %d = %d", ok ? "":"[Failed] ", param0, i->flags[param0]);
				break;
			case CONDACT_NOTZERO:
				ok = i->flags[param0] != 0;
				TRACE("%sFlag %d = %d", ok ? "":"[Failed] ", param0, i->flags[param0]);
				break;
			case CONDACT_EQ:
				ok = i->flags[param0] == param1;
				TRACE("%sFlag %d = %d", ok ? "":"[Failed] ", param0, i->flags[param0]);
				break;
			case CONDACT_NOTEQ:
				ok = i->flags[param0] != param1;
				TRACE("%sFlag %d = %d", ok ? "":"[Failed] ", param0, i->flags[param0]);
				break;
			case CONDACT_GT:
				ok = i->flags[param0] > param1;
				TRACE("%sFlag %d = %d", ok ? "":"[Failed] ", param0, i->flags[param0]);
				break;
			case CONDACT_LT:
				ok = i->flags[param0] < param1;
				TRACE("%sFlag %d = %d", ok ? "":"[Failed] ", param0, i->flags[param0]);
				break;
			case CONDACT_SAME:
				ok = i->flags[param0] == i->flags[param1];
				TRACE("%sFlag %d = %d, flag %d = %d", ok ? "":"[Failed] ", param0, i->flags[param0], param1, i->flags[param1]);
				break;
			case CONDACT_NOTSAME:
				ok = i->flags[param0] != i->flags[param1];
				TRACE("%sFlag %d = %d, flag %d = %d", ok ? "":"[Failed] ", param0, i->flags[param0], param1, i->flags[param1]);
				break;
			case CONDACT_BIGGER:
				ok = i->flags[param0] > i->flags[param1];
				TRACE("%sFlag %d = %d, flag %d = %d", ok ? "":"[Failed] ", param0, i->flags[param0], param1, i->flags[param1]);
				break;
			case CONDACT_SMALLER:
				ok = i->flags[param0] < i->flags[param1];
				TRACE("%sFlag %d = %d, flag %d = %d", ok ? "":"[Failed] ", param0, i->flags[param0], param1, i->flags[param1]);
				break;
			case CONDACT_ADJECT1:
				ok = i->flags[Flag_Adjective1] == param0;
				TRACE("%sAdjective1 = %d", ok ? "":"[Failed] ", param0, i->flags[Flag_Adjective1]);
				break;
			case CONDACT_ADJECT2:
				ok = i->flags[Flag_Adjective2] == param0;
				TRACE("%sAdjective2 = %d", ok ? "":"[Failed] ", param0, i->flags[Flag_Adjective2]);
				break;
			case CONDACT_NOUN2:
				ok = i->flags[Flag_Noun2] == param0;
				TRACE("%sNoun2 = %d", ok ? "":"[Failed] ", param0, i->flags[Flag_Noun2]);
				break;
			case CONDACT_ADVERB:
				ok = i->flags[Flag_Adverb] == param0;
				TRACE("%sAdverb = %d", ok ? "":"[Failed] ", param0, i->flags[Flag_Adverb]);
				break;
			case CONDACT_PREP:
				ok = i->flags[Flag_Preposition] == param0;
				TRACE("%sPreposition = %d", ok ? "":"[Failed] ", param0, i->flags[Flag_Preposition]);
				break;
			case CONDACT_ISDONE:
				ok = i->done;
				TRACE("%s", ok ? "":"[Failed] ");
				break;
			case CONDACT_ISNDONE:
				ok = !i->done;
				TRACE("%s", ok ? "":"[Failed] ");
				break;

			// Parser

			case CONDACT_NEWTEXT:
				DDB_NewText(i);
				break;

			// Text output

			case CONDACT_DESC:
				UpdatePos(i, process, entry, offset + params + 1);
				if (params == 0)
					param0 = i->flags[Flag_Locno];
				DDB_Desc(i, param0);
				if (i->ddb->oldMainLoop)
				{
					TRACE("\n");
					return;
				}
				break;
			case CONDACT_MES:
				MarkWindowOutput();
				DDB_OutputMessage(i, DDB_MSG, param0);
				i->done = true;
				break;
			case CONDACT_SYSMESS:
				MarkWindowOutput();
				DDB_OutputMessage(i, DDB_SYSMSG, param0);
				i->done = true;
				break;
			case CONDACT_MESSAGE:
				MarkWindowOutput();
				DDB_OutputMessage(i, DDB_MSG, param0);
				DDB_Flush(i);
				DDB_NewLine(i);
				i->done = true;
				break;
			case CONDACT_TAB:
				DDB_Flush(i);
				i->win.posX = i->win.x + param0 * columnWidth;
				if (i->win.posX > i->win.x + i->win.width - columnWidth) {
					i->win.posX = i->win.x + i->win.width - columnWidth;
				}
				i->done = true;
				break;
			case CONDACT_SPACE:
				DDB_OutputText(i, " ");
				i->done = true;
				break;
			case CONDACT_NEWLINE:
				DDB_Flush(i);
				DDB_NewLine(i);
				i->done = true;
				break;
			case CONDACT_PRINT:
				LongToChar(i->flags[param0], output, 10);
				DDB_OutputText(i, output);
				break;			
			case CONDACT_DPRINT:
				LongToChar(i->flags[param0] + 256*i->flags[param0+1], output, 10);
				DDB_OutputText(i, output);
				i->done = true;
				TRACE("%d", i->flags[param0] + 256*i->flags[param0+1]);
				break;
			case CONDACT_PRINTAT:
				PrintAt(i, &i->win, param0, param1);
				i->done = true;
				break;
			case CONDACT_LISTOBJ:
				i->flags[Flag_ListFlags] &= ~0x80;
				if (CountObjectsAt(i, i->flags[Flag_Locno]) == 0)
					break;
				param0 = i->flags[Flag_Locno];
				DDB_OutputMessage(i, DDB_SYSMSG, 1);
				// Fall through
			case CONDACT_LISTAT:
			{
				if (param0 == 255) param0 = i->flags[Flag_Locno];
				int count = CountObjectsAt(i, param0);
				if (count == 0)
					DDB_OutputMessage(i, DDB_SYSMSG, 53);	// Nothing.
				else
					ListObjectsAt(i, param0);
				i->done = true;
				break;
			}
			case CONDACT_INPUT:
				i->inputWindow = param0;
				i->inputFlags = param1;
				break;
			case CONDACT_END:
			{
				DDB_Window* iw = DDB_GetInputWindow(i);
				DDB_Flush(i);
				DDB_OutputMessageWin(i, DDB_SYSMSG, 13, iw);
				DDB_FlushWindow(i, iw);
				DDB_StartInput(i);
				i->state = DDB_INPUT_END;
				DDB_PrintInputLine(i, true);
				TRACE("\n");
				return;
			}
			case CONDACT_QUIT:
			{
				DDB_Window* iw = DDB_GetInputWindow(i);
				DDB_Flush(i);
				DDB_OutputMessageWin(i, DDB_SYSMSG, 12, iw);
				DDB_FlushWindow(i, iw);
				DDB_StartInput(i);
				i->state = DDB_INPUT_QUIT;
				DDB_PrintInputLine(i, true);
				UpdatePos(i, process, entry, offset + params + 1);
				TRACE("\n");
				return;
			}

			// Flow

			case CONDACT_EXIT:
				if (param0 == 0)
				{
					i->state = DDB_QUIT;
					UpdatePos(i, process, entry, offset);
					TRACE("\n");
					return;
				}
				DDB_Reset(i);
				process = 0;
				entry = 0;
				offset = 0;
				entryPtr = i->ddb->data + i->ddb->processTable[process];
				code = i->ddb->data + *(uint16_t *)(entryPtr + 2);
				TRACE("\n");
				continue;

			case CONDACT_PROCESS:
				if (i->procstackptr == MAX_PROC_STACK - 1)
				{
					//fputs("\nMaximum process stack depth reached!\n", stderr);
					i->state = DDB_FATAL_ERROR;
					TRACE("\n");
					return;
				}
				TRACE("\n\nSaving state at process %d, entry %d, offset %d at stack %d\n", process, entry, offset, i->procstackptr);
				UpdatePos(i, process, entry, offset + params + 1);
				i->procstackptr++;
				process = param0;
				entry = 0;
				offset = 0;
				entryPtr = i->ddb->data + i->ddb->processTable[process];
				code = i->ddb->data + *(uint16_t *)(entryPtr + 2);
				if (i->doall) 
					i->doallDepth++;
				i->done = false;
				TRACE("\n");
				continue;

			case CONDACT_NOTDONE:
			case_NOTDONE:
				condact = CONDACT_NOTDONE;
				// Fall through
			case CONDACT_OK:
			case CONDACT_DONE:
			case_DONE:
				if (condact == CONDACT_OK)
				{
					DDB_OutputMessage(i, DDB_SYSMSG, 15);
					condact = CONDACT_DONE;
				}
				i->done = condact != CONDACT_NOTDONE;
				if (i->doall)
				{
					if (i->doallDepth > 0)
						i->doallDepth--;
					else if (DoAll(i, i->doallLocno, false))
					{
						TRACE("\nPerforming next DoAll: process %d, entry %d, offset %d\n", i->doallProcess, i->doallEntry, i->doallOffset);						entry    = i->doallEntry;
						offset   = i->doallOffset;
						process  = i->doallProcess;
						entryPtr = i->ddb->data + i->ddb->processTable[process] + entry * 4;
						code     = i->ddb->data + *(uint16_t *)(entryPtr + 2) + offset;
						continue;						
					}
					else
					{
						TRACE("\nDoAll finished\n");
					}
				}
				if (i->procstackptr == 0)
				{
					DDB_Flush(i);
					i->state = DDB_FINISHED;
					TRACE("\n");
					return;
				}
				i->procstackptr--;
				process  = i->procstack[i->procstackptr].process;
				entry    = i->procstack[i->procstackptr].entry;
				offset   = i->procstack[i->procstackptr].offset;
				entryPtr = i->ddb->data + i->ddb->processTable[process] + entry * 4;
				code     = i->ddb->data + *(uint16_t *)(entryPtr + 2) + offset;
				condact  = i->ddb->condactMap[*code & 0x7F].condact;
				params   = i->ddb->condactMap[*code & 0x7F].parameters;
				TRACE("\n\nResuming process %d, entry %d, offset %d\n\n", process, entry, offset);
				continue;

			case CONDACT_REDO:
				offset = 0;
				entry = 0;
				entryPtr = i->ddb->data + i->ddb->processTable[process];
				TRACE("\n");
				continue;

			case CONDACT_RESTART:
				i->procstackptr = 0;
				i->doall = false;
				process  = 0;
				entry    = 0;
				offset   = 0;
				entryPtr = i->ddb->data + i->ddb->processTable[process];
				code     = i->ddb->data + *(uint16_t *)(entryPtr + 2);
				TRACE("\n");
				continue;

			case CONDACT_SKIP:
			{
				// TODO: Check overflow (going pass last entry)
				int8_t increment = (int8_t)param0;
				entry++;
				if (entry + increment < 0)
					entry = 0;
				else
					entry += increment;
				offset = 0;
				entryPtr = i->ddb->data + i->ddb->processTable[process] + entry * 4;
				TRACE("\n");
				continue;			
			}
				
			case CONDACT_PAUSE:
				DDB_Flush(i);
				while (SCR_AnyKey())
					SCR_GetKey(0, 0, 0);
				i->state = DDB_PAUSED;
				i->pauseFrames = param0 == 0 ? 65535 : param0;
				SCR_GetMilliseconds(&i->pauseStart);
				UpdatePos(i, process, entry, offset + params + 1);
				if (param0 == 0)
					DDB_ResetScrollCounts(i);
				TRACE("\n");
				return;

			case CONDACT_ANYKEY:
				DDB_OutputMessage(i, DDB_SYSMSG, 16);
				DDB_Flush(i);
				i->state = DDB_WAITING_FOR_KEY;
				UpdatePos(i, process, entry, offset + params + 1);
				if (i->flags[Flag_TimeoutFlags] & Timeout_AnyKey)
				{
					i->timeout = true;
					i->timeoutRemainingMs = i->flags[Flag_Timeout] * 1000;
				}
				i->flags[Flag_TimeoutFlags] &= ~Timeout_LastFrame;
				DDB_ResetScrollCounts(i);
				DDB_ResetSmoothScrollFlags(i);
				TRACE("\n");
				return;

			// Flag manipulation actions 
			
			case CONDACT_SET:
				i->flags[param0] = 255;
				i->done = true;
				break;
			case CONDACT_CLEAR:
				i->flags[param0] = 0;
				i->done = true;
				break;
			case CONDACT_LET:
				i->flags[param0] = param1;
				i->done = true;
				break;
			case CONDACT_PLUS:
				if (param1 > 255 - i->flags[param0])
					i->flags[param0] = 255;
				else
					i->flags[param0] += param1;
				i->done = true;
				break;
			case CONDACT_MINUS:
				if (param1 > i->flags[param0])
					i->flags[param0] = 0;
				else
					i->flags[param0] -= param1;
				break;
			case CONDACT_ADD:
				if (i->flags[param0] > 255 - i->flags[param1])
					i->flags[param1] = 255;
				else
					i->flags[param1] += i->flags[param0];
				i->done = true;
				break;
			case CONDACT_SUB:
				if (i->flags[param0] > i->flags[param1])
					i->flags[param1] = 0;
				else
					i->flags[param1] -= i->flags[param0];
				i->done = true;
				break;
			case CONDACT_COPYFF:
				i->flags[param1] = i->flags[param0];
				i->done = true;
				break;
			case CONDACT_COPYBF:
				i->flags[param0] = i->flags[param1];
				i->done = true;
				break;
			case CONDACT_SYNONYM:
				if (param0 != 255)
					i->flags[Flag_Verb] = param0;
				if (param1 != 255)
					i->flags[Flag_Noun1] = param1;
				i->done = true;
				break;

			// Objects

			case CONDACT_COPYOF:
				if (param0 < i->ddb->numObjects)
					i->flags[param1] = i->objloc[param0];
				else
					i->flags[param1] = 255;
				i->done = true;
				break;
			case CONDACT_COPYOO:
				SetObjno(i, param0);
				if (param0 < i->ddb->numObjects && 
				    param1 < i->ddb->numObjects)
				{
					i->objloc[param1] = i->objloc[param0];
					SetObjno(i, param1);
				}
				i->done = true;
				break;
			case CONDACT_COPYFO:
				if (param1 < i->ddb->numObjects)
					i->objloc[param1] = i->flags[param0];
				i->done = true;
				break;
			case CONDACT_WHATO:
				SetObjno(i, Whato(i));
				i->done = true;
				TRACE("Obj#%d (at %d, weight %d)", i->flags[Flag_Objno], i->flags[Flag_ObjLocno], i->flags[Flag_ObjWeight]);
				break;
			case CONDACT_SETCO:
				SetObjno(i, param0);
				i->done = true;
				break;
			case CONDACT_CREATE:
				SetObjno(i, param0);
				if (param0 < i->ddb->numObjects)
				{
					if (i->objloc[param0] == Loc_Carried && i->flags[Flag_NumCarried] > 0 && i->flags[Flag_NumCarried] > 0)
						i->flags[Flag_NumCarried]--;
					i->objloc[param0] = i->flags[Flag_Locno];
					if (i->objloc[param0] == Loc_Carried)
						i->flags[Flag_NumCarried]++;
				}
				i->done = true;
				break;
			case CONDACT_DESTROY:
				SetObjno(i, param0);
				if (param0 < i->ddb->numObjects)
				{
					if (i->objloc[param0] == Loc_Carried && i->flags[Flag_NumCarried] > 0)
						i->flags[Flag_NumCarried]--;
					i->objloc[param0] = 252;
				}
				i->done = true;
				break;
			case CONDACT_SWAP:
				SetObjno(i, param1);
				if (param0 < i->ddb->numObjects && param1 < i->ddb->numObjects)
				{
					uint8_t tmp = i->objloc[param0];
					i->objloc[param0] = i->objloc[param1];
					i->objloc[param1] = tmp;
				}
				i->done = true;
				break;
			case CONDACT_PLACE:
				SetObjno(i, param0);
				if (param0 < i->ddb->numObjects)
				{
					SetObjno(i, param0);
					if (i->objloc[param0] == Loc_Carried && i->flags[Flag_NumCarried] > 0)
						i->flags[Flag_NumCarried]--;
					if (param1 == 255)
						param1 = i->flags[Flag_Locno];
					i->objloc[param0] = param1;
					if (i->objloc[param0] == Loc_Carried)
						i->flags[Flag_NumCarried]++;
				}
				i->done = true;
				break;

			// Specials

			case CONDACT_WEIGH:
				i->flags[param1] = CalculateWeight(i, param0, 0);
				i->done = true;
				break;	
			case CONDACT_WEIGHT:
				i->flags[param0] = CalculateCarriedWeight(i);
				i->done = true;
				break;
			case CONDACT_RESET:
				i->flags[Flag_NumCarried] = 0;
				for (int n = 0; n < i->ddb->numObjects; n++)
				{
					i->objloc[n] = i->ddb->objLocTable[n];
					if (i->objloc[n] == Loc_Carried)
						i->flags[Flag_NumCarried]++;
				}
				i->done = true;
				break;
			case CONDACT_ABILITY:
				i->flags[Flag_MaxCarried] = param0;
				i->flags[Flag_Strength]   = param1;
				i->done = true;
				break;
			case CONDACT_GOTO:
				i->flags[Flag_Locno] = param0;
				i->done = true;
				break;
			case CONDACT_TIME:
				i->flags[Flag_Timeout] = param0;
				i->flags[Flag_TimeoutFlags] = param1;
				i->done = true;
				break;
			case CONDACT_CHANCE:
				ok = RandInt(0, 100) < param0;
				break;
			case CONDACT_RANDOM:
				i->flags[param0] = RandInt(0, 100) + 1;
				i->done = true;
				break;

			// Parser

			case CONDACT_PARSE:
				if (i->ddb->version >= 2 && param0 != 1)
				{
					// In later version, perform INPUT here when no input is available
					while (i->inputBufferPtr < i->inputBufferLength && i->inputBuffer[i->inputBufferPtr] == ' ')
						i->inputBufferPtr++;
					if (i->inputBufferPtr == i->inputBufferLength)
					{
						DDB_StartInput(i);
						i->oldMainLoopState = FLOW_INPUT;
						i->state = DDB_INPUT;
						if (i->flags[Flag_TimeoutFlags] & Timeout_Input)
						{
							i->timeout = true;
							i->timeoutRemainingMs = i->flags[Flag_Timeout] * 1000;
						}
						i->flags[Flag_TimeoutFlags] &= ~Timeout_LastFrame;
						DDB_PrintInputLine(i, true);
						UpdatePos(i, process, entry, offset);
						TRACE("\n");
						return;
					}
				}
				ok = !Parse(i, i->ddb->version == 1 || param0 == 1);
				if (ok) DDB_NewText(i);
				break;

			// Low level object management

			case CONDACT_DROPALL:
				for (int n = 0; n < i->ddb->numObjects; n++)
				{
					if (i->objloc[n] == Loc_Carried)
						i->objloc[n] = i->flags[Flag_Locno];
				}
				i->flags[Flag_NumCarried] = 0;
				i->done = true;
				break;
			case CONDACT_PUTO:
				if (i->flags[Flag_Objno] < i->ddb->numObjects)
				{
					if (param0 == 255)
						param0 = i->flags[Flag_Locno];
					if (i->objloc[i->flags[Flag_Objno]] == Loc_Carried && i->flags[Flag_NumCarried] > 0)
						i->flags[Flag_NumCarried]--;
					i->objloc[i->flags[Flag_Objno]] = param0;
					if (i->objloc[i->flags[Flag_Objno]] == Loc_Carried)
						i->flags[Flag_NumCarried]++;
				}
				i->done = true;
				break;

			// Movement

			case CONDACT_MOVE:
				ok = MovePlayer(i, param0);
				break;

			// Semi-automatic object management
			// TODO: Refactor into separate functions, lots of code duplication here

			case CONDACT_AUTOP:
				param1 = param0;
				param0 = WhatoAt(i, Loc_Carried);
				if (param0 == 255)
					param0 = WhatoAt(i, Loc_Worn);
				if (param0 == 255)
					param0 = WhatoAt(i, i->flags[Flag_Locno]);
				if (param0 == 255)
				{
					param0 = WhatoAt(i, 255);
					if (param0 != 255)
						DDB_OutputMessage(i, DDB_SYSMSG, 28);	// I can't see any of those
					else
						DDB_OutputMessage(i, DDB_SYSMSG, 8);	// I can't do that
					DDB_NewText(i);
					goto case_DONE;
				}
				// Fall through
			case CONDACT_PUTIN:
				SetObjno(i, param0);
				locno = param0 < i->ddb->numObjects ? i->objloc[param0] : 252;
				if (locno == Loc_Worn)
				{
					DDB_OutputMessage(i, DDB_SYSMSG, 24);		// I can't, I'm wearing _.
					DDB_NewText(i);
					goto case_DONE;
				}
				if (locno == i->flags[Flag_Locno])
				{
					DDB_OutputMessage(i, DDB_SYSMSG, 49);		// I don't have _.
					DDB_NewText(i);
					goto case_DONE;
				}
				if (locno != Loc_Carried)
				{
					DDB_OutputMessage(i, DDB_SYSMSG, 28);		// I can't see _ around
					DDB_NewText(i);
					goto case_DONE;
				}
				i->objloc[param0] = param1;
				if (i->flags[Flag_NumCarried] > 0)
					i->flags[Flag_NumCarried]--;
				DDB_OutputMessage(i, DDB_SYSMSG, 44);			// _ is in 
				SetObjno(i, param1);
				DDB_OutputText(i, "_");
				SetObjno(i, param0);
				DDB_OutputMessage(i, DDB_SYSMSG, 51);			// .
				i->done = true;
				break;

			case CONDACT_AUTOT:
				param1 = param0;
				param0 = WhatoAt(i, param1);
				if (param0 == 255)
					param0 = WhatoAt(i, Loc_Carried);
				if (param0 == 255)
					param0 = WhatoAt(i, Loc_Worn);
				if (param0 == 255)
					param0 = WhatoAt(i, i->flags[Flag_Locno]);
				if (param0 == 255)
				{
					param0 = WhatoAt(i, 255);
					if (param0 != 255)
						DDB_OutputMessage(i, DDB_SYSMSG, 28);	// I can't see any of those
					else
						DDB_OutputMessage(i, DDB_SYSMSG, 8);	// I can't do that
					DDB_NewText(i);
					goto case_DONE;
				}
				// Fall through
			case CONDACT_TAKEOUT:
				SetObjno(i, param0);
				locno = param0 < i->ddb->numObjects ? i->objloc[param0] : 252;
				if (locno == Loc_Worn || locno == Loc_Carried)
				{
					DDB_OutputMessage(i, DDB_SYSMSG, 25);		// I already have _
					DDB_NewText(i);
					goto case_DONE;
				}
				if (locno == i->flags[Flag_Locno])
				{
					DDB_OutputMessage(i, DDB_SYSMSG, 45);		// _ is not in
					DDB_OutputMessage(i, DDB_OBJNAME, param1);
					DDB_OutputMessage(i, DDB_SYSMSG, 51);		// .
					DDB_NewText(i);
					goto case_DONE;
				}
				if (locno != param1 || param0 >= i->ddb->numObjects)
				{
					DDB_OutputMessage(i, DDB_SYSMSG, 52);		// I can't see _ in
					DDB_OutputMessage(i, DDB_OBJNAME, param1);
					DDB_OutputMessage(i, DDB_SYSMSG, 51);		// .
					DDB_NewText(i);
					goto case_DONE;
				}
				value = CalculateWeight(i, param0, 0);
				if (value + CalculateCarriedWeight(i) > i->flags[Flag_Strength])
				{
					DDB_OutputMessage(i, DDB_SYSMSG, 43);		// I can't carry any more weight.
					DDB_NewText(i);
					goto case_DONE;
				}
				if (i->flags[Flag_NumCarried] >= i->flags[Flag_MaxCarried])
				{
					DDB_OutputMessage(i, DDB_SYSMSG, 27);		// I can't carry any more.
					DDB_NewText(i);
					i->doall = false;
					goto case_DONE;
				}
				i->objloc[param0] = Loc_Carried;
				i->flags[Flag_NumCarried]++;
				DDB_OutputMessage(i, DDB_SYSMSG, 36);			// _ is in
				i->done = true;
				break;

			case CONDACT_AUTOD:
				param0 = WhatoAt(i, Loc_Carried);
				if (param0 == 255)
					param0 = WhatoAt(i, Loc_Worn);
				if (param0 == 255)
					param0 = WhatoAt(i, i->flags[Flag_Locno]);
				if (param0 == 255)
				{
					param0 = WhatoAt(i, 255);
					if (param0 != 255)
						DDB_OutputMessage(i, DDB_SYSMSG, 28);	// I can't see any of those
					else
						DDB_OutputMessage(i, DDB_SYSMSG, 8);	// I can't do that
					DDB_NewText(i);
					goto case_DONE;
				}
				// Fall through				
			case CONDACT_DROP:
				SetObjno(i, param0);
				if (param0 >= i->ddb->numObjects)
				{
					ok = false;
					break;
				}
				locno = i->objloc[param0];
				if (locno == Loc_Worn)
				{
					DDB_OutputMessage(i, DDB_SYSMSG, 24);		// I can't, I'm wearing _.
					DDB_NewText(i);
					goto case_DONE;
				}
				if (locno == i->flags[Flag_Locno])
				{
					DDB_OutputMessage(i, DDB_SYSMSG, 49);		// I don't have _.
					DDB_NewText(i);
					goto case_DONE;
				}
				if (locno != Loc_Carried)
				{
					DDB_OutputMessage(i, DDB_SYSMSG, 28);		// I don't have that.
					DDB_NewText(i);
					goto case_DONE;
				}
				i->objloc[param0] = i->flags[Flag_Locno];
				if (i->flags[Flag_NumCarried] > 0)
					i->flags[Flag_NumCarried]--;
				DDB_OutputMessage(i, DDB_SYSMSG, 39);			// I dropped _
				i->done = true;
				break;

			case CONDACT_AUTOG:
				param0 = WhatoAt(i, i->flags[Flag_Locno]);
				if (param0 == 255)
					param0 = WhatoAt(i, Loc_Carried);
				if (param0 == 255)
					param0 = WhatoAt(i, Loc_Worn);
				if (param0 == 255)
				{
					param0 = WhatoAt(i, 255);
					if (param0 != 255)
						DDB_OutputMessage(i, DDB_SYSMSG, 26);	// I can't see any of those
					else
						DDB_OutputMessage(i, DDB_SYSMSG, 8);	// I can't do that
					DDB_NewText(i);
					goto case_DONE;
				}
				// Fall through
			case CONDACT_GET:
				SetObjno(i, param0);
				if (param0 >= i->ddb->numObjects)
				{
					ok = false;
					break;
				}
				locno = i->objloc[param0];
				if (locno == Loc_Worn || locno == Loc_Carried)
				{
					DDB_OutputMessage(i, DDB_SYSMSG, 25);		// I already have _
					DDB_NewText(i);
					goto case_DONE;
				}
				if (locno != i->flags[Flag_Locno])
				{
					DDB_OutputMessage(i, DDB_SYSMSG, 26);		// I can't see that around
					DDB_NewText(i);
					goto case_DONE;
				}
				value = CalculateWeight(i, param0, 0);
				if (value + CalculateCarriedWeight(i) > i->flags[Flag_Strength])
				{
					DDB_OutputMessage(i, DDB_SYSMSG, 43);		// I can't carry any more weight.
					DDB_NewText(i);
					goto case_DONE;
				}
				if (i->flags[Flag_NumCarried] >= i->flags[Flag_MaxCarried])
				{
					DDB_OutputMessage(i, DDB_SYSMSG, 27);		// I can't carry any more.
					DDB_NewText(i);
					i->doall = false;
					goto case_DONE;
				}
				i->objloc[param0] = Loc_Carried;
				i->flags[Flag_NumCarried]++;
				DDB_OutputMessage(i, DDB_SYSMSG, 36);			// I now have _
				i->done = true;
				break;

			case CONDACT_AUTOW:
				param0 = WhatoAt(i, Loc_Carried);
				if (param0 == 255)
					param0 = WhatoAt(i, Loc_Worn);
				if (param0 == 255)
					param0 = WhatoAt(i, i->flags[Flag_Locno]);
				if (param0 == 255)
				{
					if (param0 != 255)
						DDB_OutputMessage(i, DDB_SYSMSG, 26);	// I can't see any of those
					else
						DDB_OutputMessage(i, DDB_SYSMSG, 8);	// I can't do that
					DDB_NewText(i);
					goto case_DONE;
				}
				// Fall through
			case CONDACT_WEAR:
				SetObjno(i, param0);
				if (param0 >= i->ddb->numObjects)
				{
					ok = false;
					break;
				}
				locno = i->objloc[param0];
				if (locno == Loc_Worn)
				{
					DDB_OutputMessage(i, DDB_SYSMSG, 29);		// I'm already wearing _
					DDB_NewText(i);
					goto case_DONE;
				}
				if (locno != Loc_Carried)
				{
					DDB_OutputMessage(i, DDB_SYSMSG, 28);		// I don't have that.
					DDB_NewText(i);
					goto case_DONE;
				}
				if (!(i->ddb->objAttrTable[param0] & Obj_Wearable))
				{
					DDB_OutputMessage(i, DDB_SYSMSG, 40);		// I can't wear that.
					DDB_NewText(i);
					goto case_DONE;
				}
				i->objloc[param0] = Loc_Worn;
				if (i->flags[Flag_NumCarried] > 0)
					i->flags[Flag_NumCarried]--;
				DDB_OutputMessage(i, DDB_SYSMSG, 37);		// I'm now wearing _
				i->done = true;
				break;

			case CONDACT_AUTOR:
				param0 = WhatoAt(i, Loc_Worn);
				if (param0 == 255)
					param0 = WhatoAt(i, Loc_Carried);
				if (param0 == 255)
					param0 = WhatoAt(i, i->flags[Flag_Locno]);
				if (param0 == 255)
				{
					if (param0 != 255)
						DDB_OutputMessage(i, DDB_SYSMSG, 26);	// I can't see any of those
					else
						DDB_OutputMessage(i, DDB_SYSMSG, 8);	// I can't do that
					DDB_NewText(i);
					goto case_DONE;
				}
				// Fall through
			case CONDACT_REMOVE:
				SetObjno(i, param0);
				if (param0 >= i->ddb->numObjects)
				{
					ok = false;
					break;
				}
				locno = i->objloc[param0];
				if (locno == Loc_Carried || locno == i->flags[Flag_Locno])
				{
					DDB_OutputMessage(i, DDB_SYSMSG, 50);		// I'm not wearing _
					DDB_NewText(i);
					goto case_DONE;
				}
				if (locno != Loc_Worn)
				{
					DDB_OutputMessage(i, DDB_SYSMSG, 23);		// I am not wearing that.
					DDB_NewText(i);
					goto case_DONE;
				}
				if (!(i->ddb->objAttrTable[param0] & Obj_Wearable))
				{
					DDB_OutputMessage(i, DDB_SYSMSG, 41);		// I can't remove that.
					DDB_NewText(i);
					goto case_DONE;
				}
				if (i->flags[Flag_NumCarried] >= i->flags[Flag_MaxCarried])
				{
					DDB_OutputMessage(i, DDB_SYSMSG, 42);		// I can't carry any more.
					DDB_NewText(i);
					i->doall = false;
					goto case_DONE;
				}
				i->objloc[param0] = Loc_Carried;
				i->flags[Flag_NumCarried]++;
				DDB_OutputMessage(i, DDB_SYSMSG, 38);		// I'm no longer wearing _
				i->done = true;
				break;

			case CONDACT_DOALL:
				i->doallProcess = process;
				i->doallEntry = entry;
				i->doallOffset = offset + params + 1;
				UpdatePos(i, process, entry, offset + params + 1);
				if (!DoAll(i, param0 == 255 ? i->flags[Flag_Locno] : param0, true))
					ok = false;
				break;

			// Graphics and window management condacts

			case CONDACT_WINDOW:
				DDB_Flush(i);
				DDB_SetWindow(i, param0);
				repeatingDisplay = false;
				TRACE("Window %d: at %d,%d %dx%d", i->curwin, i->win.x, i->win.y, i->win.width, i->win.height);
				break;
			case CONDACT_WINAT:
				DDB_Flush(i);
				WinAt(i, param0, param1);
				TRACE("Window %d: at %d,%d %dx%d", i->curwin, i->win.x, i->win.y, i->win.width, i->win.height);
				break;
			case CONDACT_WINSIZE:
				DDB_Flush(i);
				WinSize(i, param0, param1);
				TRACE("Window %d: at %d,%d %dx%d", i->curwin, i->win.x, i->win.y, i->win.width, i->win.height);
				break;
			case CONDACT_CLS: 
				DDB_Flush(i);
                if (AnyWindowOverlapsCurrent(i))
                {
					// fprintf(stderr, "WARNING: Overlap detected in CLS in process %d, entry %d, offset %d\n", process, entry, offset);
					DDB_Flush(i);
					i->state = DDB_VSYNC;
					UpdatePos(i, process, entry, offset);
					TRACE("\n");
					return;
                }
				DDB_ClearWindow(i, &i->win);
				break;
			case CONDACT_PICTURE:
				if (i->ddb->version == 1)
				{
					if (BufferPicture(i, param0))
						DrawBufferedPicture(i);
				}
				else
				{
					ok = BufferPicture(i, param0);
					if (ok == false && SCR_SampleExists(param0))
					{
						ok = true;
						i->currentPicture = param0;
					}
					repeatingDisplay = false;
				}
				break;
			case CONDACT_DISPLAY:
				if (param0 == 0)
				{
					if (repeatingDisplay)
					{
						i->state = DDB_VSYNC;
						UpdatePos(i, process, entry, offset);
						TRACE("\n");
						return;
					}
					DrawBufferedPicture(i);
					repeatingDisplay = true;
				}
				else
				{
					DDB_ClearWindow(i, &i->win);
				}
				MarkWindowOutput();
				break;
			case CONDACT_MODE:
				i->win.flags = param0;
				break;
			case CONDACT_GFX:		// GRAPHIC in old version
				if (i->ddb->version == 1)
				{
					// Set bits 3 to 6 of graphic flags
					i->flags[Flag_GraphicFlags] &= 0x87;
					i->flags[Flag_GraphicFlags] |= ((param0 & 0x3) << 4) | ((param0 & 0x1) << 3);
				}
				else
				{
					switch (param1)
					{
						case 0:			// copy backbuffer --> physical screen
							SCR_RestoreScreen();
							break;
						case 1:			// copy physical screen --> backbuffer
							SCR_SaveScreen();
							break;
						case 2:			// swap physical screen <-> backbuffer
							SCR_SwapScreen();
							break;
						case 3:			// set picture output to physical screen
							SCR_SetOpBuffer(SCR_OP_DRAWPICTURE, true);
							break;
						case 4:			// set picture output to backbuffer
							SCR_SetOpBuffer(SCR_OP_DRAWPICTURE, false);
							break;
						case 5:			// clear physical screen
							SCR_ClearBuffer(true);
							break;
						case 6:			// clear backbuffer
							SCR_ClearBuffer(false);
							break;
						case 7:			// set text output to physical screen
							SCR_SetOpBuffer(SCR_OP_DRAWTEXT, true);
							break;
						case 8:			// set text output to backbuffer
							SCR_SetOpBuffer(SCR_OP_DRAWTEXT, false);
							break;
						case 9:			// set palette color (param0: index of a 4-flag buffer with color#,R,G,B)
							if (paletteChanges & (1 << i->flags[param0]))
							{
								// If this color was already changed this frame, wait
								TRACE("\n\nPalette color %d already changed this frame, waiting\n", i->flags[param0]);
								DDB_Flush(i);
								i->state = DDB_VSYNC;
								UpdatePos(i, process, entry, offset);
								TRACE("\n");
								return;
							}
							TRACE("\nSetting palette color %d to %d,%d,%d\n", i->flags[param0], i->flags[param0+1], i->flags[param0+2], i->flags[param0+3]);
							SCR_SetPaletteColor(i->flags[param0], i->flags[param0+1], i->flags[param0+2], i->flags[param0+3]);
							paletteChanges |= 1 << i->flags[param0];
							break;
						case 10:		// get palette color (param0: index of a 4-flag buffer with color#,R,G,B)
							SCR_GetPaletteColor(i->flags[param0], &i->flags[param0+1], &i->flags[param0+2], &i->flags[param0+3]);
							break;
					}
				}
				break;
			case CONDACT_INKEY:
				// HACK: prevent 'more' to be shown in Espacial
				// TODO: Check which condact resets the 'more...' counter
				DDB_ResetScrollCounts(i);
				DDB_ResetSmoothScrollFlags(i);

				// Temporary hack to make debug more manageable
				if (PAUSE_ON_INKEY)
				{
					DDB_Flush(i);
					i->state = DDB_WAITING_FOR_KEY;
					UpdatePos(i, process, entry, offset + params + 1);
					TRACE("\n");
					return;
				}

				// This is a convoluted mess because games will perform several INKEY in sequence
				// and expect the same key to be returned, but we need some way to stop sending the
				// same key back for things like menus. The way it's implemented right now is that
				// INKEY status is reset by a DDB_Flush command (essentially, printing text).
				//
				// In addition, checking for a key requires waiting a frame since the system
				// is event driven. 
				if (i->keyChecked && i->keyPressed && i->keyReuseCount < 16)
				{
					ok = true;
					i->flags[Flag_Key1] = i->lastKey1;
					i->flags[Flag_Key2] = i->lastKey2;
					i->keyReuseCount++;
				}			
				else
				{
					if (i->keyCheckInProgress)
					{
						ok = SCR_AnyKey();
						if (ok) {
							SCR_GetKey(&i->flags[Flag_Key1], &i->flags[Flag_Key2], 0);
						}
						i->keyCheckInProgress = false;
						i->keyChecked = true;
						i->keyPressed = ok;
						i->lastKey1 = i->flags[Flag_Key1];
						i->lastKey2 = i->flags[Flag_Key2];
						i->keyReuseCount = 0;
					}
					else
					{
						// Synchronize and wait one frame

						DDB_Flush(i);
						i->keyCheckInProgress = true;
						i->state = DDB_CHECKING_KEY;
						UpdatePos(i, process, entry, offset);			
						TRACE("\n");
						return;
					}
				}
				break;
			case CONDACT_TURNS:
				DDB_OutputMessage(i, DDB_SYSMSG, 17);		// You have taken
				LongToChar(i->flags[Flag_Turns+1] + 256*i->flags[Flag_Turns], output, 10);
				DDB_OutputText(i, output);
				DDB_OutputMessage(i, DDB_SYSMSG, 18);		// turn
				if (i->flags[Flag_Turns+1] + 256*i->flags[Flag_Turns] != 1)
					DDB_OutputMessage(i, DDB_SYSMSG, 19);	// s
				DDB_OutputMessage(i, DDB_SYSMSG, 20);		// so far.
				i->done = true;
				break;
			case CONDACT_PROMPT:
				// Doesn't seem to do anything
				i->prompt = param0;
				break;
			case CONDACT_PAPER:
				DDB_Flush(i);
				// Transparent paper is not supported in the original
				i->win.paper = param0 == 255 ? 255 : param0 & 0x0F;
				break;
			case CONDACT_INK:
				DDB_Flush(i);
				i->win.ink = param0 & 0x0F;
				break;
			case CONDACT_TIMEOUT:
				ok = (i->flags[Flag_TimeoutFlags] & Timeout_LastFrame) != 0;
				break;

			// Save states

			case CONDACT_SAVE:
				if (supportsOpenFileDialog)
				{
					i->inputBufferLength = 0;
					i->inputBufferPtr = 0;
					SCR_OpenFileDialog(false, (char *)i->inputBuffer, sizeof(i->inputBuffer));
					if (i->inputBuffer[0] != 0)
					{
						i->inputBufferLength = StrLen((const char *)i->inputBuffer);
						UpdatePos(i, process, entry, offset + params + 1);
						DDB_ResolveInputSave(i);
						TRACE("\n");
						return;
					}
					goto case_NOTDONE;
				}
				else
				{
					DDB_OutputMessage(i, DDB_SYSMSG, 60);		// Enter file name
					DDB_OutputText(i, " ");
					DDB_Flush(i);
					i->state = DDB_INPUT_SAVE;
					DDB_NewText(i);
					DDB_PrintInputLine(i, true);
					UpdatePos(i, process, entry, offset + params + 1);
					TRACE("\n");
					return;
				}
				break;

			case CONDACT_LOAD:
				if (supportsOpenFileDialog)
				{
					i->inputBufferLength = 0;
					i->inputBufferPtr = 0;
					SCR_OpenFileDialog(true, (char*)i->inputBuffer, sizeof(i->inputBuffer));
					if (i->inputBuffer[0] != 0)
					{
						i->inputBufferLength = StrLen((const char*)i->inputBuffer);
						UpdatePos(i, process, entry, offset + params + 1);
						DDB_ResolveInputLoad(i);
						TRACE("\n");
						return;
					}
					goto case_NOTDONE;
				}
				else
				{
					DDB_OutputMessage(i, DDB_SYSMSG, 60);		// Enter file name
					DDB_OutputText(i, " ");
					DDB_Flush(i);
					i->state = DDB_INPUT_LOAD;
					DDB_NewText(i);
					DDB_PrintInputLine(i, true);
					UpdatePos(i, process, entry, offset + params + 1);
					TRACE("\n");
					return;
				}
				break;
			
			case CONDACT_RAMSAVE:
				MemCopy(i->ramSaveArea, i->buffer, i->saveStateSize);
				i->ramSaveAvailable = true;
				break;

			case CONDACT_RAMLOAD:
				if (i->ramSaveAvailable)
				{
					// Flags are required to be first in buffer
					int flagCount = param0 + 1;
					MemCopy(i->buffer, i->ramSaveArea, flagCount);
					MemCopy(i->buffer + 256, i->ramSaveArea + 256, i->saveStateSize - flagCount);
				}
				else
				{
					ok = false;
				}
				break;


			case CONDACT_SAVEAT:
				DDB_Flush(i);
				i->win.saveX = i->win.posX;
				i->win.saveY = i->win.posY;
				break;

			case CONDACT_BACKAT:
				DDB_Flush(i);
				i->win.posX = i->win.saveX;
				i->win.posY = i->win.saveY;
				break;

			case CONDACT_SFX:
				if (param1 == 255) {
					int durationMs = 0;
					SCR_PlaySample(i->currentPicture, &durationMs);

					DDB_Flush(i);
					i->state = DDB_PAUSED;
					SCR_GetMilliseconds(&i->pauseStart);
					UpdatePos(i, process, entry, offset + params + 1);
					i->pauseFrames = durationMs / 15;
					TRACE("\n");
					return;
				} else {
					// This enables keyboard click (bit 0) and key repeat (bit 1) in original interpreter
				}
				break;

			case CONDACT_CENTRE:
				DDB_Flush(i);
				CenterWindow(i, &i->win);
				i->windef[i->curwin].x = i->win.x;
				i->windef[i->curwin].width = i->win.width;
				i->windef[i->curwin].posX = i->win.posX;
				break;

			// TODO

			case CONDACT_CALL:
			case CONDACT_EXTERN:
			case CONDACT_BEEP:
			case CONDACT_MOUSE:
			case CONDACT_BORDER:
				i->done = true;
				break;

			default:
				DDB_Flush(i);
				#if TRACE_ON
				TRACE("Condact %s not implemented (process %d, entry %d, offset %d)\n", DDB_GetCondactName((DDB_Condact)condact), process, entry, offset);
				#endif
				i->state = DDB_FATAL_ERROR;
				TRACE("\n");
				return;
		}
		TRACE("\n");

		if (finished)
			break;
		if (!ok)
		{
			entry++;
			offset = 0;
			entryPtr += 4;
			TRACE("\n");
		}
		else
		{
			offset += params + 1;
			code   += params + 1;
		}
		if (!SCR_Synchronized())
			break;
	}

	UpdatePos(i, process, entry, offset);
}

static void StepFunction(int elapsed)
{
	DDB_Interpreter* i = interpreter;

	if (SCR_Synchronized() == false)
	{
		if (waitingForKey)
		{
			if (!SCR_AnyKey())
			{
				if (i->timeout)
				{
					i->timeoutRemainingMs -= elapsed;
					if (i->timeoutRemainingMs <= 0)
					{
						i->timeout = false;
						i->flags[Flag_TimeoutFlags] |= Timeout_LastFrame;
						i->state = DDB_RUNNING;
					}
				}
				return;
			}
			waitingForKey = false;
			SCR_GetKey(0, 0, 0);
		}
		SCR_ConsumeBuffer();
		return;
	}

	switch (i->state)
	{
		case DDB_CHECKING_KEY:
			i->state = DDB_RUNNING;
			break;

		case DDB_VSYNC:
			i->state = DDB_RUNNING;
			break;

		case DDB_RUNNING:
			DDB_Step(i, 16384);
			break;

		case DDB_INPUT:
			if (i->timeout && i->inputBufferLength == 0)
			{
				i->timeoutRemainingMs -= elapsed;
				if (i->timeoutRemainingMs <= 0)
				{
					i->timeout = false;
					i->flags[Flag_TimeoutFlags] |= Timeout_LastFrame;
					if (i->ddb->oldMainLoop)
						i->state = DDB_FINISHED;
					else
						i->state = DDB_RUNNING;
					DDB_PrintInputLine(i, false);
					DDB_FinishInput(i, true);
					break;
				}
			}
			// Fall through			
		case DDB_INPUT_QUIT:
		case DDB_INPUT_END:
		case DDB_INPUT_LOAD:
		case DDB_INPUT_SAVE:
			DDB_ProcessInputFrame();
			break;

		case DDB_PAUSED:
		{
			uint32_t current = 0;
			SCR_GetMilliseconds(&current);
			
			if (SCR_AnyKey()) 
			{
				i->state = DDB_RUNNING;
				SCR_GetKey(&i->lastKey1, &i->lastKey2, 0);
				DDB_PlayClick(i, false);
				if (i->ddb->version > 1)
				{
					i->flags[Flag_Key1] = i->lastKey1;
					i->flags[Flag_Key2] = i->lastKey2;
				}
			} 
			else 
			{
				if (i->pauseFrames < 0 || current - i->pauseStart >= (uint32_t)i->pauseFrames * 16)
					i->state = DDB_RUNNING;
			}
			break;
		}

		case DDB_WAITING_FOR_KEY:
			if (SCR_AnyKey())
			{
				SCR_GetKey(&i->lastKey1, &i->lastKey2, 0);
				DDB_PlayClick(i, true);
				if (i->ddb->version > 1)
				{
					i->flags[Flag_Key1] = i->lastKey1;
					i->flags[Flag_Key2] = i->lastKey2;
				}
				i->state = DDB_RUNNING;
				i->timeout = false;
			}
			else if (i->timeout)
			{
				i->timeoutRemainingMs -= elapsed;
				if (i->timeoutRemainingMs <= 0)
				{
					i->timeout = false;
					i->flags[Flag_TimeoutFlags] |= Timeout_LastFrame;
					i->state = DDB_RUNNING;
				}
			}
			break;

		case DDB_FINISHED:
			if (i->ddb->oldMainLoop)
			{
				TRACE("\nSimulating PAWS main loop (current state: %s)\n", DDB_FlowNames[i->oldMainLoopState]);
				DDB_Flush(i);

				switch (i->oldMainLoopState)
				{
					case FLOW_RESPONSES:
						if (i->flags[Flag_TimeoutFlags] & 0x80)
							DDB_OutputMessage(i, DDB_SYSMSG, 35);		// Time passes...
						else if (!i->done)
						{
							DDB_OutputMessage(i, DDB_SYSMSG, 8);		// I don't understand that.
							DDB_NewText(i);
						}
						// Fall through

					case FLOW_DESC:
						i->oldMainLoopState = FLOW_AFTER_TURN;
						i->state = DDB_RUNNING;
						i->procstack[0].process = 2;
						i->procstack[0].entry = 0;
						i->procstack[0].offset = 0;
						i->procstackptr = 0;
						break;

					case FLOW_AFTER_TURN:
						if (i->flags[5] > 0) i->flags[5]--;
						if (i->flags[6] > 0) i->flags[6]--;
						if (i->flags[7] > 0) i->flags[7]--;
						if (i->flags[8] > 0) i->flags[8]--;
						if (i->flags[Flag_Darkness] != 0)
						{
							if (i->flags[9] > 0) i->flags[9]--;
							if (i->flags[10] > 0 && Absent(i, 0)) i->flags[10]--;
						}
						if (i->flags[Flag_Turns+1] == 255) {
							i->flags[Flag_Turns+1] = 0;
							if (i->flags[Flag_Turns] != 255) 
								i->flags[Flag_Turns]++;
						} else {
							i->flags[Flag_Turns+1]++;
						}
						if (Parse(i, 0)) {
							i->done = false;
							i->oldMainLoopState = FLOW_RESPONSES;
							i->state = DDB_RUNNING;
							i->procstack[0].process = 0;
							i->procstack[0].entry = 0;
							i->procstack[0].offset = 0;
							i->procstackptr = 0;
						} else {
							DDB_StartInput(i);
							i->oldMainLoopState = FLOW_INPUT;
							DDB_PrintInputLine(i, true);
							if (i->flags[Flag_TimeoutFlags] & Timeout_Input)
							{
								i->timeout = true;
								i->timeoutRemainingMs = i->flags[Flag_Timeout] * 1000;
							}
							i->flags[Flag_TimeoutFlags] &= ~Timeout_LastFrame;
						}
						break;

					case FLOW_INPUT:
						if (!Parse(i, false))
						{
							if (i->flags[Flag_TimeoutFlags] & Timeout_LastFrame)
								DDB_OutputMessage(i, DDB_SYSMSG, 35);
							else
								DDB_OutputMessage(i, DDB_SYSMSG, 6);
							i->oldMainLoopState = FLOW_AFTER_TURN;
							i->state = DDB_RUNNING;
							i->procstack[0].process = 2;
							i->procstack[0].entry = 0;
							i->procstack[0].offset = 0;
							i->procstackptr = 0;
							break;
						}
						i->oldMainLoopState = FLOW_RESPONSES;
						i->state = DDB_RUNNING;
						i->done = false;
						i->procstack[0].process = 0;
						i->procstack[0].entry = 0;
						i->procstack[0].offset = 0;
						i->procstackptr = 0;
						break;
				}
			}
			break;

		case DDB_QUIT:
		{
			uint32_t time = 0;
			SCR_GetMilliseconds(&time);
			if (time < i->quitStart + 500)
				break;
			SCR_Quit();
			break;
		}

		case DDB_FATAL_ERROR:
			SCR_Quit();
			break;
	}
}

void DDB_Run (DDB_Interpreter* i)
{
	SCR_MainLoop(i, StepFunction);
}

DDB_Interpreter* DDB_CreateInterpreter (DDB* ddb)
{
	DDB_Interpreter* i = Allocate<DDB_Interpreter>("DDB Interpreter");
	if (!i)
	{
		DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
		return 0;
	}

	if (interpreter == 0)
		interpreter = i;

	MemClear(i, sizeof(DDB_Interpreter));
	i->ddb = ddb;
	i->screenMode = ScreenMode_VGA16;

	i->saveStateSize = 256 + ddb->numObjects;
	i->bufferSize = i->saveStateSize * 2;
	i->buffer = Allocate<uint8_t>("DDB Savestate", i->bufferSize);
	if (!i->buffer)
	{
		DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
		Free(i);
		return 0;
	}
	MemClear(i->buffer, i->bufferSize);

	i->flags = i->buffer;
	i->objloc = i->buffer + 256;
	i->ramSaveArea = i->buffer + i->saveStateSize;
	i->keyClick = 2;

	DDB_Reset(i);
	DDB_ResetWindows(i);
	return i;
}

void DDB_CloseInterpreter (DDB_Interpreter* i)
{
	Free(i->buffer);
	Free(i);
}
