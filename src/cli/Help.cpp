#include "cli/Help.h"

#include <iostream>
#include <string>

namespace cli
{
static const char *GENERAL_HELP =
    R"(trypillia {VERSION} - Trypillia Programming Language

{tryp} {VERSION}
A modern, fast, and elegantly designed programming language compiler and
virtual machine.

USAGE:
    {tryp} [OPTIONS] <COMMAND> [ARGS]
    {tryp} [OPTIONS] <FILE> [ARGS...]

DESCRIPTION:
    If no COMMAND is given, the FILE is interpreted as a Trypillia source
    file (.try) and executed. Any remaining ARGS are passed to the program
    as command-line arguments (available via the `os` module).

COMMANDS:
    build <FILE> [OUTPUT]    Compile FILE into a standalone executable.
                             Default OUTPUT is `app`.
    fmt <FILE|DIR>...        Format Trypillia source files in place.
                             Directories are scanned recursively for .try files.
    run <FILE> [ARGS...]     Execute a .try source file. (default)

OPTIONS:
    -h, --help               Print this help message and exit.
    -V, --version            Print version information and exit.
        --coverage           Collect code coverage and write LCOV output to
                             coverage.info after execution. Implies running the file.

EXAMPLES:
    Run a source file:
        {tryp} fib.try
        {tryp} fib.try 10 30

    Build a standalone executable:
        {tryp} build fib.try fib
        {tryp} build fib.try ./bin/myapp

    Format source files:
        {tryp} fmt src/
        {tryp} fmt hello.try main.try

    Run with code coverage:
        {tryp} --coverage fib.try

For help on a specific command, run:
    {tryp} <command> --help
)";

static const char *BUILD_HELP =
    R"(trypillia-build - Compile a Trypillia source file into a standalone executable

USAGE:
    {tryp} build <FILE> [OUTPUT]

ARGUMENTS:
    <FILE>                   A .try source file to compile.
    [OUTPUT]                 Path of the standalone executable to produce.
                             Default: app

DESCRIPTION:
    Parses, type-checks, and compiles FILE into a native executable that
    embeds the compiled bytecode and the Trypillia VM. The resulting binary
    can be distributed and run on a target machine without the Trypillia
    compiler or any `.try` source.

EXAMPLES:
    Build `app` from a source file:
        {tryp} build fib.try

    Build `app` from a source file (explicit name):
        {tryp} build fib.try fib

    Build into a custom path:
        {tryp} build fib.try ./bin/myapp
)";

static const char *FMT_HELP =
    R"(trypillia-fmt - Format Trypillia source files in place

USAGE:
    {tryp} fmt <FILE|DIR>...

ARGUMENTS:
    <FILE|DIR>...            One or more .try files or directories. Directory
                             trees are scanned recursively; every .try file
                             found is rewritten in canonical form.

DESCRIPTION:
    Runs the Trypillia formatter over each file. Comments, spacing, and
    brace style are normalized. Files that fail to parse are skipped with a
    diagnostic on stderr and left untouched.

EXAMPLES:
    Format a single file:
        {tryp} fmt hello.try

    Format every .try file under a directory tree:
        {tryp} fmt src/
        {tryp} fmt src/ lib/
)";

static const char *RUN_HELP =
    R"(trypillia-run - Execute a Trypillia source file

USAGE:
    {tryp} [OPTIONS] <FILE> [ARGS...]

ARGUMENTS:
    <FILE>                   A .try source file to execute.
    [ARGS...]                Optional values passed to the program, accessible
                             via the `os` standard library module.

EXAMPLES:
    Run a source file:
        {tryp} fib.try

    Run with arguments:
        {tryp} fib.try 10 30
)";

static const char *USAGE_ERROR =
    R"(usage: {tryp} [OPTIONS] <COMMAND> [ARGS]
    {tryp} [OPTIONS] <FILE> [ARGS...]

For more details, run:
    {tryp} --help
)";

static std::string basename(const std::string &programName)
{
    if (programName.size() <= 1)
        return programName;
    size_t pos = programName.find_last_of('/');
    if (pos == std::string::npos)
        return programName;
    return programName.substr(pos + 1);
}

static std::string substitute(const std::string &text, const std::string &programName)
{
    std::string name = basename(programName);
    std::string result = text;
    size_t pos = 0;
    while ((pos = result.find("{tryp}", pos)) != std::string::npos)
        result.replace(pos, 6, name);
    pos = 0;
    while ((pos = result.find("{VERSION}", pos)) != std::string::npos)
        result.replace(pos, 9, TRYP_VERSION);
    return result;
}

void printGeneralHelp(const std::string &programName, std::ostream &out)
{
    out << substitute(GENERAL_HELP, programName);
}

void printVersion(const std::string &programName, std::ostream &out)
{
    out << basename(programName) << " " << TRYP_VERSION << "\n";
    out << "Trypillia Programming Language\n";
}

bool printCommandHelp(const std::string &programName, const std::string &command, std::ostream &out)
{
    const char *text = nullptr;
    if (command == "build")
        text = BUILD_HELP;
    else if (command == "fmt")
        text = FMT_HELP;
    else if (command == "run")
        text = RUN_HELP;
    else
        return false;
    out << substitute(text, programName);
    return true;
}

bool printUsageError(const std::string &programName, const std::string &command, std::ostream &out)
{
    (void)command;
    out << substitute(USAGE_ERROR, programName);
    return true;
}
} // namespace cli
