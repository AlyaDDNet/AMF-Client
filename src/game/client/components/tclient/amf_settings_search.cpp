#include "amf_settings_search.h"
#include "amf_settings_search_match.h"

#include <base/str.h>

#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/input.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/menus.h>
#include <game/client/components/tclient/amf_features/ui_theme/style.h>
#include <game/client/gameclient.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <vector>

namespace AmfSettingsSearch
{
namespace
{
constexpr float HIGHLIGHT_DURATION = 1.6f;
constexpr ColorRGBA HIGHLIGHT_COLOR(1.0f, 0.28f, 0.28f, 1.0f);

struct SEntry
{
	const char *m_pName;
	const char *m_pAliases;
	const char *m_pSection;
	int m_SettingsPage;
	int m_Subpage;
	const void *m_pId;
};

#define AMF_ENTRY(Name, Aliases, Section, Page, Subpage, Id) {Name, Aliases, Section, Page, Subpage, Id}
const SEntry gs_aCatalog[] = {
	AMF_ENTRY("General", "AMF, Основное, Загальне", "AMF Client", CMenus::SETTINGS_AMF_CLIENT, 0, &g_Config.m_AmfInputMode),
	AMF_ENTRY("Visual", "AMF, Визуальное, Візуальне", "AMF Client", CMenus::SETTINGS_AMF_CLIENT, 1, &g_Config.m_AmfSmoothHud),
	AMF_ENTRY("Information", "AMF, Информация, Інформація", "AMF Client", CMenus::SETTINGS_AMF_CLIENT, 2, nullptr),

	AMF_ENTRY("Focus Mode", "Focus, Фокус, Режим фокуса", "General", CMenus::SETTINGS_AMF_CLIENT, 0, &g_Config.m_AmfFocusMode),
	AMF_ENTRY("Run Statistics", "Impact, Run History, статистика забегов, статистика забігів", "General", CMenus::SETTINGS_AMF_CLIENT, 0, &g_Config.m_AmfImpactStatisticsAccess),
	AMF_ENTRY("Low Priority Discord", "Discord priority, низкий приоритет Discord, низький пріоритет Discord", "General", CMenus::SETTINGS_AMF_CLIENT, 0, &g_Config.m_AmfDiscordLowPriority),
	AMF_ENTRY("Low Priority Spotify | SoundCloud", "Spotify, SoundCloud, низкий приоритет, низький пріоритет", "General", CMenus::SETTINGS_AMF_CLIENT, 0, &g_Config.m_AmfSpotifySoundCloudLowPriority),
	AMF_ENTRY("Finish Prediction", "finish, финиш, фініш", "General", CMenus::SETTINGS_AMF_CLIENT, 0, &g_Config.m_AmfFinishPrediction),
	AMF_ENTRY("Gores Mode", "Gores, горес", "General", CMenus::SETTINGS_AMF_CLIENT, 0, &g_Config.m_AmfGoresMode),
	AMF_ENTRY("Optimizer", "Performance, оптимизация, оптимізація", "General", CMenus::SETTINGS_AMF_CLIENT, 0, &g_Config.m_AmfOptimizer),
	AMF_ENTRY("Input Mode", "Fast Input, Best Input, Snap Tap, предсказание, передбачення", "General", CMenus::SETTINGS_AMF_CLIENT, 0, &g_Config.m_AmfInputMode),
	AMF_ENTRY("Auto margin", "Prediction Margin, Prediction Margin, автоподбор задержки", "General", CMenus::SETTINGS_AMF_CLIENT, 0, &g_Config.m_AmfAutoMargin),
	AMF_ENTRY("Snap Tap", "opposite directions, противоположные направления, протилежні напрямки", "General", CMenus::SETTINGS_AMF_CLIENT, 0, &g_Config.m_AmfSnapTap),
	AMF_ENTRY("High Priority", "Client process priority, высокий приоритет", "General", CMenus::SETTINGS_AMF_CLIENT, 0, &g_Config.m_AmfHighPriority),
	AMF_ENTRY("Moving Tiles", "map tiles, движущиеся тайлы, рухомі тайли", "General", CMenus::SETTINGS_AMF_CLIENT, 0, &g_Config.m_AmfMovingTilesMode),

	AMF_ENTRY("Smooth HUD", "HUD animation, плавный HUD, плавний HUD", "Visual", CMenus::SETTINGS_AMF_CLIENT, 1, &g_Config.m_AmfSmoothHud),
	AMF_ENTRY("Music Player", "Music, музыка, музика, cover, visualizer", "Visual", CMenus::SETTINGS_AMF_CLIENT, 1, &g_Config.m_AmfMusicPlayer),
	AMF_ENTRY("Music Player Visualizer", "Visualizer, визуализатор, візуалізатор", "Visual", CMenus::SETTINGS_AMF_CLIENT, 1, &g_Config.m_AmfMusicPlayerVisualizer),
	AMF_ENTRY("Custom Tune Zone Colors", "Tune Zone, tune colors, цвета зон, кольори зон", "Visual", CMenus::SETTINGS_AMF_CLIENT, 1, &g_Config.m_AmfTuneZoneColors),
	AMF_ENTRY("Custom Hooks", "Whirlwind, Vortex, Black hole, hook style, вихор, вихрь, черная дыра, чорна діра", "Visual", CMenus::SETTINGS_AMF_CLIENT, 1, &g_Config.m_AmfHookStyle),
	AMF_ENTRY("Whirlwind", "Vortex, вихор, вихрь", "Custom Hooks", CMenus::SETTINGS_AMF_CLIENT, 1, &g_Config.m_AmfHookStyle),
	AMF_ENTRY("Black hole", "blackhole, черная дыра, чорна діра", "Custom Hooks", CMenus::SETTINGS_AMF_CLIENT, 1, &g_Config.m_AmfHookStyle),
	AMF_ENTRY("Custom Effects", "Air jump ring, jump ring, кольцо прыжка, кільце стрибка", "Visual", CMenus::SETTINGS_AMF_CLIENT, 1, &g_Config.m_AmfAirJump),
	AMF_ENTRY("Air jump ring", "jump effect, воздушный прыжок, стрибок у повітрі", "Custom Effects", CMenus::SETTINGS_AMF_CLIENT, 1, &g_Config.m_AmfAirJump),
	AMF_ENTRY("Hook lasso", "Hook effects, лассо, ласо", "Custom Effects", CMenus::SETTINGS_AMF_CLIENT, 1, &g_Config.m_AmfHookTether),
	AMF_ENTRY("Hook hold", "hook effect, удержание, утримання", "Custom Effects", CMenus::SETTINGS_AMF_CLIENT, 1, &g_Config.m_AmfGatheredEnergy),
	AMF_ENTRY("UI Themes", "Theme, темы, теми", "Visual", CMenus::SETTINGS_AMF_CLIENT, 1, &g_Config.m_AmfUiThemes),
	AMF_ENTRY("Improved UI", "Better UI, улучшенный интерфейс, покращений інтерфейс", "Visual", CMenus::SETTINGS_AMF_CLIENT, 1, &g_Config.m_AmfImprovedUi),
	AMF_ENTRY("Motion Blur", "blur, размытие, розмиття", "Visual", CMenus::SETTINGS_AMF_CLIENT, 1, &g_Config.m_AmfMotionBlurEnabled),
	AMF_ENTRY("AMF Client indicators", "CAT, Presence, индикаторы, індикатори", "Visual", CMenus::SETTINGS_AMF_CLIENT, 1, &g_Config.m_AmfClientIndicator),
	AMF_ENTRY("Key Indicator", "keys, клавиши, клавіші", "Visual", CMenus::SETTINGS_AMF_CLIENT, 1, &g_Config.m_AmfKeyIndicator),
	AMF_ENTRY("Mouse Indicator", "mouse, мышь, миша", "Visual", CMenus::SETTINGS_AMF_CLIENT, 1, &g_Config.m_AmfMouseIndicator),
	AMF_ENTRY("Aspect Ratio", "resolution, aspect, соотношение сторон", "Visual", CMenus::SETTINGS_AMF_CLIENT, 1, &g_Config.m_AmfCustomAspectRatioMode),
	AMF_ENTRY("Laser Glow", "laser, свечение лазера, сяйво лазера", "Visual", CMenus::SETTINGS_AMF_CLIENT, 1, &g_Config.m_AmfLaserGlow),
	AMF_ENTRY("Weather", "Atmosphere, дождь, погода, погода", "Visual", CMenus::SETTINGS_AMF_CLIENT, 1, &g_Config.m_AmfAtmosphere),
	AMF_ENTRY("Flying Nameplates", "name plates, летающие ники, рухомі нікнейми", "Visual", CMenus::SETTINGS_AMF_CLIENT, 1, &g_Config.m_AmfFlyingNamePlates),
	AMF_ENTRY("Freeze Lifestyle", "freeze pull, заморозка, заморожування", "Visual", CMenus::SETTINGS_AMF_CLIENT, 1, &g_Config.m_AmfFreezePull),
	AMF_ENTRY("Physic Balls", "physics balls, физические мячи, фізичні м'ячі", "Visual", CMenus::SETTINGS_AMF_CLIENT, 1, &g_Config.m_AmfPhysicBallsSpawnCount),

	AMF_ENTRY("Player Indicator", "TClient, индикатор игрока", "TClient Settings", CMenus::SETTINGS_TCLIENT, 0, &g_Config.m_TcPlayerIndicator),
	AMF_ENTRY("Tee Trails", "trails, следы, сліди", "TClient Settings", CMenus::SETTINGS_TCLIENT, 0, &g_Config.m_TcTeeTrail),
	AMF_ENTRY("Rainbow", "rainbow hook, радуга, веселка", "TClient Settings", CMenus::SETTINGS_TCLIENT, 0, &g_Config.m_TcRainbowTees),
	AMF_ENTRY("Ghost Tools", "ghosts, призраки, привиди", "TClient Settings", CMenus::SETTINGS_TCLIENT, 0, &g_Config.m_TcShowOthersGhosts),
	AMF_ENTRY("Frozen Tee Display", "frozen HUD, замороженные, заморожені", "TClient Settings", CMenus::SETTINGS_TCLIENT, 0, &g_Config.m_TcShowFrozenHud),
	AMF_ENTRY("Tile Outlines", "outlines, контуры, контури", "TClient Settings", CMenus::SETTINGS_TCLIENT, 0, &g_Config.m_TcOutline),
	AMF_ENTRY("Status Bar", "status, строка состояния, рядок стану", "TClient", CMenus::SETTINGS_TCLIENT, 4, &g_Config.m_TcStatusBar),

	AMF_ENTRY("Controls", "Controls, управление, керування", "DDNet", CMenus::SETTINGS_CONTROLS, -1, &g_Config.m_ClMouseDeadzone),
	AMF_ENTRY("Graphics", "Graphics, графика, графіка", "DDNet", CMenus::SETTINGS_GRAPHICS, -1, &g_Config.m_GfxScreenWidth),
	AMF_ENTRY("Sound", "Audio, звук", "DDNet", CMenus::SETTINGS_SOUND, -1, &g_Config.m_SndVolume),
};
#undef AMF_ENTRY

struct SPopupContext : SPopupMenuId
{
	CMenus *m_pMenus = nullptr;
	CUi *m_pUi = nullptr;
	CGameClient *m_pGameClient = nullptr;
};

SPopupContext gs_PopupContext;
CLineInputBuffered<128> gs_SearchInput;
CScrollRegion gs_ResultsScroll;
std::vector<CScrollRegion *> gs_vScrollStack;
std::vector<const SEntry *> gs_vFiltered;
std::array<char, 128> gs_aCachedQuery{};
std::array<char, 64> gs_aCachedLanguage{};
const void *gs_pHighlightId = nullptr;
const void *gs_pBlockHighlightDrawn = nullptr;
float gs_HighlightTime = 0.0f;
bool gs_PendingScroll = false;
bool gs_SearchFocused = false;

CScrollRegion *ActiveScrollRegion()
{
	return gs_vScrollStack.empty() ? nullptr : gs_vScrollStack.back();
}

const char *LocalizedName(const SEntry &Entry)
{
	if(Entry.m_SettingsPage == CMenus::SETTINGS_AMF_CLIENT)
		return TCLocalize(Entry.m_pName, "AMF Client");
	return TCLocalize(Entry.m_pName);
}

const char *PageLabel(const SEntry &Entry)
{
	if(Entry.m_SettingsPage == CMenus::SETTINGS_AMF_CLIENT)
	{
		if(Entry.m_Subpage == 0)
			return TCLocalize("General", "AMF Client");
		if(Entry.m_Subpage == 1)
			return TCLocalize("Visual", "AMF Client");
		return TCLocalize("Information", "AMF Client");
	}
	if(Entry.m_SettingsPage == CMenus::SETTINGS_TCLIENT)
		return Entry.m_Subpage == 4 ? TCLocalize("Status Bar") : TCLocalize("TClient");
	if(Entry.m_SettingsPage == CMenus::SETTINGS_CONTROLS)
		return Localize("Controls");
	if(Entry.m_SettingsPage == CMenus::SETTINGS_GRAPHICS)
		return Localize("Graphics");
	if(Entry.m_SettingsPage == CMenus::SETTINGS_SOUND)
		return Localize("Sound");
	return Localize("Settings");
}

bool EntryMatches(const SEntry &Entry, const char *pQuery)
{
	return MatchesText(LocalizedName(Entry), pQuery) || MatchesText(Entry.m_pName, pQuery) ||
		MatchesText(Entry.m_pAliases, pQuery) || MatchesText(Entry.m_pSection, pQuery) || MatchesText(PageLabel(Entry), pQuery);
}

void RebuildFilterIfNeeded()
{
	const char *pQuery = gs_SearchInput.GetString();
	if(str_comp(gs_aCachedQuery.data(), pQuery) == 0 && str_comp(gs_aCachedLanguage.data(), g_Config.m_ClLanguagefile) == 0)
		return;
	str_copy(gs_aCachedQuery.data(), pQuery, gs_aCachedQuery.size());
	str_copy(gs_aCachedLanguage.data(), g_Config.m_ClLanguagefile, gs_aCachedLanguage.size());
	gs_vFiltered.clear();
	if(pQuery[0] == '\0')
		return;
	for(const SEntry &Entry : gs_aCatalog)
		if(EntryMatches(Entry, pQuery))
			gs_vFiltered.push_back(&Entry);
}

void FormatPath(const SEntry &Entry, char *pBuf, size_t BufSize)
{
	str_format(pBuf, BufSize, "%s > %s", PageLabel(Entry), Entry.m_pSection ? TCLocalize(Entry.m_pSection, Entry.m_SettingsPage == CMenus::SETTINGS_AMF_CLIENT ? "AMF Client" : "") : "");
}

void NavigateTo(CMenus *pMenus, const SEntry &Entry)
{
	pMenus->NavigateToSettingsSearchResult(Entry.m_SettingsPage, Entry.m_Subpage);
	gs_pHighlightId = Entry.m_pId;
	gs_HighlightTime = HIGHLIGHT_DURATION;
	gs_PendingScroll = Entry.m_pId != nullptr;
}

bool DoSearchEdit(CUi *pUi, CLineInput *pLineInput, const CUIRect *pRect, bool HotkeyEnabled)
{
	const bool Changed = pLineInput->WasChanged();
	const bool CursorChanged = pLineInput->WasCursorChanged();
	const bool Inside = pUi->MouseHovered(pRect);
	if(HotkeyEnabled && pUi->Input()->ModifierIsPressed() && pUi->Input()->KeyPress(KEY_F))
	{
		pUi->SetActiveItem(pLineInput);
		pLineInput->SelectAll();
		gs_SearchFocused = true;
	}
	if(pUi->MouseButtonClicked(0))
		gs_SearchFocused = Inside;
	if(Inside && !pUi->MouseButton(0))
		pUi->SetHotItem(pLineInput);
	if(Inside && pUi->MouseButton(0))
		pUi->SetActiveItem(pLineInput);
	else if(pUi->CheckActiveItem(pLineInput) && !pUi->MouseButton(0))
		pUi->SetActiveItem(nullptr);
	if(pUi->Enabled() && gs_SearchFocused)
		pLineInput->Activate(EInputPriority::UI);
	else
		pLineInput->Deactivate();

	pRect->Draw(ColorRGBA(1.0f, 1.0f, 1.0f, gs_SearchFocused ? 0.14f : (Inside ? 0.11f : 0.08f)), IGraphics::CORNER_ALL, 3.0f);
	CUIRect Textbox;
	pRect->VMargin(4.0f, &Textbox);
	pLineInput->SetEmptyText(Localize("Search"));
	Textbox.x -= pLineInput->GetScrollOffset();
	pUi->ClipEnable(pRect);
	pLineInput->Render(&Textbox, 13.0f, TEXTALIGN_ML, Changed || CursorChanged, -1.0f, 0.0f, {});
	pUi->ClipDisable();
	return Changed;
}

CUi::EPopupMenuFunctionResult PopupRender(void *pContext, CUIRect View, bool Active)
{
	SPopupContext *pPopup = static_cast<SPopupContext *>(pContext);
	CUIRect SearchBar, List;
	View.HSplitTop(26.0f, &SearchBar, &List);
	List.HSplitTop(6.0f, nullptr, &List);
	DoSearchEdit(pPopup->m_pUi, &gs_SearchInput, &SearchBar, Active && !pPopup->m_pGameClient->m_GameConsole.IsActive());
	RebuildFilterIfNeeded();

	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollbarThickness = 10.0f;
	ScrollParams.m_ScrollbarMargin = 4.0f;
	ScrollParams.m_ScrollbarNoOuterMargin = true;
	ScrollParams.m_ScrollUnit = 42.0f;
	ScrollParams.m_ForceShowScrollbar = gs_vFiltered.size() > 5;
	gs_ResultsScroll.Begin(&List, &ScrollParams);
	if(gs_vFiltered.empty())
	{
		CUIRect Empty;
		List.HSplitTop(24.0f, &Empty, &List);
		if(gs_ResultsScroll.AddRect(Empty))
		{
			const char *pText = gs_SearchInput.IsEmpty() ? TCLocalize("Type to search settings", "AMF Client") : Localize("No results");
			pPopup->m_pUi->DoLabel(&Empty, pText, 12.0f, TEXTALIGN_MC);
		}
	}
	else
	{
		for(const SEntry *pEntry : gs_vFiltered)
		{
			CUIRect Row, Title, Path;
			List.HSplitTop(2.0f, nullptr, &List);
			List.HSplitTop(40.0f, &Row, &List);
			if(!gs_ResultsScroll.AddRect(Row))
				continue;
			if(pPopup->m_pUi->HotItem() == pEntry)
				Row.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f), IGraphics::CORNER_ALL, 4.0f);
			Row.HSplitTop(18.0f, &Title, &Path);
			pPopup->m_pUi->DoLabel(&Title, LocalizedName(*pEntry), 13.0f, TEXTALIGN_ML);
			char aPath[192];
			FormatPath(*pEntry, aPath, sizeof(aPath));
			SLabelProperties Props;
			Props.SetColor(ColorRGBA(0.7f, 0.7f, 0.7f, 1.0f));
			pPopup->m_pUi->DoLabel(&Path, aPath, 11.0f, TEXTALIGN_ML, Props);
			if(Active && pPopup->m_pUi->DoButtonLogic(pEntry, 0, &Row, BUTTONFLAG_LEFT))
			{
				NavigateTo(pPopup->m_pMenus, *pEntry);
				gs_ResultsScroll.End();
				gs_SearchFocused = false;
				return CUi::POPUP_CLOSE_CURRENT;
			}
		}
	}
	gs_ResultsScroll.End();
	return CUi::POPUP_KEEP_OPEN;
}

void OpenPopup(CMenus *pMenus, CUi *pUi, CGameClient *pGameClient, const CUIRect &Anchor)
{
	gs_PopupContext.m_pMenus = pMenus;
	gs_PopupContext.m_pUi = pUi;
	gs_PopupContext.m_pGameClient = pGameClient;
	const float PopupWidth = 320.0f;
	const float PopupHeight = 320.0f;
	const bool NavOnLeft = AmfUiTheme::IsSettingsNavOnLeft();
	const float X = NavOnLeft ? Anchor.x : Anchor.x + Anchor.w - PopupWidth;
	pUi->DoPopupMenu(&gs_PopupContext, X, Anchor.y + Anchor.h + 4.0f, PopupWidth, PopupHeight, &gs_PopupContext, PopupRender);
	gs_SearchFocused = true;
	pUi->SetActiveItem(&gs_SearchInput);
	gs_SearchInput.SelectAll();
}
} // namespace

void RenderButton(CMenus *pMenus, CUi *pUi, CGameClient *pGameClient, IInput *pInput, const CUIRect &Header)
{
	CUIRect Button = Header;
	if(AmfUiTheme::IsSettingsNavOnLeft())
	{
		Button.VSplitLeft(12.0f, nullptr, &Button);
		Button.VSplitLeft(28.0f, &Button, nullptr);
	}
	else
	{
		Button.VSplitRight(12.0f, &Button, nullptr);
		Button.VSplitRight(28.0f, nullptr, &Button);
	}
	Button.HMargin(11.0f, &Button);
	static char s_IconId;
	const bool Open = pUi->IsPopupOpen(&gs_PopupContext);
	const bool Hot = pUi->HotItem() == &s_IconId;
	pUi->TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	pUi->TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	SLabelProperties Props;
	Props.SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, Open || Hot ? 1.0f : 0.65f));
	pUi->DoLabel(&Button, FontIcon::MAGNIFYING_GLASS, Button.h * 0.85f, TEXTALIGN_MC, Props);
	pUi->TextRender()->SetRenderFlags(0);
	pUi->TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	if(pUi->DoButtonLogic(&s_IconId, 0, &Button, BUTTONFLAG_LEFT))
	{
		if(Open)
		{
			pUi->ClosePopupMenu(&gs_PopupContext);
			gs_SearchFocused = false;
			gs_SearchInput.Deactivate();
		}
		else
			OpenPopup(pMenus, pUi, pGameClient, Button);
	}
	if(!pUi->IsPopupOpen() && !pGameClient->m_GameConsole.IsActive() && pInput->ModifierIsPressed() && pInput->KeyPress(KEY_F))
		OpenPopup(pMenus, pUi, pGameClient, Button);
}

void Update(float DeltaTime)
{
	gs_pBlockHighlightDrawn = nullptr;
	if(gs_HighlightTime <= 0.0f)
		return;
	gs_HighlightTime = std::max(0.0f, gs_HighlightTime - DeltaTime);
	if(gs_HighlightTime == 0.0f)
	{
		gs_pHighlightId = nullptr;
		gs_PendingScroll = false;
	}
}

namespace
{
float HighlightAlpha()
{
	if(gs_HighlightTime <= 0.0f || !gs_pHighlightId)
		return 0.0f;
	const float T = gs_HighlightTime / HIGHLIGHT_DURATION;
	return T > 0.7f ? 1.0f : std::clamp(T / 0.7f, 0.0f, 1.0f);
}

void ApplyHighlight(const CUIRect *pRect)
{
	const float Alpha = HighlightAlpha();
	if(Alpha <= 0.0f)
		return;
	CUIRect Glow = *pRect;
	Glow.Draw(ColorRGBA(HIGHLIGHT_COLOR.r, HIGHLIGHT_COLOR.g, HIGHLIGHT_COLOR.b, 0.18f * Alpha), IGraphics::CORNER_ALL, 4.0f);
	Glow.DrawOutline(ColorRGBA(HIGHLIGHT_COLOR.r, HIGHLIGHT_COLOR.g, HIGHLIGHT_COLOR.b, Alpha));
	if(gs_PendingScroll && ActiveScrollRegion())
	{
		ActiveScrollRegion()->AddRect(*pRect, false);
		ActiveScrollRegion()->ScrollHere(CScrollRegion::SCROLLHERE_KEEP_IN_VIEW);
		gs_PendingScroll = false;
	}
}
} // namespace

void DrawBlockHighlight(const void *pId, const CUIRect *pRect)
{
	if(!pId || !pRect || pId != gs_pHighlightId)
		return;
	gs_pBlockHighlightDrawn = pId;
	ApplyHighlight(pRect);
}

void DrawItemHighlight(const void *pId, const CUIRect *pRect)
{
	if(!pId || !pRect || pId != gs_pHighlightId || pId == gs_pBlockHighlightDrawn)
		return;
	ApplyHighlight(pRect);
}

void SetActiveScrollRegion(CScrollRegion *pRegion)
{
	if(pRegion)
		gs_vScrollStack.push_back(pRegion);
}

void ClearActiveScrollRegion(CScrollRegion *pRegion)
{
	if(!pRegion)
		return;
	for(auto It = gs_vScrollStack.rbegin(); It != gs_vScrollStack.rend(); ++It)
	{
		if(*It == pRegion)
		{
			gs_vScrollStack.erase(std::next(It).base());
			return;
		}
	}
}

} // namespace AmfSettingsSearch
