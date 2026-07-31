#ifndef SEMANTIC_ANALYZER_H
#define SEMANTIC_ANALYZER_H

#include "../ast/AST.h"
#include "../symbol/SymbolTable.h"

class SemanticAnalyzer {
  public:
    std::string currentFilename;
    SymbolTable *analyze(ASTNode *ast);
};

#endif // SEMANTIC_ANALYZER_H
