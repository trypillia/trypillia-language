#include "Options.h"

#include <string>

namespace cli
{
static bool isCommandKeyword(const std::string &arg)
{
    return arg == "build" || arg == "fmt" || arg == "run";
}

static CommandKind kindFromKeyword(const std::string &kw)
{
    if (kw == "build")
        return CommandKind::Build;
    if (kw == "fmt")
        return CommandKind::Fmt;
    return CommandKind::Run;
}

std::optional<ParsedArgs> parseArgs(int argc, char **argv)
{
    ParsedArgs out;
    enum class State
    {
        ScanFlags,
        AfterCommand,
        AfterFile
    };
    State st = State::ScanFlags;

    auto recordCommand = [&](const std::string &kw) {
        out.command = kindFromKeyword(kw);
        out.commandName = kw;
    };

    auto recordPositional = [&](const std::string &token) {
        if (out.file.empty())
            out.file = token;
        else
            out.rest.push_back(token);
    };

    for (int i = 1; i < argc; i++)
    {
        const std::string arg = argv[i];

        if (st == State::ScanFlags || st == State::AfterCommand)
        {
            if (arg == "--help" || arg == "-h")
            {
                out.showHelp = true;
                if (st == State::AfterCommand)
                    out.helpForCommand = out.commandName;
            }
            else if (arg == "--version" || arg == "-V")
            {
                out.showVersion = true;
            }
            else if (arg == "--coverage")
            {
                out.coverage = true;
            }
            else if (arg == "--aot")
            {
                out.aot = true;
            }
            else if (isCommandKeyword(arg))
            {
                if (st == State::ScanFlags)
                {
                    recordCommand(arg);
                    st = State::AfterCommand;
                }
                else
                {
                    recordPositional(arg);
                    st = State::AfterFile;
                }
            }
            else if (arg.size() > 1 && arg[0] == '-')
            {
                // Unknown option before any file was seen -> fatal.
                return std::nullopt;
            }
            else
            {
                recordPositional(arg);
                st = State::AfterFile;
            }
        }
        else
        {
            // AfterFile: every remaining token is a program argument.
            out.rest.push_back(arg);
        }
    }

    return out;
}
} // namespace cli
