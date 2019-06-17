
#ifndef __INET_LOCAL_H__
#define __INET_LOCAL_H__

#include <inet/socket.h>

// global paths
#define LCADDR_INPUT "/lc/input"
#define LCADDR_WM    "/lc/wm"

struct sockaddr_lc {
	uint8_t     sa_len;
	sa_family_t sa_family;   // AF_LOCAL
    char        sa_path[32]; // see defines above
};

#endif //!__INET_LOCAL_H__
