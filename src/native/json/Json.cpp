#include "Json.h"

#include <cctype>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace StdLib
{
namespace Json
{

static std::string stringifyValue(const VMValue &val, int indent = -1, int currentIndent = 0)
{
    if (val.isNil())
        return "null";
    if (val.isBool())
        return val.asBool() ? "true" : "false";
    if (val.isNumber())
    {
        double d = val.asNumber();
        if (d == static_cast<double>(static_cast<long long>(d)))
        {
            return std::to_string(static_cast<long long>(d));
        }
        std::stringstream ss;
        ss << d;
        return ss.str();
    }
    if (val.isString())
    {
        std::string s = val.asString()->flatten();
        std::string res = "\"";
        for (char c : s)
        {
            if (c == '"')
                res += "\\\"";
            else if (c == '\\')
                res += "\\\\";
            else if (c == '\n')
                res += "\\n";
            else if (c == '\r')
                res += "\\r";
            else if (c == '\t')
                res += "\\t";
            else if (c == '\b')
                res += "\\b";
            else if (c == '\f')
                res += "\\f";
            else
                res += c;
        }
        res += "\"";
        return res;
    }

    std::string nl = (indent >= 0) ? "\n" : "";
    std::string sp = (indent >= 0) ? " " : "";

    if (val.isList())
    {
        auto list = val.asList();
        if (list->elements.empty())
            return "[]";

        std::string res = "[" + nl;
        int nextIndent = (indent >= 0) ? currentIndent + indent : 0;
        std::string indentStr = (indent >= 0) ? std::string(nextIndent, ' ') : "";
        std::string endIndentStr = (indent >= 0) ? std::string(currentIndent, ' ') : "";

        for (size_t i = 0; i < list->elements.size(); ++i)
        {
            res += indentStr + stringifyValue(list->elements[i], indent, nextIndent);
            if (i < list->elements.size() - 1)
                res += "," + nl;
            else
                res += nl;
        }
        res += endIndentStr + "]";
        return res;
    }
    if (val.isMap())
    {
        auto map = val.asMap();
        if (map->values.empty())
            return "{}";

        std::string res = "{" + nl;
        int nextIndent = (indent >= 0) ? currentIndent + indent : 0;
        std::string indentStr = (indent >= 0) ? std::string(nextIndent, ' ') : "";
        std::string endIndentStr = (indent >= 0) ? std::string(currentIndent, ' ') : "";

        bool first = true;
        for (auto const &[k, v] : map->values)
        {
            if (!first)
                res += "," + nl;
            first = false;

            res += indentStr;
            if (k.isString())
            {
                res += stringifyValue(k, -1, 0) + ":" + sp + stringifyValue(v, indent, nextIndent);
            }
            else
            {
                std::stringstream ss;
                if (k.isNumber())
                {
                    double dk = k.asNumber();
                    if (dk == static_cast<double>(static_cast<long long>(dk)))
                    {
                        ss << static_cast<long long>(dk);
                    }
                    else
                    {
                        ss << dk;
                    }
                }
                else if (k.isBool())
                    ss << (k.asBool() ? "true" : "false");
                else if (k.isNil())
                    ss << "null";
                res += "\"" + ss.str() + "\":" + sp + stringifyValue(v, indent, nextIndent);
            }
        }
        res += nl + endIndentStr + "}";
        return res;
    }
    return "null"; // Default fallback
}

static VMValue jsonStringify(int argCount, VMValue *args)
{
    if (argCount < 1 || argCount > 2)
        return nullptr;
    int indent = -1;
    if (argCount == 2 && args[1].isNumber())
    {
        indent = static_cast<int>(args[1].asNumber());
    }
    return stringifyValue(args[0], indent, 0);
}

// Simple JSON Parser
class JsonParser
{
    const std::string &src;
    size_t pos = 0;
    const int MAX_DEPTH = 512;

    void skipWhitespace()
    {
        while (pos < src.length() && std::isspace(src[pos]))
            pos++;
    }

    VMValue parseString()
    {
        pos++; // skip "
        std::string res;
        while (pos < src.length() && src[pos] != '"')
        {
            if (src[pos] == '\\' && pos + 1 < src.length())
            {
                pos++;
                if (src[pos] == 'n')
                    res += '\n';
                else if (src[pos] == 'r')
                    res += '\r';
                else if (src[pos] == 't')
                    res += '\t';
                else if (src[pos] == 'b')
                    res += '\b';
                else if (src[pos] == 'f')
                    res += '\f';
                else if (src[pos] == 'u' && pos + 4 < src.length())
                {
                    std::string hexStr = src.substr(pos + 1, 4);
                    try
                    {
                        int codePoint = std::stoi(hexStr, nullptr, 16);
                        pos += 4;

                        // Handle surrogate pairs
                        if (codePoint >= 0xD800 && codePoint <= 0xDBFF)
                        {
                            if (pos + 6 < src.length() && src[pos + 1] == '\\' && src[pos + 2] == 'u')
                            {
                                std::string hexStr2 = src.substr(pos + 3, 4);
                                try
                                {
                                    int lowSurrogate = std::stoi(hexStr2, nullptr, 16);
                                    if (lowSurrogate >= 0xDC00 && lowSurrogate <= 0xDFFF)
                                    {
                                        codePoint = 0x10000 + (((codePoint - 0xD800) << 10) | (lowSurrogate - 0xDC00));
                                        pos += 6;
                                    }
                                }
                                catch (...)
                                {
                                }
                            }
                        }

                        if (codePoint <= 0x7F)
                        {
                            res += static_cast<char>(codePoint);
                        }
                        else if (codePoint <= 0x7FF)
                        {
                            res += static_cast<char>(0xC0 | ((codePoint >> 6) & 0x1F));
                            res += static_cast<char>(0x80 | (codePoint & 0x3F));
                        }
                        else if (codePoint <= 0xFFFF)
                        {
                            res += static_cast<char>(0xE0 | ((codePoint >> 12) & 0x0F));
                            res += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
                            res += static_cast<char>(0x80 | (codePoint & 0x3F));
                        }
                        else if (codePoint <= 0x10FFFF)
                        {
                            res += static_cast<char>(0xF0 | ((codePoint >> 18) & 0x07));
                            res += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
                            res += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
                            res += static_cast<char>(0x80 | (codePoint & 0x3F));
                        }
                    }
                    catch (...)
                    {
                        res += src[pos];
                    }
                }
                else
                    res += src[pos];
            }
            else
            {
                res += src[pos];
            }
            pos++;
        }
        if (pos < src.length())
            pos++; // skip "
        return res;
    }

    VMValue parseNumber()
    {
        size_t start = pos;
        while (pos < src.length() && (std::isdigit(src[pos]) || src[pos] == '.' || src[pos] == '-' || src[pos] == 'e' ||
                                      src[pos] == 'E' || src[pos] == '+'))
        {
            pos++;
        }
        std::string numStr = src.substr(start, pos - start);
        try
        {
            return std::stod(numStr);
        }
        catch (...)
        {
            return 0.0;
        }
    }

    VMValue parseArray(int depth)
    {
        if (depth > MAX_DEPTH)
            return nullptr;
        pos++; // skip [
        skipWhitespace();
        std::vector<VMValue> elements;
        if (pos < src.length() && src[pos] == ']')
        {
            pos++;
            return new ObjList(elements);
        }
        while (pos < src.length())
        {
            elements.push_back(parseValue(depth + 1));
            skipWhitespace();
            if (pos < src.length() && src[pos] == ',')
            {
                pos++;
                skipWhitespace();
            }
            else if (pos < src.length() && src[pos] == ']')
            {
                pos++;
                break;
            }
            else
            {
                break; // Unexpected character
            }
        }
        return new ObjList(elements);
    }

    VMValue parseObject(int depth)
    {
        if (depth > MAX_DEPTH)
            return nullptr;
        pos++; // skip {
        skipWhitespace();
        auto map = new ObjMap();
        if (pos < src.length() && src[pos] == '}')
        {
            pos++;
            return map;
        }
        while (pos < src.length())
        {
            skipWhitespace();
            if (src[pos] != '"')
                break;
            VMValue key = parseString();
            skipWhitespace();
            if (src[pos] != ':')
                break;
            pos++;
            skipWhitespace();
            VMValue value = parseValue(depth + 1);
            map->values[key] = value;
            skipWhitespace();
            if (pos < src.length() && src[pos] == ',')
            {
                pos++;
            }
            else if (pos < src.length() && src[pos] == '}')
            {
                pos++;
                break;
            }
            else
            {
                break;
            }
        }
        return map;
    }

  public:
    JsonParser(const std::string &src) : src(src)
    {
    }

    VMValue parseValue(int depth = 0)
    {
        if (depth > MAX_DEPTH)
            return nullptr;
        skipWhitespace();
        if (pos >= src.length())
            return nullptr;
        char c = src[pos];
        if (c == '"')
            return parseString();
        if (c == '[')
            return parseArray(depth);
        if (c == '{')
            return parseObject(depth);
        if (std::isdigit(c) || c == '-')
            return parseNumber();
        if (src.compare(pos, 4, "true") == 0)
        {
            pos += 4;
            return true;
        }
        if (src.compare(pos, 5, "false") == 0)
        {
            pos += 5;
            return false;
        }
        if (src.compare(pos, 4, "null") == 0)
        {
            pos += 4;
            return nullptr;
        }
        return nullptr;
    }
};

static VMValue jsonParse(int argCount, VMValue *args)
{
    if (argCount != 1 || !args[0].isString())
        return nullptr;
    std::string jsonStr = args[0].asString()->flatten();
    JsonParser parser(jsonStr);
    return parser.parseValue();
}

void registerAll(VM *vm)
{
    auto jsonClass = new ObjClass("Json");
    jsonClass->statics["stringify"] = new ObjNative("stringify", -1, jsonStringify);
    jsonClass->statics["parse"] = new ObjNative("parse", 1, jsonParse);
    vm->globals["Json"] = jsonClass;
}

void registerSymbols(SymbolTable *scope)
{
    Symbol sym;
    sym.name = "Json";
    sym.type = "class";
    sym.isConst = true;
    scope->define(sym);
}
} // namespace Json
} // namespace StdLib
