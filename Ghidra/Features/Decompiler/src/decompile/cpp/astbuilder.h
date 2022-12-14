#pragma once

#include <vector>

#include "printc.hh"
#include "ast.h"

class Funcdata;
struct PendingExpr;
struct PendingNode;

/** helper functions I haven't moved elsewhere  yet */
template<typename T> string to_hex(T data);

class ASTBuilder : public PrintC
{
public:
    /**
     * @brief Construct a new AST Builder object
     *
     * @param fbody is the CompoundStmt node (child to FunctionDecl node) that
     * contains the function body statements
     */
    ASTBuilder();

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

    virtual void emitExpression(const PcodeOp *op);

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

protected:

    void buildFunctionParams(FuncProto& fp, FunctionDecl* fdecl);
    void buildLocalDeclsFromScope(const Scope& scope, CompoundStmt* fbody);
    VarDecl* tryCreateLocalVarDecl(const SymbolEntry* sym_entry);

    /**
     * @brief Build a PendingNode for this implied varnode
     */
    PendingNode* buildNodeImplied(const Varnode* vn, const PcodeOp* op);

    /**
     * @brief Build a PendingNode for this LHS varnode
     */
    PendingNode* buildNodeLHS(const Varnode* vn, const PcodeOp* op);

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

    vector<PendingExpr*> _pending_expressions;

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

    // Maintain maps to allow looking up ValueDecl's we've already
    // created by their Symbol (for DeclRefExpr's which point to them, etc)
    std::map<Symbol*, VarDecl*> _locals;
    std::map<Symbol*, ParmVarDecl*> _parameters;
    std::map<Symbol*, VarDecl*> _globals;

    // counter to generate unique ValueDecl ids within a given context
    // (FunctionDecl for now). This can be reset for various contexts if
    // appropriate
    int _next_vdecl_id;
};
