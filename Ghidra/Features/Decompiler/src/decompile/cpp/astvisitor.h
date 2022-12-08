#pragma once

#include "ast.h"

/**
 * @brief ASTVisitor interface is what should be implemented by code that
 * needs to interact with the AST. Since the base ASTVisitor does nothing by
 * default, derived classes may "opt in" to visit elements they care about
 * without having to create boilerplate for every possible type of node.
 */
class ASTVisitor
{
public:
    virtual void visitBinaryOperator(BinaryOperator*);
    virtual void visitCompoundStmt(CompoundStmt*);
    virtual void visitDeclRefExpr(DeclRefExpr*);
    virtual void visitDeclStmt(DeclStmt*);
    virtual void visitIntegerLiteral(IntegerLiteral*);
    virtual void visitFunctionDecl(FunctionDecl*);
    virtual void visitLogMsg(LogMsg*);
    virtual void visitParmVarDecl(ParmVarDecl*);
    virtual void visitValueDecl(ValueDecl*);
    virtual void visitVarDecl(VarDecl*);
};
