#include "ErrorHandling.h"

#include <iostream>

std::vector<std::string> ErrorHandling::errors;

void ErrorHandling::reportError(const std::string& message) {
  errors.push_back(message);
  std::cerr << "Error: " << message << std::endl;
}

const std::vector<std::string>& ErrorHandling::getErrors() { return errors; }

void ErrorHandling::clearErrors() { errors.clear(); }
