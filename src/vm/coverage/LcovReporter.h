#ifndef TRYPILLIA_LCOV_REPORTER_H
#define TRYPILLIA_LCOV_REPORTER_H

#include <string>

#include "../core/VM.h"

class LcovReporter {
 public:
  static void generateReport(VM* vm, const std::string& outputPath);
};

#endif  // TRYPILLIA_LCOV_REPORTER_H
