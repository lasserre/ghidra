#include "ast.h"
#include "astvisitor.h"

ASTNode::ASTNode()
    : _parent(nullptr), _children()
{
}

ASTNode::~ASTNode()
{
    for (ASTNode* child : _children) {
        delete child;
    }
}

void ASTNode::addChild(ASTNode* child)
{
    child->_parent = this;
    _children.push_back(child);

    // // verify this child pointer is not already in our children list
    // auto child_in_list = std::find(_children.begin(), _children.end(), child);
    // if (child_in_list == _children.end()) {
    //     _children.push_back(child);
    // }
}

void ASTNode::accept(ASTVisitor* v)
{
    // visit this node
    doAccept(v);

    // visit children
    for (ASTNode* node : _children) {
        node->accept(v);
    }
}

BinaryOperator::BinaryOperator(std::string opcode)
    : _opcode(opcode)
{
}

void BinaryOperator::doAccept(ASTVisitor* v)
{
    v->visitBinaryOperator(this);
}

CompoundStmt::CompoundStmt()
{
}

void CompoundStmt::doAccept(ASTVisitor* v)
{
    v->visitCompoundStmt(this);
}

DeclStmt::DeclStmt()
{
}

void DeclStmt::doAccept(ASTVisitor* v)
{
    v->visitDeclStmt(this);
}

FunctionDecl::FunctionDecl(Funcdata* fd)
    : _fd(fd)
{
}

void FunctionDecl::doAccept(ASTVisitor* v)
{
    v->visitFunctionDecl(this);
}

LogMsg::LogMsg(std::string msg)
    : _msg(msg)
{
}

void LogMsg::doAccept(ASTVisitor* v)
{
    v->visitLogMsg(this);
}

ParmVarDecl::ParmVarDecl(ProtoParameter* param)
    : _param(param)
{
}

void ParmVarDecl::doAccept(ASTVisitor* v)
{
    v->visitParmVarDecl(this);
}

VarDecl::VarDecl(Symbol* sym)
    : _sym(sym)
{
}

void VarDecl::doAccept(ASTVisitor* v)
{
    v->visitVarDecl(this);
}
