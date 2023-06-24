#include "astbuilder.h"
#include "astvisitors/typedefdeclvisitor.h"
#include "astvisitors/commavisitor.h"

#include <ctime>
#include <string>

#include "funcdata.hh"
#include "varnode.hh"

using namespace std;

// Constructing this registers the capability
ASTBuilderCapability ASTBuilderCapability::astBuilderCapability;

ASTBuilderCapability::ASTBuilderCapability()
{
    name = "ast-builder";
    isdefault = false;
}

/** CLS HACK:
 * This is NOT the right way to do this...but I need to satisfy their interface
 * and I don't want to change the way I have things set up. So the effect
 * of this hack is:
 *
 * 1) this is not designed to be used any other way than the way I'm using
 *    it right now (exportFunctionAst from ghidra_process.cc when the
 *    GHIDRA_AST_CONFIG_FILE env var is set)
 * 2) if you try and construct an "ast-builder" PrintLanguage generically
 *    right now (although it will be properly registered) this will likely
 *    fail. I'm abusing their system to reuse ghidra Architecture's "print"
 *    member initialization (which gets called in ghidra->setLanguage())
 *    SO THAT MY STATE MATCHES AND I GET THE SAME AST RESULTS - that's the
 *    only reason I care.
*/
ASTBuilder* _builder_ptr = nullptr;
PrintLanguage* ASTBuilderCapability::buildLanguage(Architecture* glb)
{
    return _builder_ptr;
}

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
    bool isRead;    // for processSymbolDetail (i.e. pushSymbolDetail)
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
    _head_translation_unit(nullptr), _next_vdecl_id(0),
    _original_ghidra_printlang_name(ghidra->print->getName())
{
    // initialize AST callbacks...
    _ast_callbacks.toAstTypeCallback = [this](const Datatype* dt) {
        return this->toAstType(dt);
        // return ((ASTBuilder*)context)->toAstType(dt);
    };
    _ast_callbacks.unimplementedCodeCallback = [this](string msg) {
        unimplementedCode(msg);
    };

    initASTCallbacks(&_ast_callbacks);

    // this must be set before setPrintLanguage() is called so that ghidra
    // gets the proper handle to US
    _builder_ptr = this;
    /** NOTE: if this doesn't work because ghidra can't properly recover from
     * switching back and forth then I can do a full copy of ghidra and call
     * ghidra->init() the same way to create a parallel universe that is separate :)
     *
     * there is code that calls setPrintLanguage() already, so one would think this
     * has been tested and works...but just in case
    */
    ghidra->setPrintLanguage("ast-builder");
}

ASTBuilder::~ASTBuilder()
{
    glb->setPrintLanguage(_original_ghidra_printlang_name);
    _builder_ptr = nullptr;     // reset just so we can tell if something is going wrong
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

void ASTBuilder::buildFunctionParamTypes(const FuncProto* fp, FunctionType* ftype)
{
    /** NOTE: parameters need to be IN ORDER within the list of children */
    for (int i = 0; i < fp->numParams(); i++) {
        ftype->addChild(toAstType(fp->getParam(i)->getType()));
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

Type* ASTBuilder::getOpFuncOutputType(int out_size, string opFuncName, bool is_bool /*= false*/)
{
    switch (out_size) {
        case 1:
            return is_bool ? new BuiltinType("bool", 1, false, false)
                           : new BuiltinType("unsigned char", 1, false, false);
        case 2:
            return new BuiltinType("unsigned short", 2, false, false);
        case 3:     // fallthrough
        case 4:
            return new BuiltinType("unsigned int", 4, false, false);
        case 5:     // fallthrough
        case 6:     // fallthrough
        case 7:     // fallthrough
        case 8:
            return new BuiltinType("unsigned long", 8, false, false);
        default:
            unimplementedCode(opFuncName + " output size of " + std::to_string(out_size));
            return new BuiltinType("unsigned long", 8, false, false);
    }
}

FunctionDecl* ASTBuilder::buildOpFuncDecl(const PcodeOp* op, string name)
{
    Type* return_type = nullptr;

    if (name.find("SUB") == 0) {
        return_type = getOpFuncOutputType(op->getOut()->getSize(), "SUB");
    } else if (name.find("CONCAT") == 0) {
        int out_size = op->getIn(0)->getSize() + op->getIn(1)->getSize();
        return_type = getOpFuncOutputType(out_size, "CONCAT");
    } else if (name.find("ZEXT") == 0) {
        return_type = getOpFuncOutputType(op->getOut()->getSize(), "ZEXT");
    } else if (name.find("SEXT") == 0) {
        return_type = getOpFuncOutputType(op->getOut()->getSize(), "SEXT");
    } else if (name.find("SBORROW") == 0) {
        return_type = getOpFuncOutputType(1, "SBORROW", true);
    } else if (name.find("CARRY") == 0) {
        return_type = getOpFuncOutputType(1, "CARRY", true);
    } else if (name.find("SCARRY") == 0) {
        return_type = getOpFuncOutputType(1, "SCARRY", true);
    } else {
        unimplementedCode("Unhandled opFunc type " + name);
        return_type = getOpFuncOutputType(8, "UNHANDLED");
    }

    FunctionDecl* fdecl = new FunctionDecl(_next_vdecl_id++, name, return_type);

    for (int i = 0; i < op->numInput(); i++) {
        ParmVarDecl* pvdecl = new ParmVarDecl(_next_vdecl_id++, "param" + std::to_string(i+1),
                    new BuiltinType("unsigned long", 8, false, false));
        fdecl->addChild(pvdecl);
    }

    return fdecl;
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

    // prune unnecessary commas and add hierarchical comma ops here
    CommaVisitor cv;
    cv.fixCommaOps(fdecl);

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
        return nullptr;
    }
    else if (fd->hasNoStructBlocks()) {
        // not fully decompiled, no structure present
        stringstream ss;
        ss << "Function at 0x" << to_hex(fd->getAddress().getOffset());
        ss << " not fully decompiled (no structure present)";
        _logfile << ss.str();
        _logfile.close();
        return nullptr;
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

    for (auto stype : _type_lib.getMappedStructs()) {
        _head_translation_unit->addChild(new RecordDecl(stype));
    }

    // add global decls to the AST under top-level translation unit
    for (auto const& entry : _globals) {
        _head_translation_unit->addChild(entry.second);
    }

    for (auto const& entry : _mismatch_globals) {
        _head_translation_unit->addChild(entry.second);
    }

    for (auto const& entry : _unnamedLoc_globals) {
        _head_translation_unit->addChild(entry.second);
    }

    // add function forward-declarations to top-level translation unit
    for (auto const& entry : _fwd_decl_funcs) {
        _head_translation_unit->addChild(entry.second);
    }
    for (auto const& entry : _fwd_decl_opFunc_funcs) {
        _head_translation_unit->addChild(entry.second);
    }

    // add the function itself last
    _head_translation_unit->addChild(fdecl);

    // forward declare types/typedefs
    // (these will be prepended @ top, but have to compute last so we process
    // the globals/fwd decls/function code)
    TypedefDeclVisitor typedef_visitor(this);
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
            processSymbolDetail(node);
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

bool ASTBuilder::pushEquate(uintb val,int4 sz,const EquateSymbol *sym, const Varnode *vn,const PcodeOp *op)
{
    uintb mask = calc_mask(sz);
    uintb baseval = sym->getValue();
    uintb modval = baseval & mask;
    if (modval != baseval) {                    // If 1-bits are getting masked
        if (sign_extend(modval,sz,sizeof(uintb)) != baseval) {  // make sure we only mask sign extension bits
            return false;
        }
    }
    if (modval == val) {
        pushSymbolAST(const_cast<EquateSymbol*>(sym));
        return true;
    }
    modval = (~baseval) & mask;
    if (modval == val) {        // Negation
        UnaryOperator* unop = new UnaryOperator("~");
        currentASTNode()->addChild(unop);
        pushASTNode(unop);
        pushSymbolAST(const_cast<EquateSymbol*>(sym));
        popASTNode();
        return true;
    }
    modval = (-baseval) & mask;
    if (modval == val) {        // twos complement
        UnaryOperator* unop = new UnaryOperator("-");
        currentASTNode()->addChild(unop);
        pushASTNode(unop);
        pushSymbolAST(const_cast<EquateSymbol*>(sym));
        popASTNode();
        return true;
    }
    modval = (baseval + 1) & mask;
    if (modval == val) {
        BinaryOperator* binop = new BinaryOperator("+");
        currentASTNode()->addChild(binop);
        pushASTNode(binop);
        pushSymbolAST(const_cast<EquateSymbol*>(sym));
        push_integer(1, sz, false, (const Varnode *)0, (const PcodeOp *)0);
        popASTNode();
        return true;
    }
    modval = (baseval - 1) & mask;
    if (modval == val) {
        BinaryOperator* binop = new BinaryOperator("-");
        currentASTNode()->addChild(binop);
        pushASTNode(binop);
        pushSymbolAST(const_cast<EquateSymbol*>(sym));
        push_integer(1, sz, false, (const Varnode *)0, (const PcodeOp *)0);
        popASTNode();
        return true;
    }
    return false;
}

// have to mirror some of this logic because it could result in a call to pushEquate()
void ASTBuilder::push_integer(uintb val,int4 sz,bool sign, const Varnode *vn,const PcodeOp *op)
{
    push_integer(nullptr, val, sz, sign, vn, op);
}

void ASTBuilder::push_integer(Datatype* dt, uintb val,int4 sz,bool sign, const Varnode *vn,const PcodeOp *op)
{
    bool print_negsign;
    bool force_unsigned_token;
    bool force_sized_token;
    uint4 displayFormat = 0;

    force_unsigned_token = false;
    force_sized_token = false;
    if ((vn != (const Varnode *)0)&&(!vn->isAnnotation())) {
        HighVariable *high = vn->getHigh();
        Symbol *sym = high->getSymbol();
        if (sym) {
            if (sym->isNameLocked() && (sym->getCategory() == Symbol::equate)) {
                if (pushEquate(val,sz,(EquateSymbol *)sym,vn,op)) {
                    return;
                }
            }
            displayFormat = sym->getDisplayFormat();
        }
        force_unsigned_token = vn->isUnsignedPrint();
        force_sized_token = vn->isLongPrint();
        if (displayFormat == 0) {   // The symbol's formatting overrides any formatting on the data-type
            displayFormat = high->getType()->getDisplayFormat();
        }
    }
    if (sign && displayFormat != Symbol::force_char) { // Print the constant as signed
        uintb mask = calc_mask(sz);
        uintb flip = val^mask;
        print_negsign = (flip < val);
        if (print_negsign) {
            val = flip+1;
        }
        force_unsigned_token = false;
    }
    else {
        print_negsign = false;
    }

                    // Figure whether to print as hex or decimal
    if (displayFormat != 0) {
        // Format is forced by the Symbol
    }
    else if ((mods & force_hex)!=0) {
        displayFormat = Symbol::force_hex;
    }
    else if ((val<=10)||((mods & force_dec))) {
        displayFormat = Symbol::force_dec;
    }
    else {			// Otherwise decide if dec or hex is more natural
        displayFormat = (PrintLanguage::mostNaturalBase(val)==16) ? Symbol::force_hex : Symbol::force_dec;
    }

    ostringstream t;
    if (print_negsign) {
        t << '-';   // original PrintC code, doesn't matter here but keeping for clarity

        /**
         * CLS: this explicitly turns into a UnaryOperator '-' in the AST so
         * generate it that way here
         * (and thus we also have to compute the two's complement magnitude
         * since we are rendering -MAGNITUDE instead of TWOS_COMPL_VALUE)
        */
        UnaryOperator* unop = new UnaryOperator("-");
        currentASTNode()->addChild(unop);

        // compute magnitude of negative 2's complement number
        // int magnitude = (~val) + 1;

        Type* int_dt = dt ? toAstType(dt) : new BuiltinType("int", 4, false, true);
        IntegerLiteral* lit = new IntegerLiteral(int_dt, val);
        unop->addChild(lit);
        return;
    }
    if (displayFormat == Symbol::force_hex)
        t << hex << "0x" << val;
    else if (displayFormat == Symbol::force_dec)
        t << dec << val;
    else if (displayFormat == Symbol::force_oct)
        t << oct << '0' << val;
    else if (displayFormat == Symbol::force_char) {
        // need to push a char literal for this case
        BuiltinType* builtin = new BuiltinType("char", 1, false, true);
        CharacterLiteral* chr = new CharacterLiteral(builtin, val);
        currentASTNode()->addChild(chr);

        // printing details we don't care about...
        // if (doEmitWideCharPrefix() && sz > 1) {
        //     t << 'L';			// Print symbol indicating wide character
        // }
        // t << '\'';			// char is surrounded with single quotes
        // if (sz == 1 && val >= 0x80) {
        //     printCharHexEscape(t,(int4)val);
        // }
        // else {
        //     printUnicode(t,(int4)val);
        // }
        // t << '\'';
    }
    else {	// Must be Symbol::force_bin
        t << "0b";
        formatBinary(t, val);
    }
    if (force_unsigned_token)
        t << 'U';			// Force unsignedness explicitly
    if (force_sized_token)
        t << sizeSuffix;

    // push int literal
    Type* int_dt = dt ? toAstType(dt) : new BuiltinType("int", 4, false, true);
    IntegerLiteral* lit = new IntegerLiteral(int_dt, val);
    currentASTNode()->addChild(lit);
}

void ASTBuilder::createCharConstant(Datatype* ct, uintb val, const Varnode* vn, const PcodeOp* op)
{
    uint4 displayFormat = 0;
    bool isSigned = (ct->getMetatype() == TYPE_INT);

    if (vn && (!vn->isAnnotation())) {
        HighVariable *high = vn->getHigh();
        Symbol *sym = high->getSymbol();
        if (sym) {
            if (sym->isNameLocked() && (sym->getCategory() == Symbol::equate)) {
                if (pushEquate(val,vn->getSize(),(EquateSymbol *)sym,vn,op)) {
                    return;
                }
            }
            displayFormat = sym->getDisplayFormat();
        }
        if (displayFormat == 0) {
            displayFormat = high->getType()->getDisplayFormat();
        }
    }
    if (displayFormat != 0 && displayFormat != Symbol::force_char) {
        if (!castStrategy->caresAboutCharRepresentation(vn, op)) {
            push_integer(val, ct->getSize(), isSigned, vn, op);
            return;
        }
    }
    if ((ct->getSize()==1)&&(val >= 0x80)) {
        // For byte characters, the encoding is assumed to be ASCII, UTF-8, or some other
        // code-page that extends ASCII. At 0x80 and above, we cannot treat the value as a
        // unicode code-point. Its either part of a multi-byte UTF-8 encoding or an unknown
        // code-page value. In either case, we print as an integer or an escape sequence.
        if (displayFormat != Symbol::force_hex && displayFormat != Symbol::force_char) {
            push_integer(val, 1, isSigned, vn, op);
            return;
        }
        displayFormat = Symbol::force_hex;	// Fallthru but force a hex representation
    }

    BuiltinType* builtin = new BuiltinType(ct);
    CharacterLiteral* chr = new CharacterLiteral(builtin, val);
    currentASTNode()->addChild(chr);

    /**
     * below here is original code from PrintC::pushCharConstant()
     * ----------
     * CLS: from here below the logic is related to printing/formatting the
     * character, so I think we can ignore and simply generate a CharacterLiteral
     * here (whereas above we possibly generate an IntegerLiteral, etc)
     */
    // ostringstream t;
    // // From here we assume, the constant value is a direct unicode code-point.
    // // The value could be an illegal code-point (surrogates or beyond the max code-point),
    // // but this will just be emitted as an escape sequence.
    // if (doEmitWideCharPrefix() && ct->getSize() > 1) {
    //     t << 'L';       // Print symbol indicating wide character
    // }
    // t << '\'';          // char is surrounded with single quotes
    // if (displayFormat == Symbol::force_hex) {
    //     printCharHexEscape(t,(int4)val);
    // }
    // else {
    //     printUnicode(t,(int4)val);
    // }

    // t << '\'';
    // pushAtom(Atom(t.str(),vartoken,EmitMarkup::const_color,op,vn));
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

void ASTBuilder::push_float(uintb val,int4 sz,const Varnode *vn, const PcodeOp *op)
{
    push_float(nullptr, val, sz, vn, op);
}

void ASTBuilder::push_float(Datatype* dt, uintb val,int4 sz,const Varnode *vn, const PcodeOp *op)
{
    string token;

    const FloatFormat *format = glb->translate->getFloatFormat(sz);

    if (format == (const FloatFormat *)0) {
        unimplementedCode("FLOAT_UNKNOWN");
        return;
    } else {
        FloatFormat::floatclass type;
        double floatval = format->getHostFloat(val,&type);

        // CLS: assuming float or double only here if no Datatype* supplied
        string tname = (sz == 4) ? "float" : "double";
        Type* float_dt = new BuiltinType(tname, sz, true, true);
        if (dt) {
            delete float_dt;
            float_dt = toAstType(dt);
        }

        string special_value = "";

        if (type == FloatFormat::infinity) {
            if (format->extractSign(val)) {
                special_value = "-INF";
            } else {
                special_value = "INF";
            }
        } else if (type == FloatFormat::nan) {
            if (format->extractSign(val)) {
                special_value = "-NaN";
            } else {
                special_value = "NaN";
            }
        } else {
            ostringstream t;
            if ((mods & force_scinote)!=0) {
                t.setf( ios::scientific ); // Set to scientific notation
                t.precision(format->getDecimalPrecision()-1);
                t << floatval;
                token = t.str();
            } else {
                // Try to print "minimal" accurate representation of the float
                t.unsetf( ios::floatfield );	// Use "default" notation
                t.precision(format->getDecimalPrecision());
                t << floatval;
                token = t.str();
                bool looksLikeFloat = false;
                for(int4 i=0;i<token.size();++i) {
                    char c = token[i];
                    if (c == '.' || c == 'e') {
                        looksLikeFloat = true;
                        break;
                    }
                }
                if (!looksLikeFloat) {
                    token += ".0";	// Force token to look like a floating-point value
                }
            }
        }

        double value_to_use = 0.0;
        if (special_value == "") {
            value_to_use = std::stod(token);
        }

        FloatingLiteral* lit = nullptr;

        if (value_to_use < 0) {
            UnaryOperator* unop = new UnaryOperator("-");
            currentASTNode()->addChild(unop);
            lit = new FloatingLiteral(float_dt, -value_to_use);
            unop->addChild(lit);
        } else {
            lit = new FloatingLiteral(float_dt, value_to_use);
            currentASTNode()->addChild(lit);
        }

        if (special_value != "") {
            lit->setSpecialValue(special_value);    // override 0.0 with real value
        }
    }
}

void ASTBuilder::push_bool(uintb value, int size)
{
    // CLS: we can make this a BoolLiteral but C doesn't actually have bool datatype
    // ...however, Ghidra believes this is a bool somehow and we might benefit from
    // that extra information
    currentASTNode()->addChild(new IntegerLiteral(
            new BuiltinType("bool", size, false, false),
            value));
}

// corresponds to pushConstant()
void ASTBuilder::processPendingConstant(uintb value, Datatype* dt, const Varnode* vn,
                                        const PcodeOp* op)
{
    // auto value = node->vnode->getOffset();
    // HighVariable* high = node->vnode->getHigh();
    // Datatype* dt = high->getType();
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
                createCharConstant((TypeChar*)dt, value, vn, op);
            }
            else if (dt->isEnumType()) {
                unimplementedCode("enum constant");
            }
            else {
                push_integer(dt, value, dt->getSize(), !is_uint, vn, op);
            }
            return;
        case TYPE_UNKNOWN:
            push_integer(dt, value, dt->getSize(), false, vn, op);
            return;
        case TYPE_BOOL:
            push_bool(value, dt->getSize());
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
                if (createPtrCharConstant((TypePointer*)dt, value, vn, op)) {
                    return;
                }
            } else if (subtype->getMetatype() == TYPE_CODE) {
                if (createPtrCodeConstant((TypePointer*)dt, value, vn, op)) {
                    return;
                }
            }
            break;  // break out to default print below
        case TYPE_FLOAT:
            push_float(dt, value, dt->getSize(), vn, op);
            return;
        case TYPE_SPACEBASE:
        case TYPE_CODE:
        case TYPE_ARRAY:
        case TYPE_STRUCT:
        case TYPE_UNION:
        case TYPE_PARTIALSTRUCT:
        case TYPE_PARTIALUNION:
            break;
        default:
            unimplementedCode("default 'printing' for constant with metatype" + (int)dt->getMetatype());
            // typecast, integer
            break;  // break out to default print below
    }

    if (!option_nocasts) {
        // insert type cast and the literal integer value for the constant
        createAndPushTypeCast(dt);
    }

    pushMod();
    if (!isSet(force_dec)) {
        setMod(force_hex);
    }
    push_integer(dt, value, dt->getSize(), false, vn, op);
    popMod();

    if (!option_nocasts) {
        popASTNode();   // CStyleCastExpr
    }
}

// corresponds to pushVnExplicit()
void ASTBuilder::processPendingTerminal(PendingNode* node)
{
    if (node->vnode->isAnnotation()) {
        unimplementedCode("handle Annotations");
        return;
    }

    if (node->vnode->isConstant()) {
        auto vn = node->vnode;
        auto op = node->op;
        processPendingConstant(vn->getOffset(),vn->getHighTypeReadFacing(op),vn,op);
        return;
    }

    node->isRead = true;    // as passed to pushSymbolDetail() in pushVnExplicit()
    processSymbolDetail(node);
}

// corresponds to PrintC:pushSymbol()
// pushes a DeclRefExpr node to the AST
void ASTBuilder::pushSymbolAST(Symbol* sym)
{
    // handle FunctionDecl case
    FunctionSymbol* func_sym = dynamic_cast<FunctionSymbol*>(sym);
    if (func_sym) {
        // this should really be a function decl
        FunctionDecl* fdecl = nullptr;
        if (_fwd_decl_funcs.count(sym)) {
            fdecl = _fwd_decl_funcs.at(sym);
        } else {
            fdecl = buildFunctionDecl(func_sym->getFunction(), true);
            _fwd_decl_funcs[sym] = fdecl;
        }
        DeclRefExpr* refexpr = new DeclRefExpr(fdecl);
        currentASTNode()->addChild(refexpr);
        return;
    }

    // normal VarDecl case
    VarDecl* sym_decl = nullptr;

    if (_locals.count(sym)) {
        sym_decl = _locals.at(sym);     // trying without clone()
    }
    else if (_parameters.count(sym)) {
        sym_decl = _parameters.at(sym);     // trying without clone()
    }
    else if (_globals.count(sym)) {
        sym_decl = _globals.at(sym);        // trying without clone()
    }
    else {
        // this is the only way I can figure out so far to "discover" globals
        if (sym->getScope()->isGlobal()) {
            // add it to globals map
            sym_decl = new VarDecl(_next_vdecl_id++, sym);
            _globals[sym] = sym_decl;
        }
        else {
            // symbol not found!
            unimplementedCode("TODO: handle symbol not found");
        }
    }

    DeclRefExpr* refexpr = new DeclRefExpr(sym_decl);
    currentASTNode()->addChild(refexpr);
}

void ASTBuilder::pushMismatchSymbolAST(Symbol *sym,int4 off,int4 sz,
            const Varnode *vn,const PcodeOp *op)
{
    if (off == 0) {
        // The most common situation is when a user sees a reference
        // to a variable and forces a symbol to be there but guesses
        // the type (or size) incorrectly
        // The address of the symbol is correct, but the size is too small

        /** CLS: my comment: */
        // save and index mismatch_globals by the original symbol (node->sym)
        // which points to a list of decl's (one each for different mismatching
        // accesses to that symbol)

        VarDecl* sym_decl = nullptr;

        if (sym->getScope()->isGlobal()) {
            if (!_globals.count(sym)) {
                _globals[sym] = new VarDecl(_next_vdecl_id++, sym);
            }
            sym_decl = _globals[sym];
        } else {
            // local var
            if (_locals.count(sym)) {
                sym_decl = _locals[sym];
            } else {
                throw LowlevelError("No local variable mapped for " + sym->getName());
            }
        }

        // add globals OR locals here ONLY for validation purposes...
        string mismatch_name = "_" + sym->getName();
        if (!_mismatch_globals.count(mismatch_name)) {
            _mismatch_globals[mismatch_name] = new VarDecl(
                _next_vdecl_id++,
                mismatch_name,
                new BuiltinType("int", 4, false, true));
        }

        DeclRefExpr* refexpr = new DeclRefExpr(sym_decl);
        currentASTNode()->addChild(refexpr);
        return;
    }
    else {
        unimplementedCode("pushUnnamedLocation in pushMismatchSymbol");
        return;
    }
}

void ASTBuilder::pushUnnamedLocation(const Address &addr, const Varnode *vn,const PcodeOp *op)
{
    // I'm going to generate these names a bit differently to ensure they are valid var names

    ostringstream s;
    s << addr.getSpace()->getName();
    // my version (after done validating):
    // s << "_";
    // s << addr.getOffset();

    // Ghidra's version:
    addr.printRaw(s);
    // pushAtom(Atom(s.str(),vartoken,EmitMarkup::var_color,op,vn));

    string varname = s.str();

    if (!_unnamedLoc_globals.count(varname)) {
        Type* addr_dt = new BuiltinType("unsigned long", 8, false, false);
        _unnamedLoc_globals[varname] = new VarDecl(_next_vdecl_id++, varname, addr_dt);
    }

    VarDecl* vdecl = _unnamedLoc_globals.at(varname);
    DeclRefExpr* refexpr = new DeclRefExpr(vdecl);
    currentASTNode()->addChild(refexpr);

    // IntegerLiteral* lit = new IntegerLiteral(addr_dt, addr.getOffset());
    // currentASTNode()->addChild(lit);
}

/**
 * @brief My version of PartialSymbolEntry that contains the data I need
 * to implement pushPartialSymbol
 */
struct ASTPartialSymEntry
{
    ASTNode* node;
    int arr_idx;
};

void ASTBuilder::pushPartialSymbol(const Symbol *sym,int4 off,int4 sz,
            const Varnode *vn,const PcodeOp *op,int4 inslot)
{
    // We need to print "bottom up" in order to get parentheses right
    // I.e. we want to print globalstruct.arrayfield[0], rather than
    //                       globalstruct.(arrayfield[0])

    Datatype *finalcast = (Datatype *)0;

    // for X.Y:
    /**
     * MemberExpr
     * - name=Y
     * - inner
     *      DeclRefExpr (X)
     */

    // for X.Y.Z:
    /**
     * MemberExpr
     * - name=Z
     * - inner
     *      MemberExpr
     *      - name=Y
     *      - inner
     *          DeclRefExpr (X)
     */
    vector<ASTPartialSymEntry> ast_stack;

    Datatype *ct = sym->getType();

    while(ct) {
        if (off == 0) {
            if (sz == 0 || (sz == ct->getSize() && (!ct->needsResolution() || ct->getMetatype()==TYPE_PTR))) {
                break;
            }
        }
        bool succeeded = false;
        if (ct->getMetatype()==TYPE_STRUCT) {
            if (ct->needsResolution() && ct->getSize() == sz) {
                Datatype *outtype = ct->findResolve(op, inslot);
                if (outtype == ct) {
                    break;  // Turns out we don't resolve to the field
                }
            }
            const TypeField *field;
            field = ct->findTruncation(off,sz,op,inslot,off);
            if (field) {
                ast_stack.emplace_back();
                ASTPartialSymEntry &ast_entry(ast_stack.back());
                StructType* stype = dynamic_cast<StructType*>(toAstType(ct));
                if (!stype) {
                    throw LowlevelError("unable to convert ct to StructType* when metatype == TYPE_STRUCT");
                }
                ast_entry.node = new MemberExpr(field->name, field->offset, /*isArrow=*/ false, stype->sid());
                delete stype;   // saved sid so we're done with it now :)

                ct = field->type;
                succeeded = true;
            }
        } else if (ct->getMetatype() == TYPE_ARRAY) {
            int4 el;
            Datatype *arrayof = ((TypeArray *)ct)->getSubEntry(off,sz,&off,&el);
            if (arrayof) {
                ast_stack.emplace_back();
                ASTPartialSymEntry &ast_entry(ast_stack.back());
                ast_entry.node = new ArraySubscriptExpr();
                ast_entry.arr_idx = el;

                ct = arrayof;
                succeeded = true;
            }
        } else if (ct->getMetatype() == TYPE_UNION) {
            unimplementedCode("TYPE_UNION in pushPartialSymbol");
            succeeded = false;

            // const TypeField *field;
            // field = ct->findTruncation(off,sz,op,inslot,off);
            // if (field) {
            //     stack.emplace_back();
            //     PartialSymbolEntry &entry(stack.back());
            //     entry.token = &object_member;       // this is '.' - i.e. X.Y
            //     entry.field = field;
            //     entry.parent = ct;
            //     entry.fieldname = entry.field->name;
            //     entry.hilite = EmitMarkup::no_color;
            //     ct = field->type;
            //     succeeded = true;
            // } else if (ct->getSize() == sz) {
            //     break;		// Turns out we don't need to resolve the field
            // }
        } else if (inslot >= 0) {
            Datatype *outtype = vn->getHigh()->getType();
            if (castStrategy->isSubpieceCastEndian(outtype,ct,off,
                                sym->getFirstWholeMap()->getAddr().getSpace()->isBigEndian())) {
                // Treat truncation as SUBPIECE style cast
                finalcast = outtype;
                ct = nullptr;
                succeeded = true;
            }
        }

        if (!succeeded) {       // Subtype was not good
            ast_stack.emplace_back();
            ASTPartialSymEntry &ast_entry(ast_stack.back());
            if (sz == 0) {
                sz = ct->getSize() - off;
            }
            ast_entry.node = new MemberExpr(unnamedField(off, sz), off, /*isArrow=*/ false);

            ct = nullptr;
        }
    }

    // if (finalcast && !option_nocasts) {
    if ((finalcast != (Datatype *)0)&&(!option_nocasts)) {
        createAndPushTypeCast(finalcast);
    }

    // we're walking the stack backwards (back->front) and pushing each item
    // onto the AST top->down. so the last stack entry is the first item to be pushed

    int arr_idx = 0;
    bool pending_arr_idx = false;   // set this if we need to push an array idx next iteration

    for (int i = ast_stack.size()-1; i >= 0; i--) {
        currentASTNode()->addChild(ast_stack[i].node);

        // handle a pending arr_idx first because we could have 2 of these in a row X[0][1]
        if (pending_arr_idx) {
            IntegerLiteral* lit = new IntegerLiteral(new BuiltinType("int", 4, false, true), arr_idx);
            currentASTNode()->addChild(lit);
            pending_arr_idx = false;
        }

        if (dynamic_cast<ArraySubscriptExpr*>(ast_stack[i].node)) {
            // push RHS (array index expr) as the 2nd child of this node
            // (so that has to be done next iteration)
            pending_arr_idx = true;
            arr_idx = ast_stack[i].arr_idx;
        }

        pushASTNode(ast_stack[i].node);     // we'll pop at the end
    }

    // base symbol name
    // -> needs to be before the final pending_arr_idx check because if its
    //    parent is ArraySubscriptExpr the LHS/first child should be sym
    pushSymbolAST((Symbol*)sym);

    // check one more time in case the final item had a pending idx
    if (pending_arr_idx) {
        IntegerLiteral* lit = new IntegerLiteral(new BuiltinType("int", 4, false, true), arr_idx);
        currentASTNode()->addChild(lit);
        pending_arr_idx = false;
    }

    // pop off all the AST nodes we just added
    for (int i = 0; i < ast_stack.size(); i++) {
        popASTNode();
    }

    if ((finalcast != (Datatype *)0)&&(!option_nocasts)) {
        popASTNode();   // createAndPushTypeCast()
    }
}

void ASTBuilder::processSymbolDetail(PendingNode* node)
{
    /** CLS: this logic corresponds to pushSymbolDetail */
    Symbol* sym = node->sym;

    if (!sym) {
        // pushUnnamedLocation
        unimplementedCode("processPendingTerminal: unnamed location");
    } else {
        int4 symboloff = node->high->getSymbolOffset();
        if (symboloff == -1) {
            // perfect match - all good, process below
            // I've just kept this to preserve structure similar to
            // Ghidra code
            if (!sym->getType()->needsResolution()) {
                pushSymbolAST(sym);
                return;
            }
            symboloff = 0;
        }

        if (symboloff + node->vnode->getSize() <= sym->getType()->getSize()) {
            // partial symbol => size+offset
            // partial: STRUCT FIELDS/ARRAYS!
            int4 inslot = node->isRead ? node->op->getSlot(node->vnode) : -1;
            pushPartialSymbol(sym, symboloff, node->vnode->getSize(), node->vnode, node->op, inslot);
        } else {
            pushMismatchSymbolAST(sym, symboloff, node->vnode->getSize(), node->vnode, node->op);
        }
    }
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

    lhs = new PendingNode();
    lhs->node_type = ePendingNodeType::node_symbol;
    lhs->high = hv;
    lhs->sym = sym;
    lhs->vnode = vnode;
    lhs->op = op;
    lhs->mods = mods;   // I don't think this matters here, but just in case
    lhs->isRead = false;

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
        processSymbolDetail(lhs);   // isRead = false set in buildNodeLHS
        popASTNode();

        if (lhs)
            delete lhs;
    }
    else if ((op->doesSpecialPrinting())) {
        // looks like this is for constructors?
        // TODO: this spot also needs isRead = false!
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
    emitLabelStatement(bb);

    if (isSet(only_branch)) {
        const PcodeOp* instr = bb->lastOp();
        if (instr->isBranch()) {
            emitExpression(instr);
        }
    }
    else {
        BinaryOperator* comma_op = nullptr;

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
                // string regname = getRegName(vnode);
                continue;
            }

            emitExpression(instr);
        }

        if (comma_op) {
            popASTNode();   // BinaryOperator(",")
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
    emitAnyLabelStatement(bl);
    bl->subBlock(0)->emit(this);
}

void ASTBuilder::emitBlockGoto(const BlockGoto *bl)
{
    pushMod();
    setMod(no_branch);
    bl->getBlock(0)->emit(this);
    popMod();
                    // Make sure we don't print goto, if it is the
                    // next block to be printed
    if (bl->gotoPrints()) {
        emitGotoStatement(bl->getBlock(0),bl->getGotoTarget(),bl->getGotoType());
    }
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

/**
 * CLS: we should insert a comma_op everywhere we see setMod(comma_separate)
 * so that if it is needed the child nodes will be properly added AS CHILDREN
 * of the comma op (as opposed to Ghidra code where they are using L-R print order
 * instead of top-down AST order)
 * we will prune unnecessary comma ops at the end (ones with only one child) as
 * well as inserting multi-layer comma ops (if there are > 2 children) taking
 * L-R associativity of comma operator into account
 */
BinaryOperator* ASTBuilder::insertCommaOperator(ASTNode* parent)
{
    BinaryOperator* comma_op = new BinaryOperator(",");
    parent->addChild(comma_op);
    return comma_op;
}

void ASTBuilder::emitBlockCondition(const BlockCondition *bl)
{
    // FIXME: get rid of parens and properly emit && and ||
    if (isSet(no_branch)) {
        bl->getBlock(0)->emit(this);
        return;
    }

    if (isSet(only_branch) || isSet(comma_separate)) {
        string opcode = bl->getOpcode() == CPUI_BOOL_AND ? "&&" : "||";
        BinaryOperator* binop = new BinaryOperator(opcode);
        ParenExpr* lhs_parens = new ParenExpr();
        ParenExpr* rhs_parens = new ParenExpr();

        BinaryOperator* parent_op = dynamic_cast<BinaryOperator*>(currentASTNode());
        bool parent_is_comma_op = parent_op && parent_op->opcode() == ",";

        // generate LHS/RHS of conditional
        if (parent_is_comma_op) {
            pushASTNode(parent_op);     // push the comma
        } else {
            pushASTNode(lhs_parens);
        }

        bl->getBlock(0)->emit(this);    // this should be LHS of binop
        popASTNode();   // LHS Parens OR parent_op

        if (parent_is_comma_op) {
            // ok, TAKE THE LAST CHILD of parent_op and put it under the &&/||
            ASTNode* last_child = parent_op->children()->back();
            parent_op->children()->pop_back();

            if (!last_child) {
                throw LowlevelError("There was no LHS of conditional!");
            }

            if (parent_op->children()->size() > 0) {
                // this is a legit comma operator, there is a LHS of the comma remaining
                // In this case, Ghidra's comma logic is screwed up and the last_child
                // is placed under binop directly, while the "LHS" parens are actually
                // surrounding the entire condition
                currentASTNode()->replaceWithNodeShallow(lhs_parens);
                lhs_parens->addChild(currentASTNode());
                currentASTNode()->addChild(binop);
                binop->addChild(last_child);
                binop->addChild(rhs_parens);
            } else {
                // false alarm - comma will be pruned
                lhs_parens->addChild(last_child);
                binop->addChild(lhs_parens);
                binop->addChild(rhs_parens);
                currentASTNode()->addChild(binop);
            }
        } else {
            binop->addChild(lhs_parens);
            binop->addChild(rhs_parens);
            currentASTNode()->addChild(binop);
        }

        pushMod();
        unsetMod(only_branch);
        setMod(comma_separate);     // Notice comma_separate placed only on second block
        BinaryOperator* comma_op = insertCommaOperator(rhs_parens);

        pushASTNode(comma_op);
        bl->getBlock(1)->emit(this);    // this should be RHS of binop
        popASTNode();   // comma_op

        popMod();
    }
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
        emitGotoStatement(condBlock, bl->getGotoTarget(), bl->getGotoType());
    } else {
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
        // NOTE: clang behaves inconsistently (it appears) - sometimes generating
        // a CompoundStmt and sometimes not when there is only one child statement
        // in an else block
        // -> so we just have to account for it in validation and we'll always generate
        // a CompoundStmt with 1 or more children
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

/** @brief mostly copied from PrintC */
void ASTBuilder::emitAnyLabelStatement(const FlowBlock *bl)
{
    if (bl->isLabelBumpUp()) {
        return; // Label printed by someone else
    }
    bl = bl->getFrontLeaf();
    if (bl == (FlowBlock *)0) {
        return;
    }
    emitLabelStatement(bl);
}

/** CLS: mostly copied from PrintC */
void ASTBuilder::emitLabelStatement(const FlowBlock *bl)
{
    if (isSet(only_branch)) {
        return;
    }

    if (isSet(flat)) { // Printing flat version
        if (!bl->isJumpTarget()) {
            return; // Print all jump targets
        }
    }
    else {			// Printing structured version
        if (!bl->isUnstructuredTarget()) {
            return;
        }
        if (bl->getType() != FlowBlock::t_copy) {
            return;
        }
        // Only print labels that have unstructured jump to them
    }

    emitLabel(bl);
}

/** CLS: mostly copied from PrintC::emitLabel */
string ASTBuilder::getEmitLabelName(const FlowBlock *bl)
{
    bl = bl->getFrontLeaf();
    if (!bl) {
        return "";
    }

    BlockBasic *bb = (BlockBasic *)bl->subBlock(0);
    Address addr = bb->getEntryAddr();
    const AddrSpace *spc = addr.getSpace();
    uintb off = addr.getOffset();

    if (!bb->hasSpecialLabel()) {
        if (bb->getType() == FlowBlock::t_basic) {
            const Scope *symScope = ((const BlockBasic *)bb)->getFuncdata()->getScopeLocal();
            Symbol *sym = symScope->queryCodeLabel(addr);
            if (sym) {
                return sym->getName();
            }
        }
    }

    ostringstream lb;
    if (bb->isJoined()) {
        lb << "joined_";
    } else if (bb->isDuplicated()) {
        lb << "dup_";
    } else {
        lb << "code_";
    }
    lb << addr.getShortcut();
    addr.printRaw(lb);
    return lb.str();
}

void ASTBuilder::emitLabel(const FlowBlock *bl)
{
    string label_name = getEmitLabelName(bl);
    if (label_name == "") {
        return;
    }

    LabelStmt* ls = new LabelStmt(label_name, this);
    currentASTNode()->addChild(ls);
    pushASTNode(ls);
}

void ASTBuilder::emitGotoStatement(const FlowBlock *bl,const FlowBlock *exp_bl,uint4 type)
{
    string label_name = "";

    switch(type) {
        case FlowBlock::f_break_goto:
            currentASTNode()->addChild(new BreakStmt());
            break;
        case FlowBlock::f_continue_goto:
            currentASTNode()->addChild(new ContinueStmt());
            break;
        case FlowBlock::f_goto_goto:
            label_name = getEmitLabelName(exp_bl);
            currentASTNode()->addChild(new GotoStmt(label_name));
            break;
        default:
            throw LowlevelError("Unrecognized goto type " + type);
    }
}

void ASTBuilder::emitForLoop(const BlockWhileDo* bl)
{
    // ForStmt
    // 1) init statement
    // 2) conditional
    // 3) increment
    // 4) body
    const PcodeOp* op;

    pushMod();
    unsetMod(no_branch|only_branch);
    // if != nullptr, this has been pushed as currentASTNode
    emitAnyLabelStatement(bl);
    FlowBlock* condBlock = bl->getBlock(0);
    // op = condBlock->lastOp();    // CLS: we don't use this...
    pushMod();
    setMod(comma_separate);
    op = bl->getInitializeOp();     // optional initializer statement

    ForStmt* forstmt = new ForStmt();
    currentASTNode()->addChild(forstmt);
    BinaryOperator* comma_op_init = insertCommaOperator(forstmt);
    pushASTNode(comma_op_init);

    // 1) initializer statement
    if (op) {
        emitExpression(op);
    } else {
        currentASTNode()->addChild(new NullNode());
    }

    popASTNode();   // comma_op_init
    BinaryOperator* comma_op_cond = insertCommaOperator(forstmt);
    pushASTNode(comma_op_cond);

    // 2) conditional statement
    condBlock->emit(this);
    if (comma_op_cond->children()->size() < 1) {
        comma_op_cond->addChild(new NullNode());  // add placeholder if no cond statement
    }

    popASTNode();   // comma_op_cond
    BinaryOperator* comma_op_inc = insertCommaOperator(forstmt);
    pushASTNode(comma_op_inc);

    // 3) increment statement
    op = bl->getIterateOp();
    emitExpression(op);
    if (comma_op_inc->children()->size() < 1) {
        comma_op_inc->addChild(new NullNode());  // add placeholder if no increment statement
    }

    popASTNode();   // comma_op_inc

    popMod();   // pop comma_separate - NO MORE COMMAS below here
    setMod(no_branch); // Dont print goto at bottom of clause

    // 4) loop body
    CompoundStmt* cpd_stmt = new CompoundStmt();
    forstmt->addChild(cpd_stmt);

    pushASTNode(cpd_stmt);
    bl->getBlock(1)->emit(this);
    popASTNode();   // pop CompoundStmt

    popMod();
}

void ASTBuilder::emitBlockWhileDo(const BlockWhileDo *bl)
{
    if (bl->getIterateOp()) {
        emitForLoop(bl);
        return;
    }

    const PcodeOp *op;
    int4 indent;

    // whiledo block NEVER prints final branch

    pushMod();
    unsetMod(no_branch|only_branch);
    emitAnyLabelStatement(bl);
    FlowBlock *condBlock = bl->getBlock(0);
    op = condBlock->lastOp();
    WhileStmt* whilestmt = new WhileStmt();
    currentASTNode()->addChild(whilestmt);
    pushASTNode(whilestmt);

    if (bl->hasOverflowSyntax()) {
        // Print conditional block as
        //     while( true ) {
        //       conditionbody ...
        //       if (conditionalbranch) break;

        // child 1: condition
        push_bool(1, 4);

        // unconditionally add a CompoundStmt for the body (like IfStmt)
        // child 2: body
        CompoundStmt* body = new CompoundStmt();
        currentASTNode()->addChild(body);
        pushASTNode(body);

        pushMod();
        setMod(no_branch);
        condBlock->emit(this);
        popMod();

        // emit->tagOp(KEYWORD_IF,EmitMarkup::keyword_color,op);
        IfStmt* if_stmt = new IfStmt();
        body->addChild(if_stmt);
        pushASTNode(if_stmt);

        // IfStmt conditional
        pushMod();
        setMod(only_branch);
        condBlock->emit(this);
        popMod();

        // then block (break)
        emitGotoStatement(condBlock,(const FlowBlock *)0,FlowBlock::f_break_goto);
        popASTNode();   // IfStmt
    } else {
        // Print conditional block "normally" as
        //     while(condition) {
        pushMod();
        setMod(comma_separate);
        BinaryOperator* comma_op = insertCommaOperator(whilestmt);
        pushASTNode(comma_op);
        condBlock->emit(this);      // child 1: condition
        popASTNode();   // comma_op
        popMod();   // comma mod

        // unconditionally add a CompoundStmt for the body (like IfStmt)
        // child 2: body
        CompoundStmt* body = new CompoundStmt();
        currentASTNode()->addChild(body);
        pushASTNode(body);
    }

    setMod(no_branch);  // Dont print goto at bottom of clause
    bl->getBlock(1)->emit(this);
    popMod();

    popASTNode();   // CompoundStmt
    popASTNode();   // WhileStmt
}

void ASTBuilder::emitBlockDoWhile(const BlockDoWhile *bl)
{
    // dowhile block NEVER prints final branch
    pushMod();
    unsetMod(no_branch|only_branch);
    emitAnyLabelStatement(bl);

    DoStmt* do_stmt = new DoStmt();
    CompoundStmt* cmpd_stmt = new CompoundStmt();
    currentASTNode()->addChild(do_stmt);
    do_stmt->addChild(cmpd_stmt);

    // child 1: body (block w/ no_branch)
    pushASTNode(cmpd_stmt);
    pushMod();
    setMod(no_branch);
    bl->getBlock(0)->emit(this);
    popMod();
    popASTNode();   // CompoundStmt

    // child 2: condition (same block w/ only_branch)
    pushASTNode(do_stmt);
    setMod(only_branch);
    bl->getBlock(0)->emit(this);
    popMod();
    popASTNode();   // DoStmt
}

void ASTBuilder::emitBlockInfLoop(const BlockInfLoop *bl)
{
    const PcodeOp *op;

    pushMod();
    unsetMod(no_branch|only_branch);
    emitAnyLabelStatement(bl);

    DoStmt* do_stmt = new DoStmt();
    CompoundStmt* loop_body = new CompoundStmt();
    do_stmt->addChild(loop_body);      // do nothing
    pushASTNode(loop_body);
    bl->getBlock(0)->emit(this);
    popASTNode();
    popMod();

    pushASTNode(do_stmt);
    push_bool(1, 4);    // while (true)
    popASTNode();

    currentASTNode()->addChild(do_stmt);
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
    // I add the comma operator inside opBranchind since that is where the switch
    // statement is created and the comma needs to be inside its conditional
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
            popASTNode();   // pop case statement
        }
        else {
            FlowBlock* caseblk = bl->getCaseBlock(i);
            caseblk->emit(this);

            if (bl->isExit(i) && (i != bl->getNumCaseBlocks()-1)) {
                BreakStmt* brk = new BreakStmt();
                currentASTNode()->addChild(brk);    // add underneath case stmt
            }

            popASTNode();   // pop case statement
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
    CopyPlaceholder* placeholder = new CopyPlaceholder();
    currentASTNode()->addChild(placeholder);

    PendingExpr* expr = new PendingExpr();
    expr->ast_op = placeholder;
    expr->parts.push_back(buildNodeImplied(op->getIn(0), op, mods));
    _pending_expressions.push_back(expr);
}

void ASTBuilder::opLoad(const PcodeOp *op)
{
    bool usearray = checkArrayDeref(op->getIn(1));
    uint4 m = mods;

    // FIXME: if you set expr->ast_op to a BRAND NEW ASTNode
    // then you can happily use _pending_expressions.push_back(expr) and let
    // it get resolved later on!
    //
    // but if you set expr->ast_op = currentASTNode(), then you
    // HAVE TO GENERATE ITS CHILD BEFORE RETURNING, otherwise if you wait
    // and add this expression to _pending_expressions then you can get the
    // child nodes out of order (since it will be evaluated AFTER the rest
    // of whatever's pending)...e.g. if BinaryOperator child 1 (LHS) does this,
    // then child 2 (already pending) will get added first...in child 1's place
    // as LHS! Then the real LHS expression gets processed later and is pushed
    // at the end...in the RHS spot
    //
    // (only need be 1 level deep...grandchildren can
    // be later by the first rule)

    if (usearray && (!isSet(force_pointer))) {
        m |= print_load_value;
        // process this now since we're adding to currentASTNode()
        PendingNode* node = buildNodeImplied(op->getIn(1), op, m);
        processPendingNode(node);
        delete node;
    }
    else {
        UnaryOperator* deref = new UnaryOperator("*");
        currentASTNode()->addChild(deref);

        PendingExpr* expr = new PendingExpr();
        expr->ast_op = deref;
        expr->parts.push_back(buildNodeImplied(op->getIn(1), op, m));
        _pending_expressions.push_back(expr);
    }
}

// * or ->
// store a value to memory, through a pointer
void ASTBuilder::opStore(const PcodeOp *op)
{
    // we assume the STORE is a statement
    BinaryOperator* binop = new BinaryOperator("=");
    currentASTNode()->addChild(binop);

    uint4 m = mods;
    bool usearray = checkArrayDeref(op->getIn(1));

    if (usearray && (!isSet(force_pointer))) {
        /** NOTE: not sure if setting this flag will have the same effect for me as PrintC... */
        m |= print_store_value;

        PendingExpr* expr = new PendingExpr();
        expr->ast_op = binop;
        PendingNode* lhs = buildNodeImplied(op->getIn(1), op, m);
        PendingNode* rhs = buildNodeImplied(op->getIn(2), op, mods);
        expr->parts.push_back(lhs);
        expr->parts.push_back(rhs);
        _pending_expressions.push_back(expr);
    } else {
        UnaryOperator* deref = new UnaryOperator("*");
        binop->addChild(deref);     // LHS

        // split up the LHS/RHS expressions into separate pending expressions since
        // their expr->ast_ops are different!

        PendingExpr* lhs_expr = new PendingExpr();
        lhs_expr->ast_op = deref;
        PendingNode* lhs = buildNodeImplied(op->getIn(1), op, mods);
        lhs_expr->parts.push_back(lhs);
        _pending_expressions.push_back(lhs_expr);

        PendingExpr* rhs_expr = new PendingExpr();
        rhs_expr->ast_op = binop;
        PendingNode* rhs = buildNodeImplied(op->getIn(2), op, mods);
        rhs_expr->parts.push_back(rhs);
        _pending_expressions.push_back(rhs_expr);
    }
}

void ASTBuilder::opBranch(const PcodeOp *op)
{
    unimplementedOp("opBranch");
}

void ASTBuilder::opCbranch(const PcodeOp *op)
{
    bool yesif = isSet(flat);
    bool yesparen = !isSet(comma_separate);
    // flipped => we take branch if condition is NOT true
    bool booleanflip = op->isBooleanFlip();
    uint4 m = mods;

    if (yesif) {
        unimplementedCode("CLS: I thought we wouldn't be running in flat mode?");
    }

    if (yesparen) {
        // CLS: this appears to correspond to parens "built-in" to the clang AST
        // IfStmt, not an extra set of parens here, so we don't need to push/add
        // them here
    }
    if (booleanflip) {
        // do some magic checking for how to handle negation case
        if (checkPrintNegation(op->getIn(1))) {
            m |= PrintLanguage::negatetoken;
            booleanflip = false;
        }
    }
    if (booleanflip) {
        /** TODO: prepend/insert boolean NOT operator (!) */
        unimplementedCode("insert boolean NOT operator (!) in opCbranch");
    }

    // this corresponds to pushVn(op->getIn(1)); recurse();
    PendingNode* node = buildNodeImplied(op->getIn(1), op, m);
    /** NOTE: not sure if we should do THIS or processExpressionStack() */
    // I think this will work fine for my system because either it gets added
    // to currentASTNode() during processPendingNode, or if not then it gets
    // added in the right spot (with the right parent) later on
    processPendingNode(node);
    delete node;

    if (yesparen) {
        // popASTNode();   // parens
    }

    if (yesif) {
        unimplementedCode("CLS: I thought we wouldn't be running in flat mode? (2nd location)");
    }
}

void ASTBuilder::opBranchind(const PcodeOp *op)
{
    SwitchStmt* ss = new SwitchStmt();
    currentASTNode()->addChild(ss);
    pushASTNode(ss);    // need to push so it's ready for case stmts

    // build switch conditional expression
    PendingExpr* expr = new PendingExpr();

    if (isSet(comma_separate)) {
        BinaryOperator* comma_op = insertCommaOperator(ss);
        expr->ast_op = comma_op;
    } else {
        expr->ast_op = ss;
    }

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
    /**
     * int (*DAT_CORRECT)(int, int, int);
     *  OR OPTION B DEFINITION:
     * int (*DAT_CORRECT)();
     *  AND THIS CALLSITE:
     * x = (*DAT_CORRECT)(3, 4, 5);
     *  GIVES AST:
     * -----------
     * CallExpr
     *  - ParenExpr
     *      - UnaryOperator *
     *          - DeclRefExpr DAT_CORRECT
     *  - IntegerLiteral 3
     *  - IntegerLiteral 4
     *  - IntegerLiteral 5
     */

    CallExpr* callexpr = new CallExpr();
    ParenExpr* parenexpr = new ParenExpr();
    UnaryOperator* unop = new UnaryOperator("*");

    currentASTNode()->addChild(callexpr);
    callexpr->addChild(parenexpr);
    parenexpr->addChild(unop);

    // func pointer expression
    PendingExpr* fptr_expr = new PendingExpr();
    fptr_expr->ast_op = unop;
    fptr_expr->parts.push_back(buildNodeImplied(op->getIn(0), op, mods));
    _pending_expressions.push_back(fptr_expr);

    const Funcdata *fd = op->getParent()->getFuncdata();
    FuncCallSpecs *fc = fd->getCallSpecs(op);
    if (fc == (FuncCallSpecs *)0) {
        throw LowlevelError("Missing indirect function callspec");
    }

    // call arguments expression
    PendingExpr* args_expr = new PendingExpr();
    args_expr->ast_op = callexpr;

    int4 skip = getHiddenThisSlot(op, fc);
    int4 count = op->numInput() - 1;
    count -= (skip < 0) ? 0 : 1;
    if (count > 1) {    // Multiple parameters
        for (int i = 1; i < op->numInput(); i++) {
            if (i == skip) {
                continue;
            }
            args_expr->parts.push_back(buildNodeImplied(op->getIn(i), op, mods));
        }
    } else if (count == 1) {  // One parameter
        if (skip == 1) {
            args_expr->parts.push_back(buildNodeImplied(op->getIn(2), op, mods));
        } else {
            args_expr->parts.push_back(buildNodeImplied(op->getIn(1), op, mods));
        }
    }

    _pending_expressions.push_back(args_expr);
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
        PendingExpr* expr = new PendingExpr();
        expr->ast_op = rs;
        expr->parts.push_back(buildNodeImplied(op->getIn(1), op, mods));
        _pending_expressions.push_back(expr);
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
    if (castStrategy->isZextCast(op->getOut()->getHighTypeDefFacing(), op->getIn(0)->getHighTypeReadFacing(op))) {
        if (option_hide_exts && castStrategy->isExtensionCastImplied(op, readOp)) {
            opHiddenFunc(op);
        } else {
            processTypeCastExpression(op);
        }
    } else {
        unimplementedCode("opIntZext: opFunc case");
    }
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

// creates, adds, and pushed the type cast to the AST
CStyleCastExpr* ASTBuilder::createAndPushTypeCast(Datatype* dt)
{
    CStyleCastExpr* cast = new CStyleCastExpr(toAstType(dt));
    currentASTNode()->addChild(cast);
    pushASTNode(cast);
    return cast;
}

// this corresponds to opTypeCast(op)
void ASTBuilder::processTypeCastExpression(const PcodeOp* op)
{
    if (!option_nocasts) {
        createAndPushTypeCast(op->getOut()->getHighTypeDefFacing());
    }

    // process this now since we're adding to currentASTNode()
    PendingNode* node = buildNodeImplied(op->getIn(0), op, mods);
    processPendingNode(node);
    delete node;

    if (!option_nocasts) {
        popASTNode();   // CStyleCastExpr
    }
}

FunctionType* ASTBuilder::createNewFunctionType(TypeCode* code_type)
{
    const FuncProto* proto = code_type->getPrototype();
    FunctionType* ftype = nullptr;
    if (proto) {
        ftype = new FunctionType(code_type->getName(), toAstType(proto->getOutputType()));
        buildFunctionParamTypes(proto, ftype);
    } else {
        // we could make default return type void? not sure it matters...
        ftype = new FunctionType(code_type->getName(), new BuiltinType("int", 4, false, true));
    }
    return ftype;
}

Type* ASTBuilder::toAstType(const Datatype* dt)
{
    TypeStruct* ghidra_struct = nullptr;
    TypeCode* code_type = nullptr;
    int sid = -1;

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
            ghidra_struct = (TypeStruct*)dt;
            sid = _type_lib.mapStruct(ghidra_struct);
            return new StructType(sid, ghidra_struct);
        case TYPE_ARRAY:
            return new ConstantArrayType((TypeArray*)dt);
        case TYPE_UNKNOWN:
            // convert undefinedX types to Type() and TypedefDeclVisitor will generate
            // the proper typedefs for them
            // -> (we can also change this to generate BuiltinType directly if desired)
            return new Type(dt);
        case TYPE_CODE:
            return createNewFunctionType((TypeCode*)dt);
        case TYPE_SPACEBASE:    // fall-through
        default:
            // (TypeCode*)(dt)->
            // PointerType -> FunctionType (TODO: add this for "code"/function pointer)
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

void ASTBuilder::opHiddenFunc(const PcodeOp *op)
{
    /**
     * CLS: if I understand correctly from their comment, the only purpose of this
     * is to add a parenthetical placeholder that will essentially be evaluated
     * later (as to whether or not it is needed)
     *
     * So I think the easiest way to implement this would be to simply add a
     * ParenExpr with a hidden=true attribute. Then after we've constructed the
     * whole AST, we send a visitor back through all ParenExpr's to evaluate whether
     * or not the hidden ones are still needed. If so, they are left alone. If not,
     * they can be extracted from the AST and replaced with their single child
     * node in the same spot safely
     */
    ParenExpr* parens = new ParenExpr(/*hidden = */ true);
    currentASTNode()->addChild(parens, /*append =*/ true, /*check_parens =*/ false);

    PendingExpr* expr = new PendingExpr();
    expr->ast_op = parens;
    expr->parts.push_back(buildNodeImplied(op->getIn(0), op, mods));
    _pending_expressions.push_back(expr);
}

void ASTBuilder::opIntSext(const PcodeOp *op,const PcodeOp *readOp)
{
    Datatype* outType = op->getOut()->getHighTypeDefFacing();
    Datatype* inType = op->getIn(0)->getHighTypeReadFacing(op);
    if (castStrategy->isSextCast(outType, inType)) {
        /** QUESTION: does option_hide_exts even make sense here? */
        if (option_hide_exts && castStrategy->isExtensionCastImplied(op, readOp)) {
            opHiddenFunc(op);
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

void ASTBuilder::unaryOperator(string opcode, const PcodeOp* op)
{
    UnaryOperator* unop = new UnaryOperator(opcode);
    currentASTNode()->addChild(unop);

    PendingExpr* expr = new PendingExpr();
    expr->ast_op = unop;
    expr->parts.push_back(buildNodeImplied(op->getIn(0), op, mods));
    _pending_expressions.push_back(expr);
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
    binaryOperator("-", op);
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

// integer two's complement (-X)
void ASTBuilder::opInt2Comp(const PcodeOp *op)
{
    unaryOperator("-", op);
}

void ASTBuilder::opIntNegate(const PcodeOp *op)
{
    unimplementedOp("opIntNegate");
}

void ASTBuilder::opIntXor(const PcodeOp *op)
{
    binaryOperator("^", op);
}

void ASTBuilder::opIntAnd(const PcodeOp *op)
{
    binaryOperator("&", op);
}

void ASTBuilder::opIntOr(const PcodeOp *op)
{
    binaryOperator("|", op);
}

void ASTBuilder::opIntLeft(const PcodeOp *op)
{
    binaryOperator("<<", op);
}

void ASTBuilder::opIntRight(const PcodeOp *op)
{
    binaryOperator(">>", op);
}

void ASTBuilder::opIntSright(const PcodeOp *op)
{
    binaryOperator(">>", op);
}

void ASTBuilder::opIntMult(const PcodeOp *op)
{
    binaryOperator("*", op);
}

void ASTBuilder::opIntDiv(const PcodeOp *op)
{
    binaryOperator("/", op);
}

void ASTBuilder::opIntSdiv(const PcodeOp *op)
{
    binaryOperator("/", op);
}

void ASTBuilder::opIntRem(const PcodeOp *op)
{
    binaryOperator("%", op);
}

void ASTBuilder::opIntSrem(const PcodeOp *op)
{
    binaryOperator("%", op);
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
    binaryOperator("==", op, "!=");
}

void ASTBuilder::opFloatNotEqual(const PcodeOp *op)
{
    binaryOperator("!=", op, "==");
}

void ASTBuilder::opFloatLess(const PcodeOp *op)
{
    binaryOperator("<", op, ">=");   // << this should be it, but wait for a test case to verify
}

void ASTBuilder::opFloatLessEqual(const PcodeOp *op)
{
    binaryOperator("<=", op, ">");
}

void ASTBuilder::opFloatNan(const PcodeOp *op)
{
    unimplementedOp("opFloatNan");
}

void ASTBuilder::opFloatAdd(const PcodeOp *op)
{
    binaryOperator("+", op);
}

void ASTBuilder::opFloatDiv(const PcodeOp *op)
{
    binaryOperator("/", op);
}

void ASTBuilder::opFloatMult(const PcodeOp *op)
{
    binaryOperator("*", op);
}

void ASTBuilder::opFloatSub(const PcodeOp *op)
{
    binaryOperator("-", op);
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
    processTypeCastExpression(op);
}

void ASTBuilder::opFloatFloat2Float(const PcodeOp *op)
{
    processTypeCastExpression(op);
}

void ASTBuilder::opFloatTrunc(const PcodeOp *op)
{
    processTypeCastExpression(op);
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
    opFunc(op);
}

// SUB
// extract a subset of bytes
void ASTBuilder::opSubpiece(const PcodeOp *op)
{
    if (op->doesSpecialPrinting()) {    // special printing means it is a field extraction
        const Varnode *vn = op->getIn(0);
        Datatype *ct = vn->getHighTypeReadFacing(op);
        if (ct->isPieceStructured()) {
            int4 offset;
            int4 byteOff = TypeOpSubpiece::computeByteOffsetForComposite(op);
            const TypeField *field = ct->findTruncation(byteOff,op->getOut()->getSize(),op,1,offset);	// Use artificial slot
            if (field && (offset == 0)) {      // A formal structure field
                pushMemberExpression(op, vn, field->name, field->offset, false, mods);
                return;
            } else if (vn->isExplicit() && vn->getHigh()->getSymbolOffset() == -1) {    // An explicit, entire, structured object
                Symbol *sym = vn->getHigh()->getSymbol();
                if (sym != (Symbol *)0) {
                    int4 sz = op->getOut()->getSize();
                    int4 off = (int4)op->getIn(1)->getOffset();
                    off = vn->getSpace()->isBigEndian() ? vn->getSize() - (sz + off) : off;
                    pushPartialSymbol(sym, off, sz, vn, op, -1);
                    return;
                }
            }
            // (else) Fall thru to functional printing
        }
    }

    if (castStrategy->isSubpieceCast(op->getOut()->getHighTypeDefFacing(),
                        op->getIn(0)->getHighTypeReadFacing(op),
                        (uint4)op->getIn(1)->getOffset()))
    {
        processTypeCastExpression(op);
    } else {
        unimplementedCode("SUBPIECE opFunc");
    }
}

void ASTBuilder::opFunc(const PcodeOp* op)
{
    // this is the fdecl we need to refer to in our
    // DeclRefExpr (which is the first child of CallExpr)
    FunctionDecl* fdecl = nullptr;

    string nm = op->getOpcode()->getOperatorName(op);

    if (_fwd_decl_opFunc_funcs.count(nm)) {
        fdecl = _fwd_decl_opFunc_funcs.at(nm);
    }
    else {
        fdecl = buildOpFuncDecl(op, nm);
        _fwd_decl_opFunc_funcs[nm] = fdecl;
    }

    DeclRefExpr* callee_ref = new DeclRefExpr(fdecl);

    CallExpr* callexpr = new CallExpr();
    callexpr->addChild(callee_ref);         // first child is a reference to callee function
    currentASTNode()->addChild(callexpr);

    PendingExpr* expr = new PendingExpr();
    expr->ast_op = callexpr;

    // arguments
    for (int i = 0; i < op->numInput(); i++) {
        expr->parts.push_back(buildNodeImplied(op->getIn(i),op,mods));
    }

    _pending_expressions.push_back(expr);
}

void ASTBuilder::opCast(const PcodeOp *op)
{
    processTypeCastExpression(op);
}

// PTRADD +
// add an offset to a pointer
void ASTBuilder::opPtradd(const PcodeOp *op)
{
    bool printval = isSet(print_load_value|print_store_value);
    uint4 m = mods & ~(print_load_value|print_store_value);

    if (!printval) {
        TypePointer *tp = (TypePointer *)op->getIn(0)->getHighTypeReadFacing(op);
        if (tp->getMetatype() == TYPE_PTR) {
            if (tp->getPtrTo()->getMetatype() == TYPE_ARRAY) {
                printval = true;
            }
        }
    }

    PendingExpr* expr = new PendingExpr();

    if (printval) {
        // array notation
        ArraySubscriptExpr* arrexpr = new ArraySubscriptExpr();
        currentASTNode()->addChild(arrexpr);
        expr->ast_op = arrexpr;
    } else {
        // simple + addition
        BinaryOperator* binop = new BinaryOperator("+");
        currentASTNode()->addChild(binop);
        expr->ast_op = binop;
    }

    PendingNode* lhs = buildNodeImplied(op->getIn(0), op, m);
    PendingNode* rhs = buildNodeImplied(op->getIn(1), op, m);
    expr->parts.push_back(lhs);
    expr->parts.push_back(rhs);
    _pending_expressions.push_back(expr);
}

/** CLS: copied from printc.cc (it was local to that file) */
static bool isValueFlexible(const Varnode *vn)
{
  if ((vn->isImplied())&&(vn->isWritten())) {
    const PcodeOp *def = vn->getDef();
    if (def->code() == CPUI_PTRSUB) return true;
    if (def->code() == CPUI_PTRADD) return true;
  }
  return false;
}

/**
 * @brief Walk inside the newly-created MemberExpr (carefully, using
 * dynamic_cast to verify the structure is what we expect) and retreive
 * the SID from underlying VarDecl's StructType
 *
 * @param memexpr
 * @return int
 */
static int getSidFromMemberExprChild(MemberExpr* memexpr)
{
    // this looks really hacky but if we are in this case we should always
    // end up with this structure (after processPendingNode(structNode)):
    // MemberExpr.inner[0] (DeclRefExpr*)
    //      DeclRefExpr.ref (VarDecl*)
    //          VarDecl.type() (StructType*)
    int sid = -1;
    ASTNode* child = memexpr->children()->front();
    DeclRefExpr* declref = dynamic_cast<DeclRefExpr*>(child);
    if (declref) {
        ValueDecl* val = declref->ref();
        VarDecl* vardecl = dynamic_cast<VarDecl*>(val);
        if (vardecl) {
            Type* vtype = vardecl->type();
            StructType* stype = nullptr;

            // isArrow means this is a pointer to StructType, otherwise it's just StructType
            if (memexpr->isArrow()) {
                PointerType* ptype = dynamic_cast<PointerType*>(vtype);
                if (ptype) {
                    stype = dynamic_cast<StructType*>(ptype->children()->front());
                }
            } else {
                stype = dynamic_cast<StructType*>(vtype);
            }

            if (stype) {
                sid = stype->sid();
            }
        }
    }

    if (sid == -1) {
        throw LowlevelError("We failed to access the SID from a MemberExpr");
    }

    return sid;
}

void ASTBuilder::pushMemberExpression(const PcodeOp* op, const Varnode* struct_vn,
    string fieldname, int member_offset, bool is_arrow, uint4 mods)
{
    MemberExpr* memexpr = new MemberExpr(fieldname, member_offset, /* isArrow = */ is_arrow);
    currentASTNode()->addChild(memexpr);

    PendingNode* structNode = buildNodeImplied(struct_vn, op, mods);
    pushASTNode(memexpr);
    processPendingNode(structNode);
    popASTNode();
    delete structNode;

    // retrieve sid from memexpr's child node (just created above within
    // processPendingNode(structNode))
    memexpr->setSid(getSidFromMemberExprChild(memexpr));
}

// PTRSUB . or -> - Dereference a subfield from a pointer
void ASTBuilder::opPtrsub(const PcodeOp *op)
{
    /**
     * CLS: trying to get this DONE - structure simply mirrored from
     * PrintC::opPtrsub()
    */
    const Varnode* in0;
    uintb in1const;
    TypePointer* ptype;
    TypePointerRel* ptrel;
    Datatype* ct;
    bool valueon,flex,arrayvalue;
    uint4 m;

    in0 = op->getIn(0);
    in1const = op->getIn(1)->getOffset();
    ptype = (TypePointer*)in0->getHighTypeReadFacing(op);
    if (ptype->getMetatype() != TYPE_PTR) {
        // we won't get here - main decompiler fails in this case
        throw LowlevelError("PTRSUB off of non-pointer type");
    }

    if (ptype->isFormalPointerRel() && ((TypePointerRel*)ptype)->evaluateThruParent(in1const)) {
        ptrel = (TypePointerRel*) ptype;
        ct = ptrel->getParent();
    } else {
        ptrel = nullptr;
        ct = ptype->getPtrTo();
    }

    m = mods & ~(print_load_value|print_store_value); // Current state of mods
    valueon = (mods & (print_load_value|print_store_value)) != 0;
    flex = isValueFlexible(in0);

    if (ct->getMetatype() == TYPE_STRUCT || ct->getMetatype() == TYPE_UNION) {

        // ------------ TYPE_STRUCT | TYPE_UNION ------------
        uintb suboff = in1const;    // how far into container
        if (ptrel) {
            unimplementedCode("PTRSUB TYPE_STRUCT ptrel non-NULL");
            return;
        }
        suboff = AddrSpace::addressToByte(suboff, ptype->getWordSize());
        string fieldname;
        Datatype *fieldtype;
        int4 fieldid;
        int4 newoff;
        if (ct->getMetatype() == TYPE_UNION) {
            unimplementedCode("PTRSUB TYPE_UNION case");
            return;
        } else {
            // TYPE_STRUCT
            const TypeField *fld = ct->findTruncation((int4)suboff,0,op,0,newoff);
            if (fld == nullptr) {
                unimplementedCode("PTRSUB TYPE_STRUCT fld == nullptr");
                return;
            } else {
                fieldname = fld->name;
                fieldtype = fld->type;
                fieldid = fld->ident;
            }
        }

        arrayvalue = false;
        // The '&' is dropped if the output type is an array
        if (fieldtype && (fieldtype->getMetatype()==TYPE_ARRAY)) {
            arrayvalue = valueon;	// If printing value, use [0]
            valueon = true;		// Don't print &
        }

        if (!valueon) {
            // print an ampersand
            unimplementedCode("PTRSUB TYPE_STRUCT !valueon case");
            return;
        } else {
            // no ampersand
            if (arrayvalue) {
                unimplementedCode("PTRSUB TYPE_STRUCT arrayvalue case");
                return;
            }
            if (flex) {
                // EMIT ( ).name
                unimplementedCode("PTRSUB TYPE_STRUCT flex case");
                return;
            } else {
                // EMIT ( )->name

                if (ptrel) {
                    unimplementedCode("PTRSUB TYPE_STRUCT !flex ptrel");
                    return;
                }

                // in0 is the var (the struct)
                // fieldname is the field name
                pushMemberExpression(op, in0, fieldname, suboff, true, m);
            }

            if (arrayvalue) {
                unimplementedCode("PTRSUB TYPE_STRUCT 2nd arrayvalue case?");
                return;
            }
        }


    } else if (ct->getMetatype() == TYPE_SPACEBASE) {

        // ------------ TYPE_SPACEBASE ------------
        HighVariable* high = op->getIn(1)->getHigh();
        Symbol* symbol = high->getSymbol();
        arrayvalue = false;

        if (symbol) {
            ct = symbol->getType();     // ct is now type of getIn(1)

            if (ct->getMetatype() == TYPE_ARRAY) {
                // & is dropped if the output type is an array
                arrayvalue = valueon;   // If printing value, use [0]
                valueon = true;         // If printing ptr, don't use &
            } else if (ct->getMetatype() == TYPE_CODE) {
                valueon = true;     // If printing ptr, don't use &
                // CLS: does valueon mean "print the variable, not the address of variable?"
                // CLS: does arrayvalue mean "print as an array value [i]"
            }
        }

        bool need_to_pop_astnode = false;

        if (!valueon) {
            // EMIT &name
            UnaryOperator* unary = new UnaryOperator("&");
            currentASTNode()->addChild(unary);
            pushASTNode(unary);
            need_to_pop_astnode = true;
        } else {
            // EMIT name
            if (arrayvalue) {
                // pushOp(subscript)
                unimplementedCode("PTRSUB valeuon case (arrayvalue = TRUE)");
            }
        }

        if (!symbol) {
            TypeSpacebase *sb = (TypeSpacebase *)ct;
            Address addr = sb->getAddress(in1const,in0->getSize(),op->getAddr());
            pushUnnamedLocation(addr,(Varnode *)0,op);
        } else {
            int4 off = high->getSymbolOffset();
            if (off == 0) {
                pushSymbolAST(symbol);
            } else {
                pushPartialSymbol(symbol, off, 0, nullptr, op, -1);
            }
        }

        if (arrayvalue) {
            unimplementedCode("PTRSUB arrayvalue case");
        }

        if (need_to_pop_astnode) {
            popASTNode();
        }

    } else if (ct->getMetatype() == TYPE_ARRAY) {

        // ------------ TYPE_ARRAY ------------
        unimplementedCode("PTRSUB ct ARRAY");
    } else {
        // we won't get here - main decompiler fails in this case
        throw LowlevelError("PTRSUB off of non structured pointer type");
    }
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
