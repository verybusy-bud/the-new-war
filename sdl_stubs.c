/*
 * SDL Stub Functions - compiled without ncurses
 * These provide stub implementations needed for non-SDL build
 */

#include <stdbool.h>
#include "empire.h"

int cbreak(void) { return 0; }
int nocbreak(void) { return 0; }
int beep(void) { return 0; }

void sdl_clear_screen(void) {}
void sdl_draw_map(view_map_t *vmap, int player) { (void)vmap; (void)player; }
void sdl_refresh(void) {}
int sdl_get_input(void) { return 0; }
int sdl_wait_input(void) { return 0; }
int sdl_draw_sector(view_map_t *vmap, int sector_row, int sector_col) { (void)vmap; (void)sector_row; (void)sector_col; return 0; }
bool is_sdl_active(void) { return false; }
