#pragma once

#include "astvisitor.h"

/**
 * @brief ASTVisitor that generates forward declarations for each non-builtin
 * data type referenced within an AST snippet
 *
 */
class DataTypeForwardDeclVisitor : public ASTVisitor
{
public:
    /**
     * @param builder is needed for reuse of some PrintC member
     * functions, otherwise I don't need it
     */
    DataTypeForwardDeclVisitor();

    /**
     * @brief Inserts statements as children of the given parent node that
     * declare all non-builtin data types referenced in the processed AST.
     * This only inserts data if it is run after the target AST is processed
     * (i.e. node->accept(visitor) has executed on the node of interest).
     *
     * NOTE: the only things that need to be forward declared are:
     * 1. user data types
     * 2. functions called
     * 3. global variables referenced
     *
     * DECISION: only do non-builtin DATA TYPES here!!
     *           (leave globals/funcs in ast builder for now!)
     *
     * @param parent
     */
    void insertForwardDecls(ASTNode* parent);

    virtual void* visitCStyleCastExpr(CStyleCastExpr*, void*);
    virtual void* visitDeclRefExpr(DeclRefExpr*, void*);
    virtual void* visitDeclStmt(DeclStmt*, void*);
    virtual void* visitParmVarDecl(ParmVarDecl*, void*);
    virtual void* visitVarDecl(VarDecl*, void*);

protected:
    /**
     * @brief Checks the given data type to see if it should
     * be forward-declared. If so, inserts it as a child of the
     * parent node supplied at insertForwardDecls()
     *
     * @param dt
     */
    void checkDataType(Datatype* dt);

    // forward-decl nodes to add when finished traversing
    // the AST (so we don't modify _parent's children while traversing)
    map<string, ASTNode*> _children_to_add;
};
