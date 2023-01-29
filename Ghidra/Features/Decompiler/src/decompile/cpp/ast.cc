#include "ast.h"
#include "astvisitor.h"

ASTNode::ASTNode()
    : _parent(nullptr), _children(), _messages()
{
}

ASTNode::~ASTNode()
{
    for (ASTNode* child : _children) {
        delete child;
    }
}

/**
 * @brief True if the child expression needs parentheses around it
 *
 * CHILD == first evaluated subexpr...
 *
 * if CHILD on side of first eval -> no parens
 * ---
 * if CHILD on LEFT and L2R -> no parens
 * if CHILD on RIGHT and R2L -> no parens
 * if CHILD on LEFT and R2L -> PARENS around child
 * if CHILD on RIGHT and L2R -> PARENS around child
 * -----------
 * FOR UNARY: (sole) CHILD will always be to RIGHT of the parent
 * wouldNextChildBeLeftOfOp() for unary, always false
 */
bool needsParens(ASTNode* parent, ASTNode* child)
{
    if (parent->hasPrecedence() && child->hasPrecedence()) {
        if (child->precedence() == parent->precedence()) {
            // determine by associativity
            bool child_on_first_eval_side =
                child->isLRAssociative() == parent->wouldNextChildBeLeftOfOp();

            // needs parens if not on side that's eval'd first
            return !child_on_first_eval_side;
        } else {
            // lower CHILD precedence -> need parens!
            // (remember, highest is 1, lowest is 17...)
            return child->precedence() > parent->precedence();
        }
    }
    return false;
}

void ASTNode::addChild(ASTNode* child, bool append /*= true*/, bool check_parens /*= true*/)
{
    if (check_parens) {
        if (!append && _children.size() > 0) {
            // ERROR: this won't work if we mess with the order of children!
            _messages.push_back("ERROR: Prepending child will mess up paren calculations!");
        }

        if (needsParens(this, child)) {
            ParenExpr* parens = new ParenExpr();
            this->addChild(parens, append, false);
            parens->addChild(child, append, false);
            return;
        }
    }

    child->_parent = this;

    if (append) {
        _children.push_back(child);
    }
    else {
        _children.insert(_children.begin(), child);
    }

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

int BinaryOperator::precedence()
{
    if (_opcode == "*" || _opcode == "/" || _opcode == "%") {
        return 5;
    }
    else if (_opcode == "+" || _opcode == "-") {
        return 6;
    }
    else if (_opcode == "<" || _opcode == ">" || _opcode == "<=" || _opcode == ">=") {
        return 9;
    }
    else if (_opcode == "==" || _opcode == "!=") {
        return 10;
    }
    else if (_opcode == "=") {
        return 16;
    }
    else {
        _messages.push_back("No precedence mapped for binary operator '" + _opcode + "'");
        return -1;
    }
}

// we just check for presence of the key, so don't care about bool
std::map<string,bool> rl_assoc_binops = {
    {"=", true},
    {"*=", true},
    {"/=", true},
    {"%=", true},
    {"+=", true},
    {"-=", true},
    {"<<=", true},
    {">>=", true},
    {"&=", true},
    {"|=", true},
    {"^=", true},
};

std::map<string,bool> rl_assoc_unops = {
    {"+", true},
    {"-", true},
    // NOTE: would need to add logic to support pre/post-increment/decrement
    {"!", true},
    {"~", true},
    {"&", true},
    {"*", true},
};

bool BinaryOperator::isLRAssociative()
{
    // if not in BINARY RL assoc map then its LR assoc
    return rl_assoc_binops.count(_opcode) == 0;
}

bool BinaryOperator::wouldNextChildBeLeftOfOp()
{
    // first child will be on left, second on right
    return _children.size() == 0;
}

BreakStmt::BreakStmt()
{
}

void* BreakStmt::doAccept(ASTVisitor* v, void* context)
{
    return v->visitBreakStmt(this, context);
}

CallExpr::CallExpr()
{
}

void* CallExpr::doAccept(ASTVisitor* v, void* context)
{
    return v->visitCallExpr(this, context);
}

CaseStmt::CaseStmt()
{
}

void* CaseStmt::doAccept(ASTVisitor* v, void* context)
{
    return v->visitCaseStmt(this, context);
}

CharacterLiteral::CharacterLiteral(Datatype* dt, uintb value)
    : _dt(dt), _value(value)
{
}

void* CharacterLiteral::doAccept(ASTVisitor* v, void* context)
{
    return v->visitCharacterLiteral(this, context);
}

CompoundStmt::CompoundStmt()
{
}

void* CompoundStmt::doAccept(ASTVisitor* v, void* context)
{
    return v->visitCompoundStmt(this, context);
}

ConstantExpr::ConstantExpr()
{
}

void* ConstantExpr::doAccept(ASTVisitor* v, void* context)
{
    return v->visitConstantExpr(this, context);
}

CStyleCastExpr::CStyleCastExpr(Datatype* dt)
    : _dt(dt)
{
}

void* CStyleCastExpr::doAccept(ASTVisitor* v, void* context)
{
    return v->visitCStyleCastExpr(this, context);
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

FunctionDecl::FunctionDecl(int id, Funcdata* fd)
    : ValueDecl(id), _fd(fd)
{
}

void* FunctionDecl::doAccept(ASTVisitor* v, void* context)
{
    return v->visitFunctionDecl(this, context);
}

IfStmt::IfStmt()
{
}

void* IfStmt::doAccept(ASTVisitor* v, void* context)
{
    return v->visitIfStmt(this, context);
}

IntegerLiteral::IntegerLiteral(const Datatype* dt, uintb value)
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

ParenExpr::ParenExpr()
{
}

void* ParenExpr::doAccept(ASTVisitor* v, void* context)
{
    return v->visitParenExpr(this, context);
}

SwitchStmt::SwitchStmt()
{
}

void* SwitchStmt::doAccept(ASTVisitor* v, void* context)
{
    return v->visitSwitchStmt(this, context);
}

TranslationUnitDecl::TranslationUnitDecl()
{
}

void* TranslationUnitDecl::doAccept(ASTVisitor* v, void* context)
{
    return v->visitTranslationUnitDecl(this, context);
}

UnaryOperator::UnaryOperator(std::string opcode, Datatype* dt)
    : _opcode(opcode), _dt(dt)
{
}

int UnaryOperator::precedence()
{
    if (_opcode == "*") {
        return 3;
    }
    else {
        _messages.push_back("TODO - map UnaryOperator precedence for '" + _opcode + "'");
        return -1;
    }
}

bool UnaryOperator::isLRAssociative()
{
    // if not in UNARY RL assoc map then it's LR assoc
    return rl_assoc_unops.count(_opcode) == 0;
}

void* UnaryOperator::doAccept(ASTVisitor* v, void* context)
{
    return v->visitUnaryOperator(this, context);
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
