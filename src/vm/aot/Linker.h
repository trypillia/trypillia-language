#ifndef TRYPILLIA_AOT_LINKER_H
#define TRYPILLIA_AOT_LINKER_H

#include <string>
#include <vector>

namespace trypillia::aot
{

// Linker — Phase-1 AOT invokes the system C compiler (`cc`) to
// link the .o file with libtrypillia_rt into a final executable.
//
// We use `cc` rather than raw `ld` for two reasons:
//   1) `cc` automatically picks the right C runtime, CRT files,
//      and dynamic linker for the host platform.
//   2) `cc` knows about libc/libm/libpthread by default.
//
// This is the single "compromise" called out in the RFC §7: the
// final link is delegated to the system linker. A future Phase
// will introduce `trypillia-ld` to remove this dependency.
class Linker
{
  public:
    // Run cc to link the given object files + the runtime static
    // library into `outPath`. Returns true on success.
    //
    // `ccArgs` may contain extra flags (e.g. user-supplied
    // libraries) that are appended after our standard flags.
    static bool link(const std::string &outPath, const std::vector<std::string> &objectFiles,
                     const std::string &rtLibPath, const std::vector<std::string> &ccArgs, std::string &err);
};

} // namespace trypillia::aot

#endif
