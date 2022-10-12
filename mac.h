#ifndef MAC_H
#define MAC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    uint64_t nonce;
    uint8_t hash[32];
} mac_t;

void mac_init(const char* sec, size_t seclen);
mac_t mac_gen(const char* msg, size_t msglen, uint64_t nonce);
bool mac_test(const char* msg, size_t msglen, mac_t mac);

#endif
