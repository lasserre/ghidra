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

struct PendingNode
{
    PendingNode() : vn(nullptr), op(nullptr), sym(nullptr)
    { }

    // for NodePending case:
    Varnode* vn;    // implied varnode
    PcodeOp* op;    // operator consuming value from implied varnode

    // for LHS case:
    Symbol* sym;    // for now, if != nullptr this is valid
};

struct PendingExpr
{
    PendingExpr() : ast_op(nullptr), parts()
    { }

    ~PendingExpr()
    {
        // don't delete ast_op - this is part of the AST
        for (auto p : parts) {
            delete p;
        }
    }

    ASTNode* ast_op;
    vector<PendingNode*> parts;
};

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

    delete ast;

    return json();  /** TEMP: replace w/ JSON visitor! */
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

/** TODO: processPendingNode() */

// or processPendingExpressions()
void ASTBuilder::processExpressionStack()
{
    /** TODO: implement this */
    while (!_pending_expressions.empty()) {
        PendingExpr* expr = _pending_expressions.back();
        _pending_expressions.pop_back();

        pushASTNode(expr->ast_op);

        for (PendingNode* node : expr->parts) {
            /** TODO: process pending node */
        }

        popASTNode();
    }
}

PendingNode* ASTBuilder::buildNodeLHS(const Varnode* vn)
{
    PendingNode* lhs = new PendingNode();

    HighVariable* hv = vn->getHigh();
    Symbol* sym = hv->getSymbol();
    if (sym) {
        auto sym_offset = hv->getSymbolOffset();
        if (sym_offset == -1) {
            // perfect match
            lhs->sym = sym;
        } else if (sym_offset + vn->getSize() <= sym->getType()->getSize()) {
            // partial: STRUCT FIELDS/ARRAYS!
            pushASTNode(nullptr);  // REMOVE: dummy instruction for breakpoint
        } else {
            // mismatch
            pushASTNode(nullptr);  // REMOVE: dummy instruction for breakpoint
        }
    } else {
        // pushUnnamedLocation
        pushASTNode(nullptr);  // REMOVE: dummy instruction for breakpoint
    }

    return lhs;
}

void ASTBuilder::emitExpression(const PcodeOp *op)
{
    /**
     * TODO: now this needs to just "set up" the expression
     * stack, sit back, and let it run...
    */

    PendingExpr* expr = nullptr;

    const Varnode* outvn = op->getOut();
    if (outvn) {
        /*
            TODO: handle inplace ops (x += 3) if
            PrintC::option_inplace_ops is set
         */

        BinaryOperator* assignment = new BinaryOperator("=");
        currentASTNode()->addChild(assignment);     // links to AST

        /**
         * BinaryOperator
         * ----
         * > first child => first operand, second child => second operand
         * > for assignment, first child => LHS, second child => RHS
         */
        expr = new PendingExpr();
        expr->ast_op = assignment;
        expr->parts.push_back(buildNodeLHS(outvn));

    } else if (op->doesSpecialPrinting()) {
        /** TODO: what changes here? */
        pushASTNode(nullptr);  // REMOVE: dummy instruction for breakpoint
    }

    if (!expr) {
        // create a "blank" expression for the top of the expression stack, so
        // the getOpcode->push() call (which calls our opXXX() method) can simply
        // access the top of the expression stack (and for this case it will
        // init expr->ast_op appropriately)
        expr = new PendingExpr();
    }

    _pending_expressions.push_back(expr);

    /**
     * THOUGHT: I think what we need to do here is push expr to the stack
     * regardless...either the initial LHS version, or a new PendingExpr()
     * with nothing filled in.
     *
     * Then when push() calls opXyz(), that ASTBuilder member function can
     * access the expression on top of the stack and ADD itself to the
     * expression's parts list.
     *      a) For LHS expr case: the LHS already exists, we now push the RHS
     *      b) For "unary" case: no parts exist, we push the single PendingNode
     */

    /** PICK UP HERE:
     * - debug this, follow the logic (and finish implementing)
     * - look at Ghidra window w/ both decompiled C and PCode visible
     *   so you can see which ops are implied, explicit, LHS, etc...
    */

    op->getOpcode()->push(this, op, nullptr);

    processExpressionStack();
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

/**
 * ----------------------------------------------------------
 * OP implementations
 * ----------------------------------------------------------
*/

void ASTBuilder::opCopy(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opCopy");
}

void ASTBuilder::opLoad(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opLoad");
}

void ASTBuilder::opStore(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opStore");
}

void ASTBuilder::opBranch(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opBranch");
}

void ASTBuilder::opCbranch(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opCbranch");
}

void ASTBuilder::opBranchind(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opBranchind");
}

void ASTBuilder::opCall(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opCall");
}

void ASTBuilder::opCallind(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opCallind");
}

void ASTBuilder::opCallother(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opCallother");
}

void ASTBuilder::opConstructor(const PcodeOp *op,bool withNew)
{
    throw new std::exception("Unimplemented OP opConstructor");
}

void ASTBuilder::opReturn(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opReturn");
}

void ASTBuilder::opIntEqual(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opIntEqual");
}

void ASTBuilder::opIntNotEqual(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opIntNotEqual");
}

void ASTBuilder::opIntSless(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opIntSless");
}

void ASTBuilder::opIntSlessEqual(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opIntSlessEqual");
}

void ASTBuilder::opIntLess(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opIntLess");
}

void ASTBuilder::opIntLessEqual(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opIntLessEqual");
}

void ASTBuilder::opIntZext(const PcodeOp *op,const PcodeOp *readOp)
{
    throw new std::exception("Unimplemented OP opIntZext");
}

void ASTBuilder::opIntSext(const PcodeOp *op,const PcodeOp *readOp)
{
    throw new std::exception("Unimplemented OP opIntSext");
}

void ASTBuilder::opIntAdd(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opIntAdd");
}

void ASTBuilder::opIntSub(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opIntSub");
}

void ASTBuilder::opIntCarry(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opIntCarry");
}

void ASTBuilder::opIntScarry(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opIntScarry");
}

void ASTBuilder::opIntSborrow(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opIntSborrow");
}

void ASTBuilder::opInt2Comp(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opInt2Comp");
}

void ASTBuilder::opIntNegate(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opIntNegate");
}

void ASTBuilder::opIntXor(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opIntXor");
}

void ASTBuilder::opIntAnd(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opIntAnd");
}

void ASTBuilder::opIntOr(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opIntOr");
}

void ASTBuilder::opIntLeft(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opIntLeft");
}

void ASTBuilder::opIntRight(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opIntRight");
}

void ASTBuilder::opIntSright(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opIntSright");
}

void ASTBuilder::opIntMult(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opIntMult");
}

void ASTBuilder::opIntDiv(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opIntDiv");
}

void ASTBuilder::opIntSdiv(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opIntSdiv");
}

void ASTBuilder::opIntRem(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opIntRem");
}

void ASTBuilder::opIntSrem(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opIntSrem");
}

void ASTBuilder::opBoolNegate(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opBoolNegate");
}

void ASTBuilder::opBoolXor(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opBoolXor");
}

void ASTBuilder::opBoolAnd(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opBoolAnd");
}

void ASTBuilder::opBoolOr(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opBoolOr");
}

void ASTBuilder::opFloatEqual(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opFloatEqual");
}

void ASTBuilder::opFloatNotEqual(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opFloatNotEqual");
}

void ASTBuilder::opFloatLess(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opFloatLess");
}

void ASTBuilder::opFloatLessEqual(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opFloatLessEqual");
}

void ASTBuilder::opFloatNan(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opFloatNan");
}

void ASTBuilder::opFloatAdd(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opFloatAdd");
}

void ASTBuilder::opFloatDiv(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opFloatDiv");
}

void ASTBuilder::opFloatMult(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opFloatMult");
}

void ASTBuilder::opFloatSub(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opFloatSub");
}

void ASTBuilder::opFloatNeg(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opFloatNeg");
}

void ASTBuilder::opFloatAbs(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opFloatAbs");
}

void ASTBuilder::opFloatSqrt(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opFloatSqrt");
}

void ASTBuilder::opFloatInt2Float(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opFloatInt2Float");
}

void ASTBuilder::opFloatFloat2Float(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opFloatFloat2Float");
}

void ASTBuilder::opFloatTrunc(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opFloatTrunc");
}

void ASTBuilder::opFloatCeil(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opFloatCeil");
}

void ASTBuilder::opFloatFloor(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opFloatFloor");
}

void ASTBuilder::opFloatRound(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opFloatRound");
}

void ASTBuilder::opMultiequal(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opMultiequal");
}

void ASTBuilder::opIndirect(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opIndirect");
}

void ASTBuilder::opPiece(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opPiece");
}

void ASTBuilder::opSubpiece(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opSubpiece");
}

void ASTBuilder::opCast(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opCast");
}

void ASTBuilder::opPtradd(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opPtradd");
}

void ASTBuilder::opPtrsub(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opPtrsub");
}

void ASTBuilder::opSegmentOp(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opSegmentOp");
}

void ASTBuilder::opCpoolRefOp(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opCpoolRefOp");
}

void ASTBuilder::opNewOp(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opNewOp");
}

void ASTBuilder::opInsertOp(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opInsertOp");
}

void ASTBuilder::opExtractOp(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opExtractOp");
}

void ASTBuilder::opPopcountOp(const PcodeOp *op)
{
    throw new std::exception("Unimplemented OP opPopcountOp");
}
