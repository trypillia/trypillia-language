#ifndef TRYPILLIA_JIT_H
#define TRYPILLIA_JIT_H

#include <memory>
#include <variant>

#include "../compiler/Chunk.h"
#include "Emitter.h"

class JITCompiler
{
  public:
    JITCompiler()
    {
    }
    ~JITCompiler()
    {
    }

    // Tries to compile a chunk into native code using sljit.
    // Returns nullptr if compilation is unsupported (e.g., uses strings or
    // objects)
    JitFunc compileMathFunction(ObjFunction *function);
};

#endif // TRYPILLIA_JIT_H
