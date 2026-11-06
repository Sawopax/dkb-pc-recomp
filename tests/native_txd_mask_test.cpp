// Owned instructions and sample values: checks lowering's register routing, not GPU texture filtering.
#include <array>
#include <cstdio>
#include <vector>
#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

using namespace Shader::Ampere;
static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); } } while (0)

static void bits(Instruction& op, unsigned at, unsigned count, u32 value) {
    for (unsigned b = 0; b < count; ++b) {
        const unsigned word = (at + b) / 32, bit = (at + b) % 32;
        op.words[word] = (op.words[word] & ~(1u << bit)) | (((value >> b) & 1u) << bit);
    }
}

static LoweredProgram lower(unsigned mask, unsigned high, unsigned dimension, unsigned predicate) {
    Instruction txd{}, exit{};
    bits(txd, 0, 12, 0xb6c);
    bits(txd, 12, 4, predicate);
    bits(txd, 16, 8, 12);           // first destination pair
    bits(txd, 24, 8, 32);           // coordinates
    bits(txd, 32, 8, 40);           // derivatives
    bits(txd, 40, 14, 16);          // texture descriptor offset
    bits(txd, 54, 5, 3);            // texture constant buffer
    bits(txd, 61, 3, dimension);
    txd.words[2] = 0x001e0000 | mask << 8 | high;
    bits(exit, 0, 12, 0x94d);
    bits(exit, 12, 4, 7);
    bits(exit, 87, 4, 7);
    const std::array code{txd, exit};
    return LowerToMaxwell(code, "full");
}

static void check_routing(unsigned mask, unsigned high, unsigned dimension, unsigned predicate, bool p0) {
    const auto result = lower(mask, high, dimension, predicate);
    CHECK(bool(result));
    if (!result) {
        std::fprintf(stderr, "mask %u high %u dim %u pred %u: %.*s\n", mask, high, dimension, predicate,
                     int(result.error.size()), result.error.data());
        return;
    }
    CHECK(result.required_texture_buffer == 3);
    std::array<unsigned, 256> regs{}, expected{};
    for (unsigned r = 0; r < regs.size(); ++r) regs[r] = expected[r] = 1000 + r;
    const bool enabled = predicate == 7 || (predicate == 0 ? p0 : !p0);
    if (enabled) {
        expected[12] = 11;
        expected[13] = 22;
        expected[high] = 33;
        if (mask == 15) expected[high + 1] = 44;
    }
    unsigned textures = 0;
    for (const auto op : result.instructions) {
        if ((op >> 56) == 0xde) {
            ++textures;
            CHECK(((op >> 31) & 15) == mask);
            CHECK(((op >> 28) & 7) == (dimension == 5 ? 3u : 2u));
            CHECK(((op >> 8) & 255) == 32);
            CHECK(((op >> 20) & 255) == 40);
            CHECK(((op >> 36) & 0x1fff) == 16);
            CHECK(((op >> 16) & 15) == predicate);
            if (enabled) {
                unsigned dst = op & 255;
                // Maxwell TXD packs selected components into consecutive registers, as both
                // Eglantine texture_gradient.cpp and Ryujinx InstEmitTexture.EmitTxd specify.
                for (unsigned component = 0; component < 4; ++component)
                    if (mask & (1u << component)) regs[dst++] = 11 * (component + 1);
            }
        } else if ((op >> 48) == 0x5c98) {
            CHECK(((op >> 16) & 15) == predicate);
            if (enabled) regs[op & 255] = regs[(op >> 20) & 255];
        }
    }
    CHECK(textures == 1);
    for (unsigned r = 0; r < 248; ++r) CHECK(regs[r] == expected[r]);
}

int main() {
    for (const unsigned dimension : {1u, 5u})
        for (const unsigned predicate : {0u, 7u, 8u})
            for (const bool p0 : {false, true}) {
                for (const unsigned high : {14u, 19u, 44u, 199u})
                    check_routing(7, high, dimension, predicate, p0);
                for (const unsigned high : {14u, 18u, 44u, 198u})
                    check_routing(15, high, dimension, predicate, p0);
            }
    for (unsigned mask = 0; mask < 16; ++mask)
        if (mask != 7 && mask != 15) CHECK(!lower(mask, 18, 1, 7));
    CHECK(!lower(15, 19, 1, 7));
    CHECK(!lower(7, 248, 1, 7));
    CHECK(!lower(7, 247, 1, 7));  // reserved by the lowering's global scratch-register guard
    CHECK(!lower(7, 19, 2, 7));
    std::printf("native TXD: %u register-routing checks, %u failures\n", checks, failures);
    return failures != 0;
}
