#include <stdlib.h>
#include <string.h>
#include "test_common.h"

int main(void) {
    const size_t lengths[] = {65535, 65536, 66576, 66577, 150000};
    const char *pending[] = {"", "\033", "\033[0"};
    for (size_t p = 0; p < sizeof pending / sizeof pending[0]; p++) {
        for (size_t n = 0; n < sizeof lengths / sizeof lengths[0]; n++) {
            size_t size = lengths[n];
            uint8_t *input = calloc(size, 1);
            test_assert_true(input != NULL, "allocate large input");
            input[0] = p == 1 ? 'c' : p == 2 ? 'm' : 0;
            memcpy(input + size - 4, "TAIL", 4);
            test_reset_terminal(4, 16);
            test_feed_string(pending[p]);
            test_feed_bytes(input, size);
            test_assert_true(tgetline(0)[0].u == 'T', "large input tail survives");
            test_assert_true(tgetline(0)[3].u == 'L', "complete tail survives");
            test_assert_cursor(0, 4);
            free(input);
        }
    }
    test_print_ok("parser/large_write");
    return 0;
}
