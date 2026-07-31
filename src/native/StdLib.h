#ifndef TRYPILLIA_STDLIB_H
#define TRYPILLIA_STDLIB_H

#include "../frontend/symbol/SymbolTable.h"
#include "../vm/core/VM.h"
#include "list/List.h"
#include "map/Map.h"
#include "os/OS.h"
#include "regex/Regex.h"
#include "string/String.h"
#include "terminal/Terminal.h"
#include "test/Test.h"

namespace StdLib {
// Registers actual functions and objects in the VM
void registerAll(VM *vm);

// Registers symbol names in the compiler's semantic analyzer scope
void registerSymbols(SymbolTable *scope);

// Helpers for returning Result objects
VMValue makeResultOk(VM *vm, VMValue value);
VMValue makeResultErr(VM *vm, const std::string &message, double code = 0.0);
} // namespace StdLib

#endif
