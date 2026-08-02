#pragma once

#include "Version.h"

#include <optional>
#include <string>
#include <vector>

namespace cli
{
enum class CommandKind
{
    Run,
    Build,
    Fmt
};

struct ParsedArgs
{
    bool showHelp = false;
    bool showVersion = false;
    bool coverage = false;

    std::optional<std::string> helpForCommand;

    CommandKind command = CommandKind::Run;
    std::string commandName;       // explicit keyword ("build"/"fmt"/"run"); empty for implicit run
    std::string file;              // primary file/target;
    std::vector<std::string> rest; // script args (run) | [output] (build) | extra targets (fmt)
};

// Parses argv into a ParsedArgs. Returns std::nullopt on a fatal argument
// error (e.g. an unknown option); callers print a usage message.
std::optional<ParsedArgs> parseArgs(int argc, char **argv);

} // namespace cli
