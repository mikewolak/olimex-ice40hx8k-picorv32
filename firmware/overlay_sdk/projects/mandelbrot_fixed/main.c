//==============================================================================
// Olimex iCE40HX8K-EVB RISC-V Platform
// Mandelbrot Set Explorer - Fixed-Point Overlay Version
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//
// NOT FOR COMMERCIAL USE
// Educational and research purposes only
//==============================================================================

/*
 * Mandelbrot Set Explorer - Fixed-Point Version
 *
 * Overlay version that:
 * - Automatically starts drawing (no keypress needed)
 * - Uses 100% fixed-point arithmetic (no floating point)
 * - Draws fractal and waits for keypress
 * - Returns cleanly to main menu on exit
 */

#include "hardware.h"
#include "io.h"
#include <stdint.h>
#include <stdio.h>
#include <curses.h>

//==============================================================================
// VT100 Terminal Size Detection
//==============================================================================
static int g_term_rows = 24;  // Default fallback
static int g_term_cols = 80;

//==============================================================================
// Mandelbrot Configuration
//==============================================================================
#define MAX_ITER_DEFAULT 128
#define MAX_ITER_MAX 1024

// Screen dimensions (use detected terminal size, minus room for info bar)
#define SCREEN_WIDTH  (g_term_cols)
#define SCREEN_HEIGHT (g_term_rows - 1)  // Reserve 1 line for info

// Palette using various shading characters for iteration depth
static const char* PALETTE[] = {
    " ",   // 0: inside set
    ".",   // 1-2 iterations
    ":",   // 3-4
    "-",   // 5-8
    "=",   // 9-16
    "+",   // 17-32
    "*",   // 33-64
    "#",   // 65-128
    "%",   // 129-256
    "@",   // 257-512
    "\xE2\x96\x93"  // 513+: dark shade █
};

//==============================================================================
// Mandelbrot State - ALL FIXED-POINT
//==============================================================================
typedef struct {
    int32_t min_real, max_real;  // Fixed-point coordinates
    int32_t min_imag, max_imag;
    int max_iter;
    int screen_rows, screen_cols;
} mandelbrot_state;

static mandelbrot_state state;

// Render buffer
static char render_buffer[200][150];

//==============================================================================
// Fixed-point Mandelbrot (faster than floating point)
//==============================================================================
#define FIXED_SHIFT 16
#define FIXED_ONE (1 << FIXED_SHIFT)

static inline int32_t double_to_fixed(double d) {
    return (int32_t)(d * FIXED_ONE);
}

static inline int32_t fixed_mul(int32_t a, int32_t b) {
    return (int32_t)(((int64_t)a * (int64_t)b) >> FIXED_SHIFT);
}

//==============================================================================
// Map iteration count to character
//==============================================================================
static const char* iter_to_char(int iter, int max_iter) {
    if (iter >= max_iter) {
        return PALETTE[0];  // Inside set
    }

    // Map to palette index logarithmically
    int idx = 1;
    int threshold = 2;

    while (idx < 10 && iter > threshold) {
        threshold *= 2;
        idx++;
    }

    return PALETTE[idx];
}

//==============================================================================
// Draw the Mandelbrot Set - PURE FIXED-POINT (NO FLOAT!)
//==============================================================================
static void draw_mandelbrot(WINDOW *win) {
    // Calculate step size in fixed-point
    int32_t real_step = (state.max_real - state.min_real) / SCREEN_WIDTH;
    int32_t imag_step = (state.max_imag - state.min_imag) / SCREEN_HEIGHT;

    int32_t imag = state.min_imag;

    for (int row = 0; row < SCREEN_HEIGHT; row++) {
        int32_t real = state.min_real;

        for (int col = 0; col < SCREEN_WIDTH; col++) {
            int32_t zr = 0;
            int32_t zi = 0;
            int32_t zr2 = 0;
            int32_t zi2 = 0;

            int iter = 0;
            int32_t escape_radius_sq = 4 << FIXED_SHIFT;

            while (iter < state.max_iter && (zr2 + zi2) < escape_radius_sq) {
                zi = fixed_mul(zr, zi);
                zi += zi;  // 2 * zr * zi
                zi += imag;

                zr = zr2 - zi2 + real;

                zr2 = fixed_mul(zr, zr);
                zi2 = fixed_mul(zi, zi);

                iter++;
            }

            const char* ch = iter_to_char(iter, state.max_iter);

            // Store in render buffer
            if (row < 200 && col < 150) {
                render_buffer[row][col] = ch[0];
            }

            real += real_step;
        }

        imag += imag_step;
    }

    // Display to screen
    for (int row = 0; row < SCREEN_HEIGHT; row++) {
        wmove(win, row, 0);
        for (int col = 0; col < SCREEN_WIDTH; col++) {
            if (row < 200 && col < 150) {
                waddch(win, render_buffer[row][col]);
            }
        }
    }

    wrefresh(win);
}

//==============================================================================
// Reset to default view - FIXED-POINT CONSTANTS
//==============================================================================
static void reset_view(void) {
    // Standard Mandelbrot view: real=[-2.5, 1.0], imag=[-1.0, 1.0]
    state.min_real = double_to_fixed(-2.5);
    state.max_real = double_to_fixed(1.0);
    state.min_imag = double_to_fixed(-1.0);
    state.max_imag = double_to_fixed(1.0);
}

//==============================================================================
// Display info bar
//==============================================================================
static void draw_info_bar(void) {
    move(SCREEN_HEIGHT, 0);
    clrtoeol();
    printw("Mandelbrot Set (Fixed-Point) | Display: %dx%d | Iter: %d | Press any key to exit",
           g_term_cols, g_term_rows, state.max_iter);
    refresh();
}

//==============================================================================
// Main Program
//==============================================================================
int main(void) {
    uart_puts("\r\n");
    uart_puts("===========================================\r\n");
    uart_puts("  Mandelbrot Set (Fixed-Point)\r\n");
    uart_puts("===========================================\r\n");
    uart_puts("Drawing fractal...\r\n");
    uart_puts("\r\n");

    // Initialize ncurses
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    timeout(0);
    curs_set(0);

    // Initialize state
    reset_view();
    state.max_iter = MAX_ITER_DEFAULT;
    state.screen_rows = g_term_rows;
    state.screen_cols = g_term_cols;

    // Create main window
    WINDOW *mandel_win = newwin(SCREEN_HEIGHT, SCREEN_WIDTH, 0, 0);

    // Draw mandelbrot set
    draw_mandelbrot(mandel_win);
    draw_info_bar();

    // Wait for keypress
    int ch;
    do {
        ch = getch();
        // Small delay
        for (volatile int i = 0; i < 10000; i++);
    } while (ch == ERR);

    // Cleanup
    wclear(stdscr);
    endwin();

    uart_puts("\033[2J\033[H");  // Clear screen, home cursor
    uart_puts("\r\n");
    uart_puts("Mandelbrot Set exited. Returning to main menu...\r\n");
    uart_puts("\r\n");

    return 0;
}
