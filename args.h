#ifndef ARGS_H
#define ARGS_H

typedef struct {
    unsigned  listenport;
    char* service;
    char* outside;
    char *service_host;
    unsigned service_port;
    char* outside_host;
    unsigned outside_port;
    char* secret;
    unsigned keepalive;
} args_parsed_t;

args_parsed_t args_parse(int argc, char* args[]);

#endif // ARGS_H
