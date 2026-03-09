/*
 * empire_frontend.c -- Telnet frontend with PTY bridging
 *
 * This is the network-facing component that:
 * - Listens on port 5000 for telnet connections
 * - Handles RFC 854 telnet negotiation
 * - Allocates a PTY for each connection
 * - Bridges TCP socket <-> PTY master
 * - Runs empire_client in the PTY slave
 *
 * Security features:
 * - Rate limiting per IP
 * - Privilege dropping after binding
 * - Environment sanitization
 * - No direct server access
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <syslog.h>
#include <pty.h>
#include <utmp.h>
#include <termios.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include <sys/time.h>
#include <ctype.h>

#define FRONTEND_PORT 5000
#define MAX_CLIENTS 64
#define BACKLOG 10
#define BUFFER_SIZE 4096
#define MAX_INPUT_LEN 256
#define RATE_LIMIT_WINDOW 60  /* seconds */
#define RATE_LIMIT_MAX 100     /* connections per window */

/* Telnet RFC 854 constants */
#define IAC  0xFF
#define DONT 0xFE
#define DO   0xFD
#define WONT 0xFC
#define WILL 0xFB
#define SB   0xFA
#define SE   0xF0

/* Telnet options */
#define TELOPT_ECHO   1
#define TELOPT_SGA    3
#define TELOPT_NAWS   31
#define TELOPT_LINEMODE 34

/* Client states */
enum client_state {
    STATE_NEW = 0,
    STATE_TELNET_NEG,
    STATE_PTY_SETUP,
    STATE_CONNECTED,
    STATE_CLOSING
};

/* Connection tracking for rate limiting */
typedef struct {
    struct in_addr addr;
    time_t first_seen;
    int count;
} rate_entry_t;

/* Client session structure */
typedef struct {
    int socket;
    enum client_state state;
    struct sockaddr_in addr;
    time_t connect_time;
    time_t last_activity;
    
    /* PTY */
    int pty_master;
    pid_t child_pid;
    
    /* Buffers */
    unsigned char input_buf[BUFFER_SIZE];
    int input_len;
    unsigned char output_buf[BUFFER_SIZE];
    int output_len;
    int output_pos;
    
    /* Telnet state */
    int iac_state;  /* 0=normal, 1=IAC received, 2=option received */
    unsigned char iac_cmd;
    unsigned char iac_opt;
    
    /* Terminal info */
    int term_width;
    int term_height;
    char term_type[32];
    
    /* Logging */
    char username[32];
    char client_ip[INET_ADDRSTRLEN];
} client_t;

/* Global state */
static client_t clients[MAX_CLIENTS];
static int listen_socket = -1;
static int running = 1;
static int daemon_mode = 0;
static rate_entry_t rate_table[256];
static int rate_count = 0;
static int frontend_port = FRONTEND_PORT;
static char *backend_host = "127.0.0.1";
static int backend_port = 4000;
static char *backend_socket = NULL;

/* Forward declarations */
static void log_msg(const char *fmt, ...);
static void init_frontend(int port);
static void daemonize(void);
static void drop_privileges(const char *user);
static void signal_handler(int sig);
static void reap_children(void);
static int check_rate_limit(struct in_addr *addr);
static void accept_connection(void);
static void disconnect_client(int idx);
static void handle_telnet_negotiation(int idx, unsigned char *data, int len);
static void send_telnet_cmd(int idx, unsigned char cmd, unsigned char opt);
static int setup_pty(int idx);
static void bridge_data(int idx);
static void cleanup_client(int idx);
static int sanitize_env(void);

/*
 * Log a message
 */
static void log_msg(const char *fmt, ...) {
    va_list args;
    char buf[1024];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    
    if (daemon_mode) {
        syslog(LOG_INFO, "%s", buf);
    } else {
        fprintf(stderr, "[%02d:%02d:%02d] %s\n",
                tm->tm_hour, tm->tm_min, tm->tm_sec, buf);
    }
}

/*
 * Initialize the frontend socket
 */
static void init_frontend(int port) {
    int opt = 1;
    struct sockaddr_in addr;
    
    listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_socket < 0) {
        log_msg("socket: %s", strerror(errno));
        exit(1);
    }
    
    if (setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        log_msg("setsockopt SO_REUSEADDR: %s", strerror(errno));
        exit(1);
    }
    
    /* Enable TCP keepalive */
    int keepalive = 1;
    int keepidle = 60;
    int keepintvl = 10;
    int keepcnt = 6;
    setsockopt(listen_socket, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
    setsockopt(listen_socket, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
    setsockopt(listen_socket, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
    setsockopt(listen_socket, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));
    
    fcntl(listen_socket, F_SETFL, O_NONBLOCK);
    
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(listen_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_msg("bind: %s", strerror(errno));
        exit(1);
    }
    
    if (listen(listen_socket, BACKLOG) < 0) {
        log_msg("listen: %s", strerror(errno));
        exit(1);
    }
    
    log_msg("Frontend listening on port %d", port);
}

/*
 * Daemonize the process
 */
static void daemonize(void) {
    pid_t pid;
    
    pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }
    if (pid > 0) {
        exit(0);
    }
    
    if (setsid() < 0) {
        perror("setsid");
        exit(1);
    }
    
    signal(SIGHUP, SIG_IGN);
    
    pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }
    if (pid > 0) {
        exit(0);
    }
    
    chdir("/");
    
    freopen("/dev/null", "r", stdin);
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    
    daemon_mode = 1;
    openlog("empire_frontend", LOG_PID, LOG_DAEMON);
}

/*
 * Drop privileges
 */
static void drop_privileges(const char *user) {
    struct passwd *pw = getpwnam(user);
    if (!pw) {
        log_msg("Cannot find user %s", user);
        exit(1);
    }
    
    if (initgroups(pw->pw_name, pw->pw_gid) != 0) {
        log_msg("initgroups failed: %s", strerror(errno));
        exit(1);
    }
    
    if (setgid(pw->pw_gid) != 0 || setuid(pw->pw_uid) != 0) {
        log_msg("Failed to drop privileges: %s", strerror(errno));
        exit(1);
    }
    
    log_msg("Dropped privileges to %s", user);
}

/*
 * Signal handler
 */
static void signal_handler(int sig) {
    log_msg("Received signal %d, shutting down...", sig);
    running = 0;
}

/*
 * Reap zombie children
 */
static void reap_children(void) {
    pid_t pid;
    int status;
    int i;
    
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        /* Find which client session this belonged to */
        for (i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].child_pid == pid) {
                log_msg("Child process %d for client %d exited", pid, i);
                clients[i].child_pid = -1;
                clients[i].state = STATE_CLOSING;
                break;
            }
        }
    }
}

/*
 * Check rate limit for an IP
 */
static int check_rate_limit(struct in_addr *addr) {
    time_t now = time(NULL);
    int i;
    
    /* Clean old entries */
    for (i = 0; i < rate_count; i++) {
        if (now - rate_table[i].first_seen > RATE_LIMIT_WINDOW) {
            /* Remove this entry */
            if (i < rate_count - 1) {
                memcpy(&rate_table[i], &rate_table[rate_count - 1], 
                       sizeof(rate_entry_t));
            }
            rate_count--;
            i--;
        }
    }
    
    /* Find or create entry for this IP */
    for (i = 0; i < rate_count; i++) {
        if (rate_table[i].addr.s_addr == addr->s_addr) {
            if (rate_table[i].count >= RATE_LIMIT_MAX) {
                return 0; /* Rate limit exceeded */
            }
            rate_table[i].count++;
            return 1;
        }
    }
    
    /* Add new entry */
    if (rate_count < 256) {
        rate_table[rate_count].addr = *addr;
        rate_table[rate_count].first_seen = now;
        rate_table[rate_count].count = 1;
        rate_count++;
        return 1;
    }
    
    return 0; /* Table full, deny connection */
}

/*
 * Send telnet command
 */
static void send_telnet_cmd(int idx, unsigned char cmd, unsigned char opt) {
    unsigned char buf[3];
    buf[0] = IAC;
    buf[1] = cmd;
    buf[2] = opt;
    
    if (clients[idx].output_len + 3 <= BUFFER_SIZE) {
        memcpy(clients[idx].output_buf + clients[idx].output_len, buf, 3);
        clients[idx].output_len += 3;
    }
}

/*
 * Handle telnet negotiation
 */
static void handle_telnet_negotiation(int idx, unsigned char *data, int len) {
    int i;
    
    for (i = 0; i < len; i++) {
        unsigned char c = data[i];
        
        switch (clients[idx].iac_state) {
            case 0: /* Normal data */
                if (c == IAC) {
                    clients[idx].iac_state = 1;
                }
                break;
                
            case 1: /* IAC received */
                if (c == IAC) {
                    /* Escaped IAC, treat as data */
                    clients[idx].iac_state = 0;
                } else if (c == DO || c == DONT || c == WILL || c == WONT ||
                           c == SB) {
                    clients[idx].iac_cmd = c;
                    clients[idx].iac_state = 2;
                } else {
                    /* Single-byte command, ignore */
                    clients[idx].iac_state = 0;
                }
                break;
                
            case 2: /* Option byte */
                clients[idx].iac_opt = c;
                
                /* Process the command */
                switch (clients[idx].iac_cmd) {
                    case DO:
                        /* Refuse most options */
                        if (c == TELOPT_NAWS) {
                            /* Accept window size */
                            send_telnet_cmd(idx, WILL, TELOPT_NAWS);
                        } else if (c == TELOPT_SGA) {
                            send_telnet_cmd(idx, WILL, TELOPT_SGA);
                        } else {
                            send_telnet_cmd(idx, WONT, c);
                        }
                        break;
                        
                    case WILL:
                        /* Server handles echo */
                        if (c == TELOPT_ECHO) {
                            send_telnet_cmd(idx, DONT, c);
                        } else {
                            send_telnet_cmd(idx, DONT, c);
                        }
                        break;
                        
                    case DONT:
                    case WONT:
                        /* Acknowledge */
                        break;
                }
                
                clients[idx].iac_state = 0;
                break;
        }
    }
}

/*
 * Strip telnet sequences from data
 */
static int strip_telnet(int idx, unsigned char *data, int len) {
    unsigned char *src = data;
    unsigned char *dst = data;
    int count = 0;
    int i;
    
    for (i = 0; i < len; i++) {
        if (src[i] == IAC) {
            /* Skip IAC sequence (2 or 3 bytes) */
            if (i + 1 < len) {
                unsigned char cmd = src[i + 1];
                if (cmd >= 0xF0) {
                    /* Single byte command */
                    i += 1;
                } else if (i + 2 < len) {
                    /* Two byte command (option) */
                    i += 2;
                } else {
                    break; /* Incomplete sequence */
                }
            } else {
                break; /* Incomplete IAC */
            }
        } else {
            dst[count++] = src[i];
        }
    }
    
    return count;
}

/*
 * Setup PTY and fork empire_client
 */
static int setup_pty(int idx) {
    int master;
    pid_t pid;
    struct winsize ws;
    
    memset(&ws, 0, sizeof(ws));
    ws.ws_col = clients[idx].term_width ? clients[idx].term_width : 80;
    ws.ws_row = clients[idx].term_height ? clients[idx].term_height : 24;
    
    pid = forkpty(&master, NULL, NULL, &ws);
    if (pid < 0) {
        log_msg("forkpty: %s", strerror(errno));
        return -1;
    }
    
    if (pid == 0) {
        /* Child process - run empire_client */
        
        /* Sanitize environment */
        sanitize_env();
        
        /* Set terminal */
        setenv("TERM", clients[idx].term_type[0] ? 
               clients[idx].term_type : "xterm-256color", 1);
        
        /* Set window size */
        char geo[32];
        snprintf(geo, sizeof(geo), "%dx%d", ws.ws_col, ws.ws_row);
        setenv("COLUMNS", geo, 1);
        
        /* Set client info */
        setenv("EMPIRE_CLIENT_IP", clients[idx].client_ip, 1);
        setenv("EMPIRE_TERM_WIDTH", "80", 1);
        setenv("EMPIRE_TERM_HEIGHT", "24", 1);
        
        /* Build argument list */
        char host_arg[64];
        char port_arg[16];
        snprintf(host_arg, sizeof(host_arg), "-h%s", backend_host);
        
        /* Use empire_client from PATH - add CWD to PATH if not found */
        char *orig_path = getenv("PATH");
        char new_path[4096];
        if (orig_path) {
            snprintf(new_path, sizeof(new_path), ".:%s", orig_path);
        } else {
            strcpy(new_path, ".");
        }
        setenv("PATH", new_path, 1);
        
        if (backend_socket) {
            execlp("empire_client", "empire_client",
                   "-s", backend_socket,
                   "-i", clients[idx].client_ip,
                   NULL);
        } else {
            snprintf(port_arg, sizeof(port_arg), "%d", backend_port);
            execlp("empire_client", "empire_client",
                   host_arg,
                   "-p", port_arg,
                   "-i", clients[idx].client_ip,
                   NULL);
        }
        
        /* If we get here, exec failed */
        perror("execlp");
        _exit(1);
    }
    
    /* Parent */
    clients[idx].pty_master = master;
    clients[idx].child_pid = pid;
    clients[idx].state = STATE_CONNECTED;
    
    /* Make PTY non-blocking */
    fcntl(master, F_SETFL, O_NONBLOCK);
    
    log_msg("Started empire_client for client %d (PID %d)", idx, pid);
    
    return 0;
}

/*
 * Sanitize environment variables
 */
static int sanitize_env(void) {
    char *safe_env[] = {
        "PATH=/usr/local/bin:/usr/bin:/bin",
        "HOME=/tmp",
        "USER=empire",
        "LOGNAME=empire",
        "SHELL=/bin/sh",
        "TERM=xterm-256color",
        NULL
    };
    
    /* Clear all environment */
    extern char **environ;
    environ[0] = NULL;
    
    /* Set safe environment */
    int i;
    for (i = 0; safe_env[i]; i++) {
        putenv(strdup(safe_env[i]));
    }
    
    return 0;
}

/*
 * Accept a new connection
 */
static void accept_connection(void) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int new_socket;
    int i;
    
    new_socket = accept(listen_socket, (struct sockaddr *)&client_addr, &addr_len);
    if (new_socket < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            log_msg("accept: %s", strerror(errno));
        }
        return;
    }
    
    /* Check rate limit */
    if (!check_rate_limit(&client_addr.sin_addr)) {
        log_msg("Rate limit exceeded for %s", inet_ntoa(client_addr.sin_addr));
        close(new_socket);
        return;
    }
    
    /* Find free slot */
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].state == STATE_NEW) {
            memset(&clients[i], 0, sizeof(client_t));
            clients[i].socket = new_socket;
            clients[i].state = STATE_TELNET_NEG;
            clients[i].addr = client_addr;
            clients[i].connect_time = time(NULL);
            clients[i].last_activity = time(NULL);
            clients[i].pty_master = -1;
            clients[i].child_pid = -1;
            clients[i].term_width = 80;
            clients[i].term_height = 24;
            strcpy(clients[i].term_type, "xterm-256color");
            
            inet_ntop(AF_INET, &client_addr.sin_addr, 
                     clients[i].client_ip, sizeof(clients[i].client_ip));
            
            /* Set non-blocking */
            fcntl(new_socket, F_SETFL, O_NONBLOCK);
            int flag = 1;
            setsockopt(new_socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
            
            log_msg("Client %d connected from %s", i, clients[i].client_ip);
            
            /* Send initial telnet negotiation */
            send_telnet_cmd(i, WILL, TELOPT_SGA);
            send_telnet_cmd(i, DO, TELOPT_SGA);
            send_telnet_cmd(i, WILL, TELOPT_NAWS);
            send_telnet_cmd(i, DO, TELOPT_NAWS);
            
            return;
        }
    }
    
    /* No free slots */
    log_msg("Connection rejected: maximum clients reached");
    close(new_socket);
}

/*
 * Disconnect a client
 */
static void disconnect_client(int idx) {
    if (clients[idx].state == STATE_NEW) {
        return;
    }
    
    log_msg("Disconnecting client %d from %s (duration: %ld seconds)",
            idx, clients[idx].client_ip,
            time(NULL) - clients[idx].connect_time);
    
    /* Kill child process if running */
    if (clients[idx].child_pid > 0) {
        kill(clients[idx].child_pid, SIGHUP);
        waitpid(clients[idx].child_pid, NULL, WNOHANG);
    }
    
    /* Close sockets */
    if (clients[idx].socket >= 0) {
        close(clients[idx].socket);
    }
    if (clients[idx].pty_master >= 0) {
        close(clients[idx].pty_master);
    }
    
    clients[idx].state = STATE_NEW;
}

/*
 * Bridge data between socket and PTY
 */
static void bridge_data(int idx) {
    struct pollfd fds[2];
    int ret;
    unsigned char buf[BUFFER_SIZE];
    int n;
    
    /* Poll socket and PTY */
    fds[0].fd = clients[idx].socket;
    fds[0].events = POLLIN;
    if (clients[idx].output_len > clients[idx].output_pos) {
        fds[0].events |= POLLOUT;
    }
    
    fds[1].fd = clients[idx].pty_master;
    fds[1].events = POLLIN;
    
    ret = poll(fds, 2, 10); /* 10ms timeout */
    if (ret < 0) {
        if (errno != EINTR) {
            disconnect_client(idx);
        }
        return;
    }
    
    /* Socket -> PTY (user input) */
    if (fds[0].revents & POLLIN) {
        n = recv(clients[idx].socket, buf, sizeof(buf), 0);
        if (n <= 0) {
            if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
                disconnect_client(idx);
                return;
            }
        } else {
            clients[idx].last_activity = time(NULL);
            
            /* Strip telnet sequences */
            int clean_len = strip_telnet(idx, buf, n);
            
            /* Forward to PTY */
            if (clean_len > 0) {
                int written = 0;
                while (written < clean_len) {
                    int w = write(clients[idx].pty_master, 
                                  buf + written, clean_len - written);
                    if (w < 0) {
                        if (errno != EAGAIN && errno != EWOULDBLOCK) {
                            disconnect_client(idx);
                            return;
                        }
                        break;
                    }
                    written += w;
                }
            }
        }
    }
    
    /* PTY -> Socket (game output) */
    if (fds[1].revents & POLLIN) {
        n = read(clients[idx].pty_master, buf, sizeof(buf));
        if (n <= 0) {
            if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
                /* PTY closed, disconnect */
                disconnect_client(idx);
                return;
            }
        } else {
            /* Add to output buffer */
            int space = BUFFER_SIZE - clients[idx].output_len;
            if (n > space) n = space;
            if (n > 0) {
                memcpy(clients[idx].output_buf + clients[idx].output_len, buf, n);
                clients[idx].output_len += n;
            }
        }
    }
    
    /* Send buffered output */
    if (fds[0].revents & POLLOUT || clients[idx].output_len > clients[idx].output_pos) {
        int remaining = clients[idx].output_len - clients[idx].output_pos;
        if (remaining > 0) {
            n = send(clients[idx].socket,
                     clients[idx].output_buf + clients[idx].output_pos,
                     remaining, 0);
            if (n < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    disconnect_client(idx);
                    return;
                }
            } else if (n > 0) {
                clients[idx].output_pos += n;
                if (clients[idx].output_pos >= clients[idx].output_len) {
                    clients[idx].output_len = 0;
                    clients[idx].output_pos = 0;
                }
            }
        }
    }
    
    /* Check for errors */
    if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
        disconnect_client(idx);
        return;
    }
    
    if (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
        disconnect_client(idx);
        return;
    }
    
    /* Check timeout */
    if (time(NULL) - clients[idx].last_activity > 1800) { /* 30 min */
        log_msg("Client %d timed out", idx);
        disconnect_client(idx);
    }
}

/*
 * Main loop
 */
static void main_loop(void) {
    struct pollfd fds;
    time_t last_reap = 0;
    
    while (running) {
        /* Poll listen socket */
        fds.fd = listen_socket;
        fds.events = POLLIN;
        
        int ret = poll(&fds, 1, 10); /* 10ms timeout */
        if (ret < 0) {
            if (errno != EINTR) {
                log_msg("poll: %s", strerror(errno));
            }
            continue;
        }
        
        /* Accept new connections */
        if (ret > 0 && (fds.revents & POLLIN)) {
            accept_connection();
        }
        
        /* Handle connected clients */
        int i;
        for (i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].state == STATE_TELNET_NEG) {
                /* Check if telnet negotiation complete */
                /* For simplicity, transition after short delay */
                if (time(NULL) - clients[i].connect_time > 1) {
                    clients[i].state = STATE_PTY_SETUP;
                    if (setup_pty(i) < 0) {
                        disconnect_client(i);
                    }
                }
            } else if (clients[i].state == STATE_CONNECTED) {
                bridge_data(i);
            } else if (clients[i].state == STATE_CLOSING) {
                disconnect_client(i);
            }
        }
        
        /* Reap zombie children periodically */
        if (time(NULL) - last_reap > 5) {
            reap_children();
            last_reap = time(NULL);
        }
    }
}

/*
 * Cleanup
 */
static void cleanup(void) {
    int i;
    
    log_msg("Shutting down frontend...");
    
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].state != STATE_NEW) {
            disconnect_client(i);
        }
    }
    
    if (listen_socket >= 0) {
        close(listen_socket);
    }
    
    if (daemon_mode) {
        closelog();
    }
}

/*
 * Print usage
 */
static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [options]\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -d              Run as daemon\n");
    fprintf(stderr, "  -p port         Listen port (default: %d)\n", FRONTEND_PORT);
    fprintf(stderr, "  -s path         Connect to Unix socket (instead of TCP)\n");
    fprintf(stderr, "  -b host         Backend host (default: 127.0.0.1)\n");
    fprintf(stderr, "  -P port         Backend port (default: 4000)\n");
    fprintf(stderr, "  -u user         Run as user after binding\n");
    fprintf(stderr, "  -h              Show this help\n");
}

/*
 * Main function
 */
int main(int argc, char *argv[]) {
    int opt;
    int daemon = 0;
    const char *user = NULL;
    
    while ((opt = getopt(argc, argv, "dp:s:b:P:u:h")) != -1) {
        switch (opt) {
            case 'd':
                daemon = 1;
                break;
            case 'p':
                frontend_port = atoi(optarg);
                break;
            case 's':
                backend_socket = optarg;
                break;
            case 'b':
                backend_host = optarg;
                break;
            case 'P':
                backend_port = atoi(optarg);
                break;
            case 'u':
                user = optarg;
                break;
            case 'h':
                usage(argv[0]);
                return 0;
            default:
                usage(argv[0]);
                return 1;
        }
    }
    
    /* Initialize client array */
    memset(clients, 0, sizeof(clients));
    int i;
    for (i = 0; i < MAX_CLIENTS; i++) {
        clients[i].state = STATE_NEW;
        clients[i].socket = -1;
        clients[i].pty_master = -1;
        clients[i].child_pid = -1;
    }
    
    /* Setup signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN); /* We'll reap manually */
    
    /* Initialize frontend socket */
    init_frontend(frontend_port);
    
    /* Daemonize if requested */
    if (daemon) {
        daemonize();
    }
    
    /* Drop privileges if requested */
    if (user) {
        drop_privileges(user);
    }
    
    log_msg("Empire frontend started (PID: %d)", getpid());
    
    /* Run main loop */
    main_loop();
    
    /* Cleanup */
    cleanup();
    
    log_msg("Empire frontend stopped");
    
    return 0;
}
