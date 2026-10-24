// Board Thickness setting.
//
// Each snowboard model is two flat cards 4 units apart (top and base, each
// split into two halves), and the board's rounded outline comes from the
// texture's transparency. Nothing closes the gap between the cards, so on the
// board select screen, where the chosen board turns, it looks paper thin.
//
// "Thick" (the default) gives the boards a solid edge there. The game's model
// writer (bootup_05CA28) puts each card half into the display list as one
// vertex load and two triangles; recomp/us.toml hooks its end. For a board
// card while rider/board select is loaded, the vertex load becomes a call to a
// display list built here, in RDRAM above the game's 8 MB (the game never uses
// it, and RT64 reads up to 16 MB). That list draws copies of the card stepping
// in through the board's thickness, unlit and darker so they read as the
// board's edge, then loads the card's vertices moved out to the new thickness,
// so the game's own two triangles draw the face there.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <variant>

#include "game_config.h"
#include "recomp.h"
#include "recompui/config.h"

extern "C" bool game_module_loaded(const char* name);

namespace {
enum class BoardThickness : uint32_t {
    Original = 0,
    Thick = 1,
};

constexpr const char* option_id = "board_thickness";

BoardThickness mode() {
    auto& config = recompui::config::get_graphics_config();
    if (config.has_option(option_id)) {
        return static_cast<BoardThickness>(std::get<uint32_t>(config.get_option_value(option_id)));
    }
    return BoardThickness::Thick;
}

// Board card shape: 700 x 170 units, cards at y = -2 and +2.
constexpr int32_t card_y = 2;
// New half thickness. Edge layers sit one unit apart (vertex positions are
// whole units), from just inside the face to the board's middle.
constexpr int32_t half_thickness = 8;
constexpr int32_t layer_count = half_thickness;
// Shade of the edge layers (the texture is multiplied by it).
constexpr uint32_t edge_color = 0x3A3A40FF;

// Display list commands (F3DEX).
constexpr uint32_t g_vtx_4 = 0x0400103F;   // load 4 vertices into slots 0-3
constexpr uint32_t g_tri1 = 0xBF000000;
constexpr uint32_t g_dl_call = 0x06000000;
constexpr uint32_t g_cleargeometrymode = 0xB6000000;
constexpr uint32_t g_setgeometrymode = 0xB7000000;
constexpr uint32_t g_enddl = 0xB8000000;
constexpr uint32_t g_lighting = 0x00020000;
constexpr uint32_t g_cull_back = 0x00002000;

// Unused RDRAM for the generated lists and vertices, used as a ring. One board
// select screen needs about 60 KB; a list is only ever reused after more than
// a hundred visits to the screen, long after the game freed the old boards.
constexpr uint32_t pool_start = 0x80C00000;
constexpr uint32_t pool_end = 0x81000000;
uint32_t pool_next = pool_start;

gpr pool_alloc(uint32_t size) {
    size = (size + 15) & ~15u;
    if (pool_next + size > pool_end) {
        pool_next = pool_start;
    }
    uint32_t addr = pool_next;
    pool_next += size;
    return (gpr)(int32_t)addr;
}

uint32_t to_physical(gpr addr) {
    return (uint32_t)addr & 0x1FFFFFFF;
}

struct Vertex {
    uint32_t words[4];   // x|y, z|flag, s|t, color or normal
    int16_t x() const { return (int16_t)(words[0] >> 16); }
    int16_t y() const { return (int16_t)(words[0] & 0xFFFF); }
    int16_t z() const { return (int16_t)(words[1] >> 16); }
    void set_y(int32_t y) { words[0] = (words[0] & 0xFFFF0000u) | ((uint32_t)y & 0xFFFF); }
};

bool is_board_card(const Vertex (&v)[4]) {
    const int32_t y = v[0].y();
    if (std::abs(y) != card_y) {
        return false;
    }
    int32_t max_x = 0;
    for (const Vertex& vert : v) {
        if (vert.y() != y) {
            return false;
        }
        const int32_t az = std::abs(vert.z());
        if (az < 60 || az > 120) {
            return false;
        }
        const int32_t ax = std::abs(vert.x());
        if (ax > 500) {
            return false;
        }
        max_x = std::max(max_x, ax);
    }
    return max_x >= 200;
}

void write_vertices(uint8_t* rdram, gpr addr, const Vertex (&v)[4]) {
    for (int i = 0; i < 4; i++) {
        for (int w = 0; w < 4; w++) {
            MEM_W(i * 16 + w * 4, addr) = (int32_t)v[i].words[w];
        }
    }
}
}

namespace game {
void add_board_thickness_option(recomp::config::Config& graphics) {
    graphics.add_enum_option(
        option_id,
        "Board Thickness",
        "How the boards look on the board select screen. "
        "<recomp-color primary>Original</recomp-color> shows them paper thin like the N64. "
        "<recomp-color primary>Thick</recomp-color> (the default) gives them a solid, darker edge that shows as the chosen board turns.",
        {
            { BoardThickness::Original, "Original", "Original" },
            { BoardThickness::Thick, "Thick", "Thick" },
        },
        BoardThickness::Thick);
}
}

// End of bootup_05CA28, after it wrote one vertex load and its triangles.
// $s0 is the batch: +0x4 vertex count, +0x8 triangle count. The vertex load
// command's address is at 0x20($sp).
extern "C" void game_board_batch_end(uint8_t* rdram, recomp_context* ctx) {
    if (mode() != BoardThickness::Thick) {
        return;
    }
    const gpr batch = ctx->r16;
    if (MEM_W(0x4, batch) != 4 || MEM_W(0x8, batch) != 2) {
        return;
    }
    if (!game_module_loaded("charsel.uso")) {
        return;
    }

    const gpr cmd = (gpr)MEM_W(0x20, ctx->r29);
    if ((uint32_t)MEM_W(0x0, cmd) != g_vtx_4 ||
        (uint32_t)MEM_W(0x8, cmd) != g_tri1 || (uint32_t)MEM_W(0x10, cmd) != g_tri1) {
        return;
    }
    const uint32_t tri_a = (uint32_t)MEM_W(0xC, cmd);
    const uint32_t tri_b = (uint32_t)MEM_W(0x14, cmd);
    const gpr vtx = (gpr)(int32_t)(((uint32_t)MEM_W(0x4, cmd) & 0x00FFFFFF) | 0x80000000u);

    Vertex card[4];
    for (int i = 0; i < 4; i++) {
        for (int w = 0; w < 4; w++) {
            card[i].words[w] = (uint32_t)MEM_W(i * 16 + w * 4, vtx);
        }
    }
    if (!is_board_card(card)) {
        return;
    }
    const int32_t side = (card[0].y() > 0) ? 1 : -1;

    // Vertices: one set per edge layer, then the moved face.
    const gpr verts = pool_alloc((layer_count + 1) * 64);
    for (int layer = 0; layer < layer_count; layer++) {
        Vertex v[4];
        for (int i = 0; i < 4; i++) {
            v[i] = card[i];
            v[i].set_y(side * (half_thickness - 1 - layer));
            v[i].words[3] = edge_color;
        }
        write_vertices(rdram, verts + layer * 64, v);
    }
    Vertex face[4];
    for (int i = 0; i < 4; i++) {
        face[i] = card[i];
        face[i].set_y(side * half_thickness);
    }
    const gpr face_verts = verts + layer_count * 64;
    write_vertices(rdram, face_verts, face);

    // Display list: 1 + 3 per layer + 3 commands.
    const gpr dl = pool_alloc((1 + 3 * layer_count + 3) * 8);
    gpr out = dl;
    auto emit = [&](uint32_t w0, uint32_t w1) {
        MEM_W(0, out) = (int32_t)w0;
        MEM_W(4, out) = (int32_t)w1;
        out += 8;
    };
    emit(g_cleargeometrymode, g_lighting | g_cull_back);
    for (int layer = 0; layer < layer_count; layer++) {
        emit(g_vtx_4, to_physical(verts + layer * 64));
        emit(g_tri1, tri_a);
        emit(g_tri1, tri_b);
    }
    emit(g_setgeometrymode, g_lighting | g_cull_back);
    emit(g_vtx_4, to_physical(face_verts));
    emit(g_enddl, 0);

    // Call it in place of the vertex load; the game's triangles then draw the face.
    MEM_W(0x0, cmd) = (int32_t)g_dl_call;
    MEM_W(0x4, cmd) = (int32_t)to_physical(dl);
}
