/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus.h"

#include <base/dbg.h>
#include <base/math.h>
#include <base/str.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <cmath>

#include <game/client/components/menu_background.h>
#include <game/client/components/tclient/amf_features/settings_policy.h>
#include <game/client/components/tclient/amf_settings_tab_policy.h>
#include <game/client/components/tclient/amf_settings_search.h>
#include <game/client/components/tclient/amf_features/ui_theme/style.h>
#include <game/client/gameclient.h>
#include <game/client/smooth_ui.h>
#include <game/client/ui.h>
#include <game/localization.h>

void CMenus::SetNeedSendInfo()
{
	if(m_Dummy)
		m_NeedSendDummyinfo = true;
	else
		m_NeedSendinfo = true;
}

void CMenus::NavigateToSettingsSearchResult(int SettingsPage, int Subpage)
{
	g_Config.m_UiSettingsPage = SettingsPage;
	m_AmfRunStatisticsOpen = false;
	m_AmfSettingsSearchNavigationActive = true;
	m_AmfSettingsSearchTemporaryPage = -1;
	m_TClientSettingsSearchTemporaryTab = -1;
	if(SettingsPage == SETTINGS_AMF_CLIENT)
	{
		m_AmfSettingsPage = Subpage;
		if(AmfSettingsTabPolicy::IsHidden(g_Config.m_TcTClientSettingsTabs, Subpage))
			m_AmfSettingsSearchTemporaryPage = Subpage;
	}
	else if(SettingsPage == SETTINGS_TCLIENT)
	{
		m_TClientSettingsTab = Subpage;
		if((g_Config.m_TcTClientSettingsTabs & (1 << Subpage)) != 0)
			m_TClientSettingsSearchTemporaryTab = Subpage;
	}
}

void CMenus::RenderSettings(CUIRect MainView)
{
	AmfSettingsSearch::Update(Client()->RenderFrameTime());
	// A saved value may still point at the removed DDNet Credits page.
	// Keep the remaining settings pages valid and land on Configs instead.
	if(g_Config.m_UiSettingsPage < 0 || g_Config.m_UiSettingsPage >= SETTINGS_LENGTH)
		g_Config.m_UiSettingsPage = SETTINGS_CONFIGS;

	// render background
	CUIRect Button, TabBar, RestartBar;
	constexpr float SettingsTabsTopHeight = 50.0f;
	constexpr float SettingsTabGap = 10.0f;
	constexpr float SettingsTabHeight = 26.0f;
	const bool NavOnLeft = AmfUiTheme::IsSettingsNavOnLeft();
	if(NavOnLeft)
		MainView.VSplitLeft(120.0f, &TabBar, &MainView);
	else
		MainView.VSplitRight(120.0f, &MainView, &TabBar);
	CUIRect SettingsBackground = MainView;
	const bool GroupedNavigation = AmfUiTheme::IsNewTabOption();
	const float ConfigsButtonBottom = TabBar.y + SettingsTabsTopHeight + SETTINGS_LENGTH * (SettingsTabGap + SettingsTabHeight);
	SettingsBackground.h = ConfigsButtonBottom - SettingsBackground.y;
	AmfUiTheme::DrawUiMainPanel(&SettingsBackground, ms_ColorTabbarActive, false, NavOnLeft ? IGraphics::CORNER_BR : IGraphics::CORNER_BL, 10.0f);
	MainView.Margin(20.0f, &MainView);

	const bool NeedRestart = m_NeedRestartGraphics || m_NeedRestartSound || m_NeedRestartUpdate;
	if(NeedRestart)
	{
		MainView.HSplitBottom(20.0f, &MainView, &RestartBar);
		MainView.HSplitBottom(10.0f, &MainView, nullptr);
	}

	TabBar.HSplitTop(SettingsTabsTopHeight, &Button, &TabBar);
	AmfUiTheme::DrawUiMainPanel(&Button, ms_ColorTabbarActive, false, NavOnLeft ? IGraphics::CORNER_BL : IGraphics::CORNER_BR, 10.0f);
	AmfSettingsSearch::RenderButton(this, Ui(), GameClient(), Input(), Button);

	const char *apTabs[SETTINGS_LENGTH] = {
		Localize("Language"),
		Localize("General"),
		Localize("Player"),
		Client()->IsSixup() ? "Tee 0.7" : Localize("Tee"),
		Localize("Appearance"),
		Localize("Controls"),
		Localize("Graphics"),
		Localize("Sound"),
		Localize("DDNet"),
		Localize("Assets"),
		TCLocalize("TClient"),
		"AMF Client",
		Localize("Profiles"),
		Localize("Configs")};
	static CButtonContainer s_aTabButtons[SETTINGS_LENGTH];

	if(!GroupedNavigation)
	{
		for(int i = 0; i < SETTINGS_LENGTH; i++)
		{
			TabBar.HSplitTop(SettingsTabGap, nullptr, &TabBar);
			TabBar.HSplitTop(SettingsTabHeight, &Button, &TabBar);
			if(DoButton_MenuTab(&s_aTabButtons[i], apTabs[i], g_Config.m_UiSettingsPage == i, &Button, NavOnLeft ? IGraphics::CORNER_L : IGraphics::CORNER_R, &m_aAnimatorsSettingsTab[i]))
			{
				m_AmfSettingsSearchNavigationActive = false;
				m_AmfSettingsSearchTemporaryPage = -1;
				g_Config.m_UiSettingsPage = i;
			}
		}
	}
	else
	{
		// Match the donor's expandable settings groups, using only the existing
		// DDNet, TClient and AMF pages. AMF's normal settings tab remains inside
		// its page and no BestClient navigation/status is introduced.
		enum
		{
			NAV_DDNET,
			NAV_TCLIENT,
			NAV_AMF,
			NAV_GROUP_COUNT,
		};
		using AmfSettingsPolicy::SDestination;
		static const SDestination s_aDdnetPages[] = {
			{SETTINGS_LANGUAGE, -1, "Language"}, {SETTINGS_GENERAL, -1, "General"}, {SETTINGS_PLAYER, -1, "Player"},
			{SETTINGS_TEE, -1, "Tee"}, {SETTINGS_APPEARANCE, -1, "Appearance"}, {SETTINGS_CONTROLS, -1, "Controls"},
			{SETTINGS_GRAPHICS, -1, "Graphics"}, {SETTINGS_SOUND, -1, "Sound"}, {SETTINGS_DDNET, -1, "DDNet"}, {SETTINGS_ASSETS, -1, "Assets"}};
		// Subpage indices follow the existing TClient tab enum, not new pages.
		static const SDestination s_aTClientPages[] = {
			{SETTINGS_TCLIENT, 0, "Settings"}, {SETTINGS_TCLIENT, 1, "Bind Wheel"}, {SETTINGS_TCLIENT, 2, "War List"},
			{SETTINGS_TCLIENT, 3, "Chat Binds"}, {SETTINGS_TCLIENT, 4, "Status Bar"}, {SETTINGS_TCLIENT, 5, "Info"},
			{SETTINGS_PROFILES, -1, "Profiles"}, {SETTINGS_CONFIGS, -1, "Configs"}};
		static const SDestination s_aAmfPages[] = {
			{SETTINGS_AMF_CLIENT, 0, "General"}, {SETTINGS_AMF_CLIENT, 1, "Visual"}, {SETTINGS_AMF_CLIENT, 2, "Information"}};
		const auto IsVisible = [&](const SDestination &Destination) {
			if(Destination.m_Page == SETTINGS_TCLIENT && Destination.m_Subpage >= 0)
				return Destination.m_Subpage == m_TClientSettingsSearchTemporaryTab || (g_Config.m_TcTClientSettingsTabs & (1 << Destination.m_Subpage)) == 0;
			if(Destination.m_Page == SETTINGS_AMF_CLIENT && Destination.m_Subpage >= 0)
				return Destination.m_Subpage == m_AmfSettingsSearchTemporaryPage || !AmfSettingsTabPolicy::IsHidden(g_Config.m_TcTClientSettingsTabs, Destination.m_Subpage);
			return true;
		};
		const auto ApplyDestination = [&](const SDestination &Destination) {
			m_AmfSettingsSearchNavigationActive = false;
			m_AmfSettingsSearchTemporaryPage = -1;
			m_TClientSettingsSearchTemporaryTab = -1;
			g_Config.m_UiSettingsPage = Destination.m_Page;
			if(Destination.m_Page == SETTINGS_AMF_CLIENT)
			{
				m_AmfSettingsPage = Destination.m_Subpage;
				m_AmfRunStatisticsOpen = false;
			}
			else if(Destination.m_Page == SETTINGS_TCLIENT)
				m_TClientSettingsTab = Destination.m_Subpage;
		};
		static bool s_aExpanded[NAV_GROUP_COUNT]{};
		static bool s_Initialized = false;
		static int s_LastMask = -1;
		static CSmoothUiSectionAnimation s_aGroupAnimations[NAV_GROUP_COUNT];
		static SUIAnimator s_aGroupAnimators[NAV_GROUP_COUNT];
		static CButtonContainer s_aGroupButtons[NAV_GROUP_COUNT];
		static CButtonContainer s_aLeafButtons[NAV_GROUP_COUNT][10];
		static SUIAnimator s_aLeafAnimators[NAV_GROUP_COUNT][10];

		const auto CurrentGroup = [&]() {
			if(g_Config.m_UiSettingsPage == SETTINGS_TCLIENT || g_Config.m_UiSettingsPage == SETTINGS_PROFILES || g_Config.m_UiSettingsPage == SETTINGS_CONFIGS)
				return NAV_TCLIENT;
			if(g_Config.m_UiSettingsPage == SETTINGS_AMF_CLIENT)
				return NAV_AMF;
			return NAV_DDNET;
		};
		const auto LoadExpanded = [&](int Mask) {
			bool Any = false;
			for(int Group = 0; Group < NAV_GROUP_COUNT; ++Group)
			{
				s_aExpanded[Group] = (Mask & (1 << Group)) != 0;
				Any |= s_aExpanded[Group];
			}
			if(!Any)
				s_aExpanded[CurrentGroup()] = true;
		};
		const auto SaveExpanded = [&]() {
			int Mask = 0;
			for(int Group = 0; Group < NAV_GROUP_COUNT; ++Group)
				if(s_aExpanded[Group])
					Mask |= 1 << Group;
			g_Config.m_AmfSettingsNavExpanded = Mask;
			s_LastMask = Mask;
		};
		if(!s_Initialized || s_LastMask != g_Config.m_AmfSettingsNavExpanded)
		{
			LoadExpanded(g_Config.m_AmfSettingsNavExpanded);
			for(int Group = 0; Group < NAV_GROUP_COUNT; ++Group)
				s_aGroupAnimations[Group].Update(s_aExpanded[Group], 0.0f, 180, false);
			s_LastMask = g_Config.m_AmfSettingsNavExpanded;
			s_Initialized = true;
		}

		const bool Animate = g_Config.m_AmfSmoothHud && g_Config.m_AmfAnimMenu;
		const float DeltaTime = Client()->RenderFrameTime();
		constexpr float GroupHeight = 26.0f;
		constexpr float LeafHeight = 22.0f;
		constexpr float GroupGap = 8.0f;
		constexpr float LeafGap = 4.0f;
		constexpr float LeafInset = 14.0f;
		const char *apGroups[NAV_GROUP_COUNT] = {Localize("DDNet"), TCLocalize("TClient"), TCLocalize("AMF Client", "AMF Client")};
		const SDestination *apPages[NAV_GROUP_COUNT] = {s_aDdnetPages, s_aTClientPages, s_aAmfPages};
		const int aPageCounts[NAV_GROUP_COUNT] = {(int)std::size(s_aDdnetPages), (int)std::size(s_aTClientPages), (int)std::size(s_aAmfPages)};

		for(int Group = 0; Group < NAV_GROUP_COUNT; ++Group)
		{
			TabBar.HSplitTop(Group == 0 ? 4.0f : GroupGap, nullptr, &TabBar);
			TabBar.HSplitTop(GroupHeight, &Button, &TabBar);
			const bool IsCurrentGroup = CurrentGroup() == Group;
			if(DoButton_MenuTab(&s_aGroupButtons[Group], apGroups[Group], s_aExpanded[Group] || IsCurrentGroup, &Button,
				NavOnLeft ? IGraphics::CORNER_L : IGraphics::CORNER_R,
				Animate ? &s_aGroupAnimators[Group] : nullptr))
			{
				m_AmfSettingsSearchNavigationActive = false;
				m_AmfSettingsSearchTemporaryPage = -1;
				m_TClientSettingsSearchTemporaryTab = -1;
				s_aExpanded[Group] = !s_aExpanded[Group];
				if(s_aExpanded[Group])
				{
					if(Group == NAV_DDNET)
						s_aExpanded[NAV_TCLIENT] = false;
					else if(Group == NAV_TCLIENT)
						s_aExpanded[NAV_DDNET] = false;
					if(!IsCurrentGroup)
						for(int Index = 0; Index < aPageCounts[Group]; ++Index)
							if(IsVisible(apPages[Group][Index]))
							{
								ApplyDestination(apPages[Group][Index]);
								break;
							}
				}
				SaveExpanded();
			}

			int VisibleCount = 0;
			for(int Index = 0; Index < aPageCounts[Group]; ++Index)
				VisibleCount += IsVisible(apPages[Group][Index]);
			const float TargetHeight = VisibleCount * (LeafHeight + LeafGap);
			const bool ExpandedForSearch = m_AmfSettingsSearchNavigationActive && IsCurrentGroup;
			const float Progress = s_aGroupAnimations[Group].Update(s_aExpanded[Group] || ExpandedForSearch, DeltaTime, 180, Animate);
			if(!s_aGroupAnimations[Group].IsVisible())
				continue;
			CUIRect Leaves;
			TabBar.HSplitTop(std::ceil(TargetHeight * Progress), &Leaves, &TabBar);
			Ui()->ClipEnable(&Leaves);
			CUIRect LeafCursor = Leaves;
			for(int Index = 0; Index < aPageCounts[Group]; ++Index)
			{
				const SDestination &Destination = apPages[Group][Index];
				if(!IsVisible(Destination))
					continue;
				const int Subpage = Group == NAV_AMF ? m_AmfSettingsPage : m_TClientSettingsTab;
				const char *pName = Destination.m_Page == SETTINGS_TEE && Client()->IsSixup() ? "Tee 0.7" : TCLocalize(Destination.m_pLabel);
				if(Group == NAV_AMF)
					pName = TCLocalize(Destination.m_pLabel, "AMF Client");
				LeafCursor.HSplitTop(LeafGap, nullptr, &LeafCursor);
				LeafCursor.HSplitTop(LeafHeight, &Button, &LeafCursor);
				if(NavOnLeft)
					Button.VSplitLeft(LeafInset, nullptr, &Button);
				else
					Button.VSplitRight(LeafInset, &Button, nullptr);
				if(DoButton_MenuTab(&s_aLeafButtons[Group][Index], pName, Destination.IsActive(g_Config.m_UiSettingsPage, Subpage), &Button,
					NavOnLeft ? IGraphics::CORNER_L : IGraphics::CORNER_R,
					Animate ? &s_aLeafAnimators[Group][Index] : nullptr) &&
					s_aGroupAnimations[Group].AcceptsInput())
				{
					s_aExpanded[Group] = true;
					SaveExpanded();
					ApplyDestination(Destination);
				}
			}
			Ui()->ClipDisable();
		}
	}

	if(g_Config.m_UiSettingsPage == SETTINGS_LANGUAGE)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_LANGUAGE);
		RenderLanguageSettings(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_GENERAL)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_GENERAL);
		RenderSettingsGeneral(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_PLAYER)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_PLAYER);
		RenderSettingsPlayer(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_TEE)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_TEE);
		if(Client()->IsSixup())
			RenderSettingsTee7(MainView);
		else
			RenderSettingsTee(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_APPEARANCE)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_APPEARANCE);
		RenderSettingsAppearance(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_CONTROLS)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_CONTROLS);
		m_MenusSettingsControls.Render(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_GRAPHICS)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_GRAPHICS);
		RenderSettingsGraphics(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_SOUND)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_SOUND);
		RenderSettingsSound(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_DDNET)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_DDNET);
		RenderSettingsDDNet(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_ASSETS)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_ASSETS);
		RenderSettingsAssets(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_TCLIENT)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_RESERVED0);
		RenderSettingsTClient(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_AMF_CLIENT)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_RESERVED1);
		RenderSettingsAmfClient(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_PROFILES)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_RESERVED2);
		RenderSettingsTClientProfiles(MainView);
	}
	else if(g_Config.m_UiSettingsPage == SETTINGS_CONFIGS)
	{
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_RESERVED3);
		RenderSettingsTClientConfigs(MainView);
	}
	else
	{
		dbg_assert_failed("ui_settings_page invalid");
	}

	if(NeedRestart)
	{
		CUIRect RestartWarning, RestartButton;
		RestartBar.VSplitRight(125.0f, &RestartWarning, &RestartButton);
		RestartWarning.VSplitRight(10.0f, &RestartWarning, nullptr);
		if(m_NeedRestartUpdate)
		{
			TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
			Ui()->DoLabel(&RestartWarning, Localize("DDNet Client needs to be restarted to complete update!"), 14.0f, TEXTALIGN_ML);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		}
		else
		{
			Ui()->DoLabel(&RestartWarning, Localize("You must restart the game for all settings to take effect."), 14.0f, TEXTALIGN_ML);
		}

		static CButtonContainer s_RestartButton;
		if(DoButton_Menu(&s_RestartButton, Localize("Restart"), 0, &RestartButton))
		{
			if(Client()->State() == IClient::STATE_ONLINE || GameClient()->Editor()->HasUnsavedData())
			{
				m_Popup = POPUP_RESTART;
			}
			else
			{
				Client()->Restart();
			}
		}
	}
}

bool CMenus::RenderHslaScrollbars(CUIRect *pRect, unsigned int *pColor, bool Alpha, float DarkestLight)
{
	const unsigned PrevPackedColor = *pColor;
	ColorHSLA Color(*pColor, Alpha);
	const ColorHSLA OriginalColor = Color;
	const char *apLabels[] = {Localize("Hue"), Localize("Sat."), Localize("Lht."), Localize("Alpha")};
	const float SizePerEntry = 20.0f;
	const float MarginPerEntry = 5.0f;
	const float PreviewMargin = 2.5f;
	const float PreviewHeight = 40.0f + 2 * PreviewMargin;
	const float OffY = (SizePerEntry + MarginPerEntry) * (3 + (Alpha ? 1 : 0)) - PreviewHeight;

	CUIRect Preview;
	pRect->VSplitLeft(PreviewHeight, &Preview, pRect);
	Preview.HSplitTop(OffY / 2.0f, nullptr, &Preview);
	Preview.HSplitTop(PreviewHeight, &Preview, nullptr);

	Preview.Draw(ColorRGBA(0.15f, 0.15f, 0.15f, 1.0f), IGraphics::CORNER_ALL, 4.0f + PreviewMargin);
	Preview.Margin(PreviewMargin, &Preview);
	Preview.Draw(color_cast<ColorRGBA>(Color.UnclampLighting(DarkestLight)), IGraphics::CORNER_ALL, 4.0f + PreviewMargin);

	auto &&RenderHueRect = [&](CUIRect *pColorRect) {
		float CurXOff = pColorRect->x;
		const float SizeColor = pColorRect->w / 6;

		// red to yellow
		{
			Graphics()->SetColor4(
				ColorRGBA(1, 0, 0, 1),
				ColorRGBA(1, 1, 0, 1),
				ColorRGBA(1, 1, 0, 1),
				ColorRGBA(1, 0, 0, 1));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		// yellow to green
		CurXOff += SizeColor;
		{
			Graphics()->SetColor4(
				ColorRGBA(1, 1, 0, 1),
				ColorRGBA(0, 1, 0, 1),
				ColorRGBA(0, 1, 0, 1),
				ColorRGBA(1, 1, 0, 1));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		CurXOff += SizeColor;
		// green to turquoise
		{
			Graphics()->SetColor4(
				ColorRGBA(0, 1, 0, 1),
				ColorRGBA(0, 1, 1, 1),
				ColorRGBA(0, 1, 1, 1),
				ColorRGBA(0, 1, 0, 1));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		CurXOff += SizeColor;
		// turquoise to blue
		{
			Graphics()->SetColor4(
				ColorRGBA(0, 1, 1, 1),
				ColorRGBA(0, 0, 1, 1),
				ColorRGBA(0, 0, 1, 1),
				ColorRGBA(0, 1, 1, 1));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		CurXOff += SizeColor;
		// blue to purple
		{
			Graphics()->SetColor4(
				ColorRGBA(0, 0, 1, 1),
				ColorRGBA(1, 0, 1, 1),
				ColorRGBA(1, 0, 1, 1),
				ColorRGBA(0, 0, 1, 1));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		CurXOff += SizeColor;
		// purple to red
		{
			Graphics()->SetColor4(
				ColorRGBA(1, 0, 1, 1),
				ColorRGBA(1, 0, 0, 1),
				ColorRGBA(1, 0, 0, 1),
				ColorRGBA(1, 0, 1, 1));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}
	};

	auto &&RenderSaturationRect = [&](CUIRect *pColorRect, const ColorRGBA &CurColor) {
		ColorHSLA LeftColor = color_cast<ColorHSLA>(CurColor);
		ColorHSLA RightColor = color_cast<ColorHSLA>(CurColor);

		LeftColor.s = 0.0f;
		RightColor.s = 1.0f;

		const ColorRGBA LeftColorRGBA = color_cast<ColorRGBA>(LeftColor);
		const ColorRGBA RightColorRGBA = color_cast<ColorRGBA>(RightColor);

		Graphics()->SetColor4(LeftColorRGBA, RightColorRGBA, RightColorRGBA, LeftColorRGBA);

		IGraphics::CFreeformItem Freeform(
			pColorRect->x, pColorRect->y,
			pColorRect->x + pColorRect->w, pColorRect->y,
			pColorRect->x, pColorRect->y + pColorRect->h,
			pColorRect->x + pColorRect->w, pColorRect->y + pColorRect->h);
		Graphics()->QuadsDrawFreeform(&Freeform, 1);
	};

	auto &&RenderLightingRect = [&](CUIRect *pColorRect, const ColorRGBA &CurColor) {
		ColorHSLA LeftColor = color_cast<ColorHSLA>(CurColor);
		ColorHSLA RightColor = color_cast<ColorHSLA>(CurColor);

		LeftColor.l = DarkestLight;
		RightColor.l = 1.0f;

		const ColorRGBA LeftColorRGBA = color_cast<ColorRGBA>(LeftColor);
		const ColorRGBA RightColorRGBA = color_cast<ColorRGBA>(RightColor);

		Graphics()->SetColor4(LeftColorRGBA, RightColorRGBA, RightColorRGBA, LeftColorRGBA);

		IGraphics::CFreeformItem Freeform(
			pColorRect->x, pColorRect->y,
			pColorRect->x + pColorRect->w, pColorRect->y,
			pColorRect->x, pColorRect->y + pColorRect->h,
			pColorRect->x + pColorRect->w, pColorRect->y + pColorRect->h);
		Graphics()->QuadsDrawFreeform(&Freeform, 1);
	};

	auto &&RenderAlphaRect = [&](CUIRect *pColorRect, const ColorRGBA &CurColorFull) {
		const ColorRGBA LeftColorRGBA = color_cast<ColorRGBA>(color_cast<ColorHSLA>(CurColorFull).WithAlpha(0.0f));
		const ColorRGBA RightColorRGBA = color_cast<ColorRGBA>(color_cast<ColorHSLA>(CurColorFull).WithAlpha(1.0f));

		Graphics()->SetColor4(LeftColorRGBA, RightColorRGBA, RightColorRGBA, LeftColorRGBA);

		IGraphics::CFreeformItem Freeform(
			pColorRect->x, pColorRect->y,
			pColorRect->x + pColorRect->w, pColorRect->y,
			pColorRect->x, pColorRect->y + pColorRect->h,
			pColorRect->x + pColorRect->w, pColorRect->y + pColorRect->h);
		Graphics()->QuadsDrawFreeform(&Freeform, 1);
	};

	for(int i = 0; i < 3 + Alpha; i++)
	{
		CUIRect Button, Label;
		pRect->HSplitTop(SizePerEntry, &Button, pRect);
		pRect->HSplitTop(MarginPerEntry, nullptr, pRect);
		Button.VSplitLeft(140.0f, &Label, &Button);
		Label.VMargin(10.0f, &Label);

		Button.Draw(ColorRGBA(0.15f, 0.15f, 0.15f, 1.0f), IGraphics::CORNER_ALL, 1.0f);

		CUIRect Rail;
		Button.Margin(2.0f, &Rail);

		char aBuf[32];

		// Hue
		if(i == 0)
			str_format(aBuf, sizeof(aBuf), "%s: %.1f° (%03d)", apLabels[i], Color[i] * 360.0f, round_to_int(Color[i] * 255.0f));
		// Lht
		else if(i == 2)
		{
			// handle internal light clamping, see `UnclampLighting`
			float Lht = DarkestLight + Color[i] * (1.0f - DarkestLight);
			str_format(aBuf, sizeof(aBuf), "%s: %.1f%% (%03d)", apLabels[i], Lht * 100.0f, round_to_int(Color[i] * 255.0f));
		}
		// Sat and Alpha
		else
			str_format(aBuf, sizeof(aBuf), "%s: %.1f%% (%03d)", apLabels[i], Color[i] * 100.0f, round_to_int(Color[i] * 255.0f));
		Ui()->DoLabel(&Label, aBuf, 12.0f, TEXTALIGN_ML);

		ColorRGBA HandleColor;
		Graphics()->TextureClear();
		Graphics()->TrianglesBegin();
		if(i == 0)
		{
			RenderHueRect(&Rail);
			HandleColor = color_cast<ColorRGBA>(ColorHSLA(Color.h, 1.0f, 0.5f, 1.0f));
		}
		else if(i == 1)
		{
			RenderSaturationRect(&Rail, color_cast<ColorRGBA>(ColorHSLA(Color.h, 1.0f, 0.5f, 1.0f)));
			HandleColor = color_cast<ColorRGBA>(ColorHSLA(Color.h, Color.s, 0.5f, 1.0f));
		}
		else if(i == 2)
		{
			RenderLightingRect(&Rail, color_cast<ColorRGBA>(ColorHSLA(Color.h, Color.s, 0.5f, 1.0f)));
			HandleColor = color_cast<ColorRGBA>(ColorHSLA(Color.h, Color.s, Color.l, 1.0f).UnclampLighting(DarkestLight));
		}
		else if(i == 3)
		{
			RenderAlphaRect(&Rail, color_cast<ColorRGBA>(ColorHSLA(Color.h, Color.s, Color.l, 1.0f).UnclampLighting(DarkestLight)));
			HandleColor = color_cast<ColorRGBA>(Color.UnclampLighting(DarkestLight));
		}
		Graphics()->TrianglesEnd();

		Color[i] = Ui()->DoScrollbarH(&((char *)pColor)[i], &Button, Color[i], &HandleColor);
	}

	if(OriginalColor != Color)
	{
		*pColor = Color.Pack(Alpha);
	}
	return PrevPackedColor != *pColor;
}
