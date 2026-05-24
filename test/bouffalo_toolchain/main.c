/* Smoke test for the bouffalo_sdk umbrella component.
 *
 * Nothing actually runs — this is a link-only test confirming the umbrella
 * resolves all of its own symbols. */

#include <stdint.h>

volatile uint32_t counter;

int main(void) {
    while (1) {
        counter++;
    }
}
