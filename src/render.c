#include <stdint.h>
#include "memmap.h"
#include "platform.h"
#include "platform_native.h"

/*
 * Native (oscar64 C) replacement for src/render.s. Phase 0 of the oscar64
 * migration measured plain C here beating the hand-tuned cc65-ABI assembly
 * by ~1.9-2x on code size (see OSCAR64_MIGRATION.md) - that hand-tuning
 * existed only to defeat cc65's calling-convention overhead, which oscar64
 * does not share, so these are ordinary portable loops rather than
 * self-modifying code.
 */

extern const uint8_t tile_data[];
extern const uint8_t tile_properties[];

/* Tile/color draw loop - one pass over the 20x11 tile grid, each tile
 * expanding to a 2x2 screen/color cell. TILE_DATA is 8 bytes/tile: top-left
 * char/color, top-right char/color, bottom-left char/color, bottom-right
 * char/color. */
void platform_map_draw_native(const PlatformRoom* room) {
    const uint8_t* t = room->tiles;
    uint16_t so = 0u;
    uint8_t row, col;

    for (row = 0u; row < MAP_HEIGHT_TILES; ++row) {
        for (col = 0u; col < MAP_WIDTH_TILES; ++col) {
            uint8_t tile = *t++;
            const uint8_t* td = tile_data + ((uint16_t)tile << 3);

            SCREEN_RAM[so]                    = td[0];
            platform_base_colors[so]           = td[1] & 0x0fu;
            SCREEN_RAM[so + 1u]                = td[2];
            platform_base_colors[so + 1u]      = td[3] & 0x0fu;
            SCREEN_RAM[so + MAP_WIDTH_CHARS]              = td[4];
            platform_base_colors[so + MAP_WIDTH_CHARS]    = td[5] & 0x0fu;
            SCREEN_RAM[so + MAP_WIDTH_CHARS + 1u]           = td[6];
            platform_base_colors[so + MAP_WIDTH_CHARS + 1u] = td[7] & 0x0fu;

            so += 2u;
        }
        so += MAP_WIDTH_CHARS;
    }
}

/* C prepares a clipped rectangle/source index in native_object_*; this
 * transfers screen/color bytes for one row-major object glyph, skipping
 * transparent (0) cells. */
void platform_object_draw_native(void) {
    const uint8_t* chars = native_object_type->chars;
    const uint8_t* colors = native_object_type->colors;
    uint16_t src = native_object_source;
    uint16_t dst = native_object_screen_offset;
    uint8_t row, col;

    for (row = 0u; row < native_object_rows; ++row) {
        for (col = 0u; col < native_object_columns; ++col) {
            uint8_t ch = chars[src];
            if (ch != 0u) {
                SCREEN_RAM[dst] = ch;
                platform_base_colors[dst] = colors[src] & 0x0fu;
            }
            ++src;
            ++dst;
        }
        src += native_object_row_skip;
        dst += MAP_WIDTH_CHARS - native_object_columns;
    }
}

/* C resolves the banked object type and prepares one clipped source
 * rectangle in native_light_*; this max-combines the light source's
 * distance-derived brightness into platform_brightness over that
 * rectangle. */
void platform_light_source_apply_native(void) {
    uint8_t row, col;
    uint16_t brightness_row = native_light_screen_offset;
    uint8_t visibility_row = native_light_visibility_offset;
    uint8_t world_y = native_light_min_y;

    for (row = 0u; row < native_light_rows; ++row) {
        uint8_t world_x = native_light_min_x;
        uint8_t delta_y = world_y < native_light_source_y
                              ? native_light_source_y - world_y
                              : world_y - native_light_source_y;
        for (col = 0u; col < native_light_columns; ++col) {
            if (platform_light_visibility[visibility_row + col]) {
                uint8_t delta_x = world_x < native_light_source_x
                                      ? native_light_source_x - world_x
                                      : world_x - native_light_source_x;
                /* Table is indexed in full tiles; radius/bands are in
                 * half-tile asset units, so double before comparing. */
                uint8_t distance = platform_light_distance[(uint16_t)delta_y * 16u + delta_x];
                distance = (uint8_t)(distance << 1);
                if (distance < native_light_radius) {
                    uint8_t remaining = native_light_radius - distance;
                    uint8_t level = remaining >= 4u ? 3u : (remaining >= 2u ? 2u : 1u);
                    if (level > platform_brightness[brightness_row + col]) {
                        platform_brightness[brightness_row + col] = level;
                    }
                }
            }
            ++world_x;
        }
        brightness_row += MAP_WIDTH_TILES;
        visibility_row += MAP_WIDTH_TILES;
        ++world_y;
    }
}

/* Translate base colors and brightness into final, visibility-masked
 * Color RAM in one tile-oriented pass. No fully-lit intermediate Color RAM
 * state exists. */
void platform_lighting_apply_native(void) {
    uint16_t so = 0u;
    uint8_t vi = 0u;
    uint8_t row, col;

    for (row = 0u; row < MAP_HEIGHT_TILES; ++row) {
        for (col = 0u; col < MAP_WIDTH_TILES; ++col) {
            if (platform_view_tiles[vi]) {
                uint8_t light = (uint8_t)(platform_brightness[vi] << 4);
                COLOR_RAM[so]                        = platform_light_colors[light | platform_base_colors[so]];
                COLOR_RAM[so + 1u]                   = platform_light_colors[light | platform_base_colors[so + 1u]];
                COLOR_RAM[so + MAP_WIDTH_CHARS]        = platform_light_colors[light | platform_base_colors[so + MAP_WIDTH_CHARS]];
                COLOR_RAM[so + MAP_WIDTH_CHARS + 1u]   = platform_light_colors[light | platform_base_colors[so + MAP_WIDTH_CHARS + 1u]];
            } else {
                COLOR_RAM[so] = 0u;
                COLOR_RAM[so + 1u] = 0u;
                COLOR_RAM[so + MAP_WIDTH_CHARS] = 0u;
                COLOR_RAM[so + MAP_WIDTH_CHARS + 1u] = 0u;
            }
            so += 2u;
            ++vi;
        }
        so += MAP_WIDTH_CHARS;
    }
}

/* Clear exactly the 40x22 map Color RAM region, preserving both status rows. */
void platform_color_clear_native(void) {
    uint16_t i;
    for (i = 0u; i < MAP_WIDTH_CHARS * MAP_HEIGHT_CHARS; ++i) {
        COLOR_RAM[i] = 0u;
    }
}

/* Straight 40x22 copy from platform_base_colors to Color RAM, used by the
 * static-full/LOS-disabled fast path in place of the real per-tile
 * brightness/view pass, when that combination makes the transform
 * provably the identity (see platform_lighting_rebuild()). */
void platform_color_copy_native(void) {
    uint16_t i;
    for (i = 0u; i < MAP_WIDTH_CHARS * MAP_HEIGHT_CHARS; ++i) {
        COLOR_RAM[i] = platform_base_colors[i];
    }
}

/* Room tiles occupy rows 0-21. Clear the separator and both status rows on
 * a full room redraw so bytes left by the previous room or a text screen
 * cannot leak into the split-screen text area. */
void platform_text_area_clear_native(void) {
    uint8_t x;
    for (x = 0u; x < MAP_WIDTH_CHARS; ++x) {
        SCREEN_RAM[22u * MAP_WIDTH_CHARS + x] = 0x20u;
        SCREEN_RAM[23u * MAP_WIDTH_CHARS + x] = 0x20u;
        SCREEN_RAM[24u * MAP_WIDTH_CHARS + x] = 0x20u;
        COLOR_RAM[22u * MAP_WIDTH_CHARS + x] = 0u;
        COLOR_RAM[23u * MAP_WIDTH_CHARS + x] = 0u;
        COLOR_RAM[24u * MAP_WIDTH_CHARS + x] = 0u;
    }
}

/* Produce a short VIC flash without modifying screen RAM or either
 * offscreen lighting buffer. */
void platform_lightning_native(void) {
    volatile uint8_t* vic = (volatile uint8_t*)0xd000u;
    uint8_t frame;
    uint8_t remaining;

    vic[0x20] = 1u;
    vic[0x21] = 1u;
    platform_color_clear_native();
    remaining = 2u;
    frame = platform_frame_counter;
    while (remaining != 0u) {
        while (frame == platform_frame_counter) {
        }
        frame = platform_frame_counter;
        --remaining;
    }
    vic[0x20] = 0u;
    vic[0x21] = 0u;
    platform_lighting_apply_native();
}

/* Build visibility as a one-parent ring propagation. States are 0
 * unprocessed, 1 visible, 2 occluded. Each ring partitions every cell in
 * the next ring; there is deliberately no merging or conflict resolution. */
static uint8_t vis_relative_offset(int8_t rel_x, int8_t rel_y, uint8_t* out_offset) {
    int16_t wx = (int16_t)native_visibility_origin_x + rel_x;
    int16_t wy;
    if (wx < 0 || wx >= MAP_WIDTH_TILES) return 0u;
    wy = (int16_t)native_visibility_origin_y + rel_y;
    if (wy < 0 || wy >= MAP_HEIGHT_TILES) return 0u;
    *out_offset = (uint8_t)((uint8_t)wy * MAP_WIDTH_TILES + (uint8_t)wx);
    return 1u;
}

static void vis_write_child(uint8_t outgoing_state, int8_t child_x, int8_t child_y) {
    uint8_t offset;
    if (vis_relative_offset(child_x, child_y, &offset)) {
        native_visibility_mask[offset] = outgoing_state;
    }
}

/* Structural translation of write_children in the original src/render.s -
 * the exact corner/edge/gap-child geometry is preserved verbatim rather
 * than re-derived, since "each ring partitions every cell in the next
 * ring" (no merge/conflict resolution) depends on precisely which cell
 * writes which child. */
static void vis_write_children(uint8_t ring, int8_t rel_x, int8_t rel_y,
                               uint8_t outgoing_state) {
    int8_t sign_x = rel_x > 0 ? 1 : (rel_x < 0 ? -1 : 0);
    int8_t sign_y = rel_y > 0 ? 1 : (rel_y < 0 ? -1 : 0);
    uint8_t abs_x = rel_x < 0 ? (uint8_t)-rel_x : (uint8_t)rel_x;
    uint8_t abs_y = rel_y < 0 ? (uint8_t)-rel_y : (uint8_t)rel_y;
    int8_t r = (int8_t)ring;

    if (abs_x == ring && abs_y == ring) {
        /* Corners have one diagonal child. */
        vis_write_child(outgoing_state, (int8_t)(rel_x + sign_x), (int8_t)(rel_y + sign_y));
        return;
    }
    if (abs_x == ring) {
        /* Vertical edge (left/right side, not corner). */
        if ((int8_t)(1 - r) == rel_y) {
            /* Near the upper corner: outward-up gap. */
            vis_write_child(outgoing_state, (int8_t)(rel_x + sign_x), (int8_t)(rel_y - 1));
        }
        vis_write_child(outgoing_state, (int8_t)(rel_x + sign_x), rel_y);
        if ((int8_t)(r - 1) == rel_y) {
            /* Near the lower corner: outward-down gap. */
            vis_write_child(outgoing_state, (int8_t)(rel_x + sign_x), (int8_t)(rel_y + 1));
        }
        return;
    }
    /* abs_y == ring: horizontal edge (top/bottom row, not corner). */
    if ((int8_t)(1 - r) == rel_x) {
        /* Near the left corner: outward-left gap. */
        vis_write_child(outgoing_state, (int8_t)(rel_x - 1), (int8_t)(rel_y + sign_y));
    }
    vis_write_child(outgoing_state, rel_x, (int8_t)(rel_y + sign_y));
    if ((int8_t)(r - 1) == rel_x) {
        /* Near the right corner: outward-right gap. */
        vis_write_child(outgoing_state, (int8_t)(rel_x + 1), (int8_t)(rel_y + sign_y));
    }
}

static void vis_process_tile(uint8_t ring, int8_t rel_x, int8_t rel_y) {
    uint8_t offset;
    uint8_t tile;
    uint8_t outgoing;

    if (!vis_relative_offset(rel_x, rel_y, &offset)) return;
    if (native_visibility_mask[offset] == 0u) return;
    if (native_visibility_mask[offset] == 2u) {
        outgoing = 2u;
    } else {
        tile = native_visibility_room->tiles[offset];
        outgoing = (tile_properties[tile] & PLATFORM_TILE_BLOCKS_VIEW) ? 2u : 1u;
    }
    vis_write_children(ring, rel_x, rel_y, outgoing);
}

void platform_visibility_build_native(void) {
    uint8_t i;
    uint8_t ring;
    int8_t x, y;
    uint8_t origin_offset = native_visibility_origin_offset;
    uint8_t origin_tile;

    for (i = 0u; i < MAP_WIDTH_TILES * MAP_HEIGHT_TILES; ++i) {
        native_visibility_mask[i] = 0u;
    }
    /* The viewer is always visible. */
    native_visibility_mask[origin_offset] = 1u;

    /* An opaque viewer does not initialize ring 1. */
    origin_tile = native_visibility_room->tiles[origin_offset];
    if (tile_properties[origin_tile] & PLATFORM_TILE_BLOCKS_VIEW) {
        goto normalize;
    }
    if (native_visibility_max_ring == 0u) goto normalize;

    /* Initialize all eight in-bounds neighbors as visible (unconditionally,
     * independent of their own tile properties - ring 1 is always seen,
     * even a wall tile bordering the viewer). */
    for (y = -1; y <= 1; ++y) {
        for (x = -1; x <= 1; ++x) {
            if (x == 0 && y == 0) continue;
            vis_write_child(1u, x, y);
        }
    }

    /* Rings 1..max_ring inclusive, each fully processed (top row, then
     * bottom row, then left/right sides) before moving to the next ring -
     * same order as the original, since a child cell could in principle
     * be targeted by more than one same-ring writer (see the gap-child
     * cases above) and there is no merge, just last-write-wins. */
    for (ring = 1u; ring <= native_visibility_max_ring; ++ring) {
        int8_t r = (int8_t)ring;
        for (x = -r; x <= r; ++x) vis_process_tile(ring, x, -r);
        for (x = -r; x <= r; ++x) vis_process_tile(ring, x, r);
        for (y = (int8_t)(-r + 1); y < r; ++y) {
            vis_process_tile(ring, -r, y);
            vis_process_tile(ring, r, y);
        }
    }

normalize:
    for (i = 0u; i < MAP_WIDTH_TILES * MAP_HEIGHT_TILES; ++i) {
        if (native_visibility_mask[i] == 2u) native_visibility_mask[i] = 0u;
    }
}
