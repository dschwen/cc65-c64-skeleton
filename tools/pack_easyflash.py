#!/usr/bin/env python3
"""Append fixed-layout runtime room and object assets to an EasyFlash image."""

from __future__ import annotations

import argparse
from pathlib import Path

BANK_BYTES = 0x4000
ROML_BYTES = 0x2000
ROOM_BYTES = 1257
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
OBJECT_TYPE_ZONE_A_COUNT = 117
OBJECT_TYPE_ZONE_B_COUNT = 117
OBJECT_TYPE_ZONE_AB_COUNT = OBJECT_TYPE_ZONE_A_COUNT + OBJECT_TYPE_ZONE_B_COUNT
OBJECT_TYPE_ZONE_C_COUNT = 256 - OBJECT_TYPE_ZONE_AB_COUNT
OBJECT_TYPE_ZONE_AB_BYTES = OBJECT_TYPE_ZONE_AB_COUNT * OBJECT_TYPE_HOT_BYTES
OBJECT_TYPE_ZONE_C_BYTES = OBJECT_TYPE_ZONE_C_COUNT * OBJECT_TYPE_HOT_BYTES
OBJECT_TYPE_COLD_BASE = OBJECT_TYPE_ZONE_C_BYTES
INVENTORY_BANK = 48
PORTRAIT_BYTES = 256
PORTRAITS_PER_BANK = 32
FIRST_PORTRAIT_BANK = 49
PORTRAIT_BANKS = 8
# Generic sparse resource directory: 256 read-only, variable-size blobs for
# future variable-size/sparse content (see platform_resource_fetch() in
# src/platform.c). Reserves every remaining EasyFlash bank up to the 64-bank
# hardware limit, so no banks remain free after this pool.
RESOURCE_DIRECTORY_BANK = FIRST_PORTRAIT_BANK + PORTRAIT_BANKS
RESOURCE_FIRST_BANK = RESOURCE_DIRECTORY_BANK
RESOURCE_LAST_BANK = 63
RESOURCE_HALF_BYTES = ROML_BYTES
RESOURCE_DIRECTORY_ENTRY_BYTES = 8
RESOURCE_DIRECTORY_BYTES = 256 * RESOURCE_DIRECTORY_ENTRY_BYTES
OUTPUT_BANKS = RESOURCE_LAST_BANK + 1
ROOM_CODE_HEADER_BYTES = 24
ROOM_CODE_MAX_BYTES = 0x0400
ROOM_CODE_ABI = 4
ROOM_CODE_DIRECTORY_BANK = 3
ROOM_CODE_DIRECTORY_BYTES = 0x800
ROOM_CODE_FIRST_BANK = 3
ROOM_CODE_LAST_BANK = TYPE_BANK_1
INVENTORY_LOAD_ADDRESS = 0xA4E9
INVENTORY_HEADER_BYTES = 16
INVENTORY_MAX_BYTES = 0x0FF0
INVENTORY_ABI = 1


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
    bank47 = bytes(hot_blob[OBJECT_TYPE_ZONE_AB_BYTES:]) + bytes(cold_blob)
    return bank46, bank47


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


def load_resource(asset_dir: Path, resource_id: int) -> bytes | None:
    path = asset_dir / f"R{resource_id:02X}"
    if not path.exists():
        return None
    data = path.read_bytes()
    if not 1 <= len(data) <= RESOURCE_HALF_BYTES:
        raise ValueError(f"{path}: invalid resource size {len(data)}")
    return data


def pack_resources(image: bytearray, asset_dir: Path) -> None:
    """Pack sparse, variable-size resources into banks RESOURCE_FIRST_BANK-
    RESOURCE_LAST_BANK behind a 256-entry directory at the head of
    RESOURCE_DIRECTORY_BANK's ROML half.

    Each 8-byte directory entry is (bank, mode, offset lo/hi, length lo/hi,
    checksum lo/hi); mode 0 selects the bank's ROML half, mode 1 its ROMH
    half. Entries are packed first-fit in ID order and a resource never
    crosses an 8 KiB half, so it never spans an EasyFlash bank switch.
    """
    directory = bytearray([0xFF]) * RESOURCE_DIRECTORY_BYTES
    bank = RESOURCE_DIRECTORY_BANK
    mode = 0
    offset = RESOURCE_DIRECTORY_BYTES

    for resource_id in range(256):
        data = load_resource(asset_dir, resource_id)
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

    start = RESOURCE_DIRECTORY_BANK * BANK_BYTES
    image[start:start + RESOURCE_DIRECTORY_BYTES] = directory


def load_inventory(path: Path) -> bytes:
    raw = path.read_bytes()
    if len(raw) < 2 or int.from_bytes(raw[:2], "little") != INVENTORY_LOAD_ADDRESS:
        raise ValueError(f"{path}: invalid inventory load address")
    data = raw[2:]
    if not INVENTORY_HEADER_BYTES <= len(data) <= INVENTORY_MAX_BYTES:
        raise ValueError(f"{path}: invalid inventory size {len(data)}")
    if (data[0] != 0x4C or
            data[3:6] != bytes((0x49, 0x55, INVENTORY_ABI)) or
            int.from_bytes(data[6:8], "little") != len(data)):
        raise ValueError(f"{path}: invalid inventory header")
    checksum = sum(data[INVENTORY_HEADER_BYTES:]) & 0xFFFF
    if checksum != int.from_bytes(data[12:14], "little"):
        raise ValueError(f"{path}: invalid inventory checksum")
    return data


def build_image(base: bytes, asset_dir: Path, object_types: Path,
                code_dir: Path, inventory: Path) -> bytes:
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

    bank46, bank47 = build_object_type_banks(types)
    start = TYPE_BANK_0 * BANK_BYTES
    image[start : start + len(bank46)] = bank46
    start = TYPE_BANK_1 * BANK_BYTES
    image[start : start + len(bank47)] = bank47
    pack_room_code(image, code_dir, asset_dir)
    inventory_data = load_inventory(inventory)
    start = INVENTORY_BANK * BANK_BYTES + ROML_BYTES
    image[start:start + len(inventory_data)] = inventory_data
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
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.write_bytes(
        build_image(args.base.read_bytes(), args.assets, args.objects,
                    args.room_code, args.inventory)
    )


if __name__ == "__main__":
    main()
