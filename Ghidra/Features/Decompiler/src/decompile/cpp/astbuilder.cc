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

ASTBuilder::ASTBuilder()
    : PrintC(nullptr)
{
    // _ast_node_stack.push_back(fbody["inner"]);
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
        fdecl->addChild(new ParmVarDecl(fp.getParam(i)));
    }
}

/**
 * @brief Check if this symbol entry is an appropriate entry for creating
 * a local variable declaration. If so, create the corresponding VarDecl object
 * and return it. If not, return nullptr
 */
VarDecl* tryCreateLocalVarDecl(const SymbolEntry* sym_entry)
{
    if (sym_entry->isPiece())   // skip partial entry
        return nullptr;

    Symbol* sym = sym_entry->getSymbol();

    if (sym->getCategory() != -1)
        // skip parameters and "equates" (w/e that is)
        return nullptr;

    /** CLS: from other function, idk why we want to skip it if no name? */
    // if (sym->getName().size() == 0) continue;

    if (dynamic_cast<FunctionSymbol*>(sym) != nullptr)
        return nullptr;
    if (dynamic_cast<LabSymbol*>(sym) != nullptr) {
        return nullptr;
    }

    if (sym->isMultiEntry()) {
        if (sym->getFirstWholeMap() != sym_entry)
            return nullptr;        // Only emit the first SymbolEntry for declaration of multi-entry Symbol
    }

    return new VarDecl(sym);
}

void buildLocalDeclsFromScope(const Scope& scope, CompoundStmt* fbody)
{
    for (auto sym_entry : scope) {
        VarDecl* vdecl = tryCreateLocalVarDecl(sym_entry);
        if (vdecl) {
            DeclStmt* ds = new DeclStmt();
            ds->addChild(vdecl);
            fbody->addChild(ds);
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
        VarDecl* vdecl = tryCreateLocalVarDecl(sym_entry);
        if (vdecl) {
            DeclStmt* ds = new DeclStmt();
            ds->addChild(vdecl);
            fbody->addChild(ds);
        }
    }
}

json buildAstForFunction(Funcdata* fd)
{
    ASTBuilder builder;
    ASTNode* ast = builder.buildAST(fd);
    /** TODO: JSON AST visitor here... */
}

ASTNode* ASTBuilder::buildAST(Funcdata* fd)
{
    if (!fd->isProcStarted()) {
        // not decompiled
        stringstream ss;
        ss << "Function at 0x" << to_hex(fd->getAddress().getOffset());
        ss << " not decompiled";
        return new LogMsg(ss.str());
    } else if (fd->hasNoStructBlocks()) {
        // not fully decompiled, no structure present
        stringstream ss;
        ss << "Function at 0x" << to_hex(fd->getAddress().getOffset());
        ss << " not fully decompiled (no structure present)";
        return new LogMsg(ss.str());
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

    FunctionDecl* fdecl = new FunctionDecl(fd);

    // params are child nodes
    FuncProto& fp = fd->getFuncProto();
    buildFunctionParams(fp, fdecl);

    // -------- LOCAL DECLS
    CompoundStmt* fbody = new CompoundStmt();
    fdecl->addChild(fbody);

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
    // AstBuilder builder;     // before: builder(fbody)
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
    pushASTNode(fbody);
    emitBlockGraph(&fd->getStructure());
    popASTNode();

    return fdecl;
}

void ASTBuilder::pushASTNode(ASTNode* current_node)
{
    _ast_node_stack.push_back(current_node);
}

ASTNode* ASTBuilder::popASTNode()
{
    auto back = _ast_node_stack.back();
    _ast_node_stack.pop_back();
    return back;
}

void ASTBuilder::emitExpression(const PcodeOp *op)
{
    const Varnode* outvn = op->getOut();
    if (outvn) {
        /*
            TODO: handle inplace ops (x += 3) if
            PrintC::option_inplace_ops is set
         */
        BinaryOperator* assignment = new BinaryOperator("=");
        currentASTNode()->addChild(assignment);

        /**
         * BinaryOperator
         * ----
         * > first child => first operand, second child => second operand
         * > for assignment, first child => LHS, second child => RHS
         */

        /** TODO: handle LHS of assignment expression... */

        /** NOTE: I think we may need both a "context" stack with
         * the current AST node as well as a "todo" stack with the
         * list of remaining sub-expressions (like LHS, RHS) that
         * need to be processed
        */

        // pushAstNode()/popAstNode()?
        // this->pushAstNode()

    } else if (op->doesSpecialPrinting()) {
        /** TODO: what changes here? */
    }

    /** TODO: RHS if present/main expression based on opcode */
}

void ASTBuilder::emitBlockBasic(const BlockBasic *bb)
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

void ASTBuilder::emitBlockGraph(const BlockGraph *bl)
{
    // const vector<FlowBlock *> &list(bl->getList());
    // auto list = bl->getList()

    for (FlowBlock* fb : bl->getList()) {
        fb->emit(this);
    }
}

void ASTBuilder::emitBlockCopy(const BlockCopy *bl)
{
    // here
    FlowBlock* sub = bl->subBlock(0);
    sub->emit(this);
}

void ASTBuilder::emitBlockGoto(const BlockGoto *bl)
{

}

void ASTBuilder::emitBlockLs(const BlockList *bl)
{
    for (int i = 0; i < bl->getSize(); i++) {
        /** NOTE: maybe these need to be sibling entries in the json? */
        bl->getBlock(i)->emit(this);
    }
}

void ASTBuilder::emitBlockCondition(const BlockCondition *bl)
{

}

void ASTBuilder::emitBlockIf(const BlockIf *bl)
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

void ASTBuilder::emitBlockWhileDo(const BlockWhileDo *bl)
{

}

void ASTBuilder::emitBlockDoWhile(const BlockDoWhile *bl)
{

}

void ASTBuilder::emitBlockInfLoop(const BlockInfLoop *bl)
{

}

void ASTBuilder::emitBlockSwitch(const BlockSwitch *bl)
{

}
