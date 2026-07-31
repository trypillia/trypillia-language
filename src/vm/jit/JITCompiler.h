#ifndef TRYPILLIA_JIT_H
#define TRYPILLIA_JIT_H

#include "../compiler/Chunk.h"
#include "Emitter.h"
#include <memory>
#include <variant>

class JITCompiler {
  public:
    JITCompiler() {
    }
    ~JITCompiler() {
    }

    // Tries to compile a chunk into native code using sljit.
    // Returns nullptr if compilation is unsupported (e.g., uses strings or objects)
    JitFunc compileMathFunction(ObjFunction *function);
};

#endif // TRYPILLIA_JIT_H
