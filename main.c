/**
 * @file main.c
 * @author Bernd Kreuss
 * @date 7 Oct 2022
 * @brief UDP reverse tunnel
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <time.h>
#include <netdb.h>
#include <sys/select.h>

#include "args.h"
#include "connlist.h"
#include "mac.h"
#include "misc.h"

#define CONN_LIFETIME_SECONDS   60
#define BUF_SIZE                0xffff
#define KEEPALIVE_SECONDS       10

static void run_outside(unsigned port) {
    int sockfd;
    char buffer[BUF_SIZE + 1];
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
                }
                conn->last_acticity = millisec();
                conn_table_clean(CONN_LIFETIME_SECONDS, true); // periodic cleaning of stale entries
                continue;
            }
        }

        // Test whether this originates from the inside agent. All possible inside agent
        // tunnel addresses must be present in our connection table.
        conn_entry_t* conn = conn_table_find_tunnel_address(&addr_incoming);
        if(conn) {
            sendto(sockfd, buffer, nbytes, 0, (struct sockaddr*)&conn->addr_client, len_addr);
            conn->last_acticity = millisec();
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
            conn->last_acticity = millisec();
        } else {
            printf("<4> could not find tunnel connection for client, dropping package\n");
        }
    }
}

static void run_inside(char* outsude_host, int outside_port, char* service_host, int service_port) {
    fd_set sock_set;
    struct timeval tv;
    int fd_max;
    int result;
    size_t nbytes;
    struct sockaddr_in addr_outside = {0};
    struct sockaddr_in addr_service = {0};
    struct sockaddr_in addr_incoming = {0};
    struct hostent* he;
    socklen_t len_addr = sizeof(struct sockaddr_in);
    char buffer[BUF_SIZE + 1];

    printf("<6> UDP tunnel inside agent\n");
    printf("<6> trying to contact outside agent at %s, port %d\n", outsude_host, outside_port);
    printf("<6> forwarding incomimg UDP to %s, port %d\n", service_host, service_port);

    if ((he = gethostbyname(outsude_host)) == NULL) {
        perror("<3> outside host name could not be resolved");
        exit(EXIT_FAILURE);
    }

    memcpy(&addr_outside.sin_addr, he->h_addr_list[0], he->h_length);
    addr_outside.sin_family = AF_INET;
    addr_outside.sin_port = htons(outside_port);

    if ((he = gethostbyname(service_host)) == NULL) {
        perror("<3> srvice host name could not be resolved");
        exit(EXIT_FAILURE);
    }

    memcpy(&addr_service.sin_addr, he->h_addr_list[0], he->h_length);
    addr_service.sin_family = AF_INET;
    addr_service.sin_port = htons(service_port);

    // we start out with one unused spare tunnel
    conn_entry_t* spare_conn = conn_table_insert();
    spare_conn->spare = true;
    spare_conn->sock_tunnel = socket(AF_INET, SOCK_DGRAM, 0);
    if (spare_conn->sock_tunnel < 0) {
        printf("<3> could not create new UDP socket for tunnel\n");
        exit(EXIT_FAILURE);
    }

    #define FD_SET2(fd) {\
        FD_SET(fd, &sock_set);\
        if (fd > fd_max) {\
            fd_max = fd;\
        }\
    }

    while("my guitar gently wheeps") {
        fd_max = 0;
        FD_ZERO(&sock_set);
        conn_entry_t* e = conn_table;
        while (e) {
            if (e->sock_service > 0) {
                FD_SET2(e->sock_service);
            }
            if (e->sock_tunnel > 0) {
                FD_SET2(e->sock_tunnel);
            }
            e = e->next;
        }
        tv.tv_sec = 0;
        tv.tv_usec = 100 * 1000;
        result = select(fd_max + 1, &sock_set, NULL, NULL, &tv);

        if (result < 0) {
            perror("<3> select error");
        }

        else if (result == 0) {
            // timeout
        }

        else {
            conn_entry_t* e = conn_table;
            while (e) {

                // check all the sockets in the con table (these are all facing towards the service host)
                if (e->sock_service > 0) {
                    if (FD_ISSET(e->sock_service, &sock_set)) {
                        nbytes = recvfrom(e->sock_service, buffer, BUF_SIZE, 0, (struct sockaddr*) &addr_incoming, &len_addr);
                        if (e->sock_tunnel > 0) {
                            sendto(e->sock_tunnel, buffer, nbytes, 0, (struct sockaddr*)&addr_outside, len_addr);
                            e->last_acticity = millisec();
                        }
                    }
                }

                // check all the sockets that are facing towards the tunnel outside agent
                if (e->sock_tunnel > 0) {
                    if (FD_ISSET(e->sock_tunnel, &sock_set)) {
                        nbytes = recvfrom(e->sock_tunnel, buffer, BUF_SIZE, 0, (struct sockaddr*) &addr_incoming, &len_addr);
                        if (e->spare) {
                            // this came in on one of the spare connections
                            // remove the spare status and create a socket to use it
                            printf("<6> new incoming connection\n");
                            e->spare = false;
                            e->sock_service = socket(AF_INET, SOCK_DGRAM, 0);
                            if (e->sock_service < 0) {
                                printf("<3> could not create new UDP socket for service\n");
                                exit(EXIT_FAILURE);
                            }

                            // and immediately create another new spare connection
                            conn_entry_t* spare_conn = conn_table_insert();
                            spare_conn->spare = true;
                            spare_conn->sock_tunnel = socket(AF_INET, SOCK_DGRAM, 0);
                            if (e->sock_tunnel < 0) {
                                printf("<3> could not create UDP socket for new spare connectionl\n");
                                exit(EXIT_FAILURE);
                            }
                        }

                        if (e->sock_service > 0) {
                            sendto(e->sock_service, buffer, nbytes, 0, (struct sockaddr*)&addr_service, len_addr);
                            e->last_acticity = millisec();
                        }
                    }
                }

                e = e->next;
            }
        }

        // in regular intervals we need to send a keepalive datagram to the outside agent. This has the
        // purpose of punching a hole into the NAT and keeping it open, and it also tells the outside
        // agent the public address and port of that hole, so it can send datagrams back to the inside.
        e = conn_table;
        uint64_t ms = millisec();
        while(e) {
            if (e->sock_tunnel > 0) {
                if (ms - e->last_keepalive > KEEPALIVE_SECONDS * 1000) {
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

/**
 * main program entry point
 */
int main(int argc, char *args[]) {
    setbuf(stdout, NULL);
    args_parsed_t parsed = args_parse(argc, args);
    if (parsed.secret) {
        mac_init(parsed.secret, strlen(parsed.secret));
    }
    if (parsed.listenport) {
        run_outside(parsed.listenport);
    } else {
        run_inside(parsed.outside_host, parsed.outside_port, parsed.service_host, parsed.service_port);
    }
}
