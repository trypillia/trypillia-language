#include "WebSocket.h"

#include <mbedtls/net_sockets.h>

#include <cstring>
#include <iostream>
#include <sstream>
#include <vector>

#include "../StdLib.h"
#include "../crypto/Crypto.h"

namespace StdLib
{
namespace WebSocketModule
{

struct WSServerData
{
    mbedtls_net_context fd;
};
struct WSClientData
{
    mbedtls_net_context fd;
};

static void freeServer(void *data)
{
    if (data)
    {
        mbedtls_net_free(&((WSServerData *)data)->fd);
        delete (WSServerData *)data;
    }
}

static void freeClient(void *data)
{
    if (data)
    {
        mbedtls_net_free(&((WSClientData *)data)->fd);
        delete (WSClientData *)data;
    }
}

static VMValue wsServerCreate(int argCount, VMValue *args)
{
    if (argCount != 1 || !args[0].isNumber())
        return makeResultErr(currentVM, "Invalid port");
    int port = (int)args[0].asNumber();

    WSServerData *data = new WSServerData();
    mbedtls_net_init(&data->fd);

    if (mbedtls_net_bind(&data->fd, NULL, std::to_string(port).c_str(), MBEDTLS_NET_PROTO_TCP) != 0)
    {
        delete data;
        return makeResultErr(currentVM, "Bind failed");
    }

    auto inst = new ObjInstance(currentVM->globals["WebSocketServer"].asClass());
    inst->nativeData = data;
    inst->freeFn = freeServer;
    return makeResultOk(currentVM, inst);
}

static std::string extractHeader(const std::string &request, const std::string &header)
{
    size_t pos = request.find(header + ": ");
    if (pos == std::string::npos)
        return "";
    pos += header.length() + 2;
    size_t end = request.find("\r\n", pos);
    return (end == std::string::npos) ? "" : request.substr(pos, end - pos);
}

static VMValue wsServerAccept(int argCount, VMValue *args)
{
    auto inst = args[-1].asInstance();
    WSServerData *data = (WSServerData *)inst->nativeData;

    mbedtls_net_context client_fd;
    mbedtls_net_init(&client_fd);
    if (mbedtls_net_accept(&data->fd, &client_fd, NULL, 0, NULL) != 0)
    {
        return makeResultErr(currentVM, "Accept failed");
    }

    unsigned char buffer[4096] = {0};
    int ret = mbedtls_net_recv(&client_fd, buffer, 4095);
    if (ret <= 0)
    {
        mbedtls_net_free(&client_fd);
        return makeResultErr(currentVM, "Handshake read failed");
    }

    std::string request((char *)buffer);
    std::string wsKey = extractHeader(request, "Sec-WebSocket-Key");
    if (wsKey.empty())
    {
        const char *bad = "HTTP/1.1 400 Bad Request\r\n\r\n";
        mbedtls_net_send(&client_fd, (const unsigned char *)bad, strlen(bad));
        mbedtls_net_free(&client_fd);
        return makeResultErr(currentVM, "Not a websocket");
    }

    VMValue shaArg[1] = {wsKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"};
    auto cryptoClass = currentVM->globals["Crypto"].asClass();
    auto shaFn = cryptoClass->statics["sha1Base64"].asNative();
    VMValue res = shaFn->function(1, shaArg);

    std::string acceptKey = res.asInstance()->fields["value"].asString()->flatten();
    std::string response = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: "
                           "Upgrade\r\nSec-WebSocket-Accept: " +
                           acceptKey + "\r\n\r\n";
    mbedtls_net_send(&client_fd, (const unsigned char *)response.c_str(), response.length());

    WSClientData *clientData = new WSClientData{client_fd};
    auto wsInst = new ObjInstance(currentVM->globals["WebSocket"].asClass());
    wsInst->nativeData = clientData;
    wsInst->freeFn = freeClient;
    return makeResultOk(currentVM, wsInst);
}

static VMValue wsSend(int argCount, VMValue *args)
{
    auto inst = args[-1].asInstance();
    WSClientData *data = (WSClientData *)inst->nativeData;
    std::string msg = args[0].asString()->flatten();
    std::vector<uint8_t> frame = {0x81};
    size_t len = msg.length();

    if (len <= 125)
        frame.push_back((uint8_t)len);
    else if (len <= 65535)
    {
        frame.push_back(126);
        frame.push_back((len >> 8) & 0xFF);
        frame.push_back(len & 0xFF);
    }
    else
    {
        frame.push_back(127);
        for (int i = 7; i >= 0; i--)
            frame.push_back((len >> (i * 8)) & 0xFF);
    }

    frame.insert(frame.end(), msg.begin(), msg.end());
    return makeResultOk(currentVM, mbedtls_net_send(&data->fd, frame.data(), frame.size()) >= 0);
}

static VMValue wsRecv(int argCount, VMValue *args)
{
    auto inst = args[-1].asInstance();
    WSClientData *data = (WSClientData *)inst->nativeData;
    uint8_t header[2];
    if (mbedtls_net_recv(&data->fd, header, 2) <= 0)
        return makeResultErr(currentVM, "Closed");

    uint8_t opcode = header[0] & 0x0F;
    bool masked = (header[1] & 0x80) != 0;
    uint64_t len = header[1] & 0x7F;

    if (opcode == 0x8)
        return makeResultErr(currentVM, "Closed by client");

    if (len == 126)
    {
        uint8_t ext[2];
        mbedtls_net_recv(&data->fd, ext, 2);
        len = (uint64_t(ext[0]) << 8) | ext[1];
    }
    else if (len == 127)
    {
        uint8_t ext[8];
        mbedtls_net_recv(&data->fd, ext, 8);
        len = 0;
        for (int i = 0; i < 8; i++)
            len = (len << 8) | ext[i];
    }

    uint8_t mask[4] = {0};
    if (masked)
        mbedtls_net_recv(&data->fd, mask, 4);

    std::vector<uint8_t> payload(static_cast<size_t>(len));
    size_t total = 0;
    while (total < len)
    {
        int r = mbedtls_net_recv(&data->fd, payload.data() + total, static_cast<size_t>(len - total));
        if (r <= 0)
            return makeResultErr(currentVM, "Read error");
        total += static_cast<size_t>(r);
    }

    if (masked)
        for (size_t i = 0; i < static_cast<size_t>(len); i++)
            payload[i] ^= mask[i % 4];
    return makeResultOk(currentVM, std::string(payload.begin(), payload.end()));
}

static VMValue wsClose(int argCount, VMValue *args)
{
    auto inst = args[-1].asInstance();
    if (inst->nativeData)
    {
        uint8_t frame[] = {0x88, 0x00};
        mbedtls_net_send(&((WSClientData *)inst->nativeData)->fd, frame, 2);
        mbedtls_net_free(&((WSClientData *)inst->nativeData)->fd);
        inst->nativeData = nullptr;
    }
    return makeResultOk(currentVM, true);
}

void registerAll(VM *vm)
{
    currentVM = vm;
    auto srv = new ObjClass("WebSocketServer");
    srv->statics["create"] = new ObjNative("create", 1, wsServerCreate);
    srv->methods["accept"] = new ObjNative("accept", 0, wsServerAccept);
    vm->globals["WebSocketServer"] = srv;

    auto ws = new ObjClass("WebSocket");
    ws->methods["send"] = new ObjNative("send", 1, wsSend);
    ws->methods["recv"] = new ObjNative("recv", 0, wsRecv);
    ws->methods["close"] = new ObjNative("close", 0, wsClose);
    vm->globals["WebSocket"] = ws;
}

void registerSymbols(SymbolTable *scope)
{
    Symbol s;
    s.type = "class";
    s.isConst = true;
    s.name = "WebSocketServer";
    scope->define(s);
    s.name = "WebSocket";
    scope->define(s);
}
} // namespace WebSocketModule
} // namespace StdLib
