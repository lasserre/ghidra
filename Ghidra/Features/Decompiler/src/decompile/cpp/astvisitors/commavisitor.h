#pragma once

#include "astvisitor.h"

class CommaVisitor : public ASTVisitor
{
public:
    CommaVisitor()
    { }

    void fixCommaOps(ASTNode* root);

    virtual void* visitBinaryOperator(BinaryOperator*, void*);

protected:
    vector<BinaryOperator*> comma_ops_to_remove;
};