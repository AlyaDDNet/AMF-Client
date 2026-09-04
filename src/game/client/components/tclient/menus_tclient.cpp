#include <base/log.h>
#include <base/math.h>
#include <base/dbg.h>
#include <base/str.h>
#include <base/time.h>
#include <base/types.h>

#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/linereader.h>
#include <engine/shared/localization.h>
#include <engine/shared/protocol7.h>
#include <engine/storage.h>
#include <engine/textrender.h>
#include <engine/updater.h>

#include <generated/client_data.h>

#include <game/client/animstate.h>
#include <game/client/components/binds.h>
#include <game/client/components/chat.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/menu_background.h>
#include <game/client/components/menus.h>
#include <game/client/components/skins.h>
#include <game/client/components/sounds.h>
#include <game/client/components/tclient/bindchat.h>
#include <game/client/components/tclient/bindwheel.h>
#include <game/client/components/tclient/amf_gradient.h>
#include <game/client/components/tclient/trails.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/skin.h>
#include <game/client/smooth_ui.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>
#include <game/tune_zone_colors.h>
#include <game/version.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

enum
{
	TCLIENT_TAB_SETTINGS = 0,
	TCLIENT_TAB_BINDWHEEL,
	TCLIENT_TAB_WARLIST,
	TCLIENT_TAB_BINDCHAT,
	TCLIENT_TAB_STATUSBAR,
	TCLIENT_TAB_INFO,
	NUMBER_OF_TCLIENT_TABS
};

typedef struct
{
	const char *m_pName;
	const char *m_pCommand;
	int m_KeyId;
	int m_ModifierCombination;
} CKeyInfo;

static float s_Time = 0.0f;
static bool s_StartedTime = false;

const float FontSize = 14.0f;
const float EditBoxFontSize = 12.0f;
const float LineSize = 20.0f;
const float ColorPickerLineSize = 25.0f;
const float HeadlineFontSize = 20.0f;
const float StandardFontSize = 14.0f;

const float HeadlineHeight = HeadlineFontSize + 0.0f;
const float Margin = 10.0f;
const float MarginSmall = 5.0f;
const float MarginExtraSmall = 2.5f;
const float MarginBetweenSections = 30.0f;
const float MarginBetweenViews = 30.0f;

namespace
{
struct SAmfClientInfoSection
{
	const char *m_pIcon;
	const char *m_pTitle;
	const char *m_pSubtitle;
	const char *m_pText;
	const char *m_pMeta;
};

struct SAmfClientInfoNavItem
{
	const char *m_pIcon;
	const char *m_pTitle;
	const char *m_pSubtitle;
};

struct SGradientNicknameOtherPreview
{
	char m_aSkinName[MAX_SKIN_LENGTH] = "default";
	int m_TeamId = TEAM_FLOCK + 1;
	ColorRGBA m_TeamOrWarColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	bool m_HasTeamOrWarColor = false;
	bool m_Initialized = false;
};

struct SGradientNicknameSelfPreview
{
	char m_aSkinName[MAX_SKIN_LENGTH] = "default";
};

void RandomizeGradientPreviewTee(SGradientNicknameOtherPreview &OtherPreview, SGradientNicknameSelfPreview &SelfPreview, CSkins &Skins)
{
	std::vector<const CSkins::CSkinContainer *> vBuiltInSkins;
	for(const CSkins::CSkinListEntry &Entry : Skins.SkinList().Skins())
	{
		const CSkins::CSkinContainer *pContainer = Entry.SkinContainer();
		if(pContainer != nullptr && pContainer->IsVanilla() && pContainer->State() == CSkins::CSkinContainer::EState::LOADED)
			vBuiltInSkins.push_back(pContainer);
	}

	const char *pSkinName = "default";
	if(!vBuiltInSkins.empty())
		pSkinName = vBuiltInSkins[std::rand() % vBuiltInSkins.size()]->Name();
	str_copy(OtherPreview.m_aSkinName, pSkinName);
	str_copy(SelfPreview.m_aSkinName, pSkinName);
	OtherPreview.m_Initialized = true;
}

constexpr int GRADIENT_PREVIEW_FIRST_TEAM = TEAM_FLOCK + 1;
constexpr int GRADIENT_PREVIEW_LAST_TEAM = TEAM_SUPER - 1;

void SetGradientPreviewTeam(SGradientNicknameOtherPreview &Preview, int TeamId, CGameClient *pGameClient)
{
	Preview.m_TeamId = std::clamp(TeamId, GRADIENT_PREVIEW_FIRST_TEAM, GRADIENT_PREVIEW_LAST_TEAM);
	Preview.m_HasTeamOrWarColor = pGameClient != nullptr;
	if(pGameClient != nullptr)
		Preview.m_TeamOrWarColor = pGameClient->GetDDTeamColor(Preview.m_TeamId);
}

void RandomizeGradientPreviewTeam(SGradientNicknameOtherPreview &Preview, CGameClient *pGameClient)
{
	SetGradientPreviewTeam(Preview, GRADIENT_PREVIEW_FIRST_TEAM + std::rand() % (GRADIENT_PREVIEW_LAST_TEAM - GRADIENT_PREVIEW_FIRST_TEAM + 1), pGameClient);
}

const char *const AMF_INFO_ICON_SHIELD = "\uF132";
const char *const AMF_INFO_ICON_CHECK = "\uF058";
constexpr int AMF_INFO_POPUP_ANIMATION_DURATION = 150;

const SAmfClientInfoNavItem gs_aAmfClientInfoNavItems[] = {
	{FontIcon::FILE, "About the agreement", "Essential information"},
	{FontIcon::LAYER_GROUP, "About the project", "AMF Client"},
	{AMF_INFO_ICON_SHIELD, "Project status", "Unofficial modification"},
	{FontIcon::USER, "Responsibility", "User"},
	{AMF_INFO_ICON_CHECK, "Rules", "Use"},
	{FontIcon::HEART, "Acknowledgements", "Creators"},
};

const int gs_aAmfClientInfoNavSectionIndices[] = {-1, 0, 1, 2, 3, 4};

const SAmfClientInfoSection gs_aAmfClientInfoSections[] = {
	{FontIcon::LAYER_GROUP, "About the project", "AMF Client",
		CLIENT_NAME " " CLIENT_RELEASE_VERSION_DISPLAY " is a custom client based on Best Client and DDNet " GAME_RELEASE_VERSION ". This foundation is extended with AMF Client features, interface and capabilities. AMF Client is owned by alya?. Artificial intelligence, including OpenAI Codex, is used to develop the client and write code.", nullptr},
	{AMF_INFO_ICON_SHIELD, "Project status", "Unofficial modification",
		"AMF Client is an unofficial modification and is not affiliated with the official DDNet team.", nullptr},
	{FontIcon::USER, "User responsibility", "Use at your own risk",
		"You are responsible for using the client, its settings and your actions on servers.", nullptr},
	{AMF_INFO_ICON_CHECK, "Rules of use", "Follow server rules",
		"Follow server rules. Do not use the client for cheats, bypassing restrictions or harming other players.", nullptr},
	{FontIcon::HEART, "Acknowledgements", "Best Client and DDNet",
		"Special thanks to the Best Client creators for the project foundation and their contribution to DDNet clients. Thanks to DDNet developers, component authors and AMF Client testers.", nullptr},
};

ColorRGBA AmfInfoAccentColor(size_t)
{
	return ColorRGBA(0.22f, 0.52f, 0.98f, 1.0f);
}

ColorRGBA AmfInfoNavAccentColor(size_t Index)
{
	return AmfInfoAccentColor(Index);
}

void DrawAmfGlassPanel(CUIRect Rect, ColorRGBA Background, ColorRGBA Border, float Rounding, int Corners = IGraphics::CORNER_ALL, float Opacity = 1.0f)
{
	constexpr float BorderWidth = 0.6f;
	Rect.Draw(Border.WithMultipliedAlpha(Opacity), Corners, Rounding);
	Rect.Margin(BorderWidth, &Rect);
	Rect.Draw(Background.WithMultipliedAlpha(Opacity), Corners, std::max(0.0f, Rounding - BorderWidth));
}

void DrawAmfInfoSectionHighlight(CUIRect Rect, float Highlight, float Opacity)
{
	if(Highlight <= 0.0f)
		return;

	// Use the identical highlight layer for the agreement intro and every
	// information card so navigation always has the same visible feedback.
	DrawAmfGlassPanel(Rect, ColorRGBA(0.94f, 0.975f, 1.0f, 0.78f * Highlight), ColorRGBA(0.64f, 0.80f, 1.0f, 0.72f * Highlight), 2.0f, IGraphics::CORNER_ALL, Opacity);
}

bool CreateAmfBlurredBackground(const CImageInfo &Source, CImageInfo &Output)
{
	if(Source.m_pData == nullptr || Source.m_Width == 0 || Source.m_Height == 0 ||
		(Source.m_Format != CImageInfo::FORMAT_RGB && Source.m_Format != CImageInfo::FORMAT_RGBA))
		return false;

	const float Scale = std::min(1.0f, std::min(384.0f / Source.m_Width, 216.0f / Source.m_Height));
	const size_t Width = std::max<size_t>(1, static_cast<size_t>(Source.m_Width * Scale));
	const size_t Height = std::max<size_t>(1, static_cast<size_t>(Source.m_Height * Scale));
	const size_t SourcePixelSize = Source.PixelSize();

	Output.Free();
	Output.m_Width = Width;
	Output.m_Height = Height;
	Output.m_Format = CImageInfo::FORMAT_RGBA;
	Output.m_pData = static_cast<uint8_t *>(malloc(Output.DataSize()));
	if(Output.m_pData == nullptr)
	{
		Output.Free();
		return false;
	}

	// Bilinear downsampling is both the first blur stage and the main performance
	// optimization. The separable kernel below then runs on at most 384x216 pixels.
	for(size_t y = 0; y < Height; ++y)
	{
		const float SourceY = ((y + 0.5f) * Source.m_Height / Height) - 0.5f;
		const int Y0 = std::clamp(static_cast<int>(SourceY), 0, static_cast<int>(Source.m_Height) - 1);
		const int Y1 = std::min(Y0 + 1, static_cast<int>(Source.m_Height) - 1);
		const float Fy = std::clamp(SourceY - static_cast<float>(Y0), 0.0f, 1.0f);
		for(size_t x = 0; x < Width; ++x)
		{
			const float SourceX = ((x + 0.5f) * Source.m_Width / Width) - 0.5f;
			const int X0 = std::clamp(static_cast<int>(SourceX), 0, static_cast<int>(Source.m_Width) - 1);
			const int X1 = std::min(X0 + 1, static_cast<int>(Source.m_Width) - 1);
			const float Fx = std::clamp(SourceX - static_cast<float>(X0), 0.0f, 1.0f);
			uint8_t *pDestination = &Output.m_pData[(y * Width + x) * 4];
			for(size_t Channel = 0; Channel < 3; ++Channel)
			{
				const float Top = mix(
					Source.m_pData[(static_cast<size_t>(Y0) * Source.m_Width + X0) * SourcePixelSize + Channel],
					Source.m_pData[(static_cast<size_t>(Y0) * Source.m_Width + X1) * SourcePixelSize + Channel],
					Fx);
				const float Bottom = mix(
					Source.m_pData[(static_cast<size_t>(Y1) * Source.m_Width + X0) * SourcePixelSize + Channel],
					Source.m_pData[(static_cast<size_t>(Y1) * Source.m_Width + X1) * SourcePixelSize + Channel],
					Fx);
				pDestination[Channel] = static_cast<uint8_t>(mix(Top, Bottom, Fy));
			}
			pDestination[3] = 255;
		}
	}

	static constexpr int s_aKernel[] = {1, 4, 7, 10, 13, 10, 7, 4, 1};
	static constexpr int KernelRadius = static_cast<int>(std::size(s_aKernel)) / 2;
	static constexpr int KernelWeight = 67;
	std::vector<uint8_t> vTemporary(Output.DataSize());
	for(int Iteration = 0; Iteration < 2; ++Iteration)
	{
		for(size_t y = 0; y < Height; ++y)
		{
			for(size_t x = 0; x < Width; ++x)
			{
				for(size_t Channel = 0; Channel < 4; ++Channel)
				{
					int Sum = 0;
					for(int Offset = -KernelRadius; Offset <= KernelRadius; ++Offset)
					{
						const size_t SampleX = std::clamp(static_cast<int>(x) + Offset, 0, static_cast<int>(Width) - 1);
						Sum += Output.m_pData[(y * Width + SampleX) * 4 + Channel] * s_aKernel[Offset + KernelRadius];
					}
					vTemporary[(y * Width + x) * 4 + Channel] = static_cast<uint8_t>(Sum / KernelWeight);
				}
			}
		}
		for(size_t y = 0; y < Height; ++y)
		{
			for(size_t x = 0; x < Width; ++x)
			{
				for(size_t Channel = 0; Channel < 4; ++Channel)
				{
					int Sum = 0;
					for(int Offset = -KernelRadius; Offset <= KernelRadius; ++Offset)
					{
						const size_t SampleY = std::clamp(static_cast<int>(y) + Offset, 0, static_cast<int>(Height) - 1);
						Sum += vTemporary[(SampleY * Width + x) * 4 + Channel] * s_aKernel[Offset + KernelRadius];
					}
					Output.m_pData[(y * Width + x) * 4 + Channel] = static_cast<uint8_t>(Sum / KernelWeight);
				}
			}
		}
	}

	return true;
}

struct SAmfClientInfoPopupContext : public SPopupMenuId
{
	CMenus *m_pMenus = nullptr;
	CUi *m_pUi = nullptr;
	IClient *m_pClient = nullptr;
	ITextRender *m_pTextRender = nullptr;
	CScrollRegion m_ScrollRegion;
	CButtonContainer m_aNavButtons[std::size(gs_aAmfClientInfoNavItems)];
	CButtonContainer m_CloseIconButton;
	CButtonContainer m_CloseButton;
	CButtonContainer m_ShowOnStartButton;
	CUIRect m_PopupRect;
	IGraphics::CTextureHandle m_BlurredBackground;
	CSmoothUiSectionAnimation m_VisibilityAnimation;
	float m_aSectionScrollY[std::size(gs_aAmfClientInfoNavItems)] = {};
	float m_aSectionHeights[std::size(gs_aAmfClientInfoNavItems)] = {};
	float m_LastScrollY = 0.0f;
	float m_ContentHeight = 0.0f;
	float m_ViewportHeight = 0.0f;
	float m_VisualOpacity = 1.0f;
	float m_ScrollAnimationFrom = 0.0f;
	float m_ScrollAnimationTarget = 0.0f;
	float m_ScrollAnimationStart = 0.0f;
	float m_HighlightStart = 0.0f;
	int m_SelectedSection = 0;
	int m_HighlightSection = -1;
	bool m_Closing = false;
	bool m_ScrollAnimationActive = false;
	bool m_CaptureBlur = false;
	int m_CaptureAttempts = 0;
	int m_CaptureDelayFrames = 0;
	int m_CapturedScreenWidth = 0;
	int m_CapturedScreenHeight = 0;
};

SAmfClientInfoPopupContext s_AmfClientInfoPopup;

void RenderAmfInfoLabel(SAmfClientInfoPopupContext *pPopup, const CUIRect &Rect, const char *pText, float FontSize, int Align, ColorRGBA Color, bool IconFont = false)
{
	if(pText == nullptr || pText[0] == '\0')
		return;

	const unsigned OldRenderFlags = pPopup->m_pTextRender->GetRenderFlags();
	const ColorRGBA OldTextColor = pPopup->m_pTextRender->GetTextColor();
	const ColorRGBA OldOutlineColor = pPopup->m_pTextRender->GetTextOutlineColor();
	if(IconFont)
		pPopup->m_pTextRender->SetFontPreset(EFontPreset::ICON_FONT);
	pPopup->m_pTextRender->SetRenderFlags((OldRenderFlags & ~TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT) | TEXT_RENDER_FLAG_ONE_TIME_USE);
	pPopup->m_pTextRender->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
	pPopup->m_pTextRender->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f));

	float RenderFontSize = FontSize;
	while(RenderFontSize > 5.0f && pPopup->m_pTextRender->TextWidth(RenderFontSize, pText) > Rect.w)
		RenderFontSize -= 1.0f;
	const STextBoundingBox TextBounds = pPopup->m_pTextRender->TextBoundingBox(RenderFontSize, pText);
	const vec2 CursorPosition = CUi::CalcAlignedCursorPos(&Rect, TextBounds.Size(), Align);
	const float PixelSize = pPopup->m_pUi->PixelSize();
	const auto SnapToPixel = [PixelSize](float Value) { return std::round(Value / PixelSize) * PixelSize; };
	CTextCursor Cursor;
	Cursor.SetPosition(vec2(SnapToPixel(CursorPosition.x), SnapToPixel(CursorPosition.y)));
	Cursor.m_FontSize = RenderFontSize;
	STextContainerIndex TextContainer;
	pPopup->m_pTextRender->CreateTextContainer(TextContainer, &Cursor, pText);
	pPopup->m_pTextRender->SetRenderFlags(OldRenderFlags);
	if(TextContainer.Valid())
	{
		pPopup->m_pTextRender->RenderTextContainer(TextContainer, Color.WithMultipliedAlpha(pPopup->m_VisualOpacity), ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f));
		pPopup->m_pTextRender->DeleteTextContainer(TextContainer);
	}
	pPopup->m_pTextRender->TextColor(OldTextColor);
	pPopup->m_pTextRender->TextOutlineColor(OldOutlineColor);
	if(IconFont)
		pPopup->m_pTextRender->SetFontPreset(EFontPreset::DEFAULT_FONT);
}

void RenderAmfInfoIcon(SAmfClientInfoPopupContext *pPopup, const CUIRect &Rect, const char *pIcon, float Size, ColorRGBA Color)
{
	RenderAmfInfoLabel(pPopup, Rect, pIcon, Size, TEXTALIGN_MC, Color, true);
}

void DrawAmfGlassCircle(CUIRect Rect, ColorRGBA Background, ColorRGBA Border, float Opacity)
{
	const float Diameter = std::min(Rect.w, Rect.h);
	Rect.x += (Rect.w - Diameter) / 2.0f;
	Rect.y += (Rect.h - Diameter) / 2.0f;
	Rect.w = Rect.h = Diameter;
	Rect.Draw(Border.WithMultipliedAlpha(Opacity), IGraphics::CORNER_ALL, Diameter / 2.0f);
	Rect.Margin(1.0f, &Rect);
	Rect.Draw(Background.WithMultipliedAlpha(Opacity), IGraphics::CORNER_ALL, Rect.w / 2.0f);
}

float RenderAmfInfoWrappedText(SAmfClientInfoPopupContext *pPopup, const char *pText, float X, float Y, float Width, float FontSize, ColorRGBA Color)
{
	CTextCursor Cursor;
	Cursor.m_FontSize = FontSize;
	Cursor.m_LineWidth = Width;
	STextContainerIndex TextContainer;
	const unsigned OldRenderFlags = pPopup->m_pTextRender->GetRenderFlags();
	const ColorRGBA OldTextColor = pPopup->m_pTextRender->GetTextColor();
	const ColorRGBA OldOutlineColor = pPopup->m_pTextRender->GetTextOutlineColor();
	pPopup->m_pTextRender->SetRenderFlags((OldRenderFlags & ~TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT) | TEXT_RENDER_FLAG_ONE_TIME_USE);
	pPopup->m_pTextRender->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
	pPopup->m_pTextRender->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f));
	pPopup->m_pTextRender->CreateTextContainer(TextContainer, &Cursor, pText);
	pPopup->m_pTextRender->SetRenderFlags(OldRenderFlags);
	pPopup->m_pTextRender->TextColor(OldTextColor);
	pPopup->m_pTextRender->TextOutlineColor(OldOutlineColor);
	if(!TextContainer.Valid())
		return 0.0f;

	const float Height = pPopup->m_pTextRender->GetBoundingBoxTextContainer(TextContainer).m_H;
	const float PixelSize = pPopup->m_pUi->PixelSize();
	const auto SnapToPixel = [PixelSize](float Value) { return std::round(Value / PixelSize) * PixelSize; };
	pPopup->m_pTextRender->RenderTextContainer(TextContainer, Color.WithMultipliedAlpha(pPopup->m_VisualOpacity), ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f), SnapToPixel(X), SnapToPixel(Y));
	pPopup->m_pTextRender->DeleteTextContainer(TextContainer);
	return Height;
}

float RenderAmfInfoCard(SAmfClientInfoPopupContext *pPopup, CUIRect &Flow, const SAmfClientInfoSection &Section, size_t SectionIndex, float Highlight)
{
	const ColorRGBA Accent = AmfInfoAccentColor(SectionIndex);
	const char *pTitle = TCLocalize(Section.m_pTitle, "AMF Client Info");
	const char *pText = TCLocalize(Section.m_pText, "AMF Client Info");
	const char *pMeta = Section.m_pMeta ? TCLocalize(Section.m_pMeta, "AMF Client Info") : nullptr;
	const float CardMargin = 9.0f;
	const float IconColumnWidth = 35.0f;
	const float BodyFontSize = 9.0f;
	const float MetaFontSize = 8.0f;
	const float TitleHeight = 13.0f;
	const float TitleBodyGap = 2.0f;
	const float BodyMetaGap = 2.5f;
	const float TextWidth = std::max(1.0f, Flow.w - CardMargin * 2.0f - IconColumnWidth);

	const auto MeasureWrappedText = [&](const char *pValue, float FontSize) {
		if(pValue == nullptr || pValue[0] == '\0')
			return 0.0f;
		CTextCursor MeasureCursor;
		MeasureCursor.m_FontSize = FontSize;
		MeasureCursor.m_LineWidth = TextWidth;
		STextContainerIndex MeasureContainer;
		const unsigned OldRenderFlags = pPopup->m_pTextRender->GetRenderFlags();
		pPopup->m_pTextRender->SetRenderFlags((OldRenderFlags & ~TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT) | TEXT_RENDER_FLAG_ONE_TIME_USE);
		pPopup->m_pTextRender->CreateTextContainer(MeasureContainer, &MeasureCursor, pValue);
		pPopup->m_pTextRender->SetRenderFlags(OldRenderFlags);
		const float Height = MeasureContainer.Valid() ? pPopup->m_pTextRender->GetBoundingBoxTextContainer(MeasureContainer).m_H : 0.0f;
		if(MeasureContainer.Valid())
			pPopup->m_pTextRender->DeleteTextContainer(MeasureContainer);
		return Height;
	};
	const float BodyHeight = MeasureWrappedText(pText, BodyFontSize);
	const float MetaHeight = MeasureWrappedText(pMeta, MetaFontSize);

	const float CardHeight = std::max(58.0f, CardMargin * 2.0f + TitleHeight + TitleBodyGap + BodyHeight + (MetaHeight > 0.0f ? BodyMetaGap + MetaHeight : 0.0f));
	CUIRect Card = Flow;
	Card.h = CardHeight;
	CUIRect ScrollExtent = Card;
	if(SectionIndex + 1 == std::size(gs_aAmfClientInfoSections))
		ScrollExtent.h = std::max(0.0f, ScrollExtent.h - 1.0f);
	pPopup->m_ScrollRegion.AddRect(ScrollExtent);
	DrawAmfGlassPanel(
		Card,
		ColorRGBA(0.985f, 0.991f, 1.0f, 1.0f),
		ColorRGBA(0.80f, 0.86f, 0.96f, 0.84f),
		2.0f,
		IGraphics::CORNER_ALL,
		pPopup->m_VisualOpacity);
	DrawAmfInfoSectionHighlight(Card, Highlight, pPopup->m_VisualOpacity);

	CUIRect IconBox = Card;
	IconBox.x += CardMargin;
	IconBox.y += CardMargin;
	IconBox.w = 25.0f;
	IconBox.h = 25.0f;
	DrawAmfGlassPanel(IconBox, ColorRGBA(0.95f, 0.975f, 1.0f, 1.0f), ColorRGBA(0.84f, 0.90f, 0.99f, 0.78f), 2.0f, IGraphics::CORNER_ALL, pPopup->m_VisualOpacity);
	RenderAmfInfoIcon(pPopup, IconBox, Section.m_pIcon, 12.0f, Accent);

	CUIRect Title = Card;
	Title.x += CardMargin + IconColumnWidth;
	Title.y += CardMargin - 0.25f;
	Title.w = TextWidth;
	Title.h = TitleHeight;
	RenderAmfInfoLabel(pPopup, Title, pTitle, 11.0f, TEXTALIGN_ML, ColorRGBA(0.12f, 0.15f, 0.20f, 1.0f));
	const float BodyY = Title.y + Title.h + TitleBodyGap;
	RenderAmfInfoWrappedText(pPopup, pText, Title.x, BodyY, TextWidth, BodyFontSize, ColorRGBA(0.39f, 0.43f, 0.49f, 1.0f));
	if(MetaHeight > 0.0f)
		RenderAmfInfoWrappedText(pPopup, pMeta, Title.x, BodyY + BodyHeight + BodyMetaGap, TextWidth, MetaFontSize, ColorRGBA(0.24f, 0.50f, 0.86f, 1.0f));

	Flow.y += CardHeight + 5.0f;
	return CardHeight;
}

CUi::EPopupMenuFunctionResult RenderAmfClientInfoPopup(void *pContext, CUIRect View, bool Active)
{
	auto *pPopup = static_cast<SAmfClientInfoPopupContext *>(pContext);
	CUIRect Header, HeaderIcon, CloseHitbox, CloseIcon, Content, Sidebar, MainPanel, Footer, CheckBox, CloseButton;
	const float CurrentTime = pPopup->m_pClient->GlobalTime();
	constexpr float ScrollAnimationDuration = 0.58f;
	constexpr float HighlightDuration = 0.82f;
	const bool AnimatePopup = g_Config.m_AmfSmoothHud != 0;
	const float PopupProgress = pPopup->m_VisibilityAnimation.Update(!pPopup->m_Closing, pPopup->m_pClient->RenderFrameTime(), AMF_INFO_POPUP_ANIMATION_DURATION, AnimatePopup);
	pPopup->m_VisualOpacity = PopupProgress;
	// Smooth HUD only fades this popup. Its geometry remains fixed for the
	// whole open/close lifetime, so no scale or position interpolation can
	// pull it towards a screen corner.
	View = pPopup->m_PopupRect;
	const bool PopupInteractive = Active && pPopup->m_VisibilityAnimation.AcceptsInput();

	CUIRect Window = View;
	Window.Draw(ColorRGBA(0.70f, 0.79f, 0.93f, 0.90f).WithMultipliedAlpha(pPopup->m_VisualOpacity), IGraphics::CORNER_NONE, 0.0f);
	// Both edges are snapped when the popup opens, so one physical pixel is
	// enough for an even border on every side at every UI scale.
	Window.Margin(pPopup->m_pUi->PixelSize(), &Window);
	Window.Draw(ColorRGBA(0.989f, 0.993f, 0.998f, 0.995f).WithMultipliedAlpha(pPopup->m_VisualOpacity), IGraphics::CORNER_NONE, 0.0f);

	float HighlightAmount = 0.0f;
	if(pPopup->m_HighlightSection >= 0)
	{
		const float Progress = (CurrentTime - pPopup->m_HighlightStart) / HighlightDuration;
		if(Progress >= 1.0f)
			pPopup->m_HighlightSection = -1;
		else
			HighlightAmount = std::sin(std::clamp(Progress, 0.0f, 1.0f) * pi);
	}

	View.HSplitTop(40.0f, &Header, &View);
	HeaderIcon = CUIRect{Header.x + 4.0f, Header.y + (Header.h - 28.0f) / 2.0f, 28.0f, 28.0f};
	CloseHitbox = CUIRect{Header.x + Header.w - 34.0f, Header.y + (Header.h - 30.0f) / 2.0f, 30.0f, 30.0f};
	CloseIcon = CloseHitbox;
	CloseIcon.Margin(3.0f, &CloseIcon);
	Header.x = HeaderIcon.x + HeaderIcon.w + 8.0f;
	Header.w = CloseHitbox.x - Header.x - 6.0f;
	DrawAmfGlassPanel(HeaderIcon, ColorRGBA(0.95f, 0.975f, 1.0f, 1.0f), ColorRGBA(0.73f, 0.84f, 1.0f, 0.90f), 2.0f, IGraphics::CORNER_ALL, pPopup->m_VisualOpacity);
	RenderAmfInfoIcon(pPopup, HeaderIcon, FontIcon::LOCK, 12.5f, ColorRGBA(0.28f, 0.55f, 0.96f, 1.0f));
	RenderAmfInfoLabel(pPopup, Header, CLIENT_NAME " " CLIENT_RELEASE_VERSION_DISPLAY, 16.0f, TEXTALIGN_ML, ColorRGBA(0.09f, 0.12f, 0.17f, 1.0f));

	const bool CloseIconClicked = PopupInteractive && pPopup->m_pUi->DoButtonLogic(&pPopup->m_CloseIconButton, 0, &CloseHitbox, BUTTONFLAG_LEFT);
	const bool CloseIconHovered = pPopup->m_pUi->HotItem() == &pPopup->m_CloseIconButton;
	DrawAmfGlassPanel(
		CloseIcon,
		CloseIconHovered ? ColorRGBA(0.92f, 0.96f, 1.0f, 1.0f) : ColorRGBA(0.98f, 0.99f, 1.0f, 1.0f),
		CloseIconHovered ? ColorRGBA(0.58f, 0.75f, 1.0f, 0.98f) : ColorRGBA(0.74f, 0.84f, 0.98f, 0.90f),
		2.0f,
		IGraphics::CORNER_ALL,
		pPopup->m_VisualOpacity);
	RenderAmfInfoIcon(pPopup, CloseIcon, FontIcon::XMARK, 11.5f, ColorRGBA(0.23f, 0.50f, 0.96f, 1.0f));
	bool RequestClose = CloseIconClicked;

	CUIRect HeaderDivider = View;
	HeaderDivider.h = 0.6f;
	HeaderDivider.Draw(ColorRGBA(0.78f, 0.84f, 0.93f, 0.72f).WithMultipliedAlpha(pPopup->m_VisualOpacity), IGraphics::CORNER_NONE, 0.0f);
	View.HSplitTop(5.0f, nullptr, &View);
	View.HSplitBottom(40.0f, &Content, &Footer);
	const float SidebarWidth = std::min(162.0f, Content.w * 0.255f);
	Content.VSplitLeft(SidebarWidth, &Sidebar, &MainPanel);
	CUIRect SidebarDivider = MainPanel;
	SidebarDivider.x -= 0.3f;
	SidebarDivider.y = Content.y;
	SidebarDivider.w = 0.6f;
	SidebarDivider.h = Content.h;
	SidebarDivider.Draw(ColorRGBA(0.80f, 0.86f, 0.94f, 0.68f).WithMultipliedAlpha(pPopup->m_VisualOpacity), IGraphics::CORNER_NONE, 0.0f);
	MainPanel.VSplitLeft(12.0f, nullptr, &MainPanel);
	MainPanel.VSplitRight(7.0f, &MainPanel, nullptr);

	Sidebar.Margin(6.0f, &Sidebar);
	constexpr float NavItemSpacing = 4.0f;
	const float NavItemHeight = std::min(
		44.0f,
		std::max(36.0f, (Sidebar.h - NavItemSpacing * (std::size(gs_aAmfClientInfoNavItems) - 1)) / std::size(gs_aAmfClientInfoNavItems)));
	for(size_t Index = 0; Index < std::size(gs_aAmfClientInfoNavItems); ++Index)
	{
		CUIRect Item;
		Sidebar.HSplitTop(NavItemHeight, &Item, &Sidebar);
		if(PopupInteractive && !RequestClose && pPopup->m_pUi->DoButtonLogic(&pPopup->m_aNavButtons[Index], 0, &Item, BUTTONFLAG_LEFT))
		{
			const float MaxScroll = std::max(0.0f, pPopup->m_ContentHeight - pPopup->m_ViewportHeight);
			pPopup->m_SelectedSection = static_cast<int>(Index);
			pPopup->m_ScrollAnimationFrom = pPopup->m_LastScrollY;
			const float CenteredScrollY = pPopup->m_aSectionScrollY[Index] +
						     pPopup->m_aSectionHeights[Index] / 2.0f -
						     pPopup->m_ViewportHeight / 2.0f;
			pPopup->m_ScrollAnimationTarget = std::clamp(CenteredScrollY, 0.0f, MaxScroll);
			pPopup->m_ScrollAnimationStart = CurrentTime;
			pPopup->m_ScrollAnimationActive = absolute(pPopup->m_ScrollAnimationFrom - pPopup->m_ScrollAnimationTarget) >= 0.5f;
			if(!pPopup->m_ScrollAnimationActive)
			{
				pPopup->m_HighlightSection = static_cast<int>(Index);
				pPopup->m_HighlightStart = CurrentTime;
			}
		}
		const bool Selected = static_cast<int>(Index) == pPopup->m_SelectedSection;
		const bool Hovered = pPopup->m_pUi->HotItem() == &pPopup->m_aNavButtons[Index];
		const ColorRGBA Accent = AmfInfoNavAccentColor(Index);
		if(Selected)
		{
			DrawAmfGlassPanel(
				Item,
				ColorRGBA(0.925f, 0.963f, 1.0f, 1.0f),
				ColorRGBA(0.69f, 0.82f, 1.0f, 0.94f),
				2.0f,
				IGraphics::CORNER_R,
				pPopup->m_VisualOpacity);
		}
		else if(Hovered)
		{
			DrawAmfGlassPanel(
				Item,
				ColorRGBA(0.975f, 0.987f, 1.0f, 1.0f),
				ColorRGBA(0.83f, 0.90f, 0.99f, 0.76f),
				2.0f,
				IGraphics::CORNER_ALL,
				pPopup->m_VisualOpacity);
		}
		if(Selected)
		{
			CUIRect ActiveLine = Item;
			// Keep the accent strictly inside the selected item's border.
			ActiveLine.x += 0.6f;
			ActiveLine.y += 0.6f;
			ActiveLine.w = 2.0f;
			ActiveLine.h = std::max(0.0f, ActiveLine.h - 1.2f);
			ActiveLine.Draw(Accent.WithMultipliedAlpha(pPopup->m_VisualOpacity), IGraphics::CORNER_NONE, 0.0f);
		}

		CUIRect Icon, ItemText, ItemTitle, ItemSubtitle;
		Item.VSplitLeft(34.0f, &Icon, &ItemText);
		Icon.Margin(7.0f, &Icon);
		RenderAmfInfoIcon(pPopup, Icon, gs_aAmfClientInfoNavItems[Index].m_pIcon, 13.0f, Accent);
		ItemText.VSplitRight(3.0f, &ItemText, nullptr);
		ItemText.HSplitTop(21.0f, &ItemTitle, &ItemSubtitle);
		ItemTitle.y += 3.5f;
		ItemSubtitle.y -= 1.0f;
		const float NavHighlight = pPopup->m_HighlightSection == static_cast<int>(Index) ? HighlightAmount : 0.0f;
		const ColorRGBA ItemTitleBase = Selected ? ColorRGBA(0.10f, 0.15f, 0.23f, 1.0f) : ColorRGBA(0.19f, 0.24f, 0.31f, 1.0f);
		const ColorRGBA ItemTitleColor(
			mix(ItemTitleBase.r, 0.18f, NavHighlight),
			mix(ItemTitleBase.g, 0.46f, NavHighlight),
			mix(ItemTitleBase.b, 0.90f, NavHighlight),
			1.0f);
		const ColorRGBA ItemSubtitleColor(
			mix(0.45f, 0.30f, NavHighlight),
			mix(0.48f, 0.56f, NavHighlight),
			mix(0.54f, 0.93f, NavHighlight),
			1.0f);
		RenderAmfInfoLabel(pPopup, ItemTitle, TCLocalize(gs_aAmfClientInfoNavItems[Index].m_pTitle, "AMF Client Info Navigation"), 9.0f, TEXTALIGN_ML, ItemTitleColor);
		RenderAmfInfoLabel(pPopup, ItemSubtitle, TCLocalize(gs_aAmfClientInfoNavItems[Index].m_pSubtitle, "AMF Client Info Navigation"), 8.0f, TEXTALIGN_ML, ItemSubtitleColor);

		if(Index + 1 < std::size(gs_aAmfClientInfoNavItems))
		{
			CUIRect DividerSpace;
			Sidebar.HSplitTop(NavItemSpacing, &DividerSpace, &Sidebar);
			CUIRect Divider = DividerSpace;
			Divider.x += 2.5f;
			Divider.w -= 5.0f;
			Divider.y += (Divider.h - 0.5f) / 2.0f;
			Divider.h = 0.5f;
			Divider.Draw(ColorRGBA(0.80f, 0.86f, 0.95f, 0.64f).WithMultipliedAlpha(pPopup->m_VisualOpacity), IGraphics::CORNER_NONE, 0.0f);
		}
	}

	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 26.0f;
	ScrollParams.m_ForceShowScrollbar = true;
	ScrollParams.m_ScrollbarThickness = 4.5f;
	ScrollParams.m_ScrollbarMargin = 1.0f;
	ScrollParams.m_SliderMinSize = 22.0f;
	ScrollParams.m_RailBgColor = ColorRGBA(0.82f, 0.88f, 0.97f, 0.40f).WithMultipliedAlpha(pPopup->m_VisualOpacity);
	ScrollParams.m_SliderColor = ColorRGBA(0.53f, 0.70f, 0.98f, 0.72f).WithMultipliedAlpha(pPopup->m_VisualOpacity);
	ScrollParams.m_SliderColorHover = ColorRGBA(0.39f, 0.62f, 0.98f, 0.88f).WithMultipliedAlpha(pPopup->m_VisualOpacity);
	ScrollParams.m_SliderColorGrabbed = ColorRGBA(0.29f, 0.54f, 0.97f, 0.96f).WithMultipliedAlpha(pPopup->m_VisualOpacity);
	pPopup->m_ViewportHeight = MainPanel.h;
	const CUIRect UnscrolledMainPanel = MainPanel;
	pPopup->m_ScrollRegion.Begin(&MainPanel, &ScrollParams);
	const vec2 ScrollOffset(MainPanel.x - UnscrolledMainPanel.x, MainPanel.y - UnscrolledMainPanel.y);
	const float CurrentScrollY = -ScrollOffset.y;
	pPopup->m_LastScrollY = CurrentScrollY;
	float RequestedScrollPosition = 0.0f;
	bool RequestAnimatedScroll = false;
	if(pPopup->m_ScrollAnimationActive)
	{
		const float Progress = std::clamp((CurrentTime - pPopup->m_ScrollAnimationStart) / ScrollAnimationDuration, 0.0f, 1.0f);
		const float EaseOut = 1.0f - std::pow(1.0f - Progress, 3.0f);
		const float DesiredScrollY = mix(pPopup->m_ScrollAnimationFrom, pPopup->m_ScrollAnimationTarget, EaseOut);
		RequestedScrollPosition = DesiredScrollY;
		RequestAnimatedScroll = true;
		if(Progress >= 1.0f)
		{
			pPopup->m_ScrollAnimationActive = false;
			pPopup->m_HighlightSection = pPopup->m_SelectedSection;
			pPopup->m_HighlightStart = CurrentTime;
		}
	}
	CUIRect Flow = MainPanel;
	Flow.VSplitRight(4.5f, &Flow, nullptr);
	const float ContentStartY = Flow.y - ScrollOffset.y;
	const float IntroHeight = 52.0f;
	pPopup->m_aSectionScrollY[0] = 0.0f;
	pPopup->m_aSectionHeights[0] = IntroHeight;

	CUIRect Intro;
	Flow.HSplitTop(IntroHeight, &Intro, &Flow);
	pPopup->m_ScrollRegion.AddRect(Intro);
	if(pPopup->m_HighlightSection == 0)
		DrawAmfInfoSectionHighlight(Intro, HighlightAmount, pPopup->m_VisualOpacity);
	CUIRect IntroIcon, IntroText, IntroTitle, IntroSubtitle;
	Intro.VSplitLeft(45.0f, &IntroIcon, &IntroText);
	IntroIcon = CUIRect{IntroIcon.x + 5.0f, IntroIcon.y + (IntroIcon.h - 32.0f) / 2.0f, 32.0f, 32.0f};
	DrawAmfGlassCircle(IntroIcon, ColorRGBA(0.53f, 0.71f, 1.0f, 1.0f), ColorRGBA(0.67f, 0.81f, 1.0f, 1.0f), pPopup->m_VisualOpacity);
	RenderAmfInfoIcon(pPopup, IntroIcon, FontIcon::INFO, 15.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
	IntroText.HSplitTop(25.0f, &IntroTitle, &IntroSubtitle);
	RenderAmfInfoLabel(pPopup, IntroTitle, TCLocalize("User agreement", "AMF Client Info"), 15.0f, TEXTALIGN_ML, ColorRGBA(0.10f, 0.13f, 0.18f, 1.0f));
	RenderAmfInfoLabel(pPopup, IntroSubtitle, TCLocalize("Before using AMF Client, please read the information below carefully.", "AMF Client Info"), 9.0f, TEXTALIGN_ML, ColorRGBA(0.43f, 0.46f, 0.51f, 1.0f));
	Flow.y += 5.0f;
	for(size_t Index = 0; Index < std::size(gs_aAmfClientInfoSections); ++Index)
	{
		for(size_t NavIndex = 1; NavIndex < std::size(gs_aAmfClientInfoNavSectionIndices); ++NavIndex)
		{
			if(gs_aAmfClientInfoNavSectionIndices[NavIndex] == static_cast<int>(Index))
				pPopup->m_aSectionScrollY[NavIndex] = Flow.y - ScrollOffset.y - ContentStartY;
		}
		float CardHighlight = 0.0f;
		if(pPopup->m_HighlightSection >= 1 &&
			gs_aAmfClientInfoNavSectionIndices[pPopup->m_HighlightSection] == static_cast<int>(Index))
			CardHighlight = HighlightAmount;
		const float CardHeight = RenderAmfInfoCard(pPopup, Flow, gs_aAmfClientInfoSections[Index], Index, CardHighlight);
		for(size_t NavIndex = 1; NavIndex < std::size(gs_aAmfClientInfoNavSectionIndices); ++NavIndex)
		{
			if(gs_aAmfClientInfoNavSectionIndices[NavIndex] == static_cast<int>(Index))
				pPopup->m_aSectionHeights[NavIndex] = CardHeight;
		}
	}
	pPopup->m_ContentHeight = std::max(0.0f, Flow.y - ScrollOffset.y - ContentStartY - 5.0f);
	if(RequestAnimatedScroll)
		pPopup->m_ScrollRegion.ScrollToDirect(RequestedScrollPosition);
	pPopup->m_ScrollRegion.End();

	CUIRect FooterDivider = Footer;
	FooterDivider.h = 0.6f;
	FooterDivider.Draw(ColorRGBA(0.78f, 0.84f, 0.93f, 0.72f).WithMultipliedAlpha(pPopup->m_VisualOpacity), IGraphics::CORNER_NONE, 0.0f);
	Footer.HSplitTop(0.6f, nullptr, &Footer);
	Footer.Margin(8.0f, &Footer);
	const char *pHideOnStartLabel = TCLocalize("Do not show on startup", "AMF Client Info");
	const float CheckBoxBlockWidth = std::min(
		Footer.w * 0.38f,
		16.0f + 6.0f + pPopup->m_pTextRender->TextWidth(9.0f, pHideOnStartLabel) + 6.0f);
	Footer.VSplitLeft(CheckBoxBlockWidth, &CheckBox, &CloseButton);
	CheckBox.VSplitRight(6.0f, &CheckBox, nullptr);
	CloseButton.VSplitLeft(8.0f, nullptr, &CloseButton);

	const bool HideOnStart = !g_Config.m_AmfShowClientInfoOnStart;
	if(PopupInteractive && !RequestClose && pPopup->m_pUi->DoButtonLogic(&pPopup->m_ShowOnStartButton, HideOnStart, &CheckBox, BUTTONFLAG_LEFT))
		g_Config.m_AmfShowClientInfoOnStart ^= 1;
	CUIRect CheckRow, CheckMark, CheckLabel;
	CheckBox.HMargin(std::max(0.0f, (CheckBox.h - 16.0f) / 2.0f), &CheckRow);
	CheckRow.VSplitLeft(16.0f, &CheckMark, &CheckLabel);
	CheckLabel.VSplitLeft(6.0f, nullptr, &CheckLabel);
	const bool CheckHovered = pPopup->m_pUi->HotItem() == &pPopup->m_ShowOnStartButton;
	DrawAmfGlassPanel(
		CheckMark,
		HideOnStart ? ColorRGBA(0.24f, 0.54f, 0.98f, 1.0f) : ColorRGBA(0.98f, 0.99f, 1.0f, 1.0f),
		HideOnStart ? ColorRGBA(0.24f, 0.54f, 0.98f, 1.0f) : ColorRGBA(0.66f, 0.78f, 0.96f, CheckHovered ? 0.98f : 0.84f),
		0.0f,
		IGraphics::CORNER_NONE,
		pPopup->m_VisualOpacity);
	if(HideOnStart)
		RenderAmfInfoIcon(pPopup, CheckMark, "\uF00C", 9.0f, ColorRGBA(0.95f, 0.97f, 1.0f, 1.0f));
	RenderAmfInfoLabel(pPopup, CheckLabel, pHideOnStartLabel, 9.0f, TEXTALIGN_ML, ColorRGBA(0.42f, 0.45f, 0.50f, CheckHovered ? 1.0f : 0.90f));

	const bool CloseButtonClicked = PopupInteractive && !RequestClose && pPopup->m_pUi->DoButtonLogic(&pPopup->m_CloseButton, 0, &CloseButton, BUTTONFLAG_LEFT);
	const bool CloseButtonHovered = pPopup->m_pUi->HotItem() == &pPopup->m_CloseButton;
	const char *pCloseKey = "Backspace";
	const char *pCloseSuffix = TCLocalize("- close", "AMF Client Info");
	constexpr float CloseTextSize = 10.0f;
	const float CloseKeyWidth = pPopup->m_pTextRender->TextWidth(CloseTextSize, pCloseKey);
	const float CloseSuffixWidth = pPopup->m_pTextRender->TextWidth(CloseTextSize, pCloseSuffix);
	const float CloseTextWidth = CloseKeyWidth + 4.0f + CloseSuffixWidth;
	const float CloseTextX = CloseButton.x + (CloseButton.w - CloseTextWidth) / 2.0f;
	CUIRect CloseKeyLabel{CloseTextX, CloseButton.y, CloseKeyWidth, CloseButton.h};
	CUIRect CloseSuffixLabel{CloseTextX + CloseKeyWidth + 4.0f, CloseButton.y, CloseSuffixWidth, CloseButton.h};
	RenderAmfInfoLabel(pPopup, CloseKeyLabel, pCloseKey, CloseTextSize, TEXTALIGN_ML, CloseButtonHovered ? ColorRGBA(0.18f, 0.46f, 0.90f, 1.0f) : ColorRGBA(0.23f, 0.50f, 0.94f, 1.0f));
	RenderAmfInfoLabel(pPopup, CloseSuffixLabel, pCloseSuffix, CloseTextSize, TEXTALIGN_ML, ColorRGBA(0.42f, 0.45f, 0.50f, 1.0f));
	RequestClose = RequestClose || CloseButtonClicked || (PopupInteractive && pPopup->m_pUi->ConsumeHotkey(CUi::HOTKEY_BACKSPACE));
	if(RequestClose)
	{
		pPopup->m_Closing = true;
		pPopup->m_VisibilityAnimation.Update(false, 0.0f, AMF_INFO_POPUP_ANIMATION_DURATION, AnimatePopup);
	}

	const bool CloseAnimationFinished = pPopup->m_Closing && !pPopup->m_VisibilityAnimation.IsVisible();

	return CloseAnimationFinished ? CUi::POPUP_CLOSE_CURRENT : CUi::POPUP_KEEP_OPEN;
}

void ApplyRecommendedAmfAnimationPreset()
{
	g_Config.m_AmfAnimMenu = 1;
	g_Config.m_AmfAnimStartMenu = 1;
	g_Config.m_AmfAnimChatOpen = 1;
	g_Config.m_AmfAnimText = 1;
	g_Config.m_AmfAnimBlur = 0;
	g_Config.m_AmfAnimSlide = 1;
	g_Config.m_AmfAnimDuration = 220;
	g_Config.m_AmfAnimBlurRadius = 2;
	g_Config.m_AmfAnimSlideDistance = 4;
	g_Config.m_AmfAnimSmoothCaret = 1;
	g_Config.m_AmfAnimCaretSpeed = 75;
	g_Config.m_AmfAnimCaretWidth = 1;
	g_Config.m_AmfAnimChatMessages = 1;
	g_Config.m_AmfAnimChatMessageDuration = 300;
	g_Config.m_AmfAnimScoreboard = 1;
	g_Config.m_AmfAnimScoreboardOpenDuration = 125;
	g_Config.m_AmfAnimScoreboardCloseDuration = 85;
	g_Config.m_AmfAnimScoreboardSlideDistance = 12;
}
}

void CMenus::OpenAmfClientInfoPopup()
{
	if(Ui()->IsPopupOpen(&s_AmfClientInfoPopup))
		return;

	s_AmfClientInfoPopup.m_pMenus = this;
	s_AmfClientInfoPopup.m_pUi = Ui();
	s_AmfClientInfoPopup.m_pClient = Client();
	s_AmfClientInfoPopup.m_pTextRender = TextRender();
	s_AmfClientInfoPopup.m_ScrollRegion.Reset();
	s_AmfClientInfoPopup.m_LastScrollY = 0.0f;
	s_AmfClientInfoPopup.m_ContentHeight = 0.0f;
	s_AmfClientInfoPopup.m_ViewportHeight = 0.0f;
	s_AmfClientInfoPopup.m_SelectedSection = 0;
	s_AmfClientInfoPopup.m_HighlightSection = -1;
	s_AmfClientInfoPopup.m_ScrollAnimationActive = false;
	s_AmfClientInfoPopup.m_Closing = false;
	s_AmfClientInfoPopup.m_VisualOpacity = 1.0f;
	// Initialize the existing Smooth HUD transition state as closed so opening
	// this popup can use the same animation path as the AMF settings sections.
	s_AmfClientInfoPopup.m_VisibilityAnimation.Update(false, 0.0f, AMF_INFO_POPUP_ANIMATION_DURATION, false);
	std::fill(std::begin(s_AmfClientInfoPopup.m_aSectionScrollY), std::end(s_AmfClientInfoPopup.m_aSectionScrollY), 0.0f);
	std::fill(std::begin(s_AmfClientInfoPopup.m_aSectionHeights), std::end(s_AmfClientInfoPopup.m_aSectionHeights), 0.0f);
	if(s_AmfClientInfoPopup.m_BlurredBackground.IsValid())
		Graphics()->UnloadTexture(&s_AmfClientInfoPopup.m_BlurredBackground);
	s_AmfClientInfoPopup.m_CaptureBlur = true;
	s_AmfClientInfoPopup.m_CaptureAttempts = 0;
	s_AmfClientInfoPopup.m_CaptureDelayFrames = 1;
	s_AmfClientInfoPopup.m_CapturedScreenWidth = 0;
	s_AmfClientInfoPopup.m_CapturedScreenHeight = 0;

	const CUIRect Screen = *Ui()->Screen();
	const float Width = std::min(660.0f, Screen.w * 0.74f);
	const float Height = std::min(490.0f, Screen.h * 0.74f);
	const float RawX = Screen.x + (Screen.w - Width) / 2.0f;
	const float RawY = Screen.y + (Screen.h - Height) / 2.0f;
	const float PixelSize = Ui()->PixelSize();
	const auto SnapToPixel = [PixelSize](float Value) { return std::round(Value / PixelSize) * PixelSize; };
	const float X = SnapToPixel(RawX);
	const float Y = SnapToPixel(RawY);
	const float Right = SnapToPixel(RawX + Width);
	const float Bottom = SnapToPixel(RawY + Height);
	const float PopupWidth = std::max(PixelSize, Right - X);
	const float PopupHeight = std::max(PixelSize, Bottom - Y);
	s_AmfClientInfoPopup.m_PopupRect = CUIRect{X, Y, PopupWidth, PopupHeight};
	SPopupMenuProperties Props;
	// RenderAmfClientInfoPopup owns the square window surface so this popup can
	// keep its regular input and close lifecycle without a second backing panel.
	Props.m_BorderColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);
	Props.m_BackgroundColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);
	Props.m_CloseOnClickOutside = false;
	Props.m_CloseOnEscape = false;
	Ui()->DoPopupMenu(&s_AmfClientInfoPopup, X, Y, PopupWidth, PopupHeight, &s_AmfClientInfoPopup, RenderAmfClientInfoPopup, Props);
}

void CMenus::RenderAmfClientInfoOverlay()
{
	if(!Ui()->IsPopupOpen(&s_AmfClientInfoPopup))
	{
		if(s_AmfClientInfoPopup.m_BlurredBackground.IsValid())
			Graphics()->UnloadTexture(&s_AmfClientInfoPopup.m_BlurredBackground);
		s_AmfClientInfoPopup.m_CaptureBlur = false;
		s_AmfClientInfoPopup.m_CaptureAttempts = 0;
		s_AmfClientInfoPopup.m_CaptureDelayFrames = 0;
		return;
	}

	if(s_AmfClientInfoPopup.m_CapturedScreenWidth != Graphics()->ScreenWidth() ||
		s_AmfClientInfoPopup.m_CapturedScreenHeight != Graphics()->ScreenHeight())
	{
		const bool FirstCapture = s_AmfClientInfoPopup.m_CapturedScreenWidth == 0 && s_AmfClientInfoPopup.m_CapturedScreenHeight == 0;
		s_AmfClientInfoPopup.m_CaptureBlur = true;
		s_AmfClientInfoPopup.m_CaptureAttempts = 0;
		if(!FirstCapture)
			s_AmfClientInfoPopup.m_CaptureDelayFrames = 0;
	}

	if(s_AmfClientInfoPopup.m_CaptureBlur && s_AmfClientInfoPopup.m_CaptureDelayFrames > 0)
	{
		--s_AmfClientInfoPopup.m_CaptureDelayFrames;
	}
	else if(s_AmfClientInfoPopup.m_CaptureBlur)
	{
		s_AmfClientInfoPopup.m_CaptureBlur = false;
		s_AmfClientInfoPopup.m_CapturedScreenWidth = Graphics()->ScreenWidth();
		s_AmfClientInfoPopup.m_CapturedScreenHeight = Graphics()->ScreenHeight();
		++s_AmfClientInfoPopup.m_CaptureAttempts;
		if(s_AmfClientInfoPopup.m_BlurredBackground.IsValid())
			Graphics()->UnloadTexture(&s_AmfClientInfoPopup.m_BlurredBackground);

		CImageInfo CapturedFrame;
		CImageInfo BlurredFrame;
		const bool CaptureSucceeded = Graphics()->CaptureScreen(CapturedFrame) && CreateAmfBlurredBackground(CapturedFrame, BlurredFrame);
		if(CaptureSucceeded)
		{
			s_AmfClientInfoPopup.m_BlurredBackground = Graphics()->LoadTextureRawMove(BlurredFrame, 0, "amf-client-info-gaussian-blur");
		}
		else if(s_AmfClientInfoPopup.m_CaptureAttempts < 3)
		{
			// Vulkan may not have a presented image during the very first menu
			// frame. Retry on the next frame, when a stable image is available.
			s_AmfClientInfoPopup.m_CaptureBlur = true;
		}
		CapturedFrame.Free();
		BlurredFrame.Free();
	}

	if(s_AmfClientInfoPopup.m_BlurredBackground.IsValid())
	{
		Graphics()->TextureSet(s_AmfClientInfoPopup.m_BlurredBackground);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(0.92f, 0.96f, 1.0f, 0.78f);
		const CUIRect Screen = *Ui()->Screen();
		const IGraphics::CQuadItem Quad(Screen.x, Screen.y, Screen.w, Screen.h);
		Graphics()->QuadsDrawTL(&Quad, 1);
		Graphics()->QuadsEnd();
	}
	Ui()->Screen()->Draw(ColorRGBA(0.90f, 0.94f, 0.99f, 0.58f), IGraphics::CORNER_NONE, 0.0f);

	// Keep the subtle blue glow symmetric around the fixed popup rect.
	CUIRect Shadow = s_AmfClientInfoPopup.m_PopupRect;
	constexpr float ShadowExtent = 6.0f;
	Shadow.x -= ShadowExtent;
	Shadow.y -= ShadowExtent;
	Shadow.w += ShadowExtent * 2.0f;
	Shadow.h += ShadowExtent * 2.0f;
	Shadow.Draw(ColorRGBA(0.08f, 0.18f, 0.34f, 0.10f * s_AmfClientInfoPopup.m_VisualOpacity), IGraphics::CORNER_NONE, 0.0f);
	Shadow.Margin(3.0f, &Shadow);
	Shadow.Draw(ColorRGBA(0.30f, 0.52f, 0.90f, 0.06f * s_AmfClientInfoPopup.m_VisualOpacity), IGraphics::CORNER_NONE, 0.0f);
}

void CMenus::OpenAmfHudEditor()
{
	if(Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK)
	{
		SetActive(false);
		GameClient()->m_AmfHudEditor.Activate();
	}
	else
	{
		GameClient()->m_Menus.PopupMessage(TCLocalize("HUD Editor", "AMF Client"), TCLocalize("Connect to a server first to open the HUD Editor.", "AMF Client"), Localize("Ok"));
	}
}

void CMenus::RenderAspectRatioTestView(CUIRect MainView)
{
	// This view is intentionally UI-only. It visualizes the ratio that the
	// graphics backend is currently using; it never creates a render target or
	// changes the projection itself.
	const float AppliedAspect = std::max(0.01f, Graphics()->ScreenAspect());
	const float GeometryAspect = std::clamp(AppliedAspect, 0.25f, 4.0f);
	const int ScreenWidth = Graphics()->ScreenWidth();
	const int ScreenHeight = Graphics()->ScreenHeight();
	const int ApplyMode = std::clamp(g_Config.m_AmfCustomAspectRatioApplyMode, 0, 2);
	const char *apApplyModeNames[] = {
		TCLocalize("Game only", "AMF Client"),
		TCLocalize("Full", "AMF Client"),
		TCLocalize("Game no HUD", "AMF Client")};

	CUIRect Panel;
	MainView.Margin(MarginSmall, &Panel);
	Panel.Draw(ColorRGBA(0.025f, 0.04f, 0.08f, 0.94f), IGraphics::CORNER_ALL, 8.0f);
	Panel.Margin(12.0f, &Panel);

	CUIRect TitleRow, Body;
	Panel.HSplitTop(HeadlineHeight, &TitleRow, &Body);
	char aTitle[128];
	str_format(aTitle, sizeof(aTitle), "%s %.3f", TCLocalize("Aspect Ratio", "AMF Client"), AppliedAspect);
	Ui()->DoLabel(&TitleRow, aTitle, HeadlineFontSize, TEXTALIGN_ML);

	Body.HSplitTop(MarginExtraSmall, nullptr, &Body);
	CUIRect InfoRow;
	Body.HSplitTop(LineSize, &InfoRow, &Body);
	char aInfo[256];
	str_format(aInfo, sizeof(aInfo), "%s: %d × %d    %s: %s",
		TCLocalize("Resolution", "AMF Client"), ScreenWidth, ScreenHeight,
		TCLocalize("Mode", "AMF Client"), apApplyModeNames[ApplyMode]);
	Ui()->DoLabel(&InfoRow, aInfo, FontSize, TEXTALIGN_ML);
	Body.HSplitTop(MarginSmall, nullptr, &Body);

	// Reserve the footer before computing the preview bounds so the geometry is
	// stable at every supported resolution and UI scale.
	const float FooterHeight = 2.0f * LineSize + 3.0f * MarginSmall;
	CUIRect PreviewArea, Footer;
	Body.HSplitBottom(FooterHeight, &PreviewArea, &Footer);
	PreviewArea.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.35f), IGraphics::CORNER_ALL, 6.0f);

	CUIRect PreviewBounds;
	PreviewArea.Margin(18.0f, &PreviewBounds);
	if(PreviewBounds.w > 1.0f && PreviewBounds.h > 1.0f)
	{
		float ScreenW = PreviewBounds.w;
		float ScreenH = ScreenW / GeometryAspect;
		if(ScreenH > PreviewBounds.h)
		{
			ScreenH = PreviewBounds.h;
			ScreenW = ScreenH * GeometryAspect;
		}
		const float ScreenX = PreviewBounds.x + (PreviewBounds.w - ScreenW) * 0.5f;
		const float ScreenY = PreviewBounds.y + (PreviewBounds.h - ScreenH) * 0.5f;
		const float Rounding = std::min(8.0f, std::min(ScreenW, ScreenH) * 0.12f);
		Graphics()->DrawRect(ScreenX, ScreenY, ScreenW, ScreenH, ColorRGBA(0.06f, 0.10f, 0.18f, 1.0f), IGraphics::CORNER_ALL, Rounding);

		const float Inset = std::min(4.0f, std::min(ScreenW, ScreenH) * 0.035f);
		const float InnerX = ScreenX + Inset;
		const float InnerY = ScreenY + Inset;
		const float InnerW = std::max(1.0f, ScreenW - 2.0f * Inset);
		const float InnerH = std::max(1.0f, ScreenH - 2.0f * Inset);
		Graphics()->DrawRect(InnerX, InnerY, InnerW, InnerH, ColorRGBA(0.14f, 0.30f, 0.48f, 0.86f), IGraphics::CORNER_ALL, std::max(0.0f, Rounding - Inset));

		const float GridLine = std::max(1.0f, Ui()->PixelSize());
		const ColorRGBA GridColor(0.82f, 0.93f, 1.0f, 0.23f);
		for(int Column = 1; Column < 4; ++Column)
		{
			const float X = InnerX + InnerW * Column / 4.0f - GridLine * 0.5f;
			Graphics()->DrawRect(X, InnerY, GridLine, InnerH, GridColor, IGraphics::CORNER_NONE, 0.0f);
		}
		for(int Row = 1; Row < 3; ++Row)
		{
			const float Y = InnerY + InnerH * Row / 3.0f - GridLine * 0.5f;
			Graphics()->DrawRect(InnerX, Y, InnerW, GridLine, GridColor, IGraphics::CORNER_NONE, 0.0f);
		}

		const float MarkerRadius = std::max(5.0f, std::min(InnerW, InnerH) * 0.11f);
		// DrawCircle is a low-level freeform primitive and, unlike DrawRect,
		// requires an active quad batch. Keep this batch self-contained so the
		// preview cannot inherit or leak render state from the surrounding cards.
		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		Graphics()->SetColor(0.96f, 0.96f, 1.0f, 0.88f);
		Graphics()->DrawCircle(InnerX + InnerW * 0.5f, InnerY + InnerH * 0.5f, MarkerRadius, 32);
		Graphics()->QuadsEnd();
		const float MarkerSize = MarkerRadius * 0.95f;
		Graphics()->DrawRect(InnerX + InnerW * 0.18f, InnerY + InnerH * 0.22f, MarkerSize, MarkerSize, ColorRGBA(1.0f, 0.82f, 0.40f, 0.90f), IGraphics::CORNER_ALL, MarkerSize * 0.18f);
		Graphics()->DrawRect(InnerX + InnerW * 0.82f - MarkerSize, InnerY + InnerH * 0.68f, MarkerSize, MarkerSize, ColorRGBA(0.48f, 0.90f, 0.72f, 0.90f), IGraphics::CORNER_ALL, MarkerSize * 0.18f);
	}

	Footer.HSplitTop(LineSize, &TitleRow, &Footer);
	// The countdown is formatted separately so the localized string retains its
	// numeric placeholder instead of relying on a hardcoded English suffix.
	char aCountdown[64];
	str_format(aCountdown, sizeof(aCountdown), TCLocalize("Reverting in %d seconds", "AMF Client"), AspectConfirmSecondsLeft());
	char aPrompt[192];
	str_format(aPrompt, sizeof(aPrompt), "%s  %s", TCLocalize("Keep this aspect ratio?", "AMF Client"), aCountdown);
	Ui()->DoLabel(&TitleRow, aPrompt, FontSize, TEXTALIGN_ML);
	Footer.HSplitTop(MarginSmall, nullptr, &Footer);

	CUIRect ConfirmButton, RevertButton, ButtonRow;
	const float ButtonGap = MarginSmall * 2.0f;
	const float ButtonWidth = std::min(124.0f, std::max(104.0f, (Footer.w - ButtonGap) * 0.5f));
	const float ButtonRowWidth = ButtonWidth * 2.0f + ButtonGap;
	Footer.VSplitLeft(std::max(0.0f, (Footer.w - ButtonRowWidth) * 0.5f), nullptr, &ButtonRow);
	ButtonRow.VSplitLeft(ButtonWidth, &ConfirmButton, &ButtonRow);
	ButtonRow.VSplitLeft(ButtonGap, nullptr, &ButtonRow);
	ButtonRow.VSplitLeft(ButtonWidth, &RevertButton, nullptr);
	static CButtonContainer s_AspectTestConfirmButton;
	static CButtonContainer s_AspectTestRevertButton;
	const ColorRGBA ConfirmNormal(0.12f, 0.42f, 0.60f, 0.98f);
	const ColorRGBA ConfirmHover(0.17f, 0.54f, 0.73f, 1.0f);
	const ColorRGBA ConfirmPressed(0.08f, 0.31f, 0.48f, 1.0f);
	const ColorRGBA RevertNormal(0.55f, 0.22f, 0.28f, 0.98f);
	const ColorRGBA RevertHover(0.68f, 0.30f, 0.38f, 1.0f);
	const ColorRGBA RevertPressed(0.43f, 0.15f, 0.21f, 1.0f);
	DrawAspectRatioButton(ConfirmButton, ConfirmNormal, ConfirmHover, ConfirmPressed, Ui()->HotItem() == &s_AspectTestConfirmButton, Ui()->ActiveItem() == &s_AspectTestConfirmButton);
	DrawAspectRatioButton(RevertButton, RevertNormal, RevertHover, RevertPressed, Ui()->HotItem() == &s_AspectTestRevertButton, Ui()->ActiveItem() == &s_AspectTestRevertButton);
	if(DoButtonLineSize_Menu(&s_AspectTestConfirmButton, TCLocalize("Confirm", "AMF Client"), 0, &ConfirmButton, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f)))
		ConfirmAspectRatioTest();
	if(DoButtonLineSize_Menu(&s_AspectTestRevertButton, TCLocalize("Revert", "AMF Client"), 0, &RevertButton, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f)))
		RevertAspectRatioTest();
}

void CMenus::RenderSettingsAmfClient(CUIRect MainView)
{
	CUIRect TabBar, Content, LeftView, RightView, Button, Label;
	MainView.HSplitTop(LineSize, &TabBar, &Content);
	static int s_AmfClientPage = 0;
	static float s_AmfPageTransition = 1.0f;
	static int s_AmfPageTransitionDirection = 0;
	static CButtonContainer s_GeneralTab, s_VisualTab, s_InformationTab;
	static SUIAnimator s_aAmfPageAnimators[3] = {};
	static bool s_AmfPageAnimatorsInitialized = false;
	const bool SmoothHudWindowActive = Kernel()->RequestInterface<IEngineGraphics>()->WindowActive();
	if(!s_AmfPageAnimatorsInitialized)
	{
		for(SUIAnimator &Animator : s_aAmfPageAnimators)
		{
			// Keep the tab geometry fixed. DoButton_MenuTab still uses the
			// animator for a color transition, but not for resizing or moving a
			// tab into one of its neighbours.
			Animator.m_XOffset = 0.0f;
			Animator.m_YOffset = 0.0f;
			Animator.m_WOffset = 0.0f;
			Animator.m_HOffset = 0.0f;
			Animator.m_RepositionLabel = false;
		}
		s_AmfPageAnimatorsInitialized = true;
	}
	CUIRect GeneralTab, VisualTab, InformationTab;
	// Keep every animated tab inside the tab-row. The tab geometry itself is
	// fixed above; this is an additional guard for fills/highlights at every UI
	// scale and for long localized labels.
	const CUIRect TabBarClip = TabBar;
	const float TabWidth = TabBar.w / 3.0f;
	TabBar.VSplitLeft(TabWidth, &GeneralTab, &TabBar);
	TabBar.VSplitLeft(TabWidth, &VisualTab, &InformationTab);
	const char *pGeneral = TCLocalize("General", "AMF Client");
	const char *pVisual = TCLocalize("Visual", "AMF Client");
	const char *pInformation = TCLocalize("Information", "AMF Client");
	Ui()->ClipEnable(&TabBarClip);
	const auto SelectAmfPage = [&](int Page) {
		if(Page == s_AmfClientPage)
			return;
		s_AmfPageTransitionDirection = Page > s_AmfClientPage ? -1 : 1;
		s_AmfClientPage = Page; // Logical page/input changes immediately.
		s_AmfPageTransition = g_Config.m_AmfSmoothHud && SmoothHudWindowActive && g_Config.m_AmfAnimMenu ? 0.0f : 1.0f;
	};
	if(DoButton_MenuTab(&s_GeneralTab, pGeneral, s_AmfClientPage == 0, &GeneralTab, IGraphics::CORNER_L, g_Config.m_AmfSmoothHud && SmoothHudWindowActive && g_Config.m_AmfAnimMenu ? &s_aAmfPageAnimators[0] : nullptr, nullptr, nullptr, nullptr, 4.0f))
		SelectAmfPage(0);
	if(DoButton_MenuTab(&s_VisualTab, pVisual, s_AmfClientPage == 1, &VisualTab, IGraphics::CORNER_NONE, g_Config.m_AmfSmoothHud && SmoothHudWindowActive && g_Config.m_AmfAnimMenu ? &s_aAmfPageAnimators[1] : nullptr, nullptr, nullptr, nullptr, 4.0f))
		SelectAmfPage(1);
	if(DoButton_MenuTab(&s_InformationTab, pInformation, s_AmfClientPage == 2, &InformationTab, IGraphics::CORNER_R, g_Config.m_AmfSmoothHud && SmoothHudWindowActive && g_Config.m_AmfAnimMenu ? &s_aAmfPageAnimators[2] : nullptr, nullptr, nullptr, nullptr, 4.0f))
		SelectAmfPage(2);
	Ui()->ClipDisable();
	GameClient()->m_Tooltips.DoToolTip(&s_GeneralTab, &GeneralTab, TCLocalize("Controls AMF input and prediction features.", "AMF Client"));
	GameClient()->m_Tooltips.DoToolTip(&s_VisualTab, &VisualTab, TCLocalize("Controls AMF visual features.", "AMF Client"));
	GameClient()->m_Tooltips.DoToolTip(&s_InformationTab, &InformationTab, TCLocalize("Shows AMF Client information and links.", "AMF Client"));
	const bool AnimatePageTransition = g_Config.m_AmfSmoothHud && SmoothHudWindowActive && g_Config.m_AmfAnimMenu;
	if(AnimatePageTransition && s_AmfPageTransition < 1.0f)
		s_AmfPageTransition = SmoothApproachDuration(s_AmfPageTransition, 1.0f, Client()->RenderFrameTime(), std::clamp(g_Config.m_AmfAnimDuration * 11 / 10, 100, 180));
	else if(!AnimatePageTransition)
		s_AmfPageTransition = 1.0f;
	const float PageTransitionEased = SmoothUiEaseOutCubic(s_AmfPageTransition);
	const CUIRect PageViewport = Content;
	Content.y += std::round((1.0f - PageTransitionEased) * s_AmfPageTransitionDirection * 4.0f / Ui()->PixelSize()) * Ui()->PixelSize();
	const auto RenderAmfPageTransition = [&]() {
		if(AnimatePageTransition && s_AmfPageTransition < 1.0f)
			PageViewport.Draw(ColorRGBA(0.03f, 0.04f, 0.10f, (1.0f - PageTransitionEased) * 0.35f), IGraphics::CORNER_NONE, 0.0f);
	};
	// The page slide never changes layout or hitboxes. This outer clip also keeps
	// a transitioning page out of the tab row at every UI scale.
	Ui()->ClipEnable(&PageViewport);

	// During the ten-second confirmation, replace the regular two-column flow
	// with a single, large test view. It is intentionally rendered at the page
	// origin so the user never has to hunt for the confirmation controls after
	// the aspect test starts. The previous scroll position is kept by CMenus and
	// restored when the test is reverted or times out.
	if(s_AmfClientPage == 1 && m_AspectConfirmActive)
	{
		m_AspectTestViewRendered = true;
		RenderAspectRatioTestView(Content);
		RenderAmfPageTransition();
		Ui()->ClipDisable();
		return;
	}

	if(s_AmfClientPage == 0)
	{
		static CScrollRegion s_GeneralScrollRegion;
		CScrollRegionParams ScrollParams;
		ScrollParams.m_ScrollUnit = 40.0f;
		ScrollParams.m_ForceShowScrollbar = true;
		ScrollParams.m_ScrollbarMargin = 5.0f;
		s_GeneralScrollRegion.Begin(&Content, &ScrollParams);
		Content.VSplitLeft(MarginSmall, nullptr, &Content);
		Content.VSplitRight(MarginSmall, &Content, nullptr);
		Content.HSplitTop(MarginSmall, nullptr, &Content);
		CUIRect GeneralLeftColumn = Content;
		CUIRect GeneralRightColumn = Content;
		const bool HasTwoGeneralColumns = Content.w >= 620.0f;
		if(HasTwoGeneralColumns)
			Content.VSplitMid(&GeneralLeftColumn, &GeneralRightColumn, MarginSmall);
		Content = GeneralLeftColumn;
		// Cards in the two columns intentionally flow independently. A short card
		// in one column must never reserve vertical space in the other one.
		const float GeneralCardGap = MarginSmall;

		int InputsMode = g_Config.m_AmfInputMode;
		bool InputsEnabled = InputsMode != AMF_INPUTS_OFF;
		static int s_LastNonOffMode = AMF_INPUTS_FAST;
		if(InputsEnabled)
			s_LastNonOffMode = InputsMode;
		const int DisplayedInputsMode = InputsEnabled ? InputsMode : s_LastNonOffMode;
		const bool InputsBestMode = DisplayedInputsMode == AMF_INPUTS_BEST;
		const float DetailRows = InputsBestMode ? 9.0f : 5.0f;
		constexpr float BaseCardHeight = 2.0f * 8.0f + HeadlineHeight + MarginExtraSmall + LineSize;
		const float FullInputDetailsHeight = MarginExtraSmall + LineSize + DetailRows * (MarginExtraSmall + LineSize);
		static CSmoothUiSectionAnimation s_InputDetailsSection;
		const bool AnimateInputDetails = g_Config.m_AmfSmoothHud && SmoothHudWindowActive && g_Config.m_AmfAnimMenu;
		constexpr int InputDetailsDuration = 180;
		float InputDetailsProgress = s_InputDetailsSection.Update(
			InputsEnabled,
			Client()->RenderFrameTime(),
			InputDetailsDuration,
			AnimateInputDetails);
		const float CardHeight = BaseCardHeight + FullInputDetailsHeight * InputDetailsProgress;

		CUIRect Card, CardContent, ContentRow;
		Content.HSplitTop(CardHeight, &Card, &Content);
		Card.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 8.0f);
		Card.Margin(8.0f, &CardContent);
		const auto DoAmfTooltip = [&](const void *pId, const CUIRect *pRect, const char *pText) {
			GameClient()->m_Tooltips.DoToolTip(pId, pRect, TCLocalize(pText, "AMF Client"));
		};

		CardContent.HSplitTop(HeadlineHeight, &Label, &CardContent);
		Ui()->DoLabel(&Label, TCLocalize("Fast Input Mode"), HeadlineFontSize, TEXTALIGN_ML);
		CardContent.HSplitTop(MarginExtraSmall, nullptr, &CardContent);

		CardContent.HSplitTop(LineSize, &ContentRow, &CardContent);
		const CUIRect InputToggleRow = ContentRow;
		static CButtonContainer s_InputsEnabledButton;
		static bool s_InputDetailsAutoScrollRequested = false;
		if(DoButton_CheckBox(&s_InputsEnabledButton, TCLocalize("Enable Fast Input", "AMF Client"), InputsEnabled, &ContentRow))
		{
			// Do not let the dropdown below overwrite OFF during the same frame.
			// The old code used stale InputsEnabled/InputsMode values, which made
			// a click on this checkbox immediately re-enable Fast Input.
			g_Config.m_AmfInputMode = InputsEnabled ? AMF_INPUTS_OFF : s_LastNonOffMode;
			InputsMode = g_Config.m_AmfInputMode;
			InputsEnabled = InputsMode != AMF_INPUTS_OFF;
			if(InputsEnabled)
				s_InputDetailsAutoScrollRequested = true;
			InputDetailsProgress = s_InputDetailsSection.Update(
				InputsEnabled,
				0.0f,
				InputDetailsDuration,
				AnimateInputDetails);
		}
		DoAmfTooltip(&s_InputsEnabledButton, &ContentRow, "Enables faster input processing to reduce control latency.");

		if(s_InputDetailsSection.IsVisible())
		{
			const float UiPixelSize = Ui()->PixelSize();
			CUIRect InputDetailsClip = CardContent;
			InputDetailsClip.h = std::round(FullInputDetailsHeight * InputDetailsProgress / UiPixelSize) * UiPixelSize;
			CUIRect InputDetailsContent = InputDetailsClip;
			InputDetailsContent.h = FullInputDetailsHeight;
			InputDetailsContent.y += std::round((1.0f - InputDetailsProgress) * 3.0f / UiPixelSize) * UiPixelSize;
			CardContent = InputDetailsContent;
			Ui()->ClipEnable(&InputDetailsClip);
			const ColorRGBA OldTextColor = TextRender()->GetTextColor();
			const ColorRGBA OldOutlineColor = TextRender()->GetTextOutlineColor();
			TextRender()->TextColor(OldTextColor.WithMultipliedAlpha(InputDetailsProgress));
			TextRender()->TextOutlineColor(OldOutlineColor.WithMultipliedAlpha(InputDetailsProgress));
			const bool InputDetailsInteractive = s_InputDetailsSection.AcceptsInput();
			static CConfig s_InputDetailsSavedConfig;
			if(!InputDetailsInteractive)
				s_InputDetailsSavedConfig = g_Config;

			CardContent.HSplitTop(MarginExtraSmall, nullptr, &CardContent);
			CardContent.HSplitTop(LineSize, &Button, &CardContent);
			CUIRect InputModeLabel, InputModeDropDown;
			Button.VSplitLeft(std::min(110.0f, Button.w * 0.35f), &InputModeLabel, &InputModeDropDown);
			Ui()->DoLabel(&InputModeLabel, TCLocalize("Input"), 12.0f, TEXTALIGN_ML);
			// Localize every frame so a language switch is immediate, while avoiding
			// the dynamic vector assignment formerly done for this render path.
			static constexpr std::array<int, 9> s_aInputModeValues = {
				AMF_INPUTS_FAST,
				AMF_INPUTS_BEST,
				AMF_INPUTS_SAIKO,
				AMF_INPUTS_SAIKO_PLUS,
				AMF_INPUTS_DELTA,
				AMF_INPUTS_F,
				AMF_INPUTS_MEOW,
				AMF_INPUTS_CLOUD_OLD,
				AMF_INPUTS_CLOUD};
			std::array<const char *, 9> aInputModeNames = {
				TCLocalize("Fast"),
				TCLocalize("Best"),
				TCLocalize("Saiko"),
				TCLocalize("Saiko+", "AMF Client"),
				TCLocalize("Delta"),
				TCLocalize("F"),
				TCLocalize("Meow Input", "AMF Client"),
				TCLocalize("Cloud Input Old", "AMF Client"),
				TCLocalize("Cloud Input", "AMF Client")};
			auto InputModeIndex = [&](int Mode) {
				for(size_t i = 0; i < s_aInputModeValues.size(); ++i)
					if(s_aInputModeValues[i] == Mode)
						return (int)i;
				return 0;
			};
			static CUi::SDropDownState s_InputModeDropDownState;
			static CScrollRegion s_InputModeDropDownScrollRegion;
			s_InputModeDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_InputModeDropDownScrollRegion;
			if(InputDetailsInteractive)
			{
				const int InputModeSelected = Ui()->DoDropDown(
					&InputModeDropDown,
					InputModeIndex(DisplayedInputsMode),
					aInputModeNames.data(),
					aInputModeNames.size(),
					s_InputModeDropDownState);
				if(InputModeSelected >= 0 && InputModeSelected < (int)s_aInputModeValues.size())
					g_Config.m_AmfInputMode = s_aInputModeValues[InputModeSelected];
				DoAmfTooltip(&s_InputModeDropDownState.m_ButtonContainer, &Button, "Selects the input behavior used while playing.");
			}
			else
			{
				InputModeDropDown.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f * InputDetailsProgress), IGraphics::CORNER_ALL, 4.0f);
				CUIRect ReadOnlyDropDown = InputModeDropDown;
				ReadOnlyDropDown.Margin(5.0f, &ReadOnlyDropDown);
				const int DisplayedIndex = InputModeIndex(DisplayedInputsMode);
				Ui()->DoLabel(&ReadOnlyDropDown, aInputModeNames[DisplayedIndex], 12.0f, TEXTALIGN_ML);
			}

			auto DoTickAmountSlider = [&](int *pValue, const CUIRect *pRect, const char *pLabel, int Min, int Max, int Scale = 100) {
			CUIRect SliderButton = *pRect;
			int Value = std::clamp(*pValue, Min, Max);
			const int Increment = std::max(1, (Max - Min) / 35);
			if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_UP) && Ui()->MouseInside(&SliderButton))
				Value = std::clamp(Value + Increment, Min, Max);
			if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN) && Ui()->MouseInside(&SliderButton))
				Value = std::clamp(Value - Increment, Min, Max);

			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "%s: %.2f %s", pLabel, Value / (float)Scale, TCLocalize("ticks"));
			CUIRect SliderLabel, ScrollBar;
			SliderButton.VSplitMid(&SliderLabel, &ScrollBar, std::min(10.0f, SliderButton.w * 0.05f));
			Ui()->DoLabel(&SliderLabel, aBuf, SliderLabel.h * CUi::ms_FontmodHeight * 0.8f, TEXTALIGN_ML);
			const float RelativeValue = (Value - Min) / (float)(Max - Min);
			const float NewRelativeValue = Ui()->DoScrollbarH(pValue, &ScrollBar, RelativeValue);
			*pValue = std::clamp((int)(Min + NewRelativeValue * (Max - Min) + 0.5f), Min, Max);
			DoAmfTooltip(pValue, pRect, "Controls how far the selected input predicts ahead.");
		};

		if(DisplayedInputsMode == AMF_INPUTS_FAST)
		{
			CardContent.HSplitTop(MarginSmall, nullptr, &CardContent);
			CardContent.HSplitTop(LineSize, &Button, &CardContent);
			DoSliderWithScaledValue(&g_Config.m_AmfFastInputAmount, &g_Config.m_AmfFastInputAmount, &Button, TCLocalize("Prediction offset"), 1, 40, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
			DoAmfTooltip(&g_Config.m_AmfFastInputAmount, &Button, "Controls how far the selected input predicts ahead.");
			CardContent.HSplitTop(MarginSmall, nullptr, &CardContent);
			CardContent.HSplitTop(LineSize, &ContentRow, &CardContent);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AmfFastInputOthers, TCLocalize("Fast Input others"), &g_Config.m_AmfFastInputOthers, &ContentRow, LineSize);
			DoAmfTooltip(&g_Config.m_AmfFastInputOthers, &ContentRow, "Applies Fast Input prediction to other tees.");
		}
		else if(DisplayedInputsMode == AMF_INPUTS_BEST)
		{
			CardContent.HSplitTop(MarginSmall, nullptr, &CardContent);
			CardContent.HSplitTop(LineSize, &Button, &CardContent);
			DoTickAmountSlider(&g_Config.m_AmfBestInputAmount, &Button, TCLocalize("Prediction offset"), 0, 1000);
			CardContent.HSplitTop(MarginSmall, nullptr, &CardContent);
			CardContent.HSplitTop(LineSize, &Button, &CardContent);
			Ui()->DoScrollbarOption(&g_Config.m_AmfBestInputSmoothing, &g_Config.m_AmfBestInputSmoothing, &Button, TCLocalize("Smoothing"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
			DoAmfTooltip(&g_Config.m_AmfBestInputSmoothing, &Button, "Controls visual smoothing for Best Input.");
			CardContent.HSplitTop(MarginSmall, nullptr, &CardContent);
			CardContent.HSplitTop(LineSize, &Button, &CardContent);
			Ui()->DoScrollbarOption(&g_Config.m_AmfBestInputLatencyComp, &g_Config.m_AmfBestInputLatencyComp, &Button, TCLocalize("Latency compensation"), 0, 50, &CUi::ms_LinearScrollbarScale, 0, "%");
			DoAmfTooltip(&g_Config.m_AmfBestInputLatencyComp, &Button, "Compensates a small part of network latency.");
			CardContent.HSplitTop(MarginSmall, nullptr, &CardContent);
			CardContent.HSplitTop(LineSize, &Label, &CardContent);
			Ui()->DoLabel(&Label, TCLocalize("Interpolation"), Label.h * CUi::ms_FontmodHeight * 0.8f, TEXTALIGN_ML);
			CardContent.HSplitTop(MarginSmall, nullptr, &CardContent);
			CardContent.HSplitTop(LineSize, &Button, &CardContent);
			static CButtonContainer s_aInterpolationButtons[3];
			static const char *s_apInterpolationNames[] = {"Linear", "Cubic", "Smooth"};
			static const int s_aInterpolationValues[] = {1, 2, 3};
			CUIRect InterpolationButtons = Button;
			const float Spacing = 2.0f;
			const float InterpolationButtonWidth = (InterpolationButtons.w - Spacing * 2.0f) / 3.0f;
			for(int i = 0; i < 3; ++i)
			{
				CUIRect InterpolationButton;
				if(i < 2)
				{
					InterpolationButtons.VSplitLeft(InterpolationButtonWidth, &InterpolationButton, &InterpolationButtons);
					InterpolationButtons.VSplitLeft(Spacing, nullptr, &InterpolationButtons);
				}
				else
					InterpolationButton = InterpolationButtons;
				InterpolationButton.HMargin(2.0f, &InterpolationButton);
				const int Corners = i == 0 ? IGraphics::CORNER_L : i == 2 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE;
				if(DoButton_Menu(&s_aInterpolationButtons[i], TCLocalize(s_apInterpolationNames[i]), g_Config.m_AmfBestInputInterpolation == s_aInterpolationValues[i], &InterpolationButton, BUTTONFLAG_LEFT, nullptr, Corners))
					g_Config.m_AmfBestInputInterpolation = s_aInterpolationValues[i];
				DoAmfTooltip(&s_aInterpolationButtons[i], &InterpolationButton, "Selects the interpolation curve for Best Input.");
			}
			CardContent.HSplitTop(MarginSmall, nullptr, &CardContent);
			CardContent.HSplitTop(LineSize, &ContentRow, &CardContent);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AmfBestInputOthers, TCLocalize("Best input others"), &g_Config.m_AmfBestInputOthers, &ContentRow, LineSize);
			DoAmfTooltip(&g_Config.m_AmfBestInputOthers, &ContentRow, "Applies Best Input prediction to other tees.");
		}
		else if(DisplayedInputsMode == AMF_INPUTS_SAIKO)
		{
			CardContent.HSplitTop(MarginSmall, nullptr, &CardContent);
			CardContent.HSplitTop(LineSize, &Button, &CardContent);
			DoTickAmountSlider(&g_Config.m_AmfSaikoInputAmount, &Button, TCLocalize("Prediction offset"), 0, 500);
			CardContent.HSplitTop(MarginSmall, nullptr, &CardContent);
			CardContent.HSplitTop(LineSize, &ContentRow, &CardContent);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AmfSaikoInputOthers, TCLocalize("Saiko input others"), &g_Config.m_AmfSaikoInputOthers, &ContentRow, LineSize);
			DoAmfTooltip(&g_Config.m_AmfSaikoInputOthers, &ContentRow, "Applies Saiko Input prediction to other tees.");
		}
		else if(DisplayedInputsMode == AMF_INPUTS_SAIKO_PLUS)
		{
			CardContent.HSplitTop(MarginSmall, nullptr, &CardContent);
			CardContent.HSplitTop(LineSize, &Button, &CardContent);
			DoTickAmountSlider(&g_Config.m_AmfSaikoPlusInputAmount, &Button, TCLocalize("Prediction offset"), 0, 500);
			CardContent.HSplitTop(MarginSmall, nullptr, &CardContent);
			CardContent.HSplitTop(LineSize, &ContentRow, &CardContent);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AmfSaikoPlusInputOthers, TCLocalize("Saiko+ input others", "AMF Client"), &g_Config.m_AmfSaikoPlusInputOthers, &ContentRow, LineSize);
			DoAmfTooltip(&g_Config.m_AmfSaikoPlusInputOthers, &ContentRow, "Applies Saiko+ Input prediction to other tees.");
		}
		else if(DisplayedInputsMode == AMF_INPUTS_DELTA)
		{
			CardContent.HSplitTop(MarginSmall, nullptr, &CardContent);
			CardContent.HSplitTop(LineSize, &Button, &CardContent);
			DoTickAmountSlider(&g_Config.m_AmfDeltaInputAmount, &Button, TCLocalize("Prediction offset"), 0, 500);
			CardContent.HSplitTop(MarginSmall, nullptr, &CardContent);
			CardContent.HSplitTop(LineSize, &ContentRow, &CardContent);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AmfDeltaInputOthers, TCLocalize("Delta input others"), &g_Config.m_AmfDeltaInputOthers, &ContentRow, LineSize);
			DoAmfTooltip(&g_Config.m_AmfDeltaInputOthers, &ContentRow, "Applies Delta Input prediction to other tees.");
		}
		else if(DisplayedInputsMode == AMF_INPUTS_F)
		{
			CardContent.HSplitTop(MarginSmall, nullptr, &CardContent);
			CardContent.HSplitTop(LineSize, &Button, &CardContent);
			DoTickAmountSlider(&g_Config.m_AmfFInputAmount, &Button, TCLocalize("Prediction offset"), 0, 5000, 1000);
			CardContent.HSplitTop(MarginSmall, nullptr, &CardContent);
			CardContent.HSplitTop(LineSize, &ContentRow, &CardContent);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AmfFInputOthers, TCLocalize("F input others"), &g_Config.m_AmfFInputOthers, &ContentRow, LineSize);
			DoAmfTooltip(&g_Config.m_AmfFInputOthers, &ContentRow, "Applies F Input prediction to other tees.");
		}
		else if(DisplayedInputsMode == AMF_INPUTS_MEOW)
		{
			CardContent.HSplitTop(MarginSmall, nullptr, &CardContent);
			CardContent.HSplitTop(LineSize, &Button, &CardContent);
			DoTickAmountSlider(&g_Config.m_AmfMeowInputAmount, &Button, TCLocalize("Prediction offset"), 0, 500, 100);
			CardContent.HSplitTop(MarginSmall, nullptr, &CardContent);
			CardContent.HSplitTop(LineSize, &ContentRow, &CardContent);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AmfMeowInputOthers, TCLocalize("Meow Input others", "AMF Client"), &g_Config.m_AmfMeowInputOthers, &ContentRow, LineSize);
			DoAmfTooltip(&g_Config.m_AmfMeowInputOthers, &ContentRow, "Applies Meow Input prediction to other tees.");
		}
		else if(DisplayedInputsMode == AMF_INPUTS_CLOUD)
		{
			CardContent.HSplitTop(MarginSmall, nullptr, &CardContent);
			CardContent.HSplitTop(LineSize, &Button, &CardContent);
			DoTickAmountSlider(&g_Config.m_AmfCloudInputAmount, &Button, TCLocalize("Prediction offset"), 0, 500);
			CardContent.HSplitTop(MarginSmall, nullptr, &CardContent);
			CardContent.HSplitTop(LineSize, &ContentRow, &CardContent);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AmfCloudInputOthers, TCLocalize("Cloud Input others", "AMF Client"), &g_Config.m_AmfCloudInputOthers, &ContentRow, LineSize);
			DoAmfTooltip(&g_Config.m_AmfCloudInputOthers, &ContentRow, "Applies Cloud Input prediction to other tees.");
		}
		else if(DisplayedInputsMode == AMF_INPUTS_CLOUD_OLD)
		{
			CardContent.HSplitTop(MarginSmall, nullptr, &CardContent);
			CardContent.HSplitTop(LineSize, &Button, &CardContent);
			DoTickAmountSlider(&g_Config.m_AmfCloudInputOldAmount, &Button, TCLocalize("Prediction offset"), 0, 500);
			CardContent.HSplitTop(MarginSmall, nullptr, &CardContent);
			CardContent.HSplitTop(LineSize, &ContentRow, &CardContent);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AmfCloudInputOldOthers, TCLocalize("Cloud Input Old others", "AMF Client"), &g_Config.m_AmfCloudInputOldOthers, &ContentRow, LineSize);
			DoAmfTooltip(&g_Config.m_AmfCloudInputOldOthers, &ContentRow, "Applies Cloud Input Old prediction to other tees.");
		}

		CardContent.HSplitTop(MarginExtraSmall, nullptr, &CardContent);
		CardContent.HSplitTop(LineSize, &ContentRow, &CardContent);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClSubTickAiming, TCLocalize("Sub-Tick aiming"), &g_Config.m_ClSubTickAiming, &ContentRow, LineSize);
		DoAmfTooltip(&g_Config.m_ClSubTickAiming, &ContentRow, "Uses the precise mouse position when an action is pressed.");
		CardContent.HSplitTop(MarginExtraSmall, nullptr, &CardContent);
		CardContent.HSplitTop(LineSize, &ContentRow, &CardContent);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AmfAutoMargin, TCLocalize("Auto margin"), &g_Config.m_AmfAutoMargin, &ContentRow, LineSize);
		DoAmfTooltip(&g_Config.m_AmfAutoMargin, &ContentRow, "Automatically replaces the manual prediction margin using latency.");

		CardContent.HSplitTop(MarginExtraSmall, nullptr, &CardContent);
		CardContent.HSplitTop(LineSize, &Label, &CardContent);
		char aAutoMargin[128];
		const int CurrentMargin = Client()->PredictionMargin();
		str_format(aAutoMargin, sizeof(aAutoMargin), "Prediction margin: %d.%dms", CurrentMargin / 10, std::abs(CurrentMargin % 10));
		SLabelProperties AutoMarginProps;
		AutoMarginProps.SetColor((g_Config.m_AmfAutoMargin ? ColorRGBA(0.68f, 0.78f, 0.95f, 1.0f) : ColorRGBA(0.58f, 0.62f, 0.72f, 1.0f)).WithMultipliedAlpha(InputDetailsProgress));
		Ui()->DoLabel(&Label, aAutoMargin, 11.0f, TEXTALIGN_ML, AutoMarginProps);

		if(!InputDetailsInteractive)
		{
			g_Config = s_InputDetailsSavedConfig;
			Ui()->SetActiveItem(nullptr);
			Ui()->SetHotItem(nullptr);
		}
		TextRender()->TextColor(OldTextColor);
		TextRender()->TextOutlineColor(OldOutlineColor);
		Ui()->ClipDisable();
		}
		if(s_InputDetailsAutoScrollRequested)
		{
			if(Input()->KeyPress(KEY_MOUSE_WHEEL_UP) || Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN))
				s_InputDetailsAutoScrollRequested = false;
			else if(InputDetailsProgress >= 0.60f)
			{
				CUIRect InputRevealRect = InputToggleRow;
				InputRevealRect.h += MarginExtraSmall + LineSize * 1.5f;
				s_GeneralScrollRegion.AddRect(InputRevealRect);
				s_GeneralScrollRegion.ScrollHereAnimated(CScrollRegion::SCROLLHERE_KEEP_IN_VIEW, 0.18f);
				s_InputDetailsAutoScrollRequested = false;
			}
		}

		Content.HSplitTop(GeneralCardGap, nullptr, &Content);
		const bool SnapTapEnabled = g_Config.m_AmfSnapTap != 0;
		const bool SnapTapBlocked = GameClient()->IsSnapTapBlockedByCommunity();
		constexpr int SnapTapSettingsDuration = 180;
		const float SnapTapBaseHeight = 2.0f * 8.0f + HeadlineHeight + MarginExtraSmall + LineSize;
		const float FullSnapTapSettingsHeight = MarginExtraSmall + LineSize +
			(SnapTapBlocked ? MarginExtraSmall + LineSize : 0.0f);
		static CSmoothUiSectionAnimation s_SnapTapSettingsSection;
		const bool AnimateSnapTapSettings = g_Config.m_AmfSmoothHud && SmoothHudWindowActive && g_Config.m_AmfAnimMenu;
		float SnapTapSettingsProgress = s_SnapTapSettingsSection.Update(
			SnapTapEnabled,
			Client()->RenderFrameTime(),
			SnapTapSettingsDuration,
			AnimateSnapTapSettings);
		const bool SnapTapSettingsVisible = s_SnapTapSettingsSection.IsVisible();
		const float GeneralUiPixelSize = Ui()->PixelSize();
		const float SnapTapSettingsHeight = SnapTapSettingsVisible ?
			std::round(FullSnapTapSettingsHeight * SnapTapSettingsProgress / GeneralUiPixelSize) * GeneralUiPixelSize :
			0.0f;
		CUIRect SnapTapCard, SnapTapContent;
		Content.HSplitTop(SnapTapBaseHeight + SnapTapSettingsHeight, &SnapTapCard, &Content);
		SnapTapCard.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 8.0f);
		SnapTapCard.Margin(8.0f, &SnapTapContent);
		SnapTapContent.HSplitTop(HeadlineHeight, &Label, &SnapTapContent);
		Ui()->DoLabel(&Label, TCLocalize("Snap Tap", "AMF Client"), HeadlineFontSize, TEXTALIGN_ML);
		SnapTapContent.HSplitTop(MarginExtraSmall, nullptr, &SnapTapContent);
		SnapTapContent.HSplitTop(LineSize, &ContentRow, &SnapTapContent);
		const bool SnapTapChanged = DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AmfSnapTap, TCLocalize("Enable"), &g_Config.m_AmfSnapTap, &ContentRow, LineSize);
		if(SnapTapChanged)
		{
			SnapTapSettingsProgress = s_SnapTapSettingsSection.Update(
				g_Config.m_AmfSnapTap != 0,
				0.0f,
				SnapTapSettingsDuration,
				AnimateSnapTapSettings);
		}
		GameClient()->m_Tooltips.DoToolTip(&g_Config.m_AmfSnapTap, &ContentRow, TCLocalize("Prioritizes the most recently pressed direction when Left and Right are held together.", "AMF Client"));

		if(SnapTapSettingsVisible)
		{
			CUIRect SnapTapSettingsClip;
			SnapTapContent.HSplitTop(SnapTapSettingsHeight, &SnapTapSettingsClip, &SnapTapContent);
			CUIRect SnapTapSettingsContent = SnapTapSettingsClip;
			SnapTapSettingsContent.h = FullSnapTapSettingsHeight;
			SnapTapSettingsContent.y += std::round((1.0f - SnapTapSettingsProgress) * 3.0f / GeneralUiPixelSize) * GeneralUiPixelSize;
			Ui()->ClipEnable(&SnapTapSettingsClip);
			const ColorRGBA OldSnapTapTextColor = TextRender()->GetTextColor();
			const ColorRGBA OldSnapTapOutlineColor = TextRender()->GetTextOutlineColor();
			TextRender()->TextColor(OldSnapTapTextColor.WithMultipliedAlpha(SnapTapSettingsProgress));
			TextRender()->TextOutlineColor(OldSnapTapOutlineColor.WithMultipliedAlpha(SnapTapSettingsProgress));
			const bool SnapTapSettingsInteractive = s_SnapTapSettingsSection.AcceptsInput();

			SnapTapSettingsContent.HSplitTop(MarginExtraSmall, nullptr, &SnapTapSettingsContent);
			SnapTapSettingsContent.HSplitTop(LineSize, &Button, &SnapTapSettingsContent);
			constexpr int SnapTapDelayMin = 0;
			constexpr int SnapTapDelayMax = 200;
			int Value = std::clamp(g_Config.m_AmfSnapTapDelay, SnapTapDelayMin, SnapTapDelayMax);
			const int Increment = std::max(1, (SnapTapDelayMax - SnapTapDelayMin) / 35);
			if(SnapTapSettingsInteractive && Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_UP) && Ui()->MouseInside(&Button))
				Value = std::clamp(Value + Increment, SnapTapDelayMin, SnapTapDelayMax);
			if(SnapTapSettingsInteractive && Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN) && Ui()->MouseInside(&Button))
				Value = std::clamp(Value - Increment, SnapTapDelayMin, SnapTapDelayMax);

			char aDelay[128];
			if(Value == 0)
				str_format(aDelay, sizeof(aDelay), "%s: %s", TCLocalize("Delay"), TCLocalize("Off"));
			else
				str_format(aDelay, sizeof(aDelay), "%s: %dms", TCLocalize("Delay"), Value);
			CUIRect DelayLabel, ScrollBar;
			Button.VSplitMid(&DelayLabel, &ScrollBar, std::min(10.0f, Button.w * 0.05f));
			Ui()->DoLabel(&DelayLabel, aDelay, DelayLabel.h * CUi::ms_FontmodHeight * 0.8f, TEXTALIGN_ML);
			const float RelativeValue = (Value - SnapTapDelayMin) / (float)(SnapTapDelayMax - SnapTapDelayMin);
			if(SnapTapSettingsInteractive)
			{
				const float NewRelativeValue = Ui()->DoScrollbarH(&g_Config.m_AmfSnapTapDelay, &ScrollBar, RelativeValue);
				g_Config.m_AmfSnapTapDelay = std::clamp((int)(SnapTapDelayMin + NewRelativeValue * (SnapTapDelayMax - SnapTapDelayMin) + 0.5f), SnapTapDelayMin, SnapTapDelayMax);
			}
			else
			{
				CUIRect Rail;
				ScrollBar.HMargin(5.0f, &Rail);
				Rail.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f * SnapTapSettingsProgress), IGraphics::CORNER_ALL, Rail.h / 2.0f);
				CUIRect Handle;
				Rail.VSplitLeft(std::clamp(33.0f, Rail.h, Rail.w / 3.0f), &Handle, nullptr);
				Handle.x += (Rail.w - Handle.w) * RelativeValue;
				Handle.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f * SnapTapSettingsProgress), IGraphics::CORNER_ALL, Rail.h / 2.0f);
			}

			if(SnapTapBlocked)
			{
				SnapTapSettingsContent.HSplitTop(MarginExtraSmall, nullptr, &SnapTapSettingsContent);
				SnapTapSettingsContent.HSplitTop(LineSize, &Label, &SnapTapSettingsContent);
				SLabelProperties BlockedProps;
				BlockedProps.SetColor(ColorRGBA(1.0f, 0.4f, 0.4f, SnapTapSettingsProgress));
				Ui()->DoLabel(&Label, TCLocalize("Looks like you're on a server where this feature is forbidden", "AMF Client"), FontSize, TEXTALIGN_ML, BlockedProps);
			}

			TextRender()->TextColor(OldSnapTapTextColor);
			TextRender()->TextOutlineColor(OldSnapTapOutlineColor);
			Ui()->ClipDisable();
		}

		Content.HSplitTop(GeneralCardGap, nullptr, &Content);
		constexpr float MovingTilesCardPadding = 6.0f;
		constexpr float MovingTilesTitleFontSize = 16.0f;
		constexpr float MovingTilesTitleHeight = 18.0f;
		constexpr float MovingTilesSelectorHeight = 18.0f;
		constexpr float MovingTilesSectionGap = 2.0f;
		constexpr float MovingTilesCardHeight = 2.0f * MovingTilesCardPadding + MovingTilesTitleHeight + MovingTilesSectionGap + MovingTilesSelectorHeight;
		CUIRect MovingTilesCard, MovingTilesContent;
		Content.HSplitTop(MovingTilesCardHeight, &MovingTilesCard, &Content);
		MovingTilesCard.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 8.0f);
		MovingTilesCard.Margin(MovingTilesCardPadding, &MovingTilesContent);
		MovingTilesContent.HSplitTop(MovingTilesTitleHeight, &Label, &MovingTilesContent);
		Ui()->DoLabel(&Label, TCLocalize("Moving Tiles", "AMF Client"), MovingTilesTitleFontSize, TEXTALIGN_ML);
		MovingTilesContent.HSplitTop(MovingTilesSectionGap, nullptr, &MovingTilesContent);
		MovingTilesContent.HSplitTop(MovingTilesSelectorHeight, &Button, &MovingTilesContent);
		static CButtonContainer s_aMovingTilesModeButtons[3];
		const std::array<const char *, 3> aMovingTilesModeNames = {
			TCLocalize("Off", "AMF Client"),
			TCLocalize("T Client", "AMF Client"),
			TCLocalize("Soon...", "AMF Client")};
		const int MovingTilesMode = std::clamp(g_Config.m_AmfMovingTilesMode, static_cast<int>(AMF_MOVING_TILES_OFF), static_cast<int>(AMF_MOVING_TILES_TCLIENT));
		CUIRect MovingTilesButtons = Button;
		const float MovingTilesButtonSpacing = 2.0f;
		const float MovingTilesButtonWidth = (MovingTilesButtons.w - 2.0f * MovingTilesButtonSpacing) / 3.0f;
		for(int Mode = AMF_MOVING_TILES_OFF; Mode <= AMF_MOVING_TILES_TCLIENT + 1; ++Mode)
		{
			CUIRect ModeButton;
			if(Mode > AMF_MOVING_TILES_OFF)
				MovingTilesButtons.VSplitLeft(MovingTilesButtonSpacing, nullptr, &MovingTilesButtons);
			MovingTilesButtons.VSplitLeft(MovingTilesButtonWidth, &ModeButton, &MovingTilesButtons);
			const int Corners = Mode == AMF_MOVING_TILES_OFF ? IGraphics::CORNER_L : Mode == AMF_MOVING_TILES_TCLIENT + 1 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE;
			if(DoButton_Menu(&s_aMovingTilesModeButtons[Mode], aMovingTilesModeNames[Mode], MovingTilesMode == Mode, &ModeButton, BUTTONFLAG_LEFT, nullptr, Corners))
			{
				if(Mode == AMF_MOVING_TILES_TCLIENT + 1)
					PopupMessage(TCLocalize("Moving Tiles", "AMF Client"), TCLocalize("Coming soon...", "AMF Client"), Localize("Ok"));
				else
					g_Config.m_AmfMovingTilesMode = Mode;
			}
		}

		// Keep the input-related cards together in the left column. The system
		// and focus cards start at the same baseline in the right column.
		GeneralLeftColumn = Content;
		if(HasTwoGeneralColumns)
			Content = GeneralRightColumn;
		else
			Content.HSplitTop(GeneralCardGap, nullptr, &Content);
		CUIRect PriorityCard, PriorityContent;
		Content.HSplitTop(2.0f * 8.0f + HeadlineHeight + 2.0f * MarginExtraSmall + 2.0f * LineSize, &PriorityCard, &Content);
		PriorityCard.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 8.0f);
		PriorityCard.Margin(8.0f, &PriorityContent);
		PriorityContent.HSplitTop(HeadlineHeight, &Label, &PriorityContent);
		Ui()->DoLabel(&Label, TCLocalize("System", "AMF Client"), HeadlineFontSize, TEXTALIGN_ML);
		PriorityContent.HSplitTop(MarginExtraSmall, nullptr, &PriorityContent);
		PriorityContent.HSplitTop(LineSize, &Button, &PriorityContent);
		static CButtonContainer s_AmfHighPriorityButton;
		// Keep this transition explicit. Unlike regular visual settings it changes
		// a Windows process property and must not depend on a deferred config poll.
		if(DoButton_CheckBox(
			&s_AmfHighPriorityButton,
			TCLocalize("High Priority DDNet", "AMF Client"),
			g_Config.m_AmfHighPriority,
			&Button))
		{
			g_Config.m_AmfHighPriority ^= 1;
			GameClient()->ApplyAmfProcessPriority();
		}
		GameClient()->m_Tooltips.DoToolTip(
			&s_AmfHighPriorityButton,
			&Button,
			TCLocalize("Sets only the AMF Client process to Windows High priority while enabled.", "AMF Client"));

		PriorityContent.HSplitTop(MarginExtraSmall, nullptr, &PriorityContent);
		PriorityContent.HSplitTop(LineSize, &Button, &PriorityContent);
		static CButtonContainer s_AmfDiscordLowPriorityButton;
		if(DoButton_CheckBox(
			&s_AmfDiscordLowPriorityButton,
			TCLocalize("Low Priority Discord", "AMF Client"),
			g_Config.m_AmfDiscordLowPriority,
			&Button))
		{
			g_Config.m_AmfDiscordLowPriority ^= 1;
			GameClient()->ApplyAmfDiscordProcessPriority(true);
		}
		GameClient()->m_Tooltips.DoToolTip(
			&s_AmfDiscordLowPriorityButton,
			&Button,
			TCLocalize("Sets only Discord.exe processes to Windows Low priority while enabled.", "AMF Client"));

		Content.HSplitTop(GeneralCardGap, nullptr, &Content);
		const bool FocusModeEnabled = g_Config.m_AmfFocusMode != 0;
		const float FocusModeDetailsHeight = 9.0f * (MarginExtraSmall + LineSize);
		constexpr int FocusModeSettingsDuration = 180;
		static CSmoothUiSectionAnimation s_FocusModeSettingsSection;
		const bool AnimateFocusModeSettings = g_Config.m_AmfSmoothHud && SmoothHudWindowActive && g_Config.m_AmfAnimMenu;
		float FocusModeSettingsProgress = s_FocusModeSettingsSection.Update(
			FocusModeEnabled,
			Client()->RenderFrameTime(),
			FocusModeSettingsDuration,
			AnimateFocusModeSettings);
		const bool FocusModeSettingsVisible = s_FocusModeSettingsSection.IsVisible();
		const float FocusModeSettingsHeight = FocusModeSettingsVisible ?
			std::round(FocusModeDetailsHeight * FocusModeSettingsProgress / GeneralUiPixelSize) * GeneralUiPixelSize :
			0.0f;
		const float FocusModeCardHeight = 2.0f * 8.0f + HeadlineHeight + MarginExtraSmall + LineSize + FocusModeSettingsHeight;
		CUIRect FocusModeCard, FocusModeContent;
		Content.HSplitTop(FocusModeCardHeight, &FocusModeCard, &Content);
		FocusModeCard.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 8.0f);
		FocusModeCard.Margin(8.0f, &FocusModeContent);
		FocusModeContent.HSplitTop(HeadlineHeight, &Label, &FocusModeContent);
		Ui()->DoLabel(&Label, TCLocalize("Focus Mode", "AMF Client"), HeadlineFontSize, TEXTALIGN_ML);
		FocusModeContent.HSplitTop(MarginExtraSmall, nullptr, &FocusModeContent);
		FocusModeContent.HSplitTop(LineSize, &Button, &FocusModeContent);
		static CButtonContainer s_AmfFocusModeButton;
		const bool FocusModeChanged = DoButton_CheckBoxAutoVMarginAndSet(
			&s_AmfFocusModeButton,
			TCLocalize("Enable Focus Mode", "AMF Client"),
			&g_Config.m_AmfFocusMode,
			&Button,
			LineSize);
		if(FocusModeChanged)
		{
			FocusModeSettingsProgress = s_FocusModeSettingsSection.Update(
				g_Config.m_AmfFocusMode != 0,
				0.0f,
				FocusModeSettingsDuration,
				AnimateFocusModeSettings);
		}

		if(FocusModeSettingsVisible)
		{
			CUIRect FocusModeSettingsClip;
			FocusModeContent.HSplitTop(FocusModeSettingsHeight, &FocusModeSettingsClip, &FocusModeContent);
			CUIRect FocusModeSettingsContent = FocusModeSettingsClip;
			FocusModeSettingsContent.h = FocusModeDetailsHeight;
			FocusModeSettingsContent.y += std::round((1.0f - FocusModeSettingsProgress) * 3.0f / GeneralUiPixelSize) * GeneralUiPixelSize;
			Ui()->ClipEnable(&FocusModeSettingsClip);
			const ColorRGBA OldFocusModeTextColor = TextRender()->GetTextColor();
			const ColorRGBA OldFocusModeOutlineColor = TextRender()->GetTextOutlineColor();
			TextRender()->TextColor(OldFocusModeTextColor.WithMultipliedAlpha(FocusModeSettingsProgress));
			TextRender()->TextOutlineColor(OldFocusModeOutlineColor.WithMultipliedAlpha(FocusModeSettingsProgress));
			const bool FocusModeSettingsInteractive = s_FocusModeSettingsSection.AcceptsInput();

			auto DoFocusModeOption = [&](CButtonContainer &ButtonContainer, int *pConfig, const char *pText) {
				FocusModeSettingsContent.HSplitTop(MarginExtraSmall, nullptr, &FocusModeSettingsContent);
				FocusModeSettingsContent.HSplitTop(LineSize, &Button, &FocusModeSettingsContent);
				if(FocusModeSettingsInteractive)
					DoButton_CheckBoxAutoVMarginAndSet(&ButtonContainer, TCLocalize(pText, "AMF Client"), pConfig, &Button, LineSize);
				else
				{
					if(Ui()->CheckActiveItem(&ButtonContainer))
						Ui()->SetActiveItem(nullptr);
					CUIRect Box, CheckLabel;
					Button.VSplitLeft(Button.h, &Box, &CheckLabel);
					CheckLabel.VSplitLeft(5.0f, nullptr, &CheckLabel);
					Box.Margin(2.0f, &Box);
					Box.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f * FocusModeSettingsProgress), IGraphics::CORNER_ALL, 3.0f);
					if(*pConfig)
					{
						TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
						TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
						Ui()->DoLabel(&Box, FontIcon::XMARK, Box.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
						TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
						TextRender()->SetRenderFlags(0);
					}
					Ui()->DoLabel(&CheckLabel, TCLocalize(pText, "AMF Client"), Box.h * CUi::ms_FontmodHeight, TEXTALIGN_ML);
				}
			};
			static CButtonContainer s_AmfFocusModeHideEmoticonsButton;
			static CButtonContainer s_AmfFocusModeHideNamesButton;
			static CButtonContainer s_AmfFocusModeHideEffectsButton;
			static CButtonContainer s_AmfFocusModeHideHudButton;
			static CButtonContainer s_AmfFocusModeHideMusicPlayerButton;
			static CButtonContainer s_AmfFocusModeHideUiButton;
			static CButtonContainer s_AmfFocusModeHideChatButton;
			static CButtonContainer s_AmfFocusModeHideScoreboardButton;
			DoFocusModeOption(s_AmfFocusModeHideEmoticonsButton, &g_Config.m_AmfFocusModeHideEmoticons, "Hide Emoticons");
			DoFocusModeOption(s_AmfFocusModeHideNamesButton, &g_Config.m_AmfFocusModeHideNames, "Hide Player Names");
			DoFocusModeOption(s_AmfFocusModeHideEffectsButton, &g_Config.m_AmfFocusModeHideEffects, "Hide Visual Effects");
			DoFocusModeOption(s_AmfFocusModeHideHudButton, &g_Config.m_AmfFocusModeHideHud, "Hide HUD");
			DoFocusModeOption(s_AmfFocusModeHideMusicPlayerButton, &g_Config.m_AmfFocusModeHideMusicPlayer, "Hide Music Player");
			DoFocusModeOption(s_AmfFocusModeHideUiButton, &g_Config.m_AmfFocusModeHideUi, "Hide Unnecessary UI");
			DoFocusModeOption(s_AmfFocusModeHideChatButton, &g_Config.m_AmfFocusModeHideChat, "Hide Chat");
			DoFocusModeOption(s_AmfFocusModeHideScoreboardButton, &g_Config.m_AmfFocusModeHideScoreboard, "Hide Scoreboard");

			FocusModeSettingsContent.HSplitTop(MarginExtraSmall, nullptr, &FocusModeSettingsContent);
			static CButtonContainer s_AmfFocusModeBindReader;
			static CButtonContainer s_AmfFocusModeBindClear;
			if(FocusModeSettingsInteractive)
			{
				DoLine_KeyReader(
					FocusModeSettingsContent,
					s_AmfFocusModeBindReader,
					s_AmfFocusModeBindClear,
					TCLocalize("Focus Mode Bind", "AMF Client"),
					"toggle amf_focus_mode 0 1");
			}
			else
			{
				CBindSlot Bind(KEY_UNKNOWN, KeyModifier::NONE);
				for(int Mod = 0; Mod < KeyModifier::COMBINATION_COUNT; Mod++)
				{
					for(int KeyId = 0; KeyId < KEY_LAST; KeyId++)
					{
						const char *pBind = GameClient()->m_Binds.Get(KeyId, Mod);
						if(pBind[0] && str_comp(pBind, "toggle amf_focus_mode 0 1") == 0)
						{
							Bind = CBindSlot(KeyId, Mod);
							break;
						}
					}
				}
				CUIRect KeyButton, KeyLabel, KeyReaderButton, ClearButton;
				FocusModeSettingsContent.HSplitTop(LineSize, &KeyButton, &FocusModeSettingsContent);
				KeyButton.VSplitMid(&KeyLabel, &KeyReaderButton);
				char aBindLabel[128];
				str_format(aBindLabel, sizeof(aBindLabel), "%s:", TCLocalize("Focus Mode Bind", "AMF Client"));
				Ui()->DoLabel(&KeyLabel, aBindLabel, FontSize, TEXTALIGN_ML);
				KeyReaderButton.VSplitRight(KeyReaderButton.h, &KeyReaderButton, &ClearButton);
				KeyReaderButton.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f * FocusModeSettingsProgress), IGraphics::CORNER_L, 5.0f);
				ClearButton.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f * FocusModeSettingsProgress), IGraphics::CORNER_R, 5.0f);
				char aBindName[64];
				if(Bind.m_Key == KEY_UNKNOWN)
					aBindName[0] = '\0';
				else
					GameClient()->m_Binds.GetKeyBindName(Bind.m_Key, Bind.m_ModifierMask, aBindName, sizeof(aBindName));
				CUIRect BindLabel;
				KeyReaderButton.HMargin(1.0f, &BindLabel);
				Ui()->DoLabel(&BindLabel, aBindName, BindLabel.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
				TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
				TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
				Ui()->DoLabel(&ClearButton, FontIcon::TRASH, ClearButton.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
				TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
				TextRender()->SetRenderFlags(0);
				FocusModeSettingsContent.HSplitTop(MarginExtraSmall, nullptr, &FocusModeSettingsContent);
			}

			TextRender()->TextColor(OldFocusModeTextColor);
			TextRender()->TextOutlineColor(OldFocusModeOutlineColor);
			Ui()->ClipDisable();
		}

		CUIRect ScrollEnd = Content;
		ScrollEnd.y = HasTwoGeneralColumns ? std::max(GeneralLeftColumn.y, Content.y) : Content.y;
		ScrollEnd.h = 0.0f;
		s_GeneralScrollRegion.AddRect(ScrollEnd);
		s_GeneralScrollRegion.End();
		GameClient()->NotifyAmfConfigChanged();
		RenderAmfPageTransition();
		Ui()->ClipDisable();
		return;
	}

	if(s_AmfClientPage == 1)
	{
		const float VisualCardGap = MarginSmall;

		// Keep the page-level HUD Editor action outside the scroll viewport. The
		// button keeps its existing margin/alignment while only the cards below it
		// receive the visual page scroll offset.
		Content.HSplitTop(MarginSmall, nullptr, &Content);
		CUIRect HudEditorButtonRow;
		Content.HSplitTop(LineSize, &HudEditorButtonRow, &Content);
		CUIRect HudEditorButton = HudEditorButtonRow;
		HudEditorButton.VSplitLeft(MarginSmall, nullptr, &HudEditorButton);
		HudEditorButton.VSplitRight(MarginSmall, &HudEditorButton, nullptr);
		static CButtonContainer s_AmfVisualsHudEditorButton;
		if(DoButtonLineSize_Menu(
			&s_AmfVisualsHudEditorButton,
			TCLocalize("HUD Editor", "AMF Client"),
			0,
			&HudEditorButton,
			LineSize,
			false,
			nullptr,
			IGraphics::CORNER_ALL,
			5.0f,
			0.0f,
			ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f)))
		{
			OpenAmfHudEditor();
		}
		const bool CanOpenHudEditor = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
		GameClient()->m_Tooltips.DoToolTip(
			&s_AmfVisualsHudEditorButton,
			&HudEditorButton,
			CanOpenHudEditor ? TCLocalize("Drag, scale, reset and save the position of AMF HUD elements.", "AMF Client") : TCLocalize("Connect to a server first to open the HUD Editor.", "AMF Client"));

		Content.HSplitTop(VisualCardGap, nullptr, &Content);
		CUIRect VisualScrollViewport = Content;
		CScrollRegionParams VisualScrollParams;
		VisualScrollParams.m_ScrollUnit = 40.0f;
		VisualScrollParams.m_ForceShowScrollbar = true;
		VisualScrollParams.m_ScrollbarMargin = 5.0f;
		m_AmfVisualScrollRegion.Begin(&VisualScrollViewport, &VisualScrollParams);
		if(m_AspectVisualScrollRestorePending)
		{
			m_AmfVisualScrollRegion.ScrollToDirect(m_AspectVisualScrollRestorePosition);
			m_AspectVisualScrollRestorePending = false;
		}
		Content = VisualScrollViewport;
		Content.VSplitLeft(MarginSmall, nullptr, &Content);
		Content.VSplitRight(MarginSmall, &Content, nullptr);
		const CUIRect VisualScrollBounds = Content;
		constexpr float VisualCardPadding = 8.0f;
		constexpr float VisualCardRounding = 8.0f;
		const auto BeginVisualCard = [&](CUIRect &Column, const float Height) {
			CUIRect Card, CardContent;
			Column.HSplitTop(Height, &Card, &Column);
			Card.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, VisualCardRounding);
			Card.Margin(VisualCardPadding, &CardContent);
			return CardContent;
		};
		auto RenderTuneZoneColorSetting = [&](CUIRect &Column) {
			// The selector uses the same compact label/control rhythm as the other
			// settings inside a Visuals card.
			CUIRect SettingLabel, Selector;
			Column.HSplitTop(LineSize, &SettingLabel, &Column);
			Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
			Column.HSplitTop(LineSize, &Selector, &Column);
			Ui()->DoLabel(&SettingLabel, TCLocalize("Tune Zone Color", "AMF Client"), 12.0f, TEXTALIGN_ML);

			static CButtonContainer s_aTuneZoneColorButtons[TuneZoneColor::PRESET_COUNT];
			const std::array<const char *, TuneZoneColor::PRESET_COUNT> aPresetNames = {
				TCLocalize("Blue", "AMF Client"),
				TCLocalize("Red", "AMF Client"),
				TCLocalize("Green", "AMF Client"),
				TCLocalize("Purple", "AMF Client")};
			const int CurrentPreset = TuneZoneColor::NormalizePreset(g_Config.m_AmfTuneZoneColorPreset);
			for(int Preset = TuneZoneColor::PRESET_BLUE; Preset < TuneZoneColor::PRESET_COUNT; ++Preset)
			{
				CUIRect Segment;
				const int RemainingSegments = TuneZoneColor::PRESET_COUNT - Preset;
				if(RemainingSegments > 1)
					Selector.VSplitLeft(Selector.w / RemainingSegments, &Segment, &Selector);
				else
					Segment = Selector;
				const int Corners = Preset == TuneZoneColor::PRESET_BLUE ? IGraphics::CORNER_L :
					Preset == TuneZoneColor::PRESET_PURPLE ? IGraphics::CORNER_R : IGraphics::CORNER_NONE;
				if(DoButton_Menu(&s_aTuneZoneColorButtons[Preset], aPresetNames[Preset], CurrentPreset == Preset, &Segment, BUTTONFLAG_LEFT, nullptr, Corners))
					GameClient()->m_MapImages.SetTuneZoneColorPreset(Preset);
			}
		};
		CUIRect VisualLeftColumn = Content;
		CUIRect VisualRightColumn = Content;
		const bool HasTwoVisualColumns = Content.w >= 620.0f;
		if(HasTwoVisualColumns)
			Content.VSplitMid(&VisualLeftColumn, &VisualRightColumn, MarginSmall);

		constexpr int DependentSettingsDuration = 180;
		const bool AnimateDependentSettings = g_Config.m_AmfSmoothHud && SmoothHudWindowActive && g_Config.m_AmfAnimMenu;
		static CSmoothUiSectionAnimation s_AnimationSettingsSection;
		static int64_t s_AnimationSettingsResetTime = 0;
		const bool ShowAnimationSettingsResetNotice = s_AnimationSettingsResetTime != 0 &&
			time_get() - s_AnimationSettingsResetTime < time_freq() * 2;
		const float AnimationRowHeight = LineSize + MarginExtraSmall;
		const float AnimationGroupHeadingHeight = LineSize + 1.0f + MarginExtraSmall;
		// This is intentionally derived from the rows actually rendered below.
		// The previous fixed 605px reserve made the Smooth HUD card look like it
		// contained an empty fourth section.
		const float FullAnimationSectionHeight =
			HeadlineHeight + MarginSmall +
			AnimationGroupHeadingHeight + 2.0f * AnimationRowHeight + MarginSmall +
			AnimationGroupHeadingHeight + 12.0f * AnimationRowHeight + MarginSmall +
			AnimationGroupHeadingHeight + 4.0f * AnimationRowHeight + LineSize +
			(ShowAnimationSettingsResetNotice ? LineSize : 0.0f);
		const auto ShouldAnimateSmoothHudSection = [&]() {
			// Switching Smooth HUD itself off must still be allowed to finish the
			// already visible collapse animation.
			return SmoothHudWindowActive && g_Config.m_AmfAnimMenu &&
				(g_Config.m_AmfSmoothHud || s_AnimationSettingsSection.IsVisible());
		};
		const bool AnimateSmoothHudSection = ShouldAnimateSmoothHudSection();
		const float UiPixelSize = Ui()->PixelSize();
		float AnimationSectionProgress = s_AnimationSettingsSection.Update(
			g_Config.m_AmfSmoothHud != 0,
			Client()->RenderFrameTime(),
			DependentSettingsDuration,
			AnimateSmoothHudSection);
		const bool AnimationSettingsVisible = s_AnimationSettingsSection.IsVisible();
		const float AnimationSectionHeight = AnimationSettingsVisible ? std::round(FullAnimationSectionHeight * AnimationSectionProgress / UiPixelSize) * UiPixelSize : 0.0f;
		const float SmoothHudCardHeight = VisualCardPadding * 2.0f + LineSize + (AnimationSettingsVisible ? MarginSmall * AnimationSectionProgress + AnimationSectionHeight : 0.0f);
		Content = BeginVisualCard(VisualLeftColumn, SmoothHudCardHeight);
		Content.HSplitTop(LineSize, &Button, &Content);
		static CButtonContainer s_AmfSmoothHudButton;
		const bool SmoothHudWasEnabled = g_Config.m_AmfSmoothHud != 0;
		const bool SmoothHudChanged = DoButton_CheckBoxAutoVMarginAndSet(
			&s_AmfSmoothHudButton,
			TCLocalize("Smooth HUD", "AMF Client"),
			&g_Config.m_AmfSmoothHud,
			&Button,
			LineSize);
		if(SmoothHudChanged)
		{
			// Keep the animated card's target in sync without changing settings on
			// page entry or allocating a new hit area in this click frame.
			s_AnimationSettingsSection.Update(g_Config.m_AmfSmoothHud != 0, 0.0f, DependentSettingsDuration, ShouldAnimateSmoothHudSection());
			if(!SmoothHudWasEnabled && g_Config.m_AmfSmoothHud && !g_Config.m_AmfAnimPresetInitialized)
			{
				// Deliberately event-driven: opening the page never overwrites saved values.
				ApplyRecommendedAmfAnimationPreset();
				g_Config.m_AmfAnimPresetInitialized = 1;
			}
		}
		GameClient()->m_Tooltips.DoToolTip(
			&s_AmfSmoothHudButton,
			&Button,
			TCLocalize("Enables smooth HUD/interface animations.", "AMF Client"));

		if(AnimationSettingsVisible)
		{
			Content.HSplitTop(MarginSmall * AnimationSectionProgress, nullptr, &Content);
			CUIRect AnimationSectionClip;
			Content.HSplitTop(AnimationSectionHeight, &AnimationSectionClip, &Content);

			CUIRect AnimationContent = AnimationSectionClip;
			AnimationContent.h = FullAnimationSectionHeight;
			AnimationContent.y += std::round((1.0f - AnimationSectionProgress) * 4.0f / UiPixelSize) * UiPixelSize;

			const ColorRGBA OldTextColor = TextRender()->GetTextColor();
			const ColorRGBA OldOutlineColor = TextRender()->GetTextOutlineColor();
			TextRender()->TextColor(OldTextColor.WithMultipliedAlpha(AnimationSectionProgress));
			TextRender()->TextOutlineColor(OldOutlineColor.WithMultipliedAlpha(AnimationSectionProgress));
			Ui()->ClipEnable(&AnimationSectionClip);
			const bool AnimationControlsInteractive = s_AnimationSettingsSection.AcceptsInput();

			const auto DoAnimationTooltip = [&](const void *pId, const CUIRect *pRect, const char *pText) {
				if(AnimationControlsInteractive)
					GameClient()->m_Tooltips.DoToolTip(pId, pRect, TCLocalize(pText, "AMF Client"));
			};
			auto DoAnimationCheck = [&](int *pValue, const char *pTitle, const char *pTooltip) {
				CUIRect Row;
				AnimationContent.HSplitTop(LineSize, &Row, &AnimationContent);
				if(AnimationControlsInteractive)
					DoButton_CheckBoxAutoVMarginAndSet(pValue, TCLocalize(pTitle, "AMF Client"), pValue, &Row, LineSize);
				else
				{
					if(Ui()->CheckActiveItem(pValue))
						Ui()->SetActiveItem(nullptr);
					CUIRect Box, CheckLabel;
					Row.VSplitLeft(Row.h, &Box, &CheckLabel);
					CheckLabel.VSplitLeft(5.0f, nullptr, &CheckLabel);
					Box.Margin(2.0f, &Box);
					Box.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f * AnimationSectionProgress), IGraphics::CORNER_ALL, 3.0f);
					if(*pValue)
					{
						TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
						TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
						Ui()->DoLabel(&Box, FontIcon::XMARK, Box.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
						TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
						TextRender()->SetRenderFlags(0);
					}
					Ui()->DoLabel(&CheckLabel, TCLocalize(pTitle, "AMF Client"), Box.h * CUi::ms_FontmodHeight, TEXTALIGN_ML);
				}
				DoAnimationTooltip(pValue, &Row, pTooltip);
				AnimationContent.HSplitTop(MarginExtraSmall, nullptr, &AnimationContent);
			};
			auto DoAnimationSlider = [&](int *pValue, const char *pTitle, const char *pTooltip, int Min, int Max, const char *pSuffix) {
				CUIRect Row;
				AnimationContent.HSplitTop(LineSize, &Row, &AnimationContent);
				if(AnimationControlsInteractive)
					Ui()->DoScrollbarOption(pValue, pValue, &Row, TCLocalize(pTitle, "AMF Client"), Min, Max, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, pSuffix);
				else
				{
					if(Ui()->CheckActiveItem(pValue))
						Ui()->SetActiveItem(nullptr);
					CUIRect SliderLabel, Slider;
					Row.VSplitMid(&SliderLabel, &Slider, std::min(10.0f, Row.w * 0.05f));
					char aValue[256];
					str_format(aValue, sizeof(aValue), "%s: %d%s", TCLocalize(pTitle, "AMF Client"), *pValue, pSuffix);
					Ui()->DoLabel(&SliderLabel, aValue, SliderLabel.h * CUi::ms_FontmodHeight * 0.8f, TEXTALIGN_ML);
					CUIRect Rail;
					Slider.HMargin(5.0f, &Rail);
					Rail.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f * AnimationSectionProgress), IGraphics::CORNER_ALL, Rail.h / 2.0f);
					CUIRect Handle;
					Rail.VSplitLeft(std::clamp(33.0f, Rail.h, Rail.w / 3.0f), &Handle, nullptr);
					const float RelativeValue = Max == Min ? 0.0f : std::clamp((*pValue - Min) / (float)(Max - Min), 0.0f, 1.0f);
					Handle.x += (Rail.w - Handle.w) * RelativeValue;
					Handle.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f * AnimationSectionProgress), IGraphics::CORNER_ALL, Rail.h / 2.0f);
				}
				DoAnimationTooltip(pValue, &Row, pTooltip);
				AnimationContent.HSplitTop(MarginExtraSmall, nullptr, &AnimationContent);
			};
			const auto DoGroupTitle = [&](const char *pTitle) {
				CUIRect GroupTitle;
				AnimationContent.HSplitTop(LineSize, &GroupTitle, &AnimationContent);
				Ui()->DoLabel(&GroupTitle, TCLocalize(pTitle, "AMF Client"), 13.0f, TEXTALIGN_ML);
				CUIRect Separator;
				AnimationContent.HSplitTop(1.0f, &Separator, &AnimationContent);
				Separator.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.10f * AnimationSectionProgress), IGraphics::CORNER_NONE, 0.0f);
				AnimationContent.HSplitTop(MarginExtraSmall, nullptr, &AnimationContent);
			};

			AnimationContent.HSplitTop(HeadlineHeight, &Label, &AnimationContent);
			Ui()->DoLabel(&Label, TCLocalize("Animations", "AMF Client"), HeadlineFontSize, TEXTALIGN_ML);
			AnimationContent.HSplitTop(MarginSmall, nullptr, &AnimationContent);

			DoGroupTitle("Menu animations");
			DoAnimationCheck(&g_Config.m_AmfAnimMenu, "Menu animations", "Smoothly animates menus, tabs, lists and popups.");
			DoAnimationCheck(&g_Config.m_AmfAnimStartMenu, "Start menu animations", "Smoothly shows the main menu elements when the client starts.");

			AnimationContent.HSplitTop(MarginSmall, nullptr, &AnimationContent);
			DoGroupTitle("Chat animations");
			DoAnimationCheck(&g_Config.m_AmfAnimChatOpen, "Chat opening animation", "Smoothly shows and hides the chat without delaying text input.");
			DoAnimationCheck(&g_Config.m_AmfAnimText, "Animate text", "Smoothly shows text blocks without delaying typed characters.");
			DoAnimationCheck(&g_Config.m_AmfAnimBlur, "Backdrop dimming", "Adds a lightweight dim behind animated containers.");
			DoAnimationCheck(&g_Config.m_AmfAnimSlide, "Slide effect", "Adds a small container movement when it appears or disappears.");
			DoAnimationSlider(&g_Config.m_AmfAnimDuration, "Duration", "Sets the base speed of interface animations.", 40, 400, " ms");
			DoAnimationSlider(&g_Config.m_AmfAnimBlurRadius, "Backdrop darkness", "Sets the strength of the animated container background dim.", 0, 12, "");
			DoAnimationSlider(&g_Config.m_AmfAnimSlideDistance, "Slide distance", "Sets how far animated containers move.", 0, 12, " px");
			DoAnimationCheck(&g_Config.m_AmfAnimSmoothCaret, "Smooth text caret", "Smoothly moves only the text caret and does not affect the DDNet mouse cursor.");
			DoAnimationSlider(&g_Config.m_AmfAnimCaretSpeed, "Text caret speed", "Sets the visual approach speed of the text caret.", 10, 100, "%");
			DoAnimationSlider(&g_Config.m_AmfAnimCaretWidth, "Text caret width", "Changes the thickness of the chat text caret.", 1, 8, " px");
			DoAnimationCheck(&g_Config.m_AmfAnimChatMessages, "Animate messages", "Smoothly shows new chat messages.");
			DoAnimationSlider(&g_Config.m_AmfAnimChatMessageDuration, "Message animation", "Sets a new message animation duration; zero uses the base duration.", 0, 400, " ms");

			AnimationContent.HSplitTop(MarginSmall, nullptr, &AnimationContent);
			DoGroupTitle("Scoreboard animations");
			DoAnimationCheck(&g_Config.m_AmfAnimScoreboard, "Animate scoreboard", "Adds a quick smooth appearance and disappearance to the scoreboard.");
			DoAnimationSlider(&g_Config.m_AmfAnimScoreboardOpenDuration, "Scoreboard opening duration", "Sets how quickly the scoreboard appears.", 60, 250, " ms");
			DoAnimationSlider(&g_Config.m_AmfAnimScoreboardCloseDuration, "Scoreboard closing duration", "Sets how quickly the scoreboard disappears.", 40, 200, " ms");
			DoAnimationSlider(&g_Config.m_AmfAnimScoreboardSlideDistance, "Scoreboard slide distance", "Sets how far the scoreboard moves during its transition.", 0, 24, " px");

			CUIRect ResetAnimationSettingsButton;
			AnimationContent.HSplitTop(LineSize, &ResetAnimationSettingsButton, &AnimationContent);
			static CButtonContainer s_ResetAnimationSettingsButton;
			if(AnimationControlsInteractive && DoButtonLineSize_Menu(&s_ResetAnimationSettingsButton, TCLocalize("Reset animation settings", "AMF Client"), 0, &ResetAnimationSettingsButton, LineSize, false, nullptr, IGraphics::CORNER_ALL, 4.0f, 0.0f, ColorRGBA(0.12f, 0.16f, 0.28f, 0.55f)))
			{
				ApplyRecommendedAmfAnimationPreset();
				g_Config.m_AmfAnimPresetInitialized = 1;
				s_AnimationSettingsResetTime = time_get();
			}
			DoAnimationTooltip(&s_ResetAnimationSettingsButton, &ResetAnimationSettingsButton, "Restores balanced animation values recommended for smooth and responsive interface behavior.");
			if(ShowAnimationSettingsResetNotice)
			{
				CUIRect ResetNotice;
				AnimationContent.HSplitTop(LineSize, &ResetNotice, &AnimationContent);
				SLabelProperties ResetNoticeProps;
				ResetNoticeProps.SetColor(ColorRGBA(0.60f, 0.82f, 0.70f, AnimationSectionProgress));
				Ui()->DoLabel(&ResetNotice, TCLocalize("Animation settings were reset to the recommended values.", "AMF Client"), 11.0f, TEXTALIGN_ML, ResetNoticeProps);
			}

			Ui()->ClipDisable();
			TextRender()->TextColor(OldTextColor);
			TextRender()->TextOutlineColor(OldOutlineColor);
		}

		VisualLeftColumn.HSplitTop(VisualCardGap, nullptr, &VisualLeftColumn);
		static CButtonContainer s_AmfClientIndicatorButton;
		static bool s_IndicatorSettingsAutoScrollRequested = false;
		// Update the section once before handling the parent click. This preserves
		// the previous target for the current frame; otherwise a first click on an
		// uninitialized section would make it snap directly to its open state.
		static CSmoothUiSectionAnimation s_IndicatorSettingsSection;
		float IndicatorSettingsProgress = s_IndicatorSettingsSection.Update(
			g_Config.m_AmfClientIndicator != 0,
			Client()->RenderFrameTime(),
			DependentSettingsDuration,
			AnimateDependentSettings);
		constexpr float FullIndicatorSettingsHeight = MarginExtraSmall + LineSize + MarginSmall + LineSize + MarginExtraSmall + 52.0f;
		const bool IndicatorSettingsVisible = s_IndicatorSettingsSection.IsVisible();
		const float IndicatorSettingsHeight = IndicatorSettingsVisible ? std::round(FullIndicatorSettingsHeight * IndicatorSettingsProgress / UiPixelSize) * UiPixelSize : 0.0f;
		const float IndicatorCardHeight = VisualCardPadding * 2.0f + LineSize + IndicatorSettingsHeight;
		Content = BeginVisualCard(VisualLeftColumn, IndicatorCardHeight);
		Content.HSplitTop(LineSize, &Button, &Content);
		const CUIRect IndicatorToggleRow = Button;
		if(DoButton_CheckBoxAutoVMarginAndSet(
			&s_AmfClientIndicatorButton,
			TCLocalize("Show AMF Client Indicator", "AMF Client"),
			&g_Config.m_AmfClientIndicator,
			&Button,
			LineSize))
		{
			// Switch the target immediately but deliberately advance it by no time
			// in the click frame. The visual section then expands from zero on the
			// following frames, exactly like the Input settings section.
			IndicatorSettingsProgress = s_IndicatorSettingsSection.Update(
				g_Config.m_AmfClientIndicator != 0,
				0.0f,
				DependentSettingsDuration,
				AnimateDependentSettings);
			if(g_Config.m_AmfClientIndicator)
				s_IndicatorSettingsAutoScrollRequested = true;
		}
		GameClient()->m_Tooltips.DoToolTip(
			&s_AmfClientIndicatorButton,
			&Button,
			TCLocalize("Shows the AMF Client icon next to supported player names.", "AMF Client"));

		if(IndicatorSettingsVisible)
		{
			CUIRect IndicatorSettingsClip;
			Content.HSplitTop(IndicatorSettingsHeight, &IndicatorSettingsClip, &Content);
			CUIRect IndicatorSettingsContent = IndicatorSettingsClip;
			IndicatorSettingsContent.h = FullIndicatorSettingsHeight;
			IndicatorSettingsContent.y += std::round((1.0f - IndicatorSettingsProgress) * 3.0f / UiPixelSize) * UiPixelSize;
			Ui()->ClipEnable(&IndicatorSettingsClip);
			const ColorRGBA OldTextColor = TextRender()->GetTextColor();
			const ColorRGBA OldOutlineColor = TextRender()->GetTextOutlineColor();
			TextRender()->TextColor(OldTextColor.WithMultipliedAlpha(IndicatorSettingsProgress));
			TextRender()->TextOutlineColor(OldOutlineColor.WithMultipliedAlpha(IndicatorSettingsProgress));
			const bool IndicatorSettingsInteractive = s_IndicatorSettingsSection.AcceptsInput();

			IndicatorSettingsContent.HSplitTop(MarginExtraSmall, nullptr, &IndicatorSettingsContent);
			IndicatorSettingsContent.HSplitTop(LineSize, &Button, &IndicatorSettingsContent);
			CUIRect PositionLabel, PositionDropDown;
			Button.VSplitLeft(std::min(175.0f, Button.w * 0.45f), &PositionLabel, &PositionDropDown);
			Ui()->DoLabel(&PositionLabel, TCLocalize("Indicator position", "AMF Client"), 12.0f, TEXTALIGN_ML);
			std::array<const char *, 3> aIndicatorPositionNames = {
				TCLocalize("Left of nickname", "AMF Client"),
				TCLocalize("Right of nickname", "AMF Client"),
				TCLocalize("Hide for yourself", "AMF Client")};
			static CUi::SDropDownState s_IndicatorPositionDropDownState;
			static CScrollRegion s_IndicatorPositionDropDownScrollRegion;
			s_IndicatorPositionDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_IndicatorPositionDropDownScrollRegion;
			if(IndicatorSettingsInteractive)
			{
				g_Config.m_AmfClientIndicatorPosition = Ui()->DoDropDown(
					&PositionDropDown,
					g_Config.m_AmfClientIndicatorPosition,
					aIndicatorPositionNames.data(),
					aIndicatorPositionNames.size(),
					s_IndicatorPositionDropDownState);
				GameClient()->m_Tooltips.DoToolTip(
					&s_IndicatorPositionDropDownState.m_ButtonContainer,
					&Button,
					TCLocalize("Choose where the indicator appears next to the nickname.", "AMF Client"));
			}
			else
			{
				PositionDropDown.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f * IndicatorSettingsProgress), IGraphics::CORNER_ALL, 4.0f);
				CUIRect ReadOnlyDropDown = PositionDropDown;
				ReadOnlyDropDown.Margin(5.0f, &ReadOnlyDropDown);
				const int Position = std::clamp(g_Config.m_AmfClientIndicatorPosition, 0, (int)aIndicatorPositionNames.size() - 1);
				Ui()->DoLabel(&ReadOnlyDropDown, aIndicatorPositionNames[Position], 12.0f, TEXTALIGN_ML);
			}

			IndicatorSettingsContent.HSplitTop(MarginSmall, nullptr, &IndicatorSettingsContent);
			IndicatorSettingsContent.HSplitTop(LineSize, &Label, &IndicatorSettingsContent);
			Ui()->DoLabel(&Label, TCLocalize("Preview", "AMF Client"), 12.0f, TEXTALIGN_ML);
			IndicatorSettingsContent.HSplitTop(MarginExtraSmall, nullptr, &IndicatorSettingsContent);
			CUIRect Preview;
			IndicatorSettingsContent.HSplitTop(52.0f, &Preview, &IndicatorSettingsContent);
			Preview.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f * IndicatorSettingsProgress), IGraphics::CORNER_ALL, 7.0f);
			constexpr const char *pPreviewName = "alya?";
			const float PreviewFontSize = 18.0f;
			const float PreviewIconHeight = 32.0f;
			const float PreviewIconWidth = PreviewIconHeight * (327.0f / 287.0f);
			const float PreviewSpacing = 6.0f;
			const float PreviewNameWidth = TextRender()->TextWidth(PreviewFontSize, pPreviewName);
			CUIRect PreviewName = {
				Preview.x + (Preview.w - PreviewNameWidth) / 2.0f,
				Preview.y,
				PreviewNameWidth,
				Preview.h};
			CUIRect PreviewIcon = {
				PreviewName.x,
				Preview.y + (Preview.h - PreviewIconHeight) / 2.0f,
				PreviewIconWidth,
				PreviewIconHeight};
			if(g_Config.m_AmfClientIndicator && g_Config.m_AmfClientIndicatorPosition == 0)
			{
				PreviewIcon.x = PreviewName.x - PreviewSpacing - PreviewIconWidth;
				GameClient()->m_AmfClientIndicator.RenderIcon(PreviewIcon, IndicatorSettingsProgress);
			}
			else if(g_Config.m_AmfClientIndicator && g_Config.m_AmfClientIndicatorPosition == 1)
			{
				PreviewIcon.x = PreviewName.x + PreviewNameWidth + PreviewSpacing;
				GameClient()->m_AmfClientIndicator.RenderIcon(PreviewIcon, IndicatorSettingsProgress);
			}
			Ui()->DoLabel(&PreviewName, pPreviewName, PreviewFontSize, TEXTALIGN_ML);

			TextRender()->TextColor(OldTextColor);
			TextRender()->TextOutlineColor(OldOutlineColor);
			Ui()->ClipDisable();
		}
		if(s_IndicatorSettingsAutoScrollRequested)
		{
			if(Input()->KeyPress(KEY_MOUSE_WHEEL_UP) || Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN))
				s_IndicatorSettingsAutoScrollRequested = false;
			else if(IndicatorSettingsProgress >= 0.60f)
			{
				CUIRect IndicatorRevealRect = IndicatorToggleRow;
				IndicatorRevealRect.h += MarginExtraSmall + LineSize * 1.5f;
				m_AmfVisualScrollRegion.AddRect(IndicatorRevealRect);
				m_AmfVisualScrollRegion.ScrollHereAnimated(CScrollRegion::SCROLLHERE_KEEP_IN_VIEW, 0.18f);
				s_IndicatorSettingsAutoScrollRequested = false;
			}
		}

		if(!HasTwoVisualColumns)
		{
			VisualRightColumn = VisualLeftColumn;
			// In the single-column layout the right column aliases the left one.
			// Reserve the same outer-card gap used everywhere else before placing
			// Aspect Ratio, otherwise its border touches Indicators at narrow
			// resolutions (notably 4:3).
			VisualRightColumn.HSplitTop(VisualCardGap, nullptr, &VisualRightColumn);
		}

		// Both module-local buttons use the same activation path; their owning
		// card decides where the row is placed.
		auto RenderModuleHudEditorButton = [&](CUIRect &HudEditorButton, CButtonContainer *pButtonContainer) {
			if(DoButtonLineSize_Menu(
				pButtonContainer,
				TCLocalize("HUD Editor", "AMF Client"),
				0,
				&HudEditorButton,
				LineSize,
				false,
				nullptr,
				IGraphics::CORNER_ALL,
				5.0f,
				0.0f,
				ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f)))
			{
				OpenAmfHudEditor();
			}
			GameClient()->m_Tooltips.DoToolTip(
				pButtonContainer,
				&HudEditorButton,
				CanOpenHudEditor ? TCLocalize("Drag, scale, reset and save the position of AMF HUD elements.", "AMF Client") : TCLocalize("Connect to a server first to open the HUD Editor.", "AMF Client"));
		};

		// Best Client custom aspect ratio controls. Keep the donor's stored
		// values and transitions, while exposing them inside AMF Visuals.
		const bool AspectBlocked = GameClient()->IsAspectRatioBlockedByFng();
		const int AspectMode = g_Config.m_AmfCustomAspectRatioMode >= 0 ?
			g_Config.m_AmfCustomAspectRatioMode : (g_Config.m_AmfCustomAspectRatio > 0 ? 1 : 0);
		// Custom is an editor state until Apply is pressed. Keep the active
		// graphics/config aspect untouched while the user edits the two fields.
		const bool AspectCustomMode = AspectMode == 2 || m_AspectCustomEditPending;
		const float AspectCustomHeight = AspectCustomMode ? (MarginSmall + LineSize + MarginSmall + LineSize) : 0.0f;
		const float AspectBlockedHeight = AspectBlocked ? (MarginSmall + LineSize) : 0.0f;
		// The detailed geometry preview is shown only by the active confirmation
		// view below. Keep the idle card compact and derive its height solely from
		// the controls that are actually rendered here.
		const float AspectSummaryHeight = LineSize;
		const float AspectCardHeight = VisualCardPadding * 2.0f +
			LineSize + MarginSmall + LineSize + MarginSmall + LineSize +
			AspectCustomHeight + MarginSmall + AspectSummaryHeight + AspectBlockedHeight;
		const CUIRect AspectCardRect = {
			VisualRightColumn.x,
			VisualRightColumn.y,
			VisualRightColumn.w,
			AspectCardHeight};
		CUIRect AspectCardContent = BeginVisualCard(VisualRightColumn, AspectCardHeight);
		AspectCardContent.HSplitTop(LineSize, &Label, &AspectCardContent);
		Ui()->DoLabel(&Label, TCLocalize("Aspect Ratio", "AMF Client"), HeadlineFontSize, TEXTALIGN_ML);
		AspectCardContent.HSplitTop(MarginSmall, nullptr, &AspectCardContent);

		const char *apAspectPresetNames[] = {
			TCLocalize("Off (default)", "AMF Client"), "5:4", "4:3", "3:2", TCLocalize("Custom", "AMF Client")};
		static constexpr std::array<int, 4> s_aAspectPresetValues = {0, 125, 133, 150};
		static CUi::SDropDownState s_AmfAspectPresetState;
		static CScrollRegion s_AmfAspectPresetScrollRegion;
		s_AmfAspectPresetState.m_SelectionPopupContext.m_pScrollRegion = &s_AmfAspectPresetScrollRegion;
		const int CustomPresetIndex = (int)std::size(apAspectPresetNames) - 1;
		const auto GetAspectPresetIndex = [&]() {
			if(m_AspectCustomEditPending)
				return CustomPresetIndex;
			if(AspectMode <= 0 || g_Config.m_AmfCustomAspectRatio == 0)
				return 0;
			if(AspectMode == 2)
				return CustomPresetIndex;
			for(size_t i = 1; i < s_aAspectPresetValues.size(); ++i)
				if(g_Config.m_AmfCustomAspectRatio == s_aAspectPresetValues[i])
					return (int)i;
			int BestIndex = 1;
			int BestDiff = absolute(g_Config.m_AmfCustomAspectRatio - s_aAspectPresetValues[BestIndex]);
			for(size_t i = 2; i < s_aAspectPresetValues.size(); ++i)
			{
				const int CurDiff = absolute(g_Config.m_AmfCustomAspectRatio - s_aAspectPresetValues[i]);
				if(CurDiff < BestDiff)
				{
					BestDiff = CurDiff;
					BestIndex = (int)i;
				}
			}
			return BestIndex;
		};

		CUIRect AspectPresetRow, AspectPresetLabel, AspectPresetControl;
		AspectCardContent.HSplitTop(LineSize, &AspectPresetRow, &AspectCardContent);
		AspectPresetRow.VSplitLeft(std::clamp(AspectPresetRow.w * 0.40f, 90.0f, 170.0f), &AspectPresetLabel, &AspectPresetControl);
		Ui()->DoLabel(&AspectPresetLabel, TCLocalize("Preset", "AMF Client"), FontSize, TEXTALIGN_ML);
		const int CurrentAspectPreset = GetAspectPresetIndex();
		const int NewAspectPreset = Ui()->DoDropDown(&AspectPresetControl, CurrentAspectPreset, apAspectPresetNames, (int)std::size(apAspectPresetNames), s_AmfAspectPresetState);
		if(NewAspectPreset != CurrentAspectPreset)
		{
			if(NewAspectPreset == 0)
			{
				m_AspectCustomEditPending = false;
				g_Config.m_AmfCustomAspectRatioMode = 0;
				g_Config.m_AmfCustomAspectRatio = 0;
			}
			else if(NewAspectPreset == CustomPresetIndex)
			{
				// Do not commit the mode or ratio yet. The existing line edits below
				// provide a staging area and the Apply button starts the normal test.
				m_AspectCustomEditPending = true;
			}
			else
			{
				m_AspectCustomEditPending = false;
				g_Config.m_AmfCustomAspectRatioMode = 1;
				g_Config.m_AmfCustomAspectRatio = s_aAspectPresetValues[NewAspectPreset];
			}
			if(NewAspectPreset != CustomPresetIndex)
				GameClient()->m_TClient.SetForcedAspect();
		}

		AspectCardContent.HSplitTop(MarginSmall, nullptr, &AspectCardContent);
		CUIRect AspectApplyRow, AspectApplyLabel, AspectApplyControl;
		AspectCardContent.HSplitTop(LineSize, &AspectApplyRow, &AspectCardContent);
		AspectApplyRow.VSplitLeft(std::clamp(AspectApplyRow.w * 0.40f, 90.0f, 170.0f), &AspectApplyLabel, &AspectApplyControl);
		Ui()->DoLabel(&AspectApplyLabel, TCLocalize("Apply", "AMF Client"), FontSize, TEXTALIGN_ML);
		const char *apAspectApplyNames[] = {TCLocalize("Game only", "AMF Client"), TCLocalize("Full", "AMF Client"), TCLocalize("Game no HUD", "AMF Client")};
		static CUi::SDropDownState s_AmfAspectApplyState;
		static CScrollRegion s_AmfAspectApplyScrollRegion;
		s_AmfAspectApplyState.m_SelectionPopupContext.m_pScrollRegion = &s_AmfAspectApplyScrollRegion;
		const int CurrentAspectApplyMode = std::clamp(g_Config.m_AmfCustomAspectRatioApplyMode, 0, 2);
		const int NewAspectApplyMode = Ui()->DoDropDown(&AspectApplyControl, CurrentAspectApplyMode, apAspectApplyNames, (int)std::size(apAspectApplyNames), s_AmfAspectApplyState);
		if(NewAspectApplyMode != CurrentAspectApplyMode)
		{
			g_Config.m_AmfCustomAspectRatioApplyMode = NewAspectApplyMode;
			if(!m_AspectCustomEditPending)
				GameClient()->m_TClient.SetForcedAspect();
		}

		static CLineInputNumber s_AmfAspectNumeratorInput;
		static CLineInputNumber s_AmfAspectDenominatorInput;
		static bool s_AmfAspectInputInitialized = false;
		static int s_AmfAspectLastNum = -1;
		static int s_AmfAspectLastDen = -1;
		if(AspectCustomMode)
		{
			const int ConfiguredNum = g_Config.m_AmfCustomAspectRatioNum > 0 ? g_Config.m_AmfCustomAspectRatioNum : 16;
			const int ConfiguredDen = g_Config.m_AmfCustomAspectRatioDen > 0 ? g_Config.m_AmfCustomAspectRatioDen : 9;
			if(!s_AmfAspectNumeratorInput.IsActive() && !s_AmfAspectDenominatorInput.IsActive() &&
				(!s_AmfAspectInputInitialized || s_AmfAspectLastNum != ConfiguredNum || s_AmfAspectLastDen != ConfiguredDen))
			{
				s_AmfAspectNumeratorInput.SetInteger(ConfiguredNum);
				s_AmfAspectDenominatorInput.SetInteger(ConfiguredDen);
				s_AmfAspectLastNum = ConfiguredNum;
				s_AmfAspectLastDen = ConfiguredDen;
				s_AmfAspectInputInitialized = true;
			}

			AspectCardContent.HSplitTop(MarginSmall, nullptr, &AspectCardContent);
			CUIRect CustomRatioRow, CustomRatioLabel, CustomRatioControls;
			AspectCardContent.HSplitTop(LineSize, &CustomRatioRow, &AspectCardContent);
			CustomRatioRow.VSplitLeft(std::clamp(CustomRatioRow.w * 0.40f, 90.0f, 170.0f), &CustomRatioLabel, &CustomRatioControls);
			Ui()->DoLabel(&CustomRatioLabel, TCLocalize("Custom size", "AMF Client"), FontSize, TEXTALIGN_ML);
			const float RatioGap = std::min(6.0f, CustomRatioControls.w * 0.08f);
			const float SeparatorWidth = std::min(12.0f, CustomRatioControls.w * 0.18f);
			const float FieldWidth = std::max(1.0f, (CustomRatioControls.w - SeparatorWidth - 2.0f * RatioGap) / 2.0f);
			CUIRect NumeratorRect, SeparatorRect, DenominatorRect;
			CustomRatioControls.VSplitLeft(FieldWidth, &NumeratorRect, &CustomRatioControls);
			CustomRatioControls.VSplitLeft(RatioGap, nullptr, &CustomRatioControls);
			CustomRatioControls.VSplitLeft(SeparatorWidth, &SeparatorRect, &CustomRatioControls);
			CustomRatioControls.VSplitLeft(RatioGap, nullptr, &CustomRatioControls);
			CustomRatioControls.VSplitLeft(FieldWidth, &DenominatorRect, nullptr);
			Ui()->DoEditBox(&s_AmfAspectNumeratorInput, &NumeratorRect, EditBoxFontSize);
			Ui()->DoLabel(&SeparatorRect, ":", FontSize, TEXTALIGN_MC);
			Ui()->DoEditBox(&s_AmfAspectDenominatorInput, &DenominatorRect, EditBoxFontSize);

			const int RawInputNum = s_AmfAspectNumeratorInput.GetInteger();
			const int RawInputDen = s_AmfAspectDenominatorInput.GetInteger();
			const bool CustomRatioValid = RawInputNum > 0 && RawInputNum <= 100000 && RawInputDen > 0 && RawInputDen <= 100000;
			const int InputNum = std::max(1, RawInputNum);
			const int InputDen = std::max(1, RawInputDen);
			const bool HasPendingCustomChange = CustomRatioValid && (m_AspectCustomEditPending || InputNum != g_Config.m_AmfCustomAspectRatioNum || InputDen != g_Config.m_AmfCustomAspectRatioDen);
			AspectCardContent.HSplitTop(MarginSmall, nullptr, &AspectCardContent);
			CUIRect CustomApplyRow, CustomApplySpace, CustomApplyButton;
			AspectCardContent.HSplitTop(LineSize, &CustomApplyRow, &AspectCardContent);
			CustomApplyRow.VSplitLeft(std::clamp(CustomApplyRow.w * 0.40f, 90.0f, 170.0f), &CustomApplySpace, &CustomApplyButton);
			(void)CustomApplySpace;
			static CButtonContainer s_AmfAspectApplyButton;
			const ColorRGBA ApplyNormal(0.12f, 0.42f, 0.60f, HasPendingCustomChange ? 0.98f : 0.38f);
			const ColorRGBA ApplyHover(0.17f, 0.54f, 0.73f, 1.0f);
			const ColorRGBA ApplyPressed(0.08f, 0.31f, 0.48f, 1.0f);
			DrawAspectRatioButton(CustomApplyButton, ApplyNormal, ApplyHover, ApplyPressed, Ui()->HotItem() == &s_AmfAspectApplyButton, Ui()->ActiveItem() == &s_AmfAspectApplyButton);
			if(DoButtonLineSize_Menu(&s_AmfAspectApplyButton, TCLocalize("Apply", "AMF Client"), HasPendingCustomChange ? 0 : -1, &CustomApplyButton, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f)) && HasPendingCustomChange)
			{
				m_AspectCustomEditPending = false;
				g_Config.m_AmfCustomAspectRatioMode = 2;
				g_Config.m_AmfCustomAspectRatioNum = InputNum;
				g_Config.m_AmfCustomAspectRatioDen = InputDen;
				g_Config.m_AmfCustomAspectRatio = std::clamp((int)std::lround((double)InputNum * 100.0 / (double)InputDen), 100, 1000);
				s_AmfAspectLastNum = InputNum;
				s_AmfAspectLastDen = InputDen;
				GameClient()->m_TClient.SetForcedAspect();
			}
		}
		else
		{
			s_AmfAspectInputInitialized = false;
			s_AmfAspectLastNum = -1;
			s_AmfAspectLastDen = -1;
		}

		AspectCardContent.HSplitTop(MarginSmall, nullptr, &AspectCardContent);
		CUIRect AspectSummary;
		AspectCardContent.HSplitTop(AspectSummaryHeight, &AspectSummary, &AspectCardContent);
		const float CurrentAspect = Graphics()->ScreenAspect();
		char aAspectSummary[128];
		str_format(aAspectSummary, sizeof(aAspectSummary), "%s %.3f", TCLocalize("Selected aspect:", "AMF Client"), CurrentAspect);
		Ui()->DoLabel(&AspectSummary, aAspectSummary, 11.0f, TEXTALIGN_MC);

		if(AspectBlocked)
		{
			AspectCardContent.HSplitTop(MarginSmall, nullptr, &AspectCardContent);
			AspectCardContent.HSplitTop(LineSize, &Label, &AspectCardContent);
			SLabelProperties BlockedProperties;
			BlockedProperties.SetColor(ColorRGBA(1.0f, 0.4f, 0.4f, 1.0f));
			Ui()->DoLabel(&Label, TCLocalize("This feature is blocked on this server", "AMF Client"), 11.0f, TEXTALIGN_ML, BlockedProperties);
		}
		VisualRightColumn.HSplitTop(VisualCardGap, nullptr, &VisualRightColumn);
		const float TuneZoneColorCardHeight = VisualCardPadding * 2.0f + LineSize + MarginExtraSmall + LineSize;
		CUIRect TuneZoneColorCardContent = BeginVisualCard(VisualRightColumn, TuneZoneColorCardHeight);
		RenderTuneZoneColorSetting(TuneZoneColorCardContent);
		VisualRightColumn.HSplitTop(VisualCardGap, nullptr, &VisualRightColumn);

		// The idle Visuals page keeps its regular cards compact; the full aspect
		// preview is rendered only while its confirmation test is active above.
		const float FancyWeaponsCardHeight = VisualCardPadding * 2.0f +
			LineSize + MarginExtraSmall + LineSize + 48.0f + MarginExtraSmall + LineSize + 48.0f;
		Content = BeginVisualCard(VisualRightColumn, FancyWeaponsCardHeight);
		Content.HSplitTop(LineSize, &Button, &Content);
		static CButtonContainer s_AmfFancyWeaponsButton;
		DoButton_CheckBoxAutoVMarginAndSet(
			&s_AmfFancyWeaponsButton,
			TCLocalize("Fancy Weapons", "AMF Client"),
			&g_Config.m_AmfFancyWeapons,
			&Button,
			LineSize);
		GameClient()->m_Tooltips.DoToolTip(
			&s_AmfFancyWeaponsButton,
			&Button,
			TCLocalize("Renders rifle and shotgun lasers with the AMF visual effect without changing gameplay.", "AMF Client"));
		Content.HSplitTop(MarginExtraSmall, nullptr, &Content);
		Content.HSplitTop(LineSize, &Label, &Content);
		Ui()->DoLabel(&Label, TCLocalize("Rifle preview", "AMF Client"), 12.0f, TEXTALIGN_ML);
		Content.HSplitTop(48.0f, &Button, &Content);
		DoLaserPreview(&Button, ColorHSLA(g_Config.m_ClLaserRifleOutlineColor), ColorHSLA(g_Config.m_ClLaserRifleInnerColor), LASERTYPE_RIFLE);
		Content.HSplitTop(MarginExtraSmall, nullptr, &Content);
		Content.HSplitTop(LineSize, &Label, &Content);
		Ui()->DoLabel(&Label, TCLocalize("Shotgun preview", "AMF Client"), 12.0f, TEXTALIGN_ML);
		Content.HSplitTop(48.0f, &Button, &Content);
		DoLaserPreview(&Button, ColorHSLA(g_Config.m_ClLaserShotgunOutlineColor), ColorHSLA(g_Config.m_ClLaserShotgunInnerColor), LASERTYPE_SHOTGUN);

		VisualRightColumn.HSplitTop(VisualCardGap, nullptr, &VisualRightColumn);
		static CButtonContainer s_AmfKeyIndicatorButton;
		static CSmoothUiSectionAnimation s_KeyIndicatorSettingsSection;
		const bool KeyIndicatorEnabled = g_Config.m_AmfKeyIndicator != 0;
		float KeyIndicatorSettingsProgress = s_KeyIndicatorSettingsSection.Update(
			KeyIndicatorEnabled,
			Client()->RenderFrameTime(),
			DependentSettingsDuration,
			AnimateDependentSettings);
		// Style is deliberately exposed only from the HUD Editor. The regular
		// Visuals page keeps the two display toggles for the Key Indicator family here.
		constexpr float FullKeyIndicatorSettingsHeight = 2.0f * (MarginExtraSmall + LineSize);
		const bool KeyIndicatorSettingsVisible = s_KeyIndicatorSettingsSection.IsVisible();
		const float KeyIndicatorSettingsHeight = KeyIndicatorSettingsVisible ?
			std::round(FullKeyIndicatorSettingsHeight * KeyIndicatorSettingsProgress / UiPixelSize) * UiPixelSize :
			0.0f;
		const float KeyIndicatorCardHeight = VisualCardPadding * 2.0f + LineSize + KeyIndicatorSettingsHeight;
		Content = BeginVisualCard(VisualRightColumn, KeyIndicatorCardHeight);
		Content.HSplitTop(LineSize, &Button, &Content);
		if(DoButton_CheckBoxAutoVMarginAndSet(
			&s_AmfKeyIndicatorButton,
			TCLocalize("Enable Key Indicator", "AMF Client"),
			&g_Config.m_AmfKeyIndicator,
			&Button,
			LineSize))
		{
			KeyIndicatorSettingsProgress = s_KeyIndicatorSettingsSection.Update(
				g_Config.m_AmfKeyIndicator != 0,
				0.0f,
				DependentSettingsDuration,
				AnimateDependentSettings);
		}
		GameClient()->m_Tooltips.DoToolTip(
			&s_AmfKeyIndicatorButton,
			&Button,
			TCLocalize("Shows movement, jump, fire and hook keys.", "AMF Client"));

		if(KeyIndicatorSettingsVisible)
		{
			CUIRect KeyIndicatorSettingsClip;
			Content.HSplitTop(KeyIndicatorSettingsHeight, &KeyIndicatorSettingsClip, &Content);
			CUIRect KeyIndicatorSettingsContent = KeyIndicatorSettingsClip;
			KeyIndicatorSettingsContent.h = FullKeyIndicatorSettingsHeight;
			KeyIndicatorSettingsContent.y += std::round((1.0f - KeyIndicatorSettingsProgress) * 3.0f / UiPixelSize) * UiPixelSize;
			Ui()->ClipEnable(&KeyIndicatorSettingsClip);
			const ColorRGBA OldKeyIndicatorTextColor = TextRender()->GetTextColor();
			const ColorRGBA OldKeyIndicatorOutlineColor = TextRender()->GetTextOutlineColor();
			TextRender()->TextColor(OldKeyIndicatorTextColor.WithMultipliedAlpha(KeyIndicatorSettingsProgress));
			TextRender()->TextOutlineColor(OldKeyIndicatorOutlineColor.WithMultipliedAlpha(KeyIndicatorSettingsProgress));
			const bool KeyIndicatorSettingsInteractive = s_KeyIndicatorSettingsSection.AcceptsInput();

			KeyIndicatorSettingsContent.HSplitTop(MarginExtraSmall, nullptr, &KeyIndicatorSettingsContent);
			KeyIndicatorSettingsContent.HSplitTop(LineSize, &Button, &KeyIndicatorSettingsContent);
			static CButtonContainer s_AmfKeyIndicatorShowCpsButton;
			static CButtonContainer s_AmfMouseIndicatorButton;
			if(KeyIndicatorSettingsInteractive)
			{
				DoButton_CheckBoxAutoVMarginAndSet(
					&s_AmfKeyIndicatorShowCpsButton,
					TCLocalize("Show CPS", "AMF Client"),
					&g_Config.m_AmfKeyIndicatorShowCps,
					&Button,
					LineSize);
			}
			else
			{
				if(Ui()->CheckActiveItem(&s_AmfKeyIndicatorShowCpsButton))
					Ui()->SetActiveItem(nullptr);
				CUIRect CheckBox, CheckLabel;
				Button.VSplitLeft(Button.h, &CheckBox, &CheckLabel);
				CheckLabel.VSplitLeft(5.0f, nullptr, &CheckLabel);
				CheckBox.Margin(2.0f, &CheckBox);
				CheckBox.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f * KeyIndicatorSettingsProgress), IGraphics::CORNER_ALL, 3.0f);
				if(g_Config.m_AmfKeyIndicatorShowCps)
				{
					TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
					TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
					Ui()->DoLabel(&CheckBox, FontIcon::XMARK, CheckBox.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
					TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
					TextRender()->SetRenderFlags(0);
				}
				Ui()->DoLabel(&CheckLabel, TCLocalize("Show CPS", "AMF Client"), CheckBox.h * CUi::ms_FontmodHeight, TEXTALIGN_ML);
			}
			KeyIndicatorSettingsContent.HSplitTop(MarginExtraSmall, nullptr, &KeyIndicatorSettingsContent);
			KeyIndicatorSettingsContent.HSplitTop(LineSize, &Button, &KeyIndicatorSettingsContent);
			if(KeyIndicatorSettingsInteractive)
			{
				DoButton_CheckBoxAutoVMarginAndSet(
					&s_AmfMouseIndicatorButton,
					TCLocalize("Show Mouse Indicator", "AMF Client"),
					&g_Config.m_AmfMouseIndicator,
					&Button,
					LineSize);
			}
			else
			{
				if(Ui()->CheckActiveItem(&s_AmfMouseIndicatorButton))
					Ui()->SetActiveItem(nullptr);
				CUIRect CheckBox, CheckLabel;
				Button.VSplitLeft(Button.h, &CheckBox, &CheckLabel);
				CheckLabel.VSplitLeft(5.0f, nullptr, &CheckLabel);
				CheckBox.Margin(2.0f, &CheckBox);
				CheckBox.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f * KeyIndicatorSettingsProgress), IGraphics::CORNER_ALL, 3.0f);
				if(g_Config.m_AmfMouseIndicator)
				{
					TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
					TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
					Ui()->DoLabel(&CheckBox, FontIcon::XMARK, CheckBox.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
					TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
					TextRender()->SetRenderFlags(0);
				}
				Ui()->DoLabel(&CheckLabel, TCLocalize("Show Mouse Indicator", "AMF Client"), CheckBox.h * CUi::ms_FontmodHeight, TEXTALIGN_ML);
			}
			TextRender()->TextColor(OldKeyIndicatorTextColor);
			TextRender()->TextOutlineColor(OldKeyIndicatorOutlineColor);
			Ui()->ClipDisable();
		}
		if(KeyIndicatorEnabled)
		{
			VisualRightColumn.HSplitTop(VisualCardGap, nullptr, &VisualRightColumn);
			CUIRect KeyIndicatorHudEditorCardContent = BeginVisualCard(VisualRightColumn, VisualCardPadding * 2.0f + LineSize);
			CUIRect KeyIndicatorHudEditorButton;
			KeyIndicatorHudEditorCardContent.HSplitTop(LineSize, &KeyIndicatorHudEditorButton, &KeyIndicatorHudEditorCardContent);
			static CButtonContainer s_AmfKeyIndicatorHudEditorButton;
			RenderModuleHudEditorButton(KeyIndicatorHudEditorButton, &s_AmfKeyIndicatorHudEditorButton);
		}

		VisualRightColumn.HSplitTop(VisualCardGap, nullptr, &VisualRightColumn);
		const bool MusicPlayerEnabled = g_Config.m_AmfMusicPlayer != 0;
		const bool ShowMusicPlayerHudEditorButton = !KeyIndicatorEnabled && MusicPlayerEnabled;
		// The selected color mode is layout state, not visibility state. Keeping it
		// independent of the master checkbox prevents the closing card from losing
		// a row halfway through its animation.
		const bool MusicPlayerShowsStaticColor = g_Config.m_AmfMusicPlayerColorMode == 0;
		static CSmoothUiSectionAnimation s_MusicPlayerSettingsSection;
		const bool AnimateMusicPlayerSettings = g_Config.m_AmfSmoothHud && SmoothHudWindowActive && g_Config.m_AmfAnimMenu;
		float MusicPlayerSettingsProgress = s_MusicPlayerSettingsSection.Update(
			MusicPlayerEnabled,
			Client()->RenderFrameTime(),
			180,
			AnimateMusicPlayerSettings);
		const float MusicPlayerExpandedSettingsHeight =
			8.0f * (MarginExtraSmall + LineSize) +
			(MusicPlayerShowsStaticColor ? MarginExtraSmall + ColorPickerLineSize : 0.0f);
		const bool MusicPlayerSettingsVisible = s_MusicPlayerSettingsSection.IsVisible();
		const float MusicPlayerSettingsHeight = MusicPlayerSettingsVisible ?
			std::round(MusicPlayerExpandedSettingsHeight * MusicPlayerSettingsProgress / UiPixelSize) * UiPixelSize :
			0.0f;
		const float MusicPlayerHudEditorButtonHeight = ShowMusicPlayerHudEditorButton ? MarginExtraSmall + LineSize : 0.0f;
		const float MusicPlayerCardHeight = VisualCardPadding * 2.0f + LineSize + MusicPlayerSettingsHeight + MusicPlayerHudEditorButtonHeight;
		CUIRect MusicPlayerCardContent = BeginVisualCard(VisualRightColumn, MusicPlayerCardHeight);
		CUIRect MusicPlayerContent, ContentAfterMusicPlayer;
		MusicPlayerCardContent.HSplitTop(LineSize, &MusicPlayerContent, &ContentAfterMusicPlayer);
		MusicPlayerContent.HSplitTop(LineSize, &Button, &MusicPlayerContent);
		static CButtonContainer s_AmfMusicPlayerButton;
		const bool MusicPlayerChanged = DoButton_CheckBoxAutoVMarginAndSet(&s_AmfMusicPlayerButton, TCLocalize("Enable Music Player", "AMF Client"), &g_Config.m_AmfMusicPlayer, &Button, LineSize);
		if(MusicPlayerChanged)
		{
			MusicPlayerSettingsProgress = s_MusicPlayerSettingsSection.Update(
				g_Config.m_AmfMusicPlayer != 0,
				0.0f,
				180,
				AnimateMusicPlayerSettings);
		}
		GameClient()->m_Tooltips.DoToolTip(&s_AmfMusicPlayerButton, &Button, TCLocalize("Shows the current Windows media session in the HUD.", "AMF Client"));
		if(MusicPlayerSettingsVisible)
		{
			CUIRect MusicPlayerSettingsClip;
			ContentAfterMusicPlayer.HSplitTop(MusicPlayerSettingsHeight, &MusicPlayerSettingsClip, &ContentAfterMusicPlayer);
			MusicPlayerContent = MusicPlayerSettingsClip;
			MusicPlayerContent.h = MusicPlayerExpandedSettingsHeight;
			MusicPlayerContent.y += std::round((1.0f - MusicPlayerSettingsProgress) * 3.0f / UiPixelSize) * UiPixelSize;
			Ui()->ClipEnable(&MusicPlayerSettingsClip);
			const ColorRGBA OldMusicPlayerTextColor = TextRender()->GetTextColor();
			const ColorRGBA OldMusicPlayerOutlineColor = TextRender()->GetTextOutlineColor();
			TextRender()->TextColor(OldMusicPlayerTextColor.WithMultipliedAlpha(MusicPlayerSettingsProgress));
			TextRender()->TextOutlineColor(OldMusicPlayerOutlineColor.WithMultipliedAlpha(MusicPlayerSettingsProgress));
			const bool MusicPlayerSettingsInteractive = s_MusicPlayerSettingsSection.AcceptsInput();
			if(MusicPlayerSettingsInteractive)
			{
			MusicPlayerContent.HSplitTop(MarginExtraSmall, nullptr, &MusicPlayerContent);
			MusicPlayerContent.HSplitTop(LineSize, &Button, &MusicPlayerContent);
			CUIRect MusicPlayerTitle, MusicPlayerResetButton;
			Button.VSplitRight(LineSize + 8.0f, &MusicPlayerTitle, &MusicPlayerResetButton);
			Ui()->DoLabel(&MusicPlayerTitle, TCLocalize("Music Player", "AMF Client"), 12.0f, TEXTALIGN_ML);

			static CButtonContainer s_AmfMusicPlayerResetButton;
			if(Ui()->DoButton_FontIcon(&s_AmfMusicPlayerResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &MusicPlayerResetButton, BUTTONFLAG_LEFT))
			{
				g_Config.m_AmfMusicPlayerColorMode = 1;
				g_Config.m_AmfMusicPlayerStaticColor = 128;
				g_Config.m_AmfMusicPlayerTextScale = 110;
				g_Config.m_AmfMusicPlayerVisualizerMode = 1;
				g_Config.m_AmfMusicPlayerVisualizerRounding = 0;
				g_Config.m_AmfMusicPlayerVisualizerColumns = 5;
				g_Config.m_AmfMusicPlayerShowLyrics = 1;
				g_Config.m_AmfMusicPlayerShowCurrentTime = 1;
			}
			GameClient()->m_Tooltips.DoToolTip(&s_AmfMusicPlayerResetButton, &MusicPlayerResetButton, TCLocalize("Reset Music Player settings", "AMF Client"));

			auto DoMusicPlayerDropDown = [&](const char *pLabel, int &Value, const char **ppNames, int NumNames, CUi::SDropDownState &State, CScrollRegion &ScrollRegion) {
				MusicPlayerContent.HSplitTop(MarginExtraSmall, nullptr, &MusicPlayerContent);
				MusicPlayerContent.HSplitTop(LineSize, &Button, &MusicPlayerContent);
				CUIRect Label, Select;
				Button.VSplitLeft(std::min(150.0f, Button.w * 0.45f), &Label, &Select);
				Ui()->DoLabel(&Label, pLabel, 12.0f, TEXTALIGN_ML);
				State.m_SelectionPopupContext.m_pScrollRegion = &ScrollRegion;
				Value = Ui()->DoDropDown(&Select, std::clamp(Value, 0, NumNames - 1), ppNames, NumNames, State);
			};

			static CUi::SDropDownState s_AmfMusicPlayerColorModeState;
			static CScrollRegion s_AmfMusicPlayerColorModeScrollRegion;
			std::array<const char *, 2> aMusicPlayerColorModes = {TCLocalize("Static", "AMF Client"), TCLocalize("Cover", "AMF Client")};
			DoMusicPlayerDropDown(
				TCLocalize("Color Mode", "AMF Client"),
				g_Config.m_AmfMusicPlayerColorMode,
				aMusicPlayerColorModes.data(),
				(int)aMusicPlayerColorModes.size(),
				s_AmfMusicPlayerColorModeState,
				s_AmfMusicPlayerColorModeScrollRegion);
			if(g_Config.m_AmfMusicPlayerColorMode == 0)
			{
				static CButtonContainer s_AmfMusicPlayerStaticColorButton;
				MusicPlayerContent.HSplitTop(MarginExtraSmall, nullptr, &MusicPlayerContent);
				DoLine_ColorPicker(&s_AmfMusicPlayerStaticColorButton, ColorPickerLineSize, 13.0f, 0.0f, &MusicPlayerContent, TCLocalize("Static Color", "AMF Client"), &g_Config.m_AmfMusicPlayerStaticColor, ColorRGBA(0.34f, 0.53f, 0.79f, 1.0f), false);
			}

			static CUi::SDropDownState s_AmfMusicPlayerVisualizerModeState;
			static CScrollRegion s_AmfMusicPlayerVisualizerModeScrollRegion;
			std::array<const char *, 3> aMusicPlayerVisualizerModes = {TCLocalize("Bottom", "AMF Client"), TCLocalize("Center", "AMF Client"), TCLocalize("Up", "AMF Client")};
			DoMusicPlayerDropDown(
				TCLocalize("Visualizer Position", "AMF Client"),
				g_Config.m_AmfMusicPlayerVisualizerMode,
				aMusicPlayerVisualizerModes.data(),
				(int)aMusicPlayerVisualizerModes.size(),
				s_AmfMusicPlayerVisualizerModeState,
				s_AmfMusicPlayerVisualizerModeScrollRegion);

			MusicPlayerContent.HSplitTop(MarginExtraSmall, nullptr, &MusicPlayerContent);
			MusicPlayerContent.HSplitTop(LineSize, &Button, &MusicPlayerContent);
			Ui()->DoScrollbarOption(&g_Config.m_AmfMusicPlayerTextScale, &g_Config.m_AmfMusicPlayerTextScale, &Button, TCLocalize("Text Scale", "AMF Client"), 70, 150, &CUi::ms_LinearScrollbarScale, 0u, "%");
			MusicPlayerContent.HSplitTop(MarginExtraSmall, nullptr, &MusicPlayerContent);
			MusicPlayerContent.HSplitTop(LineSize, &Button, &MusicPlayerContent);
			Ui()->DoScrollbarOption(&g_Config.m_AmfMusicPlayerVisualizerColumns, &g_Config.m_AmfMusicPlayerVisualizerColumns, &Button, TCLocalize("Columns", "AMF Client"), 5, 10);
			MusicPlayerContent.HSplitTop(MarginExtraSmall, nullptr, &MusicPlayerContent);
			MusicPlayerContent.HSplitTop(LineSize, &Button, &MusicPlayerContent);
			static CButtonContainer s_AmfMusicPlayerShowLyricsButton;
			DoButton_CheckBoxAutoVMarginAndSet(&s_AmfMusicPlayerShowLyricsButton, TCLocalize("Show Lyrics", "AMF Client"), &g_Config.m_AmfMusicPlayerShowLyrics, &Button, LineSize);
			MusicPlayerContent.HSplitTop(MarginExtraSmall, nullptr, &MusicPlayerContent);
			MusicPlayerContent.HSplitTop(LineSize, &Button, &MusicPlayerContent);
			static CButtonContainer s_AmfMusicPlayerShowCurrentTimeButton;
			DoButton_CheckBoxAutoVMarginAndSet(&s_AmfMusicPlayerShowCurrentTimeButton, TCLocalize("Show Current Time", "AMF Client"), &g_Config.m_AmfMusicPlayerShowCurrentTime, &Button, LineSize);

			MusicPlayerContent.HSplitTop(MarginExtraSmall, nullptr, &MusicPlayerContent);
			MusicPlayerContent.HSplitTop(LineSize, &Button, &MusicPlayerContent);
			CUIRect RoundingLabel, RoundingButtons, CubeButton, SoftButton;
			Button.VSplitLeft(std::min(150.0f, Button.w * 0.45f), &RoundingLabel, &RoundingButtons);
			Ui()->DoLabel(&RoundingLabel, TCLocalize("Rounding", "AMF Client"), 12.0f, TEXTALIGN_ML);
			RoundingButtons.VSplitMid(&CubeButton, &SoftButton, MarginExtraSmall);
			static CButtonContainer s_AmfMusicPlayerCubeButton;
			static CButtonContainer s_AmfMusicPlayerSoftButton;
			if(DoButton_Menu(&s_AmfMusicPlayerCubeButton, TCLocalize("Cube", "AMF Client"), g_Config.m_AmfMusicPlayerVisualizerRounding < 100, &CubeButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
				g_Config.m_AmfMusicPlayerVisualizerRounding = 0;
			if(DoButton_Menu(&s_AmfMusicPlayerSoftButton, TCLocalize("Soft", "AMF Client"), g_Config.m_AmfMusicPlayerVisualizerRounding >= 100, &SoftButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
				g_Config.m_AmfMusicPlayerVisualizerRounding = 200;
			}
			else
			{
				// Draw the same controls while the section is moving, but without
				// registering buttons, sliders or popup hitboxes.
				Ui()->SetActiveItem(nullptr);
				Ui()->SetHotItem(nullptr);
				auto DrawPassiveDropDown = [&](const char *pLabel, int Value, const std::array<const char *, 3> &aNames) {
					MusicPlayerContent.HSplitTop(MarginExtraSmall, nullptr, &MusicPlayerContent);
					MusicPlayerContent.HSplitTop(LineSize, &Button, &MusicPlayerContent);
					CUIRect DropDownLabel, Select;
					Button.VSplitLeft(std::min(150.0f, Button.w * 0.45f), &DropDownLabel, &Select);
					Ui()->DoLabel(&DropDownLabel, pLabel, 12.0f, TEXTALIGN_ML);
					Select.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f * MusicPlayerSettingsProgress), IGraphics::CORNER_ALL, 4.0f);
					Select.Margin(5.0f, &Select);
					Ui()->DoLabel(&Select, aNames[std::clamp(Value, 0, (int)aNames.size() - 1)], 12.0f, TEXTALIGN_ML);
				};
				auto DrawPassiveSlider = [&](const char *pLabel, int Value, int Min, int Max, const char *pSuffix) {
					MusicPlayerContent.HSplitTop(MarginExtraSmall, nullptr, &MusicPlayerContent);
					MusicPlayerContent.HSplitTop(LineSize, &Button, &MusicPlayerContent);
					CUIRect SliderLabel, Slider;
					Button.VSplitMid(&SliderLabel, &Slider, std::min(10.0f, Button.w * 0.05f));
					char aValue[128];
					str_format(aValue, sizeof(aValue), "%s: %d%s", pLabel, Value, pSuffix);
					Ui()->DoLabel(&SliderLabel, aValue, SliderLabel.h * CUi::ms_FontmodHeight * 0.8f, TEXTALIGN_ML);
					CUIRect Rail;
					Slider.HMargin(5.0f, &Rail);
					Rail.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f * MusicPlayerSettingsProgress), IGraphics::CORNER_ALL, Rail.h / 2.0f);
					CUIRect Handle;
					Rail.VSplitLeft(std::clamp(33.0f, Rail.h, Rail.w / 3.0f), &Handle, nullptr);
					const float RelativeValue = Max == Min ? 0.0f : std::clamp((Value - Min) / (float)(Max - Min), 0.0f, 1.0f);
					Handle.x += (Rail.w - Handle.w) * RelativeValue;
					Handle.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f * MusicPlayerSettingsProgress), IGraphics::CORNER_ALL, Rail.h / 2.0f);
				};

				MusicPlayerContent.HSplitTop(MarginExtraSmall, nullptr, &MusicPlayerContent);
				MusicPlayerContent.HSplitTop(LineSize, &Button, &MusicPlayerContent);
				CUIRect MusicPlayerTitle, MusicPlayerResetButton;
				Button.VSplitRight(LineSize + 8.0f, &MusicPlayerTitle, &MusicPlayerResetButton);
				Ui()->DoLabel(&MusicPlayerTitle, TCLocalize("Music Player", "AMF Client"), 12.0f, TEXTALIGN_ML);
				MusicPlayerResetButton.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.20f * MusicPlayerSettingsProgress), IGraphics::CORNER_ALL, 4.0f);
				TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
				TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
				Ui()->DoLabel(&MusicPlayerResetButton, FontIcon::ARROW_ROTATE_LEFT, MusicPlayerResetButton.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
				TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
				TextRender()->SetRenderFlags(0);

				DrawPassiveDropDown(TCLocalize("Color Mode", "AMF Client"), std::clamp(g_Config.m_AmfMusicPlayerColorMode, 0, 1), {TCLocalize("Static", "AMF Client"), TCLocalize("Cover", "AMF Client")});
				if(MusicPlayerShowsStaticColor)
				{
					MusicPlayerContent.HSplitTop(MarginExtraSmall, nullptr, &MusicPlayerContent);
					MusicPlayerContent.HSplitTop(ColorPickerLineSize, &Button, &MusicPlayerContent);
					CUIRect ColorLabel, ColorPreview;
					Button.VSplitRight(ColorPickerLineSize, &ColorLabel, &ColorPreview);
					Ui()->DoLabel(&ColorLabel, TCLocalize("Static Color", "AMF Client"), 13.0f, TEXTALIGN_ML);
					ColorPreview.Margin(2.0f, &ColorPreview);
					ColorPreview.Draw(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_AmfMusicPlayerStaticColor, true)).WithMultipliedAlpha(MusicPlayerSettingsProgress), IGraphics::CORNER_ALL, 3.0f);
				}
				DrawPassiveDropDown(TCLocalize("Visualizer Position", "AMF Client"), g_Config.m_AmfMusicPlayerVisualizerMode, {TCLocalize("Bottom", "AMF Client"), TCLocalize("Center", "AMF Client"), TCLocalize("Up", "AMF Client")});
				DrawPassiveSlider(TCLocalize("Text Scale", "AMF Client"), g_Config.m_AmfMusicPlayerTextScale, 70, 150, "%");
				DrawPassiveSlider(TCLocalize("Columns", "AMF Client"), g_Config.m_AmfMusicPlayerVisualizerColumns, 5, 10, "");
				auto DrawPassiveCheckBox = [&](const char *pLabel, bool Checked) {
					MusicPlayerContent.HSplitTop(MarginExtraSmall, nullptr, &MusicPlayerContent);
					MusicPlayerContent.HSplitTop(LineSize, &Button, &MusicPlayerContent);
					CUIRect CheckBox, CheckLabel;
					Button.VSplitLeft(Button.h, &CheckBox, &CheckLabel);
					CheckLabel.VSplitLeft(5.0f, nullptr, &CheckLabel);
					CheckBox.Margin(2.0f, &CheckBox);
					CheckBox.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f * MusicPlayerSettingsProgress), IGraphics::CORNER_ALL, 3.0f);
					if(Checked)
					{
						TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
						TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
						Ui()->DoLabel(&CheckBox, FontIcon::XMARK, CheckBox.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
						TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
						TextRender()->SetRenderFlags(0);
					}
					Ui()->DoLabel(&CheckLabel, pLabel, CheckBox.h * CUi::ms_FontmodHeight, TEXTALIGN_ML);
				};
				DrawPassiveCheckBox(TCLocalize("Show Lyrics", "AMF Client"), g_Config.m_AmfMusicPlayerShowLyrics != 0);
				DrawPassiveCheckBox(TCLocalize("Show Current Time", "AMF Client"), g_Config.m_AmfMusicPlayerShowCurrentTime != 0);

				MusicPlayerContent.HSplitTop(MarginExtraSmall, nullptr, &MusicPlayerContent);
				MusicPlayerContent.HSplitTop(LineSize, &Button, &MusicPlayerContent);
				CUIRect RoundingLabel, RoundingButtons, CubeButton, SoftButton;
				Button.VSplitLeft(std::min(150.0f, Button.w * 0.45f), &RoundingLabel, &RoundingButtons);
				Ui()->DoLabel(&RoundingLabel, TCLocalize("Rounding", "AMF Client"), 12.0f, TEXTALIGN_ML);
				RoundingButtons.VSplitMid(&CubeButton, &SoftButton, MarginExtraSmall);
				CubeButton.Draw(g_Config.m_AmfMusicPlayerVisualizerRounding < 100 ? ColorRGBA(0.60f, 0.60f, 0.60f, 0.50f * MusicPlayerSettingsProgress) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f * MusicPlayerSettingsProgress), IGraphics::CORNER_L, 4.0f);
				SoftButton.Draw(g_Config.m_AmfMusicPlayerVisualizerRounding >= 100 ? ColorRGBA(0.60f, 0.60f, 0.60f, 0.50f * MusicPlayerSettingsProgress) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f * MusicPlayerSettingsProgress), IGraphics::CORNER_R, 4.0f);
				Ui()->DoLabel(&CubeButton, TCLocalize("Cube", "AMF Client"), 12.0f, TEXTALIGN_MC);
				Ui()->DoLabel(&SoftButton, TCLocalize("Soft", "AMF Client"), 12.0f, TEXTALIGN_MC);
			}
			Ui()->ClipDisable();
			TextRender()->TextColor(OldMusicPlayerTextColor);
			TextRender()->TextOutlineColor(OldMusicPlayerOutlineColor);
		}
		if(ShowMusicPlayerHudEditorButton)
		{
			ContentAfterMusicPlayer.HSplitTop(MarginExtraSmall, nullptr, &ContentAfterMusicPlayer);
			CUIRect MusicPlayerHudEditorButton;
			ContentAfterMusicPlayer.HSplitTop(LineSize, &MusicPlayerHudEditorButton, &ContentAfterMusicPlayer);
			static CButtonContainer s_AmfMusicPlayerHudEditorButton;
			RenderModuleHudEditorButton(MusicPlayerHudEditorButton, &s_AmfMusicPlayerHudEditorButton);
		}
		VisualRightColumn.HSplitTop(VisualCardGap, nullptr, &VisualRightColumn);
		static CButtonContainer s_AmfMotionBlurEnabledButton;
		static bool s_MotionBlurSettingsAutoScrollRequested = false;
		static CSmoothUiSectionAnimation s_MotionBlurSettingsSection;
		float MotionBlurSettingsProgress = s_MotionBlurSettingsSection.Update(
			g_Config.m_AmfMotionBlurEnabled != 0,
			Client()->RenderFrameTime(),
			DependentSettingsDuration,
			AnimateDependentSettings);
		constexpr float FullMotionBlurSettingsHeight = MarginExtraSmall + LineSize + MarginExtraSmall + LineSize;
		const bool MotionBlurSettingsVisible = s_MotionBlurSettingsSection.IsVisible();
		const float MotionBlurSettingsHeight = MotionBlurSettingsVisible ? std::round(FullMotionBlurSettingsHeight * MotionBlurSettingsProgress / UiPixelSize) * UiPixelSize : 0.0f;
		const float MotionBlurCardHeight = VisualCardPadding * 2.0f + LineSize + MotionBlurSettingsHeight;
		Content = BeginVisualCard(VisualRightColumn, MotionBlurCardHeight);
		Content.HSplitTop(LineSize, &Button, &Content);
		const CUIRect MotionBlurToggleRow = Button;
		if(DoButton_CheckBoxAutoVMarginAndSet(
			&s_AmfMotionBlurEnabledButton,
			TCLocalize("Motion Blur", "AMF Client"),
			&g_Config.m_AmfMotionBlurEnabled,
			&Button,
			LineSize))
		{
			MotionBlurSettingsProgress = s_MotionBlurSettingsSection.Update(
				g_Config.m_AmfMotionBlurEnabled != 0,
				0.0f,
				DependentSettingsDuration,
				AnimateDependentSettings);
			if(g_Config.m_AmfMotionBlurEnabled)
				s_MotionBlurSettingsAutoScrollRequested = true;
		}
		GameClient()->m_Tooltips.DoToolTip(
			&s_AmfMotionBlurEnabledButton,
			&Button,
			TCLocalize("Blurs the game world slightly while it moves.", "AMF Client"));
		if(MotionBlurSettingsVisible)
		{
			CUIRect MotionBlurSettingsClip;
			Content.HSplitTop(MotionBlurSettingsHeight, &MotionBlurSettingsClip, &Content);
			CUIRect MotionBlurSettingsContent = MotionBlurSettingsClip;
			MotionBlurSettingsContent.h = FullMotionBlurSettingsHeight;
			MotionBlurSettingsContent.y += std::round((1.0f - MotionBlurSettingsProgress) * 3.0f / UiPixelSize) * UiPixelSize;
			Ui()->ClipEnable(&MotionBlurSettingsClip);
			const ColorRGBA OldTextColor = TextRender()->GetTextColor();
			const ColorRGBA OldOutlineColor = TextRender()->GetTextOutlineColor();
			TextRender()->TextColor(OldTextColor.WithMultipliedAlpha(MotionBlurSettingsProgress));
			TextRender()->TextOutlineColor(OldOutlineColor.WithMultipliedAlpha(MotionBlurSettingsProgress));
			const bool MotionBlurSettingsInteractive = s_MotionBlurSettingsSection.AcceptsInput();

			MotionBlurSettingsContent.HSplitTop(MarginExtraSmall, nullptr, &MotionBlurSettingsContent);
			MotionBlurSettingsContent.HSplitTop(LineSize, &Button, &MotionBlurSettingsContent);
			if(MotionBlurSettingsInteractive)
			{
				Ui()->DoScrollbarOption(
					&g_Config.m_AmfMotionBlur,
					&g_Config.m_AmfMotionBlur,
					&Button,
					TCLocalize("Strength", "AMF Client"),
					0,
					100,
					&CUi::ms_LinearScrollbarScale,
					0,
					"%");
				GameClient()->m_Tooltips.DoToolTip(
					&g_Config.m_AmfMotionBlur,
					&Button,
					TCLocalize("Controls the strength of the motion blur effect.", "AMF Client"));
			}
			else
			{
				CUIRect SliderLabel, Slider;
				Button.VSplitMid(&SliderLabel, &Slider, std::min(10.0f, Button.w * 0.05f));
				char aStrength[128];
				str_format(aStrength, sizeof(aStrength), "%s: %d%%", TCLocalize("Strength", "AMF Client"), g_Config.m_AmfMotionBlur);
				Ui()->DoLabel(&SliderLabel, aStrength, SliderLabel.h * CUi::ms_FontmodHeight * 0.8f, TEXTALIGN_ML);
				CUIRect Rail;
				Slider.HMargin(5.0f, &Rail);
				Rail.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f * MotionBlurSettingsProgress), IGraphics::CORNER_ALL, Rail.h / 2.0f);
				CUIRect Handle;
				Rail.VSplitLeft(std::clamp(33.0f, Rail.h, Rail.w / 3.0f), &Handle, nullptr);
				Handle.x += (Rail.w - Handle.w) * std::clamp(g_Config.m_AmfMotionBlur / 100.0f, 0.0f, 1.0f);
				Handle.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f * MotionBlurSettingsProgress), IGraphics::CORNER_ALL, Rail.h / 2.0f);
			}

			MotionBlurSettingsContent.HSplitTop(MarginExtraSmall, nullptr, &MotionBlurSettingsContent);
			MotionBlurSettingsContent.HSplitTop(LineSize, &Label, &MotionBlurSettingsContent);
			SLabelProperties HintProps;
			int BackendMajor = 0;
			int BackendMinor = 0;
			int BackendPatch = 0;
			const char *pBackendName = "";
			const bool IsVulkan =
				Graphics()->GetDriverVersion(
					GRAPHICS_DRIVER_AGE_TYPE_DEFAULT,
					BackendMajor,
					BackendMinor,
					BackendPatch,
					pBackendName,
					BACKEND_TYPE_AUTO) &&
				str_comp_nocase(pBackendName, "Vulkan") == 0;
			HintProps.SetColor((IsVulkan ? ColorRGBA(0.68f, 0.74f, 0.86f, 1.0f) : ColorRGBA(0.58f, 0.62f, 0.72f, 1.0f)).WithMultipliedAlpha(MotionBlurSettingsProgress));
			Ui()->DoLabel(&Label, TCLocalize("Works only when using Vulkan.", "AMF Client"), 11.0f, TEXTALIGN_ML, HintProps);
			TextRender()->TextColor(OldTextColor);
			TextRender()->TextOutlineColor(OldOutlineColor);
			Ui()->ClipDisable();
		}
		if(s_MotionBlurSettingsAutoScrollRequested)
		{
			if(Input()->KeyPress(KEY_MOUSE_WHEEL_UP) || Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN))
				s_MotionBlurSettingsAutoScrollRequested = false;
			else if(MotionBlurSettingsProgress >= 0.60f)
			{
				CUIRect MotionBlurRevealRect = MotionBlurToggleRow;
				MotionBlurRevealRect.h += MarginExtraSmall + LineSize * 1.5f;
				m_AmfVisualScrollRegion.AddRect(MotionBlurRevealRect);
				m_AmfVisualScrollRegion.ScrollHereAnimated(CScrollRegion::SCROLLHERE_KEEP_IN_VIEW, 0.18f);
				s_MotionBlurSettingsAutoScrollRequested = false;
			}
		}

		// Keep Gradient Modules directly below Motion Blur in the right column.
		VisualRightColumn.HSplitTop(VisualCardGap, nullptr, &VisualRightColumn);
		const bool GradientModulesEnabled = g_Config.m_AmfGradientNicknames != 0;
		constexpr float GradientNicknamePreviewTeeHeight = 26.0f;
		constexpr float GradientNicknamePreviewLabelHeight = 10.0f;
		constexpr float GradientNicknamePreviewNicknameHeight = 14.0f;
		constexpr float GradientNicknamePreviewTeesHeight = GradientNicknamePreviewTeeHeight + GradientNicknamePreviewLabelHeight + GradientNicknamePreviewNicknameHeight;
		const float GradientNicknamePreviewHeight = GradientNicknamePreviewTeesHeight + 3.0f * MarginExtraSmall + 2.0f * LineSize;
		constexpr float GradientNicknamePreviewDividerHeight = 1.0f;
		const float GradientNicknameGeneralSettingsHeight = 8.0f * (MarginExtraSmall + LineSize);
		const float GradientNicknamePreviewHeaderHeight = 3.0f * MarginExtraSmall + LineSize + GradientNicknamePreviewDividerHeight;
		const float FullGradientModulesSettingsHeight = GradientNicknameGeneralSettingsHeight + GradientNicknamePreviewHeaderHeight + GradientNicknamePreviewHeight;
		static CSmoothUiSectionAnimation s_GradientModulesSettingsSection;
		float GradientModulesSettingsProgress = s_GradientModulesSettingsSection.Update(
			GradientModulesEnabled,
			Client()->RenderFrameTime(),
			DependentSettingsDuration,
			AnimateDependentSettings);
		const bool GradientModulesSettingsVisible = s_GradientModulesSettingsSection.IsVisible();
		const float GradientModulesSettingsHeight = GradientModulesSettingsVisible ?
			std::round(FullGradientModulesSettingsHeight * GradientModulesSettingsProgress / UiPixelSize) * UiPixelSize :
			0.0f;
		const float GradientModulesCardHeight = VisualCardPadding * 2.0f + LineSize + GradientModulesSettingsHeight;
		Content = BeginVisualCard(VisualRightColumn, GradientModulesCardHeight);
		Content.HSplitTop(LineSize, &Button, &Content);
		static CButtonContainer s_AmfGradientNicknamesButton;
		const bool GradientModulesChanged = DoButton_CheckBoxAutoVMarginAndSet(
			&s_AmfGradientNicknamesButton,
			TCLocalize("Gradient Nicknames", "AMF Client"),
			&g_Config.m_AmfGradientNicknames,
			&Button,
			LineSize);
		if(GradientModulesChanged)
		{
			GradientModulesSettingsProgress = s_GradientModulesSettingsSection.Update(
				g_Config.m_AmfGradientNicknames != 0,
				0.0f,
				DependentSettingsDuration,
				AnimateDependentSettings);
		}

		if(GradientModulesSettingsVisible)
		{
			CUIRect GradientModulesSettingsClip;
			Content.HSplitTop(GradientModulesSettingsHeight, &GradientModulesSettingsClip, &Content);
			CUIRect GradientModulesSettingsContent = GradientModulesSettingsClip;
			GradientModulesSettingsContent.h = FullGradientModulesSettingsHeight;
			GradientModulesSettingsContent.y += std::round((1.0f - GradientModulesSettingsProgress) * 3.0f / UiPixelSize) * UiPixelSize;
			Ui()->ClipEnable(&GradientModulesSettingsClip);
			const ColorRGBA OldGradientTextColor = TextRender()->GetTextColor();
			const ColorRGBA OldGradientOutlineColor = TextRender()->GetTextOutlineColor();
			TextRender()->TextColor(OldGradientTextColor.WithMultipliedAlpha(GradientModulesSettingsProgress));
			TextRender()->TextOutlineColor(OldGradientOutlineColor.WithMultipliedAlpha(GradientModulesSettingsProgress));
			const bool GradientModulesSettingsInteractive = s_GradientModulesSettingsSection.AcceptsInput();
			if(!GradientModulesSettingsInteractive)
			{
				Ui()->SetActiveItem(nullptr);
				Ui()->SetHotItem(nullptr);
			}

			auto DrawPassiveGradientCheck = [&](const CUIRect &Row, int Value, const char *pText) {
				CUIRect Box, CheckLabel;
				Row.VSplitLeft(Row.h, &Box, &CheckLabel);
				CheckLabel.VSplitLeft(5.0f, nullptr, &CheckLabel);
				Box.Margin(2.0f, &Box);
				Box.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f * GradientModulesSettingsProgress), IGraphics::CORNER_ALL, 3.0f);
				if(Value)
				{
					TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
					TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
					Ui()->DoLabel(&Box, FontIcon::XMARK, Box.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
					TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
					TextRender()->SetRenderFlags(0);
				}
				Ui()->DoLabel(&CheckLabel, TCLocalize(pText, "AMF Client"), Box.h * CUi::ms_FontmodHeight, TEXTALIGN_ML);
			};
			auto DrawPassiveGradientSegment = [&](const CUIRect &Rect, const char *pText, bool Selected, int Corners) {
				Rect.Draw(
					Selected ? ColorRGBA(0.60f, 0.60f, 0.60f, 0.50f * GradientModulesSettingsProgress) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f * GradientModulesSettingsProgress),
					Corners,
					4.0f);
				Ui()->DoLabel(&Rect, TCLocalize(pText, "AMF Client"), 12.0f, TEXTALIGN_MC);
			};

			GradientModulesSettingsContent.HSplitTop(MarginExtraSmall, nullptr, &GradientModulesSettingsContent);
			GradientModulesSettingsContent.HSplitTop(LineSize, &Label, &GradientModulesSettingsContent);
			Ui()->DoLabel(&Label, TCLocalize("Gradient Source", "AMF Client"), 12.0f, TEXTALIGN_ML);
			GradientModulesSettingsContent.HSplitTop(MarginExtraSmall, nullptr, &GradientModulesSettingsContent);
			GradientModulesSettingsContent.HSplitTop(LineSize, &Button, &GradientModulesSettingsContent);
			CUIRect GradientSourceSkin, GradientSourceTeam;
			Button.VSplitMid(&GradientSourceSkin, &GradientSourceTeam);
			static CButtonContainer s_AmfGradientSourceSkinButton;
			static CButtonContainer s_AmfGradientSourceTeamButton;
			if(GradientModulesSettingsInteractive)
			{
				if(DoButton_Menu(&s_AmfGradientSourceSkinButton, TCLocalize("Skin Color", "AMF Client"), g_Config.m_AmfGradientNicknameSource == 0, &GradientSourceSkin, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
					g_Config.m_AmfGradientNicknameSource = 0;
				if(DoButton_Menu(&s_AmfGradientSourceTeamButton, TCLocalize("Team/War", "AMF Client"), g_Config.m_AmfGradientNicknameSource == 1, &GradientSourceTeam, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
					g_Config.m_AmfGradientNicknameSource = 1;
			}
			else
			{
				DrawPassiveGradientSegment(GradientSourceSkin, "Skin Color", g_Config.m_AmfGradientNicknameSource == 0, IGraphics::CORNER_L);
				DrawPassiveGradientSegment(GradientSourceTeam, "Team/War", g_Config.m_AmfGradientNicknameSource == 1, IGraphics::CORNER_R);
			}

			static SGradientNicknameOtherPreview s_GradientNicknameOtherPreview;
			static SGradientNicknameSelfPreview s_GradientNicknameSelfPreview;
			if(!s_GradientNicknameOtherPreview.m_Initialized)
			{
				RandomizeGradientPreviewTee(s_GradientNicknameOtherPreview, s_GradientNicknameSelfPreview, GameClient()->m_Skins);
				RandomizeGradientPreviewTeam(s_GradientNicknameOtherPreview, GameClient());
			}

			GradientModulesSettingsContent.HSplitTop(MarginExtraSmall, nullptr, &GradientModulesSettingsContent);
			GradientModulesSettingsContent.HSplitTop(LineSize, &Button, &GradientModulesSettingsContent);
			CUIRect CustomGradientToggle, CustomGradientColor1, CustomGradientColor2, CustomGradientColors;
			Button.VSplitRight(LineSize * 2.0f + MarginExtraSmall, &CustomGradientToggle, &CustomGradientColors);
			CustomGradientColors.VSplitLeft(LineSize, &CustomGradientColor1, &CustomGradientColors);
			CustomGradientColors.VSplitLeft(MarginExtraSmall, nullptr, &CustomGradientColors);
			CustomGradientColor2 = CustomGradientColors;
			static CButtonContainer s_AmfGradientCustomSelfButton;
			if(GradientModulesSettingsInteractive)
			{
				DoButton_CheckBoxAutoVMarginAndSet(&s_AmfGradientCustomSelfButton, TCLocalize("Custom Gradient for Yourself", "AMF Client"), &g_Config.m_AmfGradientCustomSelf, &CustomGradientToggle, LineSize);
				DoButton_ColorPicker(&CustomGradientColor1, &g_Config.m_AmfGradientColor1, true);
				DoButton_ColorPicker(&CustomGradientColor2, &g_Config.m_AmfGradientColor2, true);
			}
			else
			{
				DrawPassiveGradientCheck(CustomGradientToggle, g_Config.m_AmfGradientCustomSelf, "Custom Gradient for Yourself");
				CustomGradientColor1.Margin(2.0f, &CustomGradientColor1);
				CustomGradientColor2.Margin(2.0f, &CustomGradientColor2);
				CustomGradientColor1.Draw(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_AmfGradientColor1, true)).WithMultipliedAlpha(GradientModulesSettingsProgress), IGraphics::CORNER_ALL, 3.0f);
				CustomGradientColor2.Draw(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_AmfGradientColor2, true)).WithMultipliedAlpha(GradientModulesSettingsProgress), IGraphics::CORNER_ALL, 3.0f);
			}

			auto DoGradientCheck = [&](CButtonContainer &ButtonContainer, int *pConfig, const char *pText) {
				GradientModulesSettingsContent.HSplitTop(MarginExtraSmall, nullptr, &GradientModulesSettingsContent);
				GradientModulesSettingsContent.HSplitTop(LineSize, &Button, &GradientModulesSettingsContent);
				if(GradientModulesSettingsInteractive)
					DoButton_CheckBoxAutoVMarginAndSet(&ButtonContainer, TCLocalize(pText, "AMF Client"), pConfig, &Button, LineSize);
				else
					DrawPassiveGradientCheck(Button, *pConfig, pText);
			};
			static CButtonContainer s_AmfGradientTeamColorsButton;
			static CButtonContainer s_AmfGradientSpectatorMenuButton;
			static CButtonContainer s_AmfGradientShowPlayerCustomButton;
			DoGradientCheck(s_AmfGradientShowPlayerCustomButton, &g_Config.m_AmfGradientShowPlayerCustom, "Show Custom Player Gradients");
			DoGradientCheck(s_AmfGradientTeamColorsButton, &g_Config.m_AmfGradientTeamColors, "Gradient Team Colors");
			DoGradientCheck(s_AmfGradientSpectatorMenuButton, &g_Config.m_AmfGradientSpectatorMenu, "Gradient Spectator Menu");

			GradientModulesSettingsContent.HSplitTop(MarginExtraSmall, nullptr, &GradientModulesSettingsContent);
			GradientModulesSettingsContent.HSplitTop(LineSize, &Button, &GradientModulesSettingsContent);
			CUIRect GradientPositionLabel, GradientPositionSelector, GradientPositionLeft, GradientPositionRight;
			Button.VSplitLeft(std::min(150.0f, Button.w * 0.45f), &GradientPositionLabel, &GradientPositionSelector);
			Ui()->DoLabel(&GradientPositionLabel, TCLocalize("Gradient Position", "AMF Client"), 12.0f, TEXTALIGN_ML);
			GradientPositionSelector.VSplitMid(&GradientPositionLeft, &GradientPositionRight);
			static CButtonContainer s_AmfGradientPositionLeftButton;
			static CButtonContainer s_AmfGradientPositionRightButton;
			if(GradientModulesSettingsInteractive)
			{
				if(DoButton_Menu(&s_AmfGradientPositionLeftButton, TCLocalize("Left", "AMF Client"), g_Config.m_AmfGradientPosition == 0, &GradientPositionLeft, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
					g_Config.m_AmfGradientPosition = 0;
				if(DoButton_Menu(&s_AmfGradientPositionRightButton, TCLocalize("Right", "AMF Client"), g_Config.m_AmfGradientPosition == 1, &GradientPositionRight, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
					g_Config.m_AmfGradientPosition = 1;
			}
			else
			{
				DrawPassiveGradientSegment(GradientPositionLeft, "Left", g_Config.m_AmfGradientPosition == 0, IGraphics::CORNER_L);
				DrawPassiveGradientSegment(GradientPositionRight, "Right", g_Config.m_AmfGradientPosition == 1, IGraphics::CORNER_R);
			}

			GradientModulesSettingsContent.HSplitTop(MarginExtraSmall, nullptr, &GradientModulesSettingsContent);
			GradientModulesSettingsContent.HSplitTop(LineSize, &Button, &GradientModulesSettingsContent);
			if(GradientModulesSettingsInteractive)
			{
				Ui()->DoScrollbarOption(&g_Config.m_AmfGradientStrength, &g_Config.m_AmfGradientStrength, &Button, TCLocalize("Gradient Strength", "AMF Client"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "");
			}
			else
			{
				CUIRect SliderLabel, Slider;
				Button.VSplitMid(&SliderLabel, &Slider, std::min(10.0f, Button.w * 0.05f));
				char aStrength[128];
				str_format(aStrength, sizeof(aStrength), "%s: %d", TCLocalize("Gradient Strength", "AMF Client"), g_Config.m_AmfGradientStrength);
				Ui()->DoLabel(&SliderLabel, aStrength, SliderLabel.h * CUi::ms_FontmodHeight * 0.8f, TEXTALIGN_ML);
				CUIRect Rail;
				Slider.HMargin(5.0f, &Rail);
				Rail.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f * GradientModulesSettingsProgress), IGraphics::CORNER_ALL, Rail.h / 2.0f);
				CUIRect Handle;
				Rail.VSplitLeft(std::clamp(33.0f, Rail.h, Rail.w / 3.0f), &Handle, nullptr);
				Handle.x += (Rail.w - Handle.w) * std::clamp(g_Config.m_AmfGradientStrength / 100.0f, 0.0f, 1.0f);
				Handle.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f * GradientModulesSettingsProgress), IGraphics::CORNER_ALL, Rail.h / 2.0f);
			}

			// Keep preview-only state at the bottom so it is visually separate from
			// the persistent gradient settings above.
			GradientModulesSettingsContent.HSplitTop(MarginExtraSmall, nullptr, &GradientModulesSettingsContent);
			CUIRect GradientPreviewDivider;
			GradientModulesSettingsContent.HSplitTop(GradientNicknamePreviewDividerHeight, &GradientPreviewDivider, &GradientModulesSettingsContent);
			GradientPreviewDivider.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.16f * GradientModulesSettingsProgress), IGraphics::CORNER_NONE, 0.0f);
			GradientModulesSettingsContent.HSplitTop(MarginExtraSmall, nullptr, &GradientModulesSettingsContent);
			GradientModulesSettingsContent.HSplitTop(LineSize, &Label, &GradientModulesSettingsContent);
			Ui()->DoLabel(&Label, TCLocalize("Preview", "AMF Client"), 12.0f, TEXTALIGN_ML);
			GradientModulesSettingsContent.HSplitTop(MarginExtraSmall, nullptr, &GradientModulesSettingsContent);
			CUIRect GradientPreview;
			GradientModulesSettingsContent.HSplitTop(GradientNicknamePreviewHeight, &GradientPreview, &GradientModulesSettingsContent);
			GradientPreview.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.12f * GradientModulesSettingsProgress), IGraphics::CORNER_ALL, 4.0f);
			CUIRect GradientPreviewTees, GradientPreviewTeamRow;
			GradientPreview.HSplitTop(GradientNicknamePreviewTeesHeight, &GradientPreviewTees, &GradientPreviewTeamRow);
			GradientPreviewTeamRow.HSplitTop(MarginExtraSmall, nullptr, &GradientPreviewTeamRow);
			CUIRect GradientPreviewTeamSelectorRow, GradientPreviewRandomButtonsRow;
			GradientPreviewTeamRow.HSplitTop(LineSize, &GradientPreviewTeamSelectorRow, &GradientPreviewRandomButtonsRow);
			GradientPreviewRandomButtonsRow.HSplitTop(MarginExtraSmall, nullptr, &GradientPreviewRandomButtonsRow);
			CUIRect GradientPreviewRandomButtons, GradientPreviewBottomPadding;
			GradientPreviewRandomButtonsRow.HSplitTop(LineSize, &GradientPreviewRandomButtons, &GradientPreviewBottomPadding);
			CUIRect GradientOtherPreview, GradientSelfPreview;
			GradientPreviewTees.VSplitMid(&GradientOtherPreview, &GradientSelfPreview, MarginSmall);

			auto RenderGradientPreviewTee = [&](const CUIRect &PreviewRect, const CSkin *pSkin, const char *pLabel, const AmfGradient::CNicknameGradientSource *pSource) {
				CUIRect PreviewContent = PreviewRect;
				CUIRect TeeRect, PreviewLabel, PreviewName;
				PreviewContent.HSplitTop(GradientNicknamePreviewTeeHeight, &TeeRect, &PreviewContent);
				PreviewContent.HSplitTop(GradientNicknamePreviewLabelHeight, &PreviewLabel, &PreviewName);
				Ui()->DoLabel(&PreviewLabel, TCLocalize(pLabel, "AMF Client"), GradientNicknamePreviewLabelHeight, TEXTALIGN_MC);

				CTeeRenderInfo TeeInfo;
				TeeInfo.Apply(pSkin);
				TeeInfo.m_CustomColoredSkin = false;
				TeeInfo.m_ColorBody = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
				TeeInfo.m_ColorFeet = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
				TeeInfo.m_Size = GradientNicknamePreviewTeeHeight;
				vec2 TeeOffset;
				CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &TeeInfo, TeeOffset);
				RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), TeeRect.Center() + TeeOffset);

				if(pSource == nullptr)
				{
					Ui()->DoLabel(&PreviewName, "AMF Tee", 12.0f, TEXTALIGN_MC);
					return;
				}

				CTextCursor PreviewCursor;
				PreviewCursor.SetPosition(vec2(PreviewName.x + (PreviewName.w - TextRender()->TextWidth(12.0f, "AMF Tee")) * 0.5f, PreviewName.y + (PreviewName.h - 12.0f) * 0.5f));
				PreviewCursor.m_FontSize = 12.0f;
				AmfGradient::AppendNicknameColorSplits(PreviewCursor, "AMF Tee", *pSource, TextRender()->GetTextColor());
				TextRender()->TextEx(&PreviewCursor, "AMF Tee");
			};

			const CSkin *pGradientOtherSkin = GameClient()->m_Skins.Find(s_GradientNicknameOtherPreview.m_aSkinName);
			AmfGradient::CNicknameGradientSource OtherPreviewSource;
			OtherPreviewSource.m_SkinBody = pGradientOtherSkin->m_OriginalBodyColor;
			OtherPreviewSource.m_SkinFeet = pGradientOtherSkin->m_OriginalFeetColor;
			OtherPreviewSource.m_TeamOrWarColor = s_GradientNicknameOtherPreview.m_TeamOrWarColor;
			OtherPreviewSource.m_HasSkinColors = true;
			OtherPreviewSource.m_HasTeamOrWarColor = s_GradientNicknameOtherPreview.m_HasTeamOrWarColor;
			OtherPreviewSource.m_IsLocalPlayer = false;
			RenderGradientPreviewTee(GradientOtherPreview, pGradientOtherSkin, "Other Players", &OtherPreviewSource);

			const CSkin *pGradientSelfSkin = GameClient()->m_Skins.Find(s_GradientNicknameSelfPreview.m_aSkinName);
			AmfGradient::CNicknameGradientSource SelfPreviewSource = OtherPreviewSource;
			SelfPreviewSource.m_SkinBody = pGradientSelfSkin->m_OriginalBodyColor;
			SelfPreviewSource.m_SkinFeet = pGradientSelfSkin->m_OriginalFeetColor;
			SelfPreviewSource.m_IsLocalPlayer = true;
			RenderGradientPreviewTee(GradientSelfPreview, pGradientSelfSkin, "You", &SelfPreviewSource);

			// The selector belongs to the other-player preview only. Keep all split
			// results separate so both arrows retain equal, non-overlapping hitboxes.
			const float GradientPreviewTeamCenterX = (GradientOtherPreview.Center().x + GradientSelfPreview.Center().x) * 0.5f;
			CUIRect GradientPreviewTeamSelector = GradientPreviewTeamSelectorRow;
			GradientPreviewTeamSelector.w = (GradientOtherPreview.w + GradientSelfPreview.w + MarginSmall) * 0.5f;
			GradientPreviewTeamSelector.x = GradientPreviewTeamCenterX - GradientPreviewTeamSelector.w * 0.5f;
			CUIRect GradientPreviewPreviousTeamButton, GradientPreviewTeamValue, GradientPreviewNextTeamButton, GradientPreviewTeamRemainder;
			GradientPreviewTeamSelector.VSplitLeft(LineSize, &GradientPreviewPreviousTeamButton, &GradientPreviewTeamRemainder);
			GradientPreviewTeamRemainder.VSplitRight(LineSize, &GradientPreviewTeamValue, &GradientPreviewNextTeamButton);
			char aPreviewTeam[64];
			str_format(aPreviewTeam, sizeof(aPreviewTeam), "%s: %d", TCLocalize("Team", "AMF Client"), s_GradientNicknameOtherPreview.m_TeamId);
			GradientPreviewTeamValue.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f * GradientModulesSettingsProgress), IGraphics::CORNER_NONE, 0.0f);
			Ui()->DoLabel(&GradientPreviewTeamValue, aPreviewTeam, 12.0f, TEXTALIGN_MC);
			static CButtonContainer s_GradientPreviewPreviousTeamButton;
			static CButtonContainer s_GradientPreviewNextTeamButton;
			if(GradientModulesSettingsInteractive)
			{
				if(Ui()->DoButton_FontIcon(&s_GradientPreviewPreviousTeamButton, FontIcon::CHEVRON_LEFT, 0, &GradientPreviewPreviousTeamButton, BUTTONFLAG_LEFT, IGraphics::CORNER_L))
					SetGradientPreviewTeam(s_GradientNicknameOtherPreview, s_GradientNicknameOtherPreview.m_TeamId == GRADIENT_PREVIEW_FIRST_TEAM ? GRADIENT_PREVIEW_LAST_TEAM : s_GradientNicknameOtherPreview.m_TeamId - 1, GameClient());
				if(Ui()->DoButton_FontIcon(&s_GradientPreviewNextTeamButton, FontIcon::CHEVRON_RIGHT, 0, &GradientPreviewNextTeamButton, BUTTONFLAG_LEFT, IGraphics::CORNER_R))
					SetGradientPreviewTeam(s_GradientNicknameOtherPreview, s_GradientNicknameOtherPreview.m_TeamId == GRADIENT_PREVIEW_LAST_TEAM ? GRADIENT_PREVIEW_FIRST_TEAM : s_GradientNicknameOtherPreview.m_TeamId + 1, GameClient());
			}
			else
			{
				TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
				Ui()->DoLabel(&GradientPreviewPreviousTeamButton, FontIcon::CHEVRON_LEFT, 12.0f, TEXTALIGN_MC);
				Ui()->DoLabel(&GradientPreviewNextTeamButton, FontIcon::CHEVRON_RIGHT, 12.0f, TEXTALIGN_MC);
				TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			}

			CUIRect RandomGradientTeeButton, RandomGradientTeamButton;
			GradientPreviewRandomButtons.VSplitMid(&RandomGradientTeeButton, &RandomGradientTeamButton, MarginSmall);
			RandomGradientTeeButton.VSplitRight(LineSize, nullptr, &RandomGradientTeeButton);
			RandomGradientTeamButton.VSplitLeft(LineSize, &RandomGradientTeamButton, nullptr);
			static CButtonContainer s_RandomGradientTeeButton;
			static CButtonContainer s_RandomGradientTeamButton;
			if(GradientModulesSettingsInteractive)
			{
				if(Ui()->DoButton_FontIcon(&s_RandomGradientTeeButton, FontIcon::DICE_FIVE, 0, &RandomGradientTeeButton, IGraphics::CORNER_ALL))
					RandomizeGradientPreviewTee(s_GradientNicknameOtherPreview, s_GradientNicknameSelfPreview, GameClient()->m_Skins);
				if(Ui()->DoButton_FontIcon(&s_RandomGradientTeamButton, FontIcon::ICON_USERS, 0, &RandomGradientTeamButton, IGraphics::CORNER_ALL))
					RandomizeGradientPreviewTeam(s_GradientNicknameOtherPreview, GameClient());
				GameClient()->m_Tooltips.DoToolTip(&s_RandomGradientTeeButton, &RandomGradientTeeButton, TCLocalize("Generate Random Tee", "AMF Client"));
				GameClient()->m_Tooltips.DoToolTip(&s_RandomGradientTeamButton, &RandomGradientTeamButton, TCLocalize("Generate Random Team", "AMF Client"));
			}
			else
			{
				TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
				Ui()->DoLabel(&RandomGradientTeeButton, FontIcon::DICE_FIVE, 12.0f, TEXTALIGN_MC);
				Ui()->DoLabel(&RandomGradientTeamButton, FontIcon::ICON_USERS, 12.0f, TEXTALIGN_MC);
				TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			}

			TextRender()->TextColor(OldGradientTextColor);
			TextRender()->TextOutlineColor(OldGradientOutlineColor);
			Ui()->ClipDisable();
		}
		CUIRect ScrollEnd = VisualScrollBounds;
		ScrollEnd.y = std::max(VisualLeftColumn.y, VisualRightColumn.y);
		ScrollEnd.h = 0.0f;
		m_AmfVisualScrollRegion.AddRect(ScrollEnd);
		if(m_AspectVisualScrollFocusPending)
		{
			// Use the same one-shot focus path as the successful preset-confirmation
			// flow. The request is issued after the complete content extent has been
			// registered and uses the standard keep-in-view anchor.
			m_AmfVisualScrollRegion.AddRect(AspectCardRect);
			m_AmfVisualScrollRegion.ScrollHereAnimated(CScrollRegion::SCROLLHERE_KEEP_IN_VIEW, 0.18f);
			m_AspectVisualScrollFocusPending = false;
		}
		m_AmfVisualScrollRegion.End();
		GameClient()->NotifyAmfConfigChanged();
		RenderAmfPageTransition();
		Ui()->ClipDisable();
		return;
	}

	Content.VSplitMid(&LeftView, &RightView, MarginSmall);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("AMF Client links", "AMF Client"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	static CButtonContainer s_DiscordButton, s_GithubButton, s_ConfigButton;
	CUIRect ButtonLeft, ButtonRight;
	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&ButtonLeft, &ButtonRight, MarginSmall);
	if(DoButtonLineSize_Menu(&s_DiscordButton, TCLocalize("Discord"), 0, &ButtonLeft, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://discord.gg/7zG28thvRV");
	if(DoButtonLineSize_Menu(&s_GithubButton, TCLocalize("GitHub"), 0, &ButtonRight, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://github.com/AlyaDDNet/AMF-Client/releases");
	GameClient()->m_Tooltips.DoToolTip(&s_DiscordButton, &ButtonLeft, TCLocalize("AMF Client Discord community.", "AMF Client"));
	GameClient()->m_Tooltips.DoToolTip(&s_GithubButton, &ButtonRight, TCLocalize("Official AMF Client page on GitHub.", "AMF Client"));

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	CUIRect AmfClientSettingsButton;
	LeftView.HSplitTop(LineSize * 2.0f, &AmfClientSettingsButton, &LeftView);
	if(DoButtonLineSize_Menu(&s_ConfigButton, TCLocalize("AMF Client settings", "AMF Client"), 0, &AmfClientSettingsButton, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		char aBuf[IO_MAX_PATH_LENGTH];
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::AMFCLIENT].m_aConfigPath, aBuf, sizeof(aBuf));
		Client()->ViewFile(aBuf);
	}
	GameClient()->m_Tooltips.DoToolTip(&s_ConfigButton, &AmfClientSettingsButton, TCLocalize("Opens the AMF Client configuration file.", "AMF Client"));

	const char *pProfileTitle = TCLocalize("AMF Client developer", "AMF Client");
	RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	Ui()->DoLabel(&Label, pProfileTitle, HeadlineFontSize, TEXTALIGN_ML);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);

	const float TeeSize = 57.304f;
	const float CardSize = TeeSize + MarginSmall;
	const float PanelPadding = MarginSmall;
	const float PanelHeight = AmfClientSettingsButton.y + AmfClientSettingsButton.h - RightView.y;
	const float NicknameFontSize = LineSize * 1.04275f;
	const float ProfileContentWidth = PanelPadding * 2.0f + CardSize + TextRender()->TextWidth(NicknameFontSize, "alya?") + MarginSmall + LineSize;
	const float ProfilePanelWidth = std::max(ProfileContentWidth, TextRender()->TextWidth(HeadlineFontSize, pProfileTitle));
	CUIRect ProfilePanel, PanelContent, ProfileCard, TeeRect, ProfileText;
	RightView.HSplitTop(PanelHeight, &ProfilePanel, &RightView);
	ProfilePanel.w = std::min(ProfilePanel.w, ProfilePanelWidth);
	ProfilePanel.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 5.0f);
	ProfilePanel.Margin(PanelPadding, &PanelContent);
	PanelContent.VSplitLeft(CardSize, &TeeRect, &ProfileText);
	ProfileText.x -= MarginExtraSmall + 2.5f;
	ProfileText.w += MarginExtraSmall + 2.5f;
	ProfileText.VSplitLeft(0.0f, nullptr, &ProfileText);
	ProfileCard = ProfileText;
	static CButtonContainer s_ProfileButton;
	static std::vector<float> s_vHeartStarts;
	if(Ui()->DoButtonLogic(&s_ProfileButton, 0, &TeeRect, BUTTONFLAG_LEFT))
		s_vHeartStarts.push_back(Client()->GlobalTime());

	const ColorRGBA DeveloperColor = color_cast<ColorRGBA>(ColorHSLA(180.0f / 255.0f, 141.0f / 255.0f, 0.0f).UnclampLighting(ColorHSLA::DARKEST_LGT));
	if(str_comp(GameClient()->m_Skins.Find("grizli2")->GetName(), "grizli2") == 0)
		RenderDevSkin(TeeRect.Center(), TeeSize, "grizli2", "default", true, 0, 0, 0, false, true, DeveloperColor, DeveloperColor);
	ProfileCard.VSplitLeft(TextRender()->TextWidth(NicknameFontSize, "alya?"), &Label, &Button);
	Button.VSplitLeft(MarginSmall, nullptr, &Button);
	Button.w = LineSize;
	Button.h = LineSize;
	Button.y = Label.y + (Label.h / 2.0f - Button.h / 2.0f);
	Ui()->DoLabel(&Label, "alya?", NicknameFontSize, TEXTALIGN_ML);
	static CButtonContainer s_DiscordLinkButton;
	if(Ui()->DoButton_FontIcon(&s_DiscordLinkButton, FontIcon::ARROW_UP_RIGHT_FROM_SQUARE, 0, &Button, IGraphics::CORNER_ALL))
		Client()->ViewLink("https://discord.com/channels/@me/779833135453831188");

	for(auto It = s_vHeartStarts.begin(); It != s_vHeartStarts.end();)
	{
		const float HeartTime = Client()->GlobalTime() - *It;
		if(HeartTime >= 1.0f)
		{
			It = s_vHeartStarts.erase(It);
			continue;
		}

		const float FadeIn = std::min(1.0f, HeartTime / 0.15f);
		const float FadeOut = std::min(1.0f, (1.0f - HeartTime) / 0.25f);
		const float Alpha = FadeIn * FadeOut;
		const float Size = 47.5f;
		const vec2 HeartPos = TeeRect.Center() + vec2(0.0f, -TeeSize * 0.75f - HeartTime * 18.0f);
		Graphics()->TextureSet(g_pData->m_aImages[IMAGE_AMF_HEART].m_Id);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
		IGraphics::CQuadItem HeartQuad(HeartPos.x - Size / 2.0f, HeartPos.y - Size / 2.0f, Size, Size);
		Graphics()->QuadsDrawTL(&HeartQuad, 1);
		Graphics()->QuadsEnd();
		++It;
	}

	CUIRect ClientInfoButton;
	Content.HSplitBottom(LineSize * 2.0f, nullptr, &ClientInfoButton);
	ClientInfoButton.VSplitLeft(125.0f, &ClientInfoButton, nullptr);
	static CButtonContainer s_ClientInfoButton;
	if(DoButtonLineSize_Menu(&s_ClientInfoButton, TCLocalize("Client Info", "AMF Client Info"), 0, &ClientInfoButton, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		OpenAmfClientInfoPopup();
	GameClient()->m_Tooltips.DoToolTip(&s_ClientInfoButton, &ClientInfoButton, TCLocalize("Shows the user agreement and AMF Client information.", "AMF Client"));

	GameClient()->NotifyAmfConfigChanged();
	RenderAmfPageTransition();
	Ui()->ClipDisable();
	RenderAmfClientInfoOverlay();
}

const float ColorPickerLabelSize = 13.0f;
const float ColorPickerLineSpacing = 5.0f;

static void SetFlag(int32_t &Flags, int n, bool Value)
{
	if(Value)
		Flags |= (1 << n);
	else
		Flags &= ~(1 << n);
}

static bool IsFlagSet(int32_t Flags, int n)
{
	return (Flags & (1 << n)) != 0;
}

bool CMenus::DoLine_KeyReader(CUIRect &View, CButtonContainer &ReaderButton, CButtonContainer &ClearButton, const char *pName, const char *pCommand)
{
	CBindSlot Bind(0, 0);
	for(int Mod = 0; Mod < KeyModifier::COMBINATION_COUNT; Mod++)
	{
		for(int KeyId = 0; KeyId < KEY_LAST; KeyId++)
		{
			const char *pBind = GameClient()->m_Binds.Get(KeyId, Mod);
			if(!pBind[0])
				continue;

			if(str_comp(pBind, pCommand) == 0)
			{
				Bind.m_Key = KeyId;
				Bind.m_ModifierMask = Mod;
				break;
			}
		}
	}

	CUIRect KeyButton, KeyLabel;
	View.HSplitTop(LineSize, &KeyButton, &View);
	KeyButton.VSplitMid(&KeyLabel, &KeyButton);

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "%s:", pName);
	Ui()->DoLabel(&KeyLabel, aBuf, FontSize, TEXTALIGN_ML);

	View.HSplitTop(MarginExtraSmall, nullptr, &View);

	const auto Result = GameClient()->m_KeyBinder.DoKeyReader(&ReaderButton, &ClearButton, &KeyButton, Bind, false);
	if(Result.m_Bind != Bind)
	{
		if(Bind.m_Key != KEY_UNKNOWN)
			GameClient()->m_Binds.Bind(Bind.m_Key, "", false, Bind.m_ModifierMask);
		if(Result.m_Bind.m_Key != KEY_UNKNOWN)
			GameClient()->m_Binds.Bind(Result.m_Bind.m_Key, pCommand, false, Result.m_Bind.m_ModifierMask);
		return true;
	}
	return false;
}

bool CMenus::DoSliderWithScaledValue(const void *pId, int *pOption, const CUIRect *pRect, const char *pStr, int Min, int Max, int Scale, const IScrollbarScale *pScale, unsigned Flags, const char *pSuffix)
{
	const bool NoClampValue = Flags & CUi::SCROLLBAR_OPTION_NOCLAMPVALUE;

	int Value = *pOption;
	Min /= Scale;
	Max /= Scale;
	// Allow adjustment of slider options when ctrl is pressed (to avoid scrolling, or accidentally adjusting the value)
	int Increment = std::max(1, (Max - Min) / 35);
	if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_UP) && Ui()->MouseInside(pRect))
	{
		Value += Increment;
		Value = std::clamp(Value, Min, Max);
	}
	if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN) && Ui()->MouseInside(pRect))
	{
		Value -= Increment;
		Value = std::clamp(Value, Min, Max);
	}

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "%s: %i%s", pStr, Value * Scale, pSuffix);

	if(NoClampValue)
	{
		// Clamp the value internally for the scrollbar
		Value = std::clamp(Value, Min, Max);
	}

	CUIRect Label, ScrollBar;
	pRect->VSplitMid(&Label, &ScrollBar, std::min(10.0f, pRect->w * 0.05f));

	const float LabelFontSize = Label.h * CUi::ms_FontmodHeight * 0.8f;
	Ui()->DoLabel(&Label, aBuf, LabelFontSize, TEXTALIGN_ML);

	Value = pScale->ToAbsolute(Ui()->DoScrollbarH(pId, &ScrollBar, pScale->ToRelative(Value, Min, Max)), Min, Max);
	if(NoClampValue && ((Value == Min && *pOption < Min) || (Value == Max && *pOption > Max)))
	{
		Value = *pOption;
	}

	if(*pOption != Value)
	{
		*pOption = Value;
		return true;
	}
	return false;
}

bool CMenus::DoSliderWithDividedValue(const void *pId, int *pOption, const CUIRect *pRect, const char *pStr, int Min, int Max, int Divisor, const IScrollbarScale *pScale, unsigned Flags, const char *pSuffix, bool DecimalDisplay)
{
	dbg_assert(Divisor > 0, "DoSliderWithDividedValue: Divisor must be > 0");
	dbg_assert(Max >= Min, "DoSliderWithDividedValue: Max must be >= Min");

	const bool NoClampValue = Flags & CUi::SCROLLBAR_OPTION_NOCLAMPVALUE;
	const int Step = DecimalDisplay ? 1 : Divisor;
	int Value = *pOption;

	if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_UP) && Ui()->MouseInside(pRect))
		Value = std::clamp(Value + Step, Min, Max);
	if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN) && Ui()->MouseInside(pRect))
		Value = std::clamp(Value - Step, Min, Max);

	char aBuf[256];
	if(DecimalDisplay)
	{
		const int Whole = Value / Divisor;
		const int Fraction = std::abs(Value % Divisor);
		str_format(aBuf, sizeof(aBuf), "%s: %d.%d%s", pStr, Whole, Fraction, pSuffix);
	}
	else
	{
		str_format(aBuf, sizeof(aBuf), "%s: %d%s", pStr, Value / Divisor, pSuffix);
	}

	const int PrevValue = Value;
	Value = std::clamp(Value, Min, Max);

	CUIRect Label, ScrollBar;
	pRect->VSplitMid(&Label, &ScrollBar, std::min(10.0f, pRect->w * 0.05f));

	const float LabelFontSize = Label.h * CUi::ms_FontmodHeight * 0.8f;
	Ui()->DoLabel(&Label, aBuf, LabelFontSize, TEXTALIGN_ML);

	Value = pScale->ToAbsolute(Ui()->DoScrollbarH(pId, &ScrollBar, pScale->ToRelative(Value, Min, Max)), Min, Max);
	if(!DecimalDisplay)
		Value = (Value / Divisor) * Divisor;
	if(NoClampValue && ((Value == Min && PrevValue < Min) || (Value == Max && PrevValue > Max)))
		Value = PrevValue;

	if(*pOption != Value)
	{
		*pOption = Value;
		return true;
	}
	return false;
}

bool CMenus::DoEditBoxWithLabel(CLineInput *LineInput, const CUIRect *pRect, const char *pLabel, const char *pDefault, char *pBuf, size_t BufSize)
{
	CUIRect Button, Label;
	pRect->VSplitLeft(210.0f, &Label, &Button);
	Ui()->DoLabel(&Label, pLabel, FontSize, TEXTALIGN_ML);
	LineInput->SetBuffer(pBuf, BufSize);
	LineInput->SetEmptyText(pDefault);
	return Ui()->DoEditBox(LineInput, &Button, EditBoxFontSize);
}

int CMenus::DoButtonLineSize_Menu(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, float ButtonLineSize, bool Fake, const char *pImageName, int Corners, float Rounding, float FontFactor, ColorRGBA Color)
{
	CUIRect Text = *pRect;

	if(Checked)
		Color = ColorRGBA(0.6f, 0.6f, 0.6f, 0.5f);
	Color.a *= Ui()->ButtonColorMul(pButtonContainer);

	if(Fake)
		Color.a *= 0.5f;

	pRect->Draw(Color, Corners, Rounding);

	Text.HMargin((Text.h - ButtonLineSize) / 2.0f, &Text);
	Text.HMargin(pRect->h >= 20.0f ? 2.0f : 1.0f, &Text);
	Text.HMargin((Text.h * FontFactor) / 2.0f, &Text);
	Ui()->DoLabel(&Text, pText, Text.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);

	if(Fake)
		return 0;

	return Ui()->DoButtonLogic(pButtonContainer, Checked, pRect, BUTTONFLAG_LEFT);
}

void CMenus::RenderDevSkin(vec2 RenderPos, float Size, const char *pSkinName, const char *pBackupSkin, bool CustomColors, int FeetColor, int BodyColor, int Emote, bool Rainbow, bool Cute, ColorRGBA ColorFeet, ColorRGBA ColorBody)
{
	bool WhiteFeetTemp = g_Config.m_TcWhiteFeet;
	g_Config.m_TcWhiteFeet = false;

	float DefTick = std::fmod(s_Time, 1.0f);

	CTeeRenderInfo SkinInfo;
	const CSkin *pSkin = GameClient()->m_Skins.Find(pSkinName);
	if(str_comp(pSkin->GetName(), pSkinName) != 0)
		pSkin = GameClient()->m_Skins.Find(pBackupSkin);

	SkinInfo.m_OriginalRenderSkin = pSkin->m_OriginalSkin;
	SkinInfo.m_ColorableRenderSkin = pSkin->m_ColorableSkin;
	SkinInfo.m_SkinMetrics = pSkin->m_Metrics;
	SkinInfo.m_CustomColoredSkin = CustomColors;
	if(SkinInfo.m_CustomColoredSkin)
	{
		SkinInfo.m_ColorBody = color_cast<ColorRGBA>(ColorHSLA(BodyColor).UnclampLighting(ColorHSLA::DARKEST_LGT));
		SkinInfo.m_ColorFeet = color_cast<ColorRGBA>(ColorHSLA(FeetColor).UnclampLighting(ColorHSLA::DARKEST_LGT));
		if(ColorBody.a != 0.0f)
			SkinInfo.m_ColorBody = ColorBody;
		if(ColorFeet.a != 0.0f)
			SkinInfo.m_ColorFeet = ColorFeet;
	}
	else
	{
		SkinInfo.m_ColorBody = ColorRGBA(1.0f, 1.0f, 1.0f);
		SkinInfo.m_ColorFeet = ColorRGBA(1.0f, 1.0f, 1.0f);
	}
	if(Rainbow)
	{
		ColorRGBA Col = color_cast<ColorRGBA>(ColorHSLA(DefTick, 1.0f, 0.5f));
		SkinInfo.m_ColorBody = Col;
		SkinInfo.m_ColorFeet = Col;
	}
	SkinInfo.m_Size = Size;
	const CAnimState *pIdleState = CAnimState::GetIdle();
	vec2 OffsetToMid;
	CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &SkinInfo, OffsetToMid);
	vec2 TeeRenderPos(RenderPos.x, RenderPos.y + OffsetToMid.y);
	if(Cute)
		RenderTeeCute(pIdleState, &SkinInfo, Emote, vec2(1.0f, 0.0f), TeeRenderPos, true);
	else
		RenderTools()->RenderTee(pIdleState, &SkinInfo, Emote, vec2(1.0f, 0.0f), TeeRenderPos);
	g_Config.m_TcWhiteFeet = WhiteFeetTemp;
}

void CMenus::RenderTeeCute(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, int Emote, vec2 Dir, vec2 Pos, bool CuteEyes, float Alpha)
{
	Dir = Ui()->MousePos() - Pos;
	if(pInfo->m_Size > 0.0f)
		Dir /= pInfo->m_Size;
	const float Length = length(Dir);
	if(Length > 1.0f)
		Dir /= Length;
	if(CuteEyes && Length < 0.4f)
		Emote = 2;
	RenderTools()->RenderTee(pAnim, pInfo, Emote, Dir, Pos, Alpha);
}

void CMenus::RenderFontIcon(const CUIRect Rect, const char *pText, float Size, int Align)
{
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
	Ui()->DoLabel(&Rect, pText, Size, Align);
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
}

int CMenus::DoButtonNoRect_FontIcon(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, int Corners)
{
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
	TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
	TextRender()->TextColor(TextRender()->DefaultTextSelectionColor());
	if(Ui()->HotItem() == pButtonContainer)
	{
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
	CUIRect Temp;
	pRect->HMargin(0.0f, &Temp);
	Ui()->DoLabel(&Temp, pText, Temp.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

	return Ui()->DoButtonLogic(pButtonContainer, Checked, pRect, BUTTONFLAG_LEFT);
}

void CMenus::PopupConfirmRemoveWarType()
{
	GameClient()->m_WarList.RemoveWarType(m_pRemoveWarType->m_aWarName);
	m_pRemoveWarType = nullptr;
}

void CMenus::RenderSettingsTClient(CUIRect MainView)
{
	s_Time += Client()->RenderFrameTime() * (1.0f / 100.0f);
	if(!s_StartedTime)
	{
		s_StartedTime = true;
		s_Time = (float)rand() / (float)RAND_MAX;
	}

	static int s_CurCustomTab = 0;

	CUIRect TabBar, Button;
	int TabCount = NUMBER_OF_TCLIENT_TABS;
	for(int Tab = 0; Tab < NUMBER_OF_TCLIENT_TABS; ++Tab)
	{
		if(IsFlagSet(g_Config.m_TcTClientSettingsTabs, Tab))
		{
			TabCount--;
			if(s_CurCustomTab == Tab)
				s_CurCustomTab++;
		}
	}

	MainView.HSplitTop(LineSize, &TabBar, &MainView);
	const float TabWidth = TabBar.w / TabCount;
	static CButtonContainer s_aPageTabs[NUMBER_OF_TCLIENT_TABS] = {};
	const char *apTabNames[] = {
		TCLocalize("Settings"),
		TCLocalize("Bind Wheel"),
		TCLocalize("War List"),
		TCLocalize("Chat Binds"),
		TCLocalize("Status Bar"),
		TCLocalize("Info")};

	for(int Tab = 0; Tab < NUMBER_OF_TCLIENT_TABS; ++Tab)
	{
		if(IsFlagSet(g_Config.m_TcTClientSettingsTabs, Tab))
			continue;

		TabBar.VSplitLeft(TabWidth, &Button, &TabBar);
		const int Corners = Tab == 0 ? IGraphics::CORNER_L : Tab == NUMBER_OF_TCLIENT_TABS - 1 ? IGraphics::CORNER_R :
													 IGraphics::CORNER_NONE;
		if(DoButton_MenuTab(&s_aPageTabs[Tab], apTabNames[Tab], s_CurCustomTab == Tab, &Button, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
			s_CurCustomTab = Tab;
	}

	MainView.HSplitTop(Margin, nullptr, &MainView);

	if(s_CurCustomTab == TCLIENT_TAB_SETTINGS)
		RenderSettingsTClientSettings(MainView);
	if(s_CurCustomTab == TCLIENT_TAB_BINDCHAT)
		RenderSettingsTClientChatBinds(MainView);
	if(s_CurCustomTab == TCLIENT_TAB_BINDWHEEL)
		RenderSettingsTClientBindWheel(MainView);
	if(s_CurCustomTab == TCLIENT_TAB_WARLIST)
		RenderSettingsTClientWarList(MainView);
	if(s_CurCustomTab == TCLIENT_TAB_STATUSBAR)
		RenderSettingsTClientStatusBar(MainView);
	if(s_CurCustomTab == TCLIENT_TAB_INFO)
		RenderSettingsTClientInfo(MainView);
}

void CMenus::RenderSettingsTClientSettings(CUIRect MainView)
{
	CUIRect Column, LeftView, RightView, Button, Label;

	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	ScrollParams.m_ForceShowScrollbar = true;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	const CUIRect UnscrolledMainView = MainView;
	s_ScrollRegion.Begin(&MainView, &ScrollParams);
	const vec2 ScrollOffset(MainView.x - UnscrolledMainView.x, MainView.y - UnscrolledMainView.y);

	static std::vector<CUIRect> s_SectionBoxes;
	static vec2 s_PrevScrollOffset(0.0f, 0.0f);

	MainView.VSplitRight(5.0f, &MainView, nullptr); // Padding for scrollbar
	MainView.VSplitLeft(5.0f, nullptr, &MainView); // Padding for scrollbar

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	for(CUIRect &Section : s_SectionBoxes)
	{
		float Padding = MarginBetweenViews * 0.6666f;
		Section.w += Padding;
		Section.h += Padding;
		Section.x -= Padding * 0.5f;
		Section.y -= Padding * 0.5f;
		Section.y -= s_PrevScrollOffset.y - ScrollOffset.y;
		Section.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 10.0f);
	}
	s_PrevScrollOffset = ScrollOffset;
	s_SectionBoxes.clear();

	// ***** LeftView ***** //
	Column = LeftView;

	// ***** Visual Miscellaneous ***** //
	Column.HSplitTop(Margin, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Visual"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	static std::vector<const char *> s_FontDropDownNames = {};
	static CUi::SDropDownState s_FontDropDownState;
	static CScrollRegion s_FontDropDownScrollRegion;
	s_FontDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_FontDropDownScrollRegion;
	s_FontDropDownState.m_SelectionPopupContext.m_SpecialFontRenderMode = true;
	int FontSelectedOld = -1;
	for(size_t i = 0; i < TextRender()->GetCustomFaces()->size(); ++i)
	{
		if(s_FontDropDownNames.size() != TextRender()->GetCustomFaces()->size())
			s_FontDropDownNames.push_back(TextRender()->GetCustomFaces()->at(i).c_str());

		if(str_find_nocase(g_Config.m_TcCustomFont, TextRender()->GetCustomFaces()->at(i).c_str()))
			FontSelectedOld = i;
	}
	CUIRect FontDropDownRect, FontDirectory;
	Column.HSplitTop(LineSize, &FontDropDownRect, &Column);
	FontDropDownRect.VSplitLeft(100.0f, &Label, &FontDropDownRect);
	FontDropDownRect.VSplitRight(20.0f, &FontDropDownRect, &FontDirectory);
	FontDropDownRect.VSplitRight(MarginSmall, &FontDropDownRect, nullptr);

	Ui()->DoLabel(&Label, TCLocalize("Custom Font: "), FontSize, TEXTALIGN_ML);
	const int FontSelectedNew = Ui()->DoDropDown(&FontDropDownRect, FontSelectedOld, s_FontDropDownNames.data(), s_FontDropDownNames.size(), s_FontDropDownState);
	if(FontSelectedOld != FontSelectedNew)
	{
		str_copy(g_Config.m_TcCustomFont, s_FontDropDownNames[FontSelectedNew]);
		TextRender()->SetCustomFace(g_Config.m_TcCustomFont);

		// Attempt to reset all the containers
		TextRender()->OnPreWindowResize();
		GameClient()->OnWindowResize();
		GameClient()->Editor()->OnWindowResize();
		TextRender()->OnWindowResize();
		GameClient()->m_MapImages.SetTextureScale(101);
		GameClient()->m_MapImages.SetTextureScale(g_Config.m_ClTextEntitiesSize);
	}

	static CButtonContainer s_FontDirectoryId;
	if(Ui()->DoButton_FontIcon(&s_FontDirectoryId, FontIcon::FOLDER, 0, &FontDirectory, IGraphics::CORNER_ALL))
	{
		Storage()->CreateFolder("tclient", IStorage::TYPE_SAVE);
		Storage()->CreateFolder("tclient/fonts", IStorage::TYPE_SAVE);
		char aBuf[IO_MAX_PATH_LENGTH];
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, "tclient/fonts", aBuf, sizeof(aBuf));
		Client()->ViewFile(aBuf);
	}

	CUIRect TinyTeeConfig;
	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);

	{
		Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
		static std::vector<const char *> s_DropDownNames;
		s_DropDownNames = {TCLocalize("Normal", "Hammer Mode"), TCLocalize("Rotate with cursor", "Hammer Mode"), TCLocalize("Rotate with cursor like gun", "Hammer Mode")};
		static CUi::SDropDownState s_DropDownState;
		static CScrollRegion s_DropDownScrollRegion;
		s_DropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_DropDownScrollRegion;
		CUIRect DropDownRect;
		Column.HSplitTop(LineSize, &DropDownRect, &Column);
		DropDownRect.VSplitLeft(120.0f, &Label, &DropDownRect);
		Ui()->DoLabel(&Label, TCLocalize("Hammer Mode: "), FontSize, TEXTALIGN_ML);
		g_Config.m_TcHammerRotatesWithCursor = Ui()->DoDropDown(&DropDownRect, g_Config.m_TcHammerRotatesWithCursor, s_DropDownNames.data(), s_DropDownNames.size(), s_DropDownState);
		Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	}

	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcCursorScale, &g_Config.m_TcCursorScale, &Button, TCLocalize("Ingame cursor scale"), 0, 500, &CUi::ms_LinearScrollbarScale, 0, "%");

	Column.HSplitTop(LineSize, &Button, &Column);
	if(g_Config.m_TcAnimateWheelTime > 0)
		Ui()->DoScrollbarOption(&g_Config.m_TcAnimateWheelTime, &g_Config.m_TcAnimateWheelTime, &Button, TCLocalize("Wheel animate"), 0, 1000, &CUi::ms_LinearScrollbarScale, 0, "ms");
	else
		Ui()->DoScrollbarOption(&g_Config.m_TcAnimateWheelTime, &g_Config.m_TcAnimateWheelTime, &Button, TCLocalize("Wheel animate"), 0, 1000, &CUi::ms_LinearScrollbarScale, 0, "ms (off)");

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcNameplatePingCircle, TCLocalize("Show ping colored circle in nameplates"), &g_Config.m_TcNameplatePingCircle, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcNameplateCountry, TCLocalize("Show country flags in nameplates"), &g_Config.m_TcNameplateCountry, &Column, LineSize);
	// DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRenderNameplateSpec, TCLocalize("Hide nameplates in spec"), &g_Config.m_TcRenderNameplateSpec, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcNameplateSkins, TCLocalize("Show skin names in nameplate"), &g_Config.m_TcNameplateSkins, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFreezeStars, TCLocalize("Freeze stars"), &g_Config.m_ClFreezeStars, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcColorFreeze, TCLocalize("Colored frozen tee skins"), &g_Config.m_TcColorFreeze, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFrozenKatana, TCLocalize("Show katan on frozen players"), &g_Config.m_TcFrozenKatana, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRenderWeaponsAsGun, TCLocalize("Render weapons as the gun sprite"), &g_Config.m_TcRenderWeaponsAsGun, &Column, LineSize);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWhiteFeet, TCLocalize("Render all custom colored feet as white feet skin"), &g_Config.m_TcWhiteFeet, &Column, LineSize);
	CUIRect FeetBox;
	Column.HSplitTop(LineSize + MarginExtraSmall, &FeetBox, &Column);
	if(g_Config.m_TcWhiteFeet)
	{
		FeetBox.HSplitTop(MarginExtraSmall, nullptr, &FeetBox);
		FeetBox.VSplitMid(&FeetBox, nullptr);
		static CLineInput s_WhiteFeet(g_Config.m_TcWhiteFeetSkin, sizeof(g_Config.m_TcWhiteFeetSkin));
		s_WhiteFeet.SetEmptyText("x_ninja");
		Ui()->DoEditBox(&s_WhiteFeet, &FeetBox, EditBoxFontSize);
	}

	{
		static std::vector<CButtonContainer> s_vButtonContainers = {{}, {}, {}};
		int Value = g_Config.m_TcTinyTees ? (g_Config.m_TcTinyTeesOthers ? 2 : 1) : 0;
		if(DoLine_RadioMenu(Column, TCLocalize("Tiny Tees"),
			   s_vButtonContainers,
			   {Localize("None"), Localize("Own"), Localize("All")},
			   {0, 1, 2},
			   Value))
		{
			g_Config.m_TcTinyTees = Value > 0 ? 1 : 0;
			g_Config.m_TcTinyTeesOthers = Value > 1 ? 1 : 0;
		}
		Column.HSplitTop(LineSize, &TinyTeeConfig, &Column);
		if(g_Config.m_TcTinyTees > 0)
			Ui()->DoScrollbarOption(&g_Config.m_TcTinyTeeSize, &g_Config.m_TcTinyTeeSize, &TinyTeeConfig, TCLocalize("Tiny Tee Size"), 85, 115);
	}

	{
		static std::vector<CButtonContainer> s_vButtonContainers = {{}, {}, {}};
		int Value = g_Config.m_TcFakeCtfFlags;
		if(DoLine_RadioMenu(Column, TCLocalize("Fake CTF flags"),
			   s_vButtonContainers,
			   {Localize("None"), Localize("Red"), Localize("Blue")},
			   {0, 1, 2},
			   Value))
		{
			g_Config.m_TcFakeCtfFlags = Value;
		}
	}
	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Anti Latency Tools ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Anti Latency Tools"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	Column.HSplitTop(LineSize, &Button, &Column);
	DoSliderWithDividedValue(&g_Config.m_ClPredictionMargin, &g_Config.m_ClPredictionMargin, &Button, "Prediction margin", 10, 750, 10, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms", true);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRemoveAnti, TCLocalize("Remove prediction & antiping in freeze"), &g_Config.m_TcRemoveAnti, &Column, LineSize);
	if(g_Config.m_TcRemoveAnti)
	{
		if(g_Config.m_TcUnfreezeLagDelayTicks < g_Config.m_TcUnfreezeLagTicks)
			g_Config.m_TcUnfreezeLagDelayTicks = g_Config.m_TcUnfreezeLagTicks;
		Column.HSplitTop(LineSize, &Button, &Column);
		DoSliderWithScaledValue(&g_Config.m_TcUnfreezeLagTicks, &g_Config.m_TcUnfreezeLagTicks, &Button, TCLocalize("Amount"), 100, 300, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
		Column.HSplitTop(LineSize, &Button, &Column);
		DoSliderWithScaledValue(&g_Config.m_TcUnfreezeLagDelayTicks, &g_Config.m_TcUnfreezeLagDelayTicks, &Button, TCLocalize("Delay"), 100, 3000, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
	}
	else
		Column.HSplitTop(LineSize * 2, nullptr, &Column);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcUnpredOthersInFreeze, TCLocalize("Dont predict other players if you are frozen"), &g_Config.m_TcUnpredOthersInFreeze, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcPredMarginInFreeze, TCLocalize("Adjust your prediction margin while frozen"), &g_Config.m_TcPredMarginInFreeze, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	if(g_Config.m_TcPredMarginInFreeze)
		Ui()->DoScrollbarOption(&g_Config.m_TcPredMarginInFreezeAmount, &g_Config.m_TcPredMarginInFreezeAmount, &Button, TCLocalize("Frozen Margin"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "ms");
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Improved Anti Ping ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Anti Ping Smoothing"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAntiPingImproved, TCLocalize("Use new smoothing algorithm"), &g_Config.m_TcAntiPingImproved, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAntiPingStableDirection, TCLocalize("Optimistic prediction in stable direction"), &g_Config.m_TcAntiPingStableDirection, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAntiPingNegativeBuffer, TCLocalize("Remember instability for longer"), &g_Config.m_TcAntiPingNegativeBuffer, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcAntiPingUncertaintyScale, &g_Config.m_TcAntiPingUncertaintyScale, &Button, TCLocalize("Uncertainty duration"), 50, 400, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "%");
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Execute on join ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);

	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Auto execute"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	{
		CUIRect Box;
		Column.HSplitTop(LineSize + MarginExtraSmall, &Box, &Column);
		Box.VSplitMid(&Label, &Button);
		Ui()->DoLabel(&Label, Localize("Execute before connect"), FontSize, TEXTALIGN_ML);
		static CLineInput s_LineInput(g_Config.m_TcExecuteOnConnect, sizeof(g_Config.m_TcExecuteOnConnect));
		s_LineInput.SetEmptyText(TCLocalize("Run a console command before connect"));

		Ui()->DoEditBox(&s_LineInput, &Button, EditBoxFontSize);
	}
	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	{
		CUIRect Box;
		Column.HSplitTop(LineSize + MarginExtraSmall, &Box, &Column);
		Box.VSplitMid(&Label, &Button);
		Ui()->DoLabel(&Label, Localize("Execute on join"), FontSize, TEXTALIGN_ML);
		static CLineInput s_LineInput(g_Config.m_TcExecuteOnJoin, sizeof(g_Config.m_TcExecuteOnJoin));
		s_LineInput.SetEmptyText(TCLocalize("Run a console command on join"));

		Ui()->DoEditBox(&s_LineInput, &Button, EditBoxFontSize);
	}

	Column.HSplitTop(LineSize, &Button, &Column);
	DoSliderWithScaledValue(&g_Config.m_TcExecuteOnJoinDelay, &g_Config.m_TcExecuteOnJoinDelay, &Button, TCLocalize("Delay"), 140, 2000, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Voting ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Voting"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAutoVoteWhenFar, TCLocalize("Auto vote no to map changes when far"), &g_Config.m_TcAutoVoteWhenFar, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcAutoVoteWhenFarTime, &g_Config.m_TcAutoVoteWhenFarTime, &Button, TCLocalize("Minimum Time"), 1, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, " minutes");

	CUIRect VoteMessage;
	Column.HSplitTop(LineSize + MarginExtraSmall, &VoteMessage, &Column);
	VoteMessage.HSplitTop(MarginExtraSmall, nullptr, &VoteMessage);
	VoteMessage.VSplitMid(&Label, &VoteMessage);
	Ui()->DoLabel(&Label, TCLocalize("Message to send in chat:"), FontSize, TEXTALIGN_ML);
	static CLineInput s_VoteMessage(g_Config.m_TcAutoVoteWhenFarMessage, sizeof(g_Config.m_TcAutoVoteWhenFarMessage));
	s_VoteMessage.SetEmptyText(TCLocalize("Leave empty to disable"));
	Ui()->DoEditBox(&s_VoteMessage, &VoteMessage, EditBoxFontSize);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);

	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Auto Reply ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Auto Reply"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAutoReplyMuted, TCLocalize("Auto reply to muted players"), &g_Config.m_TcAutoReplyMuted, &Column, LineSize);
	CUIRect MutedReply;
	Column.HSplitTop(LineSize + MarginExtraSmall, &MutedReply, &Column);
	if(g_Config.m_TcAutoReplyMuted)
	{
		MutedReply.HSplitTop(MarginExtraSmall, nullptr, &MutedReply);
		static CLineInput s_MutedReply(g_Config.m_TcAutoReplyMutedMessage, sizeof(g_Config.m_TcAutoReplyMutedMessage));
		s_MutedReply.SetEmptyText("I have muted you");
		Ui()->DoEditBox(&s_MutedReply, &MutedReply, EditBoxFontSize);
	}
	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAutoReplyMinimized, TCLocalize("Auto reply when tabbed out"), &g_Config.m_TcAutoReplyMinimized, &Column, LineSize);
	CUIRect MinimizedReply;
	Column.HSplitTop(LineSize + MarginExtraSmall, &MinimizedReply, &Column);
	if(g_Config.m_TcAutoReplyMinimized)
	{
		MinimizedReply.HSplitTop(MarginExtraSmall, nullptr, &MinimizedReply);
		static CLineInput s_MinimizedReply(g_Config.m_TcAutoReplyMinimizedMessage, sizeof(g_Config.m_TcAutoReplyMinimizedMessage));
		s_MinimizedReply.SetEmptyText("I am not tabbed in");
		Ui()->DoEditBox(&s_MinimizedReply, &MinimizedReply, EditBoxFontSize);
	}
	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Player Indicator ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Player Indicator"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcPlayerIndicator, TCLocalize("Show any enabled Indicators"), &g_Config.m_TcPlayerIndicator, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcIndicatorHideVisible, TCLocalize("Hide indicator for tees on your screen"), &g_Config.m_TcIndicatorHideVisible, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcPlayerIndicatorFreeze, TCLocalize("Show only freeze Players"), &g_Config.m_TcPlayerIndicatorFreeze, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcIndicatorTeamOnly, TCLocalize("Only show after joining a team"), &g_Config.m_TcIndicatorTeamOnly, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcIndicatorTees, TCLocalize("Render tiny tees instead of circles"), &g_Config.m_TcIndicatorTees, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListIndicator, TCLocalize("Use warlist groups for indicator"), &g_Config.m_TcWarListIndicator, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcIndicatorRadius, &g_Config.m_TcIndicatorRadius, &Button, TCLocalize("Indicator size"), 1, 16);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcIndicatorOpacity, &g_Config.m_TcIndicatorOpacity, &Button, TCLocalize("Indicator opacity"), 0, 100);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcIndicatorVariableDistance, TCLocalize("Change indicator offset based on distance to other tees"), &g_Config.m_TcIndicatorVariableDistance, &Column, LineSize);
	if(g_Config.m_TcIndicatorVariableDistance)
	{
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_TcIndicatorOffset, &g_Config.m_TcIndicatorOffset, &Button, TCLocalize("Indicator min offset"), 16, 200);
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_TcIndicatorOffsetMax, &g_Config.m_TcIndicatorOffsetMax, &Button, TCLocalize("Indicator max offset"), 16, 200);
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_TcIndicatorMaxDistance, &g_Config.m_TcIndicatorMaxDistance, &Button, TCLocalize("Indicator max distance"), 500, 7000);
	}
	else
	{
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_TcIndicatorOffset, &g_Config.m_TcIndicatorOffset, &Button, TCLocalize("Indicator offset"), 16, 200);
		Column.HSplitTop(LineSize * 2, nullptr, &Column);
	}
	if(g_Config.m_TcWarListIndicator)
	{
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListIndicatorColors, TCLocalize("Use warlist colors instead of regular colors"), &g_Config.m_TcWarListIndicatorColors, &Column, LineSize);
		char aBuf[128];
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListIndicatorAll, TCLocalize("Show all warlist groups"), &g_Config.m_TcWarListIndicatorAll, &Column, LineSize);
		str_format(aBuf, sizeof(aBuf), "Show %s group", GameClient()->m_WarList.m_WarTypes.at(1)->m_aWarName);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListIndicatorEnemy, aBuf, &g_Config.m_TcWarListIndicatorEnemy, &Column, LineSize);
		str_format(aBuf, sizeof(aBuf), "Show %s group", GameClient()->m_WarList.m_WarTypes.at(2)->m_aWarName);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListIndicatorTeam, aBuf, &g_Config.m_TcWarListIndicatorTeam, &Column, LineSize);
	}
	if(!g_Config.m_TcWarListIndicatorColors || !g_Config.m_TcWarListIndicator)
	{
		static CButtonContainer s_IndicatorAliveColorId, s_IndicatorDeadColorId, s_IndicatorSavedColorId;
		DoLine_ColorPicker(&s_IndicatorAliveColorId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column, TCLocalize("Indicator alive color"), &g_Config.m_TcIndicatorAlive, ColorRGBA(0.0f, 0.0f, 0.0f), false);
		DoLine_ColorPicker(&s_IndicatorDeadColorId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column, TCLocalize("Indicator in freeze color"), &g_Config.m_TcIndicatorFreeze, ColorRGBA(0.0f, 0.0f, 0.0f), false);
		DoLine_ColorPicker(&s_IndicatorSavedColorId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column, TCLocalize("Indicator safe color"), &g_Config.m_TcIndicatorSaved, ColorRGBA(0.0f, 0.0f, 0.0f), false);
	}
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Pet ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Pet"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcPetShow, TCLocalize("Show the pet"), &g_Config.m_TcPetShow, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcPetSize, &g_Config.m_TcPetSize, &Button, TCLocalize("Pet size"), 10, 500, &CUi::ms_LinearScrollbarScale, 0, "%");
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcPetAlpha, &g_Config.m_TcPetAlpha, &Button, TCLocalize("Pet alpha"), 10, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	Column.HSplitTop(LineSize + MarginExtraSmall, &Button, &Column);
	Button.VSplitMid(&Label, &Button);
	Ui()->DoLabel(&Label, TCLocalize("Pet Skin:"), FontSize, TEXTALIGN_ML);
	static CLineInput s_PetSkin(g_Config.m_TcPetSkin, sizeof(g_Config.m_TcPetSkin));
	Ui()->DoEditBox(&s_PetSkin, &Button, EditBoxFontSize);

	// Pet Preview
	Column.HSplitTop(MarginSmall, nullptr, &Column);
	CUIRect Preview;
	Column.HSplitTop(64.0f, &Preview, &Column);

	CTeeRenderInfo TeeInfo;
	const CSkin *pSkin = GameClient()->m_Skins.Find(g_Config.m_TcPetSkin);
	if(!pSkin || str_comp(pSkin->GetName(), g_Config.m_TcPetSkin) != 0)
		pSkin = GameClient()->m_Skins.Find("default");

	TeeInfo.m_OriginalRenderSkin = pSkin->m_OriginalSkin;
	TeeInfo.m_ColorableRenderSkin = pSkin->m_ColorableSkin;
	TeeInfo.m_SkinMetrics = pSkin->m_Metrics;
	TeeInfo.m_CustomColoredSkin = false;
	TeeInfo.m_ColorBody = ColorRGBA(1.0f, 1.0f, 1.0f);
	TeeInfo.m_ColorFeet = ColorRGBA(1.0f, 1.0f, 1.0f);
	TeeInfo.m_Size = 64.0f;

	const CAnimState *pIdleState = CAnimState::GetIdle();
	vec2 OffsetToMid;
	CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeInfo, OffsetToMid);
	vec2 TeeRenderPos = Preview.Center();
	TeeRenderPos.y += OffsetToMid.y;

	vec2 Dir = Ui()->MousePos() - TeeRenderPos;
	const float Length = length(Dir);
	if(Length > 0.0f)
		Dir /= Length;
	if(Length < 0.4f * 64.0f)
	{
		Dir = vec2(1.0f, 0.0f);
	}

	int PetEmote = g_Config.m_ClPlayerDefaultEyes;
	RenderTools()->RenderTee(pIdleState, &TeeInfo, PetEmote, Dir, TeeRenderPos);

	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** RightView ***** //
	LeftView = Column;
	Column = RightView;

	// ***** HUD ***** //
	Column.HSplitTop(Margin, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("HUD"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcMiniVoteHud, TCLocalize("Show mini vote HUD"), &g_Config.m_TcMiniVoteHud, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcMiniDebug, TCLocalize("Show position and angle (mini debug)"), &g_Config.m_TcMiniDebug, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRenderCursorSpec, TCLocalize("Show your cursor when in free spectate"), &g_Config.m_TcRenderCursorSpec, &Column, LineSize);

	Column.HSplitTop(LineSize, &Button, &Column);
	if(g_Config.m_TcRenderCursorSpec)
	{
		Ui()->DoScrollbarOption(&g_Config.m_TcRenderCursorSpecAlpha, &g_Config.m_TcRenderCursorSpecAlpha, &Button, TCLocalize("Spectate cursor alpha"), 0, 100);
	}

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcNotifyWhenLast, TCLocalize("Show when you are the last alive"), &g_Config.m_TcNotifyWhenLast, &Column, LineSize);
	CUIRect NotificationConfig;
	Column.HSplitTop(LineSize + MarginSmall, &NotificationConfig, &Column);
	if(g_Config.m_TcNotifyWhenLast)
	{
		NotificationConfig.VSplitMid(&Button, &NotificationConfig);
		static CLineInput s_LastInput(g_Config.m_TcNotifyWhenLastText, sizeof(g_Config.m_TcNotifyWhenLastText));
		s_LastInput.SetEmptyText(TCLocalize("Last!"));
		Button.HSplitTop(MarginSmall, nullptr, &Button);
		Ui()->DoEditBox(&s_LastInput, &Button, EditBoxFontSize);
		static CButtonContainer s_ClientNotifyWhenLastColor;
		DoLine_ColorPicker(&s_ClientNotifyWhenLastColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &NotificationConfig, "", &g_Config.m_TcNotifyWhenLastColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_TcNotifyWhenLastX, &g_Config.m_TcNotifyWhenLastX, &Button, TCLocalize("Horizontal Position"), 1, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_TcNotifyWhenLastY, &g_Config.m_TcNotifyWhenLastY, &Button, TCLocalize("Vertical Position"), 1, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_TcNotifyWhenLastSize, &g_Config.m_TcNotifyWhenLastSize, &Button, TCLocalize("Font Size"), 1, 50);
	}
	else
	{
		Column.HSplitTop(LineSize * 3.0f, nullptr, &Column);
	}

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowCenter, TCLocalize("Show screen center lines"), &g_Config.m_TcShowCenter, &Column, LineSize);
	Column.HSplitTop(LineSize + MarginSmall, &Button, &Column);
	if(g_Config.m_TcShowCenter)
	{
		static CButtonContainer s_ShowCenterLineColor;
		DoLine_ColorPicker(&s_ShowCenterLineColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Button, TCLocalize("Screen center line color"), &g_Config.m_TcShowCenterColor, DefaultConfig::TcShowCenterColor, false, nullptr, true);
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_TcShowCenterWidth, &g_Config.m_TcShowCenterWidth, &Button, TCLocalize("Screen center line width"), 0, 20);
	}
	else
	{
		Column.HSplitTop(LineSize, nullptr, &Column);
	}

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Frozen Tee Display ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Frozen Tee Display"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowFrozenHud, TCLocalize("Show frozen tee display"), &g_Config.m_TcShowFrozenHud, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowFrozenHudSkins, TCLocalize("Use skins instead of ninja tees"), &g_Config.m_TcShowFrozenHudSkins, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFrozenHudTeamOnly, TCLocalize("Only show after joining a team"), &g_Config.m_TcFrozenHudTeamOnly, &Column, LineSize);

	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcFrozenMaxRows, &g_Config.m_TcFrozenMaxRows, &Button, TCLocalize("Max Rows"), 1, 6);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcFrozenHudTeeSize, &g_Config.m_TcFrozenHudTeeSize, &Button, TCLocalize("Tee Size"), 8, 27);

	{
		CUIRect CheckBoxRect, CheckBoxRect2;
		Column.HSplitTop(LineSize, &CheckBoxRect, &Column);
		Column.HSplitTop(LineSize, &CheckBoxRect2, &Column);
		if(DoButton_CheckBox(&g_Config.m_TcShowFrozenText, TCLocalize("Tees left alive text"), g_Config.m_TcShowFrozenText >= 1, &CheckBoxRect))
			g_Config.m_TcShowFrozenText = g_Config.m_TcShowFrozenText >= 1 ? 0 : 1;

		if(g_Config.m_TcShowFrozenText)
		{
			static int s_CountFrozenText = 0;
			if(DoButton_CheckBox(&s_CountFrozenText, TCLocalize("Count frozen tees"), g_Config.m_TcShowFrozenText == 2, &CheckBoxRect2))
				g_Config.m_TcShowFrozenText = g_Config.m_TcShowFrozenText != 2 ? 2 : 1;
		}
	}
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Tile Outlines ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Tile Outlines"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcOutline, TCLocalize("Show any enabled outlines"), &g_Config.m_TcOutline, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcOutlineEntities, TCLocalize("Only show outlines in entities"), &g_Config.m_TcOutlineEntities, &Column, LineSize);

	auto DoOutlineType = [&](CButtonContainer &ButtonContainer, const char *pName, int &Enable, int &Width, unsigned int &Color, const unsigned int &ColorDefault) {
		// Checkbox & Color
		DoLine_ColorPicker(&ButtonContainer, ColorPickerLineSize, ColorPickerLabelSize, 0, &Column, pName, &Color, ColorDefault, true, &Enable, true);
		// Width
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&Width, &Width, &Button, TCLocalize("Width", "Outlines"), 1, 16);
		//
		Column.HSplitTop(ColorPickerLineSpacing, nullptr, &Column);
	};
	Column.HSplitTop(ColorPickerLineSpacing, nullptr, &Column);
	static CButtonContainer s_aOutlineButtonContainers[5];
	DoOutlineType(s_aOutlineButtonContainers[0], TCLocalize("Unhook & hook"), g_Config.m_TcOutlineSolid, g_Config.m_TcOutlineWidthSolid, g_Config.m_TcOutlineColorSolid, DefaultConfig::TcOutlineColorSolid);
	DoOutlineType(s_aOutlineButtonContainers[1], TCLocalize("Freeze & deep"), g_Config.m_TcOutlineFreeze, g_Config.m_TcOutlineWidthFreeze, g_Config.m_TcOutlineColorFreeze, DefaultConfig::TcOutlineColorFreeze);
	DoOutlineType(s_aOutlineButtonContainers[2], TCLocalize("Unfreeze & undeep"), g_Config.m_TcOutlineUnfreeze, g_Config.m_TcOutlineWidthUnfreeze, g_Config.m_TcOutlineColorUnfreeze, DefaultConfig::TcOutlineColorUnfreeze);
	DoOutlineType(s_aOutlineButtonContainers[3], TCLocalize("Kill"), g_Config.m_TcOutlineKill, g_Config.m_TcOutlineWidthKill, g_Config.m_TcOutlineColorKill, DefaultConfig::TcOutlineColorKill);
	DoOutlineType(s_aOutlineButtonContainers[4], TCLocalize("Tele"), g_Config.m_TcOutlineTele, g_Config.m_TcOutlineWidthTele, g_Config.m_TcOutlineColorTele, DefaultConfig::TcOutlineColorTele);
	Column.h -= ColorPickerLineSpacing;

	// DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcOutlineFreeze, TCLocalize("Outline freeze & deep"), &g_Config.m_TcOutlineFreeze, &Column, LineSize);
	// DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcOutlineSolid, TCLocalize("Outline walls"), &g_Config.m_TcOutlineSolid, &Column, LineSize);
	// DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcOutlineTele, TCLocalize("Outline teleporter"), &g_Config.m_TcOutlineTele, &Column, LineSize);
	// DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcOutlineUnfreeze, TCLocalize("Outline unfreeze & undeep"), &g_Config.m_TcOutlineUnfreeze, &Column, LineSize);
	// DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcOutlineKill, TCLocalize("Outline kill"), &g_Config.m_TcOutlineKill, &Column, LineSize);
	// Column.HSplitTop(LineSize, &Button, &Column);
	// Ui()->DoScrollbarOption(&g_Config.m_TcOutlineWidth, &g_Config.m_TcOutlineWidth, &Button, TCLocalize("Outline width"), 1, 16);
	// Column.HSplitTop(LineSize, &Button, &Column);
	// Ui()->DoScrollbarOption(&g_Config.m_TcOutlineAlpha, &g_Config.m_TcOutlineAlpha, &Button, TCLocalize("Outline alpha"), 0, 100);
	// Column.HSplitTop(LineSize, &Button, &Column);
	// Ui()->DoScrollbarOption(&g_Config.m_TcOutlineAlphaSolid, &g_Config.m_TcOutlineAlphaSolid, &Button, TCLocalize("Outline Alpha (walls)"), 0, 100);
	// static CButtonContainer s_OutlineColorFreezeId, s_OutlineColorSolidId, s_OutlineColorTeleId, s_OutlineColorUnfreezeId, s_OutlineColorKillId;
	// DoLine_ColorPicker(&s_OutlineColorFreezeId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column, TCLocalize("Freeze outline color"), &g_Config.m_TcOutlineColorFreeze, ColorRGBA(0.0f, 0.0f, 0.0f), false);
	// DoLine_ColorPicker(&s_OutlineColorSolidId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column, TCLocalize("Walls outline color"), &g_Config.m_TcOutlineColorSolid, ColorRGBA(0.0f, 0.0f, 0.0f), false);
	// DoLine_ColorPicker(&s_OutlineColorTeleId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column, TCLocalize("Teleporter outline color"), &g_Config.m_TcOutlineColorTele, ColorRGBA(0.0f, 0.0f, 0.0f), false);
	// DoLine_ColorPicker(&s_OutlineColorUnfreezeId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column, TCLocalize("Unfreeze outline color"), &g_Config.m_TcOutlineColorUnfreeze, ColorRGBA(0.0f, 0.0f, 0.0f), false);
	// DoLine_ColorPicker(&s_OutlineColorKillId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column, TCLocalize("Kill outline color"), &g_Config.m_TcOutlineColorKill, ColorRGBA(0.0f, 0.0f, 0.0f), false);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Ghost Tools ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Ghost Tools"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowOthersGhosts, TCLocalize("Show unpredicted ghosts for other players"), &g_Config.m_TcShowOthersGhosts, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcSwapGhosts, TCLocalize("Swap ghosts and normal players"), &g_Config.m_TcSwapGhosts, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcPredGhostsAlpha, &g_Config.m_TcPredGhostsAlpha, &Button, TCLocalize("Predicted alpha"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcUnpredGhostsAlpha, &g_Config.m_TcUnpredGhostsAlpha, &Button, TCLocalize("Unpredicted alpha"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcHideFrozenGhosts, TCLocalize("Hide ghosts of frozen players"), &g_Config.m_TcHideFrozenGhosts, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRenderGhostAsCircle, TCLocalize("Render ghosts as circles"), &g_Config.m_TcRenderGhostAsCircle, &Column, LineSize);

	static CButtonContainer s_ReaderButtonGhost, s_ClearButtonGhost;
	DoLine_KeyReader(Column, s_ReaderButtonGhost, s_ClearButtonGhost, TCLocalize("Toggle ghosts key"), "toggle tc_show_others_ghosts 0 1");

	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Rainbow ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Rainbow"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRainbowTees, TCLocalize("Rainbow Tees"), &g_Config.m_TcRainbowTees, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRainbowWeapon, TCLocalize("Rainbow weapons"), &g_Config.m_TcRainbowWeapon, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRainbowHook, TCLocalize("Rainbow hook"), &g_Config.m_TcRainbowHook, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRainbowOthers, TCLocalize("Rainbow others"), &g_Config.m_TcRainbowOthers, &Column, LineSize);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	static std::vector<const char *> s_RainbowDropDownNames;
	s_RainbowDropDownNames = {TCLocalize("Rainbow"), TCLocalize("Pulse"), TCLocalize("Black"), TCLocalize("Random")};
	static CUi::SDropDownState s_RainbowDropDownState;
	static CScrollRegion s_RainbowDropDownScrollRegion;
	s_RainbowDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_RainbowDropDownScrollRegion;
	int RainbowSelectedOld = g_Config.m_TcRainbowMode - 1;
	CUIRect RainbowDropDownRect;
	Column.HSplitTop(LineSize, &RainbowDropDownRect, &Column);
	const int RainbowSelectedNew = Ui()->DoDropDown(&RainbowDropDownRect, RainbowSelectedOld, s_RainbowDropDownNames.data(), s_RainbowDropDownNames.size(), s_RainbowDropDownState);
	if(RainbowSelectedOld != RainbowSelectedNew)
	{
		g_Config.m_TcRainbowMode = RainbowSelectedNew + 1;
	}
	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcRainbowSpeed, &g_Config.m_TcRainbowSpeed, &Button, TCLocalize("Rainbow speed"), 0, 5000, &CUi::ms_LogarithmicScrollbarScale, 0, "%");
	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	// ***** Tee Trails ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Tee Trails"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcTeeTrail, TCLocalize("Enable tee trails"), &g_Config.m_TcTeeTrail, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcTeeTrailOthers, TCLocalize("Show other tees' trails"), &g_Config.m_TcTeeTrailOthers, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcTeeTrailFade, TCLocalize("Fade trail alpha"), &g_Config.m_TcTeeTrailFade, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcTeeTrailTaper, TCLocalize("Taper trail width"), &g_Config.m_TcTeeTrailTaper, &Column, LineSize);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	std::vector<const char *> vTrailDropDownNames;
	vTrailDropDownNames = {TCLocalize("Solid"), TCLocalize("Tee"), TCLocalize("Rainbow"), TCLocalize("Speed")};
	static CUi::SDropDownState s_TrailDropDownState;
	static CScrollRegion s_TrailDropDownScrollRegion;
	s_TrailDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_TrailDropDownScrollRegion;
	int TrailSelectedOld = g_Config.m_TcTeeTrailColorMode - 1;
	CUIRect TrailDropDownRect;
	Column.HSplitTop(LineSize, &TrailDropDownRect, &Column);
	const int TrailSelectedNew = Ui()->DoDropDown(&TrailDropDownRect, TrailSelectedOld, vTrailDropDownNames.data(), vTrailDropDownNames.size(), s_TrailDropDownState);
	if(TrailSelectedOld != TrailSelectedNew)
	{
		g_Config.m_TcTeeTrailColorMode = TrailSelectedNew + 1;
	}
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	static CButtonContainer s_TeeTrailColor;
	if(g_Config.m_TcTeeTrailColorMode == CTrails::COLORMODE_SOLID)
		DoLine_ColorPicker(&s_TeeTrailColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column, TCLocalize("Tee trail color"), &g_Config.m_TcTeeTrailColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
	else
		Column.HSplitTop(ColorPickerLineSize + ColorPickerLineSpacing, &Button, &Column);

	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcTeeTrailWidth, &g_Config.m_TcTeeTrailWidth, &Button, TCLocalize("Trail width"), 0, 20);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcTeeTrailLength, &g_Config.m_TcTeeTrailLength, &Button, TCLocalize("Trail length"), 0, 200);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcTeeTrailAlpha, &g_Config.m_TcTeeTrailAlpha, &Button, TCLocalize("Trail alpha"), 0, 100);

	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	// ***** BG Draw ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Background Draw"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	static CButtonContainer s_BgDrawColor;
	DoLine_ColorPicker(&s_BgDrawColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column, TCLocalize("Color"), &g_Config.m_TcBgDrawColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);

	Column.HSplitTop(LineSize * 2.0f, &Button, &Column);
	if(g_Config.m_TcBgDrawFadeTime == 0)
		Ui()->DoScrollbarOption(&g_Config.m_TcBgDrawFadeTime, &g_Config.m_TcBgDrawFadeTime, &Button, TCLocalize("Time until strokes disappear"), 0, 600, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE, TCLocalize(" seconds (never)"));
	else
		Ui()->DoScrollbarOption(&g_Config.m_TcBgDrawFadeTime, &g_Config.m_TcBgDrawFadeTime, &Button, TCLocalize("Time until strokes disappear"), 0, 600, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE, TCLocalize(" seconds"));

	Column.HSplitTop(LineSize * 2.0f, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcBgDrawWidth, &g_Config.m_TcBgDrawWidth, &Button, TCLocalize("Width"), 1, 50, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE);

	static CButtonContainer s_ReaderButtonDraw, s_ClearButtonDraw;
	DoLine_KeyReader(Column, s_ReaderButtonDraw, s_ClearButtonDraw, TCLocalize("Draw where mouse is"), "+bg_draw");

	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	// ***** Finish Name ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Finish Name"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcChangeNameNearFinish, TCLocalize("Attempt to change your name when near finish"), &g_Config.m_TcChangeNameNearFinish, &Column, LineSize);
	// Column.HSplitTop(LineSize, &Button, &Column); // TODO finish scan radius
	// Ui()->DoScrollbarOption(&g_Config.m_TcPetSize, &g_Config.m_TcPetSize, &Button, TCLocalize("Pet size"), 10, 500, &CUi::ms_LinearScrollbarScale, 0, "%");
	Column.HSplitTop(LineSize + MarginExtraSmall, &Button, &Column);
	Button.VSplitMid(&Label, &Button);
	Ui()->DoLabel(&Label, TCLocalize("Finish Name:"), FontSize, TEXTALIGN_ML);
	static CLineInput s_FinishName(g_Config.m_TcFinishName, sizeof(g_Config.m_TcFinishName));
	Ui()->DoEditBox(&s_FinishName, &Button, EditBoxFontSize);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** END OF PAGE 1 SETTINGS ***** //
	RightView = Column;

	// Scroll
	CUIRect ScrollRegion;
	ScrollRegion.x = MainView.x;
	ScrollRegion.y = std::max(LeftView.y, RightView.y) + MarginSmall * 2.0f;
	ScrollRegion.w = MainView.w;
	ScrollRegion.h = 0.0f;
	s_ScrollRegion.AddRect(ScrollRegion);
	s_ScrollRegion.End();
}

void CMenus::RenderSettingsTClientBindWheel(CUIRect MainView)
{
	CUIRect LeftView, RightView, Label, Button;
	MainView.VSplitLeft(MainView.w / 2.1f, &LeftView, &RightView);

	const float Radius = std::min(RightView.w, RightView.h) / 2.0f;
	vec2 Center = RightView.Center();
	// Draw Circle
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.3f);
	Graphics()->DrawCircle(Center.x, Center.y, Radius, 64);
	Graphics()->QuadsEnd();

	static char s_aBindName[BINDWHEEL_MAX_NAME];
	static char s_aBindCommand[BINDWHEEL_MAX_CMD];

	static int s_SelectedBindIndex = -1;
	int HoveringIndex = -1;

	float MouseDist = distance(Center, Ui()->MousePos());
	const int SegmentCount = GameClient()->m_BindWheel.m_vBinds.size();
	if(MouseDist < Radius && MouseDist > Radius * 0.25f && SegmentCount > 0)
	{
		float SegmentAngle = 2.0f * pi / SegmentCount;

		float HoveringAngle = angle(Ui()->MousePos() - Center) + SegmentAngle / 2.0f;
		if(HoveringAngle < 0.0f)
			HoveringAngle += 2.0f * pi;

		HoveringIndex = (int)(HoveringAngle / (2.0f * pi) * SegmentCount);
		HoveringIndex = std::clamp(HoveringIndex, 0, SegmentCount - 1);
		if(Ui()->MouseButtonClicked(0))
		{
			s_SelectedBindIndex = HoveringIndex;
			str_copy(s_aBindName, GameClient()->m_BindWheel.m_vBinds[HoveringIndex].m_aName);
			str_copy(s_aBindCommand, GameClient()->m_BindWheel.m_vBinds[HoveringIndex].m_aCommand);
		}
		else if(Ui()->MouseButtonClicked(1) && s_SelectedBindIndex >= 0 && HoveringIndex >= 0 && HoveringIndex != s_SelectedBindIndex)
		{
			CBindWheel::CBind BindA = GameClient()->m_BindWheel.m_vBinds[s_SelectedBindIndex];
			CBindWheel::CBind BindB = GameClient()->m_BindWheel.m_vBinds[HoveringIndex];
			str_copy(GameClient()->m_BindWheel.m_vBinds[s_SelectedBindIndex].m_aName, BindB.m_aName);
			str_copy(GameClient()->m_BindWheel.m_vBinds[s_SelectedBindIndex].m_aCommand, BindB.m_aCommand);
			str_copy(GameClient()->m_BindWheel.m_vBinds[HoveringIndex].m_aName, BindA.m_aName);
			str_copy(GameClient()->m_BindWheel.m_vBinds[HoveringIndex].m_aCommand, BindA.m_aCommand);
		}
		else if(Ui()->MouseButtonClicked(2))
		{
			s_SelectedBindIndex = HoveringIndex;
		}
	}
	else if(MouseDist < Radius && Ui()->MouseButtonClicked(0))
	{
		s_SelectedBindIndex = -1;
		str_copy(s_aBindName, "");
		str_copy(s_aBindCommand, "");
	}

	const float Theta = pi * 2.0f / std::max<float>(1.0f, GameClient()->m_BindWheel.m_vBinds.size());
	for(int i = 0; i < static_cast<int>(GameClient()->m_BindWheel.m_vBinds.size()); i++)
	{
		TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));

		float SegmentFontSize = FontSize * 1.1f;
		if(i == s_SelectedBindIndex)
		{
			SegmentFontSize = FontSize * 1.7f;
			TextRender()->TextColor(ColorRGBA(0.5f, 1.0f, 0.75f, 1.0f));
		}
		else if(i == HoveringIndex)
		{
			SegmentFontSize = FontSize * 1.35f;
		}

		const CBindWheel::CBind Bind = GameClient()->m_BindWheel.m_vBinds[i];
		const float Angle = Theta * i;

		const vec2 Pos = direction(Angle) * (Radius * 0.75f) + Center;
		const CUIRect Rect = CUIRect{Pos.x - 50.0f, Pos.y - 50.0f, 100.0f, 100.0f};
		Ui()->DoLabel(&Rect, Bind.m_aName, SegmentFontSize, TEXTALIGN_MC);
	}

	TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));

	LeftView.HSplitTop(LineSize, &Button, &LeftView);
	Button.VSplitLeft(100.0f, &Label, &Button);
	Ui()->DoLabel(&Label, TCLocalize("Name:"), FontSize, TEXTALIGN_ML);
	static CLineInput s_NameInput;
	s_NameInput.SetBuffer(s_aBindName, sizeof(s_aBindName));
	s_NameInput.SetEmptyText(TCLocalize("Name"));
	Ui()->DoEditBox(&s_NameInput, &Button, EditBoxFontSize);

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize, &Button, &LeftView);
	Button.VSplitLeft(100.0f, &Label, &Button);
	Ui()->DoLabel(&Label, TCLocalize("Command:"), FontSize, TEXTALIGN_ML);
	static CLineInput s_BindInput;
	s_BindInput.SetBuffer(s_aBindCommand, sizeof(s_aBindCommand));
	s_BindInput.SetEmptyText(TCLocalize("Command"));
	Ui()->DoEditBox(&s_BindInput, &Button, EditBoxFontSize);

	static CButtonContainer s_AddButton, s_RemoveButton, s_OverrideButton;

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize, &Button, &LeftView);
	if(DoButton_Menu(&s_OverrideButton, TCLocalize("Override Selected"), 0, &Button) && s_SelectedBindIndex >= 0 && s_SelectedBindIndex < static_cast<int>(GameClient()->m_BindWheel.m_vBinds.size()))
	{
		CBindWheel::CBind TempBind;
		if(str_length(s_aBindName) == 0)
			str_copy(TempBind.m_aName, "*");
		else
			str_copy(TempBind.m_aName, s_aBindName);

		str_copy(GameClient()->m_BindWheel.m_vBinds[s_SelectedBindIndex].m_aName, TempBind.m_aName);
		str_copy(GameClient()->m_BindWheel.m_vBinds[s_SelectedBindIndex].m_aCommand, s_aBindCommand);
	}
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize, &Button, &LeftView);
	CUIRect ButtonAdd, ButtonRemove;
	Button.VSplitMid(&ButtonRemove, &ButtonAdd, MarginSmall);
	if(DoButton_Menu(&s_AddButton, TCLocalize("Add Bind"), 0, &ButtonAdd))
	{
		CBindWheel::CBind TempBind;
		if(str_length(s_aBindName) == 0)
			str_copy(TempBind.m_aName, "*");
		else
			str_copy(TempBind.m_aName, s_aBindName);

		GameClient()->m_BindWheel.AddBind(TempBind.m_aName, s_aBindCommand);
		s_SelectedBindIndex = static_cast<int>(GameClient()->m_BindWheel.m_vBinds.size()) - 1;
	}
	if(DoButton_Menu(&s_RemoveButton, TCLocalize("Remove Bind"), 0, &ButtonRemove) && s_SelectedBindIndex >= 0)
	{
		GameClient()->m_BindWheel.RemoveBind(s_SelectedBindIndex);
		s_SelectedBindIndex = -1;
	}

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("The command is ran in console not chat"), FontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(LineSize * 0.8f, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("Use left mouse to select"), FontSize * 0.8f, TEXTALIGN_ML);
	LeftView.HSplitTop(LineSize * 0.8f, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("Use right mouse to swap with selected"), FontSize * 0.8f, TEXTALIGN_ML);
	LeftView.HSplitTop(LineSize * 0.8f, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("Use middle mouse select without copy"), FontSize * 0.8f, TEXTALIGN_ML);

	LeftView.HSplitBottom(LineSize, &LeftView, &Label);
	static CButtonContainer s_ReaderButtonWheel, s_ClearButtonWheel;
	DoLine_KeyReader(Label, s_ReaderButtonWheel, s_ClearButtonWheel, TCLocalize("Bind Wheel Key"), "+bindwheel");

	LeftView.HSplitBottom(LineSize, &LeftView, &Label);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcResetBindWheelMouse, TCLocalize("Reset position of mouse when opening bindwheel"), &g_Config.m_TcResetBindWheelMouse, &Label, LineSize);
}

void CMenus::RenderSettingsTClientChatBinds(CUIRect MainView)
{
	CUIRect LeftView, RightView, Button, Label;

	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	ScrollParams.m_ForceShowScrollbar = true;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	const CUIRect UnscrolledMainView = MainView;
	s_ScrollRegion.Begin(&MainView, &ScrollParams);
	const vec2 ScrollOffset(MainView.x - UnscrolledMainView.x, MainView.y - UnscrolledMainView.y);

	static std::vector<CUIRect> s_SectionBoxes;
	static vec2 s_PrevScrollOffset(0.0f, 0.0f);

	MainView.HSplitTop(Margin, nullptr, &MainView);
	MainView.VSplitRight(5.0f, &MainView, nullptr); // Padding for scrollbar
	MainView.VSplitLeft(5.0f, nullptr, &MainView); // Padding for scrollbar

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	for(CUIRect &Section : s_SectionBoxes)
	{
		float Padding = MarginBetweenViews * 0.6666f;
		Section.w += Padding;
		Section.h += Padding;
		Section.x -= Padding * 0.5f;
		Section.y -= Padding * 0.5f;
		Section.y -= s_PrevScrollOffset.y - ScrollOffset.y;
		Section.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 10.0f);
	}
	s_PrevScrollOffset = ScrollOffset;
	s_SectionBoxes.clear();

	// ***** All the stuff ***** //

	auto DoBindchatDefault = [&](CUIRect &Column, CBindChat::CBindDefault &BindDefault) {
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		CBindChat::CBind *pOldBind = GameClient()->m_BindChat.GetBind(BindDefault.m_Bind.m_aCommand);
		static char s_aTempName[BINDCHAT_MAX_NAME] = "";
		char *pName;
		if(pOldBind == nullptr)
			pName = s_aTempName;
		else
			pName = pOldBind->m_aName;
		if(DoEditBoxWithLabel(&BindDefault.m_LineInput, &Button, TCLocalize(BindDefault.m_pTitle), BindDefault.m_Bind.m_aName, pName, BINDCHAT_MAX_NAME) && BindDefault.m_LineInput.IsActive())
		{
			if(!pOldBind && pName[0] != '\0')
			{
				auto BindNew = BindDefault.m_Bind;
				str_copy(BindNew.m_aName, pName);
				GameClient()->m_BindChat.RemoveBind(pName); // Prevent duplicates
				GameClient()->m_BindChat.AddBind(BindNew);
				s_aTempName[0] = '\0';
			}
			if(pOldBind && pName[0] == '\0')
			{
				GameClient()->m_BindChat.RemoveBind(pName);
			}
		}
	};

	auto DoBindchatDefaults = [&](CUIRect &Column, const char *pTitle, std::vector<CBindChat::CBindDefault> &vBindchatDefaults) {
		s_SectionBoxes.push_back(Column);
		Column.HSplitTop(HeadlineHeight, &Label, &Column);
		Ui()->DoLabel(&Label, pTitle, HeadlineFontSize, TEXTALIGN_ML);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		for(CBindChat::CBindDefault &BindchatDefault : vBindchatDefaults)
			DoBindchatDefault(Column, BindchatDefault);
		s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
		Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	};

	float SizeL = 0.0f, SizeR = 0.0f;
	for(auto &[pTitle, vBindDefaults] : CBindChat::BIND_DEFAULTS)
	{
		float &Size = SizeL > SizeR ? SizeR : SizeL;
		CUIRect &Column = SizeL > SizeR ? RightView : LeftView;
		DoBindchatDefaults(Column, TCLocalize(pTitle), vBindDefaults);
		Size += vBindDefaults.size() * (MarginSmall + LineSize) + HeadlineHeight + HeadlineFontSize + MarginSmall * 2.0f;
	}

	// Scroll
	CUIRect ScrollRegion;
	ScrollRegion.x = MainView.x;
	ScrollRegion.y = std::max(LeftView.y, RightView.y) + MarginSmall * 2.0f;
	ScrollRegion.w = MainView.w;
	ScrollRegion.h = 0.0f;
	s_ScrollRegion.AddRect(ScrollRegion);
	s_ScrollRegion.End();
}

void CMenus::RenderSettingsTClientWarList(CUIRect MainView)
{
	CUIRect RightView, LeftView, Column1, Column2, Column3, Column4, Button, ButtonL, ButtonR, Label;

	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	MainView.VSplitMid(&LeftView, &RightView, Margin);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	// WAR LIST will have 4 columns
	//  [War entries] - [Entry Editing] - [Group Types] - [Recent Players]
	//									 [Group Editing]

	// putting this here so it can be updated by the entry list
	static char s_aEntryName[MAX_NAME_LENGTH];
	static char s_aEntryClan[MAX_CLAN_LENGTH];
	static char s_aEntryReason[MAX_WARLIST_REASON_LENGTH];
	static bool s_IsClan = false;
	static bool s_IsName = true;

	LeftView.VSplitMid(&Column1, &Column2, Margin);
	RightView.VSplitMid(&Column3, &Column4, Margin);

	// ======WAR ENTRIES======
	static CWarEntry *s_pSelectedEntry = nullptr;
	static CWarType *s_pSelectedType = GameClient()->m_WarList.m_WarTypes[0];
	{
		Column1.HSplitTop(HeadlineHeight, &Label, &Column1);
		Label.VSplitRight(25.0f, &Label, &Button);
		Ui()->DoLabel(&Label, TCLocalize("War Entries"), HeadlineFontSize, TEXTALIGN_ML);
		Column1.HSplitTop(MarginSmall, nullptr, &Column1);

		static CButtonContainer s_ReverseEntries;
		static bool s_Reversed = true;
		if(Ui()->DoButton_FontIcon(&s_ReverseEntries, s_Reversed ? FontIcon::CHEVRON_UP : FontIcon::CHEVRON_DOWN, 0, &Button, IGraphics::CORNER_ALL))
		{
			s_Reversed = !s_Reversed;
		}

		CUIRect EntriesSearch;
		Column1.HSplitBottom(25.0f, &Column1, &EntriesSearch);
		EntriesSearch.HSplitTop(MarginSmall, nullptr, &EntriesSearch);

		// Filter the list
		static CLineInputBuffered<128> s_EntriesFilterInput;
		std::vector<CWarEntry *> vpFilteredEntries;
		for(CWarEntry &Entry : GameClient()->m_WarList.m_vWarEntries)
		{
			if(str_find_nocase(Entry.m_aName, s_EntriesFilterInput.GetString()))
				vpFilteredEntries.push_back(&Entry);
			else if(str_find_nocase(Entry.m_aClan, s_EntriesFilterInput.GetString()))
				vpFilteredEntries.push_back(&Entry);
			else if(str_find_nocase(Entry.m_pWarType->m_aWarName, s_EntriesFilterInput.GetString()))
				vpFilteredEntries.push_back(&Entry);
		}
		if(s_Reversed)
			std::reverse(vpFilteredEntries.begin(), vpFilteredEntries.end());

		int SelectedOldEntry = -1;
		static CListBox s_EntriesListBox;
		s_EntriesListBox.DoStart(35.0f, vpFilteredEntries.size(), 1, 2, SelectedOldEntry, &Column1);

		static std::vector<unsigned char> s_vItemIds;
		static std::vector<CButtonContainer> s_vDeleteButtons;

		const int MaxEntries = GameClient()->m_WarList.m_vWarEntries.size();
		s_vItemIds.resize(MaxEntries);
		s_vDeleteButtons.resize(MaxEntries);

		for(size_t i = 0; i < vpFilteredEntries.size(); i++)
		{
			CWarEntry *pEntry = vpFilteredEntries[i];

			if(s_pSelectedEntry && pEntry == s_pSelectedEntry)
				SelectedOldEntry = i;

			const CListboxItem Item = s_EntriesListBox.DoNextItem(&s_vItemIds[i], SelectedOldEntry >= 0 && (size_t)SelectedOldEntry == i);
			if(!Item.m_Visible)
				continue;

			CUIRect EntryRect, DeleteButton, EntryTypeRect, WarType, ToolTip;
			Item.m_Rect.Margin(0.0f, &EntryRect);
			EntryRect.VSplitLeft(26.0f, &DeleteButton, &EntryRect);
			DeleteButton.HMargin(7.5f, &DeleteButton);
			DeleteButton.VSplitLeft(MarginSmall, nullptr, &DeleteButton);
			DeleteButton.VSplitRight(MarginExtraSmall, &DeleteButton, nullptr);

			if(Ui()->DoButton_FontIcon(&s_vDeleteButtons[i], FontIcon::TRASH, 0, &DeleteButton, IGraphics::CORNER_ALL))
				GameClient()->m_WarList.RemoveWarEntry(pEntry);

			bool IsClan = false;
			char aBuf[32];
			if(str_comp(pEntry->m_aClan, "") != 0)
			{
				str_copy(aBuf, pEntry->m_aClan);
				IsClan = true;
			}
			else
			{
				str_copy(aBuf, pEntry->m_aName);
			}
			EntryRect.VSplitLeft(35.0f, &EntryTypeRect, &EntryRect);

			if(IsClan)
			{
				RenderFontIcon(EntryTypeRect, FontIcon::ICON_USERS, 18.0f, TEXTALIGN_MC);
			}
			else
			{
				// TODO: stop misusing this function
				// TODO: render the real skin with skin remembering component (to be added)
				RenderDevSkin(EntryTypeRect.Center(), 35.0f, "default", "default", false, 0, 0, 0, false, false);
			}

			if(str_comp(pEntry->m_aReason, "") != 0)
			{
				EntryRect.VSplitRight(20.0f, &EntryRect, &ToolTip);
				RenderFontIcon(ToolTip, FontIcon::COMMENT, 18.0f, TEXTALIGN_MC);
				GameClient()->m_Tooltips.DoToolTip(&s_vItemIds[i], &ToolTip, pEntry->m_aReason);
				GameClient()->m_Tooltips.SetFadeTime(&s_vItemIds[i], 0.0f);
			}

			EntryRect.HMargin(MarginExtraSmall, &EntryRect);
			EntryRect.HSplitMid(&EntryRect, &WarType, MarginSmall);

			Ui()->DoLabel(&EntryRect, aBuf, StandardFontSize, TEXTALIGN_ML);
			TextRender()->TextColor(pEntry->m_pWarType->m_Color);
			Ui()->DoLabel(&WarType, pEntry->m_pWarType->m_aWarName, StandardFontSize, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
		const int NewSelectedEntry = s_EntriesListBox.DoEnd();
		if(SelectedOldEntry != NewSelectedEntry || (SelectedOldEntry >= 0 && Ui()->HotItem() == &s_vItemIds[NewSelectedEntry] && Ui()->MouseButtonClicked(0)))
		{
			s_pSelectedEntry = vpFilteredEntries[NewSelectedEntry];
			if(!Ui()->LastMouseButton(1) && !Ui()->LastMouseButton(2))
			{
				str_copy(s_aEntryName, s_pSelectedEntry->m_aName);
				str_copy(s_aEntryClan, s_pSelectedEntry->m_aClan);
				str_copy(s_aEntryReason, s_pSelectedEntry->m_aReason);
				if(str_comp(s_pSelectedEntry->m_aClan, "") != 0)
				{
					s_IsName = false;
					s_IsClan = true;
				}
				else
				{
					s_IsName = true;
					s_IsClan = false;
				}
				s_pSelectedType = s_pSelectedEntry->m_pWarType;
			}
		}

		Ui()->DoEditBox_Search(&s_EntriesFilterInput, &EntriesSearch, 14.0f, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive());
	}

	// ======WAR ENTRY EDITING======
	Column2.HSplitTop(HeadlineHeight, &Label, &Column2);
	Label.VSplitRight(25.0f, &Label, &Button);
	Ui()->DoLabel(&Label, TCLocalize("Edit Entry"), HeadlineFontSize, TEXTALIGN_ML);
	Column2.HSplitTop(MarginSmall, nullptr, &Column2);
	Column2.HSplitTop(HeadlineFontSize, &Button, &Column2);

	Button.VSplitMid(&ButtonL, &ButtonR, MarginSmall);
	static CLineInput s_NameInput;
	s_NameInput.SetBuffer(s_aEntryName, sizeof(s_aEntryName));
	s_NameInput.SetEmptyText(TCLocalize("Name"));
	if(s_IsName)
	{
		Ui()->DoEditBox(&s_NameInput, &ButtonL, 12.0f);
	}
	else
	{
		ButtonL.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), 15, 3.0f);
		Ui()->ClipEnable(&ButtonL);
		ButtonL.VMargin(2.0f, &ButtonL);
		s_NameInput.Render(&ButtonL, 12.0f, TEXTALIGN_ML, false, -1.0f, 0.0f);
		Ui()->ClipDisable();
	}

	static CLineInput s_ClanInput;
	s_ClanInput.SetBuffer(s_aEntryClan, sizeof(s_aEntryClan));
	s_ClanInput.SetEmptyText(TCLocalize("Clan"));
	if(s_IsClan)
		Ui()->DoEditBox(&s_ClanInput, &ButtonR, 12.0f);
	else
	{
		ButtonR.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), 15, 3.0f);
		Ui()->ClipEnable(&ButtonR);
		ButtonR.VMargin(2.0f, &ButtonR);
		s_ClanInput.Render(&ButtonR, 12.0f, TEXTALIGN_ML, false, -1.0f, 0.0f);
		Ui()->ClipDisable();
	}

	Column2.HSplitTop(MarginSmall, nullptr, &Column2);
	Column2.HSplitTop(LineSize, &Button, &Column2);
	Button.VSplitMid(&ButtonL, &ButtonR, MarginSmall);
	static unsigned char s_NameRadio, s_ClanRadio;
	if(DoButton_CheckBox_Common(&s_NameRadio, TCLocalize("Name"), s_IsName ? "X" : "", &ButtonL, BUTTONFLAG_LEFT))
	{
		s_IsName = true;
		s_IsClan = false;
	}
	if(DoButton_CheckBox_Common(&s_ClanRadio, TCLocalize("Clan"), s_IsClan ? "X" : "", &ButtonR, BUTTONFLAG_LEFT))
	{
		s_IsName = false;
		s_IsClan = true;
	}
	if(!s_IsName)
		str_copy(s_aEntryName, "");
	if(!s_IsClan)
		str_copy(s_aEntryClan, "");

	Column2.HSplitTop(MarginSmall, nullptr, &Column2);
	Column2.HSplitTop(HeadlineFontSize, &Button, &Column2);
	static CLineInput s_ReasonInput;
	s_ReasonInput.SetBuffer(s_aEntryReason, sizeof(s_aEntryReason));
	s_ReasonInput.SetEmptyText(TCLocalize("Reason"));
	Ui()->DoEditBox(&s_ReasonInput, &Button, 12.0f);

	static CButtonContainer s_AddButton, s_OverrideButton;

	Column2.HSplitTop(MarginSmall, nullptr, &Column2);
	Column2.HSplitTop(LineSize * 2.0f, &Button, &Column2);
	Button.VSplitMid(&ButtonL, &ButtonR, MarginSmall);

	if(DoButtonLineSize_Menu(&s_OverrideButton, TCLocalize("Override Entry"), 0, &ButtonL, LineSize) && s_pSelectedEntry)
	{
		if(s_pSelectedEntry && s_pSelectedType && (str_comp(s_aEntryName, "") != 0 || str_comp(s_aEntryClan, "") != 0))
		{
			str_copy(s_pSelectedEntry->m_aName, s_aEntryName);
			str_copy(s_pSelectedEntry->m_aClan, s_aEntryClan);
			str_copy(s_pSelectedEntry->m_aReason, s_aEntryReason);
			s_pSelectedEntry->m_pWarType = s_pSelectedType;
		}
	}
	if(DoButtonLineSize_Menu(&s_AddButton, TCLocalize("Add Entry"), 0, &ButtonR, LineSize))
	{
		if(s_pSelectedType)
			GameClient()->m_WarList.AddWarEntry(s_aEntryName, s_aEntryClan, s_aEntryReason, s_pSelectedType->m_aWarName);
	}
	Column2.HSplitTop(MarginSmall, nullptr, &Column2);
	Column2.HSplitTop(HeadlineFontSize + MarginSmall, &Button, &Column2);
	if(s_pSelectedType)
	{
		float Shade = 0.0f;
		Button.Draw(ColorRGBA(Shade, Shade, Shade, 0.25f), 15, 3.0f);
		TextRender()->TextColor(s_pSelectedType->m_Color);
		Ui()->DoLabel(&Button, s_pSelectedType->m_aWarName, HeadlineFontSize, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}

	Column2.HSplitBottom(150.0f, nullptr, &Column2);

	Column2.HSplitTop(HeadlineHeight, &Label, &Column2);
	Ui()->DoLabel(&Label, TCLocalize("Settings"), HeadlineFontSize, TEXTALIGN_ML);
	Column2.HSplitTop(MarginSmall, nullptr, &Column2);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListAllowDuplicates, TCLocalize("Allow Duplicate Entries"), &g_Config.m_TcWarListAllowDuplicates, &Column2, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarList, TCLocalize("Enable warlist"), &g_Config.m_TcWarList, &Column2, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListChat, TCLocalize("Colors in chat"), &g_Config.m_TcWarListChat, &Column2, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListScoreboard, TCLocalize("Colors in scoreboard"), &g_Config.m_TcWarListScoreboard, &Column2, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListSpectate, TCLocalize("Colors in spectate select"), &g_Config.m_TcWarListSpectate, &Column2, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListShowClan, TCLocalize("Show clan if war"), &g_Config.m_TcWarListShowClan, &Column2, LineSize);

	// ======WAR TYPE EDITING======

	Column3.HSplitTop(HeadlineHeight, &Label, &Column3);
	Ui()->DoLabel(&Label, TCLocalize("War Groups"), HeadlineFontSize, TEXTALIGN_ML);
	Column3.HSplitTop(MarginSmall, nullptr, &Column3);

	static char s_aTypeName[MAX_WARLIST_TYPE_LENGTH];
	static ColorRGBA s_GroupColor = ColorRGBA(1, 1, 1, 1);

	CUIRect WarTypeList;
	Column3.HSplitBottom(180.0f, &WarTypeList, &Column3);
	m_pRemoveWarType = nullptr;
	int SelectedOldType = -1;
	static CListBox s_WarTypeListBox;
	s_WarTypeListBox.DoStart(25.0f, GameClient()->m_WarList.m_WarTypes.size(), 1, 2, SelectedOldType, &WarTypeList, true, IGraphics::CORNER_ALL, true);

	static std::vector<unsigned char> s_vTypeItemIds;
	static std::vector<CButtonContainer> s_vTypeDeleteButtons;

	const int MaxTypes = GameClient()->m_WarList.m_WarTypes.size();
	s_vTypeItemIds.resize(MaxTypes);
	s_vTypeDeleteButtons.resize(MaxTypes);

	for(int i = 0; i < (int)GameClient()->m_WarList.m_WarTypes.size(); i++)
	{
		CWarType *pType = GameClient()->m_WarList.m_WarTypes[i];

		if(!pType)
			continue;

		if(s_pSelectedType && pType == s_pSelectedType)
			SelectedOldType = i;

		const CListboxItem Item = s_WarTypeListBox.DoNextItem(&s_vTypeItemIds[i], SelectedOldType >= 0 && SelectedOldType == i);
		if(!Item.m_Visible)
			continue;

		CUIRect TypeRect, DeleteButton;
		Item.m_Rect.Margin(0.0f, &TypeRect);

		if(pType->m_Removable)
		{
			TypeRect.VSplitRight(20.0f, &TypeRect, &DeleteButton);
			DeleteButton.HSplitTop(20.0f, &DeleteButton, nullptr);
			DeleteButton.Margin(2.0f, &DeleteButton);
			if(DoButtonNoRect_FontIcon(&s_vTypeDeleteButtons[i], FontIcon::TRASH, 0, &DeleteButton, IGraphics::CORNER_ALL))
				m_pRemoveWarType = pType;
		}
		TextRender()->TextColor(pType->m_Color);
		Ui()->DoLabel(&TypeRect, pType->m_aWarName, StandardFontSize, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
	const int NewSelectedType = s_WarTypeListBox.DoEnd();
	if((SelectedOldType != NewSelectedType && NewSelectedType >= 0) || (NewSelectedType >= 0 && Ui()->HotItem() == &s_vTypeItemIds[NewSelectedType] && Ui()->MouseButtonClicked(0)))
	{
		s_pSelectedType = GameClient()->m_WarList.m_WarTypes[NewSelectedType];
		if(!Ui()->LastMouseButton(1) && !Ui()->LastMouseButton(2))
		{
			str_copy(s_aTypeName, s_pSelectedType->m_aWarName);
			s_GroupColor = s_pSelectedType->m_Color;
		}
	}
	if(m_pRemoveWarType != nullptr)
	{
		char aMessage[256];
		str_format(aMessage, sizeof(aMessage),
			TCLocalize("Are you sure that you want to remove '%s' from your war groups?"),
			m_pRemoveWarType->m_aWarName);
		PopupConfirm(TCLocalize("Remove War Group"), aMessage, TCLocalize("Yes"), TCLocalize("No"), &CMenus::PopupConfirmRemoveWarType);
	}

	static CLineInput s_TypeNameInput;
	Column3.HSplitTop(MarginSmall, nullptr, &Column3);
	Column3.HSplitTop(HeadlineFontSize + MarginSmall, &Button, &Column3);
	s_TypeNameInput.SetBuffer(s_aTypeName, sizeof(s_aTypeName));
	s_TypeNameInput.SetEmptyText("Group Name");
	Ui()->DoEditBox(&s_TypeNameInput, &Button, 12.0f);
	static CButtonContainer s_AddGroupButton, s_OverrideGroupButton, s_GroupColorPicker;

	Column3.HSplitTop(MarginSmall, nullptr, &Column3);
	static unsigned int s_ColorValue = 0;
	s_ColorValue = color_cast<ColorHSLA>(s_GroupColor).Pack(false);
	ColorHSLA PickedColor = DoLine_ColorPicker(&s_GroupColorPicker, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column3, TCLocalize("Color"), &s_ColorValue, ColorRGBA(1.0f, 1.0f, 1.0f), true);
	s_GroupColor = color_cast<ColorRGBA>(PickedColor);

	Column3.HSplitTop(LineSize * 2.0f, &Button, &Column3);
	Button.VSplitMid(&ButtonL, &ButtonR, MarginSmall);
	bool OverrideDisabled = NewSelectedType == 0;
	if(DoButtonLineSize_Menu(&s_OverrideGroupButton, TCLocalize("Override Group"), 0, &ButtonL, LineSize, OverrideDisabled) && s_pSelectedType)
	{
		if(s_pSelectedType && str_comp(s_aTypeName, "") != 0)
		{
			str_copy(s_pSelectedType->m_aWarName, s_aTypeName);
			s_pSelectedType->m_Color = s_GroupColor;
		}
	}
	bool AddDisabled = str_comp(GameClient()->m_WarList.FindWarType(s_aTypeName)->m_aWarName, "none") != 0 || str_comp(s_aTypeName, "none") == 0;
	if(DoButtonLineSize_Menu(&s_AddGroupButton, TCLocalize("Add Group"), 0, &ButtonR, LineSize, AddDisabled))
	{
		GameClient()->m_WarList.AddWarType(s_aTypeName, s_GroupColor);
	}

	// ======ONLINE PLAYER LIST======

	Column4.HSplitTop(HeadlineHeight, &Label, &Column4);
	Ui()->DoLabel(&Label, TCLocalize("Online Players"), HeadlineFontSize, TEXTALIGN_ML);
	Column4.HSplitTop(MarginSmall, nullptr, &Column4);

	CUIRect PlayerSearch;
	Column4.HSplitBottom(25.0f, &Column4, &PlayerSearch);
	PlayerSearch.HSplitTop(MarginSmall, nullptr, &PlayerSearch);
	static CLineInputBuffered<128> s_PlayerSearchInput;
	Ui()->DoEditBox_Search(&s_PlayerSearchInput, &PlayerSearch, 14.0f, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive());

	CUIRect PlayerList;
	Column4.HSplitBottom(0.0f, &PlayerList, &Column4);
	static CListBox s_PlayerListBox;
	s_PlayerListBox.DoStart(30.0f, MAX_CLIENTS, 1, 2, -1, &PlayerList, true, IGraphics::CORNER_ALL, true);

	static std::vector<unsigned char> s_vPlayerItemIds;
	static std::vector<CButtonContainer> s_vNameButtons;
	static std::vector<CButtonContainer> s_vClanButtons;

	s_vPlayerItemIds.resize(MAX_CLIENTS);
	s_vNameButtons.resize(MAX_CLIENTS);
	s_vClanButtons.resize(MAX_CLIENTS);

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!GameClient()->m_Snap.m_apPlayerInfos[i])
			continue;

		const auto &Client = GameClient()->m_aClients[i];

		if(!str_find_nocase(Client.m_aName, s_PlayerSearchInput.GetString()) &&
			!str_find_nocase(Client.m_aClan, s_PlayerSearchInput.GetString()))
			continue;

		const CListboxItem Item = s_PlayerListBox.DoNextItem(&s_vPlayerItemIds[i], false);
		if(!Item.m_Visible)
			continue;

		CUIRect PlayerRect, TeeRect, NameRect, ClanRect;
		Item.m_Rect.Margin(0.0f, &PlayerRect);
		PlayerRect.VSplitLeft(25.0f, &TeeRect, &PlayerRect);

		PlayerRect.VSplitMid(&NameRect, &ClanRect);
		PlayerRect = NameRect;
		PlayerRect.x = TeeRect.x;
		PlayerRect.w += TeeRect.w;
		TextRender()->TextColor(GameClient()->m_WarList.GetWarData(i).m_NameColor);
		ColorRGBA NameButtonColor = Ui()->CheckActiveItem(&s_vNameButtons[i]) ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.75f) :
											(Ui()->HotItem() == &s_vNameButtons[i] ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.33f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.0f));
		PlayerRect.Draw(NameButtonColor, IGraphics::CORNER_L, 5.0f);
		Ui()->DoLabel(&NameRect, Client.m_aName, StandardFontSize, TEXTALIGN_ML);
		if(Ui()->DoButtonLogic(&s_vNameButtons[i], false, &PlayerRect, BUTTONFLAG_LEFT))
		{
			s_IsName = true;
			s_IsClan = false;
			str_copy(s_aEntryName, Client.m_aName);
		}

		TextRender()->TextColor(GameClient()->m_WarList.GetWarData(i).m_ClanColor);
		ColorRGBA ClanButtonColor = Ui()->CheckActiveItem(&s_vClanButtons[i]) ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.75f) :
											(Ui()->HotItem() == &s_vClanButtons[i] ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.33f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.0f));
		ClanRect.Draw(ClanButtonColor, IGraphics::CORNER_R, 5.0f);
		Ui()->DoLabel(&ClanRect, Client.m_aClan, StandardFontSize, TEXTALIGN_ML);
		if(Ui()->DoButtonLogic(&s_vClanButtons[i], false, &ClanRect, BUTTONFLAG_LEFT))
		{
			s_IsName = false;
			s_IsClan = true;
			str_copy(s_aEntryClan, Client.m_aClan);
		}
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		CTeeRenderInfo TeeInfo = Client.m_RenderInfo;
		TeeInfo.m_Size = 25.0f;
		RenderTeeCute(CAnimState::GetIdle(), &TeeInfo, 0, vec2(1.0f, 0.0f), TeeRect.Center() + vec2(-1.0f, 2.5f), true);
	}
	s_PlayerListBox.DoEnd();
}

void CMenus::RenderSettingsTClientStatusBar(CUIRect MainView)
{
	CUIRect LeftView, RightView, Button, Label, StatusBar;
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitBottom(100.0f, &MainView, &StatusBar);

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("Status Bar"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcStatusBar, TCLocalize("Show status bar"), &g_Config.m_TcStatusBar, &LeftView, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcStatusBarLabels, TCLocalize("Show labels on status bar items"), &g_Config.m_TcStatusBarLabels, &LeftView, LineSize);
	LeftView.HSplitTop(LineSize, &Button, &LeftView);
	Ui()->DoScrollbarOption(&g_Config.m_TcStatusBarHeight, &g_Config.m_TcStatusBarHeight, &Button, TCLocalize("Status bar height"), 1, 16);

	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);

	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("Local Time"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcStatusBar12HourClock, TCLocalize("Use 12 hour clock"), &g_Config.m_TcStatusBar12HourClock, &LeftView, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcStatusBarLocalTimeSeocnds, TCLocalize("Show seconds on clock"), &g_Config.m_TcStatusBarLocalTimeSeocnds, &LeftView, LineSize);
	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);

	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("Colors"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	static CButtonContainer s_StatusbarColor, s_StatusbarTextColor;

	DoLine_ColorPicker(&s_StatusbarColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, TCLocalize("Status bar color"), &g_Config.m_TcStatusBarColor, ColorRGBA(0.0f, 0.0f, 0.0f), false);
	DoLine_ColorPicker(&s_StatusbarTextColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, TCLocalize("Text color"), &g_Config.m_TcStatusBarTextColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
	LeftView.HSplitTop(LineSize, &Button, &LeftView);
	Ui()->DoScrollbarOption(&g_Config.m_TcStatusBarAlpha, &g_Config.m_TcStatusBarAlpha, &Button, TCLocalize("Status bar alpha"), 0, 100);
	LeftView.HSplitTop(LineSize, &Button, &LeftView);
	Ui()->DoScrollbarOption(&g_Config.m_TcStatusBarTextAlpha, &g_Config.m_TcStatusBarTextAlpha, &Button, TCLocalize("Text alpha"), 0, 100);

	RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("Status Bar Codes:"), HeadlineFontSize, TEXTALIGN_ML);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	RightView.HSplitTop(LineSize, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("a = Angle"), FontSize, TEXTALIGN_ML);
	RightView.HSplitTop(LineSize, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("p = Ping"), FontSize, TEXTALIGN_ML);
	RightView.HSplitTop(LineSize, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("d = Prediction"), FontSize, TEXTALIGN_ML);
	RightView.HSplitTop(LineSize, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("c = Position"), FontSize, TEXTALIGN_ML);
	RightView.HSplitTop(LineSize, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("l = Local Time"), FontSize, TEXTALIGN_ML);
	RightView.HSplitTop(LineSize, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("r = Race Time"), FontSize, TEXTALIGN_ML);
	RightView.HSplitTop(LineSize, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("f = FPS"), FontSize, TEXTALIGN_ML);
	RightView.HSplitTop(LineSize, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("v = Velocity"), FontSize, TEXTALIGN_ML);
	RightView.HSplitTop(LineSize, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("z = Zoom"), FontSize, TEXTALIGN_ML);
	RightView.HSplitTop(LineSize, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("_ or ' ' = Space"), FontSize, TEXTALIGN_ML);
	static int s_SelectedItem = -1;
	static int s_TypeSelectedOld = -1;

	CUIRect StatusScheme, StatusButtons, ItemLabel;
	static CButtonContainer s_ApplyButton, s_AddButton, s_RemoveButton;
	StatusBar.HSplitBottom(LineSize + MarginSmall, &StatusBar, &StatusScheme);
	StatusBar.HSplitTop(LineSize + MarginSmall, &ItemLabel, &StatusBar);
	StatusScheme.HSplitTop(MarginSmall, nullptr, &StatusScheme);

	if(s_TypeSelectedOld >= 0)
		Ui()->DoLabel(&ItemLabel, GameClient()->m_StatusBar.m_StatusItemTypes[s_TypeSelectedOld].m_aDesc, FontSize, TEXTALIGN_ML);

	StatusScheme.VSplitMid(&StatusButtons, &StatusScheme, MarginSmall);
	StatusScheme.VSplitMid(&Label, &StatusScheme, MarginSmall);
	StatusScheme.VSplitMid(&StatusScheme, &Button, MarginSmall);
	if(DoButton_Menu(&s_ApplyButton, TCLocalize("Apply"), 0, &Button))
	{
		GameClient()->m_StatusBar.ApplyStatusBarScheme(g_Config.m_TcStatusBarScheme);
		GameClient()->m_StatusBar.UpdateStatusBarScheme(g_Config.m_TcStatusBarScheme);
		s_SelectedItem = -1;
	}
	Ui()->DoLabel(&Label, TCLocalize("Status Scheme:"), FontSize, TEXTALIGN_MR);
	static CLineInput s_StatusScheme(g_Config.m_TcStatusBarScheme, sizeof(g_Config.m_TcStatusBarScheme));
	s_StatusScheme.SetEmptyText("");
	Ui()->DoEditBox(&s_StatusScheme, &StatusScheme, EditBoxFontSize);

	static std::vector<const char *> s_DropDownNames = {};
	for(const CStatusItem &StatusItemType : GameClient()->m_StatusBar.m_StatusItemTypes)
		if(s_DropDownNames.size() != GameClient()->m_StatusBar.m_StatusItemTypes.size())
			s_DropDownNames.push_back(StatusItemType.m_aName);

	static CUi::SDropDownState s_DropDownState;
	static CScrollRegion s_DropDownScrollRegion;
	s_DropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_DropDownScrollRegion;
	CUIRect DropDownRect;

	StatusButtons.VSplitMid(&DropDownRect, &StatusButtons, MarginSmall);
	const int TypeSelectedNew = Ui()->DoDropDown(&DropDownRect, s_TypeSelectedOld, s_DropDownNames.data(), s_DropDownNames.size(), s_DropDownState);
	if(s_TypeSelectedOld != TypeSelectedNew)
	{
		s_TypeSelectedOld = TypeSelectedNew;
		if(s_SelectedItem >= 0)
		{
			GameClient()->m_StatusBar.m_StatusBarItems[s_SelectedItem] = &GameClient()->m_StatusBar.m_StatusItemTypes[s_TypeSelectedOld];
			GameClient()->m_StatusBar.UpdateStatusBarScheme(g_Config.m_TcStatusBarScheme);
		}
	}
	CUIRect ButtonL, ButtonR;
	StatusButtons.VSplitMid(&ButtonL, &ButtonR, MarginSmall);
	size_t NumItems = GameClient()->m_StatusBar.m_StatusBarItems.size();
	if(DoButton_Menu(&s_AddButton, TCLocalize("Add Item"), 0, &ButtonL) && s_TypeSelectedOld >= 0 && NumItems < 128)
	{
		GameClient()->m_StatusBar.m_StatusBarItems.push_back(&GameClient()->m_StatusBar.m_StatusItemTypes[s_TypeSelectedOld]);
		GameClient()->m_StatusBar.UpdateStatusBarScheme(g_Config.m_TcStatusBarScheme);
		s_SelectedItem = (int)GameClient()->m_StatusBar.m_StatusBarItems.size() - 1;
	}
	if(DoButton_Menu(&s_RemoveButton, TCLocalize("Remove Item"), 0, &ButtonR) && s_SelectedItem >= 0)
	{
		GameClient()->m_StatusBar.m_StatusBarItems.erase(GameClient()->m_StatusBar.m_StatusBarItems.begin() + s_SelectedItem);
		GameClient()->m_StatusBar.UpdateStatusBarScheme(g_Config.m_TcStatusBarScheme);
		s_SelectedItem = -1;
	}

	// color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcStatusBarColor)).WithAlpha(0.5f)
	StatusBar.Draw(ColorRGBA(0, 0, 0, 0.5f), IGraphics::CORNER_ALL, 5.0f);
	int ItemCount = GameClient()->m_StatusBar.m_StatusBarItems.size();
	float AvailableWidth = StatusBar.w;
	// AvailableWidth -= (ItemCount - 1) * MarginSmall;
	AvailableWidth -= MarginSmall;
	StatusBar.VSplitLeft(MarginExtraSmall, nullptr, &StatusBar);
	float ItemWidth = AvailableWidth / (float)ItemCount;
	CUIRect StatusItemButton;
	static std::vector<CButtonContainer *> s_pItemButtons;
	static std::vector<CButtonContainer> s_ItemButtons;
	static vec2 s_ActivePos = vec2(0.0f, 0.0f);
	class CSwapItem
	{
	public:
		vec2 m_InitialPosition = vec2(0.0f, 0.0f);
		float m_Duration = 0.0f;
	};

	static std::vector<CSwapItem> s_ItemSwaps;

	if((int)s_ItemButtons.size() != ItemCount)
	{
		s_ItemSwaps.resize(ItemCount);
		s_pItemButtons.resize(ItemCount);
		s_ItemButtons.resize(ItemCount);
		for(int i = 0; i < ItemCount; ++i)
		{
			s_pItemButtons[i] = &s_ItemButtons[i];
		}
	}
	bool StatusItemActive = false;
	int HotStatusIndex = 0;
	for(int i = 0; i < ItemCount; ++i)
	{
		if(Ui()->ActiveItem() == s_pItemButtons[i])
		{
			StatusItemActive = true;
			HotStatusIndex = i;
		}
	}

	for(int i = 0; i < ItemCount; ++i)
	{
		// if(i > 0)
		//	StatusBar.VSplitLeft(MarginSmall, nullptr, &StatusBar);
		StatusBar.VSplitLeft(ItemWidth, &StatusItemButton, &StatusBar);
		StatusItemButton.HMargin(MarginSmall, &StatusItemButton);
		StatusItemButton.VMargin(MarginExtraSmall, &StatusItemButton);
		CStatusItem *StatusItem = GameClient()->m_StatusBar.m_StatusBarItems[i];
		ColorRGBA Col = ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f);
		if(s_SelectedItem == i)
			Col = ColorRGBA(1.0f, 0.35f, 0.35f, 0.75f);
		CUIRect TempItemButton = StatusItemButton;
		TempItemButton.y = 0, TempItemButton.h = 10000.0f;
		if(StatusItemActive && Ui()->ActiveItem() != s_pItemButtons[i] && Ui()->MouseInside(&TempItemButton))
		{
			std::swap(s_pItemButtons[i], s_pItemButtons[HotStatusIndex]);
			std::swap(GameClient()->m_StatusBar.m_StatusBarItems[i], GameClient()->m_StatusBar.m_StatusBarItems[HotStatusIndex]);
			s_SelectedItem = -2;
			s_ItemSwaps[HotStatusIndex].m_InitialPosition = vec2(StatusItemButton.x, StatusItemButton.y);
			s_ItemSwaps[HotStatusIndex].m_Duration = 0.15f;
			s_ItemSwaps[i].m_InitialPosition = vec2(s_ActivePos.x, s_ActivePos.y);
			s_ItemSwaps[i].m_Duration = 0.15f;
			GameClient()->m_StatusBar.UpdateStatusBarScheme(g_Config.m_TcStatusBarScheme);
		}
		TempItemButton = StatusItemButton;
		s_ItemSwaps[i].m_Duration = std::max(0.0f, s_ItemSwaps[i].m_Duration - Client()->RenderFrameTime());
		if(s_ItemSwaps[i].m_Duration > 0.0f)
		{
			float Progress = std::pow(2.0, -5.0 * (1.0 - s_ItemSwaps[i].m_Duration / 0.15f));
			TempItemButton.x = mix(TempItemButton.x, s_ItemSwaps[i].m_InitialPosition.x, Progress);
		}
		if(DoButtonLineSize_Menu(s_pItemButtons[i], StatusItem->m_aDisplayName, 0, &TempItemButton, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, Col))
		{
			if(s_SelectedItem == -2)
				s_SelectedItem++;
			else if(s_SelectedItem != i)
			{
				s_SelectedItem = i;
				for(int t = 0; t < (int)GameClient()->m_StatusBar.m_StatusItemTypes.size(); ++t)
					if(str_comp(GameClient()->m_StatusBar.m_StatusItemTypes[t].m_aName, StatusItem->m_aName) == 0)
						s_TypeSelectedOld = t;
			}
			else
			{
				s_SelectedItem = -1;
				s_TypeSelectedOld = -1;
			}
		}
		if(Ui()->ActiveItem() == s_pItemButtons[i])
			s_ActivePos = vec2(StatusItemButton.x, StatusItemButton.y);
	}
	if(!StatusItemActive)
		s_SelectedItem = std::max(-1, s_SelectedItem);
}

void CMenus::RenderSettingsTClientInfo(CUIRect MainView)
{
	CUIRect LeftView, RightView, Button, Label, LowerLeftView;
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);
	LeftView.HSplitMid(&LeftView, &LowerLeftView, 0.0f);

	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("TClient Links"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	static CButtonContainer s_DiscordButton, s_WebsiteButton, s_GithubButton, s_SupportButton;
	CUIRect ButtonLeft, ButtonRight;

	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&ButtonLeft, &ButtonRight, MarginSmall);
	if(DoButtonLineSize_Menu(&s_DiscordButton, TCLocalize("Discord"), 0, &ButtonLeft, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://discord.gg/fBvhH93Bt6");
	if(DoButtonLineSize_Menu(&s_WebsiteButton, TCLocalize("Website"), 0, &ButtonRight, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://tclient.app/");

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&ButtonLeft, &ButtonRight, MarginSmall);

	if(DoButtonLineSize_Menu(&s_GithubButton, TCLocalize("Github"), 0, &ButtonLeft, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://github.com/sjrc6/TaterClient-ddnet");
	if(DoButtonLineSize_Menu(&s_SupportButton, TCLocalize("Support ♥"), 0, &ButtonRight, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://ko-fi.com/Totar");

	LeftView = LowerLeftView;
	LeftView.HSplitBottom(LineSize * 4.0f + MarginSmall * 2.0f + HeadlineFontSize, nullptr, &LeftView);
	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("Config Files"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	char aBuf[128 + IO_MAX_PATH_LENGTH];
	CUIRect TClientConfig, ProfilesFile, WarlistFile, ChatbindsFile;

	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&TClientConfig, &ProfilesFile, MarginSmall);

	static CButtonContainer s_Config, s_Profiles, s_Warlist, s_Chatbinds;
	if(DoButtonLineSize_Menu(&s_Config, TCLocalize("TClient Settings"), 0, &TClientConfig, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::TCLIENT].m_aConfigPath, aBuf, sizeof(aBuf));
		Client()->ViewFile(aBuf);
	}
	if(DoButtonLineSize_Menu(&s_Profiles, TCLocalize("Profiles"), 0, &ProfilesFile, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::TCLIENTPROFILES].m_aConfigPath, aBuf, sizeof(aBuf));
		Client()->ViewFile(aBuf);
	}
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&WarlistFile, &ChatbindsFile, MarginSmall);

	if(DoButtonLineSize_Menu(&s_Warlist, TCLocalize("War List"), 0, &WarlistFile, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::TCLIENTWARLIST].m_aConfigPath, aBuf, sizeof(aBuf));
		Client()->ViewFile(aBuf);
	}
	if(DoButtonLineSize_Menu(&s_Chatbinds, TCLocalize("Chat Binds"), 0, &ChatbindsFile, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::TCLIENTCHATBINDS].m_aConfigPath, aBuf, sizeof(aBuf));
		Client()->ViewFile(aBuf);
	}

	// =======RIGHT VIEW========

	RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("TClient Developers"), HeadlineFontSize, TEXTALIGN_ML);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);

	const float TeeSize = 50.0f;
	const float CardSize = TeeSize + MarginSmall;
	CUIRect TeeRect, DevCardRect;
	static CButtonContainer s_LinkButton1, s_LinkButton2, s_LinkButton3, s_LinkButton4, s_LinkButton5;
	{
		RightView.HSplitTop(CardSize, &DevCardRect, &RightView);
		DevCardRect.VSplitLeft(CardSize, &TeeRect, &Label);
		Label.VSplitLeft(TextRender()->TextWidth(LineSize, "Tater"), &Label, &Button);
		Button.VSplitLeft(MarginSmall, nullptr, &Button);
		Button.w = LineSize, Button.h = LineSize, Button.y = Label.y + (Label.h / 2.0f - Button.h / 2.0f);
		Ui()->DoLabel(&Label, "Tater", LineSize, TEXTALIGN_ML);
		if(Ui()->DoButton_FontIcon(&s_LinkButton1, FontIcon::ARROW_UP_RIGHT_FROM_SQUARE, 0, &Button, IGraphics::CORNER_ALL))
			Client()->ViewLink("https://github.com/sjrc6");
		RenderDevSkin(TeeRect.Center(), 50.0f, "glow_mermyfox", "mermyfox", true, 0, 0, 0, false, true, ColorRGBA(0.92f, 0.29f, 0.48f, 1.0f), ColorRGBA(0.55f, 0.64f, 0.76f, 1.0f));
	}
	{
		RightView.HSplitTop(CardSize, &DevCardRect, &RightView);
		DevCardRect.VSplitLeft(CardSize, &TeeRect, &Label);
		Label.VSplitLeft(TextRender()->TextWidth(LineSize, "SollyBunny / bun bun"), &Label, &Button);
		Button.VSplitLeft(MarginSmall, nullptr, &Button);
		Button.w = LineSize, Button.h = LineSize, Button.y = Label.y + (Label.h / 2.0f - Button.h / 2.0f);
		Ui()->DoLabel(&Label, "SollyBunny / bun bun", LineSize, TEXTALIGN_ML);
		if(Ui()->DoButton_FontIcon(&s_LinkButton3, FontIcon::ARROW_UP_RIGHT_FROM_SQUARE, 0, &Button, IGraphics::CORNER_ALL))
			Client()->ViewLink("https://github.com/SollyBunny");
		RenderDevSkin(TeeRect.Center(), 50.0f, "tuzi", "tuzi", false, 0, 0, 2, true, true, true);
	}
	{
		RightView.HSplitTop(CardSize, &DevCardRect, &RightView);
		DevCardRect.VSplitLeft(CardSize, &TeeRect, &Label);
		Label.VSplitLeft(TextRender()->TextWidth(LineSize, "PeBox"), &Label, &Button);
		Button.VSplitLeft(MarginSmall, nullptr, &Button);
		Button.w = LineSize, Button.h = LineSize, Button.y = Label.y + (Label.h / 2.0f - Button.h / 2.0f);
		Ui()->DoLabel(&Label, "PeBox", LineSize, TEXTALIGN_ML);
		if(Ui()->DoButton_FontIcon(&s_LinkButton2, FontIcon::ARROW_UP_RIGHT_FROM_SQUARE, 0, &Button, IGraphics::CORNER_ALL))
			Client()->ViewLink("https://github.com/danielkempf");
		RenderDevSkin(TeeRect.Center(), 50.0f, "greyfox", "greyfox", true, 0, 0, 2, false, true, ColorRGBA(0.00f, 0.09f, 1.00f, 1.00f), ColorRGBA(1.00f, 0.92f, 0.00f, 1.00f));
	}
	{
		RightView.HSplitTop(CardSize, &DevCardRect, &RightView);
		DevCardRect.VSplitLeft(CardSize, &TeeRect, &Label);
		Label.VSplitLeft(TextRender()->TextWidth(LineSize, "Teero"), &Label, &Button);
		Button.VSplitLeft(MarginSmall, nullptr, &Button);
		Button.w = LineSize, Button.h = LineSize, Button.y = Label.y + (Label.h / 2.0f - Button.h / 2.0f);
		Ui()->DoLabel(&Label, "Teero", LineSize, TEXTALIGN_ML);
		if(Ui()->DoButton_FontIcon(&s_LinkButton4, FontIcon::ARROW_UP_RIGHT_FROM_SQUARE, 0, &Button, IGraphics::CORNER_ALL))
			Client()->ViewLink("https://github.com/Teero888");
		RenderDevSkin(TeeRect.Center(), 50.0f, "glow_mermyfox", "mermyfox", true, 0, 0, 0, false, true, ColorRGBA(1.00f, 1.00f, 1.00f, 1.00f), ColorRGBA(1.00f, 0.02f, 0.13f, 1.00f));
	}
	{
		RightView.HSplitTop(CardSize, &DevCardRect, &RightView);
		DevCardRect.VSplitLeft(CardSize, &TeeRect, &Label);
		Label.VSplitLeft(TextRender()->TextWidth(LineSize, "ChillerDragon"), &Label, &Button);
		Button.VSplitLeft(MarginSmall, nullptr, &Button);
		Button.w = LineSize, Button.h = LineSize, Button.y = Label.y + (Label.h / 2.0f - Button.h / 2.0f);
		Ui()->DoLabel(&Label, "ChillerDragon", LineSize, TEXTALIGN_ML);
		if(Ui()->DoButton_FontIcon(&s_LinkButton5, FontIcon::ARROW_UP_RIGHT_FROM_SQUARE, 0, &Button, IGraphics::CORNER_ALL))
			Client()->ViewLink("https://github.com/ChillerDragon");
		RenderDevSkin(TeeRect.Center(), 50.0f, "glow_greensward", "greensward", false, 0, 0, 0, false, true, ColorRGBA(1.00f, 1.00f, 1.00f, 1.00f), ColorRGBA(1.00f, 0.02f, 0.13f, 1.00f));
	}

	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("Hide Settings Tabs"), HeadlineFontSize, TEXTALIGN_ML);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	CUIRect LeftSettings, RightSettings;

	RightView.VSplitMid(&LeftSettings, &RightSettings, MarginSmall);
	RightView.HSplitTop(LineSize * 3.5f, nullptr, &RightView);

	const char *apTabNames[] = {
		TCLocalize("Settings"),
		TCLocalize("Bind Wheel"),
		TCLocalize("War List"),
		TCLocalize("Chat Binds"),
		TCLocalize("Status Bar"),
		TCLocalize("Info")};
	static int s_aShowTabs[NUMBER_OF_TCLIENT_TABS] = {};
	for(int i = 0; i < NUMBER_OF_TCLIENT_TABS - 1; ++i)
	{
		DoButton_CheckBoxAutoVMarginAndSet(&s_aShowTabs[i], apTabNames[i], &s_aShowTabs[i], i % 2 == 0 ? &LeftSettings : &RightSettings, LineSize);
		SetFlag(g_Config.m_TcTClientSettingsTabs, i, s_aShowTabs[i]);
	}

	// RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	// Ui()->DoLabel(&Label, TCLocalize("Integration"), HeadlineFontSize, TEXTALIGN_ML);
	// RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	// DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AmfDiscordRPC, TCLocalize("Enable Discord Integration"), &g_Config.m_AmfDiscordRPC, &RightView, LineSize);
}

void CMenus::RenderSettingsTClientProfiles(CUIRect MainView)
{
	int *pCurrentUseCustomColor = m_Dummy ? &g_Config.m_ClDummyUseCustomColor : &g_Config.m_ClPlayerUseCustomColor;

	const char *pCurrentSkinName = m_Dummy ? g_Config.m_ClDummySkin : g_Config.m_ClPlayerSkin;
	const unsigned CurrentColorBody = *pCurrentUseCustomColor == 1 ? (m_Dummy ? g_Config.m_ClDummyColorBody : g_Config.m_ClPlayerColorBody) : -1;
	const unsigned CurrentColorFeet = *pCurrentUseCustomColor == 1 ? (m_Dummy ? g_Config.m_ClDummyColorFeet : g_Config.m_ClPlayerColorFeet) : -1;
	const int CurrentFlag = m_Dummy ? g_Config.m_ClDummyCountry : g_Config.m_PlayerCountry;
	const int Emote = m_Dummy ? g_Config.m_ClDummyDefaultEyes : g_Config.m_ClPlayerDefaultEyes;
	const char *pCurrentName = m_Dummy ? g_Config.m_ClDummyName : g_Config.m_PlayerName;
	const char *pCurrentClan = m_Dummy ? g_Config.m_ClDummyClan : g_Config.m_PlayerClan;

	const CProfile CurrentProfile(
		CurrentColorBody,
		CurrentColorFeet,
		CurrentFlag,
		Emote,
		pCurrentSkinName,
		pCurrentName,
		pCurrentClan);

	static int s_SelectedProfile = -1;

	CUIRect Label, Button;

	auto RenderProfile = [&](CUIRect Rect, const CProfile &Profile, bool Main) {
		auto RenderCross = [&](CUIRect Cross, float MaxSize = 0.0f) {
			float MaxExtent = std::max(Cross.w, Cross.h);
			if(MaxSize > 0.0f && MaxExtent > MaxSize)
				MaxExtent = MaxSize;
			TextRender()->TextColor(ColorRGBA(1.0f, 0.0f, 0.0f));
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			const auto TextBoundingBox = TextRender()->TextBoundingBox(MaxExtent * 0.8f, FontIcon::XMARK);
			TextRender()->Text(Cross.x + (Cross.w - TextBoundingBox.m_W) / 2.0f, Cross.y + (Cross.h - TextBoundingBox.m_H) / 2.0f, MaxExtent * 0.8f, FontIcon::XMARK);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		};
		{
			CUIRect Skin;
			Rect.VSplitLeft(50.0f, &Skin, &Rect);
			if(!Main && Profile.m_SkinName[0] == '\0')
			{
				RenderCross(Skin, 20.0f);
			}
			else
			{
				CTeeRenderInfo TeeRenderInfo;
				TeeRenderInfo.Apply(GameClient()->m_Skins.Find(Profile.m_SkinName));
				TeeRenderInfo.ApplyColors(Profile.m_BodyColor >= 0 && Profile.m_FeetColor > 0, Profile.m_BodyColor, Profile.m_FeetColor);
				TeeRenderInfo.m_Size = 50.0f;
				const vec2 Pos = Skin.Center() + vec2(0.0f, TeeRenderInfo.m_Size / 10.0f); // Prevent overflow from hats
				vec2 Dir = vec2(1.0f, 0.0f);
				if(Main)
					RenderTeeCute(CAnimState::GetIdle(), &TeeRenderInfo, std::max(0, Profile.m_Emote), Dir, Pos, false);
				else
					RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeRenderInfo, std::max(0, Profile.m_Emote), Dir, Pos);
			}
		}
		Rect.VSplitLeft(5.0f, nullptr, &Rect);
		{
			CUIRect Colors;
			Rect.VSplitLeft(10.0f, &Colors, &Rect);
			CUIRect BodyColor{Colors.Center().x - 5.0f, Colors.Center().y - 11.0f, 10.0f, 10.0f};
			CUIRect FeetColor{Colors.Center().x - 5.0f, Colors.Center().y + 1.0f, 10.0f, 10.0f};
			if(Profile.m_BodyColor >= 0 && Profile.m_FeetColor > 0)
			{
				// Body Color
				Graphics()->DrawRect(BodyColor.x, BodyColor.y, BodyColor.w, BodyColor.h,
					color_cast<ColorRGBA>(ColorHSLA(Profile.m_BodyColor).UnclampLighting(ColorHSLA::DARKEST_LGT)).WithAlpha(1.0f),
					IGraphics::CORNER_ALL, 2.0f);
				// Feet Color;
				Graphics()->DrawRect(FeetColor.x, FeetColor.y, FeetColor.w, FeetColor.h,
					color_cast<ColorRGBA>(ColorHSLA(Profile.m_FeetColor).UnclampLighting(ColorHSLA::DARKEST_LGT)).WithAlpha(1.0f),
					IGraphics::CORNER_ALL, 2.0f);
			}
			else
			{
				RenderCross(BodyColor);
				RenderCross(FeetColor);
			}
		}
		Rect.VSplitLeft(5.0f, nullptr, &Rect);
		{
			CUIRect Flag;
			Rect.VSplitRight(50.0f, &Rect, &Flag);
			Flag = {Flag.x, Flag.y + (Flag.h - 25.0f) / 2.0f, Flag.w, 25.0f};
			if(Profile.m_CountryFlag == -2)
				RenderCross(Flag, 20.0f);
			else
				GameClient()->m_CountryFlags.Render(Profile.m_CountryFlag, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), Flag.x, Flag.y, Flag.w, Flag.h);
		}
		Rect.VSplitRight(5.0f, &Rect, nullptr);
		{
			const float Height = Rect.h / 3.0f;
			if(Main)
			{
				char aBuf[256];
				Rect.HSplitTop(Height, &Label, &Rect);
				str_format(aBuf, sizeof(aBuf), TCLocalize("Name: %s"), Profile.m_Name);
				Ui()->DoLabel(&Label, aBuf, Height / LineSize * FontSize, TEXTALIGN_ML);
				Rect.HSplitTop(Height, &Label, &Rect);
				str_format(aBuf, sizeof(aBuf), TCLocalize("Clan: %s"), Profile.m_Clan);
				Ui()->DoLabel(&Label, aBuf, Height / LineSize * FontSize, TEXTALIGN_ML);
				Rect.HSplitTop(Height, &Label, &Rect);
				str_format(aBuf, sizeof(aBuf), TCLocalize("Skin: %s"), Profile.m_SkinName);
				Ui()->DoLabel(&Label, aBuf, Height / LineSize * FontSize, TEXTALIGN_ML);
			}
			else
			{
				Rect.HSplitTop(Height, &Label, &Rect);
				Ui()->DoLabel(&Label, Profile.m_Name, Height / LineSize * FontSize, TEXTALIGN_ML);
				Rect.HSplitTop(Height, &Label, &Rect);
				Ui()->DoLabel(&Label, Profile.m_Clan, Height / LineSize * FontSize, TEXTALIGN_ML);
			}
		}
	};

	{
		CUIRect Top;
		MainView.HSplitTop(160.0f, &Top, &MainView);
		CUIRect Profiles, Settings, Actions;
		Top.VSplitLeft(300.0f, &Profiles, &Top);
		{
			CUIRect Skin;
			Profiles.HSplitTop(LineSize, &Label, &Profiles);
			Ui()->DoLabel(&Label, TCLocalize("Your profile"), FontSize, TEXTALIGN_ML);
			Profiles.HSplitTop(MarginSmall, nullptr, &Profiles);
			Profiles.HSplitTop(50.0f, &Skin, &Profiles);
			RenderProfile(Skin, CurrentProfile, true);

			// After load
			if(s_SelectedProfile != -1 && s_SelectedProfile < (int)GameClient()->m_SkinProfiles.m_Profiles.size())
			{
				Profiles.HSplitTop(MarginSmall, nullptr, &Profiles);
				Profiles.HSplitTop(LineSize, &Label, &Profiles);
				Ui()->DoLabel(&Label, TCLocalize("After Load"), FontSize, TEXTALIGN_ML);
				Profiles.HSplitTop(MarginSmall, nullptr, &Profiles);
				Profiles.HSplitTop(50.0f, &Skin, &Profiles);

				CProfile LoadProfile = CurrentProfile;
				const CProfile &Profile = GameClient()->m_SkinProfiles.m_Profiles[s_SelectedProfile];
				if(g_Config.m_TcProfileSkin && strlen(Profile.m_SkinName) != 0)
					str_copy(LoadProfile.m_SkinName, Profile.m_SkinName);
				if(g_Config.m_TcProfileColors && Profile.m_BodyColor != -1 && Profile.m_FeetColor != -1)
				{
					LoadProfile.m_BodyColor = Profile.m_BodyColor;
					LoadProfile.m_FeetColor = Profile.m_FeetColor;
				}
				if(g_Config.m_TcProfileEmote && Profile.m_Emote != -1)
					LoadProfile.m_Emote = Profile.m_Emote;
				if(g_Config.m_TcProfileName && strlen(Profile.m_Name) != 0)
					str_copy(LoadProfile.m_Name, Profile.m_Name);
				if(g_Config.m_TcProfileClan && (strlen(Profile.m_Clan) != 0 || g_Config.m_TcProfileOverwriteClanWithEmpty))
					str_copy(LoadProfile.m_Clan, Profile.m_Clan);
				if(g_Config.m_TcProfileFlag && Profile.m_CountryFlag != -2)
					LoadProfile.m_CountryFlag = Profile.m_CountryFlag;

				RenderProfile(Skin, LoadProfile, true);
			}
		}
		Top.VSplitLeft(20.0f, nullptr, &Top);
		Top.VSplitMid(&Settings, &Actions, 20.0f);
		{
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileSkin, TCLocalize("Save/Load Skin"), &g_Config.m_TcProfileSkin, &Settings, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileColors, TCLocalize("Save/Load Colors"), &g_Config.m_TcProfileColors, &Settings, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileEmote, TCLocalize("Save/Load Emote"), &g_Config.m_TcProfileEmote, &Settings, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileName, TCLocalize("Save/Load Name"), &g_Config.m_TcProfileName, &Settings, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileClan, TCLocalize("Save/Load Clan"), &g_Config.m_TcProfileClan, &Settings, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileFlag, TCLocalize("Save/Load Flag"), &g_Config.m_TcProfileFlag, &Settings, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileAssetsTiles, TCLocalize("Save/Load Entities"), &g_Config.m_TcProfileAssetsTiles, &Settings, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileAssetsGunpacks, TCLocalize("Save/Load Gunpacks"), &g_Config.m_TcProfileAssetsGunpacks, &Settings, LineSize);
		}
		{
			Actions.HSplitTop(30.0f, &Button, &Actions);
			static CButtonContainer s_LoadButton;
			if(DoButton_Menu(&s_LoadButton, TCLocalize("Load"), 0, &Button))
			{
				if(s_SelectedProfile != -1 && s_SelectedProfile < (int)GameClient()->m_SkinProfiles.m_Profiles.size())
				{
					CProfile LoadProfile = GameClient()->m_SkinProfiles.m_Profiles[s_SelectedProfile];
					GameClient()->m_SkinProfiles.ApplyProfile(m_Dummy, LoadProfile);
				}
			}
			Actions.HSplitTop(5.0f, nullptr, &Actions);

			Actions.HSplitTop(30.0f, &Button, &Actions);
			static CButtonContainer s_SaveButton;
			if(DoButton_Menu(&s_SaveButton, TCLocalize("Save"), 0, &Button))
			{
				GameClient()->m_SkinProfiles.AddProfile(
					g_Config.m_TcProfileColors ? CurrentColorBody : -1,
					g_Config.m_TcProfileColors ? CurrentColorFeet : -1,
					g_Config.m_TcProfileFlag ? CurrentFlag : -2,
					g_Config.m_TcProfileEmote ? Emote : -1,
					g_Config.m_TcProfileSkin ? pCurrentSkinName : "",
					g_Config.m_TcProfileName ? pCurrentName : "",
					g_Config.m_TcProfileClan ? pCurrentClan : "",
					g_Config.m_TcProfileAssetsTiles ? g_Config.m_ClAssetsEntities : "",
					g_Config.m_TcProfileAssetsGunpacks ? g_Config.m_ClAssetGame : "",
					g_Config.m_TcProfileAssetsGunpacks ? g_Config.m_ClAssetParticles : "",
					g_Config.m_TcProfileAssetsGunpacks ? g_Config.m_ClAssetHud : "",
					g_Config.m_TcProfileAssetsGunpacks ? g_Config.m_ClAssetExtras : "",
					g_Config.m_TcProfileAssetsGunpacks ? g_Config.m_ClAssetCursor : "",
					g_Config.m_TcProfileAssetsGunpacks ? g_Config.m_ClAssetArrow : "");
			}
			Actions.HSplitTop(5.0f, nullptr, &Actions);

			static int s_AllowDelete;
			DoButton_CheckBoxAutoVMarginAndSet(&s_AllowDelete, Localizable("Enable Deleting"), &s_AllowDelete, &Actions, LineSize);
			Actions.HSplitTop(5.0f, nullptr, &Actions);

			if(s_AllowDelete)
			{
				Actions.HSplitTop(30.0f, &Button, &Actions);
				static CButtonContainer s_DeleteButton;
				if(DoButton_Menu(&s_DeleteButton, TCLocalize("Delete"), 0, &Button))
					if(s_SelectedProfile != -1 && s_SelectedProfile < (int)GameClient()->m_SkinProfiles.m_Profiles.size())
						GameClient()->m_SkinProfiles.m_Profiles.erase(GameClient()->m_SkinProfiles.m_Profiles.begin() + s_SelectedProfile);
				Actions.HSplitTop(5.0f, nullptr, &Actions);

				Actions.HSplitTop(30.0f, &Button, &Actions);
				static CButtonContainer s_OverrideButton;
				if(DoButton_Menu(&s_OverrideButton, TCLocalize("Override"), 0, &Button))
				{
					if(s_SelectedProfile != -1 && s_SelectedProfile < (int)GameClient()->m_SkinProfiles.m_Profiles.size())
					{
						GameClient()->m_SkinProfiles.m_Profiles[s_SelectedProfile] = CProfile(
							g_Config.m_TcProfileColors ? CurrentColorBody : -1,
							g_Config.m_TcProfileColors ? CurrentColorFeet : -1,
							g_Config.m_TcProfileFlag ? CurrentFlag : -2,
							g_Config.m_TcProfileEmote ? Emote : -1,
							g_Config.m_TcProfileSkin ? pCurrentSkinName : "",
							g_Config.m_TcProfileName ? pCurrentName : "",
							g_Config.m_TcProfileClan ? pCurrentClan : "",
							g_Config.m_TcProfileAssetsTiles ? g_Config.m_ClAssetsEntities : "",
							g_Config.m_TcProfileAssetsGunpacks ? g_Config.m_ClAssetGame : "",
							g_Config.m_TcProfileAssetsGunpacks ? g_Config.m_ClAssetParticles : "",
							g_Config.m_TcProfileAssetsGunpacks ? g_Config.m_ClAssetHud : "",
							g_Config.m_TcProfileAssetsGunpacks ? g_Config.m_ClAssetExtras : "",
							g_Config.m_TcProfileAssetsGunpacks ? g_Config.m_ClAssetCursor : "",
							g_Config.m_TcProfileAssetsGunpacks ? g_Config.m_ClAssetArrow : "");
					}
				}
			}
		}
	}
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	{
		CUIRect Options;
		MainView.HSplitTop(LineSize, &Options, &MainView);

		Options.VSplitLeft(150.0f, &Button, &Options);
		if(DoButton_CheckBox(&m_Dummy, TCLocalize("Dummy"), m_Dummy, &Button))
			m_Dummy = 1 - m_Dummy;

		Options.VSplitLeft(150.0f, &Button, &Options);
		static int s_CustomColorId = 0;
		if(DoButton_CheckBox(&s_CustomColorId, TCLocalize("Custom colors"), *pCurrentUseCustomColor, &Button))
		{
			*pCurrentUseCustomColor = *pCurrentUseCustomColor ? 0 : 1;
			SetNeedSendInfo();
		}

		Button = Options;
		if(DoButton_CheckBox(&g_Config.m_TcProfileOverwriteClanWithEmpty, TCLocalize("Overwrite clan even if empty"), g_Config.m_TcProfileOverwriteClanWithEmpty, &Button))
			g_Config.m_TcProfileOverwriteClanWithEmpty = 1 - g_Config.m_TcProfileOverwriteClanWithEmpty;
	}
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	{
		CUIRect SelectorRect;
		MainView.HSplitBottom(LineSize + MarginSmall, &MainView, &SelectorRect);
		SelectorRect.HSplitTop(MarginSmall, nullptr, &SelectorRect);

		static CButtonContainer s_ProfilesFile;
		SelectorRect.VSplitLeft(130.0f, &Button, &SelectorRect);
		if(DoButton_Menu(&s_ProfilesFile, TCLocalize("Profiles file"), 0, &Button))
		{
			char aBuf[IO_MAX_PATH_LENGTH];
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::TCLIENTPROFILES].m_aConfigPath, aBuf, sizeof(aBuf));
			Client()->ViewFile(aBuf);
		}
	}

	const std::vector<CProfile> &ProfileList = GameClient()->m_SkinProfiles.m_Profiles;
	static CListBox s_ListBox;
	s_ListBox.DoStart(50.0f, ProfileList.size(), MainView.w / 200.0f, 3, s_SelectedProfile, &MainView, true, IGraphics::CORNER_ALL, true);

	static bool s_Indexes[1024];

	for(size_t i = 0; i < ProfileList.size(); ++i)
	{
		CListboxItem Item = s_ListBox.DoNextItem(&s_Indexes[i], s_SelectedProfile >= 0 && (size_t)s_SelectedProfile == i);
		if(!Item.m_Visible)
			continue;

		RenderProfile(Item.m_Rect, ProfileList[i], false);
	}

	s_SelectedProfile = s_ListBox.DoEnd();
}

void CMenus::RenderSettingsTClientConfigs(CUIRect MainView)
{
	// hi hello, this is a relatively self contained mess, sorry if you're forking or need to modify this -Tater

	struct SIntStage
	{
		int m_Value;
	};
	struct SStrStage
	{
		std::string m_Value;
	};
	struct SColStage
	{
		unsigned m_Value;
	};
	static std::unordered_map<const SConfigVariable *, SIntStage> s_StagedInts;
	static std::unordered_map<const SConfigVariable *, SStrStage> s_StagedStrs;
	static std::unordered_map<const SConfigVariable *, SColStage> s_StagedCols;

	struct SIntState
	{
		CLineInputNumber m_Input;
		int m_LastValue = 0;
		bool m_Inited = false;
	};
	struct SStrState
	{
		CLineInputBuffered<512> m_Input;
		bool m_Inited = false;
	};
	struct SColState
	{
		unsigned m_LastValue = 0;
		unsigned m_Working = 0;
		bool m_Inited = false;
	};
	static std::unordered_map<const SConfigVariable *, SIntState> s_IntInputs;
	static std::unordered_map<const SConfigVariable *, SStrState> s_StrInputs;
	static std::unordered_map<const SConfigVariable *, SColState> s_ColInputs;

	auto ClearStagedAndCaches = [&]() {
		s_StagedInts.clear();
		s_StagedStrs.clear();
		s_StagedCols.clear();
		s_IntInputs.clear();
		s_StrInputs.clear();
		s_ColInputs.clear();
	};

	size_t ChangesCount = 0;

	CUIRect ApplyBar, TopBar, ListArea;
	MainView.VSplitRight(5.0f, &MainView, nullptr); // padding for scrollbar
	MainView.VSplitLeft(5.0f, nullptr, &MainView);
	MainView.HSplitTop(LineSize + MarginSmall, &ApplyBar, &MainView);
	MainView.HSplitTop(LineSize + MarginSmall, &TopBar, &ListArea);
	ListArea.HSplitTop(MarginSmall, nullptr, &ListArea);

	static CLineInputBuffered<128> s_SearchInput;

	ChangesCount = s_StagedInts.size() + s_StagedStrs.size() + s_StagedCols.size();
	{
		CUIRect LeftHalf, RightHalf;
		ApplyBar.VSplitMid(&LeftHalf, &RightHalf, 0.0f);
		CUIRect Row = LeftHalf;
		Row.HMargin(MarginSmall, &Row);
		Row.h = LineSize;
		Row.y = ApplyBar.y + (ApplyBar.h - LineSize) / 2.0f;

		const float BtnWidth = 120.0f;
		CUIRect ApplyBtn, ClearBtn, Counter;
		Row.VSplitLeft(BtnWidth, &ApplyBtn, &Row);
		Row.VSplitLeft(MarginSmall, nullptr, &Row);
		Row.VSplitLeft(BtnWidth, &ClearBtn, &Row);
		Row.VSplitLeft(MarginSmall, nullptr, &Counter);

		static CButtonContainer s_ApplyBtn, s_ClearBtn;
		int DisabledStyle = ChangesCount > 0 ? 0 : -1;
		const bool ApplyClicked = DoButton_Menu(&s_ApplyBtn, Localize("Apply Changes"), DisabledStyle, &ApplyBtn);
		if(ChangesCount > 0 && ApplyClicked)
		{
			for(const auto &It : s_StagedInts)
			{
				const SConfigVariable *pVar = It.first;
				char aCmd[256];
				str_format(aCmd, sizeof(aCmd), "%s %d", pVar->m_pScriptName, It.second.m_Value);
				Console()->ExecuteLine(aCmd, IConsole::CLIENT_ID_UNSPECIFIED);
			}
			for(const auto &It : s_StagedStrs)
			{
				const SConfigVariable *pVar = It.first;
				char aEsc[1024];
				aEsc[0] = '\0';
				char *pDst = aEsc;
				str_escape(&pDst, It.second.m_Value.c_str(), aEsc + sizeof(aEsc));
				char aCmd[1200];
				str_format(aCmd, sizeof(aCmd), "%s \"%s\"", pVar->m_pScriptName, aEsc);
				Console()->ExecuteLine(aCmd, IConsole::CLIENT_ID_UNSPECIFIED);
			}
			for(const auto &It : s_StagedCols)
			{
				const SConfigVariable *pVar = It.first;
				char aCmd[256];
				str_format(aCmd, sizeof(aCmd), "%s %u", pVar->m_pScriptName, It.second.m_Value);
				Console()->ExecuteLine(aCmd, IConsole::CLIENT_ID_UNSPECIFIED);
			}
			ClearStagedAndCaches();
		}
		const bool ClearClicked = DoButton_Menu(&s_ClearBtn, Localize("Clear Changes"), DisabledStyle, &ClearBtn);
		if(ChangesCount > 0 && ClearClicked)
		{
			ClearStagedAndCaches();
		}

		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), Localize("Changes: %d"), (int)ChangesCount);
		Ui()->DoLabel(&Counter, aBuf, FontSize, TEXTALIGN_ML);

		CUIRect RightRow = RightHalf;
		RightRow.h = LineSize;
		RightRow.y = ApplyBar.y + (ApplyBar.h - LineSize) / 2.0f;
		const float RightInset = 24.0f;
		RightRow.VSplitLeft(RightInset, nullptr, &RightRow);
		CUIRect TopCol1, TopCol2;
		RightRow.VSplitMid(&TopCol1, &TopCol2, 0.0f);
		if(DoButton_CheckBox(&g_Config.m_TcUiShowTClient, Localize("TClient"), g_Config.m_TcUiShowTClient, &TopCol1))
			g_Config.m_TcUiShowTClient ^= 1;
		if(DoButton_CheckBox(&g_Config.m_TcUiCompactList, Localize("Compact List"), g_Config.m_TcUiCompactList, &TopCol2))
			g_Config.m_TcUiCompactList ^= 1;
	}

	const float SearchLabelW = 60.0f;
	{
		CUIRect SearchRow = TopBar;
		SearchRow.h = LineSize;
		SearchRow.y = TopBar.y + (TopBar.h - LineSize) / 2.0f;

		CUIRect LeftHalf, RightHalf;
		SearchRow.VSplitMid(&LeftHalf, &RightHalf, 0.0f);

		CUIRect SearchLabel, SearchEdit;
		LeftHalf.VSplitLeft(SearchLabelW, &SearchLabel, &SearchEdit);
		Ui()->DoLabel(&SearchLabel, Localize("Search"), FontSize, TEXTALIGN_ML);
		Ui()->DoClearableEditBox(&s_SearchInput, &SearchEdit, EditBoxFontSize);

		CUIRect RightCol1, RightCol2;
		const float RightInset2 = 24.0f;
		RightHalf.VSplitLeft(RightInset2, nullptr, &RightHalf);
		RightHalf.VSplitMid(&RightCol1, &RightCol2, 0.0f);
		if(DoButton_CheckBox(&g_Config.m_TcUiShowDDNet, Localize("DDNet"), g_Config.m_TcUiShowDDNet, &RightCol1))
			g_Config.m_TcUiShowDDNet ^= 1;
		if(DoButton_CheckBox(&g_Config.m_TcUiOnlyModified, Localize("Only modified"), g_Config.m_TcUiOnlyModified, &RightCol2))
			g_Config.m_TcUiOnlyModified ^= 1;
	}

	const int FlagMask = CFGFLAG_CLIENT;

	struct SEntry
	{
		const SConfigVariable *m_pVar;
	};
	std::vector<SEntry> vEntries;
	vEntries.reserve(256);

	auto Collector = [](const SConfigVariable *pVar, void *pUserData) {
		auto *pVec = static_cast<std::vector<SEntry> *>(pUserData);
		pVec->push_back({pVar});
	};
	ConfigManager()->PossibleConfigVariables("", FlagMask, Collector, &vEntries);

	auto DomainEnabled = [&](ConfigDomain Domain) {
		if(Domain == ConfigDomain::DDNET)
			return g_Config.m_TcUiShowDDNet != 0;
		if(Domain == ConfigDomain::TCLIENT)
			return g_Config.m_TcUiShowTClient != 0;
		// only show DDNet and TClient domains
		return false;
	};

	const char *pSearch = s_SearchInput.GetString();

	auto IsEffectiveDefaultVar = [&](const SConfigVariable *p) -> bool {
		if(p->m_Type == SConfigVariable::VAR_INT)
		{
			const SIntConfigVariable *pInt = static_cast<const SIntConfigVariable *>(p);
			auto It = s_StagedInts.find(p);
			int Value = It != s_StagedInts.end() ? It->second.m_Value : *pInt->m_pVariable;
			return Value == pInt->m_Default;
		}
		if(p->m_Type == SConfigVariable::VAR_STRING)
		{
			const SStringConfigVariable *pStr = static_cast<const SStringConfigVariable *>(p);
			auto It = s_StagedStrs.find(p);
			const char *pValue = It != s_StagedStrs.end() ? It->second.m_Value.c_str() : pStr->m_pStr;
			return str_comp(pValue, pStr->m_pDefault) == 0;
		}
		if(p->m_Type == SConfigVariable::VAR_COLOR)
		{
			const SColorConfigVariable *pColor = static_cast<const SColorConfigVariable *>(p);
			auto It = s_StagedCols.find(p);
			unsigned Value = It != s_StagedCols.end() ? It->second.m_Value : *pColor->m_pVariable;
			return Value == pColor->m_Default;
		}
		return true;
	};

	std::vector<const SConfigVariable *> vpFiltered;
	vpFiltered.reserve(vEntries.size());
	for(const auto &E : vEntries)
	{
		const SConfigVariable *pVar = E.m_pVar;
		if(!DomainEnabled(pVar->m_ConfigDomain))
			continue;
		if(g_Config.m_TcUiOnlyModified && IsEffectiveDefaultVar(pVar))
			continue;
		if(pSearch && pSearch[0])
		{
			const char *pName = pVar->m_pScriptName ? pVar->m_pScriptName : "";
			const char *pHelp = pVar->m_pHelp ? pVar->m_pHelp : "";
			if(!str_find_nocase(pName, pSearch) && !str_find_nocase(pHelp, pSearch))
				continue;
		}
		vpFiltered.push_back(pVar);
	}

	std::sort(vpFiltered.begin(), vpFiltered.end(), [](const SConfigVariable *a, const SConfigVariable *b) {
		if(a->m_ConfigDomain != b->m_ConfigDomain)
			return a->m_ConfigDomain < b->m_ConfigDomain;
		return str_comp(a->m_pScriptName, b->m_pScriptName) < 0;
	});

	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	ScrollParams.m_ForceShowScrollbar = true;
	s_ScrollRegion.Begin(&ListArea, &ScrollParams);

	ListArea.VSplitRight(5.0f, &ListArea, nullptr);
	CUIRect Content = ListArea;

	auto DomainName = [](ConfigDomain D) {
		switch(D)
		{
		case ConfigDomain::DDNET: return "DDNet";
		case ConfigDomain::TCLIENT: return "TClient";
		default: return "Other";
		}
	};

	ConfigDomain CurrentDomain = ConfigDomain::NUM;
	for(const SConfigVariable *pVar : vpFiltered)
	{
		if(pVar->m_ConfigDomain != CurrentDomain)
		{
			CurrentDomain = pVar->m_ConfigDomain;
			CUIRect Header;
			Content.HSplitTop(HeadlineHeight, &Header, &Content);
			if(s_ScrollRegion.AddRect(Header))
				Ui()->DoLabel(&Header, DomainName(CurrentDomain), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);
		}

		CUIRect RowItem;
		const float RowHeight = g_Config.m_TcUiCompactList ? (std::max(LineSize, ColorPickerLineSize) + 5.0f) : 55.0f;
		Content.HSplitTop(RowHeight, &RowItem, &Content);
		Content.HSplitTop(MarginExtraSmall, nullptr, &Content);
		const bool Visible = s_ScrollRegion.AddRect(RowItem);
		if(!Visible)
			continue;

		const bool Modified = !IsEffectiveDefaultVar(pVar);
		const ColorRGBA BgModified = ColorRGBA(1.0f, 0.8f, 0.0f, 0.15f);
		const ColorRGBA BgNormal = ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f);
		RowItem.Draw(Modified ? BgModified : BgNormal, IGraphics::CORNER_ALL, 6.0f);

		CUIRect RowContent;
		RowItem.Margin(5.0f, &RowContent);

		CUIRect TopLine, Below;
		if(g_Config.m_TcUiCompactList)
		{
			const float UsedHeight = (pVar->m_Type == SConfigVariable::VAR_COLOR) ? ColorPickerLineSize : LineSize;
			TopLine = RowContent;
			TopLine.h = UsedHeight;
			TopLine.y = round_to_int(RowContent.y + (RowContent.h - UsedHeight) / 2.0f);
			Below = RowContent;
		}
		else
		{
			RowContent.HSplitTop(LineSize, &TopLine, &Below);
		}
		CUIRect NameLine, Right;
		TopLine.VSplitRight(320.0f, &NameLine, &Right);
		NameLine.VSplitLeft(10.0f, nullptr, &NameLine);

		Ui()->DoLabel(&NameLine, pVar->m_pScriptName, FontSize, TEXTALIGN_ML);

		CUIRect Controls, ResetRect;
		Right.VSplitRight(120.0f, &Controls, &ResetRect);
		Controls.h = LineSize;
		Controls.y = TopLine.y + (TopLine.h - LineSize) / 2.0f;
		ResetRect.h = LineSize;
		ResetRect.y = Controls.y;
		Controls.VSplitRight(MarginSmall, &Controls, nullptr);

		if(!g_Config.m_TcUiCompactList)
		{
			CUIRect Help;
			Below.HSplitTop(2.0f, nullptr, &Below);
			Help = Below;
			Help.VSplitLeft(10.0f, nullptr, &Help);
			Ui()->DoLabel(&Help, pVar->m_pHelp ? pVar->m_pHelp : "", 11.0f, TEXTALIGN_ML);
		}

		static std::unordered_map<const SConfigVariable *, CButtonContainer> s_ResetBtns;
		if(Modified && pVar->m_Type != SConfigVariable::VAR_COLOR)
		{
			CButtonContainer &ResetBtn = s_ResetBtns[pVar];
			if(DoButton_Menu(&ResetBtn, Localize("Reset"), 0, &ResetRect))
			{
				if(pVar->m_Type == SConfigVariable::VAR_INT)
				{
					const SIntConfigVariable *pInt = static_cast<const SIntConfigVariable *>(pVar);
					s_StagedInts[pVar] = {pInt->m_Default};
				}
				else if(pVar->m_Type == SConfigVariable::VAR_STRING)
				{
					const SStringConfigVariable *pStr = static_cast<const SStringConfigVariable *>(pVar);
					s_StagedStrs[pVar] = {std::string(pStr->m_pDefault)};
				}
			}
		}

		if(pVar->m_Type == SConfigVariable::VAR_INT)
		{
			const SIntConfigVariable *pInt = static_cast<const SIntConfigVariable *>(pVar);
			// treat 0 1 ints as checkboxes
			if(pInt->m_Min == 0 && pInt->m_Max == 1)
			{
				const int Effective = s_StagedInts.contains(pVar) ? s_StagedInts[pVar].m_Value : *pInt->m_pVariable;
				if(DoButton_CheckBox(pVar, "", Effective, &Controls))
				{
					const int NewVal = Effective ? 0 : 1;
					if(NewVal == *pInt->m_pVariable)
						s_StagedInts.erase(pVar);
					else
						s_StagedInts[pVar] = {NewVal};
				}
			}
			else
			{
				SIntState &State = s_IntInputs[pVar];
				const int Effective = s_StagedInts.contains(pVar) ? s_StagedInts[pVar].m_Value : *pInt->m_pVariable;
				if(!State.m_Inited)
				{
					State.m_Input.SetInteger(Effective);
					State.m_LastValue = Effective;
					State.m_Inited = true;
				}
				else if(!State.m_Input.IsActive() && State.m_LastValue != Effective)
				{
					State.m_Input.SetInteger(Effective);
					State.m_LastValue = Effective;
				}

				CUIRect InputBox, Dummy;
				Controls.VSplitLeft(60.0f, &InputBox, &Dummy);

				if(Ui()->DoEditBox(&State.m_Input, &InputBox, EditBoxFontSize))
				{
					int NewVal = State.m_Input.GetInteger();
					bool InRange = true;
					if(pInt->m_Min != pInt->m_Max)
					{
						if(NewVal < pInt->m_Min)
							InRange = false;
						if(pInt->m_Max != 0 && NewVal > pInt->m_Max)
							InRange = false;
					}
					if(InRange && NewVal != State.m_LastValue)
					{
						if(NewVal == *pInt->m_pVariable)
							s_StagedInts.erase(pVar);
						else
							s_StagedInts[pVar] = {NewVal};
						State.m_LastValue = NewVal;
					}
				}
			}
		}
		else if(pVar->m_Type == SConfigVariable::VAR_STRING)
		{
			const SStringConfigVariable *pStr = static_cast<const SStringConfigVariable *>(pVar);
			SStrState &State = s_StrInputs[pVar];
			const char *Effective = s_StagedStrs.contains(pVar) ? s_StagedStrs[pVar].m_Value.c_str() : pStr->m_pStr;
			if(!State.m_Inited)
			{
				State.m_Input.Set(Effective);
				State.m_Inited = true;
			}
			else if(!State.m_Input.IsActive())
			{
				if(str_comp(State.m_Input.GetString(), Effective) != 0)
					State.m_Input.Set(Effective);
			}

			if(Ui()->DoEditBox(&State.m_Input, &Controls, EditBoxFontSize))
			{
				const char *NewVal = State.m_Input.GetString();
				if(str_comp(NewVal, pStr->m_pStr) == 0)
					s_StagedStrs.erase(pVar);
				else
					s_StagedStrs[pVar] = {std::string(NewVal)};
			}
		}
		else if(pVar->m_Type == SConfigVariable::VAR_COLOR)
		{
			const SColorConfigVariable *pCol = static_cast<const SColorConfigVariable *>(pVar);
			CUIRect ColorRect;
			ColorRect.x = Controls.x;
			ColorRect.h = ColorPickerLineSize;
			ColorRect.y = TopLine.y + (TopLine.h - ColorPickerLineSize) / 2.0f;
			ColorRect.w = ColorPickerLineSize + 8.0f + 60.0f;
			const ColorRGBA DefaultColor = color_cast<ColorRGBA>(ColorHSLA(pCol->m_Default, true).UnclampLighting(pCol->m_DarkestLighting));
			static std::unordered_map<const SConfigVariable *, CButtonContainer> s_ColorResetIds;
			CButtonContainer &ResetId = s_ColorResetIds[pVar];

			SColState &ColState = s_ColInputs[pVar];
			unsigned Effective = s_StagedCols.contains(pVar) ? s_StagedCols[pVar].m_Value : *pCol->m_pVariable;
			if(!ColState.m_Inited)
			{
				ColState.m_Working = Effective;
				ColState.m_LastValue = Effective;
				ColState.m_Inited = true;
			}
			else
			{
				const bool EditingThis = Ui()->IsPopupOpen(&m_ColorPickerPopupContext) && m_ColorPickerPopupContext.m_pHslaColor == &ColState.m_Working;
				if(!EditingThis && ColState.m_Working != Effective)
				{
					ColState.m_Working = Effective;
					ColState.m_LastValue = Effective;
				}
			}

			DoLine_ColorPicker(&ResetId, ColorPickerLineSize, ColorPickerLabelSize, 0.0f, &ColorRect, "", &ColState.m_Working, DefaultColor, false, nullptr, pCol->m_Alpha);
			if(ColState.m_Working != Effective)
			{
				if(ColState.m_Working == *pCol->m_pVariable)
					s_StagedCols.erase(pVar);
				else
					s_StagedCols[pVar] = {ColState.m_Working};
				ColState.m_LastValue = ColState.m_Working;
			}
		}
	}

	CUIRect EndPad{Content.x, Content.y, Content.w, 5.0f};
	s_ScrollRegion.AddRect(EndPad);
	s_ScrollRegion.End();
}
