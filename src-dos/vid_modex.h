#pragma once

#include <stdint.h>

enum VideoMode
{
	MODE_TEXT,
	MODE_320x200,
	MODE_320x240,
	MODE_320x400,
	MODE_360x270,
	MODE_400x300
};

void ModeX_SetVideoMode(int mode);
bool ModeX_BeginFixedPicturePresentation();
void ModeX_EndFixedPicturePresentation();
void ModeX_RestoreScreen();
void ModeX_SaveScreen();
void ModeX_SetActiveBuffer(bool front);
void ModeX_ClearBuffer(bool front, uint8_t color);
void ModeX_SwapBuffers();
void ModeX_BlitNativePicture(const uint8_t* input, int inputWidth, int inputHeight, int x, int y, int w, int h);
void ModeX_BlitLinearPicture(const uint8_t* input, int inputWidth, int x, int y, int w, int h);
void ModeX_DrawPackedPicture(const uint8_t* input, int inputWidth, int inputHeight, int x, int y, int w, int h);
void ModeX_ClearRect(int x, int y, int width, int height, uint8_t color);
void ModeX_DrawCharacter(int x, int y, uint8_t c, uint8_t ink, uint8_t paper);
void ModeX_Scroll(int x, int y, int w, int h, int lines, uint8_t paper);