#pragma once

#include <stddef.h>
#include <stdbool.h>

#define CLI_MAX_PARSED_OPTIONS 64
#define CLI_MAX_ARGUMENTS 512

typedef enum
{
    CLI_OPTION_NONE,
    CLI_OPTION_REQUIRED_VALUE,
}
CLI_OptionArgumentMode;

typedef struct
{
    const char* name;
    int value;
}
CLI_ActionSpec;

typedef struct
{
    char shortName;
    int id;
    CLI_OptionArgumentMode argumentMode;
}
CLI_OptionSpec;

typedef struct
{
    int id;
    const char* value;
}
CLI_ParsedOption;

typedef struct
{
    int action;
    const char* actionName;
    int optionCount;
    CLI_ParsedOption options[CLI_MAX_PARSED_OPTIONS];
    int argumentCount;
    const char* arguments[CLI_MAX_ARGUMENTS];
}
CLI_CommandLine;

bool CLI_ParseCommandLine(
    int argc,
    char* argv[],
    const CLI_ActionSpec* actions,
    int defaultAction,
    const CLI_OptionSpec* options,
    CLI_CommandLine* commandLine,
    char* errorBuffer,
    size_t errorBufferSize);

bool CLI_HasOption(const CLI_CommandLine* commandLine, int optionId);
const char* CLI_GetOptionValue(const CLI_CommandLine* commandLine, int optionId);
