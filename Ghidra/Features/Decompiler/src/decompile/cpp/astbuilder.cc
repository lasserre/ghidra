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

class AstBuilder : public PrintC
{
public:
    AstBuilder()
        : PrintC(nullptr)
    { }

    /**
     * This is the BlockVisitor interface inside PrintLanguage, but it is not
     * broken out into its own interface...so I have to inherit from PrintC to
     * avoid implementing all the other pure virtual functions I don't care about
     */
    virtual void emitBlockBasic(const BlockBasic *bb);
    virtual void emitBlockGraph(const BlockGraph *bl);
    virtual void emitBlockCopy(const BlockCopy *bl);
    virtual void emitBlockGoto(const BlockGoto *bl);
    virtual void emitBlockLs(const BlockList *bl);
    virtual void emitBlockCondition(const BlockCondition *bl);
    virtual void emitBlockIf(const BlockIf *bl);
    virtual void emitBlockWhileDo(const BlockWhileDo *bl);
    virtual void emitBlockDoWhile(const BlockDoWhile *bl);
    virtual void emitBlockInfLoop(const BlockInfLoop *bl);
    virtual void emitBlockSwitch(const BlockSwitch *bl);
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
 * @param arr The json array to insert function parameters into (FuncDecl.children)
 */
void buildFunctionParams(FuncProto& fp, json* arr)
{
    /** NOTE: parameters need to be IN ORDER within the list of children */
    for (int i = 0; i < fp.numParams(); i++) {
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

    json fdecl;
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

    // basic function node info
    fdecl["kind"] = "FunctionDecl";
    fdecl["inner"] = json::array();
    fdecl["name"] = fd->getName();
    fdecl["address"] = to_hex(fd->getAddress().getOffset());

    // prototype
    FuncProto& fp = fd->getFuncProto();
    fdecl["return_dtype"] = datatype_to_json(fp.getOutputType());
    buildFunctionParams(fp, &fdecl["inner"]);

    // -------- FUNCTION BODY
    json fbody;
    fbody["kind"] = "CompoundStmt";
    fbody["inner"] = json::array();

    // -------- LOCAL DECLS
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
    AstBuilder builder;
    builder.emitBlockGraph(&fd->getStructure());

    fdecl["inner"].push_back(fbody);
    return fdecl;
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
        int offset = instr->getAddr().getOffset();
        // TODO: finish this...
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
