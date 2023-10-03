#ifndef __DDB_H__
#define __DDB_H__

#include <os_types.h>

#define MAX_DDB_SIZE	 65536
#define MAX_PROC_STACK 	 16
#define HISTORY_SIZE     1024
#define UNDO_BUFFER_SIZE 512

/* ───────────────────────────────────────────────────────────────────────── */
/*  .DDB - DAAD Database Functions			                                 */
/* ───────────────────────────────────────────────────────────────────────── */

struct DDB_Interpreter;

enum DDB_WordType
{
	WordType_Verb         = 0,
	WordType_Adverb       = 1,
	WordType_Noun         = 2,
	WordType_Adjective    = 3,
	WordType_Preposition  = 4,
	WordType_Conjunction  = 5,
	WordType_Pronoun      = 6,
};

enum DDB_Flag
{
	Flag_Darkness 		= 0,	
	Flag_NumCarried		= 1,			// 01  Numbers of objects currently carried by the player

	// Flags 2 to 28 are unused

	Flag_GraphicFlags	= 29,			// 1D  Graphic flags (can be checked with HASAT):

			// ^ Bit 7: 1 if graphics are available
			//   Bit 6: 1 do not clear window before desc		(manual: 1 to set invisible draw mode)
			//   Bit 5: 1 to turn pictures off
			//   Bit 4: 1 to wait for a key after drawing picture
			//   Bit 3: 1 to change border to picture colours
			//   Bit 0: 1 if mouse is present
			//
			// Old condact GRAPHIC [0-3] [0-1] sets bits 3-6 of this flag

	Flag_Score     		= 30,			// 1E  User score
	Flag_Turns     		= 31,			// 1F  Turn count (2 bytes) incremented by PARSE 0

	Flag_Verb      		= 33,			// 21
	Flag_Noun1     		= 34,			// 22
	Flag_Adjective1 	= 35,			// 23
	Flag_Adverb    		= 36,			// 24
	Flag_Preposition    = 43,			// 2B
	Flag_Noun2     		= 44,			// 2C
	Flag_Adjective2     = 45,			// 2D
	Flag_CPNoun    		= 46,			// 2E  Current pronoun noun ("IT")
	Flag_CPAdjective    = 47,			// 2F  Current pronoun adjective ("IT")

	Flag_MaxCarried		= 37,			// 25  Maximum number of objects to be carried (ABILITY)
	Flag_Locno     		= 38,			// 26  Current player location

	// Flags 39 and 40 are unused

	Flag_InputStream    = 41,			// 29  Input stream (window 1-7, 0 = current)
	Flag_Prompt    		= 42,			// 2A  System message for prompt (0 = random)
	Flag_Timeout   		= 48,			// 30  Timeout in seconds
	Flag_TimeoutFlags	= 49,			// 31  Timeout control flags

			// ^ Bit 0: 1 to enable timeout during input
			//   Bit 1: 1 to enable timeout during the "More..." prompt
			//   Bit 2: 1 to enable timeout during the ANYKEY action
			//   Bit 7: 1 if there was a timeout last frame

	Flag_DoAllObjNo		= 50,			// 32  Object number for DOALL
	Flag_Objno     		= 51,			// 33  Current object (for _) referenced by WHATO etc.
	Flag_Strength  		= 52,			// 34  Maximum weight that can be carried (ABILITY)
	Flag_ListFlags		= 53,			// 35  Object list print flags
	Flag_ObjLocno  		= 54,			// 36  Location of the currently referenced object
	Flag_ObjWeight    	= 55,			// 37  Weight of the currently referenced object
	Flag_ObjContainer   = 56,			// 38  128 is current object is a container
	Flag_ObjWearable 	= 57,			// 39  128 if current object is wearable

	// The following flags are version 2 only

	Flag_ObjAttributes  = 58,			// 3A  (2 bytes) Attributes of the current object
	Flag_Key1      		= 60,			// 3C  Current ASCII key code after INKEY
	Flag_Key2      		= 61,			// 3D  Extended key code after INKEY
	Flag_ScreenMode 	= 62,			// 3E  2: Text, 4: CGA, 13: EGA, 141: VGA
	Flag_Window    		= 63,			// 3F  Which window is active at the moment

	// Flags 64 to 255 are unused
};

enum DDB_ObjectFlags
{
	Obj_Container = 0x40,
	Obj_Wearable  = 0x80,
	Obj_Weight    = 0x3F
};

enum DDB_GraphicsFlags
{
	Graphics_MousePresnet    = 0x01,
	Graphics_ColorBorder     = 0x08,
	Graphics_KeyAfterPicture = 0x10,
	Graphics_PicturesOff     = 0x20,
	Graphics_NoClsBeforeDesc = 0x40,
	Graphics_Available       = 0x80,
};

enum DDB_WindowFlags
{
	Win_ForceGraphics = 0x01,
	Win_NoMorePrompt  = 0x02,
};

enum DDB_TimeoutFlags
{
	Timeout_Input      = 0x01,
	Timeout_MorePrompt = 0x02,
	Timeout_AnyKey     = 0x04,
	Timeout_LastFrame  = 0x80,
};

enum DDB_InputFlags
{
	Input_ClearWindow       = 0x01,
	Input_PrintAfterInput   = 0x02,
	Input_PrintAfterTimeout = 0x04,
};

enum DDB_Location
{
	Loc_Destroyed = 252,
	Loc_Worn      = 253,
	Loc_Carried   = 254,
	Loc_Here      = 255
};

enum DDB_ScreenMode
{
	ScreenMode_Text   = 0x02,
	ScreenMode_CGA    = 0x04,
	ScreenMode_EGA    = 0x0D,
	ScreenMode_VGA16  = 0x8D,
};

enum DDB_Error
{
	DDB_ERROR_NONE,
	DDB_ERROR_FILE_NOT_FOUND,
	DDB_ERROR_READING_FILE,
	DDB_ERROR_SEEKING_FILE,
	DDB_ERROR_OUT_OF_MEMORY,
	DDB_ERROR_INVALID_FILE,
	DDB_ERROR_SDL,
	DDB_ERROR_NO_DDBS_FOUND,
};

enum DDB_Condact
{
	CONDACT_ABILITY,
	CONDACT_ABSENT,
	CONDACT_ADD,
	CONDACT_ADJECT1,
	CONDACT_ADJECT2,
	CONDACT_ADVERB,
	CONDACT_ANYKEY,
	CONDACT_AT,
	CONDACT_ATGT,
	CONDACT_ATLT,
	CONDACT_AUTOD,
	CONDACT_AUTOG,
	CONDACT_AUTOP,
	CONDACT_AUTOR,
	CONDACT_AUTOT,
	CONDACT_AUTOW,
	CONDACT_BACKAT,
	CONDACT_BEEP,
	CONDACT_BIGGER,
	CONDACT_BORDER,
	CONDACT_CALL,
	CONDACT_CARRIED,
	CONDACT_CENTRE,
	CONDACT_CHANCE,
	CONDACT_CLEAR,
	CONDACT_CLS,
	CONDACT_COPYBF,
	CONDACT_COPYFF,
	CONDACT_COPYFO,
	CONDACT_COPYOF,
	CONDACT_COPYOO,
	CONDACT_CREATE,
	CONDACT_DESC,
	CONDACT_DESTROY,
	CONDACT_DISPLAY,
	CONDACT_DOALL,
	CONDACT_DONE,
	CONDACT_DPRINT,
	CONDACT_DROP,
	CONDACT_DROPALL,
	CONDACT_END,
	CONDACT_EQ,
	CONDACT_EXIT,
	CONDACT_EXTERN,
	CONDACT_GET,
	CONDACT_GFX,
	CONDACT_GOTO,
	CONDACT_GT,
	CONDACT_HASAT,
	CONDACT_HASNAT,
	CONDACT_INK,
	CONDACT_INKEY,
	CONDACT_INPUT,
	CONDACT_ISAT,
	CONDACT_ISDONE,
	CONDACT_ISNDONE,
	CONDACT_ISNOTAT,
	CONDACT_LET,
	CONDACT_LISTAT,
	CONDACT_LISTOBJ,
	CONDACT_LOAD,
	CONDACT_LT,
	CONDACT_MES,
	CONDACT_MESSAGE,
	CONDACT_MINUS,
	CONDACT_MODE,
	CONDACT_MOUSE,
	CONDACT_MOVE,
	CONDACT_NEWLINE,
	CONDACT_NEWTEXT,
	CONDACT_NOTAT,
	CONDACT_NOTCARR,
	CONDACT_NOTDONE,
	CONDACT_NOTEQ,
	CONDACT_NOTSAME,
	CONDACT_NOTWORN,
	CONDACT_NOTZERO,
	CONDACT_NOUN2,
	CONDACT_OK,
	CONDACT_PAPER,
	CONDACT_PARSE,
	CONDACT_PAUSE,
	CONDACT_PICTURE,
	CONDACT_PLACE,
	CONDACT_PLUS,
	CONDACT_PREP,
	CONDACT_PRESENT,
	CONDACT_PRINT,
	CONDACT_PRINTAT,
	CONDACT_PROCESS,
	CONDACT_PROMPT,
	CONDACT_PUTIN,
	CONDACT_PUTO,
	CONDACT_QUIT,
	CONDACT_RAMLOAD,
	CONDACT_RAMSAVE,
	CONDACT_RANDOM,
	CONDACT_REDO,
	CONDACT_REMOVE,
	CONDACT_RESET,
	CONDACT_RESTART,
	CONDACT_SAME,
	CONDACT_SAVE,
	CONDACT_SAVEAT,
	CONDACT_SET,
	CONDACT_SETCO,
	CONDACT_SFX,
	CONDACT_SKIP,
	CONDACT_SMALLER,
	CONDACT_SPACE,
	CONDACT_SUB,
	CONDACT_SWAP,
	CONDACT_SYNONYM,
	CONDACT_SYSMESS,
	CONDACT_TAB,
	CONDACT_TAKEOUT,
	CONDACT_TIME,
	CONDACT_TIMEOUT,
	CONDACT_TURNS,
	CONDACT_WEAR,
	CONDACT_WEIGH,
	CONDACT_WEIGHT,
	CONDACT_WHATO,
	CONDACT_WINAT,
	CONDACT_WINDOW,
	CONDACT_WINSIZE,
	CONDACT_WORN,
	CONDACT_ZERO,

	CONDACT_INVALID,
};

struct DDB_CondactMap
{
	DDB_Condact	condact;
	uint8_t     parameters;
};

enum DDB_Machine
{
	DDB_MACHINE_IBMPC		= 0,
	DDB_MACHINE_SPECTRUM,
	DDB_MACHINE_C64,
	DDB_MACHINE_CPC,
	DDB_MACHINE_MSX,
	DDB_MACHINE_ATARIST,
	DDB_MACHINE_AMIGA,
	DDB_MACHINE_PCW,
	DDB_MACHINE_PLUS4		= 14,		// Commodore Plus/4
	DDB_MACHINE_MSX2
};

enum DDB_Language
{
	DDB_ENGLISH,
	DDB_SPANISH
};

enum DDB_Flow
{
	FLOW_DESC,
	FLOW_AFTER_TURN,
	FLOW_INPUT,
	FLOW_RESPONSES,
};

struct DDB
{
	uint8_t 		version;			// 1: Original/Jabato - 2: Later
	DDB_Machine		target;				// Target machine
	DDB_Language	language;			// Target language
	DDB_CondactMap*	condactMap;			// maps 0-127 to condacts

	bool			littleEndian;
	bool			oldMainLoop;

	uint8_t			numObjects;
	uint8_t			numLocations;
	uint8_t			numMessages;
	uint8_t			numSystemMessages;
	uint8_t			numProcesses;

	bool			hasTokens;
	uint8_t*        tokens;
	uint8_t*		tokensPtr[128];
	size_t 			tokenBlockSize;

	uint16_t*		processTable;
	uint16_t*		msgTable;
	uint16_t*		sysMsgTable;
	uint16_t*		objNamTable;
	uint8_t*		objWordsTable;
	uint8_t*		objAttrTable;
	uint16_t*		objExAttrTable;
	uint8_t*		objLocTable;
	uint16_t*		locDescTable;
	uint16_t*		connections;
	uint8_t*		vocabulary;
	
	// Data storage: all pointers above are required to point to this block

	uint8_t*		data;
	uint16_t		dataSize;
	uint16_t		baseOffset;
};

struct DDB_Window
{
	uint16_t		x;
	uint16_t		y;
	uint16_t		width;
	uint16_t		height;

	uint16_t		posX;
	uint16_t		posY;
	uint8_t			ink;
	uint8_t			paper;

	uint8_t			flags;
	uint16_t		saveX;
	uint16_t		saveY;

	bool			graphics;
	bool 			smooth;

	uint8_t			scrollCount;			// For More... prompt
};

typedef struct
{
	uint8_t			process;
	uint16_t		entry;
	uint16_t		offset;
}
DDB_ProcAddr;

typedef enum
{
	DDB_MSG,
	DDB_SYSMSG,
	DDB_OBJNAME,
	DDB_LOCDESC
}
DDB_MsgType;

typedef enum
{
	DDB_RUNNING,
	DDB_PAUSED,
	DDB_FINISHED,
	DDB_FATAL_ERROR,
	DDB_QUIT,
	DDB_WAITING_FOR_KEY,
	DDB_CHECKING_KEY,
	DDB_VSYNC,

	// Remaining ones are input states
	DDB_INPUT,
	DDB_INPUT_QUIT,
	DDB_INPUT_END,
	DDB_INPUT_SAVE,
	DDB_INPUT_LOAD
}
DDB_State;

typedef enum
{
	SentenceFlag_UnknownWord 	= 0x01,
	SentenceFlag_Question		= 0x02,
	SentenceFlag_Colon			= 0x04,
}
DDB_SentenceFlags;

struct DDB_Interpreter
{
	DDB*			ddb;
	DDB_ScreenMode  screenMode;
	DDB_State		state;
	DDB_Flow		oldMainLoopState;
	int             pauseFrames;
	uint32_t        pauseStart;
	uint32_t        quitStart;

	uint8_t*		buffer;
	size_t			bufferSize;
	uint16_t		saveStateSize;

	int				keyClick;
	bool			keyChecked;
	bool			keyCheckInProgress;
	bool			keyPressed;
	uint32_t        lastClick;
	uint8_t         keyReuseCount;
	uint8_t			lastKey1;
	uint8_t			lastKey2;

	bool			timeout;
	int				timeoutRemainingMs;

	// State saved inside buffer

	uint8_t*	 	flags;			// Guaranteed to be equal to buffer
	uint8_t*		objloc;
	uint8_t*		ramSaveArea;
	bool            ramSaveAvailable;

	DDB_Window		win;
	DDB_Window		windef[8];

	uint8_t			curwin;
	uint8_t			inputWindow;
	uint8_t			inputFlags;
	uint8_t			cellX;
	uint8_t			cellW;

			// ^ Bit 0: 1 to clear window after input
			//   Bit 1: 1 to print input line to current stream after input
			//   Bit 2: 1 to print input line to current stream after timeout

	uint8_t         prompt;

	uint8_t         currentPicture;

	DDB_ProcAddr	procstack[MAX_PROC_STACK];
	uint8_t			procstackptr;
	bool			doall;
	uint8_t         doallDepth;
	uint8_t         doallLocno;
	uint8_t         doallProcess;
	uint16_t        doallEntry;
	uint16_t        doallOffset;
	bool 			done;

	uint8_t			inputHistory[HISTORY_SIZE];
	uint16_t		inputHistoryLength;
	uint16_t		inputHistoryLastEntry;
	uint16_t		inputHistoryCurrentEntry;
	uint8_t			undoBuffer[UNDO_BUFFER_SIZE];
	uint16_t		undoBufferLength;
	uint16_t		undoBufferCurrentEntry;
	uint8_t			sentenceFlags;

	uint8_t 		inputBuffer[256];
	uint8_t			inputBufferPtr;
	uint8_t			inputBufferLength;
	uint8_t			inputCursorX;
	uint8_t         inputCompletionX;
	uint8_t*        quotedString;
	uint8_t			quotedStringLength;

	uint8_t			pending[64];
	uint8_t			pendingPtr;
};

enum SCR_Operation
{
	SCR_OP_DRAWTEXT,
	SCR_OP_DRAWPICTURE
};

enum PlayerState
{
	Player_Starting,
	Player_SelectingPart,
	Player_ShowingScreen,
	Player_FadingOut,
	Player_InGame,
	Player_Finished,
	Player_Error,
};

extern DDB_Interpreter* interpreter;

typedef int (*DDB_PrintFunc)(const char* format, ...);

extern DDB*				DDB_Load				 (const char* filename);
extern bool             DDB_Check                (const char* filename, DDB_Machine* target, DDB_Language* language, int* version);
extern DDB*             DDB_Create               ();
extern const char* 		DDB_GetDebugMessage 	 (DDB* ddb, DDB_MsgType type, uint8_t msgId);
extern void 			DDB_GetMessage 			 (DDB* ddb, DDB_MsgType type, uint8_t msgId, char* buffer, size_t bufferSize);
extern void				DDB_Dump				 (DDB* ddb, DDB_PrintFunc print);
extern void				DDB_DumpMetrics			 (DDB* ddb, DDB_PrintFunc print);
extern void 			DDB_DumpProcess			 (DDB* ddb, uint8_t process, DDB_PrintFunc print);
extern void				DDB_Close				 (DDB* ddb);
 
extern DDB_Interpreter* DDB_CreateInterpreter	 (DDB* ddb);
extern void				DDB_Run					 (DDB_Interpreter* interpreter);
extern void				DDB_Step				 (DDB_Interpreter* interpreter, int lines);
extern void				DDB_Reset				 (DDB_Interpreter* interpreter);
extern void				DDB_ResetWindows		 (DDB_Interpreter* interpreter);
extern void				DDB_CloseInterpreter	 (DDB_Interpreter* interpreter);

extern DDB_Error    	DDB_GetError             ();
extern void		    	DDB_SetError             (DDB_Error error);
extern const char*  	DDB_GetErrorString       ();
extern void         	DDB_SetWarningHandler    (void (*handler)(const char* message));
extern void             DDB_Warning              (const char* format, ...);
extern const char*      DDB_GetCondactName       (DDB_Condact condact);

extern void             DDB_Flush                (DDB_Interpreter* i);
extern void             DDB_FlushWindow          (DDB_Interpreter* i, DDB_Window* w);
extern void             DDB_ResetScrollCounts    (DDB_Interpreter* i);
extern void             DDB_OutputInputPrompt    (DDB_Interpreter* i);
extern void             DDB_OutputText           (DDB_Interpreter* i, const char* text);
extern bool             DDB_OutputMessage        (DDB_Interpreter* i, DDB_MsgType type, uint8_t index);
extern void             DDB_Desc                 (DDB_Interpreter* i, uint8_t locno);
extern bool             DDB_NewLine              (DDB_Interpreter* i);
extern bool             DDB_NewLineAtWindow      (DDB_Interpreter* i, DDB_Window* w);
extern bool             DDB_NextLine             (DDB_Interpreter* i);
extern bool             DDB_NextLineAtWindow     (DDB_Interpreter* i, DDB_Window* w);
extern void             DDB_ClearWindow          (DDB_Interpreter* i, DDB_Window* w);
extern void             DDB_SetWindow            (DDB_Interpreter* i, int winno);
extern void             DDB_PlayClick            (DDB_Interpreter* i, bool allowRepeats);
extern void             DDB_CalculateCells       (DDB_Interpreter* i, DDB_Window* w, uint8_t* cellX, uint8_t* cellW);

extern void             DDB_ProcessInputFrame    ();
extern void             DDB_StartInput           (DDB_Interpreter* i);
extern void             DDB_FinishInput          (DDB_Interpreter * i, bool timeout);
extern void             DDB_PrintInputLine       (DDB_Interpreter* i, bool withCursor);
extern void             DDB_ResolveInputEnd      (DDB_Interpreter* i);
extern void             DDB_ResolveInputQuit     (DDB_Interpreter* i);
extern void             DDB_ResolveInputLoad     (DDB_Interpreter* i);
extern void             DDB_ResolveInputSave     (DDB_Interpreter* i);
extern void             DDB_NewText              (DDB_Interpreter* i);
extern DDB_Window*      DDB_GetInputWindow       (DDB_Interpreter* i);

extern PlayerState      DDB_RunPlayerAsync       (const char* location);
extern bool             DDB_RunPlayer            ();
extern void             DDB_RestartAsyncPlayer   ();

extern bool             SCR_GetScreen            (const char* fileName, DDB_Machine target, uint8_t* buffer, size_t bufferSize, uint8_t* output, int width, int height, uint32_t* palette);
#endif