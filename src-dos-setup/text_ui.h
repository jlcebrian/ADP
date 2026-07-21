#ifndef ADP_TEXT_UI_H
#define ADP_TEXT_UI_H

enum UIKey
{
	UIKey_None,
	UIKey_Up,
	UIKey_Down,
	UIKey_Left,
	UIKey_Right,
	UIKey_Enter,
	UIKey_Escape
};

// Localizable UI chrome. Defaults to English; SETUP overrides it from the
// strings table once the game language is known. Yes/No are padded to a fixed
// 7-column button width.
struct UILabels
{
	const char* yes;
	const char* no;
	const char* menuHelp;    // footer shown under menus
	const char* messageHelp; // footer shown under message boxes
};

void UI_SetLabels(const UILabels* labels);

void UI_Initialize();
void UI_Shutdown();
void UI_Clear();
void UI_DrawTitle(const char* title, const char* subtitle);
void UI_ShowMessageBox(const char* title, const char* message);
bool UI_Confirm(const char* title, const char* message, bool defaultYes = false);
int UI_SelectMenu(const char* title, const char* const* options, int optionCount, int selected);
UIKey UI_ReadKey();

#endif
