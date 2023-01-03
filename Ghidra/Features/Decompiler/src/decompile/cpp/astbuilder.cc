#include "astbuilder.h"

#include "funcdata.hh"
#include "varnode.hh"

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

/**
 * @brief Describes type of pending node.
 *
 * As I understand more about what types of pending nodes we will see and how
 * we must construct them, I may discover a better method of doing this than
 * an enum. For now I don't know and don't want to slow down...so starting out
 * by storing the type of pending node so it's available if I need to differentiate
 * when we process pending nodes
 */
enum class ePendingNodeType {
    node_invalid = 0,
    node_symbol = 1,
    node_temporary = 2,    // implied => temporary, including non-terminal expressions
};

class PendingNode
{
public:
    PendingNode();
    ~PendingNode()
    { }

    ePendingNodeType node_type;
    const Varnode* vnode;     /** JUST DON'T NAME THIS "vn" ...wow... */
    const PcodeOp* op;
    Symbol* sym;
    HighVariable* high;
};

PendingNode::PendingNode()
{
    node_type = ePendingNodeType::node_invalid;
    vnode = nullptr;
    op = nullptr;
    sym = nullptr;
    high = nullptr;
}

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
    std::vector<PendingNode*> parts;
};

ASTBuilder::ASTBuilder()
    : PrintC(nullptr), _head_translation_unit(nullptr), _next_vdecl_id(0)
{
}

/**
 * @brief Build
 *
 * @param fp The function prototype to build parameters from
 * @param fdecl The function to insert parameters into
 */
void ASTBuilder::buildFunctionParams(FuncProto& fp, FunctionDecl* fdecl)
{
    /** NOTE: parameters need to be IN ORDER within the list of children */
    for (int i = 0; i < fp.numParams(); i++) {
        ParmVarDecl* pvdecl = new ParmVarDecl(_next_vdecl_id++, fp.getParam(i));
        fdecl->addChild(pvdecl);

        // save this function parameter for future lookups
        _parameters[pvdecl->sym()] = pvdecl;
    }
}

/**
 * @brief Check if this symbol entry is an appropriate entry for creating
 * a local variable declaration. If so, create the corresponding VarDecl object
 * and return it. If not, return nullptr
 */
VarDecl* ASTBuilder::tryCreateLocalVarDecl(const SymbolEntry* sym_entry)
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

    return new VarDecl(_next_vdecl_id++, sym);
}

void ASTBuilder::buildLocalDeclsFromScope(const Scope& scope, CompoundStmt* fbody)
{
    for (auto sym_entry : scope) {
        VarDecl* vdecl = tryCreateLocalVarDecl(sym_entry);
        if (vdecl) {
            DeclStmt* ds = new DeclStmt();
            ds->addChild(vdecl);
            fbody->addChild(ds);

            // save this local for future lookups
            _locals[vdecl->sym()] = vdecl;
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

            // save this local for future lookups
            _locals[vdecl->sym()] = vdecl;
        }
    }
}

ASTNode* ASTBuilder::buildAST(Funcdata* fd)
{
    if (!fd->isProcStarted()) {
        // not decompiled
        stringstream ss;
        ss << "Function at 0x" << to_hex(fd->getAddress().getOffset());
        ss << " not decompiled";
        return new LogMsg(ss.str());
    }
    else if (fd->hasNoStructBlocks()) {
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

    // translation unit is top level
    /**
     * NOTE: don't free this even if != nullptr...the caller of the last
     * buildAST() call owns the memory
     */
    _head_translation_unit = new TranslationUnitDecl();

    FunctionDecl* fdecl = new FunctionDecl(fd);
    _head_translation_unit->addChild(fdecl);

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

    // -------- GLOBALS (test)
    // find global scope
    // Scope* curr_scope = fd->getScopeLocal();
    // while (!curr_scope->isGlobal()) {
    //     curr_scope = curr_scope->getParent();
    // }

    // /** TODO: try this for Data global var and see what its scope is */
    // // sym_entry->getSymbol()->getScope()

    // for (const SymbolEntry* sym_entry : *curr_scope) {
    //     std::string sym_name = sym_entry->getSymbol()->getName();
    //     if (sym_name == "Data") {
    //         // found it
    //     }
    // }

    // -------- FUNCTION CODE
    pushASTNode(fbody);
    emitBlockGraph(&fd->getStructure());
    popASTNode();

    return _head_translation_unit;
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
    /**
     * NOTE:
     * I followed the pattern that PrintC/PrintLanguage already used
     * with the RPN stack (which is how we got here)
     *
     * But if it turns out that all we have are "symbol" and "implied" node
     * types, I may get rid of node_type completely and just handle all the
     * various cases here based on if sym != nullptr, etc.
     */
    switch (node->node_type) {
        case ePendingNodeType::node_symbol:
            processPendingSymbol(node);
            break;
        case ePendingNodeType::node_temporary:
            processPendingTemporary(node);
            break;
        case ePendingNodeType::node_invalid:
            unimplementedCode("processPendingNode: node_type invalid");
            break;
        default:
            unimplementedCode("processPendingNode: UNHANDLED node_type");
            break;
    }
}

void ASTBuilder::processPendingTemporary(PendingNode* node)
{
    if (node->vnode->isImplied()) {
        const PcodeOp* defOp = node->vnode->getDef();
        // the push() call will add a new PendingExpression to the stack
        defOp->getOpcode()->push(this, defOp, node->op);
    }
    else {
        processPendingTerminal(node);   // corresponds to pushVnExplicit()
    }
}

void createIntLiteral(ASTBuilder* builder, Datatype* dt, uintb value)
{
    IntegerLiteral* lit = new IntegerLiteral(dt, value);
    builder->currentASTNode()->addChild(lit);
}

void ASTBuilder::processPendingConstant(PendingNode* node)
{
    auto value = node->vnode->getOffset();
    HighVariable* high = node->vnode->getHigh();
    Datatype* dt = high->getType();

    /**
     * AHA:
     * The type_metatype enum (returned by dt->getMetatype()) looks like the
     * Ghidra version of "data type category" we want to predict (remember this
     * isn't truth, but still of interest)
     */
    bool is_uint = dt->getMetatype() == TYPE_UINT;

    switch (dt->getMetatype()) {
        case TYPE_UINT:
        case TYPE_INT:
            if (dt->isCharPrint()) {
                // todo: char
                unimplementedCode("char constant");
            }
            else if (dt->isEnumType()) {
                // todo: enum
                unimplementedCode("enum constant");
            }
            else {
                // int/uint
                createIntLiteral(this, dt, value);
            }
            break;
        case TYPE_UNKNOWN:
            createIntLiteral(this, dt, value);
            break;
        case TYPE_BOOL:
            unimplementedCode("bool constant");
            break;
        case TYPE_VOID:
            // this is wrong...log or ignore (Ghidra throws here so just ignore)
            break;
        case TYPE_PTR:
        case TYPE_PTRREL:
            unimplementedCode("pointer constant");
            break;
        case TYPE_FLOAT:
            unimplementedCode("float constant");
            break;
        default:
            unimplementedCode("default 'printing' for constant");
            // typecast, integer
            break;
    }
}

void ASTBuilder::processPendingTerminal(PendingNode* node)
{
    if (node->vnode->isAnnotation()) {
        unimplementedCode("handle Annotations");
        return;
    }

    if (node->vnode->isConstant()) {
        processPendingConstant(node);
        return;
    }

    if (node->sym) {
        processPendingSymbol(node);
    }
    else {
        // pushUnnamedLocation
        unimplementedCode("processPendingTerminal: unnamed location");
    }
}

void ASTBuilder::processPendingSymbol(PendingNode* node)
{
    /**
     * CLS: this logic corresponds to pushVnExplicit()/pushVnLHS()
     * - when I get to structs/arrays/unnamed locations then need to debug
     * and make sure I handle appropriately
     */
    auto sym_offset = node->high->getSymbolOffset();
    if (sym_offset == -1) {
        // perfect match - all good, process below
        // I've just kept this to preserve structure similar to
        // Ghidra code
    }
    else if (sym_offset + node->vnode->getSize() <= node->sym->getType()->getSize()) {
        // partial: STRUCT FIELDS/ARRAYS!
        unimplementedCode("buildNodeLHS: STRUCT FIELD/ARRAY");
        return;
    }
    else {
        // mismatch
        unimplementedCode("buildNodeLHS: mismatch symbol");
        return;
    }

    ValueDecl* sym_decl = nullptr;

    if (_locals.count(node->sym)) {
        sym_decl = _locals.at(node->sym);
    }
    else if (_parameters.count(node->sym)) {
        sym_decl = _parameters.at(node->sym);
    }
    else if (_globals.count(node->sym)) {
        sym_decl = _globals.at(node->sym);
    }
    else {
        // this is the only way I can figure out so far to "discover" globals
        if (node->sym->getScope()->isGlobal()) {
            // add it to globals map
            VarDecl* global_decl = new VarDecl(_next_vdecl_id++, node->sym);
            _globals[node->sym] = global_decl;
            sym_decl = _globals.at(node->sym);

            // add global decl to the AST under top-level TranslationUnitDecl
            _head_translation_unit->addChild(global_decl, false);
        }
        else {
            // symbol not found!
            unimplementedCode("TODO: handle symbol not found");
        }
    }

    DeclRefExpr* refexpr = new DeclRefExpr(sym_decl);
    currentASTNode()->addChild(refexpr);
}

// or processPendingExpressions()
void ASTBuilder::processExpressionStack()
{
    while (!_pending_expressions.empty()) {
        // need to go FIFO/front-to-back to preserve order of
        // child nodes (e.g. LHS is processed/added before RHS)
        PendingExpr* expr = _pending_expressions.front();
        _pending_expressions.pop_front();

        pushASTNode(expr->ast_op);

        for (PendingNode* node : expr->parts) {
            processPendingNode(node);
        }

        popASTNode();

        delete expr;
    }
}

/**
 * CLS: buildNodeImplied gets called by getOpcode()->push()...
 *
 * => which means I CANNOT "go ahead" and look at vn->isImplied() and
 * call ANOTHER getOpcode()->push()...because that would add PendingNodes
 * TO THE WRONG EXPRESSION
 *
 * -> thus this function must simply create a pending node which can be
 * added as part of the CORRECT/current pending expression.
 * -> once we process the expression on the stack, we can then create new
 * expressions
 *
 * -----------------------------------
 * In other words:
 *
 * - getOpcode()->push() functions MAY USE buildNode() functions only
 * - buildNode() functions MUST NOT result in getOpcode()->push()
 * - processNode() functions do call getOpcode()->push()
 *
 * getOpcode()->push():
 *      - build pending expressions
 *      - build nodes using buildNode() for expressions
 *      - place pending expressions on the stack
 *
 * buildNode():
 *      - just build the PendingNode
 *
 * processNode():
 *      - create ASTNodes from PendingNodes
 *      - call getOpcode()->push() for nested expressions
 */

PendingNode* ASTBuilder::buildNodeImplied(const Varnode* vn, const PcodeOp* op)
{
    PendingNode* node = new PendingNode();

    node->node_type = ePendingNodeType::node_temporary;
    node->op = op;
    node->vnode = vn;
    node->high = vn->getHigh();
    node->sym = node->high ? node->high->getSymbol() : nullptr;

    return node;
}

PendingNode* ASTBuilder::buildNodeLHS(const Varnode* vnode, const PcodeOp* op)
{
    PendingNode* lhs = nullptr;

    HighVariable* hv = vnode->getHigh();
    Symbol* sym = hv->getSymbol();
    if (sym) {
        lhs = new PendingNode();
        lhs->node_type = ePendingNodeType::node_symbol;
        lhs->high = hv;
        lhs->sym = sym;
        lhs->vnode = vnode;
        lhs->op = op;
    }
    else {
        // pushUnnamedLocation
        unimplementedCode("buildNodeLHS: unnamed location");
    }

    return lhs;
}

void ASTBuilder::emitExpression(const PcodeOp *op)
{
    BinaryOperator* assignment = nullptr;
    const Varnode* outvn = op->getOut();

    if (outvn) {
        assignment = new BinaryOperator("=");
        currentASTNode()->addChild(assignment);     // links to AST

        PendingNode* lhs = buildNodeLHS(outvn, op);

        // have to process this now/first before RHS or order can be wrong
        pushASTNode(assignment);
        processPendingNode(lhs);
        popASTNode();

        if (lhs)
            delete lhs;
    }
    else if ((op->doesSpecialPrinting())) {
        // looks like this is for constructors?
        unimplementedCode("op->doesSpecialPrinting() in emitExpression");
    }

    // push assignment so RHS expressions are children of BinaryOperator =
    if (assignment)
        pushASTNode(assignment);

    // process the opcode to initiate RHS expression
    op->getOpcode()->push(this, op, nullptr);

    if (assignment)
        popASTNode();   // restore initial AST context

    processExpressionStack();   // fill out RHS expression tree
}

/**
 * OLD VERSION
 * -----------
 * This was patterned after the Ghidra PrintC implementation...
 * ...but after going through, based on what I think I understand
 * there is a much simpler/better way of doing this. So I'm preserving it
 * for now, but trying my other idea...
 */
// void ASTBuilder::emitExpression(const PcodeOp *op)
// {
//     /**
//      * TODO: now this needs to just "set up" the expression
//      * stack, sit back, and let it run...
//     */

//     PendingExpr* expr = nullptr;

//     const Varnode* outvn = op->getOut();
//     if (outvn) {
//         /*
//             TODO: handle inplace ops (x += 3) if
//             PrintC::option_inplace_ops is set
//          */

//         BinaryOperator* assignment = new BinaryOperator("=");
//         currentASTNode()->addChild(assignment);     // links to AST

//         /**
//          * BinaryOperator
//          * ----
//          * > first child => first operand, second child => second operand
//          * > for assignment, first child => LHS, second child => RHS
//          */
//         expr = new PendingExpr();
//         expr->ast_op = assignment;
//         expr->parts.push_back(buildNodeLHS(outvn, op));

//     }
//     else if (op->doesSpecialPrinting()) {
//         /** TODO: what changes here? */
//         unimplementedCode("emitExpression: doesSpecialPrinting");
//     }

//     if (!expr) {
//         // create a "blank" expression for the top of the expression stack, so
//         // the getOpcode->push() call (which calls our opXXX() method) can simply
//         // access the top of the expression stack (and for this case it will
//         // init expr->ast_op appropriately)
//         expr = new PendingExpr();
//     }

//     _pending_expressions.push_back(expr);

//     /**
//      * THOUGHT: I think what we need to do here is push expr to the stack
//      * regardless...either the initial LHS version, or a new PendingExpr()
//      * with nothing filled in.
//      *
//      * Then when push() calls opXyz(), that ASTBuilder member function can
//      * access the expression on top of the stack and ADD itself to the
//      * expression's parts list.
//      *      a) For LHS expr case: the LHS already exists, we now push the RHS
//      *      b) For "unary" case: no parts exist, we push the single PendingNode
//      */

//     /** PICK UP HERE:
//      * - debug this, follow the logic (and finish implementing)
//      * - look at Ghidra window w/ both decompiled C and PCode visible
//      *   so you can see which ops are implied, explicit, LHS, etc...
//     */

//     // to look at Pcode instructions:
//     // op->getOpcode()->opcode
//     // op->getAddr().getOffset(),x

//     op->getOpcode()->push(this, op, nullptr);

//     processExpressionStack();
// }

static string getRegName(const Varnode* vn)
{
    auto trans = vn->getSpace()->getTrans();
    return trans->getRegisterName(vn->getSpace(), vn->getOffset(), vn->getSize());
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
            string regname = getRegName(vn);
            continue;
        }

        // CLS: test
        // const Translate* trans = vn->getSpace()->getTrans();
        // auto regdata = trans->getRegister("RAX");
        // trans->getRegisterName(vn->getSpace(), vn->getOffset(), vn->getSize());

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
        unimplementedCode("goto statement in BlockIf");
    }
    else {
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
    // LHS has already been processed, create our own expression
    // for RHS (assignment is currentASTNode)
    PendingExpr* expr = new PendingExpr();
    expr->ast_op = currentASTNode();
    expr->parts.push_back(buildNodeImplied(op->getIn(0), op));
    _pending_expressions.push_back(expr);
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
    BinaryOperator* addition = new BinaryOperator("+");
    currentASTNode()->addChild(addition);

    PendingExpr* expr = new PendingExpr();
    expr->ast_op = addition;
    PendingNode* lhs = buildNodeImplied(op->getIn(0), op);
    PendingNode* rhs = buildNodeImplied(op->getIn(1), op);
    expr->parts.push_back(lhs);
    expr->parts.push_back(rhs);
    _pending_expressions.push_back(expr);
    // unimplementedOp("opIntAdd");
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
