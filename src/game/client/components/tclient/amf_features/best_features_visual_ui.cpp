/* AMF visual feature settings. */
#include <game/client/components/menus.h>

#include <base/color.h>
#include <base/str.h>

#include <engine/font_icons.h>
#include <engine/input.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/menu_background.h>
#include <game/client/components/tclient/amf_features/settings_policy.h>
#include <game/client/components/tclient/amf_settings_search.h>
#include <game/client/components/tclient/amf_features/ui_theme/style.h>
#include <game/client/components/tclient/amf_features/ui_theme/settings_animation.h>
#include <game/client/gameclient.h>
#include <game/client/smooth_ui.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>

namespace
{
constexpr float LINE_HEIGHT = 20.0f;
constexpr float COLOR_HEIGHT = 25.0f;
constexpr float GAP = 5.0f;
constexpr float CARD_PADDING = 8.0f;
constexpr float HEADER_HEIGHT = 20.0f;

constexpr float ROW_HEIGHT = LINE_HEIGHT + GAP;
constexpr float COLOR_ROW_HEIGHT = COLOR_HEIGHT + GAP;

void DrawPhysicBallsEClientBadge(IGraphics *pGraphics, CUi *pUi, ITextRender *pTextRender, CUIRect *pRow, float Gap)
{
	constexpr const char *pLabel = "E-Client";
	constexpr float FontSize = 12.0f;
	constexpr ColorRGBA Left(0.95f, 0.80f, 0.20f, 1.0f);
	constexpr ColorRGBA Right(0.75f, 0.55f, 0.05f, 1.0f);
	constexpr float Rounding = 5.0f;
	const float BadgeWidth = pTextRender->TextWidth(FontSize, pLabel) + 10.0f;
	CUIRect Badge;
	pRow->VSplitRight(BadgeWidth + Gap, pRow, &Badge);
	Badge.VSplitLeft(Gap, nullptr, &Badge);
	Badge.HMargin(2.0f, &Badge);
	if(AmfUiTheme::IsCustomUiBadge())
		AmfUiTheme::DrawUiBadge(&Badge, Left.a, Right.a, Rounding, false);
	else
		pGraphics->DrawRect4(Badge.x, Badge.y, Badge.w, Badge.h, Left, Right, Left, Right, IGraphics::CORNER_ALL, Rounding);
	pUi->DoLabel(&Badge, pLabel, FontSize, TEXTALIGN_MC);
}

struct SThemeElement
{
	const char *m_pName;
	int *m_pEnabled;
	unsigned *m_pColor;
	int *m_pGradient;
	unsigned *m_pGradientColor;
	int *m_pAccent;
	unsigned *m_pAccentColor;
	int *m_pOpacity;
	unsigned m_DefaultColor;
};

void RefreshImprovedFont(ITextRender *pTextRender, CGameClient *pGameClient);

unsigned ParseHexColor(const char *pText)
{
	if(pText == nullptr)
		return 0;
	if(*pText == '$')
		++pText;
	else if(pText[0] == '0' && (pText[1] == 'x' || pText[1] == 'X'))
		pText += 2;
	return static_cast<unsigned>(std::strtoul(pText, nullptr, 16));
}

bool ParseIntToken(const char *pToken, const char *pKey, int *pValue)
{
	const size_t KeyLength = str_length(pKey);
	if(str_comp_nocase_num(pToken, pKey, KeyLength) != 0 || pToken[KeyLength] != '=')
		return false;
	*pValue = str_toint(pToken + KeyLength + 1);
	return true;
}

bool ParseColorToken(const char *pToken, const char *pKey, unsigned *pValue)
{
	const size_t KeyLength = str_length(pKey);
	if(str_comp_nocase_num(pToken, pKey, KeyLength) != 0 || pToken[KeyLength] != '=')
		return false;
	*pValue = ParseHexColor(pToken + KeyLength + 1);
	return true;
}

bool ParseThemeElementLine(const char *pLine, SThemeElement &Element)
{
	char aToken[128];
	const char *pCursor = pLine;
	bool First = true;
	while((pCursor = str_next_token(pCursor, " \t", aToken, sizeof(aToken))))
	{
		if(First)
		{
			First = false;
			continue;
		}
		ParseIntToken(aToken, "enable", Element.m_pEnabled) ||
			ParseColorToken(aToken, "color", Element.m_pColor) ||
			ParseIntToken(aToken, "gradient", Element.m_pGradient) ||
			ParseColorToken(aToken, "gradient_color", Element.m_pGradientColor) ||
			ParseIntToken(aToken, "accent", Element.m_pAccent) ||
			ParseColorToken(aToken, "accent_color", Element.m_pAccentColor) ||
			(Element.m_pOpacity != nullptr && ParseIntToken(aToken, "opacity", Element.m_pOpacity));
	}
	return !First;
}

bool ImportTheme(const char *pText, std::array<SThemeElement, AmfUiTheme::UI_THEME_ELEM_NUM> &aElements)
{
	if(pText == nullptr || !str_startswith(pText, "AMF_UI_THEME_EXPORT"))
		return false;

	const char *p = pText;
	while(*p)
	{
		char aLine[512];
		int Length = 0;
		while(*p && *p != '\n' && *p != '\r' && Length < (int)sizeof(aLine) - 1)
			aLine[Length++] = *p++;
		aLine[Length] = '\0';
		while(*p == '\n' || *p == '\r')
			++p;
		if(aLine[0] == '\0' || str_startswith(aLine, "AMF_UI_THEME_EXPORT"))
			continue;
		if(str_startswith(aLine, "animate_speed="))
		{
			g_Config.m_AmfCustomUiGradientAnimateSpeed = std::clamp(str_toint(aLine + 14), 0, 100);
			continue;
		}
		if(str_startswith(aLine, "background="))
		{
			g_Config.m_AmfCustomUiBackground = str_toint(aLine + 11) != 0;
			continue;
		}
		if(str_startswith(aLine, "ui_color="))
		{
			g_Config.m_UiColor = ParseHexColor(aLine + 9);
			continue;
		}
		for(SThemeElement &Element : aElements)
		{
			const size_t NameLength = str_length(Element.m_pName);
			if(str_comp_nocase_num(aLine, Element.m_pName, NameLength) == 0 && (aLine[NameLength] == ' ' || aLine[NameLength] == '\t'))
				ParseThemeElementLine(aLine, Element);
		}
	}
	g_Config.m_AmfCustomUiBlockOpacity = std::clamp(g_Config.m_AmfCustomUiBlockOpacity, 0, 100);
	g_Config.m_AmfCustomUiMainPanelOpacity = std::clamp(g_Config.m_AmfCustomUiMainPanelOpacity, 0, 100);
	g_Config.m_AmfCustomUiNavTabOpacity = std::clamp(g_Config.m_AmfCustomUiNavTabOpacity, 0, 100);
	return true;
}

void ExportTheme(IInput *pInput, const std::array<SThemeElement, AmfUiTheme::UI_THEME_ELEM_NUM> &aElements)
{
	char aBuffer[4096] = "AMF_UI_THEME_EXPORT v1\n";
	char aLine[256];
	str_format(aLine, sizeof(aLine), "animate_speed=%d\nbackground=%d\nui_color=$%08X\n",
		g_Config.m_AmfCustomUiGradientAnimateSpeed, g_Config.m_AmfCustomUiBackground, g_Config.m_UiColor);
	str_append(aBuffer, aLine, sizeof(aBuffer));
	for(const SThemeElement &Element : aElements)
	{
		if(Element.m_pOpacity != nullptr)
			str_format(aLine, sizeof(aLine), "%s enable=%d color=$%08X gradient=%d gradient_color=$%08X accent=%d accent_color=$%08X opacity=%d\n",
				Element.m_pName, *Element.m_pEnabled, *Element.m_pColor, *Element.m_pGradient, *Element.m_pGradientColor, *Element.m_pAccent, *Element.m_pAccentColor, *Element.m_pOpacity);
		else
			str_format(aLine, sizeof(aLine), "%s enable=%d color=$%08X gradient=%d gradient_color=$%08X accent=%d accent_color=$%08X\n",
				Element.m_pName, *Element.m_pEnabled, *Element.m_pColor, *Element.m_pGradient, *Element.m_pGradientColor, *Element.m_pAccent, *Element.m_pAccentColor);
		str_append(aBuffer, aLine, sizeof(aBuffer));
	}
	pInput->SetClipboardText(aBuffer);
}

void ResetTheme(std::array<SThemeElement, AmfUiTheme::UI_THEME_ELEM_NUM> &aElements)
{
	g_Config.m_AmfUiThemeElement = DefaultConfig::AmfUiThemeElement;
	g_Config.m_AmfCustomUiBackground = DefaultConfig::AmfCustomUiBackground;
	g_Config.m_AmfCustomUiGradientAnimateSpeed = DefaultConfig::AmfCustomUiGradientAnimateSpeed;
	g_Config.m_UiColor = DefaultConfig::UiColor;
	for(SThemeElement &Element : aElements)
	{
		*Element.m_pEnabled = 0;
		*Element.m_pGradient = 0;
		*Element.m_pAccent = 0;
		*Element.m_pColor = Element.m_DefaultColor;
		*Element.m_pGradientColor = Element.m_pOpacity != nullptr ? 0xFF000000U : 0xFFA0A0A0U;
		*Element.m_pAccentColor = Element.m_DefaultColor;
		if(Element.m_pOpacity != nullptr)
			*Element.m_pOpacity = Element.m_pName[0] == 'b' ? 25 : 50;
	}
}

void ResetImprovedUi(ITextRender *pTextRender, CGameClient *pGameClient)
{
	// The main toggle transitions the selected font back to its default state.
	g_Config.m_AmfImprovedUi = 0;
	g_Config.m_AmfImprovedUiCheckbox = 1;
	g_Config.m_AmfImprovedUiScrollbar = 1;
	g_Config.m_AmfImprovedUiTabs = 1;
	g_Config.m_AmfImprovedUiTabSide = 1;
	g_Config.m_AmfSettingsNavExpanded = DefaultConfig::AmfSettingsNavExpanded;
	g_Config.m_AmfImprovedUiColorPicker = 1;
	g_Config.m_AmfImprovedUiFont = 0;
	RefreshImprovedFont(pTextRender, pGameClient);
}

void ResetWeather()
{
	g_Config.m_AmfWeather = DefaultConfig::AmfWeather;
	g_Config.m_AmfWeatherAmount = DefaultConfig::AmfWeatherAmount;
	g_Config.m_AmfWeatherSnowColor = DefaultConfig::AmfWeatherSnowColor;
	g_Config.m_AmfWeatherSnowflakes = DefaultConfig::AmfWeatherSnowflakes;
	g_Config.m_AmfWeatherRainColor = DefaultConfig::AmfWeatherRainColor;
	g_Config.m_AmfWeatherFireflyColor = DefaultConfig::AmfWeatherFireflyColor;
	g_Config.m_AmfAmbientDust = DefaultConfig::AmfAmbientDust;
	g_Config.m_AmfAmbientDustAmount = DefaultConfig::AmfAmbientDustAmount;
	g_Config.m_AmfAmbientDustColor = DefaultConfig::AmfAmbientDustColor;
}

void ResetHookStyle()
{
	g_Config.m_AmfHookStyle = DefaultConfig::AmfHookStyle;
	g_Config.m_AmfHookRope = DefaultConfig::AmfHookRope;
	g_Config.m_AmfHookChargeTime = DefaultConfig::AmfHookChargeTime;
	g_Config.m_AmfHookSize = DefaultConfig::AmfHookSize;
	g_Config.m_AmfHookWindColor = DefaultConfig::AmfHookWindColor;
	g_Config.m_AmfHookWindBurst = DefaultConfig::AmfHookWindBurst;
	g_Config.m_AmfHookWindEye = DefaultConfig::AmfHookWindEye;
	g_Config.m_AmfHookWindDensity = DefaultConfig::AmfHookWindDensity;
	g_Config.m_AmfHookWindSpin = DefaultConfig::AmfHookWindSpin;
	g_Config.m_AmfHookWindThickness = DefaultConfig::AmfHookWindThickness;
	g_Config.m_AmfHookWindOpacity = DefaultConfig::AmfHookWindOpacity;
	g_Config.m_AmfHookWindLeafAmount = DefaultConfig::AmfHookWindLeafAmount;
	g_Config.m_AmfHookWindDust = DefaultConfig::AmfHookWindDust;
	g_Config.m_AmfHookHoleColor = DefaultConfig::AmfHookHoleColor;
	g_Config.m_AmfHookHoleDisk = DefaultConfig::AmfHookHoleDisk;
	g_Config.m_AmfHookHoleStreaks = DefaultConfig::AmfHookHoleStreaks;
	g_Config.m_AmfHookHoleSpin = DefaultConfig::AmfHookHoleSpin;
	g_Config.m_AmfHookMagicColor = DefaultConfig::AmfHookMagicColor;
	g_Config.m_AmfHookMagicRunes = DefaultConfig::AmfHookMagicRunes;
	g_Config.m_AmfHookMagicSpin = DefaultConfig::AmfHookMagicSpin;
	g_Config.m_AmfHookMagicSparkles = DefaultConfig::AmfHookMagicSparkles;
	g_Config.m_AmfHookRainbowSpeed = DefaultConfig::AmfHookRainbowSpeed;
	g_Config.m_AmfHookRainbowGlitter = DefaultConfig::AmfHookRainbowGlitter;
	g_Config.m_AmfHookRainbowRays = DefaultConfig::AmfHookRainbowRays;
	g_Config.m_AmfHookRainbowWidth = DefaultConfig::AmfHookRainbowWidth;
	g_Config.m_AmfHookFireColor = DefaultConfig::AmfHookFireColor;
	g_Config.m_AmfHookFireSmoke = DefaultConfig::AmfHookFireSmoke;
	g_Config.m_AmfHookFireIntensity = DefaultConfig::AmfHookFireIntensity;
	g_Config.m_AmfHookFireEmbers = DefaultConfig::AmfHookFireEmbers;
	g_Config.m_AmfHookBoltColor = DefaultConfig::AmfHookBoltColor;
	g_Config.m_AmfHookBoltJitter = DefaultConfig::AmfHookBoltJitter;
	g_Config.m_AmfHookBoltBranches = DefaultConfig::AmfHookBoltBranches;
	g_Config.m_AmfHookBoltSparks = DefaultConfig::AmfHookBoltSparks;
	g_Config.m_AmfHookBoltArc = DefaultConfig::AmfHookBoltArc;
	g_Config.m_AmfHookTentacleColor = DefaultConfig::AmfHookTentacleColor;
	g_Config.m_AmfHookTentacleWaves = DefaultConfig::AmfHookTentacleWaves;
	g_Config.m_AmfHookTentacleThickness = DefaultConfig::AmfHookTentacleThickness;
	g_Config.m_AmfHookTentacleSuckers = DefaultConfig::AmfHookTentacleSuckers;
	g_Config.m_AmfHookTentacleSlime = DefaultConfig::AmfHookTentacleSlime;
}

void ResetHookEffects()
{
	g_Config.m_AmfHookTether = DefaultConfig::AmfHookTether;
	g_Config.m_AmfHookLassoBox = DefaultConfig::AmfHookLassoBox;
	g_Config.m_AmfAirJump = DefaultConfig::AmfAirJump;
	g_Config.m_AmfAirJumpHideParticles = DefaultConfig::AmfAirJumpHideParticles;
	g_Config.m_AmfHammerSlash = DefaultConfig::AmfHammerSlash;
	g_Config.m_AmfHammerHideParticles = DefaultConfig::AmfHammerHideParticles;
	g_Config.m_AmfGatheredEnergyAura = DefaultConfig::AmfGatheredEnergyAura;
	g_Config.m_AmfGatheredEnergyBurst = DefaultConfig::AmfGatheredEnergyBurst;
	g_Config.m_AmfGatheredEnergyAmount = DefaultConfig::AmfGatheredEnergyAmount;
}

void ResetFlyingNameplates()
{
	g_Config.m_AmfFlyingNamePlatesHideLine = DefaultConfig::AmfFlyingNamePlatesHideLine;
	g_Config.m_AmfFlyingNamePlatesLift = DefaultConfig::AmfFlyingNamePlatesLift;
	g_Config.m_AmfFlyingNamePlatesDrag = DefaultConfig::AmfFlyingNamePlatesDrag;
	g_Config.m_AmfFlyingNamePlatesFollow = DefaultConfig::AmfFlyingNamePlatesFollow;
}

void ResetFreezeLifestyle()
{
	g_Config.m_AmfFreezePullColor = DefaultConfig::AmfFreezePullColor;
	g_Config.m_AmfFreezePullRange = DefaultConfig::AmfFreezePullRange;
	g_Config.m_AmfFreezePullStrands = DefaultConfig::AmfFreezePullStrands;
	g_Config.m_AmfFreezePullShield = DefaultConfig::AmfFreezePullShield;
	g_Config.m_AmfFreezePullSparks = DefaultConfig::AmfFreezePullSparks;
	g_Config.m_AmfFreezePullHookColor = DefaultConfig::AmfFreezePullHookColor;
}

void RefreshImprovedFont(ITextRender *pTextRender, CGameClient *pGameClient)
{
	pTextRender->SetCustomFace(g_Config.m_AmfImprovedUi && g_Config.m_AmfImprovedUiFont ? "ActayWide Bold" : g_Config.m_TcCustomFont);
	pTextRender->OnPreWindowResize();
	pGameClient->OnWindowResize();
	if(pGameClient->Editor() != nullptr)
		pGameClient->Editor()->OnWindowResize();
	pTextRender->OnWindowResize();
	pGameClient->m_MapImages.SetTextureScale(101);
	pGameClient->m_MapImages.SetTextureScale(g_Config.m_ClTextEntitiesSize);
}
} // namespace

void CMenus::RenderAmfBestFeatureVisuals(CUIRect &LeftColumn, CUIRect &RightColumn, bool TwoColumns, bool HeaderOnly)
{
	CUIRect &Left = LeftColumn;
	CUIRect &Right = TwoColumns ? RightColumn : LeftColumn;
	const bool AnimateFeatureSections = g_Config.m_AmfSmoothHud && g_Config.m_AmfAnimMenu;
	const float FeatureDeltaTime = Client()->RenderFrameTime();
	constexpr int FeatureSectionDuration = 180;
	const auto BeginAnimatedDetails = [&](CUIRect &Remaining, float TargetHeight, float Progress, CUIRect &Details, CUIRect &Clip) {
		Remaining.HSplitTop(TargetHeight * Progress, &Clip, &Remaining);
		Details = Clip;
		Details.h = TargetHeight;
		Ui()->ClipEnable(&Clip);
	};
	const auto BeginCard = [&](CUIRect &CardColumn, const char *pTitle, float BodyHeight, CUIRect *pResetRect = nullptr, bool PhysicBallsBadge = false) {
		CUIRect Card, Body, Header;
		const float Height = CARD_PADDING * 2.0f + HEADER_HEIGHT + GAP + BodyHeight;
		Card = AmfSettingsPolicy::TakeCard(CardColumn, Height);
		AmfUiTheme::DrawUiBlock(&Card, IGraphics::CORNER_ALL, 8.0f);
		Card.Margin(CARD_PADDING, &Body);
		Body.HSplitTop(HEADER_HEIGHT, &Header, &Body);
		if(pResetRect != nullptr)
			Header.VSplitRight(22.0f, &Header, pResetRect);
		if(PhysicBallsBadge)
		{
			Header.VSplitRight(GAP, &Header, nullptr);
			DrawPhysicBallsEClientBadge(Graphics(), Ui(), TextRender(), &Header, GAP);
		}
		Ui()->DoLabel(&Header, TCLocalize(pTitle, "AMF Client"), 18.0f, TEXTALIGN_ML);
		Body.HSplitTop(GAP, nullptr, &Body);
		return Body;
	};
	const auto AddCheck = [&](CUIRect &Body, int *pValue, const char *pText) {
		CUIRect Row;
		Body.HSplitTop(LINE_HEIGHT, &Row, &Body);
		DoButton_CheckBoxAutoVMarginAndSet(pValue, TCLocalize(pText, "AMF Client"), pValue, &Row, LINE_HEIGHT);
		Body.HSplitTop(GAP, nullptr, &Body);
	};
	const auto AddSlider = [&](CUIRect &Body, int *pValue, const char *pText, int Min, int Max, const char *pSuffix = "") {
		CUIRect Row;
		Body.HSplitTop(LINE_HEIGHT, &Row, &Body);
		Ui()->DoScrollbarOption(pValue, pValue, &Row, TCLocalize(pText, "AMF Client"), Min, Max, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE, pSuffix);
		Body.HSplitTop(GAP, nullptr, &Body);
	};
	const auto AddChoice = [&](CUIRect &Body, const char *pLabel, int *pValue, const char **apItems, int Count, CUi::SDropDownState &State, CScrollRegion &Scroll) {
		CUIRect Row, Label, Select;
		Body.HSplitTop(LINE_HEIGHT, &Row, &Body);
		AmfSettingsSearch::DrawItemHighlight(pValue, &Row);
		Row.VSplitLeft(std::min(105.0f, Row.w * 0.38f), &Label, &Select);
		Ui()->DoLabel(&Label, TCLocalize(pLabel, "AMF Client"), 12.0f, TEXTALIGN_ML);
		State.m_SelectionPopupContext.m_pScrollRegion = &Scroll;
		*pValue = Ui()->DoDropDown(&Select, std::clamp(*pValue, 0, Count - 1), apItems, Count, State);
		Body.HSplitTop(GAP, nullptr, &Body);
	};
	const auto AddColor = [&](CUIRect &Body, CButtonContainer *pReset, const char *pLabel, unsigned *pValue, unsigned DefaultValue, bool Alpha = false) {
		CUIRect Row;
		Body.HSplitTop(COLOR_HEIGHT, &Row, &Body);
		DoLine_ColorPicker(pReset, COLOR_HEIGHT, 12.0f, 0.0f, &Row, TCLocalize(pLabel, "AMF Client"), pValue,
			AmfSettingsPolicy::ColorPreview(DefaultValue, Alpha), false, nullptr, Alpha);
		Body.HSplitTop(GAP, nullptr, &Body);
	};
	const auto AddResetButton = [&](CUIRect &Button, CButtonContainer *pId, const auto &ResetFn) {
		if(Ui()->DoButton_FontIcon(pId, FontIcon::ARROW_ROTATE_LEFT, 0, &Button, BUTTONFLAG_LEFT))
			ResetFn();
		GameClient()->m_Tooltips.DoToolTip(pId, &Button, TCLocalize("Reset to defaults", "AMF Client"));
	};
	if(HeaderOnly)
	{
	// Improved UI is deliberately first in the Visual settings flow.
	{
		static CSmoothUiSectionAnimation s_ImprovedUiSettingsSection;
		const float ImprovedUiProgress = s_ImprovedUiSettingsSection.Update(g_Config.m_AmfImprovedUi != 0, FeatureDeltaTime, FeatureSectionDuration, AnimateFeatureSections);
		const float ImprovedUiDetailsHeight = GAP + (5.0f + (g_Config.m_AmfImprovedUiTabs ? 1.0f : 0.0f)) * ROW_HEIGHT;
		CUIRect ResetRect;
		CUIRect Body = BeginCard(Left, "Improved UI", ROW_HEIGHT + ImprovedUiProgress * ImprovedUiDetailsHeight, &ResetRect);
		static CButtonContainer s_ImprovedUiReset;
		AddResetButton(ResetRect, &s_ImprovedUiReset, [&]() { ResetImprovedUi(TextRender(), GameClient()); });
		static CButtonContainer s_ImprovedUi;
		const int OldStyle = g_Config.m_AmfImprovedUi;
		if(DoButton_CheckBoxAutoVMarginAndSet(&s_ImprovedUi, TCLocalize("Enable better UI", "AMF Client"), &g_Config.m_AmfImprovedUi, &Body, LINE_HEIGHT) && OldStyle != g_Config.m_AmfImprovedUi)
		{
			g_Config.m_AmfImprovedUiFont = g_Config.m_AmfImprovedUi != 0;
			RefreshImprovedFont(TextRender(), GameClient());
		}
		if(s_ImprovedUiSettingsSection.IsVisible())
		{
			CUIRect Details, Clip;
			BeginAnimatedDetails(Body, ImprovedUiDetailsHeight, ImprovedUiProgress, Details, Clip);
			Details.HSplitTop(GAP, nullptr, &Details);
			AddCheck(Details, &g_Config.m_AmfImprovedUiCheckbox, "Better checkbox");
			AddCheck(Details, &g_Config.m_AmfImprovedUiScrollbar, "Better scrollbar");
			AddCheck(Details, &g_Config.m_AmfImprovedUiTabs, "Better tab option");
			if(g_Config.m_AmfImprovedUiTabs)
			{
				CUIRect Row, Left, Right;
				Details.HSplitTop(LINE_HEIGHT, &Row, &Details);
				Row.VSplitMid(&Left, &Right, 2.0f);
				static CButtonContainer s_Left, s_Right;
				if(DoButton_Menu(&s_Left, TCLocalize("Left", "AMF Client"), g_Config.m_AmfImprovedUiTabSide == 0, &Left, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
					g_Config.m_AmfImprovedUiTabSide = 0;
				if(DoButton_Menu(&s_Right, TCLocalize("Right", "AMF Client"), g_Config.m_AmfImprovedUiTabSide == 1, &Right, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
					g_Config.m_AmfImprovedUiTabSide = 1;
				Details.HSplitTop(GAP, nullptr, &Details);
			}
			AddCheck(Details, &g_Config.m_AmfImprovedUiColorPicker, "Better color picker");
			static CButtonContainer s_Font;
			const int OldFont = g_Config.m_AmfImprovedUiFont;
			if(DoButton_CheckBoxAutoVMarginAndSet(&s_Font, TCLocalize("Better font", "AMF Client"), &g_Config.m_AmfImprovedUiFont, &Details, LINE_HEIGHT) && OldFont != g_Config.m_AmfImprovedUiFont)
				RefreshImprovedFont(TextRender(), GameClient());
			Details.HSplitTop(GAP, nullptr, &Details);
			Ui()->ClipDisable();
		}
	}

	static std::array<SThemeElement, AmfUiTheme::UI_THEME_ELEM_NUM> s_aThemeElements = {{
		{"text", &g_Config.m_AmfCustomUiText, &g_Config.m_AmfCustomUiTextColor, &g_Config.m_AmfCustomUiTextGradient, &g_Config.m_AmfCustomUiTextGradientColor, &g_Config.m_AmfCustomUiTextUseAccent, &g_Config.m_AmfCustomUiTextAccentColor, nullptr, 0xFFFFFFFFU},
		{"checkbox", &g_Config.m_AmfCustomUiCheckbox, &g_Config.m_AmfCustomUiCheckboxColor, &g_Config.m_AmfCustomUiCheckboxGradient, &g_Config.m_AmfCustomUiCheckboxGradientColor, &g_Config.m_AmfCustomUiCheckboxUseAccent, &g_Config.m_AmfCustomUiCheckboxAccentColor, nullptr, 0xFFFFFFFFU},
		{"scrollbar", &g_Config.m_AmfCustomUiScrollbar, &g_Config.m_AmfCustomUiScrollbarColor, &g_Config.m_AmfCustomUiScrollbarGradient, &g_Config.m_AmfCustomUiScrollbarGradientColor, &g_Config.m_AmfCustomUiScrollbarUseAccent, &g_Config.m_AmfCustomUiScrollbarAccentColor, nullptr, 0xFFFFFFFFU},
		{"button", &g_Config.m_AmfCustomUiButton, &g_Config.m_AmfCustomUiButtonColor, &g_Config.m_AmfCustomUiButtonGradient, &g_Config.m_AmfCustomUiButtonGradientColor, &g_Config.m_AmfCustomUiButtonUseAccent, &g_Config.m_AmfCustomUiButtonAccentColor, nullptr, 0xFFFFFFFFU},
		{"droplist", &g_Config.m_AmfCustomUiDropdown, &g_Config.m_AmfCustomUiDropdownColor, &g_Config.m_AmfCustomUiDropdownGradient, &g_Config.m_AmfCustomUiDropdownGradientColor, &g_Config.m_AmfCustomUiDropdownUseAccent, &g_Config.m_AmfCustomUiDropdownAccentColor, nullptr, 0xFFFFFFFFU},
		{"input", &g_Config.m_AmfCustomUiEditBox, &g_Config.m_AmfCustomUiEditBoxColor, &g_Config.m_AmfCustomUiEditBoxGradient, &g_Config.m_AmfCustomUiEditBoxGradientColor, &g_Config.m_AmfCustomUiEditBoxUseAccent, &g_Config.m_AmfCustomUiEditBoxAccentColor, nullptr, 0xFFFFFFFFU},
		{"block", &g_Config.m_AmfCustomUiBlock, &g_Config.m_AmfCustomUiBlockColor, &g_Config.m_AmfCustomUiBlockGradient, &g_Config.m_AmfCustomUiBlockGradientColor, &g_Config.m_AmfCustomUiBlockUseAccent, &g_Config.m_AmfCustomUiBlockAccentColor, &g_Config.m_AmfCustomUiBlockOpacity, 0xFF000000U},
		{"panel", &g_Config.m_AmfCustomUiMainPanel, &g_Config.m_AmfCustomUiMainPanelColor, &g_Config.m_AmfCustomUiMainPanelGradient, &g_Config.m_AmfCustomUiMainPanelGradientColor, &g_Config.m_AmfCustomUiMainPanelUseAccent, &g_Config.m_AmfCustomUiMainPanelAccentColor, &g_Config.m_AmfCustomUiMainPanelOpacity, 0xFF000000U},
		{"nav", &g_Config.m_AmfCustomUiNavTab, &g_Config.m_AmfCustomUiNavTabColor, &g_Config.m_AmfCustomUiNavTabGradient, &g_Config.m_AmfCustomUiNavTabGradientColor, &g_Config.m_AmfCustomUiNavTabUseAccent, &g_Config.m_AmfCustomUiNavTabAccentColor, &g_Config.m_AmfCustomUiNavTabOpacity, 0xFF000000U},
		{"badge", &g_Config.m_AmfCustomUiBadge, &g_Config.m_AmfCustomUiBadgeColor, &g_Config.m_AmfCustomUiBadgeGradient, &g_Config.m_AmfCustomUiBadgeGradientColor, &g_Config.m_AmfCustomUiBadgeUseAccent, &g_Config.m_AmfCustomUiBadgeAccentColor, nullptr, 0xFFFFFFFFU},
		{"binds", &g_Config.m_AmfCustomUiBind, &g_Config.m_AmfCustomUiBindColor, &g_Config.m_AmfCustomUiBindGradient, &g_Config.m_AmfCustomUiBindGradientColor, &g_Config.m_AmfCustomUiBindUseAccent, &g_Config.m_AmfCustomUiBindAccentColor, nullptr, 0xFFFFFFFFU},
	}};
	static char s_aSavedMenuMap[sizeof(g_Config.m_ClMenuMap)] = "";
	static bool s_HasSavedMenuMap = false;
	static bool s_BackgroundApplied = false;
	const auto SyncBackground = [&]() {
		const bool WantBackground = g_Config.m_AmfUiThemes != 0 && g_Config.m_AmfCustomUiBackground != 0;
		if(WantBackground && !s_BackgroundApplied)
		{
			str_copy(s_aSavedMenuMap, g_Config.m_ClMenuMap);
			s_HasSavedMenuMap = true;
			str_copy(g_Config.m_ClMenuMap, "");
			GameClient()->m_MenuBackground.LoadMenuBackground(false, false);
			s_BackgroundApplied = true;
		}
		else if(!WantBackground && s_BackgroundApplied)
		{
			if(s_HasSavedMenuMap)
			{
				str_copy(g_Config.m_ClMenuMap, s_aSavedMenuMap);
				s_HasSavedMenuMap = false;
			}
			GameClient()->m_MenuBackground.LoadMenuBackground();
			s_BackgroundApplied = false;
		}
	};
	SyncBackground();

	// UI Themes exposes the complete per-element palette, gradient, accent, opacity,
	// clipboard export/import, background color and reset behavior from the donor.
	{
		const int Element = std::clamp(g_Config.m_AmfUiThemeElement, 0, AmfUiTheme::UI_THEME_ELEM_NUM - 1);
		SThemeElement &Selected = s_aThemeElements[Element];
		static CSmoothUiSectionAnimation s_ThemeStyleSection;
		static CSmoothUiSectionAnimation s_ThemeGradientSection;
		static CSmoothUiSectionAnimation s_ThemeAccentSection;
		static CSmoothUiSectionAnimation s_ThemeOpacitySection;
		static CSmoothUiSectionAnimation s_ThemeBackgroundColorSection;
		const float ThemeStyleProgress = s_ThemeStyleSection.Update(*Selected.m_pEnabled != 0, FeatureDeltaTime, FeatureSectionDuration, AnimateFeatureSections);
		const float ThemeGradientProgress = s_ThemeGradientSection.Update(*Selected.m_pGradient != 0, FeatureDeltaTime, FeatureSectionDuration, AnimateFeatureSections);
		const float ThemeAccentProgress = s_ThemeAccentSection.Update(*Selected.m_pAccent != 0, FeatureDeltaTime, FeatureSectionDuration, AnimateFeatureSections);
		const float ThemeOpacityProgress = s_ThemeOpacitySection.Update(Selected.m_pOpacity != nullptr, FeatureDeltaTime, FeatureSectionDuration, AnimateFeatureSections);
		const float ThemeBackgroundColorProgress = s_ThemeBackgroundColorSection.Update(g_Config.m_AmfCustomUiBackground != 0, FeatureDeltaTime, FeatureSectionDuration, AnimateFeatureSections);
		const AmfUiTheme::SSettingsAnimationLayout ThemeLayout = AmfUiTheme::MakeSettingsAnimationLayout(
			ThemeStyleProgress,
			ThemeGradientProgress,
			ThemeAccentProgress,
			ThemeOpacityProgress,
			ThemeBackgroundColorProgress,
			ROW_HEIGHT,
			COLOR_ROW_HEIGHT,
			LINE_HEIGHT,
			GAP);
		static CSmoothUiSectionAnimation s_ThemeDetailsSection;
		const float ThemeProgress = s_ThemeDetailsSection.Update(g_Config.m_AmfUiThemes != 0, FeatureDeltaTime, FeatureSectionDuration, AnimateFeatureSections);
		CUIRect ResetRect;
		CUIRect Body = BeginCard(Left, "UI Themes", ROW_HEIGHT + ThemeProgress * ThemeLayout.m_TotalDetailsHeight, &ResetRect);
		static CButtonContainer s_ThemeReset;
		AddResetButton(ResetRect, &s_ThemeReset, [&]() { ResetTheme(s_aThemeElements); SyncBackground(); });
		static CButtonContainer s_ThemeEnabled;
		DoButton_CheckBoxAutoVMarginAndSet(&s_ThemeEnabled, TCLocalize("Enable", "AMF Client"), &g_Config.m_AmfUiThemes, &Body, LINE_HEIGHT);
		SyncBackground();
		if(s_ThemeDetailsSection.IsVisible())
		{
			CUIRect Details, Clip;
			BeginAnimatedDetails(Body, ThemeLayout.m_TotalDetailsHeight, ThemeProgress, Details, Clip);
			Details.HSplitTop(GAP, nullptr, &Details);
			static CUi::SDropDownState s_ElementState;
			static CScrollRegion s_ElementScroll;
			static const char *s_apElements[] = {"Text", "Checkbox", "Scrollbar", "Button", "Droplist", "Input field", "Function block", "UI background", "Settings tabs", "Badge", "Binds"};
			static std::array<const char *, AmfUiTheme::UI_THEME_ELEM_NUM> s_apTranslatedElements{};
			for(int i = 0; i < AmfUiTheme::UI_THEME_ELEM_NUM; ++i)
				s_apTranslatedElements[i] = TCLocalize(s_apElements[i], "AMF Client");
			AddChoice(Details, "Element", &g_Config.m_AmfUiThemeElement, s_apTranslatedElements.data(), (int)s_apTranslatedElements.size(), s_ElementState, s_ElementScroll);
			const int CurrentElement = std::clamp(g_Config.m_AmfUiThemeElement, 0, AmfUiTheme::UI_THEME_ELEM_NUM - 1);
			SThemeElement &Current = s_aThemeElements[CurrentElement];
			AddCheck(Details, Current.m_pEnabled, "Use custom style");
			if(s_ThemeStyleSection.IsVisible())
			{
				CUIRect StyleDetails, StyleClip;
				BeginAnimatedDetails(Details, ThemeLayout.m_StyleTargetHeight, ThemeStyleProgress, StyleDetails, StyleClip);
				static CButtonContainer s_aBaseReset[AmfUiTheme::UI_THEME_ELEM_NUM];
				AddColor(StyleDetails, &s_aBaseReset[CurrentElement], "Base color", Current.m_pColor, Current.m_DefaultColor, true);
				const int OldGradient = *Current.m_pGradient;
				AddCheck(StyleDetails, Current.m_pGradient, "Gradient");
				if(OldGradient == 0 && *Current.m_pGradient)
					*Current.m_pGradientColor = AmfUiTheme::DarkenConfigColor(*Current.m_pColor);
				if(s_ThemeGradientSection.IsVisible())
				{
					CUIRect GradientDetails, GradientClip;
					BeginAnimatedDetails(StyleDetails, ThemeLayout.m_GradientTargetHeight, ThemeGradientProgress, GradientDetails, GradientClip);
					static CButtonContainer s_aGradientReset[AmfUiTheme::UI_THEME_ELEM_NUM];
					AddColor(GradientDetails, &s_aGradientReset[CurrentElement], "Gradient color", Current.m_pGradientColor, Current.m_pOpacity != nullptr ? 0xFF000000U : 0xFFA0A0A0U, true);
					AddSlider(GradientDetails, &g_Config.m_AmfCustomUiGradientAnimateSpeed, "Animate speed", 0, 100, "%");
					Ui()->ClipDisable();
				}
				const int OldAccent = *Current.m_pAccent;
				AddCheck(StyleDetails, Current.m_pAccent, "Accent");
				if(s_ThemeAccentSection.IsVisible())
				{
					CUIRect AccentDetails, AccentClip;
					BeginAnimatedDetails(StyleDetails, ThemeLayout.m_AccentTargetHeight, ThemeAccentProgress, AccentDetails, AccentClip);
					static CButtonContainer s_aAccentReset[AmfUiTheme::UI_THEME_ELEM_NUM];
					AddColor(AccentDetails, &s_aAccentReset[CurrentElement], "Accent color", Current.m_pAccentColor, Current.m_DefaultColor, true);
					Ui()->ClipDisable();
				}
				(void)OldAccent;
				if(Current.m_pOpacity != nullptr && s_ThemeOpacitySection.IsVisible())
				{
					CUIRect OpacityDetails, OpacityClip;
					BeginAnimatedDetails(StyleDetails, ThemeLayout.m_OpacityTargetHeight, ThemeOpacityProgress, OpacityDetails, OpacityClip);
					AddSlider(OpacityDetails, Current.m_pOpacity, "Opacity", 0, 100, "%");
					Ui()->ClipDisable();
				}
				Ui()->ClipDisable();
			}
			// The animated reserved heights are consumed in the same order as these rows,
			// keeping the card body and following Visual blocks on one continuous layout.
			AddCheck(Details, &g_Config.m_AmfCustomUiBackground, "Enable colorable background");
			SyncBackground();
			if(s_ThemeBackgroundColorSection.IsVisible())
			{
				CUIRect BackgroundColorDetails, BackgroundColorClip;
				BeginAnimatedDetails(Details, ThemeLayout.m_BackgroundColorTargetHeight, ThemeBackgroundColorProgress, BackgroundColorDetails, BackgroundColorClip);
				static CButtonContainer s_BackgroundReset;
				AddColor(BackgroundColorDetails, &s_BackgroundReset, "Background color", &g_Config.m_UiColor, DefaultConfig::UiColor, true);
				Ui()->ClipDisable();
			}
			CUIRect ExportImportRow, ExportRect, ImportRect;
			Details.HSplitTop(LINE_HEIGHT, &ExportImportRow, &Details);
			ExportImportRow.VSplitMid(&ExportRect, &ImportRect, GAP);
			static CButtonContainer s_Export, s_Import;
			static int64_t s_ExportTime = 0, s_ImportTime = 0;
			const int64_t Now = time_get();
			const char *pExport = s_ExportTime != 0 && Now - s_ExportTime < time_freq() ? TCLocalize("Exported!", "AMF Client") : TCLocalize("Export", "AMF Client");
			const char *pImport = s_ImportTime != 0 && Now - s_ImportTime < time_freq() ? TCLocalize("Imported!", "AMF Client") : TCLocalize("Import", "AMF Client");
			if(DoButton_Menu(&s_Export, pExport, 0, &ExportRect))
			{
				ExportTheme(Input(), s_aThemeElements);
				s_ExportTime = Now;
			}
			GameClient()->m_Tooltips.DoToolTip(&s_Export, &ExportRect, TCLocalize("Copy theme to clipboard", "AMF Client"));
			if(DoButton_Menu(&s_Import, pImport, 0, &ImportRect))
			{
				const std::string ClipboardText = Input()->GetClipboardText();
				if(ImportTheme(ClipboardText.c_str(), s_aThemeElements))
				{
					SyncBackground();
					s_ImportTime = Now;
				}
			}
			GameClient()->m_Tooltips.DoToolTip(&s_Import, &ImportRect, TCLocalize("Paste theme from clipboard", "AMF Client"));
			Ui()->ClipDisable();
		}
	}

	return;
	}

	// Weather: master, four donor modes and the corresponding per-mode options.
	{
		const int Kind = std::clamp(g_Config.m_AmfWeather, 0, 3);
		const int DetailRows = Kind == 0 ? 2 : 1;
		const float WeatherDetailsHeight = ROW_HEIGHT + DetailRows * ROW_HEIGHT + COLOR_ROW_HEIGHT;
		static CSmoothUiSectionAnimation s_WeatherDetailsSection;
		const float WeatherProgress = s_WeatherDetailsSection.Update(g_Config.m_AmfAtmosphere != 0, FeatureDeltaTime, FeatureSectionDuration, AnimateFeatureSections);
		CUIRect ResetRect;
		CUIRect Body = BeginCard(Left, "Weather", ROW_HEIGHT + WeatherDetailsHeight * WeatherProgress, &ResetRect);
		static CButtonContainer s_WeatherReset;
		static CButtonContainer s_WeatherColorReset;
		AddResetButton(ResetRect, &s_WeatherReset, ResetWeather);
		AddCheck(Body, &g_Config.m_AmfAtmosphere, "Enable");
		if(s_WeatherDetailsSection.IsVisible())
		{
			CUIRect Details, Clip;
			BeginAnimatedDetails(Body, WeatherDetailsHeight, WeatherProgress, Details, Clip);
			static CUi::SDropDownState s_WeatherState, s_SnowState;
			static CScrollRegion s_WeatherScroll, s_SnowScroll;
			static const char *s_apWeather[] = {"Snow", "Rain", "Fireflies", "Dust"};
			static const char *s_apSnow[] = {"Dots", "Snowflakes"};
			std::array<const char *, 4> aWeather{};
			for(int i = 0; i < 4; ++i)
				aWeather[i] = TCLocalize(s_apWeather[i], "AMF Client");
			AddChoice(Details, "Weather type", &g_Config.m_AmfWeather, aWeather.data(), 4, s_WeatherState, s_WeatherScroll);
			if(g_Config.m_AmfWeather == 3)
			{
				AddSlider(Details, &g_Config.m_AmfAmbientDustAmount, "Amount", 25, 200, "%");
				AddColor(Details, &s_WeatherColorReset, "Dust color", &g_Config.m_AmfAmbientDustColor, DefaultConfig::AmfAmbientDustColor);
			}
			else
			{
				AddSlider(Details, &g_Config.m_AmfWeatherAmount, "Amount", 25, 300, "%");
				if(g_Config.m_AmfWeather == 1)
					AddColor(Details, &s_WeatherColorReset, "Rain color", &g_Config.m_AmfWeatherRainColor, DefaultConfig::AmfWeatherRainColor);
				else if(g_Config.m_AmfWeather == 2)
					AddColor(Details, &s_WeatherColorReset, "Firefly color", &g_Config.m_AmfWeatherFireflyColor, DefaultConfig::AmfWeatherFireflyColor);
				else
				{
					static std::array<const char *, 2> aSnow{};
					aSnow[0] = TCLocalize(s_apSnow[0], "AMF Client");
					aSnow[1] = TCLocalize(s_apSnow[1], "AMF Client");
					AddChoice(Details, "Snow look", &g_Config.m_AmfWeatherSnowflakes, aSnow.data(), 2, s_SnowState, s_SnowScroll);
					AddColor(Details, &s_WeatherColorReset, "Snow color", &g_Config.m_AmfWeatherSnowColor, DefaultConfig::AmfWeatherSnowColor);
				}
			}
			Ui()->ClipDisable();
		}
	}

	// Hook mode and shared style parameters.
	{
		const int Style = std::clamp(g_Config.m_AmfHookStyle, 0, 8);
		static int s_LastNonZeroStyle = 0;
		if(Style != 0)
			s_LastNonZeroStyle = Style;
		const int DisplayStyle = Style != 0 ? Style : s_LastNonZeroStyle;
		int ModeRows = 0;
		switch(DisplayStyle)
		{
		case 1: ModeRows = 8; break;
		case 2: ModeRows = 3; break;
		case 3: ModeRows = 3; break;
		case 4: ModeRows = 3; break;
		case 6: ModeRows = 4; break;
		case 7: ModeRows = 4; break;
		case 8: ModeRows = 4; break;
		default: break;
		}
		const int SharedRows = DisplayStyle == 0 ? 0 : 3;
		const float HookStyleDetailsHeight = (SharedRows + ModeRows) * ROW_HEIGHT + (DisplayStyle == 1 || DisplayStyle == 2 || DisplayStyle == 3 || DisplayStyle == 4 || DisplayStyle == 7 || DisplayStyle == 8 ? COLOR_ROW_HEIGHT : 0.0f);
		static CSmoothUiSectionAnimation s_HookStyleSection;
		const float HookStyleProgress = s_HookStyleSection.Update(Style != 0, FeatureDeltaTime, FeatureSectionDuration, AnimateFeatureSections);
		CUIRect ResetRect;
		CUIRect Body = BeginCard(Left, "Custom Hooks", ROW_HEIGHT + HookStyleDetailsHeight * HookStyleProgress, &ResetRect);
		static CButtonContainer s_HookReset;
		static CButtonContainer s_HookColorReset;
		AddResetButton(ResetRect, &s_HookReset, ResetHookStyle);
		static const char *s_apStyles[] = {"None", "Whirlwind", "Black hole", "Magic", "Fire", "Troll", "Rainbow", "Lightning", "Tentacle"};
		static std::array<const char *, 9> aStyles{};
		for(int i = 0; i < 9; ++i)
			aStyles[i] = TCLocalize(s_apStyles[i], "AMF Client");
		static CUi::SDropDownState s_StyleState;
		static CScrollRegion s_StyleScroll;
		AddChoice(Body, "Hook", &g_Config.m_AmfHookStyle, aStyles.data(), 9, s_StyleState, s_StyleScroll);
		if(s_HookStyleSection.IsVisible() && DisplayStyle != 0)
		{
			CUIRect Details, Clip;
			BeginAnimatedDetails(Body, HookStyleDetailsHeight, HookStyleProgress, Details, Clip);
			AddCheck(Details, &g_Config.m_AmfHookRope, "Style the hook");
			AddSlider(Details, &g_Config.m_AmfHookChargeTime, "Grow time", 200, 4000, "ms");
			AddSlider(Details, &g_Config.m_AmfHookSize, "Size", 50, 200, "%");
			if(DisplayStyle == 1)
			{
				AddCheck(Details, &g_Config.m_AmfHookWindBurst, "Grab gust"); AddCheck(Details, &g_Config.m_AmfHookWindEye, "Eye");
				AddSlider(Details, &g_Config.m_AmfHookWindDensity, "Wind lines", 25, 300, "%"); AddSlider(Details, &g_Config.m_AmfHookWindSpin, "Spin speed", 25, 300, "%");
				AddSlider(Details, &g_Config.m_AmfHookWindThickness, "Line thickness", 25, 300, "%"); AddSlider(Details, &g_Config.m_AmfHookWindOpacity, "Wind opacity", 10, 100, "%");
				AddSlider(Details, &g_Config.m_AmfHookWindLeafAmount, "Leaves", 0, 300, "%"); AddSlider(Details, &g_Config.m_AmfHookWindDust, "Dust", 0, 300, "%");
				AddColor(Details, &s_HookColorReset, "Wind color", &g_Config.m_AmfHookWindColor, DefaultConfig::AmfHookWindColor);
			}
			else if(DisplayStyle == 2)
			{
				AddCheck(Details, &g_Config.m_AmfHookHoleDisk, "Disk"); AddSlider(Details, &g_Config.m_AmfHookHoleStreaks, "Matter", 0, 300, "%"); AddSlider(Details, &g_Config.m_AmfHookHoleSpin, "Spin", 25, 300, "%");
				AddColor(Details, &s_HookColorReset, "Disk color", &g_Config.m_AmfHookHoleColor, DefaultConfig::AmfHookHoleColor);
			}
			else if(DisplayStyle == 3)
			{
				AddCheck(Details, &g_Config.m_AmfHookMagicRunes, "Runes"); AddSlider(Details, &g_Config.m_AmfHookMagicSpin, "Spin", 25, 300, "%"); AddSlider(Details, &g_Config.m_AmfHookMagicSparkles, "Sparkles", 0, 300, "%");
				AddColor(Details, &s_HookColorReset, "Spell color", &g_Config.m_AmfHookMagicColor, DefaultConfig::AmfHookMagicColor);
			}
			else if(DisplayStyle == 4)
			{
				AddCheck(Details, &g_Config.m_AmfHookFireSmoke, "Smoke"); AddSlider(Details, &g_Config.m_AmfHookFireIntensity, "Flames", 25, 300, "%"); AddSlider(Details, &g_Config.m_AmfHookFireEmbers, "Embers", 0, 300, "%");
				AddColor(Details, &s_HookColorReset, "Fire color", &g_Config.m_AmfHookFireColor, DefaultConfig::AmfHookFireColor);
			}
			else if(DisplayStyle == 6)
			{
				AddCheck(Details, &g_Config.m_AmfHookRainbowRays, "Rays"); AddSlider(Details, &g_Config.m_AmfHookRainbowSpeed, "Flow", 25, 300, "%"); AddSlider(Details, &g_Config.m_AmfHookRainbowGlitter, "Glitter", 0, 300, "%"); AddSlider(Details, &g_Config.m_AmfHookRainbowWidth, "Ribbon width", 50, 250, "%");
			}
			else if(DisplayStyle == 7)
			{
				AddCheck(Details, &g_Config.m_AmfHookBoltArc, "Wall arcs"); AddSlider(Details, &g_Config.m_AmfHookBoltJitter, "Jitter", 25, 300, "%"); AddSlider(Details, &g_Config.m_AmfHookBoltBranches, "Forks", 0, 6); AddSlider(Details, &g_Config.m_AmfHookBoltSparks, "Sparks", 0, 300, "%");
				AddColor(Details, &s_HookColorReset, "Lightning color", &g_Config.m_AmfHookBoltColor, DefaultConfig::AmfHookBoltColor);
			}
			else if(DisplayStyle == 8)
			{
				AddCheck(Details, &g_Config.m_AmfHookTentacleSuckers, "Suckers"); AddSlider(Details, &g_Config.m_AmfHookTentacleWaves, "Writhing", 25, 300, "%"); AddSlider(Details, &g_Config.m_AmfHookTentacleThickness, "Thickness", 50, 250, "%"); AddSlider(Details, &g_Config.m_AmfHookTentacleSlime, "Slime", 0, 300, "%");
				AddColor(Details, &s_HookColorReset, "Flesh color", &g_Config.m_AmfHookTentacleColor, DefaultConfig::AmfHookTentacleColor);
			}
			Ui()->ClipDisable();
		}
	}

	// Hook interactions and the gathered-energy effect are independent of hook style.
	{
		static CSmoothUiSectionAnimation s_AirJumpDetailsSection;
		static CSmoothUiSectionAnimation s_HammerDetailsSection;
		static CSmoothUiSectionAnimation s_EnergyDetailsSection;
		const float AirJumpProgress = s_AirJumpDetailsSection.Update(g_Config.m_AmfAirJump != 0, FeatureDeltaTime, FeatureSectionDuration, AnimateFeatureSections);
		const float HammerProgress = s_HammerDetailsSection.Update(g_Config.m_AmfHammerSlash != 0, FeatureDeltaTime, FeatureSectionDuration, AnimateFeatureSections);
		const float EnergyProgress = s_EnergyDetailsSection.Update(g_Config.m_AmfGatheredEnergy != 0, FeatureDeltaTime, FeatureSectionDuration, AnimateFeatureSections);
		const float DetailsHeight = (AirJumpProgress + HammerProgress + 3.0f * EnergyProgress) * ROW_HEIGHT;
		CUIRect ResetRect;
		CUIRect Body = BeginCard(Left, "Custom Effects", 5.0f * ROW_HEIGHT + DetailsHeight, &ResetRect);
		static CButtonContainer s_AirJump, s_AirJumpParticles, s_Hammer, s_HammerParticles, s_Tether, s_Box, s_Energy, s_Aura, s_Burst;
		static CButtonContainer s_HookEffectsReset;
		AddResetButton(ResetRect, &s_HookEffectsReset, ResetHookEffects);
		AddCheck(Body, &g_Config.m_AmfAirJump, "Air jump ring");
		if(s_AirJumpDetailsSection.IsVisible())
		{
			CUIRect Details, Clip;
			BeginAnimatedDetails(Body, ROW_HEIGHT, AirJumpProgress, Details, Clip);
			AddCheck(Details, &g_Config.m_AmfAirJumpHideParticles, "Disable jump particles");
			Ui()->ClipDisable();
		}
		AddCheck(Body, &g_Config.m_AmfHammerSlash, "Hammer slash");
		if(s_HammerDetailsSection.IsVisible())
		{
			CUIRect Details, Clip;
			BeginAnimatedDetails(Body, ROW_HEIGHT, HammerProgress, Details, Clip);
			AddCheck(Details, &g_Config.m_AmfHammerHideParticles, "Disable hammer particles");
			Ui()->ClipDisable();
		}
		AddCheck(Body, &g_Config.m_AmfHookTether, "Hook lasso");
		AddCheck(Body, &g_Config.m_AmfHookLassoBox, "Hook box");
		AddCheck(Body, &g_Config.m_AmfGatheredEnergy, "Hook hold");
		if(s_EnergyDetailsSection.IsVisible())
		{
			CUIRect Details, Clip;
			BeginAnimatedDetails(Body, 3.0f * ROW_HEIGHT, EnergyProgress, Details, Clip);
			AddCheck(Details, &g_Config.m_AmfGatheredEnergyAura, "Hold aura");
			AddCheck(Details, &g_Config.m_AmfGatheredEnergyBurst, "Hold burst");
			AddSlider(Details, &g_Config.m_AmfGatheredEnergyAmount, "Energy motes", 25, 300, "%");
			Ui()->ClipDisable();
		}
	}

	// Keep card bodies in small render lambdas so the column can allocate each
	// feature in the requested visual order without changing feature behavior.
	auto RenderFlyingNameplatesCard = [&]() {
	{
		static CSmoothUiSectionAnimation s_FlyingNameplatesSection;
		const float FlyingProgress = s_FlyingNameplatesSection.Update(g_Config.m_AmfFlyingNamePlates != 0, FeatureDeltaTime, FeatureSectionDuration, AnimateFeatureSections);
		CUIRect ResetRect;
		CUIRect Body = BeginCard(Right, "Flying Nameplates", ROW_HEIGHT + 4.0f * ROW_HEIGHT * FlyingProgress, &ResetRect);
		static CButtonContainer s_FlyingNameplatesReset;
		AddResetButton(ResetRect, &s_FlyingNameplatesReset, ResetFlyingNameplates);
		AddCheck(Body, &g_Config.m_AmfFlyingNamePlates, "Enable flying name plates");
		if(s_FlyingNameplatesSection.IsVisible())
		{
			CUIRect Details, Clip;
			BeginAnimatedDetails(Body, 4.0f * ROW_HEIGHT, FlyingProgress, Details, Clip);
			AddCheck(Details, &g_Config.m_AmfFlyingNamePlatesHideLine, "Hide line");
			AddSlider(Details, &g_Config.m_AmfFlyingNamePlatesLift, "Lift above player", 0, 120);
			AddSlider(Details, &g_Config.m_AmfFlyingNamePlatesDrag, "Movement drag", 0, 200);
			AddSlider(Details, &g_Config.m_AmfFlyingNamePlatesFollow, "Follow speed", 1, 100);
			Ui()->ClipDisable();
		}
	}
	};

	// Freeze Lifestyle is the donor's freeze-pull effect, with nearby freeze tiles
	// drawing cold strands, an optional shield arc, and frost sparks toward own tees.
	auto RenderFreezeLifestyleCard = [&]() {
	{
		static CSmoothUiSectionAnimation s_FreezeLifestyleSection;
		static CSmoothUiSectionAnimation s_FreezeColdColorSection;
		const float FreezeColdColorProgress = s_FreezeColdColorSection.Update(
			g_Config.m_AmfFreezePullHookColor == 0,
			FeatureDeltaTime,
			FeatureSectionDuration,
			AnimateFeatureSections);
		const float FreezeLifestyleDetailsHeight = 5.0f * ROW_HEIGHT + FreezeColdColorProgress * COLOR_ROW_HEIGHT;
		const float FreezeLifestyleProgress = s_FreezeLifestyleSection.Update(g_Config.m_AmfFreezePull != 0, FeatureDeltaTime, FeatureSectionDuration, AnimateFeatureSections);
		CUIRect ResetRect;
		CUIRect Body = BeginCard(Right, "Freeze Lifestyle", ROW_HEIGHT + FreezeLifestyleProgress * FreezeLifestyleDetailsHeight, &ResetRect);
		static CButtonContainer s_FreezePullColor;
		static CButtonContainer s_FreezeLifestyleReset;
		AddResetButton(ResetRect, &s_FreezeLifestyleReset, ResetFreezeLifestyle);
		AddCheck(Body, &g_Config.m_AmfFreezePull, "Enable");
		if(s_FreezeLifestyleSection.IsVisible())
		{
			CUIRect Details, Clip;
			BeginAnimatedDetails(Body, FreezeLifestyleDetailsHeight, FreezeLifestyleProgress, Details, Clip);
			AddCheck(Details, &g_Config.m_AmfFreezePullShield, "Shield");
			AddSlider(Details, &g_Config.m_AmfFreezePullRange, "Reach", 60, 260);
			AddSlider(Details, &g_Config.m_AmfFreezePullStrands, "Amount", 1, 8);
			AddSlider(Details, &g_Config.m_AmfFreezePullSparks, "Frost", 0, 300, "%");
			AddCheck(Details, &g_Config.m_AmfFreezePullHookColor, "Custom hook color");
			if(s_FreezeColdColorSection.IsVisible())
				AddColor(Details, &s_FreezePullColor, "Cold color", &g_Config.m_AmfFreezePullColor, DefaultConfig::AmfFreezePullColor);
			Ui()->ClipDisable();
		}
	}
	};

	// Physic Balls has persistent skin/count controls and the donor's three actions.
	auto RenderPhysicBallsCard = [&]() {
	{
		CUIRect Body = BeginCard(Right, "Physic Balls", 3.0f * ROW_HEIGHT + 25.0f, nullptr, true);
		CUIRect CountRow, CountLabel, ClearButton;
		Body.HSplitTop(LINE_HEIGHT, &CountRow, &Body);
		CountRow.VSplitRight(LINE_HEIGHT, &CountLabel, &ClearButton);
		char aCount[64];
		str_format(aCount, sizeof(aCount), TCLocalize("Ball amount: %d", "AMF Client"), (int)GameClient()->m_AmfPhysicBalls.GetBallCount());
		Ui()->DoLabel(&CountLabel, aCount, 13.0f, TEXTALIGN_ML);
		static CButtonContainer s_ClearBalls;
		if(Ui()->DoButton_FontIcon(&s_ClearBalls, FontIcon::TRASH, 0, &ClearButton, BUTTONFLAG_LEFT))
			GameClient()->m_AmfPhysicBalls.OnReset();
		GameClient()->m_Tooltips.DoToolTip(&s_ClearBalls, &ClearButton, TCLocalize("Clear all balls", "AMF Client"));
		Body.HSplitTop(GAP, nullptr, &Body);
		CUIRect SkinRow, SkinLabel, SkinBox;
		Body.HSplitTop(LINE_HEIGHT, &SkinRow, &Body);
		SkinRow.VSplitLeft(75.0f, &SkinLabel, &SkinBox);
		Ui()->DoLabel(&SkinLabel, TCLocalize("Ball skin", "AMF Client"), 12.0f, TEXTALIGN_ML);
		static CLineInput s_BallSkin;
		s_BallSkin.SetBuffer(g_Config.m_AmfPhysicBallsSkin, sizeof(g_Config.m_AmfPhysicBallsSkin));
		s_BallSkin.SetEmptyText("volleyball");
		Ui()->DoEditBox(&s_BallSkin, &SkinBox, 12.0f);
		Body.HSplitTop(GAP, nullptr, &Body);
		AddSlider(Body, &g_Config.m_AmfPhysicBallsSpawnCount, "Spawn count", 1, 50);
		CUIRect Buttons, PlayerButton, CursorButton;
		Body.HSplitTop(AmfSettingsPolicy::BallButtonHeight, &Buttons, &Body);
		const float ActionScale = std::min(1.0f, Buttons.w / (AmfSettingsPolicy::BallPlayerWidth + GAP + AmfSettingsPolicy::BallCursorWidth));
		Buttons.VSplitLeft(AmfSettingsPolicy::BallPlayerWidth * ActionScale, &PlayerButton, &Buttons);
		Buttons.VSplitLeft(GAP * ActionScale, nullptr, &Buttons);
		Buttons.VSplitLeft(AmfSettingsPolicy::BallCursorWidth * ActionScale, &CursorButton, nullptr);
		static CButtonContainer s_NewBall, s_NewBallCursor;
		if(DoButton_Menu(&s_NewBall, TCLocalize("New Ball", "AMF Client"), 0, &PlayerButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f)))
			for(int i = 0, Count = std::clamp(g_Config.m_AmfPhysicBallsSpawnCount, 1, 50); i < Count; ++i)
				GameClient()->m_AmfPhysicBalls.NewBallPlayer(60.0f);
		if(DoButton_Menu(&s_NewBallCursor, TCLocalize("New Ball Cursor", "AMF Client"), 0, &CursorButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f)))
			for(int i = 0, Count = std::clamp(g_Config.m_AmfPhysicBallsSpawnCount, 1, 50); i < Count; ++i)
				GameClient()->m_AmfPhysicBalls.NewBallCursor(60.0f);
	}
	};

	RenderPhysicBallsCard();
	RenderFreezeLifestyleCard();
	RenderFlyingNameplatesCard();

}
