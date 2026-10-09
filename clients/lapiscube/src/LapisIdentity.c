#include "LapisIdentity.h"
#if !defined(__cplusplus) && (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L)
#define inline __inline
#endif
#include "../third_party/bearssl/bearssl_hash.h"
#include <string.h>

/* MD5 here is UUIDv3 interoperability, never authentication or a signature. */
void LapisIdentity_OfflineUUID(const char* username, cc_uint8 uuid[16]) {
    br_md5_context ctx;
    br_md5_init(&ctx);
    br_md5_update(&ctx, "OfflinePlayer:", 14);
    br_md5_update(&ctx, username, strlen(username));
    br_md5_out(&ctx, uuid);
    uuid[6] = (cc_uint8)((uuid[6] & 15) | 0x30);
    uuid[8] = (cc_uint8)((uuid[8] & 63) | 0x80);
}
