#include "jsonastvisitor.h"
#include "astbuilder.h"

json datatype_to_json(Datatype* dt)
{
    /** TODO: figure out how we want to handle data types */
    /** TODO: also include the category here if possible...?
     * - well, I guess we really want to extract the category from the debug
     *   information since that is "truth" (and then we need to MAP true debug
     *   info variables to Ghidra AST variables)
    */
    return dt->getName();
}

json buildAstForFunction(Funcdata* fd)
{
    ASTBuilder builder;
    ASTNode* ast = builder.buildAST(fd);

    JsonASTVisitor visitor;
    ast->accept(&visitor);
    delete ast;

    return visitor.get_json();
}

json* JsonASTVisitor::copy_to_parent(json& data, void* parent_context)
{
    if (parent_context) {
        // copy to parent
        json& parent = *((json*)parent_context);
        parent["inner"].push_back(data);

        // return pointer to copied memory inside parent
        return &parent["inner"].back();
    }
    else {
        _ast_json = data;
        return &_ast_json;
    }
}

void* JsonASTVisitor::visitBinaryOperator(BinaryOperator* b, void* context)
{
    json binop;
    binop["kind"] = "BinaryOperator";
    binop["inner"] = json::array();
    binop["opcode"] = b->opcode();
    return copy_to_parent(binop, context);
}

void* JsonASTVisitor::visitCompoundStmt(CompoundStmt*, void* context)
{
    json cmp_stmt;
    cmp_stmt["kind"] = "CompoundStmt";
    cmp_stmt["inner"] = json::array();
    return copy_to_parent(cmp_stmt, context);
}

void* JsonASTVisitor::visitCStyleCastExpr(CStyleCastExpr* cast_expr, void* context)
{
    json cast;
    cast["kind"] = "CStyleCastExpr";
    cast["dtype"] = datatype_to_json(cast_expr->dt());
    return copy_to_parent(cast, context);
}

void* JsonASTVisitor::visitDeclRefExpr(DeclRefExpr* dr, void* context)
{
    json decl_ref;
    decl_ref["kind"] = "DeclRefExpr";
    decl_ref["referencedDecl_id"] = dr->ref()->id();
    return copy_to_parent(decl_ref, context);
}

void* JsonASTVisitor::visitDeclStmt(DeclStmt*, void* context)
{
    json declstmt;
    declstmt["kind"] = "DeclStmt";
    declstmt["inner"] = json::array();
    return copy_to_parent(declstmt, context);
}

void* JsonASTVisitor::visitFunctionDecl(FunctionDecl* fd, void* context)
{
    // basic function node info
    json fdecl;
    fdecl["kind"] = "FunctionDecl";
    fdecl["inner"] = json::array();
    fdecl["name"] = fd->name();
    fdecl["address"] = to_hex(fd->address());
    fdecl["return_dtype"] = datatype_to_json(fd->return_dtype());
    return copy_to_parent(fdecl, context);
}

void* JsonASTVisitor::visitIntegerLiteral(IntegerLiteral* lit, void* context)
{
    json int_lit;
    int_lit["kind"] = "IntegerLiteral";
    int_lit["value"] = lit->value();
    int_lit["dtype"] = datatype_to_json(lit->dt());
    return copy_to_parent(int_lit, context);
}

void* JsonASTVisitor::visitIfStmt(IfStmt* stmt, void* context)
{
    json ifstmt;
    ifstmt["kind"] = "IfStmt";
    return copy_to_parent(ifstmt, context);
}

void* JsonASTVisitor::visitLogMsg(LogMsg* logmsg, void* context)
{
    json msg;
    msg["kind"] = "LogMsg";
    msg["message"] = logmsg->message();
    return copy_to_parent(msg, context);
}

void* JsonASTVisitor::visitParmVarDecl(ParmVarDecl* pv, void* context)
{
    json pvdecl;
    pvdecl["kind"] = "ParmVarDecl";
    pvdecl["id"] = pv->id();

    if (pv->sym()) {
        pvdecl["dtype"] = datatype_to_json(pv->sym()->getType());
        pvdecl["name"] = pv->sym()->getName();
    }
    else {
        pvdecl["dtype"] = datatype_to_json(pv->param()->getType());
    }

    return copy_to_parent(pvdecl, context);
}

void* JsonASTVisitor::visitTranslationUnitDecl(TranslationUnitDecl* td, void* context)
{
    json tudecl;
    tudecl["kind"] = "TranslationUnitDecl";
    tudecl["inner"] = json::array();
    return copy_to_parent(tudecl, context);
}

void* JsonASTVisitor::visitValueDecl(ValueDecl* vd, void* context)
{
    json value_decl;
    value_decl["id"] = vd->id();
    return copy_to_parent(value_decl, context);
}

void* JsonASTVisitor::visitVarDecl(VarDecl* vd, void* context)
{
    json var_decl;
    var_decl["kind"] = "VarDecl";
    var_decl["id"] = vd->id();
    var_decl["dtype"] = datatype_to_json(vd->sym()->getType());
    var_decl["name"] = vd->sym()->getName();
    return copy_to_parent(var_decl, context);
}
