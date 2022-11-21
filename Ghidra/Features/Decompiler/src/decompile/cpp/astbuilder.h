#include "../../../third-party/json/single_include/nlohmann/json.hpp"

using json = nlohmann::json;

class Funcdata;

/**
 * Constructs an AST representation from Ghidra's internal representation of
 * the high-level code. The PrintLanguage (PrintC) class was used as a reference
 * to develop the algorithm for traversing the information in FuncData, etc
 * needed to construct an AST.
 *
 * CLS: not even sure I need the builder class yet...
*/
json buildAstForFunction(Funcdata* fd);

class AstBuilder : public PrintC
{
public:
    /**
     * @brief Construct a new Ast Builder object
     *
     * @param fbody is the CompoundStmt node (child to FunctionDecl node) that
     * contains the function body statements
     */
    AstBuilder(json& fbody)
        : _fbody(fbody), _context_stack(), PrintC(nullptr)
    { }

    /**
     * This is the BlockVisitor interface inside PrintLanguage, but it is not
     * broken out into its own interface...so I have to inherit from PrintC to
     * avoid implementing all the other pure virtual functions I don't care about
     */
    virtual void emitBlockBasic(const BlockBasic *bb);
    virtual void emitBlockGraph(const BlockGraph *bl);
    virtual void emitBlockCopy(const BlockCopy *bl);
    virtual void emitBlockGoto(const BlockGoto *bl);
    virtual void emitBlockLs(const BlockList *bl);
    virtual void emitBlockCondition(const BlockCondition *bl);
    virtual void emitBlockIf(const BlockIf *bl);
    virtual void emitBlockWhileDo(const BlockWhileDo *bl);
    virtual void emitBlockDoWhile(const BlockDoWhile *bl);
    virtual void emitBlockInfLoop(const BlockInfLoop *bl);
    virtual void emitBlockSwitch(const BlockSwitch *bl);

    /**
     * @brief Pushes the current context onto the stack, making
     * new_context the current context
     */
    void pushAstContext(json& new_context);

    /**
     * @brief Pops and returns the current context
     *
     * @return json&
     */
    json& popAstContext();

    /**
     * @brief Returns a reference to the current node (at the top of
     * the stack)
     */
    json& currentNode();

protected:
    json& _fbody;

    /**
     * Current context for "emitting" into the AST. This could have been passed as
     * a parameter to each function call, but due to the Ghidra interface I'm
     * conforming to it's easier to maintain state separately like they did
     */
    // json& _current_node;
    vector<json&> _context_stack;
};
