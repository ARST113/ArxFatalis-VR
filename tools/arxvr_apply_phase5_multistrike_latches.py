#!/usr/bin/env python3
"""Upgrade VrDefenseRuntime to a bounded keyed table for defended NPC strikes.

Overlapping NPC equipment strikes can share the same 300 ms suppression window.
A single mutable latch lets a later defended strike overwrite an earlier one, so
a later action point from the first strike can leak into normal Arx damage. The
generator is deterministic, idempotent, and also migrates the first table version
whose allocator could reuse an expired slot before discovering a matching key.
"""

from pathlib import Path

PATH = Path("ArxAndroid/src/vr/VrDefenseRuntime.h")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one anchor, found {count}")
    return text.replace(old, new, 1)


OLD_TABLE_ARM = '''\tvoid armDefenseLatch(std::uint64_t sourceToken,
\t                     std::uint64_t strikeToken,
\t                     VrDefenseEventType type,
\t                     std::uint64_t timestampUs) {
\t\tif(sourceToken == 0 || strikeToken == 0 || timestampUs == 0
\t\t   || type == VrDefenseEventType::None) {
\t\t\treturn;
\t\t}

\t\tDefenseLatch * target = nullptr;
\t\tDefenseLatch * oldest = &m_latches[0];
\t\tfor(DefenseLatch & latch : m_latches) {
\t\t\tif(latch.active && latch.sourceToken == sourceToken
\t\t\t   && latch.strikeToken == strikeToken) {
\t\t\t\ttarget = &latch;
\t\t\t\tbreak;
\t\t\t}
\t\t\tif(!latch.active) {
\t\t\t\ttarget = &latch;
\t\t\t\tbreak;
\t\t\t}
\t\t\tif(timestampUs >= latch.timestampUs
\t\t\t   && timestampUs - latch.timestampUs > m_config.defenseLatchUs) {
\t\t\t\ttarget = &latch;
\t\t\t\tbreak;
\t\t\t}
\t\t\tif(latch.timestampUs < oldest->timestampUs) {
\t\t\t\toldest = &latch;
\t\t\t}
\t\t}
\t\tif(!target) {
\t\t\ttarget = oldest;
\t\t}

\t\ttarget->sourceToken = sourceToken;
\t\ttarget->strikeToken = strikeToken;
\t\ttarget->type = type;
\t\ttarget->timestampUs = timestampUs;
\t\ttarget->active = true;
\t}
'''

NEW_TABLE_ARM = '''\tvoid armDefenseLatch(std::uint64_t sourceToken,
\t                     std::uint64_t strikeToken,
\t                     VrDefenseEventType type,
\t                     std::uint64_t timestampUs) {
\t\tif(sourceToken == 0 || strikeToken == 0 || timestampUs == 0
\t\t   || type == VrDefenseEventType::None) {
\t\t\treturn;
\t\t}

\t\t// Preserve an existing key before considering reusable slots. This avoids
\t\t// creating duplicate entries when an expired slot appears earlier in the
\t\t// table than the still-active entry for the same NPC weapon strike.
\t\tfor(DefenseLatch & latch : m_latches) {
\t\t\tif(latch.active && latch.sourceToken == sourceToken
\t\t\t   && latch.strikeToken == strikeToken) {
\t\t\t\tstoreDefenseLatch(latch, sourceToken, strikeToken, type, timestampUs);
\t\t\t\treturn;
\t\t\t}
\t\t}

\t\tDefenseLatch * target = nullptr;
\t\tDefenseLatch * oldest = &m_latches[0];
\t\tfor(DefenseLatch & latch : m_latches) {
\t\t\tif(!latch.active) {
\t\t\t\ttarget = &latch;
\t\t\t\tbreak;
\t\t\t}
\t\t\tif(timestampUs >= latch.timestampUs
\t\t\t   && timestampUs - latch.timestampUs > m_config.defenseLatchUs) {
\t\t\t\ttarget = &latch;
\t\t\t\tbreak;
\t\t\t}
\t\t\tif(latch.timestampUs < oldest->timestampUs) {
\t\t\t\toldest = &latch;
\t\t\t}
\t\t}
\t\tstoreDefenseLatch(target ? *target : *oldest,
\t\t                 sourceToken, strikeToken, type, timestampUs);
\t}

\tstatic void storeDefenseLatch(DefenseLatch & latch,
\t                              std::uint64_t sourceToken,
\t                              std::uint64_t strikeToken,
\t                              VrDefenseEventType type,
\t                              std::uint64_t timestampUs) {
\t\tlatch.sourceToken = sourceToken;
\t\tlatch.strikeToken = strikeToken;
\t\tlatch.type = type;
\t\tlatch.timestampUs = timestampUs;
\t\tlatch.active = true;
\t}
'''


def validate_table(text: str) -> None:
    if "DefenseLatch m_latch{};" in text:
        raise RuntimeError("mixed single/table defense latch state")
    if text.count("std::array<DefenseLatch, 8> m_latches{};") != 1:
        raise RuntimeError("expected one bounded latch table")
    if "for(const DefenseLatch & latch : m_latches)" not in text:
        raise RuntimeError("latch table storage exists without table query")
    if "storeDefenseLatch(target ? *target : *oldest" not in text:
        raise RuntimeError("latch table exists without two-pass allocator")


def main() -> None:
    text = PATH.read_text(encoding="utf-8")

    if "std::array<DefenseLatch, 8> m_latches{};" in text:
        if NEW_TABLE_ARM in text:
            validate_table(text)
            print("VrDefenseRuntime multi-strike latch table already applied")
            return
        if OLD_TABLE_ARM in text:
            text = replace_once(text, OLD_TABLE_ARM, NEW_TABLE_ARM, "table allocator migration")
            validate_table(text)
            PATH.write_text(text, encoding="utf-8")
            print("Migrated VrDefenseRuntime latch allocator to two-pass key preservation")
            return
        raise RuntimeError("unrecognised partial multi-strike latch table state")

    old_query = '''\tbool defenseLatched(std::uint64_t sourceToken,
\t                    std::uint64_t strikeToken,
\t                    std::uint64_t timestampUs) const {
\t\tif(!m_latch.active || sourceToken == 0 || strikeToken == 0
\t\t   || timestampUs == 0 || m_latch.sourceToken != sourceToken
\t\t   || m_latch.strikeToken != strikeToken || timestampUs < m_latch.timestampUs) {
\t\t\treturn false;
\t\t}
\t\treturn timestampUs - m_latch.timestampUs <= m_config.defenseLatchUs;
\t}

\tVrDefenseEventType latchedDefenseType() const {
\t\treturn m_latch.active ? m_latch.type : VrDefenseEventType::None;
\t}
'''
    new_query = '''\tbool defenseLatched(std::uint64_t sourceToken,
\t                    std::uint64_t strikeToken,
\t                    std::uint64_t timestampUs) const {
\t\tif(sourceToken == 0 || strikeToken == 0 || timestampUs == 0) {
\t\t\treturn false;
\t\t}
\t\tfor(const DefenseLatch & latch : m_latches) {
\t\t\tif(!latch.active || latch.sourceToken != sourceToken
\t\t\t   || latch.strikeToken != strikeToken || timestampUs < latch.timestampUs) {
\t\t\t\tcontinue;
\t\t\t}
\t\t\tif(timestampUs - latch.timestampUs <= m_config.defenseLatchUs) {
\t\t\t\treturn true;
\t\t\t}
\t\t}
\t\treturn false;
\t}

\tVrDefenseEventType latchedDefenseType() const {
\t\tconst DefenseLatch * latest = nullptr;
\t\tfor(const DefenseLatch & latch : m_latches) {
\t\t\tif(latch.active && (!latest || latch.timestampUs > latest->timestampUs)) {
\t\t\t\tlatest = &latch;
\t\t\t}
\t\t}
\t\treturn latest ? latest->type : VrDefenseEventType::None;
\t}
'''
    text = replace_once(text, old_query, new_query, "latch query")

    old_reset = '''\t\tm_shield = VrPublishedShield{};
\t\tm_defenderWeapon = VrPublishedDefenderWeapon{};
\t\tm_latch = DefenseLatch{};
\t\tfor(IncomingHistory & history : m_histories) {
'''
    new_reset = '''\t\tm_shield = VrPublishedShield{};
\t\tm_defenderWeapon = VrPublishedDefenderWeapon{};
\t\tfor(DefenseLatch & latch : m_latches) {
\t\t\tlatch = DefenseLatch{};
\t\t}
\t\tfor(IncomingHistory & history : m_histories) {
'''
    text = replace_once(text, old_reset, new_reset, "session reset")

    old_arm = '''\tvoid armDefenseLatch(std::uint64_t sourceToken,
\t                     std::uint64_t strikeToken,
\t                     VrDefenseEventType type,
\t                     std::uint64_t timestampUs) {
\t\tm_latch.sourceToken = sourceToken;
\t\tm_latch.strikeToken = strikeToken;
\t\tm_latch.type = type;
\t\tm_latch.timestampUs = timestampUs;
\t\tm_latch.active = type != VrDefenseEventType::None;
\t}
'''
    text = replace_once(text, old_arm, NEW_TABLE_ARM, "latch allocation")

    old_member = '''\tVrPublishedShield m_shield{};
\tVrPublishedDefenderWeapon m_defenderWeapon{};
\tDefenseLatch m_latch{};
\tstd::array<IncomingHistory, 16> m_histories{};
'''
    new_member = '''\tVrPublishedShield m_shield{};
\tVrPublishedDefenderWeapon m_defenderWeapon{};
\t// Multiple NPCs can overlap their equipment strike windows. Keep a small
\t// bounded keyed table so one defended strike cannot overwrite another.
\tstd::array<DefenseLatch, 8> m_latches{};
\tstd::array<IncomingHistory, 16> m_histories{};
'''
    text = replace_once(text, old_member, new_member, "latch storage")

    if "m_latch" in text.replace("m_latches", ""):
        raise RuntimeError("legacy single defense latch reference remains")
    validate_table(text)
    PATH.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
