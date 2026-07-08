/* tether_split.c - The Weaver Engine OBS Network Multiplexer */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <string.h>
#include <sys/socket.h>
#include <stdio.h>

// Define the interface just like a global state
// ADJUST NETWORK DEVICE HERE
static const char* IPHONE_IFACE = "enp0s24g0u5e4h2"; // <-- HERE
static int tether_initialized = 0;

// Intercept all OS socket creations
int socket(int domain, int type, int protocol) {
    // 1. Grab the real OS socket function
    int (*orig_socket)(int, int, int) = dlsym(RTLD_NEXT, "socket");
    int fd = orig_socket(domain, type, protocol);

    // 2. Only intercept valid IPv4/IPv6 sockets (Ignore local UNIX sockets)
    if (fd >= 0 && (domain == AF_INET || domain == AF_INET6)) {

        if (!tether_initialized) {
            printf("[VX_NET] Intercepting OBS Sockets -> Forcing to %s\n", IPHONE_IFACE);
            tether_initialized = 1;
        }

        // 3. Bind strictly to the iPhone interface
        setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, IPHONE_IFACE, strlen(IPHONE_IFACE));

        // 4. [CRITICAL WAN PATCH]
        // Force a 1MB Send Buffer for OBS to survive cellular jitter spikes
        int buf_size = 1024 * 1024;
        setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (const char*)&buf_size, sizeof(buf_size));
    }

    return fd;
}
