#pragma once

#include <deque>
#include <fstream>
#include <vector>

// #include "funcdata.hh"
#include "printc.hh"
#include "ast.h"


class ghidra::Funcdata;
struct PendingExpr;
struct PendingNode;

using namespace ghidra;

/** helper functions I haven't moved elsewhere  yet */
template<typename T> string to_hex(T data);

/**
 * @brief Returns a version of filename that is valid on Windows and Linux
 * by removing/replacing bad characters
 */
string ensureValidFilename(string filename);

/// \brief Factory and static initializer for the "ast-builder" back-end to the decompiler
///
/// The singleton adds itself to the list of possible back-end languages for the decompiler.
/// I needed this because there was state that wasn't getting initialized properly just
/// pointing to PrintC's ghidra architecture pointer (they reach inside each other
/// because tight coupling is great :P)
class ASTBuilderCapability : public PrintLanguageCapability {
  static ASTBuilderCapability astBuilderCapability;     ///< The singleton instance
  ASTBuilderCapability(void);                       ///< Initialize the singleton
  ASTBuilderCapability(const ASTBuilderCapability &op2);                ///< Not implemented
  ASTBuilderCapability &operator=(const ASTBuilderCapability &op);      ///< Not implemented
public:
  virtual PrintLanguage *buildLanguage(Architecture *glb);
};

class ASTBuilder : public PrintC
{
public:
    /**
     * @brief Construct a new AST Builder object
     *
     * @param is the ghidra Architecture pointer
     * @param fbody is the CompoundStmt node (child to FunctionDecl node) that
     * contains the function body statements
     */
    ASTBuilder(Architecture* ghidra, string logfolder);
    ~ASTBuilder();

    /**
     * --------------------------------------------------------------
     * PrintLanguage methods/interface
     * --------------------------------------------------------------
    */

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
    // --- END BlockVisitor interface

    // hides PrintC functions:
    void opHiddenFunc(const PcodeOp *op);
    void emitAnyLabelStatement(const FlowBlock *bl);
    void emitLabelStatement(const FlowBlock *bl);
    void emitLabel(const FlowBlock *bl);
    /**
     * @brief Performs the emitLabel logic from PrintC to determine the
     * label name, but returns it as a string instead of a LabelStmt*.
     * This allows other functions (emitGotoStatement) to generate the label
     * name without the LabelStmt
     *
     * An empty string indicates no label was generated, and a null LabelStmt
     * should be returned if called from emitLabel
     *
     * @param bl
     * @return string
     */
    string getEmitLabelName(const FlowBlock *bl);

    /**
     * @brief This can return a BreakStmt, ContinueStmt, or GotoStmt depending
     * on the supplied type
     */
    void emitGotoStatement(const FlowBlock *bl,const FlowBlock *exp_bl,uint4 type);
    void emitForLoop(const BlockWhileDo* bl);

    virtual void emitExpression(const PcodeOp *op);

    // analogous to opBinary, just wanted a different name
    void binaryOperator(string opcode, const PcodeOp* op, string negateOpcode="");
    // analagous to opUnary, just wanted a different name
    void unaryOperator(string opcode, const PcodeOp* op);

    virtual void opCopy(const PcodeOp *op);
    virtual void opLoad(const PcodeOp *op);
    virtual void opStore(const PcodeOp *op);
    virtual void opBranch(const PcodeOp *op);
    virtual void opCbranch(const PcodeOp *op);
    virtual void opBranchind(const PcodeOp *op);
    virtual void opCall(const PcodeOp *op);
    virtual void opCallind(const PcodeOp *op);
    virtual void opCallother(const PcodeOp *op);
    virtual void opConstructor(const PcodeOp *op,bool withNew);
    virtual void opReturn(const PcodeOp *op);
    virtual void opIntEqual(const PcodeOp *op);
    virtual void opIntNotEqual(const PcodeOp *op);
    virtual void opIntSless(const PcodeOp *op);
    virtual void opIntSlessEqual(const PcodeOp *op);
    virtual void opIntLess(const PcodeOp *op);
    virtual void opIntLessEqual(const PcodeOp *op);
    virtual void opIntZext(const PcodeOp *op,const PcodeOp *readOp);
    virtual void opIntSext(const PcodeOp *op,const PcodeOp *readOp);
    virtual void opIntAdd(const PcodeOp *op);
    virtual void opIntSub(const PcodeOp *op);
    virtual void opIntCarry(const PcodeOp *op);
    virtual void opIntScarry(const PcodeOp *op);
    virtual void opIntSborrow(const PcodeOp *op);
    virtual void opInt2Comp(const PcodeOp *op);
    virtual void opIntNegate(const PcodeOp *op);
    virtual void opIntXor(const PcodeOp *op);
    virtual void opIntAnd(const PcodeOp *op);
    virtual void opIntOr(const PcodeOp *op);
    virtual void opIntLeft(const PcodeOp *op);
    virtual void opIntRight(const PcodeOp *op);
    virtual void opIntSright(const PcodeOp *op);
    virtual void opIntMult(const PcodeOp *op);
    virtual void opIntDiv(const PcodeOp *op);
    virtual void opIntSdiv(const PcodeOp *op);
    virtual void opIntRem(const PcodeOp *op);
    virtual void opIntSrem(const PcodeOp *op);
    virtual void opBoolNegate(const PcodeOp *op);
    virtual void opBoolXor(const PcodeOp *op);
    virtual void opBoolAnd(const PcodeOp *op);
    virtual void opBoolOr(const PcodeOp *op);
    virtual void opFloatEqual(const PcodeOp *op);
    virtual void opFloatNotEqual(const PcodeOp *op);
    virtual void opFloatLess(const PcodeOp *op);
    virtual void opFloatLessEqual(const PcodeOp *op);
    virtual void opFloatNan(const PcodeOp *op);
    virtual void opFloatAdd(const PcodeOp *op);
    virtual void opFloatDiv(const PcodeOp *op);
    virtual void opFloatMult(const PcodeOp *op);
    virtual void opFloatSub(const PcodeOp *op);
    virtual void opFloatNeg(const PcodeOp *op);
    virtual void opFloatAbs(const PcodeOp *op);
    virtual void opFloatSqrt(const PcodeOp *op);
    virtual void opFloatInt2Float(const PcodeOp *op);
    virtual void opFloatFloat2Float(const PcodeOp *op);
    virtual void opFloatTrunc(const PcodeOp *op);
    virtual void opFloatCeil(const PcodeOp *op);
    virtual void opFloatFloor(const PcodeOp *op);
    virtual void opFloatRound(const PcodeOp *op);
    virtual void opMultiequal(const PcodeOp *op);
    virtual void opIndirect(const PcodeOp *op);
    virtual void opPiece(const PcodeOp *op);
    virtual void opSubpiece(const PcodeOp *op);
    virtual void opCast(const PcodeOp *op);
    virtual void opPtradd(const PcodeOp *op);
    virtual void opPtrsub(const PcodeOp *op);
    virtual void opSegmentOp(const PcodeOp *op);
    virtual void opCpoolRefOp(const PcodeOp *op);
    virtual void opNewOp(const PcodeOp *op);
    virtual void opInsertOp(const PcodeOp *op);
    virtual void opExtractOp(const PcodeOp *op);
    virtual void opPopcountOp(const PcodeOp *op);
    virtual void opLzcountOp(const PcodeOp *op);
    /** -------------------------------------------------------------- */

    /**
     * @brief Builds an AST representation of the given function. The returned
     * pointer must be freed by the caller.
     */
    ASTNode* buildAST(Funcdata* fd);

    /**
     * @brief Pushes current_node onto the top of the AST stack
     */
    void pushASTNode(ASTNode* current_node);

    /**
     * @brief Pops and returns the node that was at the top of
     * the stack
     */
    ASTNode* popASTNode();

    /**
     * @brief Returns a reference to the current node (at the top of
     * the stack)
     */
    ASTNode* currentASTNode()
    {
        return _ast_node_stack.back();
    }

    std::string getFullTypeString(const Datatype* dt);
    std::string getTypeStringStart(const Datatype* dt);
    std::string getTypeStringEnd(const Datatype* dt);
    std::string getProtoInputString(const FuncProto* proto);

    StructTypeLibrary* type_library() { return &_type_lib; }

    Type* toAstType(const Datatype* dt);

protected:

    /**
     * @brief Called at control-flow breaks to check if we've been adding code
     * to a LabelStmt and if so, pop the LabelStmt from the ASTNode stack so
     * we don't group more code underneath this label (because control flow is
     * moving on)
     */
    // void endPendingLabel()
    // {
    //     if (_pending_label) {
    //         popASTNode();
    //     }
    // }

    /**
     * @brief Build a FunctionDecl for the given function.
     *
     * @param fd The function
     * @param fwd_decl only build the prototype info for a forward declaration,
     * don't include the function body in a CompoundStmt child of this node
     */
    FunctionDecl* buildFunctionDecl(Funcdata* fd, bool fwd_decl);
    void buildFunctionParams(FuncProto& fp, FunctionDecl* fdecl);
    void buildFunctionParamTypes(const FuncProto* fp, FunctionType* ftype);
    void buildLocalDeclsFromScope(const Scope& scope, CompoundStmt* fbody);
    VarDecl* tryCreateLocalVarDecl(const SymbolEntry* sym_entry);
    FunctionType* createNewFunctionType(TypeCode* code_type);
    EnumDecl* createNewEnumDecl(TypeEnum* enum_type);

    Type* getOpFuncOutputType(int out_size, string opFuncName, bool is_bool = false);

    /**
     * @brief Build a FunctionDecl for an opFunc function
     * (e.g. CONCAT32(x,y) or CARRY4(x,y))
     */
    FunctionDecl* buildOpFuncDecl(const PcodeOp* op, string name);

    /**
     * @brief Build a PendingNode for this implied varnode
     */
    PendingNode* buildNodeImplied(const Varnode* vn, const PcodeOp* op, uint4 modflags);

    /**
     * @brief Build a PendingNode for this LHS varnode
     */
    PendingNode* buildNodeLHS(const Varnode* vnode, const PcodeOp* op);

    /**
     * @brief Processes a pending node from the parts of an expression
     */
    void processPendingNode(PendingNode* node);

    /** @brief Process a pending implied/temporary node */
    void processPendingTemporary(PendingNode* node);

    /** @brief Process a pending constant node */
    void processPendingConstant(uintb value, Datatype* dt, const Varnode* vn, const PcodeOp* op);

    /** @brief Process a pending terminal node
     * (corresponds to pushVnExplicit) */
    void processPendingTerminal(PendingNode* node);

    /**
     * @brief Process a pending symbol node
     */
    void processSymbolDetail(PendingNode* node);

    void pushSymbolAST(Symbol* sym, uintb instr_addr);

    // similar to PrintC version but no const
    void pushMismatchSymbolAST(Symbol *sym,int4 off,int4 sz,
            const Varnode *vn,const PcodeOp *op);

    virtual void pushUnnamedLocation(const Address &addr, const Varnode *vn,const PcodeOp *op);

    virtual void pushPartialSymbol(const Symbol *sym,int4 off,int4 sz,
            const Varnode *vn,const PcodeOp *op,int4 inslot);

    virtual bool pushEquate(uintb val,int4 sz,const EquateSymbol *sym,
                const Varnode *vn,const PcodeOp *op);

    /**
     * @brief Recursively process the expression stack, converting each
     * expression into ASTNodes and adding sub-expressions to the stack
     * as appropriate
     */
    void processExpressionStack();

    /**
     * @brief Inserts a comma operator underneath the given parent node and
     * returns a pointer to it
     */
    BinaryOperator* insertCommaOperator(ASTNode* parent, uintb instr_addr);

    void push_bool(uintb value, int size, const PcodeOp* op);

    // override PrintC::push_integer for when we don't know the datatype
    virtual void push_integer(uintb val,int4 sz,bool sign, const Varnode *vn,const PcodeOp *op);
    // this is the version to call if we DO know the datatype
    void push_integer(Datatype* dt, uintb val,int4 sz,bool sign, const Varnode *vn,const PcodeOp *op);

    // override PrintC::push_float
    virtual void push_float(uintb val,int4 sz,const Varnode *vn, const PcodeOp *op);
    // this is the version to call if we DO know the datatype
    void push_float(Datatype* dt, uintb val,int4 sz,const Varnode *vn, const PcodeOp *op);

    void createCharConstant(Datatype* ct, uintb val, const Varnode* vn, const PcodeOp* op);
    void createEnumConstant(uintb val,const TypeEnum *dt,const Varnode *vn, const PcodeOp *op);
    bool createPtrCharConstant(TypePointer* pt, uintb value, const Varnode* vn, const PcodeOp* op);
    bool createPtrCodeConstant(TypePointer* pt, uintb value, const Varnode* vn, const PcodeOp* op);
    void processTypeCastExpression(const PcodeOp *op);
    CStyleCastExpr* createAndPushTypeCast(Datatype* dt, const PcodeOp* op);

    // hide PrintC::opFunc()
    void opFunc(const PcodeOp* op);

    void pushMemberExpression(const PcodeOp* op, const Varnode* struct_vn,
                              string fieldname, int member_offset, bool is_arrow, uint4 mods, int sid);

    /** temp functions for logging spots I need to implement */
    void unimplementedCode(std::string description);
    void unimplementedOp(std::string opname);

    std::deque<PendingExpr*> _pending_expressions;

    vector<string> constructStructDepOrder();
    string get_struct_dep_from_field(const TypeField* ghidra_field, StructType& stype,
                                    int loop_max, int pointer_resolve_threshold);

    /**
     * The AST node stack provides our current context within the AST as we
     * build it. The top of the stack holds the current node into which we are
     * "emitting" the AST.
     *
     * This could have been passed as a parameter to each function call, but due
     * to the Ghidra interface I'm conforming to it's easier to maintain state
     * separately like they did
     */
    vector<ASTNode*> _ast_node_stack;

    /**
     * The translation unit that is the head of the current AST
     */
    TranslationUnitDecl* _head_translation_unit;

    // Maintain maps to allow looking up ValueDecl's we've already
    // created by their Symbol (for DeclRefExpr's which point to them, etc)
    std::map<Symbol*, VarDecl*> _locals;
    std::map<Symbol*, VarDecl*> _mismatch_locals;
    std::map<Symbol*, ParmVarDecl*> _parameters;
    std::map<Symbol*, VarDecl*> _globals;
    std::map<string, VarDecl*> _unnamedLoc_globals;     // treating unnamedLocations as globals
    std::map<Symbol*, FunctionDecl*> _fwd_decl_funcs;
    std::map<string, FunctionDecl*> _fwd_decl_other_funcs;
    // references to globals where the size doesn't match the
    // variable size (Ghidra indicates this with _VARNAME)
    // each original Symbol* maps to a list of VarDecls, with
    // entries being added only if they introduce a reference with
    // a size not already present in the list of VarDecls
    std::map<string, VarDecl*> _mismatch_globals;

    /**
     * The structure type library which contains the mapping of structure names
     * (which must be unique in this context) to structure ids
     */
    StructTypeLibrary _type_lib;

    std::map<string, EnumDecl*> _enum_decls;

    // counter to generate unique ValueDecl ids within a given context
    // ** This includes globals, locals, and function decls **
    // (TranslationUnitDecl for now). This can be reset for various contexts if
    // appropriate.
    int _next_vdecl_id;

    /**
     * @brief wrap access to _logfile so I can monitor whether we ever write to
     * the logfile or not - if not, I won't ever create it
     */
    ofstream& get_logfile();

    string _logfolder;
    ofstream _logfile;  // log unimplemented code for review
    string _logfile_name;
    bool _logfile_created;
    ASTCallbacks _ast_callbacks;
    string _original_ghidra_printlang_name;     // save so we can restore at the end
    // LabelStmt* _pending_label;
};

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

/**
 * @brief Same idea as getPcodeOps above, but here we wrap TypeStruct
 * beginField/endField iterators
 */
class getStructFields
{
public:
    getStructFields(const TypeStruct* ts)
        : _ts(ts)
    { }

    vector<TypeField>::const_iterator begin() const { return _ts->beginField(); }
    vector<TypeField>::const_iterator end() const { return _ts->endField(); }

    const TypeStruct* _ts;
};

class getEnumFields
{
public:
    getEnumFields(const TypeEnum* t)
        : _t(t)
    { }

    map<uintb,string>::const_iterator begin() const { return _t->beginEnum(); }
    map<uintb,string>::const_iterator end() const { return _t->endEnum(); }

    const TypeEnum* _t;
};