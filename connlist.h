#include <stdint.h>
#include <stdbool.h>
#include <arpa/inet.h>

#ifndef CONNLIST_H
#define CONNLIST_H

typedef struct conn_entry conn_entry_t;
struct conn_entry {
    struct sockaddr_in addr_client;
    struct sockaddr_in addr_tunnel;
    int sock_service;
    int sock_service_pollidx;
    int sock_tunnel;
    int sock_tunnel_pollidx;
    conn_entry_t* prev;
    conn_entry_t* next;
    bool spare;
    uint64_t last_keepalive;
    uint64_t last_acticity;
};

extern conn_entry_t* conn_table;

conn_entry_t* conn_table_insert(void);
void conn_table_remove(conn_entry_t* entry);
conn_entry_t* conn_table_find_client_address(struct sockaddr_in* addr);
conn_entry_t* conn_table_find_tunnel_address(struct sockaddr_in* addr);
conn_entry_t* conn_table_find_next_spare(void);
void conn_table_clean(unsigned max_age, bool clean_spares);
unsigned conn_count();
unsigned conn_spare_count();
unsigned conn_socket_count();
void conn_print_numbers();

#endif // CONNLIST_H
