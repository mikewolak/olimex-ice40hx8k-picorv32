//==============================================================================
// Minimal test to debug lockup issue
//==============================================================================

#include <stdio.h>

int main(void) {
    printf("Hello from memory test!\r\n");
    printf("If you see this, printf works.\r\n");

    // Loop forever
    while (1) {
        __asm__ volatile ("wfi");
    }

    return 0;
}
