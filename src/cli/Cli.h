#pragma once

namespace cli
{
// Entry point for the compiler/VM front-end. Parses argv, dispatches to the
// appropriate subcommand, and returns the process exit code.
int run(int argc, char **argv);
} // namespace cli
