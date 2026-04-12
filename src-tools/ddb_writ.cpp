#include <ddb.h>
#include <os_file.h>
#include <os_lib.h>
#include <os_mem.h>
#include <os_bito.h>

extern void DDB_FixOffsets (DDB* ddb);

static uint32_t DDB_GetHeaderSize(const DDB* ddb)
{
	return ddb->version == DDB_VERSION_1 ? 34 : 36;
}

static uint16_t DDB_GetStoredPointer(const DDB* ddb, const void* ptr, uint16_t storedBase)
{
	if (ptr == 0)
		return 0;

	const uint8_t* bytes = (const uint8_t*)ptr;
	if (bytes < ddb->data || bytes >= ddb->data + ddb->dataSize)
		return 0;

	return DDB_EncodeStoredOffset((uint32_t)(bytes - ddb->data), storedBase);
}

static void DDB_WriteHeader(DDB* ddb, uint16_t storedBase)
{
	if (ddb->dataSize < DDB_GetHeaderSize(ddb))
		return;

	write16(ddb->data + 0x08,
		(ddb->hasTokens && ddb->tokens != 0) ? DDB_GetStoredPointer(ddb, ddb->tokens, storedBase) : 0,
		ddb->littleEndian);
	write16(ddb->data + 0x0A, DDB_GetStoredPointer(ddb, ddb->processTable, storedBase), ddb->littleEndian);
	write16(ddb->data + 0x0C, DDB_GetStoredPointer(ddb, ddb->objNamTable, storedBase), ddb->littleEndian);
	write16(ddb->data + 0x0E, DDB_GetStoredPointer(ddb, ddb->locDescTable, storedBase), ddb->littleEndian);
	write16(ddb->data + 0x10, DDB_GetStoredPointer(ddb, ddb->msgTable, storedBase), ddb->littleEndian);
	write16(ddb->data + 0x12, DDB_GetStoredPointer(ddb, ddb->sysMsgTable, storedBase), ddb->littleEndian);
	write16(ddb->data + 0x14, DDB_GetStoredPointer(ddb, ddb->conTable, storedBase), ddb->littleEndian);
	write16(ddb->data + 0x16, DDB_GetStoredPointer(ddb, ddb->vocabulary, storedBase), ddb->littleEndian);
	write16(ddb->data + 0x18, DDB_GetStoredPointer(ddb, ddb->objLocTable, storedBase), ddb->littleEndian);
	write16(ddb->data + 0x1A, DDB_GetStoredPointer(ddb, ddb->objWordsTable, storedBase), ddb->littleEndian);
	write16(ddb->data + 0x1C, DDB_GetStoredPointer(ddb, ddb->objAttrTable, storedBase), ddb->littleEndian);

	if (ddb->version >= DDB_VERSION_2)
	{
		write16(ddb->data + 0x1E, DDB_GetStoredPointer(ddb, ddb->objExAttrTable, storedBase), ddb->littleEndian);
		write16(ddb->data + 0x20, DDB_EncodeStoredOffset(ddb->dataSize, storedBase), ddb->littleEndian);
		write16(ddb->data + 0x22, DDB_GetStoredPointer(ddb, ddb->externData, storedBase), ddb->littleEndian);
	}
	else
	{
		write16(ddb->data + 0x1E, DDB_EncodeStoredOffset(ddb->dataSize, storedBase), ddb->littleEndian);
		write16(ddb->data + 0x20, DDB_GetStoredPointer(ddb, ddb->externData, storedBase), ddb->littleEndian);
	}

	if (ddb->dataSize >= 0x2A)
		write16(ddb->data + 0x28, DDB_GetStoredPointer(ddb, ddb->externPsgTable, storedBase), ddb->littleEndian);
}

void DDB_RestoreOffsets (DDB* ddb, uint16_t storedBase)
{
	int n;

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
			entryOffset = DDB_EncodeStoredOffset(entryOffset, storedBase);
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
			offset = DDB_EncodeStoredOffset(offset, storedBase);
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

	ddb->version = DDB_VERSION_2;
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

	if (ddb->msgTable == 0 || ddb->locDescTable == 0 || ddb->conTable == 0)
	{
		// PAWS 128K databases do not have unified locations & message tables
		// and rely on internal DDB pointers instead.

		// TODO: Add support to write those databases to DDB files.
		// It may not be feasible since DDBs are limited to 64K (!)
		
		DDB_SetError(DDB_ERROR_INVALID_FILE);
		return false;
	}

	File* file = File_Create(filename);
	if (file == 0)
	{
		DDB_SetError(DDB_ERROR_CREATING_FILE);
		return false;
	}

	uint8_t originalHeader[0x2A];
	uint32_t originalHeaderSize = ddb->dataSize < sizeof(originalHeader) ? ddb->dataSize : sizeof(originalHeader);
	MemCopy(originalHeader, ddb->data, originalHeaderSize);

	DDB_RestoreOffsets(ddb, 0);
	DDB_WriteHeader(ddb, 0);
	uint64_t r = File_Write(file, ddb->data, ddb->dataSize);
	MemCopy(ddb->data, originalHeader, originalHeaderSize);
	DDB_FixOffsets(ddb);
	File_Close(file);

	if (r != ddb->dataSize)
	{
		DDB_SetError(DDB_ERROR_CREATING_FILE);
		return false;
	}
	return true;
}