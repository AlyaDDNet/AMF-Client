#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_PROFILE_ASSETS_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_PROFILE_ASSETS_H

class CGameClient;
class CProfile;
class CUi;
class CMenus;
class CUIRect;

namespace TClientProfileAssets
{
enum
{
	ENTITIES = 1 << 0,
	GAME = 1 << 1,
	EMOTICONS = 1 << 2,
	PARTICLES = 1 << 3,
	HUD = 1 << 4,
	EXTRAS = 1 << 5,
	CURSOR = 1 << 6,
	ARROW = 1 << 7,
	AUDIO = 1 << 8,
	ALL = (1 << 9) - 1,
};

constexpr int LegacyFlags(int Tiles, int Gunpacks)
{
	return (Tiles ? ENTITIES : 0) | (Gunpacks ? (GAME | PARTICLES | HUD | EXTRAS | CURSOR | ARROW) : 0);
}

constexpr int EffectiveFlags(int ConfiguredFlags, int Tiles, int Gunpacks)
{
	return ConfiguredFlags < 0 ? LegacyFlags(Tiles, Gunpacks) : ConfiguredFlags & ALL;
}

int CurrentFlags();
void Apply(CGameClient *pGameClient, const CProfile &Profile);
void Fill(CProfile &Profile);
int DoDropDown(CUi *pUi, CMenus *pMenus, CUIRect *pRect, int Flags);
} // namespace TClientProfileAssets

#endif
