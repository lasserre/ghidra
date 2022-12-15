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

    /**
     * @brief Adds child to this node's children, if it is not already
     * in the list (the pointer value itself, not the object value).
     *
     * @param child is the child to add
     * @param append If true, add to end of list. Otherwise add to beginning
     */
    void addChild(ASTNode* child, bool append=true);

    void accept(ASTVisitor*, void* context=nullptr);

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
};

class BinaryOperator : public ASTNode
{
public:
    BinaryOperator(std::string opcode);

    // assignment["kind"] = "BinaryOperator";
    // assignment["opcode"] = "=";
    // // basing dtype on outvn type for now
    // assignment["dtype"] = datatype_to_json(op->getOut()->getType());
    // assignment["inner"] = json::array();

    inline std::string opcode() { return _opcode; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    std::string _opcode;
};

/**
 * @brief A list of statements
 */
class CompoundStmt : public ASTNode
{
public:
    CompoundStmt();

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
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

    inline ValueDecl* ref()
    {
        return _ref;
    }

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

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
};

/**
 * @brief Function declaration node
 */
class FunctionDecl : public ASTNode
{
public:
    FunctionDecl(Funcdata* fd);

    inline std::string name() { return _fd->getName(); }
    inline uintb address() { return _fd->getAddress().getOffset(); }
    inline Datatype* return_dtype() { return _fd->getFuncProto().getOutputType(); }

    /** @brief The backing Funcdata for this FunctionDecl */
    inline Funcdata* funcdata() { return _fd; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    Funcdata* _fd;
};

class IntegerLiteral : public ASTNode
{
public:
    IntegerLiteral(Datatype* dt, uintb value);

    inline uintb value() { return _value; }
    inline Datatype* dt() { return _dt; }

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    Datatype* _dt;
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

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    std::string _msg;
};

/**
 * @brief Represents a top-level translation unit
 */
class TranslationUnitDecl : public ASTNode
{
public:
    TranslationUnitDecl();

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
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

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    int _id;
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

protected:
    virtual void* doAccept(ASTVisitor* v, void* context);
    ProtoParameter* _param;
};
