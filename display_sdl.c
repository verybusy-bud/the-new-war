/*
 * SDL2 Display Module for Empire - GUI Version
 * Uses colored rectangles instead of BMPs
 */

#include "empire.h"
#include "extern.h"
#include <SDL2/SDL.h>

#define SCREEN_WIDTH 1200
#define SCREEN_HEIGHT 800
#define TILE_SIZE 16
#define MAP_OFFSET_X 20
#define MAP_OFFSET_Y 5

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static bool sdl_initialized = false;

static int current_sector_row = 0;
static int current_sector_col = 0;

typedef struct {
    Uint8 r, g, b, a;
} Color;

Color get_player_color(int owner) {
    Color c = {128, 128, 128, 255};
    switch(owner) {
        case 1: c = (Color){139, 69, 19, 255}; break;
        case 2: c = (Color){255, 255, 0, 255}; break;
        case 3: c = (Color){255, 0, 0, 255}; break;
        case 4: c = (Color){255, 255, 255, 255}; break;
        case 5: c = (Color){0, 255, 0, 255}; break;
    }
    return c;
}

void init_sdl_display(void) {
    /* SDL is already initialized in term_sdl.c */
    /* This function exists for compatibility but does nothing */
}

void close_sdl_display(void) {
    /* SDL is closed in term_sdl.c */
}

void sdl_clear_screen(void) {
    if (!sdl_initialized) return;
    SDL_SetRenderDrawColor(renderer, 0x00, 0x00, 0x00, 0xFF);
    SDL_RenderClear(renderer);
}

void sdl_draw_rect(int x, int y, int w, int h, Color c) {
    if (!sdl_initialized) return;
    SDL_Rect rect = {x, y, w, h};
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(renderer, &rect);
}

void sdl_draw_tile(int x, int y, char contents, int owner) {
    if (!sdl_initialized) return;
    
    int screen_x = MAP_OFFSET_X + x * TILE_SIZE;
    int screen_y = MAP_OFFSET_Y + y * TILE_SIZE;
    
    Color terrain_color = {0, 0, 128, 255};
    if (contents == '*' || contents == 'X' || contents == '+') {
        terrain_color = (Color){34, 139, 34, 255};
    } else if (contents == 'O') {
        terrain_color = (Color){0, 0, 139, 255};
    }
    
    sdl_draw_rect(screen_x, screen_y, TILE_SIZE, TILE_SIZE, terrain_color);
    
    if (owner > 0 && owner <= 4) {
        Color player_color = get_player_color(owner);
        int padding = 2;
        sdl_draw_rect(screen_x + padding, screen_y + padding, 
                      TILE_SIZE - 2*padding, TILE_SIZE - 2*padding, player_color);
    }
}

void sdl_draw_map(view_map_t *vmap, int player) {
    if (!sdl_initialized) return;
    
    for (int y = 0; y < MAP_HEIGHT; y++) {
        for (int x = 0; x < MAP_WIDTH; x++) {
            int loc = y * MAP_WIDTH + x;
            char contents = vmap[loc].contents;
            int owner = 0;
            
            if (game.real_map[loc].cityp) {
                owner = game.real_map[loc].cityp->owner;
            } else if (contents != ' ' && contents != '.' && contents != '-') {
                piece_info_t *p = find_obj_at_loc(loc);
                if (p) owner = p->owner;
            }
            
            sdl_draw_tile(x, y, contents, owner);
        }
    }
}

void sdl_refresh(void) {
    if (!sdl_initialized) return;
    SDL_RenderPresent(renderer);
}

int sdl_get_input(void) {
    if (!sdl_initialized) return -1;
    
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) return 'q';
        if (e.type == SDL_KEYDOWN) {
            SDL_Keycode key = e.key.keysym.sym;
            if (key >= SDLK_a && key <= SDLK_z) return key;
            if (key >= SDLK_0 && key <= SDLK_9) return key;
            switch(key) {
                case SDLK_UP: case SDLK_KP_8: return '8';
                case SDLK_DOWN: case SDLK_KP_2: return '2';
                case SDLK_LEFT: case SDLK_KP_4: return '4';
                case SDLK_RIGHT: case SDLK_KP_6: return '6';
                case SDLK_KP_7: return '7';
                case SDLK_KP_9: return '9';
                case SDLK_KP_1: return '1';
                case SDLK_KP_3: return '3';
                case SDLK_RETURN: case SDLK_KP_ENTER: return '\n';
                case SDLK_ESCAPE: return 27;
                default: return key;
            }
        }
    }
    return -1;
}

int sdl_wait_input(void) {
    if (!sdl_initialized) return -1;
    
    SDL_Event e;
    while (SDL_WaitEvent(&e)) {
        if (e.type == SDL_QUIT) return 'q';
        if (e.type == SDL_KEYDOWN) {
            SDL_Keycode key = e.key.keysym.sym;
            if (key >= SDLK_a && key <= SDLK_z) return key;
            if (key >= SDLK_0 && key <= SDLK_9) return key;
            switch(key) {
                case SDLK_UP: case SDLK_KP_8: return '8';
                case SDLK_DOWN: case SDLK_KP_2: return '2';
                case SDLK_LEFT: case SDLK_KP_4: return '4';
                case SDLK_RIGHT: case SDLK_KP_6: return '6';
                case SDLK_KP_7: return '7';
                case SDLK_KP_9: return '9';
                case SDLK_KP_1: return '1';
                case SDLK_KP_3: return '3';
                case SDLK_RETURN: case SDLK_KP_ENTER: return '\n';
                case SDLK_ESCAPE: return 27;
                default: return key;
            }
        }
    }
    return -1;
}

/* is_sdl_active is defined in term_sdl.c */
