#include "ast.h"
#include "astvisitor.h"

ASTNode::ASTNode(ASTNode* parent)
    : _parent(parent), _children()
{
}

ASTNode::~ASTNode()
{
    for (ASTNode* child : _children) {
        delete child;
    }
}

void ASTNode::accept(ASTVisitor* v)
{
    // visit this node
    do_accept(v);

    // visit children
    for (ASTNode* node : _children) {
        node->accept(v);
    }
}

FunctionDecl::FunctionDecl(ASTNode* parent, Funcdata* fd)
    : ASTNode(parent), _fd(fd)
{
}

void FunctionDecl::do_accept(ASTVisitor* v)
{
    v->visitFunctionDecl(this);
    visitChildren(v);
}

ParmVarDecl::ParmVarDecl(FunctionDecl* parent)
    : ASTNode(parent)
{
}

void ParmVarDecl::do_accept(ASTVisitor* v)
{
    v->visitParmVarDecl(this);
    visitChildren(v);
}
