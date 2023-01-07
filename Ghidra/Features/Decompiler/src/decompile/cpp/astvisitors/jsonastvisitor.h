#pragma once

#include "astvisitor.h"

#include "../../../../third-party/json/single_include/nlohmann/json.hpp"

using json = nlohmann::json;

/**
 * Constructs an AST representation from Ghidra's internal representation of
 * the high-level code. The PrintLanguage (PrintC) class was used as a reference
 * to develop the algorithm for traversing the information in FuncData, etc
 * needed to construct an AST.
 *
 * CLS: not even sure I need the builder class yet...
*/
json buildAstForFunction(Funcdata* fd);

class ASTBuilder;

class JsonASTVisitor : public ASTVisitor
{
public:
    /**
     * @param builder is needed for reuse of some PrintC member
     * functions, otherwise I don't need it
     */
    JsonASTVisitor(ASTBuilder* builder);

    virtual void* visitBinaryOperator(BinaryOperator*, void*);
    virtual void* visitCharacterLiteral(CharacterLiteral*, void*);
    virtual void* visitCompoundStmt(CompoundStmt*, void*);
    virtual void* visitCStyleCastExpr(CStyleCastExpr*, void*);
    virtual void* visitDeclRefExpr(DeclRefExpr*, void*);
    virtual void* visitDeclStmt(DeclStmt*, void*);
    virtual void* visitFunctionDecl(FunctionDecl*, void*);
    virtual void* visitIfStmt(IfStmt*, void*);
    virtual void* visitIntegerLiteral(IntegerLiteral*, void*);
    virtual void* visitLogMsg(LogMsg*, void*);
    virtual void* visitParenExpr(ParenExpr*, void*);
    virtual void* visitParmVarDecl(ParmVarDecl*, void*);
    virtual void* visitTranslationUnitDecl(TranslationUnitDecl*, void*);
    virtual void* visitUnaryOperator(UnaryOperator*, void*);
    virtual void* visitValueDecl(ValueDecl*, void*);
    virtual void* visitVarDecl(VarDecl*, void*);

    inline json& get_json() { return _ast_json; }

protected:

    /**
     * @brief Copies the data into the parent_context and return
     * a pointer to the data after it has been copied inside the parent
     * json instance. If parent_context is null, then the data is set
     * as the HEAD of the tree (in _ast_json) and a pointer to it
     * is returned directly
     */
    json* copy_to_parent(json& data, void* parent_context);

    json datatype_to_json(Datatype* dt);

    json _ast_json;
    ASTBuilder* _builder;
};
