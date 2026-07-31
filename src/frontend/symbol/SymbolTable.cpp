#include "SymbolTable.h"

SymbolTable::SymbolTable(SymbolTable *parent) : parent(parent) {
}

bool SymbolTable::define(const Symbol &symbol) {
    if (symbols.count(symbol.name))
        return false;
    symbols[symbol.name] = symbol;
    return true;
}

Symbol *SymbolTable::resolve(const std::string &name) {
    if (symbols.count(name))
        return &symbols[name];
    if (parent)
        return parent->resolve(name);
    return nullptr;
}

SymbolTable *SymbolTable::getParent() const {
    return parent;
}
