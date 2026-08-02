#pragma once

#include "../Options.h"

#include <string>

namespace cli
{
// Executes a .try source file. Returns the process exit code.
int runCommand(const ParsedArgs &args, const std::string &programName);

// Compiles a .try source file into a standalone executable.
int buildCommand(const ParsedArgs &args, const std::string &programName);

// Formats .try source files in place.
int fmtCommand(const ParsedArgs &args, const std::string &programName);
} // namespace cli
