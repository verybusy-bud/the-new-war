/*
 * empire_client.c -- Empire game client
 *
 * This client connects to the empire_server and provides:
 * - Terminal handling
 * - User interface
 * - Command processing
 * - Display rendering
 *
 * It runs in a PTY allocated by empire_frontend, giving the
 * game a real terminal experience while being network-transparent.
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
#include <sys/un.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <time.h>
#include <ctype.h>

#define BUF_SIZE 8192
#define MAX_INPUT_LEN 256

/* Protocol commands */
#define CMD_LOGIN 'L'
#define CMD_LOGOUT 'X'
#define CMD_MOVE 'M'
#define CMD_ATTACK 'A'
#define CMD_BUILD 'B'
#define CMD_SET_FUNCTION 'F'
#define CMD_INFO 'I'
#define CMD_END_TURN 'E'
#define CMD_QUIT 'Q'
#define CMD_PING 'P'
#define CMD_MAP_DATA 'D'

/* Client states */
enum client_state {
    STATE_CONNECTING = 0,
    STATE_LOGIN,
    STATE_PLAYING,
    STATE_DISCONNECTED
};

/* Client session */
typedef struct {
    int socket;
    enum client_state state;
    int player_id;
    char username[32];
    char nation[32];
    
    /* Buffers */
    char input_buf[MAX_INPUT_LEN];
    int input_len;
    
    /* Network buffers */
    char net_in_buf[BUF_SIZE];
    int net_in_len;
    
    /* Terminal */
    struct termios orig_termios;
    int term_width;
    int term_height;
} client_t;

static client_t client;
static int running = 1;

/* Forward declarations */
static void die(const char *fmt, ...);
static void init_terminal(void);
static void restore_terminal(void);
static void signal_handler(int sig);
static int connect_to_server(const char *host, int port, const char *socket_path);
static void disconnect_from_server(void);
static void send_to_server(const char *data);
static void send_command(char cmd, const char *args);
static void poll_network(void);
static void handle_server_data(const char *data, int len);
static void process_input(void);

/*
 * Print error and exit
 */
static void die(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    exit(1);
}

/*
 * Initialize terminal
 */
static void init_terminal(void) {
    struct termios tios;
    
    tcgetattr(STDIN_FILENO, &client.orig_termios);
    
    tios = client.orig_termios;
    tios.c_lflag &= ~(ICANON | ECHO | ISIG);
    tios.c_iflag &= ~(IXON | ICRNL);
    tios.c_oflag &= ~OPOST;
    tios.c_cc[VMIN] = 1;
    tios.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &tios);
    
    struct winsize ws;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0) {
        client.term_width = ws.ws_col;
        client.term_height = ws.ws_row;
    } else {
        client.term_width = 80;
        client.term_height = 24;
    }
}

/*
 * Restore terminal
 */
static void restore_terminal(void) {
    tcsetattr(STDIN_FILENO, TCSANOW, &client.orig_termios);
}

/*
 * Signal handler
 */
static void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        running = 0;
    }
}

/*
 * Connect to the empire server
 */
static int connect_to_server(const char *host, int port, const char *socket_path) {
    int sock;
    int flags;
    
    if (socket_path && socket_path[0]) {
        struct sockaddr_un addr;
        sock = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock < 0) {
            die("socket: %s", strerror(errno));
        }
        
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
        
        if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            close(sock);
            die("connect to %s: %s", socket_path, strerror(errno));
        }
    } else {
        struct sockaddr_in addr;
        
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            die("socket: %s", strerror(errno));
        }
        
        flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);
        
        int opt = 1;
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
        
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        
        if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
            close(sock);
            die("Invalid address: %s", host);
        }
        
        if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            if (errno != EINPROGRESS) {
                close(sock);
                die("connect to %s:%d: %s", host, port, strerror(errno));
            }
            
            struct pollfd fds;
            fds.fd = sock;
            fds.events = POLLOUT;
            
            int ret = poll(&fds, 1, 10000);
            if (ret <= 0) {
                close(sock);
                die("Connection timed out");
            }
            
            int so_error;
            socklen_t len = sizeof(so_error);
            getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
            if (so_error != 0) {
                close(sock);
                die("Connection failed: %s", strerror(so_error));
            }
        }
        
        fcntl(sock, F_SETFL, flags);
    }
    
    return sock;
}

/*
 * Disconnect from server
 */
static void disconnect_from_server(void) {
    if (client.socket >= 0) {
        close(client.socket);
        client.socket = -1;
    }
    client.state = STATE_DISCONNECTED;
}

/*
 * Send data to server
 */
static void send_to_server(const char *data) {
    int len = strlen(data);
    int sent = 0;
    
    while (sent < len) {
        int n = send(client.socket, data + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(1000);
                continue;
            }
            die("send: %s", strerror(errno));
        }
        sent += n;
    }
}

/*
 * Send a command to server
 */
static void send_command(char cmd, const char *args) {
    char buf[512];
    if (args && args[0]) {
        snprintf(buf, sizeof(buf), "%c%s\n", cmd, args);
    } else {
        snprintf(buf, sizeof(buf), "%c\n", cmd);
    }
    send_to_server(buf);
}

/*
 * Handle input from user
 */
static void process_input(void) {
    char c;
    int n = read(STDIN_FILENO, &c, 1);
    
    if (n <= 0) {
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            running = 0;
        }
        return;
    }
    
    if (c == '\003') {
        running = 0;
        return;
    }
    
    if (c == '\r' || c == '\n') {
        if (client.input_len > 0) {
            client.input_buf[client.input_len] = '\0';
            
            char cmd = toupper(client.input_buf[0]);
            char *args = client.input_buf + 1;
            while (*args && isspace(*args)) args++;
            
            switch (cmd) {
                case 'M':
                case 'A':
                case 'B':
                case 'F':
                case 'E':
                case 'Q':
                    send_command(cmd, args);
                    break;
                case 'H':
                    printf("\r\nCommands: M=Move, A=Attack, B=Build, F=Function, E=End Turn, Q=Quit, H=Help\r\n");
                    break;
                default:
                    printf("\r\nUnknown command: %c\r\n", cmd);
                    break;
            }
            
            client.input_len = 0;
        }
    } else if (c == '\b' || c == 127) {
        if (client.input_len > 0) {
            client.input_len--;
            printf("\b \b");
            fflush(stdout);
        }
    } else if (c >= 32 && c < 127) {
        if (client.input_len < MAX_INPUT_LEN - 1) {
            client.input_buf[client.input_len++] = c;
            putchar(c);
            fflush(stdout);
        }
    }
}

/*
 * Poll network for data
 */
static void poll_network(void) {
    struct pollfd fds;
    int ret;
    char buf[BUF_SIZE];
    
    fds.fd = client.socket;
    fds.events = POLLIN;
    
    ret = poll(&fds, 1, 10);
    if (ret < 0) {
        if (errno != EINTR) {
            die("poll: %s", strerror(errno));
        }
        return;
    }
    
    if (ret == 0) {
        return;
    }
    
    if (fds.revents & POLLIN) {
        int n = recv(client.socket, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            if (n == 0) {
                die("Server closed connection");
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                die("recv: %s", strerror(errno));
            }
            return;
        }
        buf[n] = '\0';
        handle_server_data(buf, n);
    }
    
    if (fds.revents & (POLLERR | POLLHUP | POLLNVAL)) {
        die("Connection error");
    }
}

/*
 * Handle data from server
 */
static void handle_server_data(const char *data, int len) {
    if (client.net_in_len + len < BUF_SIZE) {
        memcpy(client.net_in_buf + client.net_in_len, data, len);
        client.net_in_len += len;
        client.net_in_buf[client.net_in_len] = '\0';
    }
    
    char *line = client.net_in_buf;
    char *end;
    
    while ((end = strchr(line, '\n')) != NULL) {
        *end = '\0';
        
        if (strncmp(line, "OK", 2) == 0) {
            /* Acknowledgment */
        } else if (strncmp(line, "ERROR", 5) == 0) {
            printf("\r\nServer error: %s\r\n", line + 6);
        } else if (strncmp(line, "SHUTDOWN", 8) == 0) {
            die("Server is shutting down");
        } else {
            printf("\r\n%s\r\n", line);
        }
        
        line = end + 1;
    }
    
    int remaining = client.net_in_buf + client.net_in_len - line;
    if (remaining > 0) {
        memmove(client.net_in_buf, line, remaining);
    }
    client.net_in_len = remaining;
}

/*
 * Show login prompt
 */
static void show_login_prompt(void) {
    printf("\033[2J\033[H");
    printf("\033[10;20H=== EMPIRE CLIENT ===");
    printf("\033[12;20HEnter your name: ");
    fflush(stdout);
    
    if (fgets(client.username, sizeof(client.username), stdin)) {
        char *p = strchr(client.username, '\n');
        if (p) *p = '\0';
    }
    
    printf("\033[14;20HEnter nation name: ");
    fflush(stdout);
    
    if (fgets(client.nation, sizeof(client.nation), stdin)) {
        char *p = strchr(client.nation, '\n');
        if (p) *p = '\0';
    }
}

/*
 * Print usage
 */
static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [options]\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -h host       Server host (default: 127.0.0.1)\n");
    fprintf(stderr, "  -p port       Server port (default: 4000)\n");
    fprintf(stderr, "  -s path       Unix socket path\n");
    fprintf(stderr, "  -i ip         Client IP address\n");
    fprintf(stderr, "  -u username   Username\n");
    fprintf(stderr, "  -n nation     Nation name\n");
    fprintf(stderr, "  --help        Show this help\n");
}

/*
 * Main function
 */
int main(int argc, char *argv[]) {
    int opt;
    const char *host = "127.0.0.1";
    int port = 4000;
    const char *socket_path = NULL;
    const char *username = NULL;
    const char *nation = NULL;
    
    for (opt = 1; opt < argc; opt++) {
        if (strcmp(argv[opt], "-h") == 0 && opt + 1 < argc) {
            host = argv[++opt];
        } else if (strcmp(argv[opt], "-p") == 0 && opt + 1 < argc) {
            port = atoi(argv[++opt]);
        } else if (strcmp(argv[opt], "-s") == 0 && opt + 1 < argc) {
            socket_path = argv[++opt];
        } else if (strcmp(argv[opt], "-i") == 0 && opt + 1 < argc) {
            opt++; /* Skip IP */
        } else if (strcmp(argv[opt], "-u") == 0 && opt + 1 < argc) {
            username = argv[++opt];
        } else if (strcmp(argv[opt], "-n") == 0 && opt + 1 < argc) {
            nation = argv[++opt];
        } else if (strcmp(argv[opt], "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);
    atexit(restore_terminal);
    
    init_terminal();
    
    if (!username) {
        show_login_prompt();
    } else {
        strncpy(client.username, username, sizeof(client.username) - 1);
        if (nation) {
            strncpy(client.nation, nation, sizeof(client.nation) - 1);
        } else {
            strncpy(client.nation, client.username, sizeof(client.nation) - 1);
        }
    }
    
    client.socket = connect_to_server(host, port, socket_path);
    client.state = STATE_LOGIN;
    
    char login_cmd[128];
    snprintf(login_cmd, sizeof(login_cmd), "%s:%s", client.username, client.nation);
    send_command(CMD_LOGIN, login_cmd);
    
    printf("\r\nConnected to Empire server. Type H for help.\r\n");
    
    while (running) {
        poll_network();
        process_input();
        usleep(1000);
    }
    
    send_command(CMD_QUIT, NULL);
    disconnect_from_server();
    
    printf("\r\nGoodbye!\r\n");
    
    return 0;
}
