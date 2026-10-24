/* host stub: a stand-in mixer with mbedTLS's SHA-256 interface. The test
   checks mac_entropy.c's flow, not the hash (see test_mac_entropy.c). */
#pragma once
#include <stddef.h>
typedef struct { unsigned long long h; } mbedtls_sha256_context;
void mbedtls_sha256_init(mbedtls_sha256_context *ctx);
int  mbedtls_sha256_starts(mbedtls_sha256_context *ctx, int is224);
int  mbedtls_sha256_update(mbedtls_sha256_context *ctx, const unsigned char *in, size_t len);
int  mbedtls_sha256_finish(mbedtls_sha256_context *ctx, unsigned char out[32]);
void mbedtls_sha256_free(mbedtls_sha256_context *ctx);
