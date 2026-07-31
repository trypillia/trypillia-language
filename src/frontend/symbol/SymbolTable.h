#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

#include <string>
#include <unordered_map>

class Symbol {
  public:
    std::string name;
    std::string type;
    bool isConst;
};

class SymbolTable {
  private:
    std::unordered_map<std::string, Symbol> symbols;
    SymbolTable *parent;

  public:
    SymbolTable(SymbolTable *parent = nullptr);

    bool define(const Symbol &symbol);

    Symbol *resolve(const std::string &name);

    SymbolTable *getParent() const;
};

#endif // SYMBOL_TABLE_H
