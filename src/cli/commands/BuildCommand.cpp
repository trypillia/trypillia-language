#include "Command.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "../../frontend/ast/ASTOptimizer.h"
#include "../../frontend/lexer/Lexer.h"
#include "../../frontend/parser/Parser.h"
#include "../../frontend/semantic/SemanticAnalyzer.h"
#include "../../vm/compiler/BytecodeCompiler.h"
#include "../../vm/core/VM.h"
#include "../../vm/serializer/Serializer.h"

namespace cli
{
int buildCommand(const ParsedArgs &args, const std::string &programName, const std::string &exePath)
{
    (void)programName;

    const std::string &inputFile = args.file;
    if (inputFile.empty())
    {
        std::cerr << "Usage: " << programName << " build <file.try> [output]" << std::endl;
        std::cerr << "Run `" << programName << " build --help` for more information." << std::endl;
        return 1;
    }

    std::string outputFile = args.rest.empty() ? "app" : args.rest[0];

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
        if (Serializer::buildStandalone(function, exePath, outputFile))
        {
            std::cout << "Successfully built standalone executable: " << outputFile << std::endl;
        }
        else
        {
            std::cerr << "Error building standalone executable: " << outputFile << std::endl;
            return 1;
        }
    }

    return 0;
}
} // namespace cli
