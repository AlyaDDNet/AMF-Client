"""Targeted invariants for AMF settings sidebar ordering and tab identity."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COMPONENTS = ROOT / "src/game/client/components"
NAV = (COMPONENTS / "menus_settings.cpp").read_text(encoding="utf-8-sig")
AMF_MENU = (COMPONENTS / "tclient/menus_tclient.cpp").read_text(encoding="utf-8-sig")
POLICY = (COMPONENTS / "tclient/amf_settings_tab_policy.h").read_text(encoding="utf-8-sig")
COMPAT = (ROOT / "src/test/config_compat_test.cpp").read_text(encoding="utf-8-sig")

AMF_PAGES = re.search(r"static const SDestination s_aAmfPages\[\] = \{(.*?)\};", NAV, re.S)
assert AMF_PAGES, "AMF sidebar page list is missing"
ENTRIES = [(int(page), label) for page, label in re.findall(r'\{SETTINGS_AMF_CLIENT, (\d+), "([^"]+)"\}', AMF_PAGES.group(1))]
assert ENTRIES == [(0, "General"), (1, "Visual"), (2, "Information")], ENTRIES
assert len({page for page, _ in ENTRIES}) == len(ENTRIES), "AMF page IDs must be unique"
assert len({label for _, label in ENTRIES}) == len(ENTRIES), "AMF sidebar labels must be unique"

# Button highlight and activation are keyed by the destination IDs, not the
# display-array index. Expanding AMF selects the first visible destination.
assert "Destination.IsActive(g_Config.m_UiSettingsPage, Subpage)" in NAV
assert "ApplyDestination(Destination);" in NAV
assert "m_AmfSettingsPage = Destination.m_Subpage;" in NAV
assert "pName = TCLocalize(Destination.m_pLabel, \"AMF Client\");" in NAV
assert "for(int Index = 0; Index < aPageCounts[Group]; ++Index)" in NAV
assert "if(IsVisible(apPages[Group][Index]))" in NAV

# The stable IDs and persisted hide bits remain General=0, Visual=1,
# Information=2; Information maps to no hide bit and is always visible.
assert "PAGE_GENERAL = 0" in POLICY
assert "PAGE_VISUAL = 1" in POLICY
assert "PAGE_INFORMATION = 2" in POLICY
assert "MASK_GENERAL = 1 << 6" in POLICY
assert "MASK_VISUAL = 1 << 7" in POLICY
assert "Page == PAGE_GENERAL ? MASK_GENERAL : Page == PAGE_VISUAL ? MASK_VISUAL : 0" in POLICY
assert "return HiddenMask != 0 && (Mask & HiddenMask) != 0;" in POLICY
assert "return PAGE_INFORMATION;" in POLICY
assert "AmfSettingsTabHidingSharesPersistentMaskAndKeepsInformationVisible" in COMPAT

# The page tab labels and active page buttons continue to use those same IDs.
assert 'TCLocalize("General", "AMF Client")' in AMF_MENU
assert 'TCLocalize("Visual", "AMF Client")' in AMF_MENU
assert 'TCLocalize("Information", "AMF Client")' in AMF_MENU
assert "for(int Page = AmfSettingsTabPolicy::PAGE_GENERAL; Page <= AmfSettingsTabPolicy::PAGE_INFORMATION; ++Page)" in AMF_MENU
assert "s_aAmfPageTabs[Page], apAmfPageNames[Page], m_AmfSettingsPage == Page" in AMF_MENU

print("AMF settings sidebar order, page identity, and hide-bit invariants PASS")
