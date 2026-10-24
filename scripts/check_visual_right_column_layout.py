"""Targeted source invariants for the Visual page's two-column card order."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MENUS = (ROOT / "src/game/client/components/tclient/menus_tclient.cpp").read_text(encoding="utf-8-sig")
FEATURES = (ROOT / "src/game/client/components/tclient/amf_features/best_features_visual_ui.cpp").read_text(encoding="utf-8-sig")
POLICY = (ROOT / "src/game/client/components/tclient/amf_features/settings_policy.h").read_text(encoding="utf-8-sig")
CONFIG = (ROOT / "src/engine/shared/config_variables_amf.h").read_text(encoding="utf-8-sig")
TUNE_COLORS = (ROOT / "src/game/tune_zone_colors.h").read_text(encoding="utf-8-sig")
MAP_IMAGES = (ROOT / "src/game/client/components/mapimages.cpp").read_text(encoding="utf-8-sig")
RENDER_MAP = (ROOT / "src/game/map/render_map.cpp").read_text(encoding="utf-8-sig")
IMAGE_MANIPULATION = (ROOT / "src/engine/gfx/image_manipulation.cpp").read_text(encoding="utf-8-sig")
RU = (ROOT / "data/languages/russian.txt").read_text(encoding="utf-8-sig")
UA = (ROOT / "data/languages/ukrainian.txt").read_text(encoding="utf-8-sig")

assert "constexpr float SectionGap = 8.0f;" in POLICY

Start = MENUS.index("const bool AspectBlocked =")
DeferredCards = MENUS.index(
    "RenderAmfBestFeatureVisuals(VisualLeftColumn, VisualRightColumn, HasTwoVisualColumns, false)", Start
)
Prefix = MENUS[Start:DeferredCards]

Aspect = Prefix.index("AspectCardRect = AmfSettingsPolicy::TakeCard(VisualRightColumn, AspectCardHeight)")
Statistics = Prefix.index("ImpactCard = AmfSettingsPolicy::TakeCard(VisualRightColumn, ImpactCardHeight)")
Weapons = Prefix.index("RenderSettingsAmfEnhancedWeapons(VisualRightColumn)")
assert Aspect < Statistics < Weapons
assert "if(HasTwoVisualColumns)" in Prefix

# The helper's production render calls allocate the feature cards after the top
# three, using the same independent right cursor and TakeCard section gap.
assert Weapons < DeferredCards
FeatureCallStart = FEATURES.index("RenderPhysicBallsCard();")
FeatureCallEnd = FEATURES.index("\n}", FeatureCallStart)
FeatureCalls = FEATURES[FeatureCallStart:FeatureCallEnd]
assert FeatureCalls.index("RenderPhysicBallsCard();") < FeatureCalls.index("RenderFreezeLifestyleCard();")
assert FeatureCalls.index("RenderFreezeLifestyleCard();") < FeatureCalls.index("RenderFlyingNameplatesCard();")
for Title in ("Physic Balls", "Freeze Lifestyle", "Flying Nameplates"):
    assert f'BeginCard(Right, "{Title}"' in FEATURES

TailStart = MENUS.index("RenderTuneZoneColorCard();", MENUS.index("auto RenderGradientNamesCard"))
ScrollEnd = MENUS.index("CUIRect ScrollEnd = VisualScrollBounds;", TailStart)
TailCalls = MENUS[TailStart:ScrollEnd]
CallOrder = [
    "RenderTuneZoneColorCard();",
    "RenderMusicPlayerCard();",
    "RenderKeyIndicatorCard();",
    "RenderGradientNamesCard();",
    "RenderMotionBlurCard();",
]
Positions = [TailCalls.index(Call) for Call in CallOrder]
assert Positions == sorted(Positions)
assert TailCalls.rfind("RenderMotionBlurCard();") > TailCalls.rfind("RenderGradientNamesCard();")
assert "RenderMotionBlurCard();" in TailCalls

# Renames and toggle/selector use the production AMF config and native preset
# consumer; the original Blue slot remains index 0 and is now Standard.
assert 'TCLocalize("Custom Tune Zone Colors", "AMF Client")' in MENUS
assert "m_AmfTuneZoneColors" in MENUS
assert "s_TuneZoneSelectorSection.Update(" in MENUS
assert "Ui()->DoDropDown(" in MENUS
assert "SetTuneZoneColorsEnabled" in MENUS
assert "SetTuneZoneColorPreset(SelectedPreset)" in MENUS
assert "PRESET_BLUE = 0" in TUNE_COLORS
assert "PRESET_YELLOW" in TUNE_COLORS and "PRESET_WHITE" in TUNE_COLORS and "PRESET_BLACK" in TUNE_COLORS
assert "m_AmfTuneZoneColors" in TUNE_COLORS
assert "CustomTuneZoneColors = g_Config.m_AmfTuneZoneColors != 0" in MAP_IMAGES
assert "if(Graphics()->HasTextureArraysSupport() && g_Config.m_AmfTuneZoneColors)" in MAP_IMAGES

# Custom Tune Zone Colors has only its enable row and conditional full-width
# selector; no separate color preview or reserved horizontal space remains.
TuneCardStart = MENUS.index("auto RenderTuneZoneColorCard = [&]() {")
TuneCardEnd = MENUS.index("\n\t\t};\n\t\tfloat AnimationSectionProgress", TuneCardStart)
TuneCard = MENUS[TuneCardStart:TuneCardEnd]
assert "Preview" not in TuneCard
assert "VisualCardPadding * 2.0f + LineSize + SelectorHeight" in TuneCard
assert "FullSelectorHeight = MarginExtraSmall + LineSize" in TuneCard
assert "DoButton_CheckBoxAutoVMarginAndSet(" in TuneCard
assert "if(SelectorVisible)" in TuneCard
assert "CUIRect Swatch" not in TuneCard
assert "SwatchColor.Draw" not in TuneCard
assert "Button.VSplitLeft(" not in TuneCard
assert "DoDropDown(\n\t\t\t\t\t\t&Button," in TuneCard
assert "apPresetNames[CurrentPreset]" in TuneCard

# The preset is baked once into the source tune tile, then copied to every
# texture-array slice. Runtime per-tile color is neutral, matching the fallback.
assert "ColorizeWithPresetRect(ImgInfo, TuneZoneColor::ColorForIndex(1)" in MAP_IMAGES
assert MAP_IMAGES.count("ColorizeWithPresetRect(") == 1
assert "TuneMapInfo.CopyRectFrom(ImgInfo, TuneTileX, TuneTileY" in MAP_IMAGES
assert "return TuneZoneColor::RENDER_MULTIPLIER;" in RENDER_MAP
assert "PixelColorHSLA.l = PresetHsla.l;" in IMAGE_MANIPULATION
assert "PixelColor = color_cast<ColorRGBA>(PixelColorHSLA).Multiply(PresetColor);" in IMAGE_MANIPULATION
assert 'TCLocalize("Freeze Lifestyle", "AMF Client")' not in FEATURES # title is passed through BeginCard's shared Localize path
assert 'BeginCard(Right, "Freeze Lifestyle"' in FEATURES

def context_entries(text, context):
    lines = text.splitlines()
    entries = {}
    i = 0
    while i < len(lines):
        if lines[i] == f"[{context}]" and i + 2 < len(lines):
            entries[lines[i + 1]] = lines[i + 2][3:] if lines[i + 2].startswith("== ") else ""
            i += 3
        else:
            i += 1
    return entries

ExpectedTranslations = {
    "Custom Tune Zone Colors": ("Кастомные цвета тюн-зон", "Кастомні кольори тюн-зон"),
    "Toggle the custom tint used for tune zones.": ("Включает пользовательскую окраску тюн-зон.", "Вмикає власне забарвлення тюн-зон."),
    "Standard": ("Стандартный", "Стандартний"),
    "Red": ("Красный", "Червоний"),
    "Green": ("Зелёный", "Зелений"),
    "Purple": ("Фиолетовый", "Фіолетовий"),
    "Yellow": ("Жёлтый", "Жовтий"),
    "White": ("Белый", "Білий"),
    "Black": ("Чёрный", "Чорний"),
    "Freeze Lifestyle": ("Фриз лайфстайл", "Фріз лайфстайл"),
}
RuEntries = context_entries(RU, "AMF Client")
UaEntries = context_entries(UA, "AMF Client")
for Key, (RuValue, UaValue) in ExpectedTranslations.items():
    assert RuEntries.get(Key) == RuValue, ("RU missing/mismatch", Key, RuEntries.get(Key))
    assert UaEntries.get(Key) == UaValue, ("UA missing/mismatch", Key, UaEntries.get(Key))

# The preallocated Aspect rectangle is the one rendered and used for scroll focus;
# statistics likewise renders into its TakeCard allocation. No residual-rect path.
assert "AspectCardRect.Draw(" in MENUS
assert "AspectCardRect.Margin(VisualCardPadding, &AspectCardContent)" in MENUS
assert "ImpactCard.Draw(" in MENUS
assert "VisualRightColumn.HSplitTop(AspectCardHeight" not in MENUS
assert "VisualRightColumn.HSplitTop(ImpactCardHeight" not in MENUS

print("Visual right-column order and allocation invariants PASS")
