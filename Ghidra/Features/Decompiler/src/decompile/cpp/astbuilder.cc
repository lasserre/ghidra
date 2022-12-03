#include "astbuilder.h"

#include "funcdata.hh"
#include "printc.hh"

/**
 * BlockBasic was so close...but named their iterators beginOp/endOp.
 * So this simply wraps BlockBasic to enable range-based for loops :)
 */
class getPcodeOps
{
public:
    getPcodeOps(const BlockBasic* bb)
        : _bb(bb)
    { }

    list<PcodeOp*>::const_iterator begin() const { return _bb->beginOp(); }
    list<PcodeOp*>::const_iterator end() const { return _bb->endOp(); }

    const BlockBasic* _bb;
};

template<typename T> string to_hex(T data)
{
    stringstream ss;
    ss << hex << data;
    return ss.str();
}

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

/**
 * @brief Build
 *
 * @param fp The function prototype to build parameters from
 * @param fdecl The function to insert parameters into
 */
void buildFunctionParams(FuncProto& fp, FunctionDecl* fdecl)
{
    /** NOTE: parameters need to be IN ORDER within the list of children */
    for (int i = 0; i < fp.numParams(); i++) {

        /** TODO: pick up here w/ ParmVarDecl instance */

        json pvdecl;
        pvdecl["kind"] = "ParmVarDecl";

        ProtoParameter* param = fp.getParam(i);
        Symbol* sym = param->getSymbol();

        if (sym) {
            pvdecl["dtype"] = datatype_to_json(sym->getType());
            pvdecl["name"] = sym->getName();
        } else {
            pvdecl["dtype"] = datatype_to_json(param->getType());
        }

        // fdecl->children()->push_back()
        arr->push_back(pvdecl);
    }
}

/**
 * @brief Check if this symbol entry is an appropriate entry for creating
 * a local variable declaration. If so, return true and create the corresponding
 * VarDecl JSON object in var_decl. If not, return false.
 */
bool tryCreateLocalVarDecl(const SymbolEntry* sym_entry, json& var_decl)
{
    if (sym_entry->isPiece())   // skip partial entry
        return false;

    Symbol* sym = sym_entry->getSymbol();

    if (sym->getCategory() != -1)
        // skip parameters and "equates" (w/e that is)
        return false;

    /** CLS: from other function, idk why we want to skip it if no name? */
    // if (sym->getName().size() == 0) continue;

    if (dynamic_cast<FunctionSymbol*>(sym) != nullptr)
        return false;
    if (dynamic_cast<LabSymbol*>(sym) != nullptr) {
        return false;
    }

    if (sym->isMultiEntry()) {
        if (sym->getFirstWholeMap() != sym_entry)
            return false;        // Only emit the first SymbolEntry for declaration of multi-entry Symbol
    }

    var_decl["kind"] = "VarDecl";
    var_decl["dtype"] = datatype_to_json(sym->getType());
    var_decl["name"] = sym->getName();
    return true;
}

void buildLocalDeclsFromScope(const Scope& scope, json& fbody)
{
    for (auto sym_entry : scope) {
        json var_decl;
        if (tryCreateLocalVarDecl(sym_entry, var_decl)) {
            json declstmt;
            declstmt["kind"] = "DeclStmt";
            declstmt["inner"] = json::array();
            declstmt["inner"].push_back(var_decl);
            fbody["inner"].push_back(declstmt);
        }
    }

    /**
     * Iterate over "dynamic symbols"
     *
     * According to database.hh, dynamic symbols are represented by (stored in?)
     * constants or temporary registers vs. addresses as usual
     */
    list<SymbolEntry>::const_iterator iter_d = scope.beginDynamic();
    list<SymbolEntry>::const_iterator enditer_d = scope.endDynamic();
    for (; iter_d!=enditer_d; ++iter_d) {
        const SymbolEntry *sym_entry = &(*iter_d);
        json var_decl;
        if (tryCreateLocalVarDecl(sym_entry, var_decl)) {
            json declstmt;
            declstmt["kind"] = "DeclStmt";
            declstmt["inner"] = json::array();
            declstmt["inner"].push_back(var_decl);
            fbody["inner"].push_back(declstmt);
        }
    }
}

json buildAstForFunction(Funcdata* fd)
{
    if (!fd->isProcStarted()) {
        // not decompiled
        stringstream ss;
        ss << "Function at 0x" << to_hex(fd->getAddress().getOffset());
        ss << " not decompiled";
        return json{ss.str()};
    } else if (fd->hasNoStructBlocks()) {
        // not fully decompiled, no structure present
        stringstream ss;
        ss << "Function at 0x" << to_hex(fd->getAddress().getOffset());
        ss << " not fully decompiled (no structure present)";
        return json{ss.str()};
    }

    // json fdecl;

    // rough algorithm copied from PrintC::docFunction
    // emit->beginFunction(fd);
    // emitFunctionDeclaration(fd);    // Causes us to enter function's scope
    // emitLocalVarDecls(fd);
    // if (isSet(flat))
    //   emitBlockGraph(&fd->getBasicBlocks());
    // else
    //   emitBlockGraph(&fd->getStructure());
    // popScope();                // Exit function's scope
    // emit->endFunction(id1);



    /** TODO: pick up here and replace JSON w/ FunctionDecl node
     *
     * NOTE: ...remember to MOVE the JSON to where we will create the
     * JSON ASTVisitor class...we still need this, just not here!
    */

    // parent=null since this is the head of AST
    FunctionDecl* fdecl = new FunctionDecl(nullptr, fd);

    // params are child nodes
    FuncProto& fp = fd->getFuncProto();
    buildFunctionParams(fp, &fdecl["inner"]);

    // -------- LOCAL DECLS
    json fbody;
    fbody["kind"] = "CompoundStmt";
    fbody["inner"] = json::array();

    // locals (main scope)
    ScopeLocal& const scope = *fd->getScopeLocal();
    buildLocalDeclsFromScope(scope, fbody);
    // locals from nested scopes
    ScopeMap::const_iterator iter = fd->getScopeLocal()->childrenBegin();
    ScopeMap::const_iterator enditer = fd->getScopeLocal()->childrenEnd();
    for (; iter!=enditer; iter++) {
        Scope *l1 = (*iter).second;
        buildLocalDeclsFromScope(*l1, fbody);
    }

    // -------- FUNCTION CODE
    AstBuilder builder;     // before: builder(fbody)
    /** NOTE: before, this was part of builder's constructor */
    // _ast_node_stack({&fbody["inner"]})

    /**
     * TODO: change this to builder.buildAST()
     * (which can call emitBlockGraph, etc.)
     * TODO: remove json-isms from ASTBuilder, move them to an ASTVisitor
     * that converts the AST to JSON
     * TODO: call the JSON ASTVisitor on the resulting AST after the builder
     * has completed building
     * TODO: pick up where I left off at emitExpression
    */
    builder.emitBlockGraph(&fd->getStructure());

    fdecl["inner"].push_back(fbody);
    return fdecl;
}

void AstBuilder::pushASTNode(ASTNode* current_node)
{
    _ast_node_stack.push_back(current_node);
}

ASTNode* AstBuilder::popASTNode()
{
    auto back = _ast_node_stack.back();
    _ast_node_stack.pop_back();
    return back;
}

void AstBuilder::emitExpression(const PcodeOp *op)
{
    const Varnode* outvn = op->getOut();
    if (outvn) {
        /*
            TODO: handle inplace ops (x += 3) if
            PrintC::option_inplace_ops is set
         */
        json assignment;
        assignment["kind"] = "BinaryOperator";
        assignment["opcode"] = "=";
        // basing dtype on outvn type for now
        assignment["dtype"] = datatype_to_json(op->getOut()->getType());
        assignment["inner"] = json::array();

        /**
         * BinaryOperator
         * ----
         * > first child => first operand, second child => second operand
         * > for assignment, first child => LHS, second child => RHS
         */

        /** TODO: handle LHS/RHS of assignment expression... */

        // pushAstNode()/popAstNode()?
        // this->pushAstNode()

        currentNode()->push_back(assignment);

    } else if (op->doesSpecialPrinting()) {
        /** TODO: what changes here? */
    }

    /** TODO: RHS if present/main expression based on opcode */
}

void AstBuilder::emitBlockBasic(const BlockBasic *bb)
{
    /** TODO: emit this! */
    // CLS: I know this actually gets called 2x, one with no_branch
    // and again with only_branch...but can I simply emit ALL of the
    // basic block in one go? (just do all the ops in order!)
        // - maybe this is problematic "structurally" with the last
        //   op being the if conditional block?
        // - but maybe I can make it work?

    for (PcodeOp* instr : getPcodeOps(bb)) {
        // int offset = instr->getAddr().getOffset();
        const Varnode* vn = instr->getOut();
        if (vn && vn->isImplied()) {
            // skip Pcode instruction with implied result
            continue;
        }
        emitExpression(instr);
    }
}

void AstBuilder::emitBlockGraph(const BlockGraph *bl)
{
    // const vector<FlowBlock *> &list(bl->getList());
    // auto list = bl->getList()

    for (FlowBlock* fb : bl->getList()) {
        fb->emit(this);
    }
}

void AstBuilder::emitBlockCopy(const BlockCopy *bl)
{
    // here
    FlowBlock* sub = bl->subBlock(0);
    sub->emit(this);
}

void AstBuilder::emitBlockGoto(const BlockGoto *bl)
{

}

void AstBuilder::emitBlockLs(const BlockList *bl)
{
    for (int i = 0; i < bl->getSize(); i++) {
        /** NOTE: maybe these need to be sibling entries in the json? */
        bl->getBlock(i)->emit(this);
    }
}

void AstBuilder::emitBlockCondition(const BlockCondition *bl)
{

}

void AstBuilder::emitBlockIf(const BlockIf *bl)
{
    FlowBlock* condBlock = bl->getBlock(0);
    // setMod(no_branch);
    condBlock->emit(this);
    // setMod(only_branch);
    // condBlock->emit(this);

    if (bl->getGotoTarget() != nullptr) {
        /** TODO: emit goto statement */
    } else {
        auto trueBlk = bl->getBlock(1);
        trueBlk->emit(this);
        if (bl->getSize() > 2) {
            auto elseBlk = bl->getBlock(2);
            elseBlk->emit(this);
        }
    }
}

void AstBuilder::emitBlockWhileDo(const BlockWhileDo *bl)
{

}

void AstBuilder::emitBlockDoWhile(const BlockDoWhile *bl)
{

}

void AstBuilder::emitBlockInfLoop(const BlockInfLoop *bl)
{

}

void AstBuilder::emitBlockSwitch(const BlockSwitch *bl)
{

}
