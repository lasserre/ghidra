#pragma once


#include <vector>
#include <string>
#include <functional>

#include "types.h"
#include "funcdata.hh"

class ASTVisitor;
class ValueDecl;

class BuiltinType;
class Type;

// using namespace std;
using std::string;
using namespace ghidra;

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
    ASTNode();
    virtual ~ASTNode();

    inline ASTNode* parent() { return _parent; }
    inline std::vector<ASTNode*>* children() { return &_children; }
    inline std::vector<string>* messages() { return &_messages; }

    /**
     * @brief Adds child to this node's children, if it is not already
     * in the list (the pointer value itself, not the object value).
     *
     * @param child is the child to add
     * @param append If true, add to end of list. Otherwise add to beginning
     */
    void addChild(ASTNode* child, bool append=true, bool check_parens=true);

    /**
     * @brief Removes this node and replaces it with new_node in the AST.
     * This node's children are moved over to become children of new_node,
     * and new_node takes the place of this node with respect to its parent
     *
     * @param replacement
     * @returns a pointer to this node which may now be deleted
     */
    ASTNode* replaceWith(ASTNode* new_node);

    void accept(ASTVisitor*, void* context=nullptr);

    /**
     * @brief Return the precedence level of this node, or -1 if it doesn't
     * apply/doesn't have one.
     *
     * The highest precedence is 1 and the lowest is 17, per the table
     * here:
     * https://www.learncpp.com/cpp-tutorial/operator-precedence-and-associativity/
     */
    virtual int precedence() = 0;

    /**
     * @brief Return true if LR associative, false otherwise. If there is no
     * associativity for this node, returns false.
     */
    virtual bool isLRAssociative() = 0;

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
    virtual bool wouldNextChildBeLeftOfOp() = 0;

    /**
     * @brief True if this node has a precedence level
    */
    bool hasPrecedence() { return precedence() > 0; }

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
    std::vector<string> _messages;  // diagnostic/error messages for validation
};

class BinaryOperator : public ASTNode
{
public:
    BinaryOperator(std::string opcode);

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

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

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
    CallExpr();

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

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

class CharacterLiteral : public ASTNode
{
public:
    CharacterLiteral(BuiltinType* type, uintb value);
    virtual ~CharacterLiteral() { delete _type; }

    inline uintb value() { return _value; }
    inline BuiltinType* type() { return _type; }

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

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

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

class ConstantExpr : public ASTNode
{
public:
    ConstantExpr();

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

class CStyleCastExpr : public ASTNode
{
public:
    CStyleCastExpr(Type* type);

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
    DeclRefExpr(ValueDecl* referencedDecl);

    inline ValueDecl* ref() { return _ref; }
    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    ValueDecl* _ref;
};

/**
 * @brief Adaptor to allow mixing declarations with statements and expressions.
 */
class DeclStmt : public ASTNode
{
public:
    DeclStmt();

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
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
    IfStmt();

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

class IntegerLiteral : public ASTNode
{
public:
    IntegerLiteral(Type* type, uintb value);

    inline uintb value() { return _value; }
    inline Type* type() { return _type; }

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    uintb _value;
    Type* _type;
};

/**
 * @brief Not really part of the AST...
 * I don't want to lose errors or warnings I need to be aware of,
 * and the way the Ghidra decompiler runs in a separate process I may
 * not readily notice silent issues. This allows me to pass through
 * errors into the output JSON for now (later there may be a better way)
 */
class LogMsg : public ASTNode
{
public:
    LogMsg(std::string msg);

    inline std::string message() { return _msg; }

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    std::string _msg;
};

class ParenExpr : public ASTNode
{
public:
    ParenExpr();

    int precedence() { return 2; }
    bool isLRAssociative() { return true; }     // actually L->R
    // I think paren children are right of op since they are
    // right of opening paren '('
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

class ReturnStmt : public ASTNode
{
public:
    ReturnStmt();

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

class SwitchStmt : public ASTNode
{
public:
    SwitchStmt();

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

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

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

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
    virtual ~Type() {}

    /**
     * @brief The Ghidra Datatype associated with this Type if one exists,
     * otherwise nullptr.
     */
    inline const Datatype* ghidra_dtype() { return _ghidra_dt; }

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

    inline string name() { return _name; }

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

    /** @brief Size of type in bytes */
    int size();

    /** @brief True if floating point type, false if integral type */
    bool isFloatingPoint();

    /** @brief True if signed, false if unsigned */
    bool isSigned();

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    bool _is_floating;
    bool _is_signed;
    int _size;
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

    inline int numElements() { return _num_elements; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    int _num_elements;
};

/**
 * @brief Pointer type, child node is the pointed-to type
 */
class PointerType : public Type
{
public:
    PointerType(const Datatype* pointedToType);

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

    inline string name() { return _name; }

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

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

    /** @brief Returns a pointer to the declaration of this typedef */
    inline TypedefDecl* getDecl() { return _decl; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    TypedefDecl* _decl;
};

class UnaryOperator : public ASTNode
{
public:
    UnaryOperator(std::string opcode, Type* type);

    inline std::string opcode() { return _opcode; }
    inline Type* type() { return _type; }

    int precedence();
    bool isLRAssociative();
    // for unary operators, children are always on its right!
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    std::string _opcode;
    Type* _type;    // output datatype of operator
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
    virtual ~ValueDecl() {}

    inline int id() { return _id; }

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    int _id;
};

/**
 * @brief Function declaration node
 */
class FunctionDecl : public ValueDecl
{
public:
    FunctionDecl(int id, Funcdata* fd);

    inline std::string name() { return _fd->getName(); }
    inline uintb address() { return _fd->getAddress().getOffset(); }
    inline Type* return_type() { return _return_type; }

    inline void replace_return_type(Type* newtype)
    {
        delete _return_type;
        _return_type = newtype;
    }

    /** @brief The backing Funcdata for this FunctionDecl */
    inline Funcdata* funcdata() { return _fd; }

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    Funcdata* _fd;
    Type* _return_type;
};

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
    VarDecl(int id, string name, Type* type);
    VarDecl(int id, string name, const Datatype* dt);
    virtual ~VarDecl()
    {
        delete _type;
    }

    inline Symbol* ghidra_sym() { return _sym; }
    inline string name() { return _name; }
    inline Type* type() { return _type; }

    inline void replace_type(Type* newtype)
    {
        delete _type;
        _type = newtype;
    }

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    Symbol* _sym;
    string _name;
    Type* _type;
};

/**
 * @brief Function parameter declaration
 */
class ParmVarDecl : public VarDecl
{
public:
    ParmVarDecl(int id, ProtoParameter* param);

    /** @brief The backing ProtoParameter for this ParmVarDecl */
    inline ProtoParameter* param() { return _param; }

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    ProtoParameter* _param;
};

