#include <dc_condacts.h>

#include <ddb_condact_defs.h>
#include <os_lib.h>

struct DC_NamedCondact
{
	const char* name;
	DDB_Condact condact;
};

static const DC_NamedCondact condactNames[] =
{
	{ "ABILITY", CONDACT_ABILITY },
	{ "ABSENT", CONDACT_ABSENT },
	{ "ADD", CONDACT_ADD },
	{ "ADJECT1", CONDACT_ADJECT1 },
	{ "ADJECT2", CONDACT_ADJECT2 },
	{ "ADVERB", CONDACT_ADVERB },
	{ "ANYKEY", CONDACT_ANYKEY },
	{ "AT", CONDACT_AT },
	{ "ATGT", CONDACT_ATGT },
	{ "ATLT", CONDACT_ATLT },
	{ "AUTOD", CONDACT_AUTOD },
	{ "AUTOG", CONDACT_AUTOG },
	{ "AUTOP", CONDACT_AUTOP },
	{ "AUTOR", CONDACT_AUTOR },
	{ "AUTOT", CONDACT_AUTOT },
	{ "AUTOW", CONDACT_AUTOW },
	{ "BACKAT", CONDACT_BACKAT },
	{ "BEEP", CONDACT_BEEP },
	{ "BORDER", CONDACT_BORDER },
	{ "BIGGER", CONDACT_BIGGER },
	{ "CALL", CONDACT_CALL },
	{ "CARRIED", CONDACT_CARRIED },
	{ "CENTRE", CONDACT_CENTRE },
	{ "CHANCE", CONDACT_CHANCE },
	{ "CLEAR", CONDACT_CLEAR },
	{ "CLS", CONDACT_CLS },
	{ "COPYBF", CONDACT_COPYBF },
	{ "COPYFF", CONDACT_COPYFF },
	{ "COPYFO", CONDACT_COPYFO },
	{ "COPYOF", CONDACT_COPYOF },
	{ "COPYOO", CONDACT_COPYOO },
	{ "CREATE", CONDACT_CREATE },
	{ "DESC", CONDACT_DESC },
	{ "DESTROY", CONDACT_DESTROY },
	{ "DISPLAY", CONDACT_DISPLAY },
	{ "DOALL", CONDACT_DOALL },
	{ "DONE", CONDACT_DONE },
	{ "DPRINT", CONDACT_DPRINT },
	{ "DROP", CONDACT_DROP },
	{ "DROPALL", CONDACT_DROPALL },
	{ "END", CONDACT_END },
	{ "EQ", CONDACT_EQ },
	{ "EXIT", CONDACT_EXIT },
	{ "EXTERN", CONDACT_EXTERN },
	{ "GET", CONDACT_GET },
	{ "GFX", CONDACT_GFX },
	{ "GOTO", CONDACT_GOTO },
	{ "GRAPHIC", CONDACT_GRAPHIC },
	{ "GT", CONDACT_GT },
	{ "HASAT", CONDACT_HASAT },
	{ "HASNAT", CONDACT_HASNAT },
	{ "INK", CONDACT_INK },
	{ "INKEY", CONDACT_INKEY },
	{ "INPUT", CONDACT_INPUT },
	{ "ISAT", CONDACT_ISAT },
	{ "ISDONE", CONDACT_ISDONE },
	{ "ISNDONE", CONDACT_ISNDONE },
	{ "ISNOTAT", CONDACT_ISNOTAT },
	{ "LET", CONDACT_LET },
	{ "LISTAT", CONDACT_LISTAT },
	{ "LISTOBJ", CONDACT_LISTOBJ },
	{ "LOAD", CONDACT_LOAD },
	{ "LT", CONDACT_LT },
	{ "MES", CONDACT_MES },
	{ "MESSAGE", CONDACT_MESSAGE },
	{ "MINUS", CONDACT_MINUS },
	{ "MODE", CONDACT_MODE },
	{ "MOUSE", CONDACT_MOUSE },
	{ "MOVE", CONDACT_MOVE },
	{ "NEWLINE", CONDACT_NEWLINE },
	{ "NEWTEXT", CONDACT_NEWTEXT },
	{ "NOTAT", CONDACT_NOTAT },
	{ "NOTCARR", CONDACT_NOTCARR },
	{ "NOTDONE", CONDACT_NOTDONE },
	{ "NOTEQ", CONDACT_NOTEQ },
	{ "NOTSAME", CONDACT_NOTSAME },
	{ "NOTWORN", CONDACT_NOTWORN },
	{ "NOTZERO", CONDACT_NOTZERO },
	{ "NOUN2", CONDACT_NOUN2 },
	{ "OK", CONDACT_OK },
	{ "PAPER", CONDACT_PAPER },
	{ "PARSE", CONDACT_PARSE },
	{ "PAUSE", CONDACT_PAUSE },
	{ "PICTURE", CONDACT_PICTURE },
	{ "PLACE", CONDACT_PLACE },
	{ "PLUS", CONDACT_PLUS },
	{ "PREP", CONDACT_PREP },
	{ "PRESENT", CONDACT_PRESENT },
	{ "PRINT", CONDACT_PRINT },
	{ "PRINTAT", CONDACT_PRINTAT },
	{ "PROCESS", CONDACT_PROCESS },
	{ "PROMPT", CONDACT_PROMPT },
	{ "PUTIN", CONDACT_PUTIN },
	{ "PUTO", CONDACT_PUTO },
	{ "QUIT", CONDACT_QUIT },
	{ "RAMLOAD", CONDACT_RAMLOAD },
	{ "RAMSAVE", CONDACT_RAMSAVE },
	{ "RANDOM", CONDACT_RANDOM },
	{ "REDO", CONDACT_REDO },
	{ "REMOVE", CONDACT_REMOVE },
	{ "RESET", CONDACT_RESET },
	{ "RESTART", CONDACT_RESTART },
	{ "SAME", CONDACT_SAME },
	{ "SAVE", CONDACT_SAVE },
	{ "SAVEAT", CONDACT_SAVEAT },
	{ "SET", CONDACT_SET },
	{ "SETCO", CONDACT_SETCO },
	{ "SFX", CONDACT_SFX },
	{ "SKIP", CONDACT_SKIP },
	{ "SMALLER", CONDACT_SMALLER },
	{ "SPACE", CONDACT_SPACE },
	{ "SUB", CONDACT_SUB },
	{ "SWAP", CONDACT_SWAP },
	{ "SYNONYM", CONDACT_SYNONYM },
	{ "SYSMESS", CONDACT_SYSMESS },
	{ "TAB", CONDACT_TAB },
	{ "TAKEOUT", CONDACT_TAKEOUT },
	{ "TIME", CONDACT_TIME },
	{ "TIMEOUT", CONDACT_TIMEOUT },
	{ "TURNS", CONDACT_TURNS },
	{ "WEAR", CONDACT_WEAR },
	{ "WEIGH", CONDACT_WEIGH },
	{ "WEIGHT", CONDACT_WEIGHT },
	{ "WHATO", CONDACT_WHATO },
	{ "WINAT", CONDACT_WINAT },
	{ "WINDOW", CONDACT_WINDOW },
	{ "WINSIZE", CONDACT_WINSIZE },
	{ "WORN", CONDACT_WORN },
	{ "ZERO", CONDACT_ZERO }
};

static bool FindCondactName(const char* name, DDB_Condact* condact)
{
	for (size_t i = 0; i < sizeof(condactNames) / sizeof(condactNames[0]); i++)
	{
		if (StrIComp(name, condactNames[i].name) == 0)
		{
			*condact = condactNames[i].condact;
			return true;
		}
	}
	return false;
}

static bool FindVersionCondactCode(DDB_Version version, DDB_Condact condact, uint8_t* opcode, uint8_t* parameters)
{
	if (version == DDB_VERSION_1)
	{
		#define DC_MATCH_VERSION1_CONDACT(code, mappedCondact, mappedParameters) \
			if (condact == mappedCondact) \
			{ \
				if (opcode != 0) \
					*opcode = code; \
				if (parameters != 0) \
					*parameters = mappedParameters; \
				return true; \
			}
		DDB_VERSION1_CONDACTS(DC_MATCH_VERSION1_CONDACT)
		#undef DC_MATCH_VERSION1_CONDACT
	}
	if (version == DDB_VERSION_2)
	{
		#define DC_MATCH_VERSION2_CONDACT(code, mappedCondact, mappedParameters) \
		if (condact == mappedCondact) \
		{ \
			if (opcode != 0) \
				*opcode = code; \
			if (parameters != 0) \
				*parameters = mappedParameters; \
			return true; \
		}
		DDB_VERSION2_CONDACTS(DC_MATCH_VERSION2_CONDACT)
		#undef DC_MATCH_VERSION2_CONDACT
	}
	return false;
}

bool DC_FindCondact(DDB_Version version, const char* name, uint8_t* opcode, uint8_t* parameters)
{
	if (version != DDB_VERSION_1 && version != DDB_VERSION_2)
		return false;
	DDB_Condact condact = CONDACT_INVALID;
	if (!FindCondactName(name, &condact))
		return false;
	return FindVersionCondactCode(version, condact, opcode, parameters);
}
