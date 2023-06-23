#pragma once

#include "ast.h"

/**
 * @brief ASTVisitor interface is what should be implemented by code that
 * needs to interact with the AST. Since the base ASTVisitor does nothing by
 * default, derived classes may "opt in" to visit elements they care about
 * without having to create boilerplate for every possible type of node.
 *
 * Each visit() function accepts a void* context and returns a void* context.
 * This may be used at the discretion of the concrete visitor, and values
 * returned from visit() functions will be passed as parameters to the visit()
 * calls to child nodes
 */
class ASTVisitor
{
public:
    virtual void* visitArraySubscriptExpr(ArraySubscriptExpr*, void*);
    virtual void* visitBinaryOperator(BinaryOperator*, void*);
    virtual void* visitBreakStmt(BreakStmt*, void*);
    virtual void* visitBuiltinType(BuiltinType*, void*);
    virtual void* visitCallExpr(CallExpr*, void*);
    virtual void* visitCaseStmt(CaseStmt*, void*);
    virtual void* visitCharacterLiteral(CharacterLiteral*, void*);
    virtual void* visitCompoundStmt(CompoundStmt*, void*);
    virtual void* visitConstantArrayType(ConstantArrayType*, void*);
    virtual void* visitConstantExpr(ConstantExpr*, void*);
    virtual void* visitContinueStmt(ContinueStmt*, void*);
    virtual void* visitCopyPlaceholder(CopyPlaceholder*, void*);
    virtual void* visitCStyleCastExpr(CStyleCastExpr*, void*);
    virtual void* visitDeclRefExpr(DeclRefExpr*, void*);
    virtual void* visitDeclStmt(DeclStmt*, void*);
    virtual void* visitDoStmt(DoStmt*, void*);
    virtual void* visitFloatingLiteral(FloatingLiteral*, void*);
    virtual void* visitForStmt(ForStmt*, void*);
    virtual void* visitFunctionDecl(FunctionDecl*, void*);
    virtual void* visitFunctionType(FunctionType*, void*);
    virtual void* visitGotoStmt(GotoStmt*, void*);
    virtual void* visitIfStmt(IfStmt*, void*);
    virtual void* visitIntegerLiteral(IntegerLiteral*, void*);
    virtual void* visitLabelStmt(LabelStmt*, void*);
    virtual void* visitMemberExpr(MemberExpr*, void*);
    virtual void* visitNullNode(NullNode*, void*);
    virtual void* visitParenExpr(ParenExpr*, void*);
    virtual void* visitFieldDecl(FieldDecl*, void*);
    virtual void* visitRecordDecl(RecordDecl*, void*);
    virtual void* visitParmVarDecl(ParmVarDecl*, void*);
    virtual void* visitPointerType(PointerType*, void*);
    virtual void* visitStructType(StructType*, void*);
    virtual void* visitVoidType(VoidType*, void*);
    virtual void* visitReturnStmt(ReturnStmt*, void*);
    virtual void* visitStringLiteral(StringLiteral*, void*);
    virtual void* visitSwitchStmt(SwitchStmt*, void*);
    virtual void* visitTranslationUnitDecl(TranslationUnitDecl*, void*);
    virtual void* visitType(Type*, void*);
    virtual void* visitTypedefDecl(TypedefDecl*, void*);
    virtual void* visitTypedefType(TypedefType*, void*);
    virtual void* visitUnaryOperator(UnaryOperator*, void*);
    virtual void* visitValueDecl(ValueDecl*, void*);
    virtual void* visitVarDecl(VarDecl*, void*);
    virtual void* visitWhileStmt(WhileStmt*, void*);
};
