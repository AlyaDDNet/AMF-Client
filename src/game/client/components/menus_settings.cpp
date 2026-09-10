/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus.h"

#include <base/dbg.h>
#include <base/math.h>
#include <base/str.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/components/menu_background.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>

void CMenus::SetNeedSendInfo()
{
	if(m_Dummy)
		m_NeedSendDummyinfo = true;
	else
		m_NeedSendinfo = true;
}


/* DDNet 19.9 settings implementation kept in the original menu translation unit. */
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus.h"

#include <base/str.h>

#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/components/menu_background.h>
#include <game/client/components/tooltips.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>

void CMenus::RenderSettingsGeneral(CUIRect MainView)
{
	char aBuf[128 + IO_MAX_PATH_LENGTH];
	CUIRect Label, Button, Left, Right, Game, ClientSettings;
	MainView.HSplitTop(150.0f, &Game, &ClientSettings);

	// game
	{
		// headline
		Game.HSplitTop(30.0f, &Label, &Game);
		Ui()->DoLabel(&Label, Localize("Game"), 20.0f, TEXTALIGN_ML);
		Game.HSplitTop(5.0f, nullptr, &Game);
		Game.VSplitMid(&Left, nullptr, 20.0f);

		// dynamic camera
		Left.HSplitTop(20.0f, &Button, &Left);
		const bool IsDyncam = g_Config.m_ClDyncam || g_Config.m_ClMouseFollowfactor > 0;
		if(DoButton_CheckBox(&g_Config.m_ClDyncam, Localize("Dynamic Camera"), IsDyncam, &Button))
		{
			if(IsDyncam)
			{
				g_Config.m_ClDyncam = 0;
				g_Config.m_ClMouseFollowfactor = 0;
			}
			else
			{
				g_Config.m_ClDyncam = 1;
			}
		}

		// smooth dynamic camera
		Left.HSplitTop(5.0f, nullptr, &Left);
		Left.HSplitTop(20.0f, &Button, &Left);
		if(g_Config.m_ClDyncam)
		{
			if(DoButton_CheckBox(&g_Config.m_ClDyncamSmoothness, Localize("Smooth Dynamic Camera"), g_Config.m_ClDyncamSmoothness, &Button))
			{
				if(g_Config.m_ClDyncamSmoothness)
				{
					g_Config.m_ClDyncamSmoothness = 0;
				}
				else
				{
					g_Config.m_ClDyncamSmoothness = 50;
					g_Config.m_ClDyncamStabilizing = 50;
				}
			}
		}

		// weapon pickup
		Left.HSplitTop(5.0f, nullptr, &Left);
		Left.HSplitTop(20.0f, &Button, &Left);
		if(DoButton_CheckBox(&g_Config.m_ClAutoswitchWeapons, Localize("Switch weapon on pickup"), g_Config.m_ClAutoswitchWeapons, &Button))
			g_Config.m_ClAutoswitchWeapons ^= 1;

		// weapon out of ammo autoswitch
		Left.HSplitTop(5.0f, nullptr, &Left);
		Left.HSplitTop(20.0f, &Button, &Left);
		if(DoButton_CheckBox(&g_Config.m_ClAutoswitchWeaponsOutOfAmmo, Localize("Switch weapon when out of ammo"), g_Config.m_ClAutoswitchWeaponsOutOfAmmo, &Button))
			g_Config.m_ClAutoswitchWeaponsOutOfAmmo ^= 1;
	}

	// client
	{
		// headline
		ClientSettings.HSplitTop(30.0f, &Label, &ClientSettings);
		Ui()->DoLabel(&Label, Localize("Client"), 20.0f, TEXTALIGN_ML);
		ClientSettings.HSplitTop(5.0f, nullptr, &ClientSettings);
		ClientSettings.VSplitMid(&Left, &Right, 20.0f);

		// skip main menu
		Left.HSplitTop(20.0f, &Button, &Left);
		if(DoButton_CheckBox(&g_Config.m_ClSkipStartMenu, Localize("Skip the main menu"), g_Config.m_ClSkipStartMenu, &Button))
			g_Config.m_ClSkipStartMenu ^= 1;

		Left.HSplitTop(10.0f, nullptr, &Left);
		Left.HSplitTop(20.0f, &Button, &Left);
		str_copy(aBuf, " ");
		str_append(aBuf, Localize("Hz", "Hertz"));
		Ui()->DoScrollbarOption(&g_Config.m_ClRefreshRate, &g_Config.m_ClRefreshRate, &Button, Localize("Update Rate"), 10, 1000, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_INFINITE | CUi::SCROLLBAR_OPTION_NOCLAMPVALUE | CUi::SCROLLBAR_OPTION_DELAYUPDATE, aBuf);
		Left.HSplitTop(5.0f, nullptr, &Left);
		Left.HSplitTop(20.0f, &Button, &Left);
		static int s_LowerRefreshRate;
		if(DoButton_CheckBox(&s_LowerRefreshRate, Localize("Save power by lowering update rate (higher input latency)"), g_Config.m_ClRefreshRate <= 480 && g_Config.m_ClRefreshRate != 0, &Button))
			g_Config.m_ClRefreshRate = g_Config.m_ClRefreshRate > 480 || g_Config.m_ClRefreshRate == 0 ? 480 : 0;

		CUIRect SettingsButton;
		Left.HSplitBottom(20.0f, &Left, &SettingsButton);
		Left.HSplitBottom(5.0f, &Left, nullptr);
		static CButtonContainer s_SettingsButtonId;
		if(DoButton_Menu(&s_SettingsButtonId, Localize("Settings file"), 0, &SettingsButton))
		{
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, CONFIG_FILE, aBuf, sizeof(aBuf));
			Client()->ViewFile(aBuf);
		}
		GameClient()->m_Tooltips.DoToolTip(&s_SettingsButtonId, &SettingsButton, Localize("Open the settings file"));

		CUIRect SavesButton;
		Left.HSplitBottom(20.0f, &Left, &SavesButton);
		Left.HSplitBottom(5.0f, &Left, nullptr);
		static CButtonContainer s_SavesButtonId;
		if(DoButton_Menu(&s_SavesButtonId, Localize("Saves file"), 0, &SavesButton))
		{
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, SAVES_FILE, aBuf, sizeof(aBuf));
			Client()->ViewFile(aBuf);
		}
		GameClient()->m_Tooltips.DoToolTip(&s_SavesButtonId, &SavesButton, Localize("Open the saves file"));

		CUIRect ConfigButton;
		Left.HSplitBottom(20.0f, &Left, &ConfigButton);
		Left.HSplitBottom(5.0f, &Left, nullptr);
		static CButtonContainer s_ConfigButtonId;
		if(DoButton_Menu(&s_ConfigButtonId, Localize("Config directory"), 0, &ConfigButton))
		{
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, "", aBuf, sizeof(aBuf));
			Client()->ViewFile(aBuf);
		}
		GameClient()->m_Tooltips.DoToolTip(&s_ConfigButtonId, &ConfigButton, Localize("Open the directory that contains the configuration and user files"));

		CUIRect DirectoryButton;
		Left.HSplitBottom(20.0f, &Left, &DirectoryButton);
		Left.HSplitBottom(5.0f, &Left, nullptr);
		static CButtonContainer s_ThemesButtonId;
		if(DoButton_Menu(&s_ThemesButtonId, Localize("Themes directory"), 0, &DirectoryButton))
		{
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, "themes", aBuf, sizeof(aBuf));
			Storage()->CreateFolder("themes", IStorage::TYPE_SAVE);
			Client()->ViewFile(aBuf);
		}
		GameClient()->m_Tooltips.DoToolTip(&s_ThemesButtonId, &DirectoryButton, Localize("Open the directory to add custom themes"));

		Left.HSplitTop(20.0f, nullptr, &Left);
		RenderThemeSelection(Left);

		// auto demo settings
		{
			Right.HSplitTop(40.0f, nullptr, &Right);
			Right.HSplitTop(20.0f, &Button, &Right);
			if(DoButton_CheckBox(&g_Config.m_ClAutoDemoRecord, Localize("Automatically record demos"), g_Config.m_ClAutoDemoRecord, &Button))
				g_Config.m_ClAutoDemoRecord ^= 1;

			Right.HSplitTop(2 * 20.0f, &Button, &Right);
			if(g_Config.m_ClAutoDemoRecord)
				Ui()->DoScrollbarOption(&g_Config.m_ClAutoDemoMax, &g_Config.m_ClAutoDemoMax, &Button, Localize("Max demos"), 1, 1000, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_INFINITE | CUi::SCROLLBAR_OPTION_MULTILINE);

			Right.HSplitTop(10.0f, nullptr, &Right);
			Right.HSplitTop(20.0f, &Button, &Right);
			if(DoButton_CheckBox(&g_Config.m_ClAutoScreenshot, Localize("Automatically take game over screenshot"), g_Config.m_ClAutoScreenshot, &Button))
				g_Config.m_ClAutoScreenshot ^= 1;

			Right.HSplitTop(2 * 20.0f, &Button, &Right);
			if(g_Config.m_ClAutoScreenshot)
				Ui()->DoScrollbarOption(&g_Config.m_ClAutoScreenshotMax, &g_Config.m_ClAutoScreenshotMax, &Button, Localize("Max Screenshots"), 1, 1000, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_INFINITE | CUi::SCROLLBAR_OPTION_MULTILINE);
		}

		// auto statboard screenshot
		{
			Right.HSplitTop(10.0f, nullptr, &Right);
			Right.HSplitTop(20.0f, &Button, &Right);
			if(DoButton_CheckBox(&g_Config.m_ClAutoStatboardScreenshot, Localize("Automatically take statboard screenshot"), g_Config.m_ClAutoStatboardScreenshot, &Button))
			{
				g_Config.m_ClAutoStatboardScreenshot ^= 1;
			}

			Right.HSplitTop(2 * 20.0f, &Button, &Right);
			if(g_Config.m_ClAutoStatboardScreenshot)
				Ui()->DoScrollbarOption(&g_Config.m_ClAutoStatboardScreenshotMax, &g_Config.m_ClAutoStatboardScreenshotMax, &Button, Localize("Max Screenshots"), 1, 1000, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_INFINITE | CUi::SCROLLBAR_OPTION_MULTILINE);
		}

		// auto statboard csv
		{
			Right.HSplitTop(10.0f, nullptr, &Right);
			Right.HSplitTop(20.0f, &Button, &Right);
			if(DoButton_CheckBox(&g_Config.m_ClAutoCSV, Localize("Automatically create statboard csv"), g_Config.m_ClAutoCSV, &Button))
			{
				g_Config.m_ClAutoCSV ^= 1;
			}

			Right.HSplitTop(2 * 20.0f, &Button, &Right);
			if(g_Config.m_ClAutoCSV)
				Ui()->DoScrollbarOption(&g_Config.m_ClAutoCSVMax, &g_Config.m_ClAutoCSVMax, &Button, Localize("Max CSVs"), 1, 1000, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_INFINITE | CUi::SCROLLBAR_OPTION_MULTILINE);
		}
	}
}

void CMenus::RenderThemeSelection(CUIRect MainView)
{
	const std::vector<CTheme> &vThemes = GameClient()->m_MenuBackground.GetThemes();

	int SelectedTheme = -1;
	for(int i = 0; i < (int)vThemes.size(); i++)
	{
		if(str_comp(vThemes[i].m_Name.c_str(), g_Config.m_ClMenuMap) == 0)
		{
			SelectedTheme = i;
			break;
		}
	}
	const int OldSelected = SelectedTheme;

	static CListBox s_ListBox;
	s_ListBox.DoHeader(&MainView, Localize("Theme"), 20.0f);
	s_ListBox.DoStart(20.0f, vThemes.size(), 1, 3, SelectedTheme);

	for(int i = 0; i < (int)vThemes.size(); i++)
	{
		const CTheme &Theme = vThemes[i];
		const CListboxItem Item = s_ListBox.DoNextItem(&Theme.m_Name, i == SelectedTheme);

		if(!Item.m_Visible)
			continue;

		CUIRect Icon, Label;
		Item.m_Rect.VSplitLeft(Item.m_Rect.h * 2.0f, &Icon, &Label);

		// draw icon if it exists
		if(Theme.m_IconTexture.IsValid())
		{
			Icon.VMargin(6.0f, &Icon);
			Icon.HMargin(3.0f, &Icon);
			Graphics()->TextureSet(Theme.m_IconTexture);
			Graphics()->QuadsBegin();
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
			IGraphics::CQuadItem QuadItem(Icon.x, Icon.y, Icon.w, Icon.h);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsEnd();
		}

		char aName[128];
		if(Theme.m_Name.empty())
			str_copy(aName, "(none)");
		else if(str_comp(Theme.m_Name.c_str(), "auto") == 0)
			str_copy(aName, "(seasons)");
		else if(str_comp(Theme.m_Name.c_str(), "rand") == 0)
			str_copy(aName, "(random)");
		else if(Theme.m_HasDay && Theme.m_HasNight)
			str_copy(aName, Theme.m_Name.c_str());
		else if(Theme.m_HasDay && !Theme.m_HasNight)
			str_format(aName, sizeof(aName), "%s (day)", Theme.m_Name.c_str());
		else if(!Theme.m_HasDay && Theme.m_HasNight)
			str_format(aName, sizeof(aName), "%s (night)", Theme.m_Name.c_str());
		else // generic
			str_copy(aName, Theme.m_Name.c_str());

		Ui()->DoLabel(&Label, aName, 16.0f * CUi::ms_FontmodHeight, TEXTALIGN_ML);
	}

	SelectedTheme = s_ListBox.DoEnd();

	if(OldSelected != SelectedTheme)
	{
		const CTheme &Theme = vThemes[SelectedTheme];
		str_copy(g_Config.m_ClMenuMap, Theme.m_Name.c_str());
		GameClient()->m_MenuBackground.LoadMenuBackground(Theme.m_HasDay, Theme.m_HasNight);
	}
}



/* Settings section: player */
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus.h"

#include <base/str.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/components/console.h>
#include <game/client/components/countryflags.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>

#include <vector>

void CMenus::RenderSettingsPlayer(CUIRect MainView)
{
	CUIRect TabBar, PlayerTab, DummyTab, ChangeInfo, QuickSearch;
	MainView.HSplitTop(20.0f, &TabBar, &MainView);
	TabBar.VSplitMid(&TabBar, &ChangeInfo, 20.f);
	TabBar.VSplitMid(&PlayerTab, &DummyTab);
	MainView.HSplitTop(10.0f, nullptr, &MainView);

	static CButtonContainer s_PlayerTabButton;
	if(DoButton_MenuTab(&s_PlayerTabButton, Localize("Player"), !m_Dummy, &PlayerTab, IGraphics::CORNER_L, nullptr, nullptr, nullptr, nullptr, 4.0f))
	{
		m_Dummy = false;
	}

	static CButtonContainer s_DummyTabButton;
	if(DoButton_MenuTab(&s_DummyTabButton, Localize("Dummy"), m_Dummy, &DummyTab, IGraphics::CORNER_R, nullptr, nullptr, nullptr, nullptr, 4.0f))
	{
		m_Dummy = true;
	}

	if(Client()->State() == IClient::STATE_ONLINE &&
		GameClient()->m_aNextChangeInfo[m_Dummy] > Client()->GameTick(m_Dummy))
	{
		char aChangeInfo[128], aTimeLeft[32];
		str_format(aTimeLeft, sizeof(aTimeLeft), Localize("%ds left"), (GameClient()->m_aNextChangeInfo[m_Dummy] - Client()->GameTick(m_Dummy) + Client()->GameTickSpeed() - 1) / Client()->GameTickSpeed());
		str_format(aChangeInfo, sizeof(aChangeInfo), "%s: %s", Localize("Player info change cooldown"), aTimeLeft);
		Ui()->DoLabel(&ChangeInfo, aChangeInfo, 10.f, TEXTALIGN_ML);
	}

	static CLineInput s_NameInput;
	static CLineInput s_ClanInput;

	int *pCountry;
	if(!m_Dummy)
	{
		pCountry = &g_Config.m_PlayerCountry;
		s_NameInput.SetBuffer(g_Config.m_PlayerName, sizeof(g_Config.m_PlayerName));
		s_NameInput.SetEmptyText(Client()->PlayerName());
		s_ClanInput.SetBuffer(g_Config.m_PlayerClan, sizeof(g_Config.m_PlayerClan));
	}
	else
	{
		pCountry = &g_Config.m_ClDummyCountry;
		s_NameInput.SetBuffer(g_Config.m_ClDummyName, sizeof(g_Config.m_ClDummyName));
		s_NameInput.SetEmptyText(Client()->DummyName());
		s_ClanInput.SetBuffer(g_Config.m_ClDummyClan, sizeof(g_Config.m_ClDummyClan));
	}

	// player name
	CUIRect Button, Label;
	MainView.HSplitTop(20.0f, &Button, &MainView);
	Button.VSplitLeft(80.0f, &Label, &Button);
	Button.VSplitLeft(150.0f, &Button, nullptr);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "%s:", Localize("Name"));
	Ui()->DoLabel(&Label, aBuf, 14.0f, TEXTALIGN_ML);
	if(Ui()->DoEditBox(&s_NameInput, &Button, 14.0f))
	{
		SetNeedSendInfo();
	}

	// player clan
	MainView.HSplitTop(5.0f, nullptr, &MainView);
	MainView.HSplitTop(20.0f, &Button, &MainView);
	Button.VSplitLeft(80.0f, &Label, &Button);
	Button.VSplitLeft(150.0f, &Button, nullptr);
	str_format(aBuf, sizeof(aBuf), "%s:", Localize("Clan"));
	Ui()->DoLabel(&Label, aBuf, 14.0f, TEXTALIGN_ML);
	if(Ui()->DoEditBox(&s_ClanInput, &Button, 14.0f))
	{
		SetNeedSendInfo();
	}

	// country flag selector
	static CLineInputBuffered<25> s_FlagFilterInput;

	class CCountryFlagEntry
	{
	public:
		const CCountryFlags::CCountryFlag *m_pFlag;
		std::optional<std::pair<int, int>> m_NameMatch;
	};
	std::vector<CCountryFlagEntry> vFilteredFlags;
	for(size_t i = 0; i < GameClient()->m_CountryFlags.Num(); ++i)
	{
		const CCountryFlags::CCountryFlag &Entry = GameClient()->m_CountryFlags.GetByIndex(i);
		if(!s_FlagFilterInput.IsEmpty())
		{
			const char *pNameMatchEnd;
			const char *pNameMatchStart = str_utf8_find_nocase(Entry.m_aCountryCodeString, s_FlagFilterInput.GetString(), &pNameMatchEnd);
			if(pNameMatchStart != nullptr)
			{
				vFilteredFlags.emplace_back(&Entry, std::make_pair<int, int>(pNameMatchStart - Entry.m_aCountryCodeString, pNameMatchEnd - pNameMatchStart));
			}
		}
		else
		{
			vFilteredFlags.emplace_back(&Entry, std::nullopt);
		}
	}

	MainView.HSplitTop(10.0f, nullptr, &MainView);
	MainView.HSplitBottom(20.0f, &MainView, &QuickSearch);
	MainView.HSplitBottom(5.0f, &MainView, nullptr);
	QuickSearch.VSplitLeft(220.0f, &QuickSearch, nullptr);

	int OldSelected = -1;
	static CListBox s_ListBox;
	s_ListBox.DoStart(48.0f, vFilteredFlags.size(), 10, 3, OldSelected, &MainView);

	for(size_t i = 0; i < vFilteredFlags.size(); i++)
	{
		const CCountryFlagEntry &Entry = vFilteredFlags[i];

		if(Entry.m_pFlag->m_CountryCode == *pCountry)
			OldSelected = i;

		const CListboxItem Item = s_ListBox.DoNextItem(&Entry.m_pFlag->m_CountryCode, OldSelected >= 0 && (size_t)OldSelected == i);
		if(!Item.m_Visible)
			continue;

		CUIRect FlagRect;
		Item.m_Rect.Margin(5.0f, &FlagRect);
		FlagRect.HSplitBottom(12.0f, &FlagRect, &Label);
		Label.HSplitTop(2.0f, nullptr, &Label);
		const float OldWidth = FlagRect.w;
		FlagRect.w = FlagRect.h * 2;
		FlagRect.x += (OldWidth - FlagRect.w) / 2.0f;
		GameClient()->m_CountryFlags.Render(Entry.m_pFlag->m_CountryCode, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), FlagRect.x, FlagRect.y, FlagRect.w, FlagRect.h);

		if(Entry.m_pFlag->m_Texture.IsValid() || Entry.m_pFlag->m_CountryCode == CountryCode::DEFAULT)
		{
			SLabelProperties Props;
			Props.m_MaxWidth = Label.w - 5.0f;
			if(Entry.m_NameMatch.has_value())
			{
				const auto [MatchStart, MatchLength] = Entry.m_NameMatch.value();
				Props.m_vColorSplits.emplace_back(MatchStart, MatchLength, ColorRGBA(0.4f, 0.4f, 1.0f, 1.0f));
			}
			Ui()->DoLabel(&Label, Entry.m_pFlag->m_aCountryCodeString, 10.0f, TEXTALIGN_MC, Props);
		}
	}

	const int NewSelected = s_ListBox.DoEnd();
	if(OldSelected != NewSelected)
	{
		*pCountry = vFilteredFlags[NewSelected].m_pFlag->m_CountryCode;
		SetNeedSendInfo();
	}

	if(GameClient()->m_CountryFlags.Num() > 0 && vFilteredFlags.empty())
	{
		CUIRect FilterLabel, ResetButton;
		MainView.HMargin((MainView.h - (16.0f + 18.0f + 8.0f)) / 2.0f, &FilterLabel);
		FilterLabel.HSplitTop(16.0f, &FilterLabel, &ResetButton);
		ResetButton.HSplitTop(8.0f, nullptr, &ResetButton);
		ResetButton.VMargin((ResetButton.w - 200.0f) / 2.0f, &ResetButton);
		Ui()->DoLabel(&FilterLabel, Localize("No country flags match your filter criteria"), 16.0f, TEXTALIGN_MC);
		static CButtonContainer s_ResetButton;
		if(DoButton_Menu(&s_ResetButton, Localize("Reset filter"), 0, &ResetButton))
		{
			s_FlagFilterInput.Clear();
		}
	}

	Ui()->DoEditBox_Search(&s_FlagFilterInput, &QuickSearch, 14.0f, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive());
}



/* Settings section: tee */
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus.h"

#include <base/str.h>

#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/animstate.h>
#include <game/client/components/console.h>
#include <game/client/components/emoticon.h>
#include <game/client/components/skins.h>
#include <game/client/components/tooltips.h>
#include <game/client/gameclient.h>
#include <game/client/skin.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>

#include <algorithm>
#include <vector>

void CMenus::RenderSettingsTee(CUIRect MainView)
{
	CUIRect TabBar, PlayerTab, DummyTab, ChangeInfo;
	MainView.HSplitTop(20.0f, &TabBar, &MainView);
	TabBar.VSplitMid(&TabBar, &ChangeInfo, 20.f);
	TabBar.VSplitMid(&PlayerTab, &DummyTab);
	MainView.HSplitTop(10.0f, nullptr, &MainView);

	static CButtonContainer s_PlayerTabButton;
	if(DoButton_MenuTab(&s_PlayerTabButton, Localize("Player"), !m_Dummy, &PlayerTab, IGraphics::CORNER_L, nullptr, nullptr, nullptr, nullptr, 4.0f))
	{
		m_Dummy = false;
		m_SkinListScrollToSelected = true;
	}

	static CButtonContainer s_DummyTabButton;
	if(DoButton_MenuTab(&s_DummyTabButton, Localize("Dummy"), m_Dummy, &DummyTab, IGraphics::CORNER_R, nullptr, nullptr, nullptr, nullptr, 4.0f))
	{
		m_Dummy = true;
		m_SkinListScrollToSelected = true;
	}

	if(Client()->State() == IClient::STATE_ONLINE &&
		GameClient()->m_aNextChangeInfo[m_Dummy] > Client()->GameTick(m_Dummy))
	{
		char aChangeInfo[128], aTimeLeft[32];
		str_format(aTimeLeft, sizeof(aTimeLeft), Localize("%ds left"), (GameClient()->m_aNextChangeInfo[m_Dummy] - Client()->GameTick(m_Dummy) + Client()->GameTickSpeed() - 1) / Client()->GameTickSpeed());
		str_format(aChangeInfo, sizeof(aChangeInfo), "%s: %s", Localize("Player info change cooldown"), aTimeLeft);
		Ui()->DoLabel(&ChangeInfo, aChangeInfo, 10.f, TEXTALIGN_ML);
	}

	if(g_Config.m_Debug)
	{
		const CSkins::CSkinLoadingStats Stats = GameClient()->m_Skins.LoadingStats();
		char aStats[256];
		str_format(aStats, sizeof(aStats), "unloaded: %" PRIzu ", pending: %" PRIzu ", loading: %" PRIzu ",\nloaded: %" PRIzu ", error: %" PRIzu ", notfound: %" PRIzu,
			Stats.m_NumUnloaded, Stats.m_NumPending, Stats.m_NumLoading, Stats.m_NumLoaded, Stats.m_NumError, Stats.m_NumNotFound);
		Ui()->DoLabel(&ChangeInfo, aStats, 9.0f, TEXTALIGN_MR);
	}

	char *pSkinName;
	size_t SkinNameSize;
	int *pUseCustomColor;
	unsigned *pColorBody;
	unsigned *pColorFeet;
	int *pEmote;
	if(!m_Dummy)
	{
		pSkinName = g_Config.m_ClPlayerSkin;
		SkinNameSize = sizeof(g_Config.m_ClPlayerSkin);
		pUseCustomColor = &g_Config.m_ClPlayerUseCustomColor;
		pColorBody = &g_Config.m_ClPlayerColorBody;
		pColorFeet = &g_Config.m_ClPlayerColorFeet;
		pEmote = &g_Config.m_ClPlayerDefaultEyes;
	}
	else
	{
		pSkinName = g_Config.m_ClDummySkin;
		SkinNameSize = sizeof(g_Config.m_ClDummySkin);
		pUseCustomColor = &g_Config.m_ClDummyUseCustomColor;
		pColorBody = &g_Config.m_ClDummyColorBody;
		pColorFeet = &g_Config.m_ClDummyColorFeet;
		pEmote = &g_Config.m_ClDummyDefaultEyes;
	}

	const float EyeButtonSize = 40.0f;
	const bool RenderEyesBelow = MainView.w < 750.0f;
	CUIRect YourSkin, Checkboxes, SkinPrefix, Eyes, Button, Label;
	MainView.HSplitTop(90.0f, &YourSkin, &MainView);
	if(RenderEyesBelow)
	{
		YourSkin.VSplitLeft(MainView.w * 0.45f, &YourSkin, &Checkboxes);
		Checkboxes.VSplitLeft(MainView.w * 0.35f, &Checkboxes, &SkinPrefix);
		MainView.HSplitTop(5.0f, nullptr, &MainView);
		MainView.HSplitTop(EyeButtonSize, &Eyes, &MainView);
		Eyes.VSplitRight(EyeButtonSize * (float)NUM_EMOTES + 5.0f * (float)(NUM_EMOTES - 1), nullptr, &Eyes);
	}
	else
	{
		YourSkin.VSplitRight(3 * EyeButtonSize + 2 * 5.0f, &YourSkin, &Eyes);
		const float RemainderWidth = YourSkin.w;
		YourSkin.VSplitLeft(RemainderWidth * 0.4f, &YourSkin, &Checkboxes);
		Checkboxes.VSplitLeft(RemainderWidth * 0.35f, &Checkboxes, &SkinPrefix);
		SkinPrefix.VSplitRight(20.0f, &SkinPrefix, nullptr);
	}
	YourSkin.VSplitRight(20.0f, &YourSkin, nullptr);
	Checkboxes.VSplitRight(20.0f, &Checkboxes, nullptr);

	// Checkboxes
	bool ShouldRefresh = false;
	Checkboxes.HSplitTop(20.0f, &Button, &Checkboxes);
	if(DoButton_CheckBox(&g_Config.m_ClDownloadSkins, Localize("Download skins"), g_Config.m_ClDownloadSkins, &Button))
	{
		g_Config.m_ClDownloadSkins ^= 1;
		ShouldRefresh = true;
	}

	Checkboxes.HSplitTop(20.0f, &Button, &Checkboxes);
	if(DoButton_CheckBox(&g_Config.m_ClDownloadCommunitySkins, Localize("Download community skins"), g_Config.m_ClDownloadCommunitySkins, &Button))
	{
		g_Config.m_ClDownloadCommunitySkins ^= 1;
		ShouldRefresh = true;
	}

	Checkboxes.HSplitTop(20.0f, &Button, &Checkboxes);
	if(DoButton_CheckBox(&g_Config.m_ClVanillaSkinsOnly, Localize("Vanilla skins only"), g_Config.m_ClVanillaSkinsOnly, &Button))
	{
		g_Config.m_ClVanillaSkinsOnly ^= 1;
		ShouldRefresh = true;
	}

	Checkboxes.HSplitTop(20.0f, &Button, &Checkboxes);
	if(DoButton_CheckBox(&g_Config.m_ClFatSkins, Localize("Fat skins (DDFat)"), g_Config.m_ClFatSkins, &Button))
	{
		g_Config.m_ClFatSkins ^= 1;
	}

	// Skin prefix
	{
		SkinPrefix.HSplitTop(20.0f, &Label, &SkinPrefix);
		Ui()->DoLabel(&Label, Localize("Skin prefix"), 14.0f, TEXTALIGN_ML);

		SkinPrefix.HSplitTop(20.0f, &Button, &SkinPrefix);
		static CLineInput s_SkinPrefixInput(g_Config.m_ClSkinPrefix, sizeof(g_Config.m_ClSkinPrefix));
		if(Ui()->DoClearableEditBox(&s_SkinPrefixInput, &Button, 14.0f))
		{
			ShouldRefresh = true;
		}

		SkinPrefix.HSplitTop(2.0f, nullptr, &SkinPrefix);

		static const char *s_apSkinPrefixes[] = {"kitty", "santa"};
		static CButtonContainer s_aPrefixButtons[std::size(s_apSkinPrefixes)];
		for(size_t i = 0; i < std::size(s_apSkinPrefixes); i++)
		{
			SkinPrefix.HSplitTop(20.0f, &Button, &SkinPrefix);
			Button.HMargin(2.0f, &Button);
			if(DoButton_Menu(&s_aPrefixButtons[i], s_apSkinPrefixes[i], 0, &Button))
			{
				str_copy(g_Config.m_ClSkinPrefix, s_apSkinPrefixes[i]);
				ShouldRefresh = true;
			}
		}
	}

	// Player skin area
	CUIRect CustomColorsButton, RandomSkinButton;
	YourSkin.HSplitTop(20.0f, &Label, &YourSkin);
	YourSkin.HSplitBottom(20.0f, &YourSkin, &CustomColorsButton);
	CustomColorsButton.VSplitRight(30.0f, &CustomColorsButton, &RandomSkinButton);
	CustomColorsButton.VSplitRight(20.0f, &CustomColorsButton, nullptr);
	YourSkin.VSplitLeft(65.0f, &YourSkin, &Button);
	Button.VSplitLeft(5.0f, nullptr, &Button);
	Button.HMargin((Button.h - 20.0f) / 2.0f, &Button);

	char aBuf[128 + IO_MAX_PATH_LENGTH];
	str_format(aBuf, sizeof(aBuf), "%s:", Localize("Your skin"));
	Ui()->DoLabel(&Label, aBuf, 14.0f, TEXTALIGN_ML);

	CSkins::CSkinList &SkinList = GameClient()->m_Skins.SkinList();
	const CSkin *pDefaultSkin = GameClient()->m_Skins.Find("default");
	const CSkins::CSkinContainer *pOwnSkinContainer = GameClient()->m_Skins.FindContainerOrNullptr(pSkinName[0] == '\0' ? "default" : pSkinName);
	if(pOwnSkinContainer != nullptr && pOwnSkinContainer->IsSpecial())
	{
		pOwnSkinContainer = nullptr; // Special skins cannot be selected, show as missing due to invalid name
	}

	CTeeRenderInfo OwnSkinInfo;
	OwnSkinInfo.Apply(pOwnSkinContainer == nullptr || pOwnSkinContainer->Skin() == nullptr ? pDefaultSkin : pOwnSkinContainer->Skin().get());
	OwnSkinInfo.ApplyColors(*pUseCustomColor, *pColorBody, *pColorFeet);
	OwnSkinInfo.m_Size = 50.0f;

	// Tee
	{
		vec2 OffsetToMid;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &OwnSkinInfo, OffsetToMid);
		const vec2 TeeRenderPos = vec2(YourSkin.x + YourSkin.w / 2.0f, YourSkin.y + YourSkin.h / 2.0f + OffsetToMid.y);
		// tee looking towards cursor, and it is happy when you touch it
		const vec2 DeltaPosition = Ui()->MousePos() - TeeRenderPos;
		const float Distance = length(DeltaPosition);
		const float InteractionDistance = 20.0f;
		const vec2 TeeDirection = Distance < InteractionDistance ? normalize(vec2(DeltaPosition.x, std::max(DeltaPosition.y, 0.5f))) : normalize(DeltaPosition);
		const int TeeEmote = Distance < InteractionDistance ? EMOTE_HAPPY : *pEmote;
		RenderTools()->RenderTee(CAnimState::GetIdle(), &OwnSkinInfo, TeeEmote, TeeDirection, TeeRenderPos);
	}

	// Skin loading status
	const auto &&RenderSkinStatus = [&](CUIRect Parent, const CSkins::CSkinContainer *pSkinContainer, const void *pStatusTooltipId) {
		if(pSkinContainer != nullptr && pSkinContainer->State() == CSkins::CSkinContainer::EState::LOADED)
		{
			return;
		}

		CUIRect StatusIcon;
		Parent.HSplitTop(20.0f, &StatusIcon, nullptr);
		StatusIcon.VSplitLeft(20.0f, &StatusIcon, nullptr);

		if(pSkinContainer != nullptr &&
			(pSkinContainer->State() == CSkins::CSkinContainer::EState::UNLOADED ||
				pSkinContainer->State() == CSkins::CSkinContainer::EState::PENDING ||
				pSkinContainer->State() == CSkins::CSkinContainer::EState::LOADING))
		{
			Ui()->RenderProgressSpinner(StatusIcon.Center(), 5.0f);
		}
		else
		{
			TextRender()->TextColor(ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f));
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
			Ui()->DoLabel(&StatusIcon, pSkinContainer == nullptr || pSkinContainer->State() == CSkins::CSkinContainer::EState::ERROR ? FontIcon::TRIANGLE_EXCLAMATION : FontIcon::QUESTION, 12.0f, TEXTALIGN_MC);
			TextRender()->SetRenderFlags(0);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			Ui()->DoButtonLogic(pStatusTooltipId, 0, &StatusIcon, BUTTONFLAG_NONE);
			const char *pErrorTooltip;
			if(pSkinContainer == nullptr)
			{
				pErrorTooltip = Localize("This skin name cannot be used.");
			}
			else if(pSkinContainer->State() == CSkins::CSkinContainer::EState::ERROR)
			{
				pErrorTooltip = Localize("Skin could not be loaded due to an error. Check the local console for details.");
			}
			else
			{
				pErrorTooltip = Localize("Skin could not be found.");
			}
			GameClient()->m_Tooltips.DoToolTip(pStatusTooltipId, &StatusIcon, pErrorTooltip);
		}
	};
	static char s_StatusTooltipId;
	RenderSkinStatus(YourSkin, pOwnSkinContainer, &s_StatusTooltipId);

	// Skin name
	static CLineInput s_SkinInput;
	s_SkinInput.SetBuffer(pSkinName, SkinNameSize);
	s_SkinInput.SetEmptyText("default");
	if(Ui()->DoClearableEditBox(&s_SkinInput, &Button, 14.0f))
	{
		SetNeedSendInfo();
		m_SkinListScrollToSelected = true;
		SkinList.ForceRefresh();
	}

	// Random skin button
	static CButtonContainer s_RandomSkinButton;
	static const char *s_apDice[] = {FontIcon::DICE_ONE, FontIcon::DICE_TWO, FontIcon::DICE_THREE, FontIcon::DICE_FOUR, FontIcon::DICE_FIVE, FontIcon::DICE_SIX};
	static int s_CurrentDie = rand() % std::size(s_apDice);
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	if(DoButton_Menu(&s_RandomSkinButton, s_apDice[s_CurrentDie], 0, &RandomSkinButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, -0.2f))
	{
		GameClient()->m_Skins.RandomizeSkin(m_Dummy);
		SetNeedSendInfo();
		m_SkinListScrollToSelected = true;
		s_CurrentDie = rand() % std::size(s_apDice);
	}
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	GameClient()->m_Tooltips.DoToolTip(&s_RandomSkinButton, &RandomSkinButton, Localize("Create a random skin"));

	// Custom colors button
	if(DoButton_CheckBox(pUseCustomColor, Localize("Custom colors"), *pUseCustomColor, &CustomColorsButton))
	{
		*pUseCustomColor = *pUseCustomColor ? 0 : 1;
		SetNeedSendInfo();
	}

	// Default eyes
	{
		CTeeRenderInfo EyeSkinInfo = OwnSkinInfo;
		EyeSkinInfo.m_Size = EyeButtonSize;
		vec2 OffsetToMid;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &EyeSkinInfo, OffsetToMid);

		CUIRect EyesRow;
		Eyes.HSplitTop(EyeButtonSize, &EyesRow, &Eyes);
		static CButtonContainer s_aEyeButtons[NUM_EMOTES];
		for(int CurrentEyeEmote = 0; CurrentEyeEmote < NUM_EMOTES; CurrentEyeEmote++)
		{
			EyesRow.VSplitLeft(EyeButtonSize, &Button, &EyesRow);
			EyesRow.VSplitLeft(5.0f, nullptr, &EyesRow);
			if(!RenderEyesBelow && (CurrentEyeEmote + 1) % 3 == 0)
			{
				Eyes.HSplitTop(5.0f, nullptr, &Eyes);
				Eyes.HSplitTop(EyeButtonSize, &EyesRow, &Eyes);
			}

			const ColorRGBA EyeButtonColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f + (*pEmote == CurrentEyeEmote ? 0.25f : 0.0f));
			if(DoButton_Menu(&s_aEyeButtons[CurrentEyeEmote], "", 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, EyeButtonColor))
			{
				*pEmote = CurrentEyeEmote;
				if((int)m_Dummy == g_Config.m_ClDummy)
					GameClient()->m_Emoticon.EyeEmote(CurrentEyeEmote);
			}
			GameClient()->m_Tooltips.DoToolTip(&s_aEyeButtons[CurrentEyeEmote], &Button, Localize("Choose default eyes when joining a server"));
			RenderTools()->RenderTee(CAnimState::GetIdle(), &EyeSkinInfo, CurrentEyeEmote, vec2(1.0f, 0.0f), vec2(Button.x + Button.w / 2.0f, Button.y + Button.h / 2.0f + OffsetToMid.y));
		}
	}

	// Custom color pickers
	MainView.HSplitTop(5.0f, nullptr, &MainView);
	if(*pUseCustomColor)
	{
		CUIRect CustomColors;
		MainView.HSplitTop(95.0f, &CustomColors, &MainView);
		CUIRect aRects[2];
		CustomColors.VSplitMid(&aRects[0], &aRects[1], 20.0f);

		unsigned *apColors[] = {pColorBody, pColorFeet};
		const char *apParts[] = {Localize("Body"), Localize("Feet")};

		for(int i = 0; i < 2; i++)
		{
			aRects[i].HSplitTop(20.0f, &Label, &aRects[i]);
			Ui()->DoLabel(&Label, apParts[i], 14.0f, TEXTALIGN_ML);
			if(RenderHslaScrollbars(&aRects[i], apColors[i], false, ColorHSLA::DARKEST_LGT))
			{
				SetNeedSendInfo();
			}
		}
	}
	MainView.HSplitTop(5.0f, nullptr, &MainView);

	// Layout bottom controls and use remainder for skin selector
	CUIRect QuickSearch, DatabaseButton, DirectoryButton, RefreshButton;
	MainView.HSplitBottom(20.0f, &MainView, &QuickSearch);
	MainView.HSplitBottom(5.0f, &MainView, nullptr);
	QuickSearch.VSplitLeft(220.0f, &QuickSearch, &DatabaseButton);
	DatabaseButton.VSplitLeft(10.0f, nullptr, &DatabaseButton);
	DatabaseButton.VSplitLeft(150.0f, &DatabaseButton, &DirectoryButton);
	DirectoryButton.VSplitRight(175.0f, nullptr, &DirectoryButton);
	DirectoryButton.VSplitRight(25.0f, &DirectoryButton, &RefreshButton);
	DirectoryButton.VSplitRight(10.0f, &DirectoryButton, nullptr);

	// Skin selector
	static CListBox s_ListBox;
	std::vector<CSkins::CSkinListEntry> &vSkinList = SkinList.Skins();
	int OldSelected = -1;
	s_ListBox.DoStart(50.0f, vSkinList.size(), 4, 2, OldSelected, &MainView);
	for(size_t i = 0; i < vSkinList.size(); ++i)
	{
		CSkins::CSkinListEntry &SkinListEntry = vSkinList[i];
		const CSkins::CSkinContainer *pSkinContainer = vSkinList[i].SkinContainer();

		if(!m_Dummy ? SkinListEntry.IsSelectedMain() : SkinListEntry.IsSelectedDummy())
		{
			OldSelected = i;
			if(m_SkinListScrollToSelected)
			{
				s_ListBox.ScrollToSelected();
				m_SkinListScrollToSelected = false;
			}
		}

		const CListboxItem Item = s_ListBox.DoNextItem(SkinListEntry.ListItemId(), OldSelected >= 0 && (size_t)OldSelected == i);
		if(!Item.m_Visible)
		{
			continue;
		}

		SkinListEntry.RequestLoad();
		const CSkin *pSkin = pSkinContainer->State() == CSkins::CSkinContainer::EState::LOADED ? pSkinContainer->Skin().get() : pDefaultSkin;

		Item.m_Rect.VSplitLeft(60.0f, &Button, &Label);

		{
			CTeeRenderInfo Info = OwnSkinInfo;
			Info.Apply(pSkin);
			vec2 OffsetToMid;
			CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &Info, OffsetToMid);
			const vec2 TeeRenderPos = vec2(Button.x + Button.w / 2.0f, Button.y + Button.h / 2 + OffsetToMid.y);
			RenderTools()->RenderTee(CAnimState::GetIdle(), &Info, *pEmote, vec2(1.0f, 0.0f), TeeRenderPos);
		}

		{
			SLabelProperties Props;
			Props.m_MaxWidth = Label.w - 5.0f;
			const auto &NameMatch = SkinListEntry.NameMatch();
			if(NameMatch.has_value())
			{
				const auto [MatchStart, MatchLength] = NameMatch.value();
				Props.m_vColorSplits.emplace_back(MatchStart, MatchLength, ColorRGBA(0.4f, 0.4f, 1.0f, 1.0f));
			}
			Ui()->DoLabel(&Label, pSkinContainer->Name(), 12.0f, TEXTALIGN_ML, Props);
		}

		if(g_Config.m_Debug)
		{
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(*pUseCustomColor ? color_cast<ColorRGBA>(ColorHSLA(*pColorBody).UnclampLighting(ColorHSLA::DARKEST_LGT)) : pSkin->m_BloodColor);
			IGraphics::CQuadItem QuadItem(Label.x, Label.y, 12.0f, 12.0f);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsEnd();
		}

		// render skin favorite icon
		{
			CUIRect FavIcon;
			Item.m_Rect.HSplitTop(20.0f, &FavIcon, nullptr);
			FavIcon.VSplitRight(20.0f, nullptr, &FavIcon);
			if(DoButton_Favorite(SkinListEntry.FavoriteButtonId(), SkinListEntry.ListItemId(), SkinListEntry.IsFavorite(), &FavIcon))
			{
				if(SkinListEntry.IsFavorite())
				{
					GameClient()->m_Skins.RemoveFavorite(pSkinContainer->Name());
				}
				else
				{
					GameClient()->m_Skins.AddFavorite(pSkinContainer->Name());
				}
			}
		}

		RenderSkinStatus(Item.m_Rect, pSkinContainer, SkinListEntry.ErrorTooltipId());
	}

	const int NewSelected = s_ListBox.DoEnd();
	if(OldSelected != NewSelected)
	{
		str_copy(pSkinName, vSkinList[NewSelected].SkinContainer()->Name(), SkinNameSize);
		SkinList.ForceRefresh();
		SetNeedSendInfo();
	}

	static CLineInput s_SkinFilterInput(g_Config.m_ClSkinFilterString, sizeof(g_Config.m_ClSkinFilterString));
	if(SkinList.UnfilteredCount() > 0 && vSkinList.empty())
	{
		CUIRect FilterLabel, ResetButton;
		MainView.HMargin((MainView.h - (16.0f + 18.0f + 8.0f)) / 2.0f, &FilterLabel);
		FilterLabel.HSplitTop(16.0f, &FilterLabel, &ResetButton);
		ResetButton.HSplitTop(8.0f, nullptr, &ResetButton);
		ResetButton.VMargin((ResetButton.w - 200.0f) / 2.0f, &ResetButton);
		Ui()->DoLabel(&FilterLabel, Localize("No skins match your filter criteria"), 16.0f, TEXTALIGN_MC);
		static CButtonContainer s_ResetButton;
		if(DoButton_Menu(&s_ResetButton, Localize("Reset filter"), 0, &ResetButton))
		{
			s_SkinFilterInput.Clear();
			SkinList.ForceRefresh();
		}
	}

	if(Ui()->DoEditBox_Search(&s_SkinFilterInput, &QuickSearch, 14.0f, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive()))
	{
		SkinList.ForceRefresh();
	}

	static CButtonContainer s_SkinDatabaseButton;
	if(DoButton_Menu(&s_SkinDatabaseButton, Localize("Skin Database"), 0, &DatabaseButton))
	{
		Client()->ViewLink("https://ddnet.org/skins/");
	}

	static CButtonContainer s_DirectoryButton;
	if(DoButton_Menu(&s_DirectoryButton, Localize("Skins directory"), 0, &DirectoryButton))
	{
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, "skins", aBuf, sizeof(aBuf));
		Storage()->CreateFolder("skins", IStorage::TYPE_SAVE);
		Client()->ViewFile(aBuf);
	}
	GameClient()->m_Tooltips.DoToolTip(&s_DirectoryButton, &DirectoryButton, Localize("Open the directory to add custom skins"));

	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	static CButtonContainer s_SkinRefreshButton;
	if(DoButton_Menu(&s_SkinRefreshButton, FontIcon::ARROW_ROTATE_RIGHT, 0, &RefreshButton) || Input()->KeyPress(KEY_F5) || (Input()->KeyPress(KEY_R) && Input()->ModifierIsPressed()))
	{
		ShouldRefresh = true;
	}
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

	if(ShouldRefresh)
	{
		GameClient()->RefreshSkins(CSkinDescriptor::FLAG_SIX);
	}
}



/* Settings section: tee7 */
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus.h"

#include <base/dbg.h>
#include <base/str.h>

#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/animstate.h>
#include <game/client/components/console.h>
#include <game/client/components/skins7.h>
#include <game/client/components/sounds.h>
#include <game/client/components/tooltips.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>

#include <algorithm>
#include <vector>

void CMenus::RenderSettingsTee7(CUIRect MainView)
{
	CUIRect SkinPreview, NormalSkinPreview, RedTeamSkinPreview, BlueTeamSkinPreview, Buttons, QuickSearch, DirectoryButton, RefreshButton, SaveDeleteButton, TabBars, TabBar, LeftTab, RightTab;
	MainView.HSplitBottom(20.0f, &MainView, &Buttons);
	MainView.HSplitBottom(5.0f, &MainView, nullptr);
	Buttons.VSplitRight(25.0f, &Buttons, &RefreshButton);
	Buttons.VSplitRight(10.0f, &Buttons, nullptr);
	Buttons.VSplitRight(140.0f, &Buttons, &DirectoryButton);
	Buttons.VSplitLeft(220.0f, &QuickSearch, &Buttons);
	Buttons.VSplitLeft(10.0f, nullptr, &Buttons);
	Buttons.VSplitLeft(120.0f, &SaveDeleteButton, &Buttons);
	MainView.HSplitTop(50.0f, &TabBars, &MainView);
	MainView.HSplitTop(10.0f, nullptr, &MainView);
	TabBars.VSplitMid(&TabBars, &SkinPreview, 20.0f);

	TabBars.HSplitTop(20.0f, &TabBar, &TabBars);
	TabBar.VSplitMid(&LeftTab, &RightTab);
	TabBars.HSplitTop(10.0f, nullptr, &TabBars);

	SkinPreview.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 5.0f);
	SkinPreview.VMargin(10.0f, &SkinPreview);
	SkinPreview.VSplitRight(50.0f, &SkinPreview, &BlueTeamSkinPreview);
	SkinPreview.VSplitRight(10.0f, &SkinPreview, nullptr);
	SkinPreview.VSplitRight(50.0f, &SkinPreview, &RedTeamSkinPreview);
	SkinPreview.VSplitRight(10.0f, &SkinPreview, nullptr);
	SkinPreview.VSplitRight(50.0f, &SkinPreview, &NormalSkinPreview);
	SkinPreview.VSplitRight(10.0f, &SkinPreview, nullptr);

	static CButtonContainer s_PlayerTabButton;
	if(DoButton_MenuTab(&s_PlayerTabButton, Localize("Player"), !m_Dummy, &LeftTab, IGraphics::CORNER_L, nullptr, nullptr, nullptr, nullptr, 4.0f))
	{
		m_Dummy = false;
	}

	static CButtonContainer s_DummyTabButton;
	if(DoButton_MenuTab(&s_DummyTabButton, Localize("Dummy"), m_Dummy, &RightTab, IGraphics::CORNER_R, nullptr, nullptr, nullptr, nullptr, 4.0f))
	{
		m_Dummy = true;
	}

	TabBars.HSplitTop(20.0f, &TabBar, &TabBars);
	TabBar.VSplitMid(&LeftTab, &RightTab);

	static CButtonContainer s_BasicTabButton;
	if(DoButton_MenuTab(&s_BasicTabButton, Localize("Basic"), !m_CustomSkinMenu, &LeftTab, IGraphics::CORNER_L, nullptr, nullptr, nullptr, nullptr, 4.0f))
	{
		m_CustomSkinMenu = false;
	}

	static CButtonContainer s_CustomTabButton;
	if(DoButton_MenuTab(&s_CustomTabButton, Localize("Custom"), m_CustomSkinMenu, &RightTab, IGraphics::CORNER_R, nullptr, nullptr, nullptr, nullptr, 4.0f))
	{
		m_CustomSkinMenu = true;
		if(m_CustomSkinMenu && m_pSelectedSkin)
		{
			if(m_pSelectedSkin->m_Flags & CSkins7::SKINFLAG_STANDARD)
			{
				m_SkinNameInput.Set("copy_");
				m_SkinNameInput.Append(m_pSelectedSkin->m_aName);
			}
			else
				m_SkinNameInput.Set(m_pSelectedSkin->m_aName);
		}
	}

	// validate skin parts for solo mode
	char aSkinParts[protocol7::NUM_SKINPARTS][protocol7::MAX_SKIN_ARRAY_SIZE];
	char *apSkinPartsPtr[protocol7::NUM_SKINPARTS];
	int aUCCVars[protocol7::NUM_SKINPARTS];
	int aColorVars[protocol7::NUM_SKINPARTS];
	for(int Part = 0; Part < protocol7::NUM_SKINPARTS; Part++)
	{
		str_copy(aSkinParts[Part], CSkins7::ms_apSkinVariables[(int)m_Dummy][Part], protocol7::MAX_SKIN_ARRAY_SIZE);
		apSkinPartsPtr[Part] = aSkinParts[Part];
		aUCCVars[Part] = *CSkins7::ms_apUCCVariables[(int)m_Dummy][Part];
		aColorVars[Part] = *CSkins7::ms_apColorVariables[(int)m_Dummy][Part];
	}
	GameClient()->m_Skins7.ValidateSkinParts(apSkinPartsPtr, aUCCVars, aColorVars, 0);

	CTeeRenderInfo OwnSkinInfo;
	OwnSkinInfo.m_Size = 50.0f;
	for(int Part = 0; Part < protocol7::NUM_SKINPARTS; Part++)
	{
		GameClient()->m_Skins7.FindSkinPart(Part, apSkinPartsPtr[Part], false)->ApplyTo(OwnSkinInfo.m_aSixup[g_Config.m_ClDummy]);
		GameClient()->m_Skins7.ApplyColorTo(OwnSkinInfo.m_aSixup[g_Config.m_ClDummy], aUCCVars[Part], aColorVars[Part], Part);
	}

	char aBuf[128 + IO_MAX_PATH_LENGTH];
	str_format(aBuf, sizeof(aBuf), "%s:", Localize("Your skin"));
	Ui()->DoLabel(&SkinPreview, aBuf, 14.0f, TEXTALIGN_ML);

	vec2 OffsetToMid;
	CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &OwnSkinInfo, OffsetToMid);
	{
		// interactive tee: tee looking towards cursor, and it is happy when you touch it
		const vec2 TeePosition = NormalSkinPreview.Center() + OffsetToMid;
		const vec2 DeltaPosition = Ui()->MousePos() - TeePosition;
		const float Distance = length(DeltaPosition);
		const float InteractionDistance = 20.0f;
		const vec2 TeeDirection = Distance < InteractionDistance ? normalize(vec2(DeltaPosition.x, std::max(DeltaPosition.y, 0.5f))) : normalize(DeltaPosition);
		const int TeeEmote = Distance < InteractionDistance ? EMOTE_HAPPY : EMOTE_NORMAL;
		RenderTools()->RenderTee(CAnimState::GetIdle(), &OwnSkinInfo, TeeEmote, TeeDirection, TeePosition);
		static char s_InteractiveTeeButtonId;
		if(Distance < InteractionDistance && Ui()->DoButtonLogic(&s_InteractiveTeeButtonId, 0, &NormalSkinPreview, BUTTONFLAG_LEFT))
		{
			GameClient()->m_Sounds.Play(CSounds::CHN_GUI, SOUND_PLAYER_SPAWN, 1.0f);
		}
	}

	// validate skin parts for team game mode
	for(int Part = 0; Part < protocol7::NUM_SKINPARTS; Part++)
	{
		str_copy(aSkinParts[Part], CSkins7::ms_apSkinVariables[(int)m_Dummy][Part], protocol7::MAX_SKIN_ARRAY_SIZE);
		apSkinPartsPtr[Part] = aSkinParts[Part];
		aUCCVars[Part] = *CSkins7::ms_apUCCVariables[(int)m_Dummy][Part];
		aColorVars[Part] = *CSkins7::ms_apColorVariables[(int)m_Dummy][Part];
	}
	GameClient()->m_Skins7.ValidateSkinParts(apSkinPartsPtr, aUCCVars, aColorVars, GAMEFLAG_TEAMS);

	CTeeRenderInfo TeamSkinInfo;
	TeamSkinInfo.m_Size = OwnSkinInfo.m_Size;
	for(int Part = 0; Part < protocol7::NUM_SKINPARTS; Part++)
	{
		GameClient()->m_Skins7.FindSkinPart(Part, apSkinPartsPtr[Part], false)->ApplyTo(TeamSkinInfo.m_aSixup[g_Config.m_ClDummy]);
		TeamSkinInfo.m_aSixup[g_Config.m_ClDummy].m_aUseCustomColors[Part] = aUCCVars[Part];
	}

	for(int Part = 0; Part < protocol7::NUM_SKINPARTS; Part++)
	{
		TeamSkinInfo.m_aSixup[g_Config.m_ClDummy].m_aColors[Part] = GameClient()->m_Skins7.GetTeamColor(aUCCVars[Part], aColorVars[Part], TEAM_RED, Part);
	}
	RenderTools()->RenderTee(CAnimState::GetIdle(), &TeamSkinInfo, 0, vec2(1, 0), RedTeamSkinPreview.Center() + OffsetToMid);

	for(int Part = 0; Part < protocol7::NUM_SKINPARTS; Part++)
	{
		TeamSkinInfo.m_aSixup[g_Config.m_ClDummy].m_aColors[Part] = GameClient()->m_Skins7.GetTeamColor(aUCCVars[Part], aColorVars[Part], TEAM_BLUE, Part);
	}
	RenderTools()->RenderTee(CAnimState::GetIdle(), &TeamSkinInfo, 0, vec2(-1, 0), BlueTeamSkinPreview.Center() + OffsetToMid);

	if(m_CustomSkinMenu)
		RenderSettingsTeeCustom7(MainView);
	else
		RenderSkinSelection7(MainView);

	if(m_CustomSkinMenu)
	{
		static CButtonContainer s_CustomSkinSaveButton;
		if(DoButton_Menu(&s_CustomSkinSaveButton, Localize("Save"), 0, &SaveDeleteButton))
		{
			m_Popup = POPUP_SAVE_SKIN;
			m_SkinNameInput.SelectAll();
			Ui()->SetActiveItem(&m_SkinNameInput);
		}
	}
	else if(m_pSelectedSkin && (m_pSelectedSkin->m_Flags & CSkins7::SKINFLAG_STANDARD) == 0)
	{
		static CButtonContainer s_CustomSkinDeleteButton;
		if(DoButton_Menu(&s_CustomSkinDeleteButton, Localize("Delete"), 0, &SaveDeleteButton) || Ui()->ConsumeHotkey(CUi::HOTKEY_DELETE))
		{
			str_format(aBuf, sizeof(aBuf), Localize("Are you sure that you want to delete '%s'?"), m_pSelectedSkin->m_aName);
			PopupConfirm(Localize("Delete skin"), aBuf, Localize("Yes"), Localize("No"), &CMenus::PopupConfirmDeleteSkin7);
		}
	}

	static CLineInput s_SkinFilterInput(g_Config.m_ClSkinFilterString, sizeof(g_Config.m_ClSkinFilterString));
	if(Ui()->DoEditBox_Search(&s_SkinFilterInput, &QuickSearch, 14.0f, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive()))
	{
		m_SkinList7LastRefreshTime = std::nullopt;
		m_SkinPartsList7LastRefreshTime = std::nullopt;
	}

	static CButtonContainer s_DirectoryButton;
	if(DoButton_Menu(&s_DirectoryButton, Localize("Skins directory"), 0, &DirectoryButton))
	{
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, "skins7", aBuf, sizeof(aBuf));
		Storage()->CreateFolder("skins7", IStorage::TYPE_SAVE);
		Client()->ViewFile(aBuf);
	}
	GameClient()->m_Tooltips.DoToolTip(&s_DirectoryButton, &DirectoryButton, Localize("Open the directory to add custom skins"));

	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	static CButtonContainer s_SkinRefreshButton;
	if(DoButton_Menu(&s_SkinRefreshButton, FontIcon::ARROW_ROTATE_RIGHT, 0, &RefreshButton) ||
		(!Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive() && (Input()->KeyPress(KEY_F5) || (Input()->ModifierIsPressed() && Input()->KeyPress(KEY_R)))))
	{
		// reset render flags for possible loading screen
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		GameClient()->RefreshSkins(CSkinDescriptor::FLAG_SEVEN);
	}
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
}

void CMenus::PopupConfirmDeleteSkin7()
{
	dbg_assert(m_pSelectedSkin, "no skin selected for deletion");

	if(!GameClient()->m_Skins7.RemoveSkin(m_pSelectedSkin))
	{
		PopupMessage(Localize("Error"), Localize("Unable to delete skin"), Localize("Ok"));
		return;
	}
	m_pSelectedSkin = nullptr;
}

void CMenus::RenderSettingsTeeCustom7(CUIRect MainView)
{
	CUIRect ButtonBar, SkinPartSelection, CustomColors;

	MainView.HSplitTop(20.0f, &ButtonBar, &MainView);
	MainView.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_B, 5.0f);
	MainView.VSplitMid(&SkinPartSelection, &CustomColors, 10.0f);
	CustomColors.Margin(5.0f, &CustomColors);
	CUIRect CustomColorsButton, RandomSkinButton;
	CustomColors.HSplitTop(20.0f, &CustomColorsButton, &CustomColors);
	CustomColorsButton.VSplitRight(30.0f, &CustomColorsButton, &RandomSkinButton);
	CustomColorsButton.VSplitRight(20.0f, &CustomColorsButton, nullptr);

	const float ButtonWidth = ButtonBar.w / (float)protocol7::NUM_SKINPARTS;

	static CButtonContainer s_aSkinPartButtons[protocol7::NUM_SKINPARTS];
	for(int i = 0; i < protocol7::NUM_SKINPARTS; i++)
	{
		CUIRect Button;
		ButtonBar.VSplitLeft(ButtonWidth, &Button, &ButtonBar);
		const int Corners = i == 0 ? IGraphics::CORNER_TL : (i == (protocol7::NUM_SKINPARTS - 1) ? IGraphics::CORNER_TR : IGraphics::CORNER_NONE);
		if(DoButton_MenuTab(&s_aSkinPartButtons[i], Localize(CSkins7::ms_apSkinPartNamesLocalized[i], "skins"), m_TeePartSelected == i, &Button, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
		{
			m_TeePartSelected = i;
		}
	}

	RenderSkinPartSelection7(SkinPartSelection);

	int *pUseCustomColor = CSkins7::ms_apUCCVariables[(int)m_Dummy][m_TeePartSelected];
	if(DoButton_CheckBox(pUseCustomColor, Localize("Custom colors"), *pUseCustomColor, &CustomColorsButton))
	{
		*pUseCustomColor = !*pUseCustomColor;
		SetNeedSendInfo();
	}

	if(*pUseCustomColor)
	{
		CUIRect CustomColorScrollbars;
		CustomColors.HSplitTop(5.0f, nullptr, &CustomColors);
		CustomColors.HSplitTop(95.0f, &CustomColorScrollbars, &CustomColors);

		if(RenderHslaScrollbars(&CustomColorScrollbars, CSkins7::ms_apColorVariables[(int)m_Dummy][m_TeePartSelected], m_TeePartSelected == protocol7::SKINPART_MARKING, ColorHSLA::DARKEST_LGT7))
		{
			SetNeedSendInfo();
		}
	}

	// Random skin button
	static CButtonContainer s_RandomSkinButton;
	static const char *s_apDice[] = {FontIcon::DICE_ONE, FontIcon::DICE_TWO, FontIcon::DICE_THREE, FontIcon::DICE_FOUR, FontIcon::DICE_FIVE, FontIcon::DICE_SIX};
	static int s_CurrentDie = rand() % std::size(s_apDice);
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	if(DoButton_Menu(&s_RandomSkinButton, s_apDice[s_CurrentDie], 0, &RandomSkinButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, -0.2f))
	{
		GameClient()->m_Skins7.RandomizeSkin(m_Dummy);
		SetNeedSendInfo();
		s_CurrentDie = rand() % std::size(s_apDice);
	}
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	GameClient()->m_Tooltips.DoToolTip(&s_RandomSkinButton, &RandomSkinButton, Localize("Create a random skin"));
}

void CMenus::RenderSkinSelection7(CUIRect MainView)
{
	static float s_LastSelectionTime = -10.0f;
	static std::vector<const CSkins7::CSkin *> s_vpSkinList;
	static CListBox s_ListBox;

	if(!m_SkinList7LastRefreshTime.has_value() || m_SkinList7LastRefreshTime.value() != m_SkinList7LastRefreshTime)
	{
		s_vpSkinList.clear();
		for(const CSkins7::CSkin &Skin : GameClient()->m_Skins7.GetSkins())
		{
			if((Skin.m_Flags & CSkins7::SKINFLAG_SPECIAL) != 0)
				continue;
			if(g_Config.m_ClSkinFilterString[0] != '\0' && !str_utf8_find_nocase(Skin.m_aName, g_Config.m_ClSkinFilterString))
				continue;

			s_vpSkinList.emplace_back(&Skin);
		}
	}

	m_pSelectedSkin = nullptr;
	int OldSelected = -1;
	for(int i = 0; i < (int)s_vpSkinList.size(); ++i)
	{
		const CSkins7::CSkin *pSkin = s_vpSkinList[i];
		if(!str_comp(pSkin->m_aName, CSkins7::ms_apSkinNameVariables[m_Dummy]))
		{
			m_pSelectedSkin = pSkin;
			OldSelected = i;
			break;
		}
	}
	s_ListBox.DoStart(50.0f, s_vpSkinList.size(), 4, 1, OldSelected, &MainView);

	for(const CSkins7::CSkin *pSkin : s_vpSkinList)
	{
		const CListboxItem Item = s_ListBox.DoNextItem(pSkin);
		if(!Item.m_Visible)
			continue;

		CUIRect TeePreview, Label;
		Item.m_Rect.VSplitLeft(60.0f, &TeePreview, &Label);

		CTeeRenderInfo Info;
		for(int Part = 0; Part < protocol7::NUM_SKINPARTS; Part++)
		{
			pSkin->m_apParts[Part]->ApplyTo(Info.m_aSixup[g_Config.m_ClDummy]);
			GameClient()->m_Skins7.ApplyColorTo(Info.m_aSixup[g_Config.m_ClDummy], pSkin->m_aUseCustomColors[Part], pSkin->m_aPartColors[Part], Part);
		}
		Info.m_Size = 50.0f;

		{
			// interactive tee: tee is happy to be selected
			int TeeEmote = (Item.m_Selected && s_LastSelectionTime + 0.75f > Client()->GlobalTime()) ? EMOTE_HAPPY : EMOTE_NORMAL;
			vec2 OffsetToMid;
			CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &Info, OffsetToMid);
			RenderTools()->RenderTee(CAnimState::GetIdle(), &Info, TeeEmote, vec2(1.0f, 0.0f), TeePreview.Center() + OffsetToMid);
		}

		SLabelProperties Props;
		Props.m_MaxWidth = Label.w - 5.0f;
		Ui()->DoLabel(&Label, pSkin->m_aName, 12.0f, TEXTALIGN_ML, Props);
	}

	const int NewSelected = s_ListBox.DoEnd();
	if(NewSelected != -1 && NewSelected != OldSelected)
	{
		s_LastSelectionTime = Client()->GlobalTime();
		m_pSelectedSkin = s_vpSkinList[NewSelected];
		str_copy(CSkins7::ms_apSkinNameVariables[m_Dummy], m_pSelectedSkin->m_aName, protocol7::MAX_SKIN_ARRAY_SIZE);
		for(int Part = 0; Part < protocol7::NUM_SKINPARTS; Part++)
		{
			str_copy(CSkins7::ms_apSkinVariables[(int)m_Dummy][Part], m_pSelectedSkin->m_apParts[Part]->m_aName, protocol7::MAX_SKIN_ARRAY_SIZE);
			*CSkins7::ms_apUCCVariables[(int)m_Dummy][Part] = m_pSelectedSkin->m_aUseCustomColors[Part];
			*CSkins7::ms_apColorVariables[(int)m_Dummy][Part] = m_pSelectedSkin->m_aPartColors[Part];
		}
		SetNeedSendInfo();
	}
}

void CMenus::RenderSkinPartSelection7(CUIRect MainView)
{
	static std::vector<const CSkins7::CSkinPart *> s_avpList[protocol7::NUM_SKINPARTS];
	static CListBox s_ListBox;
	for(int Part = 0; Part < protocol7::NUM_SKINPARTS; Part++)
	{
		if(!m_SkinList7LastRefreshTime.has_value() || m_SkinList7LastRefreshTime.value() != GameClient()->m_Skins7.LastRefreshTime())
		{
			s_avpList[Part].clear();
			for(const CSkins7::CSkinPart &SkinPart : GameClient()->m_Skins7.GetSkinParts(Part))
			{
				if((SkinPart.m_Flags & CSkins7::SKINFLAG_SPECIAL) != 0)
					continue;

				if(g_Config.m_ClSkinFilterString[0] != '\0' && !str_utf8_find_nocase(SkinPart.m_aName, g_Config.m_ClSkinFilterString))
					continue;

				s_avpList[Part].emplace_back(&SkinPart);
			}
		}
	}

	int OldSelected = -1;
	for(int i = 0; i < (int)s_avpList[m_TeePartSelected].size(); ++i)
	{
		const CSkins7::CSkinPart *pPart = s_avpList[m_TeePartSelected][i];
		if(!str_comp(pPart->m_aName, CSkins7::ms_apSkinVariables[(int)m_Dummy][m_TeePartSelected]))
		{
			OldSelected = i;
			break;
		}
	}
	s_ListBox.DoStart(72.0f, s_avpList[m_TeePartSelected].size(), 4, 1, OldSelected, &MainView, false, IGraphics::CORNER_NONE, true);

	for(const CSkins7::CSkinPart *pPart : s_avpList[m_TeePartSelected])
	{
		CListboxItem Item = s_ListBox.DoNextItem(pPart);
		if(!Item.m_Visible)
			continue;

		CUIRect Label;
		Item.m_Rect.Margin(5.0f, &Item.m_Rect);
		Item.m_Rect.HSplitBottom(12.0f, &Item.m_Rect, &Label);

		CTeeRenderInfo Info;
		for(int Part = 0; Part < protocol7::NUM_SKINPARTS; Part++)
		{
			const CSkins7::CSkinPart *pPreviewPart = (m_TeePartSelected == Part ? pPart : GameClient()->m_Skins7.FindSkinPart(Part, CSkins7::ms_apSkinVariables[(int)m_Dummy][Part], false));
			pPreviewPart->ApplyTo(Info.m_aSixup[g_Config.m_ClDummy]);
			GameClient()->m_Skins7.ApplyColorTo(Info.m_aSixup[g_Config.m_ClDummy], *CSkins7::ms_apUCCVariables[(int)m_Dummy][Part], *CSkins7::ms_apColorVariables[(int)m_Dummy][Part], Part);
		}
		Info.m_Size = 50.0f;

		vec2 OffsetToMid;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &Info, OffsetToMid);
		const vec2 TeePos = Item.m_Rect.Center() + OffsetToMid;
		if(m_TeePartSelected == protocol7::SKINPART_HANDS)
		{
			// RenderTools()->RenderTeeHand(&Info, TeePos, vec2(1.0f, 0.0f), -pi*0.5f, vec2(18, 0));
		}
		int TeePartEmote = EMOTE_NORMAL;
		if(m_TeePartSelected == protocol7::SKINPART_EYES)
		{
			TeePartEmote = (int)(Client()->GlobalTime() * 0.5f) % NUM_EMOTES;
		}
		RenderTools()->RenderTee(CAnimState::GetIdle(), &Info, TeePartEmote, vec2(1.0f, 0.0f), TeePos);

		Ui()->DoLabel(&Label, pPart->m_aName, 12.0f, TEXTALIGN_MC);
	}

	const int NewSelected = s_ListBox.DoEnd();
	if(NewSelected != -1 && NewSelected != OldSelected)
	{
		str_copy(CSkins7::ms_apSkinVariables[(int)m_Dummy][m_TeePartSelected], s_avpList[m_TeePartSelected][NewSelected]->m_aName, protocol7::MAX_SKIN_ARRAY_SIZE);
		CSkins7::ms_apSkinNameVariables[m_Dummy][0] = '\0';
		SetNeedSendInfo();
	}
}



/* Settings section: appearance */
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus.h"

#include <base/str.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/client_data.h>

#include <game/client/animstate.h>
#include <game/client/components/chat.h>
#include <game/client/components/effects.h>
#include <game/client/components/items.h>
#include <game/client/components/mapimages.h>
#include <game/client/components/nameplates.h>
#include <game/client/components/skins.h>
#include <game/client/components/tooltips.h>
#include <game/client/gameclient.h>
#include <game/client/skin.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

enum
{
	APPEARANCE_TAB_HUD = 0,
	APPEARANCE_TAB_CHAT = 1,
	APPEARANCE_TAB_NAME_PLATE = 2,
	APPEARANCE_TAB_HOOK_COLLISION = 3,
	APPEARANCE_TAB_INFO_MESSAGES = 4,
	APPEARANCE_TAB_LASER = 5,
	NUMBER_OF_APPEARANCE_TABS = 6,
};

void CMenus::RenderSettingsAppearance(CUIRect MainView)
{
	char aBuf[128];
	static int s_CurTab = 0;

	CUIRect TabBar, LeftView, RightView, Button;

	MainView.HSplitTop(20.0f, &TabBar, &MainView);
	const float TabWidth = TabBar.w / (float)NUMBER_OF_APPEARANCE_TABS;
	static CButtonContainer s_aPageTabs[NUMBER_OF_APPEARANCE_TABS] = {};
	const char *apTabNames[NUMBER_OF_APPEARANCE_TABS] = {
		Localize("HUD"),
		Localize("Chat"),
		Localize("Name Plate"),
		Localize("Hook Collisions"),
		Localize("Info Messages"),
		Localize("Laser")};

	for(int Tab = APPEARANCE_TAB_HUD; Tab < NUMBER_OF_APPEARANCE_TABS; ++Tab)
	{
		TabBar.VSplitLeft(TabWidth, &Button, &TabBar);
		const int Corners = Tab == APPEARANCE_TAB_HUD ? IGraphics::CORNER_L : (Tab == NUMBER_OF_APPEARANCE_TABS - 1 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
		if(DoButton_MenuTab(&s_aPageTabs[Tab], apTabNames[Tab], s_CurTab == Tab, &Button, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
		{
			s_CurTab = Tab;
		}
	}

	MainView.HSplitTop(10.0f, nullptr, &MainView);

	const float LineSize = 20.0f;
	const float ColorPickerLineSize = 25.0f;
	const float HeadlineFontSize = 20.0f;
	const float HeadlineHeight = 30.0f;
	const float MarginSmall = 5.0f;
	const float MarginBetweenViews = 20.0f;

	const float ColorPickerLabelSize = 13.0f;
	const float ColorPickerLineSpacing = 5.0f;

	if(s_CurTab == APPEARANCE_TAB_HUD)
	{
		MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);

		// ***** HUD ***** //
		Ui()->DoLabel_AutoLineSize(Localize("HUD"), HeadlineFontSize,
			TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

		// Switch of the entire HUD
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowhud, Localize("Show ingame HUD"), &g_Config.m_ClShowhud, &LeftView, LineSize);

		// Switches of the various normal HUD elements
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowhudHealthAmmo, Localize("Show health, shields and ammo"), &g_Config.m_ClShowhudHealthAmmo, &LeftView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowhudScore, Localize("Show score"), &g_Config.m_ClShowhudScore, &LeftView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowLocalTimeAlways, Localize("Show local time always"), &g_Config.m_ClShowLocalTimeAlways, &LeftView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClSpecCursor, Localize("Show spectator cursor"), &g_Config.m_ClSpecCursor, &LeftView, LineSize);

		// Settings of the HUD element for votes
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowVotesAfterVoting, Localize("Show votes window after voting"), &g_Config.m_ClShowVotesAfterVoting, &LeftView, LineSize);

		// ***** Scoreboard ***** //
		LeftView.HSplitTop(MarginBetweenViews, nullptr, &LeftView);
		Ui()->DoLabel_AutoLineSize(Localize("Scoreboard"), HeadlineFontSize,
			TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

		ColorRGBA GreenDefault(0.78f, 1.0f, 0.8f, 1.0f);
		static CButtonContainer s_AuthedColor, s_SameClanColor;
		DoLine_ColorPicker(&s_AuthedColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Authed name color in scoreboard"), &g_Config.m_ClAuthedPlayerColor, GreenDefault, false);
		DoLine_ColorPicker(&s_SameClanColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Same clan color in scoreboard"), &g_Config.m_ClSameClanColor, GreenDefault, false);

		// ***** DDRace HUD ***** //
		Ui()->DoLabel_AutoLineSize(Localize("DDRace HUD"), HeadlineFontSize,
			TEXTALIGN_ML, &RightView, HeadlineHeight);
		RightView.HSplitTop(MarginSmall, nullptr, &RightView);

		// Switches of various DDRace HUD elements
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowIds, Localize("Show client IDs (scoreboard, chat, spectator)"), &g_Config.m_ClShowIds, &RightView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowhudDDRace, Localize("Show DDRace HUD"), &g_Config.m_ClShowhudDDRace, &RightView, LineSize);
		if(g_Config.m_ClShowhudDDRace)
		{
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowhudJumpsIndicator, Localize("Show jumps indicator"), &g_Config.m_ClShowhudJumpsIndicator, &RightView, LineSize);
		}
		else
		{
			RightView.HSplitTop(LineSize, nullptr, &RightView); // Create empty space for hidden option
		}

		// Eye with a number of spectators
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowhudSpectatorCount, Localize("Show number of spectators"), &g_Config.m_ClShowhudSpectatorCount, &RightView, LineSize);

		// Switch for dummy actions display
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowhudDummyActions, Localize("Show dummy actions"), &g_Config.m_ClShowhudDummyActions, &RightView, LineSize);

		// Player movement information display settings
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowhudPlayerPosition, Localize("Show player position"), &g_Config.m_ClShowhudPlayerPosition, &RightView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowhudPlayerSpeed, Localize("Show player speed"), &g_Config.m_ClShowhudPlayerSpeed, &RightView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowhudPlayerAngle, Localize("Show player target angle"), &g_Config.m_ClShowhudPlayerAngle, &RightView, LineSize);

		// Freeze bar settings
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowFreezeBars, Localize("Show freeze bars"), &g_Config.m_ClShowFreezeBars, &RightView, LineSize);
		RightView.HSplitTop(LineSize * 2.0f, &Button, &RightView);
		if(g_Config.m_ClShowFreezeBars)
		{
			Ui()->DoScrollbarOption(&g_Config.m_ClFreezeBarsAlphaInsideFreeze, &g_Config.m_ClFreezeBarsAlphaInsideFreeze, &Button, Localize("Opacity of freeze bars inside freeze"), 0, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE, "%");
		}
	}
	else if(s_CurTab == APPEARANCE_TAB_CHAT)
	{
		CChat &Chat = GameClient()->m_Chat;
		CUIRect TopView, PreviewView;
		MainView.HSplitBottom(220.0f, &TopView, &PreviewView);
		TopView.HSplitBottom(MarginBetweenViews, &TopView, nullptr);
		TopView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);

		// ***** Chat ***** //
		Ui()->DoLabel_AutoLineSize(Localize("Chat"), HeadlineFontSize,
			TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

		// General chat settings
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(DoButton_CheckBox(&g_Config.m_ClShowChat, Localize("Show chat"), g_Config.m_ClShowChat, &Button))
		{
			g_Config.m_ClShowChat = g_Config.m_ClShowChat ? 0 : 1;
		}
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(g_Config.m_ClShowChat)
		{
			static int s_ShowChat = 0;
			if(DoButton_CheckBox(&s_ShowChat, Localize("Always show chat"), g_Config.m_ClShowChat == 2, &Button))
				g_Config.m_ClShowChat = g_Config.m_ClShowChat != 2 ? 2 : 1;
		}

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClChatTeamColors, Localize("Show names in chat in team colors"), &g_Config.m_ClChatTeamColors, &LeftView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowChatFriends, Localize("Show only chat messages from friends"), &g_Config.m_ClShowChatFriends, &LeftView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClShowChatTeamMembersOnly, Localize("Show only chat messages from team members"), &g_Config.m_ClShowChatTeamMembersOnly, &LeftView, LineSize);

		if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClChatOld, Localize("Use old chat style"), &g_Config.m_ClChatOld, &LeftView, LineSize))
			GameClient()->m_Chat.RebuildChat();

		// DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClCensorChat, Localize("Censor profanity"), &g_Config.m_ClCensorChat, &LeftView, LineSize);

		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(Ui()->DoScrollbarOption(&g_Config.m_ClChatFontSize, &g_Config.m_ClChatFontSize, &Button, Localize("Chat font size"), 10, 100))
		{
			Chat.EnsureCoherentWidth();
			Chat.RebuildChat();
		}

		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(Ui()->DoScrollbarOption(&g_Config.m_ClChatWidth, &g_Config.m_ClChatWidth, &Button, Localize("Chat width"), 120, 400))
		{
			Chat.EnsureCoherentFontSize();
			Chat.RebuildChat();
		}

		static CButtonContainer s_BackgroundColor;
		DoLine_ColorPicker(&s_BackgroundColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Chat background color"), &g_Config.m_ClChatBackgroundColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::ClChatBackgroundColor, true)), false, nullptr, true);

		// ***** Messages ***** //
		Ui()->DoLabel_AutoLineSize(Localize("Messages"), HeadlineFontSize,
			TEXTALIGN_ML, &RightView, HeadlineHeight);
		RightView.HSplitTop(MarginSmall, nullptr, &RightView);

		// Message Colors and extra settings
		static CButtonContainer s_SystemMessageColor;
		DoLine_ColorPicker(&s_SystemMessageColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &RightView, Localize("System message"), &g_Config.m_ClMessageSystemColor, ColorRGBA(1.0f, 1.0f, 0.5f), true, &g_Config.m_ClShowChatSystem);
		static CButtonContainer s_HighlightedMessageColor;
		DoLine_ColorPicker(&s_HighlightedMessageColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &RightView, Localize("Highlighted message"), &g_Config.m_ClMessageHighlightColor, ColorRGBA(1.0f, 0.5f, 0.5f));
		static CButtonContainer s_TeamMessageColor;
		DoLine_ColorPicker(&s_TeamMessageColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &RightView, Localize("Team message"), &g_Config.m_ClMessageTeamColor, ColorRGBA(0.65f, 1.0f, 0.65f));
		static CButtonContainer s_FriendMessageColor;
		DoLine_ColorPicker(&s_FriendMessageColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &RightView, Localize("Friend message"), &g_Config.m_ClMessageFriendColor, ColorRGBA(1.0f, 0.137f, 0.137f), true, &g_Config.m_ClMessageFriend);
		static CButtonContainer s_NormalMessageColor;
		DoLine_ColorPicker(&s_NormalMessageColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &RightView, Localize("Normal message"), &g_Config.m_ClMessageColor, ColorRGBA(1.0f, 1.0f, 1.0f));

		str_format(aBuf, sizeof(aBuf), "%s (echo)", Localize("Client message"));
		static CButtonContainer s_ClientMessageColor;
		DoLine_ColorPicker(&s_ClientMessageColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &RightView, aBuf, &g_Config.m_ClMessageClientColor, ColorRGBA(0.5f, 0.78f, 1.0f));

		// ***** Chat Preview ***** //
		Ui()->DoLabel_AutoLineSize(Localize("Preview"), HeadlineFontSize,
			TEXTALIGN_ML, &PreviewView, HeadlineHeight);
		PreviewView.HSplitTop(MarginSmall, nullptr, &PreviewView);

		// Use the rest of the view for preview
		PreviewView.Draw(ColorRGBA(1, 1, 1, 0.1f), IGraphics::CORNER_ALL, 5.0f);
		PreviewView.Margin(MarginSmall, &PreviewView);

		ColorRGBA SystemColor = color_cast<ColorRGBA, ColorHSLA>(ColorHSLA(g_Config.m_ClMessageSystemColor));
		ColorRGBA HighlightedColor = color_cast<ColorRGBA, ColorHSLA>(ColorHSLA(g_Config.m_ClMessageHighlightColor));
		ColorRGBA TeamColor = color_cast<ColorRGBA, ColorHSLA>(ColorHSLA(g_Config.m_ClMessageTeamColor));
		ColorRGBA FriendColor = color_cast<ColorRGBA, ColorHSLA>(ColorHSLA(g_Config.m_ClMessageFriendColor));
		ColorRGBA NormalColor = color_cast<ColorRGBA, ColorHSLA>(ColorHSLA(g_Config.m_ClMessageColor));
		ColorRGBA ClientColor = color_cast<ColorRGBA, ColorHSLA>(ColorHSLA(g_Config.m_ClMessageClientColor));
		ColorRGBA DefaultNameColor(0.8f, 0.8f, 0.8f, 1.0f);

		const float RealFontSize = Chat.FontSize() * 2;
		const float RealMsgPaddingX = (!g_Config.m_ClChatOld ? Chat.MessagePaddingX() : 0) * 2;
		const float RealMsgPaddingY = (!g_Config.m_ClChatOld ? Chat.MessagePaddingY() : 0) * 2;
		const float RealMsgPaddingTee = (!g_Config.m_ClChatOld ? Chat.MessageTeeSize() + CChat::MESSAGE_TEE_PADDING_RIGHT : 0) * 2;
		const float RealOffsetY = RealFontSize + RealMsgPaddingY;

		const float X = RealMsgPaddingX / 2.0f + PreviewView.x;
		float Y = PreviewView.y;
		float LineWidth = g_Config.m_ClChatWidth * 2 - (RealMsgPaddingX * 1.5f) - RealMsgPaddingTee;

		str_copy(aBuf, Client()->PlayerName());

		const CAnimState *pIdleState = CAnimState::GetIdle();
		const float RealTeeSize = Chat.MessageTeeSize() * 2;
		const float RealTeeSizeHalved = Chat.MessageTeeSize();
		constexpr float TWSkinUnreliableOffset = -0.25f;
		const float OffsetTeeY = RealTeeSizeHalved;
		const float FullHeightMinusTee = RealOffsetY - RealTeeSize;

		struct SPreviewLine
		{
			int m_ClientId;
			bool m_Team;
			char m_aName[64];
			char m_aText[256];
			bool m_Friend;
			bool m_Player;
			bool m_Client;
			bool m_Highlighted;
			int m_TimesRepeated;

			CTeeRenderInfo m_RenderInfo;
		};

		static std::vector<SPreviewLine> s_vLines;

		enum ELineFlag
		{
			FLAG_TEAM = 1 << 0,
			FLAG_FRIEND = 1 << 1,
			FLAG_HIGHLIGHT = 1 << 2,
			FLAG_CLIENT = 1 << 3
		};
		enum
		{
			PREVIEW_SYS,
			PREVIEW_HIGHLIGHT,
			PREVIEW_TEAM,
			PREVIEW_FRIEND,
			PREVIEW_SPAMMER,
			PREVIEW_CLIENT
		};
		auto &&SetPreviewLine = [](int Index, int ClientId, const char *pName, const char *pText, int Flag, int Repeats) {
			SPreviewLine *pLine;
			if((int)s_vLines.size() <= Index)
			{
				s_vLines.emplace_back();
				pLine = &s_vLines.back();
			}
			else
			{
				pLine = &s_vLines[Index];
			}
			pLine->m_ClientId = ClientId;
			pLine->m_Team = Flag & FLAG_TEAM;
			pLine->m_Friend = Flag & FLAG_FRIEND;
			pLine->m_Player = ClientId >= 0;
			pLine->m_Highlighted = Flag & FLAG_HIGHLIGHT;
			pLine->m_Client = Flag & FLAG_CLIENT;
			pLine->m_TimesRepeated = Repeats;
			str_copy(pLine->m_aName, pName);
			str_copy(pLine->m_aText, pText);
		};
		auto &&SetLineSkin = [RealTeeSize](int Index, const CSkin *pSkin) {
			if(Index >= (int)s_vLines.size())
				return;
			s_vLines[Index].m_RenderInfo.m_Size = RealTeeSize;
			s_vLines[Index].m_RenderInfo.Apply(pSkin);
		};

		auto &&RenderPreview = [&](int LineIndex, int x, int y, bool Render = true) {
			if(LineIndex >= (int)s_vLines.size())
				return vec2(0, 0);
			CTextCursor LocalCursor;
			LocalCursor.SetPosition(vec2(x, y));
			LocalCursor.m_FontSize = RealFontSize;
			LocalCursor.m_Flags = Render ? TEXTFLAG_RENDER : 0;
			LocalCursor.m_LineWidth = LineWidth;
			const auto &Line = s_vLines[LineIndex];

			char aClientId[16] = "";
			if(g_Config.m_ClShowIds && Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
			{
				GameClient()->FormatClientId(Line.m_ClientId, aClientId, EClientIdFormat::INDENT_FORCE);
			}

			char aCount[12];
			if(Line.m_ClientId < 0)
				str_format(aCount, sizeof(aCount), "[%d] ", Line.m_TimesRepeated + 1);
			else
				str_format(aCount, sizeof(aCount), " [%d]", Line.m_TimesRepeated + 1);

			if(Line.m_Player)
			{
				LocalCursor.m_X += RealMsgPaddingTee;

				if(Line.m_Friend && g_Config.m_ClMessageFriend)
				{
					if(Render)
						TextRender()->TextColor(FriendColor);
					TextRender()->TextEx(&LocalCursor, "♥ ", -1);
				}
			}

			ColorRGBA NameColor;
			if(Line.m_Team)
				NameColor = CalculateNameColor(color_cast<ColorHSLA>(TeamColor));
			else if(Line.m_Player)
				NameColor = DefaultNameColor;
			else if(Line.m_Client)
				NameColor = ClientColor;
			else
				NameColor = SystemColor;

			if(Render)
				TextRender()->TextColor(NameColor);

			TextRender()->TextEx(&LocalCursor, aClientId);
			TextRender()->TextEx(&LocalCursor, Line.m_aName);

			if(Line.m_TimesRepeated > 0)
			{
				if(Render)
					TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.3f);
				TextRender()->TextEx(&LocalCursor, aCount, -1);
			}

			if(Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
			{
				if(Render)
					TextRender()->TextColor(NameColor);
				TextRender()->TextEx(&LocalCursor, ": ", -1);
			}

			CTextCursor AppendCursor = LocalCursor;
			AppendCursor.m_LongestLineWidth = 0.0f;
			if(!g_Config.m_ClChatOld)
			{
				AppendCursor.m_StartX = LocalCursor.m_X;
				AppendCursor.m_LineWidth -= LocalCursor.m_LongestLineWidth;
			}

			if(Render)
			{
				if(Line.m_Highlighted)
					TextRender()->TextColor(HighlightedColor);
				else if(Line.m_Team)
					TextRender()->TextColor(TeamColor);
				else if(Line.m_Player)
					TextRender()->TextColor(NormalColor);
			}

			TextRender()->TextEx(&AppendCursor, Line.m_aText, -1);
			if(Render)
				TextRender()->TextColor(TextRender()->DefaultTextColor());

			return vec2{LocalCursor.m_LongestLineWidth + AppendCursor.m_LongestLineWidth, AppendCursor.Height() + RealMsgPaddingY};
		};

		// Set preview lines
		{
			char aLineBuilder[128];

			str_format(aLineBuilder, sizeof(aLineBuilder), "'%s' entered and joined the game", aBuf);
			SetPreviewLine(PREVIEW_SYS, -1, "*** ", aLineBuilder, 0, 0);

			str_format(aLineBuilder, sizeof(aLineBuilder), "Hey, how are you %s?", aBuf);
			SetPreviewLine(PREVIEW_HIGHLIGHT, 7, "Random Tee", aLineBuilder, FLAG_HIGHLIGHT, 0);

			SetPreviewLine(PREVIEW_TEAM, 11, "Your Teammate", "Let's speedrun this!", FLAG_TEAM, 0);
			SetPreviewLine(PREVIEW_FRIEND, 8, "Friend", "Hello there", FLAG_FRIEND, 0);
			SetPreviewLine(PREVIEW_SPAMMER, 9, "Spammer", "Hey fools, I'm spamming here!", 0, 5);
			SetPreviewLine(PREVIEW_CLIENT, -1, "— ", "Echo command executed", FLAG_CLIENT, 0);
		}

		SetLineSkin(1, GameClient()->m_Skins.Find("pinky"));
		SetLineSkin(2, GameClient()->m_Skins.Find("default"));
		SetLineSkin(3, GameClient()->m_Skins.Find("cammostripes"));
		SetLineSkin(4, GameClient()->m_Skins.Find("beast"));

		// Backgrounds first
		if(!g_Config.m_ClChatOld)
		{
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClChatBackgroundColor, true)));

			float TempY = Y;
			const float RealBackgroundRounding = Chat.MessageRounding() * 2.0f;

			auto &&RenderMessageBackground = [&](int LineIndex) {
				auto Size = RenderPreview(LineIndex, 0, 0, false);
				Graphics()->DrawRectExt(X - RealMsgPaddingX / 2.0f, TempY - RealMsgPaddingY / 2.0f, Size.x + RealMsgPaddingX * 1.5f, Size.y, RealBackgroundRounding, IGraphics::CORNER_ALL);
				return Size.y;
			};

			if(g_Config.m_ClShowChatSystem)
			{
				TempY += RenderMessageBackground(PREVIEW_SYS);
			}

			if(!g_Config.m_ClShowChatFriends)
			{
				if(!g_Config.m_ClShowChatTeamMembersOnly)
					TempY += RenderMessageBackground(PREVIEW_HIGHLIGHT);
				TempY += RenderMessageBackground(PREVIEW_TEAM);
			}

			if(!g_Config.m_ClShowChatTeamMembersOnly)
				TempY += RenderMessageBackground(PREVIEW_FRIEND);

			if(!g_Config.m_ClShowChatFriends && !g_Config.m_ClShowChatTeamMembersOnly)
			{
				TempY += RenderMessageBackground(PREVIEW_SPAMMER);
			}

			TempY += RenderMessageBackground(PREVIEW_CLIENT);

			Graphics()->QuadsEnd();
		}

		// System
		if(g_Config.m_ClShowChatSystem)
		{
			Y += RenderPreview(PREVIEW_SYS, X, Y).y;
		}

		if(!g_Config.m_ClShowChatFriends)
		{
			// Highlighted
			if(!g_Config.m_ClChatOld && !g_Config.m_ClShowChatTeamMembersOnly)
				RenderTools()->RenderTee(pIdleState, &s_vLines[PREVIEW_HIGHLIGHT].m_RenderInfo, EMOTE_NORMAL, vec2(1, 0.1f), vec2(X + RealTeeSizeHalved, Y + OffsetTeeY + FullHeightMinusTee / 2.0f + TWSkinUnreliableOffset));
			if(!g_Config.m_ClShowChatTeamMembersOnly)
				Y += RenderPreview(PREVIEW_HIGHLIGHT, X, Y).y;

			// Team
			if(!g_Config.m_ClChatOld)
				RenderTools()->RenderTee(pIdleState, &s_vLines[PREVIEW_TEAM].m_RenderInfo, EMOTE_NORMAL, vec2(1, 0.1f), vec2(X + RealTeeSizeHalved, Y + OffsetTeeY + FullHeightMinusTee / 2.0f + TWSkinUnreliableOffset));
			Y += RenderPreview(PREVIEW_TEAM, X, Y).y;
		}

		// Friend
		if(!g_Config.m_ClChatOld && !g_Config.m_ClShowChatTeamMembersOnly)
			RenderTools()->RenderTee(pIdleState, &s_vLines[PREVIEW_FRIEND].m_RenderInfo, EMOTE_NORMAL, vec2(1, 0.1f), vec2(X + RealTeeSizeHalved, Y + OffsetTeeY + FullHeightMinusTee / 2.0f + TWSkinUnreliableOffset));
		if(!g_Config.m_ClShowChatTeamMembersOnly)
			Y += RenderPreview(PREVIEW_FRIEND, X, Y).y;

		// Normal
		if(!g_Config.m_ClShowChatFriends && !g_Config.m_ClShowChatTeamMembersOnly)
		{
			if(!g_Config.m_ClChatOld)
				RenderTools()->RenderTee(pIdleState, &s_vLines[PREVIEW_SPAMMER].m_RenderInfo, EMOTE_NORMAL, vec2(1, 0.1f), vec2(X + RealTeeSizeHalved, Y + OffsetTeeY + FullHeightMinusTee / 2.0f + TWSkinUnreliableOffset));
			Y += RenderPreview(PREVIEW_SPAMMER, X, Y).y;
		}
		// Client
		RenderPreview(PREVIEW_CLIENT, X, Y);

		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
	else if(s_CurTab == APPEARANCE_TAB_NAME_PLATE)
	{
		MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);

		// ***** Name Plate ***** //
		Ui()->DoLabel_AutoLineSize(Localize("Name Plate"), HeadlineFontSize,
			TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

		// General name plate settings
		{
			int Pressed = (g_Config.m_ClNamePlates ? 2 : 0) + (g_Config.m_ClNamePlatesOwn ? 1 : 0);
			if(DoLine_RadioMenu(LeftView, Localize("Show name plates"),
				   m_vButtonContainersNamePlateShow,
				   {Localize("None", "Show name plates"), Localize("Own", "Show name plates"), Localize("Others", "Show name plates"), Localize("All", "Show name plates")},
				   {0, 1, 2, 3},
				   Pressed))
			{
				g_Config.m_ClNamePlates = Pressed & 2 ? 1 : 0;
				g_Config.m_ClNamePlatesOwn = Pressed & 1 ? 1 : 0;
			}
		}
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_ClNamePlatesSize, &g_Config.m_ClNamePlatesSize, &Button, Localize("Name plates size"), -50, 100);
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_ClNamePlatesOffset, &g_Config.m_ClNamePlatesOffset, &Button, Localize("Name plates offset"), 10, 50);

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClNamePlatesClan, Localize("Show clan above name plates"), &g_Config.m_ClNamePlatesClan, &LeftView, LineSize);
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(g_Config.m_ClNamePlatesClan)
			Ui()->DoScrollbarOption(&g_Config.m_ClNamePlatesClanSize, &g_Config.m_ClNamePlatesClanSize, &Button, Localize("Clan plates size"), -50, 100);

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClNamePlatesTeamcolors, Localize("Use team colors for name plates"), &g_Config.m_ClNamePlatesTeamcolors, &LeftView, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClNamePlatesFriendMark, Localize("Show friend icon in name plates"), &g_Config.m_ClNamePlatesFriendMark, &LeftView, LineSize);

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClNamePlatesIds, Localize("Show client IDs in name plates"), &g_Config.m_ClNamePlatesIds, &LeftView, LineSize);
		if(g_Config.m_ClNamePlatesIds > 0)
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClNamePlatesIdsSeparateLine, Localize("Show client IDs on a separate line"), &g_Config.m_ClNamePlatesIdsSeparateLine, &LeftView, LineSize);
		else
			LeftView.HSplitTop(LineSize, nullptr, &LeftView);
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(g_Config.m_ClNamePlatesIds > 0 && g_Config.m_ClNamePlatesIdsSeparateLine > 0)
			Ui()->DoScrollbarOption(&g_Config.m_ClNamePlatesIdsSize, &g_Config.m_ClNamePlatesIdsSize, &Button, Localize("Client IDs size"), -50, 100);

		// ***** Hook Strength ***** //
		LeftView.HSplitTop(MarginBetweenViews, nullptr, &LeftView);
		Ui()->DoLabel_AutoLineSize(Localize("Hook Strength"), HeadlineFontSize,
			TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(DoButton_CheckBox(&g_Config.m_ClNamePlatesStrong, Localize("Show hook strength icon indicator"), g_Config.m_ClNamePlatesStrong, &Button))
		{
			g_Config.m_ClNamePlatesStrong = g_Config.m_ClNamePlatesStrong ? 0 : 1;
		}
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(g_Config.m_ClNamePlatesStrong)
		{
			static int s_NamePlatesStrong = 0;
			if(DoButton_CheckBox(&s_NamePlatesStrong, Localize("Show hook strength number indicator"), g_Config.m_ClNamePlatesStrong == 2, &Button))
				g_Config.m_ClNamePlatesStrong = g_Config.m_ClNamePlatesStrong != 2 ? 2 : 1;
		}

		LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
		if(g_Config.m_ClNamePlatesStrong)
		{
			Ui()->DoScrollbarOption(&g_Config.m_ClNamePlatesStrongSize, &g_Config.m_ClNamePlatesStrongSize, &Button, Localize("Size of hook strength icon and number indicator"), -50, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE);
		}

		// ***** Key Presses ***** //
		LeftView.HSplitTop(MarginBetweenViews, nullptr, &LeftView);
		Ui()->DoLabel_AutoLineSize(Localize("Key Presses"), HeadlineFontSize,
			TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

		DoLine_RadioMenu(LeftView, Localize("Show players' key presses"),
			m_vButtonContainersNamePlateKeyPresses,
			{Localize("None", "Show players' key presses"), Localize("Own", "Show players' key presses"), Localize("Others", "Show players' key presses"), Localize("All", "Show players' key presses")},
			{0, 3, 1, 2},
			g_Config.m_ClShowDirection);

		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(g_Config.m_ClShowDirection > 0)
			Ui()->DoScrollbarOption(&g_Config.m_ClDirectionSize, &g_Config.m_ClDirectionSize, &Button, Localize("Size of key press icons"), -50, 100);

		// ***** Name Plate Preview ***** //
		Ui()->DoLabel_AutoLineSize(Localize("Preview"), HeadlineFontSize,
			TEXTALIGN_ML, &RightView, HeadlineHeight);
		RightView.HSplitTop(2.0f * MarginSmall, nullptr, &RightView);

		// ***** Name Plate Dummy Preview ***** //
		RightView.HSplitBottom(LineSize, &RightView, &Button);
		if(DoButton_CheckBox(&m_DummyNamePlatePreview, g_Config.m_ClDummy ? Localize("Preview player's name plate") : Localize("Preview dummy's name plate"), m_DummyNamePlatePreview, &Button))
			m_DummyNamePlatePreview = !m_DummyNamePlatePreview;

		int Dummy = g_Config.m_ClDummy != (m_DummyNamePlatePreview ? 1 : 0);

		const vec2 Position = RightView.Center();

		GameClient()->m_NamePlates.RenderNamePlatePreview(Position, Dummy);
	}
	else if(s_CurTab == APPEARANCE_TAB_HOOK_COLLISION)
	{
		MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);

		// ***** Hookline ***** //
		Ui()->DoLabel_AutoLineSize(Localize("Hook collision line"), HeadlineFontSize,
			TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

		// General hookline settings
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(DoButton_CheckBox(&g_Config.m_ClShowHookCollOwn, Localize("Show own player's hook collision line"), g_Config.m_ClShowHookCollOwn, &Button))
		{
			g_Config.m_ClShowHookCollOwn = g_Config.m_ClShowHookCollOwn ? 0 : 1;
		}
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(g_Config.m_ClShowHookCollOwn)
		{
			static int s_ShowHookCollOwn = 0;
			if(DoButton_CheckBox(&s_ShowHookCollOwn, Localize("Always show own player's hook collision line"), g_Config.m_ClShowHookCollOwn == 2, &Button))
				g_Config.m_ClShowHookCollOwn = g_Config.m_ClShowHookCollOwn != 2 ? 2 : 1;
		}

		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(DoButton_CheckBox(&g_Config.m_ClShowHookCollOther, Localize("Show other players' hook collision lines"), g_Config.m_ClShowHookCollOther, &Button))
		{
			g_Config.m_ClShowHookCollOther = g_Config.m_ClShowHookCollOther >= 1 ? 0 : 1;
		}
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(g_Config.m_ClShowHookCollOther)
		{
			static int s_ShowHookCollOther = 0;
			if(DoButton_CheckBox(&s_ShowHookCollOther, Localize("Always show other players' hook collision lines"), g_Config.m_ClShowHookCollOther == 2, &Button))
				g_Config.m_ClShowHookCollOther = g_Config.m_ClShowHookCollOther != 2 ? 2 : 1;
		}

		LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_ClHookCollSize, &g_Config.m_ClHookCollSize, &Button, Localize("Width of your own hook collision line"), 0, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE);

		LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_ClHookCollSizeOther, &g_Config.m_ClHookCollSizeOther, &Button, Localize("Width of others' hook collision line"), 0, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE);

		LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_ClHookCollAlpha, &g_Config.m_ClHookCollAlpha, &Button, Localize("Hook collision line opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE, "%");

		static CButtonContainer s_HookCollNoCollResetId, s_HookCollHookableCollResetId, s_HookCollTeeCollResetId, s_HookCollTipColorResetId;
		static int s_HookCollToolTip;

		Ui()->DoLabel_AutoLineSize(Localize("Colors of the hook collision line:"), 13.0f,
			TEXTALIGN_ML, &LeftView, HeadlineHeight);

		Ui()->DoButtonLogic(&s_HookCollToolTip, 0, &LeftView, BUTTONFLAG_NONE); // Just for the tooltip, result ignored
		GameClient()->m_Tooltips.DoToolTip(&s_HookCollToolTip, &LeftView, Localize("Your movements are not taken into account when calculating the line colors"));
		DoLine_ColorPicker(&s_HookCollNoCollResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("When nothing is hookable", "Hook collision line color"), &g_Config.m_ClHookCollColorNoColl, ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f), false);
		DoLine_ColorPicker(&s_HookCollHookableCollResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("When something is hookable", "Hook collision line color"), &g_Config.m_ClHookCollColorHookableColl, ColorRGBA(130.0f / 255.0f, 232.0f / 255.0f, 160.0f / 255.0f, 1.0f), false);
		DoLine_ColorPicker(&s_HookCollTeeCollResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("When a Tee is hookable", "Hook collision line color"), &g_Config.m_ClHookCollColorTeeColl, ColorRGBA(1.0f, 1.0f, 0.0f, 1.0f), false);
		DoLine_ColorPicker(&s_HookCollTipColorResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Hook collision line tip", "Hook collision line color"), &g_Config.m_ClHookCollTipColor, ColorRGBA(1.0f, 1.0f, 0.0f, 0.5f), false, nullptr, true);

		// ***** Hook collisions preview ***** //
		Ui()->DoLabel_AutoLineSize(Localize("Preview"), HeadlineFontSize,
			TEXTALIGN_ML, &RightView, HeadlineHeight);
		RightView.HSplitTop(2 * MarginSmall, nullptr, &RightView);

		auto DoHookCollision = [this](const vec2 &Pos, const float &Length, const int &Size, const ColorRGBA &Color, const ColorRGBA &TipColor, const bool &Invert) {
			ColorRGBA ColorModified = Color;
			ColorRGBA TipColorModified = TipColor;
			if(Invert)
				ColorModified = color_invert(ColorModified);
			ColorModified = ColorModified.WithAlpha((float)g_Config.m_ClHookCollAlpha / 100);
			TipColorModified = TipColor.WithMultipliedAlpha((float)g_Config.m_ClHookCollAlpha / 100);
			Graphics()->TextureClear();
			if(Size > 0)
			{
				Graphics()->QuadsBegin();
				Graphics()->SetColor(ColorModified);
				float LineWidth = 0.5f + (float)(Size - 1) * 0.25f;
				IGraphics::CQuadItem QuadItem(Pos.x, Pos.y - LineWidth, Length, LineWidth * 2.f);
				Graphics()->QuadsDrawTL(&QuadItem, 1);
				if(TipColor.a > 0.0f)
				{
					Graphics()->SetColor(TipColorModified);
					IGraphics::CQuadItem TipQuadItem(Pos.x + Length, Pos.y - LineWidth, 15.f, LineWidth * 2.f);
					Graphics()->QuadsDrawTL(&TipQuadItem, 1);
				}
				Graphics()->QuadsEnd();
			}
			else
			{
				Graphics()->LinesBegin();
				Graphics()->SetColor(ColorModified);
				IGraphics::CLineItem LineItem(Pos.x, Pos.y, Pos.x + Length, Pos.y);
				Graphics()->LinesDraw(&LineItem, 1);
				if(TipColor.a > 0.0f)
				{
					Graphics()->SetColor(TipColorModified);
					IGraphics::CLineItem TipLineItem(Pos.x + Length, Pos.y, Pos.x + Length + 15.f, Pos.y);
					Graphics()->LinesDraw(&TipLineItem, 1);
				}
				Graphics()->LinesEnd();
			}
		};

		CTeeRenderInfo OwnSkinInfo;
		OwnSkinInfo.Apply(GameClient()->m_Skins.Find(g_Config.m_ClPlayerSkin));
		OwnSkinInfo.ApplyColors(g_Config.m_ClPlayerUseCustomColor, g_Config.m_ClPlayerColorBody, g_Config.m_ClPlayerColorFeet);
		OwnSkinInfo.m_Size = 50.0f;

		CTeeRenderInfo DummySkinInfo;
		DummySkinInfo.Apply(GameClient()->m_Skins.Find(g_Config.m_ClDummySkin));
		DummySkinInfo.ApplyColors(g_Config.m_ClDummyUseCustomColor, g_Config.m_ClDummyColorBody, g_Config.m_ClDummyColorFeet);
		DummySkinInfo.m_Size = 50.0f;

		vec2 TeeRenderPos, DummyRenderPos;

		const float LineLength = 150.f;
		const float LeftMargin = 30.f;

		const int TileScale = 32.0f;

		// Toggled via checkbox later, inverts some previews
		static bool s_HookCollPressed = false;

		CUIRect PreviewColl;

		// ***** Unhookable Tile Preview *****
		CUIRect PreviewNoColl;
		RightView.HSplitTop(50.0f, &PreviewNoColl, &RightView);
		RightView.HSplitTop(4 * MarginSmall, nullptr, &RightView);
		TeeRenderPos = vec2(PreviewNoColl.x + LeftMargin, PreviewNoColl.y + PreviewNoColl.h / 2.0f);
		DoHookCollision(TeeRenderPos, PreviewNoColl.w - LineLength, g_Config.m_ClHookCollSize, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClHookCollColorNoColl)), ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f), s_HookCollPressed);
		RenderTools()->RenderTee(CAnimState::GetIdle(), &OwnSkinInfo, 0, vec2(1.0f, 0.0f), TeeRenderPos);

		CUIRect NoHookTileRect;
		PreviewNoColl.VSplitRight(LineLength, &PreviewNoColl, &NoHookTileRect);
		NoHookTileRect.VSplitLeft(50.0f, &NoHookTileRect, nullptr);
		NoHookTileRect.Margin(10.0f, &NoHookTileRect);

		// Render unhookable tile
		Graphics()->TextureClear();
		Graphics()->TextureSet(GameClient()->m_MapImages.GetEntities(MAP_IMAGE_ENTITY_LAYER_TYPE_ALL_EXCEPT_SWITCH));
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
		RenderMap()->RenderTile(NoHookTileRect.x, NoHookTileRect.y, TILE_NOHOOK, TileScale, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));

		// ***** Hookable Tile Preview *****
		RightView.HSplitTop(50.0f, &PreviewColl, &RightView);
		RightView.HSplitTop(4 * MarginSmall, nullptr, &RightView);
		TeeRenderPos = vec2(PreviewColl.x + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
		DoHookCollision(TeeRenderPos, PreviewColl.w - LineLength, g_Config.m_ClHookCollSize, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClHookCollColorHookableColl)), ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f), s_HookCollPressed);
		RenderTools()->RenderTee(CAnimState::GetIdle(), &OwnSkinInfo, 0, vec2(1.0f, 0.0f), TeeRenderPos);

		CUIRect HookTileRect;
		PreviewColl.VSplitRight(LineLength, &PreviewColl, &HookTileRect);
		HookTileRect.VSplitLeft(50.0f, &HookTileRect, nullptr);
		HookTileRect.Margin(10.0f, &HookTileRect);

		// Render hookable tile
		Graphics()->TextureClear();
		Graphics()->TextureSet(GameClient()->m_MapImages.GetEntities(MAP_IMAGE_ENTITY_LAYER_TYPE_ALL_EXCEPT_SWITCH));
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
		RenderMap()->RenderTile(HookTileRect.x, HookTileRect.y, TILE_SOLID, TileScale, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));

		// ***** Hook Dummy Preview *****
		RightView.HSplitTop(50.0f, &PreviewColl, &RightView);
		RightView.HSplitTop(4 * MarginSmall, nullptr, &RightView);
		TeeRenderPos = vec2(PreviewColl.x + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
		DummyRenderPos = vec2(PreviewColl.x + PreviewColl.w - LineLength - 5.f + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
		DoHookCollision(TeeRenderPos, PreviewColl.w - LineLength - 15.f, g_Config.m_ClHookCollSize, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClHookCollColorTeeColl)), ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f), s_HookCollPressed);
		RenderTools()->RenderTee(CAnimState::GetIdle(), &DummySkinInfo, 0, vec2(1.0f, 0.0f), DummyRenderPos);
		RenderTools()->RenderTee(CAnimState::GetIdle(), &OwnSkinInfo, 0, vec2(1.0f, 0.0f), TeeRenderPos);

		// ***** Hook Dummy Reverse Preview *****
		RightView.HSplitTop(50.0f, &PreviewColl, &RightView);
		RightView.HSplitTop(4 * MarginSmall, nullptr, &RightView);
		TeeRenderPos = vec2(PreviewColl.x + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
		DummyRenderPos = vec2(PreviewColl.x + PreviewColl.w - LineLength - 5.f + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
		DoHookCollision(TeeRenderPos, PreviewColl.w - LineLength - 15.f, g_Config.m_ClHookCollSizeOther, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClHookCollColorTeeColl)), ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f), false);
		RenderTools()->RenderTee(CAnimState::GetIdle(), &OwnSkinInfo, 0, vec2(1.0f, 0.0f), DummyRenderPos);
		RenderTools()->RenderTee(CAnimState::GetIdle(), &DummySkinInfo, 0, vec2(1.0f, 0.0f), TeeRenderPos);

		// ***** Hook Line Tip Preview *****
		RightView.HSplitTop(50.0f, &PreviewColl, &RightView);
		RightView.HSplitTop(4 * MarginSmall, nullptr, &RightView);
		TeeRenderPos = vec2(PreviewColl.x + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
		DoHookCollision(TeeRenderPos, PreviewColl.w - LineLength - 15.f, g_Config.m_ClHookCollSize, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClHookCollColorNoColl)), color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClHookCollTipColor, true)), s_HookCollPressed);
		RenderTools()->RenderTee(CAnimState::GetIdle(), &OwnSkinInfo, 0, vec2(1.0f, 0.0f), TeeRenderPos);

		// ***** Preview +hookcoll pressed toggle *****
		RightView.HSplitTop(LineSize, &Button, &RightView);
		if(DoButton_CheckBox(&s_HookCollPressed, Localize("Preview 'Hook collisions' being pressed"), s_HookCollPressed, &Button))
			s_HookCollPressed = !s_HookCollPressed;
	}
	else if(s_CurTab == APPEARANCE_TAB_INFO_MESSAGES)
	{
		MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);

		// ***** Info Messages ***** //
		Ui()->DoLabel_AutoLineSize(Localize("Info Messages"), HeadlineFontSize,
			TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

		// General info messages settings
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(DoButton_CheckBox(&g_Config.m_ClShowKillMessages, Localize("Show kill messages"), g_Config.m_ClShowKillMessages, &Button))
		{
			g_Config.m_ClShowKillMessages ^= 1;
		}

		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(DoButton_CheckBox(&g_Config.m_ClShowFinishMessages, Localize("Show finish messages"), g_Config.m_ClShowFinishMessages, &Button))
		{
			g_Config.m_ClShowFinishMessages ^= 1;
		}

		static CButtonContainer s_KillMessageNormalColorId, s_KillMessageHighlightColorId;
		DoLine_ColorPicker(&s_KillMessageNormalColorId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Normal Color"), &g_Config.m_ClKillMessageNormalColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
		DoLine_ColorPicker(&s_KillMessageHighlightColorId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Highlight Color"), &g_Config.m_ClKillMessageHighlightColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
	}
	else if(s_CurTab == APPEARANCE_TAB_LASER)
	{
		MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);

		// ***** Weapons ***** //
		Ui()->DoLabel_AutoLineSize(Localize("Weapons"), HeadlineFontSize,
			TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

		// General weapon laser settings
		static CButtonContainer s_LaserRifleOutResetId, s_LaserRifleInResetId, s_LaserShotgunOutResetId, s_LaserShotgunInResetId;

		ColorHSLA LaserRifleOutlineColor = DoLine_ColorPicker(&s_LaserRifleOutResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Rifle Laser Outline Color"), &g_Config.m_ClLaserRifleOutlineColor, ColorRGBA(0.074402f, 0.074402f, 0.247166f, 1.0f), false);
		ColorHSLA LaserRifleInnerColor = DoLine_ColorPicker(&s_LaserRifleInResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Rifle Laser Inner Color"), &g_Config.m_ClLaserRifleInnerColor, ColorRGBA(0.498039f, 0.498039f, 1.0f, 1.0f), false);
		ColorHSLA LaserShotgunOutlineColor = DoLine_ColorPicker(&s_LaserShotgunOutResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Shotgun Laser Outline Color"), &g_Config.m_ClLaserShotgunOutlineColor, ColorRGBA(0.125490f, 0.098039f, 0.043137f, 1.0f), false);
		ColorHSLA LaserShotgunInnerColor = DoLine_ColorPicker(&s_LaserShotgunInResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Shotgun Laser Inner Color"), &g_Config.m_ClLaserShotgunInnerColor, ColorRGBA(0.570588f, 0.417647f, 0.252941f, 1.0f), false);

		// ***** Entities ***** //
		LeftView.HSplitTop(10.0f, nullptr, &LeftView);
		Ui()->DoLabel_AutoLineSize(Localize("Entities"), HeadlineFontSize,
			TEXTALIGN_ML, &LeftView, HeadlineHeight);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

		// General entity laser settings
		static CButtonContainer s_LaserDoorOutResetId, s_LaserDoorInResetId, s_LaserFreezeOutResetId, s_LaserFreezeInResetId, s_LaserDraggerOutResetId, s_LaserDraggerInResetId;

		ColorHSLA LaserDoorOutlineColor = DoLine_ColorPicker(&s_LaserDoorOutResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Door Laser Outline Color"), &g_Config.m_ClLaserDoorOutlineColor, ColorRGBA(0.0f, 0.131372f, 0.096078f, 1.0f), false);
		ColorHSLA LaserDoorInnerColor = DoLine_ColorPicker(&s_LaserDoorInResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Door Laser Inner Color"), &g_Config.m_ClLaserDoorInnerColor, ColorRGBA(0.262745f, 0.760784f, 0.639215f, 1.0f), false);
		ColorHSLA LaserFreezeOutlineColor = DoLine_ColorPicker(&s_LaserFreezeOutResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Freeze Laser Outline Color"), &g_Config.m_ClLaserFreezeOutlineColor, ColorRGBA(0.131372f, 0.123529f, 0.182352f, 1.0f), false);
		ColorHSLA LaserFreezeInnerColor = DoLine_ColorPicker(&s_LaserFreezeInResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Freeze Laser Inner Color"), &g_Config.m_ClLaserFreezeInnerColor, ColorRGBA(0.482352f, 0.443137f, 0.564705f, 1.0f), false);
		ColorHSLA LaserDraggerOutlineColor = DoLine_ColorPicker(&s_LaserDraggerOutResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Dragger Outline Color"), &g_Config.m_ClLaserDraggerOutlineColor, ColorRGBA(0.1640625f, 0.015625f, 0.015625f, 1.0f), false);
		ColorHSLA LaserDraggerInnerColor = DoLine_ColorPicker(&s_LaserDraggerInResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Dragger Inner Color"), &g_Config.m_ClLaserDraggerInnerColor, ColorRGBA(.8666666f, .3725490f, .3725490f, 1.0f), false);

		static CButtonContainer s_AllToRifleResetId, s_AllToDefaultResetId;

		LeftView.HSplitTop(4 * MarginSmall, nullptr, &LeftView);
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(DoButton_Menu(&s_AllToRifleResetId, Localize("Set all to Rifle"), 0, &Button))
		{
			g_Config.m_ClLaserShotgunOutlineColor = g_Config.m_ClLaserRifleOutlineColor;
			g_Config.m_ClLaserShotgunInnerColor = g_Config.m_ClLaserRifleInnerColor;
			g_Config.m_ClLaserDoorOutlineColor = g_Config.m_ClLaserRifleOutlineColor;
			g_Config.m_ClLaserDoorInnerColor = g_Config.m_ClLaserRifleInnerColor;
			g_Config.m_ClLaserFreezeOutlineColor = g_Config.m_ClLaserRifleOutlineColor;
			g_Config.m_ClLaserFreezeInnerColor = g_Config.m_ClLaserRifleInnerColor;
			g_Config.m_ClLaserDraggerOutlineColor = g_Config.m_ClLaserRifleOutlineColor;
			g_Config.m_ClLaserDraggerInnerColor = g_Config.m_ClLaserRifleInnerColor;
		}

		// values taken from the CL commands
		LeftView.HSplitTop(2 * MarginSmall, nullptr, &LeftView);
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(DoButton_Menu(&s_AllToDefaultResetId, Localize("Reset to defaults"), 0, &Button))
		{
			g_Config.m_ClLaserRifleOutlineColor = 11176233;
			g_Config.m_ClLaserRifleInnerColor = 11206591;
			g_Config.m_ClLaserShotgunOutlineColor = 1866773;
			g_Config.m_ClLaserShotgunInnerColor = 1467241;
			g_Config.m_ClLaserDoorOutlineColor = 7667473;
			g_Config.m_ClLaserDoorInnerColor = 7701379;
			g_Config.m_ClLaserFreezeOutlineColor = 11613223;
			g_Config.m_ClLaserFreezeInnerColor = 12001153;
			g_Config.m_ClLaserDraggerOutlineColor = 57618;
			g_Config.m_ClLaserDraggerInnerColor = 42398;
		}

		// ***** Laser Preview ***** //
		Ui()->DoLabel_AutoLineSize(Localize("Preview"), HeadlineFontSize,
			TEXTALIGN_ML, &RightView, HeadlineHeight);
		RightView.HSplitTop(MarginSmall, nullptr, &RightView);

		const float LaserPreviewHeight = 60.0f;
		CUIRect LaserPreview;
		RightView.HSplitTop(LaserPreviewHeight, &LaserPreview, &RightView);
		RightView.HSplitTop(2 * MarginSmall, nullptr, &RightView);
		DoLaserPreview(&LaserPreview, LaserRifleOutlineColor, LaserRifleInnerColor, LASERTYPE_RIFLE);

		RightView.HSplitTop(LaserPreviewHeight, &LaserPreview, &RightView);
		RightView.HSplitTop(2 * MarginSmall, nullptr, &RightView);
		DoLaserPreview(&LaserPreview, LaserShotgunOutlineColor, LaserShotgunInnerColor, LASERTYPE_SHOTGUN);

		RightView.HSplitTop(LaserPreviewHeight, &LaserPreview, &RightView);
		RightView.HSplitTop(2 * MarginSmall, nullptr, &RightView);
		DoLaserPreview(&LaserPreview, LaserDoorOutlineColor, LaserDoorInnerColor, LASERTYPE_DOOR);

		RightView.HSplitTop(LaserPreviewHeight, &LaserPreview, &RightView);
		RightView.HSplitTop(2 * MarginSmall, nullptr, &RightView);
		DoLaserPreview(&LaserPreview, LaserFreezeOutlineColor, LaserFreezeInnerColor, LASERTYPE_FREEZE);

		RightView.HSplitTop(LaserPreviewHeight, &LaserPreview, &RightView);
		RightView.HSplitTop(2 * MarginSmall, nullptr, &RightView);
		DoLaserPreview(&LaserPreview, LaserDraggerOutlineColor, LaserDraggerInnerColor, LASERTYPE_DRAGGER);
	}
}

void CMenus::DoLaserPreview(const CUIRect *pRect, const ColorHSLA LaserOutlineColor, const ColorHSLA LaserInnerColor, const int LaserType)
{
	CUIRect Section = *pRect;
	vec2 From = vec2(Section.x + 30.0f, Section.y + Section.h / 2.0f);
	vec2 Pos = vec2(Section.x + Section.w - 20.0f, Section.y + Section.h / 2.0f);

	const ColorRGBA OuterColor = color_cast<ColorRGBA>(ColorHSLA(LaserOutlineColor));
	const ColorRGBA InnerColor = color_cast<ColorRGBA>(ColorHSLA(LaserInnerColor));
	const float TicksHead = Client()->GlobalTime() * Client()->GameTickSpeed();

	// TicksBody = 4.0 for less laser width for weapon alignment
	GameClient()->m_Items.RenderLaser(From, Pos, OuterColor, InnerColor, 4.0f, TicksHead, LaserType);

	switch(LaserType)
	{
	case LASERTYPE_RIFLE:
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteWeaponLaser);
		Graphics()->SelectSprite(SPRITE_WEAPON_LASER_BODY);
		Graphics()->QuadsBegin();
		Graphics()->QuadsSetSubset(0, 0, 1, 1);
		Graphics()->DrawSprite(Section.x + 30.0f, Section.y + Section.h / 2.0f, 60.0f);
		Graphics()->QuadsEnd();
		break;
	case LASERTYPE_SHOTGUN:
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteWeaponShotgun);
		Graphics()->SelectSprite(SPRITE_WEAPON_SHOTGUN_BODY);
		Graphics()->QuadsBegin();
		Graphics()->QuadsSetSubset(0, 0, 1, 1);
		Graphics()->DrawSprite(Section.x + 30.0f, Section.y + Section.h / 2.0f, 60.0f);
		Graphics()->QuadsEnd();
		break;
	case LASERTYPE_DRAGGER:
	{
		CTeeRenderInfo TeeRenderInfo;
		TeeRenderInfo.Apply(GameClient()->m_Skins.Find(g_Config.m_ClPlayerSkin));
		TeeRenderInfo.ApplyColors(g_Config.m_ClPlayerUseCustomColor, g_Config.m_ClPlayerColorBody, g_Config.m_ClPlayerColorFeet);
		TeeRenderInfo.m_Size = 64.0f;
		RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeRenderInfo, EMOTE_NORMAL, vec2(-1, 0), Pos);
		break;
	}
	case LASERTYPE_FREEZE:
	{
		CTeeRenderInfo TeeRenderInfo;
		if(g_Config.m_ClShowNinja)
			TeeRenderInfo.Apply(GameClient()->m_Skins.Find("x_ninja"));
		else
			TeeRenderInfo.Apply(GameClient()->m_Skins.Find(g_Config.m_ClPlayerSkin));
		TeeRenderInfo.m_TeeRenderFlags = TEE_EFFECT_FROZEN;
		TeeRenderInfo.m_Size = 64.0f;
		TeeRenderInfo.m_ColorBody = ColorRGBA(1, 1, 1);
		TeeRenderInfo.m_ColorFeet = ColorRGBA(1, 1, 1);
		RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeRenderInfo, EMOTE_PAIN, vec2(1, 0), From);
		GameClient()->m_Effects.FreezingFlakes(From, vec2(32, 32), 1.0f);
		break;
	}
	default:
		GameClient()->m_Items.RenderLaser(From, From, OuterColor, InnerColor, 4.0f, TicksHead, LaserType);
	}
}



/* Settings section: graphics */
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus.h"

#include <base/str.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/components/tooltips.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <array>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

void CMenus::RenderSettingsGraphics(CUIRect MainView)
{
	CUIRect Button;
	char aBuf[128];
	bool CheckSettings = false;

	static const int MAX_RESOLUTIONS = 256;
	static CVideoMode s_aModes[MAX_RESOLUTIONS];
	static int s_NumNodes = Graphics()->GetVideoModes(s_aModes, MAX_RESOLUTIONS, g_Config.m_GfxScreen);
	static int s_GfxFsaaSamples = g_Config.m_GfxFsaaSamples;
	static bool s_GfxBackendChanged = false;
	static bool s_GfxGpuChanged = false;

	static int s_InitDisplayAllVideoModes = g_Config.m_GfxDisplayAllVideoModes;

	static bool s_WasInit = false;
	static bool s_ModesReload = false;
	if(!s_WasInit)
	{
		s_WasInit = true;

		Graphics()->AddWindowPropChangeListener([]() {
			s_ModesReload = true;
		});
	}

	if(s_ModesReload || g_Config.m_GfxDisplayAllVideoModes != s_InitDisplayAllVideoModes)
	{
		s_NumNodes = Graphics()->GetVideoModes(s_aModes, MAX_RESOLUTIONS, g_Config.m_GfxScreen);
		s_ModesReload = false;
		s_InitDisplayAllVideoModes = g_Config.m_GfxDisplayAllVideoModes;
	}

	CUIRect ModeList, ModeLabel;
	MainView.VSplitLeft(350.0f, &MainView, &ModeList);
	ModeList.HSplitTop(24.0f, &ModeLabel, &ModeList);
	MainView.VSplitLeft(340.0f, &MainView, nullptr);

	// display mode list
	static CListBox s_ListBox;
	static const float sc_RowHeightResList = 22.0f;
	static const float sc_FontSizeResListHeader = 12.0f;
	static const float sc_FontSizeResList = 10.0f;

	{
		int G = std::gcd(g_Config.m_GfxScreenWidth, g_Config.m_GfxScreenHeight);
		str_format(aBuf, sizeof(aBuf), "%s: %dx%d @%dhz (%d:%d)",
			Localize("Current"),
			(int)(g_Config.m_GfxScreenWidth * Graphics()->ScreenHiDPIScale()),
			(int)(g_Config.m_GfxScreenHeight * Graphics()->ScreenHiDPIScale()),
			g_Config.m_GfxScreenRefreshRate,
			g_Config.m_GfxScreenWidth / G,
			g_Config.m_GfxScreenHeight / G);
		Ui()->DoLabel(&ModeLabel, aBuf, sc_FontSizeResListHeader, TEXTALIGN_MC);
	}

	int OldSelected = -1;
	s_ListBox.SetActive(!Ui()->IsPopupOpen());
	s_ListBox.DoStart(sc_RowHeightResList, s_NumNodes, 1, 3, OldSelected, &ModeList);

	for(int i = 0; i < s_NumNodes; ++i)
	{
		if(g_Config.m_GfxScreenWidth == s_aModes[i].m_WindowWidth &&
			g_Config.m_GfxScreenHeight == s_aModes[i].m_WindowHeight &&
			g_Config.m_GfxScreenRefreshRate == s_aModes[i].m_RefreshRate)
		{
			OldSelected = i;
		}

		const CListboxItem Item = s_ListBox.DoNextItem(&s_aModes[i], OldSelected == i);
		if(!Item.m_Visible)
			continue;

		int G = std::gcd(s_aModes[i].m_WindowWidth, s_aModes[i].m_WindowHeight);
		str_format(aBuf, sizeof(aBuf), " %dx%d @%dhz (%d:%d)", s_aModes[i].m_CanvasWidth, s_aModes[i].m_CanvasHeight, s_aModes[i].m_RefreshRate, s_aModes[i].m_WindowWidth / G, s_aModes[i].m_WindowHeight / G);
		Ui()->DoLabel(&Item.m_Rect, aBuf, sc_FontSizeResList, TEXTALIGN_ML);
	}

	const int NewSelected = s_ListBox.DoEnd();
	if(OldSelected != NewSelected)
	{
		g_Config.m_GfxScreenWidth = s_aModes[NewSelected].m_WindowWidth;
		g_Config.m_GfxScreenHeight = s_aModes[NewSelected].m_WindowHeight;
		g_Config.m_GfxScreenRefreshRate = s_aModes[NewSelected].m_RefreshRate;
		Graphics()->ResizeToScreen();
	}

	// switches
	CUIRect WindowModeDropDown;
	MainView.HSplitTop(20.0f, &WindowModeDropDown, &MainView);

	const char *apWindowModes[] = {Localize("Windowed"), Localize("Windowed borderless"), Localize("Windowed fullscreen"), Localize("Desktop fullscreen"), Localize("Fullscreen")};
	static const int s_NumWindowMode = std::size(apWindowModes);

	const int OldWindowMode = (g_Config.m_GfxFullscreen ? (g_Config.m_GfxFullscreen == 1 ? 4 : (g_Config.m_GfxFullscreen == 2 ? 3 : 2)) : (g_Config.m_GfxBorderless ? 1 : 0));

	static CUi::SDropDownState s_WindowModeDropDownState;
	static CScrollRegion s_WindowModeDropDownScrollRegion;
	s_WindowModeDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_WindowModeDropDownScrollRegion;
	const int NewWindowMode = Ui()->DoDropDown(&WindowModeDropDown, OldWindowMode, apWindowModes, s_NumWindowMode, s_WindowModeDropDownState);
	if(OldWindowMode != NewWindowMode)
	{
		if(NewWindowMode == 0)
			Graphics()->SetWindowParams(0, false);
		else if(NewWindowMode == 1)
			Graphics()->SetWindowParams(0, true);
		else if(NewWindowMode == 2)
			Graphics()->SetWindowParams(3, false);
		else if(NewWindowMode == 3)
			Graphics()->SetWindowParams(2, false);
		else if(NewWindowMode == 4)
			Graphics()->SetWindowParams(1, false);
	}

	if(Graphics()->GetNumScreens() > 1)
	{
		CUIRect ScreenDropDown;
		MainView.HSplitTop(2.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &ScreenDropDown, &MainView);

		const int NumScreens = Graphics()->GetNumScreens();
		static std::vector<std::string> s_vScreenNames;
		static std::vector<const char *> s_vpScreenNames;
		s_vScreenNames.resize(NumScreens);
		s_vpScreenNames.resize(NumScreens);

		for(int i = 0; i < NumScreens; ++i)
		{
			str_format(aBuf, sizeof(aBuf), "%s %d: %s", Localize("Screen"), i, Graphics()->GetScreenName(i));
			s_vScreenNames[i] = aBuf;
			s_vpScreenNames[i] = s_vScreenNames[i].c_str();
		}

		static CUi::SDropDownState s_ScreenDropDownState;
		static CScrollRegion s_ScreenDropDownScrollRegion;
		s_ScreenDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_ScreenDropDownScrollRegion;
		const int NewScreen = Ui()->DoDropDown(&ScreenDropDown, g_Config.m_GfxScreen, s_vpScreenNames.data(), s_vpScreenNames.size(), s_ScreenDropDownState);
		if(NewScreen != g_Config.m_GfxScreen)
			Graphics()->SwitchWindowScreen(NewScreen, true);
	}

	MainView.HSplitTop(2.0f, nullptr, &MainView);
	MainView.HSplitTop(20.0f, &Button, &MainView);
	str_format(aBuf, sizeof(aBuf), "%s (%s)", Localize("V-Sync"), Localize("may cause delay"));
	if(DoButton_CheckBox(&g_Config.m_GfxVsync, aBuf, g_Config.m_GfxVsync, &Button))
	{
		Graphics()->SetVSync(!g_Config.m_GfxVsync);
	}

	bool MultiSamplingChanged = false;
	MainView.HSplitTop(20.0f, &Button, &MainView);
	str_format(aBuf, sizeof(aBuf), "%s (%s)", Localize("FSAA samples"), Localize("may cause delay"));
	int GfxFsaaSamplesMouseButton = DoButton_CheckBox_Number(&g_Config.m_GfxFsaaSamples, aBuf, g_Config.m_GfxFsaaSamples, &Button);
	int CurFSAA = g_Config.m_GfxFsaaSamples == 0 ? 1 : g_Config.m_GfxFsaaSamples;
	if(GfxFsaaSamplesMouseButton == 1) // inc
	{
		g_Config.m_GfxFsaaSamples = std::pow(2, (int)std::log2(CurFSAA) + 1);
		if(g_Config.m_GfxFsaaSamples > 64)
			g_Config.m_GfxFsaaSamples = 0;
		MultiSamplingChanged = true;
	}
	else if(GfxFsaaSamplesMouseButton == 2) // dec
	{
		if(CurFSAA == 1)
			g_Config.m_GfxFsaaSamples = 64;
		else if(CurFSAA == 2)
			g_Config.m_GfxFsaaSamples = 0;
		else
			g_Config.m_GfxFsaaSamples = std::pow(2, (int)std::log2(CurFSAA) - 1);
		MultiSamplingChanged = true;
	}

	uint32_t MultiSamplingCountBackend = 0;
	if(MultiSamplingChanged)
	{
		if(Graphics()->SetMultiSampling(g_Config.m_GfxFsaaSamples, MultiSamplingCountBackend))
		{
			// try again with 0 if mouse click was increasing multi sampling
			// else just accept the current value as is
			if((uint32_t)g_Config.m_GfxFsaaSamples > MultiSamplingCountBackend && GfxFsaaSamplesMouseButton == 1)
				Graphics()->SetMultiSampling(0, MultiSamplingCountBackend);
			g_Config.m_GfxFsaaSamples = (int)MultiSamplingCountBackend;
		}
		else
		{
			CheckSettings = true;
		}
	}

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_GfxHighDetail, Localize("High Detail"), g_Config.m_GfxHighDetail, &Button))
		g_Config.m_GfxHighDetail ^= 1;
	GameClient()->m_Tooltips.DoToolTip(&g_Config.m_GfxHighDetail, &Button, Localize("Allows maps to render with more detail"));

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_ClShowfps, Localize("Show FPS"), g_Config.m_ClShowfps, &Button))
		g_Config.m_ClShowfps ^= 1;
	GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ClShowfps, &Button, Localize("Renders your frame rate in the top right"));

	MainView.HSplitTop(20.0f, &Button, &MainView);
	str_copy(aBuf, " ");
	str_append(aBuf, Localize("Hz", "Hertz"));
	Ui()->DoScrollbarOption(&g_Config.m_GfxRefreshRate, &g_Config.m_GfxRefreshRate, &Button, Localize("Refresh Rate"), 10, 1000, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_INFINITE | CUi::SCROLLBAR_OPTION_NOCLAMPVALUE | CUi::SCROLLBAR_OPTION_DELAYUPDATE, aBuf);

	MainView.HSplitTop(2.0f, nullptr, &MainView);
	static CButtonContainer s_UiColorResetId;
	DoLine_ColorPicker(&s_UiColorResetId, 25.0f, 13.0f, 2.0f, &MainView, Localize("UI Color"), &g_Config.m_UiColor, color_cast<ColorRGBA>(ColorHSLA(0xE4A046AFU, true)), false, nullptr, true);

	// Backend list
	struct SMenuBackendInfo
	{
		int m_Major = 0;
		int m_Minor = 0;
		int m_Patch = 0;
		const char *m_pBackendName = "";
		bool m_Found = false;
	};
	std::array<std::array<SMenuBackendInfo, EGraphicsDriverAgeType::GRAPHICS_DRIVER_AGE_TYPE_COUNT>, EBackendType::BACKEND_TYPE_COUNT> aaSupportedBackends{};
	uint32_t FoundBackendCount = 0;
	for(uint32_t i = 0; i < BACKEND_TYPE_COUNT; ++i)
	{
		if(EBackendType(i) == BACKEND_TYPE_AUTO)
			continue;
		for(uint32_t n = 0; n < GRAPHICS_DRIVER_AGE_TYPE_COUNT; ++n)
		{
			auto &Info = aaSupportedBackends[i][n];
			if(Graphics()->GetDriverVersion(EGraphicsDriverAgeType(n), Info.m_Major, Info.m_Minor, Info.m_Patch, Info.m_pBackendName, EBackendType(i)))
			{
				// don't count blocked opengl drivers
				if(EBackendType(i) != BACKEND_TYPE_OPENGL || EGraphicsDriverAgeType(n) == GRAPHICS_DRIVER_AGE_TYPE_LEGACY || g_Config.m_GfxDriverIsBlocked == 0)
				{
					Info.m_Found = true;
					++FoundBackendCount;
				}
			}
		}
	}

	if(FoundBackendCount > 1)
	{
		CUIRect Text, BackendDropDown;
		MainView.HSplitTop(10.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &Text, &MainView);
		MainView.HSplitTop(2.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &BackendDropDown, &MainView);
		Ui()->DoLabel(&Text, Localize("Renderer"), 16.0f, TEXTALIGN_MC);

		static std::vector<std::string> s_vBackendIdNames;
		static std::vector<const char *> s_vpBackendIdNamesCStr;
		static std::vector<SMenuBackendInfo> s_vBackendInfos;

		size_t BackendCount = FoundBackendCount + 1;
		s_vBackendIdNames.resize(BackendCount);
		s_vpBackendIdNamesCStr.resize(BackendCount);
		s_vBackendInfos.resize(BackendCount);

		char aTmpBackendName[256];

		auto IsInfoDefault = [](const SMenuBackendInfo &CheckInfo) {
			return str_comp_nocase(CheckInfo.m_pBackendName, DefaultConfig::GfxBackend) == 0 && CheckInfo.m_Major == DefaultConfig::GfxGLMajor && CheckInfo.m_Minor == DefaultConfig::GfxGLMinor && CheckInfo.m_Patch == DefaultConfig::GfxGLPatch;
		};

		int OldSelectedBackend = -1;
		uint32_t CurCounter = 0;
		for(uint32_t i = 0; i < BACKEND_TYPE_COUNT; ++i)
		{
			for(uint32_t n = 0; n < GRAPHICS_DRIVER_AGE_TYPE_COUNT; ++n)
			{
				auto &Info = aaSupportedBackends[i][n];
				if(Info.m_Found)
				{
					bool IsDefault = IsInfoDefault(Info);
					str_format(aTmpBackendName, sizeof(aTmpBackendName), "%s (%d.%d.%d)%s%s", Info.m_pBackendName, Info.m_Major, Info.m_Minor, Info.m_Patch, IsDefault ? " - " : "", IsDefault ? Localize("default") : "");
					s_vBackendIdNames[CurCounter] = aTmpBackendName;
					s_vpBackendIdNamesCStr[CurCounter] = s_vBackendIdNames[CurCounter].c_str();
					if(str_comp_nocase(Info.m_pBackendName, g_Config.m_GfxBackend) == 0 && g_Config.m_GfxGLMajor == Info.m_Major && g_Config.m_GfxGLMinor == Info.m_Minor && g_Config.m_GfxGLPatch == Info.m_Patch)
					{
						OldSelectedBackend = CurCounter;
					}

					s_vBackendInfos[CurCounter] = Info;
					++CurCounter;
				}
			}
		}

		if(OldSelectedBackend != -1)
		{
			// no custom selected
			BackendCount -= 1;
		}
		else
		{
			// custom selected one
			str_format(aTmpBackendName, sizeof(aTmpBackendName), "%s (%s %d.%d.%d)", Localize("custom"), g_Config.m_GfxBackend, g_Config.m_GfxGLMajor, g_Config.m_GfxGLMinor, g_Config.m_GfxGLPatch);
			s_vBackendIdNames[CurCounter] = aTmpBackendName;
			s_vpBackendIdNamesCStr[CurCounter] = s_vBackendIdNames[CurCounter].c_str();
			OldSelectedBackend = CurCounter;

			s_vBackendInfos[CurCounter].m_pBackendName = "custom";
			s_vBackendInfos[CurCounter].m_Major = g_Config.m_GfxGLMajor;
			s_vBackendInfos[CurCounter].m_Minor = g_Config.m_GfxGLMinor;
			s_vBackendInfos[CurCounter].m_Patch = g_Config.m_GfxGLPatch;
		}

		static int s_OldSelectedBackend = -1;
		if(s_OldSelectedBackend == -1)
			s_OldSelectedBackend = OldSelectedBackend;

		static CUi::SDropDownState s_BackendDropDownState;
		static CScrollRegion s_BackendDropDownScrollRegion;
		s_BackendDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_BackendDropDownScrollRegion;
		const int NewBackend = Ui()->DoDropDown(&BackendDropDown, OldSelectedBackend, s_vpBackendIdNamesCStr.data(), BackendCount, s_BackendDropDownState);
		if(OldSelectedBackend != NewBackend)
		{
			str_copy(g_Config.m_GfxBackend, s_vBackendInfos[NewBackend].m_pBackendName);
			g_Config.m_GfxGLMajor = s_vBackendInfos[NewBackend].m_Major;
			g_Config.m_GfxGLMinor = s_vBackendInfos[NewBackend].m_Minor;
			g_Config.m_GfxGLPatch = s_vBackendInfos[NewBackend].m_Patch;

			CheckSettings = true;
			s_GfxBackendChanged = s_OldSelectedBackend != NewBackend;
		}
	}

	// GPU list
	const auto &GpuList = Graphics()->GetGpus();
	if(GpuList.m_vGpus.size() > 1)
	{
		CUIRect Text, GpuDropDown;
		MainView.HSplitTop(10.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &Text, &MainView);
		MainView.HSplitTop(2.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &GpuDropDown, &MainView);
		Ui()->DoLabel(&Text, Localize("Graphics card"), 16.0f, TEXTALIGN_MC);

		static std::vector<const char *> s_vpGpuIdNames;

		size_t GpuCount = GpuList.m_vGpus.size() + 1;
		s_vpGpuIdNames.resize(GpuCount);

		char aCurDeviceName[256 + 4];

		int OldSelectedGpu = -1;
		for(size_t i = 0; i < GpuCount; ++i)
		{
			if(i == 0)
			{
				str_format(aCurDeviceName, sizeof(aCurDeviceName), "%s (%s)", Localize("auto"), GpuList.m_AutoGpu.m_aName);
				s_vpGpuIdNames[i] = aCurDeviceName;
				if(str_comp("auto", g_Config.m_GfxGpuName) == 0)
				{
					OldSelectedGpu = 0;
				}
			}
			else
			{
				s_vpGpuIdNames[i] = GpuList.m_vGpus[i - 1].m_aName;
				if(str_comp(GpuList.m_vGpus[i - 1].m_aName, g_Config.m_GfxGpuName) == 0)
				{
					OldSelectedGpu = i;
				}
			}
		}

		static int s_OldSelectedGpu = -1;
		if(s_OldSelectedGpu == -1)
			s_OldSelectedGpu = OldSelectedGpu;

		static CUi::SDropDownState s_GpuDropDownState;
		static CScrollRegion s_GpuDropDownScrollRegion;
		s_GpuDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_GpuDropDownScrollRegion;
		const int NewGpu = Ui()->DoDropDown(&GpuDropDown, OldSelectedGpu, s_vpGpuIdNames.data(), GpuCount, s_GpuDropDownState);
		if(OldSelectedGpu != NewGpu)
		{
			if(NewGpu == 0)
				str_copy(g_Config.m_GfxGpuName, "auto");
			else
				str_copy(g_Config.m_GfxGpuName, GpuList.m_vGpus[NewGpu - 1].m_aName);
			CheckSettings = true;
			s_GfxGpuChanged = NewGpu != s_OldSelectedGpu;
		}
	}

	// check if the new settings require a restart
	if(CheckSettings)
	{
		m_NeedRestartGraphics = !(s_GfxFsaaSamples == g_Config.m_GfxFsaaSamples &&
					  !s_GfxBackendChanged &&
					  !s_GfxGpuChanged);
	}
}



/* Settings section: sound */
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus.h"

#include <engine/shared/config.h>
#include <engine/sound.h>

#include <game/localization.h>

void CMenus::RenderSettingsSound(CUIRect MainView)
{
	CUIRect Button;
	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndEnable, Localize("Use sounds"), g_Config.m_SndEnable, &Button))
	{
		g_Config.m_SndEnable ^= 1;
		UpdateMusicState();
	}

	m_NeedRestartSound = g_Config.m_SndEnable && !Sound()->IsSoundEnabled();

	if(!g_Config.m_SndEnable)
		return;

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndMusic, Localize("Play background music"), g_Config.m_SndMusic, &Button))
	{
		g_Config.m_SndMusic ^= 1;
		UpdateMusicState();
	}

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndNonactiveMute, Localize("Mute when not active"), g_Config.m_SndNonactiveMute, &Button))
		g_Config.m_SndNonactiveMute ^= 1;

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndGame, Localize("Enable game sounds"), g_Config.m_SndGame, &Button))
		g_Config.m_SndGame ^= 1;

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndGun, Localize("Enable gun sound"), g_Config.m_SndGun, &Button))
		g_Config.m_SndGun ^= 1;

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndLongPain, Localize("Enable long pain sound (used when shooting in freeze)"), g_Config.m_SndLongPain, &Button))
		g_Config.m_SndLongPain ^= 1;

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndServerMessage, Localize("Enable server message sound"), g_Config.m_SndServerMessage, &Button))
		g_Config.m_SndServerMessage ^= 1;

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndChat, Localize("Enable regular chat sound"), g_Config.m_SndChat, &Button))
		g_Config.m_SndChat ^= 1;

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndTeamChat, Localize("Enable team chat sound"), g_Config.m_SndTeamChat, &Button))
		g_Config.m_SndTeamChat ^= 1;

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndHighlight, Localize("Enable highlighted chat sound"), g_Config.m_SndHighlight, &Button))
		g_Config.m_SndHighlight ^= 1;

	// volume slider
	{
		MainView.HSplitTop(5.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_SndVolume, &g_Config.m_SndVolume, &Button, Localize("Sound volume"), 0, 100, &CUi::ms_LogarithmicScrollbarScale, 0u, "%");
	}

	// volume slider game sounds
	{
		MainView.HSplitTop(5.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_SndGameVolume, &g_Config.m_SndGameVolume, &Button, Localize("Game sound volume"), 0, 100, &CUi::ms_LogarithmicScrollbarScale, 0u, "%");
	}

	// volume slider gui sounds
	{
		MainView.HSplitTop(5.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_SndChatVolume, &g_Config.m_SndChatVolume, &Button, Localize("Chat sound volume"), 0, 100, &CUi::ms_LogarithmicScrollbarScale, 0u, "%");
	}

	// volume slider map sounds
	{
		MainView.HSplitTop(5.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_SndMapVolume, &g_Config.m_SndMapVolume, &Button, Localize("Map sound volume"), 0, 100, &CUi::ms_LogarithmicScrollbarScale, 0u, "%");
	}

	// volume slider background music
	{
		MainView.HSplitTop(5.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_SndBackgroundMusicVolume, &g_Config.m_SndBackgroundMusicVolume, &Button, Localize("Background music volume"), 0, 100, &CUi::ms_LogarithmicScrollbarScale, 0u, "%");
	}
}



/* Settings section: language */
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus.h"

#include <base/str.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/countryflags.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

void CMenus::RenderLanguageSettings(CUIRect MainView)
{
	const float CreditsFontSize = 14.0f;
	const float CreditsMargin = 10.0f;

	CUIRect List, CreditsScroll;
	MainView.HSplitBottom(4.0f * CreditsFontSize + 2.0f * CreditsMargin, &List, &CreditsScroll);
	List.HSplitBottom(5.0f, &List, nullptr);

	RenderLanguageSelection(List);

	CreditsScroll.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 5.0f);

	static CScrollRegion s_CreditsScrollRegion;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = CreditsFontSize;
	s_CreditsScrollRegion.Begin(&CreditsScroll, &ScrollParams);

	CTextCursor Cursor;
	Cursor.m_FontSize = CreditsFontSize;
	Cursor.m_LineWidth = CreditsScroll.w - 2.0f * CreditsMargin;

	const unsigned OldRenderFlags = TextRender()->GetRenderFlags();
	TextRender()->SetRenderFlags(OldRenderFlags | TEXT_RENDER_FLAG_ONE_TIME_USE);
	STextContainerIndex CreditsTextContainer;
	TextRender()->CreateTextContainer(CreditsTextContainer, &Cursor, Localize("English translation by the DDNet Team", "Translation credits: Add your own name here when you update translations"));
	TextRender()->SetRenderFlags(OldRenderFlags);
	if(CreditsTextContainer.Valid())
	{
		CUIRect CreditsLabel;
		CreditsScroll.HSplitTop(TextRender()->GetBoundingBoxTextContainer(CreditsTextContainer).m_H + 2.0f * CreditsMargin, &CreditsLabel, &CreditsScroll);
		s_CreditsScrollRegion.AddRect(CreditsLabel);
		CreditsLabel.Margin(CreditsMargin, &CreditsLabel);
		TextRender()->RenderTextContainer(CreditsTextContainer, TextRender()->DefaultTextColor(), TextRender()->DefaultTextOutlineColor(), CreditsLabel.x, CreditsLabel.y);
		TextRender()->DeleteTextContainer(CreditsTextContainer);
	}

	s_CreditsScrollRegion.End();
}

bool CMenus::RenderLanguageSelection(CUIRect MainView)
{
	static int s_SelectedLanguage = -2; // -2 = unloaded, -1 = unset
	static CListBox s_ListBox;

	if(s_SelectedLanguage == -2)
	{
		s_SelectedLanguage = -1;
		for(size_t i = 0; i < g_Localization.Languages().size(); i++)
		{
			if(str_comp(g_Localization.Languages()[i].m_Filename.c_str(), g_Config.m_ClLanguagefile) == 0)
			{
				s_SelectedLanguage = i;
				s_ListBox.ScrollToSelected();
				break;
			}
		}
	}

	const int OldSelected = s_SelectedLanguage;

	s_ListBox.DoStart(24.0f, g_Localization.Languages().size(), 1, 3, s_SelectedLanguage, &MainView);

	for(const auto &Language : g_Localization.Languages())
	{
		const CListboxItem Item = s_ListBox.DoNextItem(&Language.m_Name, s_SelectedLanguage != -1 && !str_comp(g_Localization.Languages()[s_SelectedLanguage].m_Name.c_str(), Language.m_Name.c_str()));
		if(!Item.m_Visible)
			continue;

		CUIRect FlagRect, Label;
		Item.m_Rect.VSplitLeft(Item.m_Rect.h * 2.0f, &FlagRect, &Label);
		FlagRect.VMargin(6.0f, &FlagRect);
		FlagRect.HMargin(3.0f, &FlagRect);
		GameClient()->m_CountryFlags.Render(Language.m_CountryCode, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), FlagRect.x, FlagRect.y, FlagRect.w, FlagRect.h);

		Ui()->DoLabel(&Label, Language.m_Name.c_str(), 16.0f, TEXTALIGN_ML);
	}

	s_SelectedLanguage = s_ListBox.DoEnd();

	if(OldSelected != s_SelectedLanguage)
	{
		str_copy(g_Config.m_ClLanguagefile, g_Localization.Languages()[s_SelectedLanguage].m_Filename.c_str());
		GameClient()->OnLanguageChange();
	}

	return s_ListBox.WasItemActivated();
}



/* Settings section: DDNet */
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus.h"

#include <base/dbg.h>
#include <base/fs.h>
#include <base/str.h>

#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/components/background.h>
#include <game/client/components/mapimages.h>
#include <game/client/components/tooltips.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>

#include <algorithm>

void CMenus::RenderSettingsDDNet(CUIRect MainView)
{
	CUIRect Button, Left, Right, LeftLeft, Label;

	// demo
	CUIRect Demo;
	MainView.HSplitTop(110.0f, &Demo, &MainView);
	Demo.HSplitTop(30.0f, &Label, &Demo);
	Ui()->DoLabel(&Label, Localize("Demo"), 20.0f, TEXTALIGN_ML);
	Label.VSplitMid(nullptr, &Label, 20.0f);
	Ui()->DoLabel(&Label, Localize("Ghost"), 20.0f, TEXTALIGN_ML);

	Demo.HSplitTop(5.0f, nullptr, &Demo);
	Demo.VSplitMid(&Left, &Right, 20.0f);

	Left.HSplitTop(20.0f, &Button, &Left);
	if(DoButton_CheckBox(&g_Config.m_ClAutoRaceRecord, Localize("Save the best demo of each race"), g_Config.m_ClAutoRaceRecord, &Button))
	{
		g_Config.m_ClAutoRaceRecord ^= 1;
	}

	Left.HSplitTop(20.0f, &Button, &Left);
	if(DoButton_CheckBox(&g_Config.m_ClReplays, Localize("Enable replays"), g_Config.m_ClReplays, &Button))
	{
		g_Config.m_ClReplays ^= 1;
		if(Client()->State() == IClient::STATE_ONLINE)
		{
			Client()->DemoRecorder_UpdateReplayRecorder();
		}
	}

	Left.HSplitTop(20.0f, &Button, &Left);
	if(g_Config.m_ClReplays)
		Ui()->DoScrollbarOption(&g_Config.m_ClReplayLength, &g_Config.m_ClReplayLength, &Button, Localize("Default length"), 10, 600, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE);

	Right.HSplitTop(20.0f, &Button, &Right);
	if(DoButton_CheckBox(&g_Config.m_ClRaceGhost, Localize("Enable ghost"), g_Config.m_ClRaceGhost, &Button))
	{
		g_Config.m_ClRaceGhost ^= 1;
	}
	GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ClRaceGhost, &Button, Localize("When you cross the start line, show a ghost tee replicating the movements of your best time"));

	if(g_Config.m_ClRaceGhost)
	{
		Right.HSplitTop(20.0f, &Button, &Right);
		Button.VSplitMid(&LeftLeft, &Button);
		if(DoButton_CheckBox(&g_Config.m_ClRaceShowGhost, Localize("Show ghost"), g_Config.m_ClRaceShowGhost, &LeftLeft))
		{
			g_Config.m_ClRaceShowGhost ^= 1;
		}
		Ui()->DoScrollbarOption(&g_Config.m_ClRaceGhostAlpha, &g_Config.m_ClRaceGhostAlpha, &Button, Localize("Opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");

		Right.HSplitTop(20.0f, &Button, &Right);
		if(DoButton_CheckBox(&g_Config.m_ClRaceSaveGhost, Localize("Save ghost"), g_Config.m_ClRaceSaveGhost, &Button))
		{
			g_Config.m_ClRaceSaveGhost ^= 1;
		}

		if(g_Config.m_ClRaceSaveGhost)
		{
			Right.HSplitTop(20.0f, &Button, &Right);
			if(DoButton_CheckBox(&g_Config.m_ClRaceGhostSaveBest, Localize("Only save improvements"), g_Config.m_ClRaceGhostSaveBest, &Button))
			{
				g_Config.m_ClRaceGhostSaveBest ^= 1;
			}
		}
	}

	// gameplay
	CUIRect Gameplay;
	MainView.HSplitTop(190.0f, &Gameplay, &MainView);
	Gameplay.HSplitTop(30.0f, &Label, &Gameplay);
	Ui()->DoLabel(&Label, Localize("Gameplay"), 20.0f, TEXTALIGN_ML);
	Gameplay.HSplitTop(5.0f, nullptr, &Gameplay);
	Gameplay.VSplitMid(&Left, &Right, 20.0f);

	Left.HSplitTop(20.0f, &Button, &Left);
	Ui()->DoScrollbarOption(&g_Config.m_ClOverlayEntities, &g_Config.m_ClOverlayEntities, &Button, Localize("Overlay entities"), 0, 100);

	Left.HSplitTop(20.0f, &Button, &Left);
	Button.VSplitMid(&LeftLeft, &Button);

	if(DoButton_CheckBox(&g_Config.m_ClTextEntities, Localize("Show text entities"), g_Config.m_ClTextEntities, &LeftLeft))
		g_Config.m_ClTextEntities ^= 1;

	if(g_Config.m_ClTextEntities)
	{
		if(Ui()->DoScrollbarOption(&g_Config.m_ClTextEntitiesSize, &g_Config.m_ClTextEntitiesSize, &Button, Localize("Size"), 20, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE))
			GameClient()->m_MapImages.SetTextureScale(g_Config.m_ClTextEntitiesSize);
	}

	Left.HSplitTop(20.0f, &Button, &Left);
	Button.VSplitMid(&LeftLeft, &Button);

	if(DoButton_CheckBox(&g_Config.m_ClShowOthers, Localize("Show others"), g_Config.m_ClShowOthers == SHOW_OTHERS_ON, &LeftLeft))
		g_Config.m_ClShowOthers = g_Config.m_ClShowOthers != SHOW_OTHERS_ON ? SHOW_OTHERS_ON : SHOW_OTHERS_OFF;

	Ui()->DoScrollbarOption(&g_Config.m_ClShowOthersAlpha, &g_Config.m_ClShowOthersAlpha, &Button, Localize("Opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");

	GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ClShowOthersAlpha, &Button, Localize("Adjust the opacity of entities belonging to other teams, such as tees and name plates"));

	Left.HSplitTop(20.0f, &Button, &Left);
	static int s_ShowOwnTeamId = 0;
	if(DoButton_CheckBox(&s_ShowOwnTeamId, Localize("Show others (own team only)"), g_Config.m_ClShowOthers == SHOW_OTHERS_ONLY_TEAM, &Button))
	{
		g_Config.m_ClShowOthers = g_Config.m_ClShowOthers != SHOW_OTHERS_ONLY_TEAM ? SHOW_OTHERS_ONLY_TEAM : SHOW_OTHERS_OFF;
	}

	Left.HSplitTop(20.0f, &Button, &Left);
	if(DoButton_CheckBox(&g_Config.m_ClShowQuads, Localize("Show background quads"), g_Config.m_ClShowQuads, &Button))
	{
		g_Config.m_ClShowQuads ^= 1;
	}
	GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ClShowQuads, &Button, Localize("Quads are used for background decoration"));

	Left.HSplitTop(20.0f, &Button, &Left);
	if(Ui()->DoScrollbarOption(&g_Config.m_ClDefaultZoom, &g_Config.m_ClDefaultZoom, &Button, Localize("Default zoom"), 0, 20))
		GameClient()->m_Camera.SetZoom(CCamera::ZoomStepsToValue(g_Config.m_ClDefaultZoom - 10), g_Config.m_ClSmoothZoomTime, true);

	Right.HSplitTop(20.0f, &Button, &Right);
	DoSliderWithDividedValue(&g_Config.m_ClPredictionMargin, &g_Config.m_ClPredictionMargin, &Button, "Prediction margin", 10, 3000, 10, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms", true);

	Right.HSplitTop(20.0f, &Button, &Right);
	if(DoButton_CheckBox(&g_Config.m_ClPredictEvents, Localize("Predict events (experimental)"), g_Config.m_ClPredictEvents, &Button))
	{
		g_Config.m_ClPredictEvents ^= 1;
	}

	Right.HSplitTop(20.0f, &Button, &Right);
	if(DoButton_CheckBox(&g_Config.m_ClAntiPing, Localize("AntiPing"), g_Config.m_ClAntiPing, &Button))
	{
		g_Config.m_ClAntiPing ^= 1;
	}
	GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ClAntiPing, &Button, Localize("Tries to predict other entities to give a feel of low latency"));

	if(g_Config.m_ClAntiPing)
	{
		Right.HSplitTop(20.0f, &Button, &Right);
		if(DoButton_CheckBox(&g_Config.m_ClAntiPingPlayers, Localize("AntiPing: predict other players"), g_Config.m_ClAntiPingPlayers, &Button))
			g_Config.m_ClAntiPingPlayers ^= 1;

		Right.HSplitTop(20.0f, &Button, &Right);
		if(DoButton_CheckBox(&g_Config.m_ClAntiPingWeapons, Localize("AntiPing: predict weapons"), g_Config.m_ClAntiPingWeapons, &Button))
		{
			g_Config.m_ClAntiPingWeapons ^= 1;
		}

		Right.HSplitTop(20.0f, &Button, &Right);
		if(DoButton_CheckBox(&g_Config.m_ClAntiPingGrenade, Localize("AntiPing: predict grenade paths"), g_Config.m_ClAntiPingGrenade, &Button))
		{
			g_Config.m_ClAntiPingGrenade ^= 1;
		}
	}

	CUIRect Background, Miscellaneous;
	MainView.VSplitMid(&Background, &Miscellaneous, 20.0f);

	// background
	Background.HSplitTop(30.0f, &Label, &Background);
	Background.HSplitTop(5.0f, nullptr, &Background);
	Ui()->DoLabel(&Label, Localize("Background"), 20.0f, TEXTALIGN_ML);

	ColorRGBA GreyDefault(0.5f, 0.5f, 0.5f, 1);

	static CButtonContainer s_ResetId1;
	DoLine_ColorPicker(&s_ResetId1, 25.0f, 13.0f, 5.0f, &Background, Localize("Regular background color"), &g_Config.m_ClBackgroundColor, GreyDefault, false);

	static CButtonContainer s_ResetId2;
	static CButtonContainer s_RandomizeId;
	DoLine_ColorPicker(&s_ResetId2, 25.0f, 13.0f, 5.0f, &Background, Localize("Entities background color"), &g_Config.m_ClBackgroundEntitiesColor, GreyDefault, false, nullptr, false, &s_RandomizeId);

	CUIRect EditBox, ReloadButton;
	Background.HSplitTop(20.0f, &Label, &Background);
	Background.HSplitTop(2.0f, nullptr, &Background);
	Label.VSplitLeft(100.0f, &Label, &EditBox);
	EditBox.VSplitRight(60.0f, &EditBox, &Button);
	Button.VSplitMid(&ReloadButton, &Button, 5.0f);
	EditBox.VSplitRight(5.0f, &EditBox, nullptr);

	Ui()->DoLabel(&Label, Localize("Map"), 14.0f, TEXTALIGN_ML);

	static CLineInput s_BackgroundEntitiesInput(g_Config.m_ClBackgroundEntities, sizeof(g_Config.m_ClBackgroundEntities));
	Ui()->DoEditBox(&s_BackgroundEntitiesInput, &EditBox, 14.0f);

	static CButtonContainer s_BackgroundEntitiesMapPicker, s_BackgroundEntitiesReload;

	if(Ui()->DoButton_FontIcon(&s_BackgroundEntitiesReload, FontIcon::ARROW_ROTATE_RIGHT, 0, &ReloadButton, BUTTONFLAG_LEFT))
	{
		GameClient()->m_Background.LoadBackground();
	}

	if(Ui()->DoButton_FontIcon(&s_BackgroundEntitiesMapPicker, FontIcon::FOLDER, 0, &Button, BUTTONFLAG_LEFT))
	{
		static SPopupMenuId s_PopupMapPickerId;
		static CPopupMapPickerContext s_PopupMapPickerContext;
		s_PopupMapPickerContext.m_pMenus = this;
		s_PopupMapPickerContext.MapListPopulate();
		Ui()->DoPopupMenu(&s_PopupMapPickerId, Ui()->MouseX(), Ui()->MouseY(), 300.0f, 250.0f, &s_PopupMapPickerContext, PopupMapPicker);
	}

	Background.HSplitTop(20.0f, &Button, &Background);
	const bool UseCurrentMap = str_comp(g_Config.m_ClBackgroundEntities, CURRENT_MAP) == 0;
	static int s_UseCurrentMapId = 0;
	if(DoButton_CheckBox(&s_UseCurrentMapId, Localize("Use current map as background"), UseCurrentMap, &Button))
	{
		if(UseCurrentMap)
			g_Config.m_ClBackgroundEntities[0] = '\0';
		else
			str_copy(g_Config.m_ClBackgroundEntities, CURRENT_MAP);
		GameClient()->m_Background.LoadBackground();
	}

	Background.HSplitTop(20.0f, &Button, &Background);
	if(DoButton_CheckBox(&g_Config.m_ClBackgroundShowTilesLayers, Localize("Show tiles layers from BG map"), g_Config.m_ClBackgroundShowTilesLayers, &Button))
		g_Config.m_ClBackgroundShowTilesLayers ^= 1;

	// miscellaneous
	Miscellaneous.HSplitTop(30.0f, &Label, &Miscellaneous);
	Miscellaneous.HSplitTop(5.0f, nullptr, &Miscellaneous);

	Ui()->DoLabel(&Label, Localize("Miscellaneous"), 20.0f, TEXTALIGN_ML);

	static CButtonContainer s_ButtonTimeout;
	Miscellaneous.HSplitTop(20.0f, &Button, &Miscellaneous);
	if(DoButton_Menu(&s_ButtonTimeout, Localize("New random timeout code"), 0, &Button))
	{
		Client()->GenerateTimeoutSeed();
	}

	Miscellaneous.HSplitTop(5.0f, nullptr, &Miscellaneous);
	Miscellaneous.HSplitTop(20.0f, &Label, &Miscellaneous);
	Miscellaneous.HSplitTop(2.0f, nullptr, &Miscellaneous);
	Ui()->DoLabel(&Label, Localize("Run on join"), 14.0f, TEXTALIGN_ML);
	Miscellaneous.HSplitTop(20.0f, &Button, &Miscellaneous);
	static CLineInput s_RunOnJoinInput(g_Config.m_ClRunOnJoin, sizeof(g_Config.m_ClRunOnJoin));
	s_RunOnJoinInput.SetEmptyText(Localize("Chat command (e.g. showall 1)"));
	Ui()->DoEditBox(&s_RunOnJoinInput, &Button, 14.0f);

#if defined(CONF_FAMILY_WINDOWS)
	static CButtonContainer s_ButtonUnregisterShell;
	Miscellaneous.HSplitTop(10.0f, nullptr, &Miscellaneous);
	Miscellaneous.HSplitTop(20.0f, &Button, &Miscellaneous);
	if(DoButton_Menu(&s_ButtonUnregisterShell, Localize("Unregister protocol and file extensions"), 0, &Button))
	{
		Client()->ShellUnregister();
	}
#endif

}

CUi::EPopupMenuFunctionResult CMenus::PopupMapPicker(void *pContext, CUIRect View, bool Active)
{
	CPopupMapPickerContext *pPopupContext = static_cast<CPopupMapPickerContext *>(pContext);
	CMenus *pMenus = pPopupContext->m_pMenus;

	static CListBox s_ListBox;
	s_ListBox.SetActive(Active);
	s_ListBox.DoStart(20.0f, pPopupContext->m_vMaps.size(), 1, 3, -1, &View, false);

	int MapIndex = 0;
	for(auto &Map : pPopupContext->m_vMaps)
	{
		MapIndex++;
		const CListboxItem Item = s_ListBox.DoNextItem(&Map, MapIndex == pPopupContext->m_Selection);
		if(!Item.m_Visible)
			continue;

		CUIRect Label, Icon;
		Item.m_Rect.VSplitLeft(20.0f, &Icon, &Label);

		char aLabelText[IO_MAX_PATH_LENGTH];
		str_copy(aLabelText, Map.m_aFilename);
		if(Map.m_IsDirectory)
			str_append(aLabelText, "/");

		const char *pIconType;
		if(!Map.m_IsDirectory)
		{
			pIconType = FontIcon::MAP;
		}
		else
		{
			if(!str_comp(Map.m_aFilename, ".."))
				pIconType = FontIcon::FOLDER_TREE;
			else
				pIconType = FontIcon::FOLDER;
		}

		pMenus->TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		pMenus->TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
		pMenus->Ui()->DoLabel(&Icon, pIconType, 12.0f, TEXTALIGN_ML);
		pMenus->TextRender()->SetRenderFlags(0);
		pMenus->TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

		pMenus->Ui()->DoLabel(&Label, aLabelText, 10.0f, TEXTALIGN_ML);
	}

	const int NewSelected = s_ListBox.DoEnd();
	pPopupContext->m_Selection = NewSelected >= 0 ? NewSelected : -1;
	if(s_ListBox.WasItemSelected() || s_ListBox.WasItemActivated())
	{
		const CMapListItem &SelectedItem = pPopupContext->m_vMaps[pPopupContext->m_Selection];

		if(SelectedItem.m_IsDirectory)
		{
			if(!str_comp(SelectedItem.m_aFilename, ".."))
			{
				dbg_assert(fs_parent_dir(pPopupContext->m_aCurrentMapFolder) == 0, "Parent folder item selected but there is no parent folder");
			}
			else
			{
				str_append(pPopupContext->m_aCurrentMapFolder, "/");
				str_append(pPopupContext->m_aCurrentMapFolder, SelectedItem.m_aFilename);
			}
			pPopupContext->MapListPopulate();
		}
		else
		{
			str_format(g_Config.m_ClBackgroundEntities, sizeof(g_Config.m_ClBackgroundEntities), "%s/%s", pPopupContext->m_aCurrentMapFolder, SelectedItem.m_aFilename);
			pMenus->GameClient()->m_Background.LoadBackground();
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}

	return CUi::POPUP_KEEP_OPEN;
}

void CMenus::CPopupMapPickerContext::MapListPopulate()
{
	m_vMaps.clear();
	char aTemp[IO_MAX_PATH_LENGTH];
	str_format(aTemp, sizeof(aTemp), "maps/%s", m_aCurrentMapFolder);
	m_pMenus->Storage()->ListDirectoryInfo(IStorage::TYPE_ALL, aTemp, MapListFetchCallback, this);
	std::stable_sort(m_vMaps.begin(), m_vMaps.end(), CompareFilenameAscending);
	m_Selection = -1;
}

int CMenus::CPopupMapPickerContext::MapListFetchCallback(const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser)
{
	CPopupMapPickerContext *pRealUser = (CPopupMapPickerContext *)pUser;
	if((!IsDir && !str_endswith(pInfo->m_pName, ".map")) || !str_comp(pInfo->m_pName, ".") || (!str_comp(pInfo->m_pName, "..") && (!str_comp(pRealUser->m_aCurrentMapFolder, ""))))
		return 0;

	CMapListItem Item;
	str_copy(Item.m_aFilename, pInfo->m_pName);
	Item.m_IsDirectory = IsDir;

	pRealUser->m_vMaps.emplace_back(Item);

	return 0;
}



/* Settings section: assets */
#include "menus.h"
#include "asset_organizer.h"

#include <base/log.h>
#include <base/str.h>
#include <base/time.h>

#include <game/mapitems.h>

#include <engine/font_icons.h>
#include <engine/config.h>
#include <engine/image.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <generated/client_data.h>

#include <game/client/gameclient.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>

#include <algorithm>
#include <chrono>
#include <unordered_set>

using namespace std::chrono_literals;

typedef std::function<void()> TMenuAssetScanLoadedFunc;

struct SMenuAssetScanUser
{
	void *m_pUser;
	TMenuAssetScanLoadedFunc m_LoadedFunc;
	std::unordered_set<std::string> *m_pKnownAssetIds = nullptr;
};

// The game asset is the weapon/game texture pack. Packs may live below nested
// folders on disk, but their relative path remains the stable asset ID. The
// Asset Organizer keeps its separate user-facing folder/page arrangement in
// config, so it never needs to move these resource files.
struct SGameAssetScanUser
{
	CMenus *m_pMenus;
	TMenuAssetScanLoadedFunc m_LoadedFunc;
	std::unordered_set<std::string> *m_pKnownPackIds = nullptr;
	char m_aRelativePath[IO_MAX_PATH_LENGTH] = {};
};

// IDs of the tabs in the Assets menu
enum
{
	ASSETS_TAB_ENTITIES = 0,
	ASSETS_TAB_GAME = 1,
	ASSETS_TAB_EMOTICONS = 2,
	ASSETS_TAB_PARTICLES = 3,
	ASSETS_TAB_HUD = 4,
	ASSETS_TAB_EXTRAS = 5,
	ASSETS_TAB_CURSOR = 6,
	ASSETS_TAB_ARROW = 7,
	ASSETS_TAB_AUDIO = 8,
	NUMBER_OF_ASSETS_TABS = 9,
};

// Preview textures have different transparent bounds from their visual content.
// Keep these corrections local to the organizer previews: gameplay rendering
// continues to use the unmodified native or custom texture paths.
static vec2 AssetPreviewOpticalOffset(int Tab, const char *pName, float Width, float Height)
{
	if(pName == nullptr || str_comp(pName, "default") != 0)
		return vec2(0.0f, 0.0f);
	if(Tab == ASSETS_TAB_CURSOR)
		// Alpha-weighted centroid of data/gui_cursor.png: (12.949, 29.410).
		return vec2(Width * (19.051f / 64.0f), Height * (2.590f / 64.0f));
	if(Tab == ASSETS_TAB_ARROW)
		// Alpha-weighted centroid of data/arrow.png: (23.125, 25.000).
		return vec2(Width * (0.875f / 48.0f), 0.0f);
	return vec2(0.0f, 0.0f);
}

// The alpha-weighted center of Font Awesome's MUSIC glyph is offset from its
// advance box. Scale the correction with the rendered font size so compact
// cards and the larger drag ghost share the same visual center.
static vec2 AudioPreviewOpticalOffset(float FontSize)
{
	return vec2(FontSize * (-12.943f / 160.0f), FontSize * (0.642f / 160.0f));
}

void CMenus::LoadEntities(SCustomEntities *pEntitiesItem, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;

	char aPath[IO_MAX_PATH_LENGTH];
	if(str_comp(pEntitiesItem->m_aName, "default") == 0)
	{
		for(int i = 0; i < MAP_IMAGE_MOD_TYPE_COUNT; ++i)
		{
			str_format(aPath, sizeof(aPath), "editor/entities_clear/%s.png", gs_apModEntitiesNames[i]);
			pEntitiesItem->m_aImages[i].m_Texture = pThis->Graphics()->LoadTexture(aPath, IStorage::TYPE_ALL);
			if(!pEntitiesItem->m_RenderTexture.IsValid() || pEntitiesItem->m_RenderTexture.IsNullTexture())
				pEntitiesItem->m_RenderTexture = pEntitiesItem->m_aImages[i].m_Texture;
		}
	}
	else
	{
		// Cache the flat-file fallback so packs without per-gametype variants don't get re-uploaded to the GPU MAP_IMAGE_MOD_TYPE_COUNT times.
		IGraphics::CTextureHandle FallbackTexture;
		bool FallbackAttempted = false;
		for(int i = 0; i < MAP_IMAGE_MOD_TYPE_COUNT; ++i)
		{
			str_format(aPath, sizeof(aPath), "assets/entities/%s/%s.png", pEntitiesItem->m_aName, gs_apModEntitiesNames[i]);
			pEntitiesItem->m_aImages[i].m_Texture = pThis->Graphics()->LoadTexture(aPath, IStorage::TYPE_ALL);
			if(pEntitiesItem->m_aImages[i].m_Texture.IsNullTexture())
			{
				if(!FallbackAttempted)
				{
					str_format(aPath, sizeof(aPath), "assets/entities/%s.png", pEntitiesItem->m_aName);
					FallbackTexture = pThis->Graphics()->LoadTexture(aPath, IStorage::TYPE_ALL);
					FallbackAttempted = true;
				}
				pEntitiesItem->m_aImages[i].m_Texture = FallbackTexture;
			}
			if(!pEntitiesItem->m_RenderTexture.IsValid() || pEntitiesItem->m_RenderTexture.IsNullTexture())
				pEntitiesItem->m_RenderTexture = pEntitiesItem->m_aImages[i].m_Texture;
		}
	}
}

int CMenus::EntitiesScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;

	auto Register = [&](const char *pItemName) {
		if(pRealUser->m_pKnownAssetIds)
			return pRealUser->m_pKnownAssetIds->emplace(pItemName).second;
		for(const auto &Item : pThis->m_vEntitiesList)
			if(str_comp(Item.m_aName, pItemName) == 0)
				return false;
		return true;
	};

	if(IsDir)
	{
		if(pName[0] == '.')
			return 0;

		// default is reserved
		if(str_comp(pName, "default") == 0)
			return 0;
		if(!Register(pName))
			return 0;

		SCustomEntities EntitiesItem;
		str_copy(EntitiesItem.m_aName, pName);
		CMenus::LoadEntities(&EntitiesItem, pUser);
		pThis->m_vEntitiesList.push_back(EntitiesItem);
	}
	else
	{
		if(str_endswith(pName, ".png"))
		{
			char aName[IO_MAX_PATH_LENGTH];
			str_truncate(aName, sizeof(aName), pName, str_length(pName) - 4);
			// default is reserved
			if(str_comp(aName, "default") == 0)
				return 0;
			if(!Register(aName))
				return 0;

			SCustomEntities EntitiesItem;
			str_copy(EntitiesItem.m_aName, aName);
			CMenus::LoadEntities(&EntitiesItem, pUser);
			pThis->m_vEntitiesList.push_back(EntitiesItem);
		}
	}

	pRealUser->m_LoadedFunc();

	return 0;
}

template<typename TName>
static void LoadAsset(TName *pAssetItem, const char *pAssetName, IGraphics *pGraphics)
{
	char aPath[IO_MAX_PATH_LENGTH];
	auto LoadPreviewTexture = [&](const char *pPath) {
		CImageInfo ImageInfo;
		if(pGraphics->LoadPng(ImageInfo, pPath, IStorage::TYPE_ALL))
		{
			if(ImageInfo.m_Width > 0 && ImageInfo.m_Height > 0)
				pAssetItem->m_PreviewAspect = (float)ImageInfo.m_Width / (float)ImageInfo.m_Height;
			pAssetItem->m_RenderTexture = pGraphics->LoadTextureRawMove(ImageInfo, 0, pPath);
			if(pAssetItem->m_RenderTexture.IsValid())
				return;
		}

		// Keep the original failure semantics (including the null texture) for
		// malformed or unavailable files. Successful assets take the one-decode path above.
		pAssetItem->m_RenderTexture = pGraphics->LoadTexture(pPath, IStorage::TYPE_ALL);
	};
	if(str_comp(pAssetItem->m_aName, "default") == 0)
	{
		str_format(aPath, sizeof(aPath), "%s.png", pAssetName);
		LoadPreviewTexture(aPath);
	}
	else
	{
		str_format(aPath, sizeof(aPath), "assets/%s/%s.png", pAssetName, pAssetItem->m_aName);
		LoadPreviewTexture(aPath);
		if(pAssetItem->m_RenderTexture.IsNullTexture())
		{
			str_format(aPath, sizeof(aPath), "assets/%s/%s/%s.png", pAssetName, pAssetItem->m_aName, pAssetName);
			LoadPreviewTexture(aPath);
		}
	}
}

template<typename TName>
static int AssetScan(const char *pName, int IsDir, int DirType, std::vector<TName> &vAssetList, const char *pAssetName, IGraphics *pGraphics, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;

	auto Register = [&](const char *pItemName) {
		if(pRealUser->m_pKnownAssetIds)
			return pRealUser->m_pKnownAssetIds->emplace(pItemName).second;
		for(const auto &Item : vAssetList)
			if(str_comp(Item.m_aName, pItemName) == 0)
				return false;
		return true;
	};

	if(IsDir)
	{
		if(pName[0] == '.')
			return 0;

		// default is reserved
		if(str_comp(pName, "default") == 0)
			return 0;
		if(!Register(pName))
			return 0;

		TName AssetItem;
		str_copy(AssetItem.m_aName, pName);
		LoadAsset(&AssetItem, pAssetName, pGraphics);
		vAssetList.push_back(AssetItem);
	}
	else
	{
		if(str_endswith(pName, ".png"))
		{
			char aName[IO_MAX_PATH_LENGTH];
			str_truncate(aName, sizeof(aName), pName, str_length(pName) - 4);
			// default is reserved
			if(str_comp(aName, "default") == 0)
				return 0;
			if(!Register(aName))
				return 0;

			TName AssetItem;
			str_copy(AssetItem.m_aName, aName);
			LoadAsset(&AssetItem, pAssetName, pGraphics);
			vAssetList.push_back(AssetItem);
		}
	}

	pRealUser->m_LoadedFunc();

	return 0;
}

int CMenus::GameScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pScanUser = static_cast<SGameAssetScanUser *>(pUser);
	CMenus *pThis = pScanUser->m_pMenus;

	auto AddPack = [pThis, pScanUser](const char *pRelativeName) {
		if(pRelativeName[0] == '\0' || str_length(pRelativeName) >= CMenus::SCustomItem::MAX_NAME_LENGTH)
			return;

		if(pScanUser->m_pKnownPackIds)
		{
			if(!pScanUser->m_pKnownPackIds->emplace(pRelativeName).second)
				return;
		}
		else
		{
			for(const auto &Item : pThis->m_vGameList)
				if(str_comp(Item.m_aName, pRelativeName) == 0)
					return;
		}

		SCustomGame GameItem;
		str_copy(GameItem.m_aName, pRelativeName);
		LoadAsset(&GameItem, "game", pThis->Graphics());
		pThis->m_vGameList.push_back(GameItem);
	};

	if(IsDir)
	{
		if(pName[0] == '.')
			return 0;

		char aRelativePath[IO_MAX_PATH_LENGTH];
		if(pScanUser->m_aRelativePath[0] == '\0')
			str_copy(aRelativePath, pName);
		else
			str_format(aRelativePath, sizeof(aRelativePath), "%s/%s", pScanUser->m_aRelativePath, pName);

		char aDirectoryPath[IO_MAX_PATH_LENGTH];
		str_format(aDirectoryPath, sizeof(aDirectoryPath), "assets/game/%s", aRelativePath);
		char aGamePath[IO_MAX_PATH_LENGTH];
		str_format(aGamePath, sizeof(aGamePath), "%s/%s", aDirectoryPath, g_pData->m_aImages[IMAGE_GAME].m_pFilename);
		if(pThis->Storage()->FileExists(aGamePath, IStorage::TYPE_ALL))
		{
			AddPack(aRelativePath);
		}
		else
		{
			// ListDirectory is synchronous. The child context is therefore valid
			// for the entire recursive call and no persistent scan state is needed.
			SGameAssetScanUser ChildUser = *pScanUser;
			str_copy(ChildUser.m_aRelativePath, aRelativePath);
			pThis->Storage()->ListDirectory(IStorage::TYPE_ALL, aDirectoryPath, GameScan, &ChildUser);
		}
	}
	else if(str_endswith(pName, ".png"))
	{
		char aPackName[IO_MAX_PATH_LENGTH];
		str_truncate(aPackName, sizeof(aPackName), pName, str_length(pName) - 4);
		char aRelativeName[IO_MAX_PATH_LENGTH];
		if(pScanUser->m_aRelativePath[0] == '\0')
			str_copy(aRelativeName, aPackName);
		else
			str_format(aRelativeName, sizeof(aRelativeName), "%s/%s", pScanUser->m_aRelativePath, aPackName);
		AddPack(aRelativeName);
	}

	pScanUser->m_LoadedFunc();
	return 0;
}

int CMenus::EmoticonsScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	IGraphics *pGraphics = pThis->Graphics();
	return AssetScan(pName, IsDir, DirType, pThis->m_vEmoticonList, "emoticons", pGraphics, pUser);
}

int CMenus::ParticlesScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	IGraphics *pGraphics = pThis->Graphics();
	return AssetScan(pName, IsDir, DirType, pThis->m_vParticlesList, "particles", pGraphics, pUser);
}

int CMenus::HudScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	IGraphics *pGraphics = pThis->Graphics();
	return AssetScan(pName, IsDir, DirType, pThis->m_vHudList, "hud", pGraphics, pUser);
}

int CMenus::ExtrasScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	IGraphics *pGraphics = pThis->Graphics();
	return AssetScan(pName, IsDir, DirType, pThis->m_vExtrasList, "extras", pGraphics, pUser);
}

static int FavoriteAssetTabFromString(const char *pTab)
{
	if(str_comp_nocase(pTab, "entities") == 0)
		return ASSETS_TAB_ENTITIES;
	if(str_comp_nocase(pTab, "game") == 0)
		return ASSETS_TAB_GAME;
	if(str_comp_nocase(pTab, "emoticons") == 0)
		return ASSETS_TAB_EMOTICONS;
	if(str_comp_nocase(pTab, "particles") == 0)
		return ASSETS_TAB_PARTICLES;
	if(str_comp_nocase(pTab, "hud") == 0)
		return ASSETS_TAB_HUD;
	if(str_comp_nocase(pTab, "extras") == 0)
		return ASSETS_TAB_EXTRAS;
	if(str_comp_nocase(pTab, "cursor") == 0)
		return ASSETS_TAB_CURSOR;
	if(str_comp_nocase(pTab, "arrow") == 0)
		return ASSETS_TAB_ARROW;
	if(str_comp_nocase(pTab, "audio") == 0)
		return ASSETS_TAB_AUDIO;
	return -1;
}

static const char *FavoriteAssetTabToString(int Tab)
{
	switch(Tab)
	{
	case ASSETS_TAB_ENTITIES:
		return "entities";
	case ASSETS_TAB_GAME:
		return "game";
	case ASSETS_TAB_EMOTICONS:
		return "emoticons";
	case ASSETS_TAB_PARTICLES:
		return "particles";
	case ASSETS_TAB_HUD:
		return "hud";
	case ASSETS_TAB_EXTRAS:
		return "extras";
	case ASSETS_TAB_CURSOR:
		return "cursor";
	case ASSETS_TAB_ARROW:
		return "arrow";
	case ASSETS_TAB_AUDIO:
		return "audio";
	default:
		return "";
	}
}

static void LoadCursorPreview(CMenus::SCustomCursor *pCursorItem, IGraphics *pGraphics)
{
	char aPath[IO_MAX_PATH_LENGTH];
	if(str_comp(pCursorItem->m_aName, "default") == 0)
	{
		pCursorItem->m_RenderTexture = g_pData->m_aImages[IMAGE_CURSOR].m_Id;
		return;
	}

	str_format(aPath, sizeof(aPath), "assets/cursor/%s.png", pCursorItem->m_aName);
	pCursorItem->m_RenderTexture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
	if(pCursorItem->m_RenderTexture.IsNullTexture())
	{
		str_format(aPath, sizeof(aPath), "assets/cursor/%s/gui_cursor.png", pCursorItem->m_aName);
		pCursorItem->m_RenderTexture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
		if(pCursorItem->m_RenderTexture.IsNullTexture())
		{
			str_format(aPath, sizeof(aPath), "assets/cursor/%s/cursor.png", pCursorItem->m_aName);
			pCursorItem->m_RenderTexture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
		}
	}
}

int CMenus::CursorScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	IGraphics *pGraphics = pThis->Graphics();

	auto Register = [&](const char *pItemName) {
		if(pRealUser->m_pKnownAssetIds)
			return pRealUser->m_pKnownAssetIds->emplace(pItemName).second;
		for(const auto &Item : pThis->m_vCursorList)
			if(str_comp(Item.m_aName, pItemName) == 0)
				return false;
		return true;
	};

	if(IsDir)
	{
		if(pName[0] == '.')
			return 0;
		if(str_comp(pName, "default") == 0)
			return 0;
		if(!Register(pName))
			return 0;

		SCustomCursor CursorItem;
		str_copy(CursorItem.m_aName, pName);
		LoadCursorPreview(&CursorItem, pGraphics);
		pThis->m_vCursorList.push_back(CursorItem);
	}
	else
	{
		if(str_endswith(pName, ".png"))
		{
			char aName[IO_MAX_PATH_LENGTH];
			str_truncate(aName, sizeof(aName), pName, str_length(pName) - 4);
			if(str_comp(aName, "default") == 0)
				return 0;
			if(!Register(aName))
				return 0;

			SCustomCursor CursorItem;
			str_copy(CursorItem.m_aName, aName);
			LoadCursorPreview(&CursorItem, pGraphics);
			pThis->m_vCursorList.push_back(CursorItem);
		}
	}

	pRealUser->m_LoadedFunc();
	return 0;
}

static void LoadArrowPreview(CMenus::SCustomArrow *pArrowItem, IGraphics *pGraphics)
{
	char aPath[IO_MAX_PATH_LENGTH];
	if(str_comp(pArrowItem->m_aName, "default") == 0)
	{
		pArrowItem->m_RenderTexture = g_pData->m_aImages[IMAGE_ARROW].m_Id;
		return;
	}

	str_format(aPath, sizeof(aPath), "assets/arrow/%s.png", pArrowItem->m_aName);
	pArrowItem->m_RenderTexture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
	if(pArrowItem->m_RenderTexture.IsNullTexture())
	{
		str_format(aPath, sizeof(aPath), "assets/arrow/%s/arrow.png", pArrowItem->m_aName);
		pArrowItem->m_RenderTexture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
	}
}

int CMenus::ArrowScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	IGraphics *pGraphics = pThis->Graphics();

	auto Register = [&](const char *pItemName) {
		if(pRealUser->m_pKnownAssetIds)
			return pRealUser->m_pKnownAssetIds->emplace(pItemName).second;
		for(const auto &Item : pThis->m_vArrowList)
			if(str_comp(Item.m_aName, pItemName) == 0)
				return false;
		return true;
	};

	if(IsDir)
	{
		if(pName[0] == '.')
			return 0;
		if(str_comp(pName, "default") == 0)
			return 0;
		if(!Register(pName))
			return 0;

		SCustomArrow ArrowItem;
		str_copy(ArrowItem.m_aName, pName);
		LoadArrowPreview(&ArrowItem, pGraphics);
		pThis->m_vArrowList.push_back(ArrowItem);
	}
	else
	{
		if(str_endswith(pName, ".png"))
		{
			char aName[IO_MAX_PATH_LENGTH];
			str_truncate(aName, sizeof(aName), pName, str_length(pName) - 4);
			if(str_comp(aName, "default") == 0)
				return 0;
			if(!Register(aName))
				return 0;

			SCustomArrow ArrowItem;
			str_copy(ArrowItem.m_aName, aName);
			LoadArrowPreview(&ArrowItem, pGraphics);
			pThis->m_vArrowList.push_back(ArrowItem);
		}
	}

	pRealUser->m_LoadedFunc();
	return 0;
}

int CMenus::AudioPackScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;

	if(!IsDir || pName[0] == '.')
		return 0;
	if(str_comp(pName, "default") == 0)
		return 0;
	if(pRealUser->m_pKnownAssetIds)
	{
		if(!pRealUser->m_pKnownAssetIds->emplace(pName).second)
			return 0;
	}
	else
	{
		for(const auto &Item : pThis->m_vAudioPackList)
			if(str_comp(Item.m_aName, pName) == 0)
				return 0;
	}

	SCustomAudioPack PackItem;
	str_copy(PackItem.m_aName, pName);
	PackItem.m_RenderTexture = IGraphics::CTextureHandle();
	pThis->m_vAudioPackList.push_back(PackItem);

	pRealUser->m_LoadedFunc();
	return 0;
}

static void ClearCursorAssetList(std::vector<CMenus::SCustomCursor> &vList, IGraphics *pGraphics)
{
	for(CMenus::SCustomCursor &Asset : vList)
	{
		if(str_comp(Asset.m_aName, "default") == 0)
			continue;
		pGraphics->UnloadTexture(&Asset.m_RenderTexture);
	}
	vList.clear();
}

static void ClearArrowAssetList(std::vector<CMenus::SCustomArrow> &vList, IGraphics *pGraphics)
{
	for(CMenus::SCustomArrow &Asset : vList)
	{
		if(str_comp(Asset.m_aName, "default") == 0)
			continue;
		pGraphics->UnloadTexture(&Asset.m_RenderTexture);
	}
	vList.clear();
}

void CMenus::ConAddFavoriteAsset(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CMenus *>(pUserData);
	pSelf->SetAssetOrganizerFavorite(FavoriteAssetTabFromString(pResult->GetString(0)), pResult->GetString(1), true);
}

void CMenus::ConRemoveFavoriteAsset(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CMenus *>(pUserData);
	pSelf->SetAssetOrganizerFavorite(FavoriteAssetTabFromString(pResult->GetString(0)), pResult->GetString(1), false);
}

void CMenus::ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData)
{
	auto *pSelf = static_cast<CMenus *>(pUserData);
	pSelf->OnConfigSave(pConfigManager);
}

void CMenus::OnConfigSave(IConfigManager *pConfigManager)
{
	for(int Tab = 0; Tab < NUMBER_OF_ASSETS_TABS; ++Tab)
	{
		const char *pTabName = FavoriteAssetTabToString(Tab);
		for(const auto &Favorite : m_aAssetFavorites[Tab])
		{
			char aBuffer[IO_MAX_PATH_LENGTH + 64];
			const char *pEnd = aBuffer + sizeof(aBuffer) - 2;

			str_copy(aBuffer, "add_favorite_asset \"");
			char *pDst = aBuffer + str_length(aBuffer);
			str_escape(&pDst, pTabName, pEnd);
			str_append(aBuffer, "\" \"");
			pDst = aBuffer + str_length(aBuffer);
			str_escape(&pDst, Favorite.c_str(), pEnd);
			str_append(aBuffer, "\"");

			pConfigManager->WriteLine(aBuffer, ConfigDomain::TCLIENT);
		}
	}
}

static bool gs_aInitCustomList[NUMBER_OF_ASSETS_TABS] = {
	true,
};

void CMenus::AddFavoriteAsset(const char *pTab, const char *pName)
{
	AddFavoriteAsset(FavoriteAssetTabFromString(pTab), pName);
}

void CMenus::RemoveFavoriteAsset(const char *pTab, const char *pName)
{
	RemoveFavoriteAsset(FavoriteAssetTabFromString(pTab), pName);
}

void CMenus::AddFavoriteAsset(int Tab, const char *pName)
{
	if(Tab < 0 || Tab >= NUMBER_OF_ASSETS_TABS)
	{
		log_error("menus", "Invalid favorite asset tab '%d'", Tab);
		return;
	}
	if(pName[0] == '\0')
		return;

	const auto &[_, Inserted] = m_aAssetFavorites[Tab].emplace(pName);
	if(Inserted)
	{
		gs_aInitCustomList[Tab] = true;
	}
}

void CMenus::RemoveFavoriteAsset(int Tab, const char *pName)
{
	if(Tab < 0 || Tab >= NUMBER_OF_ASSETS_TABS)
	{
		log_error("menus", "Invalid favorite asset tab '%d'", Tab);
		return;
	}

	const auto FavoriteIt = m_aAssetFavorites[Tab].find(pName);
	if(FavoriteIt != m_aAssetFavorites[Tab].end())
	{
		m_aAssetFavorites[Tab].erase(FavoriteIt);
		gs_aInitCustomList[Tab] = true;
	}
}

void CMenus::SetAssetOrganizerFavorite(int Tab, const char *pName, bool Favorite)
{
	if(Tab < 0 || Tab >= NUMBER_OF_ASSETS_TABS || pName == nullptr || pName[0] == '\0')
		return;

	AssetOrganizer::SetFavorite(Tab, pName, Favorite);
	if(Favorite)
		AddFavoriteAsset(Tab, pName);
	else
		RemoveFavoriteAsset(Tab, pName);
}

bool CMenus::IsFavoriteAsset(int Tab, const char *pName) const
{
	return Tab >= 0 && Tab < NUMBER_OF_ASSETS_TABS && pName != nullptr && AssetOrganizer::IsFavorite(Tab, pName);
}

static void AssetsGetRelativePaths(int Tab, const char *pName, char *pFilePath, int FilePathSize, char *pFolderPath, int FolderPathSize, char *pAltFolderPath, int AltFolderPathSize)
{
	pFilePath[0] = '\0';
	pFolderPath[0] = '\0';
	if(pAltFolderPath != nullptr)
		pAltFolderPath[0] = '\0';

	const char *pTabFolder = FavoriteAssetTabToString(Tab);
	if(pTabFolder[0] == '\0' || pName == nullptr || pName[0] == '\0')
		return;

	if(Tab == ASSETS_TAB_AUDIO)
	{
		str_format(pFolderPath, FolderPathSize, "assets/audio/%s", pName);
		if(pAltFolderPath != nullptr)
			str_format(pAltFolderPath, AltFolderPathSize, "audio/%s", pName);
		return;
	}

	str_format(pFilePath, FilePathSize, "assets/%s/%s.png", pTabFolder, pName);
	str_format(pFolderPath, FolderPathSize, "assets/%s/%s", pTabFolder, pName);
}

bool CMenus::CanDeleteCustomAsset(int Tab, const char *pName) const
{
	if(Tab < 0 || Tab >= NUMBER_OF_ASSETS_TABS || pName == nullptr || pName[0] == '\0')
		return false;
	if(str_comp(pName, "default") == 0)
		return false;

	char aFilePath[IO_MAX_PATH_LENGTH];
	char aFolderPath[IO_MAX_PATH_LENGTH];
	char aAltFolderPath[IO_MAX_PATH_LENGTH];
	AssetsGetRelativePaths(Tab, pName, aFilePath, sizeof(aFilePath), aFolderPath, sizeof(aFolderPath), aAltFolderPath, sizeof(aAltFolderPath));

	if(aFilePath[0] != '\0' && Storage()->FileExists(aFilePath, IStorage::TYPE_SAVE))
		return true;
	if(aFolderPath[0] != '\0' && Storage()->FolderExists(aFolderPath, IStorage::TYPE_SAVE))
		return true;
	if(aAltFolderPath[0] != '\0' && Storage()->FolderExists(aAltFolderPath, IStorage::TYPE_SAVE))
		return true;
	return false;
}

struct SAssetsDeleteFolderContext
{
	IStorage *m_pStorage;
	char m_aBasePath[IO_MAX_PATH_LENGTH];
	bool m_Success = true;
	int m_Depth = 0;
};

static int AssetsDeleteFolderContents(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pContext = static_cast<SAssetsDeleteFolderContext *>(pUser);
	// Skip only "." / ".." — other dotfiles (e.g. .DS_Store) must be removed or RemoveFolder fails.
	if(pName[0] == '.' && (pName[1] == '\0' || (pName[1] == '.' && pName[2] == '\0')))
		return 0;

	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "%s/%s", pContext->m_aBasePath, pName);
	if(IsDir)
	{
		if(pContext->m_Depth >= 16)
		{
			pContext->m_Success = false;
			return 0;
		}
		SAssetsDeleteFolderContext ChildContext;
		ChildContext.m_pStorage = pContext->m_pStorage;
		ChildContext.m_Depth = pContext->m_Depth + 1;
		str_copy(ChildContext.m_aBasePath, aPath);
		pContext->m_pStorage->ListDirectory(IStorage::TYPE_SAVE, aPath, AssetsDeleteFolderContents, &ChildContext);
		if(!ChildContext.m_Success || !pContext->m_pStorage->RemoveFolder(aPath, IStorage::TYPE_SAVE))
			pContext->m_Success = false;
	}
	else if(!pContext->m_pStorage->RemoveFile(aPath, IStorage::TYPE_SAVE))
	{
		pContext->m_Success = false;
	}
	return 0;
}

static bool AssetsDeleteSavePath(IStorage *pStorage, const char *pPath, bool IsFolder)
{
	if(pPath == nullptr || pPath[0] == '\0')
		return true;

	if(IsFolder)
	{
		if(!pStorage->FolderExists(pPath, IStorage::TYPE_SAVE))
			return true;

		SAssetsDeleteFolderContext Context;
		Context.m_pStorage = pStorage;
		str_copy(Context.m_aBasePath, pPath);
		pStorage->ListDirectory(IStorage::TYPE_SAVE, pPath, AssetsDeleteFolderContents, &Context);
		if(!Context.m_Success)
			return false;
		return pStorage->RemoveFolder(pPath, IStorage::TYPE_SAVE);
	}

	if(!pStorage->FileExists(pPath, IStorage::TYPE_SAVE))
		return true;
	return pStorage->RemoveFile(pPath, IStorage::TYPE_SAVE);
}

bool CMenus::DeleteCustomAsset(int Tab, const char *pName)
{
	if(!CanDeleteCustomAsset(Tab, pName))
		return false;

	char aFilePath[IO_MAX_PATH_LENGTH];
	char aFolderPath[IO_MAX_PATH_LENGTH];
	char aAltFolderPath[IO_MAX_PATH_LENGTH];
	AssetsGetRelativePaths(Tab, pName, aFilePath, sizeof(aFilePath), aFolderPath, sizeof(aFolderPath), aAltFolderPath, sizeof(aAltFolderPath));

	// Best-effort: remove every SAVE path. Success = nothing deletable remains
	// (handles partial deletes where e.g. the png is gone but a leftover folder stays).
	AssetsDeleteSavePath(Storage(), aFilePath, false);
	AssetsDeleteSavePath(Storage(), aFolderPath, true);
	AssetsDeleteSavePath(Storage(), aAltFolderPath, true);
	return !CanDeleteCustomAsset(Tab, pName);
}

static void AssetsResetSelectedToDefault(int Tab)
{
	if(Tab == ASSETS_TAB_ENTITIES)
		str_copy(g_Config.m_ClAssetsEntities, "default");
	else if(Tab == ASSETS_TAB_GAME)
		str_copy(g_Config.m_ClAssetGame, "default");
	else if(Tab == ASSETS_TAB_EMOTICONS)
		str_copy(g_Config.m_ClAssetEmoticons, "default");
	else if(Tab == ASSETS_TAB_PARTICLES)
		str_copy(g_Config.m_ClAssetParticles, "default");
	else if(Tab == ASSETS_TAB_HUD)
		str_copy(g_Config.m_ClAssetHud, "default");
	else if(Tab == ASSETS_TAB_EXTRAS)
		str_copy(g_Config.m_ClAssetExtras, "default");
	else if(Tab == ASSETS_TAB_CURSOR)
		str_copy(g_Config.m_ClAssetCursor, "default");
	else if(Tab == ASSETS_TAB_ARROW)
		str_copy(g_Config.m_ClAssetArrow, "default");
	else if(Tab == ASSETS_TAB_AUDIO)
		str_copy(g_Config.m_SndPack, "default");
}

static bool AssetsIsCurrentlySelected(int Tab, const char *pName)
{
	if(Tab == ASSETS_TAB_ENTITIES)
		return str_comp(pName, g_Config.m_ClAssetsEntities) == 0;
	if(Tab == ASSETS_TAB_GAME)
		return str_comp(pName, g_Config.m_ClAssetGame) == 0;
	if(Tab == ASSETS_TAB_EMOTICONS)
		return str_comp(pName, g_Config.m_ClAssetEmoticons) == 0;
	if(Tab == ASSETS_TAB_PARTICLES)
		return str_comp(pName, g_Config.m_ClAssetParticles) == 0;
	if(Tab == ASSETS_TAB_HUD)
		return str_comp(pName, g_Config.m_ClAssetHud) == 0;
	if(Tab == ASSETS_TAB_EXTRAS)
		return str_comp(pName, g_Config.m_ClAssetExtras) == 0;
	if(Tab == ASSETS_TAB_CURSOR)
		return str_comp(pName, g_Config.m_ClAssetCursor) == 0;
	if(Tab == ASSETS_TAB_ARROW)
		return str_comp(pName, g_Config.m_ClAssetArrow) == 0;
	if(Tab == ASSETS_TAB_AUDIO)
		return str_comp(pName, g_Config.m_SndPack) == 0;
	return false;
}

void CMenus::PopupConfirmDeleteAsset()
{
	if(m_DeleteAssetTab < 0 || m_DeleteAssetTab >= NUMBER_OF_ASSETS_TABS || m_aDeleteAssetName[0] == '\0')
		return;

	const int Tab = m_DeleteAssetTab;
	const bool WasSelected = AssetsIsCurrentlySelected(Tab, m_aDeleteAssetName);
	if(!DeleteCustomAsset(Tab, m_aDeleteAssetName))
	{
		char aError[128 + sizeof(m_aDeleteAssetName)];
		str_format(aError, sizeof(aError), Localize("Unable to delete the asset '%s'"), m_aDeleteAssetName);
		PopupMessage(Localize("Error"), aError, Localize("Ok"));
		m_aDeleteAssetName[0] = '\0';
		m_DeleteAssetTab = -1;
		return;
	}

	SetAssetOrganizerFavorite(Tab, m_aDeleteAssetName, false);
	if(WasSelected)
		AssetsResetSelectedToDefault(Tab);

	// Drop only the deleted entry — full ClearCustomItems reloads every preview texture and causes hitch.
	RemoveCustomAssetFromList(Tab, m_aDeleteAssetName);

	if(WasSelected)
	{
		if(Tab == ASSETS_TAB_ENTITIES)
			GameClient()->m_MapImages.ChangeEntitiesPath(g_Config.m_ClAssetsEntities);
		else if(Tab == ASSETS_TAB_GAME)
			GameClient()->LoadGameSkin(g_Config.m_ClAssetGame);
		else if(Tab == ASSETS_TAB_EMOTICONS)
			GameClient()->LoadEmoticonsSkin(g_Config.m_ClAssetEmoticons);
		else if(Tab == ASSETS_TAB_PARTICLES)
			GameClient()->LoadParticlesSkin(g_Config.m_ClAssetParticles);
		else if(Tab == ASSETS_TAB_HUD)
			GameClient()->LoadHudSkin(g_Config.m_ClAssetHud);
		else if(Tab == ASSETS_TAB_EXTRAS)
			GameClient()->LoadExtrasSkin(g_Config.m_ClAssetExtras);
		else if(Tab == ASSETS_TAB_CURSOR)
			GameClient()->LoadCursorAsset(g_Config.m_ClAssetCursor);
		else if(Tab == ASSETS_TAB_ARROW)
			GameClient()->LoadArrowAsset(g_Config.m_ClAssetArrow);
		else if(Tab == ASSETS_TAB_AUDIO)
			GameClient()->m_Sounds.Clear();
	}

	gs_aInitCustomList[Tab] = true;
	m_aDeleteAssetName[0] = '\0';
	m_DeleteAssetTab = -1;
}

static void AssetsUnloadEntitiesPreview(CMenus::SCustomEntities &Entity, IGraphics *pGraphics)
{
	for(int i = 0; i < MAP_IMAGE_MOD_TYPE_COUNT; ++i)
	{
		IGraphics::CTextureHandle &Tex = Entity.m_aImages[i].m_Texture;
		if(!Tex.IsValid() || Tex.IsNullTexture())
			continue;

		const int TextureId = Tex.Id();
		for(int j = i + 1; j < MAP_IMAGE_MOD_TYPE_COUNT; ++j)
		{
			if(Entity.m_aImages[j].m_Texture.IsValid() && !Entity.m_aImages[j].m_Texture.IsNullTexture() && Entity.m_aImages[j].m_Texture.Id() == TextureId)
				Entity.m_aImages[j].m_Texture.Invalidate();
		}
		if(Entity.m_RenderTexture.IsValid() && !Entity.m_RenderTexture.IsNullTexture() && Entity.m_RenderTexture.Id() == TextureId)
			Entity.m_RenderTexture.Invalidate();

		pGraphics->UnloadTexture(&Tex);
	}
	Entity.m_RenderTexture.Invalidate();
}

template<typename TName>
static void AssetsEraseNamedPreview(std::vector<TName> &vList, IGraphics *pGraphics, const char *pName, bool UnloadTexture)
{
	for(auto It = vList.begin(); It != vList.end();)
	{
		if(str_comp(It->m_aName, pName) != 0)
		{
			++It;
			continue;
		}
		if(UnloadTexture)
			pGraphics->UnloadTexture(&It->m_RenderTexture);
		It = vList.erase(It);
	}
}

void CMenus::RemoveCustomAssetFromList(int Tab, const char *pName)
{
	if(Tab == ASSETS_TAB_ENTITIES)
	{
		for(auto It = m_vEntitiesList.begin(); It != m_vEntitiesList.end();)
		{
			if(str_comp(It->m_aName, pName) != 0)
			{
				++It;
				continue;
			}
			AssetsUnloadEntitiesPreview(*It, Graphics());
			It = m_vEntitiesList.erase(It);
		}
	}
	else if(Tab == ASSETS_TAB_GAME)
		AssetsEraseNamedPreview(m_vGameList, Graphics(), pName, true);
	else if(Tab == ASSETS_TAB_EMOTICONS)
		AssetsEraseNamedPreview(m_vEmoticonList, Graphics(), pName, true);
	else if(Tab == ASSETS_TAB_PARTICLES)
		AssetsEraseNamedPreview(m_vParticlesList, Graphics(), pName, true);
	else if(Tab == ASSETS_TAB_HUD)
		AssetsEraseNamedPreview(m_vHudList, Graphics(), pName, true);
	else if(Tab == ASSETS_TAB_EXTRAS)
		AssetsEraseNamedPreview(m_vExtrasList, Graphics(), pName, true);
	else if(Tab == ASSETS_TAB_CURSOR)
		AssetsEraseNamedPreview(m_vCursorList, Graphics(), pName, true);
	else if(Tab == ASSETS_TAB_ARROW)
		AssetsEraseNamedPreview(m_vArrowList, Graphics(), pName, true);
	else if(Tab == ASSETS_TAB_AUDIO)
		AssetsEraseNamedPreview(m_vAudioPackList, Graphics(), pName, false);
}

void CMenus::MarkCustomAssetsDeletable(int Tab)
{
	auto MarkList = [this, Tab](auto &vList) {
		for(auto &Asset : vList)
			Asset.m_Deletable = CanDeleteCustomAsset(Tab, Asset.m_aName);
	};

	if(Tab == ASSETS_TAB_ENTITIES)
		MarkList(m_vEntitiesList);
	else if(Tab == ASSETS_TAB_GAME)
		MarkList(m_vGameList);
	else if(Tab == ASSETS_TAB_EMOTICONS)
		MarkList(m_vEmoticonList);
	else if(Tab == ASSETS_TAB_PARTICLES)
		MarkList(m_vParticlesList);
	else if(Tab == ASSETS_TAB_HUD)
		MarkList(m_vHudList);
	else if(Tab == ASSETS_TAB_EXTRAS)
		MarkList(m_vExtrasList);
	else if(Tab == ASSETS_TAB_CURSOR)
		MarkList(m_vCursorList);
	else if(Tab == ASSETS_TAB_ARROW)
		MarkList(m_vArrowList);
	else if(Tab == ASSETS_TAB_AUDIO)
		MarkList(m_vAudioPackList);
}

static std::vector<const CMenus::SCustomEntities *> gs_vpSearchEntitiesList;
static std::vector<const CMenus::SCustomGame *> gs_vpSearchGamesList;
static std::vector<const CMenus::SCustomEmoticon *> gs_vpSearchEmoticonsList;
static std::vector<const CMenus::SCustomParticle *> gs_vpSearchParticlesList;
static std::vector<const CMenus::SCustomHud *> gs_vpSearchHudList;
static std::vector<const CMenus::SCustomExtras *> gs_vpSearchExtrasList;
static std::vector<const CMenus::SCustomCursor *> gs_vpSearchCursorList;
static std::vector<const CMenus::SCustomArrow *> gs_vpSearchArrowList;
static std::vector<const CMenus::SCustomAudioPack *> gs_vpSearchAudioPackList;

static size_t gs_aCustomListSize[NUMBER_OF_ASSETS_TABS] = {
	0,
};

static CLineInputBuffered<64> s_aFilterInputs[NUMBER_OF_ASSETS_TABS];

static int s_CurCustomTab = ASSETS_TAB_ENTITIES;
static void ResetAssetOrganizerInteraction(CUi *pUi = nullptr);

void CMenus::OpenAssetsSelector(EAssetsSelectorTab Tab)
{
	s_CurCustomTab = std::max((int)ASSETS_TAB_ENTITIES, std::min((int)Tab, (int)NUMBER_OF_ASSETS_TABS - 1));
	ResetAssetOrganizerInteraction(Ui());
	// Keep already scanned preview textures resident. Reopening a selector must
	// not unload/reload every card or reapply the active skin; the explicit
	// Refresh action remains the deliberate way to rescan the filesystem.
	g_Config.m_UiSettingsPage = SETTINGS_ASSETS;
	SetMenuPage(PAGE_SETTINGS);
}

static const CMenus::SCustomItem *GetCustomItem(int CurTab, size_t Index)
{
	if(CurTab == ASSETS_TAB_ENTITIES)
		return gs_vpSearchEntitiesList[Index];
	else if(CurTab == ASSETS_TAB_GAME)
		return gs_vpSearchGamesList[Index];
	else if(CurTab == ASSETS_TAB_EMOTICONS)
		return gs_vpSearchEmoticonsList[Index];
	else if(CurTab == ASSETS_TAB_PARTICLES)
		return gs_vpSearchParticlesList[Index];
	else if(CurTab == ASSETS_TAB_HUD)
		return gs_vpSearchHudList[Index];
	else if(CurTab == ASSETS_TAB_EXTRAS)
		return gs_vpSearchExtrasList[Index];
	else if(CurTab == ASSETS_TAB_CURSOR)
		return gs_vpSearchCursorList[Index];
	else if(CurTab == ASSETS_TAB_ARROW)
		return gs_vpSearchArrowList[Index];
	else if(CurTab == ASSETS_TAB_AUDIO)
		return gs_vpSearchAudioPackList[Index];

	return nullptr;
}

template<typename TName>
static void ClearAssetList(std::vector<TName> &vList, IGraphics *pGraphics)
{
	for(TName &Asset : vList)
	{
		pGraphics->UnloadTexture(&Asset.m_RenderTexture);
	}
	vList.clear();
}

void CMenus::ClearCustomItems(int CurTab)
{
	if(CurTab == s_CurCustomTab)
		ResetAssetOrganizerInteraction(Ui());
	if(CurTab == ASSETS_TAB_ENTITIES)
	{
		for(auto &Entity : m_vEntitiesList)
			AssetsUnloadEntitiesPreview(Entity, Graphics());
		m_vEntitiesList.clear();

		// reload current entities
		GameClient()->m_MapImages.ChangeEntitiesPath(g_Config.m_ClAssetsEntities);
	}
	else if(CurTab == ASSETS_TAB_GAME)
	{
		ClearAssetList(m_vGameList, Graphics());
		// reload current game skin
		GameClient()->LoadGameSkin(g_Config.m_ClAssetGame);
	}
	else if(CurTab == ASSETS_TAB_EMOTICONS)
	{
		ClearAssetList(m_vEmoticonList, Graphics());

		// reload current emoticons skin
		GameClient()->LoadEmoticonsSkin(g_Config.m_ClAssetEmoticons);
	}
	else if(CurTab == ASSETS_TAB_PARTICLES)
	{
		ClearAssetList(m_vParticlesList, Graphics());

		// reload current particles skin
		GameClient()->LoadParticlesSkin(g_Config.m_ClAssetParticles);
	}
	else if(CurTab == ASSETS_TAB_HUD)
	{
		ClearAssetList(m_vHudList, Graphics());

		// reload current hud skin
		GameClient()->LoadHudSkin(g_Config.m_ClAssetHud);
	}
	else if(CurTab == ASSETS_TAB_EXTRAS)
	{
		ClearAssetList(m_vExtrasList, Graphics());

		// reload current DDNet particles skin
		GameClient()->LoadExtrasSkin(g_Config.m_ClAssetExtras);
	}
	else if(CurTab == ASSETS_TAB_CURSOR)
	{
		ClearCursorAssetList(m_vCursorList, Graphics());
		GameClient()->LoadCursorAsset(g_Config.m_ClAssetCursor);
	}
	else if(CurTab == ASSETS_TAB_ARROW)
	{
		ClearArrowAssetList(m_vArrowList, Graphics());
		GameClient()->LoadArrowAsset(g_Config.m_ClAssetArrow);
	}
	else if(CurTab == ASSETS_TAB_AUDIO)
	{
		m_vAudioPackList.clear();
		GameClient()->m_Sounds.Clear();
	}
	gs_aInitCustomList[CurTab] = true;
}

void CMenus::SyncAssetOrganizer(int Tab)
{
	if(Tab < 0 || Tab >= NUMBER_OF_ASSETS_TABS)
		return;

	std::vector<std::string> vAssetIds;
	auto Collect = [&vAssetIds](const auto &vAssets) {
		vAssetIds.reserve(vAssets.size());
		for(const auto &Asset : vAssets)
			vAssetIds.emplace_back(Asset.m_aName);
	};

	if(Tab == ASSETS_TAB_ENTITIES)
		Collect(m_vEntitiesList);
	else if(Tab == ASSETS_TAB_GAME)
		Collect(m_vGameList);
	else if(Tab == ASSETS_TAB_EMOTICONS)
		Collect(m_vEmoticonList);
	else if(Tab == ASSETS_TAB_PARTICLES)
		Collect(m_vParticlesList);
	else if(Tab == ASSETS_TAB_HUD)
		Collect(m_vHudList);
	else if(Tab == ASSETS_TAB_EXTRAS)
		Collect(m_vExtrasList);
	else if(Tab == ASSETS_TAB_CURSOR)
		Collect(m_vCursorList);
	else if(Tab == ASSETS_TAB_ARROW)
		Collect(m_vArrowList);
	else if(Tab == ASSETS_TAB_AUDIO)
		Collect(m_vAudioPackList);

	AssetOrganizer::EnsureItems(Tab, vAssetIds, m_aAssetFavorites[Tab]);
}

template<typename TName, typename TCaller>
static void InitAssetList(std::vector<TName> &vAssetList, const char *pAssetPath, const char *pAssetName, FS_LISTDIR_CALLBACK pfnCallback, IGraphics *pGraphics, IStorage *pStorage, std::unordered_set<std::string> &KnownAssetIds, TCaller Caller)
{
	if(vAssetList.empty())
	{
		KnownAssetIds.clear();
		KnownAssetIds.emplace("default");
		TName AssetItem;
		str_copy(AssetItem.m_aName, "default");
		AssetItem.m_Deletable = false;
		LoadAsset(&AssetItem, pAssetName, pGraphics);
		vAssetList.push_back(AssetItem);

		// load assets
		pStorage->ListDirectory(IStorage::TYPE_ALL, pAssetPath, pfnCallback, Caller);
		std::sort(vAssetList.begin(), vAssetList.end());
	}
	if(vAssetList.size() != gs_aCustomListSize[s_CurCustomTab])
		gs_aInitCustomList[s_CurCustomTab] = true;
}

template<typename TName>
static int InitSearchList(std::vector<const TName *> &vpSearchList, std::vector<TName> &vAssetList)
{
	vpSearchList.clear();
	vpSearchList.reserve(vAssetList.size());
	const char *pFolderId = AssetOrganizer::CurrentFolder(s_CurCustomTab);
	const int Page = AssetOrganizer::CurrentPage(s_CurCustomTab);
	const bool HasFilter = !s_aFilterInputs[s_CurCustomTab].IsEmpty();
	const char *pFilter = HasFilter ? s_aFilterInputs[s_CurCustomTab].GetString() : nullptr;
	for(const TName &Asset : vAssetList)
	{
		const TName *pAsset = &Asset;

		if(!AssetOrganizer::ItemIsVisible(s_CurCustomTab, pAsset->m_aName, pFolderId, Page))
			continue;
		if(HasFilter && !str_utf8_find_nocase(pAsset->m_aName, pFilter))
			continue;

		vpSearchList.push_back(pAsset);
	}
	return (int)vpSearchList.size();
}

static const char *AssetOrganizerFolderLabel(int Tab, const char *pFolderId)
{
	if(AssetOrganizer::IsFavoritesFolder(pFolderId))
		return TCLocalize("Favorites", "AMF Client");
	if(str_comp(pFolderId, AssetOrganizer::FOLDER_UNSORTED) == 0)
		return TCLocalize("Unsorted", "AMF Client");
	return AssetOrganizer::FolderName(Tab, pFolderId);
}

struct SAssetOrganizerPopupContext : public SPopupMenuId
{
	CMenus *m_pMenus = nullptr;
	int m_Tab = -1;
	char m_aAssetId[CMenus::SCustomItem::MAX_NAME_LENGTH] = {};
	char m_aTargetFolderId[64] = {};
	int m_TargetPage = 0;
	CButtonContainer m_FavoriteButton;
	CButtonContainer m_TargetFolderPrevButton;
	CButtonContainer m_TargetFolderNextButton;
	CButtonContainer m_TargetPagePrevButton;
	CButtonContainer m_TargetPageNextButton;
	CButtonContainer m_MoveButton;

	static CUi::EPopupMenuFunctionResult Render(void *pContext, CUIRect View, bool Active)
	{
		auto *pPopup = static_cast<SAssetOrganizerPopupContext *>(pContext);
		if(!pPopup->m_pMenus || pPopup->m_Tab < 0 || pPopup->m_aAssetId[0] == '\0')
			return CUi::POPUP_CLOSE_CURRENT;

		CUi *pUi = pPopup->m_pMenus->AssetOrganizerUi();
		constexpr float Margin = 5.0f;
		constexpr float Spacing = 2.0f;
		constexpr float ButtonHeight = 17.5f;
		View.Margin(Margin, &View);
		// Popup rendering is a static callback, so read the same config source that
		// CMenus::UpdateColors() converts into ms_GuiColor for the main page.
		const ColorRGBA PopupInterfaceColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_UiColor, true));
		const ColorHSLA PopupInterfaceHsla = color_cast<ColorHSLA>(PopupInterfaceColor);
		const float PopupLightness = std::clamp(PopupInterfaceHsla.l, 0.28f, 0.58f);
		const auto PopupAccent = [&](float Lightness, float Alpha) {
			return color_cast<ColorRGBA>(ColorHSLA(PopupInterfaceHsla.h, PopupInterfaceHsla.s, std::clamp(Lightness, 0.0f, 1.0f), Alpha));
		};
		const ColorRGBA PopupAccentPressed = PopupAccent(PopupLightness - 0.08f, 0.96f);
		const ColorRGBA PopupAccentSoft = PopupAccent(PopupLightness, 0.62f);
		const ColorRGBA PopupNeutral = ColorRGBA(0.055f, 0.075f, 0.10f, 0.92f);
		const ColorRGBA PopupNeutralHover = ColorRGBA(0.085f, 0.11f, 0.14f, 0.95f);
		const ColorRGBA PopupNeutralDisabled = ColorRGBA(0.035f, 0.045f, 0.06f, 0.52f);
		const ColorRGBA PopupBorder = ColorRGBA(0.10f, 0.13f, 0.17f, 0.78f);
		auto DoOrganizerPopupButton = [pUi, &PopupAccentPressed, &PopupAccentSoft, &PopupNeutral, &PopupNeutralHover, &PopupNeutralDisabled, &PopupBorder](CButtonContainer *pButton, const char *pText, const CUIRect *pRect, float FontSize, bool Enabled = true) {
			const bool Hovered = Enabled && pUi->MouseHovered(pRect);
			const bool IsActive = Enabled && pUi->CheckActiveItem(pButton);
			const ColorRGBA Fill = !Enabled ? PopupNeutralDisabled : (IsActive ? PopupAccentPressed : (Hovered ? PopupNeutralHover : PopupNeutral));
			const ColorRGBA Border = !Enabled ? PopupBorder : (IsActive ? PopupAccentPressed : (Hovered ? PopupAccentSoft : PopupBorder));
			pRect->Draw(Border, IGraphics::CORNER_ALL, 3.0f);
			CUIRect Inner = *pRect;
			Inner.Margin(1.0f, &Inner);
			Inner.Draw(Fill, IGraphics::CORNER_ALL, 2.0f);
			SLabelProperties Props;
			Props.m_MaxWidth = std::max(0.0f, pRect->w - 6.0f);
			pUi->DoLabel(pRect, pText, FontSize, TEXTALIGN_MC, Props);
			return Enabled ? pUi->DoButtonLogic(pButton, 0, pRect, BUTTONFLAG_LEFT) : 0;
		};
		auto DoOrganizerPopupIconButton = [&](CButtonContainer *pButton, const char *pIcon, const CUIRect *pRect, bool Enabled = true) {
			pUi->TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			pUi->TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
			const int Result = DoOrganizerPopupButton(pButton, pIcon, pRect, 10.5f, Enabled);
			pUi->TextRender()->SetRenderFlags(0);
			pUi->TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			return Result;
		};

		CUIRect Row;
		View.HSplitTop(13.0f, &Row, &View);
		SLabelProperties TitleProps;
		TitleProps.m_MaxWidth = Row.w;
		TitleProps.m_EllipsisAtEnd = true;
		pUi->DoLabel(&Row, pPopup->m_aAssetId, 10.0f, TEXTALIGN_MC, TitleProps);

		View.HSplitTop(Spacing, nullptr, &View);
		View.HSplitTop(ButtonHeight, &Row, &View);
		const bool Favorite = AssetOrganizer::IsFavorite(pPopup->m_Tab, pPopup->m_aAssetId);
		if(DoOrganizerPopupButton(&pPopup->m_FavoriteButton, TCLocalize(Favorite ? "Remove from favorites" : "Add to favorites", "AMF Client"), &Row, 9.0f))
		{
			pPopup->m_pMenus->SetAssetOrganizerFavorite(pPopup->m_Tab, pPopup->m_aAssetId, !Favorite);
			return CUi::POPUP_KEEP_OPEN;
		}

		View.HSplitTop(Spacing, nullptr, &View);
		View.HSplitTop(ButtonHeight, &Row, &View);
		CUIRect Left, Label, Right;
		Row.VSplitLeft(ButtonHeight, &Left, &Row);
		Row.VSplitRight(ButtonHeight, &Row, &Right);
		Label = Row;
		const auto &vFolders = AssetOrganizer::Folders(pPopup->m_Tab);
		const int FolderCount = (int)vFolders.size();
		int FolderIndex = 0;
		for(int Index = 0; Index < FolderCount; ++Index)
		{
			if(vFolders[Index].m_Id == pPopup->m_aTargetFolderId)
			{
				FolderIndex = Index;
				break;
			}
		}
		if(DoOrganizerPopupIconButton(&pPopup->m_TargetFolderPrevButton, FontIcon::CHEVRON_LEFT, &Left, FolderCount > 1))
		{
			FolderIndex = (FolderIndex + FolderCount - 1) % FolderCount;
			str_copy(pPopup->m_aTargetFolderId, vFolders[FolderIndex].m_Id.c_str(), sizeof(pPopup->m_aTargetFolderId));
			pPopup->m_TargetPage = 0;
		}
		pUi->DoLabel(&Label, AssetOrganizerFolderLabel(pPopup->m_Tab, pPopup->m_aTargetFolderId), 9.0f, TEXTALIGN_MC);
		if(DoOrganizerPopupIconButton(&pPopup->m_TargetFolderNextButton, FontIcon::CHEVRON_RIGHT, &Right, FolderCount > 1))
		{
			FolderIndex = (FolderIndex + 1) % FolderCount;
			str_copy(pPopup->m_aTargetFolderId, vFolders[FolderIndex].m_Id.c_str(), sizeof(pPopup->m_aTargetFolderId));
			pPopup->m_TargetPage = 0;
		}

		View.HSplitTop(Spacing, nullptr, &View);
		View.HSplitTop(ButtonHeight, &Row, &View);
		Row.VSplitLeft(ButtonHeight, &Left, &Row);
		Row.VSplitRight(ButtonHeight, &Row, &Right);
		Label = Row;
		const int Pages = AssetOrganizer::PageCount(pPopup->m_Tab, pPopup->m_aTargetFolderId);
		if(DoOrganizerPopupIconButton(&pPopup->m_TargetPagePrevButton, FontIcon::CHEVRON_LEFT, &Left, pPopup->m_TargetPage > 0))
			--pPopup->m_TargetPage;
		char aPageLabel[64];
		str_format(aPageLabel, sizeof(aPageLabel), "%s %d / %d", TCLocalize("Page", "AMF Client"), pPopup->m_TargetPage + 1, Pages);
		pUi->DoLabel(&Label, aPageLabel, 9.0f, TEXTALIGN_MC);
		if(DoOrganizerPopupIconButton(&pPopup->m_TargetPageNextButton, FontIcon::CHEVRON_RIGHT, &Right, pPopup->m_TargetPage + 1 < Pages))
			++pPopup->m_TargetPage;

		View.HSplitTop(Spacing, nullptr, &View);
		View.HSplitTop(ButtonHeight, &Row, &View);
		if(DoOrganizerPopupButton(&pPopup->m_MoveButton, TCLocalize("Move here", "AMF Client"), &Row, 9.0f))
		{
			// Exact position is a direct manipulation feature now. The menu keeps
			// only a convenient folder/page destination and appends there.
			AssetOrganizer::MoveItem(pPopup->m_Tab, pPopup->m_aAssetId, pPopup->m_aTargetFolderId, pPopup->m_TargetPage, AssetOrganizer::ItemCount(pPopup->m_Tab, pPopup->m_aTargetFolderId, pPopup->m_TargetPage));
			gs_aInitCustomList[pPopup->m_Tab] = true;
			return CUi::POPUP_CLOSE_CURRENT;
		}

		return CUi::POPUP_KEEP_OPEN;
	}
};

static SAssetOrganizerPopupContext gs_AssetOrganizerPopupContext;

struct SAssetFolderPopupContext : public SPopupMenuId
{
	CMenus *m_pMenus = nullptr;
	int m_Tab = -1;
	char m_aFolderId[64] = {};
	CLineInputBuffered<64> m_NameInput;
	CButtonContainer m_RenameButton;
	CButtonContainer m_DeleteButton;

	static CUi::EPopupMenuFunctionResult Render(void *pContext, CUIRect View, bool Active)
	{
		auto *pPopup = static_cast<SAssetFolderPopupContext *>(pContext);
		if(!pPopup->m_pMenus || pPopup->m_Tab < 0 || pPopup->m_aFolderId[0] == '\0' || AssetOrganizer::IsSystemFolder(pPopup->m_Tab, pPopup->m_aFolderId))
			return CUi::POPUP_CLOSE_CURRENT;
		CUi *pUi = pPopup->m_pMenus->AssetOrganizerUi();
		const ColorRGBA RenameColor = ColorRGBA(0.22f, 0.64f, 0.38f, 0.99f);
		View.Margin(6.0f, &View);
		CUIRect Input, Rename, Delete;
		View.HSplitTop(21.0f, &Input, &View);
		View.HSplitTop(5.0f, nullptr, &View);
		View.HSplitTop(20.0f, &Rename, &View);
		View.HSplitTop(4.0f, nullptr, &View);
		View.HSplitTop(20.0f, &Delete, &View);
		pUi->DoEditBox(&pPopup->m_NameInput, &Input, 10.0f);

		auto DoPopupButton = [pUi](CButtonContainer *pButton, const char *pText, const CUIRect *pRect, const ColorRGBA &Base) {
			const bool Hovered = pUi->MouseHovered(pRect);
			const bool Pressed = pUi->CheckActiveItem(pButton);
			const ColorRGBA Fill = Pressed ? ColorRGBA(Base.r * 1.12f, Base.g * 1.12f, Base.b * 1.12f, Base.a) : Hovered ? ColorRGBA(Base.r * 1.22f, Base.g * 1.22f, Base.b * 1.22f, Base.a) : Base;
			pRect->Draw(Base.WithAlpha(std::min(1.0f, Base.a + 0.10f)), IGraphics::CORNER_ALL, 3.0f);
			CUIRect Inner = *pRect;
			Inner.Margin(1.0f, &Inner);
			Inner.Draw(Fill, IGraphics::CORNER_ALL, 2.0f);
			SLabelProperties Props;
			Props.m_MaxWidth = std::max(0.0f, pRect->w - 8.0f);
			Props.m_EllipsisAtEnd = true;
			pUi->DoLabel(pRect, pText, 9.5f, TEXTALIGN_MC, Props);
			return pUi->DoButtonLogic(pButton, 0, pRect, BUTTONFLAG_LEFT);
		};
		if(DoPopupButton(&pPopup->m_RenameButton, TCLocalize("Rename", "AMF Client"), &Rename, RenameColor))
		{
			AssetOrganizer::RenameFolder(pPopup->m_Tab, pPopup->m_aFolderId, pPopup->m_NameInput.GetString());
			gs_aInitCustomList[pPopup->m_Tab] = true;
			return CUi::POPUP_CLOSE_CURRENT;
		}
		if(DoPopupButton(&pPopup->m_DeleteButton, TCLocalize("Delete", "AMF Client"), &Delete, ColorRGBA(0.52f, 0.13f, 0.18f, 0.99f)))
		{
			AssetOrganizer::DeleteFolder(pPopup->m_Tab, pPopup->m_aFolderId);
			AssetOrganizer::SetCurrentFolder(pPopup->m_Tab, AssetOrganizer::FOLDER_UNSORTED);
			gs_aInitCustomList[pPopup->m_Tab] = true;
			return CUi::POPUP_CLOSE_CURRENT;
		}
		return CUi::POPUP_KEEP_OPEN;
	}
};

static SAssetFolderPopupContext gs_AssetFolderPopupContext;

// Kept independently from the asset vectors so a drag does not depend on a
// card's grid position. A press becomes a drag only after a visible movement
// threshold; otherwise it remains an ordinary pack-selection click.
struct SAssetOrganizerDragState
{
	const CMenus::SCustomItem *m_pSourceItem = nullptr;
	int m_Tab = -1;
	vec2 m_PressPos = vec2(0.0f, 0.0f);
	vec2 m_DragOffset = vec2(0.0f, 0.0f);
	vec2 m_GhostPos = vec2(0.0f, 0.0f);
	CUIRect m_SourceRect = {};
	CUIRect m_TargetRect = {};
	char m_aTargetFolderId[64] = {};
	char m_aTargetAssetId[128] = {};
	bool m_Dragging = false;
	bool m_TargetRectValid = false;
	bool m_CreateFolderDropTarget = false;
	bool m_CurrentPageDropTarget = false;

	void Reset()
	{
		m_pSourceItem = nullptr;
		m_Tab = -1;
		m_aTargetFolderId[0] = '\0';
		m_aTargetAssetId[0] = '\0';
		m_Dragging = false;
		m_TargetRectValid = false;
		m_CreateFolderDropTarget = false;
		m_CurrentPageDropTarget = false;
	}
};

static SAssetOrganizerDragState gs_AssetOrganizerDrag;

struct SAssetOrganizerSettleState
{
	bool m_Active = false;
	int m_Tab = -1;
	std::chrono::nanoseconds m_Start{};
	CUIRect m_From = {};
	CUIRect m_To = {};
	IGraphics::CTextureHandle m_Texture;
	float m_PreviewAspect = 1.0f;
	char m_aName[CMenus::SCustomItem::MAX_NAME_LENGTH] = {};
};

static SAssetOrganizerSettleState gs_AssetOrganizerSettle;

struct SAssetFolderDragState
{
	int m_Tab = -1;
	int m_SourceIndex = -1;
	int m_TargetIndex = -1;
	vec2 m_PressPos = vec2(0.0f, 0.0f);
	char m_aSourceId[64] = {};
	char m_aSourceName[64] = {};
	bool m_Dragging = false;

	void Reset()
	{
		m_Tab = -1;
		m_SourceIndex = -1;
		m_TargetIndex = -1;
		m_aSourceId[0] = '\0';
		m_aSourceName[0] = '\0';
		m_Dragging = false;
	}
};

static SAssetFolderDragState gs_AssetFolderDrag;

static void ResetAssetOrganizerInteraction(CUi *pUi)
{
	gs_AssetOrganizerDrag.Reset();
	gs_AssetFolderDrag.Reset();
	gs_AssetOrganizerSettle.m_Active = false;
	// A card ID points into one of the backing asset vectors. Refreshing that
	// vector, changing selector tab, or reopening the selector must not leave
	// it as the UI active item: otherwise a later DoButtonLogic call can meet a
	// stale active ID with no recorded activating mouse button.
	if(pUi)
		pUi->SetActiveItem(nullptr);
}

void CMenus::RenderSettingsAssets(CUIRect MainView)
{
	CUIRect TabBar, OrganizerToolbar, OrganizerBody, FoldersPanel, AssetsPanel, AssetHeader, OrganizerPagePrevious, OrganizerPageNav, OrganizerPageNext, OrganizerPageAdd, OrganizerPageDelete, CustomList, QuickSearch, InputHints, DirectoryButton, ReloadButton;
	static bool s_EntityGamePreview = true;
	// Keep the organizer's accent on the exact same source as the regular DDNet
	// menu: UpdateColors() derives ms_GuiColor from g_Config.m_UiColor every
	// frame. All variants below only adjust brightness/alpha for state contrast;
	// neutral surfaces remain independent of the user's interface color.
	const ColorRGBA InterfaceColor = ms_GuiColor.WithAlpha(1.0f);
	const ColorHSLA InterfaceHsla = color_cast<ColorHSLA>(InterfaceColor);
	const float AccentLightness = std::clamp(InterfaceHsla.l, 0.28f, 0.58f);
	const auto AccentColor = [&](float Lightness, float Alpha) {
		return color_cast<ColorRGBA>(ColorHSLA(InterfaceHsla.h, InterfaceHsla.s, std::clamp(Lightness, 0.0f, 1.0f), Alpha));
	};
	const ColorRGBA AccentSelected = AccentColor(AccentLightness, 0.94f);
	const ColorRGBA AccentHover = AccentColor(AccentLightness + 0.08f, 0.92f);
	const ColorRGBA AccentPressed = AccentColor(AccentLightness - 0.08f, 0.96f);
	const ColorRGBA AccentSoft = AccentColor(AccentLightness, 0.62f);
	const ColorRGBA AccentOutline = AccentColor(AccentLightness, 0.34f);
	const ColorRGBA AccentHeader = AccentColor(AccentLightness, 0.48f);
	const ColorRGBA NeutralBorder = ColorRGBA(0.12f, 0.15f, 0.19f, 0.90f);
	const ColorRGBA NeutralBorderSoft = ColorRGBA(0.10f, 0.13f, 0.17f, 0.72f);
	const ColorRGBA NeutralButton = ColorRGBA(0.055f, 0.075f, 0.10f, 0.92f);
	const ColorRGBA NeutralButtonHover = ColorRGBA(0.085f, 0.11f, 0.14f, 0.95f);
	const ColorRGBA NeutralButtonDisabled = ColorRGBA(0.035f, 0.045f, 0.06f, 0.52f);
	// The Audio organizer note intentionally keeps its pre-redesign fixed blue.
	// This is also the color used by the music player's no-media glyph.
	const ColorRGBA AudioNoteColor = ColorRGBA(0.54f, 0.76f, 0.92f, 0.90f);
	auto SortSearchList = [](auto &vpSearchList) {
		std::sort(vpSearchList.begin(), vpSearchList.end(), [](const auto *pLeft, const auto *pRight) {
			const int LeftSlot = AssetOrganizer::ItemSlot(s_CurCustomTab, pLeft->m_aName);
			const int RightSlot = AssetOrganizer::ItemSlot(s_CurCustomTab, pRight->m_aName);
			if(LeftSlot != RightSlot)
				return LeftSlot < RightSlot;
			return str_comp(pLeft->m_aName, pRight->m_aName) < 0;
		});
	};

	MainView.HSplitTop(20.0f, &TabBar, &MainView);
	const float TabWidth = TabBar.w / (float)NUMBER_OF_ASSETS_TABS;
	const CUIRect TabBarOuter = TabBar;
	TabBarOuter.Draw(NeutralBorder, IGraphics::CORNER_ALL, 5.0f);
	CUIRect TabBarSurface = TabBarOuter;
	TabBarSurface.Margin(1.0f, &TabBarSurface);
	TabBarSurface.Draw(ColorRGBA(0.028f, 0.042f, 0.060f, 0.96f), IGraphics::CORNER_ALL, 4.0f);
	static CButtonContainer s_aPageTabs[NUMBER_OF_ASSETS_TABS] = {};
	// One palette and the standard UI button lifecycle for every organizer
	// control. In particular, nothing here assigns ActiveItem directly.
	auto DoOrganizerButton = [&](const void *pButton, const char *pText, const CUIRect *pRect, bool Enabled = true, bool Selected = false, int Corners = IGraphics::CORNER_ALL, float FontSize = 10.5f, vec2 TextOffset = vec2(0.0f, 0.0f), const CUIRect *pVisualRect = nullptr, bool DrawBorder = true) {
		const bool Hovered = Enabled && Ui()->MouseHovered(pRect);
		const bool Active = Enabled && Ui()->CheckActiveItem(pButton);
		const CUIRect DrawRect = pVisualRect ? *pVisualRect : *pRect;
		const ColorRGBA Color = !Enabled ? NeutralButtonDisabled :
			(Selected ? AccentColor(AccentLightness, 0.76f) :
				(Active ? AccentPressed :
					(Hovered ? NeutralButtonHover : NeutralButton)));
		const ColorRGBA Border = !Enabled ? NeutralBorderSoft :
			(Selected ? AccentSelected :
				(Hovered || Active ? AccentSoft : NeutralBorder));
		CUIRect Inner = DrawRect;
		if(DrawBorder)
		{
			DrawRect.Draw(Border, Corners, 4.0f);
			const float Inset = std::min(1.0f, std::max(0.0f, std::min(DrawRect.w, DrawRect.h) * 0.08f));
			Inner.Margin(Inset, &Inner);
		}
		Inner.Draw(Color, Corners, 3.0f);
		CUIRect LabelRect = DrawRect;
		LabelRect.x += TextOffset.x;
		LabelRect.y += TextOffset.y;
		SLabelProperties Props;
		Props.m_MaxWidth = std::max(0.0f, DrawRect.w - 8.0f);
		Props.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&LabelRect, pText, FontSize, TEXTALIGN_MC, Props);
		return Enabled ? Ui()->DoButtonLogic(pButton, 0, pRect, BUTTONFLAG_LEFT) : 0;
	};
	auto DoOrganizerIconButton = [&](const void *pButton, const char *pIcon, const CUIRect *pRect, bool Enabled = true, bool Selected = false, float FontSize = 12.0f, vec2 OpticalOffset = vec2(0.0f, 0.0f), const CUIRect *pVisualRect = nullptr) {
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		// Match DDNet's standard font-icon button metrics. NO_PIXEL_ALIGNMENT and
		// NO_OVERSIZE made some glyphs (notably STAR/EYE) collapse into a tiny dot
		// at this compact size.
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
		const int Result = DoOrganizerButton(pButton, pIcon, pRect, Enabled, Selected, IGraphics::CORNER_ALL, FontSize, OpticalOffset, pVisualRect);
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		return Result;
	};
	// Tabs share one frame. Their interaction rectangles stay unchanged, while
	// the visual tabs are inset into that frame so adjacent tabs never create a
	// pair of touching button borders.
	auto DoOrganizerTab = [&](const void *pButton, const char *pText, const CUIRect *pRect, bool Selected, int Corners) {
		const bool Hovered = Ui()->MouseHovered(pRect);
		const bool Active = Ui()->CheckActiveItem(pButton);
		CUIRect Visual = *pRect;
		Visual.Margin(1.0f, &Visual);
		const ColorRGBA Fill = Selected ? AccentSelected.WithAlpha(0.88f) :
			(Hovered || Active ? NeutralButtonHover : ColorRGBA(0.045f, 0.060f, 0.080f, 0.74f));
		Visual.Draw(Fill, Corners, 3.0f);
		SLabelProperties Props;
		Props.m_MaxWidth = std::max(0.0f, Visual.w - 6.0f);
		Props.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Visual, pText, 9.0f, TEXTALIGN_MC, Props);
		return Ui()->DoButtonLogic(pButton, 0, pRect, BUTTONFLAG_LEFT);
	};
	const char *apTabNames[NUMBER_OF_ASSETS_TABS] = {
		Localize("Entities"),
		Localize("Game"),
		Localize("Emoticons"),
		Localize("Particles"),
		Localize("HUD"),
		Localize("Extras"),
		Localize("Cursor"),
		Localize("Arrow"),
		Localize("Audio")};

	for(int Tab = ASSETS_TAB_ENTITIES; Tab < NUMBER_OF_ASSETS_TABS; ++Tab)
	{
		CUIRect Button;
		TabBar.VSplitLeft(TabWidth, &Button, &TabBar);
		const int Corners = Tab == ASSETS_TAB_ENTITIES ? IGraphics::CORNER_L : (Tab == NUMBER_OF_ASSETS_TABS - 1 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
		// Nine localized tabs share one row. Keep the label compact enough for
		// RU/UA on narrow settings windows instead of allowing a two-line wrap.
		if(DoOrganizerTab(&s_aPageTabs[Tab], apTabNames[Tab], &Button, s_CurCustomTab == Tab, Corners))
		{
			if(s_CurCustomTab != Tab)
			{
				// A card/folder pointer belongs to the old list. Drop transient
				// interaction state before changing the backing vector.
				ResetAssetOrganizerInteraction(Ui());
				s_CurCustomTab = Tab;
			}
		}
		if(Tab + 1 < NUMBER_OF_ASSETS_TABS)
		{
			// The child tab surfaces are inset, leaving a single clean separator
			// gap. Draw it once, never as two neighboring tab borders.
			CUIRect Separator = {Button.x + Button.w - 0.5f, TabBarOuter.y + 4.0f, 1.0f, std::max(0.0f, TabBarOuter.h - 8.0f)};
			Separator.Draw(AccentOutline, IGraphics::CORNER_NONE, 0.0f);
		}
	}

	const auto LoadStartTime = time_get_nanoseconds();
	auto LastLoadingRenderTime = LoadStartTime;
	std::unordered_set<std::string> KnownAssetIds;
	SMenuAssetScanUser User;
	User.m_pUser = this;
	User.m_pKnownAssetIds = &KnownAssetIds;
	User.m_LoadedFunc = [&]() {
		const auto Now = time_get_nanoseconds();
		if(Now - LoadStartTime > 500ms && Now - LastLoadingRenderTime >= 100ms)
		{
			RenderLoading(Localize("Loading assets"), "", 0);
			LastLoadingRenderTime = Now;
		}
	};
	bool AssetListScanned = false;
	if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
	{
		if(m_vEntitiesList.empty())
		{
			AssetListScanned = true;
			KnownAssetIds.emplace("default");
			SCustomEntities EntitiesItem;
			str_copy(EntitiesItem.m_aName, "default");
			EntitiesItem.m_Deletable = false;
			LoadEntities(&EntitiesItem, &User);
			m_vEntitiesList.push_back(EntitiesItem);

			// load entities
			Storage()->ListDirectory(IStorage::TYPE_ALL, "assets/entities", EntitiesScan, &User);
			std::sort(m_vEntitiesList.begin(), m_vEntitiesList.end());
			MarkCustomAssetsDeletable(ASSETS_TAB_ENTITIES);
		}
		if(m_vEntitiesList.size() != gs_aCustomListSize[s_CurCustomTab])
			gs_aInitCustomList[s_CurCustomTab] = true;
	}
	else if(s_CurCustomTab == ASSETS_TAB_GAME)
	{
		if(m_vGameList.empty())
		{
			AssetListScanned = true;
			SCustomGame DefaultItem;
			str_copy(DefaultItem.m_aName, "default");
			DefaultItem.m_Deletable = false;
			LoadAsset(&DefaultItem, "game", Graphics());
			m_vGameList.push_back(DefaultItem);

			std::unordered_set<std::string> KnownGamePackIds = {"default"};
			SGameAssetScanUser GameScanUser;
			GameScanUser.m_pMenus = this;
			GameScanUser.m_LoadedFunc = User.m_LoadedFunc;
			GameScanUser.m_pKnownPackIds = &KnownGamePackIds;
			Storage()->ListDirectory(IStorage::TYPE_ALL, "assets/game", GameScan, &GameScanUser);
			std::sort(m_vGameList.begin(), m_vGameList.end());
			MarkCustomAssetsDeletable(ASSETS_TAB_GAME);
			// The initial scan fills the backing list, so rebuild the organizer's
			// prepared visible list in this same entry pass. Previously only a
			// folder interaction set this flag, leaving Game empty on first open.
			gs_aInitCustomList[ASSETS_TAB_GAME] = true;
		}
	}
	else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
	{
		const bool WasEmpty = m_vEmoticonList.empty();
		InitAssetList(m_vEmoticonList, "assets/emoticons", "emoticons", EmoticonsScan, Graphics(), Storage(), KnownAssetIds, &User);
		AssetListScanned |= WasEmpty;
		if(WasEmpty)
			MarkCustomAssetsDeletable(ASSETS_TAB_EMOTICONS);
	}
	else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
	{
		const bool WasEmpty = m_vParticlesList.empty();
		InitAssetList(m_vParticlesList, "assets/particles", "particles", ParticlesScan, Graphics(), Storage(), KnownAssetIds, &User);
		AssetListScanned |= WasEmpty;
		if(WasEmpty)
			MarkCustomAssetsDeletable(ASSETS_TAB_PARTICLES);
	}
	else if(s_CurCustomTab == ASSETS_TAB_HUD)
	{
		const bool WasEmpty = m_vHudList.empty();
		InitAssetList(m_vHudList, "assets/hud", "hud", HudScan, Graphics(), Storage(), KnownAssetIds, &User);
		AssetListScanned |= WasEmpty;
		if(WasEmpty)
			MarkCustomAssetsDeletable(ASSETS_TAB_HUD);
	}
	else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
	{
		const bool WasEmpty = m_vExtrasList.empty();
		InitAssetList(m_vExtrasList, "assets/extras", "extras", ExtrasScan, Graphics(), Storage(), KnownAssetIds, &User);
		AssetListScanned |= WasEmpty;
		if(WasEmpty)
			MarkCustomAssetsDeletable(ASSETS_TAB_EXTRAS);
	}
	else if(s_CurCustomTab == ASSETS_TAB_CURSOR)
	{
		if(m_vCursorList.empty())
		{
			AssetListScanned = true;
			KnownAssetIds.emplace("default");
			SCustomCursor CursorItem;
			str_copy(CursorItem.m_aName, "default");
			CursorItem.m_Deletable = false;
			LoadCursorPreview(&CursorItem, Graphics());
			m_vCursorList.push_back(CursorItem);

			Storage()->ListDirectory(IStorage::TYPE_ALL, "assets/cursor", CursorScan, &User);
			std::sort(m_vCursorList.begin(), m_vCursorList.end());
			MarkCustomAssetsDeletable(ASSETS_TAB_CURSOR);
		}
		if(m_vCursorList.size() != gs_aCustomListSize[s_CurCustomTab])
			gs_aInitCustomList[s_CurCustomTab] = true;
	}
	else if(s_CurCustomTab == ASSETS_TAB_ARROW)
	{
		const bool WasEmpty = m_vArrowList.empty();
		InitAssetList(m_vArrowList, "assets/arrow", "arrow", ArrowScan, Graphics(), Storage(), KnownAssetIds, &User);
		AssetListScanned |= WasEmpty;
		if(WasEmpty)
			MarkCustomAssetsDeletable(ASSETS_TAB_ARROW);
	}
	else if(s_CurCustomTab == ASSETS_TAB_AUDIO)
	{
		if(m_vAudioPackList.empty())
		{
			AssetListScanned = true;
			KnownAssetIds.emplace("default");
			SCustomAudioPack DefaultItem;
			str_copy(DefaultItem.m_aName, "default");
			DefaultItem.m_Deletable = false;
			DefaultItem.m_RenderTexture = IGraphics::CTextureHandle();
			m_vAudioPackList.push_back(DefaultItem);

			// TYPE_ALL includes the packaged data directory, so a transferred
			// Release build exposes its bundled packs (including JuKKi) directly.
			Storage()->ListDirectory(IStorage::TYPE_ALL, "assets/audio", AudioPackScan, &User);
			Storage()->ListDirectory(IStorage::TYPE_ALL, "audio", AudioPackScan, &User);
			std::sort(m_vAudioPackList.begin(), m_vAudioPackList.end());
			MarkCustomAssetsDeletable(ASSETS_TAB_AUDIO);
		}
		if(m_vAudioPackList.size() != gs_aCustomListSize[s_CurCustomTab])
			gs_aInitCustomList[s_CurCustomTab] = true;
	}
	if(AssetListScanned)
		SyncAssetOrganizer(s_CurCustomTab);

	// The organizer is deliberately only a presentation layer. It stores a
	// library layout, never moves resource files or changes existing loaders.
	MainView.HSplitTop(6.0f, nullptr, &MainView);
	MainView.HSplitTop(31.0f, &OrganizerToolbar, &MainView);
	// Keep the toolbar and the two organizer columns in one coordinate frame.
	// The column rects below are inset for content, but their outer rects remain
	// the visual parents for the header and folder-creation controls.
	const CUIRect FolderToolbarOuter = OrganizerToolbar;
	MainView.HSplitBottom(57.0f, &OrganizerBody, &QuickSearch);
	QuickSearch.HSplitBottom(18.0f, &QuickSearch, &InputHints);
	constexpr float ORGANIZER_PANEL_PADDING = 7.0f;
	constexpr float ORGANIZER_CONTROL_HEIGHT = 21.0f;
	OrganizerBody.VSplitLeft(std::min(260.0f, std::max(185.0f, OrganizerBody.w * 0.24f)), &FoldersPanel, &AssetsPanel);
	const CUIRect FolderPanelOuter = FoldersPanel;
	const CUIRect AssetsPanelOuter = AssetsPanel;
	CUIRect FolderPanelFrame = FolderPanelOuter;
	FolderPanelFrame.Margin(2.0f, &FolderPanelFrame);
	CUIRect AssetsPanelFrame = AssetsPanelOuter;
	AssetsPanelFrame.Margin(2.0f, &AssetsPanelFrame);
	FoldersPanel.Margin(4.0f, &FoldersPanel);
	AssetsPanel.Margin(4.0f, &AssetsPanel);
	// Draw the frame at the actual column bounds, then inset the content below.
	// Previously the already-inset FoldersPanel was drawn as the parent, leaving
	// the toolbar title four pixels outside its apparent frame on narrow layouts.
	FolderPanelFrame.Draw(NeutralBorder, IGraphics::CORNER_ALL, 6.0f);
	CUIRect FolderPanelSurface = FolderPanelFrame;
	FolderPanelSurface.Margin(1.0f, &FolderPanelSurface);
	FolderPanelSurface.Draw(ColorRGBA(0.035f, 0.050f, 0.070f, 0.96f), IGraphics::CORNER_ALL, 5.0f);
	AssetsPanelFrame.Draw(NeutralBorder, IGraphics::CORNER_ALL, 6.0f);
	CUIRect AssetsPanelSurface = AssetsPanelFrame;
	AssetsPanelSurface.Margin(1.0f, &AssetsPanelSurface);
	AssetsPanelSurface.Draw(ColorRGBA(0.028f, 0.042f, 0.060f, 0.96f), IGraphics::CORNER_ALL, 5.0f);
	// Both dark organizer panels keep their content inside the same breathing
	// room. The frame rect remains unchanged; only the content rect is inset.
	AssetsPanel.Margin(ORGANIZER_PANEL_PADDING, &AssetsPanel);

	static CButtonContainer s_FolderCreateButton;
	static CButtonContainer s_PagePreviousButton;
	static CButtonContainer s_PageNextButton;
	static CButtonContainer s_PageAddButton;
	static CButtonContainer s_PageDeleteButton;
	static CLineInputBuffered<64> s_FolderNameInput;
	s_FolderNameInput.SetEmptyText(TCLocalize("Folder name", "AMF Client"));

	CUIRect FolderTitle, ToolbarActions;
	OrganizerToolbar.VSplitLeft(FoldersPanel.w, &FolderTitle, &OrganizerToolbar);
	OrganizerToolbar.VSplitLeft(8.0f, nullptr, &OrganizerToolbar);
	ToolbarActions = OrganizerToolbar;
	// The whole creation row is one clipped parent. This keeps localized labels,
	// the edit box and the action button inside their toolbar even when the
	// settings viewport is narrow or UI-scaled.
	CUIRect FolderToolbarFrame = FolderToolbarOuter;
	// Keep a deliberate breathing line between the creation toolbar and the
	// organizer columns below. The controls still use FolderToolbarOuter for
	// their exact interaction/clip rects; only the decorative frame is inset.
	FolderToolbarFrame.Margin(1.0f, &FolderToolbarFrame);
	FolderToolbarFrame.Draw(NeutralBorder, IGraphics::CORNER_ALL, 6.0f);
	FolderToolbarFrame.Margin(1.0f, &FolderToolbarFrame);
	FolderToolbarFrame.Draw(ColorRGBA(0.030f, 0.045f, 0.065f, 0.94f), IGraphics::CORNER_ALL, 5.0f);
	CUIRect FolderToolbarAccent = FolderToolbarOuter;
	FolderToolbarAccent.Margin(7.0f, &FolderToolbarAccent);
	FolderToolbarAccent.HSplitTop(2.0f, &FolderToolbarAccent, nullptr);
	FolderToolbarAccent.w = std::min(52.0f, std::max(0.0f, FolderToolbarAccent.w));
	FolderToolbarAccent.Draw(AccentHeader, IGraphics::CORNER_ALL, 1.0f);
	Ui()->ClipEnable(&FolderToolbarOuter);
	CUIRect FolderTitleVisual = FolderTitle;
	FolderTitleVisual.x = FolderPanelFrame.x;
	FolderTitleVisual.w = std::min(FolderTitleVisual.w, FolderPanelFrame.w);
	// Match the 31-unit creation controls while preserving a small horizontal
	// inset from the panel frame. The title, edit box and action button therefore
	// share the same outer height and vertical axis. The title is a label only;
	// the parent row owns the frame so it does not become a nested button border.
	constexpr float FOLDER_TOOLBAR_VERTICAL_PADDING = 2.0f;
	FolderTitleVisual.Margin(vec2(2.0f, FOLDER_TOOLBAR_VERTICAL_PADDING), &FolderTitleVisual);
	SLabelProperties FolderTitleProps;
	FolderTitleProps.m_MaxWidth = std::max(0.0f, FolderTitleVisual.w - 8.0f);
	FolderTitleProps.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&FolderTitleVisual, TCLocalize("Folders", "AMF Client"), 12.0f, TEXTALIGN_MC, FolderTitleProps);

	CUIRect FolderInput, FolderCreate;
	ToolbarActions.VSplitLeft(std::min(190.0f, ToolbarActions.w * 0.45f), &FolderInput, &ToolbarActions);
	ToolbarActions.VSplitLeft(9.0f, nullptr, &ToolbarActions);
	ToolbarActions.VSplitLeft(std::min(120.0f, ToolbarActions.w), &FolderCreate, &ToolbarActions);
	FolderInput.Margin(vec2(0.0f, FOLDER_TOOLBAR_VERTICAL_PADDING), &FolderInput);
	FolderCreate.Margin(vec2(0.0f, FOLDER_TOOLBAR_VERTICAL_PADDING), &FolderCreate);
	Ui()->DoEditBox(&s_FolderNameInput, &FolderInput, 10.5f);
	// The row already owns the enclosing frame. Keep the input's native
	// background/caret/selection, but do not add a second visual outline around
	// it. The create action uses the same compact button treatment without a
	// nested border, while retaining its original interaction rectangle.
	CUIRect FolderCreateVisual = FolderCreate;
	FolderCreateVisual.Margin(vec2(2.0f, 1.0f), &FolderCreateVisual);
	if(DoOrganizerButton(&s_FolderCreateButton, TCLocalize("Create folder", "AMF Client"), &FolderCreate,
		true, false, IGraphics::CORNER_ALL, 10.5f, vec2(0.0f, 0.0f), &FolderCreateVisual, false))
	{
		std::string CreatedId;
		if(AssetOrganizer::CreateFolder(s_CurCustomTab, s_FolderNameInput.GetString(), &CreatedId))
		{
			AssetOrganizer::SetCurrentFolder(s_CurCustomTab, CreatedId.c_str());
			s_FolderNameInput.Set("");
			gs_aInitCustomList[s_CurCustomTab] = true;
		}
	}
	Ui()->ClipDisable();

	// The folder list is an actual navigation pane, rather than a collection of
	// small controls mixed into the asset grid. It only reads the organizer
	// model, which is refreshed when a selector is opened or explicitly reloaded.
	CUIRect FolderCaption, FolderList, FolderCreateDropZone;
	FoldersPanel.Margin(ORGANIZER_PANEL_PADDING, &FoldersPanel);
	FoldersPanel.HSplitTop(22.0f, &FolderCaption, &FolderList);
	CUIRect FolderCaptionLine = FolderCaption;
	FolderCaptionLine.HSplitBottom(1.0f, nullptr, &FolderCaptionLine);
	FolderCaptionLine.Draw(NeutralBorderSoft, IGraphics::CORNER_NONE, 0.0f);
	CUIRect FolderCaptionAccent = FolderCaptionLine;
	FolderCaptionAccent.w = std::min(30.0f, std::max(0.0f, FolderCaptionAccent.w));
	FolderCaptionAccent.Draw(AccentHeader, IGraphics::CORNER_NONE, 0.0f);
	CUIRect FolderCaptionText = FolderCaption;
	FolderCaptionText.VMargin(2.0f, &FolderCaptionText);
	SLabelProperties FolderCaptionProps;
	FolderCaptionProps.m_MaxWidth = std::max(0.0f, FolderCaptionText.w - 4.0f);
	FolderCaptionProps.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&FolderCaptionText, TCLocalize("Folders", "AMF Client"), 12.0f, TEXTALIGN_ML, FolderCaptionProps);
	FolderList.HSplitTop(5.0f, nullptr, &FolderList);
	FolderList.HSplitBottom(128.0f, &FolderList, &FolderCreateDropZone);
	if(gs_AssetOrganizerDrag.m_Dragging && gs_AssetOrganizerDrag.m_Tab == s_CurCustomTab)
	{
		// Drop targets are frame-local. Leaving a folder/card must never retain a
		// stale target from a previous frame.
		gs_AssetOrganizerDrag.m_CreateFolderDropTarget = false;
		gs_AssetOrganizerDrag.m_CurrentPageDropTarget = false;
		gs_AssetOrganizerDrag.m_TargetRectValid = false;
		gs_AssetOrganizerDrag.m_aTargetFolderId[0] = '\0';
		gs_AssetOrganizerDrag.m_aTargetAssetId[0] = '\0';
	}
	if(gs_AssetFolderDrag.m_Dragging && gs_AssetFolderDrag.m_Tab == s_CurCustomTab && Ui()->MouseButton(0))
	{
		// Folder insertion is recalculated for the current pointer position.
		// Keep the final valid destination for the release frame, but never reuse
		// a line that belonged to a row the pointer has already left.
		gs_AssetFolderDrag.m_TargetIndex = -1;
	}

	static CScrollRegion s_FolderScrollRegion;
	CScrollRegionParams FolderScrollParams;
	FolderScrollParams.m_ScrollbarThickness = 8.0f;
	FolderScrollParams.m_ScrollbarMargin = 2.0f;
	FolderScrollParams.m_ScrollUnit = 32.0f;
	FolderScrollParams.m_RailBgColor = AccentOutline.WithAlpha(0.22f);
	FolderScrollParams.m_SliderColor = AccentSoft;
	s_FolderScrollRegion.Begin(&FolderList, &FolderScrollParams);
	CUIRect FolderContent = FolderList;
	static CButtonContainer s_FavoritesFolderButton;
	const auto &vFolders = AssetOrganizer::Folders(s_CurCustomTab);
	char aCurrentFolderId[64];
	str_copy(aCurrentFolderId, AssetOrganizer::CurrentFolder(s_CurCustomTab), sizeof(aCurrentFolderId));
	auto RenderFolderRow = [&](const void *pId, const char *pIdString, const char *pLabel, const char *pIcon, int FolderIndex, bool UserFolder) {
		CUIRect Row;
		FolderContent.HSplitTop(30.0f, &Row, &FolderContent);
		FolderContent.HSplitTop(3.0f, nullptr, &FolderContent);
		if(!s_FolderScrollRegion.AddRect(Row))
			return;

		const bool Selected = str_comp(aCurrentFolderId, pIdString) == 0;
		const int Clicked = Ui()->DoButtonLogic(pId, 0, &Row, BUTTONFLAG_LEFT);
		const bool Hovered = Ui()->HotItem() == pId;
		const ColorRGBA RowFill = Selected ? AccentColor(AccentLightness, 0.20f) :
			(Hovered ? ColorRGBA(0.085f, 0.11f, 0.14f, 0.96f) : ColorRGBA(0.060f, 0.080f, 0.105f, 0.82f));
		const ColorRGBA RowBorder = Selected ? AccentSelected : (Hovered ? AccentSoft : NeutralBorderSoft);
		Row.Draw(RowBorder, IGraphics::CORNER_ALL, 4.0f);
		CUIRect RowInner = Row;
		RowInner.Margin(1.0f, &RowInner);
		RowInner.Draw(RowFill, IGraphics::CORNER_ALL, 3.0f);

		CUIRect IconRect, NameRect, CountRect;
		Row.VSplitLeft(27.0f, &IconRect, &NameRect);
		NameRect.VSplitRight(32.0f, &NameRect, &CountRect);
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		Ui()->DoLabel(&IconRect, pIcon, 13.0f, TEXTALIGN_MC);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		SLabelProperties FolderNameProps;
		FolderNameProps.m_MaxWidth = NameRect.w - 3.0f;
		Ui()->DoLabel(&NameRect, pLabel, 12.0f, TEXTALIGN_ML, FolderNameProps);
		char aCount[16];
		str_format(aCount, sizeof(aCount), "%d", AssetOrganizer::FolderItemCount(s_CurCustomTab, pIdString));
		Ui()->DoLabel(&CountRect, aCount, 11.0f, TEXTALIGN_MC);
		if(Clicked && !gs_AssetFolderDrag.m_Dragging)
		{
			AssetOrganizer::SetCurrentFolder(s_CurCustomTab, pIdString);
			gs_aInitCustomList[s_CurCustomTab] = true;
		}
		if(UserFolder && Ui()->MouseHovered(&Row) && Ui()->MouseButtonClicked(1))
		{
			gs_AssetFolderPopupContext.m_pMenus = this;
			gs_AssetFolderPopupContext.m_Tab = s_CurCustomTab;
			str_copy(gs_AssetFolderPopupContext.m_aFolderId, pIdString, sizeof(gs_AssetFolderPopupContext.m_aFolderId));
			gs_AssetFolderPopupContext.m_NameInput.Set(pLabel);
			gs_AssetFolderPopupContext.m_NameInput.SetEmptyText(TCLocalize("Folder name", "AMF Client"));
			Ui()->DoPopupMenu(&gs_AssetFolderPopupContext, Ui()->MouseX(), Ui()->MouseY(), 176.0f, 84.0f, &gs_AssetFolderPopupContext, SAssetFolderPopupContext::Render);
		}

		if(UserFolder && gs_AssetFolderDrag.m_aSourceId[0] == '\0' && Ui()->CheckActiveItem(pId) && Ui()->MouseButton(0))
		{
			gs_AssetFolderDrag.m_Tab = s_CurCustomTab;
			gs_AssetFolderDrag.m_SourceIndex = FolderIndex;
			gs_AssetFolderDrag.m_PressPos = Ui()->MousePos();
			str_copy(gs_AssetFolderDrag.m_aSourceId, pIdString, sizeof(gs_AssetFolderDrag.m_aSourceId));
			str_copy(gs_AssetFolderDrag.m_aSourceName, pLabel, sizeof(gs_AssetFolderDrag.m_aSourceName));
		}
		if(gs_AssetFolderDrag.m_Tab == s_CurCustomTab && gs_AssetFolderDrag.m_aSourceId[0] != '\0' && Ui()->MouseButton(0) && !gs_AssetFolderDrag.m_Dragging && distance(Ui()->MousePos(), gs_AssetFolderDrag.m_PressPos) >= 6.0f)
			gs_AssetFolderDrag.m_Dragging = true;
		if(UserFolder && gs_AssetFolderDrag.m_Dragging && gs_AssetFolderDrag.m_Tab == s_CurCustomTab && Ui()->MouseHovered(&Row))
		{
			int InsertAt = FolderIndex + (Ui()->MouseY() >= Row.y + Row.h / 2.0f ? 1 : 0);
			if(gs_AssetFolderDrag.m_SourceIndex < InsertAt)
				--InsertAt;
			const int TargetIndex = std::clamp(InsertAt, 1, std::max(1, (int)vFolders.size() - 1));
			// A line is only shown for a destination that produces an actual
			// reordering. Hovering the source row (or the adjacent no-op side) must
			// not advertise a drop target that MoveFolderTo would reject.
			if(TargetIndex != gs_AssetFolderDrag.m_SourceIndex)
			{
				gs_AssetFolderDrag.m_TargetIndex = TargetIndex;
				CUIRect InsertGuide = {Row.x + 4.0f, Ui()->MouseY() >= Row.y + Row.h / 2.0f ? Row.y + Row.h - 1.0f : Row.y, Row.w - 8.0f, 2.0f};
				InsertGuide.Draw(AccentSelected, IGraphics::CORNER_ALL, 1.0f);
			}
		}
		if(gs_AssetOrganizerDrag.m_Dragging && gs_AssetOrganizerDrag.m_Tab == s_CurCustomTab && Ui()->MouseHovered(&Row) && !AssetOrganizer::IsFavoritesFolder(pIdString))
		{
			gs_AssetOrganizerDrag.m_CreateFolderDropTarget = false;
			gs_AssetOrganizerDrag.m_CurrentPageDropTarget = false;
			str_copy(gs_AssetOrganizerDrag.m_aTargetFolderId, pIdString, sizeof(gs_AssetOrganizerDrag.m_aTargetFolderId));
			gs_AssetOrganizerDrag.m_aTargetAssetId[0] = '\0';
			gs_AssetOrganizerDrag.m_TargetRect = Row;
			gs_AssetOrganizerDrag.m_TargetRectValid = true;
			Row.Draw(AccentSoft, IGraphics::CORNER_ALL, 4.0f);
			CUIRect DragRowInner = Row;
			DragRowInner.Margin(1.0f, &DragRowInner);
			DragRowInner.Draw(AccentSoft.WithAlpha(0.28f), IGraphics::CORNER_ALL, 3.0f);
		}
	};
	for(size_t FolderIndex = 0; FolderIndex < vFolders.size(); ++FolderIndex)
	{
		const AssetOrganizer::SFolder &Folder = vFolders[FolderIndex];
		RenderFolderRow(&Folder, Folder.m_Id.c_str(), Folder.m_Name.c_str(), str_comp(Folder.m_Id.c_str(), AssetOrganizer::FOLDER_UNSORTED) == 0 ? FontIcon::FOLDER_OPEN : FontIcon::FOLDER, (int)FolderIndex, !Folder.m_System);
	}
	RenderFolderRow(&s_FavoritesFolderButton, AssetOrganizer::FOLDER_FAVORITES, TCLocalize("Favorites", "AMF Client"), FontIcon::STAR, -1, false);
	s_FolderScrollRegion.End();
	if(gs_AssetFolderDrag.m_Tab == s_CurCustomTab && gs_AssetFolderDrag.m_aSourceId[0] != '\0' && !Ui()->MouseButton(0))
	{
		if(gs_AssetFolderDrag.m_Dragging && gs_AssetFolderDrag.m_TargetIndex >= 1 && gs_AssetFolderDrag.m_TargetIndex != gs_AssetFolderDrag.m_SourceIndex)
			AssetOrganizer::MoveFolderTo(s_CurCustomTab, gs_AssetFolderDrag.m_aSourceId, gs_AssetFolderDrag.m_TargetIndex);
		gs_AssetFolderDrag.Reset();
	}
	if(gs_AssetFolderDrag.m_Dragging && gs_AssetFolderDrag.m_Tab == s_CurCustomTab)
	{
		CUIRect FolderGhost = {Ui()->MouseX() + 11.0f, Ui()->MouseY() + 9.0f, 145.0f, 27.0f};
		FolderGhost.Draw(AccentSelected, IGraphics::CORNER_ALL, 4.0f);
		CUIRect FolderGhostInner = FolderGhost;
		FolderGhostInner.Margin(1.0f, &FolderGhostInner);
		FolderGhostInner.Draw(AccentColor(AccentLightness, 0.72f), IGraphics::CORNER_ALL, 3.0f);
		SLabelProperties FolderGhostProps;
		FolderGhostProps.m_MaxWidth = FolderGhostInner.w - 8.0f;
		FolderGhostProps.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&FolderGhostInner, gs_AssetFolderDrag.m_aSourceName, 10.0f, TEXTALIGN_MC, FolderGhostProps);
	}

	// This is a passive drop target: it never owns a regular click, so it cannot
	// steal card selection. While a real drag is over it, dropping creates a
	// named folder and moves the item there in a single persisted operation.
	FolderCreateDropZone.Margin(5.0f, &FolderCreateDropZone);
	const bool HoveringCreateDropZone = gs_AssetOrganizerDrag.m_Dragging &&
		gs_AssetOrganizerDrag.m_Tab == s_CurCustomTab && Ui()->MouseHovered(&FolderCreateDropZone);
	// The organizer panel is the only solid container. The drop target itself
	// uses one dashed outline plus a quiet surface, avoiding a double frame.
	CUIRect FolderCreateDropZoneInner = FolderCreateDropZone;
	FolderCreateDropZoneInner.Margin(1.0f, &FolderCreateDropZoneInner);
	FolderCreateDropZoneInner.Draw(HoveringCreateDropZone ? AccentSoft.WithAlpha(0.58f) : ColorRGBA(0.030f, 0.045f, 0.065f, 0.84f), IGraphics::CORNER_ALL, 4.0f);
	const ColorRGBA DropBorder = HoveringCreateDropZone ? AccentSelected : NeutralBorderSoft;
	const float DashLength = 5.0f;
	const float DashGap = 4.0f;
	for(float X = FolderCreateDropZone.x + 4.0f; X < FolderCreateDropZone.x + FolderCreateDropZone.w - 4.0f; X += DashLength + DashGap)
	{
		const float Width = std::min(DashLength, FolderCreateDropZone.x + FolderCreateDropZone.w - 4.0f - X);
		CUIRect TopDash = {X, FolderCreateDropZone.y + 1.0f, Width, 1.0f};
		CUIRect BottomDash = {X, FolderCreateDropZone.y + FolderCreateDropZone.h - 2.0f, Width, 1.0f};
		TopDash.Draw(DropBorder, IGraphics::CORNER_NONE, 0.0f);
		BottomDash.Draw(DropBorder, IGraphics::CORNER_NONE, 0.0f);
	}
	for(float Y = FolderCreateDropZone.y + 4.0f; Y < FolderCreateDropZone.y + FolderCreateDropZone.h - 4.0f; Y += DashLength + DashGap)
	{
		const float Height = std::min(DashLength, FolderCreateDropZone.y + FolderCreateDropZone.h - 4.0f - Y);
		CUIRect LeftDash = {FolderCreateDropZone.x + 1.0f, Y, 1.0f, Height};
		CUIRect RightDash = {FolderCreateDropZone.x + FolderCreateDropZone.w - 2.0f, Y, 1.0f, Height};
		LeftDash.Draw(DropBorder, IGraphics::CORNER_NONE, 0.0f);
		RightDash.Draw(DropBorder, IGraphics::CORNER_NONE, 0.0f);
	}
	CUIRect DropIcon, DropText;
	FolderCreateDropZone.HSplitTop(FolderCreateDropZone.h * 0.52f, &DropIcon, &DropText);
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	Ui()->DoLabel(&DropIcon, FontIcon::FOLDER, 28.0f, TEXTALIGN_MC);
	CUIRect DropPlus = {DropIcon.x + DropIcon.w * 0.56f, DropIcon.y + DropIcon.h * 0.43f, 13.0f, 13.0f};
	Ui()->DoLabel(&DropPlus, FontIcon::PLUS, 11.0f, TEXTALIGN_MC);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	DropText.Margin(12.0f, &DropText);
	SLabelProperties DropTextProps;
	DropTextProps.m_MaxWidth = DropText.w;
	DropTextProps.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&DropText, TCLocalize("Drop here to create folder", "AMF Client"), 8.5f, TEXTALIGN_MC, DropTextProps);
	if(HoveringCreateDropZone)
	{
		gs_AssetOrganizerDrag.m_CreateFolderDropTarget = true;
		gs_AssetOrganizerDrag.m_CurrentPageDropTarget = false;
		gs_AssetOrganizerDrag.m_aTargetFolderId[0] = '\0';
		gs_AssetOrganizerDrag.m_aTargetAssetId[0] = '\0';
		gs_AssetOrganizerDrag.m_TargetRect = FolderCreateDropZone;
		gs_AssetOrganizerDrag.m_TargetRectValid = true;
	}

	// Page navigation belongs to the asset panel. Pages remain in the layout
	// even when empty, so the plus button always creates a stable destination.
	// Use the exact control height of the bottom "Assets directory" action. This
	// keeps the page row compact and visually consistent at every UI scale.
	AssetsPanel.HSplitTop(ORGANIZER_CONTROL_HEIGHT, &AssetHeader, &CustomList);
	// Split the remaining grid rect, not AssetsPanel again. The old second split
	// recreated CustomList from the full panel, which put card hitboxes on top of
	// the page controls even though the controls were visibly above them.
	CustomList.HSplitTop(6.0f, nullptr, &CustomList);
	// Keep one immutable parent rect for the complete header. It is the source
	// for the title, separator and every page-control visual center below.
	const CUIRect HeaderRect = AssetHeader;
	CUIRect FolderNameRect, PageControls;
	HeaderRect.VSplitLeft(150.0f, &FolderNameRect, &PageControls);
	SLabelProperties CurrentFolderProps;
	CurrentFolderProps.m_MaxWidth = FolderNameRect.w - 5.0f;
	Ui()->DoLabel(&FolderNameRect, AssetOrganizer::FolderName(s_CurCustomTab, AssetOrganizer::CurrentFolder(s_CurCustomTab)), 12.0f, TEXTALIGN_ML, CurrentFolderProps);
	CUIRect AssetHeaderLine = HeaderRect;
	AssetHeaderLine.HSplitBottom(1.0f, nullptr, &AssetHeaderLine);
	AssetHeaderLine.Draw(NeutralBorderSoft, IGraphics::CORNER_NONE, 0.0f);
	CUIRect AssetHeaderAccent = AssetHeaderLine;
	// Start the accent at the actual header/content edge. The previous extra
	// two-pixel shift created an unexplained dark "air" before the colored line.
	AssetHeaderAccent.w = std::min(34.0f, std::max(0.0f, AssetHeaderAccent.w));
	AssetHeaderAccent.Draw(AccentHeader, IGraphics::CORNER_NONE, 0.0f);
	constexpr float PAGE_CONTROL_GAP = 6.0f;
	constexpr float PAGE_NAV_WIDTH = 112.0f;
	const float PageIconWidth = ORGANIZER_CONTROL_HEIGHT;
	PageControls.VSplitRight(PageIconWidth, &PageControls, &OrganizerPageAdd);
	PageControls.VSplitRight(PAGE_CONTROL_GAP, &PageControls, nullptr);
	PageControls.VSplitRight(PageIconWidth, &PageControls, &OrganizerPageNext);
	PageControls.VSplitRight(PAGE_CONTROL_GAP, &PageControls, nullptr);
	PageControls.VSplitRight(PAGE_NAV_WIDTH, &PageControls, &OrganizerPageNav);
	PageControls.VSplitRight(PAGE_CONTROL_GAP, &PageControls, nullptr);
	PageControls.VSplitRight(PageIconWidth, &PageControls, &OrganizerPagePrevious);
	PageControls.VSplitRight(PAGE_CONTROL_GAP, &PageControls, nullptr);
	PageControls.VSplitRight(PageIconWidth, &PageControls, &OrganizerPageDelete);
	// Keep every interaction rect (and therefore all X coordinates) unchanged.
	// AssetHeader is inset from the visible panel frame by the content padding,
	// so it is not the visual header's top edge. The visible header runs from the
	// frame top to the single separator drawn above; use those exact bounds for
	// the common Y center instead of centering from the inset interaction rect.
	CUIRect VisibleHeaderRect = AssetsPanelFrame;
	VisibleHeaderRect.h = std::max(0.0f, AssetHeaderLine.y + AssetHeaderLine.h - VisibleHeaderRect.y);
	const float HeaderCenterY = VisibleHeaderRect.y + VisibleHeaderRect.h * 0.5f;
	const float HeaderControlHeight = std::max(0.0f, HeaderRect.h - 4.0f);
	auto CenterHeaderControl = [&](const CUIRect &Control) {
		CUIRect Visual = Control;
		Visual.h = std::min(HeaderControlHeight, Control.h);
		Visual.y = HeaderCenterY - Visual.h * 0.5f;
		return Visual;
	};
	CUIRect OrganizerPagePreviousVisual = CenterHeaderControl(OrganizerPagePrevious);
	CUIRect OrganizerPageNextVisual = CenterHeaderControl(OrganizerPageNext);
	CUIRect OrganizerPageAddVisual = CenterHeaderControl(OrganizerPageAdd);
	CUIRect OrganizerPageDeleteVisual = CenterHeaderControl(OrganizerPageDelete);
	CUIRect OrganizerPageNavVisual = CenterHeaderControl(OrganizerPageNav);
	const bool CanEditPages = !AssetOrganizer::IsFavoritesFolder(AssetOrganizer::CurrentFolder(s_CurCustomTab));
	const int CurrentPage = AssetOrganizer::CurrentPage(s_CurCustomTab);
	const int PageCount = AssetOrganizer::PageCount(s_CurCustomTab, AssetOrganizer::CurrentFolder(s_CurCustomTab));
	if(DoOrganizerIconButton(&s_PagePreviousButton, FontIcon::CHEVRON_LEFT, &OrganizerPagePrevious, CurrentPage > 0, false, 12.0f, vec2(0.0f, 0.0f), &OrganizerPagePreviousVisual) && CurrentPage > 0)
	{
		AssetOrganizer::SetCurrentPage(s_CurCustomTab, CurrentPage - 1);
		gs_aInitCustomList[s_CurCustomTab] = true;
	}
	char aPageLabel[64];
	str_format(aPageLabel, sizeof(aPageLabel), "%s %d / %d", TCLocalize("Page", "AMF Client"), CurrentPage + 1, PageCount);
	OrganizerPageNavVisual.Draw(NeutralBorderSoft, IGraphics::CORNER_ALL, 4.0f);
	CUIRect OrganizerPageNavInner = OrganizerPageNavVisual;
	OrganizerPageNavInner.Margin(1.0f, &OrganizerPageNavInner);
	OrganizerPageNavInner.Draw(ColorRGBA(0.045f, 0.060f, 0.080f, 0.92f), IGraphics::CORNER_ALL, 3.0f);
	SLabelProperties PageLabelProps;
	PageLabelProps.m_MaxWidth = OrganizerPageNavInner.w - 6.0f;
	PageLabelProps.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&OrganizerPageNavInner, aPageLabel, 10.5f, TEXTALIGN_MC, PageLabelProps);
	if(DoOrganizerIconButton(&s_PageNextButton, FontIcon::CHEVRON_RIGHT, &OrganizerPageNext, CurrentPage + 1 < PageCount, false, 12.0f, vec2(0.0f, 0.0f), &OrganizerPageNextVisual) && CurrentPage + 1 < PageCount)
	{
		AssetOrganizer::SetCurrentPage(s_CurCustomTab, CurrentPage + 1);
		gs_aInitCustomList[s_CurCustomTab] = true;
	}
	if(DoOrganizerIconButton(&s_PageAddButton, FontIcon::PLUS, &OrganizerPageAdd, CanEditPages, false, 12.0f, vec2(0.0f, 0.0f), &OrganizerPageAddVisual) && CanEditPages)
	{
		AssetOrganizer::AddPage(s_CurCustomTab, AssetOrganizer::CurrentFolder(s_CurCustomTab));
		AssetOrganizer::SetCurrentPage(s_CurCustomTab, AssetOrganizer::PageCount(s_CurCustomTab, AssetOrganizer::CurrentFolder(s_CurCustomTab)) - 1);
		gs_aInitCustomList[s_CurCustomTab] = true;
	}
	GameClient()->m_Tooltips.DoToolTip(&s_PageAddButton, &OrganizerPageAdd, TCLocalize("New page", "AMF Client"));
	const bool CanDeletePage = CanEditPages && PageCount > 1;
	TextRender()->TextColor(CanDeletePage ? ColorRGBA(1.0f, 0.70f, 0.72f, 0.98f) : ColorRGBA(0.45f, 0.49f, 0.54f, 0.65f));
	if(DoOrganizerIconButton(&s_PageDeleteButton, FontIcon::TRASH, &OrganizerPageDelete, CanDeletePage, false, 11.0f, vec2(0.0f, 0.0f), &OrganizerPageDeleteVisual) && CanDeletePage)
	{
		AssetOrganizer::DeletePage(s_CurCustomTab, AssetOrganizer::CurrentFolder(s_CurCustomTab), CurrentPage);
		gs_aInitCustomList[s_CurCustomTab] = true;
	}
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	GameClient()->m_Tooltips.DoToolTip(&s_PageDeleteButton, &OrganizerPageDelete, TCLocalize("Delete page", "AMF Client"));
	if(gs_aInitCustomList[s_CurCustomTab])
	{
		int ListSize = 0;
		if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
		{
			ListSize = InitSearchList(gs_vpSearchEntitiesList, m_vEntitiesList);
			SortSearchList(gs_vpSearchEntitiesList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_GAME)
		{
			ListSize = InitSearchList(gs_vpSearchGamesList, m_vGameList);
			SortSearchList(gs_vpSearchGamesList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
		{
			ListSize = InitSearchList(gs_vpSearchEmoticonsList, m_vEmoticonList);
			SortSearchList(gs_vpSearchEmoticonsList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
		{
			ListSize = InitSearchList(gs_vpSearchParticlesList, m_vParticlesList);
			SortSearchList(gs_vpSearchParticlesList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_HUD)
		{
			ListSize = InitSearchList(gs_vpSearchHudList, m_vHudList);
			SortSearchList(gs_vpSearchHudList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
		{
			ListSize = InitSearchList(gs_vpSearchExtrasList, m_vExtrasList);
			SortSearchList(gs_vpSearchExtrasList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_CURSOR)
		{
			ListSize = InitSearchList(gs_vpSearchCursorList, m_vCursorList);
			SortSearchList(gs_vpSearchCursorList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_ARROW)
		{
			ListSize = InitSearchList(gs_vpSearchArrowList, m_vArrowList);
			SortSearchList(gs_vpSearchArrowList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_AUDIO)
		{
			ListSize = InitSearchList(gs_vpSearchAudioPackList, m_vAudioPackList);
			SortSearchList(gs_vpSearchAudioPackList);
		}
		gs_aInitCustomList[s_CurCustomTab] = false;
		gs_aCustomListSize[s_CurCustomTab] = ListSize;
	}

	int OldSelected = -1;
	constexpr float CardSpacing = 3.0f;
	float TextureWidth = 132.0f;
	float TextureHeight = 112.0f;

	size_t SearchListSize = 0;
	bool SkipSelectionBecauseDelete = false;

	if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
	{
		SearchListSize = gs_vpSearchEntitiesList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_GAME)
	{
		SearchListSize = gs_vpSearchGamesList.size();
		TextureHeight = 82.0f;
	}
	else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
	{
		SearchListSize = gs_vpSearchEmoticonsList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
	{
		SearchListSize = gs_vpSearchParticlesList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_HUD)
	{
		SearchListSize = gs_vpSearchHudList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
	{
		SearchListSize = gs_vpSearchExtrasList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_CURSOR)
	{
		SearchListSize = gs_vpSearchCursorList.size();
		TextureHeight = 82.0f;
		TextureWidth = 82.0f;
	}
	else if(s_CurCustomTab == ASSETS_TAB_ARROW)
	{
		SearchListSize = gs_vpSearchArrowList.size();
		TextureHeight = 82.0f;
		TextureWidth = 82.0f;
	}
	else if(s_CurCustomTab == ASSETS_TAB_AUDIO)
	{
		SearchListSize = gs_vpSearchAudioPackList.size();
		TextureHeight = 74.0f;
		TextureWidth = 96.0f;
	}
	// Resolve the active asset before DoStart. Previously this was done inside
	// the card loop after DoStart had already captured -1 as the selected index,
	// which made selection state lag a frame and conflicted with drag release.
	for(size_t Index = 0; Index < SearchListSize; ++Index)
	{
		const SCustomItem *pItem = GetCustomItem(s_CurCustomTab, Index);
		if(pItem && AssetsIsCurrentlySelected(s_CurCustomTab, pItem->m_aName))
		{
			OldSelected = (int)Index;
			break;
		}
	}

	static CListBox s_ListBox;
	// The listbox provides only scrolling and page-sized rows here. Every visual
	// and mouse interaction below uses the exact same card rect, avoiding the
	// old mismatch between a padded preview and an unpadded listbox hitbox.
	const float ItemHeight = TextureHeight + 36.0f;
	const int ItemsPerRow = std::max(1, (int)((CustomList.w + CardSpacing) / (TextureWidth + CardSpacing)));
	// CListBox auto-spacing is per item, not per row. Keep it disabled for a
	// multi-column grid so scrolling and hit geometry remain deterministic.
	s_ListBox.DoAutoSpacing(0.0f);
	s_ListBox.DoStart(ItemHeight, SearchListSize, ItemsPerRow, 1, OldSelected, &CustomList, false, IGraphics::CORNER_ALL, false, CardSpacing);
	for(size_t i = 0; i < SearchListSize; ++i)
	{
		if(i > 0 && i % ItemsPerRow == 0)
			s_ListBox.DoSpacing(CardSpacing);
		const SCustomItem *pItem = GetCustomItem(s_CurCustomTab, i);
		if(pItem == nullptr)
			continue;

		const bool Favorite = IsFavoriteAsset(s_CurCustomTab, pItem->m_aName);
		const bool CanDelete = pItem->m_Deletable;
		const CListboxItem Item = s_ListBox.DoNextItem(&pItem->m_CardButtonId, OldSelected >= 0 && (size_t)OldSelected == i);
		if(!Item.m_Visible)
			continue;
		const CUIRect CardRect = Item.m_Rect;
		CUIRect ItemRect = CardRect;
		// This is the original asset-selector card inset. Card/grid dimensions and
		// the listbox hitbox stay unchanged; restoring the inset restores the old
		// favorite padding and the old preview's content origin together.
		ItemRect.Margin(5.0f, &ItemRect);
		CUIRect HeaderRow, ActionRow, TextureRect;
		ItemRect.HSplitTop(20.0f, &HeaderRow, nullptr);
		CUIRect FavoriteButton, DeleteButton;
		HeaderRow.VSplitLeft(20.0f, &FavoriteButton, nullptr);
		ItemRect.HSplitBottom(20.0f, nullptr, &ActionRow);
		if(CanDelete)
		{
			ActionRow.VSplitRight(20.0f, nullptr, &DeleteButton);
		}
		else
			DeleteButton = {0, 0, 0, 0};
		constexpr float CARD_ACTION_SCALE = 0.98f;
		// Keep the original 20-unit slots and centers, but make both action
		// hitboxes uniformly smaller by the requested couple of percent.
		FavoriteButton.Margin((20.0f - 20.0f * CARD_ACTION_SCALE) * 0.5f, &FavoriteButton);
		if(CanDelete)
			DeleteButton.Margin((20.0f - 20.0f * CARD_ACTION_SCALE) * 0.5f, &DeleteButton);
		// Keep both action controls centered in their original 20-unit slots while
		// using the uniformly reduced hitboxes for the favorite/delete actions.
		constexpr float CARD_ACTION_HEIGHT = 20.0f * CARD_ACTION_SCALE;
		constexpr float CARD_VISIBLE_ACTION_HEIGHT = 16.0f * CARD_ACTION_SCALE;
		const float CARD_ACTION_VERTICAL_INSET = (CARD_ACTION_HEIGHT - CARD_VISIBLE_ACTION_HEIGHT) * 0.5f;
		CUIRect FavoriteVisual = FavoriteButton;
		CUIRect DeleteVisual = DeleteButton;
		if(CanDelete)
			DeleteVisual.Margin(vec2(0.0f, CARD_ACTION_VERTICAL_INSET), &DeleteVisual);
		// Restore the historical organizer preview ownership from
		// menus_settings_assets.cpp. The title and actions keep their existing
		// bands, while the texture is centered in the remaining card content.
		TextureRect = ItemRect;
		if(s_CurCustomTab == ASSETS_TAB_AUDIO)
		{
			TextureRect.HSplitTop(15.0f, nullptr, &TextureRect);
			TextureRect.HSplitTop(10.0f, nullptr, &TextureRect);
			TextureRect.HSplitBottom(20.0f, &TextureRect, nullptr);
		}
		// Center every pack name in a stable, symmetric title area. The favorite
		// control remains in its fixed 20-unit slot, but it must not shift the
		// title's visual center toward the right.
		CUIRect TitleRect = HeaderRow;
		TitleRect.VMargin(23.0f, &TitleRect);
		TitleRect.y -= 3.0f;
		const bool CardSelected = OldSelected >= 0 && (size_t)OldSelected == i;
		const bool CardHovered = Ui()->MouseHovered(&CardRect);
		const bool CardIsDragSource = gs_AssetOrganizerDrag.m_Dragging && gs_AssetOrganizerDrag.m_Tab == s_CurCustomTab && gs_AssetOrganizerDrag.m_pSourceItem == pItem;
		const ColorRGBA CardColor = CardHovered ? ColorRGBA(0.085f, 0.11f, 0.14f, 0.97f) : ColorRGBA(0.045f, 0.060f, 0.080f, 0.94f);
		// A subtle frame gives every card a stable silhouette. Selection/hover
		// changes only the frame (and the neutral hover surface); the CListBox
		// rect remains the sole interaction and drag hitbox.
		const ColorRGBA CardBorder = CardSelected ? AccentSelected : (CardHovered ? AccentSoft : NeutralBorderSoft);
		CardRect.Draw(CardBorder, IGraphics::CORNER_ALL, 5.0f);
		CUIRect CardInner = CardRect;
		// Keep the content template identical for normal, hover and selected
		// cards. The selected state is carried by the border color only, so it
		// cannot change preview padding or the grid's perceived card size.
		CardInner.Margin(1.0f, &CardInner);
		CardInner.Draw(CardColor, IGraphics::CORNER_ALL, 4.0f);

		// CListBox owns the button activation. Do not call SetActiveItem here:
		// DoButtonLogic also stores which mouse button activated the item, and
		// bypassing it leaves m_ActiveButtonLogicButton at -1 (the assertion from
		// the previous implementation).
		if(gs_AssetOrganizerDrag.m_pSourceItem == nullptr && CardSelected && Ui()->CheckActiveItem(&pItem->m_CardButtonId) && Ui()->MouseButton(0) && !Ui()->MouseHovered(&FavoriteButton) && !Ui()->MouseHovered(&DeleteButton))
		{
			gs_AssetOrganizerDrag.m_pSourceItem = pItem;
			gs_AssetOrganizerDrag.m_Tab = s_CurCustomTab;
			gs_AssetOrganizerDrag.m_PressPos = Ui()->MousePos();
			gs_AssetOrganizerDrag.m_SourceRect = CardRect;
			gs_AssetOrganizerDrag.m_DragOffset = Ui()->MousePos() - vec2(CardRect.x, CardRect.y);
			gs_AssetOrganizerDrag.m_GhostPos = vec2(CardRect.x, CardRect.y);
			gs_AssetOrganizerDrag.m_Dragging = false;
			gs_AssetOrganizerDrag.m_TargetRectValid = false;
			gs_AssetOrganizerDrag.m_aTargetFolderId[0] = '\0';
			gs_AssetOrganizerDrag.m_aTargetAssetId[0] = '\0';
		}
		if(gs_AssetOrganizerDrag.m_Dragging && gs_AssetOrganizerDrag.m_Tab == s_CurCustomTab && gs_AssetOrganizerDrag.m_pSourceItem != pItem && Ui()->MouseHovered(&CardRect))
		{
			gs_AssetOrganizerDrag.m_CreateFolderDropTarget = false;
			gs_AssetOrganizerDrag.m_CurrentPageDropTarget = false;
			str_copy(gs_AssetOrganizerDrag.m_aTargetAssetId, pItem->m_aName, sizeof(gs_AssetOrganizerDrag.m_aTargetAssetId));
			gs_AssetOrganizerDrag.m_aTargetFolderId[0] = '\0';
			gs_AssetOrganizerDrag.m_TargetRect = CardRect;
			gs_AssetOrganizerDrag.m_TargetRectValid = true;
			CardRect.Draw(AccentSoft, IGraphics::CORNER_ALL, 5.0f);
		}

		// Do not register a second full-card button for RMB. A right-only
		// DoButtonLogic call steals HotItem from CListBox's left-button card on the
		// same frame, which made selection and dragging impossible. The exact mouse
		// event is sufficient for opening a popup and does not mutate active state.
		if(Ui()->MouseHovered(&CardRect) && Ui()->MouseButtonClicked(1))
		{
			gs_AssetOrganizerPopupContext.m_pMenus = this;
			gs_AssetOrganizerPopupContext.m_Tab = s_CurCustomTab;
			str_copy(gs_AssetOrganizerPopupContext.m_aAssetId, pItem->m_aName, sizeof(gs_AssetOrganizerPopupContext.m_aAssetId));
			str_copy(gs_AssetOrganizerPopupContext.m_aTargetFolderId, AssetOrganizer::ItemFolder(s_CurCustomTab, pItem->m_aName), sizeof(gs_AssetOrganizerPopupContext.m_aTargetFolderId));
			gs_AssetOrganizerPopupContext.m_TargetPage = AssetOrganizer::ItemPage(s_CurCustomTab, pItem->m_aName);
			Ui()->DoPopupMenu(&gs_AssetOrganizerPopupContext, Ui()->MouseX(), Ui()->MouseY(), 178.0f, 112.0f, &gs_AssetOrganizerPopupContext, SAssetOrganizerPopupContext::Render);
		}

		if(s_CurCustomTab == ASSETS_TAB_AUDIO)
		{
			// Keep the title in the header band and the preview in its own band.
			CUIRect IconRect = TextureRect;
			SLabelProperties AudioLabelProps;
			AudioLabelProps.m_MaxWidth = std::max(0.0f, TitleRect.w - 4.0f);
			AudioLabelProps.m_EllipsisAtEnd = true;
			Ui()->DoLabel(&TitleRect, pItem->m_aName, 10.0f, TEXTALIGN_MC, AudioLabelProps);
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
			TextRender()->TextColor(AudioNoteColor);
				const float MusicFontSize = 28.0f;
				CUIRect MusicVisual = IconRect;
				const vec2 MusicOffset = AudioPreviewOpticalOffset(MusicFontSize);
				MusicVisual.x += MusicOffset.x;
				const float PreviewAreaCenterY = IconRect.y + IconRect.h * 0.5f;
				MusicVisual.y = PreviewAreaCenterY - MusicVisual.h * 0.5f;
				Ui()->DoLabel(&MusicVisual, FontIcon::MUSIC, MusicFontSize, TEXTALIGN_MC);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			TextRender()->SetRenderFlags(0);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		}
		else
		{
		// Keep the title in the current header band. The preview quad below uses
		// the original asset-selector max-fit limits while the current action
		// hitboxes remain unchanged.
		SLabelProperties AssetLabelProps;
		AssetLabelProps.m_MaxWidth = std::max(0.0f, TitleRect.w - 4.0f);
		AssetLabelProps.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&TitleRect, pItem->m_aName, 10.0f, TEXTALIGN_MC, AssetLabelProps);
		// This is the legacy size calculation: cap the preview at the per-category
		// dimensions, then aspect-fit the item's native preview ratio inside the
		// safe card content. Do not replace this with a fixed-scale layout.
		const float MaxPreviewWidth = std::max(1.0f, std::min(TextureWidth, TextureRect.w - 8.0f));
		const float MaxPreviewHeight = std::max(1.0f, std::min(TextureHeight, TextureRect.h - 6.0f));
		const float PreviewAspect = std::clamp(pItem->m_PreviewAspect, 0.1f, 10.0f);
		float PreviewWidth = MaxPreviewWidth;
		float PreviewHeight = PreviewWidth / PreviewAspect;
		if(PreviewHeight > MaxPreviewHeight)
		{
			PreviewHeight = MaxPreviewHeight;
			PreviewWidth = PreviewHeight * PreviewAspect;
		}
		if(s_CurCustomTab == ASSETS_TAB_CURSOR || s_CurCustomTab == ASSETS_TAB_ARROW)
		{
			PreviewWidth *= 0.8f;
			PreviewHeight *= 0.8f;
		}
		// Keep the restored legacy preview virtually unchanged while leaving a
		// small, resolution-independent visual clearance from the card actions.
		constexpr float PREVIEW_SCALE = 0.975f;
		PreviewWidth *= PREVIEW_SCALE;
		PreviewHeight *= PREVIEW_SCALE;
		if(s_CurCustomTab == ASSETS_TAB_ENTITIES && s_EntityGamePreview)
		{
			const auto *pEntitiesItem = static_cast<const SCustomEntities *>(pItem);
			IGraphics::CTextureHandle Tex;
			for(int m = 0; m < MAP_IMAGE_MOD_TYPE_COUNT && !Tex.IsValid(); m++)
				Tex = pEntitiesItem->m_aImages[m].m_Texture;
			if(!Tex.IsValid())
				Tex = pItem->m_RenderTexture;

			if(Tex.IsValid())
			{
				static const int COLS = 7, ROWS = 7;
				static const unsigned char aLayout[ROWS][COLS] = {
					{TILE_SOLID, TILE_SOLID, TILE_SOLID, TILE_SOLID, TILE_SOLID, TILE_SOLID, TILE_SOLID},
					{TILE_SOLID, 0, 0, 0, 0, 0, TILE_NOHOOK},
					{TILE_SOLID, TILE_FREEZE, 0, 0, 0, 0, TILE_NOHOOK},
					{TILE_SOLID, 0, TILE_DEATH, 0, TILE_UNFREEZE, 0, TILE_NOHOOK},
					{TILE_SOLID, 0, 0, 0, 0, TILE_DFREEZE, TILE_NOHOOK},
					{TILE_SOLID, 0, 0, 0, 0, 0, TILE_NOHOOK},
					{TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK},
				};

				const float TileSize = std::min(PreviewWidth / (float)COLS, PreviewHeight / (float)ROWS);
				const float PreviewSceneWidth = COLS * TileSize;
				const float OffX = TextureRect.x + (TextureRect.w - PreviewSceneWidth) / 2.0f;
				const float OffY = TextureRect.y + (TextureRect.h - ROWS * TileSize) / 2.0f;
				const float KInset = 1.5f / 1024.0f;
				const float KTile = 1.0f / 16.0f;

				Graphics()->WrapClamp();
				Graphics()->TextureSet(Tex);
				Graphics()->QuadsBegin();
				Graphics()->SetColor(1, 1, 1, 1);
				for(int r = 0; r < ROWS; r++)
				{
					for(int c = 0; c < COLS; c++)
					{
						const unsigned char Tile = aLayout[r][c];
						if(Tile == 0)
							continue;
						const int Tx = Tile % 16;
						const int Ty = Tile / 16;
						const float U0 = Tx * KTile + KInset;
						const float V0 = Ty * KTile + KInset;
						const float U1 = U0 + KTile - KInset * 2;
						const float V1 = V0 + KTile - KInset * 2;
						Graphics()->QuadsSetSubset(U0, V0, U1, V1);
						IGraphics::CQuadItem Q(OffX + c * TileSize, OffY + r * TileSize, TileSize, TileSize);
						Graphics()->QuadsDrawTL(&Q, 1);
					}
				}
				Graphics()->QuadsEnd();
				Graphics()->WrapNormal();
			}
		}
		else if(pItem->m_RenderTexture.IsValid())
		{
			Graphics()->WrapClamp();
			Graphics()->TextureSet(pItem->m_RenderTexture);
			Graphics()->QuadsBegin();
			Graphics()->SetColor(1, 1, 1, 1);
			const vec2 PreviewOffset = AssetPreviewOpticalOffset(s_CurCustomTab, pItem->m_aName, PreviewWidth, PreviewHeight);
			IGraphics::CQuadItem QuadItem(TextureRect.x + (TextureRect.w - PreviewWidth) / 2.0f + PreviewOffset.x, TextureRect.y + (TextureRect.h - PreviewHeight) / 2.0f + PreviewOffset.y, PreviewWidth, PreviewHeight);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsEnd();
			Graphics()->WrapNormal();
		}
		} // end else (non-audio rendering)
		if(CardIsDragSource)
		{
			CardRect.Draw(AccentSelected.WithAlpha(0.82f), IGraphics::CORNER_ALL, 5.0f);
			CUIRect DragCardInner = CardRect;
			DragCardInner.Margin(1.5f, &DragCardInner);
			DragCardInner.Draw(ColorRGBA(0.025f, 0.055f, 0.08f, 0.70f), IGraphics::CORNER_ALL, 4.0f);
		}
		// Labels are intentionally compact; the card tooltip always exposes the
		// complete pack name without changing grid geometry for long names.
		GameClient()->m_Tooltips.DoToolTip(&pItem->m_CardButtonId, &CardRect, pItem->m_aName);

		if(CanDelete)
		{
			TextRender()->TextColor(ColorRGBA(0.95f, 0.42f, 0.42f, 0.95f));
			const ColorRGBA DeleteFill = Ui()->MouseHovered(&DeleteButton) ? ColorRGBA(0.28f, 0.11f, 0.14f, 0.98f) : ColorRGBA(0.20f, 0.08f, 0.11f, 0.96f);
			DeleteVisual.Draw(ColorRGBA(0.60f, 0.20f, 0.24f, 0.74f), IGraphics::CORNER_ALL, 4.0f);
			CUIRect DeleteInner = DeleteVisual;
			DeleteInner.Margin(1.0f, &DeleteInner);
			DeleteInner.Draw(DeleteFill, IGraphics::CORNER_ALL, 3.0f);
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			Ui()->DoLabel(&DeleteVisual, FontIcon::TRASH, 11.0f * CARD_ACTION_SCALE, TEXTALIGN_MC);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			// The card already owns the listbox button. A second overlapping
			// DoButtonLogic corrupts the UI active-button bookkeeping, so compact
			// card actions consume the exact mouse event without registering a
			// second active item over the same rectangle.
			if(Ui()->MouseHovered(&DeleteButton) && Ui()->MouseButtonClicked(0))
			{
				SkipSelectionBecauseDelete = true;
				str_copy(m_aDeleteAssetName, pItem->m_aName);
				m_DeleteAssetTab = s_CurCustomTab;
				char aBuf[128 + sizeof(pItem->m_aName)];
				str_format(aBuf, sizeof(aBuf), Localize("Are you sure that you want to delete '%s'?"), pItem->m_aName);
				PopupConfirm(Localize("Delete asset"), aBuf, Localize("Yes"), Localize("No"), &CMenus::PopupConfirmDeleteAsset);
			}
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			GameClient()->m_Tooltips.DoToolTip(&pItem->m_DeleteButtonId, &DeleteButton, Localize("Delete this asset from your assets directory."));
		}

		// Render STAR through the same plain icon-font path as the visible folder
		// star. The generic compact icon helper used metrics that collapsed this
		// glyph into an empty-looking square on the actual Release renderer.
		const bool StarHovered = Ui()->MouseHovered(&FavoriteButton);
		const bool StarPressed = StarHovered && Ui()->MouseButton(0);
		const ColorRGBA FavoriteFill = Favorite ? AccentColor(AccentLightness, 0.72f) :
			(StarPressed ? AccentPressed : (StarHovered ? NeutralButtonHover : NeutralButton));
		const ColorRGBA FavoriteBorder = Favorite ? AccentSelected :
			(StarHovered || StarPressed ? AccentSoft : NeutralBorderSoft);
		FavoriteVisual.Draw(FavoriteBorder, IGraphics::CORNER_ALL, 4.0f);
		CUIRect FavoriteInner = FavoriteVisual;
		FavoriteInner.Margin(1.0f, &FavoriteInner);
		FavoriteInner.Draw(FavoriteFill, IGraphics::CORNER_ALL, 3.0f);
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
		TextRender()->TextColor(AccentHover.WithAlpha(1.0f));
		CUIRect StarVisual = FavoriteVisual;
		// Render the natural STAR bitmap instead of the compressed NO_OVERSIZE
		// variant. With the x/y bearings disabled its actual glyph bounds equal
		// the measured advance box, so TEXTALIGN_MC centers the visible bitmap in
		// the unchanged 20x20 square on both axes.
		const float FavoriteCenterX = FavoriteVisual.x + FavoriteVisual.w * 0.5f;
		const float FavoriteCenterY = FavoriteVisual.y + FavoriteVisual.h * 0.5f;
		StarVisual.x = FavoriteCenterX - StarVisual.w * 0.5f;
		StarVisual.y = FavoriteCenterY - StarVisual.h * 0.5f;
		SLabelProperties StarProps;
		StarProps.m_MaxWidth = StarVisual.w;
		Ui()->DoLabel(&StarVisual, FontIcon::STAR, 14.0f * CARD_ACTION_SCALE, TEXTALIGN_MC, StarProps);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		TextRender()->SetRenderFlags(0);
		if(StarHovered && Ui()->MouseButtonClicked(0))
		{
			SkipSelectionBecauseDelete = true;
			SetAssetOrganizerFavorite(s_CurCustomTab, pItem->m_aName, !Favorite);
			gs_aInitCustomList[s_CurCustomTab] = true;
		}
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		GameClient()->m_Tooltips.DoToolTip(&pItem->m_FavoriteButtonId, &FavoriteButton,
			Favorite ? Localize("Click to remove this item from your favorites.") : Localize("Click to add this item to your favorites."));
	}
	if(gs_AssetOrganizerDrag.m_Dragging && gs_AssetOrganizerDrag.m_Tab == s_CurCustomTab &&
		gs_AssetOrganizerDrag.m_aTargetFolderId[0] == '\0' && gs_AssetOrganizerDrag.m_aTargetAssetId[0] == '\0' &&
		!gs_AssetOrganizerDrag.m_CreateFolderDropTarget && !AssetOrganizer::IsFavoritesFolder(AssetOrganizer::CurrentFolder(s_CurCustomTab)) && Ui()->MouseHovered(&CustomList))
	{
		// Empty grid space is a useful, unambiguous "append to this page" drop
		// target. It lets users place an item after the final card without a
		// separate Position control.
		gs_AssetOrganizerDrag.m_CurrentPageDropTarget = true;
	}

	bool DragFinished = false;
	if(gs_AssetOrganizerDrag.m_pSourceItem != nullptr && gs_AssetOrganizerDrag.m_Tab == s_CurCustomTab)
	{
		if(Ui()->MouseButton(0))
		{
			if(!gs_AssetOrganizerDrag.m_Dragging && distance(Ui()->MousePos(), gs_AssetOrganizerDrag.m_PressPos) >= 6.0f)
				gs_AssetOrganizerDrag.m_Dragging = true;
			if(gs_AssetOrganizerDrag.m_Dragging)
			{
				const vec2 DesiredPos = Ui()->MousePos() - gs_AssetOrganizerDrag.m_DragOffset;
				// A short visual lag gives the lifted-card feel of desktop icon drag
				// without allocating or running any animation while no drag is active.
				gs_AssetOrganizerDrag.m_GhostPos += (DesiredPos - gs_AssetOrganizerDrag.m_GhostPos) * 0.48f;
			}
		}
		else
		{
			DragFinished = gs_AssetOrganizerDrag.m_Dragging;
			if(DragFinished)
			{
				const char *pSourceId = gs_AssetOrganizerDrag.m_pSourceItem->m_aName;
				if(gs_AssetOrganizerDrag.m_CreateFolderDropTarget)
				{
					std::string CreatedId;
					const std::string BaseName = TCLocalize("New folder", "AMF Client");
					for(int Number = 1; Number < 1000 && CreatedId.empty(); ++Number)
					{
						const std::string Name = Number == 1 ? BaseName : BaseName + " " + std::to_string(Number);
						AssetOrganizer::CreateFolder(s_CurCustomTab, Name.c_str(), &CreatedId);
					}
					if(!CreatedId.empty())
					{
						AssetOrganizer::MoveItem(s_CurCustomTab, pSourceId, CreatedId.c_str(), 0, 0);
						AssetOrganizer::SetCurrentFolder(s_CurCustomTab, CreatedId.c_str());
					}
				}
				else if(gs_AssetOrganizerDrag.m_aTargetFolderId[0] != '\0')
				{
					AssetOrganizer::MoveItem(s_CurCustomTab, pSourceId, gs_AssetOrganizerDrag.m_aTargetFolderId, 0, AssetOrganizer::ItemCount(s_CurCustomTab, gs_AssetOrganizerDrag.m_aTargetFolderId, 0));
				}
				else if(gs_AssetOrganizerDrag.m_aTargetAssetId[0] != '\0')
				{
					const char *pTargetId = gs_AssetOrganizerDrag.m_aTargetAssetId;
					AssetOrganizer::SwapItems(s_CurCustomTab, pSourceId, pTargetId);
				}
				else if(gs_AssetOrganizerDrag.m_CurrentPageDropTarget)
				{
					const char *pFolderId = AssetOrganizer::CurrentFolder(s_CurCustomTab);
					const int Page = AssetOrganizer::CurrentPage(s_CurCustomTab);
					AssetOrganizer::MoveItem(s_CurCustomTab, pSourceId, pFolderId, Page, AssetOrganizer::ItemCount(s_CurCustomTab, pFolderId, Page));
				}
				gs_AssetOrganizerSettle.m_Active = true;
				gs_AssetOrganizerSettle.m_Tab = s_CurCustomTab;
				gs_AssetOrganizerSettle.m_Start = time_get_nanoseconds();
				gs_AssetOrganizerSettle.m_From = {gs_AssetOrganizerDrag.m_GhostPos.x - 2.0f, gs_AssetOrganizerDrag.m_GhostPos.y - 2.0f, gs_AssetOrganizerDrag.m_SourceRect.w + 4.0f, gs_AssetOrganizerDrag.m_SourceRect.h + 4.0f};
				gs_AssetOrganizerSettle.m_To = gs_AssetOrganizerSettle.m_From;
				if(gs_AssetOrganizerDrag.m_TargetRectValid)
				{
					gs_AssetOrganizerSettle.m_To = gs_AssetOrganizerDrag.m_TargetRect;
					if(gs_AssetOrganizerDrag.m_aTargetAssetId[0] == '\0')
					{
						const vec2 Center(gs_AssetOrganizerSettle.m_To.x + gs_AssetOrganizerSettle.m_To.w / 2.0f, gs_AssetOrganizerSettle.m_To.y + gs_AssetOrganizerSettle.m_To.h / 2.0f);
						gs_AssetOrganizerSettle.m_To.w = gs_AssetOrganizerDrag.m_SourceRect.w * 0.24f;
						gs_AssetOrganizerSettle.m_To.h = gs_AssetOrganizerDrag.m_SourceRect.h * 0.24f;
						gs_AssetOrganizerSettle.m_To.x = Center.x - gs_AssetOrganizerSettle.m_To.w / 2.0f;
						gs_AssetOrganizerSettle.m_To.y = Center.y - gs_AssetOrganizerSettle.m_To.h / 2.0f;
					}
				}
				gs_AssetOrganizerSettle.m_Texture = gs_AssetOrganizerDrag.m_pSourceItem->m_RenderTexture;
				gs_AssetOrganizerSettle.m_PreviewAspect = gs_AssetOrganizerDrag.m_pSourceItem->m_PreviewAspect;
				str_copy(gs_AssetOrganizerSettle.m_aName, gs_AssetOrganizerDrag.m_pSourceItem->m_aName, sizeof(gs_AssetOrganizerSettle.m_aName));
				gs_aInitCustomList[s_CurCustomTab] = true;
			}
			gs_AssetOrganizerDrag.Reset();
		}
	}

	const int NewSelected = s_ListBox.DoEnd();
	auto RenderFloatingCard = [&](const CUIRect &Rect, const char *pName, IGraphics::CTextureHandle Texture, float Aspect, int Tab, float Alpha) {
		CUIRect Shadow = Rect;
		Shadow.x += 3.0f;
		Shadow.y += 4.0f;
		Shadow.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.34f * Alpha), IGraphics::CORNER_ALL, 6.0f);
		Rect.Draw(AccentSelected.WithMultipliedAlpha(Alpha), IGraphics::CORNER_ALL, 6.0f);
		CUIRect Inner = Rect;
		Inner.Margin(1.5f, &Inner);
		Inner.Draw(ColorRGBA(0.045f, 0.065f, 0.090f, 0.97f * Alpha), IGraphics::CORNER_ALL, 5.0f);
		Inner.Margin(5.0f, &Inner);
		CUIRect Title, Preview = Inner;
		Inner.HSplitTop(20.0f, &Title, nullptr);
		Title.VSplitLeft(20.0f, nullptr, &Title);
		SLabelProperties Props;
		Props.m_MaxWidth = std::max(0.0f, Title.w - 3.0f);
		Props.m_EllipsisAtEnd = true;
		TextRender()->TextColor(ColorRGBA(0.93f, 0.96f, 1.0f, Alpha));
		Ui()->DoLabel(&Title, pName, 9.5f, TEXTALIGN_MC, Props);
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		CUIRect DragStar = {Inner.x, Inner.y - 1.0f, 18.0f, 18.0f};
		Ui()->DoLabel(&DragStar, FontIcon::STAR, 12.0f, TEXTALIGN_MC);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		if(Tab == ASSETS_TAB_AUDIO || !Texture.IsValid())
		{
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
			TextRender()->TextColor(AudioNoteColor.WithMultipliedAlpha(Alpha));
			const float MusicFontSize = std::min(28.0f, Preview.h * 0.55f);
			CUIRect MusicVisual = Preview;
			const vec2 MusicOffset = AudioPreviewOpticalOffset(MusicFontSize);
			MusicVisual.x += MusicOffset.x;
			MusicVisual.y += MusicOffset.y;
			Ui()->DoLabel(&MusicVisual, FontIcon::MUSIC, MusicFontSize, TEXTALIGN_MC);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			TextRender()->SetRenderFlags(0);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		}
		else
		{
			Aspect = std::clamp(Aspect, 0.1f, 10.0f);
			float Width = std::max(1.0f, Preview.w - 8.0f);
			float Height = Width / Aspect;
			if(Height > Preview.h - 6.0f)
			{
				Height = std::max(1.0f, Preview.h - 6.0f);
				Width = Height * Aspect;
			}
			if(Tab == ASSETS_TAB_CURSOR || Tab == ASSETS_TAB_ARROW)
			{
				Width *= 0.8f;
				Height *= 0.8f;
			}
			const vec2 PreviewOffset = AssetPreviewOpticalOffset(Tab, pName, Width, Height);
			Graphics()->WrapClamp();
			Graphics()->TextureSet(Texture);
			Graphics()->QuadsBegin();
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.92f * Alpha);
			IGraphics::CQuadItem Quad(Preview.x + (Preview.w - Width) / 2.0f + PreviewOffset.x, Preview.y + (Preview.h - Height) / 2.0f + PreviewOffset.y, Width, Height);
			Graphics()->QuadsDrawTL(&Quad, 1);
			Graphics()->QuadsEnd();
			Graphics()->WrapNormal();
		}
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	};
	if(gs_AssetOrganizerDrag.m_Dragging && gs_AssetOrganizerDrag.m_Tab == s_CurCustomTab && gs_AssetOrganizerDrag.m_pSourceItem != nullptr)
	{
		const SCustomItem *pDragItem = gs_AssetOrganizerDrag.m_pSourceItem;
		CUIRect DragCard = {gs_AssetOrganizerDrag.m_GhostPos.x - 2.0f, gs_AssetOrganizerDrag.m_GhostPos.y - 2.0f, gs_AssetOrganizerDrag.m_SourceRect.w + 4.0f, gs_AssetOrganizerDrag.m_SourceRect.h + 4.0f};
		RenderFloatingCard(DragCard, pDragItem->m_aName, pDragItem->m_RenderTexture, pDragItem->m_PreviewAspect, s_CurCustomTab, 0.94f);
	}
	if(gs_AssetOrganizerSettle.m_Active)
	{
		const float Progress = std::clamp(std::chrono::duration<float>(time_get_nanoseconds() - gs_AssetOrganizerSettle.m_Start).count() / 0.16f, 0.0f, 1.0f);
		const float Ease = 1.0f - (1.0f - Progress) * (1.0f - Progress) * (1.0f - Progress);
		CUIRect Rect = {
			gs_AssetOrganizerSettle.m_From.x + (gs_AssetOrganizerSettle.m_To.x - gs_AssetOrganizerSettle.m_From.x) * Ease,
			gs_AssetOrganizerSettle.m_From.y + (gs_AssetOrganizerSettle.m_To.y - gs_AssetOrganizerSettle.m_From.y) * Ease,
			gs_AssetOrganizerSettle.m_From.w + (gs_AssetOrganizerSettle.m_To.w - gs_AssetOrganizerSettle.m_From.w) * Ease,
			gs_AssetOrganizerSettle.m_From.h + (gs_AssetOrganizerSettle.m_To.h - gs_AssetOrganizerSettle.m_From.h) * Ease};
		RenderFloatingCard(Rect, gs_AssetOrganizerSettle.m_aName, gs_AssetOrganizerSettle.m_Texture, gs_AssetOrganizerSettle.m_PreviewAspect, gs_AssetOrganizerSettle.m_Tab, 1.0f - Progress * 0.75f);
		if(Progress >= 1.0f)
			gs_AssetOrganizerSettle.m_Active = false;
	}
	bool AssetSelectionChanged = false;
	if(OldSelected != NewSelected && NewSelected >= 0 && !SkipSelectionBecauseDelete && !DragFinished)
	{
		const SCustomItem *pSelectedItem = GetCustomItem(s_CurCustomTab, NewSelected);
		if(pSelectedItem != nullptr && pSelectedItem->m_aName[0] != '\0')
		{
			if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
			{
				str_copy(g_Config.m_ClAssetsEntities, pSelectedItem->m_aName);
				GameClient()->m_MapImages.ChangeEntitiesPath(pSelectedItem->m_aName);
			}
			else if(s_CurCustomTab == ASSETS_TAB_GAME)
			{
				str_copy(g_Config.m_ClAssetGame, pSelectedItem->m_aName);
				GameClient()->LoadGameSkin(g_Config.m_ClAssetGame);
			}
			else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
			{
				str_copy(g_Config.m_ClAssetEmoticons, pSelectedItem->m_aName);
				GameClient()->LoadEmoticonsSkin(g_Config.m_ClAssetEmoticons);
			}
			else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
			{
				str_copy(g_Config.m_ClAssetParticles, pSelectedItem->m_aName);
				GameClient()->LoadParticlesSkin(g_Config.m_ClAssetParticles);
			}
			else if(s_CurCustomTab == ASSETS_TAB_HUD)
			{
				str_copy(g_Config.m_ClAssetHud, pSelectedItem->m_aName);
				GameClient()->LoadHudSkin(g_Config.m_ClAssetHud);
			}
			else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
			{
				str_copy(g_Config.m_ClAssetExtras, pSelectedItem->m_aName);
				GameClient()->LoadExtrasSkin(g_Config.m_ClAssetExtras);
			}
			else if(s_CurCustomTab == ASSETS_TAB_CURSOR)
			{
				str_copy(g_Config.m_ClAssetCursor, pSelectedItem->m_aName);
				GameClient()->LoadCursorAsset(g_Config.m_ClAssetCursor);
			}
			else if(s_CurCustomTab == ASSETS_TAB_ARROW)
			{
				str_copy(g_Config.m_ClAssetArrow, pSelectedItem->m_aName);
				GameClient()->LoadArrowAsset(g_Config.m_ClAssetArrow);
			}
			else if(s_CurCustomTab == ASSETS_TAB_AUDIO)
			{
				str_copy(g_Config.m_SndPack, pSelectedItem->m_aName);
				GameClient()->m_Sounds.Clear();
			}
			AssetSelectionChanged = true;
		}
	}
	if(AssetSelectionChanged)
		ConfigManager()->SaveDomain(ConfigDomain::DDNET);

	// Quick search
	QuickSearch.Margin(4.0f, &QuickSearch);
	QuickSearch.VSplitLeft(std::min(260.0f, QuickSearch.w * 0.42f), &QuickSearch, &DirectoryButton);
	QuickSearch.HSplitTop(5.0f, nullptr, &QuickSearch);
	if(Ui()->DoEditBox_Search(&s_aFilterInputs[s_CurCustomTab], &QuickSearch, 14.0f, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive()))
	{
		gs_aInitCustomList[s_CurCustomTab] = true;
	}
	// DoEditBox_Search already owns the input's visual state. Do not add a second
	// outline around the combined search/icon/clear control.

	DirectoryButton.HSplitTop(5.0f, nullptr, &DirectoryButton);

	if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
	{
		CUIRect ToggleRect;
		DirectoryButton.VSplitLeft(10.0f, nullptr, &DirectoryButton);
		DirectoryButton.VSplitLeft(25.0f, &ToggleRect, &DirectoryButton);
		DirectoryButton.VSplitLeft(5.0f, nullptr, &DirectoryButton);
		static CButtonContainer s_EntityPreviewToggleId;
		if(DoOrganizerIconButton(&s_EntityPreviewToggleId, s_EntityGamePreview ? FontIcon::EYE : FontIcon::IMAGE, &ToggleRect, true, s_EntityGamePreview, 12.0f, vec2(0.0f, -0.5f)))
			s_EntityGamePreview = !s_EntityGamePreview;
		GameClient()->m_Tooltips.DoToolTip(&s_EntityPreviewToggleId, &ToggleRect, Localize("Toggle between game scene preview and raw texture"));
	}

	// Right cluster: Assets directory | gap | Reload
	constexpr float AssetsDirectoryW = 140.0f;
	constexpr float ReloadW = 25.0f;
	constexpr float RightGap = 10.0f;
	const float RightClusterW = AssetsDirectoryW + RightGap + ReloadW;

	CUIRect RightCluster;
	DirectoryButton.VSplitRight(RightClusterW, nullptr, &RightCluster);
	RightCluster.VSplitRight(ReloadW, &RightCluster, &ReloadButton);
	RightCluster.VSplitRight(RightGap, &RightCluster, nullptr);
	RightCluster.VSplitRight(AssetsDirectoryW, &RightCluster, &DirectoryButton);

	static CButtonContainer s_AssetsDirId;
	if(DoOrganizerButton(&s_AssetsDirId, Localize("Assets directory"), &DirectoryButton))
	{
		char aBuf[IO_MAX_PATH_LENGTH];
		char aBufFull[IO_MAX_PATH_LENGTH + 7];
		if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
			str_copy(aBufFull, "assets/entities");
		else if(s_CurCustomTab == ASSETS_TAB_GAME)
			str_copy(aBufFull, "assets/game");
		else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
			str_copy(aBufFull, "assets/emoticons");
		else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
			str_copy(aBufFull, "assets/particles");
		else if(s_CurCustomTab == ASSETS_TAB_HUD)
			str_copy(aBufFull, "assets/hud");
		else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
			str_copy(aBufFull, "assets/extras");
		else if(s_CurCustomTab == ASSETS_TAB_CURSOR)
			str_copy(aBufFull, "assets/cursor");
		else if(s_CurCustomTab == ASSETS_TAB_ARROW)
			str_copy(aBufFull, "assets/arrow");
		else if(s_CurCustomTab == ASSETS_TAB_AUDIO)
			str_copy(aBufFull, "assets/audio");
		else
			str_copy(aBufFull, "assets");
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, aBufFull, aBuf, sizeof(aBuf));
		Storage()->CreateFolder("assets", IStorage::TYPE_SAVE);
		Storage()->CreateFolder(aBufFull, IStorage::TYPE_SAVE);
		Client()->ViewFile(aBuf);
	}
	GameClient()->m_Tooltips.DoToolTip(&s_AssetsDirId, &DirectoryButton, Localize("Open the directory to add custom assets"));

	static CButtonContainer s_AssetsReloadBtnId;
	if(DoOrganizerIconButton(&s_AssetsReloadBtnId, FontIcon::ARROW_ROTATE_RIGHT, &ReloadButton) || Input()->KeyPress(KEY_F5) || (Input()->KeyPress(KEY_R) && Input()->ModifierIsPressed()))
	{
		ClearCustomItems(s_CurCustomTab);
	}

	// Keep the control legend in the same localized UI path as the rest of the
	// organizer. It is passive: no hidden input region is placed over cards.
	InputHints.Margin(4.0f, &InputHints);
	InputHints.Draw(NeutralBorderSoft, IGraphics::CORNER_ALL, 4.0f);
	CUIRect InputHintsInner = InputHints;
	InputHintsInner.Margin(1.0f, &InputHintsInner);
	InputHintsInner.Draw(ColorRGBA(0.035f, 0.050f, 0.070f, 0.82f), IGraphics::CORNER_ALL, 3.0f);
	constexpr int NUM_INPUT_HINTS = 5;
	const char *apInputHints[NUM_INPUT_HINTS] = {
		TCLocalize("LMB - select", "AMF Client"),
		TCLocalize("RMB - actions", "AMF Client"),
		TCLocalize("Drag - move", "AMF Client"),
		TCLocalize("Mouse wheel - scroll", "AMF Client"),
		TCLocalize("Esc - back", "AMF Client")};
	CUIRect HintRow = InputHints;
	for(int HintIndex = 0; HintIndex < NUM_INPUT_HINTS; ++HintIndex)
	{
		CUIRect Hint;
		HintRow.VSplitLeft(HintRow.w / (float)(NUM_INPUT_HINTS - HintIndex), &Hint, &HintRow);
		SLabelProperties HintProps;
		HintProps.m_MaxWidth = std::max(0.0f, Hint.w - 6.0f);
		HintProps.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Hint, apInputHints[HintIndex], 8.5f, TEXTALIGN_MC, HintProps);
	}
}

void CMenus::ConchainAssetsEntities(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetsEntities) != 0)
		{
			pThis->GameClient()->m_MapImages.ChangeEntitiesPath(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetGame(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetGame) != 0)
		{
			pThis->GameClient()->LoadGameSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetParticles(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetParticles) != 0)
		{
			pThis->GameClient()->LoadParticlesSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetEmoticons(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetEmoticons) != 0)
		{
			pThis->GameClient()->LoadEmoticonsSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetHud(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetHud) != 0)
		{
			pThis->GameClient()->LoadHudSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetExtras(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetExtras) != 0)
		{
			pThis->GameClient()->LoadExtrasSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetCursor(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetCursor) != 0)
		{
			pThis->GameClient()->LoadCursorAsset(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetArrow(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetArrow) != 0)
		{
			pThis->GameClient()->LoadArrowAsset(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainSndPack(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	char aOldSndPack[64];
	str_copy(aOldSndPack, g_Config.m_SndPack, sizeof(aOldSndPack));
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments() == 1)
	{
		if(str_comp(aOldSndPack, g_Config.m_SndPack) != 0)
			pThis->GameClient()->m_Sounds.Clear();
	}
}



/* Settings section: credits */
#include "menus.h"

#include <engine/graphics.h>
#include <engine/textrender.h>

#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>

void CMenus::RenderSettingsCredits(CUIRect MainView)
{
	static constexpr const char *const CREDITS =
		"\n"
		"Help and code by eeeee, HMH, east, CookieMichal, Learath2, "
		"Savander, laxa, Tobii, BeaR, Wohoo, nuborn, timakro, Shiki, "
		"trml, Soreu, hi_leute_gll, Lady Saavik, Chairn, heinrich5991, "
		"swick, oy, necropotame, Ryozuki, Redix, d3fault, marcelherd, "
		"BannZay, ACTom, SiuFuWong, PathosEthosLogos, TsFreddie, "
		"Jupeyy, noby, ChillerDragon, ZombieToad, weez15, z6zzz, "
		"Piepow, QingGo, RafaelFF, sctt, jao, daverck, fokkonaut, "
		"Bojidar, FallenKN, ardadem, archimede67, sirius1242, Aerll, "
		"trafilaw, Zwelf, Patiga, Konsti, ElXreno, MikiGamer, "
		"Fireball, Banana090, axblk, yangfl, Kaffeine, Zodiac, "
		"c0d3d3v, GiuCcc, Ravie, Robyt3, simpygirl, Tater, Cellegen, "
		"srdante, Nouaa, Voxel, luk51, Vy0x2, Avolicious, louis, "
		"Marmare314, hus3h, ArijanJ, tarunsamanta2k20, Possseidon, "
		"+KZ, Teero, furo, dobrykafe, Moiman, JSaurusRex, "
		"Steinchen, ewancg, gerdoe-jr, melon, KebsCS, bencie, "
		"DynamoFox, MilkeeyCat, iMilchshake, SchrodingerZhu, "
		"catseyenebulous, Rei-Tw, Matodor, Emilcha, art0007i, SollyBunny, "
		"0xfaulty, AssassinTee, Pioooooo, ASKLL-STAR, K1nop1c0, "
		"Bamcane, qxdFox, ZerolAcqua, swarfeya, Scrumplex, 12944qwerty, "
		"Pointer31, ProfSapphire, 0xpixty, GlimmeR, horoni & others\n"
		"\n"
		"Based on DDRace by the DDRace developers,";
	const float FontSize = 16.0f;

	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 3.0f * FontSize;
	s_ScrollRegion.Begin(&MainView, &ScrollParams);

	const auto &&RenderCreditsLink = [&](const void *pLinkId, const char *pPrefix, const char *pLink, const char *pSuffix, const char *pUrl) {
		const float LineHeight = TextRender()->TextBoundingBox(FontSize, pLink).m_H;
		const float PrefixWidth = TextRender()->TextWidth(FontSize, pPrefix);
		const bool PrefixOwnLine = PrefixWidth + TextRender()->TextWidth(FontSize, pLink) + TextRender()->TextWidth(FontSize, pSuffix) > MainView.w;

		CUIRect Line, Prefix, LinkRect;
		MainView.HSplitTop(LineHeight, &Line, &MainView);
		s_ScrollRegion.AddRect(Line);
		if(PrefixOwnLine)
		{
			Ui()->DoLabel(&Line, pPrefix, FontSize, TEXTALIGN_ML);
			MainView.HSplitTop(LineHeight, &Line, &MainView);
			s_ScrollRegion.AddRect(Line);
		}
		else
		{
			Line.VSplitLeft(PrefixWidth, &Prefix, &Line);
			Ui()->DoLabel(&Prefix, pPrefix, FontSize, TEXTALIGN_ML);
		}

		Line.VSplitLeft(TextRender()->TextWidth(FontSize, pLink), &LinkRect, &Line);
		const ColorRGBA LinkColor = Ui()->HotItem() == pLinkId ? ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f) : ColorRGBA(0.4f, 0.7f, 1.0f, 1.0f);
		TextRender()->TextColor(LinkColor);
		Ui()->DoLabel(&LinkRect, pLink, FontSize, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		CUIRect Underline;
		LinkRect.HSplitBottom(1.0f, nullptr, &Underline);
		Underline.Draw(LinkColor, IGraphics::CORNER_NONE, 0.0f);

		if(Ui()->DoButtonLogic(pLinkId, 0, &LinkRect, BUTTONFLAG_LEFT))
		{
			Client()->ViewLink(pUrl);
		}

		Ui()->DoLabel(&Line, pSuffix, FontSize, TEXTALIGN_ML);
	};

	static char s_StaffLinkId;
	RenderCreditsLink(&s_StaffLinkId, "DDNet is run by the ", "DDNet staff", ".", "https://ddnet.org/staff");
	static char s_MapsLinkId;
	RenderCreditsLink(&s_MapsLinkId, "", "Great maps", " and many ideas from the community.", "https://ddnet.org/releases/");

	CTextCursor Cursor;
	Cursor.m_FontSize = FontSize;
	Cursor.m_LineWidth = MainView.w;

	const unsigned OldRenderFlags = TextRender()->GetRenderFlags();
	TextRender()->SetRenderFlags(OldRenderFlags | TEXT_RENDER_FLAG_ONE_TIME_USE);
	STextContainerIndex CreditsTextContainer;
	TextRender()->CreateTextContainer(CreditsTextContainer, &Cursor, CREDITS);
	TextRender()->SetRenderFlags(OldRenderFlags);
	if(CreditsTextContainer.Valid())
	{
		CUIRect CreditsLabel;
		MainView.HSplitTop(TextRender()->GetBoundingBoxTextContainer(CreditsTextContainer).m_H, &CreditsLabel, &MainView);
		s_ScrollRegion.AddRect(CreditsLabel);
		TextRender()->RenderTextContainer(CreditsTextContainer, TextRender()->DefaultTextColor(), TextRender()->DefaultTextOutlineColor(), CreditsLabel.x, CreditsLabel.y);
		TextRender()->DeleteTextContainer(CreditsTextContainer);
	}

	static char s_TeeworldsLinkId;
	RenderCreditsLink(&s_TeeworldsLinkId, "which is a mod of ", "Teeworlds", " by the Teeworlds developers.", "https://teeworlds.com/");

	s_ScrollRegion.End();
}





void CMenus::RenderSettings(CUIRect MainView)
{
	// render background
	CUIRect Button, TabBar, RestartBar;
	MainView.VSplitRight(120.0f, &MainView, &TabBar);
	// The frame continues directly into the left side of the final Configs tab.
	MainView.Draw(ms_ColorTabbarActive, IGraphics::CORNER_BL, 10.0f);
	MainView.Margin(20.0f, &MainView);

	const bool NeedRestart = m_NeedRestartGraphics || m_NeedRestartSound;
	if(NeedRestart)
	{
		MainView.HSplitBottom(20.0f, &MainView, &RestartBar);
		MainView.HSplitBottom(10.0f, &MainView, nullptr);
	}

	TabBar.HSplitTop(50.0f, &Button, &TabBar);
	Button.Draw(ms_ColorTabbarActive, IGraphics::CORNER_BR, 10.0f);

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

	for(int i = 0; i < SETTINGS_LENGTH; i++)
	{
		float TabGap = 10.0f;
		if(i == SETTINGS_CONFIGS && TabBar.h > TabGap + 26.0f)
			TabGap = TabBar.h - 26.0f;
		TabBar.HSplitTop(TabGap, nullptr, &TabBar);
		TabBar.HSplitTop(26.0f, &Button, &TabBar);
		if(DoButton_MenuTab(&s_aTabButtons[i], apTabs[i], g_Config.m_UiSettingsPage == i, &Button, IGraphics::CORNER_R, &m_aAnimatorsSettingsTab[i]))
			g_Config.m_UiSettingsPage = i;
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
		Ui()->DoLabel(&RestartWarning, Localize("You must restart the game for all settings to take effect."), 14.0f, TEXTALIGN_ML);

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
			IGraphics::CColorVertex aColorVertices[] = {
				IGraphics::CColorVertex(0, 1, 0, 0, 1),
				IGraphics::CColorVertex(1, 1, 1, 0, 1),
				IGraphics::CColorVertex(2, 1, 0, 0, 1),
				IGraphics::CColorVertex(3, 1, 1, 0, 1)};
			Graphics()->SetColorVertex(aColorVertices, std::size(aColorVertices));

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
			IGraphics::CColorVertex aColorVertices[] = {
				IGraphics::CColorVertex(0, 1, 1, 0, 1),
				IGraphics::CColorVertex(1, 0, 1, 0, 1),
				IGraphics::CColorVertex(2, 1, 1, 0, 1),
				IGraphics::CColorVertex(3, 0, 1, 0, 1)};
			Graphics()->SetColorVertex(aColorVertices, std::size(aColorVertices));

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
			IGraphics::CColorVertex aColorVertices[] = {
				IGraphics::CColorVertex(0, 0, 1, 0, 1),
				IGraphics::CColorVertex(1, 0, 1, 1, 1),
				IGraphics::CColorVertex(2, 0, 1, 0, 1),
				IGraphics::CColorVertex(3, 0, 1, 1, 1)};
			Graphics()->SetColorVertex(aColorVertices, std::size(aColorVertices));

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
			IGraphics::CColorVertex aColorVertices[] = {
				IGraphics::CColorVertex(0, 0, 1, 1, 1),
				IGraphics::CColorVertex(1, 0, 0, 1, 1),
				IGraphics::CColorVertex(2, 0, 1, 1, 1),
				IGraphics::CColorVertex(3, 0, 0, 1, 1)};
			Graphics()->SetColorVertex(aColorVertices, std::size(aColorVertices));

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
			IGraphics::CColorVertex aColorVertices[] = {
				IGraphics::CColorVertex(0, 0, 0, 1, 1),
				IGraphics::CColorVertex(1, 1, 0, 1, 1),
				IGraphics::CColorVertex(2, 0, 0, 1, 1),
				IGraphics::CColorVertex(3, 1, 0, 1, 1)};
			Graphics()->SetColorVertex(aColorVertices, std::size(aColorVertices));

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
			IGraphics::CColorVertex aColorVertices[] = {
				IGraphics::CColorVertex(0, 1, 0, 1, 1),
				IGraphics::CColorVertex(1, 1, 0, 0, 1),
				IGraphics::CColorVertex(2, 1, 0, 1, 1),
				IGraphics::CColorVertex(3, 1, 0, 0, 1)};
			Graphics()->SetColorVertex(aColorVertices, std::size(aColorVertices));

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
