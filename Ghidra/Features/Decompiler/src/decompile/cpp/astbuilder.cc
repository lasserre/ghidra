#include "astbuilder.h"

#include "funcdata.hh"
#include "printc.hh"

static int _num_unimpl_ops = 0;
void unimplementedOp(std::string opname)
{
    // this is purely for debugging so I don't have to throw exceptions
    // and kill my debug session right now :)

    // either log this or set a breakpoint here to catch any unimplemented
    // ops that are involved in an expression. If I don't care, I can move on
    _num_unimpl_ops++;
}

static int _count_unimpl_stuff = 0;
void unimplementedCode(std::string description)
{
    // again purely for debugging to avoid exceptions...

    // log this, set breakpoint or ignore
    _count_unimpl_stuff++;
}

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
 * @brief Describes type of pending node.
 *
 * As I understand more about what types of pending nodes we will see and how
 * we must construct them, I may discover a better method of doing this than
 * an enum. For now I don't know and don't want to slow down...so starting out
 * by storing the type of pending node so it's available if I need to differentiate
 * when we process pending nodes
 */
enum ePendingNodeType {
    invalid = 0,
    symbol_node = 1,
};

struct PendingNode
{
    PendingNode()
    {
        node_type = ePendingNodeType::invalid;
        vn = nullptr;
        op = nullptr;
        sym = nullptr;
    }

    ePendingNodeType node_type;
    const Varnode* vn;
    const PcodeOp* op;
    Symbol* sym;
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

/**
 * @brief Process a pending node, adding a new/corresponding ASTNode to the
 * AST as well as pushing a new PendingExpression to the expression stack
 * if this node is a non-terminal.
 *
 * If this node is a terminal node, no PendingExpression is required.
 *
 * @param node
 */
void ASTBuilder::processPendingNode(PendingNode* node)
{
    switch (node->node_type) {
        case ePendingNodeType::symbol_node:
            processPendingSymbol(node);
            break;
        case ePendingNodeType::invalid:
            unimplementedCode("processPendingNode: node_type invalid");
            break;
        default:
            unimplementedCode("processPendingNode: UNHANDLED node_type");
            break;
    }
}

void ASTBuilder::processPendingSymbol(PendingNode* node)
{
    /** TODO: create DeclRefExpr ASTNode */

    unimplementedCode("processPendingSymbol");
}

// or processPendingExpressions()
void ASTBuilder::processExpressionStack()
{
    while (!_pending_expressions.empty()) {
        PendingExpr* expr = _pending_expressions.back();
        _pending_expressions.pop_back();

        pushASTNode(expr->ast_op);

        for (PendingNode* node : expr->parts) {
            processPendingNode(node);
        }

        popASTNode();
    }
}

PendingNode* ASTBuilder::buildNodeImplied(const Varnode* vn, const PcodeOp* op)
{
    PendingNode* node = new PendingNode();

    node->op = op;
    node->vn = vn;
    node->sym = nullptr;

    return node;
}

PendingNode* ASTBuilder::buildNodeLHS(const Varnode* vn, const PcodeOp* op)
{
    PendingNode* lhs = new PendingNode();

    HighVariable* hv = vn->getHigh();
    Symbol* sym = hv->getSymbol();
    if (sym) {
        auto sym_offset = hv->getSymbolOffset();
        if (sym_offset == -1) {
            // perfect match
            lhs->node_type = ePendingNodeType::symbol_node;
            lhs->sym = sym;
            lhs->vn = vn;
            lhs->op = op;
        } else if (sym_offset + vn->getSize() <= sym->getType()->getSize()) {
            // partial: STRUCT FIELDS/ARRAYS!
            unimplementedCode("buildNodeLHS: STRUCT FIELD/ARRAY");
        } else {
            // mismatch
            unimplementedCode("buildNodeLHS: mismatch symbol");
        }
    } else {
        // pushUnnamedLocation
        unimplementedCode("buildNodeLHS: unnamed location");
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
        expr->parts.push_back(buildNodeLHS(outvn, op));

    } else if (op->doesSpecialPrinting()) {
        /** TODO: what changes here? */
        unimplementedCode("emitExpression: doesSpecialPrinting");
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

    // to look at Pcode instructions:
    // op->getOpcode()->opcode
    // op->getAddr().getOffset(),x

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
    PendingExpr* expr = _pending_expressions.back();
    expr->parts.push_back(buildNodeImplied(op->getIn(0), op));

    if (expr->ast_op == nullptr) {
        throw new std::exception("opCopy: handle NULL ast_op");
    }
}

void ASTBuilder::opLoad(const PcodeOp *op)
{
    unimplementedOp("opLoad");
}

void ASTBuilder::opStore(const PcodeOp *op)
{
    unimplementedOp("opStore");
}

void ASTBuilder::opBranch(const PcodeOp *op)
{
    unimplementedOp("opBranch");
}

void ASTBuilder::opCbranch(const PcodeOp *op)
{
    unimplementedOp("opCbranch");
}

void ASTBuilder::opBranchind(const PcodeOp *op)
{
    unimplementedOp("opBranchind");
}

void ASTBuilder::opCall(const PcodeOp *op)
{
    unimplementedOp("opCall");
}

void ASTBuilder::opCallind(const PcodeOp *op)
{
    unimplementedOp("opCallind");
}

void ASTBuilder::opCallother(const PcodeOp *op)
{
    unimplementedOp("opCallother");
}

void ASTBuilder::opConstructor(const PcodeOp *op,bool withNew)
{
    unimplementedOp("opConstructor");
}

void ASTBuilder::opReturn(const PcodeOp *op)
{
    unimplementedOp("opReturn");
}

void ASTBuilder::opIntEqual(const PcodeOp *op)
{
    unimplementedOp("opIntEqual");
}

void ASTBuilder::opIntNotEqual(const PcodeOp *op)
{
    unimplementedOp("opIntNotEqual");
}

void ASTBuilder::opIntSless(const PcodeOp *op)
{
    unimplementedOp("opIntSless");
}

void ASTBuilder::opIntSlessEqual(const PcodeOp *op)
{
    unimplementedOp("opIntSlessEqual");
}

void ASTBuilder::opIntLess(const PcodeOp *op)
{
    unimplementedOp("opIntLess");
}

void ASTBuilder::opIntLessEqual(const PcodeOp *op)
{
    unimplementedOp("opIntLessEqual");
}

void ASTBuilder::opIntZext(const PcodeOp *op,const PcodeOp *readOp)
{
    unimplementedOp("opIntZext");
}

void ASTBuilder::opIntSext(const PcodeOp *op,const PcodeOp *readOp)
{
    unimplementedOp("opIntSext");
}

void ASTBuilder::opIntAdd(const PcodeOp *op)
{
    unimplementedOp("opIntAdd");
}

void ASTBuilder::opIntSub(const PcodeOp *op)
{
    unimplementedOp("opIntSub");
}

void ASTBuilder::opIntCarry(const PcodeOp *op)
{
    unimplementedOp("opIntCarry");
}

void ASTBuilder::opIntScarry(const PcodeOp *op)
{
    unimplementedOp("opIntScarry");
}

void ASTBuilder::opIntSborrow(const PcodeOp *op)
{
    unimplementedOp("opIntSborrow");
}

void ASTBuilder::opInt2Comp(const PcodeOp *op)
{
    unimplementedOp("opInt2Comp");
}

void ASTBuilder::opIntNegate(const PcodeOp *op)
{
    unimplementedOp("opIntNegate");
}

void ASTBuilder::opIntXor(const PcodeOp *op)
{
    unimplementedOp("opIntXor");
}

void ASTBuilder::opIntAnd(const PcodeOp *op)
{
    unimplementedOp("opIntAnd");
}

void ASTBuilder::opIntOr(const PcodeOp *op)
{
    unimplementedOp("opIntOr");
}

void ASTBuilder::opIntLeft(const PcodeOp *op)
{
    unimplementedOp("opIntLeft");
}

void ASTBuilder::opIntRight(const PcodeOp *op)
{
    unimplementedOp("opIntRight");
}

void ASTBuilder::opIntSright(const PcodeOp *op)
{
    unimplementedOp("opIntSright");
}

void ASTBuilder::opIntMult(const PcodeOp *op)
{
    unimplementedOp("opIntMult");
}

void ASTBuilder::opIntDiv(const PcodeOp *op)
{
    unimplementedOp("opIntDiv");
}

void ASTBuilder::opIntSdiv(const PcodeOp *op)
{
    unimplementedOp("opIntSdiv");
}

void ASTBuilder::opIntRem(const PcodeOp *op)
{
    unimplementedOp("opIntRem");
}

void ASTBuilder::opIntSrem(const PcodeOp *op)
{
    unimplementedOp("opIntSrem");
}

void ASTBuilder::opBoolNegate(const PcodeOp *op)
{
    unimplementedOp("opBoolNegate");
}

void ASTBuilder::opBoolXor(const PcodeOp *op)
{
    unimplementedOp("opBoolXor");
}

void ASTBuilder::opBoolAnd(const PcodeOp *op)
{
    unimplementedOp("opBoolAnd");
}

void ASTBuilder::opBoolOr(const PcodeOp *op)
{
    unimplementedOp("opBoolOr");
}

void ASTBuilder::opFloatEqual(const PcodeOp *op)
{
    unimplementedOp("opFloatEqual");
}

void ASTBuilder::opFloatNotEqual(const PcodeOp *op)
{
    unimplementedOp("opFloatNotEqual");
}

void ASTBuilder::opFloatLess(const PcodeOp *op)
{
    unimplementedOp("opFloatLess");
}

void ASTBuilder::opFloatLessEqual(const PcodeOp *op)
{
    unimplementedOp("opFloatLessEqual");
}

void ASTBuilder::opFloatNan(const PcodeOp *op)
{
    unimplementedOp("opFloatNan");
}

void ASTBuilder::opFloatAdd(const PcodeOp *op)
{
    unimplementedOp("opFloatAdd");
}

void ASTBuilder::opFloatDiv(const PcodeOp *op)
{
    unimplementedOp("opFloatDiv");
}

void ASTBuilder::opFloatMult(const PcodeOp *op)
{
    unimplementedOp("opFloatMult");
}

void ASTBuilder::opFloatSub(const PcodeOp *op)
{
    unimplementedOp("opFloatSub");
}

void ASTBuilder::opFloatNeg(const PcodeOp *op)
{
    unimplementedOp("opFloatNeg");
}

void ASTBuilder::opFloatAbs(const PcodeOp *op)
{
    unimplementedOp("opFloatAbs");
}

void ASTBuilder::opFloatSqrt(const PcodeOp *op)
{
    unimplementedOp("opFloatSqrt");
}

void ASTBuilder::opFloatInt2Float(const PcodeOp *op)
{
    unimplementedOp("opFloatInt2Float");
}

void ASTBuilder::opFloatFloat2Float(const PcodeOp *op)
{
    unimplementedOp("opFloatFloat2Float");
}

void ASTBuilder::opFloatTrunc(const PcodeOp *op)
{
    unimplementedOp("opFloatTrunc");
}

void ASTBuilder::opFloatCeil(const PcodeOp *op)
{
    unimplementedOp("opFloatCeil");
}

void ASTBuilder::opFloatFloor(const PcodeOp *op)
{
    unimplementedOp("opFloatFloor");
}

void ASTBuilder::opFloatRound(const PcodeOp *op)
{
    unimplementedOp("opFloatRound");
}

void ASTBuilder::opMultiequal(const PcodeOp *op)
{
    unimplementedOp("opMultiequal");
}

void ASTBuilder::opIndirect(const PcodeOp *op)
{
    unimplementedOp("opIndirect");
}

void ASTBuilder::opPiece(const PcodeOp *op)
{
    unimplementedOp("opPiece");
}

void ASTBuilder::opSubpiece(const PcodeOp *op)
{
    unimplementedOp("opSubpiece");
}

void ASTBuilder::opCast(const PcodeOp *op)
{
    unimplementedOp("opCast");
}

void ASTBuilder::opPtradd(const PcodeOp *op)
{
    unimplementedOp("opPtradd");
}

void ASTBuilder::opPtrsub(const PcodeOp *op)
{
    unimplementedOp("opPtrsub");
}

void ASTBuilder::opSegmentOp(const PcodeOp *op)
{
    unimplementedOp("opSegmentOp");
}

void ASTBuilder::opCpoolRefOp(const PcodeOp *op)
{
    unimplementedOp("opCpoolRefOp");
}

void ASTBuilder::opNewOp(const PcodeOp *op)
{
    unimplementedOp("opNewOp");
}

void ASTBuilder::opInsertOp(const PcodeOp *op)
{
    unimplementedOp("opInsertOp");
}

void ASTBuilder::opExtractOp(const PcodeOp *op)
{
    unimplementedOp("opExtractOp");
}

void ASTBuilder::opPopcountOp(const PcodeOp *op)
{
    unimplementedOp("opPopcountOp");
}
