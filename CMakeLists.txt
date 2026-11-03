cmake_minimum_required(VERSION 3.20)

project(Transmission LANGUAGES C CXX)

set(TR_NAME ${PROJECT_NAME})

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Disable tests and optional subprojects for minimal build
set(ENABLE_UTILS OFF CACHE BOOL "" FORCE)
set(ENABLE_QT OFF CACHE BOOL "" FORCE)

# Set MSYS2 MinGW64 root for finding system libraries
set(MSYS2_MINGW64_ROOT "C:/msys64/mingw64")

# Create missing third-party targets before subdirectories
# These are needed by libtransmission-app and daemon

# libevent targets
if(NOT TARGET libevent::core)
    add_library(libevent::core INTERFACE IMPORTED)
    target_include_directories(libevent::core INTERFACE "${MSYS2_MINGW64_ROOT}/include")
    target_link_libraries(libevent::core INTERFACE "${MSYS2_MINGW64_ROOT}/lib/libevent_core.dll.a")
endif()
if(NOT TARGET libevent::extra)
    add_library(libevent::extra INTERFACE IMPORTED)
    target_include_directories(libevent::extra INTERFACE "${MSYS2_MINGW64_ROOT}/include")
    target_link_libraries(libevent::extra INTERFACE "${MSYS2_MINGW64_ROOT}/lib/libevent_extra.dll.a")
endif()
if(NOT TARGET libevent::openssl)
    add_library(libevent::openssl INTERFACE IMPORTED)
    target_include_directories(libevent::openssl INTERFACE "${MSYS2_MINGW64_ROOT}/include")
    target_link_libraries(libevent::openssl INTERFACE "${MSYS2_MINGW64_ROOT}/lib/libevent_openssl.dll.a")
endif()

# fmt target
if(NOT TARGET fmt::fmt-header-only)
    add_library(fmt::fmt-header-only INTERFACE IMPORTED)
    target_compile_definitions(fmt::fmt-header-only INTERFACE FMT_HEADER_ONLY=1)
    target_compile_features(fmt::fmt-header-only INTERFACE cxx_std_11)
    target_include_directories(fmt::fmt-header-only INTERFACE "${MSYS2_MINGW64_ROOT}/include")
endif()

# libdeflate target
if(NOT TARGET libdeflate::libdeflate)
    add_library(libdeflate::libdeflate INTERFACE IMPORTED)
    target_include_directories(libdeflate::libdeflate INTERFACE "${MSYS2_MINGW64_ROOT}/include")
    target_link_libraries(libdeflate::libdeflate INTERFACE "${MSYS2_MINGW64_ROOT}/lib/libdeflate.dll.a")
endif()

# libb64 target
if(NOT TARGET libb64::libb64)
    add_library(libb64::libb64 INTERFACE IMPORTED)
    target_include_directories(libb64::libb64 INTERFACE "${MSYS2_MINGW64_ROOT}/include")
    target_link_libraries(libb64::libb64 INTERFACE "${MSYS2_MINGW64_ROOT}/lib/libb64.dll.a")
endif()

# rapidjson target (header-only)
if(NOT TARGET RapidJSON)
    add_library(RapidJSON INTERFACE IMPORTED)
    target_include_directories(RapidJSON INTERFACE "${MSYS2_MINGW64_ROOT}/include")
endif()

# libpsl target
if(NOT TARGET psl::psl)
    add_library(psl::psl INTERFACE IMPORTED)
    target_include_directories(psl::psl INTERFACE "${MSYS2_MINGW64_ROOT}/include")
    target_link_libraries(psl::psl INTERFACE "${MSYS2_MINGW64_ROOT}/lib/libpsl.dll.a")
endif()

# miniupnpc target
if(NOT TARGET miniupnpc::miniupnpc)
    add_library(miniupnpc::miniupnpc INTERFACE IMPORTED)
    target_include_directories(miniupnpc::miniupnpc INTERFACE "${MSYS2_MINGW64_ROOT}/include")
    target_link_libraries(miniupnpc::miniupnpc INTERFACE "${MSYS2_MINGW64_ROOT}/lib/libminiupnpc.dll.a")
endif()

# libutp target
if(NOT TARGET libutp::libutp)
    add_library(libutp::libutp INTERFACE IMPORTED)
    target_include_directories(libutp::libutp INTERFACE "${MSYS2_MINGW64_ROOT}/include")
    target_link_libraries(libutp::libutp INTERFACE "${MSYS2_MINGW64_ROOT}/lib/libutp.dll.a")
endif()

# libnatpmp target (may not exist in MSYS2)
if(NOT TARGET natpmp::natpmp)
    add_library(natpmp::natpmp INTERFACE IMPORTED)
    target_include_directories(natpmp::natpmp INTERFACE "${MSYS2_MINGW64_ROOT}/include")
    # target_link_libraries(natpmp::natpmp INTERFACE "${MSYS2_MINGW64_ROOT}/lib/libnatpmp.dll.a")
endif()

# FastFloat target (header-only)
if(NOT TARGET FastFloat::fast_float)
    add_library(FastFloat::fast_float INTERFACE IMPORTED)
    target_include_directories(FastFloat::fast_float INTERFACE "${MSYS2_MINGW64_ROOT}/include")
endif()

# utfcpp target (header-only)
if(NOT TARGET utf8::cpp)
    add_library(utf8::cpp INTERFACE IMPORTED)
    target_include_directories(utf8::cpp INTERFACE "${MSYS2_MINGW64_ROOT}/include")
endif()

# wildmat target (header-only, third-party)
if(NOT TARGET wildmat)
    add_library(wildmat INTERFACE IMPORTED)
    target_include_directories(wildmat INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/third-party/wildmat")
endif()

# wide-integer target (header-only, third-party)
if(NOT TARGET transmission::WideInteger)
    add_library(transmission::WideInteger INTERFACE IMPORTED)
    target_include_directories(transmission::WideInteger INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/third-party/wide-integer")
endif()

# mader-crcany target (header-only, third-party)
if(NOT TARGET MadlerCrcany)
    add_library(MadlerCrcany INTERFACE IMPORTED)
    target_include_directories(MadlerCrcany INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/third-party/madler-crcany")
endif()

# sigslot target (header-only, third-party)
if(NOT TARGET Pal::Sigslot)
    add_library(Pal::Sigslot INTERFACE IMPORTED)
    target_include_directories(Pal::Sigslot INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/third-party/sigslot")
endif()

# small target (header-only, third-party)
if(NOT TARGET transmission::small)
    add_library(transmission::small INTERFACE IMPORTED)
    target_include_directories(transmission::small INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/third-party/small")
endif()

# CURL target (from MSYS2)
if(NOT TARGET CURL::libcurl)
    add_library(CURL::libcurl INTERFACE IMPORTED)
    target_include_directories(CURL::libcurl INTERFACE "${MSYS2_MINGW64_ROOT}/include")
    target_link_libraries(CURL::libcurl INTERFACE "${MSYS2_MINGW64_ROOT}/lib/libcurl.dll.a")
endif()

# OpenSSL target (for libevent::openssl and crypto)
if(NOT TARGET OpenSSL::SSL)
    add_library(OpenSSL::SSL INTERFACE IMPORTED)
    target_include_directories(OpenSSL::SSL INTERFACE "${MSYS2_MINGW64_ROOT}/include")
    target_link_libraries(OpenSSL::SSL INTERFACE "${MSYS2_MINGW64_ROOT}/lib/libssl.dll.a")
endif()
if(NOT TARGET OpenSSL::Crypto)
    add_library(OpenSSL::Crypto INTERFACE IMPORTED)
    target_include_directories(OpenSSL::Crypto INTERFACE "${MSYS2_MINGW64_ROOT}/include")
    target_link_libraries(OpenSSL::Crypto INTERFACE "${MSYS2_MINGW64_ROOT}/lib/libcrypto.dll.a")
endif()

# dht target (third-party, built from source)
if(NOT TARGET dht::dht)
    add_library(dht::dht INTERFACE IMPORTED)
    target_include_directories(dht::dht INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/third-party/dht")
endif()

include(TrGTest)

add_subdirectory(libtransmission)
add_subdirectory(libtransmission-app)
add_subdirectory(daemon)
