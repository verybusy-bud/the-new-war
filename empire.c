/*
 * SPDX-FileCopyrightText: Copyright (C) 1987, 1988 Chuck Simmons
 * SPDX-License-Identifier: GPL-2.0+
 *
 * See the file COPYING, distributed with empire, for restriction
 * and warranty information.
 */

/*
empire.c -- this file contains initialization code, the main command
parser, and the simple commands.
*/

#include "empire.h"
#include "extern.h"
#include <stdio.h>

gamestate_t game;

void c_examine(void), c_movie(void), check_endgame(void), show_title(void);

/*
 * 03a 01Apr88 aml .Hacked movement algorithms for computer.
 * 02b 01Jun87 aml .First round of bug fixes.
 * 02a 01Jan87 aml .Translated to C.
 * 01b 27May85 cal .Fixed round number update bug. Made truename simple.
 * 01a 01Sep83 cal .Taken from a Decus tape
 */

void empire(void) {
	void do_command(char);
	void print_zoom(view_map_t *);

	char order;
	int turn = 0;

	ttinit(); /* init tty */
	rndini(); /* init random number generator */

	/* Show title screen with player colors */
	show_title();

	if (!restore_game()) /* try to restore previous game */ {
		init_game(); /* otherwise init a new game */
	}

	/* Command loop starts here. */
	for (;;) {                   /* until user quits */
		if (game.automove) { /* don't ask for cmd in auto mode */
			/* Process all player turns in automove mode */
			int players_processed = 0;
			
			/* Always start from player 1 in automove */
			game.current_player = 0;
			int start_player = game.current_player;
			
			for (int i = 0; i < game.num_players; i++) {
				int player_idx = (start_player + i) % game.num_players;
				if (game.player[player_idx].alive) {
					game.current_player = player_idx;
					/* Check if this player is AI-controlled */
					if (game.ai_mask & (1 << player_idx)) {
						comp_move(1); /* AI player uses computer logic */
					} else {
						user_move();
					}
					players_processed++;
				}
			}
			
			/* Check if game is over after all players have moved */
			check_endgame();
			
			/* Check if game is over */
			if (players_processed <= 1 || game.win != no_win) {
				/* If 1 or 0 players left, exit automove */
				game.automove = false;
				game.current_player = start_player; /* restore current player before continuing */
				continue; /* Skip to next iteration and exit the for loop */
			} else {
				game.current_player = start_player; /* reset to original player */
				if (++turn % game.save_interval == 0) {
					save_game();
				}
				continue; /* Continue to next iteration to check for keyboard input */
			}
		} else {
			prompt(""); /* blank top line */
			redisplay();
			
			/* Display whose turn it is */
			if (game.player[game.current_player].alive) {
				/* Check if current player is AI-controlled */
				if (game.ai_mask & (1 << game.current_player)) {
					comp_move(1); /* AI player uses computer logic */
					/* Move to next player after AI turn */
					game.current_player++;
					if (game.current_player >= game.num_players) {
						game.current_player = 0;
						turn++;
						if (turn % game.save_interval == 0) {
							save_game();
						}
					}
				} else {
					prompt("%s's orders? ", game.player[game.current_player].name);
					order = get_chx(); /* get a command */
					do_command(order);
				}
			}
			
			/* Check if game is over after each turn */
			check_endgame();
			
			/* Move to next player (skip if automove was just activated) */
			if (!game.automove) {
				game.current_player++;
				if (game.current_player >= game.num_players) {
					game.current_player = 0; /* back to first player */
					turn++;
					if (turn % game.save_interval == 0) {
						save_game();
					}
				}
			}
		}
	}
}

/*
Execute a command.
*/

void do_command(char orders) {
	void c_debug(char order), c_quit(void), c_sector(void), c_map(void);
	void c_give(void);

	int ncycle;

	switch (orders) {
	case 'A': /* turn on auto move mode */
		game.automove = true;
		game.current_player = 0; /* reset to first player */
		error("Now in Auto-Mode for all players");
		break;

	case 'C': /* show cities */
		c_sector();
		break;

	case 'D': /* display round number */
		error("Round #%d", game.date);
		break;

	case 'E': /* end turn - force immediate advancement */
		error("Ending %s's turn", game.player[game.current_player].name);
		/* Don't increment here - main loop will do it */
		break;

	case 'F': /* print map to file */
		c_map();
		break;

	case 'G': /* game info */
		error("Players: %d, Current: %s", game.num_players, game.player[game.current_player].name);
		break;

	case 'H': /* help */
		help(help_cmd, cmd_lines);
		break;

	case 'I': /* info about current player */
		error("Current player: %s", game.player[game.current_player].name);
		break;

	case 'J': /* edit mode */
		ncycle = cur_sector();
		if (ncycle == -1)
			ncycle = 0;
		edit(sector_loc(ncycle));
		break;

	case 'M': /* move */
		user_move();
		save_game();
		break;

	case 'N': /* next player */
		error("Moving to next player");
		break;

	case 'Y': /* end turn */
		error("Ending %s's turn", game.player[game.current_player].name);
		break;

	case 'P': /* print a sector */
		c_sector();
		break;

	case '\026': /* some interrupt */
	case 'Q':    /* quit */
		c_quit();
		break;

	case 'R': /* restore game */
		clear_screen();
		restore_game();
		break;

	case 'S': /* save game */
		save_game();
		break;

	case 'T': /* trace: toggle game.save_movie flag */
		game.save_movie = !game.save_movie;
		if (game.save_movie) {
			comment("Saving movie screens to 'empmovie.dat'.");
		} else {
			comment("No longer saving movie screens.");
		}
		break;

	case 'W': /* watch movie */
		if (game.resigned || game.debug)
			replay_movie();
		else
			error("You cannot watch movie until game is over.");
		break;

	case 'Z': /* print compressed map */
		print_zoom(game.user_map);
		break;

	case '\014': /* redraw the screen */
		redraw();
		break;

	case '+': { /* change debug state */
		char e = get_chx();
		if (e == '+') {
			game.debug = true;
		} else if (e == '-') {
			game.debug = false;
		} else {
			huh();
		}
	} break;

	default:
		if (game.debug) {
			c_debug(orders); /* debug */
		} else {
			huh(); /* illegal command */
		}
		break;
	}
}

/*
Give an unowned city (if any) to the computer.  We make
a list of unowned cities, choose one at random, and mark
it as the computers.
*/

void c_give(void) {
	int unowned[NUM_CITY];
	count_t i, count;

	count = 0; /* nothing in list yet */
	for (i = 0; i < NUM_CITY; i++) {
		if (game.city[i].owner == UNOWNED) {
			unowned[count] = i; /* remember this city */
			count += 1;
		}
	}
	if (count == 0) {
		error("There are no unowned cities.");
		ksend("There are no unowned cities.");
		return;
	}
	i = irand(count);
	i = unowned[i]; /* get city index */
	game.city[i].owner = COMP;
	game.city[i].prod = NOPIECE;
	game.city[i].work = 0;
	scan(game.comp_map, game.city[i].loc);
}

/*
Debugging commands should be implemented here.
The order cannot be any legal command.
*/

void c_debug(char order) {
	char e;

	switch (order) {
	case '#':
		c_examine();
		break;
	case '%':
		c_movie();
		break;

	case '@': /* change trace state */
		e = get_chx();
		if (e == '+') {
			game.trace_pmap = true;
		} else if (e == '-') {
			game.trace_pmap = false;
		} else {
			huh();
		}
		break;

	case '$': /* change game.print_debug state */
		e = get_chx();
		if (e == '+') {
			game.print_debug = true;
		} else if (e == '-') {
			game.print_debug = false;
		} else {
			huh();
		}
		break;

	case '&': /* change game.print_vmap state */
		game.print_vmap = get_chx();
		break;

	default:
		huh();
		break;
	}
}

/*
The quit command.  Make sure the user really wants to quit.
*/

void c_quit(void) {
	if (getyn("QUIT - Are you sure? ")) {
		empend();
	}
}

/*
Print a sector.  Read the sector number from the user
and print it.
*/

void c_sector(void) {
	int num;

	num = get_range("Sector number? ", 0, NUM_SECTORS - 1);
	print_sector_u(num);
}

/*
Print the map to a file.  We ask for a filename, attempt to open the
file, and if successful, print out the user's information to the file.
We print the map sideways to make it easier for the user to print
out the map.
*/

void c_map(void) {
	FILE *f;
	int i, j;
	char line[MAP_HEIGHT + 2];

	prompt("Filename? ");
	get_str(game.jnkbuf, STRSIZE);

	f = fopen(game.jnkbuf, "wb");
	if (f == NULL) {
		error("I can't open that file.");
		return;
	}
	for (i = 0; i < MAP_WIDTH; i++) {               /* for each column */
		for (j = MAP_HEIGHT - 1; j >= 0; j--) { /* for each row */
			line[MAP_HEIGHT - 1 - j] =
			    game.user_map[row_col_loc(j, i)].contents;
		}
		j = MAP_HEIGHT - 1;
		while (j >= 0 &&
		       line[j] == ' ') /* scan off trailing blanks */ {
			j -= 1;
		}

		line[++j] = '\n';
		line[++j] = 0; /* trailing null */
		(void)fputs(line, f);
	}
	(void)fclose(f);
}

/*
Allow user to examine the computer's map.
*/

void c_examine(void) {
	int num;

	num = get_range("Sector number? ", 0, NUM_SECTORS - 1);
	print_sector_c(num);
}

/*
We give the computer lots of free moves and
Print a "zoomed" version of the computer's map.
*/

void c_movie(void) {
	for (;;) {
		comp_move(1);
		print_zoom(game.comp_map);
		save_game();
#ifdef PROFILE
		if (game.date == 125)
			empend();
#endif
	}
}

/*
 * Show title screen with player colors
 */
void show_title(void) {
	kill_display();
	
	pos_str(7, 0, "THE NEW WAR, Version 1.1 site Benjamin Klosterman 14-Feb-2026");
	pos_str(8, 0, "Detailed directions are on the empire manual page\n");
	pos_str(9, 0, "");
	
	pos_str(10, 0, "General 1: Red Forces");
	pos_str(11, 0, "General 2: Yellow Forces");
	pos_str(12, 0, "General 3: Purple Forces");
	pos_str(13, 0, "General 4: White Forces");
	
	pos_str(15, 0, "");
	pos_str(16, 0, "There are %d Generals joining us today", game.num_players);
	pos_str(17, 0, "");
	pos_str(18, 0, "Press any key to continue...");
	redisplay();
	get_chx(); /* wait for keypress */
}

/* end */
