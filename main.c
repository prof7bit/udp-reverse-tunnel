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

#include "args.h"
#include "connlist.h"

#define NAT_LIFETIME_SECONDS	60
#define BUF_SIZE 				0xffff
#define KEEPALIVE_SECONDS		10

static void run_outside(unsigned port) {
	int sockfd;
	char buffer[BUF_SIZE + 1];
	struct sockaddr_in addr_own = {};
	struct sockaddr_in addr_incoming = {};
	struct sockaddr_in addr_inside = {};
	uint8_t id_counter = 0; // fixme
	bool know_addr_inside = false;

	printf("UDP tunnel outside agent\n");

	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("socket creation failed");
		exit(EXIT_FAILURE);
	}

	addr_own.sin_family = AF_INET;
	addr_own.sin_addr.s_addr = INADDR_ANY;
	addr_own.sin_port = htons(port);

	if (bind(sockfd, (const struct sockaddr *)&addr_own, sizeof(addr_own)) < 0) {
		perror("bind failed");
		exit(EXIT_FAILURE);
	}

	printf("listening on port %d\n", port);

	socklen_t len_addr = sizeof(addr_incoming);
	size_t nbytes;

	while ("my guitar gently weeps") {
		nbytes = recvfrom(sockfd, buffer + 1, BUF_SIZE - 1, MSG_WAITALL, (struct sockaddr*) &addr_incoming, &len_addr);
		if (strncmp(buffer + 1, "udp-mapper-keepalive", 20) == 0) {
			// the keepalive datagram is not forwarded, it originates from
			// the inside agent to keep the NAT open and we use it here to
			// learn the public address of the inside agent.
			//
			// fixme: need a way to securely authenticate the inside agent
			// to prevent DDOS attacks on the tunnel.
			//printf("received keelapive from inside agent %s:%d\n", inet_ntoa(addr_incoming.sin_addr), addr_incoming.sin_port);
			if (memcmp(&addr_inside, &addr_incoming, len_addr) != 0) {
				memcpy(&addr_inside, &addr_incoming, len_addr);
				know_addr_inside = true;
				printf("got public address of inside agent: %s:%d\n", inet_ntoa((struct in_addr)addr_incoming.sin_addr), addr_incoming.sin_port);
			}
			conn_table_clean(NAT_LIFETIME_SECONDS); // periodic cleaning of stale entries

		} else {
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
						printf("new client conection %d from %s:%d\n", nat->id, inet_ntoa((struct in_addr)addr_incoming.sin_addr), addr_incoming.sin_port);
					}
					nat->time = time(NULL);
					buffer[0] = nat->id;
					sendto(sockfd, buffer, nbytes + 1, 0, (struct sockaddr*)&addr_inside, len_addr);
				}
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
	struct sockaddr_in addr_outside = {};
	struct sockaddr_in addr_service = {};
	struct sockaddr_in addr_incoming = {};
	struct hostent* he;
	socklen_t len_addr = sizeof(struct sockaddr_in);
	char buffer[BUF_SIZE + 1];

	time_t last_keepalive = time(NULL) - KEEPALIVE_SECONDS;

	printf("UDP tunnel inside agent\n");
	printf("trying to contact outside agent at %s, port %d\n", outsude_host, outside_port);
	printf("forwarding incomimg UDP to %s, port %d\n", service_host, service_port);

	if ((sock_outside = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("socket creation failed");
		exit(EXIT_FAILURE);
	}

	if ((he = gethostbyname(outsude_host)) == NULL) {
		perror("outside host name could not be resolved");
		exit(EXIT_FAILURE);
	}

	memcpy(&addr_outside.sin_addr, he->h_addr_list[0], he->h_length);
	addr_outside.sin_family = AF_INET;
	addr_outside.sin_port = htons(outside_port);

	if ((he = gethostbyname(service_host)) == NULL) {
		perror("srvice host name could not be resolved");
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
			FD_SET2(e->sockfd);
			e = e->next;
		}
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		result = select(fd_max + 1, &sock_set, NULL, NULL, &tv);

		if (result < 0) {
			perror("select error");
		}

		else if (result == 0) {
			// timeout
		}

		else {
			// check all the sockets in the con table (these are all facing towards the service host)
			conn_entry_t* e = conn_table;
			while (e) {
				if (FD_ISSET(e->sockfd, &sock_set)) {
					nbytes = recvfrom(e->sockfd, buffer + 1, BUF_SIZE - 1, 0, (struct sockaddr*) &addr_incoming, &len_addr);
					buffer[0] = e->id;
					sendto(sock_outside, buffer, nbytes + 1, 0, (struct sockaddr*)&addr_outside, len_addr);
				}
				e = e->next;
			}

			// check teh socket that is facing towards the tunnel outside agent
			if (FD_ISSET(sock_outside, &sock_set)) {
				nbytes = recvfrom(sock_outside, buffer, BUF_SIZE, 0, (struct sockaddr*) &addr_incoming, &len_addr);
				uint8_t id = buffer[0];
				conn_entry_t* entry = conn_table_find_id(id);
				if (!entry) {
					entry = conn_table_insert();
					entry->id = id;
					entry->sockfd = socket(AF_INET, SOCK_DGRAM, 0);
					printf("new service connection %d\n", id);
				}
				entry->time = time(NULL);
				sendto(entry->sockfd, buffer + 1, nbytes - 1, 0, (struct sockaddr*)&addr_service, len_addr);
			}
		}

		if (time(NULL) - last_keepalive > KEEPALIVE_SECONDS) {
			last_keepalive = time(NULL);
			//printf("sending keepalive\n");
			sendto(sock_outside, "udp-mapper-keepalive", 20, 0, (struct sockaddr*)&addr_outside, len_addr);
			conn_table_clean(NAT_LIFETIME_SECONDS);
		}
	}
}

/**
 * main program entry point
 */
int main(int argc, char *args[]) {
	args_parsed_t parsed = args_parse(argc, args);
	if (parsed.listenport) {
		run_outside(parsed.listenport);
	} else {
		run_inside(parsed.outside_host, parsed.outside_port, parsed.service_host, parsed.service_port);
	}

}
