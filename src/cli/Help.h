#pragma once

#include "Version.h"
#include <iostream>
#include <ostream>
#include <string>

namespace cli
{
void printGeneralHelp(const std::string &programName, std::ostream &out = std::cout);

bool printCommandHelp(const std::string &programName, const std::string &command, std::ostream &out = std::cout);

void printVersion(const std::string &programName, std::ostream &out = std::cout);

bool printUsageError(const std::string &programName, const std::string &command, std::ostream &out = std::cerr);
} // namespace cli
