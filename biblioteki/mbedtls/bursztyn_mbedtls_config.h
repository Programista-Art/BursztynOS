/*
 * Minimalna konfiguracja mbedTLS dla Bursztyn OS (Bare-Metal).
 */
#pragma once
#include <stddef.h>

#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_NO_STD_FUNCTIONS

#define MBEDTLS_MEMORY_BUFFER_ALLOC_C

#undef MBEDTLS_FS_IO
#undef MBEDTLS_NET_C
#undef MBEDTLS_TIMING_C
#undef MBEDTLS_HAVE_TIME
#undef MBEDTLS_HAVE_TIME_DATE

#undef MBEDTLS_PSA_CRYPTO_C
#undef MBEDTLS_USE_PSA_CRYPTO
#undef MBEDTLS_PSA_CRYPTO_STORAGE_C
#undef MBEDTLS_PSA_ITS_FILE_C

// KRYTYCZNA POPRAWKA: Wyłączenie sprzętowej akceleracji
#undef MBEDTLS_AESNI_C
#undef MBEDTLS_PADLOCK_C

#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT
#define MBEDTLS_NO_DEFAULT_ENTROPY_SOURCES

#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
#define MBEDTLS_SSL_KEEP_PEER_CERTIFICATE
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_MD_C
#define MBEDTLS_RSA_C
#define MBEDTLS_AES_C
#define MBEDTLS_GCM_C
#define MBEDTLS_CIPHER_MODE_CBC
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_RSA_ENABLED
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_OID_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_SHA1_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA512_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_PK_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_X509_USE_C

// POPRAWKA BŁĘDÓW "implicit declaration"
#ifdef __cplusplus
extern "C" {
#endif
int snprintf_dummy(char * s, size_t n, const char * format, ...);
int printf_dummy(const char * format, ...);
void exit_dummy(int status);
#ifdef __cplusplus
}
#endif

#define MBEDTLS_PLATFORM_SNPRINTF_MACRO  snprintf_dummy
#define MBEDTLS_PLATFORM_PRINTF_MACRO    printf_dummy
#define MBEDTLS_PLATFORM_EXIT_MACRO      exit_dummy
