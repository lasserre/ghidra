#pragma once

#include <vector>

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

    virtual void accept(ASTVisitor*) = 0;

protected:
    ASTNode* _parent;       // pointer to existing parent, not our memory
    // dynamically-allocated child pointers we must free when destructed
    std::vector<ASTNode*> _children;
};

class FunctionDecl : public ASTNode
{
    virtual void accept(ASTVisitor*);
};

/**
 * @brief ASTVisitor interface is what should be implemented by code that
 * needs to interact with the AST. Since the base ASTVisitor does nothing by
 * default, derived classes may "opt in" to visit elements they care about
 * without having to create boilerplate for every possible type of node.
 */
class ASTVisitor
{
    /** TODO: implement all of these as "do-nothing" defaults */
    virtual void visitFunctionDecl(FunctionDecl*);
};
