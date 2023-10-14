#include <ddb.h>
#include <os_file.h>
#include <os_lib.h>
#include <os_mem.h>
#include <os_bito.h>

extern void DDB_FixOffsets (DDB* ddb);

void DDB_RestoreOffsets (DDB* ddb)
{
	int n;

	uint16_t* tables[] = {
		ddb->msgTable,
		ddb->sysMsgTable,
		ddb->objNamTable,
		ddb->locDescTable,
		ddb->processTable,
		ddb->connections
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

	// Restore processes

	for (n = 0; n < ddb->numProcesses; n++)
	{
		int entryIndex = 0;
		uint16_t offset = ddb->processTable[n];
		if (offset == 0)
			continue;

		uint8_t* ptr = ddb->data + offset + 2;
		while (offset < ddb->dataSize)
		{
			// End of process marker
			if (ptr[-2] == 0) 
				break;

			uint16_t entryOffset = *(uint16_t*)ptr;
			entryOffset += ddb->baseOffset;
			write16(ptr, entryOffset, ddb->littleEndian);
			*(uint16_t*)ptr = entryOffset;

			ptr += 4;
			entryIndex++;
		}
	}

	// Restore tables

	for (n = 0; n < numTables; n++)
	{
		uint16_t* table = tables[n];
		uint8_t entries = counts[n];
		int m;
		if (table == 0)
			continue;
		for (m = 0; m < entries; m++)
		{
			uint16_t offset = table[m];
			offset += ddb->baseOffset;
			write16((uint8_t*)table + m * 2, offset, ddb->littleEndian);
		}
	}
}

DDB* DDB_Create()
{
	DDB* ddb = Allocate<DDB>("DDB");
	if (ddb == 0)
	{
		DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
		return 0;
	}

	ddb->version = 2;
	ddb->target = DDB_MACHINE_ATARIST;
	ddb->language = DDB_SPANISH;
	ddb->data = 0;
	ddb->dataSize = 0;
	return ddb;
}

bool DDB_Write(DDB* ddb, const char* filename)
{
	if (ddb->data == 0)
	{
		DDB_SetError(DDB_ERROR_INVALID_FILE);
		return false;
	}

	File* file = File_Create(filename);
	if (file == 0)
	{
		DDB_SetError(DDB_ERROR_CREATING_FILE);
		return false;
	}

	DDB_RestoreOffsets(ddb);
	uint64_t r = File_Write(file, ddb->data, ddb->dataSize);
	DDB_FixOffsets(ddb);
	File_Close(file);

	if (r != ddb->dataSize)
	{
		DDB_SetError(DDB_ERROR_CREATING_FILE);
		return false;
	}
	return true;
}