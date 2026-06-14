#pragma once

#include <cstddef>
#include <cstring>
#include <string>

namespace dali_names {

/// @brief Fixed storage length for a dashboard-only custom name, including
/// the terminating NUL (15 printable chars max).
constexpr size_t NAME_LEN = 16;

constexpr size_t LAMP_COUNT = 64;
constexpr size_t GROUP_COUNT = 16;
constexpr size_t SCENE_COUNT = 16;

/// @brief Flash-persisted blob of dashboard-only display names for lamps
/// (short address 0..63), groups (0..15) and scenes (0..15). Independent of
/// the underlying Home Assistant entity names. An empty string (first byte
/// '\0') means "no custom name set".
struct DaliNamesBlob {
    char lamp_names[LAMP_COUNT][NAME_LEN];
    char group_names[GROUP_COUNT][NAME_LEN];
    char scene_names[SCENE_COUNT][NAME_LEN];
};

static_assert(sizeof(DaliNamesBlob) == (LAMP_COUNT + GROUP_COUNT + SCENE_COUNT) * NAME_LEN,
              "DaliNamesBlob must be a flat, padding-free array of fixed-size names");

/// @brief Copy `name` into `dst[NAME_LEN]`, truncating to NAME_LEN-1 bytes and
/// always NUL-terminating. An empty `name` clears the entry.
inline void copy_name(char dst[NAME_LEN], const std::string &name) {
    size_t n = name.size();
    if (n > NAME_LEN - 1) n = NAME_LEN - 1;
    std::memcpy(dst, name.data(), n);
    dst[n] = '\0';
}

/// @return true if `name` holds a non-empty custom name.
inline bool has_name(const char name[NAME_LEN]) {
    return name[0] != '\0';
}

/// @brief Force NUL-termination of every name in `blob`. Cheap insurance
/// against a corrupted or differently-laid-out blob loaded from flash.
inline void sanitize_blob(DaliNamesBlob &blob) {
    for (size_t i = 0; i < LAMP_COUNT; i++) blob.lamp_names[i][NAME_LEN - 1] = '\0';
    for (size_t i = 0; i < GROUP_COUNT; i++) blob.group_names[i][NAME_LEN - 1] = '\0';
    for (size_t i = 0; i < SCENE_COUNT; i++) blob.scene_names[i][NAME_LEN - 1] = '\0';
}

}  // namespace dali_names
