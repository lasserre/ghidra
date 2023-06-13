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
    LabelStmt* emitAnyLabelStatement(const FlowBlock *bl);
    LabelStmt* emitLabelStatement(const FlowBlock *bl);
    LabelStmt* emitLabel(const FlowBlock *bl);
    void emitForLoop(const BlockWhileDo* bl);

    virtual void emitExpression(const PcodeOp *op);

    // analogous to opBinary, just wanted a different name
    void binaryOperator(string opcode, const PcodeOp* op, string negateOpcode="");

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
     * @brief Build a FunctionDecl for the given function.
     *
     * @param fd The function
     * @param fwd_decl only build the prototype info for a forward declaration,
     * don't include the function body in a CompoundStmt child of this node
     */
    FunctionDecl* buildFunctionDecl(Funcdata* fd, bool fwd_decl);
    void buildFunctionParams(FuncProto& fp, FunctionDecl* fdecl);
    void buildLocalDeclsFromScope(const Scope& scope, CompoundStmt* fbody);
    VarDecl* tryCreateLocalVarDecl(const SymbolEntry* sym_entry);

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
    void processPendingConstant(PendingNode* node);

    /** @brief Process a pending terminal node
     * (corresponds to pushVnExplicit) */
    void processPendingTerminal(PendingNode* node);

    /**
     * @brief Process a pending symbol node
     */
    void processPendingSymbol(PendingNode* node);

    /**
     * @brief Recursively process the expression stack, converting each
     * expression into ASTNodes and adding sub-expressions to the stack
     * as appropriate
     */
    void processExpressionStack();

    void createIntLiteral(Datatype* dt, uintb value);
    void createCharConstant(Datatype* dt, uintb value, const Varnode* vn);
    bool createPtrCharConstant(TypePointer* pt, uintb value, const Varnode* vn, const PcodeOp* op);
    bool createPtrCodeConstant(TypePointer* pt, uintb value, const Varnode* vn, const PcodeOp* op);
    void processTypeCastExpression(const PcodeOp *op);
    CStyleCastExpr* createTypeCast(Datatype* dt);

    /** temp functions for logging spots I need to implement */
    void unimplementedCode(std::string description);
    void unimplementedOp(std::string opname);

    std::deque<PendingExpr*> _pending_expressions;

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
    std::map<Symbol*, ParmVarDecl*> _parameters;
    std::map<Symbol*, VarDecl*> _globals;
    std::map<Symbol*, FunctionDecl*> _fwd_decl_funcs;
    // references to globals where the size doesn't match the
    // variable size (Ghidra indicates this with _VARNAME)
    // each original Symbol* maps to a list of VarDecls, with
    // entries being added only if they introduce a reference with
    // a size not already present in the list of VarDecls
    std::map<Symbol*, vector<VarDecl*>> _mismatch_globals;

    /**
     * The structure type library which contains the mapping of structure names
     * (which must be unique in this context) to structure ids
     */
    StructTypeLibrary _type_lib;

    // counter to generate unique ValueDecl ids within a given context
    // ** This includes globals, locals, and function decls **
    // (TranslationUnitDecl for now). This can be reset for various contexts if
    // appropriate.
    int _next_vdecl_id;

    string _logfolder;
    ofstream _logfile;  // log unimplemented code for review
    ASTCallbacks _ast_callbacks;
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