#pragma once

#include "astvisitor.h"

#include "../../../../third-party/json/single_include/nlohmann/json.hpp"

using json = nlohmann::json;

struct ExportAstConfig;
class ASTBuilder;

/**
 * Constructs an AST representation from Ghidra's internal representation of
 * the high-level code. The PrintLanguage (PrintC) class was used as a reference
 * to develop the algorithm for traversing the information in FuncData, etc
 * needed to construct an AST.
 *
 * CLS: not even sure I need the builder class yet...
*/
json buildAstForFunction(Architecture* ghidra, Funcdata* fd, ExportAstConfig* config);

/**
 * @brief Exports the AST for the given function as a JSON file
 * per the parameters defined in the config file.
 */
void exportFunctionAst(Architecture* ghidra, Funcdata* fd, char* config_file_path);

class JsonASTVisitor : public ASTVisitor
{
public:
    /**
     * @param builder is needed for reuse of some PrintC member
     * functions, otherwise I don't need it
     */
    JsonASTVisitor(ASTBuilder* builder);

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

    inline json& get_json() { return _ast_json; }

    json typeToJson(Type* type);
    json datatypeToJson(const Datatype* dt);

protected:

    /**
     * @brief Copies the data into the parent_context and return
     * a pointer to the data after it has been copied inside the parent
     * json instance. If parent_context is null, then the data is set
     * as the HEAD of the tree (in _ast_json) and a pointer to it
     * is returned directly
     */
    json* copy_to_parent(json& data, void* parent_context);

    json structureFieldToJson(TypeField ghidra_field);
    json buildStructFields(TypeStruct* ghidra_struct);
    json buildStructuresById(StructTypeLibrary* type_lib);

    json _ast_json;
    ASTBuilder* _builder;
};
