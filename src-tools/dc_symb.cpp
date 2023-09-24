#include <os_types.h>
#include <dc_char.h>
#include <dc_symb.h>

// Condact IDs

IdentifierID id_ABILITY;
IdentifierID id_ABSENT;
IdentifierID id_ADD;
IdentifierID id_ADJECT1;
IdentifierID id_ADJECT2;
IdentifierID id_ADVERB;
IdentifierID id_ANYKEY;
IdentifierID id_AT;
IdentifierID id_ATGT;
IdentifierID id_ATLT;
IdentifierID id_AUTOD;
IdentifierID id_AUTOG;
IdentifierID id_AUTOP;
IdentifierID id_AUTOR;
IdentifierID id_AUTOT;
IdentifierID id_AUTOW;
IdentifierID id_BACKAT;
IdentifierID id_BEEP;
IdentifierID id_BIGGER;
IdentifierID id_BORDER;
IdentifierID id_CALL;
IdentifierID id_CARRIED;
IdentifierID id_CENTRE;
IdentifierID id_CHANCE;
IdentifierID id_CLEAR;
IdentifierID id_CLS;
IdentifierID id_COPYBF;
IdentifierID id_COPYFF;
IdentifierID id_COPYFO;
IdentifierID id_COPYOF;
IdentifierID id_COPYOO;
IdentifierID id_CREATE;
IdentifierID id_DESC;
IdentifierID id_DESTROY;
IdentifierID id_DISPLAY;
IdentifierID id_DOALL;
IdentifierID id_DONE;
IdentifierID id_DPRINT;
IdentifierID id_DROP;
IdentifierID id_DROPALL;
IdentifierID id_END;
IdentifierID id_EQ;
IdentifierID id_EXIT;
IdentifierID id_EXTERN;
IdentifierID id_GET;
IdentifierID id_GFX;
IdentifierID id_GOTO;
IdentifierID id_GT;
IdentifierID id_HASAT;
IdentifierID id_HASNAT;
IdentifierID id_INK;
IdentifierID id_INKEY;
IdentifierID id_INPUT;
IdentifierID id_ISAT;
IdentifierID id_ISDONE;
IdentifierID id_ISNDONE;
IdentifierID id_ISNOTAT;
IdentifierID id_LET;
IdentifierID id_LISTAT;
IdentifierID id_LISTOBJ;
IdentifierID id_LOAD;
IdentifierID id_LT;
IdentifierID id_MES;
IdentifierID id_MESSAGE;
IdentifierID id_MINUS;
IdentifierID id_MODE;
IdentifierID id_MOUSE;
IdentifierID id_MOVE;
IdentifierID id_NEWLINE;
IdentifierID id_NEWTEXT;
IdentifierID id_NOTAT;
IdentifierID id_NOTCARR;
IdentifierID id_NOTDONE;
IdentifierID id_NOTEQ;
IdentifierID id_NOTSAME;
IdentifierID id_NOTWORN;
IdentifierID id_NOTZERO;
IdentifierID id_NOUN2;
IdentifierID id_OK;
IdentifierID id_PAPER;
IdentifierID id_PARSE;
IdentifierID id_PAUSE;
IdentifierID id_PICTURE;
IdentifierID id_PLACE;
IdentifierID id_PLUS;
IdentifierID id_PREP;
IdentifierID id_PRESENT;
IdentifierID id_PRINT;
IdentifierID id_PRINTAT;
IdentifierID id_PROCESS;
IdentifierID id_PROMPT;
IdentifierID id_PUTIN;
IdentifierID id_PUTO;
IdentifierID id_QUIT;
IdentifierID id_RAMLOAD;
IdentifierID id_RAMSAVE;
IdentifierID id_RANDOM;
IdentifierID id_REDO;
IdentifierID id_REMOVE;
IdentifierID id_RESET;
IdentifierID id_RESTART;
IdentifierID id_SAME;
IdentifierID id_SAVE;
IdentifierID id_SAVEAT;
IdentifierID id_SET;
IdentifierID id_SETCO;
IdentifierID id_SFX;
IdentifierID id_SKIP;
IdentifierID id_SMALLER;
IdentifierID id_SPACE;
IdentifierID id_SUB;
IdentifierID id_SWAP;
IdentifierID id_SYNONYM;
IdentifierID id_SYSMESS;
IdentifierID id_TAB;
IdentifierID id_TAKEOUT;
IdentifierID id_TIME;
IdentifierID id_TIMEOUT;
IdentifierID id_TURNS;
IdentifierID id_WEAR;
IdentifierID id_WEIGH;
IdentifierID id_WEIGHT;
IdentifierID id_WHATO;
IdentifierID id_WINAT;
IdentifierID id_WINDOW;
IdentifierID id_WINSIZE;
IdentifierID id_ZERO;
IdentifierID id_WORN;

void RegisterCondactNames (Identifiers* id)
{
	id_ABILITY = GetIdentifierID(id, "ABILITY");
	id_ABSENT = GetIdentifierID(id, "ABSENT");
	id_ADD = GetIdentifierID(id, "ADD");
	id_ADJECT1 = GetIdentifierID(id, "ADJECT1");
	id_ADJECT2 = GetIdentifierID(id, "ADJECT2");
	id_ADVERB = GetIdentifierID(id, "ADVERB");
	id_ANYKEY = GetIdentifierID(id, "ANYKEY");
	id_AT = GetIdentifierID(id, "AT");
	id_ATGT = GetIdentifierID(id, "ATGT");
	id_ATLT = GetIdentifierID(id, "ATLT");
	id_AUTOD = GetIdentifierID(id, "AUTOD");
	id_AUTOG = GetIdentifierID(id, "AUTOG");
	id_AUTOP = GetIdentifierID(id, "AUTOP");
	id_AUTOR = GetIdentifierID(id, "AUTOR");
	id_AUTOT = GetIdentifierID(id, "AUTOT");
	id_AUTOW = GetIdentifierID(id, "AUTOW");
	id_BACKAT = GetIdentifierID(id, "BACKAT");
	id_BEEP = GetIdentifierID(id, "BEEP");
	id_BIGGER = GetIdentifierID(id, "BIGGER");
	id_BORDER = GetIdentifierID(id, "BORDER");
	id_CALL = GetIdentifierID(id, "CALL");
	id_CARRIED = GetIdentifierID(id, "CARRIED");
	id_CENTRE = GetIdentifierID(id, "CENTRE");
	id_CHANCE = GetIdentifierID(id, "CHANCE");
	id_CLEAR = GetIdentifierID(id, "CLEAR");
	id_CLS = GetIdentifierID(id, "CLS");
	id_COPYBF = GetIdentifierID(id, "COPYBF");
	id_COPYFF = GetIdentifierID(id, "COPYFF");
	id_COPYFO = GetIdentifierID(id, "COPYFO");
	id_COPYOF = GetIdentifierID(id, "COPYOF");
	id_COPYOO = GetIdentifierID(id, "COPYOO");
	id_CREATE = GetIdentifierID(id, "CREATE");
	id_DESC = GetIdentifierID(id, "DESC");
	id_DESTROY = GetIdentifierID(id, "DESTROY");
	id_DISPLAY = GetIdentifierID(id, "DISPLAY");
	id_DOALL = GetIdentifierID(id, "DOALL");
	id_DONE = GetIdentifierID(id, "DONE");
	id_DPRINT = GetIdentifierID(id, "DPRINT");
	id_DROP = GetIdentifierID(id, "DROP");
	id_DROPALL = GetIdentifierID(id, "DROPALL");
	id_END = GetIdentifierID(id, "END");
	id_EQ = GetIdentifierID(id, "EQ");
	id_EXIT = GetIdentifierID(id, "EXIT");
	id_EXTERN = GetIdentifierID(id, "EXTERN");
	id_GET = GetIdentifierID(id, "GET");
	id_GFX = GetIdentifierID(id, "GFX");
	id_GOTO = GetIdentifierID(id, "GOTO");
	id_GT = GetIdentifierID(id, "GT");
	id_HASAT = GetIdentifierID(id, "HASAT");
	id_HASNAT = GetIdentifierID(id, "HASNAT");
	id_INK = GetIdentifierID(id, "INK");
	id_INKEY = GetIdentifierID(id, "INKEY");
	id_INPUT = GetIdentifierID(id, "INPUT");
	id_ISAT = GetIdentifierID(id, "ISAT");
	id_ISDONE = GetIdentifierID(id, "ISDONE");
	id_ISNDONE = GetIdentifierID(id, "ISNDONE");
	id_ISNOTAT = GetIdentifierID(id, "ISNOTAT");
	id_LET = GetIdentifierID(id, "LET");
	id_LISTAT = GetIdentifierID(id, "LISTAT");
	id_LISTOBJ = GetIdentifierID(id, "LISTOBJ");
	id_LOAD = GetIdentifierID(id, "LOAD");
	id_LT = GetIdentifierID(id, "LT");
	id_MES = GetIdentifierID(id, "MES");
	id_MESSAGE = GetIdentifierID(id, "MESSAGE");
	id_MINUS = GetIdentifierID(id, "MINUS");
	id_MODE = GetIdentifierID(id, "MODE");
	id_MOUSE = GetIdentifierID(id, "MOUSE");
	id_MOVE = GetIdentifierID(id, "MOVE");
	id_NEWLINE = GetIdentifierID(id, "NEWLINE");
	id_NEWTEXT = GetIdentifierID(id, "NEWTEXT");
	id_NOTAT = GetIdentifierID(id, "NOTAT");
	id_NOTCARR = GetIdentifierID(id, "NOTCARR");
	id_NOTDONE = GetIdentifierID(id, "NOTDONE");
	id_NOTEQ = GetIdentifierID(id, "NOTEQ");
	id_NOTSAME = GetIdentifierID(id, "NOTSAME");
	id_NOTWORN = GetIdentifierID(id, "NOTWORN");
	id_NOTZERO = GetIdentifierID(id, "NOTZERO");
	id_NOUN2 = GetIdentifierID(id, "NOUN2");
	id_OK = GetIdentifierID(id, "OK");
	id_PAPER = GetIdentifierID(id, "PAPER");
	id_PARSE = GetIdentifierID(id, "PARSE");
	id_PAUSE = GetIdentifierID(id, "PAUSE");
	id_PICTURE = GetIdentifierID(id, "PICTURE");
	id_PLACE = GetIdentifierID(id, "PLACE");
	id_PLUS = GetIdentifierID(id, "PLUS");
	id_PREP = GetIdentifierID(id, "PREP");
	id_PRESENT = GetIdentifierID(id, "PRESENT");
	id_PRINT = GetIdentifierID(id, "PRINT");
	id_PRINTAT = GetIdentifierID(id, "PRINTAT");
	id_PROCESS = GetIdentifierID(id, "PROCESS");
	id_PROMPT = GetIdentifierID(id, "PROMPT");
	id_PUTIN = GetIdentifierID(id, "PUTIN");
	id_PUTO = GetIdentifierID(id, "PUTO");
	id_QUIT = GetIdentifierID(id, "QUIT");
	id_RAMLOAD = GetIdentifierID(id, "RAMLOAD");
	id_RAMSAVE = GetIdentifierID(id, "RAMSAVE");
	id_RANDOM = GetIdentifierID(id, "RANDOM");
	id_REDO = GetIdentifierID(id, "REDO");
	id_REMOVE = GetIdentifierID(id, "REMOVE");
	id_RESET = GetIdentifierID(id, "RESET");
	id_RESTART = GetIdentifierID(id, "RESTART");
	id_SAME = GetIdentifierID(id, "SAME");
	id_SAVE = GetIdentifierID(id, "SAVE");
	id_SAVEAT = GetIdentifierID(id, "SAVEAT");
	id_SET = GetIdentifierID(id, "SET");
	id_SETCO = GetIdentifierID(id, "SETCO");
	id_SFX = GetIdentifierID(id, "SFX");
	id_SKIP = GetIdentifierID(id, "SKIP");
	id_SMALLER = GetIdentifierID(id, "SMALLER");
	id_SPACE = GetIdentifierID(id, "SPACE");
	id_SUB = GetIdentifierID(id, "SUB");
	id_SWAP = GetIdentifierID(id, "SWAP");
	id_SYNONYM = GetIdentifierID(id, "SYNONYM");
	id_SYSMESS = GetIdentifierID(id, "SYSMESS");
	id_TAB = GetIdentifierID(id, "TAB");
	id_TAKEOUT = GetIdentifierID(id, "TAKEOUT");
	id_TIME = GetIdentifierID(id, "TIME");
	id_TIMEOUT = GetIdentifierID(id, "TIMEOUT");
	id_TURNS = GetIdentifierID(id, "TURNS");
	id_WEAR = GetIdentifierID(id, "WEAR");
	id_WEIGH = GetIdentifierID(id, "WEIGH");
	id_WEIGHT = GetIdentifierID(id, "WEIGHT");
	id_WHATO = GetIdentifierID(id, "WHATO");
	id_WINAT = GetIdentifierID(id, "WINAT");
	id_WINDOW = GetIdentifierID(id, "WINDOW");
	id_WINSIZE = GetIdentifierID(id, "WINSIZE");
	id_WORN = GetIdentifierID(id, "WORN");
	id_ZERO = GetIdentifierID(id, "ZERO");
}

IdentifierID id_pINCLUDE;
IdentifierID id_pIF;
IdentifierID id_pELSE;
IdentifierID id_pENDIF;
IdentifierID id_pDBADDR;
IdentifierID id_pDEFB;
IdentifierID id_pDEFINE;

#define BIT_MASK(n) ((unsigned)((1 << ((n)-1))-1))
#define ROUND_UP(n,pow2value) (((n) + (pow2value) - 1) & ~(pow2value-1))

static uint8_t scatter[256] =
{
	 39, 155, 102, 133,  17, 174, 134, 169, 
	 61, 166, 236, 151, 246,  51,  70,  18, 
	 89,  66, 140, 130, 252, 203, 243, 119, 
	 62, 105, 153, 255,  92, 249, 126, 197, 
	 93,  50, 195,   5,  66, 165, 113,  63, 
	220, 184, 151, 119, 100, 152,  40, 121, 
	121, 217, 119, 188, 142, 134, 225, 218, 
	 85, 206, 197,  52,  64,  89,  34, 154, 
	215,  66, 201,  47, 212, 204, 200,  58, 
	 22, 173, 243,  83, 108,  62,   5,  38, 
	 59, 238,  67,  18, 246, 175, 227,  69, 
	114, 196,  42,  63,  67,  67,  10,  24, 
	165, 144, 111, 117,  23,  33,  39,   3, 
	 76,   9, 183, 160, 131, 157, 221,  96, 
	 36, 212, 202, 168, 136, 157, 253,  20, 
	 91, 204, 244, 192, 113, 203, 191,  86, 
	 37, 162, 203, 233, 141, 196, 184, 135, 
	 62, 209,  60, 138, 103, 152, 167,  15, 
	 15, 122, 224,  34, 130,  28, 146, 115, 
	142, 108, 144, 172, 245, 101,  58, 231, 
	245, 224, 175,  73,   9,  75, 177, 250, 
	139, 190, 199, 120, 235,  88, 198,  42, 
	174, 243, 255,  67,  92, 169, 240, 133, 
	 43,  76,  95, 124, 235,  63, 169, 188, 
	164, 160,   5,  69, 220,   4, 132, 120, 
	155, 115,  67,   6, 106,  74, 188,  31, 
	 19,  30,   5,  15, 122, 224, 180, 156, 
	123, 160,  22,   3,  35, 239, 180,  57, 
	172, 140, 129, 152, 160, 171,  35, 254, 
	106,  44, 228, 224, 123, 154, 154, 184, 
	182, 238, 111, 239, 168, 150, 143,  17, 
	163,  74,  67, 115,  89, 240, 183,   1
};

static unsigned GetHash (DC_String s)
{
	unsigned  result = 0;
	int       character;

	while (s.ptr < s.end)
	{
		character = UTF8Next(s);
		result = (result << 1) + scatter[character & 0xFF];
	}
	return (result & HASH_MASK);
}

static int FindId (Bucket* bucket, DC_String s, uint32_t* info = 0)
{
	int bucket_num  = 0;
	int string_num  = 0;
	size_t size = s.end - s.ptr;

	while (bucket != NULL)
	{
		for (string_num = 0 ; string_num < (int)bucket->stringCount ; string_num++)
		{
			if (bucket->stringSize[string_num] == size)
			{
				DC_String bucketStr(bucket->stringData[string_num], bucket->stringSize[string_num]);
				if (UTF8ICompare(bucketStr, s) == 0)
				{
					if (info != 0)
						*info = bucket->stringInfo[string_num];
					return bucket_num * MAX_BUCKET_STRINGS + string_num;
				}
			}
		}
		bucket = bucket->next;
	}
	return -1;
}

static int AddId (Bucket* bucket, Arena* arena, DC_String str, uint32_t info)
{
	int         id = 0;
	uint8_t*    data;
	unsigned    number;

	while (bucket->stringCount == MAX_BUCKET_STRINGS)
	{
		id += MAX_BUCKET_STRINGS;
		if (bucket->next == NULL)
			bucket->next = Allocate<Bucket>(arena);
		bucket = bucket->next;
	}
	id += (int)bucket->stringCount;

	int size = str.end - str.ptr;

	number = bucket->stringCount;
	data   = Allocate<uint8_t>(arena, size);
	if (data != NULL)
	{
		bucket->stringSize[number] = size;
		bucket->stringData[number] = data;
		bucket->stringInfo[number] = info;
		MemCopy(data, str.ptr, size);
		bucket->stringCount++;
	}
	return id;
}

// ----------------------------------------------------------------------

Identifiers* NewIdentifiersTable (Arena* arena)
{
	Identifiers* table = Allocate<Identifiers>(arena);
	if (table != NULL)
	{
		table->arena = arena;
		GetIdentifierID (table, "");
	}
	return table;
}

IdentifierID GetIdentifierID (Identifiers* table, DC_String str, uint32_t* info)
{
	unsigned            hash = GetHash(str);
	Bucket*             bucket = table->buckets[hash];
	int                 bucket_id;

	/* Allocate the first bucket if it does not exist */
	if (bucket == NULL)
	{
		bucket = table->buckets[hash] = Allocate<Bucket>(table->arena);
		if (bucket == NULL)
			return INVALID_IDENTIFIER;
	}

	/* Check if the identifier already exists */
	bucket_id = FindId(bucket, str, info);
	if (bucket_id == -1)
		bucket_id = AddId(bucket, table->arena, str, info ? *info : 0);
	return hash | (unsigned)(bucket_id << HASH_BITS);
}

IdentifierID FindIdentifierID (Identifiers* table, DC_String str, uint32_t* info)
{
	unsigned hash = GetHash(str);
	return FindId(table->buckets[hash], str, info);
}

void SetIdentifierInfo (Identifiers* table, IdentifierID id, uint32_t info)
{
	unsigned hash = (id & BIT_MASK(HASH_BITS));
	Bucket*  bucket = table->buckets[hash];
	unsigned bucket_id = (id >> HASH_BITS);
    
	if (bucket == NULL)
		return;
	while (bucket_id >= MAX_BUCKET_STRINGS)
	{
		if (bucket->next == NULL)
			return;
		bucket = bucket->next;
		bucket_id -= MAX_BUCKET_STRINGS;
	}
	if (bucket_id > bucket->stringCount)
		return;

	bucket->stringInfo[bucket_id] = info;
}

const uint8_t* GetIdentifierText (Identifiers* table, IdentifierID id)
{
	unsigned hash = (id & BIT_MASK(HASH_BITS));
	Bucket*  bucket = table->buckets[hash];
	unsigned bucket_id = (id >> HASH_BITS);
    
	if (bucket == NULL)
		return 0;
	while (bucket_id >= MAX_BUCKET_STRINGS)
	{
		if (bucket->next == NULL)
			return 0;
		bucket = bucket->next;
		bucket_id -= MAX_BUCKET_STRINGS;
	}
	if (bucket_id > bucket->stringCount)
		return 0;
	return bucket->stringData[bucket_id];
}

int GetIdentifierCount (Identifiers* table)
{
	int n;
	int count = 0;

	for (n = 0 ; n < HASH_SLOTS ; n++)
	{
		Bucket* bucket = table->buckets[n];
		while (bucket != NULL)
		{
			count += (int)bucket->stringCount;
			bucket = bucket->next;
		}
	}
	return count;
}
