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

    -w water: percentage of map that is water.  Must be in the range
              10..90.  Default is 70.

    -s smooth: amount of smoothing performed to generate map.  Must
               be a nonnegative integer.  Default is 5.

    -d delay:  number of milliseconds to delay between output.
               default is 2000 (2 seconds).

    -S saveinterval: sets turn interval between saves.
               default is 10

    -p players: number of human players (1-4).  Default is 2.
    
    -a ai_mask: bitmask for AI players (e.g., 1010 for P2 and P4 as AI).
                Default is 0000 (all human).
*/

#include "empire.h"
#include "extern.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define OPTFLAGS "w:s:d:S:f:p:a:"

int main(int argc, char *argv[]) {
	int c;
	extern char *optarg;
	extern int optind;
	int errflg = 0;
	int wflg, sflg, dflg, Sflg, pflg;
	int aflg = 0; /* AI mask - default no AI players */
	int land;

	wflg = 70; /* set defaults */
	sflg = 5;
	dflg = 2000;
	Sflg = 10;
	pflg = 2; /* default to 2 players for hotseat */
	game.savefile = "empire.sav";
	game.ai_mask = 0; /* default: all human players */

	/*
	 * extract command line options
	 */

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
		(void)printf("empire: usage: empire [-w water] [-s smooth] [-d "
		             "delay] [-p players] [-a ai_mask] [-f savefile]\n");
		(void)printf("  -a ai_mask: 4-bit bitmask for AI players (e.g., 1010 for P2,P4 as AI)\n");
		exit(1);
	}

	if (wflg < 10 || wflg > 90) {
		(void)printf(
		    "empire: -w argument must be in the range 0..90.\n");
		exit(1);
	}
	if (sflg < 0) {
		(void)printf(
		    "empire: -s argument must be greater or equal to zero.\n");
		exit(1);
	}

	if (dflg < 0 || dflg > 30000) {
		(void)printf(
		    "empire: -d argument must be in the range 0..30000.\n");
		exit(1);
	}

	if (pflg < 1 || pflg > 4) {
		(void)printf(
		    "empire: -p argument must be in the range 1..4.\n");
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
	land /= NUM_CITY;                                 /* land per city */
	game.MIN_CITY_DIST = isqrt(land); /* distance between cities */

	empire(); /* call main routine */
	return (0);
}
