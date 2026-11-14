#include "falcon/assets/nbt.hpp"

#include <stb_image.h>  // declarations only; also provides the raw-deflate decoder we reuse

#include <algorithm>
#include <array>
#include <cstring>

namespace falcon {
namespace {

constexpr int kMaxDepth = 512;  // the game's own limit

// Java's "modified UTF-8" differs from UTF-8 in two ways: U+0000 is the two bytes C0 80, and a
// character above U+FFFF is written as its two UTF-16 surrogates, three bytes each.
std::string fromModifiedUtf8(const unsigned char* p, std::size_t n) {
    std::string out;
    out.reserve(n);
    const auto surrogateAt = [&](std::size_t i, unsigned& unit) {
        if (i + 2 >= n || p[i] != 0xED || (p[i + 1] & 0xC0) != 0x80 || (p[i + 2] & 0xC0) != 0x80) return false;
        unit = 0xD000u | ((p[i + 1] & 0x3Fu) << 6) | (p[i + 2] & 0x3Fu);
        return unit >= 0xD800 && unit <= 0xDFFF;
    };
    for (std::size_t i = 0; i < n;) {
        unsigned high = 0;
        unsigned low = 0;
        if (p[i] == 0xC0 && i + 1 < n && p[i + 1] == 0x80) {
            out.push_back('\0');
            i += 2;
        } else if (surrogateAt(i, high) && high <= 0xDBFF && surrogateAt(i + 3, low) && low >= 0xDC00) {
            const unsigned cp = 0x10000u + ((high - 0xD800u) << 10) + (low - 0xDC00u);
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            i += 6;
        } else {
            out.push_back(static_cast<char>(p[i++]));
        }
    }
    return out;
}

// Big-endian reader over a byte range; every read reports failure instead of running past the end.
class Reader {
public:
    Reader(const unsigned char* data, std::size_t size) : data_(data), size_(size) {}

    [[nodiscard]] std::size_t remaining() const { return size_ - pos_; }

    bool u8(std::uint8_t& out) {
        if (remaining() < 1) return false;
        out = data_[pos_++];
        return true;
    }
    bool i16(std::int16_t& out) {
        std::uint16_t v = 0;
        if (!raw(v)) return false;
        out = static_cast<std::int16_t>(v);
        return true;
    }
    bool i32(std::int32_t& out) {
        std::uint32_t v = 0;
        if (!raw(v)) return false;
        out = static_cast<std::int32_t>(v);
        return true;
    }
    bool i64(std::int64_t& out) {
        std::uint64_t v = 0;
        if (!raw(v)) return false;
        out = static_cast<std::int64_t>(v);
        return true;
    }
    bool f32(double& out) {
        std::uint32_t v = 0;
        if (!raw(v)) return false;
        float f = 0.0f;
        std::memcpy(&f, &v, sizeof(f));
        out = static_cast<double>(f);
        return true;
    }
    bool f64(double& out) {
        std::uint64_t v = 0;
        if (!raw(v)) return false;
        std::memcpy(&out, &v, sizeof(out));
        return true;
    }
    bool string(std::string& out) {
        std::uint16_t length = 0;
        if (!raw(length) || remaining() < length) return false;
        out = fromModifiedUtf8(data_ + pos_, length);
        pos_ += length;
        return true;
    }

private:
    template <class T>
    bool raw(T& out) {
        if (remaining() < sizeof(T)) return false;
        T v = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i) v = static_cast<T>((v << 8) | data_[pos_ + i]);
        pos_ += sizeof(T);
        out = v;
        return true;
    }

    const unsigned char* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
};

bool readPayload(Reader& in, NbtTag::Type type, NbtTag& out, int depth);

// Reads the elements of an array tag: `bytesPer` is 1, 4 or 8.
bool readNumbers(Reader& in, std::size_t bytesPer, NbtTag& out) {
    std::int32_t count = 0;
    if (!in.i32(count) || count < 0 || in.remaining() / bytesPer < static_cast<std::size_t>(count)) return false;
    out.numbers.reserve(static_cast<std::size_t>(count));
    for (std::int32_t i = 0; i < count; ++i) {
        std::int64_t v = 0;
        if (bytesPer == 1) {
            std::uint8_t b = 0;
            if (!in.u8(b)) return false;
            v = static_cast<std::int8_t>(b);
        } else if (bytesPer == 4) {
            std::int32_t n = 0;
            if (!in.i32(n)) return false;
            v = n;
        } else if (!in.i64(v)) {
            return false;
        }
        out.numbers.push_back(v);
    }
    return true;
}

bool readPayload(Reader& in, NbtTag::Type type, NbtTag& out, int depth) {
    if (depth > kMaxDepth) return false;
    out.type = type;
    switch (type) {
        case NbtTag::Type::Byte: {
            std::uint8_t v = 0;
            if (!in.u8(v)) return false;
            out.integer = static_cast<std::int8_t>(v);
            return true;
        }
        case NbtTag::Type::Short: {
            std::int16_t v = 0;
            if (!in.i16(v)) return false;
            out.integer = v;
            return true;
        }
        case NbtTag::Type::Int: {
            std::int32_t v = 0;
            if (!in.i32(v)) return false;
            out.integer = v;
            return true;
        }
        case NbtTag::Type::Long: return in.i64(out.integer);
        case NbtTag::Type::Float: return in.f32(out.real);
        case NbtTag::Type::Double: return in.f64(out.real);
        case NbtTag::Type::String: return in.string(out.text);
        case NbtTag::Type::ByteArray: return readNumbers(in, 1, out);
        case NbtTag::Type::IntArray: return readNumbers(in, 4, out);
        case NbtTag::Type::LongArray: return readNumbers(in, 8, out);
        case NbtTag::Type::List: {
            std::uint8_t elementType = 0;
            std::int32_t count = 0;
            if (!in.u8(elementType) || !in.i32(count) || count < 0 || elementType > 12) return false;
            if (count > 0 && elementType == 0) return false;
            // Every element takes at least one byte except empty compounds' end tags, so this
            // keeps a corrupt count from reserving gigabytes.
            if (static_cast<std::size_t>(count) > in.remaining()) return false;
            out.items.resize(static_cast<std::size_t>(count));
            for (NbtTag& item : out.items) {
                if (!readPayload(in, static_cast<NbtTag::Type>(elementType), item, depth + 1)) return false;
            }
            return true;
        }
        case NbtTag::Type::Compound: {
            for (;;) {
                std::uint8_t childType = 0;
                if (!in.u8(childType) || childType > 12) return false;
                if (childType == 0) return true;  // End
                std::string name;
                if (!in.string(name)) return false;
                NbtTag child;
                if (!readPayload(in, static_cast<NbtTag::Type>(childType), child, depth + 1)) return false;
                out.fields.emplace_back(std::move(name), std::move(child));
            }
        }
        case NbtTag::Type::End: return false;
    }
    return false;
}

}  // namespace

const NbtTag* NbtTag::get(std::string_view key) const {
    if (type != Type::Compound) return nullptr;
    for (const auto& [name, value] : fields) {
        if (name == key) return &value;
    }
    return nullptr;
}

std::string NbtTag::getString(std::string_view key, const std::string& fallback) const {
    const NbtTag* tag = get(key);
    return tag != nullptr && tag->type == Type::String ? tag->text : fallback;
}

std::int64_t NbtTag::getInt(std::string_view key, std::int64_t fallback) const {
    const NbtTag* tag = get(key);
    if (tag == nullptr || !tag->isNumber()) return fallback;
    return tag->isInteger() ? tag->integer : static_cast<std::int64_t>(tag->real);
}

double NbtTag::getDouble(std::string_view key, double fallback) const {
    const NbtTag* tag = get(key);
    if (tag == nullptr || !tag->isNumber()) return fallback;
    return tag->isInteger() ? static_cast<double>(tag->integer) : tag->real;
}

bool NbtTag::getBool(std::string_view key, bool fallback) const {
    const NbtTag* tag = get(key);
    if (tag == nullptr || !tag->isNumber()) return fallback;
    return getInt(key) != 0;
}

std::optional<std::vector<unsigned char>> gunzip(const unsigned char* data, std::size_t size) {
    // Header: 1f 8b, method 8, flags, mtime(4), extra flags, OS; then optional fields.
    if (size < 18 || data[0] != 0x1f || data[1] != 0x8b || data[2] != 8) return std::nullopt;
    const unsigned flags = data[3];
    std::size_t pos = 10;
    if ((flags & 0x04) != 0) {  // FEXTRA
        if (pos + 2 > size) return std::nullopt;
        pos += 2 + (static_cast<std::size_t>(data[pos]) | (static_cast<std::size_t>(data[pos + 1]) << 8));
    }
    for (const unsigned zeroTerminated : {0x08u, 0x10u}) {  // FNAME, FCOMMENT
        if ((flags & zeroTerminated) == 0) continue;
        while (pos < size && data[pos] != 0) ++pos;
        ++pos;
    }
    if ((flags & 0x02) != 0) pos += 2;  // FHCRC
    if (pos + 8 > size) return std::nullopt;

    const std::size_t deflateSize = size - pos - 8;  // the last 8 bytes are CRC32 and ISIZE
    int outLength = 0;
    char* inflated = stbi_zlib_decode_noheader_malloc(reinterpret_cast<const char*>(data + pos),
                                                      static_cast<int>(deflateSize), &outLength);
    if (inflated == nullptr) return std::nullopt;
    std::vector<unsigned char> out(reinterpret_cast<unsigned char*>(inflated),
                                   reinterpret_cast<unsigned char*>(inflated) + outLength);
    stbi_image_free(inflated);

    const unsigned char* trailer = data + size - 4;  // ISIZE: the uncompressed length mod 2^32
    const std::uint32_t isize = static_cast<std::uint32_t>(trailer[0]) | (static_cast<std::uint32_t>(trailer[1]) << 8) |
                                (static_cast<std::uint32_t>(trailer[2]) << 16) | (static_cast<std::uint32_t>(trailer[3]) << 24);
    if (static_cast<std::uint32_t>(out.size()) != isize) return std::nullopt;
    return out;
}

std::optional<NbtTag> parseNbt(const std::vector<unsigned char>& bytes) {
    std::vector<unsigned char> inflated;
    const std::vector<unsigned char>* body = &bytes;
    if (bytes.size() >= 2 && bytes[0] == 0x1f && bytes[1] == 0x8b) {
        auto raw = gunzip(bytes.data(), bytes.size());
        if (!raw) return std::nullopt;
        inflated = std::move(*raw);
        body = &inflated;
    }

    Reader in(body->data(), body->size());
    std::uint8_t rootType = 0;
    std::string rootName;
    if (!in.u8(rootType) || rootType == 0 || rootType > 12 || !in.string(rootName)) return std::nullopt;
    NbtTag root;
    if (!readPayload(in, static_cast<NbtTag::Type>(rootType), root, 0)) return std::nullopt;
    return root;
}

// ---- writing ------------------------------------------------------------------------------

NbtTag NbtTag::compound() {
    NbtTag tag;
    tag.type = Type::Compound;
    return tag;
}

NbtTag NbtTag::ofString(std::string value) {
    NbtTag tag;
    tag.type = Type::String;
    tag.text = std::move(value);
    return tag;
}

NbtTag NbtTag::ofInteger(Type type, std::int64_t value) {
    NbtTag tag;
    tag.type = type;
    tag.integer = value;
    return tag;
}

NbtTag NbtTag::ofList(std::vector<NbtTag> items) {
    NbtTag tag;
    tag.type = Type::List;
    tag.items = std::move(items);
    return tag;
}

NbtTag& NbtTag::put(std::string key, NbtTag value) {
    for (auto& [name, existing] : fields) {
        if (name == key) {
            existing = std::move(value);
            return *this;
        }
    }
    fields.emplace_back(std::move(key), std::move(value));
    return *this;
}

namespace {

class Writer {
public:
    std::vector<unsigned char> bytes;

    void u8(unsigned v) { bytes.push_back(static_cast<unsigned char>(v)); }
    template <class T>
    void big(T v) {  // big-endian, `sizeof(T)` bytes
        for (int shift = static_cast<int>(sizeof(T)) * 8 - 8; shift >= 0; shift -= 8) {
            bytes.push_back(static_cast<unsigned char>((static_cast<std::uint64_t>(v) >> shift) & 0xFF));
        }
    }
    // u16 length + modified UTF-8. Anything longer than the format allows is cut (the game would
    // refuse to write it at all).
    void string(std::string_view utf8) {
        std::string encoded;
        encoded.reserve(utf8.size());
        for (std::size_t i = 0; i < utf8.size();) {
            const unsigned char c = static_cast<unsigned char>(utf8[i]);
            std::size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
            if (i + len > utf8.size()) len = 1;
            if (c == 0) {
                encoded += "\xC0\x80";
            } else if (len == 4) {
                const unsigned cp = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(utf8[i + 1]) & 0x3Fu) << 12) |
                                    ((static_cast<unsigned char>(utf8[i + 2]) & 0x3Fu) << 6) |
                                    (static_cast<unsigned char>(utf8[i + 3]) & 0x3Fu);
                const unsigned v = cp - 0x10000u;
                for (const unsigned unit : {0xD800u + (v >> 10), 0xDC00u + (v & 0x3FFu)}) {
                    encoded.push_back(static_cast<char>(0xE0 | (unit >> 12)));
                    encoded.push_back(static_cast<char>(0x80 | ((unit >> 6) & 0x3F)));
                    encoded.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
                }
            } else {
                encoded.append(utf8.substr(i, len));
            }
            i += len;
        }
        if (encoded.size() > 0xFFFF) encoded.resize(0xFFFF);
        big<std::uint16_t>(static_cast<std::uint16_t>(encoded.size()));
        bytes.insert(bytes.end(), encoded.begin(), encoded.end());
    }
};

void writePayload(Writer& out, const NbtTag& tag) {
    switch (tag.type) {
        case NbtTag::Type::Byte: out.big<std::uint8_t>(static_cast<std::uint8_t>(tag.integer)); break;
        case NbtTag::Type::Short: out.big<std::uint16_t>(static_cast<std::uint16_t>(tag.integer)); break;
        case NbtTag::Type::Int: out.big<std::uint32_t>(static_cast<std::uint32_t>(tag.integer)); break;
        case NbtTag::Type::Long: out.big<std::uint64_t>(static_cast<std::uint64_t>(tag.integer)); break;
        case NbtTag::Type::Float: {
            const float f = static_cast<float>(tag.real);
            std::uint32_t bits = 0;
            std::memcpy(&bits, &f, sizeof(bits));
            out.big<std::uint32_t>(bits);
            break;
        }
        case NbtTag::Type::Double: {
            std::uint64_t bits = 0;
            std::memcpy(&bits, &tag.real, sizeof(bits));
            out.big<std::uint64_t>(bits);
            break;
        }
        case NbtTag::Type::String: out.string(tag.text); break;
        case NbtTag::Type::ByteArray:
            out.big<std::uint32_t>(static_cast<std::uint32_t>(tag.numbers.size()));
            for (const std::int64_t v : tag.numbers) out.big<std::uint8_t>(static_cast<std::uint8_t>(v));
            break;
        case NbtTag::Type::IntArray:
            out.big<std::uint32_t>(static_cast<std::uint32_t>(tag.numbers.size()));
            for (const std::int64_t v : tag.numbers) out.big<std::uint32_t>(static_cast<std::uint32_t>(v));
            break;
        case NbtTag::Type::LongArray:
            out.big<std::uint32_t>(static_cast<std::uint32_t>(tag.numbers.size()));
            for (const std::int64_t v : tag.numbers) out.big<std::uint64_t>(static_cast<std::uint64_t>(v));
            break;
        case NbtTag::Type::List:
            out.u8(tag.items.empty() ? 0u : static_cast<unsigned>(tag.items.front().type));
            out.big<std::uint32_t>(static_cast<std::uint32_t>(tag.items.size()));
            for (const NbtTag& item : tag.items) writePayload(out, item);
            break;
        case NbtTag::Type::Compound:
            for (const auto& [name, child] : tag.fields) {
                out.u8(static_cast<unsigned>(child.type));
                out.string(name);
                writePayload(out, child);
            }
            out.u8(0);  // End
            break;
        case NbtTag::Type::End: break;
    }
}

std::uint32_t crc32(const std::vector<unsigned char>& data) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1u) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const unsigned char b : data) crc = table[(crc ^ b) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

void littleEndian32(std::vector<unsigned char>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xFF));
}

}  // namespace

std::vector<unsigned char> serializeNbt(const NbtTag& root, std::string_view rootName) {
    Writer out;
    out.u8(static_cast<unsigned>(root.type));
    out.string(rootName);
    writePayload(out, root);
    return std::move(out.bytes);
}

std::vector<unsigned char> gzipStored(const std::vector<unsigned char>& data) {
    // Header: magic, method 8 (deflate), no flags, no timestamp, no extra flags, OS "unknown".
    std::vector<unsigned char> out = {0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 0xff};
    constexpr std::size_t kBlock = 0xFFFF;  // the most a stored block can hold
    std::size_t pos = 0;
    do {
        const std::size_t n = std::min(kBlock, data.size() - pos);
        const bool last = pos + n >= data.size();
        out.push_back(last ? 1 : 0);  // BFINAL, BTYPE = 00 (stored)
        out.push_back(static_cast<unsigned char>(n & 0xFF));
        out.push_back(static_cast<unsigned char>(n >> 8));
        out.push_back(static_cast<unsigned char>(~n & 0xFF));
        out.push_back(static_cast<unsigned char>((~n >> 8) & 0xFF));
        out.insert(out.end(), data.begin() + static_cast<std::ptrdiff_t>(pos),
                   data.begin() + static_cast<std::ptrdiff_t>(pos + n));
        pos += n;
    } while (pos < data.size());
    littleEndian32(out, crc32(data));
    littleEndian32(out, static_cast<std::uint32_t>(data.size()));
    return out;
}

}  // namespace falcon
