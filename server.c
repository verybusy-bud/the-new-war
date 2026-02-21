/*
 * server.c -- Network server for multiplayer empire
 *
 * This allows players to connect remotely over TCP instead of
 * using hotseat mode on a single machine.
 */

#include "empire.h"
#include "extern.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>

#define SERVER_PORT 6666
#define MAX_CLIENTS 4
#define BUF_SIZE 4096

/* Client connection states */
enum client_state {
    STATE_DISCONNECTED = 0,
    STATE_CONNECTED,
    STATE_PLAYING,
    STATE_SPECTATING
};

/* Client connection structure */
typedef struct {
    int socket;
    enum client_state state;
    int player_id;  /* Which player slot this client controls (-1 for none) */
    char name[32];
    char input_buffer[256];
    int input_len;
    char output_buffer[BUF_SIZE];
    int output_len;
    int output_pos;
} client_t;

static client_t clients[MAX_CLIENTS];
static int server_socket = -1;
static int server_mode = 0;

/* Message buffer for remote display */
static char msg_buffer[STRSIZE];
static int msg_pending = 0;

/* Forward declarations */
static void init_server_socket(int port);
static void accept_new_connection(void);
static void handle_client_input(int client_idx);
static void handle_client_output(int client_idx);
static void disconnect_client(int client_idx);
/* static void broadcast_message(const char *msg); */
static void send_to_client(int client_idx, const char *data);
/* static void serialize_game_state(int client_idx); */

/*
 * Initialize server mode
 */
void init_server(int port) {
    server_mode = 1;
    memset(clients, 0, sizeof(clients));
    init_server_socket(port);
    printf("Server listening on port %d\n", port);
}

/*
 * Check if running in server mode
 */
int is_server_mode(void) {
    return server_mode;
}

/*
 * Initialize the server socket
 */
static void init_server_socket(int port) {
    int opt = 1;
    struct sockaddr_in addr;

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("socket");
        exit(1);
    }

    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(1);
    }

    /* Set non-blocking */
    fcntl(server_socket, F_SETFL, O_NONBLOCK);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }

    if (listen(server_socket, 5) < 0) {
        perror("listen");
        exit(1);
    }
}

/*
 * Accept a new client connection
 */
static void accept_new_connection(void) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int new_socket;
    int i;

    new_socket = accept(server_socket, (struct sockaddr *)&client_addr, &addr_len);
    if (new_socket < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("accept");
        }
        return;
    }

    /* Find a free client slot */
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].state == STATE_DISCONNECTED) {
            clients[i].socket = new_socket;
            clients[i].state = STATE_CONNECTED;
            clients[i].player_id = -1;
            clients[i].input_len = 0;
            clients[i].output_len = 0;
            clients[i].output_pos = 0;
fcntl(new_socket, F_SETFL, O_NONBLOCK);

/* Disable Nagle's algorithm for low latency */
int flag = 1;
setsockopt(new_socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

/* Assign player slot if available */
if (i < game.num_players) {
clients[i].player_id = i;
snprintf(clients[i].name, sizeof(clients[i].name),
"Player %d", i + 1);
clients[i].state = STATE_PLAYING;
} else {
clients[i].state = STATE_SPECTATING;
snprintf(clients[i].name, sizeof(clients[i].name),
"Spectator %d", i - game.num_players + 1);
}

            printf("Client %d connected from %s as %s\n",
                   i, inet_ntoa(client_addr.sin_addr), clients[i].name);

            /* Send welcome message */
            send_to_client(i, "Welcome to The New War!\r\n");
            if (clients[i].player_id >= 0) {
                send_to_client(i, "You are ");
                send_to_client(i, game.player[clients[i].player_id].name);
                send_to_client(i, "\r\n");
            } else {
                send_to_client(i, "You are spectating.\r\n");
            }
            send_to_client(i, "Waiting for game to start...\r\n");
            return;
        }
    }

    /* No free slots */
    printf("Connection rejected: maximum clients reached\n");
    close(new_socket);
}

/*
 * Poll for network events
 * Returns -1 if should quit, 0 otherwise
 */
int server_poll(void) {
    struct pollfd fds[MAX_CLIENTS + 1];
    int nfds = 1;
    int i, ret;

    if (!server_mode || server_socket < 0) {
        return 0;
    }

    /* Setup poll structure */
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

    ret = poll(fds, nfds, 0);  /* Non-blocking poll */
    if (ret < 0) {
        if (errno != EINTR) {
            perror("poll");
        }
        return 0;
    }

    /* Check server socket for new connections */
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

        fd_idx++;
    }

    return 0;
}

/*
 * Handle input from a client
 */
static void handle_client_input(int client_idx) {
    char buf[256];
    int n;

    n = recv(clients[client_idx].socket, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("recv");
        }
        if (n == 0) {
            disconnect_client(client_idx);
        }
        return;
    }

/* Skip telnet negotiation sequences (IAC = 0xFF) */
int i = 0;
while (i < n) {
unsigned char c = buf[i];

/* Telnet IAC - skip negotiation sequence */
if (c == 0xFF && i + 2 < n) {
/* IAC + command + option - skip 3 bytes */
i += 3;
continue;
}

/* Skip other control characters but handle normal input */
if (c < 32 && c != '\r' && c != '\n' && c != '\b' && c != 127) {
i++;
continue;
}

/* Handle special characters */
if (c == '\r' || c == '\n') {
/* Line terminator - add a single newline marker */
if (clients[client_idx].input_len < sizeof(clients[client_idx].input_buffer) - 1) {
clients[client_idx].input_buffer[clients[client_idx].input_len++] = '\n';
}
i++;
/* Skip the matching \r or \n if present (handles \r\n pairs) */
if (i < n) {
unsigned char next = buf[i];
if ((c == '\r' && next == '\n') || (c == '\n' && next == '\r')) {
i++;
}
}
} else if (c == '\b' || c == 127) {
/* Backspace */
if (clients[client_idx].input_len > 0) {
clients[client_idx].input_len--;
}
i++;
} else if (c >= 32 && c < 127) {
/* Printable character */
if (clients[client_idx].input_len < sizeof(clients[client_idx].input_buffer) - 1) {
clients[client_idx].input_buffer[clients[client_idx].input_len++] = c;
}
i++;
}
}
}

/*
 * Handle output to a client
 */
static void handle_client_output(int client_idx) {
    int remaining = clients[client_idx].output_len - clients[client_idx].output_pos;
    int n;

    if (remaining <= 0) {
        return;
    }

/* Check if socket is still valid before sending */
if (clients[client_idx].socket < 0) {
disconnect_client(client_idx);
return;
}

n = send(clients[client_idx].socket,
clients[client_idx].output_buffer + clients[client_idx].output_pos,
remaining, 0);

if (n < 0) {
if (errno != EAGAIN && errno != EWOULDBLOCK) {
/* Silently disconnect on error - don't spam console */
disconnect_client(client_idx);
}
return;
}

    clients[client_idx].output_pos += n;

    /* If all sent, reset buffer */
    if (clients[client_idx].output_pos >= clients[client_idx].output_len) {
        clients[client_idx].output_len = 0;
        clients[client_idx].output_pos = 0;
    }
}

/*
 * Disconnect a client
 */
static void disconnect_client(int client_idx) {
    if (clients[client_idx].state == STATE_DISCONNECTED) {
        return;
    }

    printf("Client %d (%s) disconnected\n", client_idx, clients[client_idx].name);
    close(clients[client_idx].socket);
    clients[client_idx].socket = -1;
    clients[client_idx].state = STATE_DISCONNECTED;
    clients[client_idx].player_id = -1;
}

/*
 * Send data to a specific client
 */
static void send_to_client(int client_idx, const char *data) {
    int len = strlen(data);
    int space = BUF_SIZE - clients[client_idx].output_len;

    if (len > space) {
        len = space;
    }

    if (len > 0) {
        memcpy(clients[client_idx].output_buffer + clients[client_idx].output_len,
               data, len);
        clients[client_idx].output_len += len;
    }
}

/*
 * Broadcast a message to all connected clients
 */
void server_broadcast(const char *msg) {
int i;
if (!server_mode) {
return;
}
for (i = 0; i < MAX_CLIENTS; i++) {
if (clients[i].state != STATE_DISCONNECTED) {
send_to_client(i, msg);
}
}
}

/*
 * Check if there is input waiting from the current player's client
 */
int server_has_input(int player_id) {
int i;

if (!server_mode) {
return 0;
}

for (i = 0; i < MAX_CLIENTS; i++) {
if (clients[i].state != STATE_DISCONNECTED && clients[i].player_id == player_id) {
if (clients[i].input_len > 0) {
return 1;
}
}
}

return 0;
}

/*
 * Get a character from the current player's client
 */
char server_get_char(int player_id) {
    int i;
    char c;

    if (!server_mode) {
        return '\0';
    }

    for (i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].state != STATE_DISCONNECTED && clients[i].player_id == player_id) {
            if (clients[i].input_len > 0) {
                c = clients[i].input_buffer[0];
                /* Shift buffer */
                memmove(clients[i].input_buffer, clients[i].input_buffer + 1,
                        clients[i].input_len - 1);
                clients[i].input_len--;
                return c;
            }
        }
    }

    return '\0';
}

/*
 * Send the game state/display to a client
 * This serializes the current view_map and sends it as ASCII
 */
void server_send_display(int player_id, view_map_t *vmap) {
int i, j;
char buf[MAP_WIDTH + 3];
int client_idx = -1;

if (!server_mode) {
return;
}

/* Find the client for this player */
for (i = 0; i < MAX_CLIENTS; i++) {
if (clients[i].state != STATE_DISCONNECTED && clients[i].player_id == player_id) {
client_idx = i;
break;
}
}

if (client_idx < 0) {
return;  /* No client for this player */
}

/* Send clear screen sequence */
send_to_client(client_idx, "\033[2J\033[H");

/* Send game info header */
    snprintf(buf, sizeof(buf), "The New War - %s's View - Round %ld\r\n",
             game.player[player_id].name, game.date);
    send_to_client(client_idx, buf);
    snprintf(buf, sizeof(buf), "Current turn: %s\r\n\r\n",
             game.player[game.current_player].name);
    send_to_client(client_idx, buf);

    /* Send the map */
    for (j = 0; j < MAP_HEIGHT && j < 40; j++) {  /* Limit to 40 rows for telnet */
        for (i = 0; i < MAP_WIDTH && i < 80; i++) {  /* Limit to 80 cols */
            buf[i] = vmap[j * MAP_WIDTH + i].contents;
            if (buf[i] == ' ') buf[i] = '.';
        }
        buf[i] = '\r';
        buf[i+1] = '\n';
        buf[i+2] = '\0';
        send_to_client(client_idx, buf);
    }

    /* Send any pending message */
    if (msg_pending && msg_buffer[0]) {
        send_to_client(client_idx, "\r\n");
        send_to_client(client_idx, msg_buffer);
        send_to_client(client_idx, "\r\n");
        msg_pending = 0;
    }

/* Send prompt */
if (game.current_player == player_id && game.player[player_id].alive) {
send_to_client(client_idx, "\r\nYour orders? ");
} else {
send_to_client(client_idx, "\r\nWaiting for your turn...\r\n");
}

/* Flush output immediately */
handle_client_output(client_idx);
}

/*
 * Set a message to be sent with the next display update
 */
void server_set_message(const char *fmt, ...) {
    va_list args;

    if (!server_mode) {
        return;
    }

    va_start(args, fmt);
    vsnprintf(msg_buffer, sizeof(msg_buffer), fmt, args);
    va_end(args);
    msg_pending = 1;
}

/*
 * Close all client connections and server socket
 */
void server_shutdown(void) {
    int i;

    if (!server_mode) {
        return;
    }

    for (i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].state != STATE_DISCONNECTED) {
            send_to_client(i, "\r\nServer shutting down. Goodbye!\r\n");
            disconnect_client(i);
        }
    }

    if (server_socket >= 0) {
        close(server_socket);
        server_socket = -1;
    }

    server_mode = 0;
    printf("Server shut down.\n");
}

/*
 * Check if all human players are connected
 */
int server_all_players_connected(void) {
    int i, connected;

    if (!server_mode) {
        return 1;  /* In local mode, always ready */
    }

    connected = 0;
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].state != STATE_DISCONNECTED && clients[i].player_id >= 0) {
            connected++;
        }
    }

    return connected >= game.num_players;
}

/*
 * Get number of connected clients
 */
int server_num_connected(void) {
    int i, count = 0;

    for (i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].state != STATE_DISCONNECTED) {
            count++;
        }
    }

    return count;
}

/*
 * Wait for a key from any connected client
 * Used during initialization or when waiting for players
 */
void server_wait_for_key(void) {
    if (!server_mode) {
        return;
    }

    while (1) {
        server_poll();

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].state != STATE_DISCONNECTED && clients[i].input_len > 0) {
                return;
            }
        }

        usleep(10000);  /* 10ms */
    }
}
