cmake_minimum_required(VERSION 3.20)

project(Transmission LANGUAGES C CXX)

set(TR_NAME ${PROJECT_NAME})

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
#   [[CONDITION1]] src1 src2 ...
#   [[CONDITION2]] src3 ...
# )
# Conditions are detected by the [[...]] bracket syntax.
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

tr_allow_compile_if(
    [=[[TRUE]]=]
        crypto-utils-fallback.cc
    [=[[CRYPTO_PKG STREQUAL "ccrypto"]]=]
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
