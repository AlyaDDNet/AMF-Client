"""Targeted source/layout regression for the single requested Physic Balls badge."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
DONOR = Path("D:/BestClient-3.0")
TARGET = ROOT / "src/game/client/components/tclient/amf_features/best_features_visual_ui.cpp"
DONOR_MENU = DONOR / "src/game/client/components/bestclient/menus_bestclient.cpp"
DONOR_BADGES = DONOR / "src/game/client/bc_menu_badges.h"

source = TARGET.read_text(encoding="utf-8-sig")
donor_menu = DONOR_MENU.read_text(encoding="utf-8-sig")
donor_badges = DONOR_BADGES.read_text(encoding="utf-8-sig")

# The exact donor draws this badge in the Physic Balls title row.
assert donor_menu.count("DrawEClient(Graphics(), Ui(), TextRender(), &PhysicBallsTitleLabel, MarginSmall);") == 1
assert '"E-Client", 12.0f' in donor_badges
assert "ColorRGBA(0.95f, 0.80f, 0.20f, 1.0f), ColorRGBA(0.75f, 0.55f, 0.05f, 1.0f)" in donor_badges
assert "5.0f};" in donor_badges[donor_badges.index('"E-Client", 12.0f'):][:220]
for geometry in (
    "TextWidth(FontSize, pLabel) + 10.0f",
    "pRow->VSplitRight(BadgeWidth + Gap, pRow, &Badge)",
    "Badge.VSplitLeft(Gap, nullptr, &Badge)",
    "Badge.HMargin(2.0f, &Badge)",
    "IGraphics::CORNER_ALL, Rounding",
    "TEXTALIGN_MC",
):
    assert geometry in source, geometry
assert "constexpr float FontSize = 12.0f;" in source
assert "constexpr float Rounding = 5.0f;" in source
assert "constexpr float Gap = 5.0f;" not in source  # use shared AMF card gap
assert "Header.VSplitRight(GAP, &Header, nullptr);" in source
assert "DrawPhysicBallsEClientBadge(Graphics(), Ui(), TextRender(), &Header, GAP);" in source
assert source.count('BeginCard(Right, "Physic Balls", 3.0f * ROW_HEIGHT + 25.0f, nullptr, true)') == 1

# Exactly one user-facing occurrence in client production C++: this badge.
client = ROOT / "src/game/client"
literal_sites = []
for path in client.rglob("*"):
    if path.suffix in {".cpp", ".h", ".inl"}:
        literal_sites.extend((path, line_no) for line_no, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1)
                             if '"E-Client"' in line)
assert len(literal_sites) == 1 and literal_sites[0][0] == TARGET, literal_sites
assert source.count("DrawPhysicBallsEClientBadge(") == 2  # definition + its sole call

# Card extent and action geometry stay exactly as before; the badge only partitions
# the existing header row, so it cannot move the body controls or alter section flow.
assert "BeginCard(Right, \"Physic Balls\", 3.0f * ROW_HEIGHT + 25.0f, nullptr, true)" in source
assert "Body.HSplitTop(AmfSettingsPolicy::BallButtonHeight, &Buttons, &Body);" in source
assert "AmfSettingsPolicy::TakeCard(CardColumn, Height)" in source
assert "AmfSettingsPolicy::TakeCard" in source
assert re.search(r"DrawPhysicBallsEClientBadge\([^;]+\);", source)
print("Physic Balls E-Client badge: donor style/anchor PASS; unique occurrence PASS; body/layout unchanged; section spacing preserved")
