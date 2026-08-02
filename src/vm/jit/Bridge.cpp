#include <cmath>
#include <cstring>
#include <vector>

#include "../core/VM.h"
#include "../memory/ObjectRuntime.h"

extern "C" void jit_deopt_helper(void *vm_ptr)
{
    VM *vm = static_cast<VM *>(vm_ptr);
    vm->jitDeoptNeeded = true;
}

extern "C" int jit_type_check_helper(void *vm_ptr, double value)
{
    VMValue val;
    memcpy((void *)&val, &value, sizeof(double));
    if (val.isNumber())
        return 1;
    VM *vm = static_cast<VM *>(vm_ptr);
    vm->jitDeoptNeeded = true;
    return 0;
}

extern "C" double jit_mod_helper(double a, double b)
{
    return std::fmod(a, b);
}

extern "C" double jit_build_list_helper(double *args, int count)
{
    std::vector<VMValue> elements(count);
    for (int i = 0; i < count; i++)
    {
        VMValue val;
        memcpy((void *)&val, &args[i], sizeof(double));
        elements[i] = val;
    }
    ObjList *list = new ObjList(elements);

    VMValue result(list);
    double dresult;
    memcpy((void *)&dresult, &result, sizeof(double));
    return dresult;
}

extern "C" double jit_build_map_helper(double *args, int count)
{
    ObjMap *map = new ObjMap();
    for (int i = 0; i < count; i++)
    {
        VMValue key, val;
        memcpy((void *)&key, &args[i * 2], sizeof(double));
        memcpy((void *)&val, &args[i * 2 + 1], sizeof(double));
        map->values[key] = val;
    }

    VMValue result(map);
    double dresult;
    memcpy((void *)&dresult, &result, sizeof(double));
    return dresult;
}

extern "C" double jit_index_get_helper(void *vm_ptr, double object_val, double index_val)
{
    uint64_t objRaw;
    memcpy((void *)&objRaw, &object_val, sizeof(uint64_t));
    uint64_t idxRaw;
    memcpy((void *)&idxRaw, &index_val, sizeof(uint64_t));

    bool isObj = (objRaw & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT);
    bool isNum = (idxRaw & QNAN) != QNAN;

    if (isObj && isNum)
    {
        Obj *obj = (Obj *)(uintptr_t)(objRaw & ~(SIGN_BIT | QNAN));
        if (obj->type == ObjType::OBJ_LIST)
        {
            ObjList *list = (ObjList *)obj;
            double indexDouble;
            memcpy((void *)&indexDouble, &index_val, sizeof(double));
            int i = static_cast<int>(indexDouble);
            if (i >= 0 && i < static_cast<int>(list->elements.size()))
            {
                VMValue result = list->elements[i];
                double ret;
                memcpy((void *)&ret, &result, sizeof(double));
                return ret;
            }
        }
    }
    double ret = 0;
    return ret;
}

extern "C" double jit_index_set_helper(void *vm_ptr, double object_val, double index_val, double value_val)
{
    uint64_t objRaw;
    memcpy((void *)&objRaw, &object_val, sizeof(uint64_t));
    uint64_t idxRaw;
    memcpy((void *)&idxRaw, &index_val, sizeof(uint64_t));

    bool isObj = (objRaw & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT);
    bool isNum = (idxRaw & QNAN) != QNAN;

    if (isObj && isNum)
    {
        Obj *obj = (Obj *)(uintptr_t)(objRaw & ~(SIGN_BIT | QNAN));
        if (obj->type == ObjType::OBJ_LIST)
        {
            ObjList *list = (ObjList *)obj;
            double indexDouble;
            memcpy((void *)&indexDouble, &index_val, sizeof(double));
            int i = static_cast<int>(indexDouble);
            VMValue val;
            memcpy((void *)&val, &value_val, sizeof(double));
            if (i >= 0 && i < static_cast<int>(list->elements.size()))
            {
                list->elements[i] = val;
            }
        }
    }
    double ret;
    memcpy((void *)&ret, &value_val, sizeof(double));
    return ret;
}

extern "C" double jit_property_get_helper(void *vm_ptr, double object_val, const char *name)
{
    VM *vm = static_cast<VM *>(vm_ptr);
    uint64_t objRaw;
    memcpy((void *)&objRaw, &object_val, sizeof(uint64_t));
    std::string propName(name);
    VMValue result(nullptr);

    bool isObj = (objRaw & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT);
    if (isObj)
    {
        Obj *obj = (Obj *)(uintptr_t)(objRaw & ~(SIGN_BIT | QNAN));
        if (obj->type == ObjType::OBJ_INSTANCE)
        {
            ObjInstance *instance = (ObjInstance *)obj;
            if (instance->fields.count(propName))
            {
                result = instance->fields[propName];
            }
            else if (instance->klass->methods.count(propName))
            {
                auto method = instance->klass->methods[propName];
                auto bound = new ObjBoundMethod(VMValue(instance), method);

                result = bound;
            }
        }
        else if (obj->type == ObjType::OBJ_CLASS)
        {
            ObjClass *klass = (ObjClass *)obj;
            if (klass->statics.count(propName))
                result = klass->statics[propName];
        }
    }
    double ret;
    memcpy((void *)&ret, &result, sizeof(double));
    return ret;
}

extern "C" double jit_property_set_helper(void *vm_ptr, double object_val, const char *name, double value_val)
{
    VM *vm = static_cast<VM *>(vm_ptr);
    uint64_t objRaw;
    memcpy((void *)&objRaw, &object_val, sizeof(uint64_t));
    std::string propName(name);

    bool isObj = (objRaw & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT);
    if (isObj)
    {
        Obj *obj = (Obj *)(uintptr_t)(objRaw & ~(SIGN_BIT | QNAN));
        VMValue val;
        memcpy((void *)&val, &value_val, sizeof(double));
        if (obj->type == ObjType::OBJ_INSTANCE)
        {
            ObjInstance *instance = (ObjInstance *)obj;
            instance->fields[propName] = val;
        }
        else if (obj->type == ObjType::OBJ_CLASS)
        {
            ObjClass *klass = (ObjClass *)obj;
            klass->statics[propName] = val;
        }
    }
    double ret;
    memcpy((void *)&ret, &value_val, sizeof(double));
    return ret;
}

extern "C" double jit_iter_has_next_helper(double index_val, double iterable_val)
{
    uint64_t idxRaw;
    memcpy((void *)&idxRaw, &index_val, sizeof(uint64_t));
    uint64_t iterRaw;
    memcpy((void *)&iterRaw, &iterable_val, sizeof(uint64_t));

    bool result = false;
    bool isNum = (idxRaw & QNAN) != QNAN;
    bool isObj = (iterRaw & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT);
    if (isNum && isObj)
    {
        double indexDouble;
        memcpy((void *)&indexDouble, &index_val, sizeof(double));
        int index = static_cast<int>(indexDouble);
        Obj *obj = (Obj *)(uintptr_t)(iterRaw & ~(SIGN_BIT | QNAN));
        if (obj->type == ObjType::OBJ_LIST)
        {
            ObjList *list = (ObjList *)obj;
            result = index < static_cast<int>(list->elements.size());
        }
        else if (obj->type == ObjType::OBJ_STRING)
        {
            ObjString *strObj = (ObjString *)obj;
            result = index < utf8_length(strObj->flatten());
        }
    }
    VMValue ret(result);
    double dret;
    memcpy((void *)&dret, &ret, sizeof(double));
    return dret;
}

extern "C" double jit_call_helper(void *vm_ptr, double callee_val, double *args, int argCount)
{
    VM *vm = static_cast<VM *>(vm_ptr);
    VMValue result = nullptr;
    ObjClosure *savedJitClosure = vm->jitClosure;

    uint64_t calleeRaw;
    memcpy((void *)&calleeRaw, &callee_val, sizeof(uint64_t));
    bool isObj = (calleeRaw & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT);
    if (isObj)
    {
        Obj *obj = (Obj *)(uintptr_t)(calleeRaw & ~(SIGN_BIT | QNAN));
        if (obj->type == ObjType::OBJ_NATIVE)
        {
            std::vector<VMValue> vmArgs(argCount);
            for (int i = 0; i < argCount; i++)
            {
                memcpy((void *)&vmArgs[i], &args[i + 1], sizeof(double));
            }
            ObjNative *native = (ObjNative *)obj;
            result = native->function(argCount, vmArgs.data());
        }
        else if (obj->type == ObjType::OBJ_CLOSURE)
        {
            ObjClosure *closure = (ObjClosure *)obj;
            ObjFunction *function = closure->function;

            if (vm->compiledFuncs.count(function))
            {
                JitFunc nativeJitFunc = vm->compiledFuncs[function];
                std::vector<double> freshArgs(argCount + 1 > 256 ? argCount + 1 : 256);
                std::memcpy((void *)freshArgs.data(), args, (argCount + 1) * sizeof(double));
                vm->jitClosure = closure;
                result = nativeJitFunc(vm, freshArgs.data(), argCount, argCount > 0 ? freshArgs[1] : 0.0);
                vm->jitClosure = savedJitClosure;
            }
            else
            {
                std::vector<VMValue> vmArgs(argCount);
                for (int i = 0; i < argCount; i++)
                {
                    memcpy((void *)&vmArgs[i], &args[i + 1], sizeof(double));
                }
                VMValue callee;
                memcpy((void *)&callee, &callee_val, sizeof(double));
                vm->jitClosure = nullptr;
                result = vm->callClosure(callee, argCount, vmArgs.data());
                vm->jitClosure = savedJitClosure;
            }
        }
        else if (obj->type == ObjType::OBJ_BOUND_METHOD)
        {
            ObjBoundMethod *bound = (ObjBoundMethod *)obj;
            VMValue method = bound->method;
            VMValue receiver = bound->receiver;
            if (method.isClosure())
            {
                std::vector<VMValue> vmArgs(argCount + 1);
                vmArgs[0] = receiver;
                for (int i = 0; i < argCount; i++)
                {
                    memcpy((void *)&vmArgs[i + 1], &args[i + 1], sizeof(double));
                }
                vm->jitClosure = nullptr;
                result = vm->callClosure(method, argCount + 1, vmArgs.data());
                vm->jitClosure = savedJitClosure;
            }
            else if (method.isNative())
            {
                ObjNative *native = method.asNative();
                std::vector<VMValue> vmArgs(argCount + 1);
                vmArgs[0] = receiver;
                for (int i = 0; i < argCount; i++)
                {
                    memcpy((void *)&vmArgs[i + 1], &args[i + 1], sizeof(double));
                }
                result = native->function(argCount + 1, vmArgs.data());
            }
        }
        else if (obj->type == ObjType::OBJ_CLASS)
        {
            VMValue classVal;
            memcpy((void *)&classVal, &callee_val, sizeof(double));
            std::vector<VMValue> vmArgs(argCount);
            for (int i = 0; i < argCount; i++)
            {
                memcpy((void *)&vmArgs[i], &args[i + 1], sizeof(double));
            }
            vm->jitClosure = nullptr;
            result = vm->instantiateClass(classVal, argCount, vmArgs.data());
            vm->jitClosure = savedJitClosure;
        }
    }

    double ret;
    memcpy((void *)&ret, &result, sizeof(double));
    return ret;
}

extern "C" void *jit_resolve_global_address(void *vm_ptr, const char *name, VMValue **cell)
{
    VM *vm = static_cast<VM *>(vm_ptr);
    // std::map::operator[] or find is stable for pointers to values.
    // We use find to avoid inserting nulls if not found.
    auto it = vm->globals.find(name);
    if (it != vm->globals.end())
    {
        *cell = &(it->second);
        return (void *)&(it->second);
    }
    // Return address of a dummy null if not found
    static VMValue dummyNull = nullptr;
    return (void *)&dummyNull;
}

extern "C" double jit_get_global_helper(void *vm_ptr, const char *name)
{
    VM *vm = static_cast<VM *>(vm_ptr);
    auto it = vm->globals.find(name);
    VMValue result = nullptr;
    if (it != vm->globals.end())
    {
        result = it->second;
    }
    double ret;
    memcpy((void *)&ret, &result, sizeof(double));
    return ret;
}

extern "C" void jit_set_global_helper(void *vm_ptr, const char *name, double val_d)
{
    VM *vm = static_cast<VM *>(vm_ptr);
    VMValue val;
    memcpy((void *)&val, &val_d, sizeof(double));
    vm->globals[name] = val;
}

extern "C" double jit_create_class_helper(void *vm, const char *name)
{
    auto klass = new ObjClass(name);

    VMValue result(klass);
    double ret;
    memcpy((void *)&ret, &result, sizeof(double));
    return ret;
}

extern "C" double jit_create_abstract_class_helper(void *vm, const char *name)
{
    auto klass = new ObjClass(name);
    klass->isAbstract = true;

    VMValue result(klass);
    double ret;
    memcpy((void *)&ret, &result, sizeof(double));
    return ret;
}

extern "C" double jit_bind_method_helper(double class_val, double method_val, const char *name, int isAbstract)
{
    uint64_t klassRaw, methodRaw;
    memcpy((void *)&klassRaw, &class_val, sizeof(uint64_t));
    memcpy((void *)&methodRaw, &method_val, sizeof(uint64_t));
    ObjClass *klass = (ObjClass *)(klassRaw & ~(QNAN | SIGN_BIT));
    VMValue method;
    memcpy((void *)&method, &methodRaw, sizeof(VMValue));
    if (isAbstract)
    {
        if (method.isClosure())
            method.asClosure()->function->isAbstract = true;
        else if (method.isNative())
            method.asNative()->isAbstract = true;
    }
    klass->methods[name] = method;
    // String owned by JIT compiler
    return class_val;
}

extern "C" double jit_bind_static_method_helper(double class_val, double method_val, const char *name)
{
    uint64_t klassRaw, methodRaw;
    memcpy((void *)&klassRaw, &class_val, sizeof(uint64_t));
    memcpy((void *)&methodRaw, &method_val, sizeof(uint64_t));
    ObjClass *klass = (ObjClass *)(klassRaw & ~(QNAN | SIGN_BIT));
    VMValue method;
    memcpy((void *)&method, &methodRaw, sizeof(VMValue));
    klass->statics[name] = method;
    // String owned by JIT compiler
    return class_val;
}

extern "C" void jit_inherit_helper(double subclass_val, double superclass_val)
{
    uint64_t subRaw, supRaw;
    memcpy((void *)&subRaw, &subclass_val, sizeof(uint64_t));
    memcpy((void *)&supRaw, &superclass_val, sizeof(uint64_t));
    if ((subRaw & (QNAN | SIGN_BIT)) != (QNAN | SIGN_BIT))
        return;
    if ((supRaw & (QNAN | SIGN_BIT)) != (QNAN | SIGN_BIT))
        return;
    ObjClass *subclass = (ObjClass *)(subRaw & ~(QNAN | SIGN_BIT));
    ObjClass *superclass = (ObjClass *)(supRaw & ~(QNAN | SIGN_BIT));
    if (subclass->type != ObjType::OBJ_CLASS)
        return;
    if (superclass->type != ObjType::OBJ_CLASS)
        return;
    subclass->superclass = superclass;
    for (auto const &[name, mod] : superclass->fieldModifiers)
    {
        subclass->fieldModifiers[name] = mod;
    }
    for (auto const &[name, method] : superclass->methods)
    {
        subclass->methods[name] = method;
    }
}

extern "C" void jit_mixin_helper(double target_val, double mixin_val)
{
    uint64_t targetRaw, mixinRaw;
    memcpy((void *)&targetRaw, &target_val, sizeof(uint64_t));
    memcpy((void *)&mixinRaw, &mixin_val, sizeof(uint64_t));
    if ((targetRaw & (QNAN | SIGN_BIT)) != (QNAN | SIGN_BIT))
        return;
    if ((mixinRaw & (QNAN | SIGN_BIT)) != (QNAN | SIGN_BIT))
        return;
    ObjClass *targetClass = (ObjClass *)(targetRaw & ~(QNAN | SIGN_BIT));
    ObjClass *mixinClass = (ObjClass *)(mixinRaw & ~(QNAN | SIGN_BIT));
    if (targetClass->type != ObjType::OBJ_CLASS)
        return;
    if (mixinClass->type != ObjType::OBJ_CLASS)
        return;
    for (auto const &[name, method] : mixinClass->methods)
    {
        targetClass->methods[name] = method;
    }
}

extern "C" double jit_get_super_helper(double receiver_val, double superclass_val, const char *name)
{
    uint64_t recvRaw, supRaw;
    memcpy((void *)&recvRaw, &receiver_val, sizeof(uint64_t));
    memcpy((void *)&supRaw, &superclass_val, sizeof(uint64_t));
    ObjClass *superclass = nullptr;
    if ((supRaw & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))
    {
        superclass = (ObjClass *)(supRaw & ~(QNAN | SIGN_BIT));
    }
    ObjInstance *receiver = nullptr;
    if ((recvRaw & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))
    {
        receiver = (ObjInstance *)(recvRaw & ~(QNAN | SIGN_BIT));
    }
    std::string methodName(name);
    // String owned by JIT compiler
    if (superclass && receiver && superclass->methods.count(methodName))
    {
        auto method = superclass->methods[methodName];
        auto bound = new ObjBoundMethod(receiver, method);

        VMValue result(bound);
        double ret;
        memcpy((void *)&ret, &result, sizeof(double));
        return ret;
    }
    double ret = 0;
    return ret;
}

extern "C" void jit_field_modifier_helper(double class_val, const char *name, int modifier)
{
    uint64_t klassRaw;
    memcpy((void *)&klassRaw, &class_val, sizeof(uint64_t));
    if ((klassRaw & (QNAN | SIGN_BIT)) != (QNAN | SIGN_BIT))
        return;
    ObjClass *klass = (ObjClass *)(klassRaw & ~(QNAN | SIGN_BIT));
    if (klass->type != ObjType::OBJ_CLASS)
        return;
    klass->fieldModifiers[name] = static_cast<VMAccessModifier>(modifier);
}

extern "C" double jit_create_closure_helper(void *vm_ptr, double func_val, double *upvalue_data, int upvalue_count)
{
    VM *vm = static_cast<VM *>(vm_ptr);
    uint64_t funcRaw;
    memcpy((void *)&funcRaw, &func_val, sizeof(uint64_t));
    ObjFunction *function;
    if ((funcRaw & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))
        function = (ObjFunction *)(funcRaw & ~(QNAN | SIGN_BIT));
    else
        function = nullptr;
    auto closure = new ObjClosure(function);

    for (int i = 0; i < upvalue_count; i++)
    {
        uint64_t metaRaw;
        memcpy((void *)&metaRaw, &upvalue_data[i * 2], sizeof(uint64_t));
        int packed = static_cast<int>(metaRaw);
        bool isLocal = (packed >> 8) & 1;
        int index = packed & 0xFF;

        if (isLocal)
        {
            uint64_t addrRaw;
            memcpy((void *)&addrRaw, &upvalue_data[i * 2 + 1], sizeof(uint64_t));
            closure->upvalues.push_back(vm->captureUpvalue((VMValue *)addrRaw));
        }
        else
        {
            if (vm->jitClosure && index < (int)vm->jitClosure->upvalues.size())
            {
                closure->upvalues.push_back(vm->jitClosure->upvalues[index]);
            }
            else
            {
                closure->upvalues.push_back(nullptr);
            }
        }
    }

    VMValue result(closure);
    double ret;
    memcpy((void *)&ret, &result, sizeof(double));
    return ret;
}

extern "C" double jit_get_upvalue_helper(void *vm_ptr, int slot)
{
    VM *vm = static_cast<VM *>(vm_ptr);
    if (vm->jitClosure && slot < (int)vm->jitClosure->upvalues.size() && vm->jitClosure->upvalues[slot])
    {
        VMValue result = *vm->jitClosure->upvalues[slot]->location;
        double ret;
        memcpy((void *)&ret, &result, sizeof(double));
        return ret;
    }
    double ret = 0;
    return ret;
}

extern "C" void jit_set_upvalue_helper(void *vm_ptr, int slot, double val)
{
    VM *vm = static_cast<VM *>(vm_ptr);
    if (vm->jitClosure && slot < (int)vm->jitClosure->upvalues.size() && vm->jitClosure->upvalues[slot])
    {
        VMValue value;
        memcpy((void *)&value, &val, sizeof(VMValue));
        *vm->jitClosure->upvalues[slot]->location = value;
    }
}

extern "C" void jit_close_upvalue_helper(void *vm_ptr, double *addr)
{
    VM *vm = static_cast<VM *>(vm_ptr);
    vm->closeUpvalues((VMValue *)addr);
}
