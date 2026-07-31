#ifndef AST_OPTIMIZER_H
#define AST_OPTIMIZER_H

#include "AST.h"
#include <string>

class ASTOptimizer {
  public:
    static void optimize(ASTNode *node);
    static ExprNode *optimizeExpr(ExprNode *expr);
    static StmtNode *optimizeStmt(StmtNode *stmt);
};

#endif
