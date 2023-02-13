#include "datatypeforwarddeclvisitor.h"

DataTypeForwardDeclVisitor::DataTypeForwardDeclVisitor()
    : _children_to_add()
{
}

void DataTypeForwardDeclVisitor::insertForwardDecls(ASTNode* parent)
{
    parent->accept(this);   // generate list of child nodes to add

    for (const auto& entry : _children_to_add) {
        // entry.second contains the value
        // Do something with entry.second
        parent->addChild(entry.second);
    }
}

// from ghidra_arch.cc:
// types->setCoreType("void",1,TYPE_VOID,false);
// types->setCoreType("bool",1,TYPE_BOOL,false);
// types->setCoreType("byte",1,TYPE_UINT,false);
// types->setCoreType("word",2,TYPE_UINT,false);
// types->setCoreType("dword",4,TYPE_UINT,false);
// types->setCoreType("qword",8,TYPE_UINT,false);
// types->setCoreType("char",1,TYPE_INT,true);
// types->setCoreType("sbyte",1,TYPE_INT,false);
// types->setCoreType("sword",2,TYPE_INT,false);
// types->setCoreType("sdword",4,TYPE_INT,false);
// types->setCoreType("sqword",8,TYPE_INT,false);
// types->setCoreType("float",4,TYPE_FLOAT,false);
// types->setCoreType("float8",8,TYPE_FLOAT,false);
// types->setCoreType("float16",16,TYPE_FLOAT,false);
// types->setCoreType("undefined",1,TYPE_UNKNOWN,false);
// types->setCoreType("undefined2",2,TYPE_UNKNOWN,false);
// types->setCoreType("undefined4",4,TYPE_UNKNOWN,false);
// types->setCoreType("undefined8",8,TYPE_UNKNOWN,false);
// types->setCoreType("code",1,TYPE_CODE,false);
// types->setCoreType("wchar",2,TYPE_INT,true);

// e.g. typedef uint32_t undefined4;

// on my x86_64 laptop:
// char - 1B
// short - 2B
// int - 4B
// long - 8B
// long long - 8B

/**
 * ASSUMPTIONS:
 * - assume the above size mapping
 * - if/when that is wrong or a problem, we can always post-process
 *   the AST in python and replace the typedefs for undefinedX to
 *   some other mapping
 */

void DataTypeForwardDeclVisitor::checkDataType(Datatype* dt)
{
    // if we've got this type forward-declared already, don't bother!
    if (_children_to_add.count(dt->getName()))
        return;

    switch (dt->getMetatype()) {
        case TYPE_UNKNOWN:
            /** NOTE: these are unsigned types */

            break;
        default:
            break;
    }
}

void* DataTypeForwardDeclVisitor::visitCStyleCastExpr(CStyleCastExpr* castexpr, void* context)
{
    // TODO: checkDataType can go away...
    // checkDataType(castexpr->dt());

    // TODO: add methods to visit various concrete data types!
    // ...then here we simply call
    castexpr->type()->accept(this);
    return nullptr;
}

void* DataTypeForwardDeclVisitor::visitDeclRefExpr(DeclRefExpr*, void* context)
{
    return nullptr;
}

void* DataTypeForwardDeclVisitor::visitDeclStmt(DeclStmt*, void* context)
{
    return nullptr;
}

void* DataTypeForwardDeclVisitor::visitParmVarDecl(ParmVarDecl*, void* context)
{
    return nullptr;
}

void* DataTypeForwardDeclVisitor::visitVarDecl(VarDecl*, void* context)
{
    return nullptr;
}
