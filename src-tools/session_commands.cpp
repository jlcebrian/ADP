#include <session_commands.h>
#include <os_file.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static bool SetError(char* errorBuffer, size_t errorBufferSize, const char* format, const char* value)
{
    if (errorBuffer != 0 && errorBufferSize > 0)
        snprintf(errorBuffer, errorBufferSize, format, value);
    return false;
}

int Session_TokenizeLine(char* line, char* argv[], int maxArgs)
{
    int argc = 0;
    char* ptr = line;
    while (*ptr != 0)
    {
        while (*ptr && isspace((unsigned char)*ptr))
            ptr++;
        if (!*ptr)
            break;
        if (argc >= maxArgs)
            break;

        char quote = 0;
        if (*ptr == '"' || *ptr == '\'')
            quote = *ptr++;

        argv[argc++] = ptr;
        while (*ptr)
        {
            if (quote)
            {
                if (*ptr == quote)
                {
                    *ptr++ = 0;
                    break;
                }
            }
            else if (isspace((unsigned char)*ptr))
            {
                *ptr++ = 0;
                break;
            }
            ptr++;
        }
    }
    return argc;
}

bool Session_ExecuteTokenStream(
    int argc,
    char* argv[],
    const char* separator,
    Session_CommandHandler handler,
    void* context,
    char* errorBuffer,
    size_t errorBufferSize)
{
    if (handler == 0 || separator == 0 || separator[0] == 0)
        return SetError(errorBuffer, errorBufferSize, "Invalid session configuration: %s", "missing handler or separator");

    int commandStart = 0;
    bool sawSeparator = false;
    bool lastTokenWasSeparator = false;
    for (int i = 0; i <= argc; i++)
    {
        bool isSeparator = i < argc && strcmp(argv[i], separator) == 0;
        if (!isSeparator && i < argc)
            continue;

        if (isSeparator)
            sawSeparator = true;
        lastTokenWasSeparator = isSeparator;

        int commandArgc = i - commandStart;
        if (commandArgc <= 0)
            return SetError(errorBuffer, errorBufferSize, "Empty command near separator: \"%s\"", separator);

        if (!handler(commandArgc, argv + commandStart, context))
            return SetError(errorBuffer, errorBufferSize, "Command failed: \"%s\"", argv[commandStart]);

        commandStart = i + 1;
    }

    if (sawSeparator && lastTokenWasSeparator)
        return SetError(errorBuffer, errorBufferSize, "Empty command near separator: \"%s\"", separator);

    return true;
}

bool Session_ExecuteCommandFile(
    const char* fileName,
    const char* separator,
    Session_CommandHandler handler,
    void* context,
    char* errorBuffer,
    size_t errorBufferSize)
{
    if (fileName == 0 || fileName[0] == 0)
        return SetError(errorBuffer, errorBufferSize, "Invalid command file: %s", "missing filename");

    File* file = File_Open(fileName, ReadOnly);
    if (file == 0)
        return SetError(errorBuffer, errorBufferSize, "Unable to open command file: %s", fileName);

    uint64_t fileSize64 = File_GetSize(file);
    if (fileSize64 > 1024 * 1024)
    {
        File_Close(file);
        return SetError(errorBuffer, errorBufferSize, "Command file too large: %s", fileName);
    }

    size_t fileSize = (size_t)fileSize64;
    char* contents = (char*)malloc(fileSize + 1);
    if (contents == 0)
    {
        File_Close(file);
        return SetError(errorBuffer, errorBufferSize, "Out of memory reading command file: %s", fileName);
    }

    if (File_Read(file, contents, fileSize) != fileSize)
    {
        free(contents);
        File_Close(file);
        return SetError(errorBuffer, errorBufferSize, "Unable to read command file: %s", fileName);
    }
    File_Close(file);
    contents[fileSize] = 0;

    char* line = contents;
    int lineNumber = 0;
    while (*line != 0)
    {
        char* next = line;
        while (*next != 0 && *next != '\n' && *next != '\r')
            next++;

        char saved = *next;
        *next = 0;
        lineNumber++;

        char* trimmed = line;
        while (*trimmed != 0 && isspace((unsigned char)*trimmed))
            trimmed++;

        if (*trimmed != 0 && *trimmed != '#')
        {
            char* argv[128];
            int argc = Session_TokenizeLine(trimmed, argv, 128);
            if (argc > 0 && !Session_ExecuteTokenStream(argc, argv, separator, handler, context, errorBuffer, errorBufferSize))
            {
                if (errorBuffer != 0 && errorBufferSize > 0)
                {
                    char nested[256];
                    snprintf(nested, sizeof(nested), "%s", errorBuffer);
                    snprintf(errorBuffer, errorBufferSize, "%s:%d: %s", fileName, lineNumber, nested);
                }
                free(contents);
                return false;
            }
        }

        *next = saved;
        while (*next == '\n' || *next == '\r')
            next++;
        line = next;
    }

    free(contents);
    return true;
}
