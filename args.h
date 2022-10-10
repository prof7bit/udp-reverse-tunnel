#ifndef ARGS_H
#define ARGS_H

typedef struct {
    int  listenport;
    char* service;
    char* outside;
    char *service_host;
    int service_port;
    char* outside_host;
    int outside_port;
} args_parsed_t;

args_parsed_t args_parse(int argc, char* args[]);

#endif // ARGS_H
