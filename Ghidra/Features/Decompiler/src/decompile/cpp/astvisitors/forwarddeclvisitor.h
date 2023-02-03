#pragma once

#include "astvisitor.h"

/**
 * @brief ASTVisitor that generates forward declarations for each non-builtin
 * data type referenced within an AST snippet
 *
 */
class ForwardDeclVisitor : public ASTVisitor
{
public:
    /**
     * @param builder is needed for reuse of some PrintC member
     * functions, otherwise I don't need it
     */
    ForwardDeclVisitor();

    // deposit/generate/build/emit/insert
    // void generate

    /**
     * @brief Inserts statements as children of the given parent node that
     * declare all non-builtin data types referenced in the processed AST.
     * This only inserts data if it is run after the target AST is processed
     * (i.e. node->accept(visitor) has executed on the node of interest).
     *
     * TODO: just do data types here? or should we defer all forward declarations
     * by temporarily inserting PendingVar() ASTNodes and letting this (separate)
     * visitor do all the work of 1) generating ids, 2) replacing PendingVar with
     * proper DeclRefExpr's and 3) generating appropriate forward declarations?
     *
     * - seems simpler for ASTBuilder to just do it - it knows what each type of
     * variable is already
     * - avoids the replacing hassle
     * - where do you draw the line? e.g. what about ParamVars? it is weird to
     * not actually have these yet when you could just be done. Or local vars...
     *
     * only things that need to be forward declared are:
     * 1. user data types
     * 2. functions called
     * 3. global variables referenced
     *
     * DECISION: only do non-builtin DATA TYPES here!!
     *           (leave globals/funcs in ast builder for now!)
     *
     * @param parent
     */
    void insertForwardDecls(ASTNode* parent);

    virtual void* visitBinaryOperator(BinaryOperator*, void*);
    virtual void* visitBreakStmt(BreakStmt*, void*);
    virtual void* visitCallExpr(CallExpr*, void*);
    virtual void* visitCaseStmt(CaseStmt*, void*);
    virtual void* visitCharacterLiteral(CharacterLiteral*, void*);
    virtual void* visitCompoundStmt(CompoundStmt*, void*);
    virtual void* visitConstantExpr(ConstantExpr*, void*);
    virtual void* visitCStyleCastExpr(CStyleCastExpr*, void*);
    virtual void* visitDeclRefExpr(DeclRefExpr*, void*);
    virtual void* visitDeclStmt(DeclStmt*, void*);
    virtual void* visitFunctionDecl(FunctionDecl*, void*);
    virtual void* visitIfStmt(IfStmt*, void*);
    virtual void* visitIntegerLiteral(IntegerLiteral*, void*);
    virtual void* visitLogMsg(LogMsg*, void*);
    virtual void* visitParenExpr(ParenExpr*, void*);
    virtual void* visitParmVarDecl(ParmVarDecl*, void*);
    virtual void* visitSwitchStmt(SwitchStmt*, void*);
    virtual void* visitTranslationUnitDecl(TranslationUnitDecl*, void*);
    virtual void* visitUnaryOperator(UnaryOperator*, void*);
    virtual void* visitValueDecl(ValueDecl*, void*);
    virtual void* visitVarDecl(VarDecl*, void*);

protected:
};
