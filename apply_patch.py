import re
with open('C:/Users/swiig/Documents/transmission-main/libtransmission/CMakeLists.txt', 'r') as f:
    content = f.read()

old = 'check_symbol_exists(SO_REUSEPORT "sys/types.h;sys/socket.h" HAVE_SO_REUSEPORT)'
new = '''# dht target (third-party, built from source)
if(NOT TARGET dht::dht)
    add_library(dht::dht INTERFACE IMPORTED)
    target_include_directories(dht::dht INTERFACE "/../third-party/dht")
endif()

# CURL target (from MSYS2)
if(NOT TARGET CURL::libcurl)
    add_library(CURL::libcurl INTERFACE IMPORTED)
    target_include_directories(CURL::libcurl INTERFACE "/include")
    target_link_libraries(CURL::libcurl INTERFACE "/lib/libcurl.dll.a")
endif()

# OpenSSL target (for libevent::openssl and crypto)
if(NOT TARGET OpenSSL::SSL)
    add_library(OpenSSL::SSL INTERFACE IMPORTED)
    target_include_directories(OpenSSL::SSL INTERFACE "/include")
    target_link_libraries(OpenSSL::SSL INTERFACE "/lib/libssl.dll.a")
endif()
if(NOT TARGET OpenSSL::Crypto)
    add_library(OpenSSL::Crypto INTERFACE IMPORTED)
    target_include_directories(OpenSSL::Crypto INTERFACE "/include")
    target_link_libraries(OpenSSL::Crypto INTERFACE "/lib/libcrypto.dll.a")
endif()

check_symbol_exists(SO_REUSEPORT "sys/types.h;sys/socket.h" HAVE_SO_REUSEPORT)'''

content = content.replace(old, new)
with open('C:/Users/swiig/Documents/transmission-main/libtransmission/CMakeLists.txt', 'w') as f:
    f.write(content)
print('Done')