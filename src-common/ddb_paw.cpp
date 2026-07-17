#include <ddb_paw.h>

#if HAS_PAWS

#include <ddb_scr.h>
#include <ddb_data.h>
#include <ddb_vid.h>
#include <os_lib.h>

void DDB_PAWSResetState(DDB_Interpreter* i)
{
	// A finished PAW game copies the Spectrum frame counter into $5C76 on
	// every game start/restart. Use ADP's normally time-seeded random source
	// for equivalent entropy; --random-seed keeps scripted tests exact.
	i->pawsRandomSeed = (uint16_t)RandInt(0, 0xFFFF);

	// A set bit means that the corresponding automatic location picture has
	// not yet been drawn. Drawing it clears the bit.
	MemSet(i->visited, 0xFF, DDB_PAWS_GRAPHIC_BYTES);
	i->flags[Flag_GraphicFlags] = 0;
	i->flags[Flag_TopLine] = 24;
	i->flags[Flag_PAWMode] = 0;
	i->flags[Flag_SplitLine] = 12;
	i->pawsControlParams = 0;
	i->pawsPermanentInk = i->ddb->defaultInk;
	i->pawsPermanentPaper = i->ddb->defaultPaper;
	i->pawsPermanentCharset = i->ddb->defaultCharset;
	i->pawsPermanentFlags = 0;
	i->pawsBorder = i->ddb->defaultBorder & 7;
	i->pauseTickRemainderMs = 0;
	i->timeoutTickRemainderMs = 0;
}

void DDB_PAWSSetMode(DDB_Interpreter* i, uint8_t mode, uint8_t options)
{
	i->flags[Flag_PAWMode] = (mode & 0x3F) | ((options & 0x03) << 6);
}

void DDB_PAWSSetLine(DDB_Interpreter* i, uint8_t line)
{
	i->flags[Flag_SplitLine] = (uint8_t)(24 - line);
}

void DDB_PAWSProtect(DDB_Interpreter* i)
{
	DDB_Flush(i);
	int row = i->win.posY / lineHeight;
	if (row < 0) row = 0;
	if (row > 24) row = 24;
	i->flags[Flag_TopLine] = (uint8_t)(24 - row);
}

void DDB_PAWSSetGraphic(DDB_Interpreter* i, uint8_t option)
{
	i->flags[Flag_GraphicFlags] = (option & 0x03) << 5;
}

void DDB_PAWSSetTime(DDB_Interpreter* i, uint8_t duration, uint8_t options)
{
	i->flags[Flag_Timeout] = duration;
	i->flags[Flag_TimeoutFlags] =
		(i->flags[Flag_TimeoutFlags] & 0xF8) | (options & 0x07);
}

void DDB_PAWSSetInput(DDB_Interpreter* i, uint8_t options)
{
	i->flags[Flag_TimeoutFlags] =
		(i->flags[Flag_TimeoutFlags] & 0xC7) | ((options & 0x07) << 3);
	// Generic bits 1/2 have the same accepted-line/timeout echo meanings.
	// PAW bit 0 selects bottom-of-screen input; it is not "clear window".
	i->inputFlags = options & 0x06;
}

uint8_t DDB_PAWSRandom(DDB_Interpreter* i)
{
	// B03 routine $8546 updates the Spectrum seed at $5C76 using only its
	// previous low byte. A zero high byte is rejected, with the updated seed
	// feeding the next attempt, so the public result is always in 1..100.
	uint8_t result;
	do
	{
		i->pawsRandomSeed = (uint16_t)(101u *
			(uint16_t)(((i->pawsRandomSeed & 0x00FFu) + 1u)));
		result = (uint8_t)(i->pawsRandomSeed >> 8);
	}
	while (result == 0);
	return result;
}

static void SetFullScreen(DDB_Interpreter* i, int row)
{
	DDB_Window* w = &i->win;
	w->x = 0;
	w->y = 0;
	w->width = screenWidth;
	w->height = screenHeight;
	w->posX = 0;
	w->posY = row * lineHeight;
	i->curwin = 0;
	i->windef[0] = *w;
	DDB_CalculateCells(i, w, &i->cellX, &i->cellW);
}

static uint8_t SetPAWSVideoAttributes(DDB_Interpreter* i, DDB_Window* w)
{
	uint8_t ink, paper;
	DDB_GetCurrentColors(i->ddb, w, &ink, &paper);
	uint8_t attributes = (ink & 0x07) | ((paper & 0x07) << 3) |
		((ink & 0x08) << 3) | ((ink & 0x10) << 3);
	SCR_SetAttributes(attributes);
	return paper;
}

void DDB_PAWSClear(DDB_Interpreter* i)
{
	// PAW's clear/open routine restores the permanent display state before
	// clearing.  The resulting top-left position also replaces the single
	// SAVEAT slot used by BACKAT.
	DDB_ResetPAWSColors(i, &i->win);
	SetFullScreen(i, 0);

	SetPAWSVideoAttributes(i, &i->win);
	DDB_ClearWindow(i, &i->win);
	// Spectrum ROM CLS upper ($0DAF) stores 1 in SCR_CT ($5C8C).  The first
	// attempted scroll must therefore expire the counter and enter PAW's
	// MODE-controlled pagination path.  Zero means "unknown" to ADP and would
	// incorrectly reload a whole page only after the screen already overflowed.
	i->win.scrollCount = 1;

	i->win.saveX = i->win.posX;
	i->win.saveY = i->win.posY;
	i->flags[Flag_TopLine] = 24;
	i->windef[0] = i->win;
}

void DDB_PAWSOutputAnyKeyMessage(DDB_Interpreter* i)
{
	// ANYKEY prints system message 16 through the Spectrum lower screen.  Its
	// leading newline advances from row 22 to the bottom row; using the main
	// PAW window here would instead invoke its protected-scroll boundary and
	// move the prompt up into row 21.
	DDB_Flush(i);
	DDB_Window* w = &i->windef[1];
	w->x = 0;
	w->y = screenHeight - 2 * lineHeight;
	w->width = screenWidth;
	w->height = 2 * lineHeight;
	w->posX = 0;
	w->posY = w->y;
	w->ink = i->win.ink;
	w->paper = i->win.paper;
	w->flags = i->win.flags | Win_NoMorePrompt;
	w->graphics = false;
	w->smooth = false;
	w->scrollCount = 0;
	DDB_OutputMessageToWindow(i, DDB_SYSMSG, 16, w);
	DDB_FlushWindow(i, w);
}

bool DDB_PAWSPrepareDescription(DDB_Interpreter* i, uint8_t location)
{
	SetFullScreen(i, 0);
	const uint8_t mode = i->flags[Flag_PAWMode] & 0x07;
	const uint8_t mask = (uint8_t)(1u << (location & 7));
	uint8_t* drawn = i->visited + (location >> 3);
	const bool redraw = (i->flags[Flag_GraphicFlags] & Graphics_Once) != 0;
	const bool off = (i->flags[Flag_GraphicFlags] & Graphics_Off) != 0;
	const bool always = (i->flags[Flag_GraphicFlags] & Graphics_On) != 0;
	const bool normal = (*drawn & mask) != 0;
	const bool eligible = DDB_HasVectorPicture(location);
	const bool draw = !off && mode != 1 && mode != 4 && eligible &&
		(always || redraw || normal);

	// Bit 7 requests one automatic attempt, not one successful drawing.
	i->flags[Flag_GraphicFlags] &= ~Graphics_Once;

	if (draw)
	{
		SCR_Clear(0, 0, screenWidth, screenHeight, VID_GetPaper());
		SCR_DrawVectorPicture(location);
		*drawn &= (uint8_t)~mask;

		if (mode == 0)
		{
			SCR_ConsumeBuffer();
			SCR_WaitForKey();
			DDB_PAWSClear(i);
			return true;
		}

		const int internal = DDB_PAWSValidatedInternalRow(i->flags[Flag_SplitLine]);
		const int row = 24 - internal;
		uint8_t clearPaper = SetPAWSVideoAttributes(i, &i->win);
		SCR_Clear(0, row * lineHeight, screenWidth,
			screenHeight - row * lineHeight, clearPaper);
		SetFullScreen(i, row);
		// The successful automatic-picture branch jumps over the flag-39 reset.
		// It captures the current internal cursor row before opening the stream;
		// at this point that is the LINE-selected split row.
		i->flags[Flag_TopLine] = (uint8_t)internal;
		i->win.scrollCount = 1;
		return true;
	}

	if (mode != 1)
	{
		DDB_PAWSClear(i);
	}
	return false;
}

void DDB_PAWSFinishDescription(DDB_Interpreter* i, bool automaticPictureDrawn)
{
	DDB_Flush(i);
	if (automaticPictureDrawn)
		return;
	const uint8_t mode = i->flags[Flag_PAWMode] & 0x07;
	if (mode == 4)
	{
		int row = i->win.posY / lineHeight;
		if (row < 0) row = 0;
		if (row > 24) row = 24;
		i->flags[Flag_TopLine] = (uint8_t)(24 - row);
	}
	else
	{
		i->flags[Flag_TopLine] = 24;
	}
}

int DDB_PAWSValidatedInternalRow(uint8_t row)
{
	return row >= 4 && row <= 24 ? row : 12;
}

int DDB_PAWSUserRow(uint8_t internalRow)
{
	return 24 - DDB_PAWSValidatedInternalRow(internalRow);
}

static void GetPAWSScrollRegion(DDB_Interpreter* i, int* topY, int* height)
{
	// Stream 2 uses the Spectrum's 22-row upper screen. Rows 22-23 belong to
	// the lower screen and are not part of ordinary PAW wrapping/scrolling.
	const int textHeight = screenHeight - 2 * lineHeight;
	int top = 0;
	if ((i->flags[Flag_PAWMode] & 0x07) >= 3)
		top = DDB_PAWSUserRow(i->flags[Flag_TopLine]) * lineHeight;
	if (top < 0) top = 0;
	if (top > textHeight - lineHeight) top = textHeight - lineHeight;
	*topY = top;
	*height = textHeight - top;
}

void DDB_PAWSRefreshWindow(DDB_Interpreter* i, DDB_Window* w)
{
	// The text region is derived from live flags: flag 39 is game-writable
	// and consulted at scroll time, so the window rect is refreshed here
	// instead of being cached when a condact changes it
	int topY, height;
	GetPAWSScrollRegion(i, &topY, &height);
	w->x = 0;
	w->width = screenWidth;
	w->y = (int16_t)topY;
	w->height = (int16_t)height;
}

void DDB_PAWSReloadScrollCounter(DDB_Interpreter* i, DDB_Window* w)
{
	// $8ACB subtracts the current internal S_POSN row (after its two lower
	// screen rows) from the normal/protected bottom boundary. In host row
	// coordinates this is the number of occupied rows in the usable region.
	int topY, height;
	GetPAWSScrollRegion(i, &topY, &height);
	int rows = (w->posY - topY) / lineHeight + 1;
	if (rows < 1) rows = 1;
	const int regionRows = height / lineHeight;
	if (rows > regionRows) rows = regionRows;
	w->scrollCount = (uint8_t)rows;
}

void DDB_PAWSStartPause(DDB_Interpreter* i, uint8_t ticks)
{
	i->pauseFrames = ticks == 0 ? 256 : ticks;
	i->pauseTickRemainderMs = 0;
	i->saveKeyToFlags = false;
	i->state = DDB_PAUSED;
}

bool DDB_PAWSAdvancePause(DDB_Interpreter* i, uint32_t elapsedMs)
{
	uint32_t total = i->pauseTickRemainderMs + elapsedMs;
	uint32_t ticks = total / DDB_PAWS_TICK_MS;
	i->pauseTickRemainderMs = total % DDB_PAWS_TICK_MS;
	if (ticks >= (uint32_t)i->pauseFrames)
	{
		i->pauseFrames = 0;
		return true;
	}
	i->pauseFrames -= ticks;
	return false;
}

void DDB_PAWSStartTimeout(DDB_Interpreter* i)
{
	i->timeout = true;
	i->timeoutRemainingMs = (int32_t)i->flags[Flag_Timeout] * DDB_PAWS_TIME_TICKS;
	i->timeoutTickRemainderMs = 0;
}

bool DDB_PAWSAdvanceTimeout(DDB_Interpreter* i, uint32_t elapsedMs)
{
	uint32_t total = i->timeoutTickRemainderMs + elapsedMs;
	uint32_t ticks = total / DDB_PAWS_TICK_MS;
	i->timeoutTickRemainderMs = total % DDB_PAWS_TICK_MS;
	if (ticks >= (uint32_t)i->timeoutRemainingMs)
	{
		i->timeoutRemainingMs = 0;
		return true;
	}
	i->timeoutRemainingMs -= ticks;
	return false;
}

#endif
