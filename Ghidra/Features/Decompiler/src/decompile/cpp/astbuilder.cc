#include "astbuilder.h"
#include "astvisitors/typedefdeclvisitor.h"

#include <ctime>
#include <string>

#include "funcdata.hh"
#include "varnode.hh"

using namespace std;

static int _count_unimpl_stuff = 0;
void ASTBuilder::unimplementedCode(std::string description)
{
    // again purely for debugging to avoid exceptions...

    // log this, set breakpoint or ignore
    _count_unimpl_stuff++;
    _logfile << "[todo] " << description << "\n";
}

static int _num_unimpl_ops = 0;
void ASTBuilder::unimplementedOp(std::string opname)
{
    // this is purely for debugging so I don't have to throw exceptions
    // and kill my debug session right now :)

    // either log this or set a breakpoint here to catch any unimplemented
    // ops that are involved in an expression. If I don't care, I can move on
    _num_unimpl_ops++;

    // so we can catch all with unimplementedCode() breakpoint
    unimplementedCode("OP: " + opname);
}

string ensureValidFilename(string filename)
{
    // CLS: I'm sure this isn't optimal - don't care, just rushing to get it done ;)
    string invalidChars = "<>:\"/\\|?*";
    for (int i = 0; i < invalidChars.size(); i++) {
        auto charpos = filename.find(invalidChars[i]);
        if (charpos != string::npos) {
            // invalid character - replace it
            filename[charpos] = '_';
            i--;    // check this character again to make sure we catch all occurrences
        }
    }
    return filename;
}

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
    uint4 mods;     // save current mod flags like PrintLanguage::recurse()
};

PendingNode::PendingNode()
{
    node_type = ePendingNodeType::node_invalid;
    vnode = nullptr;
    op = nullptr;
    sym = nullptr;
    high = nullptr;
    mods = 0;
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

ASTBuilder::ASTBuilder(Architecture* ghidra, string logfolder)
    : PrintC(ghidra), _logfolder(logfolder),
    _head_translation_unit(nullptr), _next_vdecl_id(0)
{
    // TODO: initialize AST callbacks...
    _ast_callbacks.toAstTypeCallback = [this](const Datatype* dt) {
        return this->toAstType(dt);
        // return ((ASTBuilder*)context)->toAstType(dt);
    };
    initASTCallbacks(&_ast_callbacks);
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
        _parameters[pvdecl->ghidra_sym()] = pvdecl;
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
            _locals[vdecl->ghidra_sym()] = vdecl;
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
            _locals[vdecl->ghidra_sym()] = vdecl;
        }
    }
}

string getTimestamp()
{
    time_t t = time(0);   // get time now
    struct tm * now = localtime( & t );

    char buffer [180];
    strftime(buffer, 180, "%y-%m-%d_%H.%M.%S", now);
    return string(buffer);
}

FunctionDecl* ASTBuilder::buildFunctionDecl(Funcdata* fd, bool fwd_decl)
{
    FunctionDecl* fdecl = new FunctionDecl(_next_vdecl_id++, fd);

    // params are child nodes
    FuncProto& fp = fd->getFuncProto();
    buildFunctionParams(fp, fdecl);

    if (fwd_decl) {
        return fdecl;   // don't construct function body
    }

    // -------- LOCAL DECLS
    CompoundStmt* fbody = new CompoundStmt();
    fdecl->addChild(fbody);

    // locals (main scope)
    ScopeLocal& scope = *fd->getScopeLocal();
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
    return fdecl;
}

ASTNode* ASTBuilder::buildAST(Funcdata* fd)
{
    // if I decide to use timestamps, convert to something like this:
    // static string logfile = _logfolder + "/" + getTimestamp() + "-astbuilder.log";
    string logfile = _logfolder + "/" + ensureValidFilename(fd->getName()) + ".log";
    // CLS: add this mode when ready for timestamped files
    //| ios::app);
    _logfile.open(logfile, ios::out);

    if (!fd->isProcStarted()) {
        // not decompiled
        stringstream ss;
        _logfile << "Function at 0x" << to_hex(fd->getAddress().getOffset());
        _logfile << " not decompiled\n";
        ss << "Function at 0x" << to_hex(fd->getAddress().getOffset());
        ss << " not decompiled";
        _logfile << ss.str();
        _logfile.close();
        return new LogMsg(ss.str());
    }
    else if (fd->hasNoStructBlocks()) {
        // not fully decompiled, no structure present
        stringstream ss;
        ss << "Function at 0x" << to_hex(fd->getAddress().getOffset());
        ss << " not fully decompiled (no structure present)";
        _logfile << ss.str();
        _logfile.close();
        return new LogMsg(ss.str());
    }

    _logfile << "Building AST for " << fd->getName() << "\n";

    // translation unit is top level
    /**
     * NOTE: don't free this even if != nullptr...the caller of the last
     * buildAST() call owns the memory
     */
    _head_translation_unit = new TranslationUnitDecl();

    FunctionDecl* fdecl = buildFunctionDecl(fd, false);

    /** TODO: remove other places in code that manually add forward decls
     * to head_translation_unit and add them all HERE in a (hopefully)
     * deterministic ordering...
     *
     * [types/typedefs]
     * [globals]
     * [functions]
     *
     * >>> maybe sort each one by name to help automate any validation later <<<
     *
     * (I could even ONLY output the forward decls for a function, convert
     * this to C snippet, and copy/paste at top of Ghidra decompiled code
     * in order to automate validation process)
    */

    /** CLS: I'm thinking this is the best way to get word size? could be wrong */
    int4 arch_wordsize = glb->getDefaultSize();
    // glb->getDefaultCodeSpace()->getAddrSize()

    // ---------------------------------
    // TODO: FIRST, generate RecordDecl's for structures underneath TranslationUnitDecl
    // putting these here allows TypeDef visitor to be able to generate any further-needed typedefs
    // if that's even possible...
    // (we should already have built up the structure map via builder->_head_translation_unit.structures)
    // ---------------------------------
    // RecordDecl - fwd declaration or definition of a struct
        // sid
        // inner
            // FieldDecl
            // FieldDecl
            // ...
            // [these are present for definition, absent for fwd decl]
    // --> only reason to include FieldDecl here (instead of dynamic lookup
    // in TU._structs) is for validation with clang AST

    unimplementedCode("generate RecordDecl structure definitions");

    // add global decls to the AST under top-level translation unit
    for (auto const& entry : _globals) {
        _head_translation_unit->addChild(entry.second);
    }

    for (auto const& entry : _mismatch_globals) {
        for (auto const& vdecl : entry.second) {
            _head_translation_unit->addChild(vdecl);
        }
    }

    // add function forward-declarations to top-level translation unit
    for (auto const& entry : _fwd_decl_funcs) {
        _head_translation_unit->addChild(entry.second);
    }

    // add the function itself last
    _head_translation_unit->addChild(fdecl);

    // forward declare types/typedefs
    // (these will be prepended @ top, but have to compute last so we process
    // the globals/fwd decls/function code)
    TypedefDeclVisitor typedef_visitor;
    typedef_visitor.insertTypedefs(_head_translation_unit);

    _logfile.flush();
    _logfile.close();

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

    uint4 modsave = mods;
    mods = node->mods;

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

    mods = modsave;
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

void ASTBuilder::createIntLiteral(Datatype* dt, uintb value)
{
    IntegerLiteral* lit = new IntegerLiteral(toAstType(dt), value);
    currentASTNode()->addChild(lit);
}

void ASTBuilder::createCharConstant(Datatype* dt, uintb value, const Varnode* vn)
{
    BuiltinType* builtin = new BuiltinType(dt);
    CharacterLiteral* chr = new CharacterLiteral(builtin, value);
    currentASTNode()->addChild(chr);
    // CLS: is this sufficient? or do we need to go handle all the cases
    // and print formats like Ghidra does?
    // -> for now let's try this, I think it might be sufficient since the
    //    Ghidra code is primarily display-oriented. If we need to we can
    //    come back and add some logic back in if it e.g. changes data types
    //    to int in some cases (also the EquateSymbol case)
    //    If so, just reuse their functions and convert the stringstream
}

bool ASTBuilder::createPtrCharConstant(TypePointer* pt, uintb value, const Varnode* vn, const PcodeOp* op)
{
    if (value == 0) {
        return false;
    }

    // below mostly copied from pushPtrCharConstant()
    AddrSpace* spc = glb->getDefaultDataSpace();
    uintb fullEncoding;
    Address point;
    if (op != (const PcodeOp*)0) {
        point = op->getAddr();
    }
    Address stringaddr = glb->resolveConstant(spc, value, pt->getSize(), point, fullEncoding);
    if (stringaddr.isInvalid()) {
        return false;
    }
    if (!glb->symboltab->getGlobalScope()->isReadOnly(stringaddr,1,Address())) {
        return false;   // string location is not readonly
    }

    ostringstream str;
    Datatype* pointedto_dt = pt->getPtrTo();
    if (!printCharacterConstant(str, stringaddr, pointedto_dt)) {
        return false;   // unable to get a nice ASCII string
    }

    // add string literal to AST
    StringLiteral* strlit = new StringLiteral(str.str());
    currentASTNode()->addChild(strlit);
    return true;
}

bool ASTBuilder::createPtrCodeConstant(TypePointer* pt, uintb value, const Varnode* vn, const PcodeOp* op)
{
    AddrSpace* spc = glb->getDefaultCodeSpace();
    Funcdata* fd = (Funcdata*)nullptr;
    uintb byte_offset = AddrSpace::addressToByte(value, spc->getWordSize());
    fd = glb->symboltab->getGlobalScope()->queryFunction(Address(spc, byte_offset));
    if (fd) {
        unimplementedCode("reference to function pointer");
        return false;
        // this is probably going to be a DeclRef?
        // ALSO: make sure we properly handle any needed fwd-decls in case this adds a new
        // reference to a function (I think we postprocess those and get this automatically)
        // fd->getName()
        // return true;
    }
    return false;
}

void ASTBuilder::processPendingConstant(PendingNode* node)
{
    auto value = node->vnode->getOffset();
    HighVariable* high = node->vnode->getHigh();
    Datatype* dt = high->getType();
    Datatype* subtype = nullptr;

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
                createCharConstant((TypeChar*)dt, value, node->vnode);
            }
            else if (dt->isEnumType()) {
                unimplementedCode("enum constant");
            }
            else {
                createIntLiteral(dt, value);  // int/uint
            }
            return;
        case TYPE_UNKNOWN:
            createIntLiteral(dt, value);
            return;
        case TYPE_BOOL:
            unimplementedCode("bool constant");
            return;
        case TYPE_VOID:
            // this is wrong...log or ignore (Ghidra throws here so just ignore)
            return;
        case TYPE_PTR:
        case TYPE_PTRREL:
            if (option_NULL && (value == 0)) {
                // 'NULL' token for null pointers
                // we will never hit this unless we enable this option (in the GUI)
                unimplementedCode("NULL pointer");
                return;
            }
            subtype = ((TypePointer*)dt)->getPtrTo();
            if (subtype->isCharPrint()) {
                if (createPtrCharConstant((TypePointer*)dt, value, node->vnode, node->op)) {
                    return;
                }
            } else if (subtype->getMetatype() == TYPE_CODE) {
                if (createPtrCodeConstant((TypePointer*)dt, value, node->vnode, node->op)) {
                    return;
                }
            }
            break;  // break out to default print below
        case TYPE_FLOAT:
            unimplementedCode("float constant");
            return;
        default:
            unimplementedCode("default 'printing' for constant with metatype" + (int)dt->getMetatype());
            // typecast, integer
            break;  // break out to default print below
    }

    // insert type cast and the literal integer value for the constant
    CStyleCastExpr* cast = createTypeCast(dt);
    pushASTNode(cast);
    createIntLiteral(dt, value);
    popASTNode();
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
        // partial symbol => size+offset
        // partial: STRUCT FIELDS/ARRAYS!
        unimplementedCode("processPendingSymbol: STRUCT FIELD/ARRAY");
        return;
    }
    else {
        // mismatch
        // save and index mismatch_globals by the original symbol (node->sym)
        // which points to a list of decl's (one each for different mismatching
        // accesses to that symbol)

        if (!node->sym->getScope()->isGlobal()) {
            unimplementedCode("processPendingSymbol: mismatch symbol NON GLOBAL");
            return;
        }

        VarDecl* sym_decl = nullptr;

        if (_mismatch_globals.count(node->sym)) {
            // check size
            for (VarDecl* vd : _mismatch_globals[node->sym]) {
                auto existing_size = vd->ghidra_sym()->getType()->getSize();
                if (node->vnode->getSize() == existing_size) {
                    // use this one
                    sym_decl = vd;
                }
            }
        }

        // node->sym not mapped OR we didn't find a size match
        if (!sym_decl) {
            sym_decl = new VarDecl(_next_vdecl_id++,
                "_" + node->sym->getName(),
                node->vnode->getType());    // use vnode type, not sym type

            if (_mismatch_globals.count(node->sym)) {
                _mismatch_globals[node->sym].push_back(sym_decl);
            } else {
                _mismatch_globals[node->sym] = {sym_decl};
            }
        }

        DeclRefExpr* refexpr = new DeclRefExpr(sym_decl);
        currentASTNode()->addChild(refexpr);
        return;
    }

    VarDecl* sym_decl = nullptr;

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

PendingNode* ASTBuilder::buildNodeImplied(const Varnode* vn, const PcodeOp* op, uint4 modflags)
{
    PendingNode* node = new PendingNode();

    node->node_type = ePendingNodeType::node_temporary;
    node->op = op;
    node->vnode = vn;
    node->high = vn->getHigh();
    node->sym = node->high ? node->high->getSymbol() : nullptr;
    node->mods = modflags;

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
        lhs->mods = mods;   // I don't think this matters here, but just in case
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

static string getRegName(const Varnode* vn)
{
    // auto regdata = trans->getRegister("RAX");
    auto trans = vn->getSpace()->getTrans();
    return trans->getRegisterName(vn->getSpace(), vn->getOffset(), vn->getSize());
}

void ASTBuilder::emitBlockBasic(const BlockBasic *bb)
{
    if (isSet(only_branch)) {
        const PcodeOp* instr = bb->lastOp();
        if (instr->isBranch()) {
            emitExpression(instr);
        }
    }
    else {
        for (PcodeOp* instr : getPcodeOps(bb)) {

            if (instr->notPrinted()) {
                continue;
            }
            if (instr->isBranch()) {
                if (isSet(no_branch)) {
                    continue;
                }
                if (instr->code() == CPUI_BRANCH) {
                    // apparently, a straight branch should be processed
                    // by the block classes
                    continue;
                }
            }

            const Varnode* vnode = instr->getOut();
            if (vnode && vnode->isImplied()) {
                // skip Pcode instruction with implied result
                string regname = getRegName(vnode);
                continue;
            }

            emitExpression(instr);
        }

        // CLS: note we don't need to worry about if flat is set, since that
        // is an option to not print structured code, just using gotos and
        // labels for everything (not applicable for our AST)
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
    FlowBlock* sub = bl->subBlock(0);
    sub->emit(this);
}

void ASTBuilder::emitBlockGoto(const BlockGoto *bl)
{
    unimplementedCode("emitBlockGoto");
}

void ASTBuilder::emitBlockLs(const BlockList *bl)
{
    /**
     * NOTE: nofallthru appears to only be used for the flat layout case,
     * which we don't care about here in the AST
     */

    // not fully sure what case BlockList with only_branch
    // handles, but following the pattern for now...
    if (isSet(only_branch)) {
        FlowBlock* subbl = bl->getBlock(bl->getSize()-1);
        subbl->emit(this);
        return;
    }

    // I think the special handling other than the first only_branch check
    // is all about flat layout...
    // => so for us, if it's not only_branch just emit them all

    // not sure if it matters, but no_branch mod is only used for all but
    // the final block
    if (bl->getSize() == 1) {
        bl->getBlock(0)->emit(this);
        return;
    }

    pushMod();
    setMod(no_branch);
    for (int i = 0; i < bl->getSize()-1; i++) {
        bl->getBlock(i)->emit(this);
    }
    popMod();

    // final block: use original state of no_branch flag
    bl->getBlock(bl->getSize()-1)->emit(this);
}

void ASTBuilder::emitBlockCondition(const BlockCondition *bl)
{
    unimplementedCode("emitBlockCondition");
}

void ASTBuilder::emitBlockIf(const BlockIf *bl)
{
    FlowBlock* condBlock = bl->getBlock(0);

    pushMod();
    // clear relevant flags ahead of processing
    unsetMod(no_branch|only_branch|pending_brace);

    // --- 1x through:
    // emit any "straightline" instructions preceding the branch
    pushMod();
    setMod(no_branch);
    condBlock->emit(this);
    popMod();

    IfStmt* if_stmt = new IfStmt();
    currentASTNode()->addChild(if_stmt);
    pushASTNode(if_stmt);

    // --- 2x through:
    // emit only the branch (if statement)
    pushMod();
    setMod(only_branch);
    condBlock->emit(this);
    popMod();

    if (bl->getGotoTarget() != nullptr) {
        /** TODO: emit goto statement */
        unimplementedCode("goto statement in BlockIf");
    }
    else {
        // don't emit the branches at end of if/else blocks (these are
        // implicit at AST level from structure)
        setMod(no_branch);

        // THEN BLOCK
        CompoundStmt* then_block_ast = new CompoundStmt();
        if_stmt->addChild(then_block_ast);

        pushASTNode(then_block_ast);
        FlowBlock* trueBlk = bl->getBlock(1);
        trueBlk->emit(this);
        popASTNode();   // then block

        // ELSE BLOCK
        if (bl->getSize() > 2) {
            CompoundStmt* else_block_ast = new CompoundStmt();
            if_stmt->addChild(else_block_ast);

            pushASTNode(else_block_ast);
            FlowBlock* elseBlk = bl->getBlock(2);
            elseBlk->emit(this);
            popASTNode();   // else block
        }
    }

    popASTNode();   // pop IfStmt
    popMod();
}

void ASTBuilder::emitBlockWhileDo(const BlockWhileDo *bl)
{
    unimplementedCode("emitBlockWhileDo");
}

void ASTBuilder::emitBlockDoWhile(const BlockDoWhile *bl)
{
    unimplementedCode("emitBlockDoWhile");
}

void ASTBuilder::emitBlockInfLoop(const BlockInfLoop *bl)
{
    unimplementedCode("emitBlockInfLoop");
}

void ASTBuilder::emitBlockSwitch(const BlockSwitch *bl)
{
    pushMod();
    unsetMod(no_branch|only_branch);

    // haven't seen an occurrence yet, but assume no_branch gets any
    // "preceding" straight-line code in the block before the branching
    // instruction that corresponds to the switch conditional
    pushMod();
    setMod(no_branch);
    bl->getSwitchBlock()->emit(this);
    popMod();

    pushMod();
    setMod(only_branch|comma_separate);
    bl->getSwitchBlock()->emit(this);
    popMod();

    /** NOTE: this will push the SwitchStmt as currentASTNode */

    // cases go under CompoundStmt
    CompoundStmt* cmpstmt = new CompoundStmt();
    currentASTNode()->addChild(cmpstmt);
    pushASTNode(cmpstmt);

    const Datatype* dt = bl->getSwitchType();

    for (int i = 0; i < bl->getNumCaseBlocks(); i++) {
        // emit case statement
        if (bl->isDefaultCase(i)) {
            unimplementedCode("emit default case statement");
        }
        else {
            int nlabels = bl->getNumLabels(i);
            for (int l = 0; l < nlabels; l++) {
                uintb val = bl->getLabel(i, l);
                CaseStmt* case_stmt = new CaseStmt();
                ConstantExpr* cexpr = new ConstantExpr();
                IntegerLiteral* literal = new IntegerLiteral(toAstType(dt), val);

                cmpstmt->addChild(case_stmt);
                case_stmt->addChild(cexpr);
                cexpr->addChild(literal);

                if (l > 0) {
                    popASTNode();   // pop last case statement we pushed
                }
                // needs to be parent of code in case
                pushASTNode(case_stmt);
            }
        }

        if (bl->getGotoType(i)) {
            /** TODO: emit goto statement */
            unimplementedCode("emit goto statement");
        }
        else {
            FlowBlock* caseblk = bl->getCaseBlock(i);
            caseblk->emit(this);
            popASTNode();   // pop case statement

            if (bl->isExit(i) && (i != bl->getNumCaseBlocks()-1)) {
                BreakStmt* brk = new BreakStmt();
                cmpstmt->addChild(brk);
            }
        }
    }

    popASTNode();   // pop compound statement
    popASTNode();   // pop switch statement pushed above
    popMod();
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
    expr->parts.push_back(buildNodeImplied(op->getIn(0), op, mods));
    _pending_expressions.push_back(expr);
}

void ASTBuilder::opLoad(const PcodeOp *op)
{
    bool usearray = checkArrayDeref(op->getIn(1));
    uint4 m = mods;
    PendingExpr* expr = new PendingExpr();

    if (usearray && (!isSet(force_pointer))) {
        m |= print_load_value;
        expr->ast_op = currentASTNode();
    }
    else {
        UnaryOperator* deref = new UnaryOperator("*", toAstType(op->getOut()->getType()));
        currentASTNode()->addChild(deref);
        expr->ast_op = deref;
    }

    expr->parts.push_back(buildNodeImplied(op->getIn(1), op, m));
    _pending_expressions.push_back(expr);
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
    // flipped => we take branch if condition is NOT true
    bool booleanflip = op->isBooleanFlip();
    uint4 m = mods;

    // do some magic checking for how to handle negation case
    if (booleanflip) {
        if (checkPrintNegation(op->getIn(1))) {
            m |= PrintLanguage::negatetoken;
            booleanflip = false;
        }
    }

    if (booleanflip) {
        /** TODO: prepend/insert boolean NOT operator (!) */
        unimplementedCode("insert boolean NOT operator (!) in opCbranch");
    }

    PendingExpr* expr = new PendingExpr();
    expr->ast_op = currentASTNode();    // should be the IfStmt node
    expr->parts.push_back(buildNodeImplied(op->getIn(1), op, m));
    _pending_expressions.push_back(expr);
}

void ASTBuilder::opBranchind(const PcodeOp *op)
{
    SwitchStmt* ss = new SwitchStmt();
    currentASTNode()->addChild(ss);
    pushASTNode(ss);    // need to push so it's ready for case stmts

    // build switch conditional expression
    PendingExpr* expr = new PendingExpr();
    expr->ast_op = ss;
    expr->parts.push_back(buildNodeImplied(op->getIn(0), op, mods));
    _pending_expressions.push_back(expr);
}

void ASTBuilder::opCall(const PcodeOp *op)
{
    // CallExpr here
    CallExpr* callexpr = new CallExpr();
    currentASTNode()->addChild(callexpr);

    PendingExpr* expr = new PendingExpr();
    expr->ast_op = callexpr;

    const Varnode* callpoint = op->getIn(0);
    if (callpoint->getSpace()->getType() == IPTR_FSPEC) {
        FuncCallSpecs* fspec = FuncCallSpecs::getFspecFromConst(callpoint->getAddr());
        if (fspec->getName().size() == 0) {
            unimplementedCode("handle empty func name @ 0x" + to_hex(op->getAddr().getOffset()));
        }
        else {
            Funcdata* fd = fspec->getFuncdata();
            if (fd) {
                FunctionSymbol* sym = fd->getSymbol();

                // this is the fdecl we need to refer to in our
                // DeclRefExpr (which is the first child of CallExpr)
                FunctionDecl* fdecl = nullptr;

                if (_fwd_decl_funcs.count(sym)) {
                    fdecl = _fwd_decl_funcs.at(sym);
                }
                else {
                    fdecl = buildFunctionDecl(fd, true);
                    _fwd_decl_funcs[sym] = fdecl;
                }

                // first child is a reference to callee function
                DeclRefExpr* callee_ref = new DeclRefExpr(fdecl);
                callexpr->addChild(callee_ref);
            }
            else {
                unimplementedCode("No symbol for function @ 0x" + to_hex(fd->getAddress().getOffset()));
            }
        }
    }
    else {
        throw LowlevelError("Missing function callspec");
    }

    // arguments
    for (int i = 1; i < op->numInput(); i++) {
        // pushVnImplied op->getIn(i),op,mods
        expr->parts.push_back(buildNodeImplied(op->getIn(i),op,mods));
    }

    _pending_expressions.push_back(expr);
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
    ReturnStmt* rs = new ReturnStmt();
    currentASTNode()->addChild(rs);

    // CLS: ignoring op->getHaltType() for now...if we need this,
    // we can add it later

    if (op->numInput() > 1) {
        // returns a value?
        unimplementedOp("opReturn RETURN VALUE");
    }
}

void ASTBuilder::opIntEqual(const PcodeOp *op)
{
    binaryOperator("==", op, "!=");
}

void ASTBuilder::opIntNotEqual(const PcodeOp *op)
{
    binaryOperator("!=", op, "==");
}

void ASTBuilder::opIntSless(const PcodeOp *op)
{
    binaryOperator("<", op, ">=");
}

void ASTBuilder::opIntSlessEqual(const PcodeOp *op)
{
    binaryOperator("<=", op, ">");
}

void ASTBuilder::opIntLess(const PcodeOp *op)
{
    binaryOperator("<", op, ">=");
}

void ASTBuilder::opIntLessEqual(const PcodeOp *op)
{
    binaryOperator("<=", op, ">");
}

void ASTBuilder::opIntZext(const PcodeOp *op,const PcodeOp *readOp)
{
    // CLS: we can change this if desired, but I think I want to
    // consider zero-extension implicit in the AST and simply
    // emit the argument, not "ZEXT816()" or similar

    PendingExpr* expr = new PendingExpr();
    expr->ast_op = currentASTNode();
    expr->parts.push_back(
        buildNodeImplied(op->getIn(0), op, mods)
    );
    _pending_expressions.push_back(expr);
}

// corresponds to pushType()...just working out how we want to
// process complex datatypes
std::string ASTBuilder::getFullTypeString(const Datatype* dt)
{
    string type_str = getTypeStringStart(dt);
    type_str += getTypeStringEnd(dt);
    return type_str;
}

std::string ASTBuilder::getTypeStringStart(const Datatype* dt)
{
    vector<const Datatype*> typestack;
    buildTypeStack(dt, typestack);

    const Datatype* dtype = typestack.back();     // the base type

    string type_str = "";

    if (dtype->getName().size() == 0) {
        // anonymous type
        type_str += genericTypeName(dtype);
    }
    else {
        type_str += dtype->getName();
    }

    for (int i = typestack.size()-2; i >= 0; i--) {
        dtype = typestack[i];
        if (dtype->getMetatype() == TYPE_PTR) {
            type_str += "*";
        }
        else if (dtype->getMetatype() == TYPE_ARRAY) {
            type_str += "[]";
        }
        else if (dtype->getMetatype() == TYPE_CODE) {
            type_str += "()";
        }
        else {
            _logfile << "Bad type expression (so far we had '" << type_str << "')\n";
            return "BAD_TYPE_EXPR";
        }
    }

    return type_str;
}

std::string ASTBuilder::getProtoInputString(const FuncProto* proto)
{
    string proto_string = "";
    // proto inputs
    int sz = proto->numParams();
    if (sz == 0 && !proto->isDotdotdot()) {
        proto_string  += "void";
    }
    else {
        if (sz > 0) {
            for (int i = 0; i < sz-1; i++) {
                ProtoParameter* param = proto->getParam(i);
                proto_string  += getFullTypeString(param->getType());
                proto_string  += ",";
            }
            // last one, no comma
            ProtoParameter* param = proto->getParam(sz-1);
            proto_string  += getFullTypeString(param->getType());

            if (proto->isDotdotdot()) {
                proto_string  += "...";
            }
        }
    }
    // CLS: need to test this...
    return "(" + proto_string + ")";
}

std::string ASTBuilder::getTypeStringEnd(const Datatype* dt)
{
    const Datatype* dtype = dt;
    string type_str = "";

    while (true) {
        if (dtype->getName().size() != 0) {
            break;  // base type
        }
        if (dtype->getMetatype() == TYPE_PTR) {
            dtype = ((const TypePointer*)dtype)->getPtrTo();
        }
        else if (dtype->getMetatype() == TYPE_ARRAY) {
            const TypeArray* dtarray = (const TypeArray*)dtype;
            dtype = dtarray->getBase();     // array element dtype
            type_str += std::to_string(dtarray->numElements());
        }
        else if (dtype->getMetatype() == TYPE_CODE) {
            const TypeCode* dtcode = (const TypeCode*)dtype;
            const FuncProto* proto = dtcode->getPrototype();
            if (proto) {
                type_str += getProtoInputString(proto);
                dtype = proto->getOutputType();
            }
            else {
                type_str += "()";   // empty list of params
            }
        }
        else {
            break;  // other anonymous type
        }
    }

    return type_str;
}

CStyleCastExpr* ASTBuilder::createTypeCast(Datatype* dt)
{
    CStyleCastExpr* cast = new CStyleCastExpr(toAstType(dt));
    currentASTNode()->addChild(cast);
    return cast;
}

void ASTBuilder::processTypeCastExpression(const PcodeOp* op)
{
    Datatype* dt = op->getOut()->getHigh()->getType();
    auto cast = createTypeCast(dt);

    PendingExpr* expr = new PendingExpr();
    expr->ast_op = cast;
    PendingNode* node = buildNodeImplied(op->getIn(0), op, mods);
    expr->parts.push_back(node);
    _pending_expressions.push_back(expr);
}

Type* ASTBuilder::toAstType(const Datatype* dt)
{
    StructTypeLibrary* type_lib = _head_translation_unit->type_library();
    StructType* stype = nullptr;

    switch (dt->getMetatype()) {
        case TYPE_VOID:
            return new VoidType();
        case TYPE_UINT:     // fall-through
        case TYPE_INT:
        case TYPE_FLOAT:
        case TYPE_BOOL:
            return new BuiltinType(dt);
        case TYPE_PTR:
            return new PointerType(((TypePointer*)dt)->getPtrTo());
        case TYPE_STRUCT:
            stype = type_lib->getStructTypeForGhidraStruct((TypeStruct*)dt);
            return new StructType(stype->sid(), type_lib);  // these can get deleted, so need to return a new'd copy
        case TYPE_ARRAY:
            return new ConstantArrayType((TypeArray*)dt);
        case TYPE_UNKNOWN:
            // convert undefinedX types to Type() and TypedefDeclVisitor will generate
            // the proper typedefs for them
            // -> (we can also change this to generate BuiltinType directly if desired)
            return new Type(dt);
        case TYPE_SPACEBASE:    // fall-through
        default:
            // _messages.push_back("UNHANDLED metatype " + string(dt->getMetatype())
                // + " for " + dt->getName());
            unimplementedCode("UNHANDLED metatype " + std::to_string((int)dt->getMetatype()) +
                              " for " + dt->getName());
            return new Type(dt);
    }

    // TYPE_SPACEBASE = 13,		///< Placeholder for symbol/type look-up calculations
    // TYPE_CODE = 8,		///< Data is actual executable code

    // TYPE_PTRREL = 5,		///< Pointer relative to another data-type (specialization of TYPE_PTR)
    // TYPE_UNION = 2,		///< An overlapping union of multiple datatypes
    // TYPE_PARTIALSTRUCT = 1,	///< Part of a structure, stored separately from the whole
    // TYPE_PARTIALUNION = 0		///< Part of a union
}

void ASTBuilder::opIntSext(const PcodeOp *op,const PcodeOp *readOp)
{
    Datatype* outType = op->getOut()->getHigh()->getType();
    Datatype* inType = op->getIn(0)->getHigh()->getType();
    if (castStrategy->isSextCast(outType, inType)) {
        /** QUESTION: does option_hide_exts even make sense here? */
        if (option_hide_exts && castStrategy->isExtensionCastImplied(op, readOp)) {
            // opHiddenFunc
            unimplementedCode("opHiddenFunc case in opIntSext");
        }
        else {
            processTypeCastExpression(op);
        }
    }
    else {
        // opFunc()
        unimplementedCode("opFunc case in opIntSext");
    }
}

void ASTBuilder::binaryOperator(string opcode, const PcodeOp* op, string negateOpcode/*=""*/)
{
    string opcode_used = opcode;

    if (isSet(negatetoken)) {
        // remember, this is only used for these 6:
        // <, <=, >, >=, ==, !=
        opcode_used = negateOpcode;
        unsetMod(negatetoken);
        if (negateOpcode == "") {
            throw LowlevelError("No negateOpcode supplied! (corresponds to fliptoken)");
        }
    }

    BinaryOperator* operation = new BinaryOperator(opcode_used);
    currentASTNode()->addChild(operation);

    PendingExpr* expr = new PendingExpr();
    expr->ast_op = operation;
    PendingNode* lhs = buildNodeImplied(op->getIn(0), op, mods);
    PendingNode* rhs = buildNodeImplied(op->getIn(1), op, mods);
    expr->parts.push_back(lhs);
    expr->parts.push_back(rhs);
    _pending_expressions.push_back(expr);
}

void ASTBuilder::opIntAdd(const PcodeOp *op)
{
    binaryOperator("+", op);
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
    binaryOperator("*", op);
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
    processTypeCastExpression(op);
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
