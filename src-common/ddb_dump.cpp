#include <ddb.h>
#include <os_char.h>
#include <os_lib.h>

static uint8_t buffer[2048];

const char* DDB_GetCondactName(DDB_Condact condact)
{
	switch (condact)
	{
		case CONDACT_ABILITY: return "ABILITY";
		case CONDACT_ABSENT:  return "ABSENT";
		case CONDACT_ADD:     return "ADD";
		case CONDACT_ADJECT1: return "ADJECT1";
		case CONDACT_ADJECT2: return "ADJECT2";
		case CONDACT_ADVERB:  return "ADVERB";
		case CONDACT_ANYKEY:  return "ANYKEY";
		case CONDACT_AT:      return "AT";
		case CONDACT_ATGT:    return "ATGT";
		case CONDACT_ATLT:    return "ATLT";
		case CONDACT_AUTOD:   return "AUTOD";
		case CONDACT_AUTOG:   return "AUTOG";
		case CONDACT_AUTOP:   return "AUTOP";
		case CONDACT_AUTOR:   return "AUTOR";
		case CONDACT_AUTOT:   return "AUTOT";
		case CONDACT_AUTOW:   return "AUTOW";
		case CONDACT_BACKAT:  return "BACKAT";
		case CONDACT_BEEP:    return "BEEP";
		case CONDACT_BIGGER:  return "BIGGER";
		case CONDACT_BORDER:  return "BORDER";
		case CONDACT_CALL:    return "CALL";
		case CONDACT_CARRIED: return "CARRIED";
		case CONDACT_CENTRE:  return "CENTRE";
		case CONDACT_CHANCE:  return "CHANCE";
		case CONDACT_CLEAR:   return "CLEAR";
		case CONDACT_CLS:     return "CLS";
		case CONDACT_COPYBF:  return "COPYBF";
		case CONDACT_COPYFF:  return "COPYFF";
		case CONDACT_COPYFO:  return "COPYFO";
		case CONDACT_COPYOF:  return "COPYOF";
		case CONDACT_COPYOO:  return "COPYOO";
		case CONDACT_CREATE:  return "CREATE";
		case CONDACT_DESC:    return "DESC";
		case CONDACT_DESTROY: return "DESTROY";
		case CONDACT_DISPLAY: return "DISPLAY";
		case CONDACT_DOALL:   return "DOALL";
		case CONDACT_DONE:    return "DONE";
		case CONDACT_DPRINT:  return "DPRINT";
		case CONDACT_DROP:    return "DROP";
		case CONDACT_DROPALL: return "DROPALL";
		case CONDACT_END:     return "END";
		case CONDACT_EQ:      return "EQ";
		case CONDACT_EXIT:    return "EXIT";
		case CONDACT_EXTERN:  return "EXTERN";
		case CONDACT_GET:     return "GET";
		case CONDACT_GFX:     return "GFX";
		case CONDACT_GOTO:    return "GOTO";
		case CONDACT_GRAPHIC: return "GRAPHIC";
		case CONDACT_GT:      return "GT";
		case CONDACT_HASAT:   return "HASAT";
		case CONDACT_HASNAT:  return "HASNAT";
		case CONDACT_INK:     return "INK";
		case CONDACT_INKEY:   return "INKEY";
		case CONDACT_INPUT:   return "INPUT";
		case CONDACT_ISAT:    return "ISAT";
		case CONDACT_ISDONE:  return "ISDONE";
		case CONDACT_ISNDONE: return "ISNDONE";
		case CONDACT_ISNOTAT: return "ISNOTAT";
		case CONDACT_LET:     return "LET";
		case CONDACT_LISTAT:  return "LISTAT";
		case CONDACT_LISTOBJ: return "LISTOBJ";
		case CONDACT_LOAD:    return "LOAD";
		case CONDACT_LT:      return "LT";
		case CONDACT_MES:     return "MES";
		case CONDACT_MESSAGE: return "MESSAGE";
		case CONDACT_MINUS:   return "MINUS";
		case CONDACT_MODE:    return "MODE";
		case CONDACT_MOUSE:   return "MOUSE";
		case CONDACT_MOVE:    return "MOVE";
		case CONDACT_NEWLINE: return "NEWLINE";
		case CONDACT_NEWTEXT: return "NEWTEXT";
		case CONDACT_NOTAT:   return "NOTAT";
		case CONDACT_NOTCARR: return "NOTCARR";
		case CONDACT_NOTDONE: return "NOTDONE";
		case CONDACT_NOTEQ:   return "NOTEQ";
		case CONDACT_NOTSAME: return "NOTSAME";
		case CONDACT_NOTWORN: return "NOTWORN";
		case CONDACT_NOTZERO: return "NOTZERO";
		case CONDACT_NOUN2:   return "NOUN2";
		case CONDACT_OK:      return "OK";
		case CONDACT_PAPER:   return "PAPER";
		case CONDACT_PARSE:   return "PARSE";
		case CONDACT_PAUSE:   return "PAUSE";
		case CONDACT_PICTURE: return "PICTURE";
		case CONDACT_PLACE:   return "PLACE";
		case CONDACT_PLUS:    return "PLUS";
		case CONDACT_PREP:    return "PREP";
		case CONDACT_PRESENT: return "PRESENT";
		case CONDACT_PRINT:   return "PRINT";
		case CONDACT_PRINTAT: return "PRINTAT";
		case CONDACT_PROCESS: return "PROCESS";
		case CONDACT_PROMPT:  return "PROMPT";
		case CONDACT_PUTIN:   return "PUTIN";
		case CONDACT_PUTO:    return "PUTO";
		case CONDACT_QUIT:    return "QUIT";
		case CONDACT_RAMLOAD: return "RAMLOAD";
		case CONDACT_RAMSAVE: return "RAMSAVE";
		case CONDACT_RANDOM:  return "RANDOM";
		case CONDACT_REDO:    return "REDO";
		case CONDACT_REMOVE:  return "REMOVE";
		case CONDACT_RESET:   return "RESET";
		case CONDACT_RESTART: return "RESTART";
		case CONDACT_SAME:    return "SAME";
		case CONDACT_SAVE:    return "SAVE";
		case CONDACT_SAVEAT:  return "SAVEAT";
		case CONDACT_SET:     return "SET";
		case CONDACT_SETCO:   return "SETCO";
		case CONDACT_SFX:     return "SFX";
		case CONDACT_SKIP:    return "SKIP";
		case CONDACT_SMALLER: return "SMALLER";
		case CONDACT_SPACE:   return "SPACE";
		case CONDACT_SUB:     return "SUB";
		case CONDACT_SWAP:    return "SWAP";
		case CONDACT_SYNONYM: return "SYNONYM";
		case CONDACT_SYSMESS: return "SYSMESS";
		case CONDACT_TAB:     return "TAB";
		case CONDACT_TAKEOUT: return "TAKEOUT";
		case CONDACT_TIME:    return "TIME";
		case CONDACT_TIMEOUT: return "TIMEOUT";
		case CONDACT_TURNS:   return "TURNS";
		case CONDACT_WEAR:    return "WEAR";
		case CONDACT_WEIGH:   return "WEIGH";
		case CONDACT_WEIGHT:  return "WEIGHT";
		case CONDACT_WHATO:   return "WHATO";
		case CONDACT_WINAT:   return "WINAT";
		case CONDACT_WINDOW:  return "WINDOW";
		case CONDACT_WINSIZE: return "WINSIZE";
		case CONDACT_WORN:    return "WORN";
		case CONDACT_ZERO:    return "ZERO";
		
		case CONDACT_INVEN:   return "INVEN";
		case CONDACT_SCORE:   return "SCORE";
		case CONDACT_CHARSET: return "CHARSET";
		case CONDACT_LINE:    return "LINE";
		case CONDACT_PROTECT: return "PROTECT";

		case CONDACT_INVALID: return "[Invalid Condact]";
		default:              return "[Unknown Condact]";
	}
}

void DDB_DumpVocabularyWord (DDB* ddb, uint8_t type, uint8_t index, DDB_PrintFunc print)
{
	uint8_t* ptr = ddb->vocabulary;

	while (*ptr != 0)
	{
		if (ptr[5] == index && ptr[6] == type)
		{
			for (int n = 0; n < 5; n++)
				print("%c", DDB_Char2ISO[(ptr[n] ^ 0xFF) & 0x7F]);
			return;
		}
		ptr += 7;
	}
	if (type == WordType_Verb && index < 40)
		DDB_DumpVocabularyWord(ddb, WordType_Noun, index, print);
	else
		print("%-5d", index);
}

static bool DDB_DumpMessage(DDB* ddb, DDB_MsgType type, uint8_t n, DDB_PrintFunc print, int maxLength)
{
	DDB_GetMessage(ddb, type, n, (char *)buffer, sizeof(buffer));
	if (buffer[0] == 0) return false;
	for (const unsigned char* ptr = buffer; *ptr; ptr++)
	{
		if (maxLength > 0)
		{
			if (--maxLength == 0)
			{
				print("...");
				break;
			}
		}
		if (*ptr == '\r')
			print("\\n");
		else if (*ptr == '\t')
			print("\\t");
		else if (*ptr < 16 || *ptr >= 127)
			print("\\x%02X", (uint8_t)*ptr);
		else
			print("%c", DDB_Char2ISO[*ptr]);
	}
	return true;
}

void DDB_DumpMessageTable (DDB* ddb, DDB_MsgType type, DDB_PrintFunc print)
{
	int count;

	switch (type)
	{
		case DDB_LOCDESC: count = ddb->numLocations; break;
		case DDB_OBJNAME: count = ddb->numObjects; break;
		case DDB_MSG:     count = ddb->numMessages; break;
		case DDB_SYSMSG:  count = ddb->numSystemMessages; break;
		default:          return;
	}
	
	for (int n = 0 ; n < count; n++)
	{
		print("/%d\n", n);
		if (DDB_DumpMessage(ddb, type, n, print, 0))
			print("\n");
	}
	print("\n");
}

void DDB_DumpProcess (DDB* ddb, uint8_t index, DDB_PrintFunc print)
{
	print("\n/PRO %d\n\n", index);

	if (index >= ddb->numProcesses || ddb->processTable[index] == 0)
		return;

	uint8_t* entry = ddb->data + ddb->processTable[index];
	while (entry < ddb->data + ddb->dataSize)
	{
		if (*entry == 0)
			break;

		uint8_t  verb   = entry[0];
		uint8_t  noun   = entry[1];
		uint16_t offset = *(uint16_t*)(entry + 2);
		uint8_t* code   = ddb->data + offset;
		if (code >= ddb->data + ddb->dataSize || code == ddb->data)
			break;
		entry += 4;

		if (verb == 255)
			print("_    ");
		else
			DDB_DumpVocabularyWord(ddb, WordType_Verb, verb, print);
		print("       ");

		if (noun == 255)
			print("_    ");
		else
			DDB_DumpVocabularyWord(ddb, WordType_Noun, noun, print);
		print("       ");

		bool first = true;
		while (*code != 0xFF)
		{
			uint8_t condactIndex = *code & 0x7F;
			bool indirection = (*code & 0x80) != 0;
			int parameters = ddb->condactMap[condactIndex].parameters;
			uint8_t condact = ddb->condactMap[condactIndex].condact;
			code++;

			if (!first)
				print("                        ");
			print("%-12s", DDB_GetCondactName((DDB_Condact)condact));
			if (parameters > 0)
			{
				if (indirection)
					print("[%d]", *code++);
				else
					print("%d", *code++);
				if (parameters > 1)
					print(" %d", *code++);
			}
			if (!indirection)
			{
				if (condact == CONDACT_MES || condact == CONDACT_MESSAGE)
				{
					print("\t\t; ");
					DDB_DumpMessage(ddb, DDB_MSG, code[-1], print, 50);
				}
				else if (condact == CONDACT_SYSMESS)
				{
					print("\t\t; ");
					DDB_DumpMessage(ddb, DDB_SYSMSG, code[-1], print, 50);
				}
			}
			print("\n");
			first = false;

			if (condact == CONDACT_DONE || 
				condact == CONDACT_NOTDONE ||
				condact == CONDACT_OK ||
				condact == CONDACT_SKIP ||
				condact == CONDACT_RESTART ||
				condact == CONDACT_REDO)
				break;
		}
		print("\n");
	}
	print("\n");
}

void DDB_Dump (DDB* ddb, DDB_PrintFunc print)
{
	print("\n/CTL\n_\n\n");

	if (ddb->hasTokens)
	{
		print("/TOK\n");
		for (int n = 0; n < 128; n++)
		{
			uint8_t* ptr = ddb->tokensPtr[n];
			if (ptr != 0)
			{
				for (;; ptr++)
				{
					if ((*ptr & 0x7F) == ' ')
						print("_");
					else
						print("%c", DDB_Char2ISO[*ptr & 0x7F]);
					if (*ptr & 0x80)
						break;
				}
			}
			print("\n");
		}
		print("\n");
	}

	print("/VOC\n");
	for (uint8_t* ptr = ddb->vocabulary; *ptr; ptr += 7)
	{
		for (int n = 0; n < 5; n++)
			print("%c", DDB_Char2ISO[(ptr[n] ^ 0xFF) & 0x7F]);
		print("       %3d    ", ptr[5]);
		switch (ptr[6])
		{
			case WordType_Verb:        print("Verb\n"); break;
			case WordType_Noun:        print("Noun\n"); break;
			case WordType_Adjective:   print("Adjective\n"); break;
			case WordType_Adverb:      print("Adverb\n"); break;
			case WordType_Preposition: print("Preposition\n"); break;
			case WordType_Conjunction: print("Conjunction\n"); break;
			case WordType_Pronoun:     print("Pronoun\n"); break;
			default:              print("%d\n", ptr[6]); break;
		}
	}
	print("\n");

	print("/STX\n");
	DDB_DumpMessageTable(ddb, DDB_SYSMSG, print);
	print("/MTX\n");
	DDB_DumpMessageTable(ddb, DDB_MSG, print);
	print("/OTX\n");
	DDB_DumpMessageTable(ddb, DDB_OBJNAME, print);
	print("/LTX\n");
	DDB_DumpMessageTable(ddb, DDB_LOCDESC, print);

	print("/CON\n");
	for (int n = 0; n < ddb->numLocations; n++)
	{
		uint16_t offset = ddb->connections[n];

		print("/%d\n", n);
		if (offset == 0)
			continue;
		uint8_t* ptr = ddb->data + offset;
		while (*ptr != 0xFF && ptr < ddb->data + ddb->dataSize)
		{
			DDB_DumpVocabularyWord(ddb, WordType_Verb, *ptr++, print);
			print(" %d\n", *ptr++);
		}
	}
	print("\n");

	if (ddb->objExAttrTable == 0)
		print("/OBJ\n;       LOC     WEIGHT  CONT?   WEAR?   NOUN    ADJECTIVE\n");
	else
		print("/OBJ\n;       LOC     WEIGHT  CONT?   WEAR?   -------------------------------   NOUN    ADJECTIVE\n");

	for (int n = 0; n < ddb->numObjects; n++)
	{
		print("/%-7d", n);

		uint8_t loc = ddb->objLocTable[n];
		if (loc == 252)
			print("_       ");
		else
			print("%-8d", loc);

		uint8_t flags = ddb->objAttrTable[n];
		print("%-8d", flags & Obj_Weight);
		print("%-8c", (flags & Obj_Container) ? 'Y' : '_');
		print("%-8c", (flags & Obj_Wearable) ? 'Y' : '_');

		if (ddb->objExAttrTable != 0)
		{
			uint16_t flags = ddb->objExAttrTable[n];
			flags = (flags >> 8) | (flags << 8);
			for (int i = 15; i >= 0; i--)
				print("%c ", (flags & (0x1 << i)) ? 'Y' : '_');
			print("  ");
		}

		uint8_t noun = ddb->objWordsTable[2*n];
		uint8_t adj  = ddb->objWordsTable[2*n+1];
		if (noun == 255)
			print("_    ");
		else
			DDB_DumpVocabularyWord(ddb, WordType_Noun, noun, print);
		print("   ");
		if (adj == 255)
			print("_    ");
		else
			DDB_DumpVocabularyWord(ddb, WordType_Adjective, adj, print);

		print("\n");
	}
	print("\n");

	for (int n = 0; n < ddb->numProcesses; n++)
		DDB_DumpProcess(ddb, n, print);
}

int DDB_DumpMessageTableMetrics (DDB* ddb, DDB_MsgType type, DDB_PrintFunc print)
{
	int count;
	int total = 0;
	uint16_t* table;
	const char* name;

	switch (type)
	{
		case DDB_LOCDESC: 
			table = ddb->msgTable;
			count = ddb->numLocations; 
			name = "location descriptions";
			break;
		case DDB_OBJNAME: 
			table = ddb->objNamTable;
			count = ddb->numObjects; 
			name = "object names";
			break;
		case DDB_MSG:     
			table = ddb->msgTable;
			count = ddb->numMessages; 
			name = "messages";
			break;
		case DDB_SYSMSG:  
			table = ddb->sysMsgTable;
			count = ddb->numSystemMessages; 
			name = "system messages";
			break;
		default:          
			return 0;
	}
	
	for (int n = 0 ; n < count; n++)
	{
		uint16_t offset = table[n];
		if (offset == 0) continue;
		uint8_t* ptr = ddb->data + offset;
		while (*ptr != 0xF5 && ptr < ddb->data + ddb->dataSize)
		{
			ptr++;
			total++;
		}
		total += 3;
	}
	print("%5d bytes in %d %s\n", total, count, name);
	return total;
}

void DDB_DumpMetrics (DDB* ddb, DDB_PrintFunc print)
{
	int total = 0;
	int count = 0;

	print("-------------------------------------------\n");

	if (ddb->hasTokens) 
	{
		print("%5d bytes in tokens\n", (int)ddb->tokenBlockSize);
		total += ddb->tokenBlockSize;
	}

	count = 0;
	for (uint8_t* ptr = ddb->vocabulary; *ptr; ptr += 7)
	{
		total += 7;
		count++;
	}
	print("%5d bytes in vocabulary (%d words)\n", total, count);

	total += DDB_DumpMessageTableMetrics(ddb, DDB_SYSMSG, print);
	total += DDB_DumpMessageTableMetrics(ddb, DDB_MSG, print);
	total += DDB_DumpMessageTableMetrics(ddb, DDB_OBJNAME, print);
	total += DDB_DumpMessageTableMetrics(ddb, DDB_LOCDESC, print);

	count = ddb->numObjects * 4;
	if (ddb->objExAttrTable)
		count += 2*ddb->numObjects;
	print("%5d bytes in object tables (%d objects)\n", count, ddb->numObjects);
	total += count;

	count = 0;
	for (int n = 0; n < ddb->numLocations; n++)
	{
		uint16_t offset = ddb->connections[n];
		if (offset == 0) continue;
		uint8_t* ptr = ddb->data + offset;
		while (*ptr != 0xFF && ptr < ddb->data + ddb->dataSize)
			ptr++, count++;
		count++;
	}
	print("%5d bytes in connections (%d locations)\n", count, ddb->numLocations);
	total += count;

	count = 2 * ddb->numProcesses;
	for (int n = 0; n < ddb->numProcesses; n++)
	{
		uint8_t* entry = ddb->data + ddb->processTable[n];
		while (entry < ddb->data + ddb->dataSize)
		{
			if (*entry == 0)
			{
				count++;
				break;
			}

			// uint8_t  verb   = entry[0];
			// uint8_t  noun   = entry[1];
			uint16_t offset = *(uint16_t*)(entry + 2);
			uint8_t* code   = ddb->data + offset;
			uint8_t* start  = code;
			if (code >= ddb->data + ddb->dataSize || code == ddb->data)
				break;

			count += 4;
			entry += 4;

			while (*code != 0xFF)
			{
				uint8_t condactIndex = *code & 0x7F;
				uint8_t condact = ddb->condactMap[condactIndex].condact;
				int parameters = ddb->condactMap[condactIndex].parameters;
				code += parameters + 1;

				if (condact == CONDACT_DONE || 
					condact == CONDACT_NOTDONE ||
					condact == CONDACT_OK ||
					condact == CONDACT_SKIP ||
					condact == CONDACT_RESTART ||
					condact == CONDACT_REDO)
					break;
			}

			count += code - start;
		}
	}
	print("%5d bytes in processes (%d processes)\n", count, ddb->numProcesses);
	total += count;

	print("-------------------------------------------\n");
	print("%5d bytes in total\n", total);
}