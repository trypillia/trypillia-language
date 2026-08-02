#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "cli/Help.h"
#include "frontend/ast/ASTOptimizer.h"
#include "frontend/formatter/FormatterVisitor.h"
#include "frontend/lexer/Lexer.h"
#include "frontend/parser/Parser.h"
#include "frontend/semantic/SemanticAnalyzer.h"
#include "native/os/OS.h"
#include "vm/compiler/BytecodeCompiler.h"
#include "vm/core/VM.h"
#include "vm/coverage/LcovReporter.h"
#include "vm/serializer/Serializer.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

std::string getExecutablePath(const char *argv0)
{
#ifdef _WIN32
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    return std::string(buffer);
#else
#ifdef __APPLE__
    char apple_buffer[PATH_MAX];
    uint32_t size = sizeof(apple_buffer);
    if (_NSGetExecutablePath(apple_buffer, &size) == 0)
    {
        return std::string(apple_buffer);
    }
#endif
    char buffer[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", buffer, PATH_MAX);
    if (count != -1)
    {
        return std::string(buffer, count);
    }
    return std::string(argv0);
#endif
}

void formatFileOrDirectory(const std::string &path)
{
    if (std::filesystem::is_directory(path))
    {
        for (const auto &entry : std::filesystem::recursive_directory_iterator(path))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".try")
            {
                formatFileOrDirectory(entry.path().string());
            }
        }
        return;
    }

    std::ifstream sourceFile(path);
    if (!sourceFile.is_open())
    {
        std::cerr << "Error: Could not open source file: " << path << std::endl;
        return;
    }
    std::string sourceCode((std::istreambuf_iterator<char>(sourceFile)), std::istreambuf_iterator<char>());
    sourceFile.close();

    Lexer lexer(sourceCode, true);
    Parser parser(lexer);
    ASTNode *ast = nullptr;
    try
    {
        ast = parser.parse();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Syntax error in " << path << ": " << e.what() << ". Skipping formatting." << std::endl;
        return;
    }

    FormatterVisitor formatter;
    if (parser.hasError)
    {
        std::cerr << "Parser encountered errors in " << path << ". Skipping formatting." << std::endl;
        if (auto program = dynamic_cast<ProgramNode *>(ast))
            delete program;
        return;
    }
    ast->accept(&formatter);
    std::string formattedCode = formatter.getOutput();

    std::ofstream outFile(path, std::ios::trunc);
    if (outFile.is_open())
    {
        outFile << formattedCode;
        outFile.close();
        std::cout << "Formatted: " << path << std::endl;
    }
    else
    {
        std::cerr << "Error: Could not save formatted file: " << path << std::endl;
    }

    if (auto program = dynamic_cast<ProgramNode *>(ast))
    {
        delete program;
    }
}

int main(int argc, char **argv)
{
    VM vm;
    std::string command;
    if (argc >= 2)
        command = argv[1];

    std::string exePath = getExecutablePath(argv[0]);
    ObjFunction *function = nullptr;

    // 1. Check for embedded bytecode first (standalone executable)
    function = Serializer::loadEmbeddedBytecode(exePath);
    if (function)
    {
        // Run embedded bytecode
        for (int i = 1; i < argc; i++)
        {
            StdLib::OSModule::commandLineArgs.push_back(argv[i]);
        }
        vm.interpret(function);
        return 0;
    }

    bool buildStandalone = false;
    bool enableCoverage = false;
    std::string inputFile;
    std::string outputFile;

    // Short-circuit --help / --version before any file I/O or argument
    // parsing so the user gets instant, consistent feedback.
    {
        bool wantsHelp = false;
        bool wantsVersion = false;
        std::string commandInArgs;
        for (int i = 1; i < argc; i++)
        {
            std::string arg = argv[i];
            if (arg == "--help" || arg == "-h")
                wantsHelp = true;
            else if (arg == "--version" || arg == "-V")
                wantsVersion = true;
            else if (arg == "build" || arg == "fmt" || arg == "run")
                commandInArgs = arg;
        }
        if (wantsVersion)
        {
            cli::printVersion(argv[0]);
            return 0;
        }
        if (wantsHelp)
        {
            if (!commandInArgs.empty() && cli::printCommandHelp(argv[0], commandInArgs))
                return 0;
            cli::printGeneralHelp(argv[0]);
            return 0;
        }
    }

    int argIdx = 1;
    if (argIdx < argc && std::string(argv[argIdx]) == "--coverage")
    {
        enableCoverage = true;
        argIdx++;
        if (argIdx < argc)
        {
            command = argv[argIdx];
        }
    }

    if (argIdx >= argc)
    {
        std::cerr << "Usage: " << argv[0] << " [--coverage] [build] <file> [output]" << std::endl;
        return 1;
    }

    if (command == "build")
    {
        argIdx++;
        if (argIdx >= argc)
        {
            std::cerr << "Usage: " << argv[0] << " build <file.try> [output]" << std::endl;
            return 1;
        }
        buildStandalone = true;
        inputFile = argv[argIdx++];
        if (argIdx < argc)
        {
            outputFile = argv[argIdx];
        }
        else
        {
            outputFile = "app";
        }
    }
    else if (command == "fmt")
    {
        argIdx++;
        if (argIdx >= argc)
        {
            std::cerr << "Usage: " << argv[0] << " fmt <file_or_directory>..." << std::endl;
            return 1;
        }
        for (int i = argIdx; i < argc; i++)
        {
            formatFileOrDirectory(argv[i]);
        }
        return 0;
    }
    else
    {
        inputFile = argv[argIdx++];
        for (int i = argIdx; i < argc; i++)
        {
            StdLib::OSModule::commandLineArgs.push_back(argv[i]);
        }
    }

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
    function = compiler.compile(ast, globals);

    if (globals)
        delete globals;

    if (function)
    {
        if (buildStandalone)
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
        else
        {
            if (enableCoverage)
            {
                vm.collectCoverage = true;
            }
            InterpretResult result = vm.interpret(function);

            if (enableCoverage)
            {
                LcovReporter::generateReport(&vm, "coverage.info");
            }

            if (result == InterpretResult::INTERPRET_RUNTIME_ERROR)
            {
                return 1;
            }
        }
    }

    return 0;
}
