#include "framework.h"
#include "dali_input_frame.h"

using dali_input::is_supported_forward_frame_length;

TEST(input_frame_accepts_standard_forward_lengths) {
    CHECK(is_supported_forward_frame_length(16));
    CHECK(is_supported_forward_frame_length(24));
}

TEST(input_frame_rejects_truncated_and_overlong_lengths) {
    CHECK(!is_supported_forward_frame_length(0));
    CHECK(!is_supported_forward_frame_length(15));
    CHECK(!is_supported_forward_frame_length(17));
    CHECK(!is_supported_forward_frame_length(23));
    CHECK(!is_supported_forward_frame_length(25));
    CHECK(!is_supported_forward_frame_length(26));
}
