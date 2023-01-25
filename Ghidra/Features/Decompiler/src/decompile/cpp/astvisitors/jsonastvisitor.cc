#include "jsonastvisitor.h"
#include "astbuilder.h"

struct ExportAstConfig
{
    std::string output_folder;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ExportAstConfig,
    output_folder
)

json JsonASTVisitor::datatype_to_json(const Datatype* dt)
{
    /** TODO: figure out how we want to handle data types */
    /** TODO: also include the category here if possible...?
     * - well, I guess we really want to extract the category from the debug
     *   information since that is "truth" (and then we need to MAP true debug
     *   info variables to Ghidra AST variables)
    */
    // return dt->getName();
    return _builder->getFullTypeString(dt);
}

json buildAstForFunction(Funcdata* fd, ExportAstConfig* config)
{
    ASTBuilder builder(config->output_folder);
    ASTNode* ast = builder.buildAST(fd);

    JsonASTVisitor visitor(&builder);
    ast->accept(&visitor);
    delete ast;

    return visitor.get_json();
}

// making this its own function so it only gets called 1x just
// by using static on the config variable :)
ExportAstConfig readConfig(char* config_file_path)
{
    std::ifstream f(config_file_path);
    if (f.fail()) {
        throw LowlevelError("AST export config file " +
            string(config_file_path) + " does not exist");
    }

    json data = json::parse(f);
    return data.get<ExportAstConfig>();
}

void exportFunctionAst(Funcdata* fd, char* config_file_path)
{
    static ExportAstConfig config = readConfig(config_file_path);
    json ast_json = buildAstForFunction(fd, &config);

    string filename = config.output_folder + "/" + ensureValidFilename(fd->getName()) + ".json";
    ofstream outfile(filename, ios::out);
    outfile << setw(2) << ast_json << endl;
    outfile.close();
}

JsonASTVisitor::JsonASTVisitor(ASTBuilder* builder)
    : _builder(builder)
{
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

/**
 * @brief Add messages key to dict if this node has any diagnostic or
 * error messages
 */
void addMessages(ASTNode* node, json& jnode)
{
    if (node->messages()->size() > 0) {
        jnode["messages"] = json::array();
        for (auto msg : *node->messages()) {
            jnode["messages"].push_back(msg);
        }
    }
}

void* JsonASTVisitor::visitBinaryOperator(BinaryOperator* b, void* context)
{
    json binop;
    binop["kind"] = "BinaryOperator";
    binop["inner"] = json::array();
    binop["opcode"] = b->opcode();
    addMessages(b, binop);
    return copy_to_parent(binop, context);
}

void* JsonASTVisitor::visitBreakStmt(BreakStmt* bs, void* context)
{
    json bs_j;
    bs_j["kind"] = "BreakStmt";
    addMessages(bs, bs_j);
    return copy_to_parent(bs_j, context);
}

void* JsonASTVisitor::visitCaseStmt(CaseStmt* cs, void* context)
{
    json cs_j;
    cs_j["kind"] = "CaseStmt";
    cs_j["inner"] = json::array();
    addMessages(cs, cs_j);
    return copy_to_parent(cs_j, context);
}

void* JsonASTVisitor::visitCharacterLiteral(CharacterLiteral* cl, void* context)
{
    json cl_json;
    cl_json["kind"] = "CharacterLiteral";
    cl_json["dtype"] = datatype_to_json(cl->dt());
    cl_json["value"] = cl->value();
    addMessages(cl, cl_json);
    return copy_to_parent(cl_json, context);
}

void* JsonASTVisitor::visitCompoundStmt(CompoundStmt* cs, void* context)
{
    json cmp_stmt;
    cmp_stmt["kind"] = "CompoundStmt";
    cmp_stmt["inner"] = json::array();
    addMessages(cs, cmp_stmt);
    return copy_to_parent(cmp_stmt, context);
}

void* JsonASTVisitor::visitConstantExpr(ConstantExpr* cexpr, void* context)
{
    json cexpr_j;
    cexpr_j["kind"] = "ConstantExpr";
    cexpr_j["inner"] = json::array();
    addMessages(cexpr, cexpr_j);
    return copy_to_parent(cexpr_j, context);
}

void* JsonASTVisitor::visitCStyleCastExpr(CStyleCastExpr* cast_expr, void* context)
{
    json cast;
    cast["kind"] = "CStyleCastExpr";
    cast["dtype"] = datatype_to_json(cast_expr->dt());
    addMessages(cast_expr, cast);
    return copy_to_parent(cast, context);
}

void* JsonASTVisitor::visitDeclRefExpr(DeclRefExpr* dr, void* context)
{
    json decl_ref;
    decl_ref["kind"] = "DeclRefExpr";
    decl_ref["referencedDecl_id"] = dr->ref()->id();
    addMessages(dr, decl_ref);
    return copy_to_parent(decl_ref, context);
}

void* JsonASTVisitor::visitDeclStmt(DeclStmt* ds, void* context)
{
    json declstmt;
    declstmt["kind"] = "DeclStmt";
    declstmt["inner"] = json::array();
    addMessages(ds, declstmt);
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
    addMessages(fd, fdecl);
    return copy_to_parent(fdecl, context);
}

void* JsonASTVisitor::visitIntegerLiteral(IntegerLiteral* lit, void* context)
{
    json int_lit;
    int_lit["kind"] = "IntegerLiteral";
    int_lit["value"] = lit->value();
    int_lit["dtype"] = datatype_to_json(lit->dt());
    addMessages(lit, int_lit);
    return copy_to_parent(int_lit, context);
}

void* JsonASTVisitor::visitIfStmt(IfStmt* stmt, void* context)
{
    json ifstmt;
    ifstmt["kind"] = "IfStmt";
    ifstmt["inner"] = json::array();
    addMessages(stmt, ifstmt);
    return copy_to_parent(ifstmt, context);
}

void* JsonASTVisitor::visitLogMsg(LogMsg* logmsg, void* context)
{
    json msg;
    msg["kind"] = "LogMsg";
    msg["message"] = logmsg->message();
    return copy_to_parent(msg, context);
}

void* JsonASTVisitor::visitParenExpr(ParenExpr* pe, void* context)
{
    json paren;
    paren["kind"] = "ParenExpr";
    paren["inner"] = json::array();
    addMessages(pe, paren);
    return copy_to_parent(paren, context);
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

    addMessages(pv, pvdecl);
    return copy_to_parent(pvdecl, context);
}

void* JsonASTVisitor::visitSwitchStmt(SwitchStmt* ss, void* context)
{
    json ss_j;
    ss_j["kind"] = "SwitchStmt";
    ss_j["inner"] = json::array();
    addMessages(ss, ss_j);
    return copy_to_parent(ss_j, context);
}

void* JsonASTVisitor::visitTranslationUnitDecl(TranslationUnitDecl* td, void* context)
{
    json tudecl;
    tudecl["kind"] = "TranslationUnitDecl";
    tudecl["inner"] = json::array();
    addMessages(td, tudecl);
    return copy_to_parent(tudecl, context);
}

void* JsonASTVisitor::visitUnaryOperator(UnaryOperator* uo, void* context)
{
    json uo_json;
    uo_json["kind"] = "UnaryOperator";
    uo_json["inner"] = json::array();
    uo_json["opcode"] = uo->opcode();
    uo_json["dtype"] = datatype_to_json(uo->dt());
    addMessages(uo, uo_json);
    return copy_to_parent(uo_json, context);
}

void* JsonASTVisitor::visitValueDecl(ValueDecl* vd, void* context)
{
    json value_decl;
    value_decl["id"] = vd->id();
    addMessages(vd, value_decl);
    return copy_to_parent(value_decl, context);
}

void* JsonASTVisitor::visitVarDecl(VarDecl* vd, void* context)
{
    json var_decl;
    var_decl["kind"] = "VarDecl";
    var_decl["id"] = vd->id();
    var_decl["dtype"] = datatype_to_json(vd->sym()->getType());
    var_decl["name"] = vd->sym()->getName();
    addMessages(vd, var_decl);
    return copy_to_parent(var_decl, context);
}
