// Weapon settings use the AMF presentation framework; render semantics are donor-derived.
#include <game/client/components/menus.h>
#include <engine/font_icons.h>
#include <engine/shared/config.h>
#include <generated/client_data.h>
#include <game/client/gameclient.h>
#include <game/client/components/tclient/amf_features/settings_policy.h>
#include <game/client/components/tclient/amf_features/ui_theme/style.h>
#include <game/localization.h>
#include <game/client/smooth_ui.h>
#include <algorithm>
#include <cmath>

void CMenus::RenderSettingsAmfEnhancedWeapons(CUIRect &Column)
{
	constexpr float LineSize = 20.0f;
	constexpr float MarginSmall = 5.0f;
	constexpr float PreviewHeight = 106.0f;
	static int s_Weapon = 0;
	static CButtonContainer s_aWeaponButtons[3];
	static CButtonContainer s_aModeButtons[4];
	static CButtonContainer s_aResetColor[3];
	static CButtonContainer s_ResetButton;
	const char *apWeapons[] = {TCLocalize("Laser", "AMF Client"), TCLocalize("Shotgun", "AMF Client"), TCLocalize("Rocket", "AMF Client")};
	const char *apModes[] = {TCLocalize("Classic", "AMF Client"), TCLocalize("Prism", "AMF Client"), TCLocalize("Pulse", "AMF Client"), TCLocalize("Crystal", "AMF Client")};
	int *apEnabled[] = {&g_Config.m_AmfLaserGlow, &g_Config.m_AmfShotgunGlow, &g_Config.m_AmfRocketGlow};
	int *apPower[] = {&g_Config.m_AmfLaserGlowPower, &g_Config.m_AmfShotgunGlowPower, &g_Config.m_AmfRocketGlowPower};
	int *apMode[] = {&g_Config.m_AmfLaserGlowMode, &g_Config.m_AmfShotgunGlowMode, &g_Config.m_AmfRocketGlowMode};
	unsigned *apColor[] = {&g_Config.m_AmfLaserGlowColor, &g_Config.m_AmfShotgunGlowColor, &g_Config.m_AmfRocketGlowColor};
	const unsigned aDefaultColors[] = {DefaultConfig::AmfLaserGlowColor, DefaultConfig::AmfShotgunGlowColor, DefaultConfig::AmfRocketGlowColor};
	static CSmoothUiSectionAnimation s_NukeDetails;
	const float NukeProgress = s_NukeDetails.Update(g_Config.m_AmfMemeNukeExplosion != 0,
		Client()->RenderFrameTime(), 180, g_Config.m_AmfSmoothHud && g_Config.m_AmfAnimMenu);
	const float NukeHeight = s_Weapon == 2 ? (2.0f + 2.0f * NukeProgress) * (LineSize + MarginSmall) : 0.0f;
	const float Height = 6 * (LineSize + MarginSmall) + PreviewHeight + MarginSmall + NukeHeight;
	CUIRect Block;
	Block = AmfSettingsPolicy::TakeCard(Column, Height + 16.0f);
	AmfUiTheme::DrawUiBlock(&Block, IGraphics::CORNER_ALL, 8.0f);
	Block.Margin(8.0f, &Block);
	CUIRect Row, Button;
	Block.HSplitTop(LineSize, &Row, &Block);
	CUIRect ResetButton;
	Row.VSplitRight(LineSize + 8.0f, &Row, &ResetButton);
	const bool ResetClicked = Ui()->DoButton_FontIcon(&s_ResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &ResetButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_ResetButton, &ResetButton, TCLocalize("Reset to defaults", "AMF Client"));
	Ui()->DoLabel(&Row, TCLocalize("Enhanced Weapons", "AMF Client"), 20.0f, TEXTALIGN_ML);
	Block.HSplitTop(MarginSmall, nullptr, &Block);
	Block.HSplitTop(LineSize, &Row, &Block);
	const float WeaponWidth = Row.w / 3.0f;
	int NextWeapon = s_Weapon;
	for(int i = 0; i < 3; ++i)
	{
		Row.VSplitLeft(WeaponWidth, &Button, &Row);
		Button.HMargin(2.0f, &Button);
		const int Corners = i == 0 ? IGraphics::CORNER_L : (i == 2 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
		if(DoButton_Menu(&s_aWeaponButtons[i], apWeapons[i], s_Weapon == i, &Button, BUTTONFLAG_LEFT, nullptr, Corners))
			NextWeapon = i;
	}
	Block.HSplitTop(MarginSmall, nullptr, &Block);
	Block.HSplitTop(LineSize, &Row, &Block);
	DoButton_CheckBoxAutoVMarginAndSet(apEnabled[s_Weapon], TCLocalize("Enable", "AMF Client"), apEnabled[s_Weapon], &Row, LineSize);
	Block.HSplitTop(MarginSmall, nullptr, &Block);
	Block.HSplitTop(LineSize, &Row, &Block);
	Ui()->DoScrollbarOption(apPower[s_Weapon], apPower[s_Weapon], &Row, TCLocalize("Power", "AMF Client"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");
	Block.HSplitTop(MarginSmall, nullptr, &Block);
	DoLine_ColorPicker(&s_aResetColor[s_Weapon], LineSize, 15.0f, MarginSmall, &Block, TCLocalize("Color", "AMF Client"), apColor[s_Weapon],
		color_cast<ColorRGBA>(ColorHSLA(aDefaultColors[s_Weapon])), false, nullptr, false);
	CUIRect Preview;
	Block.HSplitTop(PreviewHeight, &Preview, &Block);
	Ui()->ClipEnable(&Preview);
	if(s_Weapon == 0)
		DoLaserPreview(&Preview, ColorHSLA(g_Config.m_ClLaserRifleOutlineColor), ColorHSLA(g_Config.m_ClLaserRifleInnerColor), LASERTYPE_RIFLE);
	else if(s_Weapon == 1)
		DoLaserPreview(&Preview, ColorHSLA(g_Config.m_ClLaserShotgunOutlineColor), ColorHSLA(g_Config.m_ClLaserShotgunInnerColor), LASERTYPE_SHOTGUN);
	else
		DoRocketPreview(&Preview);
	Ui()->ClipDisable();
	Block.HSplitTop(MarginSmall, nullptr, &Block);
	Block.HSplitTop(LineSize, &Row, &Block);
	const float ModeWidth = Row.w / 4.0f;
	for(int i = 0; i < 4; ++i)
	{
		Row.VSplitLeft(ModeWidth, &Button, &Row);
		Button.HMargin(2.0f, &Button);
		const int Corners = i == 0 ? IGraphics::CORNER_L : (i == 3 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
		if(DoButton_Menu(&s_aModeButtons[i], apModes[i], *apMode[s_Weapon] == i, &Button, BUTTONFLAG_LEFT, nullptr, Corners))
			*apMode[s_Weapon] = i;
	}
	if(s_Weapon == 2)
	{
		Block.HSplitTop(MarginSmall, nullptr, &Block);
		Block.HSplitTop(LineSize, &Row, &Block);
		Ui()->DoLabel(&Row, TCLocalize("Nuke effects", "AMF Client"), 16.0f, TEXTALIGN_ML);
		Block.HSplitTop(MarginSmall, nullptr, &Block);
		Block.HSplitTop(LineSize, &Row, &Block);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AmfMemeNukeExplosion, TCLocalize("Enable Nuke effects", "AMF Client"), &g_Config.m_AmfMemeNukeExplosion, &Row, LineSize);
		if(s_NukeDetails.IsVisible())
		{
			CUIRect DetailsClip;
			Block.HSplitTop(2.0f * (LineSize + MarginSmall) * NukeProgress, &DetailsClip, &Block);
			CUIRect Details = DetailsClip;
			Details.h = 2.0f * (LineSize + MarginSmall);
			Ui()->ClipEnable(&DetailsClip);
			// Closing controls are presentation only and must not change runtime state.
			const int SavedShake = g_Config.m_AmfMemeNukeShake;
			const int SavedScale = g_Config.m_AmfMemeNukeScale;
			Details.HSplitTop(MarginSmall, nullptr, &Details);
			Details.HSplitTop(LineSize, &Row, &Details);
			Ui()->DoScrollbarOption(&g_Config.m_AmfMemeNukeShake, &g_Config.m_AmfMemeNukeShake, &Row, TCLocalize("Screen shake", "AMF Client"), 0, 200, &CUi::ms_LinearScrollbarScale, 0u, "%");
			Details.HSplitTop(MarginSmall, nullptr, &Details);
			Details.HSplitTop(LineSize, &Row, &Details);
			Ui()->DoScrollbarOption(&g_Config.m_AmfMemeNukeScale, &g_Config.m_AmfMemeNukeScale, &Row, TCLocalize("Nuke size", "AMF Client"), 20, 300, &CUi::ms_LinearScrollbarScale, 0u, "%");
			if(!g_Config.m_AmfMemeNukeExplosion)
			{
				g_Config.m_AmfMemeNukeShake = SavedShake;
				g_Config.m_AmfMemeNukeScale = SavedScale;
			}
			Ui()->ClipDisable();
		}
	}
	if(ResetClicked)
	{
		const int aDefaultEnabled[] = {DefaultConfig::AmfLaserGlow, DefaultConfig::AmfShotgunGlow, DefaultConfig::AmfRocketGlow};
		const int aDefaultPower[] = {DefaultConfig::AmfLaserGlowPower, DefaultConfig::AmfShotgunGlowPower, DefaultConfig::AmfRocketGlowPower};
		const int aDefaultModes[] = {DefaultConfig::AmfLaserGlowMode, DefaultConfig::AmfShotgunGlowMode, DefaultConfig::AmfRocketGlowMode};
		*apEnabled[s_Weapon] = aDefaultEnabled[s_Weapon];
		*apPower[s_Weapon] = aDefaultPower[s_Weapon];
		*apMode[s_Weapon] = aDefaultModes[s_Weapon];
		*apColor[s_Weapon] = aDefaultColors[s_Weapon];
		if(s_Weapon == 2)
		{
			g_Config.m_AmfMemeNukeExplosion = DefaultConfig::AmfMemeNukeExplosion;
			g_Config.m_AmfMemeNukeShake = DefaultConfig::AmfMemeNukeShake;
			g_Config.m_AmfMemeNukeScale = DefaultConfig::AmfMemeNukeScale;
		}
	}
	s_Weapon = NextWeapon;
}

void CMenus::DoRocketPreview(const CUIRect *pRect)
{
	CUIRect Section = *pRect;

	const vec2 From = vec2(Section.x + 36.0f, Section.y + Section.h * 0.5f);
	const vec2 Pos = vec2(Section.x + Section.w - 36.0f, Section.y + Section.h * 0.5f);

	if(g_Config.m_AmfRocketGlow)
	{
		const float Power = (float)g_Config.m_AmfRocketGlowPower;
		const int Mode = std::clamp(g_Config.m_AmfRocketGlowMode, 0, 3);
		const int NumPoints = 16;
		vec2 aPoints[16];
		float aAlphas[16];
		const float GlobalTime = Client()->GlobalTime();

		for(int i = 0; i < NumPoints; ++i)
		{
			float t = (float)i / (float)(NumPoints - 1);
			float Arc = std::sin(t * pi) * 10.0f * std::sin(GlobalTime * 3.0f + t * 2.0f);
			aPoints[i] = mix(Pos, From, t) + vec2(0.0f, Arc);
			aAlphas[i] = 1.0f;
		}
		GameClient()->m_WeaponVfx.RenderRocketTrajectory(aPoints, aAlphas, NumPoints, Mode, Power);
	}

	Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteWeaponGrenade);
	Graphics()->SelectSprite(SPRITE_WEAPON_GRENADE_BODY);
	Graphics()->QuadsBegin();
	Graphics()->QuadsSetSubset(0, 0, 1, 1);
	Graphics()->DrawSprite(From.x, From.y, 48.0f);
	Graphics()->QuadsEnd();

	if(GameClient()->m_GameSkin.m_aSpriteWeaponProjectiles[WEAPON_GRENADE].IsValid())
	{
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_aSpriteWeaponProjectiles[WEAPON_GRENADE]);
		Graphics()->SelectSprite(SPRITE_WEAPON_GRENADE_PROJ);
		Graphics()->QuadsBegin();
		Graphics()->QuadsSetSubset(0, 0, 1, 1);
		Graphics()->DrawSprite(Pos.x, Pos.y, 24.0f);
		Graphics()->QuadsEnd();
	}
}
