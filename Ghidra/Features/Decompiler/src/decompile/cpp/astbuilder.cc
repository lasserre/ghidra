#include "astbuilder.h"

#include "funcdata.hh"

template<typename T> string to_hex(T data)
{
    stringstream ss;
    ss << hex << data;
    return ss.str();
}

json buildAstForFunction(Funcdata* fd)
{
    if (!fd->isProcStarted()) {
        // not decompiled
    } else if (fd->hasNoStructBlocks()) {
        // not fully decompiled, no structure present
    }

    json fdecl;
    // rough algorithm copied from PrintC::docFunction
    // emit->beginFunction(fd);
    // emitCommentFuncHeader(fd);
    // emitFunctionDeclaration(fd);    // Causes us to enter function's scope
    // emitLocalVarDecls(fd);
    // if (isSet(flat))
    //   emitBlockGraph(&fd->getBasicBlocks());
    // else
    //   emitBlockGraph(&fd->getStructure());
    // popScope();                // Exit function's scope
    // emit->endFunction(id1);

    // js["FunctionDecl"] = json{{"tag", "FunctionDecl"}, {"name", "hello"}};

    fdecl["node_type"] = "FunctionDecl";
    fdecl["name"] = fd->getName();
    fdecl["address"] = to_hex(fd->getAddress().getOffset());

    // TODO: write a generic "datatype_to_json()" function that will handle however
    // we need to process data types and output a JSON object that corresponds to
    // whatever my resulting "DataType" object will look like
    // (e.g. do we also include category here? structure definitions? type id/database?)
    // Datatype* dt = fd->getFuncProto().getOutput()->getType();

    auto fd1 = json{{"node_type", "FunctionDecl"}, {"name", "hello"}};
    auto fd2 = json{{"node_type", "FunctionDecl"}, {"name", "anotherFunc"}};
    // js = json{fd1, fd2};

    return fdecl;
}