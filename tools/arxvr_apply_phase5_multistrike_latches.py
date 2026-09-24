#!/usr/bin/env python3
"""Upgrade VrDefenseRuntime from a single defense latch to a bounded keyed latch table.

The live equipment path can evaluate overlapping NPC weapon strikes in the same
300 ms suppression window. A single mutable latch lets a later defended strike
overwrite an earlier one, so a subsequent action point from the first strike can
leak through to normal Arx damage. This generator is intentionally deterministic
and refuses to run unless the expected pre-patch source is present exactly once.
"""

from pathlib import Path

PATH = Path("ArxAndroid/src/vr/VrDefenseRuntime.h")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one anchor, found {count}")
    return text.replace(old, new, 1)


def main() -> None:
    text = PATH.read_text(encoding="utf-8")

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
    new_arm = '''\tvoid armDefenseLatch(std::uint64_t sourceToken,
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
    text = replace_once(text, old_arm, new_arm, "latch allocation")

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
    if text.count("std::array<DefenseLatch, 8> m_latches{};") != 1:
        raise RuntimeError("expected one bounded latch table")

    PATH.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
