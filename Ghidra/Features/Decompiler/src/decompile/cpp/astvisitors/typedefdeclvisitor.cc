#include "typedefdeclvisitor.h"

/**
 * Probably a better way to do this...but I just need a flexible way
 * of update Type* properties that are ATTACHED to nodes but not proper
 * nodes themselves (i.e. not children of other nodes). This gives me
 * alot of flexibility, especially if a particular node type ends up
 * having multiple Type* properties that need to be updated...instead
 * of another visitor implementation "not being sure" which property
 * needs to be updated, this approach lets me simply define 2 lambdas
 * - one for each property - and it should "just work".
 */
struct AttachedTypeUpdate
{
    AttachedTypeUpdate(ASTNode* (*updateType)(ASTNode*,Type*), ASTNode* node)
        : updateType(updateType), node(node)
    {}

    ASTNode* (*updateType)(ASTNode*, Type*);
    ASTNode* node;
};

void NewTypedef::addUse(AttachedTypeUpdate* atu, Type* type)
{
    if (atu) {
        // make a copy so caller can delete
        attached_uses.push_back(new AttachedTypeUpdate(*atu));
    } else {
        tree_node_uses.push_back(type);
    }
}

TypedefDeclVisitor::TypedefDeclVisitor()
    : _typedefs_to_add()
{
}

void TypedefDeclVisitor::insertTypedefs(ASTNode* parent)
{
    parent->accept(this);   // generate list of child nodes to add

    for (const auto& entry : _typedefs_to_add) {
        // add typedef declarations for each at the top
        parent->addChild(entry.second.decl, /* append =*/ false);

        // replace each use of this typedef
        for (Type* typedef_use : entry.second.tree_node_uses) {
            typedef_use->replaceWith(new TypedefType(entry.second.decl));
            delete typedef_use;     // clean up the original
        }

        for (AttachedTypeUpdate* attached_update : entry.second.attached_uses) {
            attached_update->updateType(
                attached_update->node,
                new TypedefType(entry.second.decl
            ));

            delete attached_update;     // done with this now
        }
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

/**
 * ------------------------------------------------------------------------
 * PROCESS NODE TYPES
 * ------------------------------------------------------------------------
 * These methods actually create and save the needed typedefs, and are used
 * both for data type nodes in the AST hierarchy proper as well as ones
 * which are attached to AST nodes and caught by the methods below
 * (like CStyleCastExpr->type() which is not inside CStyleCastExpr's
 * children)
 */
// void* TypedefDeclVisitor::visitBuiltinType(BuiltinType* bit, void*)
// {

// }

void* TypedefDeclVisitor::visitType(Type* type, void* atu)
{
    if (_typedefs_to_add.count(type->name())) {
        // already defined the typedef, so add this use to the list
        _typedefs_to_add[type->name()].addUse((AttachedTypeUpdate*)atu, type);
        return nullptr;
    }

    const Datatype* dt = type->ghidra_dtype();
    auto meta = dt->getMetatype();
    string name = "";

    // if we detect the need for a typedef, we'll make real_type point
    // to the existing type the typedef is based on
    Type* real_type = nullptr;

    switch (meta) {
        case TYPE_UNKNOWN:
            switch (type->ghidra_dtype()->getSize()) {
                case 1:
                    name = "unsigned char";
                    break;
                case 2:
                    name = "unsigned short";
                    break;
                case 4:
                    name = "unsigned int";
                    break;
                case 8:
                    name = "unsigned long";
                    break;
                default:
                    break;
            }
            real_type = new BuiltinType(name, dt->getSize(), false, false);
            break;
        default:
            break;
    }

    if (real_type) {
        // we found a type that needs a typedef
        NewTypedef td;
        td.decl = new TypedefDecl(type->name());
        td.decl->addChild(real_type);
        td.addUse((AttachedTypeUpdate*)atu, type);
        _typedefs_to_add.insert({type->name(), td});
    }

    return nullptr;
}

/**
 * ------------------------------------------------------------------------
 * CHECK NON-NODE DATA TYPES
 * ------------------------------------------------------------------------
 * These methods are here to process each kind of node that may have a
 * datatype embedded in it (i.e. a data type node that is not a child but
 * is accessed from another node, like CStyleCastExpr->type())
 */

void* TypedefDeclVisitor::visitCStyleCastExpr(CStyleCastExpr* castexpr, void* context)
{
    auto type_update = new AttachedTypeUpdate(
        [](ASTNode* cast, Type* newtype) {
            ((CStyleCastExpr*)cast)->replace_type(newtype);
            return cast;
        },
        castexpr
    );

    castexpr->type()->accept(this, type_update);

    delete type_update;
    return nullptr;
}

void* TypedefDeclVisitor::visitDeclRefExpr(DeclRefExpr* declref, void* context)
{
    declref->ref()->accept(this);
    return nullptr;
}

void* TypedefDeclVisitor::visitFunctionDecl(FunctionDecl* fdecl, void* context)
{
    auto type_update = new AttachedTypeUpdate(
        [](ASTNode* fd, Type* newtype) {
            ((FunctionDecl*)fd)->replace_return_type(newtype);
            return fd;
        },
        fdecl
    );

    fdecl->return_type()->accept(this, type_update);

    delete type_update;
    return nullptr;
}

void* TypedefDeclVisitor::visitParmVarDecl(ParmVarDecl* pvdecl, void* context)
{
    auto type_update = new AttachedTypeUpdate(
        [](ASTNode* pv, Type* newtype) {
            ((ParmVarDecl*)pv)->replace_type(newtype);
            return pv;
        },
        pvdecl
    );

    pvdecl->type()->accept(this, type_update);

    delete type_update;
    return nullptr;
}

void* TypedefDeclVisitor::visitVarDecl(VarDecl* vdecl, void* context)
{
    auto type_update = new AttachedTypeUpdate(
        [](ASTNode* vd, Type* newtype) {
            ((VarDecl*)vd)->replace_type(newtype);
            return vd;
        },
        vdecl
    );

    vdecl->type()->accept(this, type_update);

    delete type_update;
    return nullptr;
}
