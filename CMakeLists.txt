cmake_minimum_required(VERSION 3.10)
project(trypillia)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

# =============================================================
# Source layout
#
# trypillia_core   — common VM, frontend, JIT runtime, serializer.
#                    Built as a STATIC lib so it can be linked into
#                    both the trypillia CLI binary and the
#                    trypillia-test runner. Does NOT include the
#                    AOT encoder, ELF writer, AOT main, or the
#                    Bridge.cpp helpers — those go in trypillia_rt.
#
# trypillia_rt     — minimal runtime for AOT-produced binaries:
#                    jit_*_helper (Bridge.cpp), AOTMain.cpp, the
#                    GC stub used by those helpers. Linked into
#                    the trypillia CLI (so the JIT path still
#                    resolves the symbols) and into AOT binaries
#                    (so they don't need to drag the rest of the
#                    VM in).
#
# trypillia_aot    — AOT-only code: IR, IRLowering, x64 encoder,
#                    object/ELF writer, Linker wrapper. Not linked
#                    into the CLI binary, but the CLI uses it
#                    implicitly when build --aot is invoked (the
#                    CLI binary is built with --whole-archive to
#                    pull in AOTMain's constructor and any other
#                    symbols needed at startup).
# =============================================================

# --- trypillia_core ---

file(GLOB_RECURSE CORE_SOURCES "src/*.cpp")
list(REMOVE_ITEM CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
list(REMOVE_ITEM CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/test_runner_main.cpp")
# Bridge.cpp moves to trypillia_rt (so AOT binaries don't need the
# rest of the VM).
list(REMOVE_ITEM CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/vm/jit/Bridge.cpp")
# AOT sources move to trypillia_aot.
list(REMOVE_ITEM CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/vm/aot/AOTMain.cpp")
list(REMOVE_ITEM CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/vm/aot/AOTModule.cpp")
list(REMOVE_ITEM CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/vm/aot/IRLowering.cpp")
list(REMOVE_ITEM CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/vm/aot/Linker.cpp")
list(REMOVE_ITEM CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/vm/aot/AOTTrampoline.cpp")
list(REMOVE_ITEM CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/vm/aot/x64/Encoder.cpp")
list(REMOVE_ITEM CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/vm/aot/x64/X64ObjectBackend.cpp")
list(REMOVE_ITEM CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/vm/aot/object/ElfWriter.cpp")

include(FetchContent)
FetchContent_Declare(
  mbedtls
  GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
  GIT_TAG        v3.6.0
)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(mbedtls)

FetchContent_Declare(
  sljit
  GIT_REPOSITORY https://github.com/zherczeg/sljit.git
  GIT_TAG        master
)
FetchContent_GetProperties(sljit)
if(NOT sljit_POPULATED)
  FetchContent_Populate(sljit)
endif()

include_directories(include ${sljit_SOURCE_DIR}/sljit_src)

add_library(trypillia_core STATIC ${CORE_SOURCES} ${sljit_SOURCE_DIR}/sljit_src/sljitLir.c)
target_include_directories(trypillia_core PUBLIC include ${sljit_SOURCE_DIR}/sljit_src)
target_link_libraries(trypillia_core PUBLIC mbedtls mbedx509 mbedcrypto)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFI REQUIRED libffi)

# dlopen/dlsym/dlclose for the FFI module (native/ffi/FFI.cpp). CMAKE_DL_LIBS
# is empty on platforms where these are already part of libc (e.g. modern
# glibc) and "-ldl" on platforms where a separate library is still required.
target_link_libraries(trypillia_core PUBLIC ${CMAKE_DL_LIBS} ${FFI_LIBRARIES})
target_include_directories(trypillia_core PUBLIC ${FFI_INCLUDE_DIRS})

# --- trypillia_rt (Bridge.cpp + AOTMain.cpp) ---

add_library(trypillia_rt STATIC
    src/vm/jit/Bridge.cpp
    src/vm/aot/AOTMain.cpp
)
target_include_directories(trypillia_rt PUBLIC include)

# The runtime depends on the core VM (for `VM` in AOTMain and the
# helpers). We link PUBLIC so that anything linking trypillia_rt
# also gets the transitive dependency chain.
target_link_libraries(trypillia_rt PUBLIC trypillia_core)

# --- trypillia_aot (AOT-only code) ---

add_library(trypillia_aot STATIC
    src/vm/aot/AOTModule.cpp
    src/vm/aot/IRLowering.cpp
    src/vm/aot/Linker.cpp
    src/vm/aot/AOTTrampoline.cpp
    src/vm/aot/x64/Encoder.cpp
    src/vm/aot/x64/X64ObjectBackend.cpp
    src/vm/aot/object/ElfWriter.cpp
)
target_include_directories(trypillia_aot PUBLIC include)
target_link_libraries(trypillia_aot PUBLIC trypillia_rt trypillia_core)

# --- CLI binaries ---

add_executable(trypillia src/main.cpp)
# --whole-archive is needed so AOTMain's symbols (used only by
# external linkers, not by the CLI itself) are present in the
# final binary, and so the AOTModule / Linker / AOTTrampoline
# symbols are pulled in for `build --aot` to use.
target_link_libraries(trypillia
    -Wl,--whole-archive trypillia_aot -Wl,--no-whole-archive
    trypillia_core
    trypillia_rt
)

add_executable(trypillia-test src/test_runner_main.cpp)
target_link_libraries(trypillia-test
    -Wl,--whole-archive trypillia_aot -Wl,--no-whole-archive
    trypillia_core
    trypillia_rt
)

# Explicitly link atomic for 32-bit ARM (like Android/Termux armv7)
include(CheckLibraryExists)
check_library_exists(atomic __atomic_fetch_add_4 "" HAVE_LIBATOMIC)
if(HAVE_LIBATOMIC OR ANDROID OR CMAKE_SYSTEM_NAME MATCHES "Android|Linux")
    target_link_libraries(trypillia_core PUBLIC atomic)
endif()

# Explicitly link filesystem for older compilers (Clang 9 in Termux)
find_library(CXXFS_LIB NAMES c++fs stdc++fs c++experimental)
if(CXXFS_LIB)
    target_link_libraries(trypillia_core PUBLIC ${CXXFS_LIB})
endif()

if(DEFINED ARTIFACT_NAME)
    set_target_properties(trypillia PROPERTIES OUTPUT_NAME "${ARTIFACT_NAME}")
endif()

if(MSVC)
    target_compile_options(trypillia PRIVATE /wd4244 /wd4267)
    target_compile_definitions(trypillia PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(trypillia_core PRIVATE /wd4244 /wd4267)
    target_compile_definitions(trypillia_core PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
