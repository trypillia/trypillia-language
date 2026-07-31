#include "ASTOptimizer.h"
#include <cmath>

void ASTOptimizer::optimize(ASTNode *node) {
    if (!node)
        return;
    if (auto prog = dynamic_cast<ProgramNode *>(node)) {
        for (auto &decl : prog->declarations) {
            if (auto stmt = dynamic_cast<StmtNode *>(decl)) {
                decl = optimizeStmt(stmt);
            }
        }
    } else if (auto stmt = dynamic_cast<StmtNode *>(node)) {
        optimizeStmt(stmt);
    }
}

StmtNode *ASTOptimizer::optimizeStmt(StmtNode *stmt) {
    if (!stmt)
        return nullptr;
    if (auto exprStmt = dynamic_cast<ExpressionStmt *>(stmt)) {
        exprStmt->expression = optimizeExpr(exprStmt->expression);
    } else if (auto varStmt = dynamic_cast<VarStmt *>(stmt)) {
        varStmt->initializer = optimizeExpr(varStmt->initializer);
    } else if (auto blockStmt = dynamic_cast<BlockStmt *>(stmt)) {
        for (auto &s : blockStmt->statements) {
            s = optimizeStmt(s);
        }
    } else if (auto ifStmt = dynamic_cast<IfStmt *>(stmt)) {
        ifStmt->condition = optimizeExpr(ifStmt->condition);
        ifStmt->thenBranch = optimizeStmt(ifStmt->thenBranch);
        ifStmt->elseBranch = optimizeStmt(ifStmt->elseBranch);
    } else if (auto whileStmt = dynamic_cast<WhileStmt *>(stmt)) {
        whileStmt->condition = optimizeExpr(whileStmt->condition);
        whileStmt->body = optimizeStmt(whileStmt->body);
    } else if (auto doWhileStmt = dynamic_cast<DoWhileStmt *>(stmt)) {
        doWhileStmt->condition = optimizeExpr(doWhileStmt->condition);
        doWhileStmt->body = optimizeStmt(doWhileStmt->body);
    } else if (auto forStmt = dynamic_cast<ForStmt *>(stmt)) {
        if (auto initStmt = dynamic_cast<StmtNode *>(forStmt->initializer)) {
            forStmt->initializer = optimizeStmt(initStmt);
        }
        forStmt->condition = optimizeExpr(forStmt->condition);
        forStmt->increment = optimizeExpr(forStmt->increment);
        forStmt->body = optimizeStmt(forStmt->body);
    } else if (auto retStmt = dynamic_cast<ReturnStmt *>(stmt)) {
        retStmt->value = optimizeExpr(retStmt->value);
    } else if (auto funcStmt = dynamic_cast<FunctionNode *>(stmt)) {
        for (auto &s : funcStmt->body) {
            s = optimizeStmt(s);
        }
    } else if (auto classStmt = dynamic_cast<ClassNode *>(stmt)) {
        for (auto method : classStmt->methods) {
            optimizeStmt(method);
        }
    }
    return stmt;
}

ExprNode *ASTOptimizer::optimizeExpr(ExprNode *expr) {
    if (!expr)
        return nullptr;
    if (auto binExpr = dynamic_cast<BinaryExpr *>(expr)) {
        binExpr->left = optimizeExpr(binExpr->left);
        binExpr->right = optimizeExpr(binExpr->right);

        if (auto leftLit = dynamic_cast<LiteralExpr *>(binExpr->left)) {
            if (auto rightLit = dynamic_cast<LiteralExpr *>(binExpr->right)) {
                if (leftLit->value.type == TokenType::NUMBER && rightLit->value.type == TokenType::NUMBER) {
                    double l = std::stod(leftLit->value.lexeme);
                    double r = std::stod(rightLit->value.lexeme);
                    double res = 0;
                    bool canFold = true;
                    if (binExpr->op.type == TokenType::PLUS)
                        res = l + r;
                    else if (binExpr->op.type == TokenType::MINUS)
                        res = l - r;
                    else if (binExpr->op.type == TokenType::STAR)
                        res = l * r;
                    else if (binExpr->op.type == TokenType::SLASH)
                        res = l / r;
                    else
                        canFold = false;

                    if (canFold) {
                        Token t;
                        t.type = TokenType::NUMBER;
                        if (res == std::floor(res)) {
                            t.lexeme = std::to_string((long long)res);
                        } else {
                            t.lexeme = std::to_string(res);
                        }
                        auto newLit = new LiteralExpr(t);
                        delete binExpr;
                        return newLit;
                    }
                } else if (leftLit->value.type == TokenType::STRING && rightLit->value.type == TokenType::STRING) {
                    if (binExpr->op.type == TokenType::PLUS) {
                        Token t;
                        t.type = TokenType::STRING;
                        t.lexeme = leftLit->value.lexeme + rightLit->value.lexeme;
                        auto newLit = new LiteralExpr(t);
                        delete binExpr;
                        return newLit;
                    }
                }
            }
        }
        return binExpr;
    } else if (auto unExpr = dynamic_cast<UnaryExpr *>(expr)) {
        unExpr->right = optimizeExpr(unExpr->right);
        if (auto rightLit = dynamic_cast<LiteralExpr *>(unExpr->right)) {
            if (unExpr->op.type == TokenType::MINUS && rightLit->value.type == TokenType::NUMBER) {
                double r = std::stod(rightLit->value.lexeme);
                Token t;
                t.type = TokenType::NUMBER;
                r = -r;
                if (r == std::floor(r)) {
                    t.lexeme = std::to_string((long long)r);
                } else {
                    t.lexeme = std::to_string(r);
                }
                auto newLit = new LiteralExpr(t);
                delete unExpr;
                return newLit;
            }
        }
        return unExpr;
    } else if (auto callExpr = dynamic_cast<CallExpr *>(expr)) {
        callExpr->callee = optimizeExpr(callExpr->callee);
        for (auto &arg : callExpr->arguments) {
            arg = optimizeExpr(arg);
        }
        return callExpr;
    } else if (auto listExpr = dynamic_cast<ListExpr *>(expr)) {
        for (auto &el : listExpr->elements) {
            el = optimizeExpr(el);
        }
        return listExpr;
    } else if (auto getExpr = dynamic_cast<GetExpr *>(expr)) {
        getExpr->object = optimizeExpr(getExpr->object);
        return getExpr;
    } else if (auto assignExpr = dynamic_cast<AssignExpr *>(expr)) {
        assignExpr->value = optimizeExpr(assignExpr->value);
        return assignExpr;
    } else if (auto compAssignExpr = dynamic_cast<CompoundAssignExpr *>(expr)) {
        compAssignExpr->value = optimizeExpr(compAssignExpr->value);
        return compAssignExpr;
    }
    return expr;
}
