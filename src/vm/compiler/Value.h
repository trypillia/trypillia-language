#ifndef TRYPILLIA_VALUE_H
#define TRYPILLIA_VALUE_H

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

struct Obj;
struct ObjString;
struct ObjFunction;
struct ObjClosure;
struct ObjNative;
struct ObjList;
struct ObjMap;
struct ObjClass;
struct ObjInstance;
struct ObjBoundMethod;
struct ObjWeakRef;
struct ObjUpvalue;
struct ObjPromise;

enum class ObjType {
    OBJ_STRING,
    OBJ_FUNCTION,
    OBJ_CLOSURE,
    OBJ_NATIVE,
    OBJ_LIST,
    OBJ_MAP,
    OBJ_CLASS,
    OBJ_INSTANCE,
    OBJ_BOUND_METHOD,
    OBJ_WEAK_REF,
    OBJ_UPVALUE,
    OBJ_PROMISE
};

struct Obj {
    ObjType type;
    Obj *nextObj;
    bool isMarked;

    Obj(ObjType type);
    virtual ~Obj() = default;
};

#define QNAN ((uint64_t)0x7ffc000000000000)
#define SIGN_BIT ((uint64_t)0x8000000000000000)
#define TAG_NIL 1
#define TAG_FALSE 2
#define TAG_TRUE 3

class VMValue {
    uint64_t val;

  public:
    VMValue() : val(QNAN | TAG_NIL) {
    }
    VMValue(std::nullptr_t) : val(QNAN | TAG_NIL) {
    }
    VMValue(bool b) : val(QNAN | (b ? TAG_TRUE : TAG_FALSE)) {
    }
    VMValue(double num) {
        memcpy(&val, &num, sizeof(double));
    }
    VMValue(const std::string &s);
    VMValue(const char *s);

    VMValue(Obj *obj) {
        val = SIGN_BIT | QNAN | (uint64_t)(uintptr_t)obj;
    }

    VMValue(const VMValue &other) : val(other.val) {
    }

    VMValue &operator=(const VMValue &other) {
        val = other.val;
        return *this;
    }

    ~VMValue() {
    }

    bool isNumber() const {
        return (val & QNAN) != QNAN;
    }
    bool isNil() const {
        return val == (QNAN | TAG_NIL);
    }
    bool isBool() const {
        return (val | 1) == (QNAN | TAG_TRUE);
    }
    bool isObj() const {
        return (val & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT);
    }

    double asNumber() const {
        double num;
        memcpy(&num, &val, sizeof(double));
        return num;
    }

    bool asBool() const {
        return val == (QNAN | TAG_TRUE);
    }
    Obj *asObj() const {
        return (Obj *)(uintptr_t)(val & ~(SIGN_BIT | QNAN));
    }

    bool isString() const {
        return isObj() && asObj()->type == ObjType::OBJ_STRING;
    }
    ObjString *asString() const {
        return (ObjString *)asObj();
    }

    bool isFunction() const {
        return isObj() && asObj()->type == ObjType::OBJ_FUNCTION;
    }
    ObjFunction *asFunction() const {
        return (ObjFunction *)asObj();
    }

    bool isClosure() const {
        return isObj() && asObj()->type == ObjType::OBJ_CLOSURE;
    }
    ObjClosure *asClosure() const {
        return (ObjClosure *)asObj();
    }

    bool isNative() const {
        return isObj() && asObj()->type == ObjType::OBJ_NATIVE;
    }
    ObjNative *asNative() const {
        return (ObjNative *)asObj();
    }

    bool isList() const {
        return isObj() && asObj()->type == ObjType::OBJ_LIST;
    }
    ObjList *asList() const {
        return (ObjList *)asObj();
    }

    bool isMap() const {
        return isObj() && asObj()->type == ObjType::OBJ_MAP;
    }
    ObjMap *asMap() const {
        return (ObjMap *)asObj();
    }

    bool isClass() const {
        return isObj() && asObj()->type == ObjType::OBJ_CLASS;
    }
    ObjClass *asClass() const {
        return (ObjClass *)asObj();
    }

    bool isInstance() const {
        return isObj() && asObj()->type == ObjType::OBJ_INSTANCE;
    }
    ObjInstance *asInstance() const {
        return (ObjInstance *)asObj();
    }

    bool isBoundMethod() const {
        return isObj() && asObj()->type == ObjType::OBJ_BOUND_METHOD;
    }
    ObjBoundMethod *asBoundMethod() const {
        return (ObjBoundMethod *)asObj();
    }

    bool isWeakRef() const {
        return isObj() && asObj()->type == ObjType::OBJ_WEAK_REF;
    }
    ObjWeakRef *asWeakRef() const {
        return (ObjWeakRef *)asObj();
    }

    bool isUpvalue() const {
        return isObj() && asObj()->type == ObjType::OBJ_UPVALUE;
    }
    ObjUpvalue *asUpvalue() const {
        return (ObjUpvalue *)asObj();
    }

    bool isPromise() const {
        return isObj() && asObj()->type == ObjType::OBJ_PROMISE;
    }
    ObjPromise *asPromise() const {
        return (ObjPromise *)asObj();
    }

    uint64_t getRaw() const {
        return val;
    }

    std::string toString(bool inContainer = false) const;

    bool operator==(const VMValue &other) const {
        if (val == other.val)
            return true;
        if (isNumber() && other.isNumber())
            return asNumber() == other.asNumber();
        return equalsImpl(other);
    }

  private:
    bool equalsImpl(const VMValue &other) const;
};

#endif // TRYPILLIA_VALUE_H
