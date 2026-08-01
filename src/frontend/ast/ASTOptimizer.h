#ifndef AST_OPTIMIZER_H
#define AST_OPTIMIZER_H

#include <string>

#include "AST.h"

class ASTOptimizer
{
  public:
    static void optimize(ASTNode *node);
    static ExprNode *optimizeExpr(ExprNode *expr);
    static StmtNode *optimizeStmt(StmtNode *stmt);
};

#endif
