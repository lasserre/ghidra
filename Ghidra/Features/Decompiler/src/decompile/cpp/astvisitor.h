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
    virtual void visitFunctionDecl(FunctionDecl*);
    virtual void visitParmVarDecl(ParmVarDecl*);
};
