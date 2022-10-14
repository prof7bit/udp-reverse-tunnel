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

#define NAT_LIFETIME_SECONDS    60
#define BUF_SIZE                0xffff
#define KEEPALIVE_SECONDS       10

static void run_outside(unsigned port) {
    int sockfd;
    char buffer[BUF_SIZE + 1];
    struct sockaddr_in addr_own = {0};
    struct sockaddr_in addr_incoming = {0};
    struct sockaddr_in addr_inside = {0};
    uint8_t id_counter = 0; // fixme
    bool know_addr_inside = false;

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
        nbytes = recvfrom(sockfd, buffer + 1, BUF_SIZE - 1, MSG_WAITALL, (struct sockaddr*) &addr_incoming, &len_addr);

        // the keepalive datagram from the inside agent is a 40 byte message authentication code
        // for an empty message with a strictly increasing nonce, each code can only be used 
        // exactly once) to prevent replay attacks. This datagram is used to learn the public
        // address and port of the inside agent.
        if (nbytes == sizeof(mac_t)) {
            mac_t mac;
            memcpy(&mac, buffer + 1, sizeof(mac_t));
            if (mac_test(NULL, 0, mac)) {
                // We could successfully verify the authentication code, we know this datagram
                // originates from the inside agent and we can store the source address.
                // From this moment on we know where to forward the client datagrams.
                if (memcmp(&addr_inside, &addr_incoming, len_addr) != 0) {
                    memcpy(&addr_inside, &addr_incoming, len_addr);
                    know_addr_inside = true;
                    printf("<6> got public address of inside agent: %s:%d\n", inet_ntoa(addr_incoming.sin_addr), addr_incoming.sin_port);
                }
                conn_table_clean(NAT_LIFETIME_SECONDS); // periodic cleaning of stale entries
                continue;
            }
        }

        if (know_addr_inside) {
            if (memcmp(&addr_incoming, &addr_inside, len_addr) == 0) {
                // This originates from the inside agent, it can only be a tunneled
                // datagram. We need to look up the client address in out NAT table,
                // unpack the payload and send it to the client.
                conn_entry_t* nat = conn_table_find_id(buffer[1]);
                if(nat) {
                    sendto(sockfd, buffer + 2, nbytes - 1, 0, (struct sockaddr*)&nat->addr, len_addr);
                }

            } else {
                // This originates from a client. We look up its address in our NAT
                // table (or crate a new entry), wrap it into a tunnel datagram and
                // send it to the inside agent.
                conn_entry_t* nat = nat_table_find_address(&addr_incoming);
                if (nat == NULL) {
                    nat = conn_table_insert();
                    memcpy(&nat->addr, &addr_incoming, len_addr);
                    nat->id = id_counter++;
                    printf("<6> new client conection %d from %s:%d\n", nat->id, inet_ntoa(addr_incoming.sin_addr), addr_incoming.sin_port);
                }
                nat->time = time(NULL);
                buffer[0] = nat->id;
                sendto(sockfd, buffer, nbytes + 1, 0, (struct sockaddr*)&addr_inside, len_addr);
            }
        }
    }
}

static void run_inside(char* outsude_host, int outside_port, char* service_host, int service_port) {
    fd_set sock_set;
    struct timeval tv;
    int fd_max;
    int result;
    int nbytes;
    int sock_outside;
    struct sockaddr_in addr_outside = {0};
    struct sockaddr_in addr_service = {0};
    struct sockaddr_in addr_incoming = {0};
    struct hostent* he;
    socklen_t len_addr = sizeof(struct sockaddr_in);
    char buffer[BUF_SIZE + 1];

    time_t last_keepalive = time(NULL) - KEEPALIVE_SECONDS;

    printf("<6> UDP tunnel inside agent\n");
    printf("<6> trying to contact outside agent at %s, port %d\n", outsude_host, outside_port);
    printf("<6> forwarding incomimg UDP to %s, port %d\n", service_host, service_port);

    if ((sock_outside = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("<3> socket creation failed");
        exit(EXIT_FAILURE);
    }

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

    #define FD_SET2(fd) {\
        FD_SET(fd, &sock_set);\
        if (fd > fd_max) {\
            fd_max = fd;\
        }\
    }

    while("my guitar gently wheeps") {
        fd_max = 0;
        FD_ZERO(&sock_set);
        FD_SET2(sock_outside);
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
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        result = select(fd_max + 1, &sock_set, NULL, NULL, &tv);

        if (result < 0) {
            perror("<3> select error");
        }

        else if (result == 0) {
            // timeout
        }

        else {
            // check all the sockets in the con table (these are all facing towards the service host)
            conn_entry_t* e = conn_table;
            while (e) {
                if (FD_ISSET(e->sock_service, &sock_set)) {
                    nbytes = recvfrom(e->sock_service, buffer + 1, BUF_SIZE - 1, 0, (struct sockaddr*) &addr_incoming, &len_addr);
                    buffer[0] = e->id;
                    sendto(sock_outside, buffer, nbytes + 1, 0, (struct sockaddr*)&addr_outside, len_addr);
                }
                e = e->next;
            }

            // check the socket that is facing towards the tunnel outside agent
            if (FD_ISSET(sock_outside, &sock_set)) {
                nbytes = recvfrom(sock_outside, buffer, BUF_SIZE, 0, (struct sockaddr*) &addr_incoming, &len_addr);
                uint8_t id = buffer[0];
                conn_entry_t* entry = conn_table_find_id(id);
                if (!entry) {
                    entry = conn_table_insert();
                    entry->id = id;
                    entry->sock_service = socket(AF_INET, SOCK_DGRAM, 0);
                    if (entry->sock_service < 0) {
                        printf("<3> could not create socket for connection %d\n", id);
                        entry->sock_service = 0;
                    } else {
                        printf("<6> new service connection %d\n", id);
                    }
                }
                if (entry->sock_service > 0) {
                    entry->time = time(NULL);
                    sendto(entry->sock_service, buffer + 1, nbytes - 1, 0, (struct sockaddr*)&addr_service, len_addr);
                }
            }
        }

        // in regular intervals we need to send a keepalive datagram to the outside agent. This has the
        // porpose of punching a hole into the NAT and keeping it open, and it also tells the outside
        // agent the public address and port of that hole, so it can send datagrams back to the inside.
        if (time(NULL) - last_keepalive > KEEPALIVE_SECONDS) {
            last_keepalive = time(NULL);

            // the keepalive datagram is a 40 byte message authentication code, based on the sha-256 over 
            // a strictly increasing nonce and a pre shared secret (the -k argument). This is done to
            // prevent spoofing of the keepalive datagrams by an attacker.
            mac_t mac = mac_gen(NULL, 0, time(NULL));
            sendto(sock_outside, &mac, sizeof(mac), 0, (struct sockaddr*)&addr_outside, len_addr);

            // remove any stale inactive connections from the connection table and close their sockets.
            conn_table_clean(NAT_LIFETIME_SECONDS);
        }
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
