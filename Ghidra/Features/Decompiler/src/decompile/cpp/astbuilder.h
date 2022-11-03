#include "../../../third-party/json/single_include/nlohmann/json.hpp"

using json = nlohmann::json;

class FuncData;

/**
 * Constructs an AST representation from Ghidra's internal representation of
 * the high-level code. The PrintLanguage (PrintC) class was used as a reference
 * to develop the algorithm for traversing the information in FuncData, etc
 * needed to construct an AST.
*/
class AstBuilder
{
    /**
     * Builds the AST for the given function as a JSON object
    */
    json buildAstForFunction(FuncData* fd);

    /**
     * Just return a json object? Or make an actual class?
     *
     * NOTE: speed is priority right now...just create a JSON object for starters,
     * (using strings and w/e else directly...)
     * if I need to turn it into an AST object or see a clear path to doing so
     * it will be much quicker to CONVERT it later than plan it now :)
    */
    // GhidraAst buildAstForFunction(FuncData* fd);
};