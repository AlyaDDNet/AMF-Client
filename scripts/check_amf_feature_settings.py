"""Bounded production-source regression checks for the ten ported settings families."""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COMPONENTS = ROOT / 'src/game/client/components'
visual = (COMPONENTS / 'tclient/amf_features/best_features_visual_ui.cpp').read_text(encoding='utf-8-sig')
menus = (COMPONENTS / 'tclient/menus_tclient.cpp').read_text(encoding='utf-8-sig')
nav = (COMPONENTS / 'menus_settings.cpp').read_text(encoding='utf-8-sig')

assert '{SETTINGS_AMF_CLIENT, 1, "Visual"}' in nav
assert '{SETTINGS_AMF_CLIENT, 0, "General"}' in nav
assert 'Destination.IsActive(g_Config.m_UiSettingsPage, Subpage)' in nav
assert 'm_AmfSettingsPage = Destination.m_Subpage' in nav
assert 'm_TClientSettingsTab = Destination.m_Subpage' in nav
assert 'g_Config.m_AmfSettingsNavExpanded = Mask' in nav
assert 's_aLeafButtons[NAV_GROUP_COUNT][10]' in nav
assert 'AmfSettingsPolicy::TakeCard(CardColumn, Height)' in visual
assert 'AmfSettingsPolicy::TakeCard(Column, Height)' in menus
assert 'VisualLeftColumn.y - VisualRightColumn.y' not in menus
assert 'BeginCard(Left, "UI Themes"' in visual
assert visual.index('BeginCard(Left, "Improved UI"') < visual.index('BeginCard(Left, "UI Themes"')
assert 'BeginCard(Right, "Hook Effects"' not in visual
assert 'CUIRect VisualLeftColumn = Content;' in menus
assert 'CUIRect VisualRightColumn = Content;' in menus
assert 'TakeCard(Content, CardHeight)' in menus  # Inputs -> Finish Prediction
assert 'TakeCard(Content, 2.0f * 8.0f + HeadlineHeight' in menus  # Optimizer -> System
assert 'GeneralCardGap' not in menus
assert menus.index('BeginVisualCard(VisualLeftColumn, SmoothHudCardHeight)') < menus.index('HasTwoVisualColumns, false)')
assert 'BeginVisualCard(VisualRightColumn, GradientModulesCardHeight)' in menus
visual_page = menus[menus.index('const CUIRect VisualScrollBounds = Content;'):menus.index('ScrollEnd.y = std::max(VisualLeftColumn.y, VisualRightColumn.y);')]
assert visual_page.rfind('BeginVisualCard(') == visual_page.index('BeginVisualCard(VisualRightColumn, GradientModulesCardHeight)')
general_start = menus.index('if(m_AmfSettingsPage == 0)\n\t{')
general_end = menus.index('\n\tif(m_AmfSettingsPage == 1)\n\t{', general_start)
general_page = menus[general_start:general_end]
focus_pos = general_page.index('TCLocalize("Focus Mode", "AMF Client")')
stats_pos = general_page.index('TCLocalize("Run Statistics", "AMF Client")')
general_scroll_pos = general_page.index('s_GeneralScrollRegion.AddRect(ScrollEnd)')
assert focus_pos < stats_pos < general_scroll_pos
assert general_page.count('TCLocalize("Run Statistics", "AMF Client")') == 1
assert 'AmfSettingsPolicy::TakeCard(Content, StatisticsCardHeight)' in general_page
assert 'g_Config.m_AmfImpactStatisticsAccess' in general_page
assert 'AMF_RUN_STATISTICS_BIND_COMMAND' in general_page
assert 'OpenAmfRunStatistics()' in general_page
assert 'Run Statistics' not in visual_page
assert 'ImpactCardHeight' not in visual_page and 'ImpactCard' not in visual_page
assert 'BallPlayerWidth * ActionScale' in visual and 'BallCursorWidth * ActionScale' in visual
assert 'DefaultValue, bool Alpha = false' in visual
assert 'AmfSettingsPolicy::ColorPreview(DefaultValue, Alpha), false, nullptr, Alpha' in visual
color_lines = [line.strip() for line in visual.splitlines() if 'AddColor(' in line]
assert len(color_lines) == 15
for line in color_lines:
    assert line.endswith(', true);') == ('Current.' in line or 'UiColor' in line), line
assert '"BestClient"' not in nav
assert visual.count('constexpr const char *pLabel = "E-Client";') == 1
assert visual.count('DrawPhysicBallsEClientBadge(Graphics(), Ui(), TextRender(), &Header, GAP);') == 1
assert 'AddCheck(Details, &g_Config.m_AmfFreezePullHookColor, "Custom hook color");' in visual
assert 'm_AmfFreezePullColor' in visual and 'IceWard' not in visual

def catalog(path):
    entries = {}
    context = ''
    key = None
    for line in path.read_text(encoding='utf-8-sig').splitlines():
        if not line or line.startswith('#'):
            continue
        if line.startswith('[') and line.endswith(']'):
            context = line[1:-1]
        elif line.startswith('== '):
            entries[(context, key)] = line[3:]
            key, context = None, ''
        else:
            key = line
    return entries

keys = set(re.findall(r'TCLocalize\("([^"\n]+)", "AMF Client"\)', visual))
for call in re.findall(r'(?:BeginCard|AddCheck|AddSlider|AddChoice|AddColor)\([^;\n]+', visual):
    strings = re.findall(r'"([^"\n]*)"', call)
    if strings:
        keys.add(strings[0])
for array in re.findall(r's_ap\w+\[\]\s*=\s*\{([^}]+)\}', visual):
    keys.update(re.findall(r'"([^"\n]+)"', array))
for label in ('Visual', 'General', 'Information'):
    keys.add(label)
general = menus[menus.index('// their dependent controls'):menus.index('// their dependent controls') + 16000] if '// their dependent controls' in menus else menus[menus.index('FinishPredictionRows'):menus.index('FinishPredictionRows') + 9000]
keys.update(re.findall(r'TCLocalize\("([^"\n]+)", "AMF Client"\)', general))
for call in re.findall(r'(?:BeginGeneralFeatureCard|DoGeneralFeatureCheck|DoGeneralFeatureSlider)\([^;\n]+', general):
    strings = re.findall(r'"([^"\n]*)"', call)
    if strings:
        keys.add(strings[0])
keys.discard('')
missing = {}
for language in ('russian', 'ukrainian'):
    entries = catalog(ROOT / f'data/languages/{language}.txt')
    missing[language] = sorted(key for key in keys if not entries.get(('AMF Client', key), entries.get(('', key))))
    for key in keys:
        value = entries.get(('AMF Client', key), entries.get(('', key), ''))
        if value:
            assert re.findall(r'%(?:\.\d+)?[sdif]', key) == re.findall(r'%(?:\.\d+)?[sdif]', value), (language, key)
print(json.dumps({'display_keys_checked': len(keys), 'color_call_sites': len(color_lines), 'missing': missing}, ensure_ascii=False))
assert not any(missing.values()), missing
print('AMF feature settings source/localization regressions PASS (not live rendering verification)')
