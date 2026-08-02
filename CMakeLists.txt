cmake_minimum_required(VERSION 3.10)
project(trypillia)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

file(GLOB_RECURSE CORE_SOURCES "src/*.cpp")
list(REMOVE_ITEM CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
list(REMOVE_ITEM CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/test_runner_main.cpp")

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

# dlopen/dlsym/dlclose for the FFI module (native/ffi/FFI.cpp). CMAKE_DL_LIBS
# is empty on platforms where these are already part of libc (e.g. modern
# glibc) and "-ldl" on platforms where a separate library is still required.
target_link_libraries(trypillia_core PUBLIC ${CMAKE_DL_LIBS})

add_executable(trypillia src/main.cpp)
target_link_libraries(trypillia trypillia_core)

add_executable(trypillia-test src/test_runner_main.cpp)
target_link_libraries(trypillia-test trypillia_core)

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

