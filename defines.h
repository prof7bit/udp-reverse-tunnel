#ifndef DEFINES_H
#define DEFINES_H

#define CONN_LIFETIME_SECONDS   60
#define BUF_SIZE                0xffff

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define VERSION_STR TOSTRING(VERSION)  // passed by compiler -DVERSION option

#endif

