#include <os_types.h>

// ---------- Identifier tables ---------

#define HASH_BITS   10
#define HASH_MASK   ((1 << (HASH_BITS-1))-1)
#define HASH_SLOTS  (1 << (HASH_BITS-1))

#define MAX_BUCKET_STRINGS 64

#define INVALID_IDENTIFIER        ((unsigned int)-1)

typedef unsigned int IdentifierID;

struct Bucket
{
	Bucket*        next;
	const uint8_t* stringData[MAX_BUCKET_STRINGS];
	size_t         stringSize[MAX_BUCKET_STRINGS];
	uint32_t       stringInfo[MAX_BUCKET_STRINGS];
	unsigned       stringCount;
};

struct Identifiers
{
	Bucket*     buckets[HASH_SLOTS];
	Arena*      arena;
};

extern Identifiers*   NewIdentifiersTable (Arena* arena);
extern IdentifierID   FindIdentifierID    (Identifiers* table, DC_String str, uint32_t* info = 0);
extern IdentifierID   GetIdentifierID     (Identifiers* table, DC_String str, uint32_t* info = 0);
extern void	          SetIdentifierInfo   (Identifiers* table, DC_String str, uint32_t info);
extern const uint8_t* GetIdentifierText   (Identifiers* table, IdentifierID id);
extern int            GetIdentifierCount  (Identifiers* table);

// ---------- Identifier ids ---------

extern void RegisterCondactNames(Identifiers* table);

extern IdentifierID id_ABSENT;
extern IdentifierID id_ABILITY;
extern IdentifierID id_ADD;
extern IdentifierID id_ADJECT1;
extern IdentifierID id_ADJECT2;
extern IdentifierID id_ADVERB;
extern IdentifierID id_ANYKEY;
extern IdentifierID id_AT;
extern IdentifierID id_ATGT;
extern IdentifierID id_ATLT;
extern IdentifierID id_AUTOD;
extern IdentifierID id_AUTOG;
extern IdentifierID id_AUTOP;
extern IdentifierID id_AUTOR;
extern IdentifierID id_AUTOT;
extern IdentifierID id_AUTOW;
extern IdentifierID id_BACKAT;
extern IdentifierID id_BEEP;
extern IdentifierID id_BIGGER;
extern IdentifierID id_BORDER;
extern IdentifierID id_CALL;
extern IdentifierID id_CARRIED;
extern IdentifierID id_CENTRE;
extern IdentifierID id_CHANCE;
extern IdentifierID id_CLEAR;
extern IdentifierID id_CLS;
extern IdentifierID id_COPYBF;
extern IdentifierID id_COPYFF;
extern IdentifierID id_COPYFO;
extern IdentifierID id_COPYOF;
extern IdentifierID id_COPYOO;
extern IdentifierID id_CREATE;
extern IdentifierID id_DESC;
extern IdentifierID id_DESTROY;
extern IdentifierID id_DISPLAY;
extern IdentifierID id_DOALL;
extern IdentifierID id_DONE;
extern IdentifierID id_DPRINT;
extern IdentifierID id_DROP;
extern IdentifierID id_DROPALL;
extern IdentifierID id_END;
extern IdentifierID id_EQ;
extern IdentifierID id_EXIT;
extern IdentifierID id_EXTERN;
extern IdentifierID id_GET;
extern IdentifierID id_GFX;
extern IdentifierID id_GOTO;
extern IdentifierID id_GT;
extern IdentifierID id_HASAT;
extern IdentifierID id_HASNAT;
extern IdentifierID id_INK;
extern IdentifierID id_INKEY;
extern IdentifierID id_INPUT;
extern IdentifierID id_ISAT;
extern IdentifierID id_ISDONE;
extern IdentifierID id_ISNDONE;
extern IdentifierID id_ISNOTAT;
extern IdentifierID id_LET;
extern IdentifierID id_LISTAT;
extern IdentifierID id_LISTOBJ;
extern IdentifierID id_LOAD;
extern IdentifierID id_LT;
extern IdentifierID id_MES;
extern IdentifierID id_MESSAGE;
extern IdentifierID id_MINUS;
extern IdentifierID id_MODE;
extern IdentifierID id_MOUSE;
extern IdentifierID id_MOVE;
extern IdentifierID id_NEWLINE;
extern IdentifierID id_NEWTEXT;
extern IdentifierID id_NOTAT;
extern IdentifierID id_NOTCARR;
extern IdentifierID id_NOTDONE;
extern IdentifierID id_NOTEQ;
extern IdentifierID id_NOTSAME;
extern IdentifierID id_NOTWORN;
extern IdentifierID id_NOTZERO;
extern IdentifierID id_NOUN2;
extern IdentifierID id_OK;
extern IdentifierID id_PAPER;
extern IdentifierID id_PARSE;
extern IdentifierID id_PAUSE;
extern IdentifierID id_PICTURE;
extern IdentifierID id_PLACE;
extern IdentifierID id_PLUS;
extern IdentifierID id_PREP;
extern IdentifierID id_PRESENT;
extern IdentifierID id_PRINT;
extern IdentifierID id_PRINTAT;
extern IdentifierID id_PROCESS;
extern IdentifierID id_PROMPT;
extern IdentifierID id_PUTIN;
extern IdentifierID id_PUTO;
extern IdentifierID id_QUIT;
extern IdentifierID id_RAMLOAD;
extern IdentifierID id_RAMSAVE;
extern IdentifierID id_RANDOM;
extern IdentifierID id_REDO;
extern IdentifierID id_REMOVE;
extern IdentifierID id_RESET;
extern IdentifierID id_RESTART;
extern IdentifierID id_SAME;
extern IdentifierID id_SAVE;
extern IdentifierID id_SAVEAT;
extern IdentifierID id_SET;
extern IdentifierID id_SETCO;
extern IdentifierID id_SFX;
extern IdentifierID id_SKIP;
extern IdentifierID id_SMALLER;
extern IdentifierID id_SPACE;
extern IdentifierID id_SUB;
extern IdentifierID id_SWAP;
extern IdentifierID id_SYNONYM;
extern IdentifierID id_SYSMESS;
extern IdentifierID id_TAB;
extern IdentifierID id_TAKEOUT;
extern IdentifierID id_TIME;
extern IdentifierID id_TIMEOUT;
extern IdentifierID id_TURNS;
extern IdentifierID id_WEAR;
extern IdentifierID id_WEIGH;
extern IdentifierID id_WEIGHT;
extern IdentifierID id_WHATO;
extern IdentifierID id_WINAT;
extern IdentifierID id_WINDOW;
extern IdentifierID id_WINSIZE;
extern IdentifierID id_ZERO;
extern IdentifierID id_WORN;