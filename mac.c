#include "mac.h"

#include <stdlib.h>
#include <string.h>
#include "sha-256.h"

static uint64_t last_nonce = 0;
static size_t secret_len = 0;
static char* secret = NULL;

void mac_init(const char* sec, size_t seclen) {
    if ((sec == NULL) || (seclen == 0)) {
        sec = NULL;
        seclen = 0;
    }
    if (secret_len) {
        free(secret);
    }
    secret_len = seclen;
    if (seclen) {
        secret = malloc(seclen);
        memcpy(secret, sec, seclen);
    } else {
        secret = NULL;
    }
    last_nonce = 0;
}

mac_t mac_gen(const char* msg, size_t msglen, uint64_t nonce) {
    mac_t mac;
    if ((msg == NULL) || (msglen == 0)) {
        msg = NULL;
        msglen = 0;
    }
    char* buf = malloc(secret_len + msglen + sizeof(nonce));
    memcpy(buf, secret, secret_len);
    memcpy(buf + secret_len, msg, msglen);
    memcpy(buf + secret_len + msglen, &nonce, sizeof(nonce));
    calc_sha_256(mac.hash, buf, secret_len + msglen + sizeof(nonce));
    mac.nonce = nonce;
    free(buf);
    return mac;
}

bool mac_test(const char* msg, size_t msglen, mac_t mac) {
    if (mac.nonce > last_nonce) {
        mac_t own_mac = mac_gen(msg, msglen, mac.nonce);
        if (memcmp(&own_mac, &mac, sizeof(mac_t)) == 0) {
            last_nonce = mac.nonce;
            return true;
        }
    }
    return false;
}
