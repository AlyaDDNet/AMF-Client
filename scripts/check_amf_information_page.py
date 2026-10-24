#!/usr/bin/env python3
"""Bounded source/package regression checks for the AMF Information page."""

from pathlib import Path
import re
import struct


ROOT = Path(__file__).resolve().parents[1]
MENU_PATH = ROOT / "src/game/client/components/tclient/menus_tclient.cpp"
MENU_SETTINGS_PATH = ROOT / "src/game/client/components/menus_settings.cpp"
CONTENT_PATH = ROOT / "datasrc/content.py"
CMAKE_PATH = ROOT / "CMakeLists.txt"

MENU = MENU_PATH.read_text(encoding="utf-8")
MENU_SETTINGS = MENU_SETTINGS_PATH.read_text(encoding="utf-8")
CONTENT = CONTENT_PATH.read_text(encoding="utf-8")
CMAKE = CMAKE_PATH.read_text(encoding="utf-8")

PAGE_START = MENU.index("CUIRect InformationContent = Content;")
PAGE_END = MENU.index("GameClient()->NotifyAmfConfigChanged();", PAGE_START)
PAGE = MENU[PAGE_START:PAGE_END]

assert "InformationContent.VSplitMid(&LeftView, &RightView" in PAGE
assert "const float InformationTopPadding = LineSize;" in PAGE
assert "InformationContent.HSplitTop(InformationTopPadding, nullptr, &InformationContent);" in PAGE
assert "const float InformationBottomPadding = MarginSmall;" in PAGE
assert "LeftView.HSplitBottom(InformationBottomPadding, &LeftView, nullptr);" in PAGE
assert PAGE.index("LeftView.HSplitBottom(InformationBottomPadding") < PAGE.index("LeftView.HSplitBottom(HeadlineHeight + MarginSmall + LineSize * 2.0f")
assert "RightView.HSplitBottom(" not in PAGE
assert "TCLocalize(\"Config Files\")" in PAGE
assert "const float TeeSize = 50.0f;" in PAGE
assert "ProfilePanel" not in PAGE
assert "RightView.HSplitTop(TeeSize, &MemberRow, &RightView);" in PAGE
assert "AmfTabOptions.VSplitMid(&GeneralTabOption, &VisualTabOption" in PAGE
assert "Website" not in PAGE and "Support" not in PAGE
assert "Client Info" in PAGE and "OpenAmfClientInfoPopup()" in PAGE
CLIENT_INFO_BUTTON = 'DoButtonLineSize_Menu(&s_ClientInfoButton'
assert PAGE.count(CLIENT_INFO_BUTTON) == 1, "Client Info must remain a single existing button"
assert PAGE.index(CLIENT_INFO_BUTTON) > PAGE.index('ViewLink("https://github.com/AlyaDDNet/AMF-Client/releases")')
assert PAGE.index(CLIENT_INFO_BUTTON) < PAGE.index("CUIRect ConfigFilesView")
assert "&InformationButtonRow" in PAGE and "&ClientInfoButton" not in PAGE
assert "CUIRect InformationContent = Content;" in PAGE
assert "CUIRect ConfigFilesView, AmfClientSettingsButton;" in PAGE
assert 'DoButtonLineSize_Menu(&s_ConfigButton, TCLocalize("AMF Client settings"' in PAGE

TEAM_MEMBERS = (
    'RenderTeamMember(0, TCLocalize("Developer AMF", "AMF Client"), "alya?", "grizli2", IMAGE_AMF_HEART, true);',
    'RenderTeamMember(1, TCLocalize("Lead Tester", "AMF Client"), "mordıser", "Kirisaki_glow", IMAGE_AMF_MORDISER_CLICK, false);',
    'RenderTeamMember(2, TCLocalize("Helper AMF", "AMF Client"), "ϝσʅȥყ♡", "Kirisaki_black", IMAGE_AMF_HELPER_CLICK, false);',
)
positions = [PAGE.index(member) for member in TEAM_MEMBERS]
assert positions == sorted(positions), "Team order must be Alya, Mordiser, Helper"
assert [ord(char) for char in "mordıser"] == [0x006D, 0x006F, 0x0072, 0x0064, 0x0131, 0x0073, 0x0065, 0x0072]
assert [ord(char) for char in "ϝσʅȥყ♡"] == [0x03DD, 0x03C3, 0x0285, 0x0225, 0x10E7, 0x2661]
assert "https://guns.lol/alyaddnet" in PAGE
assert "IMAGE_AMF_HEART" in PAGE and "IMAGE_AMF_MORDISER_CLICK" in PAGE and "IMAGE_AMF_HELPER_CLICK" in PAGE
assert "DoButtonLogic(&s_aTeamMemberButtons[Member], 0, &MemberTeeRect, BUTTONFLAG_LEFT)" in PAGE
assert "RenderClickEmoticons(Member, MemberTeeRect, ClickImage);" in PAGE
assert '"mordıser", "Kirisaki_glow", IMAGE_AMF_MORDISER_CLICK, false);' in PAGE
assert "const bool IsMordiser = Member == 1;" in PAGE
assert "const bool MordiserHovered = IsMordiser && Ui()->MouseHovered(&MemberTeeRect);" in PAGE
assert "const int TeeEmote = MordiserHovered ? EMOTE_ANGRY : EMOTE_NORMAL;" in PAGE
assert "const bool HappyEyesOnHover = !IsMordiser;" in PAGE
assert 'RenderDevSkin(MemberTeeRect.Center(), TeeSize, pSkinName, "default", false, 0, 0, TeeEmote, false, true,' in PAGE
assert "ColorRGBA(0, 0, 0, 0), ColorRGBA(0, 0, 0, 0), HappyEyesOnHover);" in PAGE
assert 'RenderDevSkin(MemberTeeRect.Center(), TeeSize, "grizli2", "default", true, 0, 0, 0, false, true, DeveloperColor, DeveloperColor);' in PAGE
assert '"ϝσʅȥყ♡", "Kirisaki_black", IMAGE_AMF_HELPER_CLICK, false);' in PAGE

nickname_size = re.search(r"const float NicknameFontSize = LineSize \* ([0-9.]+)f;", PAGE)
assert nickname_size and float(nickname_size.group(1)) < 1.0, "Nickname font must be reduced below the former 1.04275 scale"
assert "const float TeamMemberRowGap = 14.0f;" in PAGE
assert "RightView.HSplitTop(TeamMemberRowGap, nullptr, &RightView);" in PAGE
assert "if(Member != 2)" not in PAGE, "All team rows must use the same spacing path"
assert "const float TeamSettingsSectionGap = 24.0f;" in PAGE
assert "RightView.HSplitTop(TeamSettingsSectionGap, nullptr, &RightView);" in PAGE
assert "RoleRow.y = MemberRow.y - MarginExtraSmall;" in PAGE
assert "RoleRow.h = FontSize + MarginExtraSmall;" in PAGE
assert "Ui()->DoLabel(&RoleRow, pRole, FontSize, TEXTALIGN_TL);" in PAGE
assert "NicknameRow = MemberTextRect;" in PAGE and "NicknameLabel = NicknameRow;" in PAGE
assert "DoLabelStreamed(*s_pTeamNicknameUiElement->Rect(Member), &NicknameLabel, pNickname, NicknameFontSize, TEXTALIGN_ML)" in PAGE
assert "TextRender()->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f));" in PAGE
assert "TextRender()->TextOutlineColor(PreviousOutlineColor);" in PAGE
assert "Dir = Ui()->MousePos() - Pos;" in MENU
assert "if(CuteEyes && HappyEyesOnHover && Length < 0.4f)" in MENU
assert "float Alpha = 1.0f, bool HappyEyesOnHover = true" in (ROOT / "src/game/client/components/menus.h").read_text(encoding="utf-8")

PAGE_LINKS = re.findall(r'ViewLink\("([^\"]+)"\)', PAGE)
assert set(PAGE_LINKS) == {
    "https://discord.gg/7zG28thvRV",
    "https://github.com/AlyaDDNet/AMF-Client/releases",
    "https://guns.lol/alyaddnet",
}, PAGE_LINKS
assert "AppData" not in PAGE and "C:\\Users\\" not in PAGE

assert "AmfSettingsTabPolicy::ResolvePage" in MENU
assert "AmfSettingsTabPolicy::IsHidden" in MENU_SETTINGS
assert "AmfSettingsTabHidingSharesPersistentMaskAndKeepsInformationVisible" in (ROOT / "src/test/config_compat_test.cpp").read_text(encoding="utf-8")

def translated_value(catalog: Path, key: str) -> str:
    lines = catalog.read_text(encoding="utf-8").splitlines()
    for index, line in enumerate(lines[:-1]):
        if line == key and lines[index + 1].startswith("== "):
            return lines[index + 1][3:]
    raise AssertionError(f"Missing translation: {key} in {catalog}")


translations = {
    "data/languages/russian.txt": {
        "AMF Client Team": "Команда AMF Client",
        "Developer AMF": "Разработчик AMF",
        "Lead Tester": "Гл. Тестер",
        "Helper AMF": "Хелпер AMF",
        "Hide Settings Tabs": "Скрыть вкладки настроек",
        "Config Files": "Файлы конфигурации",
    },
    "data/languages/ukrainian.txt": {
        "AMF Client Team": "Команда AMF Client",
        "Developer AMF": "Розробник AMF",
        "Lead Tester": "Гл. Тестер",
        "Helper AMF": "Хелпер AMF",
        "Hide Settings Tabs": "Приховати вкладки налаштувань",
        "Config Files": "Файли конфігурації",
    },
}
for relative_path, expected in translations.items():
    for key, value in expected.items():
        assert translated_value(ROOT / relative_path, key) == value

assets = (
    "data/skins/Kirisaki_glow.png",
    "data/skins/Kirisaki_black.png",
    "data/tclient/amf/mordiser_click.png",
    "data/tclient/amf/helper_click.png",
)
for relative_path in assets:
    asset_path = ROOT / relative_path
    assert asset_path.is_file(), f"Missing bundled asset: {asset_path}"
    if "click.png" in relative_path:
        image_data = asset_path.read_bytes()
        assert image_data[:8] == b"\x89PNG\r\n\x1a\n"
        assert struct.unpack(">II", image_data[16:24]) == (128, 128)
        assert relative_path.rsplit("/", 1)[1] in CMAKE

assert 'Image("amf_mordiser_click", "tclient/amf/mordiser_click.png")' in CONTENT
assert 'Image("amf_helper_click", "tclient/amf/helper_click.png")' in CONTENT
assert "skins/Kirisaki_glow.png" in CMAKE and "skins/Kirisaki_black.png" in CMAKE

print("AMF Information page, localization, tab policy, and bundled-asset invariants PASS")
