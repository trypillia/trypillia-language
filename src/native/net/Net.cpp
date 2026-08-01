#include "Net.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "../StdLib.h"

namespace StdLib
{
namespace Net
{

static bool parseUrl(const std::string &url, std::string &host, int &port, std::string &path)
{
    size_t pos = 0;
    if (url.find("http://") == 0)
    {
        pos = 7;
        port = 80;
    }
    else if (url.find("https://") == 0)
    {
        pos = 8;
        port = 443;
    }
    else
        return false;

    size_t pathPos = url.find('/', pos);
    size_t colonPos = url.find(':', pos);

    if (pathPos == std::string::npos)
    {
        path = "/";
        pathPos = url.length();
    }
    else
        path = url.substr(pathPos);

    if (colonPos != std::string::npos && colonPos < pathPos)
    {
        host = url.substr(pos, colonPos - pos);
        try
        {
            port = std::stoi(url.substr(colonPos + 1, pathPos - colonPos - 1));
        }
        catch (...)
        {
            return false;
        }
    }
    else
        host = url.substr(pos, pathPos - pos);

    return true;
}

#include <psa/crypto.h>

static mbedtls_x509_crt cacert;
static bool is_psa_initialized = false;

static std::string makeHttpRequest(const std::string &host, int port, const std::string &requestStr, bool &success,
                                   std::string &errorMsg)
{
    if (!is_psa_initialized)
    {
        psa_crypto_init();
        mbedtls_x509_crt_init(&cacert);
        mbedtls_x509_crt_parse_file(&cacert, "/etc/ssl/certs/ca-certificates.crt");
        is_psa_initialized = true;
    }

    mbedtls_net_context fd;
    mbedtls_net_init(&fd);

    if (mbedtls_net_connect(&fd, host.c_str(), std::to_string(port).c_str(), MBEDTLS_NET_PROTO_TCP) != 0)
    {
        errorMsg = "Connection failed";
        success = false;
        return "";
    }

    if (port == 443)
    {
        mbedtls_entropy_context entropy;
        mbedtls_ctr_drbg_context ctr_drbg;
        mbedtls_ssl_context ssl;
        mbedtls_ssl_config conf;

        mbedtls_ssl_init(&ssl);
        mbedtls_ssl_config_init(&conf);
        mbedtls_ctr_drbg_init(&ctr_drbg);
        mbedtls_entropy_init(&entropy);

        if (mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)"trypillia", 9) !=
                0 ||
            mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                        MBEDTLS_SSL_PRESET_DEFAULT) != 0)
        {
            errorMsg = "SSL setup failed";
            success = false;
            return "";
        }

        mbedtls_ssl_conf_max_version(&conf, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);
        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
        mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);
        mbedtls_ssl_setup(&ssl, &conf);
        mbedtls_ssl_set_hostname(&ssl, host.c_str());
        mbedtls_ssl_set_bio(&ssl, &fd, mbedtls_net_send, mbedtls_net_recv, NULL);

        int ret;
        while ((ret = mbedtls_ssl_handshake(&ssl)) != 0)
        {
            if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
            {
                errorMsg = "SSL handshake failed: " + std::to_string(ret);
                success = false;
                mbedtls_ssl_free(&ssl);
                mbedtls_ssl_config_free(&conf);
                mbedtls_ctr_drbg_free(&ctr_drbg);
                mbedtls_entropy_free(&entropy);
                mbedtls_net_free(&fd);
                return "";
            }
        }

        mbedtls_ssl_write(&ssl, (const unsigned char *)requestStr.c_str(), requestStr.length());

        std::string response;
        unsigned char buf[4096];
        while ((ret = mbedtls_ssl_read(&ssl, buf, sizeof(buf))) > 0)
            response.append((char *)buf, ret);

        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&conf);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
        mbedtls_net_free(&fd);
        success = true;
        return response;
    }
    else
    {
        mbedtls_net_send(&fd, (const unsigned char *)requestStr.c_str(), requestStr.length());
        std::string response;
        unsigned char buf[4096];
        int ret;
        while ((ret = mbedtls_net_recv(&fd, buf, sizeof(buf))) > 0)
            response.append((char *)buf, ret);
        mbedtls_net_free(&fd);
        success = true;
        return response;
    }
}

static VMValue httpGet(int argCount, VMValue *args)
{
    if (argCount != 1 || !args[0].isString())
        return nullptr;
    std::string host, path;
    int port;
    if (!parseUrl(args[0].asString()->flatten(), host, port, path))
        return makeResultErr(currentVM, "Invalid URL");

    std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\n\r\n";
    bool success;
    std::string err;
    std::string res = makeHttpRequest(host, port, req, success, err);
    if (!success)
        return makeResultErr(currentVM, err);

    size_t body = res.find("\r\n\r\n");
    return makeResultOk(currentVM, body != std::string::npos ? res.substr(body + 4) : res);
}

static VMValue httpPost(int argCount, VMValue *args)
{
    if (argCount != 2 || !args[0].isString() || !args[1].isString())
        return nullptr;
    std::string host, path;
    int port;
    if (!parseUrl(args[0].asString()->flatten(), host, port, path))
        return makeResultErr(currentVM, "Invalid URL");

    std::string payload = args[1].asString()->flatten();
    std::string req = "POST " + path + " HTTP/1.1\r\nHost: " + host +
                      "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(payload.length()) +
                      "\r\nConnection: close\r\n\r\n" + payload;
    bool success;
    std::string err;
    std::string res = makeHttpRequest(host, port, req, success, err);
    if (!success)
        return makeResultErr(currentVM, err);

    size_t body = res.find("\r\n\r\n");
    return makeResultOk(currentVM, body != std::string::npos ? res.substr(body + 4) : res);
}

struct SocketData
{
    mbedtls_net_context fd;
};
static void freeSocket(void *data)
{
    if (data)
    {
        mbedtls_net_free(&((SocketData *)data)->fd);
        delete (SocketData *)data;
    }
}

static VMValue socketConnect(int argCount, VMValue *args)
{
    if (argCount != 2 || !args[0].isString() || !args[1].isNumber())
        return nullptr;
    SocketData *data = new SocketData();
    mbedtls_net_init(&data->fd);
    if (mbedtls_net_connect(&data->fd, args[0].asString()->flatten().c_str(),
                            std::to_string((int)args[1].asNumber()).c_str(), MBEDTLS_NET_PROTO_TCP) != 0)
    {
        delete data;
        return makeResultErr(currentVM, "Connect failed");
    }
    auto inst = new ObjInstance(currentVM->globals["Socket"].asClass());
    inst->nativeData = data;
    inst->freeFn = freeSocket;
    return makeResultOk(currentVM, inst);
}

// TCP Server Support
static VMValue socketListen(int argCount, VMValue *args)
{
    if (argCount != 1 || !args[0].isNumber())
        return nullptr;
    SocketData *data = new SocketData();
    mbedtls_net_init(&data->fd);
    if (mbedtls_net_bind(&data->fd, NULL, std::to_string((int)args[0].asNumber()).c_str(), MBEDTLS_NET_PROTO_TCP) != 0)
    {
        delete data;
        return makeResultErr(currentVM, "Bind failed");
    }
    auto inst = new ObjInstance(currentVM->globals["Socket"].asClass());
    inst->nativeData = data;
    inst->freeFn = freeSocket;
    return makeResultOk(currentVM, inst);
}

static VMValue socketAccept(int argCount, VMValue *args)
{
    auto inst = args[-1].asInstance();
    SocketData *data = (SocketData *)inst->nativeData;
    SocketData *clientData = new SocketData();
    mbedtls_net_init(&clientData->fd);
    if (mbedtls_net_accept(&data->fd, &clientData->fd, NULL, 0, NULL) != 0)
    {
        delete clientData;
        return nullptr;
    }
    auto clientInst = new ObjInstance(currentVM->globals["Socket"].asClass());
    clientInst->nativeData = clientData;
    clientInst->freeFn = freeSocket;
    return clientInst;
}

static VMValue socketSend(int argCount, VMValue *args)
{
    auto inst = args[-1].asInstance();
    SocketData *data = (SocketData *)inst->nativeData;
    std::string p = args[0].asString()->flatten();
    return makeResultOk(currentVM, mbedtls_net_send(&data->fd, (const unsigned char *)p.c_str(), p.length()) >= 0);
}

static VMValue socketRecv(int argCount, VMValue *args)
{
    auto inst = args[-1].asInstance();
    SocketData *data = (SocketData *)inst->nativeData;
    unsigned char buf[4096];
    int ret = mbedtls_net_recv(&data->fd, buf, 4096);
    return makeResultOk(currentVM, ret > 0 ? std::string((char *)buf, ret) : "");
}

static VMValue socketClose(int argCount, VMValue *args)
{
    auto inst = args[-1].asInstance();
    if (inst->nativeData)
    {
        mbedtls_net_free(&((SocketData *)inst->nativeData)->fd);
        inst->nativeData = nullptr;
    }
    return makeResultOk(currentVM, true);
}

void registerAll(VM *vm)
{
    currentVM = vm;
    auto http = new ObjClass("Http");
    http->statics["get"] = new ObjNative("get", 1, httpGet);
    http->statics["post"] = new ObjNative("post", 2, httpPost);
    vm->globals["Http"] = http;

    auto sock = new ObjClass("Socket");
    sock->statics["connect"] = new ObjNative("connect", 2, socketConnect);
    sock->statics["listen"] = new ObjNative("listen", 1, socketListen);
    sock->methods["accept"] = new ObjNative("accept", 0, socketAccept);
    sock->methods["send"] = new ObjNative("send", 1, socketSend);
    sock->methods["recv"] = new ObjNative("recv", -1, socketRecv);
    sock->methods["close"] = new ObjNative("close", 0, socketClose);
    vm->globals["Socket"] = sock;
}

void registerSymbols(SymbolTable *scope)
{
    Symbol s;
    s.type = "class";
    s.isConst = true;
    s.name = "Http";
    scope->define(s);
    s.name = "Socket";
    scope->define(s);
}
} // namespace Net
} // namespace StdLib
