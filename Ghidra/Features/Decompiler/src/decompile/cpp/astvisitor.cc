#include "astvisitor.h"

// default implementation is to do nothing

void ASTVisitor::visitBinaryOperator(BinaryOperator*)
{ }

void ASTVisitor::visitCompoundStmt(CompoundStmt*)
{ }

void ASTVisitor::visitDeclRefExpr(DeclRefExpr*)
{ }

void ASTVisitor::visitDeclStmt(DeclStmt*)
{ }

void ASTVisitor::visitFunctionDecl(FunctionDecl*)
{ }

void ASTVisitor::visitLogMsg(LogMsg*)
{ }

void ASTVisitor::visitParmVarDecl(ParmVarDecl*)
{ }

void ASTVisitor::visitValueDecl(ValueDecl*)
{ }

void ASTVisitor::visitVarDecl(VarDecl*)
{ }
