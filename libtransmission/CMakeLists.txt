include(CheckAtomic)
include(CheckLibraryExists)
include(CheckSymbolExists)

find_package(Threads REQUIRED)

# Third-party targets are created in root CMakeLists.txt
# Skip find_package for packages handled there
if(NOT TARGET fmt::fmt-header-only)
    add_library(fmt::fmt-header-only INTERFACE IMPORTED)
    target_compile_definitions(fmt::fmt-header-only INTERFACE FMT_HEADER_ONLY=1)
    target_compile_features(fmt::fmt-header-only INTERFACE cxx_std_11)
    target_include_directories(fmt::fmt-header-only INTERFACE "${MSYS2_MINGW64_ROOT}/include")
endif()

# transmission::fmt-header-only alias
if(NOT TARGET transmission::fmt-header-only)
    add_library(transmission::fmt-header-only ALIAS fmt::fmt-header-only)
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

# dht target (third-party, built from source)
if(NOT TARGET dht::dht)
    add_library(dht::dht INTERFACE IMPORTED)
    target_include_directories(dht::dht INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/../third-party/dht")
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
    target_include_directories(wildmat INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/../third-party/wildmat")
endif()

# wide-integer target (header-only, third-party)
if(NOT TARGET transmission::WideInteger)
    add_library(transmission::WideInteger INTERFACE IMPORTED)
    target_include_directories(transmission::WideInteger INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/../third-party/wide-integer")
endif()

# mader-crcany target (header-only, third-party)
if(NOT TARGET MadlerCrcany)
    add_library(MadlerCrcany INTERFACE IMPORTED)
    target_include_directories(MadlerCrcany INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/../third-party/madler-crcany")
endif()

# sigslot target (header-only, third-party)
if(NOT TARGET Pal::Sigslot)
    add_library(Pal::Sigslot INTERFACE IMPORTED)
    target_include_directories(Pal::Sigslot INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/../third-party/sigslot/include")
endif()

# small target (header-only, third-party)
if(NOT TARGET transmission::small)
    add_library(transmission::small INTERFACE IMPORTED)
    target_include_directories(transmission::small INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/../third-party/small/include")
endif()

# transmission::crypto_impl target (exposes selected crypto backend)
if(NOT TARGET transmission::crypto_impl)
    add_library(transmission::crypto_impl INTERFACE IMPORTED)
    if(CRYPTO_PKG STREQUAL "openssl")
        target_link_libraries(transmission::crypto_impl INTERFACE OpenSSL::SSL OpenSSL::Crypto)
    elseif(CRYPTO_PKG STREQUAL "mbedtls")
        target_link_libraries(transmission::crypto_impl INTERFACE MbedTLS::mbedtls MbedTLS::mbedcrypto)
    elseif(CRYPTO_PKG STREQUAL "wolfssl")
        target_link_libraries(transmission::crypto_impl INTERFACE WolfSSL::WolfSSL)
    elseif(CRYPTO_PKG STREQUAL "ccrypto")
        # CommonCrypto is part of macOS/iOS SDK, no separate target needed
    endif()
endif()

# libevent targets (in case FindLibevent doesn't create them)
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

check_symbol_exists(SO_REUSEPORT "sys/types.h;sys/socket.h" HAVE_SO_REUSEPORT)

add_compile_options(
    # equivalent of XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES for this directory
    $<$<AND:$<BOOL:${APPLE}>,$<OR:$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:Clang>>,$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>>:-fobjc-arc>)

add_library(${TR_NAME} STATIC)

set(IS_APPLE_CLANG FALSE)
if(APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(IS_APPLE_CLANG TRUE)
endif()

# Generate version.h from version.h.in
configure_file(version.h.in version.h)

# Generate version.h from version.h.in
configure_file(version.h.in version.h)

target_sources(${TR_NAME}
    PRIVATE
        announce-list.cc
        announce-list.h
        announcer-common.h
        announcer-http.cc
        announcer-udp.cc
        announcer.cc
        announcer.h
        local-data.cc
        local-data.h
        api-compat.cc
        api-compat.h
        bandwidth.cc
        bandwidth.h
        benc.h
        bep55-holepunch.cc
        bep55-holepunch.h
        bep55-introducer-store.h
        bitfield.cc
        bitfield.h
        block-info.cc
        block-info.h
        blocklist.cc
        blocklist.h
        clients.cc
        clients.h
        completion.cc
        completion.h
        config-dir-lock.cc
        config-dir-lock.h
        constants.h
        crypto-utils-ccrypto.cc
        crypto-utils-fallback.cc
        crypto-utils-mbedtls.cc
        crypto-utils-openssl.cc
        crypto-utils-wolfssl.cc
        crypto-utils.cc
        crypto-utils.h
        error-types.h
        error.cc
        error.h
        env.cc
        env.h
        file-piece-map.cc
        file-piece-map.h
        file-posix.cc
        file-utils.cc
        file-utils.h
        file-win32.cc
        file.cc
        file.h
        portable-discovery.cc
        portable-discovery.h
        handshake.cc
        handshake.h
        history.h
        inout.cc
        inout.h
        ip-cache.cc
        ip-cache.h
        log.cc
        log.h
        lru-cache.h
        magnet-metainfo.cc
        magnet-metainfo.h
        makemeta.cc
        makemeta.h
        mime-types.h
        net.cc
        net.h
        open-files.cc
        open-files.h
        peer-common.h
        peer-io.cc
        peer-io.h
        peer-mgr-wishlist.cc
        peer-mgr-wishlist.h
        peer-mgr.cc
        peer-mgr.h
        peer-mse.cc
        peer-mse.h
        peer-msgs.cc
        peer-msgs.h
        peer-socket-tcp.cc
        peer-socket-tcp.h
        peer-socket-utp.cc
        peer-socket-utp.h
        peer-socket.cc
        peer-socket.h
        platform.cc
        platform.h
        port-forwarding-upnp.cc
        port-forwarding-upnp.h
        port-forwarding.cc
        port-forwarding.h
        quark.cc
        quark.h
        resume.cc
        resume.h
        rpc-server.cc
        rpc-server.h
        rpcimpl.cc
        rpcimpl.h
        serializer.cc
        serializer.h
        session-alt-speeds.cc
        session-alt-speeds.h
        session-id.cc
        session-id.h
        session-settings.h
        session-thread.cc
        session-thread.h
        session.cc
        session.h
        stats.cc
        stats.h
        string-utils.cc
        string-utils.h
        $<$<BOOL:${IS_APPLE_CLANG}>:string-utils.mm>
        subprocess-posix.cc
        subprocess-win32.cc
        subprocess.h
        timer-ev.cc
        timer-ev.h
        timer.h
        torrent-ctor.cc
        torrent-ctor.h
        torrent-files.cc
        torrent-files.h
        torrent-magnet.cc
        torrent-magnet.h
        torrent-metainfo.cc
        torrent-metainfo.h
        torrent-queue.cc
        torrent-queue.h
        torrent.cc
        torrent.h
        torrents.cc
        torrents.h
        tr-assert.cc
        tr-assert.h
        tr-buffer.h
        tr-dht.cc
        tr-dht.h
        tr-getopt.cc
        tr-getopt.h
        tr-lpd.cc
        tr-lpd.h
        tr-macros.h
        tr-strbuf.h
        tr-udp.cc
        tr-utp.cc
        tr-utp.h
        transmission.h
        types.h
        utils-ev.cc
        utils-ev.h
        utils.cc
        utils.h
        variant-benc.cc
        variant-json.cc
        variant.cc
        variant.h
        verify.cc
        verify.h
        version.h.in
        watchdir-base.h
        watchdir-generic.cc
        watchdir-inotify.cc
        watchdir-kqueue.cc
        watchdir-win32.cc
        watchdir.cc
        watchdir.h
        web-utils.cc
        web-utils.h
        web.cc
        web.h
        webseed.cc
        webseed.h)

# Check for libnatpmp (may not exist in MSYS2 mingw64)
find_path(NATPMP_INCLUDE_DIR natpmp.h
    PATHS "${MSYS2_MINGW64_ROOT}/include"
    NO_DEFAULT_PATH)

if(NATPMP_INCLUDE_DIR)
    message(STATUS "Found libnatpmp: ${NATPMP_INCLUDE_DIR}")
    set(HAVE_NAT_PMP TRUE)
else()
    message(STATUS "libnatpmp not found, disabling NAT-PMP port forwarding")
    set(HAVE_NAT_PMP FALSE)
endif()

# Macro to conditionally add source files based on boolean expressions
# Usage: tr_allow_compile_if(
#   [=[[CONDITION1]]=] src1 src2 ...
#   [=[[CONDITION2]]=] src3 ...
# )
# Conditions are detected by the [=[[...]]=] bracket syntax.
macro(tr_allow_compile_if)
    # Collect all arguments
    set(_args ${ARGN})
    set(_current_condition "")
    set(_current_sources "")
    
    foreach(_arg IN LISTS _args)
        # Check if this argument is a condition (wrapped in [[...]])
        if(_arg MATCHES "^\\[\\[.*\\]\\]$")
            # If we have a previous condition with sources, process it
            if(_current_condition AND _current_sources)
                if(${_current_condition})
                    target_sources(${TR_NAME} PRIVATE ${_current_sources})
                endif()
            endif()
            # Start new condition - extract the inner condition from [[...]]
            string(REGEX MATCH "^\\[\\[(.*)\\]\\]$" _match "${_arg}")
            if(CMAKE_MATCH_1)
                set(_current_condition "${CMAKE_MATCH_1}")
            else()
                message(FATAL_ERROR "tr_allow_compile_if: Failed to parse condition from: ${_arg}")
            endif()
            set(_current_sources "")
        else()
            # This is a source file for the current condition
            if(_current_condition)
                list(APPEND _current_sources "${_arg}")
            else()
                message(FATAL_ERROR "tr_allow_compile_if: Source file '${_arg}' appears before any condition")
            endif()
        endif()
    endforeach()
    
    # Process the last condition
    if(_current_condition AND _current_sources)
        if(${_current_condition})
            target_sources(${TR_NAME} PRIVATE ${_current_sources})
        endif()
    endif()
endmacro()

# Macro to conditionally add source files based on boolean expressions
# Usage: tr_allow_compile_if(
#   [=[[CONDITION1]]=] src1 src2 ...
#   [=[[CONDITION2]]=] src3 ...
# )
# Conditions are detected by the [=[[...]]=] bracket syntax.
macro(tr_allow_compile_if)
    # Collect all arguments
    set(_args ${ARGN})
    set(_current_condition "")
    set(_current_sources "")
    
    foreach(_arg IN LISTS _args)
        # Check if this argument is a condition (wrapped in [=[[...]]=])
        if(_arg MATCHES "^\\[=\\[\\[.*\\]\\]=\\]$")
            # If we have a previous condition with sources, process it
            if(_current_condition AND _current_sources)
                if(${_current_condition})
                    target_sources(${TR_NAME} PRIVATE ${_current_sources})
                endif()
            endif()
            # Start new condition - extract the inner condition from [=[[...]]=]
            string(REGEX MATCH "^\\[=\\[\\[(.*)\\]\\]=\\]$" _match "${_arg}")
            if(CMAKE_MATCH_1)
                set(_current_condition "${CMAKE_MATCH_1}")
            else()
                message(FATAL_ERROR "tr_allow_compile_if: Failed to parse condition from: ${_arg}")
            endif()
            set(_current_sources "")
        else()
            # This is a source file for the current condition
            if(_current_condition)
                list(APPEND _current_sources "${_arg}")
            else()
                message(FATAL_ERROR "tr_allow_compile_if: Source file '${_arg}' appears before any condition")
            endif()
        endif()
    endforeach()
    
    # Process the last condition
    if(_current_condition AND _current_sources)
        if(${_current_condition})
            target_sources(${TR_NAME} PRIVATE ${_current_sources})
        endif()
    endif()
endmacro()

# Macro to conditionally add source files based on boolean expressions
# Usage: tr_allow_compile_if(
#   [=[[CONDITION1]]=] src1 src2 ...
#   [=[[CONDITION2]]=] src3 ...
# )
# Conditions are detected by the [=[[...]]=] bracket syntax.
macro(tr_allow_compile_if)
    # Collect all arguments
    set(_args ${ARGN})
    set(_current_condition "")
    set(_current_sources "")
    
    foreach(_arg IN LISTS _args)
        # Check if this argument is a condition (wrapped in [=[[...]]=])
        if(_arg MATCHES "^\\[=\\[\\[.*\\]\\]=\\]$")
            # If we have a previous condition with sources, process it
            if(_current_condition AND _current_sources)
                if(${_current_condition})
                    target_sources(${TR_NAME} PRIVATE ${_current_sources})
                endif()
            endif()
            # Start new condition - extract the inner condition from [=[[...]]=]
            string(REGEX MATCH "^\\[=\\[\\[(.*)\\]\\]=\\]$" _match "${_arg}")
            if(CMAKE_MATCH_1)
                set(_current_condition "${CMAKE_MATCH_1}")
            else()
                message(FATAL_ERROR "tr_allow_compile_if: Failed to parse condition from: ${_arg}")
            endif()
            set(_current_sources "")
        else()
            # This is a source file for the current condition
            if(_current_condition)
                list(APPEND _current_sources "${_arg}")
            else()
                message(FATAL_ERROR "tr_allow_compile_if: Source file '${_arg}' appears before any condition")
            endif()
        endif()
    endforeach()
    
    # Process the last condition
    if(_current_condition AND _current_sources)
        if(${_current_condition})
            target_sources(${TR_NAME} PRIVATE ${_current_sources})
        endif()
    endif()
endmacro()

tr_allow_compile_if(
    [[TRUE]]
        crypto-utils-fallback.cc
    [[CRYPTO_PKG STREQUAL "ccrypto"]]
        crypto-utils-ccrypto.cc
    [=[[CRYPTO_PKG STREQUAL "mbedtls"]]=]
        crypto-utils-mbedtls.cc
    [=[[CRYPTO_PKG STREQUAL "openssl"]]=]
        crypto-utils-openssl.cc
    [=[[CRYPTO_PKG STREQUAL "wolfssl"]]=]
        crypto-utils-wolfssl.cc
    [=[[WITH_INOTIFY]]=]
        watchdir-inotify.cc
    [=[[WITH_KQUEUE]]=]
        watchdir-kqueue.cc
    [=[[WIN32]]=]
        file-win32.cc
        subprocess-win32.cc
        watchdir-win32.cc
    [=[[NOT WIN32]]=]
        file-posix.cc
        subprocess-posix.cc
    [=[[HAVE_NAT_PMP]]=]
        port-forwarding-natpmp.cc
)

target_compile_definitions(${TR_NAME}
    PRIVATE
        __TRANSMISSION__
        WIDE_INTEGER_DISABLE_FLOAT_INTEROP
        WIDE_INTEGER_DISABLE_IOSTREAM
        PACKAGE_DATA_DIR="${CMAKE_INSTALL_FULL_DATAROOTDIR}"
        $<$<BOOL:${WITH_INOTIFY}>:WITH_INOTIFY>
        $<$<BOOL:${WITH_KQUEUE}>:WITH_KQUEUE>
        $<$<BOOL:${ENABLE_UTP}>:WITH_UTP>
        $<$<BOOL:${USE_SYSTEM_B64}>:USE_SYSTEM_B64>
        $<$<BOOL:${HAVE_SO_REUSEPORT}>:HAVE_SO_REUSEPORT=1>
        $<$<BOOL:${HAVE_NAT_PMP}>:HAVE_NAT_PMP=1>
        # NAS profile: configurable block size and max open files
        $<$<BOOL:${TR_NAS_PROFILE}>:TR_BLOCK_SIZE=65536>
        $<$<BOOL:${TR_NAS_PROFILE}>:TR_MAX_OPEN_FILES=512>
        $<$<NOT:$<BOOL:${TR_NAS_PROFILE}>>:TR_BLOCK_SIZE=16384>
        $<$<NOT:$<BOOL:${TR_NAS_PROFILE}>>:TR_MAX_OPEN_FILES=128>
    PUBLIC
        $<$<STREQUAL:${CRYPTO_PKG},ccrypto>:WITH_CCRYPTO>
        $<$<STREQUAL:${CRYPTO_PKG},mbedtls>:WITH_MBEDTLS>
        $<$<STREQUAL:${CRYPTO_PKG},openssl>:WITH_OPENSSL>
        $<$<STREQUAL:${CRYPTO_PKG},wolfssl>:WITH_WOLFSSL>
        $<$<BOOL:${Intl_FOUND}>:ENABLE_GETTEXT>)

tr_target_compile_definitions_for_functions(${TR_NAME}
    PRIVATE
        copyfile
        copy_file_range
        fallocate64
        flock
        htonll
        mkdtemp
        ntohll
        posix_fadvise
        posix_fallocate
        pread
        pwrite
        sendfile64)

target_include_directories(${TR_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_BINARY_DIR}/..
        ${Intl_INCLUDE_DIRS})

if(ANDROID)
    find_library(log-lib log)
endif()

target_link_libraries(${TR_NAME}
    PRIVATE
        Threads::Threads
        libdeflate::libdeflate
        CURL::libcurl
        FastFloat::fast_float
        psl::psl
        $<$<BOOL:${HAVE_NAT_PMP}>:natpmp::natpmp>
        miniupnpc::miniupnpc
        dht::dht
        $<$<BOOL:${ENABLE_UTP}>:libutp::libutp>
        libb64::libb64
        ${Intl_LIBRARIES}
        ${LIBM_LIBRARY}
        ${LIBQUOTA_LIBRARY}
        ${TR_NETWORK_LIBRARIES}
        RapidJSON
        utf8::cpp
        wildmat
        transmission::WideInteger
        MadlerCrcany
        $<$<BOOL:${WIN32}>:crypt32>
        $<$<BOOL:${WIN32}>:shlwapi>
        "$<$<BOOL:${APPLE}>:-framework Foundation>"
        "$<$<BOOL:${ANDROID}>:${log-lib}>"
        $<$<BOOL:${HAVE_LIBATOMIC}>:atomic>
    PUBLIC
        transmission::crypto_impl
        transmission::fmt-header-only
        Pal::Sigslot
        transmission::small
        libevent::core
        libevent::extra)

if(INSTALL_LIB)
    install(
        TARGETS ${TR_NAME}
        DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(
        FILES
            error-types.h
            error.h
            file.h
            log.h
            makemeta.h
            quark.h
            rpcimpl.h
            session-id.h
            timer-ev.h
            timer.h
            tr-assert.h
            tr-buffer.h
            tr-getopt.h
            tr-macros.h
            tr-strbuf.h
            transmission.h
            utils.h
            values.h
            variant.h
            watchdir.h
            web-utils.h
            web.h
            portable-discovery.h
            ${CMAKE_CURRENT_BINARY_DIR}/version.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${TR_NAME})
endif()
