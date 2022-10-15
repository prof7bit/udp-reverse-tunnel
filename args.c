#include "args.h"
#include <argp.h>
#include <stdlib.h>

const char *argp_program_version = "udp-tunnel-1.1";
const char *argp_program_bug_address = "<prof7bit@gmail.com>";
static char doc[] = " \
creates a reverse UDP tunnel for an UDP srvice behind NAT\n\n \
Example usage on the inside:\n \
    udp-tunnel -s wireguard.fritz.box:1234 -o jump.example.com:9999\n\n \
Example usage on the outside:\n \
    udp-tunnel -l 9999";
static char args_doc[] = "";

static struct argp_option options[] = {
    {
        .group = 1,
        .doc = "Options for running it as the inside agent:"
    },
    {
        .name = "outside",
        .arg = "host:port",
        .key = 'o',
        .group = 1,
        .doc = "address of the outside agent"
    },
    {
        .name = "service",
        .arg = "host:port",
        .key = 's',
        .group = 1,
        .doc = "address of the inside service"
    },
    {
        .group = 2,
        .doc = "Options for running it as the outside agent:"
    },
    {
        .name = "listen",
        .arg = "port",
        .key = 'l',
        .group = 2,
        .doc = "listen port"
    },
    {
        .group = 3,
        .doc = "General options:"
    },
    {
        .name = "key",
        .arg = "string",
        .key = 'k',
        .group = 3,
        .doc = "optional shared password to prevent spoofing"
    },

    {0}
};

static error_t parse_opt(int key, char *arg, struct argp_state *state){

    args_parsed_t* parsed = state->input;
    switch(key){

        case 'l':
            parsed->listenport = strtoul(arg, NULL, 10);
            break;

        case 's':
            parsed->service = arg;
            sscanf(arg, "%m[^:]:%d", &parsed->service_host, &parsed->service_port);
            break;

        case 'o':
            parsed->outside = arg;
            sscanf(arg, "%m[^:]:%d", &parsed->outside_host, &parsed->outside_port);
            break;

        case 'k':
            parsed->secret = arg;
            break;

        default:
            return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static struct argp argp = {
    .options = options,
    .parser = parse_opt,
    .args_doc = args_doc,
    .doc = doc
};

static void error(char* s) {
    fprintf(stderr, "\nusage error:\n%s\n\n", s);
    argp_help(&argp, stderr, ARGP_HELP_DOC, NULL);
    exit(1);
}

args_parsed_t args_parse(int argc, char* args[]) {
    args_parsed_t parsed;
    parsed.listenport = 0;
    parsed.service = NULL;
    parsed.outside = NULL;
    parsed.secret = NULL;
    argp_parse(&argp, argc, args, 0, 0, &parsed);

    if ((parsed.listenport > 0) && (parsed.outside != NULL)) {
        error("--listen and --outside are mutually exclusive");
    }
    if ((parsed.listenport > 0) && (parsed.service != NULL)) {
        error("--listen and --service are mutually exclusive");
    }
    if (((parsed.service != NULL) && (parsed.outside == 0)) || ((parsed.service == NULL) && (parsed.outside != 0))) {
        error("--service and --outside must both be specified");
    }
    if ((parsed.listenport == 0) && (parsed.outside == NULL) && (parsed.service == 0)) {
        error("too few options");
    }
    if (parsed.service && (parsed.service_port == 0)) {
        error("something is wrong with the service address, use host:port syntax");
    }
    if (parsed.outside && (parsed.outside_port == 0)) {
        error("something is wrong with the outside address, use host:port syntax");
    }

    return parsed;
}
