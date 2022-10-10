#include <stdint.h>
#include <arpa/inet.h>

#ifndef CONNLIST_H
#define CONNLIST_H

typedef struct conn_entry conn_entry_t;
struct conn_entry {
    struct sockaddr_in addr;
    int sockfd;
    conn_entry_t* prev;
    conn_entry_t* next;
    time_t time;
    uint8_t id;
};
 
extern conn_entry_t* conn_table;

conn_entry_t* conn_table_insert(void);
void conn_table_remove(conn_entry_t* entry);
conn_entry_t* conn_table_find_id(uint8_t id);
conn_entry_t* nat_table_find_address(struct sockaddr_in* addr);
void conn_table_clean(time_t max_age);

#endif // CONNLIST_H
