#!/usr/bin/env python3
"""Resolve AMF priority labels from the build-tree catalogs used at runtime."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MENU = (ROOT / "src/game/client/components/tclient/menus_tclient.cpp").read_text(encoding="utf-8")
CMAKE = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
VISUAL = (ROOT / "src/game/client/components/tclient/amf_features/best_features_visual_ui.cpp").read_text(encoding="utf-8")
CONFIG = (ROOT / "src/engine/shared/config_variables_amf.h").read_text(encoding="utf-8")
KEY = "Low Priority Spotify | SoundCloud"
OLD_KEY = "Spotify" + chr(47) + "SoundCloud"


def parse_runtime_catalog(path: Path) -> dict[tuple[str, str], str]:
	lines = path.read_text(encoding="utf-8-sig").splitlines()
	entries = {}
	context = ""
	index = 0
	while index < len(lines):
		line = lines[index]
		if not line:
			index += 1
			continue
		if line.startswith("[") and line.endswith("]"):
			context = line[1:-1]
			index += 1
			continue
		if index + 1 >= len(lines) or not lines[index + 1].startswith("== "):
			index += 1
			context = ""
			continue
		entries[(line, context)] = lines[index + 1][3:] or line
		index += 2
		context = ""
	return entries


def runtime_lookup(entries: dict[tuple[str, str], str], key: str, context: str) -> str:
	return entries.get((key, context), entries.get((key, ""), key))


assert 'TCLocalize("Low Priority Spotify | SoundCloud", "AMF Client")' in MENU
assert MENU.index('TCLocalize("Low Priority Discord"') < MENU.index(f'TCLocalize("{KEY}"'), "media label must remain immediately after Discord"
assert OLD_KEY not in MENU
assert "CMAKE_CONFIGURE_DEPENDS ${RUNTIME_LANGUAGE_CATALOGS}" in CMAKE, "runtime catalogs must refresh after source translation edits"
assert 'file(COPY ${RUNTIME_LANGUAGE_CATALOGS} DESTINATION "${CONFIG_LANGUAGE_DIR}")' in CMAKE, "per-configuration runtime language trees must be staged"

hook_card_start = VISUAL.index('BeginCard(Left, "Custom Hooks"')
effects_card_start = VISUAL.index('BeginCard(Left, "Custom Effects"')
hook_card = VISUAL[hook_card_start:effects_card_start]
effects_card = VISUAL[effects_card_start:]
assert 'g_Config.m_AmfHookStyle' in hook_card and '"Whirlwind"' in hook_card and '"Black hole"' in hook_card
assert 'g_Config.m_AmfAirJump' in effects_card and '"Air jump ring"' in effects_card
assert 'BeginCard(Left, "Hook Effects"' not in VISUAL
assert 'MACRO_CONFIG_INT(AmfHookStyle, amf_hook_style' in CONFIG
assert 'MACRO_CONFIG_INT(AmfAirJump, amf_air_jump' in CONFIG
assert 'TEST(LocalizationRuntime, AmfLabelsResolveFromStagedRuntimeCatalogs)' in (ROOT / "src/test/localization_runtime_test.cpp").read_text(encoding="utf-8")

EXPECTED = {
	"russian": {
		"priority": "Низкий приоритет Spotify | SoundCloud",
		"discord": "Низкий приоритет Discord",
		"hooks": "Кастомные хуки",
		"effects": "Кастомные эффекты",
	},
	"ukrainian": {
		"priority": "Низький пріоритет Spotify | SoundCloud",
		"discord": "Низький пріоритет Discord",
		"hooks": "Кастомні хуки",
		"effects": "Кастомні ефекти",
	},
}

for language, expected in EXPECTED.items():
	entries = parse_runtime_catalog(ROOT / "data/languages" / f"{language}.txt")
	assert runtime_lookup(entries, KEY, "AMF Client") == expected["priority"], f"source catalog lookup failed for {language}"
	assert runtime_lookup(entries, "Low Priority Discord", "AMF Client") == expected["discord"], f"source Discord lookup failed for {language}"
	assert runtime_lookup(entries, "Custom Hooks", "AMF Client") == expected["hooks"], f"source Custom Hooks lookup failed for {language}"
	assert runtime_lookup(entries, "Custom Effects", "AMF Client") == expected["effects"], f"source Custom Effects lookup failed for {language}"

for configuration in ("Debug", "Release"):
	for language, expected in EXPECTED.items():
		path = ROOT / f"build/{configuration}/data/languages" / f"{language}.txt"
		assert path.is_file(), f"missing actual {configuration} runtime catalog: {path}"
		entries = parse_runtime_catalog(path)
		assert runtime_lookup(entries, KEY, "AMF Client") == expected["priority"], f"{configuration} runtime lookup failed for {language}"
		assert runtime_lookup(entries, "Low Priority Discord", "AMF Client") == expected["discord"], f"Discord lookup failed for {configuration}/{language}"
		assert runtime_lookup(entries, "Custom Hooks", "AMF Client") == expected["hooks"], f"Custom Hooks lookup failed for {configuration}/{language}"
		assert runtime_lookup(entries, "Custom Effects", "AMF Client") == expected["effects"], f"Custom Effects lookup failed for {configuration}/{language}"
		assert not any(OLD_KEY in original for original, _ in entries), f"old priority label remains in {configuration}/{language} catalog"
		assert not any(original == "Hook Effects" for original, _ in entries), f"ambiguous card title remains in {configuration}/{language} catalog"

assert runtime_lookup({}, KEY, "AMF Client") == KEY, "English fallback must return the source key"
print("AMF Debug/Release-staged localization lookup: PASS (EN/RU/UA priority, Discord, Custom Hooks/Effects)")
