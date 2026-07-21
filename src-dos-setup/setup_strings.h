#ifndef ADP_SETUP_STRINGS_H
#define ADP_SETUP_STRINGS_H

#include <ddb.h>   // DDB_Language

// User-facing text for SETUP, in English and Spanish. The active language is
// chosen from the game's DDB (see Setup_DetectGameLanguage), the same way the
// interpreter picks its runtime language. Technical tokens (mode names, kHz,
// IRQ/DMA/port values) are not translated and stay as literals at the call site.
enum SetupStringId
{
	STR_TITLE,
	STR_STATUS_SOUND,
	STR_STATUS_NOSOUND,
	STR_MENU_HEADER,
	STR_MENU_VIDEO,
	STR_MENU_SOUND,
	STR_MENU_TEST,
	STR_MENU_SAVE,
	STR_MENU_EXIT,

	STR_VIDEO_TITLE,
	STR_VIDEO_AUTODETECT,
	STR_VIDEO_CGA,
	STR_VIDEO_EGA,
	STR_VIDEO_VGA16,
	STR_VIDEO_VGA,
	STR_VIDEO_SVGA,
	STR_VIDEO_DETECT_TITLE,
	STR_VIDEO_DETECT_CONFIRM,
	STR_VIDEO_DETECT_NONE,
	STR_VIDEO_DETECT_OK,

	STR_QUALITY_TITLE,

	STR_SB_PORT,
	STR_SB_IRQ,
	STR_SB_DMA8,
	STR_SB_DMA16,
	STR_SB_DETECT_TITLE,
	STR_SB_DETECT_CONFIRM,
	STR_SB_DETECT_NONE,
	STR_SB_DETECT_OK,

	STR_SOUND_TITLE,
	STR_SOUND_ENABLE_FIRST,

	STR_CARD_TITLE,
	STR_CARD_NONE,
	STR_CARD_AUTODETECT,
	STR_CARD_DETECTED,

	STR_TEST_TITLE,
	STR_TEST_DISABLED,
	STR_TEST_BEGIN,
	STR_TEST_NORESPONSE,
	STR_TEST_DMAFAIL,
	STR_TEST_HEARD,
	STR_TEST_RETRY,

	STR_WIZ_TITLE,
	STR_WIZ_INTRO,
	STR_WIZ_RESULT,
	STR_WIZ_RESULT_TITLE,
	STR_WIZ_NOSB,

	STR_SAVE_ERR_TITLE,
	STR_SAVE_ERR,
	STR_EXIT_TITLE,
	STR_EXIT_CONFIRM,
	STR_NODATA_TITLE,
	STR_NODATA,

	// UI chrome (passed to text_ui via UI_SetLabels). The Yes/No values are
	// padded to a fixed 7-column button width.
	STR_UI_YES,
	STR_UI_NO,
	STR_UI_MENUHELP,
	STR_UI_MSGHELP,

	STR_COUNT
};

void        SetupSetLanguage(DDB_Language language);
const char* SetupText(SetupStringId id);

#endif
