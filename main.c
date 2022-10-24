/**
 * @file main.c
 * @author Bernd Kreuss
 * @date 7 Oct 2022
 * @brief UDP reverse tunnel
 */

#include <stdio.h>
#include <string.h>

#include "args.h"
#include "mac.h"
#include "main-inside.h"
#include "main-outside.h"

/**
 * main program entry point
 */
int main(int argc, char *args[]) {
    args_parsed_t parsed = args_parse(argc, args);
    if (parsed.secret) {
        mac_init(parsed.secret, strlen(parsed.secret));
    }
    if (parsed.listenport) {
        run_outside(parsed);
    } else {
        run_inside(parsed);
    }
}
