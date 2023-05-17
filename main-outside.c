#include "main-outside.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "connlist.h"
#include "mac.h"
#include "misc.h"
#include "defines.h"

void run_outside(args_parsed_t args) {
    int sockfd;
    char buffer[BUF_SIZE];
    struct sockaddr_in addr_own = {0};
    struct sockaddr_in addr_incoming = {0};
    uint64_t time_last_cleanup = 0;
    bool log_client_connections = true;

    print(LOG_INFO, "UDP tunnel outside agent v" VERSION_STR);

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        print_e(LOG_ERROR, "socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500 * 1000;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    addr_own.sin_family = AF_INET;
    addr_own.sin_addr.s_addr = INADDR_ANY;
    addr_own.sin_port = htons(args.listenport);

    if (bind(sockfd, (const struct sockaddr *)&addr_own, sizeof(addr_own)) < 0) {
        print_e(LOG_ERROR, "binding to port %d failed", args.listenport);
        exit(EXIT_FAILURE);
    }

    print(LOG_INFO, "listening on port %d", args.listenport);

    socklen_t len_addr = sizeof(addr_incoming);

    while ("my guitar gently weeps") {
        ssize_t nbytes = recvfrom(sockfd, buffer, BUF_SIZE, MSG_WAITALL, (struct sockaddr*) &addr_incoming, &len_addr);
        if (nbytes > 0) {
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
                        print(LOG_DEBUG, "new incoming reverse tunnel from: %s:%d", inet_ntoa(addr_incoming.sin_addr), addr_incoming.sin_port);
                        conn = conn_table_insert();
                        memcpy(&conn->addr_tunnel, &addr_incoming, len_addr);
                        conn->spare = true;
                        conn_print_numbers();
                        log_client_connections = true;
                    }
                    conn->last_acticity = millisec();
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
                if (log_client_connections) {
                    print(LOG_INFO, "new client conection from %s:%d", inet_ntoa(addr_incoming.sin_addr), addr_incoming.sin_port);
                }

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
                if (log_client_connections) {
                    print(LOG_WARN, "could not find tunnel connection for client, dropping package");
                    print(LOG_DEBUG, "will not repeat above warning until inside agent connects again");
                    log_client_connections = false;
                }
            }
        }

        uint64_t ms = millisec();
        if (ms - time_last_cleanup > 1000) {
            time_last_cleanup = ms;
            conn_table_clean(args.keepalive + 10, true); // periodic cleaning of stale entries
        }
    }
}
