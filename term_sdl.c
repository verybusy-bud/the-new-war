/*
 * SDL2 Terminal Replacement for Empire - GUI Version (No text rendering)
 * Replaces ncurses with SDL2 graphics using colored blocks
 */

#include "empire.h"
#include "extern.h"
#include <SDL2/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define SCREEN_WIDTH 1200
#define SCREEN_HEIGHT 900
#define TILE_SIZE 12
#define MAP_OFFSET_X 10
#define MAP_OFFSET_Y 50
#define TEXT_LINE_HEIGHT 16
#define MAX_TEXT_LINES 12

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static bool sdl_initialized = false;

/* BMP surfaces for units and terrain */
static SDL_Surface *bmp_land = NULL;
static SDL_Surface *bmp_sea = NULL;
static SDL_Surface *bmp_city = NULL;
static SDL_Surface *bmp_units[6] = {NULL};
static SDL_Surface *bmp_unknown = NULL;

/* Simple 5x7 bitmap font */
static const unsigned char font_data[128][7] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* space */
    {0x00,0x00,0xEE,0x00,0x00,0x00,0x00}, /* ! */
    {0x00,0x07,0x00,0x07,0x00,0x00,0x00}, /* " */
    {0x14,0x7F,0x14,0x7F,0x14,0x00,0x00}, /* # */
    {0x24,0x2E,0x7F,0x2E,0x12,0x00,0x00}, /* $ */
    {0x46,0x26,0x10,0x4D,0x32,0x00,0x00}, /* % */
    {0x36,0x49,0x7F,0x26,0x00,0x00,0x00}, /* & */
    {0x00,0x07,0x00,0x00,0x00,0x00,0x00}, /* ' */
    {0x00,0x1C,0x22,0x41,0x00,0x00,0x00}, /* ( */
    {0x00,0x41,0x22,0x1C,0x00,0x00,0x00}, /* ) */
    {0x14,0x08,0x3E,0x08,0x14,0x00,0x00}, /* * */
    {0x08,0x08,0x3E,0x08,0x08,0x00,0x00}, /* + */
    {0x00,0x00,0x07,0x00,0x00,0x00,0x00}, /* , */
    {0x08,0x08,0x08,0x08,0x08,0x00,0x00}, /* - */
    {0x00,0x00,0x07,0x00,0x00,0x00,0x00}, /* . */
    {0x02,0x04,0x08,0x10,0x20,0x00,0x00}, /* / */
    {0x3E,0x45,0x49,0x51,0x3E,0x00,0x00}, /* 0 */
    {0x00,0x21,0x7F,0x01,0x00,0x00,0x00}, /* 1 */
    {0x3E,0x41,0x22,0x41,0x7E,0x00,0x00}, /* 2 */
    {0x3E,0x41,0x3E,0x41,0x3E,0x00,0x00}, /* 3 */
    {0x24,0x42,0x7F,0x02,0x02,0x00,0x00}, /* 4 */
    {0x7E,0x40,0x7E,0x01,0x7E,0x00,0x00}, /* 5 */
    {0x3E,0x40,0x7E,0x41,0x3E,0x00,0x00}, /* 6 */
    {0x7F,0x02,0x04,0x08,0x08,0x00,0x00}, /* 7 */
    {0x3E,0x41,0x3E,0x41,0x3E,0x00,0x00}, /* 8 */
    {0x3E,0x41,0x3F,0x01,0x3E,0x00,0x00}, /* 9 */
    {0x00,0x07,0x00,0x07,0x00,0x00,0x00}, /* : */
    {0x00,0x07,0x00,0x07,0x03,0x00,0x00}, /* ; */
    {0x02,0x04,0x08,0x10,0x20,0x00,0x00}, /* < */
    {0x14,0x14,0x14,0x14,0x14,0x00,0x00}, /* = */
    {0x20,0x10,0x08,0x04,0x02,0x00,0x00}, /* > */
    {0x3E,0x41,0x02,0x04,0x08,0x00,0x00}, /* ? */
    {0x3E,0x41,0x5E,0x41,0x3E,0x00,0x00}, /* @ */
    {0x1C,0x22,0x41,0x7F,0x41,0x00,0x00}, /* A */
    {0x7F,0x41,0x7F,0x41,0x3E,0x00,0x00}, /* B */
    {0x3E,0x41,0x40,0x41,0x3E,0x00,0x00}, /* C */
    {0x7F,0x41,0x41,0x41,0x3E,0x00,0x00}, /* D */
    {0x7F,0x40,0x7E,0x40,0x7F,0x00,0x00}, /* E */
    {0x7F,0x40,0x7E,0x40,0x40,0x00,0x00}, /* F */
    {0x3E,0x41,0x41,0x4F,0x3E,0x00,0x00}, /* G */
    {0x7F,0x08,0x08,0x08,0x7F,0x00,0x00}, /* H */
    {0x00,0x41,0x7F,0x41,0x00,0x00,0x00}, /* I */
    {0x01,0x01,0x41,0x7F,0x40,0x00,0x00}, /* J */
    {0x7F,0x08,0x14,0x22,0x41,0x00,0x00}, /* K */
    {0x7F,0x01,0x01,0x01,0x01,0x00,0x00}, /* L */
    {0x7F,0x20,0x10,0x20,0x7F,0x00,0x00}, /* M */
    {0x7F,0x10,0x08,0x04,0x7F,0x00,0x00}, /* N */
    {0x3E,0x41,0x41,0x41,0x3E,0x00,0x00}, /* O */
    {0x7F,0x41,0x7F,0x40,0x40,0x00,0x00}, /* P */
    {0x3E,0x41,0x21,0x5D,0x3E,0x00,0x00}, /* Q */
    {0x7F,0x41,0x7F,0x08,0x77,0x00,0x00}, /* R */
    {0x3E,0x40,0x3E,0x01,0x7E,0x00,0x00}, /* S */
    {0x40,0x40,0x7F,0x40,0x40,0x00,0x00}, /* T */
    {0x41,0x41,0x41,0x41,0x7F,0x00,0x00}, /* U */
    {0x41,0x22,0x14,0x08,0x41,0x00,0x00}, /* V */
    {0x41,0x41,0x3E,0x41,0x41,0x00,0x00}, /* W */
    {0x41,0x22,0x14,0x22,0x41,0x00,0x00}, /* X */
    {0x40,0x20,0x1F,0x20,0x40,0x00,0x00}, /* Y */
    {0x7F,0x02,0x04,0x10,0x7F,0x00,0x00}, /* Z */
    {0x00,0x00,0x7F,0x41,0x41,0x00,0x00}, /* [ */
    {0x20,0x10,0x08,0x04,0x02,0x00,0x00}, /* backslash */
    {0x41,0x41,0x7F,0x00,0x00,0x00,0x00}, /* ] */
    {0x04,0x02,0x01,0x02,0x04,0x00,0x00}, /* ^ */
    {0x01,0x01,0x01,0x01,0x01,0x00,0x00}, /* _ */
    {0x00,0x02,0x04,0x00,0x00,0x00,0x00}, /* ` */
    {0x3E,0x41,0x5D,0x41,0x7E,0x00,0x00}, /* a */
    {0x7F,0x20,0x3E,0x21,0x1E,0x00,0x00}, /* b */
    {0x3E,0x41,0x40,0x41,0x3E,0x00,0x00}, /* c */
    {0x1E,0x21,0x3F,0x21,0x7F,0x00,0x00}, /* d */
    {0x3E,0x40,0x5E,0x40,0x3F,0x00,0x00}, /* e */
    {0x3E,0x40,0x7E,0x40,0x40,0x00,0x00}, /* f */
    {0x3E,0x41,0x3F,0x01,0x3E,0x00,0x00}, /* g */
    {0x7F,0x10,0x3E,0x11,0x0E,0x00,0x00}, /* h */
    {0x00,0x21,0x7D,0x01,0x00,0x00,0x00}, /* i */
    {0x01,0x01,0x7D,0x40,0x20,0x00,0x00}, /* j */
    {0x7F,0x08,0x14,0x22,0x41,0x00,0x00}, /* k */
    {0x00,0x41,0x7F,0x01,0x00,0x00,0x00}, /* l */
    {0x7F,0x04,0x08,0x04,0x7F,0x00,0x00}, /* m */
    {0x7E,0x10,0x08,0x10,0x7E,0x00,0x00}, /* n */
    {0x3E,0x41,0x41,0x41,0x3E,0x00,0x00}, /* o */
    {0x7F,0x20,0x3F,0x20,0x1F,0x00,0x00}, /* p */
    {0x1F,0x21,0x3F,0x20,0x7F,0x00,0x00}, /* q */
    {0x7E,0x10,0x08,0x10,0x10,0x00,0x00}, /* r */
    {0x3E,0x40,0x3C,0x04,0x78,0x00,0x00}, /* s */
    {0x20,0x40,0x7F,0x40,0x20,0x00,0x00}, /* t */
    {0x1E,0x21,0x21,0x21,0x3F,0x00,0x00}, /* u */
    {0x11,0x22,0x3C,0x22,0x11,0x00,0x00}, /* v */
    {0x11,0x11,0x3E,0x11,0x11,0x00,0x00}, /* w */
    {0x41,0x22,0x14,0x22,0x41,0x00,0x00}, /* x */
    {0x3E,0x41,0x3F,0x01,0x06,0x00,0x00}, /* y */
    {0x7C,0x04,0x18,0x04,0x7C,0x00,0x00}, /* z */
    {0x0C,0x12,0x21,0x42,0x80,0x00,0x00}, /* { */
    {0x00,0x00,0x7F,0x00,0x00,0x00,0x00}, /* | */
    {0x80,0x42,0x21,0x12,0x0C,0x00,0x00}, /* } */
    {0x08,0x10,0x08,0x04,0x08,0x00,0x00}, /* ~ */
};

static SDL_Texture *font_texture = NULL;

void init_font_texture(void) {
    SDL_Surface *font_surf = SDL_CreateRGBSurface(0, 640, 112, 32, 0, 0, 0, 0);
    if (!font_surf) return;
    
    SDL_FillRect(font_surf, NULL, SDL_MapRGB(font_surf->format, 0, 0, 0));
    
    for (int c = 32; c < 127; c++) {
        int col = (c - 32) % 16;
        int row = (c - 32) / 16;
        int x = col * 8;
        int y = row * 8;
        
        for (int py = 0; py < 7; py++) {
            for (int px = 0; px < 5; px++) {
                if (font_data[c][py] & (0x80 >> px)) {
                    SDL_Rect pixel = {x + px, y + py, 1, 1};
                    SDL_FillRect(font_surf, &pixel, SDL_MapRGB(font_surf->format, 255, 255, 255));
                }
            }
        }
    }
    
    font_texture = SDL_CreateTextureFromSurface(renderer, font_surf);
    SDL_FreeSurface(font_surf);
}

void draw_char(int x, int y, char c) {
    if (!font_texture || c < 32 || c > 126) return;
    
    int col = (c - 32) % 16;
    int row = (c - 32) / 16;
    
    SDL_Rect src = {col * 8, row * 8, 5, 7};
    SDL_Rect dst = {x, y, 5, 7};
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderCopy(renderer, font_texture, &src, &dst);
}

void draw_text(int x, int y, const char *str) {
    for (int i = 0; str[i]; i++) {
        draw_char(x + i * 6, y, str[i]);
    }
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
    bmp_city = SDL_LoadBMP("city.bmp");
    bmp_unknown = SDL_LoadBMP("unknown.bmp");
    
    /* Load unit BMPs */
    bmp_units[1] = SDL_LoadBMP("a1.bmp");
    bmp_units[2] = SDL_LoadBMP("a2.bmp");
    bmp_units[3] = SDL_LoadBMP("a3.bmp");
    bmp_units[4] = SDL_LoadBMP("a4.bmp");
    bmp_units[5] = SDL_LoadBMP("a5.bmp");
    
    /* Initialize font texture */
    init_font_texture();
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

void draw_tile(int screen_x, int screen_y, char contents, int owner, int seen) {
    if (!sdl_initialized) return;
    
    SDL_Rect dst = {screen_x, screen_y, TILE_SIZE, TILE_SIZE};
    SDL_Texture *tex = NULL;
    
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
    if (owner > 0 && owner <= 5) {
        if (owner <= 4 && bmp_city) {
            SDL_Texture *city_tex = SDL_CreateTextureFromSurface(renderer, bmp_city);
            if (city_tex) {
                SDL_RenderCopy(renderer, city_tex, NULL, &dst);
                SDL_DestroyTexture(city_tex);
            }
        } else if (bmp_units[owner]) {
            SDL_Texture *unit_tex = SDL_CreateTextureFromSurface(renderer, bmp_units[owner]);
            if (unit_tex) {
                SDL_RenderCopy(renderer, unit_tex, NULL, &dst);
                SDL_DestroyTexture(unit_tex);
            }
        } else {
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
            
            if (game.real_map[loc].cityp) {
                owner = game.real_map[loc].cityp->owner;
            } else {
                piece_info_t *p = find_obj_at_loc(loc);
                if (p) owner = p->owner;
            }
            
            draw_tile(MAP_OFFSET_X + x * TILE_SIZE, MAP_OFFSET_Y + y * TILE_SIZE, 
                     contents, owner, seen);
        }
    }
}

void draw_messages(void) {
    if (!sdl_initialized) return;
    
    /* Message area at top */
    draw_rect(0, 0, SCREEN_WIDTH, 70, C_DARK_GRAY);
    draw_border(5, 5, SCREEN_WIDTH-10, 60, C_WHITE);
    
    /* Draw message lines */
    draw_text(10, 10, msg_line1);
    draw_text(10, 25, msg_line2);
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
    
    while (SDL_WaitEvent(&e)) {
        if (e.type == SDL_QUIT) {
            empend();
            return 'q';
        } else if (e.type == SDL_KEYDOWN) {
            SDL_Keycode key = e.key.keysym.sym;
            
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
    sdl_render();
}

void redraw(void) {
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
    /* SDL version: redraw current sector */
    sdl_render();
}

void print_sector(int whose, view_map_t vmap[], int sector) {
    /* SDL version: redraw the sector */
    sdl_clear();
    
    int sector_row = sector / SECTOR_COLS;
    int sector_col = sector % SECTOR_COLS;
    
    int start_row = sector_row * ROWS_PER_SECTOR;
    int start_col = sector_col * COLS_PER_SECTOR;
    
    for (int y = 0; y < ROWS_PER_SECTOR && (start_row + y) < MAP_HEIGHT; y++) {
        for (int x = 0; x < COLS_PER_SECTOR && (start_col + x) < MAP_WIDTH; x++) {
            int loc = (start_row + y) * MAP_WIDTH + (start_col + x);
            char contents = vmap[loc].contents;
            int owner = 0;
            int seen = vmap[loc].seen;
            
            if (game.real_map[loc].cityp) {
                owner = game.real_map[loc].cityp->owner;
            } else {
                piece_info_t *p = find_obj_at_loc(loc);
                if (p) owner = p->owner;
            }
            
            draw_tile(MAP_OFFSET_X + x * TILE_SIZE, MAP_OFFSET_Y + y * TILE_SIZE, 
                     contents, owner, seen);
        }
    }
    
    draw_messages();
    draw_text_area();
    sdl_present();
}

void display_loc(int whose, view_map_t vmap[], loc_t loc) {
    /* SDL version: just redraw the map */
    sdl_render();
}

void display_locx(int whose, view_map_t vmap[], loc_t loc) {
    /* SDL version: just redraw the map */
    sdl_render();
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
