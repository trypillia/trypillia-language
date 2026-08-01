#ifndef TRYPILLIA_GC_H
#define TRYPILLIA_GC_H

#include "../compiler/Value.h"

class VM;

class GC {
 public:
  static void markValue(VMValue& value);
  static void markObj(Obj* obj);
  static void collect(VM* vm);
};

#endif
