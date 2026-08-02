#include "Cli.h"

#include <iostream>
#include <string>

#include "Command.h"
#include "Help.h"
#include "Options.h"
#include "Version.h"
#include "native/os/OS.h"
#include "vm/core/VM.h"
#include "vm/serializer/Serializer.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

namespace cli
{
static std::string getExecutablePath(const char *argv0)
{
#ifdef _WIN32
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    return std::string(buffer);
#else
#ifdef __APPLE__
    char apple_buffer[PATH_MAX];
    uint32_t size = sizeof(apple_buffer);
    if (_NSGetExecutablePath(apple_buffer, &size) == 0)
    {
        return std::string(apple_buffer);
    }
#endif
    char buffer[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", buffer, PATH_MAX);
    if (count != -1)
    {
        return std::string(buffer, count);
    }
    return std::string(argv0);
#endif
}

int run(int argc, char **argv)
{
    const std::string exePath = getExecutablePath(argv[0]);

    // 1. Check for embedded bytecode first (standalone executable).
    ObjFunction *function = Serializer::loadEmbeddedBytecode(exePath);
    if (function)
    {
        for (int i = 1; i < argc; i++)
        {
            StdLib::OSModule::commandLineArgs.push_back(argv[i]);
        }
        VM vm;
        vm.interpret(function);
        return 0;
    }

    auto args = parseArgs(argc, argv);
    if (!args)
    {
        printUsageError(argv[0], "", std::cerr);
        return 2;
    }

    if (args->showVersion)
    {
        printVersion(argv[0]);
        return 0;
    }

    if (args->showHelp)
    {
        if (args->helpForCommand && printCommandHelp(argv[0], *args->helpForCommand))
        {
            return 0;
        }
        printGeneralHelp(argv[0]);
        return 0;
    }

    switch (args->command)
    {
    case CommandKind::Build:
        return buildCommand(*args, argv[0], exePath);
    case CommandKind::Fmt:
        return fmtCommand(*args, argv[0], exePath);
    case CommandKind::Run:
    default:
        return runCommand(*args, argv[0], exePath);
    }
}
} // namespace cli
