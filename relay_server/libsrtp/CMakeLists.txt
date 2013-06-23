
cmake_minimum_required(VERSION 2.8)

project(libsrtp)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/crypto/include
)

file(GLOB SRTP_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    srtp/*.c
    include/*.h
)

set(CRYPTO_SOURCES
    crypto/cipher/aes.c
    crypto/cipher/aes_cbc.c
    crypto/cipher/aes_icm.c
    crypto/kernel/alloc.c
    crypto/hash/auth.c
    crypto/cipher/cipher.c
    crypto/kernel/crypto_kernel.c
    crypto/rng/ctr_prng.c
    crypto/math/datatypes.c
    crypto/kernel/err.c
    crypto/math/gf2_8.c
    crypto/hash/hmac.c
    crypto/kernel/key.c
    crypto/hash/null_auth.c
    crypto/cipher/null_cipher.c
    crypto/rng/prng.c
    crypto/rng/rand_source.c
    crypto/replay/rdb.c
    crypto/replay/rdbx.c
    crypto/hash/sha1.c
    crypto/math/stat.c
    crypto/replay/ut_sim.c
)

add_library(libsrtp STATIC ${SRTP_SOURCES} ${CRYPTO_SOURCES})
