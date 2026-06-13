// dali_tx_collision: multi-master collision-detection helpers (IEC 62386-101).
//
// TX convention (writeBit, see dali_tx_collision.h):
//   half 0: drive `bit` (assert if bit==1, release if bit==0)
//   half 1: drive `!bit` (assert if bit==0, release if bit==1)
// The bus is wired-AND: a transmitter only "sees" other transmitters during a
// half-bit where it itself releases the line.

#include "dali_tx_collision.h"
#include "framework.h"

namespace {

using namespace dali_tx;

// --- master_releasing ---------------------------------------------------------

TEST(master_releasing_bit1_first_half_asserts) {
    // bit=1, half=0 -> we assert (drive low) -> not releasing
    CHECK(!master_releasing(true, 0));
}

TEST(master_releasing_bit1_second_half_releases) {
    // bit=1, half=1 -> we release (drive high) -> releasing
    CHECK(master_releasing(true, 1));
}

TEST(master_releasing_bit0_first_half_releases) {
    // bit=0, half=0 -> we release -> releasing
    CHECK(master_releasing(false, 0));
}

TEST(master_releasing_bit0_second_half_asserts) {
    // bit=0, half=1 -> we assert -> not releasing
    CHECK(!master_releasing(false, 1));
}

// --- is_collision ---------------------------------------------------------------

TEST(no_collision_when_we_are_asserting_regardless_of_rx) {
    // While we assert, our own low dominates the wired-AND bus; the RX read is
    // irrelevant.
    CHECK(!is_collision(true, 0, true));
    CHECK(!is_collision(true, 0, false));
    CHECK(!is_collision(false, 1, true));
    CHECK(!is_collision(false, 1, false));
}

TEST(no_collision_when_releasing_and_bus_reads_idle) {
    CHECK(!is_collision(true, 1, /*rx_asserted=*/false));
    CHECK(!is_collision(false, 0, /*rx_asserted=*/false));
}

TEST(collision_when_releasing_but_bus_reads_asserted) {
    // We released the line, but it reads low -> someone else is driving it.
    CHECK(is_collision(true, 1, /*rx_asserted=*/true));
    CHECK(is_collision(false, 0, /*rx_asserted=*/true));
}

// --- Exhaustive truth table -----------------------------------------------------

TEST(exhaustive_truth_table) {
    for (int bit_i = 0; bit_i <= 1; bit_i++) {
        for (int half = 0; half <= 1; half++) {
            for (int rx_i = 0; rx_i <= 1; rx_i++) {
                bool bit = bit_i;
                bool rx_asserted = rx_i;

                bool releasing = master_releasing(bit, half);
                bool collision = is_collision(bit, half, rx_asserted);

                // Collision iff releasing AND rx reads asserted.
                CHECK_EQ(collision, releasing && rx_asserted);

                // releasing is true iff bit == half (XNOR) for this encoding:
                // half 0 releases when bit==0, half 1 releases when bit==1.
                CHECK_EQ(releasing, bit_i == half);
            }
        }
    }
}

}  // namespace
