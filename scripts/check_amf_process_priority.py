#!/usr/bin/env python3
"""Bounded source regression for the shared AMF process-priority service."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
GAMECLIENT = (ROOT / "src/game/client/gameclient.cpp").read_text(encoding="utf-8")
HEADER = (ROOT / "src/game/client/gameclient.h").read_text(encoding="utf-8")
PROTOCOL = (ROOT / "other/helpers/amf_priority_restore_protocol.h").read_text(encoding="utf-8")
HELPER = (ROOT / "other/helpers/amf_priority_restore.cpp").read_text(encoding="utf-8")
CMAKE = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
UPDATER = (ROOT / "src/tools/amfclient_updater.cpp").read_text(encoding="utf-8")
CONFIG = (ROOT / "src/engine/shared/config_variables_amf.h").read_text(encoding="utf-8")
MENU = (ROOT / "src/game/client/components/tclient/menus_tclient.cpp").read_text(encoding="utf-8")
RU = (ROOT / "data/tclient/languages/russian.txt").read_text(encoding="utf-8")
UA = (ROOT / "data/tclient/languages/ukrainian.txt").read_text(encoding="utf-8")
GLOBAL_RU = (ROOT / "data/languages/russian.txt").read_text(encoding="utf-8")
GLOBAL_UA = (ROOT / "data/languages/ukrainian.txt").read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
	start = source.index(signature)
	opening = source.index("{", start)
	depth = 0
	for index in range(opening, len(source)):
		if source[index] == "{":
			depth += 1
		elif source[index] == "}":
			depth -= 1
			if depth == 0:
				return source[opening + 1 : index]
	raise AssertionError(f"Unclosed function: {signature}")


def require(condition: bool, message: str) -> None:
	if not condition:
		raise AssertionError(message)


on_update = function_body(GAMECLIENT, "void CGameClient::OnUpdate()")
update_targets = function_body(GAMECLIENT, "void CGameClient::UpdateAmfTrackedProcessPriorities(")
worker = function_body(GAMECLIENT, "void CGameClient::AmfProcessPriorityWorker()")
stop = function_body(GAMECLIENT, "void CGameClient::StopAmfProcessPriorityWorker()")

require("std::thread m_AmfPriorityWorkerThread" in HEADER, "a single owned priority worker thread must exist")
require(HEADER.count("std::thread m_AmfPriorityWorkerThread") == 1, "priority worker ownership must remain singular")
require("std::condition_variable m_AmfPriorityWorkerWake" in HEADER, "worker must sleep on a condition variable")
require("!m_AmfPriorityWorkerThread.joinable()" in GAMECLIENT, "re-enable must not create a duplicate worker")
require("m_AmfPriorityWorkerWake.wait" in worker and "wait_for" in worker, "worker must block/back off instead of busy-polling")
require("CreateToolhelp32Snapshot" not in on_update, "process discovery must not run on the game update thread")
require("EnsureAmfPriorityRestoreHelper" not in on_update, "helper launch/handshake must not run on the game update thread")
require("UpdateAmfTrackedProcessPriorities(ActiveTargets" in worker, "only the worker should reconcile requested process targets")
require("30 * time_freq()" in update_targets and "5 * 60" in update_targets, "process checks must retain bounded retry/reconciliation intervals")
require("Targets != 0 &&" in update_targets, "disabled features must not launch process discovery")
require("ShouldApplyPriority(PreviousPriority, RequestedPriority)" in update_targets, "steady-state priority application must be idempotent")
require("TARGET_DISCORD" in PROTOCOL and "TARGET_SPOTIFY" in PROTOCOL and "TARGET_SOUNDCLOUD" in PROTOCOL, "one target mask must support all three process groups")
require('L"Discord.exe"' in GAMECLIENT and 'L"Spotify.exe"' in GAMECLIENT and 'L"soundcouch.exe"' in GAMECLIENT, "targets must use exact executable names")
require('L"SoundcloudLtd.SoundCloud-MusicAudio_2xc63xn306dnw"' in GAMECLIENT, "SoundCloud matching must validate its package family")
require('"amfclient-priority-restore.exe"' in GAMECLIENT, "runtime must launch only the common restore helper")
require("IsLegacyDiscordPriorityRestoreHelperRunning()" in update_targets, "new priority sessions must wait for an active legacy helper to exit")
require('L"amfclient-discord-priority-restore.exe"' in GAMECLIENT, "legacy helper compatibility must match the exact old executable")
require(CMAKE.count("add_executable(amfclient-priority-restore") == 1, "exactly one common restore helper target must be packaged")
require("amfclient-discord-priority-restore" not in CMAKE and "amfclient-spotify-priority-restore" not in CMAKE and "amfclient-soundcloud-priority-restore" not in CMAKE, "no legacy or per-app helper may enter fresh packages")
require('L"amfclient-discord-priority-restore.exe"' in UPDATER, "updater must remove the obsolete Discord-only helper")
require("HasExpectedCreationTime" in HELPER and "IsExpectedAmfPriorityProcess" in HELPER, "crash restore must validate process identity and creation time")
require("WaitForMultipleObjects" in HELPER and "RestoreTrackedProcesses" in HELPER, "common helper must wait for its owner and restore on unexpected exit")
require("RequestedTargets(bool DiscordEnabled, bool SpotifySoundCloudEnabled)" in PROTOCOL, "feature configuration must remain independent")
require("AmfSpotifySoundCloudLowPriority, amf_spotify_soundcloud_low_priority, 0" in CONFIG, "new optional feature must default off and persist")
require(MENU.index('TCLocalize("Low Priority Discord"') < MENU.index('TCLocalize("Low Priority Spotify | SoundCloud"'), "new setting must immediately follow Discord in the System card")
require('TCLocalize("Low Priority Spotify | SoundCloud", "AMF Client")' in MENU, "setting label must use AMF localization")
require("== Низкий приоритет Spotify | SoundCloud" in RU and "== Низкий приоритет Spotify | SoundCloud" in GLOBAL_RU, "Russian label translation is missing")
require("== Низький пріоритет Spotify | SoundCloud" in UA and "== Низький пріоритет Spotify | SoundCloud" in GLOBAL_UA, "Ukrainian label translation is missing")
old_user_label = "Spotify" + chr(47) + "SoundCloud"
require(all(old_user_label not in source for source in (MENU, RU, UA, GLOBAL_RU, GLOBAL_UA)), "old slash-separated label remains in UI/localization")
require("RestoreAmfProcessPriorities(true);" in worker and "ReleaseAmfPriorityProcessHandles(true);" in worker, "worker shutdown must restore before releasing target handles")
require("Worker.join();" in stop, "client shutdown must join the priority worker")

print("AMF process-priority source regression: PASS")
