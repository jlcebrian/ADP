#pragma once

#include <stddef.h>
#include <stdbool.h>

typedef bool (*Session_CommandHandler)(int argc, char* argv[], void* context);

int Session_TokenizeLine(char* line, char* argv[], int maxArgs);

bool Session_ExecuteTokenStream(
    int argc,
    char* argv[],
    const char* separator,
    Session_CommandHandler handler,
    void* context,
    char* errorBuffer,
    size_t errorBufferSize);

bool Session_ExecuteCommandFile(
    const char* fileName,
    const char* separator,
    Session_CommandHandler handler,
    void* context,
    char* errorBuffer,
    size_t errorBufferSize);
