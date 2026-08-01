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
#include "vm/coverage/LcovReporter.h"

namespace fs = std::filesystem;

static std::vector<std::string> expandGlobs(const std::vector<std::string> &patterns)
{
    std::vector<std::string> files;

    auto addFile = [&](const fs::path &path) {
        std::error_code ec;
        if (!fs::is_regular_file(path, ec))
            return;

        std::string p = path.lexically_normal().string();

        if (std::find(files.begin(), files.end(), p) == files.end())
            files.push_back(p);
    };

    for (const auto &pattern : patterns)
    {
        std::error_code ec;
        fs::path p(pattern);

        if (fs::is_directory(p, ec))
        {
            for (const auto &entry : fs::recursive_directory_iterator(p, ec))
            {
                if (!ec && entry.path().extension() == ".try")
                    addFile(entry.path());
            }
            continue;
        }

        if (pattern.find("**") != std::string::npos)
        {
            size_t pos = pattern.find("**");

            fs::path base = pattern.substr(0, pos);
            if (base.empty())
                base = ".";

            std::string suffix = pattern.substr(pos + 2);

            if (suffix.starts_with('/'))
                suffix.erase(0, 1);

            if (fs::is_directory(base, ec))
            {
                for (const auto &entry : fs::recursive_directory_iterator(base, ec))
                {
                    if (ec || !entry.is_regular_file())
                        continue;

                    auto rel = fs::relative(entry.path(), base, ec);
                    if (ec)
                        continue;

                    std::string relPath = rel.generic_string();

                    if (suffix == "*.try")
                    {
                        if (entry.path().extension() == ".try")
                            addFile(entry.path());
                    }
                    else if (relPath.ends_with(suffix))
                    {
                        addFile(entry.path());
                    }
                }
            }

            continue;
        }

        if (pattern.find('*') == std::string::npos && pattern.find('?') == std::string::npos)
        {
            addFile(p);
        }
    }

    std::sort(files.begin(), files.end());
    return files;
}

static bool pathMatchesFilter(const std::string &path, const std::string &filter)
{
    if (filter.empty())
        return true;
    return path.find(filter) != std::string::npos;
}

static bool hasFocusedTests(const std::string &source)
{
    return source.find("fit(") != std::string::npos || source.find("fdescribe(") != std::string::npos;
}

static bool readTestResults(VM &vm, std::vector<std::string> &names, std::vector<bool> &results,
                            std::vector<std::string> &errors)
{
    auto namesIt = vm.globals.find("__test_names");
    auto resultsIt = vm.globals.find("__test_results");
    if (namesIt == vm.globals.end() || !namesIt->second.isList())
        return false;
    if (resultsIt == vm.globals.end() || !resultsIt->second.isList())
        return false;

    auto errorsIt = vm.globals.find("__test_errors");
    bool hasErrors = (errorsIt != vm.globals.end() && errorsIt->second.isList());
    auto namesList = namesIt->second.asList();
    auto resultsList = resultsIt->second.asList();
    ObjList *errorsList = hasErrors ? errorsIt->second.asList() : nullptr;

    for (size_t i = 0; i < namesList->elements.size() && i < resultsList->elements.size(); i++)
    {
        if (namesList->elements[i].isString())
        {
            names.push_back(namesList->elements[i].asString()->flatten());
            results.push_back(resultsList->elements[i].isBool() ? resultsList->elements[i].asBool() : false);
            if (errorsList && i < errorsList->elements.size() && errorsList->elements[i].isString())
            {
                errors.push_back(errorsList->elements[i].asString()->flatten());
            }
            else
            {
                errors.push_back("");
            }
        }
    }
    return true;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " [--filter <pattern>] <test-file> [test-file...]" << std::endl;
        return 1;
    }

    std::string filter;
    std::string coverageDir;
    bool enableCoverage = false;
    int argStart = 1;

    while (argStart < argc)
    {
        if (strcmp(argv[argStart], "--filter") == 0 && argStart + 1 < argc)
        {
            filter = argv[argStart + 1];
            argStart += 2;
        }
        else if (strcmp(argv[argStart], "--coverage") == 0)
        {
            enableCoverage = true;
            argStart += 1;
        }
        else if (strcmp(argv[argStart], "--coverage-dir") == 0 && argStart + 1 < argc)
        {
            coverageDir = argv[argStart + 1];
            enableCoverage = true;
            argStart += 2;
        }
        else
        {
            break;
        }
    }

    std::vector<std::string> rawPatterns;
    for (int i = argStart; i < argc; i++)
    {
        rawPatterns.push_back(argv[i]);
    }

    std::vector<std::string> paths = expandGlobs(rawPatterns);

    if (paths.empty())
    {
        std::cerr << "No test files found matching the given patterns." << std::endl;
        return 1;
    }

    int totalTests = 0;
    int totalPassed = 0;
    int totalFailed = 0;
    int tapCounter = 0;
    std::map<std::string, std::map<int, uint32_t>> globalCoverage;

    for (size_t fileIdx = 0; fileIdx < paths.size(); fileIdx++)
    {
        std::string path = paths[fileIdx];

        if (!pathMatchesFilter(path, filter))
        {
            continue;
        }

        std::ifstream sourceFile(path);
        if (!sourceFile.is_open())
        {
            std::cout << "not ok " << (++tapCounter) << " \xe2\x80\x94 " << path << std::endl;
            std::cout << "# Could not open file" << std::endl;
            totalFailed++;
            continue;
        }
        std::string source((std::istreambuf_iterator<char>(sourceFile)), std::istreambuf_iterator<char>());

        Lexer lexer(source);
        Parser parser(lexer);
        ASTNode *ast = parser.parse();
        if (!ast)
        {
            std::cout << "not ok " << (++tapCounter) << " \xe2\x80\x94 " << path << std::endl;
            std::cout << "# Parse error" << std::endl;
            totalFailed++;
            continue;
        }

        ASTOptimizer::optimize(ast);

        SemanticAnalyzer semanticAnalyzer;
        semanticAnalyzer.currentFilename = path;
        SymbolTable *globals = semanticAnalyzer.analyze(ast);
        if (!globals)
        {
            std::cout << "not ok " << (++tapCounter) << " \xe2\x80\x94 " << path << std::endl;
            std::cout << "# Semantic analysis error" << std::endl;
            totalFailed++;
            continue;
        }

        VM vm;
        if (enableCoverage)
        {
            vm.collectCoverage = true;
        }
        if (hasFocusedTests(source))
        {
            vm.globals["__test_only"] = VMValue(true);
        }

        Compiler compiler;
        compiler.currentFilename = path;
        ObjFunction *function = compiler.compile(ast, globals);
        delete globals;

        if (!function)
        {
            std::cout << "not ok " << (++tapCounter) << " \xe2\x80\x94 " << path << std::endl;
            std::cout << "# Compilation error" << std::endl;
            totalFailed++;
            continue;
        }

        std::stringstream capturedOut;
        std::stringstream capturedErr;
        auto oldBufOut = std::cout.rdbuf(capturedOut.rdbuf());
        auto oldBufErr = std::cerr.rdbuf(capturedErr.rdbuf());
        InterpretResult result = vm.interpret(function);
        if (enableCoverage)
        {
            LcovReporter::collectCoverage(&vm, globalCoverage);
        }

        std::cout.rdbuf(oldBufOut);
        std::cerr.rdbuf(oldBufErr);

        std::vector<std::string> testNames;
        std::vector<bool> testResults;
        std::vector<std::string> testErrors;
        bool hasFrameworkResults = readTestResults(vm, testNames, testResults, testErrors);

        if (hasFrameworkResults && !testNames.empty())
        {
            for (size_t j = 0; j < testNames.size(); j++)
            {
                tapCounter++;
                if (testResults[j])
                {
                    std::cout << "ok " << tapCounter << " \xe2\x80\x94 " << testNames[j] << std::endl;
                    totalPassed++;
                }
                else
                {
                    std::cout << "not ok " << tapCounter << " \xe2\x80\x94 " << testNames[j] << std::endl;
                    if (j < testErrors.size() && !testErrors[j].empty())
                    {
                        std::cout << "# " << testErrors[j] << std::endl;
                    }
                    totalFailed++;
                }
            }
            totalTests += (int)testNames.size();

            if (result != InterpretResult::INTERPRET_OK)
            {
                tapCounter++;
                totalFailed++;
                totalTests++;
                std::cout << "not ok " << tapCounter << " \xe2\x80\x94 " << path << " (runtime error after some tests)"
                          << std::endl;
                auto errIt = vm.globals.find("__test_current_error");
                if (errIt != vm.globals.end() && errIt->second.isString())
                {
                    std::cout << "# " << errIt->second.asString()->flatten() << std::endl;
                }
            }

            std::string remainingErr = capturedErr.str();
            if (!remainingErr.empty())
            {
                std::cout << remainingErr;
                if (remainingErr.back() != '\n')
                    std::cout << std::endl;
            }
        }
        else
        {
            totalTests++;
            if (result == InterpretResult::INTERPRET_OK)
            {
                tapCounter++;
                totalPassed++;
                std::cout << "ok " << tapCounter << " \xe2\x80\x94 " << path << std::endl;
            }
            else
            {
                tapCounter++;
                totalFailed++;
                std::cout << "not ok " << tapCounter << " \xe2\x80\x94 " << path << std::endl;
                auto errIt = vm.globals.find("__test_current_error");
                if (errIt != vm.globals.end() && errIt->second.isString())
                {
                    std::cout << "# " << errIt->second.asString()->flatten() << std::endl;
                }
                std::string errOutput = capturedErr.str();
                if (!errOutput.empty())
                {
                    std::cout << "# " << errOutput;
                    if (errOutput.back() != '\n')
                        std::cout << std::endl;
                }
            }
        }
    }

    if (totalTests > 0)
    {
        std::cout << "1.." << tapCounter << std::endl;
        std::cout << "# " << totalTests << " tests, " << totalPassed << " passed, " << totalFailed << " failed"
                  << std::endl;
    }

    if (enableCoverage && !coverageDir.empty())
    {
        for (const auto &entry : fs::recursive_directory_iterator(coverageDir))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".try")
            {
                std::string pathStr = entry.path().string();

                // globalCoverage stores paths as resolved during test execution,
                // but since tests load relative files, the paths in globalCoverage
                // are usually the ones returned by the OS/filesystem.
                // If it's not present, we add it.
                if (globalCoverage.find(pathStr) == globalCoverage.end() &&
                    globalCoverage.find(fs::absolute(pathStr).string()) == globalCoverage.end())
                {
                    std::ifstream sourceFile(pathStr);
                    if (sourceFile.is_open())
                    {
                        std::string source((std::istreambuf_iterator<char>(sourceFile)),
                                           std::istreambuf_iterator<char>());
                        Lexer lexer(source);
                        Parser parser(lexer);
                        ASTNode *ast = parser.parse();
                        if (ast)
                        {
                            ASTOptimizer::optimize(ast);
                            SemanticAnalyzer semanticAnalyzer;
                            semanticAnalyzer.currentFilename = pathStr;
                            SymbolTable *globals = semanticAnalyzer.analyze(ast);
                            if (globals)
                            {
                                VM vm;
                                vm.collectCoverage = true;
                                Compiler compiler;
                                compiler.currentFilename = pathStr;
                                ObjFunction *function = compiler.compile(ast, globals);
                                delete globals;
                                if (function)
                                {
                                    LcovReporter::collectCoverage(&vm, globalCoverage);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (enableCoverage && !globalCoverage.empty())
    {
        LcovReporter::writeReport(globalCoverage, "coverage.info");
    }

    return totalFailed;
}
