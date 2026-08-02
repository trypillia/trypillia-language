#include "Command.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "frontend/formatter/FormatterVisitor.h"
#include "frontend/lexer/Lexer.h"
#include "frontend/parser/Parser.h"

namespace cli
{
namespace
{
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
        {
            delete program;
        }
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
} // namespace

int fmtCommand(const ParsedArgs &args, const std::string &programName, const std::string &exePath)
{
    (void)programName;
    (void)exePath;

    std::vector<std::string> targets;
    if (!args.file.empty())
    {
        targets.push_back(args.file);
    }
    targets.insert(targets.end(), args.rest.begin(), args.rest.end());

    if (targets.empty())
    {
        std::cerr << "Usage: " << programName << " fmt <file_or_directory>..." << std::endl;
        std::cerr << "Run `" << programName << " fmt --help` for more information." << std::endl;
        return 1;
    }

    for (const auto &target : targets)
    {
        formatFileOrDirectory(target);
    }

    return 0;
}
} // namespace cli
