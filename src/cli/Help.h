#pragma once

#include <iostream>
#include <ostream>
#include <string>

namespace cli
{
const char *const TRYP_VERSION = "1.0.0";
const char *const PROG_NAME = "trypillia";

void printGeneralHelp(const std::string &programName, std::ostream &out = std::cout);

bool printCommandHelp(const std::string &programName, const std::string &command, std::ostream &out = std::cout);

void printVersion(const std::string &programName, std::ostream &out = std::cout);

bool printUsageError(const std::string &programName, const std::string &command, std::ostream &out = std::cerr);
} // namespace cli
