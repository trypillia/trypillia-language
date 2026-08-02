#include "Command.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "frontend/ast/ASTOptimizer.h"
#include "frontend/lexer/Lexer.h"
#include "frontend/parser/Parser.h"
#include "frontend/semantic/SemanticAnalyzer.h"
#include "native/os/OS.h"
#include "vm/compiler/BytecodeCompiler.h"
#include "vm/core/VM.h"
#include "vm/coverage/LcovReporter.h"

namespace cli
{
int runCommand(const ParsedArgs &args, const std::string &programName, const std::string &exePath)
{
    (void)programName;
    (void)exePath;

    const std::string &inputFile = args.file;
    if (std::filesystem::is_directory(inputFile))
    {
        std::cerr << "Error: " << inputFile << " is a directory. Please provide a source file." << std::endl;
        return 1;
    }

    std::ifstream sourceFile(inputFile);
    if (!sourceFile.is_open())
    {
        std::cerr << "Error: Could not open source file: " << inputFile << std::endl;
        return 1;
    }
    std::string sourceCode((std::istreambuf_iterator<char>(sourceFile)), std::istreambuf_iterator<char>());

    for (const auto &a : args.rest)
    {
        StdLib::OSModule::commandLineArgs.push_back(a);
    }

    VM vm;
    Lexer lexer(sourceCode);
    Parser parser(lexer);
    ASTNode *ast = parser.parse();

    ASTOptimizer::optimize(ast);

    SemanticAnalyzer semanticAnalyzer;
    semanticAnalyzer.currentFilename = inputFile;
    SymbolTable *globals = semanticAnalyzer.analyze(ast);

    Compiler compiler;
    compiler.currentFilename = inputFile;
    ObjFunction *function = compiler.compile(ast, globals);

    if (globals)
    {
        delete globals;
    }

    if (function)
    {
        if (args.coverage)
        {
            vm.collectCoverage = true;
        }
        InterpretResult result = vm.interpret(function);

        if (args.coverage)
        {
            LcovReporter::generateReport(&vm, "coverage.info");
        }

        if (result == InterpretResult::INTERPRET_RUNTIME_ERROR)
        {
            return 1;
        }
    }

    return 0;
}
} // namespace cli
