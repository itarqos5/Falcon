// The multiplayer protocol's data: the byte reader and writer and the item stacks as the network writes them (no game data is needed).
#include "falcon/net/bytes.hpp"
#include "falcon/net/multiplayer_client.hpp"
#include "falcon/net/protocol_data.hpp"

#include <iostream>

namespace {
int g_failures = 0;
}

#define CHECK(expr)                                                                        \
    do {                                                                                   \
        if (!(expr)) {                                                                     \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " << #expr << '\n'; \
            ++g_failures;                                                                  \
        }                                                                                  \
    } while (false)

using namespace falcon;
using namespace falcon::net;

namespace {

void test_varints() {
    for (const int v : {0, 1, 127, 128, 255, 300, 25565, 2097151, 2147483647, -1}) {
        Writer w;
        w.varint(v);
        Reader r(w.data);
        CHECK(r.varint() == v);
        CHECK(r.ok);
        CHECK(r.left() == 0);
    }
    Writer w;
    w.varint(300);
    CHECK(w.data.size() == 2);  // 300 takes two bytes
}

void test_reader_runs_out() {
    const Bytes none;
    Reader r(none);
    (void)r.i32();
    CHECK(!r.ok);  // reading past the end is an error, not a crash
}

ComponentIds ids() {
    ComponentIds c;
    c.damage = 3;
    c.enchantments = 9;
    c.customData = 20;
    c.unbreakable = 5;
    c.glintOverride = 11;
    return c;
}

void test_item_stacks() {
    {  // nothing: a count of zero is all there is
        Writer w;
        w.varint(0);
        Reader r(w.data);
        bool known = true;
        const ItemStack s = readItemStack(r, ids(), known);
        CHECK(s.isEmpty());
        CHECK(known);
        CHECK(r.left() == 0);
    }
    {  // a plain stack: five of item 123
        Writer w;
        w.varint(5);
        w.varint(123);
        w.varint(0);
        w.varint(0);
        Reader r(w.data);
        bool known = true;
        const ItemStack s = readItemStack(r, ids(), known);
        CHECK(static_cast<int>(s.item) == 123);
        CHECK(s.count == 5);
        CHECK(known);
        CHECK(r.left() == 0);
    }
    {  // components that can be read are stepped over, and the damage is kept; the removed ones are only a list of numbers
        Writer w;
        w.varint(1);
        w.varint(7);
        w.varint(4);  // four components
        w.varint(2);  // and two removed
        w.varint(3);  // damage
        w.varint(17);
        w.varint(9);  // enchantments: two of them
        w.varint(2);
        w.varint(11);
        w.varint(1);
        w.varint(12);
        w.varint(3);
        w.varint(5);  // unbreakable: a unit, which is nothing
        w.varint(20);  // custom data: an empty compound
        w.u8(10);
        w.u8(0);
        w.varint(40);  // removed
        w.varint(41);
        w.varint(99);  // after the stack: something else
        Reader r(w.data);
        bool known = true;
        const ItemStack s = readItemStack(r, ids(), known);
        CHECK(known);
        CHECK(s.damage == 17);
        CHECK(r.varint() == 99);  // the reader stands right after the stack
    }
    {  // a component that has no known layout stops the reading and says so
        Writer w;
        w.varint(2);
        w.varint(9);
        w.varint(1);
        w.varint(0);
        w.varint(77);  // not one of the components that can be read
        Reader r(w.data);
        bool known = true;
        const ItemStack s = readItemStack(r, ids(), known);
        CHECK(!known);
        CHECK(static_cast<int>(s.item) == 9);
        CHECK(s.count == 2);
    }
    {  // what is written (without components) reads back the same
        ItemStack s(static_cast<Item>(55), 32);
        Writer w;
        writeItemStack(w, s);
        Reader r(w.data);
        bool known = true;
        const ItemStack back = readItemStack(r, ids(), known);
        CHECK(known);
        CHECK(back.item == s.item);
        CHECK(back.count == 32);
        Writer none;
        writeItemStack(none, ItemStack());
        CHECK(none.data.size() == 1 && none.data[0] == 0);  // an empty stack is one zero
    }
}

// Which of the 18 default skins a UUID gets: Java's UUID.hashCode and Math.floorMod, worked out by hand.
void test_default_skins() {
    Uuid id{};
    CHECK(defaultSkinOf(id) == 0);  // the nil UUID hashes to 0
    id[7] = 1;                      // most significant half 1
    CHECK(defaultSkinOf(id) == 1);
    id = Uuid{};
    id[15] = 0x80;  // the least significant half 0x80 (the hash's low half is then 0x80 = 128; 128 mod 18 = 2)
    CHECK(defaultSkinOf(id) == 2);
    id = Uuid{};
    for (int i = 8; i < 16; ++i) id[static_cast<std::size_t>(i)] = 0xFF;
    id[15] = 0xFE;  // the least significant half is -2: the hash is (-1) ^ (-2) = 1
    CHECK(defaultSkinOf(id) == 1);
    id = Uuid{};
    id[12] = 0x80;  // the low half of the hash is 0x80000000, which is -2147483648 as a signed number; floorMod(-2147483648, 18) is 16
    CHECK(defaultSkinOf(id) == 16);
}

// Entity data values of the kinds whose layout is known are stepped over; one of another kind stops the reading.
void test_entity_data_skipping() {
    Writer w;
    w.u8(5);  // a byte (kind 0)
    w.varint(300);  // an int (kind 1)
    w.string("abc");  // a string (kind 4)
    w.boolean(false);  // an optional component that is absent (kind 6)
    w.boolean(true);  // an optional block position that is there (kind 11)
    w.i64(123456);
    w.varint(77);  // the marker after them all
    Reader r(w.data);
    CHECK(skipEntityDataValue(0, r));
    CHECK(skipEntityDataValue(1, r));
    CHECK(skipEntityDataValue(4, r));
    CHECK(skipEntityDataValue(6, r));
    CHECK(skipEntityDataValue(11, r));
    CHECK(r.varint() == 77);
    CHECK(!skipEntityDataValue(18, r));  // villager data: not known here
}

}  // namespace

int main() {
    test_varints();
    test_reader_runs_out();
    test_item_stacks();
    test_default_skins();
    test_entity_data_skipping();
    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "net protocol tests passed\n";
    return 0;
}
