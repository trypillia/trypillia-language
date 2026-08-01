#include "GC.h"

#include <iostream>
#include <vector>

#include "../compiler/Chunk.h"
#include "../core/VM.h"

std::vector<Obj *> grayStack;

void GC::markValue(VMValue &value)
{
    if (value.isObj())
    {
        markObj(value.asObj());
    }
}

void GC::markObj(Obj *obj)
{
    if (obj == nullptr)
        return;
    if (obj->isMarked)
        return;
    obj->isMarked = true;
    grayStack.push_back(obj);
}

void processGrayStack()
{
    while (!grayStack.empty())
    {
        Obj *obj = grayStack.back();
        grayStack.pop_back();

        switch (obj->type)
        {
        case ObjType::OBJ_STRING:
        case ObjType::OBJ_NATIVE:
            break;
        case ObjType::OBJ_FUNCTION: {
            auto func = static_cast<ObjFunction *>(obj);
            if (func->chunk)
            {
                for (auto &v : func->chunk->constants)
                {
                    GC::markValue(v);
                }
            }
            break;
        }
        case ObjType::OBJ_CLOSURE: {
            auto closure = static_cast<ObjClosure *>(obj);
            GC::markObj(closure->function);
            for (auto upvalue : closure->upvalues)
            {
                GC::markObj(upvalue);
            }
            break;
        }
        case ObjType::OBJ_LIST: {
            auto list = static_cast<ObjList *>(obj);
            for (auto &v : list->elements)
            {
                GC::markValue(v);
            }
            break;
        }
        case ObjType::OBJ_MAP: {
            auto map = static_cast<ObjMap *>(obj);
            for (auto &pair : map->values)
            {
                VMValue key = pair.first;
                VMValue val = pair.second;
                GC::markValue(key);
                GC::markValue(val);
            }
            break;
        }
        case ObjType::OBJ_CLASS: {
            auto klass = static_cast<ObjClass *>(obj);
            GC::markObj(klass->superclass);
            for (auto &pair : klass->methods)
            {
                VMValue v = pair.second;
                GC::markValue(v);
            }
            for (auto &pair : klass->statics)
            {
                VMValue v = pair.second;
                GC::markValue(v);
            }
            break;
        }
        case ObjType::OBJ_INSTANCE: {
            auto instance = static_cast<ObjInstance *>(obj);
            GC::markObj(instance->klass);
            for (auto &pair : instance->fields)
            {
                VMValue v = pair.second;
                GC::markValue(v);
            }
            break;
        }
        case ObjType::OBJ_BOUND_METHOD: {
            auto bound = static_cast<ObjBoundMethod *>(obj);
            GC::markValue(bound->receiver);
            GC::markValue(bound->method);
            break;
        }
        case ObjType::OBJ_UPVALUE: {
            auto upvalue = static_cast<ObjUpvalue *>(obj);
            GC::markValue(upvalue->closed);
            break;
        }
        case ObjType::OBJ_WEAK_REF: {
            break;
        }
        case ObjType::OBJ_PROMISE: {
            auto promise = static_cast<ObjPromise *>(obj);
            GC::markValue(promise->value);
            if (promise->onFulfilled)
                GC::markObj(promise->onFulfilled);
            if (promise->onRejected)
                GC::markObj(promise->onRejected);
            for (auto &h : promise->thenHandlers)
                GC::markValue(h);
            break;
        }
        }
    }
}

void GC::collect(VM *vm)
{
    // 1. Mark roots
    for (VMValue *slot = vm->stack; slot < vm->stackTop; slot++)
        markValue(*slot);
    for (auto &pair : vm->globals)
    {
        VMValue v = pair.second;
        markValue(v);
    }
    for (auto &frame : vm->frames)
    {
        if (frame.closure)
            markObj(frame.closure);
    }
    ObjUpvalue *upvalue = vm->openUpvalues;
    while (upvalue != nullptr)
    {
        markObj(upvalue);
        upvalue = upvalue->next;
    }

    processGrayStack();

    // 1.5 Handle weak refs
    Obj *obj = vm->objects;
    while (obj != nullptr)
    {
        if (obj->type == ObjType::OBJ_WEAK_REF)
        {
            auto weak = static_cast<ObjWeakRef *>(obj);
            if (weak->weakRef && !weak->weakRef->isMarked)
            {
                weak->weakRef = nullptr;
            }
        }
        obj = obj->nextObj;
    }

    // 2. Sweep
    Obj **object = &vm->objects;
    while (*object != nullptr)
    {
        if (!(*object)->isMarked)
        {
            Obj *unreached = *object;
            *object = unreached->nextObj;
            delete unreached;
        }
        else
        {
            (*object)->isMarked = false;
            object = &(*object)->nextObj;
        }
    }

    // reset threshold
    vm->bytesAllocated = 0;
}
