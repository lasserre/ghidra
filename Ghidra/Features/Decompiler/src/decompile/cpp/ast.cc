#include "ast.h"
#include "astvisitor.h"
#include "astbuilder.h"

string getValidCLanguageName(string ghidra_name)
{
    /**
     * Ghidra displays invalid C variable names in the decompiler window (like "file-local"),
     * but when I convert it to C using the flat API (DecompiledFunction.getC())
     * Ghidra first converts invalid names like this to valid C names (like "file_local")
     */
    string valid_name = ghidra_name;

    for (int i = 0; i < valid_name.length(); i++) {
        char c = valid_name[i];
        if (!std::isalpha(c) && !std::isdigit(c) && c != '_') {
            valid_name[i] = '_';
        }
    }

    return valid_name;
}

static ASTCallbacks* callbacks;
void initASTCallbacks(ASTCallbacks* cb)
{
    callbacks = cb;
}

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
    if (dynamic_cast<CallExpr*>(parent) || dynamic_cast<ArraySubscriptExpr*>(parent)) {
        // call expressions and array subscripts already have parentheses or brackets
        // so we don't need to add more!
        return false;
    }

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

ASTNode* ASTNode::replaceWith(ASTNode* new_node)
{
    // clean up any stale state from new_node
    for (ASTNode* child : new_node->_children) {
        delete child;
    }

    // 1. make new node point to parent/children
    new_node->_parent = _parent;
    new_node->_children = _children;    // copy children vector over (child pointers ok to stay as-is)

    // 2. make children point to new_node
    for (ASTNode* child : new_node->_children) {
        child->_parent = new_node;
    }

    // 3. make parent point to new_node (if this is not the HEAD with null parent)
    if (_parent) {
        std::replace(_parent->_children.begin(),
                    _parent->_children.end(),
                    this, new_node);
    }

    // reset this node's children vector, otherwise deleting it
    // will delete old children pointers hanging around
    _children = {};

    return this;
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

void* ArraySubscriptExpr::doAccept(ASTVisitor* v, void* context)
{
    return v->visitArraySubscriptExpr(this, context);
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
    else if (_opcode == "&") {
        return 11;
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

void* CopyPlaceholder::doAccept(ASTVisitor* v, void* context)
{
    return v->visitCopyPlaceholder(this, context);
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

CharacterLiteral::CharacterLiteral(BuiltinType* type, uintb value)
    : _type(type), _value(value)
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

CStyleCastExpr::CStyleCastExpr(Type* type)
    : _type(type)
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
    _return_type = callbacks->toAstType(fd->getFuncProto().getOutputType());
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

IntegerLiteral::IntegerLiteral(Type* type, uintb value)
    : _value(value), _type(type)
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

void* MemberExpr::doAccept(ASTVisitor* v, void* context)
{
    return v->visitMemberExpr(this, context);
}

ParenExpr::ParenExpr()
{
}

void* ParenExpr::doAccept(ASTVisitor* v, void* context)
{
    return v->visitParenExpr(this, context);
}

void* FieldDecl::doAccept(ASTVisitor* v, void* context)
{
    return v->visitFieldDecl(this, context);
}

RecordDecl::RecordDecl(StructType stype)
    : _sid(stype.sid()), _name(stype.name())
{
    for (TypeField ghidra_field : getStructFields(stype.ghidra_struct())) {
        auto ast_type = callbacks->toAstType(ghidra_field.type);
        addChild(new FieldDecl(ghidra_field.name, ast_type));
    }
}

void* RecordDecl::doAccept(ASTVisitor* v, void* context)
{
    return v->visitRecordDecl(this, context);
}

ReturnStmt::ReturnStmt()
{
}

void* ReturnStmt::doAccept(ASTVisitor* v, void* context)
{
    return v->visitReturnStmt(this, context);
}

StringLiteral::StringLiteral(string value)
    : _value(value)
{
}

void* StringLiteral::doAccept(ASTVisitor* v, void* context)
{
    return v->visitStringLiteral(this, context);
}

SwitchStmt::SwitchStmt()
{
}

void* SwitchStmt::doAccept(ASTVisitor* v, void* context)
{
    return v->visitSwitchStmt(this, context);
}

StructTypeLibrary::StructTypeLibrary(int base_id /* = 0 */)
    : _next_id(base_id), _mapped_structures({})
{
}

TranslationUnitDecl::TranslationUnitDecl()
{
}

void* TranslationUnitDecl::doAccept(ASTVisitor* v, void* context)
{
    return v->visitTranslationUnitDecl(this, context);
}

Type::Type(const Datatype* dt)
    : _ghidra_dt(dt), _name(dt->getName())
{
}

Type::Type(string name)
    : _ghidra_dt(nullptr), _name(name)
{
}

void* Type::doAccept(ASTVisitor* v, void* context)
{
    return v->visitType(this, context);
}

BuiltinType::BuiltinType(const Datatype* dt)
    : Type(dt), _size(0), _is_floating(false), _is_signed(false), _ghidra_name("")
{
    if (dt) {
        _ghidra_name = dt->getName();
    }
}

BuiltinType::BuiltinType(string name, int size, bool isFloatingPoint, bool sign)
    : Type(name), _size(size), _is_floating(isFloatingPoint), _is_signed(sign), _ghidra_name(name)
{
}

string BuiltinType::name()
{
    if (isBool()) {
        return "bool";
    }

    if (isFloatingPoint()) {
        switch (size()) {
            case 4:
                return "float";
            case 8:
                return "double";
            default:
                callbacks->unimplementedCodeCallback("UNHANDLED BuiltinType FLOAT SIZE of " + size());
                return "UNHANDLED_FLOAT_SIZE_" + size();
        }
    }

    // on my x86_64 laptop:
    // char - 1B
    // short - 2B
    // int - 4B
    // long - 8B
    // long long - 8B

    switch(size()) {
        case 1:
            return isSigned() ? "char" : "unsigned char";
        case 2:
            return isSigned() ? "short" : "unsigned short";
        case 4:
            return isSigned() ? "int" : "unsigned int";
        case 8:
            return isSigned() ? "long" : "unsigned long";
        default:
            callbacks->unimplementedCodeCallback("UNHANDLED BuiltinType INT SIZE of " + size());
            return "UNHANDLED_INT_SIZE_" + size();
    }
}

int BuiltinType::size()
{
    return _ghidra_dt ? _ghidra_dt->getSize() : _size;
}

bool BuiltinType::isFloatingPoint()
{
    return _ghidra_dt ? _ghidra_dt->getMetatype() == TYPE_FLOAT : _is_floating;
}

bool BuiltinType::isSigned()
{
    if (_ghidra_dt) {
        auto meta = _ghidra_dt->getMetatype();
        return meta == TYPE_INT || meta == TYPE_BOOL || meta == TYPE_FLOAT;
    }
    return _is_signed;
}

bool BuiltinType::isBool()
{
    if (_ghidra_dt) {
        return _ghidra_dt->getMetatype() == TYPE_BOOL;
    }
    return false;
}

void* BuiltinType::doAccept(ASTVisitor* v, void* context)
{
    return v->visitBuiltinType(this, context);
}

ConstantArrayType::ConstantArrayType(const Datatype* elementType, int numElements)
    : Type(""), _num_elements(numElements)
{
    addChild(callbacks->toAstType(elementType));
}

ConstantArrayType::ConstantArrayType(const TypeArray* arrType)
    : ConstantArrayType(arrType->getBase(), arrType->numElements())
{
}

void* ConstantArrayType::doAccept(ASTVisitor* v, void* context)
{
    return v->visitConstantArrayType(this, context);
}

PointerType::PointerType(const Datatype* pointedToType)
    : Type("")
{
    addChild(callbacks->toAstType(pointedToType));
}

void* PointerType::doAccept(ASTVisitor* v, void* context)
{
    return v->visitPointerType(this, context);
}

void* StructType::doAccept(ASTVisitor* v, void* context)
{
    return v->visitStructType(this, context);
}

void* VoidType::doAccept(ASTVisitor* v, void* context)
{
    return v->visitVoidType(this, context);
}

TypedefDecl::TypedefDecl(string name)
    : _name(name)
{
}

void* TypedefDecl::doAccept(ASTVisitor* v, void* context)
{
    return v->visitTypedefDecl(this, context);
}

TypedefType::TypedefType(TypedefDecl* decl)
    : Type(decl->name()), _decl(decl)
{
}

void* TypedefType::doAccept(ASTVisitor* v, void* context)
{
    return v->visitTypedefType(this, context);
}

UnaryOperator::UnaryOperator(std::string opcode)//, Type* type)
    : _opcode(opcode) //, _type(type)
{
}

int UnaryOperator::precedence()
{
    if (_opcode == "*") {
        return 3;
    } else if (_opcode == "&") {
        return 3;
    }
    else {
        callbacks->unimplementedCodeCallback("TODO - map UnaryOperator precedence for '" + _opcode + "'");
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
    if (!_type) {
        _type = callbacks->toAstType(param->getType());
    }
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
    : ValueDecl(id), _sym(sym), _name(""), _type(nullptr), _ghidra_type(sym->getType())
{
    if (sym) {
        _name = getValidCLanguageName(sym->getName());
        _type = callbacks->toAstType(sym->getType());
    }
}

VarDecl::VarDecl(int id, string name, Type* type)
    : ValueDecl(id), _sym(nullptr), _name(getValidCLanguageName(name)), _type(type), _ghidra_type(nullptr)
{
}

VarDecl::VarDecl(int id, string name, const Datatype* dt)
    : ValueDecl(id), _sym(nullptr), _name(getValidCLanguageName(name)), _type(nullptr), _ghidra_type(dt)
{
    _type = callbacks->toAstType(dt);
}

void* VarDecl::doAccept(ASTVisitor* v, void* context)
{
    return v->visitVarDecl(this, context);
}
