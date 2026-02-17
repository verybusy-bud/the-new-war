/*
 * SPDX-FileCopyrightText: Copyright (C) 1987, 1988 Chuck Simmons
 * SPDX-License-Identifier: GPL-2.0+
 *
 * See the file COPYING, distributed with empire, for restriction
 * and warranty information.
 */

/*
main.c -- parse command line for empire

options:

-w water: percentage of map that is water. Must be in the range
10..90. Default is 70.

-s smooth: amount of smoothing performed to generate map. Must
be a nonnegative integer. Default is 5.

-d delay: number of milliseconds to delay between output.
default is 2000 (2 seconds).

-S saveinterval: sets turn interval between saves.
default is 10

-p players: number of human players (1-4). Default is 2.

-a ai_mask: bitmask for AI players (e.g., 1010 for P2 and P4 as AI).
Default is 0000 (all human).

--server: Run as network server (clients connect remotely)
--port PORT: Server port (default 6666)
--spectate: Allow spectators to watch the game
*/

#include "empire.h"
#include "extern.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define OPTFLAGS "w:s:d:S:f:p:a:"
#define SERVER_PORT 6666

int main(int argc, char *argv[]) {
int c;
extern char *optarg;
extern int optind;
extern int opterr;
int errflg = 0;
int wflg, sflg, dflg, Sflg, pflg;
int aflg = 0; /* AI mask - default no AI players */
int land;
int server_mode = 0;
int server_port = SERVER_PORT;
int new_argc = 0;
char **new_argv;

wflg = 70; /* set defaults */
sflg = 5;
dflg = 2000;
Sflg = 10;
pflg = 2; /* default to 2 players for hotseat */
game.savefile = "empire.sav";
game.ai_mask = 0; /* default: all human players */

/* Filter out --server and --port options, getopt doesn't handle long options */
new_argv = malloc((argc + 1) * sizeof(char *));
new_argv[new_argc++] = argv[0]; /* program name */

for (int i = 1; i < argc; i++) {
if (strcmp(argv[i], "--server") == 0) {
server_mode = 1;
} else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
server_port = atoi(argv[i + 1]);
i++;
} else {
new_argv[new_argc++] = argv[i];
}
}
new_argv[new_argc] = NULL;

/* Use filtered arguments for getopt */
argc = new_argc;
argv = new_argv;

/*
 * extract command line options
 */

opterr = 0; /* Suppress getopt error messages */
while ((c = getopt(argc, argv, OPTFLAGS)) != EOF) {
		switch (c) {
		case 'w':
			wflg = atoi(optarg);
			break;
		case 's':
			sflg = atoi(optarg);
			break;
		case 'd':
			dflg = atoi(optarg);
			break;
		case 'S':
			Sflg = atoi(optarg);
			break;
		case 'f':
			game.savefile = optarg;
			break;
		case 'p':
			pflg = atoi(optarg);
			break;
		case 'a':
			aflg = atoi(optarg);
			if (aflg < 0 || aflg > 15) {
				(void)printf("empire: -a argument must be in the range 0..15 (4-bit bitmask).\n");
				exit(1);
			}
			game.ai_mask = aflg;
			break;
		case '?': /* illegal option? */
			errflg++;
			break;
		}
	}
if (errflg || (argc - optind) != 0) {
(void)printf("empire: usage: empire [options]\n");
(void)printf("Options:\n");
(void)printf("  -w water      : percentage of map that is water (10-90, default 70)\n");
(void)printf("  -s smooth     : map smoothing iterations (default 5)\n");
(void)printf("  -d delay      : delay between output in ms (default 2000)\n");
(void)printf("  -S interval   : turns between autosaves (default 10)\n");
(void)printf("  -p players    : number of players (1-4, default 2)\n");
(void)printf("  -a ai_mask    : AI player bitmask (0-15, e.g., 1010 for P2,P4 as AI)\n");
(void)printf("  -f savefile   : save file name\n");
(void)printf("  --server      : run as network server (default port %d)\n", SERVER_PORT);
(void)printf("  --port PORT   : custom server port\n");
free(new_argv);
exit(1);
}

if (wflg < 10 || wflg > 90) {
(void)printf(
"empire: -w argument must be in the range 0..90.\n");
free(new_argv);
exit(1);
}
if (sflg < 0) {
(void)printf(
"empire: -s argument must be greater or equal to zero.\n");
free(new_argv);
exit(1);
}

if (dflg < 0 || dflg > 30000) {
(void)printf(
"empire: -d argument must be in the range 0..30000.\n");
free(new_argv);
exit(1);
}

if (pflg < 1 || pflg > 4) {
(void)printf(
"empire: -p argument must be in the range 1..4.\n");
free(new_argv);
exit(1);
}

	game.SMOOTH = sflg;
	game.WATER_RATIO = wflg;
	game.delay_time = dflg;
	game.save_interval = Sflg;
	game.num_players = pflg;

	/* Set default savefile based on player count if not specified */
	if (game.savefile == NULL || strcmp(game.savefile, "empire.sav") == 0) {
		switch (pflg) {
		case 2:
			game.savefile = "empire.sav";
			break;
		case 3:
			game.savefile = "tw.sav";
			break;
		case 4:
			game.savefile = "tnw.sav";
			break;
		default:
			game.savefile = "empire.sav";
			break;
		}
	}

/* compute min distance between cities */
land = MAP_SIZE * (100 - game.WATER_RATIO) / 100; /* available land */
land /= NUM_CITY; /* land per city */
game.MIN_CITY_DIST = isqrt(land); /* distance between cities */

/* Initialize server mode if requested */
if (server_mode) {
printf("Starting server mode on port %d\n", server_port);
printf("The game will wait for all %d players to connect.\n\n", pflg);
fflush(stdout);
init_server(server_port);
}

empire(); /* call main routine */

/* Shutdown server if in server mode */
if (server_mode) {
server_shutdown();
}

free(new_argv);
return (0);
}
