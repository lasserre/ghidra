#pragma once

#include <vector>
#include <string>

#include "funcdata.hh"

class ASTVisitor;

/**
 * @brief Represents a single node in the AST
 */
class ASTNode
{
public:
    ASTNode(ASTNode* parent);
    virtual ~ASTNode();

    inline ASTNode* parent() { return _parent; }
    inline std::vector<ASTNode*>* children() { return &_children; }

    void accept(ASTVisitor*);

protected:
    /**
     * @brief This is the node-specific function to perform the accept
     * on a particular kind of ASTNode. The general accept() handles calling
     * both this function as well as recursing through any child nodes
     */
    virtual void do_accept(ASTVisitor*) = 0;

    /**
     * @brief Helper function to visit all children of this node
     */
    void visitChildren(ASTVisitor* v);

    ASTNode* _parent;       // pointer to existing parent, not our memory
    // dynamically-allocated child pointers we must free when destructed
    std::vector<ASTNode*> _children;
};

/**
 * @brief Function declaration node
 */
class FunctionDecl : public ASTNode
{
public:
    FunctionDecl(ASTNode* parent, Funcdata* fd);

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

protected:
    virtual void do_accept(ASTVisitor* v);

    Funcdata* _fd;
};

/**
 * @brief Function parameter declaration
 */
class ParmVarDecl : public ASTNode
{
public:
    ParmVarDecl(FunctionDecl* parent);

protected:
    virtual void do_accept(ASTVisitor* v);
};