#pragma once

#include <vector>
#include <string>

#include "funcdata.hh"

class ASTVisitor;
class ValueDecl;

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
    CharacterLiteral(Datatype* dt, uintb value);

    inline uintb value() { return _value; }
    inline Datatype* dt() { return _dt; }

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    Datatype* _dt;
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
    CStyleCastExpr(Datatype* dt);

    inline Datatype* dt() { return _dt; }

    int precedence() { return 3; }
    // c-style cast actually is R->L
    bool isLRAssociative() { return false; }
    // c-style cast children always to its right
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    Datatype* _dt;
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
    IntegerLiteral(const Datatype* dt, uintb value);

    inline uintb value() { return _value; }
    inline const Datatype* dt() { return _dt; }

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    const Datatype* _dt;
    uintb _value;
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
 * >> TODO: add a Datatype* ghidra_dtype() member to the Type (ASTNode) class
 *   so that for the entire time we are running within Ghidra we have an exact
 *   link back to the original Datatype* instance
 *   ...BUT - the Datatype* will not be emitted in the JSON, just the Type
 *   representation
 * >>
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
 *              - TypedefType: clang AST may not use this everywhere I will (since they
 *                love qualType) but this would easily make it explicit if it make sense
 *                >> maybe Typedef's are primarily a VALIDATION issue (since I need clang to
 *                   understand undefined8...but a model needs to just know its a long!)
 *
 * SO THAT ALSO MEANS...
 * >> TODO: create BuiltinType for now (we'll add the rest as needed...) and use
 * this for the immediate problem (class BuiltinType : public Type)
 */
class Type : public ASTNode
{
public:
    Type(Datatype* dt);

    /**
     * TODO: this is probably where the triple <core, size, sign>
     * representation will be...plus more for pointers, structs, arrays
     */

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

    inline string name() { return _name; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    string _name;
};

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

class UnaryOperator : public ASTNode
{
public:
    UnaryOperator(std::string opcode, Datatype* dt);

    inline std::string opcode() { return _opcode; }
    inline Datatype* dt() { return _dt; }

    int precedence();
    bool isLRAssociative();
    // for unary operators, children are always on its right!
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    std::string _opcode;
    Datatype* _dt;  // output datatype of operator
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
    inline Datatype* return_dtype() { return _fd->getFuncProto().getOutputType(); }

    /** @brief The backing Funcdata for this FunctionDecl */
    inline Funcdata* funcdata() { return _fd; }

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    Funcdata* _fd;
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

    inline Symbol* sym() { return _sym; }

    int precedence() { return -1; }
    bool isLRAssociative() { return false; }
    bool wouldNextChildBeLeftOfOp() { return false; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    Symbol* _sym;
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
