#pragma once

#include "astvisitor.h"

class AttachedTypeUpdate;
class ASTBuilder;

struct NewTypedef
{
    /** @brief The newly-created TypedefDecl */
    TypedefDecl* decl;

    // these are proper nodes in the tree and must be replaced by preserving
    // parent/child links
    // vector<Type*> tree_node_uses;

    // try a map to ensure we only have one entry per pointer
    map<void*, Type*> tree_node_uses;

    // these are Type* instances which are properties of nodes in the tree
    // (attached to these nodes) but not actual children. Thus there is no
    // parent/child link - we just need to update the pointer where they are
    // stored to update the type
    vector<AttachedTypeUpdate*> attached_uses;

    void addUse(AttachedTypeUpdate* atu, Type* type);
};

/**
 * @brief ASTVisitor that generates forward declarations for each non-builtin
 * data type referenced within an AST snippet
 *
 */
class TypedefDeclVisitor : public ASTVisitor
{
public:
    /**
     * @param builder is needed for reuse of some PrintC member
     * functions, otherwise I don't need it
     */
    TypedefDeclVisitor(ASTBuilder* builder);

    /**
     * @brief Inserts TypedefDecls as children of the given parent node that
     * declare all non-builtin data types referenced in the processed AST.
     * This only inserts data if it is run after the target AST is processed
     * (i.e. node->accept(visitor) has executed on the node of interest).
     *
     * @param parent
     */
    void insertTypedefs(ASTNode* parent);

    // -------------- PROCESS NODE TYPES
    // virtual void* visitBuiltinType(BuiltinType*, void*);
    virtual void* visitConstantArrayType(ConstantArrayType*, void*);
    virtual void* visitType(Type*, void*);
    // CLS: don't think we need this? if there is a TypedefType
    // it should already be declared...
    // virtual void* visitTypedefType(TypedefType*, void*);
    // -------------- CHECK NON-NODE DATA TYPES
    virtual void* visitCStyleCastExpr(CStyleCastExpr*, void*);
    virtual void* visitDeclRefExpr(DeclRefExpr*, void*);
    virtual void* visitFieldDecl(FieldDecl*, void*);
    virtual void* visitFunctionDecl(FunctionDecl*, void*);
    virtual void* visitParmVarDecl(ParmVarDecl*, void*);
    virtual void* visitVarDecl(VarDecl*, void*);

    /** CLS: HIJACKING THIS CLASS TO DO SOMETHING DIFFERENT */
    virtual void* visitParenExpr(ParenExpr*, void*);

protected:

    void insertNewTypedef(Type* alias_type, Type* real_type, AttachedTypeUpdate* atu = nullptr);

    // forward-decl nodes to add when finished traversing
    // the AST (so we don't modify _parent's children while traversing)
    map<string, NewTypedef> _typedefs_to_add;
    // hidden parens to remove when finished traversing
    // the AST (so we don't modify _parent's children while traversing)
    vector<ParenExpr*> _hidden_parens_to_remove;
    // HACK: this is for access to builder.toAstType() because I'm rushing...
    // if someone has time to rearchitect this could have a cleaner solution :)
    ASTBuilder* _builder;
};
