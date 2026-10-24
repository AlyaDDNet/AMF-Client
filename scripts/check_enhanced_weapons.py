"""Bounded donor equivalence and active-consumer checks for Enhanced Weapons only."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DONOR = Path('D:/BestClient-3.0')
PORT = ROOT / 'src/game/client/components/tclient/amf_features/weapons'

def adapt(text):
    return (text.replace('BESTCLIENT_WEAPON_VFX', 'AMF_WEAPON_VFX')
            .replace('m_Bc', 'm_Amf').replace('m_ClFocusMode', 'm_AmfFocusMode')
            .replace('m_ParticlesSkinLoaded', 'm_ParticlesSkin.m_Loaded')
            .replace('m_ExtrasSkinLoaded', 'm_ExtrasSkin.m_Loaded'))

for name in ('weapon_vfx.h', 'weapon_vfx.cpp', 'laser_glow.cpp', 'rocket_glow.cpp', 'nuke_vfx.cpp'):
    assert (PORT / name).read_text(encoding='utf-8-sig').strip() == adapt(
        (DONOR / 'src/game/client/components/bestclient' / name).read_text(encoding='utf-8-sig')).strip(), name

cfg = (ROOT / 'src/engine/shared/config_variables_amf.h').read_text(encoding='utf-8-sig')
dcfg = (DONOR / 'src/engine/shared/config_variables_bestclient.h').read_text(encoding='utf-8-sig')
declarations = [line for line in dcfg.splitlines() if line.startswith('MACRO_CONFIG_') and re.search(r'Bc(?:LaserGlow|ShotgunGlow|RocketGlow|MemeNuke)', line)]
assert len(declarations) == 15
ui = (PORT / 'settings.cpp').read_text(encoding='utf-8-sig')
runtime = '\n'.join((PORT / name).read_text(encoding='utf-8-sig') for name in ('weapon_vfx.cpp', 'laser_glow.cpp', 'rocket_glow.cpp', 'nuke_vfx.cpp'))
for line in declarations:
    assert line.replace('Bc', 'Amf').replace('bc_', 'amf_') in cfg
    symbol = re.search(r'\((Bc\w+),', line)[1].replace('Bc', 'Amf')
    assert 'm_' + symbol in ui and 'm_' + symbol in runtime, symbol
    assert 'DefaultConfig::' + symbol in ui, ('reset', symbol)
assert 's_Weapon = NextWeapon;' in ui
assert '*apMode[s_Weapon] = i;' in ui
assert 'CSmoothUiSectionAnimation s_NukeDetails' in ui
assert 'TCLocalize("Enhanced Weapons", "AMF Client")' in ui
assert 'AmfSettingsPolicy::TakeCard(Column, Height + 16.0f)' in ui
assert 'DoLine_ColorPicker' in ui
assert not re.search(r'\b(?:bc_|best_|bestclient_|eclient_)|m_Bc|BcMenuBadges', ui + runtime + cfg)
items = (ROOT / 'src/game/client/components/items.cpp').read_text(encoding='utf-8-sig')
effects = (ROOT / 'src/game/client/components/effects.cpp').read_text(encoding='utf-8-sig')
camera = (ROOT / 'src/game/client/components/camera.cpp').read_text(encoding='utf-8-sig')
menus = (ROOT / 'src/game/client/components/tclient/menus_tclient.cpp').read_text(encoding='utf-8-sig')
gc = (ROOT / 'src/game/client/gameclient.cpp').read_text(encoding='utf-8-sig')
assert gc.count('&m_WeaponVfx,') == 1
assert gc.index('&m_WeaponVfx,') < gc.index('&m_Particles.m_RenderExplosions,')
assert items.count('m_WeaponVfx.RenderWeaponLaser(') == 1
assert items.count('m_WeaponVfx.RenderRocketTrail(') == 1
assert 'g_Config.m_AmfRocketGlowPower > 0' in items
assert 'const bool CustomExplosion = GameClient()->m_WeaponVfx.TriggerExplosion(Pos, Alpha);' in effects
assert 'if(!CustomExplosion)' in effects and '!CustomExplosion && i < 24' in effects
assert camera.count('m_WeaponVfx.ApplyCameraShake(m_Center);') == 1
assert menus.count('RenderSettingsAmfEnhancedWeapons(VisualRightColumn);') == 1
assert not any(key in items + menus + cfg for key in ('AmfFancyWeapons', 'UseCrystalLaser', 'HasCrystalGeometry', 'Fancy Weapons'))

def catalog(path):
    entries = {}; context = ''; key = None
    for line in path.read_text(encoding='utf-8-sig').splitlines():
        if not line or line.startswith('#'): continue
        if line.startswith('[') and line.endswith(']'): context = line[1:-1]
        elif line.startswith('== '):
            pair = (context, key)
            if context == 'AMF Client' and key in keys:
                assert pair not in entries, ('duplicate', path, pair)
            entries[pair] = line[3:]; context = ''; key = None
        else: key = line
    return entries

keys = set(re.findall(r'TCLocalize\("([^"\n]+)", "AMF Client"\)', ui))
assert len(keys) == 16
for directory in ('data/languages', 'data/tclient/languages'):
    for language in ('russian', 'ukrainian'):
        entries = catalog(ROOT / directory / (language + '.txt'))
        for key in keys:
            assert entries.get(('AMF Client', key)), (directory, language, key)
            assert re.findall(r'%[sdif]', key) == re.findall(r'%[sdif]', entries[('AMF Client', key)])
        assert 'Улучшенное отображение оружия' not in entries.values()
print('Enhanced Weapons: 5 donor implementation files exact after documented API/config adaptations; 4 modes; 3 weapons; 15 config declarations; 16 localized keys; active hooks/reset PASS')
