#include "Command.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "../../frontend/ast/ASTOptimizer.h"
#include "../../frontend/lexer/Lexer.h"
#include "../../frontend/parser/Parser.h"
#include "../../frontend/semantic/SemanticAnalyzer.h"
#include "../../vm/aot/AOTModule.h"
#include "../../vm/compiler/BytecodeCompiler.h"
#include "../../vm/core/VM.h"
#include "../../vm/serializer/Serializer.h"

namespace cli
{

static std::string findRuntimeLibrary(const std::string &exePath)
{
    // Search order:
    //   1) $TRYPILLIA_RT_PATH environment variable (full path to libtrypillia_rt.a)
    //   2) <exeDir>/libtrypillia_rt.a  (sibling of the trypillia binary)
    //   3) <exeDir>/../lib/libtrypillia_rt.a
    if (const char *env = std::getenv("TRYPILLIA_RT_PATH"))
    {
        if (std::filesystem::exists(env))
            return env;
    }
    namespace fs = std::filesystem;
    fs::path exeDir = fs::path(exePath).parent_path();
    fs::path p1 = exeDir / "libtrypillia_rt.a";
    if (fs::exists(p1))
        return p1.string();
    fs::path p2 = exeDir / ".." / "lib" / "libtrypillia_rt.a";
    if (fs::exists(p2))
        return fs::weakly_canonical(p2).string();
    return "";
}

int buildCommand(const ParsedArgs &args, const std::string &programName, const std::string &exePath)
{
    (void)programName;

    const std::string &inputFile = args.file;
    if (inputFile.empty())
    {
        std::cerr << "Usage: " << programName << " build [--aot] <file.try> [output]" << std::endl;
        std::cerr << "Run `" << programName << " build --help` for more information." << std::endl;
        return 1;
    }

    bool aotMode = args.aot;

    std::vector<std::string> positional;
    for (const auto &r : args.rest)
    {
        positional.push_back(r);
    }
    std::string outputFile = positional.empty() ? (aotMode ? "app" : "app") : positional[0];

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

    if (!function)
    {
        std::cerr << "Error: failed to compile " << inputFile << std::endl;
        return 1;
    }

    if (aotMode)
    {
        std::string rtPath = findRuntimeLibrary(exePath);
        if (rtPath.empty())
        {
            std::cerr << "Error: libtrypillia_rt.a not found. Set $TRYPILLIA_RT_PATH or place it next to "
                         "the trypillia binary."
                      << std::endl;
            return 1;
        }
        trypillia::aot::AOTModule::Options opt;
        opt.rtLibPath = rtPath;
        std::string err;
        if (!trypillia::aot::AOTModule::compileToExecutable(function, outputFile, opt, err))
        {
            std::cerr << "AOT compilation failed: " << err << std::endl;
            std::cerr << "Hint: --aot (Phase 1) supports purely numeric programs. For full language "
                         "support, omit --aot to get the legacy self-contained bytecode build."
                      << std::endl;
            return 1;
        }
        std::cout << "Successfully built AOT executable: " << outputFile << std::endl;
    }
    else
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
