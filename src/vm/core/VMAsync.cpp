#include "VM.h"
#include "../memory/ObjectRuntime.h"
#include <cmath>
#include <iostream>
#include <map>

void VM::drainMicrotasks() {
    while (!microtaskQueue.empty() || !promiseMicrotasks.empty()) {
        while (!promiseMicrotasks.empty()) {
            auto pm = promiseMicrotasks.front();
            promiseMicrotasks.erase(promiseMicrotasks.begin());

            if (pm.onFulfilled.isClosure()) {
                auto closure = pm.onFulfilled.asClosure();
                auto fn = closure->function;
                int initialDepth = static_cast<int>(frames.size());

                push(pm.onFulfilled);
                push(pm.inputValue);
                CallFrame frame;
                frame.closure = closure;
                frame.ip = fn->chunk->code.data();
                frame.stackStart = static_cast<int>((stackTop - stack) - 2);
                frames.push_back(frame);

                InterpretResult res = run(initialDepth);

                if (res == InterpretResult::INTERPRET_OK && pm.targetPromise) {
                    VMValue result = pop();
                    pm.targetPromise->value = result;
                    pm.targetPromise->resolved = true;
                    for (size_t i = 0; i < pm.targetPromise->thenHandlers.size(); i += 3) {
                        PromiseMicrotask nextPm;
                        nextPm.targetPromise = pm.targetPromise->thenHandlers[i + 2].isPromise()
                                                   ? pm.targetPromise->thenHandlers[i + 2].asPromise()
                                                   : nullptr;
                        nextPm.onFulfilled = pm.targetPromise->thenHandlers[i];
                        nextPm.onRejected = pm.targetPromise->thenHandlers[i + 1];
                        nextPm.inputValue = result;
                        promiseMicrotasks.push_back(nextPm);
                    }
                    pm.targetPromise->thenHandlers.clear();
                } else {
                    pop();
                }
            }
        }

        if (!microtaskQueue.empty()) {
            VMValue task = microtaskQueue.front();
            microtaskQueue.erase(microtaskQueue.begin());

            if (task.isClosure()) {
                auto closure = task.asClosure();
                int initialDepth = static_cast<int>(frames.size());

                push(task);
                CallFrame frame;
                frame.closure = closure;
                frame.ip = closure->function->chunk->code.data();
                frame.stackStart = static_cast<int>((stackTop - stack) - 1);
                frames.push_back(frame);

                run(initialDepth);
            }
        }
    }
}
