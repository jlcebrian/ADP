#include <ddb.h>
#include <ddb_vid.h>
#include <os_file.h>
#include <os_lib.h>
#include <os_mem.h>
#include <os_bito.h>

#if _STDCLIB
#include <stdio.h>
#include <stdarg.h>
#endif

DDB_Interpreter*    interpreter;

static DDB_Error 	ddbError = DDB_ERROR_NONE;
static void 		(*warningHandler)(const char* message) = 0;

static DDB_CondactMap version1Condacts[128] = {
	{ CONDACT_AT,     		  1 },		// 0x00
	{ CONDACT_NOTAT,  		  1 },		// 0x01
	{ CONDACT_ATGT,           1 },		// 0x02
	{ CONDACT_ATLT,   		  1 },		// 0x03
	{ CONDACT_PRESENT,		  1 },		// 0x04
	{ CONDACT_ABSENT, 		  1 },		// 0x05
	{ CONDACT_WORN,   		  1 },		// 0x06
	{ CONDACT_NOTWORN,		  1 },		// 0x07
	{ CONDACT_CARRIED,		  1 },		// 0x08
	{ CONDACT_NOTCARR,		  1 },		// 0x09
	{ CONDACT_CHANCE, 		  1 },		// 0x0A
	{ CONDACT_ZERO,   		  1 },		// 0x0B
	{ CONDACT_NOTZERO,		  1 },		// 0x0C
	{ CONDACT_EQ,     		  2 },		// 0x0D
	{ CONDACT_GT,     		  2 },		// 0x0E
	{ CONDACT_LT,     		  2 },		// 0x0F
	{ CONDACT_ADJECT1,		  1 },		// 0x10
	{ CONDACT_ADVERB, 		  1 },		// 0x11
	{ CONDACT_SFX,    		  2 },		// 0x12
	{ CONDACT_DESC,   		  0 },		// 0x13
	{ CONDACT_QUIT,   		  0 },		// 0x14
	{ CONDACT_END,    		  0 },		// 0x15
	{ CONDACT_DONE,   		  0 },		// 0x16
	{ CONDACT_OK,     		  0 },		// 0x17
	{ CONDACT_ANYKEY, 		  0 },		// 0x18
	{ CONDACT_SAVE,   		  0 },		// 0x19
	{ CONDACT_LOAD,   		  0 },		// 0x1A
	{ CONDACT_TURNS,    	  0 },		// 0x1B
	{ CONDACT_DISPLAY,  	  1 },		// 0x1C
	{ CONDACT_CLS,    		  0 },		// 0x1D
	{ CONDACT_DROPALL,		  0 },		// 0x1E
	{ CONDACT_AUTOG,  		  0 },		// 0x1F
	{ CONDACT_AUTOD,  		  0 },		// 0x20
	{ CONDACT_AUTOW,  		  0 },		// 0x21
	{ CONDACT_AUTOR,  		  0 },		// 0x22
	{ CONDACT_PAUSE,  		  1 },		// 0x23
	{ CONDACT_TIMEOUT, 	 	  0 },		// 0x24
	{ CONDACT_GOTO,   		  1 },		// 0x25
	{ CONDACT_MESSAGE,		  1 },		// 0x26
	{ CONDACT_REMOVE, 		  1 },		// 0x27
	{ CONDACT_GET,    		  1 },		// 0x28
	{ CONDACT_DROP,   		  1 },		// 0x29
	{ CONDACT_WEAR,   		  1 },		// 0x2A
	{ CONDACT_DESTROY,		  1 },		// 0x2B
	{ CONDACT_CREATE, 		  1 },		// 0x2C
	{ CONDACT_SWAP,   		  2 },		// 0x2D
	{ CONDACT_PLACE,  		  2 },		// 0x2E
	{ CONDACT_SET,    		  1 },		// 0x2F
	{ CONDACT_CLEAR,  		  1 },		// 0x30
	{ CONDACT_PLUS,   		  2 },		// 0x31
	{ CONDACT_MINUS,  		  2 },		// 0x32
	{ CONDACT_LET,    		  2 },		// 0x33
	{ CONDACT_NEWLINE,		  0 },		// 0x34
	{ CONDACT_PRINT,  		  1 },		// 0x35
	{ CONDACT_SYSMESS,		  1 },		// 0x36
	{ CONDACT_ISAT,   		  2 },		// 0x37
	{ CONDACT_COPYOF,  		  2 },		// 0x38
	{ CONDACT_COPYOO,  		  2 },		// 0x39
	{ CONDACT_COPYFO,  		  2 },		// 0x3A
	{ CONDACT_COPYFF, 		  2 },		// 0x3B
	{ CONDACT_LISTOBJ,		  0 },		// 0x3C
	{ CONDACT_EXTERN, 		  2 },		// 0x3D
	{ CONDACT_RAMSAVE,		  0 },		// 0x3E
	{ CONDACT_RAMLOAD,		  1 },		// 0x3F
	{ CONDACT_BEEP,   		  2 },		// 0x40
	{ CONDACT_PAPER,  		  1 },		// 0x41
	{ CONDACT_INK,    		  1 },		// 0x42
	{ CONDACT_BORDER, 		  1 },		// 0x43
	{ CONDACT_PREP,   		  1 },		// 0x44
	{ CONDACT_NOUN2,  		  1 },		// 0x45
	{ CONDACT_ADJECT2,		  1 },		// 0x46
	{ CONDACT_ADD,    		  2 },		// 0x47
	{ CONDACT_SUB,    		  2 },		// 0x48
	{ CONDACT_PARSE,  		  0 },		// 0x49
	{ CONDACT_LISTAT, 		  1 },		// 0x4A
	{ CONDACT_PROCESS,		  1 },		// 0x4B
	{ CONDACT_SAME,   		  2 },		// 0x4C
	{ CONDACT_MES,    		  1 },		// 0x4D
	{ CONDACT_WINDOW, 		  1 },		// 0x4E
	{ CONDACT_NOTEQ,  		  2 },		// 0x4F
	{ CONDACT_NOTSAME,		  2 },		// 0x50
	{ CONDACT_MODE,   		  1 },		// 0x51
	{ CONDACT_WINAT,  		  2 },		// 0x52
	{ CONDACT_TIME,   		  2 },		// 0x53
	{ CONDACT_PICTURE,		  1 },		// 0x54
	{ CONDACT_DOALL,  		  1 },		// 0x55
	{ CONDACT_PROMPT,  		  1 },		// 0x56
	{ CONDACT_GRAPHIC, 		  2 },		// 0x57
	{ CONDACT_ISNOTAT,		  2 },		// 0x58
	{ CONDACT_WEIGH,  		  2 },		// 0x59
	{ CONDACT_PUTIN,  		  2 },		// 0x5A
	{ CONDACT_TAKEOUT,		  2 },		// 0x5B
	{ CONDACT_NEWTEXT,		  0 },		// 0x5C
	{ CONDACT_ABILITY,		  2 },		// 0x5D
	{ CONDACT_WEIGHT, 		  1 },		// 0x5E
	{ CONDACT_RANDOM, 		  1 },		// 0x5F
	{ CONDACT_INPUT,  		  2 },		// 0x60
	{ CONDACT_SAVEAT, 		  0 },		// 0x61
	{ CONDACT_BACKAT, 		  0 },		// 0x62
	{ CONDACT_PRINTAT,		  2 },		// 0x63
	{ CONDACT_WHATO,  		  0 },		// 0x64
	{ CONDACT_RESET,   		  1 },		// 0x65
	{ CONDACT_PUTO,   		  1 },		// 0x66
	{ CONDACT_NOTDONE,		  0 },		// 0x67
	{ CONDACT_AUTOP,  		  1 },		// 0x68
	{ CONDACT_AUTOT,  		  1 },		// 0x69
	{ CONDACT_MOVE,   		  1 },		// 0x6A
	{ CONDACT_WINSIZE,		  2 },		// 0x6B
	{ CONDACT_REDO,   		  0 },		// 0x6C

	// --------------------

	{ CONDACT_INVALID,        0 },		// 0x6D
	{ CONDACT_INVALID,        0 },		// 0x6E
	{ CONDACT_INVALID,        0 },		// 0x6F
	{ CONDACT_INVALID,        0 },		// 0x70
	{ CONDACT_INVALID,        0 },		// 0x71
	{ CONDACT_INVALID,        0 },		// 0x72
	{ CONDACT_INVALID,        0 },		// 0x73
	{ CONDACT_INVALID,        0 },		// 0x74
	{ CONDACT_INVALID,        0 },		// 0x75
	{ CONDACT_INVALID,        0 },		// 0x76
	{ CONDACT_INVALID,        0 },		// 0x77
	{ CONDACT_INVALID,        0 },		// 0x78
	{ CONDACT_INVALID,        0 },		// 0x79
	{ CONDACT_INVALID,        0 },		// 0x7A
	{ CONDACT_INVALID,        0 },		// 0x7B
	{ CONDACT_INVALID,        0 },		// 0x7C
	{ CONDACT_INVALID,        0 },		// 0x7D
	{ CONDACT_INVALID,        0 },		// 0x7E
	{ CONDACT_INVALID,        0 },		// 0x7F
};

static DDB_CondactMap version2Condacts[128] = {
	{ CONDACT_AT,     		  1 },		// 0x00
	{ CONDACT_NOTAT,  		  1 },		// 0x01
	{ CONDACT_ATGT,           1 },		// 0x02
	{ CONDACT_ATLT,   		  1 },		// 0x03
	{ CONDACT_PRESENT,		  1 },		// 0x04
	{ CONDACT_ABSENT, 		  1 },		// 0x05
	{ CONDACT_WORN,   		  1 },		// 0x06
	{ CONDACT_NOTWORN,		  1 },		// 0x07
	{ CONDACT_CARRIED,		  1 },		// 0x08
	{ CONDACT_NOTCARR,		  1 },		// 0x09
	{ CONDACT_CHANCE, 		  1 },		// 0x0A
	{ CONDACT_ZERO,   		  1 },		// 0x0B
	{ CONDACT_NOTZERO,		  1 },		// 0x0C
	{ CONDACT_EQ,     		  2 },		// 0x0D
	{ CONDACT_GT,     		  2 },		// 0x0E
	{ CONDACT_LT,     		  2 },		// 0x0F
	{ CONDACT_ADJECT1,		  1 },		// 0x10
	{ CONDACT_ADVERB, 		  1 },		// 0x11
	{ CONDACT_SFX,    		  2 },		// 0x12
	{ CONDACT_DESC,   		  1 },		// 0x13
	{ CONDACT_QUIT,   		  0 },		// 0x14
	{ CONDACT_END,    		  0 },		// 0x15
	{ CONDACT_DONE,   		  0 },		// 0x16
	{ CONDACT_OK,     		  0 },		// 0x17
	{ CONDACT_ANYKEY, 		  0 },		// 0x18
	{ CONDACT_SAVE,   		  1 },		// 0x19
	{ CONDACT_LOAD,   		  1 },		// 0x1A
	{ CONDACT_DPRINT,   	  1 },		// 0x1B
	{ CONDACT_DISPLAY,  	  1 },		// 0x1C
	{ CONDACT_CLS,    		  0 },		// 0x1D
	{ CONDACT_DROPALL,		  0 },		// 0x1E
	{ CONDACT_AUTOG,  		  0 },		// 0x1F
	{ CONDACT_AUTOD,  		  0 },		// 0x20
	{ CONDACT_AUTOW,  		  0 },		// 0x21
	{ CONDACT_AUTOR,  		  0 },		// 0x22
	{ CONDACT_PAUSE,  		  1 },		// 0x23
	{ CONDACT_SYNONYM, 	 	  2 },		// 0x24
	{ CONDACT_GOTO,   		  1 },		// 0x25
	{ CONDACT_MESSAGE,		  1 },		// 0x26
	{ CONDACT_REMOVE, 		  1 },		// 0x27
	{ CONDACT_GET,    		  1 },		// 0x28
	{ CONDACT_DROP,   		  1 },		// 0x29
	{ CONDACT_WEAR,   		  1 },		// 0x2A
	{ CONDACT_DESTROY,		  1 },		// 0x2B
	{ CONDACT_CREATE, 		  1 },		// 0x2C
	{ CONDACT_SWAP,   		  2 },		// 0x2D
	{ CONDACT_PLACE,  		  2 },		// 0x2E
	{ CONDACT_SET,    		  1 },		// 0x2F
	{ CONDACT_CLEAR,  		  1 },		// 0x30
	{ CONDACT_PLUS,   		  2 },		// 0x31
	{ CONDACT_MINUS,  		  2 },		// 0x32
	{ CONDACT_LET,    		  2 },		// 0x33
	{ CONDACT_NEWLINE,		  0 },		// 0x34
	{ CONDACT_PRINT,  		  1 },		// 0x35
	{ CONDACT_SYSMESS,		  1 },		// 0x36
	{ CONDACT_ISAT,   		  2 },		// 0x37
	{ CONDACT_SETCO,  		  1 },		// 0x38
	{ CONDACT_SPACE,  		  0 },		// 0x39
	{ CONDACT_HASAT,  		  1 },		// 0x3A
	{ CONDACT_HASNAT, 		  1 },		// 0x3B
	{ CONDACT_LISTOBJ,		  0 },		// 0x3C
	{ CONDACT_EXTERN, 		  2 },		// 0x3D
	{ CONDACT_RAMSAVE,		  0 },		// 0x3E
	{ CONDACT_RAMLOAD,		  1 },		// 0x3F
	{ CONDACT_BEEP,   		  2 },		// 0x40
	{ CONDACT_PAPER,  		  1 },		// 0x41
	{ CONDACT_INK,    		  1 },		// 0x42
	{ CONDACT_BORDER, 		  1 },		// 0x43
	{ CONDACT_PREP,   		  1 },		// 0x44
	{ CONDACT_NOUN2,  		  1 },		// 0x45
	{ CONDACT_ADJECT2,		  1 },		// 0x46
	{ CONDACT_ADD,    		  2 },		// 0x47
	{ CONDACT_SUB,    		  2 },		// 0x48
	{ CONDACT_PARSE,  		  1 },		// 0x49
	{ CONDACT_LISTAT, 		  1 },		// 0x4A
	{ CONDACT_PROCESS,		  1 },		// 0x4B
	{ CONDACT_SAME,   		  2 },		// 0x4C
	{ CONDACT_MES,    		  1 },		// 0x4D
	{ CONDACT_WINDOW, 		  1 },		// 0x4E
	{ CONDACT_NOTEQ,  		  2 },		// 0x4F
	{ CONDACT_NOTSAME,		  2 },		// 0x50
	{ CONDACT_MODE,   		  1 },		// 0x51
	{ CONDACT_WINAT,  		  2 },		// 0x52
	{ CONDACT_TIME,   		  2 },		// 0x53
	{ CONDACT_PICTURE,		  1 },		// 0x54
	{ CONDACT_DOALL,  		  1 },		// 0x55
	{ CONDACT_MOUSE,  		  2 },		// 0x56
	{ CONDACT_GFX,    		  2 },		// 0x57
	{ CONDACT_ISNOTAT,		  2 },		// 0x58
	{ CONDACT_WEIGH,  		  2 },		// 0x59
	{ CONDACT_PUTIN,  		  2 },		// 0x5A
	{ CONDACT_TAKEOUT,		  2 },		// 0x5B
	{ CONDACT_NEWTEXT,		  0 },		// 0x5C
	{ CONDACT_ABILITY,		  2 },		// 0x5D
	{ CONDACT_WEIGHT, 		  1 },		// 0x5E
	{ CONDACT_RANDOM, 		  1 },		// 0x5F
	{ CONDACT_INPUT,  		  2 },		// 0x60
	{ CONDACT_SAVEAT, 		  0 },		// 0x61
	{ CONDACT_BACKAT, 		  0 },		// 0x62
	{ CONDACT_PRINTAT,		  2 },		// 0x63
	{ CONDACT_WHATO,  		  0 },		// 0x64
	{ CONDACT_CALL,   		  1 },		// 0x65
	{ CONDACT_PUTO,   		  1 },		// 0x66
	{ CONDACT_NOTDONE,		  0 },		// 0x67
	{ CONDACT_AUTOP,  		  1 },		// 0x68
	{ CONDACT_AUTOT,  		  1 },		// 0x69
	{ CONDACT_MOVE,   		  1 },		// 0x6A
	{ CONDACT_WINSIZE,		  2 },		// 0x6B
	{ CONDACT_REDO,   		  0 },		// 0x6C
	{ CONDACT_CENTRE, 		  0 },		// 0x6D
	{ CONDACT_EXIT,   		  1 },		// 0x6E
	{ CONDACT_INKEY,  		  0 },		// 0x6F
	{ CONDACT_BIGGER, 		  2 },		// 0x70
	{ CONDACT_SMALLER,		  2 },		// 0x71
	{ CONDACT_ISDONE, 		  0 },		// 0x72
	{ CONDACT_ISNDONE,		  0 },		// 0x73
	{ CONDACT_SKIP,   		  1 },		// 0x74
	{ CONDACT_RESTART,		  0 },		// 0x75
	{ CONDACT_TAB,    		  1 },		// 0x76
	{ CONDACT_COPYOF, 		  2 },		// 0x77
	{ CONDACT_INVALID,  	  0 },		// 0x78
	{ CONDACT_COPYOO, 		  2 },		// 0x79
	{ CONDACT_INVALID, 		  1 },		// 0x7A
	{ CONDACT_COPYFO, 		  2 },		// 0x7B
	{ CONDACT_INVALID,        0 },		// 0x7C
	{ CONDACT_COPYFF, 		  2 },		// 0x7D
	{ CONDACT_COPYBF, 		  2 },		// 0x7E
	{ CONDACT_RESET,   		  0 },		// 0x7F
};

#if HAS_PAWS

static DDB_CondactMap pawsCondacts[128] = {
	{ CONDACT_AT,     		  1 },		// 0x00
	{ CONDACT_NOTAT,  		  1 },		// 0x01
	{ CONDACT_ATGT,           1 },		// 0x02
	{ CONDACT_ATLT,   		  1 },		// 0x03
	{ CONDACT_PRESENT,		  1 },		// 0x04
	{ CONDACT_ABSENT, 		  1 },		// 0x05
	{ CONDACT_WORN,   		  1 },		// 0x06
	{ CONDACT_NOTWORN,		  1 },		// 0x07
	{ CONDACT_CARRIED,		  1 },		// 0x08
	{ CONDACT_NOTCARR,		  1 },		// 0x09
	{ CONDACT_CHANCE, 		  1 },		// 0x0A
	{ CONDACT_ZERO,   		  1 },		// 0x0B
	{ CONDACT_NOTZERO,		  1 },		// 0x0C
	{ CONDACT_EQ,     		  2 },		// 0x0D
	{ CONDACT_GT,     		  2 },		// 0x0E
	{ CONDACT_LT,     		  2 },		// 0x0F
	{ CONDACT_ADJECT1,		  1 },		// 0x10
	{ CONDACT_ADVERB, 		  1 },		// 0x11
	{ CONDACT_INVEN,   		  0 },		// 0x12
	{ CONDACT_DESC,   		  0 },		// 0x13
	{ CONDACT_QUIT,   		  0 },		// 0x14
	{ CONDACT_END,    		  0 },		// 0x15
	{ CONDACT_DONE,   		  0 },		// 0x16
	{ CONDACT_OK,     		  0 },		// 0x17
	{ CONDACT_ANYKEY, 		  0 },		// 0x18
	{ CONDACT_SAVE,   		  0 },		// 0x19
	{ CONDACT_LOAD,   		  0 },		// 0x1A
	{ CONDACT_TURNS,    	  0 },		// 0x1B
	{ CONDACT_SCORE,	  	  0 },		// 0x1C
	{ CONDACT_CLS,    		  0 },		// 0x1D
	{ CONDACT_DROPALL,		  0 },		// 0x1E
	{ CONDACT_AUTOG,  		  0 },		// 0x1F
	{ CONDACT_AUTOD,  		  0 },		// 0x20
	{ CONDACT_AUTOW,  		  0 },		// 0x21
	{ CONDACT_AUTOR,  		  0 },		// 0x22
	{ CONDACT_PAUSE,  		  1 },		// 0x23
	{ CONDACT_TIMEOUT, 	 	  0 },		// 0x24
	{ CONDACT_GOTO,   		  1 },		// 0x25
	{ CONDACT_MESSAGE,		  1 },		// 0x26
	{ CONDACT_REMOVE, 		  1 },		// 0x27
	{ CONDACT_GET,    		  1 },		// 0x28
	{ CONDACT_DROP,   		  1 },		// 0x29
	{ CONDACT_WEAR,   		  1 },		// 0x2A
	{ CONDACT_DESTROY,		  1 },		// 0x2B
	{ CONDACT_CREATE, 		  1 },		// 0x2C
	{ CONDACT_SWAP,   		  2 },		// 0x2D
	{ CONDACT_PLACE,  		  2 },		// 0x2E
	{ CONDACT_SET,    		  1 },		// 0x2F
	{ CONDACT_CLEAR,  		  1 },		// 0x30
	{ CONDACT_PLUS,   		  2 },		// 0x31
	{ CONDACT_MINUS,  		  2 },		// 0x32
	{ CONDACT_LET,    		  2 },		// 0x33
	{ CONDACT_NEWLINE,		  0 },		// 0x34
	{ CONDACT_PRINT,  		  1 },		// 0x35
	{ CONDACT_SYSMESS,		  1 },		// 0x36
	{ CONDACT_ISAT,   		  2 },		// 0x37
	{ CONDACT_COPYOF,  		  2 },		// 0x38
	{ CONDACT_COPYOO,  		  2 },		// 0x39
	{ CONDACT_COPYFO,  		  2 },		// 0x3A
	{ CONDACT_COPYFF, 		  2 },		// 0x3B
	{ CONDACT_LISTOBJ,		  0 },		// 0x3C
	{ CONDACT_EXTERN, 		  1 },		// 0x3D
	{ CONDACT_RAMSAVE,		  0 },		// 0x3E
	{ CONDACT_RAMLOAD,		  1 },		// 0x3F
	{ CONDACT_BEEP,   		  2 },		// 0x40
	{ CONDACT_PAPER,  		  1 },		// 0x41
	{ CONDACT_INK,    		  1 },		// 0x42
	{ CONDACT_BORDER, 		  1 },		// 0x43
	{ CONDACT_PREP,   		  1 },		// 0x44
	{ CONDACT_NOUN2,  		  1 },		// 0x45
	{ CONDACT_ADJECT2,		  1 },		// 0x46
	{ CONDACT_ADD,    		  2 },		// 0x47
	{ CONDACT_SUB,    		  2 },		// 0x48
	{ CONDACT_PARSE,  		  0 },		// 0x49
	{ CONDACT_LISTAT, 		  1 },		// 0x4A
	{ CONDACT_PROCESS,		  1 },		// 0x4B
	{ CONDACT_SAME,   		  2 },		// 0x4C
	{ CONDACT_MES,    		  1 },		// 0x4D
	{ CONDACT_CHARSET, 		  1 },		// 0x4E
	{ CONDACT_NOTEQ,  		  2 },		// 0x4F
	{ CONDACT_NOTSAME,		  2 },		// 0x50
	{ CONDACT_MODE,   		  2 },		// 0x51
	{ CONDACT_LINE,  		  1 },		// 0x52
	{ CONDACT_TIME,   		  2 },		// 0x53
	{ CONDACT_PICTURE,		  1 },		// 0x54
	{ CONDACT_DOALL,  		  1 },		// 0x55
	{ CONDACT_PROMPT,  		  1 },		// 0x56
	{ CONDACT_GRAPHIC,		  1 },		// 0x57
	{ CONDACT_ISNOTAT,		  2 },		// 0x58
	{ CONDACT_WEIGH,  		  2 },		// 0x59
	{ CONDACT_PUTIN,  		  2 },		// 0x5A
	{ CONDACT_TAKEOUT,		  2 },		// 0x5B
	{ CONDACT_NEWTEXT,		  0 },		// 0x5C
	{ CONDACT_ABILITY,		  2 },		// 0x5D
	{ CONDACT_WEIGHT, 		  1 },		// 0x5E
	{ CONDACT_RANDOM, 		  1 },		// 0x5F
	{ CONDACT_INPUT,  		  1 },		// 0x60
	{ CONDACT_SAVEAT, 		  0 },		// 0x61
	{ CONDACT_BACKAT, 		  0 },		// 0x62
	{ CONDACT_PRINTAT,		  2 },		// 0x63
	{ CONDACT_WHATO,  		  0 },		// 0x64
	{ CONDACT_RESET,   		  1 },		// 0x65
	{ CONDACT_PUTO,   		  1 },		// 0x66
	{ CONDACT_NOTDONE,		  0 },		// 0x67
	{ CONDACT_AUTOP,  		  1 },		// 0x68
	{ CONDACT_AUTOT,  		  1 },		// 0x69
	{ CONDACT_MOVE,   		  1 },		// 0x6A
	{ CONDACT_PROTECT,		  0 },		// 0x6B

	// --------------------

	{ CONDACT_INVALID,        0 },		// 0x6C
	{ CONDACT_INVALID,        0 },		// 0x6D
	{ CONDACT_INVALID,        0 },		// 0x6E
	{ CONDACT_INVALID,        0 },		// 0x6F
	{ CONDACT_INVALID,        0 },		// 0x70
	{ CONDACT_INVALID,        0 },		// 0x71
	{ CONDACT_INVALID,        0 },		// 0x72
	{ CONDACT_INVALID,        0 },		// 0x73
	{ CONDACT_INVALID,        0 },		// 0x74
	{ CONDACT_INVALID,        0 },		// 0x75
	{ CONDACT_INVALID,        0 },		// 0x76
	{ CONDACT_INVALID,        0 },		// 0x77
	{ CONDACT_INVALID,        0 },		// 0x78
	{ CONDACT_INVALID,        0 },		// 0x79
	{ CONDACT_INVALID,        0 },		// 0x7A
	{ CONDACT_INVALID,        0 },		// 0x7B
	{ CONDACT_INVALID,        0 },		// 0x7C
	{ CONDACT_INVALID,        0 },		// 0x7D
	{ CONDACT_INVALID,        0 },		// 0x7E
	{ CONDACT_INVALID,        0 },		// 0x7F
};

#endif

void DDB_SetError(DDB_Error error)
{
	ddbError = error;
}

DDB_Error DDB_GetError()
{
	return ddbError;
}

const char* DDB_GetErrorString()
{
	switch (ddbError)
	{
		case DDB_ERROR_NONE:               return "No error";
		case DDB_ERROR_FILE_NOT_FOUND:     return "File not found";
		case DDB_ERROR_READING_FILE:       return "I/O error reading file";
		case DDB_ERROR_SEEKING_FILE:       return "I/O error seeking file";
		case DDB_ERROR_CREATING_FILE:	   return "I/O error creating file";
		case DDB_ERROR_WRITING_FILE:       return "I/O error writing file";
		case DDB_ERROR_OUT_OF_MEMORY:      return "Out of memory";
		case DDB_ERROR_INVALID_FILE:       return "Corrupted or invalid DDB file";
		case DDB_ERROR_FILE_NOT_SUPPORTED: return "Unsupported file format";
		case DDB_ERROR_SDL:                return "SDL error";
		case DDB_ERROR_NO_DDBS_FOUND:      return "No DDBs found";
		default:                           return "Unknown error";
	}
}

void DDB_SetWarningHandler(void (*handler)(const char* message))
{
	warningHandler = handler;
}

void DDB_Warning(const char* format, ...)
{
	if (warningHandler != 0)
	{
	#if _STDCLIB
		char buffer[256];
		va_list args;
		va_start(args, format);
		vsnprintf(buffer, sizeof(buffer), format, args);
		va_end(args);
		warningHandler(buffer);
	#else
		warningHandler(format);
	#endif
	}
}

/* ───────────────────────────────────────────────────────────────────────── */

const char* DDB_GetDebugMessage (DDB* ddb, DDB_MsgType type, uint8_t msgId)
{
	static char buffer[64];
	DDB_GetMessage (ddb, type, msgId, buffer, sizeof(buffer));
	if (buffer[60] != 0)
		StrCopy(buffer+60, 4, "...");
	return buffer;
}

const char* DDB_GetMessage (DDB* ddb, DDB_MsgType type, uint8_t msgId, char* buffer, size_t bufferSize)
{
	uint8_t* ptr;

	if (bufferSize == 0 || buffer == 0)
		return buffer;

	*buffer = 0;

	switch (type)
	{
		case DDB_MSG:
			ptr = ddb->messages[msgId];
			break;
		case DDB_SYSMSG:
			if (msgId >= ddb->numSystemMessages)
				return buffer;
			ptr = ddb->data + ddb->sysMsgTable[msgId];
			break;
		case DDB_OBJNAME:
			if (msgId >= ddb->numObjects)
				return buffer;
			ptr = ddb->data + ddb->objNamTable[msgId];
			break;
		case DDB_LOCDESC:
			ptr = ddb->locDescriptions[msgId];
			break;
		default:
			DDB_Warning("Invalid message type %d", type);
			return buffer;
	}

	if (ptr <= ddb->data || ptr >= ddb->data + ddb->dataSize)
		return buffer;

	uint8_t endMarker = ddb->version == DDB_VERSION_PAWS ? 0x1F : 0x0A;

	bufferSize--;
	while (bufferSize > 0)
	{
		uint8_t c = *ptr++ ^ 0xFF;
		if (c == endMarker)
		{
			*buffer = 0;
			return buffer;
		}
		if (c >= ddb->firstToken)
		{
			if (!ddb->hasTokens)
			{
				DDB_Warning("Message contains token 0x%02X but DDB has no tokens!", c);
				continue;
			}
			uint8_t* token = ddb->tokensPtr[c - ddb->firstToken];
			if (token == 0)
			{
				DDB_Warning("Message contains token 0x%02X but it's not defined in the DDB!", c);
				continue;
			}
			while (bufferSize > 0)
			{
				*buffer++ = *token & 0x7F;
				bufferSize--;
				if ((*token & 0x80) != 0)
					break;
				token++;
			}
		}
		else
		{
			*buffer++ = c;
			bufferSize--;
		}
	}
	if (bufferSize > 0)
		*buffer = 0;
	return buffer;
}

/* ───────────────────────────────────────────────────────────────────────── */

void DDB_FixOffsets (DDB* ddb)
{
	int n;

#ifndef _WEB
#ifdef _BIG_ENDIAN
	if (!ddb->littleEndian)
		return;
#endif
#ifdef _LITTLE_ENDIAN
	if (ddb->littleEndian)
		return;
#endif
#endif

	uint16_t* tables[] = {
		ddb->msgTable,
		ddb->sysMsgTable,
		ddb->objNamTable,
		ddb->locDescTable,
		ddb->processTable,
		ddb->conTable
	};
	uint8_t counts[] = {
		ddb->numMessages,
		ddb->numSystemMessages,
		ddb->numObjects,
		ddb->numLocations,
		ddb->numProcesses,
		ddb->numLocations
	};
	const char* tableName[] = {
		"Messages",
		"System messages",
		"Object names",
		"Location descriptions",
		"Processes",
		"Connections"
	};
	const int numTables = sizeof(tables) / sizeof(tables[0]);

	for (n = 0; n < numTables; n++)
	{
		uint16_t* table = tables[n];
		uint8_t entries = counts[n];
		int m;
		if (table == 0)
			continue;
		for (m = 0; m < entries; m++)
		{
			uint16_t offset = read16((uint8_t*)table + m * 2, ddb->littleEndian);
			offset -= ddb->baseOffset;
			if (offset >= ddb->dataSize || offset < 32)
			{
				DDB_Warning("Invalid internal offset 0x%04X (entry %d in %s)", offset, m, tableName[n]);
				//ddbError = DDB_ERROR_INVALID_FILE;
				table[m] = 0;
				continue;
			}
			table[m] = offset;
		}
	}

	// Fix processes

	for (n = 0; n < ddb->numProcesses; n++)
	{
		int entryIndex = 0;
		uint16_t offset = ddb->processTable[n];
		uint8_t* ptr = ddb->data + offset + 2;
		if (offset == 0)
			continue;
		while (offset < ddb->dataSize)
		{
			// End of process marker
			if (ptr[-2] == 0)
				break;

			#if HAS_PAWS
			if (ddb->version == DDB_VERSION_PAWS)
			{
				// Convert '*' entries to '_'
				if (ptr[-1] == 1) ptr[-1] = 255;
				if (ptr[-2] == 1) ptr[-2] = 255;
			}
			#endif

			uint16_t entryOffset = read16(ptr, ddb->littleEndian) - ddb->baseOffset;
			if (entryOffset >= ddb->dataSize || entryOffset < 32)
			{
				DDB_Warning("Invalid entry %d offset 0x%04X in process %d", entryIndex, entryOffset, n);
				ddbError = DDB_ERROR_INVALID_FILE;
				*(uint16_t*)ptr = 0;
				ptr += 4;
				break;
			}
			*(uint16_t*)ptr = entryOffset;

			ptr += 4;
			entryIndex++;
		}
	}
}

bool DDB_Check(const char* filename, DDB_Machine* target, DDB_Language* language, DDB_Version* version)
{
	uint8_t buffer[32];
	File* file = File_Open(filename, ReadOnly);
	if (file == 0)
		return false;
	if (File_Read(file, buffer, sizeof(buffer)) != sizeof(buffer))
	{
		File_Close(file);
		return false;
	}
	File_Close(file);

	if (language != 0)
		*language = (DDB_Language)(buffer[1] & 0x0F);
	if (target != 0)
		*target = (DDB_Machine)(buffer[1] >> 4);
	if (version != 0)
		*version = (DDB_Version)buffer[0];
	return true;
}

bool DDB_SupportsDataFile(DDB* ddb)
{
	#if HAS_PAWS
	if (ddb->version == DDB_VERSION_PAWS)
		return false;
	#endif

	return
		ddb->target == DDB_MACHINE_AMIGA ||
		ddb->target == DDB_MACHINE_ATARIST ||
		ddb->target == DDB_MACHINE_IBMPC;
}

#if HAS_SNAPSHOTS
static uint32_t GuessDDBOffsetFromSnapshot(uint8_t* memory, size_t size, DDB_Machine target, DDB* ddb)
{
	uint32_t offset = 0;

	// This is actually the minimum offset for the platform

	switch (target)
	{
		case DDB_MACHINE_SPECTRUM: offset = 0x5B00; break;
		case DDB_MACHINE_C64:      offset = 0x3880; break;
		case DDB_MACHINE_CPC:      offset = 0x2880; break;
		case DDB_MACHINE_MSX:      offset = 0x0100; break;
		case DDB_MACHINE_PLUS4:    offset = 0x7080; break;
		default:                   offset = 0; break;
	}

	// Exploring the entire memory is a bit overkill, but...

	while (offset < size)
	{
		if (   (memory[offset] == 1 || memory[offset] == 2) // DAAD Version
		    && (memory[offset+1] & 0xF0) == (target << 4)   // Machine
			&& (memory[offset+2] == 0x5F))
		{
			uint16_t eofOffset = offset + 0x1E;
			if (memory[offset] == 2) eofOffset += 2;
			uint16_t eof = read16(memory + eofOffset, true);
			if (eof <= size && eof > offset + 32)
			{
				ddb->baseOffset = offset;
				ddb->data = memory + offset;
				ddb->dataSize = eof - offset;
				return true;
			}
		}

		if (offset == 65535)
			break;
		offset++;
	}
	return 0;
}
#endif

static void DDB_FillTokenPointers(DDB* ddb)
{
	// An offset 0 means no tokens in file. Otherwise,
	// update the token pointers in the DDB structure

	if (ddb->tokens == ddb->data)
	{
		ddb->hasTokens = false;
		ddb->tokens = 0;
	}
	else
	{
		int n;
		uint8_t* ptr = ddb->tokens + 1;
		uint8_t* end = ddb->data + ddb->dataSize;
        uint8_t  eof = ddb->version == DDB_VERSION_PAWS ? 0xFF : 0x00;
		for (n = ddb->firstToken; n <= 255; n++)
		{
			ddb->tokensPtr[n - ddb->firstToken] = ptr;
			while ((*ptr & 0x80) == 0 && *ptr != 0 && ptr < end)
				ptr++;
			if (*ptr == eof || ptr == end)
				break;
			ptr++;
		}
		while (n < 256)
			ddb->tokensPtr[n++ - ddb->firstToken] = 0;
		ddb->hasTokens = true;
		ddb->tokenBlockSize = ptr - ddb->tokens;
	}
}

static void DDB_FillMsgPointers(DDB* ddb)
{
	for (int i = 0; i < ddb->numLocations; i++)
	{
		ddb->locConnections[i] = ddb->data + ddb->conTable[i];
		ddb->locDescriptions[i] = ddb->data + ddb->locDescTable[i];
	}
	for (int i = 0; i < ddb->numMessages; i++)
	{
		ddb->messages[i] = ddb->data + ddb->msgTable[i];
	}
}

#if HAS_PAWS

static bool CheckPAWSignature(uint8_t* memory, uint16_t base, uint16_t attr)
{
	return (
		base < 65535-321 &&
		base > 16384-311 &&
		memory[attr]    == 16 &&
		memory[attr+2]  == 17 &&
		memory[attr+4]  == 18 &&
		memory[attr+6]  == 19 &&
		memory[attr+8]  == 20 &&
		memory[attr+10] == 21
	);
}

static bool LoadPAWS(DDB* ddb, uint8_t* memory, size_t size)
{
	if (size < 65536) return false;

	uint16_t base = read16LE(memory + 65533);
	uint16_t attr = base + 311;
	if (CheckPAWSignature(memory, base, attr))
	{
		// PAWS
		ddb->data         = memory;
		ddb->dataSize     = size;
		ddb->version      = DDB_VERSION_PAWS;
		ddb->language     = DDB_SPANISH;			// TODO: guess
		ddb->target       = DDB_MACHINE_SPECTRUM;
		ddb->machine      = DDB_MACHINE_SPECTRUM;
		ddb->condactMap   = pawsCondacts;
		ddb->littleEndian = true;
		ddb->firstToken   = 164;

		ddb->defaultBorder     = memory[base + 323];
		ddb->defaultInk		   = memory[base + 312];
		ddb->defaultPaper	   = memory[base + 314];
		ddb->defaultCharset	   = memory[base + 281];

		ddb->numObjects        = memory[base + 324];
		ddb->numLocations      = memory[base + 325];
		ddb->numMessages       = memory[base + 326];
		ddb->numSystemMessages = memory[base + 327];
		ddb->numProcesses      = memory[base + 328];
		ddb->numCharsets       = memory[base + 329];

		ddb->charsets          = memory + read16LE(memory + base + 330);
		ddb->tokens			   = memory + read16LE(memory + base + 332);
		ddb->processTable	   = (uint16_t*)(memory + read16LE(memory + 65497));
		ddb->objNamTable	   = (uint16_t*)(memory + read16LE(memory + 65499));
		ddb->locDescTable	   = (uint16_t*)(memory + read16LE(memory + 65501));
		ddb->msgTable		   = (uint16_t*)(memory + read16LE(memory + 65503));
		ddb->sysMsgTable	   = (uint16_t*)(memory + read16LE(memory + 65505));
		ddb->conTable	       = (uint16_t*)(memory + read16LE(memory + 65507));
		ddb->vocabulary		   = memory + read16LE(memory + 65509);
		ddb->objLocTable	   = memory + read16LE(memory + 65511);
		ddb->objWordsTable	   = memory + read16LE(memory + 65513);
		ddb->objAttrTable	   = memory + read16LE(memory + 65515);

		if (size > 65536)
		{
			int from = 0;
			for (int i = 0; i < 8 && from < ddb->numLocations ; i++)
			{
				uint8_t page = memory[base + 297 + i*2];
				uint8_t to   = memory[base + 298 + i*2];
				if (to == 255) to = ddb->numLocations;
				DebugPrintf("Page %d contains locations from %d to %d\n", page, from, to-1);
				uint8_t* base = ddb->data + (page > 0 ? 0x10000 + page * 0x4000 - 0xC000: 0);
				uint16_t locDescTable = read16LE(base + 65501);
				uint16_t locConnTable = read16LE(base + 65507);
				for (int n = from; n < to; n++)
				{
					ddb->locConnections[n] = base + read16LE(base + locConnTable + (n-from)*2);
					ddb->locDescriptions[n] = base + read16LE(base + locDescTable + (n-from)*2);
				}
				from = to;
			}
			from = 0;
			for (int i = 0; i < 8 && from < ddb->numMessages; i++)
			{
				uint8_t page = memory[base + 283 + i*2];
				uint8_t to   = memory[base + 284 + i*2];
				if (to == 255) to = ddb->numMessages;
				DebugPrintf("Page %d contains messages from %d to %d\n", page, from, to-1);
				uint8_t* base = ddb->data + (page > 0 ? 0x10000 + page * 0x4000 - 0xC000 : 0);
				uint16_t msgTable = read16LE(base + 65503);
				for (int n = from; n < to; n++)
					ddb->messages[n] = base + read16LE(base + msgTable + (n-from)*2);
				from = to;
			}

			ddb->conTable = 0;
			ddb->msgTable = 0;
			ddb->locDescTable = 0;
		}
		else
		{
			DDB_FillMsgPointers(ddb);
		}

		return true;
	}
	if (CheckPAWSignature(memory, base = 26931, attr = 27908))
	{
		// TODO: QUILL Version A
		return false;
	}
	if (CheckPAWSignature(memory, base = 27356, attr = 27525))
	{
		// TODO: QUILL Version C
		return false;
	}

	return false;
}

#endif

DDB* DDB_Load(const char* filename)
{
	ddbError = DDB_ERROR_NONE;

	File* file = File_Open(filename, ReadOnly);
	if (file == 0)
	{
		ddbError = DDB_ERROR_FILE_NOT_FOUND;
		return 0;
	}

	uint64_t fileSize = File_GetSize(file);
	uint8_t* memory = 0;
	uint8_t* data = 0;

	DDB* ddb = Allocate<DDB>("DDB", 1, true);
	if (ddb == 0)
	{
		ddbError = DDB_ERROR_OUT_OF_MEMORY;
		File_Close(file);
		Free(memory);
		return 0;
	}
	ddb->machine = DDB_MACHINE_IBMPC;

	#if HAS_SNAPSHOTS

	size_t ramSize;
	DDB_Machine snapshotMachine;
	if (DDB_LoadSnapshot(file, filename, &memory, &ramSize, &snapshotMachine))
	{
		File_Close(file);

		#if HAS_PAWS
		if (LoadPAWS(ddb, memory, ramSize))
		{
			#if HAS_DRAWSTRING
			if (DDB_LoadPAWSGraphics(memory))
			{
				ddb->drawString = true;
				ddb->defaultInk = VID_GetInk();
				ddb->defaultPaper = VID_GetPaper();
				// DebugPrintf("Default Ink: %d, Paper: %d\n", ddb->defaultInk, ddb->defaultPaper);
			}
			#endif

			DDB_FixOffsets(ddb);
			DDB_FillTokenPointers(ddb);
			ddb->oldMainLoop = true;
			return ddb;
		}
		#endif

		if (GuessDDBOffsetFromSnapshot(memory, ramSize, snapshotMachine, ddb) == false)
		{
			ddbError = DDB_ERROR_FILE_NOT_SUPPORTED;
			Free(memory);
			Free(ddb);
			return 0;
		}

		#if HAS_DRAWSTRING
		if (DDB_LoadVectorGraphics(snapshotMachine, memory, ramSize))
			ddb->drawString = true;
		#endif

		data = ddb->data;
		ddb->machine = snapshotMachine;
	}
	else

	#endif

	{
		ddbError = DDB_ERROR_NONE;

		if (fileSize > MAX_DDB_SIZE)
		{
			ddbError = DDB_ERROR_INVALID_FILE;
			DDB_Warning("Invalid DDB file: too big (max size: %d)\n", filename, MAX_DDB_SIZE);
			File_Close(file);
			Free(ddb);
			return 0;
		}

		if (memory == 0)
			memory = Allocate<uint8_t>("DDB Contents", fileSize);
		if (memory == 0)
		{
			ddbError = DDB_ERROR_OUT_OF_MEMORY;
			File_Close(file);
			return 0;
		}
		if (File_Read(file, memory, fileSize) != fileSize)
		{
			ddbError = DDB_ERROR_READING_FILE;
			File_Close(file);
			Free(memory);
			Free(ddb);
			return 0;
		}
		File_Close(file);

		data          = memory;
		ddb->memory   = memory;
		ddb->data     = data;
		ddb->dataSize = fileSize;
	}

	ddb->version    = (DDB_Version)data[0];
	ddb->language   = (DDB_Language)(data[1] & 0x0F);
	ddb->target     = (DDB_Machine)(data[1] >> 4);
	ddb->condactMap = ddb->version == 1 ? version1Condacts : version2Condacts;
	ddb->firstToken = 128;

	if (ddb->baseOffset == 0)
	{
		switch (ddb->target)
		{
			case DDB_MACHINE_SPECTRUM:
				ddb->baseOffset = 0x8400;
				break;
			case DDB_MACHINE_C64:
				ddb->baseOffset = 0x3880;
				break;
			case DDB_MACHINE_CPC:
				ddb->baseOffset = 0x2880;
				break;
			case DDB_MACHINE_MSX:
				ddb->baseOffset = 0x0100;
				break;
			case DDB_MACHINE_PLUS4:
				ddb->baseOffset = 0x7080;
				break;
			default:
				ddb->baseOffset = 0;
				break;
		}
	}

	if (ddb->version > 2)
	{
		DDB_Warning("Invalid DDB header: unsupported version %d", ddb->version);
		ddbError = DDB_ERROR_INVALID_FILE;
		Free(memory);
		Free(ddb);
		return 0;
	}

	// No longer output a warning in this case, as modern compilers seem to put something else in this field
	// if (data[2] != 0x5F) DDB_Warning("Warning: unsupported wildcard character '%c' in DDB header", data[2]);

	uint16_t headerSizeBE = read16(data + (ddb->version == 1 ? 30 : 32), false);
	uint16_t headerSizeLE = read16(data + (ddb->version == 1 ? 30 : 32), true);
	if (headerSizeBE == fileSize)
		ddb->littleEndian = false;
	else if (headerSizeLE == fileSize)
		ddb->littleEndian = true;
	else
	{
		bool bigEndianValid = true;
		bool littleEndianValid = true;
		for (int n = 8 ; n < 30; n += 2)
		{
			uint16_t offsetBE = read16(data + n, false);
			uint16_t offsetLE = read16(data + n, true);
			if (offsetBE < ddb->baseOffset || (uint64_t)(offsetBE - ddb->baseOffset) >= headerSizeBE)
				bigEndianValid = false;
			if (offsetLE < ddb->baseOffset || (uint64_t)(offsetLE - ddb->baseOffset) >= headerSizeLE)
				littleEndianValid = false;
		}
		if (bigEndianValid && !littleEndianValid)
			ddb->littleEndian = false;
		else if (!bigEndianValid && littleEndianValid)
			ddb->littleEndian = true;
		else
		{
			DDB_Warning("Invalid DDB header: unable to guess endianess");
			ddbError = DDB_ERROR_INVALID_FILE;
			Free(memory);
			Free(ddb);
			return 0;
		}

		//DDB_Warning("Endianess guessed: %s-endian", ddb->littleEndian ? "little" : "big");
	}

	ddb->numObjects        = data[3];
	ddb->numLocations      = data[4];
	ddb->numMessages       = data[5];
	ddb->numSystemMessages = data[6];
	ddb->numProcesses      = data[7];

	ddb->tokens            = data + read16(data + 8, ddb->littleEndian) - ddb->baseOffset;
	ddb->processTable      = (uint16_t*)(data + read16(data + 10, ddb->littleEndian) - ddb->baseOffset);
	ddb->objNamTable       = (uint16_t*)(data + read16(data + 12, ddb->littleEndian) - ddb->baseOffset);
	ddb->locDescTable      = (uint16_t*)(data + read16(data + 14, ddb->littleEndian) - ddb->baseOffset);
	ddb->msgTable          = (uint16_t*)(data + read16(data + 16, ddb->littleEndian) - ddb->baseOffset);
	ddb->sysMsgTable       = (uint16_t*)(data + read16(data + 18, ddb->littleEndian) - ddb->baseOffset);
	ddb->conTable          = (uint16_t*)(data + read16(data + 20, ddb->littleEndian) - ddb->baseOffset);
	ddb->vocabulary        = data + read16(data + 22, ddb->littleEndian) - ddb->baseOffset;
	ddb->objLocTable       = data + read16(data + 24, ddb->littleEndian) - ddb->baseOffset;
	ddb->objWordsTable     = data + read16(data + 26, ddb->littleEndian) - ddb->baseOffset;
	ddb->objAttrTable      = data + read16(data + 28, ddb->littleEndian) - ddb->baseOffset;

	if (ddb->version == 2)
	{
		ddb->objExAttrTable = (uint16_t*)(data + read16(data + 30, ddb->littleEndian) - ddb->baseOffset);

		// TODO: Figure out the actual flag order in different architectures, this looks wrong
		for (int n = 0; n < ddb->numObjects; n++)
			ddb->objExAttrTable[n] = read16((const uint8_t*) &ddb->objExAttrTable[n], !ddb->littleEndian);
	}

	uint16_t externOffset = read16(data + (ddb->version == 2 ? 34 : 32), ddb->littleEndian);
	if (externOffset != 0 && externOffset > ddb->baseOffset)
		ddb->externData   = (uint8_t*)(data + externOffset - ddb->baseOffset);

	DDB_FixOffsets(ddb);
	DDB_FillTokenPointers(ddb);
	DDB_FillMsgPointers(ddb);

	// Old databases may need a PAWS style flow, try to
	// detect if process table 0 is a responses table
	//
	// TODO: *All* version 1 databases use a old style
	// loop, so there should be no need for this. Double check!

	if (ddb->version == 1)
	{
		uint16_t offset = ddb->processTable[0];
		if (offset != 0)
		{
			uint8_t* ptr = ddb->data + offset;
			int entriesWithVerb = 0;
			while (*ptr != 0)
			{
				if (ptr[0] != 255)
					entriesWithVerb++;
				ptr += 4;
			}
			if (entriesWithVerb > 10)
				ddb->oldMainLoop = true;
		}
	}

	if (ddbError != DDB_ERROR_NONE)
	{
		Free(memory);
		Free(ddb);
		return 0;
	}
	return ddb;
}

void DDB_Close(DDB* ddb)
{
	if (ddb == 0)
		return;
	if (ddb->memory != 0)
		Free(ddb->memory);
	Free(ddb);
}

const char* DDB_DescribeLanguage(DDB_Language lang)
{
	switch (lang)
	{
		case DDB_ENGLISH: return "English"; break;
		case DDB_SPANISH: return "Spanish"; break;
		default:          return "Unknown"; break;
	}
}

const char* DDB_DescribeMachine(DDB_Machine machine)
{
	switch (machine)
	{
		case DDB_MACHINE_IBMPC:    return "IBM PC"; break;
		case DDB_MACHINE_SPECTRUM: return "ZX Spectrum"; break;
		case DDB_MACHINE_C64:      return "Commodore 64"; break;
		case DDB_MACHINE_CPC:      return "Amstrad CPC"; break;
		case DDB_MACHINE_MSX:      return "MSX"; break;
		case DDB_MACHINE_ATARIST:  return "Atari ST"; break;
		case DDB_MACHINE_AMIGA:    return "Amiga"; break;
		case DDB_MACHINE_PCW:      return "Amstrad PCW"; break;
		case DDB_MACHINE_PLUS4:    return "Commodore Plus/4"; break;
		case DDB_MACHINE_MSX2:     return "MSX2"; break;
		default:                   return "Unknown machine"; break;
	}
}

const char* DDB_DescribeVersion (DDB_Version version)
{
	switch (version)
	{
		case DDB_VERSION_PAWS: return "PAWS"; break;
		case DDB_VERSION_1: return "DAAD v1"; break;
		case DDB_VERSION_2: return "DAAD v2"; break;
		default:            return "Unknown version"; break;
	}
}