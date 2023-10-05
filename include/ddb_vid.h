#pragma once

#include <ddb.h>

extern bool   exitGame;

extern bool   VID_Initialize          ();
extern void   VID_Finish              ();

extern bool   VID_AnyKey              ();
extern bool   VID_LoadDataFile        (const char* filename);
extern bool   VID_PictureExists       (uint8_t picno);
extern bool   VID_SampleExists        (uint8_t no);
extern void   VID_Clear               (int x, int y, int w, int h, uint8_t color);
extern void   VID_ClearBuffer         (bool front);
extern void   VID_DisplayPicture      (int x, int y, int w, int h, DDB_ScreenMode screenMode);
extern bool   VID_DisplaySCRFile      (const char* fileName, DDB_Machine target);
extern void   VID_DrawCharacter       (int x, int y, uint8_t ch, uint8_t ink, uint8_t paper);
extern void   VID_DrawText            (int x, int y, const char* text, uint8_t ink, uint8_t paper);
extern void   VID_GetKey              (uint8_t* key, uint8_t* ext, uint8_t* modifiers);
extern void   VID_GetMilliseconds     (uint32_t* time);
extern void   VID_GetPaletteColor     (uint8_t color, uint8_t* r, uint8_t* g, uint8_t* b);
extern void   VID_GetPictureInfo      (bool* fixed, int16_t* x, int16_t* y, int16_t* w, int16_t* h);
extern void   VID_LoadPicture         (uint8_t picno, DDB_ScreenMode screenMode);
extern void   VID_MainLoop            (DDB_Interpreter* i, void (*callback)(int elapsed));
extern void   VID_MainLoopAsync       (DDB_Interpreter* i, void (*callback)(int elapsed));
extern void   VID_OpenFileDialog      (bool existing, char* filename, size_t bufferSize);
extern void   VID_PlaySample          (uint8_t no, int* duration);
extern void   VID_PlaySampleBuffer    (void* buffer, int samples, int hz, int volume);
extern void   VID_Quit                ();
extern void   VID_RestoreScreen       ();
extern void   VID_SaveScreen          ();
extern void   VID_Scroll              (int x, int y, int w, int h, int lines, uint8_t paper);
extern void   VID_SetOpBuffer         (SCR_Operation op, bool front);
extern void   VID_SetDefaultPalette   ();
extern void   VID_SetPaletteColor     (uint8_t color, uint8_t r, uint8_t g, uint8_t b);
extern void   VID_SetTextInputMode    (bool enabled);
extern void   VID_SwapScreen          ();
extern void   VID_UpdateInkMap        (DDB_ScreenMode mode);
extern void   VID_VSync               ();
extern void   VID_WaitForKey          ();		// Not suported in all platforms
extern void   VID_ShowError           (const char* msg);
extern void   VID_ActivatePalette     ();
extern void   VID_ShowProgressBar     (uint16_t amount);
extern void   VID_InnerLoop           ();

#ifdef HAS_CLIPBOARD
extern bool   VID_HasClipboardText    (uint32_t* size);
extern void   VID_GetClipboardText    (uint8_t* buffer, uint32_t bufferSize);
extern void   VID_SetClipboardText    (uint8_t* buffer, uint32_t bufferSize);
#endif

#ifdef HAS_FULLSCREEN
extern void   VID_ToggleFullscreen    ();
#endif
