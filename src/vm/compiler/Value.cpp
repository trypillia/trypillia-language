#include "Value.h"
#include <string>

// To support ObjString instantiation, we might need a full definition of ObjString.
// If it's defined in VM.h or somewhere else, we might need to include it.
// Assuming it's defined in some header like "VM.h" or "Value.h" includes it.
// Actually, looking at VM.cpp, it included "VM.h". Let's just include "VM.h" which has ObjString.
#include "../core/VM.h"

VMValue::VMValue(const std::string &s) {
    Obj *obj = new ObjString(s);
    val = SIGN_BIT | QNAN | (uint64_t)(uintptr_t)obj;
}

VMValue::VMValue(const char *s) {
    Obj *obj = new ObjString(std::string(s));
    val = SIGN_BIT | QNAN | (uint64_t)(uintptr_t)obj;
}

Obj::Obj(ObjType type) : type(type), isMarked(false), nextObj(nullptr) {
    if (currentVM) {
        this->nextObj = currentVM->objects;
        currentVM->objects = this;
        currentVM->bytesAllocated += 256; // heuristic
    }
}

std::string VMValue::toString(bool inContainer) const {
    if (this->isNumber()) {
        std::string s = std::to_string(this->asNumber());
        s.erase(s.find_last_not_of('0') + 1, std::string::npos);
        if (s.back() == '.')
            s.pop_back();
        if (s.empty())
            return "0";
        return s;
    } else if (this->isString()) {
        if (inContainer) {
            return "\"" + this->asString()->flatten() + "\"";
        } else {
            return this->asString()->flatten();
        }
    } else if (this->isBool()) {
        return this->asBool() ? "true" : "false";
    } else if (this->isList()) {
        std::string s = "[";
        auto list = this->asList();
        for (size_t i = 0; i < list->elements.size(); ++i) {
            s += list->elements[i].toString(true);
            if (i < list->elements.size() - 1)
                s += ", ";
        }
        s += "]";
        return s;
    } else if (this->isMap()) {
        std::string s = "{";
        auto map = this->asMap();
        size_t i = 0;
        for (auto const &[k, v] : map->values) {
            s += k.toString(true) + ": " + v.toString(true);
            if (i < map->values.size() - 1)
                s += ", ";
            i++;
        }
        s += "}";
        return s;
    } else if (this->isClass()) {
        return "<class " + this->asClass()->name + ">";
    } else if (this->isInstance()) {
        return "<instance of " + this->asInstance()->klass->name + ">";
    } else if (this->isBoundMethod()) {
        auto boundMethod = this->asBoundMethod()->method;
        if (boundMethod.isFunction()) {
            return "<bound method " + boundMethod.asFunction()->name + ">";
        } else {
            return "<bound method " + boundMethod.asNative()->name + ">";
        }
    } else if (this->isFunction()) {
        return "<function " + this->asFunction()->name + ">";
    } else if (this->isNative()) {
        return "<native fn " + this->asNative()->name + ">";
    } else if (this->isClosure()) {
        return "<closure " + this->asClosure()->function->name + ">";
    } else if (this->isWeakRef()) {
        return "<weakref>";
    } else if (this->isPromise()) {
        auto p = this->asPromise();
        if (p->resolved)
            return "<promise resolved>";
        else
            return "<promise pending>";
    } else if (this->isNil()) {
        return "nil";
    }
    return "<unknown>";
}

bool VMValue::equalsImpl(const VMValue &other) const {
    if (isString() && other.isString()) {
        return asString()->flatten() == other.asString()->flatten();
    }
    return false;
}
