#!/usr/bin/env python3
"""Targeted structural/runtime-catalog checks for AMF Settings Search."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SEARCH = (ROOT / "src/game/client/components/tclient/amf_settings_search.cpp").read_text(encoding="utf-8")
MATCHER = (ROOT / "src/game/client/components/tclient/amf_settings_search_match.h").read_text(encoding="utf-8")
MENUS = (ROOT / "src/game/client/components/menus_settings.cpp").read_text(encoding="utf-8")
AMF_MENUS = (ROOT / "src/game/client/components/tclient/menus_tclient.cpp").read_text(encoding="utf-8")
POLICY = (ROOT / "src/game/client/components/tclient/amf_settings_tab_policy.h").read_text(encoding="utf-8")
SCROLL = (ROOT / "src/game/client/ui_scrollregion.cpp").read_text(encoding="utf-8")
LOCALIZATION_TEST = (ROOT / "src/test/localization_runtime_test.cpp").read_text(encoding="utf-8")


def parse_catalog(path: Path) -> dict[tuple[str, str], str]:
	lines = path.read_text(encoding="utf-8-sig").splitlines()
	result: dict[tuple[str, str], str] = {}
	context = ""
	i = 0
	while i < len(lines):
		line = lines[i]
		if not line:
			i += 1
			continue
		if line.startswith("[") and line.endswith("]"):
			context = line[1:-1]
			i += 1
			continue
		if i + 1 < len(lines) and lines[i + 1].startswith("== "):
			result[(line, context)] = lines[i + 1][3:] or line
			i += 2
			context = ""
		else:
			i += 1
			context = ""
	return result


def lookup(catalog: dict[tuple[str, str], str], key: str, context: str = "AMF Client") -> str:
	return catalog.get((key, context), catalog.get((key, ""), key))


assert "FontIcon::MAGNIFYING_GLASS" in SEARCH, "search uses the normal icon font, not emoji"
assert "DoPopupMenu(" in SEARCH and "PopupRender" in SEARCH
assert "pLineInput->Activate" in SEARCH and "KEY_F" in SEARCH
assert 'SetEmptyText(Localize("Search"))' in SEARCH
assert 'TCLocalize("Type to search settings", "AMF Client")' in SEARCH
assert 'Localize("No results")' in SEARCH
assert "gs_aCachedLanguage" in SEARCH and "g_Config.m_ClLanguagefile" in SEARCH
assert "MatchesText(LocalizedName(Entry), pQuery)" in SEARCH
assert "str_utf8_find_nocase" in MATCHER, "production matcher must perform case-insensitive UTF-8 search"
assert "NavigateToSettingsSearchResult(Entry.m_SettingsPage, Entry.m_Subpage)" in SEARCH
assert "Entry.m_pId" in SEARCH and "gs_PendingScroll" in SEARCH
assert "ActiveScrollRegion()->ScrollHere(CScrollRegion::SCROLLHERE_KEEP_IN_VIEW)" in SEARCH
assert "DrawOutline" in SEARCH, "selection navigation should visibly highlight the target"
assert "AmfSettingsSearch::SetActiveScrollRegion(this)" in SCROLL
assert "AmfSettingsSearch::ClearActiveScrollRegion(this)" in SCROLL
assert "AmfSettingsSearch::RenderButton(this, Ui(), GameClient(), Input(), Button)" in MENUS
assert "m_AmfSettingsSearchTemporaryPage" in MENUS and "m_TClientSettingsSearchTemporaryTab" in MENUS

route = MENUS.split("void CMenus::NavigateToSettingsSearchResult", 1)[1].split("void CMenus::RenderSettings", 1)[0]
assert "m_AmfSettingsPage = Subpage" in route
assert "m_TClientSettingsTab = Subpage" in route
assert "g_Config.m_TcTClientSettingsTabs =" not in route, "search may not persistently unhide a tab"
assert "m_AmfSettingsSearchTemporaryPage = Subpage" in route
assert "m_TClientSettingsSearchTemporaryTab = Subpage" in route
assert "AmfSettingsTabPolicy::IsHidden" in route

amf_sidebar = AMF_MENUS.split("const char *apAmfPageNames[]", 1)[1].split("};", 1)[0]
assert [amf_sidebar.index(f'TCLocalize("{label}"') for label in ("General", "Visual", "Information")] == sorted(
	amf_sidebar.index(f'TCLocalize("{label}"') for label in ("General", "Visual", "Information")
)
assert "PAGE_GENERAL" in POLICY and "PAGE_VISUAL" in POLICY and "PAGE_INFORMATION" in POLICY
assert "PAGE_INFORMATION" in POLICY.split("IsHidden", 1)[1]

for expected in (
	'AMF_ENTRY("General"', 'AMF_ENTRY("Visual"', 'AMF_ENTRY("Information"',
	'AMF_ENTRY("Focus Mode"', 'AMF_ENTRY("Run Statistics"', 'AMF_ENTRY("Music Player"',
	'AMF_ENTRY("Low Priority Discord"', 'AMF_ENTRY("Low Priority Spotify | SoundCloud"',
	'AMF_ENTRY("Custom Hooks"', 'AMF_ENTRY("Custom Effects"', 'AMF_ENTRY("Smooth HUD"',
	'AMF_ENTRY("Finish Prediction"', 'AMF_ENTRY("Custom Tune Zone Colors"',
	'AMF_ENTRY("Moving Tiles"', 'AMF_ENTRY("Improved UI"', 'AMF_ENTRY("Flying Nameplates"',
	'AMF_ENTRY("Freeze Lifestyle"', 'AMF_ENTRY("Physic Balls"',
):
	assert expected in SEARCH, f"required real search catalog entry missing: {expected}"

assert "AmfSettingsSearch::MatchesText" in LOCALIZATION_TEST
assert "AmfSettingsSearchMatchesLocalizedVisibleLabels" in LOCALIZATION_TEST

translations = {
	"russian": {
		"General": "Основное", "Visual": "Визуальное", "Information": "Информация",
		"Focus Mode": "Режим фокуса", "Run Statistics": "Статистика забегов",
		"Music Player": "Музыкальный проигрыватель",
		"Type to search settings": "Введите запрос для поиска настроек",
	},
	"ukrainian": {
		"General": "Загальні", "Visual": "Візуальне", "Information": "Інформація",
		"Focus Mode": "Режим фокусу", "Run Statistics": "Статистика забігів",
		"Music Player": "Музичний програвач",
		"Type to search settings": "Введіть запит для пошуку налаштувань",
	},
}

for language, expected in translations.items():
	for location in (ROOT / "data/languages" / f"{language}.txt",):
		catalog = parse_catalog(location)
		for key, value in expected.items():
			assert lookup(catalog, key) == value, f"source catalog lookup mismatch: {language}: {key}"
	for config in ("Debug", "Release"):
		path = ROOT / f"build/{config}/data/languages/{language}.txt"
		assert path.is_file(), f"missing {config} staged runtime catalog {path}"
		catalog = parse_catalog(path)
		for key, value in expected.items():
			assert lookup(catalog, key) == value, f"runtime catalog lookup mismatch: {config}/{language}: {key}"
		for key in ("Low Priority Discord", "Low Priority Spotify | SoundCloud", "Custom Hooks", "Custom Effects"):
			assert lookup(catalog, key) != key, f"untranslated searchable setting title: {config}/{language}: {key}"
		assert lookup(catalog, "Type to search settings") == expected["Type to search settings"]

assert lookup({}, "Music Player") == "Music Player", "English fallback must use the visible source label"
print("AMF Settings Search structure, routes, hide semantics, UTF-8 matching and staged EN/RU/UA labels: PASS")
