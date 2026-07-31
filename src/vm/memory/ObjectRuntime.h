#pragma once
#include "../core/VM.h"
#include <string>

bool isMethodAbstract(const VMValue &method);
VMAccessModifier getMethodAccessModifier(const VMValue &method);
std::string getMethodName(const VMValue &method);
int getMethodMinArity(const VMValue &method);
int getMethodMaxArity(const VMValue &method);
int utf8_length(const std::string &str);
std::string utf8_char_at(const std::string &str, int index);
bool checkAccess(VMAccessModifier modifier, ObjClass *klass, const std::string &callerClass);
