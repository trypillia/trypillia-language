#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "frontend/ast/ASTOptimizer.h"
#include "frontend/lexer/Lexer.h"
#include "frontend/parser/Parser.h"
#include "frontend/semantic/SemanticAnalyzer.h"
#include "vm/compiler/BytecodeCompiler.h"
#include "vm/core/VM.h"

namespace fs = std::filesystem;

static std::vector<std::string> expandGlobs(
    const std::vector<std::string>& patterns) {
  std::vector<std::string> files;

  auto addFile = [&](const fs::path& path) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) return;

    std::string p = path.lexically_normal().string();

    if (std::find(files.begin(), files.end(), p) == files.end())
      files.push_back(p);
  };

  for (const auto& pattern : patterns) {
    std::error_code ec;
    fs::path p(pattern);

    if (fs::is_directory(p, ec)) {
      for (const auto& entry : fs::recursive_directory_iterator(p, ec)) {
        if (!ec && entry.path().extension() == ".try") addFile(entry.path());
      }
      continue;
    }

    if (pattern.find("**") != std::string::npos) {
      size_t pos = pattern.find("**");

      fs::path base = pattern.substr(0, pos);
      if (base.empty()) base = ".";

      std::string suffix = pattern.substr(pos + 2);

      if (suffix.starts_with('/')) suffix.erase(0, 1);

      if (fs::is_directory(base, ec)) {
        for (const auto& entry : fs::recursive_directory_iterator(base, ec)) {
          if (ec || !entry.is_regular_file()) continue;

          auto rel = fs::relative(entry.path(), base, ec);
          if (ec) continue;

          std::string relPath = rel.generic_string();

          if (suffix == "*.try") {
            if (entry.path().extension() == ".try") addFile(entry.path());
          } else if (relPath.ends_with(suffix)) {
            addFile(entry.path());
          }
        }
      }

      continue;
    }

    if (pattern.find('*') == std::string::npos &&
        pattern.find('?') == std::string::npos) {
      addFile(p);
    }
  }

  std::sort(files.begin(), files.end());
  return files;
}

static bool pathMatchesFilter(const std::string& path,
                              const std::string& filter) {
  if (filter.empty()) return true;
  return path.find(filter) != std::string::npos;
}

static bool hasFocusedTests(const std::string& source) {
  return source.find("fit(") != std::string::npos ||
         source.find("fdescribe(") != std::string::npos;
}

static bool readTestResults(VM& vm, std::vector<std::string>& names,
                            std::vector<bool>& results) {
  auto namesIt = vm.globals.find("__test_names");
  auto resultsIt = vm.globals.find("__test_results");
  if (namesIt == vm.globals.end() || !namesIt->second.isList()) return false;
  if (resultsIt == vm.globals.end() || !resultsIt->second.isList())
    return false;

  auto namesList = namesIt->second.asList();
  auto resultsList = resultsIt->second.asList();

  for (size_t i = 0;
       i < namesList->elements.size() && i < resultsList->elements.size();
       i++) {
    if (namesList->elements[i].isString()) {
      names.push_back(namesList->elements[i].asString()->flatten());
      results.push_back(resultsList->elements[i].isBool()
                            ? resultsList->elements[i].asBool()
                            : false);
    }
  }
  return true;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0]
              << " [--filter <pattern>] <test-file> [test-file...]"
              << std::endl;
    return 1;
  }

  std::string filter;
  int argStart = 1;

  if (argc > 2 && strcmp(argv[1], "--filter") == 0) {
    filter = argv[2];
    argStart = 3;
  }

  std::vector<std::string> rawPatterns;
  for (int i = argStart; i < argc; i++) {
    rawPatterns.push_back(argv[i]);
  }

  std::vector<std::string> paths = expandGlobs(rawPatterns);

  if (paths.empty()) {
    std::cerr << "No test files found matching the given patterns."
              << std::endl;
    return 1;
  }

  int totalTests = 0;
  int totalPassed = 0;
  int totalFailed = 0;
  int tapCounter = 0;

  for (size_t fileIdx = 0; fileIdx < paths.size(); fileIdx++) {
    std::string path = paths[fileIdx];

    if (!pathMatchesFilter(path, filter)) {
      continue;
    }

    std::ifstream sourceFile(path);
    if (!sourceFile.is_open()) {
      std::cout << "not ok " << (++tapCounter) << " \xe2\x80\x94 " << path
                << std::endl;
      std::cout << "# Could not open file" << std::endl;
      totalFailed++;
      continue;
    }
    std::string source((std::istreambuf_iterator<char>(sourceFile)),
                       std::istreambuf_iterator<char>());

    Lexer lexer(source);
    Parser parser(lexer);
    ASTNode* ast = parser.parse();
    if (!ast) {
      std::cout << "not ok " << (++tapCounter) << " \xe2\x80\x94 " << path
                << std::endl;
      std::cout << "# Parse error" << std::endl;
      totalFailed++;
      continue;
    }

    ASTOptimizer::optimize(ast);

    SemanticAnalyzer semanticAnalyzer;
    semanticAnalyzer.currentFilename = path;
    SymbolTable* globals = semanticAnalyzer.analyze(ast);
    if (!globals) {
      std::cout << "not ok " << (++tapCounter) << " \xe2\x80\x94 " << path
                << std::endl;
      std::cout << "# Semantic analysis error" << std::endl;
      totalFailed++;
      continue;
    }

    Compiler compiler;
    compiler.currentFilename = path;
    ObjFunction* function = compiler.compile(ast, globals);
    delete globals;

    if (!function) {
      std::cout << "not ok " << (++tapCounter) << " \xe2\x80\x94 " << path
                << std::endl;
      std::cout << "# Compilation error" << std::endl;
      totalFailed++;
      continue;
    }

    std::stringstream capturedOut;
    std::stringstream capturedErr;
    auto oldBufOut = std::cout.rdbuf(capturedOut.rdbuf());
    auto oldBufErr = std::cerr.rdbuf(capturedErr.rdbuf());

    VM vm;
    if (hasFocusedTests(source)) {
      vm.globals["__test_only"] = VMValue(true);
    }
    InterpretResult result = vm.interpret(function);

    std::cout.rdbuf(oldBufOut);
    std::cerr.rdbuf(oldBufErr);

    std::vector<std::string> testNames;
    std::vector<bool> testResults;
    bool hasFrameworkResults = readTestResults(vm, testNames, testResults);

    if (hasFrameworkResults && !testNames.empty()) {
      for (size_t j = 0; j < testNames.size(); j++) {
        tapCounter++;
        if (testResults[j]) {
          std::cout << "ok " << tapCounter << " \xe2\x80\x94 " << testNames[j]
                    << std::endl;
          totalPassed++;
        } else {
          std::cout << "not ok " << tapCounter << " \xe2\x80\x94 "
                    << testNames[j] << std::endl;
          std::string errOutput = capturedErr.str();
          if (!errOutput.empty()) {
            std::cout << "# " << errOutput;
            if (errOutput.back() != '\n') std::cout << std::endl;
          }
          totalFailed++;
        }
      }
      totalTests += (int)testNames.size();
    } else {
      totalTests++;
      if (result == InterpretResult::INTERPRET_OK) {
        tapCounter++;
        totalPassed++;
        std::cout << "ok " << tapCounter << " \xe2\x80\x94 " << path
                  << std::endl;
      } else {
        tapCounter++;
        totalFailed++;
        std::cout << "not ok " << tapCounter << " \xe2\x80\x94 " << path
                  << std::endl;
        std::string errOutput = capturedErr.str();
        if (!errOutput.empty()) {
          std::cout << "# " << errOutput;
          if (errOutput.back() != '\n') std::cout << std::endl;
        }
      }
    }
  }

  if (totalTests > 0) {
    std::cout << "1.." << tapCounter << std::endl;
    std::cout << "# " << totalTests << " tests, " << totalPassed << " passed, "
              << totalFailed << " failed" << std::endl;
  }

  return totalFailed;
}
