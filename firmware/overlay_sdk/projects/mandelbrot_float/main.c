//==============================================================================
// Olimex iCE40HX8K-EVB RISC-V Platform
// Mandelbrot Set Explorer - Floating-Point Overlay Version
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//
// NOT FOR COMMERCIAL USE
// Educational and research purposes only
//==============================================================================

/*
 * Mandelbrot Set Explorer - Floating-Point Version
 *
 * Overlay version that:
 * - Automatically starts drawing (no keypress needed)
 * - Uses double-precision floating-point arithmetic
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
// Mandelbrot State - FLOATING POINT
//==============================================================================
typedef struct {
    double min_real, max_real;  // Floating-point coordinates
    double min_imag, max_imag;
    int max_iter;
    int screen_rows, screen_cols;
} mandelbrot_state;

static mandelbrot_state state;

// Render buffer
static char render_buffer[200][150];

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
// Mandelbrot calculation - FLOATING POINT
//==============================================================================
static int mandelbrot_iterations(double cr, double ci, int max_iter) {
    double zr = 0.0;
    double zi = 0.0;
    double zr2 = 0.0;
    double zi2 = 0.0;
    int iter = 0;

    while (iter < max_iter && (zr2 + zi2) < 4.0) {
        zi = 2.0 * zr * zi + ci;
        zr = zr2 - zi2 + cr;

        zr2 = zr * zr;
        zi2 = zi * zi;

        iter++;
    }

    return iter;
}

//==============================================================================
// Draw the Mandelbrot Set - FLOATING POINT
//==============================================================================
static void draw_mandelbrot(WINDOW *win) {
    double real_step = (state.max_real - state.min_real) / SCREEN_WIDTH;
    double imag_step = (state.max_imag - state.min_imag) / SCREEN_HEIGHT;

    for (int row = 0; row < SCREEN_HEIGHT; row++) {
        for (int col = 0; col < SCREEN_WIDTH; col++) {
            double real = state.min_real + col * real_step;
            double imag = state.min_imag + row * imag_step;

            int iter = mandelbrot_iterations(real, imag, state.max_iter);
            const char* ch = iter_to_char(iter, state.max_iter);

            // Store in render buffer
            if (row < 200 && col < 150) {
                render_buffer[row][col] = ch[0];
            }
        }
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
// Reset to default view - FLOATING POINT
//==============================================================================
static void reset_view(void) {
    // Standard Mandelbrot view: real=[-2.5, 1.0], imag=[-1.0, 1.0]
    state.min_real = -2.5;
    state.max_real = 1.0;
    state.min_imag = -1.0;
    state.max_imag = 1.0;
}

//==============================================================================
// Display info bar
//==============================================================================
static void draw_info_bar(void) {
    move(SCREEN_HEIGHT, 0);
    clrtoeol();
    printw("Mandelbrot Set (Floating-Point) | Display: %dx%d | Iter: %d | Press any key to exit",
           g_term_cols, g_term_rows, state.max_iter);
    refresh();
}

//==============================================================================
// Main Program
//==============================================================================
int main(void) {
    uart_puts("\r\n");
    uart_puts("===========================================\r\n");
    uart_puts("  Mandelbrot Set (Floating-Point)\r\n");
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
