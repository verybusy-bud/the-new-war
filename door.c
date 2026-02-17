/*
 * door.c -- LBBS door wrapper for The New War
 *
 * This acts as a bridge between LBBS (stdin/stdout) and the game server (TCP)
 * Usage: door <hostname> <port>
 * Or set TNW_HOST and TNW_PORT environment variables
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>

#define BUFFER_SIZE 4096

int main(int argc, char *argv[]) {
    int sock;
    struct sockaddr_in addr;
    struct hostent *server;
    char *host;
    int port;
    char buffer[BUFFER_SIZE];
    struct pollfd fds[2];
    int running = 1;

    /* Get connection info from args or environment */
    if (argc >= 3) {
        host = argv[1];
        port = atoi(argv[2]);
    } else {
        host = getenv("TNW_HOST");
        if (!host) host = "127.0.0.1";
        port = atoi(getenv("TNW_PORT") ? getenv("TNW_PORT") : "6666");
    }

    /* Create socket */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    /* Resolve hostname */
    server = gethostbyname(host);
    if (server == NULL) {
        fprintf(stderr, "ERROR: Could not resolve host: %s\n", host);
        return 1;
    }

    /* Connect to game server */
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr.s_addr, server->h_addr, server->h_length);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        fprintf(stderr, "ERROR: Failed to connect to %s:%d\n", host, port);
        fprintf(stderr, "Make sure the game server is running.\n");
        return 1;
    }

    /* Main proxy loop */
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[1].fd = sock;
    fds[1].events = POLLIN;

    while (running) {
        int ret = poll(fds, 2, 100); /* 100ms timeout */
        
        if (ret < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }

        /* stdin -> socket (user input to game) */
        if (fds[0].revents & POLLIN) {
            int n = read(STDIN_FILENO, buffer, BUFFER_SIZE);
            if (n > 0) {
                send(sock, buffer, n, 0);
            } else if (n == 0) {
                /* EOF - user disconnected */
                running = 0;
            }
        }

/* socket -> stdout (game output to user) */
if (fds[1].revents & POLLIN) {
int n = recv(sock, buffer, BUFFER_SIZE, 0);
if (n > 0) {
write(STDOUT_FILENO, buffer, n);
fflush(stdout);
} else if (n == 0) {
/* Server closed connection */
running = 0;
}
}

        /* Check for errors */
        if (fds[0].revents & (POLLERR | POLLHUP)) running = 0;
        if (fds[1].revents & (POLLERR | POLLHUP)) running = 0;
    }

    close(sock);
    return 0;
}
