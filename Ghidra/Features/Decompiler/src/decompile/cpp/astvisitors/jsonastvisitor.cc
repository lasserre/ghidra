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
    if (!ast) {
        json err;
        err["message"] = "AST for function " + fd->getName() + " failed. See log file for details";
        return err;
    }

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

    std::stringstream stream;
    stream << "Func" << std::hex << fd->getAddress().getOffset() << "-" << fd->getName();
    string filename = config.output_folder + "/" + ensureValidFilename(stream.str()) + ".json";
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

void* JsonASTVisitor::visitArraySubscriptExpr(ArraySubscriptExpr* ase, void* context)
{
    json ase_j;
    ase_j["kind"] = "ArraySubscriptExpr";
    ase_j["instr_addr"] = ase->instr_addr();
    return copy_to_parent(ase_j, context);
}

void* JsonASTVisitor::visitBinaryOperator(BinaryOperator* b, void* context)
{
    json binop;
    binop["kind"] = "BinaryOperator";
    binop["inner"] = json::array();
    binop["opcode"] = b->opcode();
    binop["instr_addr"] = b->instr_addr();
    return copy_to_parent(binop, context);
}

void* JsonASTVisitor::visitBreakStmt(BreakStmt* bs, void* context)
{
    json bs_j;
    bs_j["kind"] = "BreakStmt";
    return copy_to_parent(bs_j, context);
}

void* JsonASTVisitor::visitBuiltinType(BuiltinType* bit, void* context)
{
    json bit_j;
    bit_j["kind"] = "BuiltinType";
    bit_j["name"] = bit->name();
    bit_j["is_fp"] = bit->isFloatingPoint();
    bit_j["signed"] = bit->isSigned();
    bit_j["size"] = bit->size();
    return copy_to_parent(bit_j, context);
}

void* JsonASTVisitor::visitCallExpr(CallExpr* ce, void* context)
{
    json ce_j;
    ce_j["kind"] = "CallExpr";
    ce_j["instr_addr"] = ce->instr_addr();
    return copy_to_parent(ce_j, context);
}

void* JsonASTVisitor::visitCaseStmt(CaseStmt* cs, void* context)
{
    json cs_j;
    cs_j["kind"] = "CaseStmt";
    cs_j["inner"] = json::array();
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
    cl_json["instr_addr"] = cl->instr_addr();
    return copy_to_parent(cl_json, context);
}

void* JsonASTVisitor::visitCompoundStmt(CompoundStmt* cs, void* context)
{
    json cmp_stmt;
    cmp_stmt["kind"] = "CompoundStmt";
    cmp_stmt["inner"] = json::array();
    return copy_to_parent(cmp_stmt, context);
}

void* JsonASTVisitor::visitConstantArrayType(ConstantArrayType* cat, void* context)
{
    json cat_j;
    cat_j["kind"] = "ArrayType";    // use ArrayType to match astlib
    cat_j["inner"] = json::array();
    cat_j["nelem"] = cat->numElements();
    return copy_to_parent(cat_j, context);
}

void* JsonASTVisitor::visitConstantExpr(ConstantExpr* cexpr, void* context)
{
    json cexpr_j;
    cexpr_j["kind"] = "ConstantExpr";
    cexpr_j["inner"] = json::array();
    return copy_to_parent(cexpr_j, context);
}

void* JsonASTVisitor::visitContinueStmt(ContinueStmt* cs, void* context)
{
    json cs_j;
    cs_j["kind"] = "ContinueStmt";
    return copy_to_parent(cs_j, context);
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
    cast["instr_addr"] = cast_expr->instr_addr();
    return copy_to_parent(cast, context);
}

void* JsonASTVisitor::visitDeclRefExpr(DeclRefExpr* dr, void* context)
{
    json decl_ref;
    decl_ref["kind"] = "DeclRefExpr";
    decl_ref["referencedDecl_id"] = dr->ref()->id();
    decl_ref["type"] = dr->decl_type();
    if (dr->decl_type() == eDeclRefExprType::ENUM_DECL) {
        EnumConstantDecl* edecl = dynamic_cast<EnumConstantDecl*>(dr->ref());
        decl_ref["enum_const_name"] = edecl->name();
        decl_ref["enum_const_value"] = edecl->value();
    }
    decl_ref["instr_addr"] = dr->instr_addr();
    return copy_to_parent(decl_ref, context);
}

void* JsonASTVisitor::visitDeclStmt(DeclStmt* ds, void* context)
{
    json declstmt;
    declstmt["kind"] = "DeclStmt";
    declstmt["inner"] = json::array();
    return copy_to_parent(declstmt, context);
}

void* JsonASTVisitor::visitDefaultStmt(DefaultStmt* ds, void* context)
{
    json ds_j;
    ds_j["kind"] = "DefaultStmt";
    ds_j["inner"] = json::array();
    return copy_to_parent(ds_j, context);
}

void* JsonASTVisitor::visitDoStmt(DoStmt* ds, void* context)
{
    json ds_j;
    ds_j["kind"] = "DoStmt";
    ds_j["inner"] = json::array();
    ds_j["instr_addr"] = ds->instr_addr();
    return copy_to_parent(ds_j, context);
}

void* JsonASTVisitor::visitEnumDecl(EnumDecl* ed, void* context)
{
    json ed_j;
    ed_j["kind"] = "EnumDecl";
    ed_j["name"] = ed->name();
    ed_j["inner"] = json::array();
    return copy_to_parent(ed_j, context);
}

void* JsonASTVisitor::visitEnumConstantDecl(EnumConstantDecl* ecd, void* context)
{
    json ecd_j;
    ecd_j["kind"] = "EnumConstantDecl";
    ecd_j["id"] = ecd->id();
    ecd_j["name"] = ecd->name();
    ecd_j["value"] = ecd->value();
    return copy_to_parent(ecd_j, context);
}

void* JsonASTVisitor::visitEnumType(EnumType* et, void* context)
{
    json et_j;
    et_j["kind"] = "EnumType";
    et_j["name"] = et->name();
    return copy_to_parent(et_j, context);
}

void* JsonASTVisitor::visitFloatingLiteral(FloatingLiteral* lit, void* context)
{
    json lit_j;
    lit_j["kind"] = "FloatingLiteral";
    lit_j["value"] = lit->value();
    lit_j["special_value"] = lit->specialValue();
    lit_j["dtype"] = typeToJson(lit->type());
    lit_j["instr_addr"] = lit->instr_addr();
    return copy_to_parent(lit_j, context);
}

void* JsonASTVisitor::visitForStmt(ForStmt* fs, void* context)
{
    json fs_j;
    fs_j["kind"] = "ForStmt";
    fs_j["inner"] = json::array();
    fs_j["instr_addr"] = fs->instr_addr();
    return copy_to_parent(fs_j, context);
}

void* JsonASTVisitor::visitFunctionDecl(FunctionDecl* fd, void* context)
{
    // basic function node info
    json fdecl;
    fdecl["kind"] = "FunctionDecl";
    fdecl["id"] = fd->id();
    fdecl["inner"] = json::array();
    fdecl["name"] = fd->name();
    fdecl["address"] = fd->address();   // to_hex(fd->address());
    fdecl["is_intrinsic"] = fd->is_intrinsic();
    fdecl["return_dtype"] = typeToJson(fd->return_type());
    // fdecl["return_dtype_name"] = datatypeToJson(fd->return_type()->ghidra_dtype());
    return copy_to_parent(fdecl, context);
}

void* JsonASTVisitor::visitFunctionType(FunctionType* ftype, void* context)
{
    json ftype_j;
    ftype_j["kind"] = "FunctionType";
    ftype_j["inner"] = json::array();
    ftype_j["name"] = ftype->name();
    ftype_j["rdtype"] = typeToJson(ftype->return_type());
    return copy_to_parent(ftype_j, context);
}

void* JsonASTVisitor::visitGotoStmt(GotoStmt* gs, void* context)
{
    json gs_j;
    gs_j["kind"] = "GotoStmt";
    gs_j["label_name"] = gs->label_name();
    gs_j["instr_addr"] = gs->instr_addr();
    return copy_to_parent(gs_j, context);
}

void* JsonASTVisitor::visitIntegerLiteral(IntegerLiteral* lit, void* context)
{
    json int_lit;
    int_lit["kind"] = "IntegerLiteral";
    int_lit["value"] = lit->value();
    int_lit["dtype"] = typeToJson(lit->type());
    int_lit["instr_addr"] = lit->instr_addr();
    // int_lit["dtype_name"] = datatypeToJson(lit->type()->ghidra_dtype());
    return copy_to_parent(int_lit, context);
}

void* JsonASTVisitor::visitLabelStmt(LabelStmt* ls, void* context)
{
    json ls_j;
    ls_j["kind"] = "LabelStmt";
    ls_j["name"] = ls->name();
    ls_j["instr_addr"] = ls->instr_addr();
    return copy_to_parent(ls_j, context);
}

void* JsonASTVisitor::visitIfStmt(IfStmt* stmt, void* context)
{
    json ifstmt;
    ifstmt["kind"] = "IfStmt";
    ifstmt["inner"] = json::array();
    ifstmt["instr_addr"] = stmt->instr_addr();
    return copy_to_parent(ifstmt, context);
}

void* JsonASTVisitor::visitMemberExpr(MemberExpr* m, void* context)
{
    json m_j;
    m_j["kind"] = "MemberExpr";
    m_j["name"] = m->name();
    m_j["sid"] = m->sid();
    m_j["offset"] = m->offset();
    m_j["isArrow"] = m->isArrow();
    m_j["instr_addr"] = m->instr_addr();
    return copy_to_parent(m_j, context);
}

void* JsonASTVisitor::visitNullNode(NullNode* n, void* context)
{
    json n_j;
    n_j["kind"] = "NullNode";
    return copy_to_parent(n_j, context);
}

void* JsonASTVisitor::visitParenExpr(ParenExpr* pe, void* context)
{
    json paren;
    paren["kind"] = "ParenExpr";
    paren["inner"] = json::array();
    paren["instr_addr"] = pe->instr_addr();
    return copy_to_parent(paren, context);
}

void* JsonASTVisitor::visitFieldDecl(FieldDecl* fd, void* context)
{
    json fd_j;
    fd_j["kind"] = "FieldDecl";
    fd_j["name"] = fd->name();
    fd_j["dtype"] = typeToJson(fd->type());
    return copy_to_parent(fd_j, context);
}

void* JsonASTVisitor::visitRecordDecl(RecordDecl* rd, void* context)
{
    json rd_j;
    rd_j["kind"] = "RecordDecl";
    rd_j["sid"] = rd->sid();
    rd_j["name"] = rd->name();
    rd_j["is_union"] = rd->is_union();
    return copy_to_parent(rd_j, context);
}

void* JsonASTVisitor::visitParmVarDecl(ParmVarDecl* pv, void* context)
{
    json pvdecl;
    pvdecl["kind"] = "ParmVarDecl";
    pvdecl["id"] = pv->id();
    pvdecl["name"] = pv->name();
    pvdecl["dtype"] = typeToJson(pv->type());
    // pvdecl["dtype_name"] = datatypeToJson(pv->type()->ghidra_dtype());
    pvdecl["loc_space"] = pv->location().addr_space_name;
    if (pv->location().addr_space_name == "stack") {
        pvdecl["loc_off"] = (int64_t)pv->location().offset;
    } else {
        pvdecl["loc_off"] = pv->location().offset;
    }
    pvdecl["loc_reg"] = pv->location().register_name;
    return copy_to_parent(pvdecl, context);
}

void* JsonASTVisitor::visitPointerType(PointerType* pt, void* context)
{
    json pt_j;
    pt_j["kind"] = "PointerType";
    pt_j["inner"] = json::array();
    pt_j["size"] = pt->size();
    return copy_to_parent(pt_j, context);
}

void* JsonASTVisitor::visitStructType(StructType* st, void* context)
{
    json st_j;
    // use the proper kind to map to appropriate astlib type
    st_j["kind"] = st->is_union() ? "UnionType" : "StructType";
    st_j["sid"] = st->sid();
    st_j["name"] = st->name();
    return copy_to_parent(st_j, context);
}

void* JsonASTVisitor::visitVoidType(VoidType* vt, void* context)
{
    json vt_j;
    // vt_j["kind"] = "VoidType";

    // make VoidType a BuiltinType to match astlib
    vt_j["kind"] = "BuiltinType";
    vt_j["name"] = "void";
    vt_j["is_fp"] = false;
    vt_j["signed"] = false;
    vt_j["size"] = 0;
    return copy_to_parent(vt_j, context);
}

void* JsonASTVisitor::visitReturnStmt(ReturnStmt* rs, void* context)
{
    json rs_j;
    rs_j["kind"] = "ReturnStmt";
    rs_j["inner"] = json::array();
    rs_j["instr_addr"] = rs->instr_addr();
    return copy_to_parent(rs_j, context);
}

void* JsonASTVisitor::visitStringLiteral(StringLiteral* lit, void* context)
{
    json lit_j;
    lit_j["kind"] = "StringLiteral";
    lit_j["value"] = lit->value();
    lit_j["instr_addr"] = lit->instr_addr();
    return copy_to_parent(lit_j, context);
}

void* JsonASTVisitor::visitSwitchStmt(SwitchStmt* ss, void* context)
{
    json ss_j;
    ss_j["kind"] = "SwitchStmt";
    ss_j["inner"] = json::array();
    ss_j["instr_addr"] = ss->instr_addr();
    return copy_to_parent(ss_j, context);
}

json JsonASTVisitor::cppTemplatePlaceholderField(int size)
{
    json field_j;

    // { char[<size>] __template_placeholder__; }

    auto dtype = new ConstantArrayType(new BuiltinType("char", 1, false, true), size);

    field_j["name"] = "__template_placeholder__";
    field_j["offset"] = 0;
    field_j["dtype"] = typeToJson(dtype);

    delete dtype;

    return field_j;
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

json JsonASTVisitor::buildUnionFields(TypeUnion* ghidra_union)
{
    json fields = json::array();

    for (int i = 0; i < ghidra_union->numDepend(); i++) {
        const TypeField* union_field = ghidra_union->getField(i);
        // can't map offset -> field because all the offsets of a union
        // are zero and will collide!
        fields.push_back(structureFieldToJson(*union_field));
    }

    return fields;
}

json JsonASTVisitor::buildStructuresById(StructTypeLibrary* type_lib)
{
    json structs_by_id = json::object();

    for (auto stype : type_lib->getMappedStructs()) {
        string sid_str = std::to_string(stype.sid());
        json stype_j;

        if (stype.is_union()) {
            TypeUnion* ghidra_union = stype.ghidra_union();
            string name = ghidra_union->getName();
            auto size = ghidra_union->getSize();

            stype_j["name"] = name;
            stype_j["size"] = size;
            stype_j["is_union"] = stype.is_union();

            if (is_cpp_template_type(name)) {
                // don't define template contents! just a placeholder field
                // with total size of union/struct
                stype_j["fields"] = {{"0", cppTemplatePlaceholderField(size)}};
            } else {
                stype_j["fields"] = buildUnionFields(ghidra_union);
            }
        } else {
            TypeStruct* ghidra_struct = stype.ghidra_struct();
            string name = ghidra_struct->getName();
            auto size = ghidra_struct->getSize();

            stype_j["name"] = ghidra_struct->getName();
            stype_j["size"] = ghidra_struct->getSize();
            stype_j["is_union"] = stype.is_union();

            if (is_cpp_template_type(name)) {
                // don't define template contents! just a placeholder field
                // with total size of union/struct
                stype_j["fields"] = {{"0", cppTemplatePlaceholderField(size)}};
            } else {
                stype_j["fields"] = buildStructFields(ghidra_struct);
            }
        }
        structs_by_id[sid_str] = stype_j;
    }

    return structs_by_id;
}

void* JsonASTVisitor::visitTranslationUnitDecl(TranslationUnitDecl* td, void* context)
{
    json tudecl;
    tudecl["kind"] = "TranslationUnitDecl";
    tudecl["inner"] = json::array();
    // tudecl["structures_by_id"] = buildStructuresById(_builder->type_library());
    return copy_to_parent(tudecl, context);
}

void* JsonASTVisitor::visitType(Type* type, void* context)
{
    json type_j;
    type_j["kind"] = "Type";
    type_j["name"] = type->name();
    return copy_to_parent(type_j, context);
}

void* JsonASTVisitor::visitTypedefDecl(TypedefDecl* td, void* context)
{
    json td_j;
    td_j["kind"] = "TypedefDecl";
    td_j["name"] = td->name();
    td_j["inner"] = json::array();
    return copy_to_parent(td_j, context);
}

void* JsonASTVisitor::visitTypedefType(TypedefType* tdtype, void* context)
{
    json tdtype_j;
    tdtype_j["kind"] = "TypedefType";
    tdtype_j["name"] = tdtype->name();
    /** QUESTION: should we do anything with the decl? */
    return copy_to_parent(tdtype_j, context);
}

void* JsonASTVisitor::visitUnaryOperator(UnaryOperator* uo, void* context)
{
    json uo_json;
    uo_json["kind"] = "UnaryOperator";
    uo_json["inner"] = json::array();
    uo_json["opcode"] = uo->opcode();
    uo_json["instr_addr"] = uo->instr_addr();
    // uo_json["dtype"] = typeToJson(uo->type());
    // uo_json["dtype_name"] = datatypeToJson(uo->type()->ghidra_dtype());
    return copy_to_parent(uo_json, context);
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
    var_decl["dtype"] = typeToJson(vd->type());
    // var_decl["dtype_name"] = datatypeToJson(vd->type()->ghidra_dtype());
    var_decl["name"] = vd->name();
    var_decl["loc_space"] = vd->location().addr_space_name;
    if (vd->location().addr_space_name == "stack") {
        var_decl["loc_off"] = (int64_t)vd->location().offset;
    } else {
        var_decl["loc_off"] = vd->location().offset;
    }
    var_decl["loc_reg"] = vd->location().register_name;
    return copy_to_parent(var_decl, context);
}

void* JsonASTVisitor::visitWhileStmt(WhileStmt* ws, void* context)
{
    json ws_j;
    ws_j["kind"] = "WhileStmt";
    ws_j["instr_addr"] = ws->instr_addr();
    return copy_to_parent(ws_j, context);
}