#!/usr/bin/env python3
"""Source guards for AMF hot paths with bounded work and no render-thread config hashing."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GAMECLIENT = (ROOT / "src/game/client/gameclient.cpp").read_text(encoding="utf-8")
GAMECLIENT_H = (ROOT / "src/game/client/gameclient.h").read_text(encoding="utf-8")
MUSIC = (ROOT / "src/game/client/components/tclient/music_player/music_player.cpp").read_text(encoding="utf-8")
PRESENCE = (ROOT / "src/game/client/components/tclient/amf_presence_transport.cpp").read_text(encoding="utf-8")


def between(source: str, start: str, end: str) -> str:
    return source[source.index(start):source.index(end, source.index(start))]


on_update = between(GAMECLIENT, "void CGameClient::OnUpdate()", "void CGameClient::OnPredict()")
notify = between(GAMECLIENT, "void CGameClient::NotifyAmfConfigChanged()", "void CGameClient::ApplyAmfProcessPriority()")
persist = between(GAMECLIENT, "void CGameClient::UpdateAmfConfigPersistence()", "void CGameClient::NotifyAmfConfigChanged()")
assert "UpdateAmfConfigPersistence();" in on_update
assert "ConfigManager()->DomainHash" not in notify, "AMF pages call Notify from rendering; the full domain hash must not run there"
assert "m_AmfConfigHashCheckPending = true;" in notify
assert "m_AmfConfigNextHashCheckTime" in persist and "Now >= m_AmfConfigNextHashCheckTime" in persist
assert "m_AmfConfigNextHashCheckTime = Now + DebounceTicks;" in persist
assert "bool m_AmfConfigHashCheckPending = false;" in GAMECLIENT_H

# Metadata polling is disabled with its feature. The Windows worker waits for
# work, retries a missing GSMTC manager exponentially, and shares immutable art
# bytes between its snapshots instead of copying large vectors under the lock.
assert "if(MusicPlayerEnabled && (m_pImpl->m_LastPollTick" in MUSIC
assert "std::shared_ptr<const std::vector<unsigned char>> m_pBytes;" in MUSIC
assert "m_Cv.wait(Lock, [this]()" in MUSIC
assert "ManagerRetryDelay = std::min(ManagerRetryDelay * 2, std::chrono::milliseconds(5000));" in MUSIC

# Network work remains outside the game thread and has explicitly bounded
# cadence/backoff rather than frame-driven requests.
assert "constexpr auto WORKER_POLL_INTERVAL = 100ms;" in PRESENCE
assert "constexpr int64_t BROWSER_SNAPSHOT_INTERVAL_MS = 60000;" in PRESENCE
assert "constexpr int64_t BROWSER_SNAPSHOT_MANUAL_COOLDOWN_MS = 1000;" in PRESENCE
assert "constexpr int64_t PRESENCE_REFRESH_INTERVAL_MS = 90000;" in PRESENCE
assert "constexpr int BACKOFF_MAX_MS = 60000;" in PRESENCE

print("AMF config hashing, Music Player inactive polling, and Presence cadence source checks PASS")
