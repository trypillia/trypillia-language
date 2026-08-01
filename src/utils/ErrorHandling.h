#ifndef ERROR_HANDLING_H
#define ERROR_HANDLING_H

#include <string>
#include <vector>

class ErrorHandling {
 public:
  static void reportError(const std::string& message);
  static const std::vector<std::string>& getErrors();
  static void clearErrors();

 private:
  static std::vector<std::string> errors;
};

#endif  // ERROR_HANDLING_H
