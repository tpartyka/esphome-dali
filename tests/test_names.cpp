#include "dali_names.h"
#include "framework.h"

using namespace dali_names;

TEST(copy_name_empty_string_clears) {
    char dst[NAME_LEN];
    std::memset(dst, 'x', sizeof(dst));
    copy_name(dst, "");
    CHECK_EQ(dst[0], '\0');
    CHECK(!has_name(dst));
}

TEST(copy_name_exact_fit_15_chars) {
    char dst[NAME_LEN];
    std::string name(NAME_LEN - 1, 'a');  // 15 chars, fits exactly with NUL
    copy_name(dst, name);
    CHECK_EQ(std::strlen(dst), (size_t) (NAME_LEN - 1));
    CHECK_EQ(dst[NAME_LEN - 1], '\0');
    CHECK(has_name(dst));
}

TEST(copy_name_truncates_16_chars) {
    char dst[NAME_LEN];
    std::string name(NAME_LEN, 'b');  // 16 chars, one too many
    copy_name(dst, name);
    CHECK_EQ(std::strlen(dst), (size_t) (NAME_LEN - 1));
    CHECK_EQ(dst[NAME_LEN - 1], '\0');
}

TEST(copy_name_truncates_long_input) {
    char dst[NAME_LEN];
    std::string name(20, 'c');
    copy_name(dst, name);
    CHECK_EQ(std::strlen(dst), (size_t) (NAME_LEN - 1));
    CHECK_EQ(dst[NAME_LEN - 1], '\0');
}

TEST(has_name_detects_non_empty) {
    char dst[NAME_LEN] = {0};
    CHECK(!has_name(dst));
    copy_name(dst, "Kitchen");
    CHECK(has_name(dst));
}

TEST(sanitize_blob_forces_nul_termination) {
    DaliNamesBlob blob{};
    // Deliberately corrupt one entry: fill with non-NUL bytes, no terminator.
    std::memset(blob.lamp_names[5], 'z', NAME_LEN);
    std::memset(blob.group_names[2], 'z', NAME_LEN);
    std::memset(blob.scene_names[9], 'z', NAME_LEN);

    sanitize_blob(blob);

    CHECK_EQ(blob.lamp_names[5][NAME_LEN - 1], '\0');
    CHECK_EQ(blob.group_names[2][NAME_LEN - 1], '\0');
    CHECK_EQ(blob.scene_names[9][NAME_LEN - 1], '\0');
}

TEST(blob_size_matches_flat_layout) {
    CHECK_EQ(sizeof(DaliNamesBlob), (LAMP_COUNT + GROUP_COUNT + SCENE_COUNT) * NAME_LEN);
}
