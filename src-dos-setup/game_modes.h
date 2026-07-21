#ifndef ADP_GAME_MODES_H
#define ADP_GAME_MODES_H

#include <ddb.h>
#include <stdint.h>

// Video modes the user can choose between in SETUP. A game whose data
// files support more than one of these needs an ADPSETUP.CFG to run.
#define SETUP_SELECTABLE_VIDEO_MODES \
	(DDB_DataFileMode_CGA | DDB_DataFileMode_EGA | DDB_DataFileMode_VGA16 | \
	 DDB_DataFileMode_VGA | DDB_DataFileMode_SHiRes)

uint32_t Setup_DetectGameVideoModes();

// Language of the game in the current directory, read from the first .DDB's
// header (byte 1 low nibble, as the interpreter does). Defaults to English.
DDB_Language Setup_DetectGameLanguage();

#endif
