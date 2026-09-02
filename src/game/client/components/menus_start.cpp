/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus_start.h"

#include <algorithm>

#include <engine/client/updater.h>
#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/client_data.h>

#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>
#include <game/version.h>

#if defined(CONF_PLATFORM_ANDROID)
#include <android/android_main.h>
#endif

namespace
{
constexpr int AMF_CLIENT_INFO_AGREEMENT_REVISION = 2;
}

void CMenusStart::RenderStartMenu(CUIRect MainView)
{
	GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_START);
	static float s_StartMenuOpenedTime = -1.0f;
	const bool AnimateStartMenu = g_Config.m_AmfSmoothHud && Kernel()->RequestInterface<IEngineGraphics>()->WindowActive() && g_Config.m_AmfAnimStartMenu;
	if(s_StartMenuOpenedTime < 0.0f || !AnimateStartMenu)
		s_StartMenuOpenedTime = Client()->GlobalTime();
	const float StartMenuProgress = AnimateStartMenu ?
		std::clamp((Client()->GlobalTime() - s_StartMenuOpenedTime) / (g_Config.m_AmfAnimDuration / 1000.0f), 0.0f, 1.0f) :
		1.0f;

	static bool s_ClientInfoStartupHandled = false;
	if(!s_ClientInfoStartupHandled)
	{
		s_ClientInfoStartupHandled = true;
		if(g_Config.m_AmfClientInfoAgreementRevision != AMF_CLIENT_INFO_AGREEMENT_REVISION)
		{
			// A new agreement must not inherit the previous edition's opt-out.
			g_Config.m_AmfClientInfoAgreementRevision = AMF_CLIENT_INFO_AGREEMENT_REVISION;
			g_Config.m_AmfShowClientInfoOnStart = 1;
		}
		if(g_Config.m_AmfShowClientInfoOnStart)
			GameClient()->m_Menus.OpenAmfClientInfoPopup();
	}

	const float Rounding = 10.0f;
	const float VMargin = MainView.w / 2 - 190.0f;

	CUIRect Button;
	int NewPage = -1;

	CUIRect ExtMenu;
	MainView.VSplitLeft(30.0f, nullptr, &ExtMenu);
	ExtMenu.VSplitLeft(100.0f, &ExtMenu, nullptr);

	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static CButtonContainer s_DiscordButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_DiscordButton, Localize("Discord"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Client()->ViewLink(Localize("https://ddnet.org/discord"));
	}

	ExtMenu.HSplitBottom(5.0f, &ExtMenu, nullptr); // little space
	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static CButtonContainer s_LearnButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_LearnButton, Localize("Learn"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Client()->ViewLink(Localize("https://wiki.ddnet.org/"));
	}

	ExtMenu.HSplitBottom(5.0f, &ExtMenu, nullptr); // little space
	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static CButtonContainer s_TutorialButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_TutorialButton, Localize("Tutorial"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		GameClient()->m_Menus.JoinTutorial();
	}

	ExtMenu.HSplitBottom(5.0f, &ExtMenu, nullptr); // little space
	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static CButtonContainer s_WebsiteButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_WebsiteButton, Localize("Website"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Client()->ViewLink("https://ddnet.org/");
	}

	ExtMenu.HSplitBottom(5.0f, &ExtMenu, nullptr); // little space
	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static CButtonContainer s_NewsButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_NewsButton, Localize("News"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, g_Config.m_UiUnreadNews ? ColorRGBA(0.0f, 1.0f, 0.0f, 0.25f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || CheckHotKey(KEY_N))
		NewPage = CMenus::PAGE_NEWS;

	CUIRect Menu;
	MainView.VMargin(VMargin, &Menu);
	Menu.HSplitBottom(25.0f, &Menu, nullptr);

	constexpr int StartMenuButtonCount = 5;
	enum
	{
		START_BUTTON_PLAY,
		START_BUTTON_DEMOS,
		START_BUTTON_EDITOR,
		START_BUTTON_LOCAL_SERVER,
		START_BUTTON_SETTINGS,
	};
	CUIRect aStartMenuButtonRects[StartMenuButtonCount];
	{
		CUIRect Layout = Menu;
		Layout.HSplitBottom(40.0f, &Layout, nullptr);
		Layout.HSplitBottom(100.0f, &Layout, nullptr);
		Layout.HSplitBottom(40.0f, &Layout, &aStartMenuButtonRects[START_BUTTON_SETTINGS]);
		Layout.HSplitBottom(5.0f, &Layout, nullptr);
		Layout.HSplitBottom(40.0f, &Layout, &aStartMenuButtonRects[START_BUTTON_LOCAL_SERVER]);
		Layout.HSplitBottom(5.0f, &Layout, nullptr);
		Layout.HSplitBottom(40.0f, &Layout, &aStartMenuButtonRects[START_BUTTON_EDITOR]);
		Layout.HSplitBottom(5.0f, &Layout, nullptr);
		Layout.HSplitBottom(40.0f, &Layout, &aStartMenuButtonRects[START_BUTTON_DEMOS]);
		Layout.HSplitBottom(5.0f, &Layout, nullptr);
		Layout.HSplitBottom(40.0f, &Layout, &aStartMenuButtonRects[START_BUTTON_PLAY]);
	}
	const auto ScaleButtonRect = [](const CUIRect &Base, float Scale) {
		CUIRect Scaled = Base;
		Scaled.w *= Scale;
		Scaled.h *= Scale;
		Scaled.x = Base.x + (Base.w - Scaled.w) * 0.5f;
		Scaled.y = Base.y + (Base.h - Scaled.h) * 0.5f;
		return Scaled;
	};
	static float s_aStartMenuButtonScale[StartMenuButtonCount] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
	// Hovering the five primary actions is part of the start menu itself, not
	// the optional Smooth HUD+ entrance animation.
	const bool AnimateStartMenuHover = true;
	int HoveredStartButton = -1;
	if(AnimateStartMenuHover)
	{
		for(int i = 0; i < StartMenuButtonCount; ++i)
		{
			if(Ui()->MouseHovered(&aStartMenuButtonRects[i]))
			{
				HoveredStartButton = i;
				break;
			}
		}
	}
	const float HoverBlend = AnimateStartMenuHover ? std::clamp(Client()->RenderFrameTime() * 12.0f, 0.0f, 1.0f) : 1.0f;
	for(int i = 0; i < StartMenuButtonCount; ++i)
	{
		const float TargetScale = AnimateStartMenuHover && HoveredStartButton >= 0 ? (i == HoveredStartButton ? 1.08f : 0.94f) : 1.0f;
		s_aStartMenuButtonScale[i] += (TargetScale - s_aStartMenuButtonScale[i]) * HoverBlend;
	}
	const CUIRect PlayButtonRect = ScaleButtonRect(aStartMenuButtonRects[START_BUTTON_PLAY], s_aStartMenuButtonScale[START_BUTTON_PLAY]);

	// Keep the logo centered in the free space above the actual rendered Play button.
	Graphics()->TextureSet(g_pData->m_aImages[IMAGE_BANNER].m_Id);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1, 1, 1, StartMenuProgress);
	const float LogoWidth = std::min(484.0f, MainView.w - 80.0f);
	const float LogoHeight = LogoWidth * (2.0f / 3.0f);
	const float LogoY = MainView.y + (PlayButtonRect.y - MainView.y - LogoHeight) * 0.5f;
	const float LogoOffset = AnimateStartMenu && g_Config.m_AmfAnimSlide ?
		(1.0f - StartMenuProgress) * std::min((float)g_Config.m_AmfAnimSlideDistance, 4.0f) :
		0.0f;
	IGraphics::CQuadItem QuadItem(MainView.w / 2.0f - LogoWidth / 2.0f + 6.0f, LogoY + LogoOffset, LogoWidth, LogoHeight);
	Graphics()->QuadsDrawTL(&QuadItem, 1);
	Graphics()->QuadsEnd();

	// The donor keeps the quit action in the same bottom-center slot as the
	// former wide row, but makes its visual and clickable rect a 40x40 icon.
	{
		CUIRect QuitArea;
		MainView.VMargin(VMargin, &QuitArea);
		QuitArea.HSplitBottom(25.0f, &QuitArea, nullptr);
		QuitArea.HSplitBottom(40.0f, &QuitArea, &Button);
		CUIRect QuitButton = Button;
		QuitButton.w = QuitButton.h;
		QuitButton.x += (Button.w - QuitButton.w) * 0.5f;

		static CButtonContainer s_QuitButton;
		bool UsedEscape = false;
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
		if(GameClient()->m_Menus.DoButton_Menu(&s_QuitButton, FontIcon::POWER_OFF, 0, &QuitButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, Rounding, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || (UsedEscape = Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE)) || CheckHotKey(KEY_Q))
		{
			TextRender()->SetRenderFlags(0);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			if(UsedEscape || GameClient()->Editor()->HasUnsavedData() || (GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmQuitTime && g_Config.m_ClConfirmQuitTime >= 0))
				GameClient()->m_Menus.ShowQuitPopup();
			else
				Client()->Quit();
		}
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	}

	Button = ScaleButtonRect(aStartMenuButtonRects[START_BUTTON_SETTINGS], s_aStartMenuButtonScale[START_BUTTON_SETTINGS]);
	static CButtonContainer s_SettingsButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_SettingsButton, Localize("Settings"), 0, &Button, BUTTONFLAG_LEFT, g_Config.m_ClShowStartMenuImages ? "settings" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), &aStartMenuButtonRects[START_BUTTON_SETTINGS]) || CheckHotKey(KEY_S))
		NewPage = CMenus::PAGE_SETTINGS;

	Button = ScaleButtonRect(aStartMenuButtonRects[START_BUTTON_LOCAL_SERVER], s_aStartMenuButtonScale[START_BUTTON_LOCAL_SERVER]);
	static CButtonContainer s_LocalServerButton;

	const bool LocalServerRunning = GameClient()->m_LocalServer.IsServerRunning();
	if(GameClient()->m_Menus.DoButton_Menu(&s_LocalServerButton, LocalServerRunning ? Localize("Stop server") : Localize("Run server"), 0, &Button, BUTTONFLAG_LEFT, g_Config.m_ClShowStartMenuImages ? "local_server" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, LocalServerRunning ? ColorRGBA(0.0f, 1.0f, 0.0f, 0.25f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), &aStartMenuButtonRects[START_BUTTON_LOCAL_SERVER]) || (CheckHotKey(KEY_R) && Input()->KeyPress(KEY_R)))
	{
		if(LocalServerRunning)
		{
			GameClient()->m_LocalServer.KillServer();
		}
		else
		{
			GameClient()->m_LocalServer.RunServer({});
		}
	}

	Button = ScaleButtonRect(aStartMenuButtonRects[START_BUTTON_EDITOR], s_aStartMenuButtonScale[START_BUTTON_EDITOR]);
	static CButtonContainer s_MapEditorButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_MapEditorButton, Localize("Editor"), 0, &Button, BUTTONFLAG_LEFT, g_Config.m_ClShowStartMenuImages ? "editor" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, GameClient()->Editor()->HasUnsavedData() ? ColorRGBA(0.0f, 1.0f, 0.0f, 0.25f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), &aStartMenuButtonRects[START_BUTTON_EDITOR]) || CheckHotKey(KEY_E))
	{
		g_Config.m_ClEditor = 1;
		Input()->MouseModeRelative();
	}

	Button = ScaleButtonRect(aStartMenuButtonRects[START_BUTTON_DEMOS], s_aStartMenuButtonScale[START_BUTTON_DEMOS]);
	static CButtonContainer s_DemoButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_DemoButton, Localize("Demos"), 0, &Button, BUTTONFLAG_LEFT, g_Config.m_ClShowStartMenuImages ? "demos" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), &aStartMenuButtonRects[START_BUTTON_DEMOS]) || CheckHotKey(KEY_D))
	{
		NewPage = CMenus::PAGE_DEMOS;
	}

	Button = PlayButtonRect;
	static CButtonContainer s_PlayButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_PlayButton, Localize("Play", "Start menu"), 0, &Button, BUTTONFLAG_LEFT, g_Config.m_ClShowStartMenuImages ? "play_game" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), &aStartMenuButtonRects[START_BUTTON_PLAY]) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || CheckHotKey(KEY_P))
	{
		NewPage = g_Config.m_UiPage >= CMenus::PAGE_INTERNET && g_Config.m_UiPage <= CMenus::PAGE_FAVORITE_COMMUNITY_5 ? g_Config.m_UiPage : CMenus::PAGE_INTERNET;
	}

	// render version
	CUIRect CurVersion, ConsoleButton;
	MainView.HSplitBottom(45.0f, nullptr, &CurVersion);
	CurVersion.VSplitRight(40.0f, &CurVersion, nullptr);
	CurVersion.HSplitTop(20.0f, &ConsoleButton, &CurVersion);
	CurVersion.HSplitTop(5.0f, nullptr, &CurVersion);
	ConsoleButton.VSplitRight(40.0f, nullptr, &ConsoleButton);
	char aDDNetVersion[64];
	str_format(aDDNetVersion, sizeof(aDDNetVersion), "DDNet %s", GAME_RELEASE_VERSION);
	Ui()->DoLabel(&CurVersion, aDDNetVersion, 14.0f, TEXTALIGN_MR);

	CUIRect TClientVersion;
	MainView.HSplitTop(15.0f, &TClientVersion, &MainView);
	TClientVersion.VSplitRight(40.0f, &TClientVersion, nullptr);
	char aTBuf[64];
	str_format(aTBuf, sizeof(aTBuf), CLIENT_NAME " %s", CLIENT_RELEASE_VERSION_DISPLAY);
	Ui()->DoLabel(&TClientVersion, aTBuf, 14.0f, TEXTALIGN_MR);

#if defined(CONF_AUTOUPDATE)
	// Keep the update status directly below the AMF Client version. Only the
	// action button is compact; the status keeps enough room for its full text.
	CUIRect UpdatePanel;
	MainView.HSplitTop(44.0f, &UpdatePanel, nullptr);
	UpdatePanel.VSplitRight(40.0f, &UpdatePanel, nullptr);
	const float UpdatePanelWidth = std::min(260.0f, std::max(170.0f, MainView.w - 80.0f));
	UpdatePanel.VSplitRight(UpdatePanelWidth, nullptr, &UpdatePanel);
	CUIRect UpdateStatus, UpdateButton;
	UpdatePanel.HSplitTop(17.0f, &UpdateStatus, &UpdatePanel);
	UpdateStatus.VSplitRight(std::min(TextRender()->TextWidth(14.0f, aTBuf), UpdateStatus.w), nullptr, &UpdateStatus);
	UpdatePanel.HSplitTop(3.0f, nullptr, &UpdatePanel);
	UpdatePanel.HSplitTop(18.0f, &UpdateButton, nullptr);
	UpdateButton.VSplitRight(std::min(170.0f, UpdateStatus.w), nullptr, &UpdateButton);
#endif

	static CButtonContainer s_ConsoleButton;
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	if(GameClient()->m_Menus.DoButton_Menu(&s_ConsoleButton, FontIcon::TERMINAL, 0, &ConsoleButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.1f)))
	{
		GameClient()->m_GameConsole.Toggle(CGameConsole::CONSOLETYPE_LOCAL);
	}
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

#if defined(CONF_AUTOUPDATE)
	char aUpdateLabel[192] = "";
	const IUpdater::EUpdaterState State = Updater()->GetCurrentState();
	static CButtonContainer s_CheckUpdatesButton;
	if(State == IUpdater::VERSION_AVAILABLE)
	{
		if(UpdatePanel.w < 230.0f)
			str_copy(aUpdateLabel, TCLocalize("(Update available)", "AMF Client"));
		else
			str_format(aUpdateLabel, sizeof(aUpdateLabel), TCLocalize("AMF Client update %s is available", "AMF Client"), Updater()->GetLatestVersionString());
		static CButtonContainer s_DownloadUpdateButton;
		if(GameClient()->m_Menus.DoButton_Menu(&s_DownloadUpdateButton, TCLocalize("Download update", "AMF Client"), 0, &UpdateButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
			Updater()->InitiateUpdate();
		TextRender()->TextColor(1.0f, 0.55f, 0.35f, 1.0f);
	}
	else if(State == IUpdater::GETTING_MANIFEST)
	{
		str_copy(aUpdateLabel, TCLocalize("Checking for updates...", "AMF Client"));
		Ui()->RenderProgressBar(UpdateButton, 0.25f);
	}
	else if(State == IUpdater::DOWNLOADING)
	{
		str_format(aUpdateLabel, sizeof(aUpdateLabel), TCLocalize("Downloading update... %d%%", "AMF Client"), Updater()->GetCurrentPercent());
		Ui()->RenderProgressBar(UpdateButton, Updater()->GetCurrentPercent() / 100.0f);
	}
	else if(State == IUpdater::NEED_RESTART)
	{
		str_copy(aUpdateLabel, TCLocalize(UpdatePanel.w < 230.0f ? "(Update ready)" : "Update is ready to install", "AMF Client"));
		static CButtonContainer s_ApplyUpdateButton;
		if(GameClient()->m_Menus.DoButton_Menu(&s_ApplyUpdateButton, TCLocalize("Restart and update", "AMF Client"), 0, &UpdateButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
			Updater()->ApplyUpdateAndRestart();
	}
	else if(State == IUpdater::FAIL)
	{
		str_copy(aUpdateLabel, TCLocalize("Update failed", "AMF Client"));
		if(GameClient()->m_Menus.DoButton_Menu(&s_CheckUpdatesButton, TCLocalize("Check for updates", "AMF Client"), 0, &UpdateButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
			Updater()->CheckForUpdate();
		TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
	}
	else
	{
		if(Updater()->HasCheckedForUpdate())
			str_copy(aUpdateLabel, TCLocalize(UpdatePanel.w < 230.0f ? "(Up to date)" : "AMF Client is up to date", "AMF Client"));
		if(GameClient()->m_Menus.DoButton_Menu(&s_CheckUpdatesButton, TCLocalize("Check for updates", "AMF Client"), 0, &UpdateButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
			Updater()->CheckForUpdate();
	}
	Ui()->DoLabel(&UpdateStatus, aUpdateLabel, 12.0f, TEXTALIGN_MR);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
#endif

	GameClient()->m_Menus.RenderAmfClientInfoOverlay();

	if(NewPage != -1)
	{
		GameClient()->m_Menus.SetShowStart(false);
		GameClient()->m_Menus.SetMenuPage(NewPage);
	}
}

bool CMenusStart::CheckHotKey(int Key) const
{
	return !Input()->ShiftIsPressed() && !Input()->ModifierIsPressed() && !Input()->AltIsPressed() && // no modifier
	       Input()->KeyPress(Key) &&
	       !GameClient()->m_GameConsole.IsActive();
}
