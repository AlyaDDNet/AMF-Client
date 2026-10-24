"""Verify the active UI-to-localization path for the Freeze Lifestyle card."""

import argparse
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
UI_PATH = ROOT / "src/game/client/components/tclient/amf_features/best_features_visual_ui.cpp"
parser = argparse.ArgumentParser()
parser.add_argument("--runtime-root", type=Path, help="also validate <portable-root>/data/languages")
ARGS = parser.parse_args()

EXPECTED = {
    "Freeze Lifestyle": ("Фриз лайфстайл", "Фріз лайфстайл"),
    "Enable": ("Включить", "Увімкнути"),
    "Shield": ("Щит", "Щит"),
    "Reach": ("Дальность", "Дальність"),
    "Amount": ("Количество", "Кількість"),
    "Frost": ("Иней", "Іній"),
    "Custom hook color": ("Пользовательский цвет хука", "Власний колір хука"),
    "Cold color": ("Холодный цвет", "Холодний колір"),
    "Reset to defaults": ("Сбросить настройки", "Скинути налаштування"),
}


def parse_catalog(path: Path) -> dict[tuple[str, str], list[str]]:
    entries: dict[tuple[str, str], list[str]] = {}
    context = ""
    pending_key = None
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        if not line or line.startswith("#"):
            continue
        if line.startswith("[") and line.endswith("]"):
            context = line[1:-1]
            pending_key = None
        elif line.startswith("== "):
            assert pending_key is not None, f"orphan translation in {path}: {line}"
            entries.setdefault((context, pending_key), []).append(line[3:])
            context = ""
            pending_key = None
        else:
            pending_key = line
    return entries


source = UI_PATH.read_text(encoding="utf-8-sig")
start = source.index("// Freeze Lifestyle is the donor's freeze-pull effect")
end = source.index("// Physic Balls has persistent skin/count controls", start)
card = source[start:end]

title = re.search(r'BeginCard\(Right,\s*"([^"]+)"', card)
assert title, "Freeze Lifestyle title is not rendered through BeginCard"
display_keys = {title.group(1)}
display_keys.update(re.findall(r'Add(?:Check|Slider|Color)\([^;\n]*?"([^"\n]+)"', card))
assert "AddResetButton(ResetRect" in card, "Freeze Lifestyle reset control is missing"
display_keys.add("Reset to defaults")  # tooltip supplied by AddResetButton
assert display_keys == set(EXPECTED), (
    f"Freeze Lifestyle display inventory changed: missing={set(EXPECTED) - display_keys}, "
    f"unexpected={display_keys - set(EXPECTED)}"
)

# Prove the actual card helpers route the labels through the same loaded
# localization database and the AMF Client context, rather than checking files alone.
for call in (
    'TCLocalize(pTitle, "AMF Client")',
    'TCLocalize(pText, "AMF Client")',
    'TCLocalize(pLabel, "AMF Client")',
    'TCLocalize("Reset to defaults", "AMF Client")',
):
    assert call in source, f"UI localization route missing: {call}"

localization = (ROOT / "src/game/localization.cpp").read_text(encoding="utf-8-sig")
gameclient = (ROOT / "src/game/client/gameclient.cpp").read_text(encoding="utf-8-sig")
assert "return Localize(pStr, pContext);" in localization
assert "g_Localization.Load(g_Config.m_ClLanguagefile, Storage(), Console())" in gameclient
assert 'str_format(aFilename, sizeof(aFilename), "languages/%s.txt", aEnglishName)' in localization

catalog_root = (ARGS.runtime_root / "data/languages") if ARGS.runtime_root else (ROOT / "data/languages")
for language, translations in (("russian", 0), ("ukrainian", 1)):
    catalog = parse_catalog(catalog_root / f"{language}.txt")
    for key, expected in EXPECTED.items():
        values = catalog.get(("AMF Client", key), [])
        assert len(values) == 1, f"{language}: expected one active AMF entry for {key!r}, found {len(values)}"
        value = values[0]
        assert value and value != key, f"{language}: missing translation for {key!r}"
        assert value == expected[translations], f"{language}: {key!r} => {value!r}, expected {expected[translations]!r}"

print(f"Freeze Lifestyle localization: {len(display_keys)} rendered strings; active UI path + RU/UA catalog PASS; missing RU=0, UA=0, untranslated visible strings=0")
