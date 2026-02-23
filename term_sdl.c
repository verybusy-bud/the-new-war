/*
 * SDL2 Terminal Replacement for Empire - GUI Version (No text rendering)
 * Replaces ncurses with SDL2 graphics using colored blocks
 */

#include "empire.h"
#include "extern.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define SCREEN_WIDTH 1200
#define SCREEN_HEIGHT 900
#define TILE_SIZE 12
#define MAP_OFFSET_X 10
#define MAP_OFFSET_Y 50
#define TEXT_LINE_HEIGHT 18
#define MAX_TEXT_LINES 12
#define FONT_SIZE 16

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static bool sdl_initialized = false;

/* BMP surfaces for units and terrain */
static SDL_Surface *bmp_land = NULL;
static SDL_Surface *bmp_sea = NULL;
static SDL_Surface *bmp_city[7] = {NULL};  /* city1-city6 for players 1-6 */
static SDL_Surface *bmp_army[7] = {NULL};  /* a1-a6 for armies */
static SDL_Surface *bmp_fighter[7] = {NULL};  /* f1-f6 for fighters */
static SDL_Surface *bmp_battleship[7] = {NULL};  /* b1-b6 for battleships */
static SDL_Surface *bmp_destroyer[7] = {NULL};  /* d1-d6 for destroyers */
static SDL_Surface *bmp_submarine[7] = {NULL};  /* s1-s6 for submarines */
static SDL_Surface *bmp_transport[7] = {NULL};  /* t1-t6 for transports */
static SDL_Surface *bmp_carrier[7] = {NULL};  /* c1-c6 for carriers */
static SDL_Surface *bmp_marine[7] = {NULL};  /* use army sprites for now */
static SDL_Surface *bmp_bomber[7] = {NULL};  /* use fighter sprites for now */
static SDL_Surface *bmp_satellite[7] = {NULL};  /* use city sprites for now */
static SDL_Surface *bmp_patrol[7] = {NULL};  /* use destroyer sprites for now */
static SDL_Surface *bmp_unknown = NULL;

/* TTF font */
static TTF_Font *font = NULL;

void init_font(void) {
    if (TTF_Init() < 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        return;
    }
    
    font = TTF_OpenFont("/usr/share/fonts/truetype/3270/3270-Regular.ttf", FONT_SIZE);
    if (!font) {
        fprintf(stderr, "TTF_OpenFont failed: %s\n", TTF_GetError());
        return;
    }
}

void draw_text(int x, int y, const char *str) {
    if (!font || !str[0]) return;
    
    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface *surface = TTF_RenderText_Blended(font, str, white);
    if (!surface) return;
    
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture) {
        SDL_Rect dst = {x, y, surface->w, surface->h};
        SDL_RenderCopy(renderer, texture, NULL, &dst);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
}

/* Text buffer for messages */
static char text_buffer[MAX_TEXT_LINES][256];
static int text_lines_used = 0;
static int text_scroll __attribute__((unused)) = 0;

/* Message lines */
static char msg_line1[256] = "";
static char msg_line2[256] = "";
static char msg_line3[256] = "";

static bool need_delay = false;

bool move_cursor(loc_t *cursor, int offset) {
    (void)cursor;
    (void)offset;
    return false;
}

int cbreak(void) { return 0; }
int nocbreak(void) { return 0; }
int beep(void) { return 0; }

/* Colors */
#define C_BLACK 0, 0, 0
#define C_WHITE 255, 255, 255
#define C_RED 255, 0, 0
#define C_GREEN 0, 255, 0
#define C_BLUE 0, 0, 128
#define C_YELLOW 255, 255, 0
#define C_MAGENTA 255, 0, 255
#define C_CYAN 0, 255, 255
#define C_BROWN 139, 69, 19
#define C_GRAY 128, 128, 128
#define C_DARK_GREEN 34, 139, 34
#define C_DARK_BLUE 0, 0, 139
#define C_DARK_GRAY 64, 64, 64

typedef struct { Uint8 r, g, b; } Color;

extern Color get_player_color(int owner);

void draw_rect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b) {
    SDL_Rect rect = {x, y, w, h};
    SDL_SetRenderDrawColor(renderer, r, g, b, 255);
    SDL_RenderFillRect(renderer, &rect);
}

void draw_border(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b) {
    SDL_Rect rect = {x, y, w, h};
    SDL_SetRenderDrawColor(renderer, r, g, b, 255);
    SDL_RenderDrawRect(renderer, &rect);
}

void sdl_init(void) {
    if (sdl_initialized) return;  /* Already initialized */
    
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return;
    }
    
    window = SDL_CreateWindow("Empire - The New War", 
                              SDL_WINDOWPOS_UNDEFINED, 
                              SDL_WINDOWPOS_UNDEFINED,
                              SCREEN_WIDTH, SCREEN_HEIGHT, 
                              SDL_WINDOW_SHOWN);
    
    if (!window) return;
    
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) return;
    
    sdl_initialized = true;
    memset(text_buffer, 0, sizeof(text_buffer));
    text_lines_used = 0;
    
    /* Load BMP images */
    bmp_land = SDL_LoadBMP("land.bmp");
    bmp_sea = SDL_LoadBMP("sea.bmp");
    bmp_unknown = SDL_LoadBMP("unknown.bmp");
    
    /* Load city BMPs for each player (1-6) */
    bmp_city[1] = SDL_LoadBMP("city1.bmp");
    bmp_city[2] = SDL_LoadBMP("city2.bmp");
    bmp_city[3] = SDL_LoadBMP("city3.bmp");
    bmp_city[4] = SDL_LoadBMP("city4.bmp");
    bmp_city[5] = SDL_LoadBMP("city5.bmp");
    bmp_city[6] = SDL_LoadBMP("city6.bmp");
    
    /* Load unit BMPs for each player (1-6) */
    bmp_army[1] = SDL_LoadBMP("a1.bmp");
    bmp_army[2] = SDL_LoadBMP("a2.bmp");
    bmp_army[3] = SDL_LoadBMP("a3.bmp");
    bmp_army[4] = SDL_LoadBMP("a4.bmp");
    bmp_army[5] = SDL_LoadBMP("a5.bmp");
    bmp_army[6] = SDL_LoadBMP("a6.bmp");
    
    bmp_fighter[1] = SDL_LoadBMP("f1.bmp");
    bmp_fighter[2] = SDL_LoadBMP("f2.bmp");
    bmp_fighter[3] = SDL_LoadBMP("f3.bmp");
    bmp_fighter[4] = SDL_LoadBMP("f4.bmp");
    bmp_fighter[5] = SDL_LoadBMP("f5.bmp");
    bmp_fighter[6] = SDL_LoadBMP("f6.bmp");
    
    bmp_battleship[1] = SDL_LoadBMP("b1.bmp");
    bmp_battleship[2] = SDL_LoadBMP("b2.bmp");
    bmp_battleship[3] = SDL_LoadBMP("b3.bmp");
    bmp_battleship[4] = SDL_LoadBMP("b4.bmp");
    bmp_battleship[5] = SDL_LoadBMP("b5.bmp");
    bmp_battleship[6] = SDL_LoadBMP("b6.bmp");
    
    bmp_destroyer[1] = SDL_LoadBMP("d1.bmp");
    bmp_destroyer[2] = SDL_LoadBMP("d2.bmp");
    bmp_destroyer[3] = SDL_LoadBMP("d3.bmp");
    bmp_destroyer[4] = SDL_LoadBMP("d4.bmp");
    bmp_destroyer[5] = SDL_LoadBMP("d5.bmp");
    bmp_destroyer[6] = SDL_LoadBMP("d6.bmp");
    
    bmp_submarine[1] = SDL_LoadBMP("s1.bmp");
    bmp_submarine[2] = SDL_LoadBMP("s2.bmp");
    bmp_submarine[3] = SDL_LoadBMP("s3.bmp");
    bmp_submarine[4] = SDL_LoadBMP("s4.bmp");
    bmp_submarine[5] = SDL_LoadBMP("s5.bmp");
    bmp_submarine[6] = SDL_LoadBMP("s6.bmp");
    
    bmp_transport[1] = SDL_LoadBMP("t1.bmp");
    bmp_transport[2] = SDL_LoadBMP("t2.bmp");
    bmp_transport[3] = SDL_LoadBMP("t3.bmp");
    bmp_transport[4] = SDL_LoadBMP("t4.bmp");
    bmp_transport[5] = SDL_LoadBMP("t5.bmp");
    bmp_transport[6] = SDL_LoadBMP("t6.bmp");
    
    bmp_carrier[1] = SDL_LoadBMP("c1.bmp");
    bmp_carrier[2] = SDL_LoadBMP("c2.bmp");
    bmp_carrier[3] = SDL_LoadBMP("c3.bmp");
    bmp_carrier[4] = SDL_LoadBMP("c4.bmp");
    bmp_carrier[5] = SDL_LoadBMP("c5.bmp");
    bmp_carrier[6] = SDL_LoadBMP("c6.bmp");
    
    /* Use fallback sprites for units without specific sprites */
    for (int i = 1; i <= 6; i++) {
        bmp_marine[i] = bmp_army[i];  /* Marines use army sprites */
        bmp_bomber[i] = bmp_fighter[i];  /* Bombers use fighter sprites */
        bmp_satellite[i] = bmp_city[i];  /* Satellites use city sprites for now */
        bmp_patrol[i] = bmp_destroyer[i];  /* Patrol boats use destroyer sprites */
    }
    
    /* Initialize font texture */
    init_font();
}

void sdl_close(void) {
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    sdl_initialized = false;
}

void sdl_clear(void) {
    if (!sdl_initialized) return;
    SDL_SetRenderDrawColor(renderer, C_BLACK, 255);
    SDL_RenderClear(renderer);
}

void sdl_present(void) {
    if (!sdl_initialized) return;
    SDL_RenderPresent(renderer);
}

void draw_tile(int screen_x, int screen_y, char contents, int owner, int seen, int piece_type) {
    if (!sdl_initialized) return;
    
    SDL_Rect dst = {screen_x, screen_y, TILE_SIZE, TILE_SIZE};
    SDL_Texture *tex = NULL;
    SDL_Surface **bmp_array = NULL;
    
    /* Map owner (1-5) to sprite index (1-6). Owner 5 (COMP) uses index 6. */
    int sprite_idx = (owner >= 1 && owner <= 4) ? owner : (owner == 5 ? 6 : 0);
    
    /* Try to use BMP, fall back to colors */
    if (!seen) {
        if (bmp_unknown) {
            tex = SDL_CreateTextureFromSurface(renderer, bmp_unknown);
        }
        if (!tex) draw_rect(screen_x, screen_y, TILE_SIZE, TILE_SIZE, C_DARK_GRAY);
    } else if (contents == '*' || contents == 'X' || contents == '+') {
        if (bmp_land) tex = SDL_CreateTextureFromSurface(renderer, bmp_land);
        if (!tex) draw_rect(screen_x, screen_y, TILE_SIZE, TILE_SIZE, C_DARK_GREEN);
    } else if (contents == 'O' || contents == ' ' || contents == '.' || contents == '-') {
        if (bmp_sea) tex = SDL_CreateTextureFromSurface(renderer, bmp_sea);
        if (!tex) draw_rect(screen_x, screen_y, TILE_SIZE, TILE_SIZE, C_DARK_BLUE);
    } else if (contents == '^') {
        draw_rect(screen_x, screen_y, TILE_SIZE, TILE_SIZE, C_GRAY);
    } else {
        draw_rect(screen_x, screen_y, TILE_SIZE, TILE_SIZE, C_BLACK);
    }
    
    if (tex) {
        SDL_RenderCopy(renderer, tex, NULL, &dst);
        SDL_DestroyTexture(tex);
    }
    
    /* Draw unit/city on top */
    if (owner > 0 && owner <= 5 && sprite_idx > 0) {
        /* Check if it's a city or a unit */
        if (contents == '*' || contents == 'X' || contents == 'O') {
            /* It's a city - use city sprite */
            if (bmp_city[sprite_idx]) {
                tex = SDL_CreateTextureFromSurface(renderer, bmp_city[sprite_idx]);
            }
        } else if (piece_type >= 0 && piece_type < NUM_OBJECTS) {
            /* It's a unit - use appropriate sprite based on type */
            switch (piece_type) {
                case ARMY: bmp_array = bmp_army; break;
                case MARINE: bmp_array = bmp_marine; break;
                case FIGHTER: bmp_array = bmp_fighter; break;
                case BOMBER: bmp_array = bmp_bomber; break;
                case PATROL: bmp_array = bmp_patrol; break;
                case DESTROYER: bmp_array = bmp_destroyer; break;
                case SUBMARINE: bmp_array = bmp_submarine; break;
                case TRANSPORT: bmp_array = bmp_transport; break;
                case CARRIER: bmp_array = bmp_carrier; break;
                case BATTLESHIP: bmp_array = bmp_battleship; break;
                case SATELLITE: bmp_array = bmp_satellite; break;
                default: bmp_array = NULL; break;
            }
            if (bmp_array && bmp_array[sprite_idx]) {
                tex = SDL_CreateTextureFromSurface(renderer, bmp_array[sprite_idx]);
            }
        }
        
        if (tex) {
            SDL_RenderCopy(renderer, tex, NULL, &dst);
            SDL_DestroyTexture(tex);
        } else {
            /* Fallback to colored square if no sprite */
            Color c = get_player_color(owner);
            int padding = 3;
            draw_rect(screen_x + padding, screen_y + padding, 
                      TILE_SIZE - 2*padding, TILE_SIZE - 2*padding, 
                      c.r, c.g, c.b);
        }
    }
}

void draw_map(void) {
    if (!sdl_initialized) return;
    
    int max_x = (SCREEN_WIDTH - MAP_OFFSET_X - 10) / TILE_SIZE;
    int max_y = (SCREEN_HEIGHT - MAP_OFFSET_Y - 150) / TILE_SIZE;
    
    for (int y = 0; y < max_y && y < MAP_HEIGHT; y++) {
        for (int x = 0; x < max_x && x < MAP_WIDTH; x++) {
            int loc = y * MAP_WIDTH + x;
            char contents = game.user_map[loc].contents;
            int owner = 0;
            int seen = game.user_map[loc].seen;
            int piece_type = -1;
            
            if (game.real_map[loc].cityp) {
                owner = game.real_map[loc].cityp->owner;
            } else {
                piece_info_t *p = find_obj_at_loc(loc);
                if (p) {
                    owner = p->owner;
                    piece_type = p->type;
                }
            }
            
            draw_tile(MAP_OFFSET_X + x * TILE_SIZE, MAP_OFFSET_Y + y * TILE_SIZE, 
                     contents, owner, seen, piece_type);
        }
    }
}

void draw_messages(void) {
    if (!sdl_initialized) return;
    
    /* Message area at top */
    draw_rect(0, 0, SCREEN_WIDTH, 70, C_DARK_GRAY);
    draw_border(5, 5, SCREEN_WIDTH-10, 60, C_WHITE);
    
    /* Draw message lines (14px tall font, 16px spacing) */
    draw_text(10, 8, msg_line1);
    draw_text(10, 24, msg_line2);
    draw_text(10, 40, msg_line3);
}

void draw_text_area(void) {
    if (!sdl_initialized) return;
    
    int text_start_y = SCREEN_HEIGHT - (MAX_TEXT_LINES * TEXT_LINE_HEIGHT) - 10;
    draw_rect(0, text_start_y - 5, SCREEN_WIDTH, 
              MAX_TEXT_LINES * TEXT_LINE_HEIGHT + 10, 0, 0, 0);
    draw_border(5, text_start_y - 5, SCREEN_WIDTH-10, 
                MAX_TEXT_LINES * TEXT_LINE_HEIGHT + 10, C_GRAY);
    
    /* Draw text from buffer */
    for (int i = 0; i < text_lines_used && i < MAX_TEXT_LINES; i++) {
        draw_text(10, text_start_y + i * TEXT_LINE_HEIGHT, text_buffer[i]);
    }
}

void sdl_render(void) {
    if (!sdl_initialized) return;
    
    sdl_clear();
    draw_map();
    draw_messages();
    draw_text_area();
    sdl_present();
}

/* Terminal replacement functions */

void ttinit(void) {
    sdl_init();
}

void close_disp(void) {
    sdl_close();
}

void topini(void) {
    msg_line1[0] = '\0';
    msg_line2[0] = '\0';
    msg_line3[0] = '\0';
}

void vtopmsg(int line, const char *fmt, va_list ap) {
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    
    switch(line) {
        case 1: 
            strncpy(msg_line1, buf, sizeof(msg_line1)-1);
            msg_line1[sizeof(msg_line1)-1] = '\0';
            break;
        case 2: 
            strncpy(msg_line2, buf, sizeof(msg_line2)-1);
            msg_line2[sizeof(msg_line2)-1] = '\0';
            break;
        case 3: 
            strncpy(msg_line3, buf, sizeof(msg_line3)-1);
            msg_line3[sizeof(msg_line3)-1] = '\0';
            break;
    }
}

void topmsg(int line, char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vtopmsg(line, fmt, ap);
    va_end(ap);
    sdl_render();
}

void prompt(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vtopmsg(1, fmt, ap);
    va_end(ap);
    sdl_render();
}

void error(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vtopmsg(2, fmt, ap);
    va_end(ap);
    sdl_render();
}

void info(char *a, char *b, char *c) {
    strncpy(msg_line1, a, sizeof(msg_line1)-1);
    strncpy(msg_line2, b, sizeof(msg_line2)-1);
    strncpy(msg_line3, c, sizeof(msg_line3)-1);
    sdl_render();
}

void comment(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    
    if (text_lines_used < MAX_TEXT_LINES) {
        size_t len = strlen(buf);
        if (len > 254) len = 254;
        memcpy(text_buffer[text_lines_used], buf, len);
        text_buffer[text_lines_used][len] = '\0';
        text_lines_used++;
    } else {
        for (int i = 0; i < MAX_TEXT_LINES-1; i++) {
            strcpy(text_buffer[i], text_buffer[i+1]);
        }
        size_t len = strlen(buf);
        if (len > 254) len = 254;
        memcpy(text_buffer[MAX_TEXT_LINES-1], buf, len);
        text_buffer[MAX_TEXT_LINES-1][len] = '\0';
    }
    
    va_end(ap);
}

void extra(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    
    if (text_lines_used < MAX_TEXT_LINES) {
        size_t len = strlen(buf);
        if (len > 254) len = 254;
        memcpy(text_buffer[text_lines_used], buf, len);
        text_buffer[text_lines_used][len] = '\0';
        text_lines_used++;
    }
    
    va_end(ap);
}

void pos_str(int row, int col, char *str, ...) {
    va_list ap;
    va_start(ap, str);
    
    char buf[256];
    vsnprintf(buf, sizeof(buf), str, ap);
    
    if (text_lines_used < MAX_TEXT_LINES) {
        size_t len = strlen(buf);
        if (len > 254) len = 254;
        memcpy(text_buffer[text_lines_used], buf, len);
        text_buffer[text_lines_used][len] = '\0';
        text_lines_used++;
    }
    
    va_end(ap);
    sdl_render();
}

void clear_screen(void) {
    text_lines_used = 0;
    memset(text_buffer, 0, sizeof(text_buffer));
    topini();
    sdl_render();
}

void clreol(int line, int colp) {
    /* Not needed in SDL version */
}

void delay(void) {
    if (need_delay) {
        SDL_Delay(500);
        need_delay = false;
    }
}

void set_need_delay(void) {
    need_delay = true;
}

void complain(void) {
    error("I don't understand.");
}

void huh(void) {
    error("Huh?");
}

/* empend is defined in util.c */
extern void empend(void);

char get_chx(void) {
    SDL_Event e;
    
    /* Make sure window has focus */
    if (window) {
        SDL_RaiseWindow(window);
        SDL_SetWindowGrab(window, SDL_TRUE);
    }
    
    fprintf(stderr, "get_chx: WAITING FOR KEY (click game window and press a key)\n"); fflush(stderr);
    
    while (SDL_WaitEvent(&e)) {
        /* Debug: print all events */
        if (e.type != SDL_MOUSEMOTION && e.type != 512) {  /* Skip mouse motion spam */
            fprintf(stderr, "get_chx: got event type %d\n", e.type); fflush(stderr);
        }
        
        if (e.type == SDL_QUIT) {
            empend();
            return 'q';
        } else if (e.type == SDL_KEYDOWN) {
            SDL_Keycode key = e.key.keysym.sym;
            
            fprintf(stderr, "get_chx: KEY PRESSED: %d (%c)\n", key, (key >= 32 && key < 127) ? key : '?'); fflush(stderr);
            
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
                case SDLK_SPACE: return ' ';
                case SDLK_TAB: return '\t';
                case SDLK_BACKSPACE: return 8;
                default: 
                    if (key >= SDLK_a && key <= SDLK_z) return toupper(key);
                    if (key >= SDLK_0 && key <= SDLK_9) return key;
                    return key;
            }
        }
    }
    fprintf(stderr, "get_chx: SDL_WaitEvent returned 0!\n"); fflush(stderr);
    return -1;
}

void get_str(char *buf, int sizep) {
    int pos = 0;
    buf[0] = '\0';
    
    sdl_render();
    
    SDL_Event e;
    while (1) {
        SDL_WaitEvent(&e);
        
        if (e.type == SDL_QUIT) {
            empend();
            return;
        } else if (e.type == SDL_KEYDOWN) {
            SDL_Keycode key = e.key.keysym.sym;
            
            if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                buf[pos] = '\0';
                return;
            } else if ((key == SDLK_BACKSPACE || key == SDLK_DELETE) && pos > 0) {
                pos--;
                buf[pos] = '\0';
            } else if (key >= SDLK_SPACE && key <= SDLK_z && pos < sizep - 1) {
                buf[pos++] = (char)key;
                buf[pos] = '\0';
            }
        }
    }
}

void get_strq(char *buf, int sizep) {
    get_str(buf, sizep);
}

int getint(char *message) {
    char buf[STRSIZE];
    prompt("%s", message);
    get_str(buf, sizeof(buf));
    return atoi(buf);
}

void ksend(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    comment("%s", buf);
    
    va_end(ap);
}

void help(char **text, int nlines) {
    clear_screen();
    for (int i = 0; i < nlines && text[i]; i++) {
        comment("%s", text[i]);
    }
}

void redisplay(void) {
    static int last_render = 0;
    int now = SDL_GetTicks();
    if (now - last_render < 33) return;  /* Limit to 30fps max */
    last_render = now;
    sdl_render();
}

void redraw(void) {
    static int last_render = 0;
    int now = SDL_GetTicks();
    if (now - last_render < 33) return;
    last_render = now;
    sdl_render();
}

void announce(char *msg) {
    comment("%s", msg);
}

bool is_sdl_active(void) {
    return sdl_initialized;
}

/* Stub functions for compatibility */
int direction(chtype c) { return -1; }

void pdebug(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    comment("DEBUG: %s", buf);
    va_end(ap);
}

void kill_display(void) {
    /* SDL version doesn't need to kill display */
}

int loc_disp(int loc) {
    int row = loc / MAP_WIDTH;
    int col = loc % MAP_WIDTH;
    return row * 100 + col;
}

void print_movie_cell(view_map_t *vmap, int row, int col, int sector) {
    /* SDL version doesn't use movie cells */
}

bool getyn(char *message) {
    prompt("%s (y/n)", message);
    char c = get_chx();
    return (c == 'Y' || c == 'y');
}

static int current_sector = 0;

int cur_sector(void) {
    return current_sector;
}

void sector_change(void) {
    /* SDL version: minimal redraw - just pump events */
    SDL_PumpEvents();
}

void print_sector(int whose, view_map_t vmap[], int sector) {
    /* SDL version: just render the full map, ignore sector */
    static int last_render = 0;
    int now = SDL_GetTicks();
    
    /* Only render every 100ms max */
    if (now - last_render < 100) return;
    last_render = now;
    
    sdl_render();
}

void display_loc(int whose, view_map_t vmap[], loc_t loc) {
    /* SDL version: skip individual location renders - full map render is sufficient */
    (void)whose; (void)vmap; (void)loc;
}

void display_locx(int whose, view_map_t vmap[], loc_t loc) {
    /* SDL version: skip individual location renders - full map render is sufficient */
    (void)whose; (void)vmap; (void)loc;
}

void display_score(void) {
    /* SDL version: show score in comment area */
    comment("Score display not implemented in SDL version");
}

/* Additional empire functions */
int get_range(char *message, int low, int high) {
    prompt("%s (%d-%d): ", message, low, high);
    char buf[32];
    get_str(buf, sizeof(buf));
    int val = atoi(buf);
    if (val < low) val = low;
    if (val > high) val = high;
    return val;
}

void print_zoom(view_map_t *vmap) {
    /* SDL version: just render the map */
    sdl_render();
}

void print_xzoom(view_map_t *vmap) {
    /* SDL version: just render the map */
    sdl_render();
}

void print_pzoom(char *s, path_map_t *pmap, view_map_t *vmap) {
    /* SDL version: just render the map (ignore debug string) */
    sdl_render();
}

/* Color structure for get_player_color */
Color get_player_color(int owner) {
    Color c = {128, 128, 128};
    switch(owner) {
        case 1: c = (Color){139, 69, 19}; break;   /* Brown */
        case 2: c = (Color){255, 255, 0}; break;   /* Yellow */
        case 3: c = (Color){255, 0, 0}; break;     /* Red */
        case 4: c = (Color){255, 255, 255}; break; /* White */
        case 5: c = (Color){0, 255, 0}; break;     /* Green */
    }
    return c;
}

/* ncurses stubs for util.c, edit.c, etc. */
int LINES = 25;
int COLS = 80;
WINDOW *stdscr = NULL;

int wmove(WINDOW *win, int y, int x) { (void)win; (void)y; (void)x; return 0; }
int wrefresh(WINDOW *win) { (void)win; sdl_present(); return 0; }
int wgetch(WINDOW *win) { (void)win; return get_chx(); }
int wgetnstr(WINDOW *win, char *str, int n) { (void)win; get_str(str, n); return 0; }
int waddnstr(WINDOW *win, const char *str, int n) { (void)win; (void)n; comment("%s", str); return 0; }
int wclrtoeol(WINDOW *win) { (void)win; return 0; }
int clearok(WINDOW *win, bool bf) { (void)win; (void)bf; return 0; }
int empire_refresh(void) { sdl_present(); return 0; }

/* init_sdl_display - called from empire.c */
void init_sdl_display(void) {
    sdl_init();
}
