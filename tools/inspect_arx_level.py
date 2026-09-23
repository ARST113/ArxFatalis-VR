#!/usr/bin/env python3
"""Inspect Arx Fatalis PAK entries and the level1 DLF entity table.

This intentionally has no third-party runtime dependency other than the vendored
PKWARE implode decoder in third_party/pwexplode-src.
"""

from __future__ import annotations

import argparse
import importlib.util
import pathlib
import struct
import sys
from dataclasses import dataclass


FULL_KEY = (
    b"AVQF3FCKE50GRIAYXJP2AMEYO5QGA0JGIIH2NHBTV"
    b"OA1VOGGU5H3GSSIARKPRQPQKKYEOIAQG1XRX0J4F5OEAEFI4DD3LL45VJTVOA1VOGGUKE50GRIAYX"
)
DEMO_KEY = (
    b"NSIARKPRQPHBTE50GRIH3AYXJP2AMF3FCEYAVQO5Q"
    b"GA0JGIIH2AYXKVOA1VOGGU5GSQKKYEOIAQG1XRX0J4F5OEAEFI4DD3LL45VJTVOA1VOGGUKE50GRI"
)


@dataclass(frozen=True)
class PakEntry:
    pak: pathlib.Path
    path: str
    offset: int
    flags: int
    unpacked_size: int
    stored_size: int


def load_explode(project: pathlib.Path):
    module_path = project / "third_party" / "pwexplode-src" / "pwexplode.py"
    spec = importlib.util.spec_from_file_location("pwexplode", module_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Cannot load {module_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.explode


def cstring(data: bytes, pos: int) -> tuple[str, int]:
    end = data.index(0, pos)
    return data[pos:end].decode("cp1252", errors="replace"), end + 1


def read_pak_index(pak: pathlib.Path) -> dict[str, PakEntry]:
    raw = pak.read_bytes()
    fat_offset = struct.unpack_from("<I", raw, 0)[0]
    fat_size = struct.unpack_from("<I", raw, fat_offset)[0]
    fat = bytearray(raw[fat_offset + 4 : fat_offset + 4 + fat_size])
    if fat[:4] == b"AVQF":
        key = FULL_KEY
    elif fat[:4] == b"NSIA":
        key = DEMO_KEY
    else:
        key = None
    if key is not None:
        for i in range(len(fat)):
            fat[i] ^= key[i % len(key)]

    entries: dict[str, PakEntry] = {}
    pos = 0
    while pos < len(fat):
        dirname, pos = cstring(fat, pos)
        (count,) = struct.unpack_from("<I", fat, pos)
        pos += 4
        for _ in range(count):
            filename, pos = cstring(fat, pos)
            offset, flags, unpacked_size, stored_size = struct.unpack_from("<IIII", fat, pos)
            pos += 16
            full = (dirname.rstrip("\\/") + "\\" + filename).lstrip("\\/").lower()
            entries[full] = PakEntry(pak, full, offset, flags, unpacked_size, stored_size)
    return entries


def read_entry(entry: PakEntry, explode) -> bytes:
    with entry.pak.open("rb") as source:
        source.seek(entry.offset)
        payload = source.read(entry.stored_size)
    if entry.flags & 1 and entry.stored_size:
        payload = bytes(explode(payload))
    if entry.unpacked_size and len(payload) != entry.unpacked_size:
        raise RuntimeError(
            f"Wrong size for {entry.path}: got {len(payload)}, expected {entry.unpacked_size}"
        )
    return payload


# All records are packed to one byte in the engine.
DLF_HEADER_SIZE = (
    4 + 16 + 256 + 4 + 12 + 12 + 6 * 4 + 256 * 4 + 2 * 4
    + 4 * 4 + 250 * 4 + 12 + 253 * 4 + 4096 + 256 * 4
)
DLF_SCENE_SIZE = 512 + 16 * 4 + 16 * 4
DLF_INTER_SIZE = 512 + 12 + 12 + 4 + 4 + 14 * 4 + 16 * 4


def ztext(data: bytes) -> str:
    return data.split(b"\0", 1)[0].decode("cp1252", errors="replace")


def inspect_level(payload: bytes, explode, contains: list[str], show_paths: bool,
                  show_path_points: bool) -> None:
    if len(payload) < DLF_HEADER_SIZE:
        raise RuntimeError("DLF is smaller than its header")
    version = struct.unpack_from("<f", payload, 0)[0]
    ident = ztext(payload[4:20])
    head = 4 + 16 + 256 + 4 + 12 + 12
    nb_scn, nb_inter, nb_nodes, nb_node_links, nb_zones, lighting = struct.unpack_from(
        "<6i", payload, head
    )
    header2 = head + 6 * 4 + 256 * 4
    nb_lights, nb_fogs = struct.unpack_from("<2i", payload, header2)
    header3 = header2 + 2 * 4
    _nb_bkg, _nb_ignored, _nb_child, nb_paths = struct.unpack_from("<4i", payload, header3)
    start_pos = 4 + 16 + 256 + 4
    player_pos = struct.unpack_from("<3f", payload, start_pos)
    player_angle = struct.unpack_from("<3f", payload, start_pos + 12)
    print(
        f"DLF version={version:.3f} ident={ident!r} header={DLF_HEADER_SIZE} "
        f"scenes={nb_scn} entities={nb_inter} nodes={nb_nodes}/{nb_node_links} "
        f"zones={nb_zones} paths={nb_paths} lighting={lighting} lights={nb_lights} fogs={nb_fogs}"
    )
    print(f"PLAYER pos={player_pos} angle={player_angle}")

    body = payload[DLF_HEADER_SIZE:]
    if version >= 1.44:
        body = bytes(explode(body))
        print(f"BODY unpacked={len(body)}")
    pos = nb_scn * DLF_SCENE_SIZE
    needles = [x.casefold() for x in contains]
    for index in range(nb_inter):
        record = body[pos : pos + DLF_INTER_SIZE]
        if len(record) != DLF_INTER_SIZE:
            raise RuntimeError(f"Truncated entity #{index} at {pos}")
        name = ztext(record[:512])
        xyz = struct.unpack_from("<3f", record, 512)
        angle = struct.unpack_from("<3f", record, 524)
        ident_num, flags = struct.unpack_from("<2i", record, 536)
        if not needles or any(needle in name.casefold() for needle in needles):
            print(
                f"ENTITY {index:03d} id={ident_num:04d} flags=0x{flags:08x} "
                f"pos=({xyz[0]:.2f},{xyz[1]:.2f},{xyz[2]:.2f}) "
                f"angle=({angle[0]:.1f},{angle[1]:.1f},{angle[2]:.1f}) name={name}"
            )
        pos += DLF_INTER_SIZE

    if show_paths:
        # level1 has no embedded lighting block or static lights. Keep this
        # diagnostic deliberately strict so a format variation cannot silently
        # produce invented path coordinates.
        if lighting or nb_lights or nb_nodes:
            raise RuntimeError("Path parsing currently expects no lighting/lights/nodes")
        fog_size = 592
        path_size = 608
        pathway_size = 68
        pos += nb_fogs * fog_size
        for index in range(nb_paths):
            record = body[pos : pos + path_size]
            if len(record) != path_size:
                raise RuntimeError(f"Truncated path #{index} at {pos}")
            name = ztext(record[:64])
            idx, flags = struct.unpack_from("<hh", record, 64)
            init_pos = struct.unpack_from("<3f", record, 68)
            path_pos = struct.unpack_from("<3f", record, 80)
            (waypoint_count,) = struct.unpack_from("<i", record, 92)
            (height,) = struct.unpack_from("<i", record, 224)
            print(
                f"PATH {index:03d} idx={idx} flags=0x{flags & 0xffff:04x} height={height} "
                f"pos=({path_pos[0]:.2f},{path_pos[1]:.2f},{path_pos[2]:.2f}) "
                f"init=({init_pos[0]:.2f},{init_pos[1]:.2f},{init_pos[2]:.2f}) "
                f"points={waypoint_count} name={name}"
            )
            pos += path_size
            if show_path_points:
                for point_index in range(waypoint_count):
                    relative = struct.unpack_from("<3f", body, pos + point_index * pathway_size)
                    absolute = tuple(path_pos[i] + relative[i] for i in range(3))
                    print(
                        f"  POINT {point_index:02d} rel=({relative[0]:.2f},{relative[1]:.2f},{relative[2]:.2f}) "
                        f"abs=({absolute[0]:.2f},{absolute[1]:.2f},{absolute[2]:.2f})"
                    )
            pos += waypoint_count * pathway_size


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--game", type=pathlib.Path, default=pathlib.Path(r"D:\SteamLibrary\steamapps\common\Arx Fatalis"))
    parser.add_argument("--project", type=pathlib.Path, default=pathlib.Path(r"F:\CODEX\ArxFatalis VR"))
    parser.add_argument("--contains", action="append", default=[])
    parser.add_argument("--entry", action="append", default=[])
    parser.add_argument("--script-search", action="append", default=[])
    parser.add_argument("--paths", action="store_true")
    parser.add_argument("--path-points", action="store_true")
    args = parser.parse_args()

    explode = load_explode(args.project)
    indices: dict[str, PakEntry] = {}
    for pak in (args.game / "data.pak", args.game / "data2.pak"):
        indices.update(read_pak_index(pak))

    dlf_name = r"graph\levels\level1\level1.dlf"
    inspect_level(read_entry(indices[dlf_name], explode), explode, args.contains,
                  args.paths, args.path_points)

    for query in args.entry:
        matches = [(name, entry) for name, entry in indices.items() if query.casefold() in name.casefold()]
        print(f"\nENTRY QUERY {query!r}: {len(matches)} matches")
        for name, entry in matches:
            print(
                f"--- {name} flags={entry.flags} stored={entry.stored_size} unpacked={entry.unpacked_size}"
            )
            data = read_entry(entry, explode)
            if name.endswith((".asl", ".ini", ".txt")):
                print(data.decode("cp1252", errors="replace"))

    for query in args.script_search:
        needle = query.casefold()
        print(f"\nSCRIPT SEARCH {query!r}")
        for name, entry in indices.items():
            if not name.endswith(".asl"):
                continue
            text = read_entry(entry, explode).decode("cp1252", errors="replace")
            matching_lines = [line.strip() for line in text.splitlines() if needle in line.casefold()]
            if matching_lines:
                print(f"--- {name}")
                for line in matching_lines:
                    print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
