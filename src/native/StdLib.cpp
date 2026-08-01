#include "core/Core.h"
#include "crypto/Crypto.h"
#include "fs/FS.h"
#include "list/List.h"
#include "map/Map.h"
#include "math/Math.h"
#include "net/Net.h"
#include "net/WebSocket.h"
#include "os/OS.h"
#include "promise/Promise.h"
#include "random/Random.h"
#include "regex/Regex.h"
#include "string/String.h"
#include "terminal/Terminal.h"
#include "test/Test.h"
#include "time/Time.h"
#include "worker/Worker.h"
#include "json/Json.h"

namespace StdLib
{
void registerAll(VM *vm)
{
    Core::registerAll(vm);
    Math::registerAll(vm);
    FS::registerAll(vm);
    Net::registerAll(vm);
    Json::registerAll(vm);
    StringModule::registerAll(vm);
    TimeModule::registerAll(vm);
    ListModule::registerAll(vm);
    OSModule::registerAll(vm);
    RandomModule::registerAll(vm);
    TerminalModule::registerAll(vm);
    MapModule::registerAll(vm);
    RegexModule::registerAll(vm);
    WorkerModule::registerAll(vm);
    CryptoModule::registerAll(vm);
    WebSocketModule::registerAll(vm);
    Test::registerAll(vm);
    PromiseModule::registerAll(vm);
}

void registerSymbols(SymbolTable *scope)
{
    Core::registerSymbols(scope);
    Math::registerSymbols(scope);
    FS::registerSymbols(scope);
    Net::registerSymbols(scope);
    Json::registerSymbols(scope);
    StringModule::registerSymbols(scope);
    TimeModule::registerSymbols(scope);
    ListModule::registerSymbols(scope);
    OSModule::registerSymbols(scope);
    RandomModule::registerSymbols(scope);
    TerminalModule::registerSymbols(scope);
    MapModule::registerSymbols(scope);
    RegexModule::registerSymbols(scope);
    WorkerModule::registerSymbols(scope);
    CryptoModule::registerSymbols(scope);
    WebSocketModule::registerSymbols(scope);
    Test::registerSymbols(scope);
    PromiseModule::registerSymbols(scope);
}

VMValue makeResultOk(VM *vm, VMValue value)
{
    auto klass = vm->globals["Result"].asClass();
    auto instance = new ObjInstance(klass);
    instance->fields["value"] = value;
    instance->fields["isOk"] = true;
    return instance;
}

VMValue makeResultErr(VM *vm, const std::string &message, double code)
{
    auto errClass = vm->globals["Error"].asClass();
    auto errInst = new ObjInstance(errClass);
    errInst->fields["message"] = VMValue(message);
    errInst->fields["code"] = code;

    auto resClass = vm->globals["Result"].asClass();
    auto resInst = new ObjInstance(resClass);
    resInst->fields["error"] = errInst;
    resInst->fields["isOk"] = false;
    return resInst;
}
} // namespace StdLib
