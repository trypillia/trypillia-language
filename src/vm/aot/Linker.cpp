#include "Linker.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

namespace trypillia::aot
{

bool Linker::link(const std::string &outPath, const std::vector<std::string> &objectFiles,
                  const std::string &rtLibPath, const std::vector<std::string> &ccArgs, std::string &err)
{
    // Build the command line: cc <objs> -L<dir> -ltrypillia_rt -lm -lpthread -o <out> <ccArgs>
    // We use `-Wl,--undefined=<entry>` is *not* needed because the
    // C trampoline's __attribute__((constructor)) already
    // references the entry symbol directly. So the linker will
    // resolve it as part of the regular symbol resolution.
    std::ostringstream cmd;
    cmd << "c++";
    for (const auto &obj : objectFiles)
    {
        cmd << " '" << obj << "'";
    }
    // Add the runtime lib as -L<dir> -ltrypillia_rt
    // We extract the directory from rtLibPath to find -L; the
    // library name is "trypillia_rt".
    std::string rtDir;
    {
        auto pos = rtLibPath.find_last_of('/');
        if (pos != std::string::npos)
            rtDir = rtLibPath.substr(0, pos);
    }
    if (!rtDir.empty())
    {
        cmd << " -L'" << rtDir << "'";
    }
    cmd << " -ltrypillia_core -ltrypillia_rt -lm -lpthread";
    for (const auto &a : ccArgs)
    {
        cmd << " " << a;
    }
    cmd << " -o '" << outPath << "'";

    std::string cmdStr = cmd.str();
    int rc = std::system(cmdStr.c_str());
    if (rc != 0)
    {
        err = "linker (cc) failed with exit code " + std::to_string(rc) + ": " + cmdStr;
        return false;
    }
    return true;
}

} // namespace trypillia::aot
