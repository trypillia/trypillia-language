#include <string>

#include "../memory/ObjectRuntime.h"
#include "VM.h"

bool VM::executeIndexGet()
{
    VMValue index = pop();
    VMValue listVal = pop();
    if (listVal.isList())
    {
        auto list = listVal.asList();
        if (index.isNumber())
        {
            int i = static_cast<int>(index.asNumber());
            if (i >= 0 && i < static_cast<int>(list->elements.size()))
            {
                push(list->elements[i]);
            }
            else
            {
                runtimeError(std::string("Index out of bounds."));
                return false;
            }
        }
        else
        {
            runtimeError(std::string("List index must be a number."));
            return false;
        }
    }
    else if (listVal.isString())
    {
        auto str = listVal.asString()->flatten();
        if (index.isNumber())
        {
            int i = static_cast<int>(index.asNumber());
            int len = utf8_length(str);
            if (i >= 0 && i < len)
            {
                push(utf8_char_at(str, i));
            }
            else
            {
                runtimeError(std::string("String index out of bounds."));
                return false;
            }
        }
        else
        {
            runtimeError(std::string("String index must be a number."));
            return false;
        }
    }
    else if (listVal.isMap())
    {
        auto map = listVal.asMap();
        if (map->values.count(index))
        {
            push(map->values[index]);
        }
        else
        {
            push(nullptr); // Return nil for missing keys
        }
    }
    else
    {
        runtimeError(std::string("Can only index into lists, maps, or strings."));
        return false;
    }
    return true;
}

bool VM::executePropertyGet(const std::string &name)
{
    VMValue instanceVal = peek(0);
    std::string callerClass = frames.back().closure ? frames.back().closure->function->enclosingClassName : "";

    if (instanceVal.isInstance())
    {
        auto instance = instanceVal.asInstance();
        if (instance->fields.count(name))
        {
            VMAccessModifier mod = VMAccessModifier::PUBLIC;
            if (instance->klass->fieldModifiers.count(name))
                mod = instance->klass->fieldModifiers[name];
            if (!checkAccess(mod, instance->klass, callerClass))
            {
                runtimeError(std::string("Access error: Cannot access '") + name + "'.");
                return false;
            }
            pop();
            push(instance->fields[name]);
        }
        else if (instance->klass->methods.count(name))
        {
            auto method = instance->klass->methods[name];
            VMAccessModifier mod = VMAccessModifier::PUBLIC;
            if (method.isClosure())
            {
                mod = method.asClosure()->function->accessModifier;
            }
            if (!checkAccess(mod, instance->klass, callerClass))
            {
                runtimeError(std::string("Access error: Cannot access method '") + name + "'.");
                return false;
            }
            pop(); // instance
            push(new ObjBoundMethod(instance, method));
        }
        else
        {
            runtimeError(std::string("Undefined property '") + name + "'.");
            return false;
        }
    }
    else if (instanceVal.isClass())
    {
        auto klass = instanceVal.asClass();
        if (klass->statics.count(name))
        {
            auto methodVal = klass->statics[name];
            if (methodVal.isClosure())
            {
                auto func = methodVal.asClosure()->function;
                if (!checkAccess(func->accessModifier, klass, callerClass))
                {
                    runtimeError(std::string("Access error: Cannot access static method '") + name + "'.");
                    return false;
                }
            }
            // statics (fields) access modifier check can be added here if static
            // fields have modifiers.
            pop();
            push(klass->statics[name]);
        }
        else
        {
            runtimeError(std::string("Undefined static property '") + name + "'.");
            return false;
        }
    }
    else if (instanceVal.isString())
    {
        if (globals.count("String"))
        {
            auto klass = globals["String"].asClass();
            if (klass->statics.count(name))
            {
                pop(); // pop string
                push(new ObjBoundMethod(instanceVal, klass->statics[name]));
                return true;
            }
        }
        runtimeError(std::string("Undefined property '") + name + "' on String.");
        return false;
    }
    else if (instanceVal.isList())
    {
        if (globals.count("List"))
        {
            auto klass = globals["List"].asClass();
            if (klass->statics.count(name))
            {
                pop(); // pop list
                push(new ObjBoundMethod(instanceVal, klass->statics[name]));
                return true;
            }
        }
        runtimeError(std::string("Undefined property '") + name + "' on List.");
        return false;
    }
    else if (instanceVal.isMap())
    {
        if (globals.count("Map"))
        {
            auto klass = globals["Map"].asClass();
            if (klass->statics.count(name))
            {
                pop(); // pop map
                push(new ObjBoundMethod(instanceVal, klass->statics[name]));
                return true;
            }
        }
        runtimeError(std::string("Undefined property '") + name + "' on Map.");
        return false;
    }
    else if (instanceVal.isPromise())
    {
        if (globals.count("__promise_then") && name == "then")
        {
            pop();
            push(new ObjBoundMethod(instanceVal, globals["__promise_then"]));
            return true;
        }
        runtimeError(std::string("Undefined property '") + name + "' on Promise.");
        return false;
    }
    else
    {
        runtimeError(std::string("Only instances and classes have properties."));
        return false;
    }
    return true;
}

bool VM::executeCall(uint8_t argCount)
{
    VMValue callee = peek(argCount);
    if (callee.isClosure())
    {
        auto closure = callee.asClosure();
        auto function = closure->function;
        if (function->arity != -1 && (argCount < function->arity || argCount > function->maxArity))
        {
            std::string expected = function->arity == function->maxArity
                                       ? std::to_string(function->arity)
                                       : std::to_string(function->arity) + "-" + std::to_string(function->maxArity);
            runtimeError(std::string("Expected ") + expected + " arguments but got " + std::to_string(argCount) + ".");
            return false;
        }
        while (function->maxArity != -1 && argCount < function->maxArity)
        {
            push(nullptr);
            argCount++;
        }
        if (frames.size() == 256)
        {
            runtimeError(std::string("Stack overflow."));
            return false;
        }

        JitFunc nativeJitFunc = nullptr;
        auto funcPtr = function;
        if (compiledFuncs.count(funcPtr))
        {
            nativeJitFunc = compiledFuncs[funcPtr];
        }
        else if (funcPtr->callCount >= 50)
        {
            nativeJitFunc = jit.compileMathFunction(function);
            if (nativeJitFunc)
            {
                compiledFuncs[funcPtr] = nativeJitFunc;
                funcPtr->jitAddr = (void *)nativeJitFunc;
                std::cerr << "JIT compiled " << function->name << " at call #" << funcPtr->callCount << std::endl;
            }
            else
            {
                compiledFuncs[funcPtr] = nullptr;
                std::cerr << "JIT ABORTED for " << function->name << " at call #" << funcPtr->callCount << std::endl;
            }
        }
        else
        {
            funcPtr->callCount++;
        }

        if (nativeJitFunc)
        {
            std::vector<double> jitArgs(2048, 0.0);
            for (int i = 0; i <= argCount; ++i)
            {
                VMValue arg = peek(argCount - i);
                double raw;
                memcpy((void *)&raw, &arg, sizeof(double));
                jitArgs[i] = raw;
            }

            // Guard: JIT assumes all values are numbers; fall back to
            // interpreter if any argument is not a number
            bool allNumbers = true;
            for (int i = 1; i <= argCount; i++)
            {
                VMValue val;
                memcpy((void *)&val, &jitArgs[i], sizeof(double));
                if (!val.isNumber())
                {
                    allNumbers = false;
                    break;
                }
            }

            if (allNumbers)
            {
                jitClosure = closure;
                jitDeoptNeeded = false;
                double result = nativeJitFunc(this, jitArgs.data(), argCount, argCount > 0 ? jitArgs[1] : 0.0);
                jitClosure = nullptr;
                if (jitDeoptNeeded)
                {
                    jitDeoptNeeded = false;
                    // Fall through to interpreter
                }
                else
                {
                    stackTop -= argCount + 1;
                    push(result);
                    return true; // Skip standard frame push!
                }
            }
            // Fall through to interpreter if not all numbers or deopt triggered
        }
        CallFrame newFrame;
        newFrame.closure = closure;
        newFrame.ip = function->chunk->code.data();
        newFrame.stackStart = static_cast<int>((stackTop - stack) - argCount - 1);
        frames.push_back(newFrame);
    }
    else if (callee.isNative())
    {
        auto native = callee.asNative();
        if (native->arity != -1 && argCount != native->arity)
        {
            runtimeError(std::string("Expected ") + std::to_string(native->arity) + " arguments but got " +
                         std::to_string(argCount) + ".");
            return false;
        }
        jmp_buf prevAssertJmpBuf;
        bool prevAssertJumpEnabled = assertJumpEnabled;
        if (prevAssertJumpEnabled)
        {
            memcpy(prevAssertJmpBuf, assertJmpBuf, sizeof(jmp_buf));
        }

        if (setjmp(assertJmpBuf) == 0)
        {
            assertJumpEnabled = true;
            VMValue result = native->function(argCount, stack + (stackTop - stack) - argCount);

            assertJumpEnabled = prevAssertJumpEnabled;
            if (prevAssertJumpEnabled)
            {
                memcpy(assertJmpBuf, prevAssertJmpBuf, sizeof(jmp_buf));
            }

            stackTop -= argCount + 1;
            push(result);
        }
        else
        {
            assertJumpEnabled = prevAssertJumpEnabled;
            if (prevAssertJumpEnabled)
            {
                memcpy(assertJmpBuf, prevAssertJmpBuf, sizeof(jmp_buf));
            }
            return false;
        }
    }
    else if (callee.isClass())
    {
        auto klass = callee.asClass();
        if (klass->isAbstract)
        {
            runtimeError(std::string("Cannot instantiate abstract class '") + klass->name + "'.");
            return false;
        }
        for (auto const &[name, method] : klass->methods)
        {
            if (isMethodAbstract(method))
            {
                runtimeError(std::string("Cannot instantiate class '") + klass->name + "' because abstract method '" +
                             name + "' is not implemented.");
                return false;
            }
        }
        auto instance = new ObjInstance(klass);

        stack[(stackTop - stack) - argCount - 1] = instance;

        if (klass->methods.count("init"))
        {
            auto initMethod = klass->methods["init"];
            int minArity = getMethodMinArity(initMethod);
            int maxArity = getMethodMaxArity(initMethod);
            if (minArity != -1 && (argCount < minArity || argCount > maxArity))
            {
                std::string expected = minArity == maxArity ? std::to_string(minArity)
                                                            : std::to_string(minArity) + "-" + std::to_string(maxArity);
                runtimeError(std::string("Expected ") + expected + " arguments but got " + std::to_string(argCount) +
                             ".");
                return false;
            }
            while (maxArity != -1 && argCount < maxArity)
            {
                push(nullptr);
                argCount++;
            }

            if (initMethod.isClosure())
            {
                auto closure = initMethod.asClosure();
                auto func = closure->function;
                CallFrame newFrame;
                newFrame.closure = closure;
                newFrame.ip = func->chunk->code.data();
                newFrame.stackStart = static_cast<int>((stackTop - stack) - argCount - 1);
                frames.push_back(newFrame);
            }
            else
            {
                auto native = initMethod.asNative();
                native->function(argCount, stack + (stackTop - stack) - argCount);
                stackTop -= argCount; // leave the instance on stack
            }
        }
        else if (argCount != 0)
        {
            runtimeError(std::string("Expected 0 arguments but got ") + std::to_string(argCount) + ".");
            return false;
        }
    }
    else if (callee.isBoundMethod())
    {
        auto bound = callee.asBoundMethod();
        auto function = bound->method;
        if (isMethodAbstract(function))
        {
            runtimeError(std::string("Cannot call abstract method '") + getMethodName(function) + "'.");
            return false;
        }
        int minArity = getMethodMinArity(function);
        int maxArity = getMethodMaxArity(function);
        if (function.isNative())
        {
            if (!bound->receiver.isInstance())
            {
                if (minArity != -1)
                    minArity -= 1;
                if (maxArity != -1)
                    maxArity -= 1;
            }
        }
        if (minArity != -1 && (argCount < minArity || argCount > maxArity))
        {
            std::string expected = minArity == maxArity ? std::to_string(minArity)
                                                        : std::to_string(minArity) + "-" + std::to_string(maxArity);
            runtimeError(std::string("Expected ") + expected + " arguments but got " + std::to_string(argCount) + ".");
            return false;
        }
        while (maxArity != -1 && argCount < maxArity)
        {
            push(nullptr);
            argCount++;
        }
        if (frames.size() == 256)
        {
            runtimeError(std::string("Stack overflow."));
            return false;
        }
        stack[(stackTop - stack) - argCount - 1] = bound->receiver;

        if (function.isClosure())
        {
            auto closure = function.asClosure();
            auto func = closure->function;
            CallFrame newFrame;
            newFrame.closure = closure;
            newFrame.ip = func->chunk->code.data();
            newFrame.stackStart = static_cast<int>((stackTop - stack) - argCount - 1);
            frames.push_back(newFrame);
        }
        else
        {
            auto native = function.asNative();
            int passedArgCount = argCount;
            VMValue *argsPtr;
            if (!bound->receiver.isInstance())
            {
                passedArgCount += 1;
                argsPtr = stack + (stackTop - stack) - argCount - 1; // Primitive methods expect receiver at args[0]
            }
            else
            {
                argsPtr = stack + (stackTop - stack) - argCount; // Instance methods expect receiver at args[-1]
            }
            VMValue result = native->function(passedArgCount, argsPtr);
            stackTop -= argCount + 1;
            push(result);
        }
    }
    else
    {
        runtimeError(std::string("Can only call functions and classes."));
        return false;
    }
    return true;
}
VMValue VM::callClosure(VMValue closureVal, int argCount, VMValue *args)
{
    if (!closureVal.isClosure())
        return nullptr;
    auto closure = closureVal.asClosure();

    int initialFrameCount = static_cast<int>(frames.size());

    VMValue *savedStackTop = stackTop;
    push(closureVal);
    for (int i = 0; i < argCount; i++)
    {
        push(args[i]);
    }

    CallFrame newFrame;
    newFrame.closure = closure;
    newFrame.ip = closure->function->chunk->code.data();
    newFrame.stackStart = static_cast<int>((stackTop - stack) - argCount - 1);
    frames.push_back(newFrame);

    InterpretResult result = run(initialFrameCount);
    if (result == InterpretResult::INTERPRET_RUNTIME_ERROR)
    {
        while (frames.size() > static_cast<size_t>(initialFrameCount))
        {
            frames.pop_back();
        }
        stackTop = savedStackTop;
        return nullptr;
    }

    return pop();
}

VMValue VM::instantiateClass(VMValue classVal, int argCount, VMValue *args)
{
    if (!classVal.isClass())
        return nullptr;
    auto klass = classVal.asClass();
    if (klass->isAbstract)
        return nullptr;
    for (auto const &[name, method] : klass->methods)
    {
        if (isMethodAbstract(method))
            return nullptr;
    }
    auto instance = new ObjInstance(klass);

    if (klass->methods.count("init"))
    {
        auto initMethod = klass->methods["init"];
        int minArity = getMethodMinArity(initMethod);
        int maxArity = getMethodMaxArity(initMethod);
        if (minArity != -1 && (argCount < minArity || argCount > maxArity))
            return nullptr;
        while (maxArity != -1 && argCount < maxArity)
        {
            // Pad with nil
            VMValue nilVal = nullptr;
            // We can't easily pad here, rely on caller
            return nullptr;
        }

        if (initMethod.isClosure())
        {
            push(instance);
            for (int i = 0; i < argCount; i++)
                push(args[i]);
            auto closure = initMethod.asClosure();
            CallFrame newFrame;
            newFrame.closure = closure;
            newFrame.ip = closure->function->chunk->code.data();
            newFrame.stackStart = static_cast<int>((stackTop - stack) - argCount - 1);
            frames.push_back(newFrame);
            int initialDepth = static_cast<int>(frames.size() - 1);
            InterpretResult res = run(initialDepth);
            frames.pop_back();
            if (res == InterpretResult::INTERPRET_RUNTIME_ERROR)
                return nullptr;
            return instance;
        }
    }
    return instance;
}
