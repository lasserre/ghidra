#pragma once

#include <cctype>
#include <vector>
#include <string>
#include <functional>

#include "types.h"
#include "funcdata.hh"

class ASTBuilder;
class ASTVisitor;
class ValueDecl;

class Type;
class BuiltinType;
class StructType;

// using namespace std;
using std::string;
using namespace ghidra;

string getValidCLanguageName(string ghidra_name);

/**
 * @brief Yes, this is bad design but I need to leave the unimplementedCode
 * logging function associated with ASTBuilder state and have the toAstType()
 * function be able to call this...but also have these classes be able to call
 * toAstType(). This is my hacky workaround that I'm doing to simply get the job
 * done...I know it's not pretty lol. Once everything works, we can remove this
 * as I don't expect to use the unimplementedCode callback at all
 */
struct ASTCallbacks
{
    std::function<Type*(const Datatype* dt)> toAstTypeCallback;
    // Type* (*toAstTypeCallback)(const Datatype* dt, void* context);
    // void* context;

    std::function<void(string)> unimplementedCodeCallback;

    // this one is actually called by AST classes
    Type* toAstType(const Datatype* dt)
    {
        return toAstTypeCallback(dt);
        // return toAstTypeCallback(dt, context);
    }
};

void initASTCallbacks(ASTCallbacks* cb);

/**
 * @brief Represents a single node in the AST
 */
class ASTNode
{
public:
    ASTNode(uintb instr_addr = 0);
    virtual ~ASTNode();
    virtual ASTNode* clone() = 0;

    /**
     * @brief Clone the specialized members associated with this node in the
     * hierarchy. If this gets overriden, it should call ASTNode::clone_my_members()
     * first.
     *
     * ANYTHING DELETED SHOULD BE CLONED. ANYTHING NOT DELETED SHOULD NOT BE.
     */
    virtual ASTNode* clone_my_members(ASTNode* node)
    {
        std::vector<ASTNode*> clones;
        for (auto child : _children) {
            clones.push_back(child->clone());
        }
        node->_children = clones;
        return node;
    }

    inline ASTNode* parent() { return _parent; }
    /**
     * @brief You should normally never need to do this, I just have a few
     * unique situations where I need to update the parent (these could probably
     * be rearchitected such that I don't even need this function...but I'm in
     * a hurry)
    */
    void setParent(ASTNode* parent) { _parent = parent; }
    inline std::vector<ASTNode*>* children() { return &_children; }

    /**
     * @brief Returns the index of the given child pointer or -1 if not found.
     * This is intended to be used on known pointer values from children(), not
     * equivalent objects based on their content
     */
    int indexOfChild(ASTNode* child)
    {
        for (int i = 0; i < _children.size(); i++) {
            if (child == _children[i]) {
                return i;
            }
        }
        return -1;
    }

    /**
     * @brief Adds child to this node's children, if it is not already
     * in the list (the pointer value itself, not the object value).
     *
     * @param child is the child to add
     * @param append If true, add to end of list. Otherwise add to beginning
     */
    virtual void addChild(ASTNode* child, bool append=true, bool check_parens=true);

    /**
     * @brief Removes this node and replaces it with new_node in the AST.
     * This node's children are moved over to become children of new_node,
     * and new_node takes the place of this node with respect to its parent
     *
     * @param replacement
     * @returns a pointer to this node which may now be deleted
     */
    ASTNode* replaceWith(ASTNode* new_node);

    /**
     * @brief Removes this node and replaces it with new_node in the AST.
     * The difference from replaceWith() is that this function ONLY replaces
     * this node with new_node but leaves both nodes' children alone...so
     * this can be useful to move subtrees around.
     *
     * When this function returns, this node's parent will be nullptr
     */
    ASTNode* replaceWithNodeShallow(ASTNode* new_node);

    void accept(ASTVisitor*, void* context=nullptr);

    /**
     * @brief Return the precedence level of this node, or -1 if it doesn't
     * apply/doesn't have one.
     *
     * The highest precedence is 1 and the lowest is 17, per the table
     * here:
     * https://www.learncpp.com/cpp-tutorial/operator-precedence-and-associativity/
     */
    virtual int precedence() { return -1; }

    /**
     * @brief Return true if LR associative, false otherwise. If there is no
     * associativity for this node, returns false.
     */
    virtual bool isLRAssociative() { return false; }

    /**
     * @brief True if the next child would be on the left side of this
     * node's operation when we add it via addChild(child, append)
     *
     * Note that since append is not supplied, we assume child will be
     * appended. This logic breaks if we try to prepend a child, which would
     * alter the calculations for an existing child who thought it was the
     * first in the list of children.
     *
     * Example 1: for a + b - c, the subexpr a + b would be left of the - op
     * if added
     *
     * Example 2: for *(char*)xyz, the subexpr (char*)xyz would NOT be left
     * of the * operator if added
     */
    virtual bool wouldNextChildBeLeftOfOp() { return false; }

    /**
     * @brief True if this node has a precedence level
    */
    bool hasPrecedence() { return precedence() > 0; }

    /**
     * @brief The address of the instruction corresponding to this AST node, if
     * any such address exists. Default value is 0
     */
    uintb instr_addr() { return _instr_addr; }

    void set_instr_addr(uintb instr_addr)
    {
        _instr_addr = instr_addr;
    }

protected:
    /**
     * @brief This is the node-specific function to perform the accept
     * on a particular kind of ASTNode. The general accept() handles calling
     * both this function as well as recursing through any child nodes
     */
    virtual void* doAccept(ASTVisitor*, void*) = 0;

    ASTNode* _parent;       // pointer to existing parent, not our memory
    // dynamically-allocated child pointers we must free when destructed
    std::vector<ASTNode*> _children;
    uintb _instr_addr;  // the address of the instruction corresponding to this node, if any
};

/**
 * @brief Array subscripting
 *
 * First child: LHS expr (array variable)
 * Second child: RHS expr (expr inside brackets)
 *
 * i.e.: LHS[RHS]
 */
class ArraySubscriptExpr : public ASTNode
{
public:
    ArraySubscriptExpr(const PcodeOp* op)
        : ASTNode(op->getAddr().getOffset())
    { }
    virtual ArraySubscriptExpr* clone()
    {
        return (ArraySubscriptExpr*)clone_my_members(new ArraySubscriptExpr(*this));
    }

    int precedence() { return 2; }
    bool isLRAssociative() { return true; }
    // not sure here...we have children before and after [, and before ]
    // trying this for now
    bool wouldNextChildBeLeftOfOp()
    {
        // first child is left of op
        return children()->size() == 0;
    }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

class BinaryOperator : public ASTNode
{
public:
    BinaryOperator(std::string opcode, const PcodeOp* op);
    BinaryOperator(std::string opcode, uintb instr_addr);
    virtual BinaryOperator* clone()
    {
        return (BinaryOperator*)clone_my_members(new BinaryOperator(*this));
    }

    inline std::string opcode() { return _opcode; }

    int precedence();
    bool isLRAssociative();
    bool wouldNextChildBeLeftOfOp();

    /**
     * @brief True if this node has a precedence level
    */
    bool hasPrecedence() { return precedence() > 0; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    std::string _opcode;
};

class BreakStmt : public ASTNode
{
public:
    BreakStmt();
    virtual BreakStmt* clone()
    {
        return (BreakStmt*)clone_my_members(new BreakStmt(*this));
    }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

/**
 * @brief This is a placeholder node for the COPY pcode op
 *
 * We need to be able to insert a placeholder into the AST for the COPY
 * operation and then later on process its sub-expression. Eventually, we
 * will emit JSON which ignores this node and simply puts its child expression
 * here.
 *
 * The case I was hitting that made me decide to insert this was in CallExpr,
 * some of the parameters were COPY nodes. I was just saving the CallExpr as
 * the ast op (for param 1) and then adding a pending expression to be processed
 * later. In the meantime, I processed param 2 and added it to CallExpr.children
 * ...but now it is sitting in param1's spot! To avoid this I need to keep the
 * CopyPlaceholder node in spot 1 and this will preserve the param ordering
 */
class CopyPlaceholder : public ASTNode
{
public:
    CopyPlaceholder()
    { }
    virtual CopyPlaceholder* clone()
    {
        return (CopyPlaceholder*)clone_my_members(new CopyPlaceholder(*this));
    }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

/**
 * First child: reference to callee
 * Second child: param 1
 * Third child: param 2
 * ...
 */
class CallExpr : public ASTNode
{
public:
    CallExpr(const PcodeOp* op);
    virtual CallExpr* clone()
    {
        return (CallExpr*)clone_my_members(new CallExpr(*this));
    }

    int precedence() { return 2; }
    bool isLRAssociative() { return true; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

/**
 * First child: ConstantExpr case value
 * Second child: code for switch case
 */
class CaseStmt : public ASTNode
{
public:
    CaseStmt();
    virtual CaseStmt* clone()
    {
        return (CaseStmt*)clone_my_members(new CaseStmt(*this));
    }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

/**
 * Single child: code for default switch case
 */
class DefaultStmt : public ASTNode
{
public:
    DefaultStmt()
    { }
    virtual DefaultStmt* clone()
    {
        return (DefaultStmt*)clone_my_members(new DefaultStmt(*this));
    }
protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

class CharacterLiteral : public ASTNode
{
public:
    CharacterLiteral(BuiltinType* type, uintb value, const PcodeOp* op);
    virtual ~CharacterLiteral() { delete _type; }
    virtual CharacterLiteral* clone_my_members(ASTNode* node);

    virtual CharacterLiteral* clone()
    {
        return (CharacterLiteral*)clone_my_members(new CharacterLiteral(*this));
    }

    inline uintb value() { return _value; }
    inline BuiltinType* type() { return _type; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    BuiltinType* _type;
    uintb _value;
};

/**
 * @brief A list of statements
 */
class CompoundStmt : public ASTNode
{
public:
    CompoundStmt();
    virtual CompoundStmt* clone()
    {
        return (CompoundStmt*)clone_my_members(new CompoundStmt(*this));
    }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

class ConstantExpr : public ASTNode
{
public:
    ConstantExpr();
    virtual ConstantExpr* clone()
    {
        return (ConstantExpr*)clone_my_members(new ConstantExpr(*this));
    }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

class ContinueStmt : public ASTNode
{
public:
    ContinueStmt()
    { }
    virtual ContinueStmt* clone()
    {
        return (ContinueStmt*)clone_my_members(new ContinueStmt(*this));
    }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

class CStyleCastExpr : public ASTNode
{
public:
    CStyleCastExpr(Type* type, const PcodeOp* op);

    virtual CStyleCastExpr* clone_my_members(ASTNode* node);

    virtual CStyleCastExpr* clone()
    {
        return (CStyleCastExpr*)clone_my_members(new CStyleCastExpr(*this));
    }

    inline Type* type() { return _type; }

    inline void replace_type(Type* newtype)
    {
        delete _type;
        _type = newtype;
    }

    int precedence() { return 3; }
    // c-style cast actually is R->L
    bool isLRAssociative() { return false; }
    // c-style cast children always to its right
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    Type* _type;
};

enum eDeclRefExprType
{
    FUNC_DECL = 1,
    VAR_DECL = 2,
    ENUM_DECL = 3
};

/**
 * @brief A reference to a declared variable, function, enum, etc.
 */
class DeclRefExpr : public ASTNode
{
public:
    /**
     * @brief Construct a new DeclRefExpr
     *
     * @param referencedDecl is a pointer to the existing ValueDecl* for
     * the referenced variable (already created from its definition). This
     * may be located using the map<>'s in ASTBuilder. Thus DeclRefExpr
     * DOES NOT OWN this memory and must not delete it.
     */
    DeclRefExpr(ValueDecl* referencedDecl, eDeclRefExprType decl_type, uintb instr_addr);
    DeclRefExpr(ValueDecl* referencedDecl, eDeclRefExprType decl_type, const PcodeOp* op);
    virtual DeclRefExpr* clone_my_members(ASTNode* node);
    virtual DeclRefExpr* clone()
    {
        return (DeclRefExpr*)clone_my_members(new DeclRefExpr(*this));
    }

    inline ValueDecl* ref() { return _ref; }
    eDeclRefExprType decl_type() { return _decl_type; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    ValueDecl* _ref;    // this points to the original - WE DO NOT OWN
    eDeclRefExprType _decl_type;
};

/**
 * @brief Adaptor to allow mixing declarations with statements and expressions.
 */
class DeclStmt : public ASTNode
{
public:
    DeclStmt();
    virtual DeclStmt* clone()
    {
        return (DeclStmt*)clone_my_members(new DeclStmt(*this));
    }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

/**
 * @brief ForStmt children are:
 * - Optional init statement
 * - Optional conditional statement
 * - Optional increment statement
 * - Loop body
 */
class ForStmt : public ASTNode
{
public:
    ForStmt(uintb instr_addr)
        : ASTNode(instr_addr)
    { }
    virtual ForStmt* clone()
    {
        return (ForStmt*)clone_my_members(new ForStmt(*this));
    }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

class GotoStmt : public ASTNode
{
public:
    // GotoStmt(LabelStmt* label)
    /**
     * CLS: not sure what I want here...just starting with name for simplicity,
     * but if it becomes important than we can add a LabelStmt or an ID or something
     */
    GotoStmt(string label_name, uintb instr_addr)
        : ASTNode(instr_addr), _label_name(label_name)
    { }
    virtual GotoStmt* clone()
    {
        return (GotoStmt*)clone_my_members(new GotoStmt(*this));
    }

    string label_name() { return _label_name; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    string _label_name;
};

/**
 * IfStmt children are:
 * - Conditional
 * - Then block
 * - Else block (if present)
 */
class IfStmt : public ASTNode
{
public:
    IfStmt(uintb instr_addr);
    virtual IfStmt* clone()
    {
        return (IfStmt*)clone_my_members(new IfStmt(*this));
    }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

class FloatingLiteral : public ASTNode
{
public:
    FloatingLiteral(Type* type, double value, const PcodeOp* op)
        : ASTNode(op->getAddr().getOffset()), _type(type), _value(value), _special_value("")
    { }
    ~FloatingLiteral()
    {
        if (_type) {
            delete _type;
        }
    }
    virtual FloatingLiteral* clone_my_members(ASTNode* node);
    virtual FloatingLiteral* clone()
    {
        return (FloatingLiteral*)clone_my_members(new FloatingLiteral(*this));
    }

    inline double value() { return _value; }
    inline string specialValue() { return _special_value; }
    inline Type* type() { return _type; }

    void setSpecialValue(string special_value) { _special_value = special_value; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    double _value;
    // CLS: not sure if we want this, but to get around potential floating point issues
    // if it's one of the special cases +INFINITY, -INFINITY, NaN, -NaN I
    // will store that string in _special_value and set _value to -1
    string _special_value;
    Type* _type;    // we own this
};

class IntegerLiteral : public ASTNode
{
public:
    IntegerLiteral(Type* type, uintb value, uintb instr_addr);
    ~IntegerLiteral()
    {
        if (_type) {
            delete _type;
        }
    }
    virtual IntegerLiteral* clone_my_members(ASTNode* node);
    virtual IntegerLiteral* clone()
    {
        return (IntegerLiteral*)clone_my_members(new IntegerLiteral(*this));
    }

    inline uintb value() { return _value; }
    inline Type* type() { return _type; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    uintb _value;
    Type* _type;    // we own this
};

class LabelStmt : public ASTNode
{
public:
    LabelStmt(string name, ASTBuilder* builder, uintb instr_addr)
        : ASTNode(instr_addr), _name(name), _builder_handle(builder)
    { }
    virtual LabelStmt* clone()
    {
        return (LabelStmt*)clone_my_members(new LabelStmt(*this));
    }

    void addChild(ASTNode* child, bool append /*= true*/, bool check_parens /*= true*/);

    string name() { return _name; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    string _name;
    ASTBuilder* _builder_handle;
};

class MemberExpr : public ASTNode
{
public:
    MemberExpr(const PcodeOp* op, string name="", int offset=-1, bool isArrow=false, int sid=-1)
        : ASTNode(op->getAddr().getOffset()), _sid(sid), _offset(offset),
        _isArrow(isArrow), _name(name)
    { }
    virtual MemberExpr* clone()
    {
        return (MemberExpr*)clone_my_members(new MemberExpr(*this));
    }

    int precedence() { return 2; }
    bool isLRAssociative() { return true; }
    // var is a child (e.g. var->field)
    bool wouldNextChildBeLeftOfOp() { return true; }

    /** @brief SID for the structure that contains this member */
    int sid() { return _sid; }
    /** @brief Offset of this member within its struct */
    int offset() { return _offset; }
    /** @brief This is a struct pointer dereference (arrow), e.g. X->Y instead of X.Y */
    bool isArrow() { return _isArrow; }
    /** @brief Name of the structure member */
    string name() { return _name; }

    void setSid(int sid) { _sid = sid; }
    void setOffset(int offset) { _offset = offset; }
    void setIsArrow(bool isArrow) { _isArrow = isArrow; }
    void setName(string name) { _name = name; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    int _sid;
    int _offset;
    bool _isArrow;
    string _name;
};

class NullNode : public ASTNode
{
public:
    NullNode() { }
    virtual NullNode* clone()
    {
        return (NullNode*)clone_my_members(new NullNode(*this));
    }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

class ParenExpr : public ASTNode
{
public:
    ParenExpr(bool hidden=false, uintb instr_addr = 0)
        : ASTNode(instr_addr), _hidden(hidden)
    { }
    virtual ParenExpr* clone()
    {
        return (ParenExpr*)clone_my_members(new ParenExpr(*this));
    }

    int precedence() { return 2; }
    bool isLRAssociative() { return true; }     // actually L->R
    // I think paren children are right of op since they are
    // right of opening paren '('
    bool wouldNextChildBeLeftOfOp() { return false; }

    /** @brief True if this is a hidden paren expr (just how I'm implementing
     * PrintC's opHiddenFunc() logic...this will not persist into the
     * final AST)*/
    bool hidden() { return _hidden; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    bool _hidden;
};

class ReturnStmt : public ASTNode
{
public:
    ReturnStmt(const PcodeOp* op);
    virtual ReturnStmt* clone()
    {
        return (ReturnStmt*)clone_my_members(new ReturnStmt(*this));
    }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

class StringLiteral : public ASTNode
{
public:
    StringLiteral(string value, const PcodeOp* op);
    virtual StringLiteral* clone()
    {
        return (StringLiteral*)clone_my_members(new StringLiteral(*this));
    }

    inline string value() { return _value; }

    // CLS: not sure if we need to have a type for this or not?
    // the other literals do, but all I'm seeing so far in clang AST
    // output is char[10], char[4], char[32], etc. based on the
    // size of the value. If we need it, add it...but not sure yet
    // inline Type* type() { return _type; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);

    string _value;
    // Type* _type;
};

class SwitchStmt : public ASTNode
{
public:
    SwitchStmt(const PcodeOp* op);
    virtual SwitchStmt* clone()
    {
        return (SwitchStmt*)clone_my_members(new SwitchStmt(*this));
    }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

/**
 * @brief Represents a top-level translation unit
 */
class TranslationUnitDecl : public ASTNode
{
public:
    TranslationUnitDecl();
    virtual TranslationUnitDecl* clone()
    {
        return (TranslationUnitDecl*)clone_my_members(new TranslationUnitDecl(*this));
    }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

/**
 * DoStmt represents a do/while loop.
 * Children are:
 * - Body
 * - Condition
 */
class DoStmt : public ASTNode
{
public:
    DoStmt(uintb instr_addr)
        : ASTNode(instr_addr)
    { }
    virtual DoStmt* clone()
    {
        return (DoStmt*)clone_my_members(new DoStmt(*this));
    }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

/**
 * WhileStmt children are:
 * [- Condition variable (DeclStmt)] << clang has this, I don't think we need it
 * bc I don't think Ghidra generates this form (wait until proven wrong to implement :))
 * - Condition
 * - Body
 */
class WhileStmt : public ASTNode
{
public:
    WhileStmt(uintb instr_addr)
        : ASTNode(instr_addr)
    { }
    virtual WhileStmt* clone()
    {
        return (WhileStmt*)clone_my_members(new WhileStmt(*this));
    }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

/**
 * CLS: this is where I might end up deviating from Clang a bit...
 * not only in how "Type" is used but whether or not I use BuiltinType,
 * RecordType, etc...
 *
 * OR - if the model is architected such that it is trying to predict the
 * "ASTNode type" for category (e.g. BuiltinType vs. PointerType vs. ArrayType)
 * instead of the first element of the triple, it might be natural to follow
 * the clang pattern here
 *
 * C structs:
 * RecordDecl (name [, size?])
 * |- FieldDecl (name, type, offset)
 * |- FieldDecl (name, type, offset)
 *
 * NOTE:
 * Ok, just because I'm getting confused now on where we are and what I even
 * want for types...I'm going to just MAKE A DECISION to move forward.
 * Then once we get to look at the dataset and evaluate, we can DEFINITELY
 * come back and change/tweak/adjust it to whatever unified solution make sense!
 *
 * BUT FOR NOW...
 *
 * - We have various ASTNode's that have a Datatype* dt() property associated with them
 *      > currently we are just printing the name in JSON
 * >> TODO: go back through and convert all these Datatype* instances in the
 *   AST to Type (ASTNode) instances
 *
 * DIFFICULT TO DECIDE...
 * - Should we have a "type database" that each variable just points to (via type_id?)
 *      - This avoids making the file overly large I guess?
 *      - For structures it definitely does not make sense to splat the definition
 *        all over the place
 *          > SOLUTION: the use of different ASTNodes for different types actually
 *            helps this issue
 *              - BuiltinType: this is just the name - we know how to convert it to a triple
 *              - PointerType: represents a pointer, holds another Type instance for pointed-to type
 *              - ArrayType: probably just like pointer...but with a size
 *              - RecordType: use the "database" concept for structures, and RecordType
 *                just contains their id
 *
 * TODO - PointerType, ArrayType, RecordType
 */

class Type : public ASTNode
{
public:
    Type(const Datatype* dt);
    Type(string name);
    virtual Type* clone()
    {
        // don't copy _ghidra_dt - this is already a pointer to Ghidra's memory
        // that we shouldn't delete
        return (Type*)clone_my_members(new Type(*this));
    }

    /**
     * @brief The Ghidra Datatype associated with this Type if one exists,
     * otherwise nullptr.
     */
    inline const Datatype* ghidra_dtype() { return _ghidra_dt; }

    virtual string name() { return _name; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    string _name;
    const Datatype* _ghidra_dt;
};

/**
 * @brief Integers, floats
 */
class BuiltinType : public Type
{
public:
    BuiltinType(const Datatype* dt);
    BuiltinType(string name, int size, bool isFloatingPoint, bool sign);
    virtual BuiltinType* clone()
    {
        return (BuiltinType*)clone_my_members(new BuiltinType(*this));
    }

    /** @brief Size of type in bytes */
    int size();

    /** @brief True if floating point type, false if integral type */
    bool isFloatingPoint();

    /** @brief True if signed, false if unsigned */
    bool isSigned();

    /** @brief True if the ghidra metatype was TYPE_BOOL */
    bool isBool();

    // virtual string name();

    /** @brief The name Ghidra uses for this data type (could be a typedef) */
    string ghidra_name() { return _ghidra_name; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    bool _is_bool;
    bool _is_floating;
    bool _is_signed;
    int _size;
    string _ghidra_name;
};

/**
 * @brief Array with specified constant size
 */
class ConstantArrayType : public Type
{
public:
    /**
     * @brief The element datatype will be created as a child node,
     * but is provided here as a parameter for convenience and to
     * hopefully reduce mistakes.
     *
     * @param elementType
     * @param numElements
     */
    ConstantArrayType(const Datatype* elementType, int numElements);
    ConstantArrayType(const TypeArray* arrType);
    virtual ConstantArrayType* clone()
    {
        return (ConstantArrayType*)clone_my_members(new ConstantArrayType(*this));
    }

    inline int numElements() { return _num_elements; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    int _num_elements;
};

class EnumDecl : public ASTNode
{
public:
    EnumDecl(string name)
        : _name(name)
    { }
    virtual EnumDecl* clone()
    {
        return (EnumDecl*)clone_my_members(new EnumDecl(*this));
    }

    string name() const { return _name; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    string _name;
};

class EnumType : public Type
{
public:
    EnumType(EnumDecl* decl)
        : Type(decl->name()), _decl(decl)
    { }
    virtual EnumType* clone()
    {
        return (EnumType*)clone_my_members(new EnumType(*this));
    }

    EnumDecl* getDecl() { return _decl; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    EnumDecl* _decl;    // we do NOT own this, do not delete it
};

/**
 * @brief This represents a function data type, intended specifically
 * for function pointers. (this also maps to Ghidra's Function Definition)
 */
class FunctionType : public Type
{
public:
    FunctionType(string name, Type* return_type)
        : Type(name), _return_type(return_type)
    { }
    ~FunctionType()
    {
        if (_return_type) {
            delete _return_type;
        }
    }

    virtual FunctionType* clone_my_members(ASTNode* node)
    {
        auto ftype = (FunctionType*)Type::clone_my_members(node);
        ftype->_return_type =  _return_type ? _return_type->clone() : nullptr;
        return ftype;
    }
    virtual FunctionType* clone()
    {
        return (FunctionType*)clone_my_members(new FunctionType(*this));
    }

    inline Type* return_type() { return _return_type; }

    inline void replace_return_type(Type* newtype)
    {
        if (_return_type) {
            delete _return_type;
        }
        _return_type = newtype;
    }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    Type* _return_type;
};

/**
 * @brief Pointer type, child node is the pointed-to type
 */
class PointerType : public Type
{
public:
    PointerType(const Datatype* pointedToType);
    virtual PointerType* clone()
    {
        return (PointerType*)clone_my_members(new PointerType(*this));
    }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

/**
 * @brief Structure type
 *
 * Currently this is an immutable type, and as such the only one constructing
 * these is the StructTypeLibrary
 */
class StructType : public Type
{
public:
    StructType(int sid, TypeStruct* ghidra_struct)
        : Type(ghidra_struct), _sid(sid), _is_union(false)
    { }

    StructType(int sid, TypeUnion* ghidra_union)
        : Type(ghidra_union), _sid(sid), _is_union(true)
    { }

    StructType()
        : Type(""), _sid(-1), _is_union(false)
    {}

    virtual StructType* clone()
    {
        return (StructType*)clone_my_members(new StructType(*this));
    }

    /** @brief Structure ID */
    int sid() const { return _sid; }

    string name() const { return _ghidra_dt->getName(); }
    bool is_union() const { return _is_union; }

    TypeStruct* ghidra_struct() const
    {
        if (_is_union) {
            return nullptr;
        }
        return (TypeStruct*)_ghidra_dt;
    }

    TypeUnion* ghidra_union() const
    {
        if (_is_union) {
            return (TypeUnion*)_ghidra_dt;
        }
        return nullptr;
    }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    int _sid;
    bool _is_union;
};

/**
 * @brief Represents the void keyword when it is treated as a data type
 * (void return value, void*, etc.)
 *
 * I was going back and forth on how to represent void (clang AST just has a
 * string "void" inside qualType as far as I can tell right now), but choosing
 * to use a dedicated ASTNode type for now since it can simplify handling it
 * from the perspective of traversing the AST.
 *
 * The other option is to make void a special case of BuiltinType, where it
 * is the only valid BuiltinType to have a size of 0. Something like:
 * { size: 0, sign: ?, isFloating: ? }
 *
 * ...the only downside there is that you then have to make sure your code that
 * processes BuiltinTypes can handle sizes of zero, which might be weird. So
 * I'm representing VoidType as its own node type for now - if we choose to
 * combine it with BuiltinType for our model input data that is fine (and we can
 * always change this later).
 */
class VoidType : public Type
{
public:
    VoidType() : Type("void") {}
    virtual VoidType* clone()
    {
        return (VoidType*)clone_my_members(new VoidType(*this));
    }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

/**
 * @brief This declares the typedef
 * (e.g. typedef int foo; )
 */
class TypedefDecl : public ASTNode
{
public:
    TypedefDecl(string name);
    virtual TypedefDecl* clone()
    {
        return (TypedefDecl*)clone_my_members(new TypedefDecl(*this));
    }

    inline string name() { return _name; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    string _name;
};

/**
 * @brief This is a "use" of a typedef'd type
 * (e.g. foo myvar; -> foo has a TypedefType type in the AST) *
 */
class TypedefType: public Type
{
public:
    TypedefType(TypedefDecl* decl);
    virtual TypedefType* clone_my_members(ASTNode* node)
    {
        auto tdtype = (TypedefType*)Type::clone_my_members(node);
        // do not delete: this Decl gets inserted into the AST and we just point!!
        // tdtype->_decl = _decl->clone();
        return tdtype;
    }
    virtual TypedefType* clone()
    {
        return (TypedefType*)clone_my_members(new TypedefType(*this));
    }

    /** @brief Returns a pointer to the declaration of this typedef */
    inline TypedefDecl* getDecl() { return _decl; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    TypedefDecl* _decl;     // we DO NOT OWN THIS!!!
};

class UnaryOperator : public ASTNode
{
public:
    UnaryOperator(std::string opcode, const PcodeOp* op);
    //, Type* type);

    virtual UnaryOperator* clone()
    {
        return (UnaryOperator*)clone_my_members(new UnaryOperator(*this));
    }

    inline std::string opcode() { return _opcode; }

    int precedence();
    bool isLRAssociative();
    // for unary operators, children are always on its right!
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    std::string _opcode;
};

/**
 * @brief Declaration of a variable (in which case it is an lvalue) a function
 * (in which case it is a function designator) or an enum constant.
 */
class ValueDecl : public ASTNode
{
public:
    /**
     * @param id is a unique ID for this ValueDecl
     */
    ValueDecl(int id);
    virtual ValueDecl* clone()
    {
        return (ValueDecl*)clone_my_members(new ValueDecl(*this));
    }

    inline int id() { return _id; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    int _id;
};

class EnumConstantDecl : public ValueDecl
{
public:
    EnumConstantDecl(int id, string name, int value);
    virtual EnumConstantDecl* clone()
    {
        return (EnumConstantDecl*)clone_my_members(new EnumConstantDecl(*this));
    }

    string name() { return _name; }
    int value() { return _value; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    string _name;
    int _value;
};

/**
 * @brief Declaration of a structure field (mostly for compatibility with clang
 * AST for validation purposes...)
 */
class FieldDecl : public ASTNode
{
public:
    FieldDecl(string name, Type* dtype)
        : _name(name), _dtype(dtype)
    { }

    ~FieldDecl()
    {
        delete _dtype;
    }

    virtual FieldDecl* clone_my_members(ASTNode* node)
    {
        auto fd = (FieldDecl*)ASTNode::clone_my_members(node);
        fd->_dtype = _dtype->clone();
        return fd;
    }
    virtual FieldDecl* clone()
    {
        return (FieldDecl*)clone_my_members(new FieldDecl(*this));
    }

    string name() { return _name; }
    Type* type() { return _dtype; }

    inline void replace_type(Type* newtype)
    {
        delete _dtype;
        _dtype = newtype;
    }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    string _name;
    Type* _dtype;
};

/**
 * @brief Declaration of a structure, union, or class
 */
class RecordDecl : public ASTNode
{
public:
    RecordDecl(StructType stype);

    virtual RecordDecl* clone()
    {
        return (RecordDecl*)clone_my_members(new RecordDecl(*this));
    }

    int sid() { return _sid; }
    string name() { return _name; }
    bool is_union() const { return _is_union; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    int _sid;
    bool _is_union;
    string _name;
};

/**
 * @brief Function declaration node
 */
class FunctionDecl : public ValueDecl
{
public:
    /**
     * @brief Construct an intrinsic function (for opFunc functions)
     */
    FunctionDecl(int id, string name, Type* return_type)
        : ValueDecl(id), _name(name), _fd(nullptr), _return_type(return_type), _is_intrinsic(true)
    { }
    FunctionDecl(int id, Funcdata* fd);
    ~FunctionDecl()
    {
        if (_return_type) {
            delete _return_type;
        }
    }
    virtual FunctionDecl* clone_my_members(ASTNode* node)
    {
        auto fd = (FunctionDecl*)ValueDecl::clone_my_members(node);
        fd->_return_type =  _return_type ? _return_type->clone() : nullptr;
        return fd;
    }
    virtual FunctionDecl* clone()
    {
        return (FunctionDecl*)clone_my_members(new FunctionDecl(*this));
    }

    inline std::string name() { return _name; }
    inline uintb address() { return _fd ? _fd->getAddress().getOffset() : 0; }
    inline Type* return_type() { return _return_type; }
    inline bool is_intrinsic() { return _is_intrinsic; }

    inline void replace_return_type(Type* newtype)
    {
        if (_return_type) {
            delete _return_type;
        }
        _return_type = newtype;
    }

    /** @brief The backing Funcdata for this FunctionDecl */
    inline Funcdata* funcdata() { return _fd; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    string _name;
    Funcdata* _fd;
    Type* _return_type;
    bool _is_intrinsic;
};

/**
 * @brief Encapsulate the address/location information in general
 * for variables where we need to handle all of Ghidra's address
 * space types (RAM, register, unique, stack, etc)
 */
struct Location
{
    Location(string space_name = "", uintb off = 0, string reg_name = "")
        : addr_space_name(space_name), offset(off), register_name(reg_name)
    { }

    string addr_space_name;
    string register_name;      // only filled in if it's a register
    uintb offset;
};

/**
 * @brief Convert a Symbol to its Location representation based on
 * the first SymbolEntry mapped
 */
Location getLocFromSymbol(Symbol* sym);

/**
 * @brief Convert an Address to its Location representation
 */
Location getLocFromAddr(Address addr);

/**
 * @brief Variable declaration or definition
 */
class VarDecl : public ValueDecl
{
public:
    /**
     * @param sym The symbol for this variable
     */
    VarDecl(int id, Symbol* sym);
    VarDecl(int id, string name, Type* type, Location loc);
    VarDecl(int id, string name, const Datatype* dt, Location loc);
    virtual ~VarDecl()
    {
        delete _type;
    }

    virtual VarDecl* clone_my_members(ASTNode* node)
    {
        auto vd = (VarDecl*)ValueDecl::clone_my_members(node);
        vd->_type = _type->clone();
        return vd;
    }
    virtual VarDecl* clone()
    {
        return (VarDecl*)clone_my_members(new VarDecl(*this));
    }

    inline Symbol* ghidra_sym() { return _sym; }
    inline const Datatype* ghidra_dtype()
    {
        if (_sym) {
            return _sym->getType();
        }
        return _ghidra_type;
    }

    inline string name() { return _name; }
    inline Type* type() { return _type; }
    inline Location location() { return _loc; }

    inline void replace_type(Type* newtype)
    {
        delete _type;
        _type = newtype;
    }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    Symbol* _sym;
    string _name;
    Type* _type;
    const Datatype* _ghidra_type;
    Location _loc;
};

/**
 * @brief Function parameter declaration
 */
class ParmVarDecl : public VarDecl
{
public:
    ParmVarDecl(int id, string name, Type* dtype, Location loc);
    ParmVarDecl(int id, ProtoParameter* param);
    virtual ParmVarDecl* clone()
    {
        return (ParmVarDecl*)clone_my_members(new ParmVarDecl(*this));
    }

    /** @brief The backing ProtoParameter for this ParmVarDecl */
    // inline ProtoParameter* param() { return _param; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    // ProtoParameter* _param;
};

/**
 * @brief Holds all of the structure definitions for some AST code context.
 *
 * Right now this is associated with a TranslationUnit, but I'm adding this
 * layer of abstraction so that if we want to have a single struct type library
 * for an entire program later on (including all its functions and globals) then
 * we have the flexibility to do that - all the code will be written to a
 * StructTypeLibrary instance which we can just move as desired.
 */
class StructTypeLibrary
{
public:
    StructTypeLibrary(int base_id = 0);

    bool isStructMapped(string name)
    {
        return _mapped_structures.count(name);
    }

    /**
     * @brief Maps the given Ghidra structure if not already mapped and returns
     * its structure id (sid)
     *
     * @param ghidra_struct
     * @return int
     */
    int mapStruct(TypeStruct* ghidra_struct);

    /**
     * @brief Same as mapStruct but for a union
     */
    int mapUnion(TypeUnion* ghidra_union);

    vector<StructType> getMappedStructs()
    {
        // lots of copying, but if it's memory safe and just works I WILL TAKE IT :D
        vector<StructType> structs;
        for (auto const& pair : _mapped_structures) {
            structs.push_back(pair.second);
        }
        return structs;
    }

    map<string, StructType> mapped_structures() { return _mapped_structures; }

    // STRUCT ID OPTIONS
    // 1. Use the Ghidra ID (this won't work if this type is not defined in ghidra already)
    // 2. Autogen a new ID on the fly arbitrarily - increment a counter
    // 3. Compute a deterministic ID - hash the content or something
        // - this is slower
        // - but we can get it from the contents...
        // - we need to take care that we include or exclude the struct name in the
        //   hash if we want to prevent 2 separate structures WITH IDENTICAL CONTENT
        //   to hash to the same id or not

protected:
    /**
     * @brief Maps structure name -> structure id (sid) for this export context
     * (right now a translation decl, but could be global to a binary)
     */
    std::map<string, StructType> _mapped_structures;
    int _next_id;
};