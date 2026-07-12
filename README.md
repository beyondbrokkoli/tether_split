```c
/* tether_split.c - Micro-interceptor for per-app network routing.
 * Forces SO_BINDTODEVICE on application sockets with 1MB WAN-jitter buffers.
*/

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

```

### Build
```

gcc -fPIC -shared -o tether_split.so tether_split.c -ldl

```
### Why this exists

Sometimes you want a specific application to use a specific network device, without changing the routing rules for the rest of your operating system. Instead of installing heavy networking daemons, container runtimes, or bloated user-space wrappers, this solves it with a few lines of clean C and zero dependencies.

It is designed with a minimalist, suckless-adjacent philosophy: one tiny tool that does exactly one job at the socket layer.
The Setup:

The Problem: You have two internet connections. You want a high-stability fixed line (like Ethernet) to handle latency-sensitive traffic, but you want to offload a heavy data stream (like OBS Studio or massive uploads) to a secondary high-bandwidth line (like cellular data).

The Solution: This micro-interceptor binds target applications strictly to your chosen interface, while leaving your system's default network routing entirely untouched.

#### Step 1: Identify Your Networks

First, plug in both your Ethernet cable and your tethered phone. Run this to find their interface names:

```

ip link

```

* **Look for your Ethernet:** Usually starts with `enp...` or `eno...` (e.g., `eno1`).
* **Look for your Phone Tether:** Usually starts with `enp...` or `enx...` (e.g., `enp0s46g0u7c1h2`).

#### Step 2: Lock Down the Default Routing (NetworkManager)

By default, Linux gets confused when two internet connections are plugged in at the same time. We need to force the OS to prioritize your Ethernet so your games automatically get the best ping without any extra configuration.

Find the exact names of your connections:

```

nmcli connection show

```

Now, assign "Metrics" (priorities). A lower metric means higher priority.

```
# 1. Make your real Ethernet the absolute priority (Metric 10)
# Replace "Wired connection 1" with your actual Ethernet connection name
nmcli connection modify "Wired connection 1" ipv4.route-metric 10

# 2. Push the iPhone down so the system ignores it by default (Metric 100)
# Replace "Wired connection 2" with your actual Phone connection name
nmcli connection modify "Wired connection 2" ipv4.route-metric 100

# 3. Bounce NetworkManager to apply the rules permanently
sudo systemctl restart NetworkManager

```

*(**When to do this:** You only need to run these `nmcli` commands once. NetworkManager will remember these priorities forever.)*

#### Step 3: Compile the Middleware

Now, take the `tether_split.c` file and compile it into a shared object (`.so`) library. We use standard `gcc`, which is native to almost all Linux distributions.

```

gcc -fPIC -shared -o tether_split.so tether_split.c -ldl

```

*(**Why this is safe:** You are compiling the code locally on your own machine. There are no downloaded binaries, no AUR packages, and no hidden dependencies.)*

#### Step 4: Launch Your App (The Magic Trick)

To force an application to use your phone's internet, simply prepend the LD_PRELOAD environment variable to its launch command.

⚠️ CRITICAL: You must use the absolute path to the compiled .so file. If you just use ./tether_split.so, it will often fail with a cannot open shared object file error because large applications (like OBS) change their working directory when they boot up.

The easiest way to do this is to use $PWD (which automatically prints your current directory) if you are still in the folder where you compiled the script:

For OBS Studio:
Bash

LD_PRELOAD=$PWD/tether_split.so obs

For Discord (or any other app):
Bash

LD_PRELOAD=$PWD/tether_split.so discord

(Alternatively, you can just type out the full path manually, for example: LD_PRELOAD=/home/youruser/tether-split/tether_split.so obs)

Here is exactly what the code is doing, line by line:

#### 1. Standard Linux Libraries Only

```c
#include <dlfcn.h>
#include <string.h>
#include <sys/socket.h>
#include <stdio.h>

```

We are only including the foundational, built-in Linux headers.

* `<sys/socket.h>` gives us the vocabulary to talk to the network.
* `<dlfcn.h>` gives us the ability to temporarily intercept function calls.

#### 2. The `LD_PRELOAD` Trick

When you launch an app with `LD_PRELOAD=./tether_split.so`, you are telling the Linux operating system: *"Before you load the normal networking rules for this app, load my tiny script first."*

#### 3. Passing the Baton

```c
int (*orig_socket)(int, int, int) = dlsym(RTLD_NEXT, "socket");
int fd = orig_socket(domain, type, protocol);

```

When the app asks the system to open a network connection (a `socket`), our script steps in. The very first thing it does is use `dlsym(RTLD_NEXT...` to find the **real, original** system socket function and pass the request right back to the OS. We aren't building a custom network stack; we are just tapping the OS on the shoulder.

#### 4. Flipping Two Switches (`setsockopt`)

If the connection is a standard internet connection (IPv4 or IPv6), our script flips two native Linux settings before handing the connection back to the app:

1. **`SO_BINDTODEVICE`**: It tells the kernel, *"Force this specific connection to use the iPhone's network interface instead of the default Ethernet."*
2. **`SO_SNDBUF`**: It tells the kernel, *"Give this connection a 1MB buffer."* This is a critical stability patch. Cellular connections jitter. Expanding the buffer ensures the app doesn't drop frames or packets when the cell tower hiccups.

#### What this code CANNOT do:

* **It cannot read your files:** There is no file I/O (`fopen`, `read`, `write`) anywhere in the code.
* **It cannot execute commands:** There are no `system()` or `exec()` calls.
* **It cannot steal data:** It never actually touches the *contents* of your network packets; it only tells the system which wire to send them down.

Because it compiles locally on your own machine using your own standard compiler (`gcc`), you are in complete control of the supply chain. You can read the whole file in 10 seconds and verify it does exactly what it claims to do, and nothing else.

**How to verify it's working:**
When you hit enter, you should immediately see `[VX_NET] Intercepting OBS Sockets -> Forcing to...` print in your terminal. You can now stream massive bitrates via your phone, while your system (and your games) remain happily locked to your low-latency Ethernet!
