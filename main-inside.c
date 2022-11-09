#include "main-inside.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/poll.h>

#include "connlist.h"
#include "mac.h"
#include "misc.h"
#include "defines.h"

void run_inside(args_parsed_t args) {
    ssize_t nbytes;
    struct sockaddr_in addr_outside = {0};
    struct sockaddr_in addr_service = {0};
    struct sockaddr_in addr_incoming = {0};
    struct hostent* he;
    socklen_t len_addr = sizeof(struct sockaddr_in);
    struct pollfd* pfds = NULL;
    char buffer[BUF_SIZE];


    print(LOG_INFO, "UDP tunnel inside agent v" VERSION_STR);
    print(LOG_INFO, "building tunnels to outside agent at %s, port %d", args.outside_host, args.outside_port);
    print(LOG_INFO, "forwarding incomimg UDP to %s, port %d", args.service_host, args.service_port);

    if ((he = gethostbyname(args.outside_host)) == NULL) {
        print_e(LOG_ERROR, "outside host name '%s' could not be resolved", args.outside_host);
        exit(EXIT_FAILURE);
    }

    memcpy(&addr_outside.sin_addr, he->h_addr_list[0], he->h_length);
    addr_outside.sin_family = AF_INET;
    addr_outside.sin_port = htons(args.outside_port);

    if ((he = gethostbyname(args.service_host)) == NULL) {
        print_e(LOG_ERROR, "srvice host name '%s' could not be resolved", args.service_host);
        exit(EXIT_FAILURE);
    }

    memcpy(&addr_service.sin_addr, he->h_addr_list[0], he->h_length);
    addr_service.sin_family = AF_INET;
    addr_service.sin_port = htons(args.service_port);

    // we start out with one unused spare tunnel
    conn_entry_t* spare_conn = conn_table_insert();
    print(LOG_INFO, "creating initial outgoing tunnel");
    spare_conn->spare = true;
    spare_conn->sock_tunnel = socket(AF_INET, SOCK_DGRAM, 0);
    if (spare_conn->sock_tunnel < 0) {
        print_e(LOG_ERROR, "could not create new UDP socket for tunnel");
        exit(EXIT_FAILURE);
    }

    while ("my guitar gently weeps") {

        int count_sock = conn_socket_count();
        pfds = realloc(pfds, count_sock * sizeof(struct pollfd));
        int idx = 0;
        conn_entry_t* e = conn_table;
        while (e) {
            if (e->sock_service) {
                e->sock_service_pollidx = idx;
                pfds[idx].events = POLLIN;
                pfds[idx].fd = e->sock_service;
                ++idx;
            }
            if (e->sock_tunnel) {
                e->sock_tunnel_pollidx = idx;
                pfds[idx].events = POLLIN;
                pfds[idx].fd = e->sock_tunnel;
                ++idx;
            }
            e = e->next;
        }

        if (poll(pfds, count_sock, 100) < 0) {
            print_e(LOG_ERROR, "poll returned error");
            exit(EXIT_FAILURE);
        };

        e = conn_table;
        while (e) {

            // check all the sockets facing towards the service host
            if (e->sock_service > 0) {
                if (pfds[e->sock_service_pollidx].revents & POLLIN) {
                    nbytes = recvfrom(e->sock_service, buffer, BUF_SIZE, 0, (struct sockaddr*) &addr_incoming, &len_addr);
                    if (e->sock_tunnel > 0) {
                        sendto(e->sock_tunnel, buffer, nbytes, 0, (struct sockaddr*)&addr_outside, len_addr);
                    }
                }
            }

            // check all the sockets facing towards the tunnel outside agent
            if (e->sock_tunnel > 0) {
                if (pfds[e->sock_tunnel_pollidx].revents & POLLIN) {
                    nbytes = recvfrom(e->sock_tunnel, buffer, BUF_SIZE, 0, (struct sockaddr*) &addr_incoming, &len_addr);
                    if (e->spare) {
                        // this came in on one of the spare connections
                        // remove the spare status and create a socket to use it
                        print(LOG_INFO, "new client data arrived on spare tunnel, creating socket for it");
                        e->spare = false;
                        e->sock_service = socket(AF_INET, SOCK_DGRAM, 0);
                        if (e->sock_service < 0) {
                            print_e(LOG_ERROR, "could not create new UDP socket for service");
                            exit(EXIT_FAILURE);
                        }

                        // and immediately create another new spare connection
                        print(LOG_DEBUG, "creating new outgoing spare tunnel");
                        conn_entry_t* spare_conn = conn_table_insert();
                        spare_conn->spare = true;
                        spare_conn->sock_tunnel = socket(AF_INET, SOCK_DGRAM, 0);
                        if (e->sock_tunnel < 0) {
                            print_e(LOG_ERROR, "could not create UDP socket for new spare connectionl");
                            exit(EXIT_FAILURE);
                        }

                        conn_print_numbers();
                    }

                    if (e->sock_service > 0) {
                        sendto(e->sock_service, buffer, nbytes, 0, (struct sockaddr*)&addr_service, len_addr);
                        e->last_acticity = millisec();
                    }
                }
            }

            e = e->next;
        }

        // in regular intervals we need to send a keepalive datagram to the outside agent. This has the
        // purpose of punching a hole into the NAT and keeping it open, and it also tells the outside
        // agent the public address and port of that hole, so it can send datagrams back to the inside.
        e = conn_table;
        uint64_t ms = millisec();
        while (e) {
            if (e->sock_tunnel > 0) {
                if (ms - e->last_keepalive > args.keepalive * 1000) {
                    e->last_keepalive = ms;

                    // the keepalive datagram is a 40 byte message authentication code, based on the sha-256 over
                    // a strictly increasing nonce and a pre shared secret (the -k argument). This is done to
                    // prevent spoofing of the keepalive datagrams by an attacker.
                    mac_t mac = mac_gen(NULL, 0, ms);
                    sendto(e->sock_tunnel, &mac, sizeof(mac), 0, (struct sockaddr*)&addr_outside, len_addr);
                    break; // only send one keepalive per select iteration to spread them out in time
                }
            }
            e = e->next;
        }

        // remove any stale inactive connections from the connection table and close their sockets.
        conn_table_clean(CONN_LIFETIME_SECONDS, false);
    }
}

