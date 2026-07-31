#include "Worker.h"
#include "../../frontend/lexer/Lexer.h"
#include "../../frontend/parser/Parser.h"
#include "../../frontend/semantic/SemanticAnalyzer.h"
#include "../../vm/compiler/BytecodeCompiler.h"
#include "../StdLib.h"
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>

namespace StdLib {
namespace WorkerModule {

struct WorkerChannel {
    std::queue<std::string> mainToWorker;
    std::mutex mtwMutex;
    std::condition_variable mtwCond;

    std::queue<std::string> workerToMain;
    std::mutex wtmMutex;
    std::condition_variable wtmCond;

    bool isAlive = true;
};

struct WorkerData {
    std::shared_ptr<WorkerChannel> channel;
    std::thread thread;
};

thread_local std::shared_ptr<WorkerChannel> currentWorkerChannel = nullptr;

static void workerThreadEntry(std::string scriptPath, std::shared_ptr<WorkerChannel> channel) {
    currentWorkerChannel = channel;

    VM vm;
    StdLib::registerAll(&vm);

    std::ifstream file(scriptPath);
    if (!file.is_open()) {
        channel->isAlive = false;
        channel->mtwCond.notify_all();
        channel->wtmCond.notify_all();
        return;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();

    Lexer lexer(source);
    Parser parser(lexer);
    ASTNode *ast = parser.parse();

    SemanticAnalyzer semanticAnalyzer;
    semanticAnalyzer.analyze(ast);

    Compiler compiler;
    compiler.currentFilename = scriptPath;
    auto function = compiler.compile(ast);
    if (function) {
        vm.interpret(function);
    }

    channel->isAlive = false;
    channel->mtwCond.notify_all();
    channel->wtmCond.notify_all();
}

static VMValue workerCreate(int argCount, VMValue *args) {
    if (argCount != 1 || !args[0].isString())
        return makeResultErr(currentVM, "Expected script path");
    std::string path = args[0].asString()->flatten();

    auto klass = currentVM->globals["Worker"].asClass();
    auto instance = new ObjInstance(klass);

    WorkerData *data = new WorkerData();
    data->channel = std::make_shared<WorkerChannel>();
    data->thread = std::thread(workerThreadEntry, path, data->channel);

    instance->nativeData = data;
    instance->freeFn = [](void *ptr) {
        WorkerData *d = static_cast<WorkerData *>(ptr);
        d->channel->isAlive = false;
        d->channel->mtwCond.notify_all();
        if (d->thread.joinable())
            d->thread.join();
        delete d;
    };

    return makeResultOk(currentVM, instance);
}

static VMValue workerSend(int argCount, VMValue *args) {
    if (argCount != 1 || !args[0].isString())
        return nullptr;

    VMValue receiver = args[-1];
    auto instance = receiver.asInstance();
    WorkerData *data = static_cast<WorkerData *>(instance->nativeData);

    if (!data || !data->channel->isAlive)
        return makeResultErr(currentVM, "Worker is dead");

    std::lock_guard<std::mutex> lock(data->channel->mtwMutex);
    data->channel->mainToWorker.push(args[0].asString()->flatten());
    data->channel->mtwCond.notify_one();

    return makeResultOk(currentVM, true);
}

static VMValue workerReceive(int argCount, VMValue *args) {
    if (argCount != 0)
        return nullptr;

    VMValue receiver = args[-1];
    auto instance = receiver.asInstance();
    WorkerData *data = static_cast<WorkerData *>(instance->nativeData);

    if (!data)
        return makeResultErr(currentVM, "Invalid worker");

    std::unique_lock<std::mutex> lock(data->channel->wtmMutex);
    data->channel->wtmCond.wait(lock,
                                [&data] { return !data->channel->workerToMain.empty() || !data->channel->isAlive; });

    if (!data->channel->workerToMain.empty()) {
        std::string msg = data->channel->workerToMain.front();
        data->channel->workerToMain.pop();
        return makeResultOk(currentVM, msg);
    }

    return makeResultErr(currentVM, "Worker terminated");
}

static VMValue workerSelfSend(int argCount, VMValue *args) {
    if (argCount != 1 || !args[0].isString())
        return nullptr;
    if (!currentWorkerChannel)
        return makeResultErr(currentVM, "Not in a worker thread");

    std::lock_guard<std::mutex> lock(currentWorkerChannel->wtmMutex);
    currentWorkerChannel->workerToMain.push(args[0].asString()->flatten());
    currentWorkerChannel->wtmCond.notify_one();

    return makeResultOk(currentVM, true);
}

static VMValue workerSelfReceive(int argCount, VMValue *args) {
    if (argCount != 0)
        return nullptr;
    if (!currentWorkerChannel)
        return makeResultErr(currentVM, "Not in a worker thread");

    std::unique_lock<std::mutex> lock(currentWorkerChannel->mtwMutex);
    currentWorkerChannel->mtwCond.wait(
        lock, [] { return !currentWorkerChannel->mainToWorker.empty() || !currentWorkerChannel->isAlive; });

    if (!currentWorkerChannel->mainToWorker.empty()) {
        std::string msg = currentWorkerChannel->mainToWorker.front();
        currentWorkerChannel->mainToWorker.pop();
        return makeResultOk(currentVM, msg);
    }

    return makeResultErr(currentVM, "Main channel closed");
}

void registerAll(VM *vm) {
    currentVM = vm;
    auto workerClass = new ObjClass("Worker");

    workerClass->statics["create"] = new ObjNative("create", 1, workerCreate);
    workerClass->methods["send"] = new ObjNative("send", 1, workerSend);
    workerClass->methods["receive"] = new ObjNative("receive", 0, workerReceive);

    workerClass->statics["selfSend"] = new ObjNative("selfSend", 1, workerSelfSend);
    workerClass->statics["selfReceive"] = new ObjNative("selfReceive", 0, workerSelfReceive);

    vm->globals["Worker"] = workerClass;
}

void registerSymbols(SymbolTable *scope) {
    Symbol sym;
    sym.name = "Worker";
    sym.type = "class";
    sym.isConst = true;
    scope->define(sym);
}

} // namespace WorkerModule
} // namespace StdLib
