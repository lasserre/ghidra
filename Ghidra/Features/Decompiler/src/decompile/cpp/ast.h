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
     */
    void addChild(ASTNode* child);

    void accept(ASTVisitor*);

protected:
    /**
     * @brief This is the node-specific function to perform the accept
     * on a particular kind of ASTNode. The general accept() handles calling
     * both this function as well as recursing through any child nodes
     */
    virtual void doAccept(ASTVisitor*) = 0;

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
    virtual void doAccept(ASTVisitor* v);
    std::string _opcode;
};

/**
 * @brief A list of statements
 */
class CompoundStmt : public ASTNode
{
public:
    CompoundStmt();

    // json fbody;
    // fbody["kind"] = "CompoundStmt";
    // fbody["inner"] = json::array();

protected:
    virtual void doAccept(ASTVisitor* v);
};

/**
 * @brief A reference to a declared variable, function, enum, etc.
 */
class DeclRefExpr : public ASTNode
{
public:
    /** TODO: figure out how to do this...logically this is a pointer
     * to the afore-declared ValueDecl
     *
     * - should we actually POINT? (e.g. ID or something)
     * - or "regurgitate" the name/type/etc?
     *
     * NOTE: be clear about if this is a newly allocated ValueDecl we
     * must delete or if it points to one in the tree that we MUST NOT
     * delete!
     *
     * PICK UP HERE: in buildLocals() or w/e
     * - build up a map<Sym*, VarDecl*> for locals
     * - build up a map<Sym*, ParmVarDecl*> for parameters
     * - maybe build one up for globals?
     *
     * ...then when we create the DeclRefExpr, we can simply look up
     * the local/param/global in the map and point to the (same)
     * ValueDecl* (which we don't own memory to, of course)
    */
    DeclRefExpr(ValueDecl* referencedDecl);

    inline ValueDecl* ref() { return _ref; }

protected:
    virtual void doAccept(ASTVisitor* v);
    ValueDecl* _ref;
};

/**
 * @brief Adaptor to allow mixing declarations with statements and expressions.
 */
class DeclStmt : public ASTNode
{
public:
    DeclStmt();

    // declstmt["kind"] = "DeclStmt";
    // declstmt["inner"] = json::array();
    // declstmt["inner"].push_back(var_decl);

protected:
    virtual void doAccept(ASTVisitor* v);
};

/**
 * @brief Function declaration node
 */
class FunctionDecl : public ASTNode
{
public:
    FunctionDecl(Funcdata* fd);

    // basic function node info
    // JSON: fdecl["kind"] = "FunctionDecl";
    // JSON: fdecl["inner"] = json::array();
    // fdecl["name"] = fd->getName();
    // fdecl["address"] = to_hex(fd->getAddress().getOffset());
    // prototype
    // FuncProto& fp = fd->getFuncProto();
    // fdecl["return_dtype"] = datatype_to_json(fp.getOutputType());

    inline std::string name() { return _fd->getName(); }
    inline uintb address() { return _fd->getAddress().getOffset(); }
    inline Datatype* return_dtype() { return _fd->getFuncProto().getOutputType(); }

    /** @brief The backing Funcdata for this FunctionDecl */
    inline Funcdata* funcdata() { return _fd; }

protected:
    virtual void doAccept(ASTVisitor* v);
    Funcdata* _fd;
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
    virtual void doAccept(ASTVisitor* v);
    std::string _msg;
};

/**
 * @brief Declaration of a variable (in which case it is an lvalue) a function
 * (in which case it is a function designator) or an enum constant.
 */
class ValueDecl : public ASTNode
{
public:
    ValueDecl();

protected:
    virtual void doAccept(ASTVisitor* v);
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
    VarDecl(Symbol* sym);

    // var_decl["kind"] = "VarDecl";
    // var_decl["dtype"] = datatype_to_json(sym->getType());
    // var_decl["name"] = sym->getName();

    inline Symbol* sym() { return _sym; }

protected:
    virtual void doAccept(ASTVisitor* v);
    Symbol* _sym;
};

/**
 * @brief Function parameter declaration
 */
class ParmVarDecl : public VarDecl
{
public:
    ParmVarDecl(ProtoParameter* param);

    // json pvdecl;
    // pvdecl["kind"] = "ParmVarDecl";

    // ProtoParameter* param = fp.getParam(i);
    // Symbol* sym = param->getSymbol();

    // if (sym) {
    //     pvdecl["dtype"] = datatype_to_json(sym->getType());
    //     pvdecl["name"] = sym->getName();
    // } else {
    //     pvdecl["dtype"] = datatype_to_json(param->getType());
    // }

    /** @brief The backing ProtoParameter for this ParmVarDecl */
    inline ProtoParameter* param() { return _param; }

protected:
    virtual void doAccept(ASTVisitor* v);
    ProtoParameter* _param;
};
