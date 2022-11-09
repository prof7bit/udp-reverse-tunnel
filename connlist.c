#include "connlist.h"

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "misc.h"

conn_entry_t* conn_table = NULL;

static unsigned count = 0;

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
    ++count;
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
    --count;
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
    while (p != NULL) {
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
    while (p != NULL) {
        if (memcmp(&p->addr_tunnel, addr, sizeof(struct sockaddr_in)) == 0) {
            return p;
        }
        p = p->next;
    }
    return NULL;
}

/**
 * return the next best table entry that has the spare flag set,
 * return NULL if no such entry exists.
 */
conn_entry_t* conn_table_find_next_spare(void) {
    conn_entry_t* p = conn_table;
    while (p != NULL) {
        if (p->spare) {
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
 * @param clean_spares should spare entries also be cleaned
 */
void conn_table_clean(unsigned max_age, bool clean_spares) {
    conn_entry_t* e = conn_table;
    bool changed = false;
    uint64_t time = millisec();
    while (e != NULL) {
        conn_entry_t* next = e->next;
        if ((time - e->last_acticity > max_age * 1000) && (clean_spares || !e->spare)) {
            print(LOG_DEBUG, "removing connection");
            conn_table_remove(e);
            changed = true;
        }
        e = next;
    }
    if (changed) {
        conn_print_numbers();
    }
}

unsigned conn_count() {
    return count;
}

unsigned conn_spare_count() {
    conn_entry_t* e = conn_table;
    unsigned cnt = 0;
    while (e) {
        if (e->spare) {
            ++cnt;
        }
        e = e->next;
    }
    return cnt;
}

unsigned conn_socket_count() {
    conn_entry_t* e = conn_table;
    unsigned cnt = 0;
    while (e) {
        if (e->sock_service) {
            ++cnt;
        }
        if (e->sock_tunnel) {
            ++cnt;
        }
        e = e->next;
    }
    return cnt;
}

void conn_print_numbers() {
    unsigned spare = conn_spare_count();
    print(LOG_DEBUG, "Total: %d, active: %d, spare: %d", count, count - spare, spare);
}
