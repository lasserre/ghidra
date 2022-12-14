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

void ASTNode::accept(ASTVisitor* v, void* parent_context /*=nullptr*/)
{
    // visit this node
    void* node_context = doAccept(v, parent_context);

    // visit children
    for (ASTNode* node : _children) {
        node->accept(v, node_context);
    }
}

BinaryOperator::BinaryOperator(std::string opcode)
    : _opcode(opcode)
{
}

void* BinaryOperator::doAccept(ASTVisitor* v, void* context)
{
    return v->visitBinaryOperator(this, context);
}

CompoundStmt::CompoundStmt()
{
}

void* CompoundStmt::doAccept(ASTVisitor* v, void* context)
{
    return v->visitCompoundStmt(this, context);
}

DeclRefExpr::DeclRefExpr(ValueDecl* referencedDecl)
    : _ref(referencedDecl)
{
}

void* DeclRefExpr::doAccept(ASTVisitor* v, void* context)
{
    return v->visitDeclRefExpr(this, context);
}

DeclStmt::DeclStmt()
{
}

void* DeclStmt::doAccept(ASTVisitor* v, void* context)
{
    return v->visitDeclStmt(this, context);
}

FunctionDecl::FunctionDecl(Funcdata* fd)
    : _fd(fd)
{
}

void* FunctionDecl::doAccept(ASTVisitor* v, void* context)
{
    return v->visitFunctionDecl(this, context);
}

IntegerLiteral::IntegerLiteral(Datatype* dt, uintb value)
    : _dt(dt), _value(value)
{
}

void* IntegerLiteral::doAccept(ASTVisitor* v, void* context)
{
    return v->visitIntegerLiteral(this, context);
}

LogMsg::LogMsg(std::string msg)
    : _msg(msg)
{
}

void* LogMsg::doAccept(ASTVisitor* v, void* context)
{
    return v->visitLogMsg(this, context);
}

ParmVarDecl::ParmVarDecl(int id, ProtoParameter* param)
    : VarDecl(id, param->getSymbol()), _param(param)
{
}

void* ParmVarDecl::doAccept(ASTVisitor* v, void* context)
{
    return v->visitParmVarDecl(this, context);
}

ValueDecl::ValueDecl(int id)
    : _id(id)
{
}

void* ValueDecl::doAccept(ASTVisitor* v, void* context)
{
    return v->visitValueDecl(this, context);
}

VarDecl::VarDecl(int id, Symbol* sym)
    : ValueDecl(id), _sym(sym)
{
}

void* VarDecl::doAccept(ASTVisitor* v, void* context)
{
    return v->visitVarDecl(this, context);
}
