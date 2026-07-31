#include "frontend/ast/ASTOptimizer.h"
#include "frontend/lexer/Lexer.h"
#include "native/os/OS.h"
#include "frontend/parser/Parser.h"
#include "frontend/semantic/SemanticAnalyzer.h"
#include "vm/compiler/BytecodeCompiler.h"
#include "vm/core/VM.h"
#include "vm/serializer/Serializer.h"
#include <fstream>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

std::string getExecutablePath(const char *argv0) {
#ifdef _WIN32
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    return std::string(buffer);
#else
#ifdef __APPLE__
    char apple_buffer[PATH_MAX];
    uint32_t size = sizeof(apple_buffer);
    if (_NSGetExecutablePath(apple_buffer, &size) == 0) {
        return std::string(apple_buffer);
    }
#endif
    char buffer[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", buffer, PATH_MAX);
    if (count != -1) {
        return std::string(buffer, count);
    }
    return std::string(argv0);
#endif
}

int main(int argc, char **argv) {
    std::string command;
    if (argc >= 2)
        command = argv[1];

    std::string exePath = getExecutablePath(argv[0]);
    ObjFunction *function = nullptr;

    // 1. Check for embedded bytecode first (standalone executable)
    function = Serializer::loadEmbeddedBytecode(exePath);
    if (function) {
        // Run embedded bytecode
        for (int i = 1; i < argc; i++) {
            StdLib::OSModule::commandLineArgs.push_back(argv[i]);
        }
        VM vm;
        vm.interpret(function);
        return 0;
    }

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " [build] <file> [output]" << std::endl;
        return 1;
    }

    bool buildStandalone = false;
    std::string inputFile;
    std::string outputFile;

    if (command == "build") {
        if (argc < 3) {
            std::cerr << "Usage: " << argv[0] << " build <file.try> [output]" << std::endl;
            return 1;
        }
        buildStandalone = true;
        inputFile = argv[2];
        if (argc >= 4) {
            outputFile = argv[3];
        } else {
            outputFile = "app";
        }
    } else {
        inputFile = argv[1];
        for (int i = 2; i < argc; i++) {
            StdLib::OSModule::commandLineArgs.push_back(argv[i]);
        }
    }

    std::ifstream sourceFile(inputFile);
    if (!sourceFile.is_open()) {
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

    if (function) {
        if (buildStandalone) {
            if (Serializer::buildStandalone(function, exePath, outputFile)) {
                std::cout << "Successfully built standalone executable: " << outputFile << std::endl;
            } else {
                std::cerr << "Error building standalone executable: " << outputFile << std::endl;
                return 1;
            }
        } else {
            VM vm;
            InterpretResult result = vm.interpret(function);
            if (result == InterpretResult::INTERPRET_RUNTIME_ERROR) {
                return 1;
            }
        }
    }

    return 0;
}
