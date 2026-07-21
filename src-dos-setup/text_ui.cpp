#ifdef _DOS

#include "text_ui.h"

#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdint.h>
#include <string.h>

static const int ScreenColumns = 80;
static const int ScreenRows = 25;

static const uint8_t ColorBackground = 1;
static const uint8_t ColorText = 15;
static const uint8_t ColorTitle = 14;
static const uint8_t ColorBox = 3;
static const uint8_t ColorSelection = 1;
static const uint8_t ColorShadow = 0;
static uint8_t currentAttribute = 0x07;

static uint16_t far* TextCell(int row, int column)
{
	return (uint16_t far*)MK_FP(0xB800,
		((row - 1) * ScreenColumns + column - 1) * 2);
}

static void Set80ColumnTextMode()
{
	union REGS registers;
	registers.w.ax = 0x0003;
	int86(0x10, &registers, &registers);

}

static void SetCursorShape(uint16_t shape)
{
	union REGS registers;
	registers.h.ah = 0x01;
	registers.w.cx = shape;
	int86(0x10, &registers, &registers);
}

static void SetCursorPosition(int row, int column)
{
	union REGS registers;
	registers.h.ah = 0x02;
	registers.h.bh = 0;
	registers.h.dh = (uint8_t)(row - 1);
	registers.h.dl = (uint8_t)(column - 1);
	int86(0x10, &registers, &registers);
}

static void SetColors(uint8_t foreground, uint8_t background)
{
	currentAttribute = (uint8_t)((background << 4) | foreground);
}

static void WriteAt(int row, int column, const char* text)
{
	uint16_t far* cell = TextCell(row, column);
	while (*text != 0)
		*cell++ = ((uint16_t)currentAttribute << 8) | (uint8_t)*text++;
}

static void WriteRepeated(int row, int column, unsigned char character, int count)
{
	uint16_t value = ((uint16_t)currentAttribute << 8) | character;
	uint16_t far* cell = TextCell(row, column);
	while (count-- > 0)
		*cell++ = value;
}

static void ClearWithCurrentColors()
{
	WriteRepeated(1, 1, ' ', ScreenColumns * ScreenRows);
}

static void DrawBox(int left, int top, int width, int height, const char* title)
{
	SetColors(ColorShadow, ColorShadow);
	for (int row = top + 1; row <= top + height; row++)
		WriteRepeated(row, left + 2, ' ', width);

	SetColors(ColorText, ColorBox);
	WriteAt(top, left, "\xDA");
	WriteRepeated(top, left + 1, '\xC4', width - 2);
	WriteAt(top, left + width - 1, "\xBF");

	for (int row = top + 1; row < top + height - 1; row++)
	{
		WriteAt(row, left, "\xB3");
		WriteRepeated(row, left + 1, ' ', width - 2);
		WriteAt(row, left + width - 1, "\xB3");
	}

	WriteAt(top + height - 1, left, "\xC0");
	WriteRepeated(top + height - 1, left + 1, '\xC4', width - 2);
	WriteAt(top + height - 1, left + width - 1, "\xD9");

	if (title != 0 && title[0] != 0)
	{
		SetColors(ColorTitle, ColorBox);
		WriteAt(top, left + 2, " ");
		WriteAt(top, left + 3, title);
		WriteAt(top, left + 3 + strlen(title), " ");
	}
}

static int LongestLine(const char* text)
{
	int longest = 0;
	int current = 0;

	while (*text != 0)
	{
		if (*text++ == '\n')
		{
			if (current > longest)
				longest = current;
			current = 0;
		}
		else
			current++;
	}
	return current > longest ? current : longest;
}

static int LineCount(const char* text)
{
	int count = 1;
	while (*text != 0)
		if (*text++ == '\n')
			count++;
	return count;
}

static void WriteLines(int row, int column, const char* text)
{
	char line[76];
	while (*text != 0)
	{
		int length = 0;
		while (*text != 0 && *text != '\n' && length < (int)sizeof(line) - 1)
			line[length++] = *text++;
		line[length] = 0;
		WriteAt(row++, column, line);
		if (*text == '\n')
			text++;
	}
}

void UI_Initialize()
{
	Set80ColumnTextMode();
	SetCursorShape(0x2000);
	UI_Clear();
}

void UI_Shutdown()
{
	SetCursorShape(0x0607);
	SetColors(7, 0);
	ClearWithCurrentColors();
	SetCursorPosition(1, 1);
}

void UI_Clear()
{
	SetColors(ColorText, ColorBackground);
	ClearWithCurrentColors();
}

void UI_DrawTitle(const char* title, const char* subtitle)
{
	UI_Clear();
	SetColors(ColorTitle, ColorBackground);
	WriteAt(2, (ScreenColumns - strlen(title)) / 2 + 1, title);
	SetColors(ColorText, ColorBackground);
	WriteAt(4, (ScreenColumns - strlen(subtitle)) / 2 + 1, subtitle);
	SetColors(7, ColorBackground);
	WriteAt(ScreenRows, 2, "Arrow keys move   ENTER selects   ESC returns");
}

UIKey UI_ReadKey()
{
	int key = _getch();
	if (key == 0 || key == 0xE0)
	{
		switch (_getch())
		{
			case 72: return UIKey_Up;
			case 80: return UIKey_Down;
			case 75: return UIKey_Left;
			case 77: return UIKey_Right;
		}
		return UIKey_None;
	}
	if (key == 13)
		return UIKey_Enter;
	if (key == 27)
		return UIKey_Escape;
	return UIKey_None;
}

void UI_ShowMessageBox(const char* title, const char* message)
{
	int width = LongestLine(message) + 6;
	int height = LineCount(message) + 4;
	if (width < 34)
		width = 34;
	if (width > 74)
		width = 74;

	int left = (ScreenColumns - width) / 2 + 1;
	int top = (ScreenRows - height) / 2 + 1;
	DrawBox(left, top, width, height, title);
	SetColors(ColorText, ColorBox);
	WriteLines(top + 2, left + 3, message);
	SetColors(ColorTitle, ColorBox);
	WriteAt(top + height - 2, left + (width - 18) / 2, "Press ENTER or ESC");

	while (true)
	{
		UIKey key = UI_ReadKey();
		if (key == UIKey_Enter || key == UIKey_Escape)
			return;
	}
}

bool UI_Confirm(const char* title, const char* message, bool defaultYes)
{
	int width = LongestLine(message) + 6;
	int height = LineCount(message) + 6;
	if (width < 42)
		width = 42;
	if (width > 74)
		width = 74;

	int left = (ScreenColumns - width) / 2 + 1;
	int top = (ScreenRows - height) / 2 + 1;
	int selected = defaultYes ? 0 : 1;

	DrawBox(left, top, width, height, title);
	SetColors(ColorText, ColorBox);
	WriteLines(top + 2, left + 3, message);

	while (true)
	{
		SetColors(ColorText,
			selected == 0 ? ColorSelection : ColorBox);
		WriteAt(top + height - 2, left + width / 2 - 8, "  Yes  ");
		SetColors(ColorText,
			selected == 1 ? ColorSelection : ColorBox);
		WriteAt(top + height - 2, left + width / 2 + 2, "  No   ");

		switch (UI_ReadKey())
		{
			case UIKey_Left:
			case UIKey_Right:
				selected = 1 - selected;
				break;
			case UIKey_Enter:
				return selected == 0;
			case UIKey_Escape:
				return false;
		}
	}
}

int UI_SelectMenu(const char* title, const char* const* options, int optionCount, int selected)
{
	int width = strlen(title) + 8;
	for (int index = 0; index < optionCount; index++)
	{
		int optionWidth = strlen(options[index]) + 8;
		if (optionWidth > width)
			width = optionWidth;
	}
	if (width < 32)
		width = 32;
	if (width > 70)
		width = 70;

	int height = optionCount + 4;
	int left = (ScreenColumns - width) / 2 + 1;
	int top = (ScreenRows - height) / 2 + 1;
	if (selected < 0 || selected >= optionCount)
		selected = 0;

	DrawBox(left, top, width, height, title);
	for (int index = 0; index < optionCount; index++)
	{
		SetColors(ColorText,
			index == selected ? ColorSelection : ColorBox);
		WriteRepeated(top + 2 + index, left + 2, ' ', width - 4);
		WriteAt(top + 2 + index, left + 4, options[index]);
	}

	while (true)
	{
		UIKey key = UI_ReadKey();
		if (key == UIKey_Up || key == UIKey_Down)
		{
			int previous = selected;
			if (key == UIKey_Up)
				selected = selected == 0 ? optionCount - 1 : selected - 1;
			else
				selected = selected == optionCount - 1 ? 0 : selected + 1;

			SetColors(ColorText, ColorBox);
			WriteRepeated(top + 2 + previous, left + 2, ' ', width - 4);
			WriteAt(top + 2 + previous, left + 4, options[previous]);
			SetColors(ColorText, ColorSelection);
			WriteRepeated(top + 2 + selected, left + 2, ' ', width - 4);
			WriteAt(top + 2 + selected, left + 4, options[selected]);
		}
		else if (key == UIKey_Enter)
			return selected;
		else if (key == UIKey_Escape)
			return -1;
	}
}

#endif
