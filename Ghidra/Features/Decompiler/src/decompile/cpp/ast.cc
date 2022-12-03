#include "ast.h"


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

// ASTVisitor -----------------------------------------------

void ASTVisitor::visitFunctionDecl(FunctionDecl*)
{ }
