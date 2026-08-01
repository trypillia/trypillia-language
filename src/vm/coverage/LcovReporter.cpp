#include "LcovReporter.h"

#include <fstream>
#include <iostream>
#include <map>
#include <vector>

#include "../compiler/Chunk.h"

void LcovReporter::generateReport(VM *vm, const std::string &outputPath)
{
    // Map of filename -> (line number -> execution count)
    std::map<std::string, std::map<int, uint32_t>> fileCoverage;

    // Traverse all objects in the VM to find ObjFunctions
    for (Obj *o = vm->objects; o != nullptr; o = o->nextObj)
    {
        if (o->type == ObjType::OBJ_FUNCTION)
        {
            ObjFunction *func = static_cast<ObjFunction *>(o);
            if (func->filename.empty() || func->chunk == nullptr)
            {
                continue;
            }

            std::string filename = func->filename;
            Chunk *chunk = func->chunk;

            auto &lineMap = fileCoverage[filename];

            // Iterate over all opcodes in the chunk
            for (size_t i = 0; i < chunk->code.size(); i++)
            {
                if (i < chunk->lines.size() && i < chunk->coverage.size())
                {
                    int line = chunk->lines[i];
                    uint32_t hits = chunk->coverage[i];

                    // The line execution count is the maximum of any instruction on that
                    // line
                    if (hits > lineMap[line])
                    {
                        lineMap[line] = hits;
                    }
                }
            }
        }
    }

    std::ofstream out(outputPath);
    if (!out.is_open())
    {
        std::cerr << "Failed to open coverage report file: " << outputPath << std::endl;
        return;
    }

    for (const auto &filePair : fileCoverage)
    {
        const std::string &filename = filePair.first;
        const auto &lineMap = filePair.second;

        out << "TN:\n"; // Test Name (empty)
        out << "SF:" << filename << "\n";

        int linesFound = 0;
        int linesHit = 0;

        for (const auto &linePair : lineMap)
        {
            int line = linePair.first;
            uint32_t hits = linePair.second;

            // We only emit lines > 0.
            if (line > 0)
            {
                out << "DA:" << line << "," << hits << "\n";
                linesFound++;
                if (hits > 0)
                {
                    linesHit++;
                }
            }
        }

        out << "LF:" << linesFound << "\n";
        out << "LH:" << linesHit << "\n";
        out << "end_of_record\n";
    }

    out.close();
    std::cout << "LCOV coverage report generated at: " << outputPath << std::endl;
}
