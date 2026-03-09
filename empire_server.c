/*
 * empire_server.c -- Authoritative game server daemon
 *
 * This is the persistent daemon that maintains the world state.
 * It listens on a UNIX domain socket or TCP port for client connections.
 * All game logic runs here; clients merely send commands and receive updates.
 *
 * Architecture:
 * - Maintains authoritative game state
 * - Uses file locking for atomic world writes
 * - Logs all mutations
 * - Handles multiple concurrent client sessions
 * - Persists across client disconnects
 */

#include "empire.h"
#include "extern.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <syslog.h>
#include <time.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pwd.h>
#include <grp.h>

#define SERVER_SOCKET_PATH "/var/run/empire/server.sock"
#define SERVER_PID_FILE "/var/run/empire/server.pid"
#define SERVER_LOG_FILE "/var/log/empire/server.log"
#define WORLD_DATA_DIR "/var/lib/empire"
#define WORLD_FILE "/var/lib/empire/world.dat"
#define NATION_DIR "/var/lib/empire/nations"
#define LOG_DIR "/var/log/empire"

#define MAX_CLIENTS 16
#define BUF_SIZE 8192
#define MAX_INPUT_LEN 256

/* Client connection states */
enum client_state {
    STATE_DISCONNECTED = 0,
    STATE_CONNECTED,
    STATE_AUTHENTICATED,
    STATE_PLAYING,
    STATE_SPECTATING
};

/* IPC protocol commands */
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
#define CMD_MESSAGE 'S'
#define CMD_ERROR 'R'

/* Client connection structure */
typedef struct {
    int socket;
    enum client_state state;
    int player_id; /* -1 = none, 0-3 = player slot */
    char username[32];
    char nation[32];
    char input_buffer[MAX_INPUT_LEN];
    int input_len;
    char output_buffer[BUF_SIZE];
    int output_len;
    int output_pos;
    time_t connect_time;
    time_t last_activity;
    int ping_count;
    struct sockaddr_storage addr;
    socklen_t addr_len;
} client_t;

/* Global server state */
static client_t clients[MAX_CLIENTS];
static int server_socket = -1;
static int running = 1;
static int daemon_mode = 0;
static int use_unix_socket = 1;
static int server_port = 4000;
static char *socket_path = NULL;
static FILE *log_fp = NULL;
static int world_fd = -1; /* File descriptor for world lock */

/* Server mode flag for game engine */
int server_mode = 0;

/* Forward declarations */
static void log_message(const char *fmt, ...);
static void init_tcp_server(const char *bind_addr, int port);
static void init_unix_socket(const char *path);
static void daemonize(void);
static void write_pid_file(void);
static void remove_pid_file(void);
static void signal_handler(int sig);
static void accept_new_connection(void);
static void handle_client_input(int client_idx);
static void handle_client_output(int client_idx);
static void disconnect_client(int client_idx);
static void send_to_client(int client_idx, const char *data, int len);
static void broadcast_message(const char *data, int len, int exclude_idx);
static int find_player_slot(const char *username);
static void lock_world(void);
static void unlock_world(void);
static void atomic_save_world(void);
static int load_world(void);
static void process_command(int client_idx, const char *cmd, int len);
static void send_map_data(int client_idx);
static void send_player_info(int client_idx);
static void cleanup_resources(void);
static void drop_privileges(const char *user);

/*
 * Log a message to syslog and/or file
 */
static void log_message(const char *fmt, ...) {
    va_list args;
    char buf[1024];
    time_t now;
    struct tm *tm_info;
    
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    
    now = time(NULL);
    tm_info = localtime(&now);
    
    if (daemon_mode) {
        syslog(LOG_INFO, "%s", buf);
    } else {
        fprintf(stderr, "[%02d:%02d:%02d] %s\n", 
                tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec, buf);
    }
    
    if (log_fp) {
        fprintf(log_fp, "%04d-%02d-%02d %02d:%02d:%02d %s\n",
                tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
                tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec, buf);
        fflush(log_fp);
    }
}

/*
 * Initialize the TCP server
 */
static void init_tcp_server(const char *bind_addr, int port) {
    int opt = 1;
    struct sockaddr_in addr;
    
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        log_message("socket: %s", strerror(errno));
        exit(1);
    }
    
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        log_message("setsockopt: %s", strerror(errno));
        exit(1);
    }
    
    /* Set non-blocking */
    fcntl(server_socket, F_SETFL, O_NONBLOCK);
    
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = bind_addr ? inet_addr(bind_addr) : htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    
    if (bind(server_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_message("bind: %s", strerror(errno));
        exit(1);
    }
    
    if (listen(server_socket, 10) < 0) {
        log_message("listen: %s", strerror(errno));
        exit(1);
    }
    
    log_message("Server listening on %s:%d", 
                bind_addr ? bind_addr : "127.0.0.1", port);
}

/*
 * Initialize UNIX domain socket
 */
static void init_unix_socket(const char *path) {
    struct sockaddr_un addr;
    
    /* Remove old socket file if exists */
    unlink(path);
    
    server_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_socket < 0) {
        log_message("socket: %s", strerror(errno));
        exit(1);
    }
    
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    
    if (bind(server_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_message("bind: %s", strerror(errno));
        exit(1);
    }
    
    if (listen(server_socket, 10) < 0) {
        log_message("listen: %s", strerror(errno));
        exit(1);
    }
    
    /* Set permissions so empire group can access */
    chmod(path, 0660);
    
    log_message("Server listening on Unix socket %s", path);
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
        /* Parent exits */
        exit(0);
    }
    
    /* Child continues */
    if (setsid() < 0) {
        perror("setsid");
        exit(1);
    }
    
    /* Fork again to prevent acquiring controlling terminal */
    pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }
    if (pid > 0) {
        exit(0);
    }
    
    /* Change working directory */
    chdir("/");
    
    /* Redirect standard file descriptors to /dev/null */
    freopen("/dev/null", "r", stdin);
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    
    daemon_mode = 1;
    openlog("empire_server", LOG_PID, LOG_DAEMON);
}

/*
 * Write PID file
 */
static void write_pid_file(void) {
    FILE *fp = fopen(SERVER_PID_FILE, "w");
    if (fp) {
        fprintf(fp, "%d\n", getpid());
        fclose(fp);
    }
}

/*
 * Remove PID file
 */
static void remove_pid_file(void) {
    unlink(SERVER_PID_FILE);
}

/*
 * Signal handler
 */
static void signal_handler(int sig) {
    log_message("Received signal %d, shutting down...", sig);
    running = 0;
}

/*
 * Accept a new client connection
 */
static void accept_new_connection(void) {
    struct sockaddr_storage client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int new_socket;
    int i;
    
    new_socket = accept(server_socket, (struct sockaddr *)&client_addr, &addr_len);
    if (new_socket < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            log_message("accept: %s", strerror(errno));
        }
        return;
    }
    
    /* Find a free client slot */
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].state == STATE_DISCONNECTED) {
            memset(&clients[i], 0, sizeof(client_t));
            clients[i].socket = new_socket;
            clients[i].state = STATE_CONNECTED;
            clients[i].player_id = -1;
            clients[i].connect_time = time(NULL);
            clients[i].last_activity = time(NULL);
            memcpy(&clients[i].addr, &client_addr, addr_len);
            clients[i].addr_len = addr_len;
            
            /* Set non-blocking and TCP_NODELAY */
            fcntl(new_socket, F_SETFL, O_NONBLOCK);
            int flag = 1;
            setsockopt(new_socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
            
            log_message("Client %d connected from %s", i, 
                        inet_ntoa(((struct sockaddr_in *)&client_addr)->sin_addr));
            
            /* Send greeting */
            const char *greeting = "EMPIRE_SERVER 1.0\n";
            send_to_client(i, greeting, strlen(greeting));
            return;
        }
    }
    
    /* No free slots */
    log_message("Connection rejected: maximum clients reached");
    close(new_socket);
}

/*
 * Handle input from a client
 */
static void handle_client_input(int client_idx) {
    char buf[256];
    int n;
    client_t *c = &clients[client_idx];
    
    n = recv(c->socket, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            log_message("recv from client %d: %s", client_idx, strerror(errno));
        }
        if (n == 0) {
            disconnect_client(client_idx);
        }
        return;
    }
    
    c->last_activity = time(NULL);
    
    /* Add to input buffer */
    if (c->input_len + n < MAX_INPUT_LEN) {
        memcpy(c->input_buffer + c->input_len, buf, n);
        c->input_len += n;
        
        /* Process complete commands (newline terminated) */
        char *p;
        while ((p = memchr(c->input_buffer, '\n', c->input_len)) != NULL) {
            int cmd_len = p - c->input_buffer;
            process_command(client_idx, c->input_buffer, cmd_len);
            
            /* Remove processed command from buffer */
            c->input_len -= (cmd_len + 1);
            memmove(c->input_buffer, p + 1, c->input_len);
        }
        
        /* Prevent buffer overflow - clear if too full */
        if (c->input_len > MAX_INPUT_LEN - 256) {
            c->input_len = 0;
        }
    }
}

/*
 * Handle output to a client
 */
static void handle_client_output(int client_idx) {
    client_t *c = &clients[client_idx];
    int remaining = c->output_len - c->output_pos;
    int n;
    
    if (remaining <= 0) {
        return;
    }
    
    if (c->socket < 0) {
        disconnect_client(client_idx);
        return;
    }
    
    n = send(c->socket, c->output_buffer + c->output_pos, remaining, 0);
    if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            disconnect_client(client_idx);
        }
        return;
    }
    
    c->output_pos += n;
    
    if (c->output_pos >= c->output_len) {
        c->output_len = 0;
        c->output_pos = 0;
    }
}

/*
 * Disconnect a client
 */
static void disconnect_client(int client_idx) {
    client_t *c = &clients[client_idx];
    
    if (c->state == STATE_DISCONNECTED) {
        return;
    }
    
    log_message("Client %d (%s) disconnected", client_idx, 
                c->username[0] ? c->username : "unknown");
    
    close(c->socket);
    c->socket = -1;
    c->state = STATE_DISCONNECTED;
    c->player_id = -1;
}

/*
 * Send data to a specific client
 */
static void send_to_client(int client_idx, const char *data, int len) {
    client_t *c = &clients[client_idx];
    int space = BUF_SIZE - c->output_len;
    
    if (len > space) {
        len = space;
    }
    
    if (len > 0) {
        memcpy(c->output_buffer + c->output_len, data, len);
        c->output_len += len;
    }
}

/*
 * Broadcast message to all connected clients except one
 */
static void broadcast_message(const char *data, int len, int exclude_idx) {
    int i;
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].state != STATE_DISCONNECTED && i != exclude_idx) {
            send_to_client(i, data, len);
        }
    }
}

/*
 * Find a free player slot for a username
 */
static int find_player_slot(const char *username) {
    int i;
    int slot = -1;
    
    /* First check if user already has a slot */
    for (i = 0; i < game.num_players; i++) {
        if (clients[i].state != STATE_DISCONNECTED && 
            strcmp(clients[i].username, username) == 0) {
            return clients[i].player_id;
        }
    }
    
    /* Find first free slot */
    for (i = 0; i < game.num_players; i++) {
        int taken = 0;
        int j;
        for (j = 0; j < MAX_CLIENTS; j++) {
            if (clients[j].state != STATE_DISCONNECTED && 
                clients[j].player_id == i) {
                taken = 1;
                break;
            }
        }
        if (!taken) {
            slot = i;
            break;
        }
    }
    
    return slot;
}

/*
 * Process a command from a client
 */
static void process_command(int client_idx, const char *cmd, int len) {
    client_t *c = &clients[client_idx];
    char response[512];
    
    if (len == 0) return;
    
    /* Parse command */
    char cmd_type = cmd[0];
    const char *args = (len > 1) ? cmd + 1 : "";
    
    switch (cmd_type) {
        case CMD_LOGIN:
            /* Format: Lusername:nation */
            {
                char *colon = strchr(args, ':');
                if (colon) {
                    *colon = '\0';
                    strncpy(c->username, args, sizeof(c->username) - 1);
                    strncpy(c->nation, colon + 1, sizeof(c->nation) - 1);
                    
                    c->player_id = find_player_slot(c->username);
                    if (c->player_id >= 0) {
                        c->state = STATE_PLAYING;
                        log_message("Client %d logged in as %s (nation: %s, player_id: %d)",
                                    client_idx, c->username, c->nation, c->player_id);
                        snprintf(response, sizeof(response), "OK %d\n", c->player_id);
                    } else {
                        c->state = STATE_SPECTATING;
                        log_message("Client %d spectating as %s", client_idx, c->username);
                        snprintf(response, sizeof(response), "SPECTATOR\n");
                    }
                } else {
                    snprintf(response, sizeof(response), "ERROR invalid login format\n");
                }
            }
            break;
            
        case CMD_PING:
            snprintf(response, sizeof(response), "PONG\n");
            break;
            
        case CMD_QUIT:
            disconnect_client(client_idx);
            return;
            
        case CMD_MAP_DATA:
            if (c->state == STATE_PLAYING || c->state == STATE_SPECTATING) {
                send_map_data(client_idx);
            }
            snprintf(response, sizeof(response), "OK\n");
            break;
            
        case CMD_INFO:
            send_player_info(client_idx);
            snprintf(response, sizeof(response), "OK\n");
            break;
            
        default:
            /* Pass through to game logic for movement commands */
            if (c->state == STATE_PLAYING && c->player_id >= 0) {
                /* Lock world, process move, unlock, save */
                lock_world();
                /* TODO: Integrate with game move logic */
                unlock_world();
                atomic_save_world();
                log_message("Player %d move: %c%s", c->player_id, cmd_type, args);
            }
            snprintf(response, sizeof(response), "OK\n");
            break;
    }
    
    send_to_client(client_idx, response, strlen(response));
}

/*
 * Send map data to a client
 */
static void send_map_data(int client_idx) {
    client_t *c = &clients[client_idx];
    char buf[1024];
    view_map_t *vmap;
    int i, j;
    
    if (c->player_id < 0) {
        vmap = game.user_map; /* Spectators see player 1 view */
    } else {
        vmap = (c->player_id == 0) ? game.user_map : 
               (c->player_id == 1) ? game.user2_map :
               (c->player_id == 2) ? game.user3_map : game.user4_map;
    }
    
    /* Send map dimensions */
    snprintf(buf, sizeof(buf), "MAP %d %d\n", MAP_WIDTH, MAP_HEIGHT);
    send_to_client(client_idx, buf, strlen(buf));
    
    /* Send map data in chunks */
    for (j = 0; j < MAP_HEIGHT; j++) {
        char row[MAP_WIDTH + 2];
        for (i = 0; i < MAP_WIDTH; i++) {
            row[i] = vmap[j * MAP_WIDTH + i].contents;
            if (row[i] == ' ') row[i] = '.';
        }
        row[MAP_WIDTH] = '\n';
        row[MAP_WIDTH + 1] = '\0';
        send_to_client(client_idx, row, MAP_WIDTH + 1);
    }
    
    snprintf(buf, sizeof(buf), "ENDMAP\n");
    send_to_client(client_idx, buf, strlen(buf));
}

/*
 * Send player information
 */
static void send_player_info(int client_idx) {
    client_t *c = &clients[client_idx];
    char buf[512];
    int i;
    
    snprintf(buf, sizeof(buf), "PLAYERS %d\n", game.num_players);
    send_to_client(client_idx, buf, strlen(buf));
    
    for (i = 0; i < game.num_players; i++) {
        snprintf(buf, sizeof(buf), "%d %s %d %d\n", 
                 i, game.player[i].name, 
                 game.player[i].alive,
                 game.player[i].score);
        send_to_client(client_idx, buf, strlen(buf));
    }
    
    snprintf(buf, sizeof(buf), "TURN %ld\n", game.date);
    send_to_client(client_idx, buf, strlen(buf));
    
    snprintf(buf, sizeof(buf), "CURRENT %d\n", game.current_player);
    send_to_client(client_idx, buf, strlen(buf));
    
    snprintf(buf, sizeof(buf), "ENDPLAYERS\n");
    send_to_client(client_idx, buf, strlen(buf));
}

/*
 * Lock the world file for exclusive access
 */
static void lock_world(void) {
    if (world_fd < 0) {
        world_fd = open(WORLD_FILE ".lock", O_RDWR | O_CREAT, 0644);
    }
    if (world_fd >= 0) {
        flock(world_fd, LOCK_EX);
    }
}

/*
 * Unlock the world file
 */
static void unlock_world(void) {
    if (world_fd >= 0) {
        flock(world_fd, LOCK_UN);
    }
}

/*
 * Atomically save the world state
 */
static void atomic_save_world(void) {
    char tmpfile[256];
    FILE *f;
    
    snprintf(tmpfile, sizeof(tmpfile), "%s.tmp", WORLD_FILE);
    
    /* Save to temp file */
    f = fopen(tmpfile, "wb");
    if (!f) {
        log_message("Cannot create temp save file: %s", strerror(errno));
        return;
    }
    
    /* Write game state */
    /* TODO: Implement full serialization */
    fwrite(&game, sizeof(game), 1, f);
    fclose(f);
    
    /* Atomic rename */
    if (rename(tmpfile, WORLD_FILE) != 0) {
        log_message("Failed to rename save file: %s", strerror(errno));
        unlink(tmpfile);
    } else {
        log_message("World saved atomically");
    }
}

/*
 * Load the world state
 */
static int load_world(void) {
    FILE *f = fopen(WORLD_FILE, "rb");
    if (!f) {
        log_message("No existing world, creating new");
        return 0;
    }
    
    /* TODO: Implement full deserialization with validation */
    if (fread(&game, sizeof(game), 1, f) != 1) {
        fclose(f);
        log_message("Failed to load world");
        return 0;
    }
    
    fclose(f);
    log_message("World loaded successfully");
    return 1;
}

/*
 * Drop privileges to specified user
 */
static void drop_privileges(const char *user) {
    struct passwd *pw = getpwnam(user);
    if (!pw) {
        log_message("Cannot find user %s", user);
        exit(1);
    }
    
    if (setgid(pw->pw_gid) != 0 || setuid(pw->pw_uid) != 0) {
        log_message("Failed to drop privileges: %s", strerror(errno));
        exit(1);
    }
    
    log_message("Dropped privileges to %s", user);
}

/*
 * Cleanup resources
 */
static void cleanup_resources(void) {
    int i;
    
    /* Disconnect all clients */
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].state != STATE_DISCONNECTED) {
            send_to_client(i, "SHUTDOWN\n", 9);
            disconnect_client(i);
        }
    }
    
    /* Save world */
    lock_world();
    atomic_save_world();
    unlock_world();
    
    if (world_fd >= 0) {
        close(world_fd);
    }
    
    if (server_socket >= 0) {
        close(server_socket);
    }
    
    if (use_unix_socket && socket_path) {
        unlink(socket_path);
    }
    
    remove_pid_file();
    
    if (log_fp) {
        fclose(log_fp);
    }
    
    if (daemon_mode) {
        closelog();
    }
}

/*
 * Main server loop
 */
static void server_loop(void) {
    struct pollfd fds[MAX_CLIENTS + 1];
    int nfds;
    int i;
    time_t last_save = time(NULL);
    
    while (running) {
        /* Setup poll structure */
        nfds = 1;
        fds[0].fd = server_socket;
        fds[0].events = POLLIN;
        
        for (i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].state != STATE_DISCONNECTED) {
                fds[nfds].fd = clients[i].socket;
                fds[nfds].events = POLLIN;
                if (clients[i].output_len > clients[i].output_pos) {
                    fds[nfds].events |= POLLOUT;
                }
                nfds++;
            }
        }
        
        /* Poll with timeout */
        int ret = poll(fds, nfds, 100); /* 100ms timeout */
        if (ret < 0) {
            if (errno != EINTR) {
                log_message("poll: %s", strerror(errno));
            }
            continue;
        }
        
        /* Check for new connections */
        if (fds[0].revents & POLLIN) {
            accept_new_connection();
        }
        
        /* Handle client events */
        int fd_idx = 1;
        for (i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].state == STATE_DISCONNECTED) {
                continue;
            }
            
            if (fds[fd_idx].revents & POLLIN) {
                handle_client_input(i);
            }
            
            if (fds[fd_idx].revents & POLLOUT) {
                handle_client_output(i);
            }
            
            if (fds[fd_idx].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                disconnect_client(i);
            }
            
            /* Check for timeout */
            if (time(NULL) - clients[i].last_activity > 300) { /* 5 min timeout */
                log_message("Client %d timed out", i);
                disconnect_client(i);
            }
            
            fd_idx++;
        }
        
        /* Periodic world save */
        if (time(NULL) - last_save > 60) { /* Save every minute */
            lock_world();
            atomic_save_world();
            unlock_world();
            last_save = time(NULL);
        }
    }
}

/*
 * Print usage
 */
static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [options]\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -d          Run as daemon\n");
    fprintf(stderr, "  -t          Use TCP instead of Unix socket\n");
    fprintf(stderr, "  -p port     TCP port (default: 4000)\n");
    fprintf(stderr, "  -s path     Unix socket path (default: %s)\n", SERVER_SOCKET_PATH);
    fprintf(stderr, "  -u user     Run as user after binding\n");
    fprintf(stderr, "  -h          Show this help\n");
}

/*
 * Main function
 */
int main(int argc, char *argv[]) {
    int opt;
    int daemon = 0;
    int tcp_mode = 0;
    int port = 4000;
    const char *user = NULL;
    
    while ((opt = getopt(argc, argv, "dtp:s:u:h")) != -1) {
        switch (opt) {
            case 'd':
                daemon = 1;
                break;
            case 't':
                tcp_mode = 1;
                break;
            case 'p':
                port = atoi(optarg);
                break;
            case 's':
                socket_path = optarg;
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
    
    /* Setup signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGHUP, signal_handler);
    signal(SIGPIPE, SIG_IGN);
    
    /* Create directories if needed */
    /* TODO: These should be created by install script with proper permissions */
    
    /* Open log file */
    if (!daemon) {
        log_fp = fopen("empire_server.log", "a");
    } else {
        log_fp = fopen(SERVER_LOG_FILE, "a");
    }
    
    /* Initialize game state with proper defaults */
    memset(&game, 0, sizeof(game));
    game.num_players = 4; /* Default to 4 players */
    
    /* Set map generation parameters (must match main.c defaults) */
    game.SMOOTH = 5;              /* map smoothing iterations */
    game.WATER_RATIO = 70;        /* 70% water */
    game.delay_time = 0;          /* no delay in server mode */
    game.save_interval = 10;      /* save every 10 turns */
    game.savefile = "tnw.sav";    /* default save file for 4 players */
    
    /* Compute min city distance (same formula as main.c) */
    int land = MAP_SIZE * (100 - game.WATER_RATIO) / 100;
    land /= NUM_CITY;
    game.MIN_CITY_DIST = isqrt(land);
    
    /* Set server mode flag so game init knows not to use ncurses */
    server_mode = 1;
    
    /* Load existing world or create new */
    if (!load_world()) {
        /* Initialize new game */
        extern void init_game(void);
        extern void rndini(void);
        log_message("Seeding random number generator...");
        rndini(); /* Seed random number generator */
        log_message("Initializing game world...");
        init_game();
        log_message("Game world initialized, saving...");
        atomic_save_world();
        log_message("World saved successfully");
    } else {
        log_message("World loaded from save file");
    }
    
    /* Initialize server socket */
    if (tcp_mode) {
        use_unix_socket = 0;
        init_tcp_server("127.0.0.1", port);
    } else {
        if (!socket_path) {
            socket_path = strdup(SERVER_SOCKET_PATH);
        }
        init_unix_socket(socket_path);
    }
    
    /* Daemonize if requested */
    if (daemon) {
        daemonize();
    }
    
    /* Write PID file */
    write_pid_file();
    
    /* Drop privileges if requested */
    if (user) {
        drop_privileges(user);
    }
    
    log_message("Empire server started (PID: %d)", getpid());
    
    /* Initialize client array */
    memset(clients, 0, sizeof(clients));
    
    /* Run server loop */
    server_loop();
    
    /* Cleanup */
    cleanup_resources();
    
    log_message("Empire server stopped");
    
    return 0;
}
