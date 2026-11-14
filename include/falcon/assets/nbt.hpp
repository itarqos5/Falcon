#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace falcon {

// One value of Minecraft's NBT format (what `level.dat` and `.dat` files are made of).
struct NbtTag {
    enum class Type { End, Byte, Short, Int, Long, Float, Double, ByteArray, String, List, Compound, IntArray, LongArray };

    Type type = Type::End;
    std::int64_t integer = 0;                              // Byte, Short, Int, Long
    double real = 0.0;                                     // Float, Double
    std::string text;                                      // String
    std::vector<NbtTag> items;                             // List
    std::vector<std::pair<std::string, NbtTag>> fields;    // Compound, in file order
    std::vector<std::int64_t> numbers;                     // ByteArray, IntArray, LongArray

    [[nodiscard]] bool isInteger() const { return type >= Type::Byte && type <= Type::Long; }
    [[nodiscard]] bool isNumber() const { return isInteger() || type == Type::Float || type == Type::Double; }

    // Compound lookup; null if this is not a compound or has no such key.
    [[nodiscard]] const NbtTag* get(std::string_view key) const;
    // Typed lookups with the defaults the game uses for a missing or differently typed entry.
    [[nodiscard]] std::string getString(std::string_view key, const std::string& fallback = {}) const;
    [[nodiscard]] std::int64_t getInt(std::string_view key, std::int64_t fallback = 0) const;
    [[nodiscard]] double getDouble(std::string_view key, double fallback = 0.0) const;
    [[nodiscard]] bool getBool(std::string_view key, bool fallback = false) const;  // a nonzero byte
    [[nodiscard]] bool contains(std::string_view key) const { return get(key) != nullptr; }

    // ---- building tags (for writing files) ----
    [[nodiscard]] static NbtTag compound();
    [[nodiscard]] static NbtTag ofString(std::string value);
    // `type` is Byte, Short, Int or Long; the value is truncated to that width when written.
    [[nodiscard]] static NbtTag ofInteger(Type type, std::int64_t value);
    [[nodiscard]] static NbtTag ofBool(bool value) { return ofInteger(Type::Byte, value ? 1 : 0); }
    [[nodiscard]] static NbtTag ofList(std::vector<NbtTag> items);  // every item must have the same type
    // Adds (or replaces) a field of a compound and returns the compound, so calls can be chained.
    NbtTag& put(std::string key, NbtTag value);
};

// Writes a whole NBT document: the type byte and name of the root, then the root's contents.
// Strings use Java's "modified UTF-8" (what the game reads back). The result is raw NBT; pass it
// to gzipStored() for a file the game accepts (level.dat, servers.dat's sibling .dat files).
[[nodiscard]] std::vector<unsigned char> serializeNbt(const NbtTag& root, std::string_view rootName = "");

// Wraps `data` in a gzip stream made of stored (uncompressed) deflate blocks. It is a valid gzip file
// for any reader (including Java's GZIPInputStream) and needs no compressor.
[[nodiscard]] std::vector<unsigned char> gzipStored(const std::vector<unsigned char>& data);

// Parses a whole NBT document (a named root tag, almost always a compound). Accepts the
// gzip-compressed form that Minecraft writes as well as raw NBT. Nullopt if it is malformed.
[[nodiscard]] std::optional<NbtTag> parseNbt(const std::vector<unsigned char>& bytes);

// Decompresses a gzip stream (the header is skipped; the length trailer is checked). Nullopt on error.
[[nodiscard]] std::optional<std::vector<unsigned char>> gunzip(const unsigned char* data, std::size_t size);

}  // namespace falcon
