#include "jsonastvisitor.h"
#include "astbuilder.h"

struct ExportAstConfig
{
    std::string output_folder;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ExportAstConfig,
    output_folder
)

json JsonASTVisitor::datatypeToJson(const Datatype* dt)
{
    /** TODO: figure out how we want to handle data types */
    /** TODO: also include the category here if possible...?
     * - well, I guess we really want to extract the category from the debug
     *   information since that is "truth" (and then we need to MAP true debug
     *   info variables to Ghidra AST variables)
    */
    // return dt->getName();
    return dt ? _builder->getFullTypeString(dt) : "";
}

json buildAstForFunction(Architecture* ghidra, Funcdata* fd, ExportAstConfig* config)
{
    ASTBuilder builder(ghidra, config->output_folder);
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

void exportFunctionAst(Architecture* ghidra, Funcdata* fd, char* config_file_path)
{
    static ExportAstConfig config = readConfig(config_file_path);
    json ast_json = buildAstForFunction(ghidra, fd, &config);

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

void* JsonASTVisitor::visitArraySubscriptExpr(ArraySubscriptExpr* ase, void* context)
{
    json ase_j;
    ase_j["kind"] = "ArraySubscriptExpr";
    addMessages(ase, ase_j);
    return copy_to_parent(ase_j, context);
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

void* JsonASTVisitor::visitBuiltinType(BuiltinType* bit, void* context)
{
    json bit_j;
    bit_j["kind"] = "BuiltinType";
    bit_j["name"] = bit->name();
    bit_j["ghidra_name"] = bit->ghidra_name();
    bit_j["is_floating_point"] = bit->isFloatingPoint();
    bit_j["is_signed"] = bit->isSigned();
    bit_j["size"] = bit->size();
    addMessages(bit, bit_j);
    return copy_to_parent(bit_j, context);
}

void* JsonASTVisitor::visitCallExpr(CallExpr* ce, void* context)
{
    json ce_j;
    ce_j["kind"] = "CallExpr";
    return copy_to_parent(ce_j, context);
}

void* JsonASTVisitor::visitCaseStmt(CaseStmt* cs, void* context)
{
    json cs_j;
    cs_j["kind"] = "CaseStmt";
    cs_j["inner"] = json::array();
    addMessages(cs, cs_j);
    return copy_to_parent(cs_j, context);
}

/**
 * @brief Takes advantage of the fact that this class itself implements
 * the visitor pattern for all ASTNode types, including those derived
 * from Type. Since the normal visitor pattern will ONLY visit *child* nodes
 * automatically, the ASTNodes that have .type() fields (not proper child
 * nodes per se) will get missed by the default algorithm. So we simply
 * instantiate another JsonASTVisitor here, visit the type, and return the
 * JSON - thus handling both cases with one implemntation.
 *
 * @param type
 * @return json
 */
json JsonASTVisitor::typeToJson(Type* type)
{
    JsonASTVisitor v(_builder);
    type->accept(&v);
    return v.get_json();
}

void* JsonASTVisitor::visitCharacterLiteral(CharacterLiteral* cl, void* context)
{
    json cl_json;
    cl_json["kind"] = "CharacterLiteral";
    cl_json["dtype"] = typeToJson(cl->type());
    // cl_json["dtype_name"] = datatypeToJson(cl->type()->ghidra_dtype());
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

void* JsonASTVisitor::visitConstantArrayType(ConstantArrayType* cat, void* context)
{
    json cat_j;
    cat_j["kind"] = "ConstantArrayType";
    cat_j["inner"] = json::array();
    cat_j["num_elements"] = cat->numElements();
    addMessages(cat, cat_j);
    return copy_to_parent(cat_j, context);
}

void* JsonASTVisitor::visitConstantExpr(ConstantExpr* cexpr, void* context)
{
    json cexpr_j;
    cexpr_j["kind"] = "ConstantExpr";
    cexpr_j["inner"] = json::array();
    addMessages(cexpr, cexpr_j);
    return copy_to_parent(cexpr_j, context);
}

void* JsonASTVisitor::visitCopyPlaceholder(CopyPlaceholder* cp, void* context)
{
    // we just want the child node to be added in this node's place...let's see if
    // this works
    return context;
}

void* JsonASTVisitor::visitCStyleCastExpr(CStyleCastExpr* cast_expr, void* context)
{
    json cast;
    cast["kind"] = "CStyleCastExpr";
    cast["dtype"] = typeToJson(cast_expr->type());
    // cast["dtype_name"] = datatypeToJson(cast_expr->type()->ghidra_dtype());
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
    fdecl["id"] = fd->id();
    fdecl["inner"] = json::array();
    fdecl["name"] = fd->name();
    fdecl["address"] = to_hex(fd->address());
    fdecl["return_dtype"] = typeToJson(fd->return_type());
    // fdecl["return_dtype_name"] = datatypeToJson(fd->return_type()->ghidra_dtype());
    addMessages(fd, fdecl);
    return copy_to_parent(fdecl, context);
}

void* JsonASTVisitor::visitIntegerLiteral(IntegerLiteral* lit, void* context)
{
    json int_lit;
    int_lit["kind"] = "IntegerLiteral";
    int_lit["value"] = lit->value();
    int_lit["dtype"] = typeToJson(lit->type());
    // int_lit["dtype_name"] = datatypeToJson(lit->type()->ghidra_dtype());
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

void* JsonASTVisitor::visitFieldDecl(FieldDecl* fd, void* context)
{
    json fd_j;
    fd_j["kind"] = "FieldDecl";
    fd_j["name"] = fd->name();
    fd_j["dtype"] = typeToJson(fd->type());
    addMessages(fd, fd_j);
    return copy_to_parent(fd_j, context);
}

void* JsonASTVisitor::visitRecordDecl(RecordDecl* rd, void* context)
{
    json rd_j;
    rd_j["kind"] = "RecordDecl";
    rd_j["sid"] = rd->sid();
    rd_j["name"] = rd->name();
    addMessages(rd, rd_j);
    return copy_to_parent(rd_j, context);
}

void* JsonASTVisitor::visitParmVarDecl(ParmVarDecl* pv, void* context)
{
    json pvdecl;
    pvdecl["kind"] = "ParmVarDecl";
    pvdecl["id"] = pv->id();
    if (pv->ghidra_sym()) {
        pvdecl["name"] = pv->name();
    }
    pvdecl["dtype"] = typeToJson(pv->type());
    // pvdecl["dtype_name"] = datatypeToJson(pv->type()->ghidra_dtype());
    addMessages(pv, pvdecl);
    return copy_to_parent(pvdecl, context);
}

void* JsonASTVisitor::visitPointerType(PointerType* pt, void* context)
{
    json pt_j;
    pt_j["kind"] = "PointerType";
    pt_j["inner"] = json::array();
    addMessages(pt, pt_j);
    return copy_to_parent(pt_j, context);
}

void* JsonASTVisitor::visitStructType(StructType* st, void* context)
{
    json st_j;
    st_j["kind"] = "StructType";
    st_j["sid"] = st->sid();
    addMessages(st, st_j);
    return copy_to_parent(st_j, context);
}

void* JsonASTVisitor::visitVoidType(VoidType* vt, void* context)
{
    json vt_j;
    vt_j["kind"] = "VoidType";
    addMessages(vt, vt_j);
    return copy_to_parent(vt_j, context);
}

void* JsonASTVisitor::visitReturnStmt(ReturnStmt* rs, void* context)
{
    json rs_j;
    rs_j["kind"] = "ReturnStmt";
    rs_j["inner"] = json::array();
    addMessages(rs, rs_j);
    return copy_to_parent(rs_j, context);
}

void* JsonASTVisitor::visitStringLiteral(StringLiteral* lit, void* context)
{
    json lit_j;
    lit_j["kind"] = "StringLiteral";
    lit_j["value"] = lit->value();
    addMessages(lit, lit_j);
    return copy_to_parent(lit_j, context);
}

void* JsonASTVisitor::visitSwitchStmt(SwitchStmt* ss, void* context)
{
    json ss_j;
    ss_j["kind"] = "SwitchStmt";
    ss_j["inner"] = json::array();
    addMessages(ss, ss_j);
    return copy_to_parent(ss_j, context);
}

json JsonASTVisitor::structureFieldToJson(TypeField ghidra_field)
{
    Type* dtype = _builder->toAstType(ghidra_field.type);

    json field_j;
    field_j["name"] = ghidra_field.name;
    field_j["offset"] = ghidra_field.offset;
    field_j["dtype"] = typeToJson(dtype);

    delete dtype;

    return field_j;
}

json JsonASTVisitor::buildStructFields(TypeStruct* ghidra_struct)
{
    json fields;

    for (TypeField ghidra_field : getStructFields(ghidra_struct)) {
        string offset_str = std::to_string(ghidra_field.offset);
        fields[offset_str] = structureFieldToJson(ghidra_field);
    }

    return fields;
}

json JsonASTVisitor::buildStructuresById(StructTypeLibrary* type_lib)
{
    json structs_by_id;

    for (auto stype : type_lib->getMappedStructs()) {
        string sid_str = std::to_string(stype.sid());
        TypeStruct* ghidra_struct = stype.ghidra_struct();

        json stype_j;
        stype_j["name"] = ghidra_struct->getName();
        stype_j["size"] = ghidra_struct->getSize();
        stype_j["fields"] = buildStructFields(ghidra_struct);
        structs_by_id[sid_str] = stype_j;
    }

    return structs_by_id;
}

void* JsonASTVisitor::visitTranslationUnitDecl(TranslationUnitDecl* td, void* context)
{
    json tudecl;
    tudecl["kind"] = "TranslationUnitDecl";
    tudecl["inner"] = json::array();
    tudecl["structures_by_id"] = buildStructuresById(_builder->type_library());
    addMessages(td, tudecl);
    return copy_to_parent(tudecl, context);
}

void* JsonASTVisitor::visitType(Type* type, void* context)
{
    json type_j;
    type_j["kind"] = "Type";
    type_j["name"] = type->name();
    addMessages(type, type_j);
    return copy_to_parent(type_j, context);
}

void* JsonASTVisitor::visitTypedefDecl(TypedefDecl* td, void* context)
{
    json td_j;
    td_j["kind"] = "TypedefDecl";
    td_j["name"] = td->name();
    td_j["inner"] = json::array();
    addMessages(td, td_j);
    return copy_to_parent(td_j, context);
}

void* JsonASTVisitor::visitTypedefType(TypedefType* tdtype, void* context)
{
    json tdtype_j;
    tdtype_j["kind"] = "TypedefType";
    tdtype_j["name"] = tdtype->name();
    /** QUESTION: should we do anything with the decl? */
    addMessages(tdtype, tdtype_j);
    return copy_to_parent(tdtype_j, context);
}

void* JsonASTVisitor::visitUnaryOperator(UnaryOperator* uo, void* context)
{
    json uo_json;
    uo_json["kind"] = "UnaryOperator";
    uo_json["inner"] = json::array();
    uo_json["opcode"] = uo->opcode();
    // uo_json["dtype"] = typeToJson(uo->type());
    // uo_json["dtype_name"] = datatypeToJson(uo->type()->ghidra_dtype());
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
    var_decl["dtype"] = typeToJson(vd->type());
    // var_decl["dtype_name"] = datatypeToJson(vd->type()->ghidra_dtype());
    var_decl["name"] = vd->name();
    addMessages(vd, var_decl);
    return copy_to_parent(var_decl, context);
}
