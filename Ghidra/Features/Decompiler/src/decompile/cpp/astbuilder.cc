#include "astbuilder.h"

#include "funcdata.hh"

json buildAstForFunction(Funcdata* fd)
{
    if (!fd->isProcStarted()) {
        // not decompiled
    } else if (fd->hasNoStructBlocks()) {
        // not fully decompiled, no structure present
    }

    json js;
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
    return js;
}