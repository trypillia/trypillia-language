#include "Crypto.h"

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "../StdLib.h"

namespace StdLib {
namespace CryptoModule {

// --- Base64 Implementation ---
static const std::string base64_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

static std::string base64_encode(const std::string& in) {
  std::string out;
  int val = 0, valb = -6;
  for (uint8_t c : in) {
    val = (val << 8) + c;
    valb += 8;
    while (valb >= 0) {
      out.push_back(base64_chars[(val >> valb) & 0x3F]);
      valb -= 6;
    }
  }
  if (valb > -6) out.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
  while (out.size() % 4) out.push_back('=');
  return out;
}

// --- SHA-1 Implementation ---
static uint32_t left_rotate(uint32_t value, size_t count) {
  return (value << count) ^ (value >> (32 - count));
}

static std::string sha1_raw(const std::string& input) {
  uint32_t h0 = 0x67452301;
  uint32_t h1 = 0xEFCDAB89;
  uint32_t h2 = 0x98BADCFE;
  uint32_t h3 = 0x10325476;
  uint32_t h4 = 0xC3D2E1F0;

  std::string padded = input;
  uint64_t original_length = padded.length() * 8;
  padded += (char)0x80;
  while ((padded.length() * 8) % 512 != 448) padded += (char)0x00;

  for (int i = 7; i >= 0; --i) {
    padded += (char)((original_length >> (i * 8)) & 0xFF);
  }

  for (size_t offset = 0; offset < padded.length(); offset += 64) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<unsigned char>(padded[offset + i * 4]) << 24) |
             (static_cast<unsigned char>(padded[offset + i * 4 + 1]) << 16) |
             (static_cast<unsigned char>(padded[offset + i * 4 + 2]) << 8) |
             (static_cast<unsigned char>(padded[offset + i * 4 + 3]));
    }
    for (int i = 16; i < 80; ++i) {
      w[i] = left_rotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;

    for (int i = 0; i < 80; ++i) {
      uint32_t f, k;
      if (i < 20) {
        f = (b & c) | ((~b) & d);
        k = 0x5A827999;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDC;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6;
      }
      uint32_t temp = left_rotate(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = left_rotate(b, 30);
      b = a;
      a = temp;
    }

    h0 += a;
    h1 += b;
    h2 += c;
    h3 += d;
    h4 += e;
  }

  std::string result = "";
  uint32_t hash[5] = {h0, h1, h2, h3, h4};
  for (int i = 0; i < 5; ++i) {
    for (int j = 3; j >= 0; --j) {
      result += (char)((hash[i] >> (j * 8)) & 0xFF);
    }
  }
  return result;
}

static VMValue cryptoSha1(int argCount, VMValue* args) {
  if (argCount != 1 || !args[0].isString()) return nullptr;
  std::string input = args[0].asString()->flatten();
  std::string raw = sha1_raw(input);

  std::stringstream hexStream;
  for (unsigned char c : raw) {
    hexStream << std::hex << std::setw(2) << std::setfill('0') << (int)c;
  }
  return makeResultOk(currentVM, hexStream.str());
}

static VMValue cryptoSha1Base64(int argCount, VMValue* args) {
  if (argCount != 1 || !args[0].isString()) return nullptr;
  std::string input = args[0].asString()->flatten();
  std::string raw = sha1_raw(input);
  return makeResultOk(currentVM, base64_encode(raw));
}

static VMValue cryptoBase64Encode(int argCount, VMValue* args) {
  if (argCount != 1 || !args[0].isString()) return nullptr;
  std::string input = args[0].asString()->flatten();
  return makeResultOk(currentVM, base64_encode(input));
}

void registerAll(VM* vm) {
  currentVM = vm;
  auto cryptoClass = new ObjClass("Crypto");

  cryptoClass->statics["sha1"] = new ObjNative("sha1", 1, cryptoSha1);
  cryptoClass->statics["sha1Base64"] =
      new ObjNative("sha1Base64", 1, cryptoSha1Base64);
  cryptoClass->statics["base64Encode"] =
      new ObjNative("base64Encode", 1, cryptoBase64Encode);

  vm->globals["Crypto"] = cryptoClass;
}

void registerSymbols(SymbolTable* scope) {
  Symbol sym;
  sym.name = "Crypto";
  sym.type = "class";
  sym.isConst = true;
  scope->define(sym);
}

}  // namespace CryptoModule
}  // namespace StdLib
