#ifndef TRYPILLIA_CHUNK_H
#define TRYPILLIA_CHUNK_H

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "OpCode.h"
#include "Value.h"
enum class VMAccessModifier
{
    PUBLIC,
    PRIVATE,
    PROTECTED
};

class Chunk;

struct ObjFunction;
struct ObjClosure;
struct ObjUpvalue;
struct ObjNative;
struct ObjList;
struct ObjMap;
struct ObjClass;
struct ObjInstance;
struct ObjBoundMethod;
struct ObjWeakRef;

struct ObjString : public Obj
{
    mutable std::string flatData;
    mutable ObjString *left;
    mutable ObjString *right;
    size_t length;
    mutable bool isFlat;

    ObjString(std::string s)
        : Obj(ObjType::OBJ_STRING), flatData(std::move(s)), left(nullptr), right(nullptr), length(flatData.length()),
          isFlat(true)
    {
    }
    ObjString(ObjString *l, ObjString *r)
        : Obj(ObjType::OBJ_STRING), left(l), right(r), length(l->length + r->length), isFlat(false)
    {
    }

    std::string flatten() const
    {
        if (isFlat)
            return flatData;

        std::vector<const ObjString *> stack;
        std::string result;
        result.reserve(length);

        stack.push_back(this);

        while (!stack.empty())
        {
            const ObjString *current = stack.back();
            stack.pop_back();

            if (current->isFlat)
            {
                result += current->flatData;
            }
            else
            {
                if (current->right)
                    stack.push_back(current->right);
                if (current->left)
                    stack.push_back(current->left);
            }
        }

        flatData = std::move(result);
        left = nullptr;
        right = nullptr;
        isFlat = true;
        return flatData;
    }

    ~ObjString()
    {
    }
};

using NativeFn = VMValue (*)(int argCount, VMValue *args);

struct VMValueHash
{
    std::size_t operator()(const VMValue &v) const
    {
        if (v.isNumber())
            return std::hash<double>{}(v.asNumber());
        if (v.isString())
            return std::hash<std::string>{}(v.asString()->flatten());
        if (v.isBool())
            return std::hash<bool>{}(v.asBool());
        if (v.isNil())
            return 0;
        return std::hash<uint64_t>{}(v.getRaw());
    }
};

struct VMValueEqual
{
    bool operator()(const VMValue &a, const VMValue &b) const
    {
        return a == b;
    }
};

struct ObjClass : public Obj
{
    std::string name;
    std::unordered_map<std::string, VMValue> methods;
    ObjClass *superclass;
    bool isAbstract = false;
    std::unordered_map<std::string, VMValue> statics;
    std::unordered_map<std::string, VMAccessModifier> fieldModifiers;
    ObjClass(std::string name) : Obj(ObjType::OBJ_CLASS), name(name), superclass(nullptr)
    {
    }
};

struct ObjInstance : public Obj
{
    ObjClass *klass;
    std::unordered_map<std::string, VMValue> fields;

    // Native resource binding
    void *nativeData = nullptr;
    void (*freeFn)(void *) = nullptr;

    ObjInstance(ObjClass *k) : Obj(ObjType::OBJ_INSTANCE), klass(k)
    {
    }

    ~ObjInstance()
    {
        if (nativeData && freeFn)
        {
            freeFn(nativeData);
            nativeData = nullptr;
        }
    }
};

struct ObjBoundMethod : public Obj
{
    VMValue receiver;
    VMValue method;
    ObjBoundMethod(VMValue r, VMValue m) : Obj(ObjType::OBJ_BOUND_METHOD), receiver(r), method(m)
    {
    }
};

struct ObjWeakRef : public Obj
{
    Obj *weakRef;
    ObjWeakRef() : Obj(ObjType::OBJ_WEAK_REF), weakRef(nullptr)
    {
    }
    VMValue lock() const
    {
        return weakRef ? VMValue(weakRef) : VMValue(nullptr);
    }
};

struct ObjPromise : public Obj
{
    VMValue value;
    bool resolved;
    ObjClosure *onFulfilled;
    ObjClosure *onRejected;
    ObjPromise *next;
    std::vector<VMValue> thenHandlers;

    ObjPromise()
        : Obj(ObjType::OBJ_PROMISE), value(nullptr), resolved(false), onFulfilled(nullptr), onRejected(nullptr),
          next(nullptr)
    {
    }
};

struct ObjList : public Obj
{
    std::vector<VMValue> elements;
    ObjList(const std::vector<VMValue> &e) : Obj(ObjType::OBJ_LIST), elements(e)
    {
    }
};

struct ObjMap : public Obj
{
    std::unordered_map<VMValue, VMValue, VMValueHash, VMValueEqual> values;
    ObjMap() : Obj(ObjType::OBJ_MAP)
    {
    }
};

struct ObjNative : public Obj
{
    std::string name;
    int arity;
    NativeFn function;
    bool isAbstract = false;
    VMAccessModifier accessModifier = VMAccessModifier::PUBLIC;

    ObjNative(std::string n, int a, NativeFn f) : Obj(ObjType::OBJ_NATIVE), name(n), arity(a), function(f)
    {
    }
};

struct ObjFunction : public Obj
{
    std::string name;
    int arity;
    int maxArity;
    Chunk *chunk;
    bool isAbstract = false;
    std::unordered_map<std::string, VMValue> statics;
    VMAccessModifier accessModifier = VMAccessModifier::PUBLIC;
    std::string enclosingClassName = "";
    std::string filename = "";
    int upvalueCount = 0;
    int callCount = 0;
    void *jitAddr = nullptr;

    ObjFunction() : Obj(ObjType::OBJ_FUNCTION), arity(0), maxArity(0), upvalueCount(0), callCount(0), jitAddr(nullptr)
    {
    }
};

struct ObjUpvalue : public Obj
{
    VMValue *location;
    VMValue closed;
    ObjUpvalue *next;

    ObjUpvalue(VMValue *slot) : Obj(ObjType::OBJ_UPVALUE), location(slot), closed(nullptr), next(nullptr)
    {
    }
};

struct ObjClosure : public Obj
{
    ObjFunction *function;
    std::vector<ObjUpvalue *> upvalues;

    ObjClosure(ObjFunction *f) : Obj(ObjType::OBJ_CLOSURE), function(f)
    {
    }
};

class Chunk
{
  public:
    std::vector<uint8_t> code;
    std::vector<VMValue> constants;
    std::vector<int> lines;
    std::vector<uint32_t> coverage;

    Chunk() = default;

    void write(uint8_t byte, int line)
    {
        code.push_back(byte);
        lines.push_back(line);
    }

    void writeOp(OpCode op, int line)
    {
        write(static_cast<uint8_t>(op), line);
    }

    int addConstant(VMValue value)
    {
        // Simple deduplication to avoid exceeding 255 constants
        if (value.isString() || value.isNumber() || value.isBool())
        {
            for (size_t i = 0; i < constants.size(); i++)
            {
                if (constants[i] == value)
                {
                    return static_cast<int>(i);
                }
            }
        }
        constants.push_back(value);
        return static_cast<int>(constants.size() - 1);
    }

    void initCoverage()
    {
        coverage.assign(code.size(), 0);
    }

    void resetCoverage()
    {
        std::fill(coverage.begin(), coverage.end(), 0);
    }
};

#endif // TRYPILLIA_CHUNK_H
