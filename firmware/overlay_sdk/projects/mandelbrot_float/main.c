//==============================================================================
// Mandelbrot Set - FLOATING-POINT VERSION
//==============================================================================
// Uses floating-point for coordinate calculations (software emulated on PicoRV32)
// Controls:
//   R: Reset to default view
//   +/-: Adjust max iterations
//   Q: Quit
//==============================================================================

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <curses.h>
#include "hardware.h"
#include "io.h"
#include "timer_ms.h"

//==============================================================================
// VT100 Terminal Size Detection
//==============================================================================
static int g_term_rows = 24;  // Default fallback
static int g_term_cols = 80;

// Query terminal size using VT100 escape sequences
static bool query_terminal_size(void) {
    // Move cursor to far bottom-right (row 999, col 999)
    printf("\033[999;999H");

    // Query cursor position - terminal will respond with: ESC [ row ; col R
    printf("\033[6n");
    fflush(stdout);

    // Read response with timeout
    char buf[32];
    int i = 0;
    uint32_t start_time = get_millis();

    while (i < (int)sizeof(buf) - 1) {
        // Timeout after 500ms
        if (get_millis() - start_time > 500) {
            printf("\033[H");  // Move cursor to home
            return false;
        }

        if (uart_getc_available()) {
            buf[i] = uart_getc();
            if (buf[i] == 'R') {
                buf[i] = '\0';
                break;
            }
            i++;
        }
    }

    // Parse response: ESC [ rows ; cols R
    if (i > 0 && buf[0] == '\033' && buf[1] == '[') {
        int rows = 0, cols = 0;
        // Simple parser (avoid sscanf for embedded)
        char *p = buf + 2;

        // Parse rows
        while (*p >= '0' && *p <= '9') {
            rows = rows * 10 + (*p - '0');
            p++;
        }

        if (*p == ';') {
            p++;
            // Parse cols
            while (*p >= '0' && *p <= '9') {
                cols = cols * 10 + (*p - '0');
                p++;
            }
        }

        if (rows > 0 && cols > 0 && rows <= 200 && cols <= 300) {
            g_term_rows = rows;
            g_term_cols = cols;
            printf("\033[H");  // Move cursor to home
            return true;
        }
    }

    printf("\033[H");  // Move cursor to home
    return false;
}

//==============================================================================
// Mandelbrot Configuration
//==============================================================================
#define MAX_ITER_DEFAULT 256
#define MAX_ITER_MAX 1024

// Screen dimensions (use detected terminal size, minus room for info bars)
#define SCREEN_WIDTH  (g_term_cols)
#define SCREEN_HEIGHT (g_term_rows - 2)  // Reserve 2 lines for info/controls

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
// Mandelbrot State
//==============================================================================
typedef struct {
    double min_real, max_real;
    double min_imag, max_imag;
    int max_iter;
    uint32_t last_calc_time_ms;
    uint32_t last_total_iters;  // Total iterations in last render
    int screen_rows, screen_cols;  // Track current screen size
} mandelbrot_state;

static mandelbrot_state state;

// Render buffer - stores the rendered ASCII characters
// Max terminal size we support: 200x150
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

// Calculate Mandelbrot iterations for a point
static int mandelbrot_iterations(double cx, double cy, int max_iter) {
    int32_t cr = double_to_fixed(cx);
    int32_t ci = double_to_fixed(cy);
    int32_t zr = 0;
    int32_t zi = 0;
    int32_t zr2 = 0;
    int32_t zi2 = 0;

    int iter = 0;
    while (iter < max_iter && (zr2 + zi2) < (4 << FIXED_SHIFT)) {
        zi = fixed_mul(zr, zi);
        zi += zi;  // 2 * zr * zi
        zi += ci;

        zr = zr2 - zi2 + cr;

        zr2 = fixed_mul(zr, zr);
        zi2 = fixed_mul(zi, zi);

        iter++;
    }

    return iter;
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
// Draw the Mandelbrot Set
// Timing excludes UART display time
//==============================================================================
static void draw_mandelbrot(WINDOW *win) {
    uint32_t total_iters = 0;

    double real_step = (state.max_real - state.min_real) / SCREEN_WIDTH;
    double imag_step = (state.max_imag - state.min_imag) / SCREEN_HEIGHT;

    // TIMING START - Only measure calculation, not UART display!
    uint32_t start_time = get_millis();

    for (int row = 0; row < SCREEN_HEIGHT; row++) {
        for (int col = 0; col < SCREEN_WIDTH; col++) {
            double real = state.min_real + col * real_step;
            double imag = state.min_imag + row * imag_step;

            int iter = mandelbrot_iterations(real, imag, state.max_iter);
            total_iters += iter;
            const char* ch = iter_to_char(iter, state.max_iter);

            // Store in render buffer (not timed)
            if (row < 200 && col < 150) {
                render_buffer[row][col] = ch[0];
            }
        }
    }

    // TIMING END - Stop before UART display
    state.last_calc_time_ms = get_millis() - start_time;
    state.last_total_iters = total_iters;

    // Now display to screen (not timed)
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
// Check for terminal resize
//==============================================================================
static bool check_terminal_resize(void) {
    int old_rows = g_term_rows;
    int old_cols = g_term_cols;

    if (query_terminal_size()) {
        if (g_term_rows != old_rows || g_term_cols != old_cols) {
            return true;  // Size changed
        }
    }
    return false;
}

//==============================================================================
// Reset to default view
//==============================================================================
static void reset_view(void) {
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

    // Calculate performance metric (Million iterations per second)
    double mips = 0.0;
    if (state.last_calc_time_ms > 0) {
        mips = (double)state.last_total_iters / (double)state.last_calc_time_ms / 1000.0;
    }

    printw("FLOATING-POINT | Display: %dx%d | Iter: %d | Time: %lums | %.2fM iter/s",
           g_term_cols, g_term_rows, state.max_iter,
           (unsigned long)state.last_calc_time_ms, mips);

    move(SCREEN_HEIGHT + 1, 0);
    clrtoeol();
    printw("R:Reset +/-:Iter Q:Quit | Performance benchmark");

    refresh();
}

//==============================================================================
// Main Program
//==============================================================================
int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    // Overlay version - auto-start (no keypress wait)
    printf("Mandelbrot Set Explorer (Floating-Point)\r\n");
    printf("Initializing...\r\n");

    // Register our timer interrupt handler with the firmware
    // The firmware's IRQ handler will call this function pointer
    // Address 0x1f0e8 is the fixed location of overlay_timer_irq_handler in firmware
    volatile void (**overlay_timer_irq_handler_ptr)(void) = (void (**)(void))0x1f0e8;
    *overlay_timer_irq_handler_ptr = timer_ms_irq_handler;

    // Initialize timer (needed for query_terminal_size timeout)
    timer_ms_init();

    // Detect terminal size before initializing curses
    printf("Detecting terminal size...\r\n");
    if (query_terminal_size()) {
        printf("Terminal: %d rows x %d cols\r\n", g_term_rows, g_term_cols);
        printf("Render area: %d rows x %d cols\r\n", SCREEN_HEIGHT, SCREEN_WIDTH);
    } else {
        printf("Failed to detect terminal size, using defaults: %d x %d\r\n",
               g_term_rows, g_term_cols);
    }

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
    state.last_calc_time_ms = 0;
    state.last_total_iters = 0;
    state.screen_rows = g_term_rows;
    state.screen_cols = g_term_cols;

    // Create main window
    WINDOW *mandel_win = newwin(SCREEN_HEIGHT, SCREEN_WIDTH, 0, 0);

    printf("Drawing initial view (FLOATING-POINT)...\r\n");

    // Draw initial mandelbrot
    draw_mandelbrot(mandel_win);
    draw_info_bar();

    bool running = true;
    bool needs_redraw = false;
    int loop_counter = 0;

    // Main loop
    while (running) {
        // Check for terminal resize every 100 iterations
        loop_counter++;
        if (loop_counter >= 100) {
            loop_counter = 0;
            if (check_terminal_resize()) {
                // Terminal size changed - need to recreate window and redraw
                if (state.screen_rows != g_term_rows || state.screen_cols != g_term_cols) {
                    state.screen_rows = g_term_rows;
                    state.screen_cols = g_term_cols;

                    // Recreate window with new size
                    delwin(mandel_win);
                    wclear(stdscr);
                    mandel_win = newwin(SCREEN_HEIGHT, SCREEN_WIDTH, 0, 0);

                    needs_redraw = true;
                }
            }
        }

        int ch = getch();

        if (ch != ERR) {
            switch (ch) {
                // Quit
                case 'q':
                case 'Q':
                    running = false;
                    break;

                // Reset view
                case 'r':
                case 'R':
                    reset_view();
                    needs_redraw = true;
                    break;

                // Adjust max iterations
                case '+':
                case '=':
                    if (state.max_iter < MAX_ITER_MAX) {
                        state.max_iter = (state.max_iter < 256) ?
                                        state.max_iter + 32 :
                                        state.max_iter + 128;
                        if (state.max_iter > MAX_ITER_MAX)
                            state.max_iter = MAX_ITER_MAX;
                        needs_redraw = true;
                    }
                    break;

                case '-':
                case '_':
                    if (state.max_iter > 32) {
                        state.max_iter = (state.max_iter <= 256) ?
                                        state.max_iter - 32 :
                                        state.max_iter - 128;
                        if (state.max_iter < 32)
                            state.max_iter = 32;
                        needs_redraw = true;
                    }
                    break;
            }

            // Redraw if needed
            if (needs_redraw) {
                wclear(mandel_win);
                draw_mandelbrot(mandel_win);
                draw_info_bar();
                needs_redraw = false;
            }
        }

        // Small delay to reduce CPU usage
        for (volatile int i = 0; i < 1000; i++);
    }

    // Cleanup
    wclear(stdscr);
    endwin();

    printf("\r\n\r\nMandelbrot Explorer (FLOATING-POINT) exited.\r\n");
    printf("Max iterations: %d\r\n", state.max_iter);
    printf("Last calculation time: %lu ms\r\n", (unsigned long)state.last_calc_time_ms);
    printf("Performance: %.2f M iter/s\r\n",
           (double)state.last_total_iters / (double)state.last_calc_time_ms / 1000.0);
    printf("\r\nReturning to main menu...\r\n");

    // Unregister our timer interrupt handler
    *overlay_timer_irq_handler_ptr = 0;

    // Overlay version - return to main menu cleanly
    return 0;
}
