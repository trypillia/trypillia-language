#ifndef TRYPILLIA_LCOV_REPORTER_H
#define TRYPILLIA_LCOV_REPORTER_H

#include "../core/VM.h"
#include <string>

class LcovReporter {
public:
  static void generateReport(VM *vm, const std::string &outputPath);
};

#endif // TRYPILLIA_LCOV_REPORTER_H
