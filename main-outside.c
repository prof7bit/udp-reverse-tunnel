#include "main-outside.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "connlist.h"
#include "mac.h"
#include "misc.h"
#include "defines.h"

void run_outside(unsigned port) {
    int sockfd;
    char buffer[BUF_SIZE];
    struct sockaddr_in addr_own = {0};
    struct sockaddr_in addr_incoming = {0};

    printf("<6> UDP tunnel outside agent\n");

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("<3> socket creation failed");
        exit(EXIT_FAILURE);
    }

    addr_own.sin_family = AF_INET;
    addr_own.sin_addr.s_addr = INADDR_ANY;
    addr_own.sin_port = htons(port);

    if (bind(sockfd, (const struct sockaddr *)&addr_own, sizeof(addr_own)) < 0) {
        perror("<3> bind failed");
        exit(EXIT_FAILURE);
    }

    printf("<6> listening on port %d\n", port);

    socklen_t len_addr = sizeof(addr_incoming);
    size_t nbytes;

    while ("my guitar gently weeps") {
        nbytes = recvfrom(sockfd, buffer, BUF_SIZE, MSG_WAITALL, (struct sockaddr*) &addr_incoming, &len_addr);

        // the keepalive datagram from the inside agent is a 40 byte message authentication code
        // for an empty message with a strictly increasing nonce, each code can only be used
        // exactly once) to prevent replay attacks. This datagram is used to learn the public
        // address and port of the inside agent.
        if (nbytes == sizeof(mac_t)) {
            mac_t mac;
            memcpy(&mac, buffer, sizeof(mac_t));
            if (mac_test(NULL, 0, mac)) {
                // We could successfully verify the authentication code, we know this datagram
                // originates from the inside agent and we can store the source address.
                // From this moment on we know where to forward the client datagrams.
                conn_entry_t* conn = conn_table_find_tunnel_address(&addr_incoming);
                if (!conn) {
                    printf("<6> new incoming reverse tunnel from: %s:%d\n", inet_ntoa(addr_incoming.sin_addr), addr_incoming.sin_port);
                    conn = conn_table_insert();
                    memcpy(&conn->addr_tunnel, &addr_incoming, len_addr);
                    conn->spare = true;
                    conn_print_numbers();
                }
                conn->last_acticity = millisec();
                conn_table_clean(KEEPALIVE_SECONDS + 5, true); // periodic cleaning of stale entries
                continue;
            }
        }

        // Test whether this originates from the inside agent. All possible inside agent
        // tunnel addresses must be present in our connection table.
        conn_entry_t* conn = conn_table_find_tunnel_address(&addr_incoming);
        if (conn) {
            sendto(sockfd, buffer, nbytes, 0, (struct sockaddr*)&conn->addr_client, len_addr);
            continue;
        }

        // This is not from one of the known tunnel addresses, so it must be from a client.
        conn = conn_table_find_client_address(&addr_incoming);
        if (conn == NULL) {
            printf("<6> new client conection from %s:%d\n", inet_ntoa(addr_incoming.sin_addr), addr_incoming.sin_port);

            // now try to find a spare tunnel for this new client and activate it
            conn = conn_table_find_next_spare();
            if (conn) {
                conn->spare = false;
                memcpy(&conn->addr_client, &addr_incoming, len_addr);
            }
        }

        // if we have a tunnel conection for this client then we can forward it to the inside
        if (conn) {
            sendto(sockfd, buffer, nbytes, 0, (struct sockaddr*)&conn->addr_tunnel, len_addr);
        } else {
            printf("<4> could not find tunnel connection for client, dropping package\n");
        }
    }
}


