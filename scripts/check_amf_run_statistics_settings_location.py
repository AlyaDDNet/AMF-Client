"""Check that Run Statistics settings are rendered once in AMF General."""
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MENUS = ROOT / "src/game/client/components/tclient/menus_tclient.cpp"
SOURCE = MENUS.read_text(encoding="utf-8-sig")

GENERAL_START = SOURCE.index('if(m_AmfSettingsPage == 0)\n\t{')
GENERAL_END = SOURCE.index('\n\tif(m_AmfSettingsPage == 1)\n\t{', GENERAL_START)
GENERAL = SOURCE[GENERAL_START:GENERAL_END]

VISUAL_START = SOURCE.index("const CUIRect VisualScrollBounds = Content;")
VISUAL_END = SOURCE.index("ScrollEnd.y = std::max(VisualLeftColumn.y, VisualRightColumn.y);", VISUAL_START)
VISUAL = SOURCE[VISUAL_START:VISUAL_END]

FOCUS = GENERAL.index('TCLocalize("Focus Mode", "AMF Client")')
STATISTICS = GENERAL.index('TCLocalize("Run Statistics", "AMF Client")')
SCROLL_END = GENERAL.index("s_GeneralScrollRegion.AddRect(ScrollEnd)")

assert FOCUS < STATISTICS < SCROLL_END
assert GENERAL.count('TCLocalize("Run Statistics", "AMF Client")') == 1
assert "AmfSettingsPolicy::TakeCard(Content, StatisticsCardHeight)" in GENERAL
assert "g_Config.m_AmfImpactStatisticsAccess" in GENERAL
assert "AMF_RUN_STATISTICS_BIND_COMMAND" in GENERAL
assert "OpenAmfRunStatistics()" in GENERAL
assert "s_StatisticsButtonSection.Update(" in GENERAL
assert "s_StatisticsButtonSection.AcceptsInput()" in GENERAL

assert 'TCLocalize("Run Statistics", "AMF Client")' not in VISUAL
assert "StatisticsCardHeight" not in VISUAL
assert "ImpactCardHeight" not in VISUAL
assert "m_AmfImpactStatisticsAccess" not in VISUAL
assert "AMF_RUN_STATISTICS_BIND_COMMAND" not in VISUAL

print("AMF Run Statistics settings location regression PASS")
