#!/usr/bin/env python3
"""Append fixed-layout runtime room and object assets to an EasyFlash image."""

from __future__ import annotations

import argparse
from pathlib import Path

from easyflash_layout import ModulePlacement, load_layout

BANK_BYTES = 0x4000
ROML_BYTES = 0x2000
# Room text is a separate same-ID resource (see load_resource/pack_resources
# below), not part of the room file - see PlatformRoom in src/platform.h.
ROOM_BYTES = 13 + 220 + 768
ROOMS_PER_BANK = 6
FIRST_ROOM_BANK = 3
ROOM_BANKS = 43
TYPE_BANK_0 = 46
TYPE_BANK_1 = 47
# Each 64-byte objects.cobj record splits into a 35-byte hot part (resident,
# see PlatformObjectType in src/platform.h) and a 15-byte cold part
# (name+flags, fetched on demand via platform_object_type_info_get()); the
# remaining 14 bytes are unused padding and are dropped. Keep these in sync
# with src/platform.c's OBJECT_TYPE_* constants and src/platform.inc.
OBJECT_TYPE_FILE_BYTES = 64
OBJECT_TYPE_HOT_BYTES = 35
OBJECT_TYPE_COLD_BYTES = 15
OBJECT_TYPE_ZONE_A_COUNT = 106
OBJECT_TYPE_ZONE_B_COUNT = 117
OBJECT_TYPE_ZONE_AB_COUNT = OBJECT_TYPE_ZONE_A_COUNT + OBJECT_TYPE_ZONE_B_COUNT
OBJECT_TYPE_ZONE_C_COUNT = 256 - OBJECT_TYPE_ZONE_AB_COUNT
OBJECT_TYPE_ZONE_AB_BYTES = OBJECT_TYPE_ZONE_AB_COUNT * OBJECT_TYPE_HOT_BYTES
OBJECT_TYPE_ZONE_C_BYTES = OBJECT_TYPE_ZONE_C_COUNT * OBJECT_TYPE_HOT_BYTES
# Offset within TYPE_BANK_0's ROMH half, which holds nothing else.
OBJECT_TYPE_COLD_BASE = 0
# Fixed overlay and in-place module coordinates come from
# cfg/easyflash_layout.json. The same file generates the C/ca65 constants
# consumed by the runtime, and the final image is checked independently by
# tools/validate_easyflash_layout.py.
# Compiled script/conversation/room bytecode (tools/compile_script.py) is
# data, not code, and goes through the generic resource directory below like
# any other resource - NOT through SCRIPT_BANK/OFFSET, which is only this
# overlay's own interpreter code.
#
# Each of the three declaration kinds (script/conversation/room - see
# tools/compile_script.py's docstring) gets its own independent 256-entry
# directory, so IDs don't need to be partitioned across kinds: a conversation
# and a standalone script can both use ID 5 without colliding, and rooms can
# use the full 0x00-0xFF range. Source layout (see the Makefile):
#   assets/scripts/<ID>.script              - room (ID == the room's own ID)
#   assets/scripts/conversations/<ID>.script - conversation
#   assets/scripts/cutscenes/<ID>.script     - standalone script (cutscene)
# assets/resources/<ID> (raw, unstructured bytes, no ABI - see
# platform_resource_fetch() in src/platform.c) shares the script/cutscene
# kind's directory, since it's likewise standalone content not tied to a
# room or conversation.
RESOURCE_KIND_SCRIPT = 0
RESOURCE_KIND_CONVERSATION = 1
RESOURCE_KIND_ROOM = 2
# A room's environment module (weather + ambient sound - see
# src/platform.h's PLATFORM_RESOURCE_KIND_ENVIRONMENT and rooms/env/). Built
# from rooms/env/<ID>.s the same way room code is: assembled, resolved
# against build/game.lbl, linked at a fixed origin (cfg/env_module.cfg
# targets ENVCODE_BASE) - unlike the other three kinds' build-intermediate
# files, this one is a linked, relocated binary, not a raw asset copy or a
# tools/compile_script.py output.
RESOURCE_KIND_ENVIRONMENT = 3
# Static assets that used to be linked into the program image (charsets, tile
# bitmaps + properties, sprite bitmaps) - see src/platform.h's
# PLATFORM_RESOURCE_KIND_ASSET. Fetched at boot straight to fixed destinations,
# so they cost nothing in the contiguous PRG blob cart/ef_boot.s copies.
RESOURCE_KIND_ASSET = 4
# Build-intermediate filename prefix per kind (build/assets/<prefix><ID>) -
# keep in sync with the Makefile's rules and src/platform.h's
# PLATFORM_RESOURCE_KIND_* constants, which use the same 0-4 values.
RESOURCE_KIND_PREFIX = {
    RESOURCE_KIND_SCRIPT: "RS",
    RESOURCE_KIND_CONVERSATION: "RC",
    RESOURCE_KIND_ROOM: "RR",
    RESOURCE_KIND_ENVIRONMENT: "RE",
    RESOURCE_KIND_ASSET: "RA",
}
# Packing/lookup order. Four 256-entry directories exactly fill the directory
# bank's 8 KiB ROML half, so kinds 0-3 live there and kinds 4+ continue at the
# head of its ROMH half - resource_directory_lookup() in src/platform.c splits
# on exactly this boundary.
RESOURCE_KIND_ORDER = (
    RESOURCE_KIND_SCRIPT,
    RESOURCE_KIND_CONVERSATION,
    RESOURCE_KIND_ROOM,
    RESOURCE_KIND_ENVIRONMENT,
    RESOURCE_KIND_ASSET,
)
PORTRAIT_BYTES = 256
PORTRAITS_PER_BANK = 32
FIRST_PORTRAIT_BANK = 49
PORTRAIT_BANKS = 8
# Generic sparse resource directories: three 256-entry, read-only,
# variable-size-blob directories (one per kind above), back to back at the
# head of RESOURCE_DIRECTORY_BANK's ROML half, in RESOURCE_KIND_* order.
# Reserves every remaining EasyFlash bank up to the 64-bank hardware limit,
# so no banks remain free after this pool.
RESOURCE_DIRECTORY_BANK = FIRST_PORTRAIT_BANK + PORTRAIT_BANKS
RESOURCE_FIRST_BANK = RESOURCE_DIRECTORY_BANK
RESOURCE_LAST_BANK = 63
RESOURCE_HALF_BYTES = ROML_BYTES
RESOURCE_DIRECTORY_ENTRY_BYTES = 8
RESOURCE_DIRECTORY_BYTES = 256 * RESOURCE_DIRECTORY_ENTRY_BYTES
# Four 256-entry directories exactly fill the 8 KiB ROML half; any further
# kinds continue at the head of the ROMH half. Payload therefore starts after
# the ROMH directories, not after the ROML ones.
RESOURCE_DIRECTORIES_PER_HALF = 4
RESOURCE_ROMH_DIRECTORY_BYTES = (
    (len(RESOURCE_KIND_ORDER) - RESOURCE_DIRECTORIES_PER_HALF)
    * RESOURCE_DIRECTORY_BYTES)
OUTPUT_BANKS = RESOURCE_LAST_BANK + 1
ROOM_CODE_HEADER_BYTES = 24
ROOM_CODE_MAX_BYTES = 0x0400
ROOM_CODE_ABI = 4
ROOM_CODE_DIRECTORY_BANK = 3
ROOM_CODE_DIRECTORY_BYTES = 0x800
ROOM_CODE_FIRST_BANK = 3
ROOM_CODE_LAST_BANK = TYPE_BANK_1
LOADED_OVERLAY_ADDRESS = 0xB000
LOADED_OVERLAY_HEADER_BYTES = 16
LOADED_OVERLAY_MAX_BYTES = 0x1000
LOADED_OVERLAY_ABI = 1


def split_object_type(record: bytes) -> tuple[bytes, bytes]:
    """Split one 64-byte objects.cobj record into (hot 35B, cold 15B).

    Layout (see the PlatformObjectType/PlatformObjectTypeInfo comment in
    src/platform.h): byte 0 dimensions, byte 1 hotspot, bytes 2-15 name,
    bytes 16-31 chars, bytes 32-47 colors, byte 48 flags, byte 49 light,
    bytes 50-63 unused padding (dropped).
    """
    dimensions_hotspot = record[0:2]
    name = record[2:16]
    chars = record[16:32]
    colors = record[32:48]
    flags = record[48:49]
    light = record[49:50]
    hot = dimensions_hotspot + chars + colors + light
    cold = name + flags
    assert len(hot) == OBJECT_TYPE_HOT_BYTES
    assert len(cold) == OBJECT_TYPE_COLD_BYTES
    return hot, cold


def build_object_type_banks(types: bytes) -> tuple[bytes, bytes]:
    """Return (bank46, bank47) contents from a 16384-byte objects.cobj."""
    expected = 256 * OBJECT_TYPE_FILE_BYTES
    if len(types) != expected:
        raise ValueError(f"expected {expected} bytes of object types, got {len(types)}")
    hot_blob = bytearray()
    cold_blob = bytearray()
    for i in range(256):
        record = types[i * OBJECT_TYPE_FILE_BYTES : (i + 1) * OBJECT_TYPE_FILE_BYTES]
        hot, cold = split_object_type(record)
        hot_blob += hot
        cold_blob += cold
    bank46 = bytes(hot_blob[:OBJECT_TYPE_ZONE_AB_BYTES])
    bank47 = bytes(hot_blob[OBJECT_TYPE_ZONE_AB_BYTES:])
    return bank46, bank47, bytes(cold_blob)


def load_room(asset_dir: Path, room_id: int) -> bytes:
    path = asset_dir / f"{room_id:02X}"
    if not path.exists():
        return bytes([0xFF]) * ROOM_BYTES
    data = path.read_bytes()
    if len(data) != ROOM_BYTES:
        raise ValueError(f"{path}: expected {ROOM_BYTES} bytes, got {len(data)}")
    if data[:4] != bytes((20, 11, room_id, 3)):
        raise ValueError(f"{path}: invalid room header {data[:4].hex()}")
    return data


def load_portrait(asset_dir: Path, portrait_id: int) -> bytes:
    path = asset_dir / f"P{portrait_id:02X}"
    if not path.exists():
        return bytes([0xFF]) * PORTRAIT_BYTES
    data = path.read_bytes()
    if len(data) != PORTRAIT_BYTES:
        raise ValueError(f"{path}: expected {PORTRAIT_BYTES} bytes, got {len(data)}")
    return data


def load_room_code(code_dir: Path, room_id: int) -> bytes | None:
    path = code_dir / f"C{room_id:02X}"
    if not path.exists():
        return None
    data = path.read_bytes()
    if not ROOM_CODE_HEADER_BYTES <= len(data) <= ROOM_CODE_MAX_BYTES:
        raise ValueError(f"{path}: invalid room-code size {len(data)}")
    if (data[0] != 0x4C or data[3] != 0x4C or data[6] != 0x4C or
            data[21] != 0x4C or
            data[9:13] != bytes((0x52, 0x43, ROOM_CODE_ABI, room_id))):
        raise ValueError(f"{path}: invalid room-code header")
    if int.from_bytes(data[13:15], "little") != len(data):
        raise ValueError(f"{path}: header size does not match file")
    return data


def pack_room_code(image: bytearray, code_dir: Path, asset_dir: Path) -> None:
    directory = bytearray([0xFF]) * ROOM_CODE_DIRECTORY_BYTES
    bank = ROOM_CODE_FIRST_BANK
    offset = ROOM_CODE_DIRECTORY_BYTES

    for room_id in range(256):
        code = load_room_code(code_dir, room_id)
        has_room = (asset_dir / f"{room_id:02X}").exists()
        if has_room and code is None:
            raise ValueError(f"room {room_id:02X} has data but no C{room_id:02X} overlay")
        if code is None:
            continue
        if offset + len(code) > ROML_BYTES:
            bank += 1
            offset = 0
        if bank > ROOM_CODE_LAST_BANK:
            raise ValueError("room code exceeds available EasyFlash ROMH banks")

        checksum = int.from_bytes(code[19:21], "little")
        entry = bytes((bank, 1, offset & 0xFF, offset >> 8,
                       len(code) & 0xFF, len(code) >> 8,
                       checksum & 0xFF, checksum >> 8))
        start = room_id * 8
        directory[start:start + 8] = entry
        destination = bank * BANK_BYTES + ROML_BYTES + offset
        image[destination:destination + len(code)] = code
        offset += len(code)

    start = ROOM_CODE_DIRECTORY_BANK * BANK_BYTES + ROML_BYTES
    image[start:start + ROOM_CODE_DIRECTORY_BYTES] = directory


def load_resource(asset_dir: Path, kind: int, resource_id: int) -> bytes | None:
    path = asset_dir / f"{RESOURCE_KIND_PREFIX[kind]}{resource_id:02X}"
    if not path.exists():
        return None
    data = path.read_bytes()
    if not 1 <= len(data) <= RESOURCE_HALF_BYTES:
        raise ValueError(f"{path}: invalid resource size {len(data)}")
    return data


def pack_resources(image: bytearray, asset_dir: Path) -> None:
    """Pack sparse, variable-size resources into banks RESOURCE_FIRST_BANK-
    RESOURCE_LAST_BANK behind one 256-entry directory per RESOURCE_KIND_*, in
    RESOURCE_KIND_ORDER, at the head of RESOURCE_DIRECTORY_BANK. The first
    RESOURCE_DIRECTORIES_PER_HALF of them exactly fill its ROML half; any
    beyond that continue at the head of its ROMH half.

    Each 8-byte directory entry is (bank, mode, offset lo/hi, length lo/hi,
    checksum lo/hi); mode 0 selects the bank's ROML half, mode 1 its ROMH
    half. Within a kind, entries are packed first-fit in ID order; all kinds
    share one running packing cursor, in RESOURCE_KIND_ORDER, so they pack into
    the same payload pool back to back rather than each getting its own
    reserved space. A resource never crosses an 8 KiB half, so it never spans
    an EasyFlash bank switch.
    """
    directories = {
        kind: bytearray([0xFF]) * RESOURCE_DIRECTORY_BYTES
        for kind in RESOURCE_KIND_PREFIX
    }
    # The ROML half is entirely directory, so payload starts in the ROMH half,
    # after whatever directories spilled into it.
    bank = RESOURCE_DIRECTORY_BANK
    mode = 1
    offset = RESOURCE_ROMH_DIRECTORY_BYTES

    for kind in RESOURCE_KIND_ORDER:
        directory = directories[kind]
        for resource_id in range(256):
            data = load_resource(asset_dir, kind, resource_id)
            if data is None:
                continue
            if offset + len(data) > RESOURCE_HALF_BYTES:
                mode += 1
                offset = 0
                if mode > 1:
                    mode = 0
                    bank += 1
            if bank > RESOURCE_LAST_BANK:
                raise ValueError(
                    "generic resources exceed reserved EasyFlash banks "
                    f"{RESOURCE_FIRST_BANK}-{RESOURCE_LAST_BANK}")

            checksum = sum(data) & 0xFFFF
            entry = bytes((bank, mode, offset & 0xFF, offset >> 8,
                           len(data) & 0xFF, len(data) >> 8,
                           checksum & 0xFF, checksum >> 8))
            start = resource_id * RESOURCE_DIRECTORY_ENTRY_BYTES
            directory[start:start + RESOURCE_DIRECTORY_ENTRY_BYTES] = entry
            destination = bank * BANK_BYTES + mode * RESOURCE_HALF_BYTES + offset
            image[destination:destination + len(data)] = data
            offset += len(data)

    # Kinds 0-3 fill the ROML half; kinds 4+ start again at the head of ROMH.
    for index, kind in enumerate(RESOURCE_KIND_ORDER):
        if index < RESOURCE_DIRECTORIES_PER_HALF:
            start = (RESOURCE_DIRECTORY_BANK * BANK_BYTES +
                     index * RESOURCE_DIRECTORY_BYTES)
        else:
            start = (RESOURCE_DIRECTORY_BANK * BANK_BYTES + ROML_BYTES +
                     (index - RESOURCE_DIRECTORIES_PER_HALF) *
                     RESOURCE_DIRECTORY_BYTES)
        image[start:start + RESOURCE_DIRECTORY_BYTES] = directories[kind]


def load_overlay(path: Path, magic: bytes) -> bytes:
    """Load and validate an independently linked $B000-window overlay
    (look/script/save-load helpers); see tools/finalize_inventory_overlay.py,
    which patches the size/BSS/checksum fields this function checks."""
    raw = path.read_bytes()
    if len(raw) < 2 or int.from_bytes(raw[:2], "little") != LOADED_OVERLAY_ADDRESS:
        raise ValueError(f"{path}: invalid overlay load address")
    data = raw[2:]
    if not LOADED_OVERLAY_HEADER_BYTES <= len(data) <= LOADED_OVERLAY_MAX_BYTES:
        raise ValueError(f"{path}: invalid overlay size {len(data)}")
    if (data[0] != 0x4C or
            data[3:6] != magic + bytes((LOADED_OVERLAY_ABI,)) or
            int.from_bytes(data[6:8], "little") != len(data)):
        raise ValueError(f"{path}: invalid overlay header")
    checksum = sum(data[LOADED_OVERLAY_HEADER_BYTES:]) & 0xFFFF
    if checksum != int.from_bytes(data[12:14], "little"):
        raise ValueError(f"{path}: invalid overlay checksum")
    return data


def load_fixed_module(path: Path, placement: ModulePlacement) -> bytes:
    """Read a module in the representation declared by the shared layout."""
    if placement.kind == "in_place":
        return path.read_bytes()
    if placement.magic is None:
        raise ValueError(f"{placement.name}: overlay has no magic")
    return load_overlay(path, placement.magic)


def build_image(base: bytes, asset_dir: Path, object_types: Path,
                code_dir: Path, inventory: Path, saveload: Path,
                saveload_save: Path, room_helpers: Path, look_helpers: Path,
                script: Path, typeinfo: Path,
                layout: dict[str, ModulePlacement]) -> bytes:
    if len(base) != 3 * BANK_BYTES:
        raise ValueError(f"bootstrap image must be 49152 bytes, got {len(base)}")
    types = object_types.read_bytes()
    if len(types) != 0x4000:
        raise ValueError(f"{object_types}: expected 16384 bytes, got {len(types)}")

    image = bytearray([0xFF]) * (OUTPUT_BANKS * BANK_BYTES)
    image[: len(base)] = base
    for room_id in range(256):
        bank = FIRST_ROOM_BANK + room_id // ROOMS_PER_BANK
        offset = (room_id % ROOMS_PER_BANK) * ROOM_BYTES
        start = bank * BANK_BYTES + offset
        image[start : start + ROOM_BYTES] = load_room(asset_dir, room_id)

    bank46, bank47, cold_blob = build_object_type_banks(types)
    start = TYPE_BANK_0 * BANK_BYTES
    image[start : start + len(bank46)] = bank46
    # Cold records get TYPE_BANK_0's otherwise-unused ROMH half to themselves.
    # They used to trail the Zone C hot table in TYPE_BANK_1's ROML half, which
    # the room-helpers/script/look-helpers modules are packed into afterwards -
    # so those modules silently overwrote them (see OBJECT_TYPE_COLD_BASE in
    # src/platform.c).
    start = TYPE_BANK_0 * BANK_BYTES + ROML_BYTES
    if len(cold_blob) > ROML_BYTES:
        raise ValueError("object-type cold table exceeds its ROMH half")
    image[start : start + len(cold_blob)] = cold_blob
    start = TYPE_BANK_1 * BANK_BYTES
    image[start : start + len(bank47)] = bank47
    pack_room_code(image, code_dir, asset_dir)
    inventory_place = layout["inventory"]
    inventory_data = load_fixed_module(inventory, inventory_place)
    if inventory_place.offset + len(inventory_data) > ROML_BYTES:
        raise ValueError("inventory module exceeds its declared cartridge half")
    start = inventory_place.image_offset
    image[start:start + len(inventory_data)] = inventory_data
    saveload_place = layout["saveload"]
    saveload_data = load_fixed_module(saveload, saveload_place)
    start = saveload_place.image_offset
    image[start:start + len(saveload_data)] = saveload_data
    saveload_save_place = layout["saveload_save"]
    saveload_save_data = load_fixed_module(saveload_save, saveload_save_place)
    start = saveload_save_place.image_offset
    image[start:start + len(saveload_save_data)] = saveload_save_data
    room_helpers_place = layout["room_helpers"]
    room_helpers_data = load_fixed_module(room_helpers, room_helpers_place)
    if (room_helpers_place.bank == TYPE_BANK_1 and
            room_helpers_place.half == "roml" and
            room_helpers_place.offset < OBJECT_TYPE_ZONE_C_BYTES):
        raise ValueError(
            "room_helpers overlaps the object-type Zone C table")
    if room_helpers_place.offset + len(room_helpers_data) > ROML_BYTES:
        raise ValueError("room-helpers module exceeds its ROML half")
    start = room_helpers_place.image_offset
    image[start:start + len(room_helpers_data)] = room_helpers_data
    script_place = layout["script"]
    script_data = load_fixed_module(script, script_place)
    if (script_place.bank == room_helpers_place.bank and
            script_place.half == room_helpers_place.half and
            script_place.offset < room_helpers_place.offset + len(room_helpers_data)):
        raise ValueError("SCRIPT_OFFSET overlaps the room-helpers module")
    if script_place.offset + len(script_data) > ROML_BYTES:
        raise ValueError("script overlay exceeds its ROML half")
    start = script_place.image_offset
    image[start:start + len(script_data)] = script_data
    look_helpers_place = layout["look_helpers"]
    look_helpers_data = load_fixed_module(look_helpers, look_helpers_place)
    if (look_helpers_place.bank == script_place.bank and
            look_helpers_place.half == script_place.half and
            look_helpers_place.offset < script_place.offset + len(script_data)):
        raise ValueError("LOOK_HELPERS_OFFSET overlaps the script overlay")
    if look_helpers_place.offset + len(look_helpers_data) > ROML_BYTES:
        raise ValueError("look-helpers overlay exceeds its ROML half")
    start = look_helpers_place.image_offset
    image[start:start + len(look_helpers_data)] = look_helpers_data
    typeinfo_place = layout["typeinfo"]
    typeinfo_data = load_fixed_module(typeinfo, typeinfo_place)
    if (typeinfo_place.bank == look_helpers_place.bank and
            typeinfo_place.half == look_helpers_place.half and
            typeinfo_place.offset < look_helpers_place.offset + len(look_helpers_data)):
        raise ValueError("TYPEINFO_OFFSET overlaps the look-helpers overlay")
    if typeinfo_place.offset + len(typeinfo_data) > ROML_BYTES:
        raise ValueError("banked typeinfo module exceeds its ROML half")
    start = typeinfo_place.image_offset
    image[start:start + len(typeinfo_data)] = typeinfo_data
    for portrait_id in range(256):
        bank = FIRST_PORTRAIT_BANK + portrait_id // PORTRAITS_PER_BANK
        offset = (portrait_id % PORTRAITS_PER_BANK) * PORTRAIT_BYTES
        start = bank * BANK_BYTES + offset
        image[start : start + PORTRAIT_BYTES] = load_portrait(asset_dir, portrait_id)
    pack_resources(image, asset_dir)
    return bytes(image)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--objects", type=Path, required=True)
    parser.add_argument("--room-code", type=Path, required=True)
    parser.add_argument("--inventory", type=Path, required=True)
    parser.add_argument("--saveload", type=Path, required=True)
    parser.add_argument("--saveload-save", type=Path, required=True)
    parser.add_argument("--room-helpers", type=Path, required=True)
    parser.add_argument("--look-helpers", type=Path, required=True)
    parser.add_argument("--script", type=Path, required=True)
    parser.add_argument("--typeinfo", type=Path, required=True)
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.write_bytes(
        build_image(args.base.read_bytes(), args.assets, args.objects,
                    args.room_code, args.inventory, args.saveload,
                    args.saveload_save, args.room_helpers, args.look_helpers,
                    args.script, args.typeinfo, load_layout(args.layout))
    )


if __name__ == "__main__":
    main()
