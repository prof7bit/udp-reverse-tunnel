#include "connlist.h"

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

conn_entry_t* conn_table = NULL;

/**
 * insert an entry to the connection table. It will allocate new memory
 * on the heap, insert it into the linked list and return a pointer
 * to the newly created entry.
 *
 * @return pointer to the entry in the list on the heap
 */
conn_entry_t* conn_table_insert(void) {
    conn_entry_t* e = malloc(sizeof(conn_entry_t));
    memset(e, 0, sizeof(conn_entry_t));
    e->next = conn_table;
    if (e->next != NULL) {
        e->next->prev = e;
    }
    e->prev = NULL;
    conn_table = e;
    return e;
}

/**
 * remove an entry from the connection table.
 * The entry will also be freed from the heap.
 *
 * @param entry pointer to the entry to be removed
 */
void conn_table_remove(conn_entry_t* entry) {
    if (entry->prev != NULL) {
        entry->prev->next = entry->next;
    } else {
        conn_table = entry->next;
    }
    if (entry->next != NULL) {
        entry->next->prev = entry->prev;
    }
    if (entry->sock_service > 0) {
        close(entry->sock_service);
    }
    if (entry->sock_tunnel > 0) {
        close(entry->sock_tunnel);
    }
    free(entry);
}

/**
 * find the connection table entry by its connection id. If not found
 * it will return NULL.
 *
 * @param id connection id
 * @return pointer to connection table entry or NULL
 */
conn_entry_t* conn_table_find_id(uint8_t id) {
    conn_entry_t* p = conn_table;
    while(p != NULL) {
        if (p->id == id) {
            return p;
        }
        p = p->next;
    }
    return NULL;
}

/**
 * find the connection table entry by its client address. It will compare
 * the entire sockaddr struct (port and address) to identify the
 * entry. If not found it will return NULL.
 *
 * @param addr pointer to sockaddr struct
 * @return pointer to connection table entry or NULL
 */
conn_entry_t* conn_table_find_client_address(struct sockaddr_in* addr) {
    conn_entry_t* p = conn_table;
    while(p != NULL) {
        if (memcmp(&p->addr_client, addr, sizeof(struct sockaddr_in)) == 0) {
            return p;
        }
        p = p->next;
    }
    return NULL;
}

/**
 * find the connection table entry by its tunnel address. It will compare
 * the entire sockaddr struct (port and address) to identify the
 * entry. If not found it will return NULL.
 *
 * @param addr pointer to sockaddr struct
 * @return pointer to connection table entry or NULL
 */
conn_entry_t* conn_table_find_tunnel_address(struct sockaddr_in* addr) {
    conn_entry_t* p = conn_table;
    while(p != NULL) {
        if (memcmp(&p->addr_tunnel, addr, sizeof(struct sockaddr_in)) == 0) {
            return p;
        }
        p = p->next;
    }
    return NULL;
}

/**
 * check all connection table entries for their last usage time and remove
 * all entries that have been inactive for longer than the defined lifetime.
 * This function is meant to be called periodically every few seconds.
 *
 * @param max_age inactivity time in seconds
 */
void conn_table_clean(time_t max_age) {
    conn_entry_t* p = conn_table;
    while(p != NULL) {
        if (time(NULL) - p->time > max_age) {
            conn_entry_t* next = p->next;
            printf("<6> removing unused connection %d\n", p->id);
            conn_table_remove(p);
            p = next;
        } else {
            p = p->next;
        }
    }
}
