#ifndef TRYPILLIA_LCOV_REPORTER_H
#define TRYPILLIA_LCOV_REPORTER_H

#include <string>

#include "../core/VM.h"

#include <map>

class LcovReporter
{
  public:
    static void collectCoverage(VM *vm, std::map<std::string, std::map<int, uint32_t>> &fileCoverage);
    static void writeReport(const std::map<std::string, std::map<int, uint32_t>> &fileCoverage,
                            const std::string &outputPath);
    static void generateReport(VM *vm, const std::string &outputPath);
};

#endif // TRYPILLIA_LCOV_REPORTER_H
