#include "commavisitor.h"

void CommaVisitor::fixCommaOps(ASTNode* root)
{
    // visit the tree first to collect ops to remove and insert hierarchical
    // comma ops
    root->accept(this, nullptr);

    for (auto& op : comma_ops_to_remove) {
        op->replaceWithNodeShallow(op->children()->front());
        op->children()->clear();    // clear op's pointer to child so delete leaves child alone
        delete op;
    }
}

void* CommaVisitor::visitBinaryOperator(BinaryOperator* binop, void* context)
{
    if (binop->opcode() != ",") {
        return nullptr;
    }

    if (binop->children()->size() == 1) {
        // replace this with its child
        comma_ops_to_remove.push_back(binop);
    } else if (binop->children()->size() > 2) {
        // accept() visits NODE then visits CHILDREN, so I can move the child nodes
        // around here/now

        // insert comma as binop's FIRST child and place all current children
        // under it EXCEPT the last one
        // then we visit the newly-created comma and the algorithm will fix
        // itself going top-down...

        // remove last child first
        ASTNode* last_child = binop->children()->back();
        binop->children()->pop_back();

        // save off previous children and clear binop's children
        vector<ASTNode*> other_children;
        for (int i = 0; i < binop->children()->size(); i++) {
            other_children.push_back(binop->children()->at(i));
        }
        binop->children()->clear();

        // add CommaOp
        BinaryOperator* comma_op = new BinaryOperator(",", binop->instr_addr());
        binop->addChild(comma_op);
        binop->addChild(last_child);

        // add previous children underneath comma_op
        for (int i = 0; i < other_children.size(); i++) {
            // comma op has LOWEST precedence so we'll never add parens
            // when it is the parent node
            comma_op->addChild(other_children.at(i));   // /*append=*/ true, /*check_parens=*/ false);
        }
    }

    return nullptr;
}