#include <cli_parser.h>
#include <os_lib.h>

#include <stdio.h>
#include <string.h>

static const CLI_ActionSpec* FindAction(const CLI_ActionSpec* actions, const char* token)
{
    if (actions == 0 || token == 0)
        return 0;

    for (int i = 0; actions[i].name != 0; i++)
    {
        if (stricmp(actions[i].name, token) == 0)
            return &actions[i];
    }
    return 0;
}

static const CLI_OptionSpec* FindOption(const CLI_OptionSpec* options, char shortName)
{
    if (options == 0)
        return 0;

    for (int i = 0; options[i].shortName != 0; i++)
    {
        if (options[i].shortName == shortName)
            return &options[i];
    }
    return 0;
}

static bool SetError(char* errorBuffer, size_t errorBufferSize, const char* format, const char* value)
{
    if (errorBuffer != 0 && errorBufferSize > 0)
        snprintf(errorBuffer, errorBufferSize, format, value);
    return false;
}

static bool AddOption(CLI_CommandLine* commandLine, int id, const char* value, char* errorBuffer, size_t errorBufferSize)
{
    if (commandLine->optionCount >= CLI_MAX_PARSED_OPTIONS)
        return SetError(errorBuffer, errorBufferSize, "Too many options", "");

    commandLine->options[commandLine->optionCount].id = id;
    commandLine->options[commandLine->optionCount].value = value;
    commandLine->optionCount++;
    return true;
}

bool CLI_ParseCommandLine(
    int argc,
    char* argv[],
    const CLI_ActionSpec* actions,
    int defaultAction,
    const CLI_OptionSpec* options,
    CLI_CommandLine* commandLine,
    char* errorBuffer,
    size_t errorBufferSize)
{
    MemClear(commandLine, sizeof(*commandLine));
    commandLine->action = defaultAction;

    int index = 1;
    while (index < argc)
    {
        const char* token = argv[index];
        if (token == 0 || token[0] == 0)
            break;

        if (strcmp(token, "--") == 0)
        {
            index++;
            break;
        }

        if (token[0] == '-' && token[1] != 0)
        {
            const char* ptr = token + 1;
            while (*ptr)
            {
                const CLI_OptionSpec* option = FindOption(options, *ptr);
                if (option == 0)
                {
                    char invalid[2] = { *ptr, 0 };
                    return SetError(errorBuffer, errorBufferSize, "Unknown option: \"%s\"", invalid);
                }

                if (option->argumentMode == CLI_OPTION_REQUIRED_VALUE)
                {
                    const char* value = ptr[1] != 0 ? ptr + 1 : 0;
                    if (value == 0)
                    {
                        index++;
                        if (index >= argc)
                        {
                            char invalid[2] = { option->shortName, 0 };
                            return SetError(errorBuffer, errorBufferSize, "Missing value for option: \"%s\"", invalid);
                        }
                        value = argv[index];
                    }
                    if (!AddOption(commandLine, option->id, value, errorBuffer, errorBufferSize))
                        return false;
                    break;
                }

                if (!AddOption(commandLine, option->id, 0, errorBuffer, errorBufferSize))
                    return false;
                ptr++;
            }
            index++;
            continue;
        }

        if (commandLine->argumentCount == 0)
        {
            const CLI_ActionSpec* action = FindAction(actions, token);
            if (action != 0)
            {
                commandLine->action = action->value;
                commandLine->actionName = action->name;
                index++;
                continue;
            }
        }

        break;
    }

    for (; index < argc; index++)
    {
        if (commandLine->argumentCount >= CLI_MAX_ARGUMENTS)
            return SetError(errorBuffer, errorBufferSize, "Too many arguments", "");
        commandLine->arguments[commandLine->argumentCount++] = argv[index];
    }

    return true;
}

bool CLI_HasOption(const CLI_CommandLine* commandLine, int optionId)
{
    for (int i = 0; i < commandLine->optionCount; i++)
    {
        if (commandLine->options[i].id == optionId)
            return true;
    }
    return false;
}

const char* CLI_GetOptionValue(const CLI_CommandLine* commandLine, int optionId)
{
    for (int i = commandLine->optionCount - 1; i >= 0; i--)
    {
        if (commandLine->options[i].id == optionId)
            return commandLine->options[i].value;
    }
    return 0;
}
