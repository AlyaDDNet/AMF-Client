/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "hud.h"
#include "hud_layout.h"

#include "binds.h"
#include "camera.h"
#include "controls.h"
#include "voting.h"

#include <base/color.h>
#include <base/log.h>
#include <base/time.h>

#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <generated/client_data.h>
#include <generated/protocol.h>

#include <game/client/animstate.h>
#include <game/client/components/scoreboard.h>
#include <game/client/gameclient.h>
#include <game/client/smooth_ui.h>
#include <game/client/prediction/entities/character.h>
#include <game/layers.h>
#include <game/localization.h>

#include <cmath>

namespace
{
struct SAmfKeyIndicatorKey
{
	int m_Key;
	const char *m_pLabel;
	float m_X;
	float m_Y;
	float m_W;
};

constexpr int AMF_KEY_INDICATOR_STYLE_DEFAULT = 0;
constexpr int AMF_KEY_INDICATOR_STYLE_MINECRAFT = 1;

void DrawAmfKeyIndicatorKey(IGraphics *pGraphics, ITextRender *pTextRender, float X, float Y, float W, float H, bool Active, float Opacity, const char *pLabel, int Style, const char *pCpsText = nullptr)
{
	const ColorRGBA Background = Active ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.92f * Opacity) : ColorRGBA(0.0f, 0.0f, 0.0f, (Style == HudLayout::STYLE_CLEAN ? 0.32f : 0.55f) * Opacity);
	if(Style != HudLayout::STYLE_MINIMAL)
		pGraphics->DrawRect(X, Y, W, H, Background, Style == HudLayout::STYLE_ROUNDED ? IGraphics::CORNER_ALL : IGraphics::CORNER_NONE, Style == HudLayout::STYLE_ROUNDED ? H * 0.18f : 0.0f);
	const ColorRGBA TextColor = Active ? ColorRGBA(0.0f, 0.0f, 0.0f, Opacity) : ColorRGBA(1.0f, 1.0f, 1.0f, Opacity);
	if(pLabel[0] == '\0')
	{
		const float LineW = W * 0.42f;
		const float LineH = std::max(1.0f, H * 0.075f);
		pGraphics->DrawRect(X + (W - LineW) * 0.5f, Y + (H - LineH) * 0.5f, LineW, LineH, TextColor, IGraphics::CORNER_ALL, LineH * 0.5f);
		return;
	}
	pTextRender->TextColor(TextColor);
	if(pCpsText)
	{
		// CPS is part of the LMB/RMB key itself. Keeping both lines inside the
		// existing cell makes Show CPS a pure visual setting: it cannot alter the
		// module's bounds, anchor, scale or editor hitbox.
		const float LabelFontSize = H * 0.27f;
		const float CpsFontSize = H * 0.21f;
		const float LabelWidth = pTextRender->TextWidth(LabelFontSize, pLabel, -1, -1.0f);
		const float CpsWidth = pTextRender->TextWidth(CpsFontSize, pCpsText, -1, -1.0f);
		pTextRender->Text(X + (W - LabelWidth) * 0.5f, Y + H * 0.14f, LabelFontSize, pLabel);
		pTextRender->Text(X + (W - CpsWidth) * 0.5f, Y + H * 0.56f, CpsFontSize, pCpsText);
		pTextRender->TextColor(pTextRender->DefaultTextColor());
		return;
	}
	const float FontSize = H * (str_length(pLabel) > 1 ? 0.32f : 0.45f);
	const float TextWidth = pTextRender->TextWidth(FontSize, pLabel, -1, -1.0f);
	pTextRender->Text(X + (W - TextWidth) * 0.5f, Y + (H - FontSize) * 0.5f, FontSize, pLabel);
	pTextRender->TextColor(pTextRender->DefaultTextColor());
}

// Minecraft changes only the glyph face. The exact same key surfaces and
// layout are used in both styles, so changing it cannot affect geometry.
class CScopedAmfMinecraftFont
{
	ITextRender *m_pTextRender;

public:
	explicit CScopedAmfMinecraftFont(ITextRender *pTextRender) :
		m_pTextRender(pTextRender)
	{
		m_pTextRender->SetFontPreset(EFontPreset::DEFAULT_FONT);
		m_pTextRender->SetCustomFace("Minecraft");
	}

	~CScopedAmfMinecraftFont()
	{
		// TextRender keeps the selected default face globally. Restore the
		// player's configured face before the rest of the HUD is rendered.
		m_pTextRender->SetCustomFace(g_Config.m_TcCustomFont);
		m_pTextRender->SetFontPreset(EFontPreset::DEFAULT_FONT);
	}
};

void MapHudModuleScale(IGraphics *pGraphics, float HudWidth, float HudHeight, float Scale, float AnchorX, float AnchorY)
{
	// Keep the module's stored top-left anchor fixed while the existing real
	// renderer is uniformly scaled. This preserves the one renderer/data path
	// and avoids an editor-only copy of native DDNet HUDs.
	// MapScreen takes top-left and bottom-right coordinates, not a size. Passing
	// HudWidth / Scale as BottomRightX made the viewport depend on AnchorX and
	// pushed the scaled renderer away from its authoritative editor bounds.
	const float Left = AnchorX - AnchorX / Scale;
	const float Top = AnchorY - AnchorY / Scale;
	pGraphics->MapScreen(Left, Top, Left + HudWidth / Scale, Top + HudHeight / Scale);
}

int ScaledHudRectCorners(float AnchorX, float AnchorY, float Scale, float RectX, float RectY, float RectW, float RectH, float HudWidth, float HudHeight)
{
	const float FinalX = AnchorX + (RectX - AnchorX) * Scale;
	const float FinalY = AnchorY + (RectY - AnchorY) * Scale;
	return HudLayout::BackgroundCorners(IGraphics::CORNER_ALL, FinalX, FinalY, RectW * Scale, RectH * Scale, HudWidth, HudHeight);
}

// Mouse Indicator art and input mapping are intentionally kept local to this
// renderer. The atlas coordinates, presets and transient highlight timings are
// the Best Client 2.3 mouse-indicator implementation; HUD placement and state
// are owned by AMF's HudLayout below.
constexpr float AMF_MOUSE_INDICATOR_ATLAS_SCALE = 0.14f;
constexpr int AMF_MOUSE_INDICATOR_HIGHLIGHT_MS = 150;
constexpr int AMF_MOUSE_INDICATOR_PRESSED_TEXTURE_SPACE = 3;
constexpr int AMF_MOUSE_INDICATOR_ATLAS_WIDTH = 715;
constexpr int AMF_MOUSE_INDICATOR_ATLAS_HEIGHT = 353;
constexpr float AMF_MOUSE_INDICATOR_YELLOW_GREEN = 250.0f / 255.0f;
constexpr float AMF_MOUSE_INDICATOR_YELLOW_BLUE = 87.0f / 255.0f;

enum class EAmfMouseIndicatorInputKind
{
	NONE,
	MOUSE_BUTTON,
	WHEEL,
	MOUSE_MOVE,
};

struct SAmfMouseIndicatorElement
{
	EAmfMouseIndicatorInputKind m_InputKind;
	int m_MouseButton;
	int m_WheelDir;
	int m_MapX;
	int m_MapY;
	int m_MapW;
	int m_MapH;
	int m_PosX;
	int m_PosY;
	int m_MouseType;
	int m_MouseRadius;
	bool m_ActiveOnly;
};

struct SAmfMouseIndicatorPreset
{
	int m_OverlayWidth;
	int m_OverlayHeight;
	int m_PressedOffsetY;
	int m_AtlasWidth;
	int m_AtlasHeight;
	const SAmfMouseIndicatorElement *m_pElements;
	int m_NumElements;
};

template<typename T, size_t N>
constexpr int AmfMouseIndicatorArrayCount(const T (&)[N])
{
	return (int)N;
}

constexpr SAmfMouseIndicatorElement AmfMouseIndicatorStaticElement(int MapX, int MapY, int MapW, int MapH, int PosX, int PosY)
{
	return {EAmfMouseIndicatorInputKind::NONE, 0, 0, MapX, MapY, MapW, MapH, PosX, PosY, 0, 0, false};
}

constexpr SAmfMouseIndicatorElement AmfMouseIndicatorButtonElement(int MouseButton, int MapX, int MapY, int MapW, int MapH, int PosX, int PosY, bool ActiveOnly = false)
{
	return {EAmfMouseIndicatorInputKind::MOUSE_BUTTON, MouseButton, 0, MapX, MapY, MapW, MapH, PosX, PosY, 0, 0, ActiveOnly};
}

constexpr SAmfMouseIndicatorElement AmfMouseIndicatorWheelElement(int WheelDir, int MapX, int MapY, int MapW, int MapH, int PosX, int PosY, bool ActiveOnly = false)
{
	return {EAmfMouseIndicatorInputKind::WHEEL, 0, WheelDir, MapX, MapY, MapW, MapH, PosX, PosY, 0, 0, ActiveOnly};
}

constexpr SAmfMouseIndicatorElement AmfMouseIndicatorMoveElement(int MouseType, int MapX, int MapY, int MapW, int MapH, int PosX, int PosY, int MouseRadius)
{
	return {EAmfMouseIndicatorInputKind::MOUSE_MOVE, 0, 0, MapX, MapY, MapW, MapH, PosX, PosY, MouseType, MouseRadius, false};
}

const SAmfMouseIndicatorElement gs_aAmfMouseIndicatorArrowElements[] = {
	AmfMouseIndicatorStaticElement(328, 1, 283, 242, 2, 179),
	AmfMouseIndicatorStaticElement(1, 1, 139, 174, 2, 0),
	AmfMouseIndicatorStaticElement(143, 1, 139, 174, 146, 0),
	AmfMouseIndicatorStaticElement(285, 246, 48, 95, 117, 79),
	AmfMouseIndicatorStaticElement(285, 1, 40, 62, 0, 210),
	AmfMouseIndicatorStaticElement(284, 1, 41, 62, 11, 273),
	AmfMouseIndicatorMoveElement(1, 614, 1, 100, 100, 95, 238, 50),
	AmfMouseIndicatorButtonElement(1, 1, 178, 139, 174, 2, 0, true),
	AmfMouseIndicatorButtonElement(2, 143, 178, 139, 174, 146, 0, true),
	AmfMouseIndicatorButtonElement(3, 336, 246, 48, 95, 117, 79, true),
	AmfMouseIndicatorWheelElement(1, 387, 246, 48, 95, 117, 79, true),
	AmfMouseIndicatorWheelElement(2, 438, 246, 48, 95, 117, 79, true),
	AmfMouseIndicatorButtonElement(5, 285, 66, 40, 62, 0, 210, true),
	AmfMouseIndicatorButtonElement(4, 285, 66, 40, 62, 11, 273, true),
};

const SAmfMouseIndicatorElement gs_aAmfMouseIndicatorDotDotElements[] = {
	AmfMouseIndicatorStaticElement(328, 1, 283, 242, 1, 179),
	AmfMouseIndicatorStaticElement(1, 1, 139, 174, 2, 0),
	AmfMouseIndicatorStaticElement(143, 1, 139, 174, 146, 0),
	AmfMouseIndicatorStaticElement(285, 246, 48, 95, 117, 79),
	AmfMouseIndicatorStaticElement(285, 1, 40, 62, 0, 210),
	AmfMouseIndicatorStaticElement(284, 1, 41, 62, 11, 273),
	AmfMouseIndicatorStaticElement(493, 245, 100, 100, 91, 245),
	AmfMouseIndicatorMoveElement(0, 614, 207, 20, 20, 132, 284, 50),
	AmfMouseIndicatorButtonElement(1, 1, 178, 139, 174, 2, 0, true),
	AmfMouseIndicatorButtonElement(2, 143, 178, 139, 174, 146, 0, true),
	AmfMouseIndicatorButtonElement(3, 336, 246, 48, 95, 117, 79, true),
	AmfMouseIndicatorWheelElement(1, 387, 246, 48, 95, 117, 79, true),
	AmfMouseIndicatorWheelElement(2, 438, 246, 48, 95, 117, 79, true),
	AmfMouseIndicatorButtonElement(5, 285, 66, 40, 62, 0, 210, true),
	AmfMouseIndicatorButtonElement(4, 285, 66, 40, 62, 11, 273, true),
};

const SAmfMouseIndicatorElement gs_aAmfMouseIndicatorNothingElements[] = {
	AmfMouseIndicatorStaticElement(328, 1, 283, 242, 2, 179),
	AmfMouseIndicatorStaticElement(1, 1, 139, 174, 2, 0),
	AmfMouseIndicatorStaticElement(143, 1, 139, 174, 146, 0),
	AmfMouseIndicatorStaticElement(285, 246, 48, 95, 117, 79),
	AmfMouseIndicatorStaticElement(285, 1, 40, 62, 0, 210),
	AmfMouseIndicatorStaticElement(284, 1, 41, 62, 11, 273),
	AmfMouseIndicatorButtonElement(1, 1, 178, 139, 174, 2, 0, true),
	AmfMouseIndicatorButtonElement(2, 143, 178, 139, 174, 146, 0, true),
	AmfMouseIndicatorButtonElement(3, 336, 246, 48, 95, 117, 79, true),
	AmfMouseIndicatorWheelElement(1, 387, 246, 48, 95, 117, 79, true),
	AmfMouseIndicatorWheelElement(2, 438, 246, 48, 95, 117, 79, true),
	AmfMouseIndicatorButtonElement(5, 285, 66, 40, 62, 0, 210, true),
	AmfMouseIndicatorButtonElement(4, 285, 66, 40, 62, 11, 273, true),
};

const SAmfMouseIndicatorPreset gs_aAmfMouseIndicatorPresets[] = {
	{285, 421, 0, AMF_MOUSE_INDICATOR_ATLAS_WIDTH, AMF_MOUSE_INDICATOR_ATLAS_HEIGHT, gs_aAmfMouseIndicatorArrowElements, AmfMouseIndicatorArrayCount(gs_aAmfMouseIndicatorArrowElements)},
	{285, 421, 0, AMF_MOUSE_INDICATOR_ATLAS_WIDTH, AMF_MOUSE_INDICATOR_ATLAS_HEIGHT, gs_aAmfMouseIndicatorDotDotElements, AmfMouseIndicatorArrayCount(gs_aAmfMouseIndicatorDotDotElements)},
	{285, 421, 0, AMF_MOUSE_INDICATOR_ATLAS_WIDTH, AMF_MOUSE_INDICATOR_ATLAS_HEIGHT, gs_aAmfMouseIndicatorNothingElements, AmfMouseIndicatorArrayCount(gs_aAmfMouseIndicatorNothingElements)},
};

const SAmfMouseIndicatorPreset &GetAmfMouseIndicatorPreset(int Preset)
{
	return gs_aAmfMouseIndicatorPresets[std::clamp(Preset - 1, 0, AmfMouseIndicatorArrayCount(gs_aAmfMouseIndicatorPresets) - 1)];
}

float GetAmfMouseIndicatorScale(const HudLayout::SModuleLayout &Layout)
{
	return std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f) * AMF_MOUSE_INDICATOR_ATLAS_SCALE;
}

bool IsAmfMouseIndicatorButtonPressed(IInput *pInput, int MouseButton)
{
	switch(MouseButton)
	{
	case 1: return pInput->KeyIsPressed(KEY_MOUSE_1);
	case 2: return pInput->KeyIsPressed(KEY_MOUSE_2);
	case 3: return pInput->KeyIsPressed(KEY_MOUSE_3);
	case 4: return pInput->KeyIsPressed(KEY_MOUSE_4);
	case 5: return pInput->KeyIsPressed(KEY_MOUSE_5);
	case 6: return pInput->KeyIsPressed(KEY_MOUSE_6);
	case 7: return pInput->KeyIsPressed(KEY_MOUSE_7);
	case 8: return pInput->KeyIsPressed(KEY_MOUSE_8);
	case 9: return pInput->KeyIsPressed(KEY_MOUSE_9);
	default: return false;
	}
}

bool IsAmfMouseIndicatorButtonPressed(const CNetObj_PlayerInput *pInput, int MouseButton)
{
	if(pInput == nullptr)
		return false;
	switch(MouseButton)
	{
	case 1: return (pInput->m_Fire & 1) != 0;
	case 2: return pInput->m_Hook != 0;
	default: return false;
	}
}

bool IsAmfMouseIndicatorWheelActive(int WheelDir, int64_t Now, int64_t WheelUpEndTime, int64_t WheelDownEndTime)
{
	switch(WheelDir)
	{
	case 1: return WheelUpEndTime > Now;
	case 2: return WheelDownEndTime > Now;
	default: return WheelUpEndTime > Now || WheelDownEndTime > Now;
	}
}

bool IsAmfMouseIndicatorButtonPressedFromCharacter(const CNetObj_Character *pPrevCharacter, const CNetObj_Character *pCharacter, int MouseButton, int64_t Now, int64_t Mouse1EndTime)
{
	if(pCharacter == nullptr)
		return false;
	switch(MouseButton)
	{
	case 1: return Mouse1EndTime > Now || (pPrevCharacter != nullptr && pPrevCharacter->m_AttackTick != pCharacter->m_AttackTick);
	case 2: return pCharacter->m_HookState != HOOK_IDLE;
	default: return false;
	}
}

void DrawAmfMouseIndicatorSprite(IGraphics *pGraphics, IGraphics::CTextureHandle Texture, int AtlasWidth, int AtlasHeight, int MapX, int MapY, int MapW, int MapH, float X, float Y, float W, float H, ColorRGBA Color = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), float Rotation = 0.0f)
{
	if(!Texture.IsValid() || Texture.IsNullTexture() || W <= 0.0f || H <= 0.0f)
		return;
	pGraphics->TextureSet(Texture);
	pGraphics->QuadsBegin();
	pGraphics->SetColor(Color);
	pGraphics->QuadsSetSubset(MapX / (float)AtlasWidth, MapY / (float)AtlasHeight, (MapX + MapW) / (float)AtlasWidth, (MapY + MapH) / (float)AtlasHeight);
	pGraphics->QuadsSetRotation(Rotation);
	IGraphics::CQuadItem Quad(X + W * 0.5f, Y + H * 0.5f, W, H);
	pGraphics->QuadsDraw(&Quad, 1);
	pGraphics->QuadsSetRotation(0.0f);
	pGraphics->QuadsEnd();
}
} // namespace

CHud::CHud()
{
	m_FPSTextContainerIndex.Reset();
	m_DDRaceEffectsTextContainerIndex.Reset();
	m_PlayerAngleTextContainerIndex.Reset();
	m_PlayerPrevAngle = -INFINITY;

	for(int i = 0; i < 2; i++)
	{
		m_aPlayerSpeedTextContainers[i].Reset();
		m_aPlayerPrevSpeed[i] = -INFINITY;
		m_aPlayerPositionContainers[i].Reset();
		m_aPlayerPrevPosition[i] = -INFINITY;
	}
}

void CHud::ResetHudContainers()
{
	for(auto &ScoreInfo : m_aScoreInfo)
	{
		TextRender()->DeleteTextContainer(ScoreInfo.m_OptionalNameTextContainerIndex);
		TextRender()->DeleteTextContainer(ScoreInfo.m_TextRankContainerIndex);
		TextRender()->DeleteTextContainer(ScoreInfo.m_TextScoreContainerIndex);
		Graphics()->DeleteQuadContainer(ScoreInfo.m_RoundRectQuadContainerIndex);

		ScoreInfo.Reset();
	}
	m_ScoreHudLastRight = -1.0f;
	m_ScoreHudLastTop = -1.0f;
	m_aScoreHudLastCorners[0] = m_aScoreHudLastCorners[1] = -1;

	TextRender()->DeleteTextContainer(m_FPSTextContainerIndex);
	TextRender()->DeleteTextContainer(m_DDRaceEffectsTextContainerIndex);
	TextRender()->DeleteTextContainer(m_PlayerAngleTextContainerIndex);
	m_PlayerPrevAngle = -INFINITY;
	for(int i = 0; i < 2; i++)
	{
		TextRender()->DeleteTextContainer(m_aPlayerSpeedTextContainers[i]);
		m_aPlayerPrevSpeed[i] = -INFINITY;
		TextRender()->DeleteTextContainer(m_aPlayerPositionContainers[i]);
		m_aPlayerPrevPosition[i] = -INFINITY;
	}
}

void CHud::OnWindowResize()
{
	ResetHudContainers();
}

void CHud::OnReset()
{
	m_TimeCpDiff = 0.0f;
	m_DDRaceTime = 0;
	m_FinishTimeLastReceivedTick = 0;
	m_TimeCpLastReceivedTick = 0;
	m_ShowFinishTime = false;
	m_aPlayerRecord[0] = -1.0f;
	m_aPlayerRecord[1] = -1.0f;
	m_aPlayerSpeed[0] = 0;
	m_aPlayerSpeed[1] = 0;
	m_aLastPlayerSpeedChange[0] = ESpeedChange::NONE;
	m_aLastPlayerSpeedChange[1] = ESpeedChange::NONE;
	m_LastSpectatorCountTick = 0;
	m_AmfKeyIndicatorVisibility = g_Config.m_AmfKeyIndicator ? 1.0f : 0.0f;
	m_AmfMouseIndicatorMouse1EndTime = 0;
	m_AmfMouseIndicatorWheelUpEndTime = 0;
	m_AmfMouseIndicatorWheelDownEndTime = 0;

	ResetHudContainers();
}

void CHud::OnInit()
{
	OnReset();

	Graphics()->SetColor(1.0, 1.0, 1.0, 1.0);

	m_HudQuadContainerIndex = Graphics()->CreateQuadContainer(false);
	Graphics()->QuadsSetSubset(0, 0, 1, 1);
	PrepareAmmoHealthAndArmorQuads();

	// all cursors for the different weapons
	for(int i = 0; i < NUM_WEAPONS; ++i)
	{
		float ScaleX, ScaleY;
		Graphics()->GetSpriteScale(g_pData->m_Weapons.m_aId[i].m_pSpriteCursor, ScaleX, ScaleY);
		m_aCursorOffset[i] = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 64.f * ScaleX, 64.f * ScaleY);
	}

	// the flags
	m_FlagOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 8.f, 16.f);

	PreparePlayerStateQuads();
	m_AmfMouseIndicatorTexture = Graphics()->LoadTexture("amfclient/keystrokes/mouse.png", IStorage::TYPE_ALL);
	if(!m_AmfMouseIndicatorTexture.IsValid() || m_AmfMouseIndicatorTexture.IsNullTexture())
		log_warn("amf", "Failed to load AMF mouse indicator texture");
	// The donor atlas bakes its pressed highlight into the texture. The white
	// companion atlas has identical geometry/input frames with that highlight
	// recolored, so the selector below does not alter donor input semantics.
	m_AmfMouseIndicatorWhiteTexture = Graphics()->LoadTexture("amfclient/keystrokes/mouse_white.png", IStorage::TYPE_ALL);
	if(!m_AmfMouseIndicatorWhiteTexture.IsValid() || m_AmfMouseIndicatorWhiteTexture.IsNullTexture())
		log_warn("amf", "Failed to load AMF mouse indicator white pressed texture");

	Graphics()->QuadContainerUpload(m_HudQuadContainerIndex);
}

void CHud::OnConsoleInit()
{
	HudLayout::OnConsoleInit(Console(), ConfigManager());
}

void CHud::RenderGameTimer()
{
	m_GameTimerBounds = {};
	if(!HudLayout::IsEnabled(HudLayout::MODULE_GAME_TIMER))
		return;
	float Half = m_Width / 2.0f;

	if(!(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_SUDDENDEATH))
	{
		char aBuf[32];
		int Time = 0;
		if(GameClient()->m_Snap.m_pGameInfoObj->m_TimeLimit && (GameClient()->m_Snap.m_pGameInfoObj->m_WarmupTimer <= 0))
		{
			Time = GameClient()->m_Snap.m_pGameInfoObj->m_TimeLimit * 60 - ((Client()->GameTick(g_Config.m_ClDummy) - GameClient()->m_Snap.m_pGameInfoObj->m_RoundStartTick) / Client()->GameTickSpeed());

			if(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER)
				Time = 0;
		}
		else if(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_RACETIME)
		{
			// The Warmup timer is negative in this case to make sure that incompatible clients will not see a warmup timer
			Time = (Client()->GameTick(g_Config.m_ClDummy) + GameClient()->m_Snap.m_pGameInfoObj->m_WarmupTimer) / Client()->GameTickSpeed();
		}
		else
		{
			Time = (Client()->GameTick(g_Config.m_ClDummy) - GameClient()->m_Snap.m_pGameInfoObj->m_RoundStartTick) / Client()->GameTickSpeed();
		}

		str_time((int64_t)Time * 100, ETimeFormat::DAYS, aBuf, sizeof(aBuf));
		const auto Layout = HudLayout::Get(HudLayout::MODULE_GAME_TIMER, m_Width, m_Height);
		const float FontSize = 10.0f * std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
		const float TextWidthM = TextRender()->TextWidth(FontSize, "00:00", -1, -1.0f);
		const float TextWidthH = TextRender()->TextWidth(FontSize, "00:00:00", -1, -1.0f);
		const float TextWidth0D = TextRender()->TextWidth(FontSize, "0d 00:00:00", -1, -1.0f);
		const float TextWidth00D = TextRender()->TextWidth(FontSize, "00d 00:00:00", -1, -1.0f);
		const float TextWidth000D = TextRender()->TextWidth(FontSize, "000d 00:00:00", -1, -1.0f);
		const float w = Time >= 3600 * 24 * 100 ? TextWidth000D : (Time >= 3600 * 24 * 10 ? TextWidth00D : (Time >= 3600 * 24 ? TextWidth0D : (Time >= 3600 ? TextWidthH : TextWidthM)));
		// last 60 sec red, last 10 sec blink
		if(GameClient()->m_Snap.m_pGameInfoObj->m_TimeLimit && Time <= 60 && (GameClient()->m_Snap.m_pGameInfoObj->m_WarmupTimer <= 0))
		{
			float Alpha = Time <= 10 && (2 * time() / time_freq()) % 2 ? 0.5f : 1.0f;
			TextRender()->TextColor(1.0f, 0.25f, 0.25f, Alpha);
		}
		float X = Half - w / 2;
		float Y = 2.0f;
		if(HudLayout::HasRuntimeOverride(HudLayout::MODULE_GAME_TIMER))
		{
			X = Layout.m_X;
			Y = Layout.m_Y;
		}
		m_GameTimerBounds = HudLayout::ClampRectToScreen({X, Y, w, FontSize, 0.0f}, m_Width, m_Height);
		TextRender()->Text(m_GameTimerBounds.m_X, m_GameTimerBounds.m_Y, FontSize, aBuf, -1.0f);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	}
}

void CHud::RenderPauseNotification()
{
	if(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_PAUSED &&
		!(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER))
	{
		const char *pText = Localize("Game paused");
		float FontSize = 20.0f;
		float w = TextRender()->TextWidth(FontSize, pText, -1, -1.0f);
		TextRender()->Text(150.0f * Graphics()->ScreenAspect() + -w / 2.0f, 50.0f, FontSize, pText, -1.0f);
	}
}

HudLayout::SModuleRect CHud::GetAmfKeyIndicatorBounds() const
{
	const auto Layout = HudLayout::Get(HudLayout::MODULE_KEYSTROKES_KEYBOARD, m_Width, m_Height);
	const float Scale = Layout.m_Scale / 100.0f;
	const float KeySize = 21.0f * Scale;
	const float Gap = 1.12f * Scale;
	const float Width = 3.0f * KeySize + 2.0f * Gap;
	const float KeyHeight = 4.0f * KeySize + 3.0f * Gap;
	// CPS is rendered inside the existing LMB/RMB cells in both font modes, so
	// it has no effect on the authoritative editor/drag bounds.
	const float Height = KeyHeight;
	return HudLayout::ClampRectToScreen({
		Layout.m_X,
		Layout.m_Y,
		Width,
		Height,
		0.0f},
		m_Width, m_Height);
}

void CHud::RenderAmfKeyIndicator()
{
	const auto Layout = HudLayout::Get(HudLayout::MODULE_KEYSTROKES_KEYBOARD, m_Width, m_Height);
	const bool VisibleTarget = HudLayout::IsEnabled(HudLayout::MODULE_KEYSTROKES_KEYBOARD);
	if(g_Config.m_AmfSmoothHud)
	{
		// Keep the existing Smooth HUD easing, but cap one visual step so a long
		// frame while toggling the option cannot skip the show/hide transition.
		const float Duration = std::max(g_Config.m_AmfAnimDuration, 60) / 1000.0f;
		m_AmfKeyIndicatorVisibility = SmoothApproach(
			m_AmfKeyIndicatorVisibility,
			VisibleTarget ? 1.0f : 0.0f,
			std::min(Client()->RenderFrameTime(), 0.035f),
			4.6f / Duration);
	}
	else
		m_AmfKeyIndicatorVisibility = VisibleTarget ? 1.0f : 0.0f;
	if(m_AmfKeyIndicatorVisibility < 0.002f)
		m_AmfKeyIndicatorVisibility = 0.0f;
	else if(m_AmfKeyIndicatorVisibility > 0.998f)
		m_AmfKeyIndicatorVisibility = 1.0f;
	if(m_AmfKeyIndicatorVisibility <= 0.0f)
		return;

	// The indicator only reads physical input for its visual state. It never
	// feeds input back into gameplay, prediction or client commands.
	const float Scale = Layout.m_Scale / 100.0f;
	const float Opacity = g_Config.m_AmfKeyIndicatorOpacity / 100.0f * m_AmfKeyIndicatorVisibility;
	const int HudStyle = HudLayout::GetStyle(HudLayout::MODULE_KEYSTROKES_KEYBOARD);
	const int KeyIndicatorStyle = std::clamp(g_Config.m_AmfKeyIndicatorStyle, AMF_KEY_INDICATOR_STYLE_DEFAULT, AMF_KEY_INDICATOR_STYLE_MINECRAFT);
	const bool MinecraftStyle = KeyIndicatorStyle == AMF_KEY_INDICATOR_STYLE_MINECRAFT;
	const bool ShowCps = g_Config.m_AmfKeyIndicatorShowCps != 0;
	const float KeySize = 21.0f * Scale;
	const float Gap = 1.12f * Scale;
	const float Width = 3.0f * KeySize + 2.0f * Gap;
	const HudLayout::SModuleRect Bounds = GetAmfKeyIndicatorBounds();
	const float X = Bounds.m_X;
	const float Y = Bounds.m_Y;

	if(ShowCps)
	{
		const int64_t Now = time_get();
		const bool aMousePressed[] = {Input()->KeyIsPressed(KEY_MOUSE_1), Input()->KeyIsPressed(KEY_MOUSE_2)};
		for(int MouseButton = 0; MouseButton < 2; MouseButton++)
		{
			if(aMousePressed[MouseButton] && !m_aAmfKeyIndicatorMousePressed[MouseButton])
			{
				const int HistorySize = sizeof(m_aaAmfKeyIndicatorClickTimes[MouseButton]) / sizeof(m_aaAmfKeyIndicatorClickTimes[MouseButton][0]);
				m_aaAmfKeyIndicatorClickTimes[MouseButton][m_aAmfKeyIndicatorClickTimeOffsets[MouseButton]] = Now;
				m_aAmfKeyIndicatorClickTimeOffsets[MouseButton] = (m_aAmfKeyIndicatorClickTimeOffsets[MouseButton] + 1) % HistorySize;
			}
			m_aAmfKeyIndicatorMousePressed[MouseButton] = aMousePressed[MouseButton];

			m_aAmfKeyIndicatorCps[MouseButton] = 0;
			for(const int64_t ClickTime : m_aaAmfKeyIndicatorClickTimes[MouseButton])
			{
				if(ClickTime != 0 && Now - ClickTime <= time_freq())
					m_aAmfKeyIndicatorCps[MouseButton]++;
			}
		}
	}

	char aMouseCps[2][24] = {};
	if(ShowCps)
	{
		str_format(aMouseCps[0], sizeof(aMouseCps[0]), "%d CPS", m_aAmfKeyIndicatorCps[0]);
		str_format(aMouseCps[1], sizeof(aMouseCps[1]), "%d CPS", m_aAmfKeyIndicatorCps[1]);
	}

	const SAmfKeyIndicatorKey aKeys[] = {
		{KEY_W, "W", KeySize + Gap, 0.0f, KeySize},
		{KEY_A, "A", 0.0f, KeySize + Gap, KeySize},
		{KEY_S, "S", KeySize + Gap, KeySize + Gap, KeySize},
		{KEY_D, "D", 2.0f * (KeySize + Gap), KeySize + Gap, KeySize},
		{KEY_MOUSE_1, "LMB", 0.0f, 2.0f * (KeySize + Gap), (Width - Gap) * 0.5f},
		{KEY_MOUSE_2, "RMB", (Width + Gap) * 0.5f, 2.0f * (KeySize + Gap), (Width - Gap) * 0.5f},
		{KEY_SPACE, "", 0.0f, 3.0f * (KeySize + Gap), Width},
	};
	auto RenderKeys = [&]() {
		for(const auto &Key : aKeys)
		{
			const char *pCpsText = nullptr;
			if(ShowCps && Key.m_Key == KEY_MOUSE_1)
				pCpsText = aMouseCps[0];
			else if(ShowCps && Key.m_Key == KEY_MOUSE_2)
				pCpsText = aMouseCps[1];
			DrawAmfKeyIndicatorKey(Graphics(), TextRender(), X + Key.m_X, Y + Key.m_Y, Key.m_W, KeySize, Input()->KeyIsPressed(Key.m_Key), Opacity, Key.m_pLabel, HudStyle, pCpsText);
		}
	};

	if(MinecraftStyle)
	{
		// The style selection affects only the existing renderer's font face;
		// positions, backgrounds, key geometry and HUD editor bounds stay shared.
		CScopedAmfMinecraftFont MinecraftFont(TextRender());
		RenderKeys();
	}
	else
		RenderKeys();
}

HudLayout::SModuleRect CHud::GetAmfMouseIndicatorBounds() const
{
	const auto Layout = HudLayout::Get(HudLayout::MODULE_KEYSTROKES_MOUSE, m_Width, m_Height);
	const auto &Preset = GetAmfMouseIndicatorPreset(g_Config.m_AmfMouseIndicatorPreset);
	const float Scale = GetAmfMouseIndicatorScale(Layout);
	return HudLayout::ClampRectToScreen({
		Layout.m_X,
		Layout.m_Y,
		Preset.m_OverlayWidth * Scale,
		Preset.m_OverlayHeight * Scale,
		0.0f},
		m_Width, m_Height);
}

int CHud::GetAmfMouseIndicatorTrackedClientId() const
{
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
	{
		if(!GameClient()->m_Snap.m_SpecInfo.m_Active &&
			in_range(GameClient()->m_Snap.m_LocalClientId, 0, MAX_CLIENTS - 1) &&
			GameClient()->m_Snap.m_aCharacters[GameClient()->m_Snap.m_LocalClientId].m_Active)

			return GameClient()->m_Snap.m_LocalClientId;
		if(GameClient()->m_DemoSpecId > SPEC_FREEVIEW && GameClient()->m_DemoSpecId < MAX_CLIENTS)
			return GameClient()->m_DemoSpecId;
	}

	if(!GameClient()->m_Snap.m_SpecInfo.m_Active)
		return -1;
	const int SpectatorId = GameClient()->m_Snap.m_SpecInfo.m_SpectatorId;
	if(SpectatorId <= SPEC_FREEVIEW || SpectatorId >= MAX_CLIENTS)
		return -1;
	return SpectatorId;
}

const CNetObj_PlayerInput *CHud::GetAmfMouseIndicatorTrackedInput() const
{
	const int SpectatorId = GetAmfMouseIndicatorTrackedClientId();
	if(SpectatorId < 0)
		return nullptr;
	if(CCharacter *pCharacter = GameClient()->m_GameWorld.GetCharacterById(SpectatorId))
		return pCharacter->LatestInput();
	return nullptr;
}

bool CHud::GetAmfMouseIndicatorTrackedAim(int TrackedClientId, float Intra, vec2 &OutAim) const
{
	if(!in_range(TrackedClientId, 0, MAX_CLIENTS - 1))
		return false;

	const auto &Character = GameClient()->m_Snap.m_aCharacters[TrackedClientId];
	if(!Character.m_Active)
		return false;

	if(Character.m_HasExtendedDisplayInfo)
	{
		const CNetObj_DDNetCharacter *pExtendedData = &Character.m_ExtendedData;
		const CNetObj_DDNetCharacter *pPrevExtendedData = Character.m_pPrevExtendedData;
		if(pPrevExtendedData != nullptr)
		{
			OutAim = vec2(
				mix((float)pPrevExtendedData->m_TargetX, (float)pExtendedData->m_TargetX, Intra),
				mix((float)pPrevExtendedData->m_TargetY, (float)pExtendedData->m_TargetY, Intra));
		}
		else
		{
			OutAim = vec2((float)pExtendedData->m_TargetX, (float)pExtendedData->m_TargetY);
		}
		return length(OutAim) > 0.001f;
	}

	float Angle = 0.0f;
	if(Character.m_Cur.m_Angle > (256.0f * pi) && Character.m_Prev.m_Angle < 0)
		Angle = mix((float)Character.m_Prev.m_Angle, (float)(Character.m_Cur.m_Angle - 256.0f * 2 * pi), Intra) / 256.0f;
	else if(Character.m_Cur.m_Angle < 0 && Character.m_Prev.m_Angle > (256.0f * pi))
		Angle = mix((float)Character.m_Prev.m_Angle, (float)(Character.m_Cur.m_Angle + 256.0f * 2 * pi), Intra) / 256.0f;
	else
		Angle = mix((float)Character.m_Prev.m_Angle, (float)Character.m_Cur.m_Angle, Intra) / 256.0f;
	OutAim = direction(Angle) * 256.0f;
	return true;
}

void CHud::RenderAmfMouseIndicator()
{
	if(!HudLayout::IsEnabled(HudLayout::MODULE_KEYSTROKES_MOUSE))
		return;
	const bool WhitePressed = std::clamp(g_Config.m_AmfMouseIndicatorPressedColor, 0, 1) == 1;
	const IGraphics::CTextureHandle &MouseTexture = WhitePressed && m_AmfMouseIndicatorWhiteTexture.IsValid() && !m_AmfMouseIndicatorWhiteTexture.IsNullTexture() ?
		m_AmfMouseIndicatorWhiteTexture : m_AmfMouseIndicatorTexture;
	if(!MouseTexture.IsValid() || MouseTexture.IsNullTexture())
		return;

	const auto Layout = HudLayout::Get(HudLayout::MODULE_KEYSTROKES_MOUSE, m_Width, m_Height);
	const int MousePreset = std::clamp(g_Config.m_AmfMouseIndicatorPreset, 1, 3);
	const auto &Preset = GetAmfMouseIndicatorPreset(MousePreset);
	const float Scale = GetAmfMouseIndicatorScale(Layout);
	const int64_t Now = time_get();
	const int64_t HighlightDuration = time_freq() * AMF_MOUSE_INDICATOR_HIGHLIGHT_MS / 1000;
	const int TrackedClientId = GetAmfMouseIndicatorTrackedClientId();
	const bool HasTrackedPlayer = TrackedClientId >= 0;
	const CNetObj_PlayerInput *pTrackedInput = GetAmfMouseIndicatorTrackedInput();
	const CNetObj_Character *pTrackedCharacter = HasTrackedPlayer && GameClient()->m_Snap.m_aCharacters[TrackedClientId].m_Active ?
		&GameClient()->m_Snap.m_aCharacters[TrackedClientId].m_Cur : nullptr;
	const CNetObj_Character *pPrevTrackedCharacter = HasTrackedPlayer && GameClient()->m_Snap.m_aCharacters[TrackedClientId].m_Active ?
		&GameClient()->m_Snap.m_aCharacters[TrackedClientId].m_Prev : nullptr;

	if(!HasTrackedPlayer && pTrackedInput == nullptr)
	{
		if(Input()->KeyPress(KEY_MOUSE_WHEEL_UP))
			m_AmfMouseIndicatorWheelUpEndTime = Now + HighlightDuration;
		if(Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN))
			m_AmfMouseIndicatorWheelDownEndTime = Now + HighlightDuration;
	}
	if(HasTrackedPlayer && pTrackedInput == nullptr && pTrackedCharacter != nullptr && pPrevTrackedCharacter != nullptr &&
		pPrevTrackedCharacter->m_AttackTick != pTrackedCharacter->m_AttackTick)
	{
		m_AmfMouseIndicatorMouse1EndTime = Now + HighlightDuration;
	}

	vec2 AimOffset(0.0f, 0.0f);
	float AimRotation = 0.0f;
	bool MouseMoved = false;
	vec2 Aim(0.0f, 0.0f);
	if(pTrackedInput != nullptr)
		Aim = vec2((float)pTrackedInput->m_TargetX, (float)pTrackedInput->m_TargetY);
	else if(HasTrackedPlayer)
		GetAmfMouseIndicatorTrackedAim(TrackedClientId, Client()->IntraGameTick(g_Config.m_ClDummy), Aim);
	else
		Aim = GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy];
	const float MaxDistance = std::max(GameClient()->m_Controls.GetMaxMouseDistance(), 0.001f);
	float Length = length(Aim);
	if(Length > 0.001f)
	{
		MouseMoved = true;
		AimRotation = std::atan2(Aim.y, Aim.x) + pi / 2.0f;
		Aim /= MaxDistance;
		Length = length(Aim);
		if(Length > 1.0f)
			Aim /= Length;
		AimOffset = Aim;
	}

	const HudLayout::SModuleRect Bounds = GetAmfMouseIndicatorBounds();
	for(int i = 0; i < Preset.m_NumElements; ++i)
	{
		const auto &Element = Preset.m_pElements[i];
		bool Active = false;
		switch(Element.m_InputKind)
		{
		case EAmfMouseIndicatorInputKind::NONE:
			Active = true;
			break;
		case EAmfMouseIndicatorInputKind::MOUSE_BUTTON:
			Active = HasTrackedPlayer ?
				(pTrackedInput != nullptr ? IsAmfMouseIndicatorButtonPressed(pTrackedInput, Element.m_MouseButton) :
					IsAmfMouseIndicatorButtonPressedFromCharacter(pPrevTrackedCharacter, pTrackedCharacter, Element.m_MouseButton, Now, m_AmfMouseIndicatorMouse1EndTime)) :
				IsAmfMouseIndicatorButtonPressed(Input(), Element.m_MouseButton);
			break;
		case EAmfMouseIndicatorInputKind::WHEEL:
			Active = IsAmfMouseIndicatorWheelActive(Element.m_WheelDir, Now, m_AmfMouseIndicatorWheelUpEndTime, m_AmfMouseIndicatorWheelDownEndTime);
			break;
		case EAmfMouseIndicatorInputKind::MOUSE_MOVE:
			Active = MouseMoved;
			break;
		}

		if(Element.m_ActiveOnly && !Active)
			continue;
		if(Element.m_InputKind == EAmfMouseIndicatorInputKind::WHEEL && !Active)
			continue;

		int MapY = Element.m_MapY;
		if(Active && !Element.m_ActiveOnly && Element.m_InputKind == EAmfMouseIndicatorInputKind::MOUSE_BUTTON)
		{
			const int Candidate = MapY + Element.m_MapH + AMF_MOUSE_INDICATOR_PRESSED_TEXTURE_SPACE;
			if(Candidate + Element.m_MapH <= Preset.m_AtlasHeight)
				MapY = Candidate;
		}

		const float X = Bounds.m_X + Element.m_PosX * Scale;
		const float Y = Bounds.m_Y + Element.m_PosY * Scale;
		vec2 Offset(0.0f, 0.0f);
		float Rotation = 0.0f;
		if(Element.m_InputKind == EAmfMouseIndicatorInputKind::MOUSE_MOVE)
		{
			Offset = AimOffset * (Element.m_MouseRadius * Scale);
			if(Element.m_MouseType == 1)
				Rotation = AimRotation;
		}
		// Dot Dot has a static point in the donor atlas (white) and a moving
		// point in its active frame (yellow). Treat that static point as part of
		// the same selected pressed color; all other styles/elements keep the
		// donor texture and tint unchanged.
		const bool IsDotDotStaticPoint = MousePreset == 2 &&
			Element.m_InputKind == EAmfMouseIndicatorInputKind::NONE &&
			Element.m_MapX == 493 && Element.m_MapY == 245 &&
			Element.m_MapW == 100 && Element.m_MapH == 100;
		const ColorRGBA SpriteColor = IsDotDotStaticPoint && !WhitePressed ?
			ColorRGBA(1.0f, AMF_MOUSE_INDICATOR_YELLOW_GREEN, AMF_MOUSE_INDICATOR_YELLOW_BLUE, 1.0f) :
			ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);

		DrawAmfMouseIndicatorSprite(
			Graphics(),
			MouseTexture,
			Preset.m_AtlasWidth,
			Preset.m_AtlasHeight,
			Element.m_MapX,
			MapY,
			Element.m_MapW,
			Element.m_MapH,
			X + Offset.x,
			Y + Offset.y,
			Element.m_MapW * Scale,
			Element.m_MapH * Scale,
			SpriteColor,
			Rotation);
	}
}

void CHud::RenderSuddenDeath()
{
	if(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_SUDDENDEATH)
	{
		float Half = m_Width / 2.0f;
		const char *pText = Localize("Sudden Death");
		float FontSize = 12.0f;
		float w = TextRender()->TextWidth(FontSize, pText, -1, -1.0f);
		TextRender()->Text(Half - w / 2, 2, FontSize, pText, -1.0f);
	}
}

void CHud::RenderScoreHud()
{
	m_ScoreHudBounds = {};
	if(!HudLayout::IsEnabled(HudLayout::MODULE_SCORE))
		return;
	const auto ScoreLayout = HudLayout::Get(HudLayout::MODULE_SCORE, m_Width, m_Height);
	const float ModuleScale = std::clamp(ScoreLayout.m_Scale / 100.0f, 0.25f, 3.0f);
	// render small score hud
	if(!(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER))
	{
		float StartY = 229.0f; // the height of this display is 56, so EndY is 285

		const float ScoreSingleBoxHeight = 18.0f;

		bool ForceScoreInfoInit = !m_aScoreInfo[0].m_Initialized || !m_aScoreInfo[1].m_Initialized;
		m_aScoreInfo[0].m_Initialized = m_aScoreInfo[1].m_Initialized = true;

		if(GameClient()->IsTeamPlay() && GameClient()->m_Snap.m_pGameDataObj)
		{
			char aScoreTeam[2][16];
			str_format(aScoreTeam[TEAM_RED], sizeof(aScoreTeam[TEAM_RED]), "%d", GameClient()->m_Snap.m_pGameDataObj->m_TeamscoreRed);
			str_format(aScoreTeam[TEAM_BLUE], sizeof(aScoreTeam[TEAM_BLUE]), "%d", GameClient()->m_Snap.m_pGameDataObj->m_TeamscoreBlue);

			bool aRecreateTeamScore[2] = {str_comp(aScoreTeam[0], m_aScoreInfo[0].m_aScoreText) != 0, str_comp(aScoreTeam[1], m_aScoreInfo[1].m_aScoreText) != 0};

			const int aFlagCarrier[2] = {
				GameClient()->m_Snap.m_pGameDataObj->m_FlagCarrierRed,
				GameClient()->m_Snap.m_pGameDataObj->m_FlagCarrierBlue};

			bool RecreateRect = ForceScoreInfoInit;
			for(int t = 0; t < 2; t++)
			{
				if(aRecreateTeamScore[t])
				{
					m_aScoreInfo[t].m_ScoreTextWidth = TextRender()->TextWidth(14.0f, aScoreTeam[t == 0 ? TEAM_RED : TEAM_BLUE], -1, -1.0f);
					str_copy(m_aScoreInfo[t].m_aScoreText, aScoreTeam[t == 0 ? TEAM_RED : TEAM_BLUE]);
					RecreateRect = true;
				}
			}

			static float s_TextWidth100 = TextRender()->TextWidth(14.0f, "100", -1, -1.0f);
			float ScoreWidthMax = std::max({m_aScoreInfo[0].m_ScoreTextWidth, m_aScoreInfo[1].m_ScoreTextWidth, s_TextWidth100});
			float Split = 3.0f;
			float ImageSize = (GameClient()->m_Snap.m_pGameInfoObj->m_GameFlags & GAMEFLAG_FLAGS) ? 16.0f : Split;
			const float ScoreBoxWidth = ScoreWidthMax + ImageSize + 2 * Split;
			float NameOverflow = 0.0f;
			if(GameClient()->m_Snap.m_pGameInfoObj->m_GameFlags & GAMEFLAG_FLAGS)
			{
				for(int t = 0; t < 2; ++t)
				{
					if(aFlagCarrier[t] < 0)
						continue;
					const int Id = aFlagCarrier[t] % MAX_CLIENTS;
					const float NameWidth = TextRender()->TextWidth(8.0f, GameClient()->m_aClients[Id].m_aName, -1, -1.0f);
					NameOverflow = std::max(NameOverflow, std::max(0.0f, NameWidth + 1.0f - ScoreBoxWidth));
				}
			}
			float ScoreAnchorX = m_Width - ScoreBoxWidth * ModuleScale;
			float ScoreAnchorY = StartY;
			if(HudLayout::HasPositionOverride(HudLayout::MODULE_SCORE))
			{
				// Stored Score X is the visual left edge, including an optional
				// flag-carrier name. The internal score boxes keep their own offset.
				ScoreAnchorX = ScoreLayout.m_X + NameOverflow * ModuleScale;
				ScoreAnchorY = ScoreLayout.m_Y;
			}
			m_ScoreHudBounds = HudLayout::ClampRectToScreen({ScoreAnchorX - NameOverflow * ModuleScale, ScoreAnchorY, (ScoreBoxWidth + NameOverflow) * ModuleScale, 54.0f * ModuleScale, 5.0f * ModuleScale}, m_Width, m_Height);
			// The editor box is the final scaled rect. Feed the same clamped
			// top-left back into the renderer before MapHudModuleScale so a score
			// near an edge cannot render outside its own published bounds.
			ScoreAnchorX = m_ScoreHudBounds.m_X + NameOverflow * ModuleScale;
			ScoreAnchorY = m_ScoreHudBounds.m_Y;
			float ScoreRight = ScoreAnchorX + ScoreBoxWidth;
			StartY = ScoreAnchorY;
			const bool ScorePositionChanged = ScoreRight != m_ScoreHudLastRight || StartY != m_ScoreHudLastTop;
			m_ScoreHudLastRight = ScoreRight;
			m_ScoreHudLastTop = StartY;
			MapHudModuleScale(Graphics(), m_Width, m_Height, ModuleScale, ScoreAnchorX, ScoreAnchorY);
			if(ScorePositionChanged)
			{
				aRecreateTeamScore[0] = aRecreateTeamScore[1] = true;
				RecreateRect = true;
			}
			for(int t = 0; t < 2; t++)
			{
				const float BoxX = ScoreRight - ScoreWidthMax - ImageSize - 2 * Split;
				const float BoxY = StartY + t * 20;
				const int BoxCorners = ScaledHudRectCorners(ScoreAnchorX, ScoreAnchorY, ModuleScale, BoxX, BoxY, ScoreWidthMax + ImageSize + 2 * Split, ScoreSingleBoxHeight, m_Width, m_Height);
				if(BoxCorners != m_aScoreHudLastCorners[t])
					RecreateRect = true;

				// draw box
				if(RecreateRect)
				{
					Graphics()->DeleteQuadContainer(m_aScoreInfo[t].m_RoundRectQuadContainerIndex);

					if(t == 0)
						Graphics()->SetColor(0.975f, 0.17f, 0.17f, 0.3f);
					else
						Graphics()->SetColor(0.17f, 0.46f, 0.975f, 0.3f);
					m_aScoreInfo[t].m_RoundRectQuadContainerIndex = Graphics()->CreateRectQuadContainer(BoxX, BoxY, ScoreWidthMax + ImageSize + 2 * Split, ScoreSingleBoxHeight, 5.0f, BoxCorners);
					m_aScoreHudLastCorners[t] = BoxCorners;
				}
				Graphics()->TextureClear();
				Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
				if(m_aScoreInfo[t].m_RoundRectQuadContainerIndex != -1)
					Graphics()->RenderQuadContainer(m_aScoreInfo[t].m_RoundRectQuadContainerIndex, -1);

				// draw score
				if(aRecreateTeamScore[t])
				{
					CTextCursor Cursor;
					Cursor.SetPosition(vec2(ScoreRight - ScoreWidthMax + (ScoreWidthMax - m_aScoreInfo[t].m_ScoreTextWidth) / 2 - Split, StartY + t * 20 + (18.f - 14.f) / 2.f));
					Cursor.m_FontSize = 14.0f;
					TextRender()->RecreateTextContainer(m_aScoreInfo[t].m_TextScoreContainerIndex, &Cursor, aScoreTeam[t]);
				}
				if(m_aScoreInfo[t].m_TextScoreContainerIndex.Valid())
				{
					ColorRGBA TColor(1.f, 1.f, 1.f, 1.f);
					ColorRGBA TOutlineColor(0.f, 0.f, 0.f, 0.3f);
					TextRender()->RenderTextContainer(m_aScoreInfo[t].m_TextScoreContainerIndex, TColor, TOutlineColor);
				}

				if(GameClient()->m_Snap.m_pGameInfoObj->m_GameFlags & GAMEFLAG_FLAGS)
				{
					int BlinkTimer = (GameClient()->m_aFlagDropTick[t] != 0 &&
								 (Client()->GameTick(g_Config.m_ClDummy) - GameClient()->m_aFlagDropTick[t]) / Client()->GameTickSpeed() >= 25) ?
								 10 :
								 20;
					if(aFlagCarrier[t] == FLAG_ATSTAND || (aFlagCarrier[t] == FLAG_TAKEN && ((Client()->GameTick(g_Config.m_ClDummy) / BlinkTimer) & 1)))
					{
						// draw flag
						Graphics()->TextureSet(t == 0 ? GameClient()->m_GameSkin.m_SpriteFlagRed : GameClient()->m_GameSkin.m_SpriteFlagBlue);
						Graphics()->SetColor(1.f, 1.f, 1.f, 1.f);
						Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_FlagOffset, ScoreRight - ScoreWidthMax - ImageSize, StartY + 1.0f + t * 20);
					}
					else if(aFlagCarrier[t] >= 0)
					{
						// draw name of the flag holder
						int Id = aFlagCarrier[t] % MAX_CLIENTS;
						const char *pName = GameClient()->m_aClients[Id].m_aName;
						if(str_comp(pName, m_aScoreInfo[t].m_aPlayerNameText) != 0 || RecreateRect)
						{
							str_copy(m_aScoreInfo[t].m_aPlayerNameText, pName);

							float w = TextRender()->TextWidth(8.0f, pName, -1, -1.0f);

							CTextCursor Cursor;
							Cursor.SetPosition(vec2(std::min(ScoreRight - w - 1.0f, ScoreRight - ScoreWidthMax - ImageSize - 2 * Split), StartY + (t + 1) * 20.0f - 2.0f));
							Cursor.m_FontSize = 8.0f;
							TextRender()->RecreateTextContainer(m_aScoreInfo[t].m_OptionalNameTextContainerIndex, &Cursor, pName);
						}

						if(m_aScoreInfo[t].m_OptionalNameTextContainerIndex.Valid())
						{
							ColorRGBA TColor(1.f, 1.f, 1.f, 1.f);
							ColorRGBA TOutlineColor(0.f, 0.f, 0.f, 0.3f);
							TextRender()->RenderTextContainer(m_aScoreInfo[t].m_OptionalNameTextContainerIndex, TColor, TOutlineColor);
						}

						// draw tee of the flag holder
						CTeeRenderInfo TeeInfo = GameClient()->m_aClients[Id].m_RenderInfo;
						TeeInfo.m_Size = ScoreSingleBoxHeight;

						const CAnimState *pIdleState = CAnimState::GetIdle();
						vec2 OffsetToMid;
						CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeInfo, OffsetToMid);
						vec2 TeeRenderPos(ScoreRight - ScoreWidthMax - TeeInfo.m_Size / 2 - Split, StartY + (t * 20) + ScoreSingleBoxHeight / 2.0f + OffsetToMid.y);

						RenderTools()->RenderTee(pIdleState, &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), TeeRenderPos);
					}
				}
				StartY += 8.0f;
			}
			Graphics()->MapScreen(0.0f, 0.0f, m_Width, m_Height);
		}
		else
		{
			int Local = -1;
			int aPos[2] = {1, 2};
			const CNetObj_PlayerInfo *apPlayerInfo[2] = {nullptr, nullptr};
			int i = 0;
			for(int t = 0; t < 2 && i < MAX_CLIENTS && GameClient()->m_Snap.m_apInfoByScore[i]; ++i)
			{
				if(GameClient()->m_Snap.m_apInfoByScore[i]->m_Team != TEAM_SPECTATORS)
				{
					apPlayerInfo[t] = GameClient()->m_Snap.m_apInfoByScore[i];
					if(apPlayerInfo[t]->m_ClientId == GameClient()->m_Snap.m_LocalClientId)
						Local = t;
					++t;
				}
			}
			// search local player info if not a spectator, nor within top2 scores
			if(Local == -1 && GameClient()->m_Snap.m_pLocalInfo && GameClient()->m_Snap.m_pLocalInfo->m_Team != TEAM_SPECTATORS)
			{
				for(; i < MAX_CLIENTS && GameClient()->m_Snap.m_apInfoByScore[i]; ++i)
				{
					if(GameClient()->m_Snap.m_apInfoByScore[i]->m_Team != TEAM_SPECTATORS)
						++aPos[1];
					if(GameClient()->m_Snap.m_apInfoByScore[i]->m_ClientId == GameClient()->m_Snap.m_LocalClientId)
					{
						apPlayerInfo[1] = GameClient()->m_Snap.m_apInfoByScore[i];
						Local = 1;
						break;
					}
				}
			}
			char aScore[2][16];
			for(int t = 0; t < 2; ++t)
			{
				if(apPlayerInfo[t])
				{
					if(Client()->IsSixup() && GameClient()->m_Snap.m_pGameInfoObj->m_GameFlags & protocol7::GAMEFLAG_RACE)
					{
						str_time((int64_t)absolute(apPlayerInfo[t]->m_Score) / 10, ETimeFormat::MINS_CENTISECS, aScore[t], sizeof(aScore[t]));
					}
					else if(GameClient()->m_GameInfo.m_TimeScore)
					{
						CGameClient::CClientData &ClientData = GameClient()->m_aClients[apPlayerInfo[t]->m_ClientId];
						if(GameClient()->m_ReceivedDDNetPlayerFinishTimes && ClientData.m_FinishTimeSeconds != FinishTime::NOT_FINISHED_MILLIS)
						{
							int64_t TimeSeconds = static_cast<int64_t>(absolute(ClientData.m_FinishTimeSeconds));
							int64_t TimeMillis = TimeSeconds * 1000 + (absolute(ClientData.m_FinishTimeMillis) % 1000);

							str_time(TimeMillis / 10, ETimeFormat::HOURS, aScore[t], sizeof(aScore[t]));
						}
						else if(apPlayerInfo[t]->m_Score != FinishTime::NOT_FINISHED_TIMESCORE)
						{
							str_time((int64_t)absolute(apPlayerInfo[t]->m_Score) * 100, ETimeFormat::HOURS, aScore[t], sizeof(aScore[t]));
						}
						else
						{
							aScore[t][0] = 0;
						}
					}
					else
					{
						str_format(aScore[t], sizeof(aScore[t]), "%d", apPlayerInfo[t]->m_Score);
					}
				}
				else
				{
					aScore[t][0] = 0;
				}
			}
			// Text containers include absolute screen coordinates. Detect a moved score
			// HUD before rebuilding them, using the currently measured layout; this is
			// the same geometry the renderer uses below once the current score text has
			// been measured.
			const float CachedScoreWidth = std::max(std::max(m_aScoreInfo[0].m_ScoreTextWidth, m_aScoreInfo[1].m_ScoreTextWidth), TextRender()->TextWidth(14.0f, "10", -1, -1.0f));
			const float CachedScoreBoxWidth = CachedScoreWidth + 16.0f + 2.0f * 3.0f + 16.0f;
			float CachedNameOverflow = 0.0f;
			for(int t = 0; t < 2; ++t)
			{
				if(!apPlayerInfo[t])
					continue;
				const int Id = apPlayerInfo[t]->m_ClientId;
				if(Id >= 0 && Id < MAX_CLIENTS)
				{
					const float NameWidth = TextRender()->TextWidth(8.0f, GameClient()->m_aClients[Id].m_aName, -1, -1.0f);
					CachedNameOverflow = std::max(CachedNameOverflow, std::max(0.0f, NameWidth + 1.0f - CachedScoreBoxWidth));
				}
			}
			float CachedAnchorX = m_Width - CachedScoreBoxWidth * ModuleScale;
			float CachedAnchorY = StartY;
			if(HudLayout::HasPositionOverride(HudLayout::MODULE_SCORE))
			{
				CachedAnchorX = ScoreLayout.m_X + CachedNameOverflow * ModuleScale;
				CachedAnchorY = ScoreLayout.m_Y;
			}
			float CachedScoreRight = CachedAnchorX + CachedScoreBoxWidth;
			float CachedScoreTop = CachedAnchorY;
			if(CachedScoreRight != m_ScoreHudLastRight || CachedScoreTop != m_ScoreHudLastTop)
				ForceScoreInfoInit = true;

			bool RecreateScores = ForceScoreInfoInit || str_comp(aScore[0], m_aScoreInfo[0].m_aScoreText) != 0 || str_comp(aScore[1], m_aScoreInfo[1].m_aScoreText) != 0 || m_LastLocalClientId != GameClient()->m_Snap.m_LocalClientId;
			m_LastLocalClientId = GameClient()->m_Snap.m_LocalClientId;

			bool RecreateRect = ForceScoreInfoInit;
			for(int t = 0; t < 2; t++)
			{
				if(RecreateScores)
				{
					m_aScoreInfo[t].m_ScoreTextWidth = TextRender()->TextWidth(14.0f, aScore[t], -1, -1.0f);
					str_copy(m_aScoreInfo[t].m_aScoreText, aScore[t]);
					RecreateRect = true;
				}

				if(apPlayerInfo[t])
				{
					int Id = apPlayerInfo[t]->m_ClientId;
					if(Id >= 0 && Id < MAX_CLIENTS)
					{
						const char *pName = GameClient()->m_aClients[Id].m_aName;
						if(str_comp(pName, m_aScoreInfo[t].m_aPlayerNameText) != 0)
							RecreateRect = true;
					}
				}
				else
				{
					if(m_aScoreInfo[t].m_aPlayerNameText[0] != 0)
						RecreateRect = true;
				}

				char aBuf[16];
				str_format(aBuf, sizeof(aBuf), "%d.", aPos[t]);
				if(str_comp(aBuf, m_aScoreInfo[t].m_aRankText) != 0)
					RecreateRect = true;
			}

			static float s_TextWidth10 = TextRender()->TextWidth(14.0f, "10", -1, -1.0f);
			float ScoreWidthMax = std::max({m_aScoreInfo[0].m_ScoreTextWidth, m_aScoreInfo[1].m_ScoreTextWidth, s_TextWidth10});
			float Split = 3.0f, ImageSize = 16.0f, PosSize = 16.0f;
			const float ScoreBoxWidth = ScoreWidthMax + ImageSize + 2 * Split + PosSize;
			float NameOverflow = 0.0f;
			for(int t = 0; t < 2; ++t)
			{
				if(!apPlayerInfo[t])
					continue;
				const int Id = apPlayerInfo[t]->m_ClientId;
				if(Id >= 0 && Id < MAX_CLIENTS)
				{
					const float NameWidth = TextRender()->TextWidth(8.0f, GameClient()->m_aClients[Id].m_aName, -1, -1.0f);
					NameOverflow = std::max(NameOverflow, std::max(0.0f, NameWidth + 1.0f - ScoreBoxWidth));
				}
			}
			float ScoreAnchorX = m_Width - ScoreBoxWidth * ModuleScale;
			float ScoreAnchorY = StartY;
			if(HudLayout::HasPositionOverride(HudLayout::MODULE_SCORE))
			{
				ScoreAnchorX = ScoreLayout.m_X + NameOverflow * ModuleScale;
				ScoreAnchorY = ScoreLayout.m_Y;
			}
			m_ScoreHudBounds = HudLayout::ClampRectToScreen({ScoreAnchorX - NameOverflow * ModuleScale, ScoreAnchorY, (ScoreBoxWidth + NameOverflow) * ModuleScale, 54.0f * ModuleScale, 5.0f * ModuleScale}, m_Width, m_Height);
			// Keep the renderer and its authoritative/editor rect on the same
			// final geometry, including when the scaled score is clamped to an
			// edge of the HUD canvas.
			ScoreAnchorX = m_ScoreHudBounds.m_X + NameOverflow * ModuleScale;
			ScoreAnchorY = m_ScoreHudBounds.m_Y;
			float ScoreRight = ScoreAnchorX + ScoreBoxWidth;
			StartY = ScoreAnchorY;
			m_ScoreHudLastRight = ScoreRight;
			m_ScoreHudLastTop = StartY;
			MapHudModuleScale(Graphics(), m_Width, m_Height, ModuleScale, ScoreAnchorX, ScoreAnchorY);

			for(int t = 0; t < 2; t++)
			{
				const float BoxX = ScoreRight - ScoreWidthMax - ImageSize - 2 * Split - PosSize;
				const float BoxY = StartY + t * 20;
				const int BoxCorners = ScaledHudRectCorners(ScoreAnchorX, ScoreAnchorY, ModuleScale, BoxX, BoxY, ScoreWidthMax + ImageSize + 2 * Split + PosSize, ScoreSingleBoxHeight, m_Width, m_Height);
				if(BoxCorners != m_aScoreHudLastCorners[t])
					RecreateRect = true;

				// draw box
				if(RecreateRect)
				{
					Graphics()->DeleteQuadContainer(m_aScoreInfo[t].m_RoundRectQuadContainerIndex);

					if(t == Local)
						Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.25f);
					else
						Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.25f);
					m_aScoreInfo[t].m_RoundRectQuadContainerIndex = Graphics()->CreateRectQuadContainer(BoxX, BoxY, ScoreWidthMax + ImageSize + 2 * Split + PosSize, ScoreSingleBoxHeight, 5.0f, BoxCorners);
					m_aScoreHudLastCorners[t] = BoxCorners;
				}
				Graphics()->TextureClear();
				Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
				if(m_aScoreInfo[t].m_RoundRectQuadContainerIndex != -1)
					Graphics()->RenderQuadContainer(m_aScoreInfo[t].m_RoundRectQuadContainerIndex, -1);

				if(RecreateScores)
				{
					CTextCursor Cursor;
					Cursor.SetPosition(vec2(ScoreRight - ScoreWidthMax + (ScoreWidthMax - m_aScoreInfo[t].m_ScoreTextWidth) - Split, StartY + t * 20 + (18.f - 14.f) / 2.f));
					Cursor.m_FontSize = 14.0f;
					TextRender()->RecreateTextContainer(m_aScoreInfo[t].m_TextScoreContainerIndex, &Cursor, aScore[t]);
				}
				// draw score
				if(m_aScoreInfo[t].m_TextScoreContainerIndex.Valid())
				{
					ColorRGBA TColor(1.f, 1.f, 1.f, 1.f);
					ColorRGBA TOutlineColor(0.f, 0.f, 0.f, 0.3f);
					TextRender()->RenderTextContainer(m_aScoreInfo[t].m_TextScoreContainerIndex, TColor, TOutlineColor);
				}

				if(apPlayerInfo[t])
				{
					// draw name
					int Id = apPlayerInfo[t]->m_ClientId;
					if(Id >= 0 && Id < MAX_CLIENTS)
					{
						const char *pName = GameClient()->m_aClients[Id].m_aName;
						if(RecreateRect)
						{
							str_copy(m_aScoreInfo[t].m_aPlayerNameText, pName);

							CTextCursor Cursor;
							Cursor.SetPosition(vec2(std::min(ScoreRight - TextRender()->TextWidth(8.0f, pName) - 1.0f, ScoreRight - ScoreWidthMax - ImageSize - 2 * Split - PosSize), StartY + (t + 1) * 20.0f - 2.0f));
							Cursor.m_FontSize = 8.0f;
							TextRender()->RecreateTextContainer(m_aScoreInfo[t].m_OptionalNameTextContainerIndex, &Cursor, pName);
						}

						if(m_aScoreInfo[t].m_OptionalNameTextContainerIndex.Valid())
						{
							ColorRGBA TColor(1.f, 1.f, 1.f, 1.f);
							ColorRGBA TOutlineColor(0.f, 0.f, 0.f, 0.3f);
							TextRender()->RenderTextContainer(m_aScoreInfo[t].m_OptionalNameTextContainerIndex, TColor, TOutlineColor);
						}

						// draw tee
						CTeeRenderInfo TeeInfo = GameClient()->m_aClients[Id].m_RenderInfo;
						TeeInfo.m_Size = ScoreSingleBoxHeight;

						const CAnimState *pIdleState = CAnimState::GetIdle();
						vec2 OffsetToMid;
						CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeInfo, OffsetToMid);
						vec2 TeeRenderPos(ScoreRight - ScoreWidthMax - TeeInfo.m_Size / 2 - Split, StartY + (t * 20) + ScoreSingleBoxHeight / 2.0f + OffsetToMid.y);

						RenderTools()->RenderTee(pIdleState, &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), TeeRenderPos);
					}
				}
				else
				{
					m_aScoreInfo[t].m_aPlayerNameText[0] = 0;
				}

				// draw position
				char aBuf[16];
				str_format(aBuf, sizeof(aBuf), "%d.", aPos[t]);
				if(RecreateRect)
				{
					str_copy(m_aScoreInfo[t].m_aRankText, aBuf);

					CTextCursor Cursor;
					Cursor.SetPosition(vec2(ScoreRight - ScoreWidthMax - ImageSize - Split - PosSize, StartY + t * 20 + (18.f - 10.f) / 2.f));
					Cursor.m_FontSize = 10.0f;
					TextRender()->RecreateTextContainer(m_aScoreInfo[t].m_TextRankContainerIndex, &Cursor, aBuf);
				}
				if(m_aScoreInfo[t].m_TextRankContainerIndex.Valid())
				{
					ColorRGBA TColor(1.f, 1.f, 1.f, 1.f);
					ColorRGBA TOutlineColor(0.f, 0.f, 0.f, 0.3f);
					TextRender()->RenderTextContainer(m_aScoreInfo[t].m_TextRankContainerIndex, TColor, TOutlineColor);
				}

				StartY += 8.0f;
			}
			Graphics()->MapScreen(0.0f, 0.0f, m_Width, m_Height);
		}
	}
}

void CHud::RenderWarmupTimer()
{
	if(GameClient()->m_Snap.m_pGameInfoObj->m_WarmupTimer <= 0 ||
		(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_RACETIME) != 0)
	{
		return;
	}

	const float FontSize = 20.0f;
	const char *pTitle = Localize("Warmup");
	TextRender()->Text(150.0f * Graphics()->ScreenAspect() - TextRender()->TextWidth(FontSize, pTitle) / 2.0f, 50.0f, FontSize, pTitle);

	const int Seconds = GameClient()->m_Snap.m_pGameInfoObj->m_WarmupTimer / Client()->GameTickSpeed();
	char aWarmupTime[16];
	float TextWidth;
	if(Seconds < 5)
	{
		str_format(aWarmupTime, sizeof(aWarmupTime), "%d.%d", Seconds, (GameClient()->m_Snap.m_pGameInfoObj->m_WarmupTimer * 10 / Client()->GameTickSpeed()) % 10);
		TextWidth = TextRender()->TextWidth(FontSize, "0.0");
	}
	else
	{
		str_format(aWarmupTime, sizeof(aWarmupTime), "%d", Seconds);
		TextWidth = TextRender()->TextWidth(FontSize, aWarmupTime);
	}
	TextRender()->Text(150.0f * Graphics()->ScreenAspect() - TextWidth / 2.0f, 75.0f, FontSize, aWarmupTime);
}

void CHud::RenderTextInfo()
{
	m_PredictionTimeBounds = {};
	m_RemainingPlayersBounds = {};
	int Showfps = g_Config.m_ClShowfps;
#if defined(CONF_VIDEORECORDER)
	if(IVideo::Current())
		Showfps = 0;
#endif
	if(Showfps)
	{
		char aBuf[16];
		const int FramesPerSecond = round_to_int(1.0f / Client()->FrameTimeAverage());
		str_format(aBuf, sizeof(aBuf), "%d", FramesPerSecond);

		static float s_TextWidth0 = TextRender()->TextWidth(12.f, "0", -1, -1.0f);
		static float s_TextWidth00 = TextRender()->TextWidth(12.f, "00", -1, -1.0f);
		static float s_TextWidth000 = TextRender()->TextWidth(12.f, "000", -1, -1.0f);
		static float s_TextWidth0000 = TextRender()->TextWidth(12.f, "0000", -1, -1.0f);
		static float s_TextWidth00000 = TextRender()->TextWidth(12.f, "00000", -1, -1.0f);
		static const float s_aTextWidth[5] = {s_TextWidth0, s_TextWidth00, s_TextWidth000, s_TextWidth0000, s_TextWidth00000};

		int DigitIndex = GetDigitsIndex(FramesPerSecond, 4);

		CTextCursor Cursor;
		Cursor.SetPosition(vec2(m_Width - 10 - s_aTextWidth[DigitIndex], 5));
		Cursor.m_FontSize = 12.0f;
		auto OldFlags = TextRender()->GetRenderFlags();
		TextRender()->SetRenderFlags(OldFlags | TEXT_RENDER_FLAG_ONE_TIME_USE);
		if(m_FPSTextContainerIndex.Valid())
			TextRender()->RecreateTextContainerSoft(m_FPSTextContainerIndex, &Cursor, aBuf);
		else
			TextRender()->CreateTextContainer(m_FPSTextContainerIndex, &Cursor, "0");
		TextRender()->SetRenderFlags(OldFlags);
		if(m_FPSTextContainerIndex.Valid())
		{
			TextRender()->RenderTextContainer(m_FPSTextContainerIndex, TextRender()->DefaultTextColor(), TextRender()->DefaultTextOutlineColor());
		}
	}
	if(HudLayout::IsEnabled(HudLayout::MODULE_PING) && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "%d", Client()->GetPredictionTime());
		const auto Layout = HudLayout::Get(HudLayout::MODULE_PING, m_Width, m_Height);
		const float FontSize = 12.0f * std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
		const float TextWidth = TextRender()->TextWidth(FontSize, aBuf, -1, -1.0f);
		float X = m_Width - 10.0f - TextWidth;
		float Y = Showfps ? 20.0f : 5.0f;
		if(HudLayout::HasRuntimeOverride(HudLayout::MODULE_PING))
		{
			X = Layout.m_X;
			Y = Layout.m_Y;
		}
		m_PredictionTimeBounds = HudLayout::ClampRectToScreen({X, Y, TextWidth, FontSize, 0.0f}, m_Width, m_Height);
		TextRender()->Text(m_PredictionTimeBounds.m_X, m_PredictionTimeBounds.m_Y, FontSize, aBuf, -1.0f);
	}
	if(GameClient()->m_FastPractice.Enabled())
	{
		constexpr float FontSize = 10.0f;
		constexpr const char *pText = "Fast Practice Enabled";
		const float TextWidth = TextRender()->TextWidth(FontSize, pText, -1, -1.0f);
		TextRender()->Text((m_Width - TextWidth) * 0.5f, 25.0f, FontSize, pText, -1.0f);
	}

	if(g_Config.m_TcMiniDebug)
	{
		float FontSize = 8.0f;
		float TextHeight = 11.0f;
		char aBuf[64];
		float OffsetY = 3.0f;

		int PlayerId = GameClient()->m_Snap.m_LocalClientId;
		if(GameClient()->m_Snap.m_SpecInfo.m_Active)
			PlayerId = GameClient()->m_Snap.m_SpecInfo.m_SpectatorId;

		if(g_Config.m_ClShowhudDDRace && GameClient()->m_Snap.m_aCharacters[PlayerId].m_HasExtendedData && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW)
			OffsetY += 50.0f;
		else if(g_Config.m_ClShowhudHealthAmmo && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW)
			OffsetY += 27.0f;

		vec2 Pos;
		if(GameClient()->m_Snap.m_SpecInfo.m_SpectatorId == SPEC_FREEVIEW)
			Pos = vec2(GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy].x, GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy].y);
		else
			Pos = GameClient()->m_aClients[PlayerId].m_RenderPos;

		str_format(aBuf, sizeof(aBuf), "X: %.2f", Pos.x / 32.0f);
		TextRender()->Text(4, OffsetY, FontSize, aBuf, -1.0f);

		OffsetY += TextHeight;
		str_format(aBuf, sizeof(aBuf), "Y: %.2f", Pos.y / 32.0f);
		TextRender()->Text(4, OffsetY, FontSize, aBuf, -1.0f);
		if(GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW)
		{
			OffsetY += TextHeight;
			str_format(aBuf, sizeof(aBuf), "Angle: %d", GameClient()->m_aClients[PlayerId].m_RenderCur.m_Angle);
			TextRender()->Text(4.0f, OffsetY, FontSize, aBuf, -1.0f);

			OffsetY += TextHeight;
			str_format(aBuf, sizeof(aBuf), "VelY: %.2f", GameClient()->m_Snap.m_aCharacters[PlayerId].m_Cur.m_VelY / 256.0f * 50.0f / 32.0f);
			TextRender()->Text(4.0f, OffsetY, FontSize, aBuf, -1.0f);

			OffsetY += TextHeight;

			str_format(aBuf, sizeof(aBuf), "VelX: %.2f", GameClient()->m_Snap.m_aCharacters[PlayerId].m_Cur.m_VelX / 256.0f * 50.0f / 32.0f);
			TextRender()->Text(4.0f, OffsetY, FontSize, aBuf, -1.0f);
		}
	}
	if(g_Config.m_TcRenderCursorSpec && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId == SPEC_FREEVIEW)
	{
		int CurWeapon = 1;
		Graphics()->SetColor(1.f, 1.f, 1.f, g_Config.m_TcRenderCursorSpecAlpha / 100.0f);
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_aSpriteWeaponCursors[CurWeapon]);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_aCursorOffset[CurWeapon], m_Width / 2.0f, m_Height / 2.0f, 0.36f, 0.36f);
	}
	// render team in freeze text and last notify
	if((g_Config.m_TcShowFrozenText > 0 || g_Config.m_TcShowFrozenHud > 0 || g_Config.m_TcNotifyWhenLast) && GameClient()->m_GameInfo.m_EntitiesDDRace)
	{
		int NumInTeam = 0;
		int NumFrozen = 0;
		int LocalTeamID = 0;
		if(GameClient()->m_Snap.m_LocalClientId >= 0 && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId >= 0)
		{
			if(GameClient()->m_Snap.m_SpecInfo.m_Active == 1 && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != -1)
				LocalTeamID = GameClient()->m_Teams.Team(GameClient()->m_Snap.m_SpecInfo.m_SpectatorId);
			else
				LocalTeamID = GameClient()->m_Teams.Team(GameClient()->m_Snap.m_LocalClientId);
		}
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(!GameClient()->m_Snap.m_apPlayerInfos[i])
				continue;

			if(GameClient()->m_Teams.Team(i) == LocalTeamID)
			{
				NumInTeam++;
				if(GameClient()->m_aClients[i].m_FreezeEnd > 0 || GameClient()->m_aClients[i].m_DeepFrozen)
					NumFrozen++;
			}
		}

		// Notify when last
		if(g_Config.m_TcNotifyWhenLast)
		{
			if(NumInTeam > 1 && NumInTeam - NumFrozen == 1)
			{
				TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcNotifyWhenLastColor)));
				float FontSize = g_Config.m_TcNotifyWhenLastSize;
				float XPos = std::clamp((g_Config.m_TcNotifyWhenLastX / 100.0f) * m_Width, 1.0f, m_Width - FontSize);
				float YPos = std::clamp((g_Config.m_TcNotifyWhenLastY / 100.0f) * m_Height, 1.0f, m_Height - FontSize);

				TextRender()->Text(XPos, YPos, FontSize, g_Config.m_TcNotifyWhenLastText, -1.0f);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
		}
		// Show freeze text
		char aBuf[64];
		if(g_Config.m_TcShowFrozenText == 1)
			str_format(aBuf, sizeof(aBuf), "%d / %d", NumInTeam - NumFrozen, NumInTeam);
		else if(g_Config.m_TcShowFrozenText == 2)
			str_format(aBuf, sizeof(aBuf), "%d / %d", NumFrozen, NumInTeam);
		if(HudLayout::IsEnabled(HudLayout::MODULE_FROZEN_HUD))
		{
			const auto Layout = HudLayout::Get(HudLayout::MODULE_FROZEN_HUD, m_Width, m_Height);
			const float FontSize = 10.0f * std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
			const float TextWidth = TextRender()->TextWidth(FontSize, aBuf);
			float X = m_Width * 0.5f - TextWidth * 0.5f;
			float Y = 12.0f;
			if(HudLayout::HasRuntimeOverride(HudLayout::MODULE_FROZEN_HUD))
			{
				X = Layout.m_X;
				Y = Layout.m_Y;
			}
			m_RemainingPlayersBounds = HudLayout::ClampRectToScreen({X, Y, TextWidth, FontSize, 0.0f}, m_Width, m_Height);
			TextRender()->Text(m_RemainingPlayersBounds.m_X, m_RemainingPlayersBounds.m_Y, FontSize, aBuf);
		}

		// str_format(aBuf, sizeof(aBuf), "%d", GameClient()->m_aClients[GameClient()->m_Snap.m_LocalClientId].m_PrevPredicted.m_FreezeEnd);
		// str_format(aBuf, sizeof(aBuf), "%d", g_Config.m_ClWhatsMyPing);
		// TextRender()->Text(0, m_Width / 2 - TextRender()->TextWidth(0, 10, aBuf, -1, -1.0f) / 2, 20, 10, aBuf, -1.0f);

		if(g_Config.m_TcShowFrozenHud > 0 && !GameClient()->m_Scoreboard.IsShown() && !(LocalTeamID == 0 && g_Config.m_TcFrozenHudTeamOnly))
		{
			CTeeRenderInfo FreezeInfo;
			const CSkin *pSkin = GameClient()->m_Skins.Find("x_ninja");
			FreezeInfo.m_OriginalRenderSkin = pSkin->m_OriginalSkin;
			FreezeInfo.m_ColorableRenderSkin = pSkin->m_ColorableSkin;
			FreezeInfo.m_BloodColor = pSkin->m_BloodColor;
			FreezeInfo.m_SkinMetrics = pSkin->m_Metrics;
			FreezeInfo.m_ColorBody = ColorRGBA(1.0f, 1.0f, 1.0f);
			FreezeInfo.m_ColorFeet = ColorRGBA(1.0f, 1.0f, 1.0f);
			FreezeInfo.m_CustomColoredSkin = false;

			float ProgressiveOffset = 0.0f;
			float TeeSize = g_Config.m_TcFrozenHudTeeSize;
			int MaxTees = (int)(8.3f * (m_Width / m_Height) * 13.0f / TeeSize);
			if(!g_Config.m_ClShowfps && !g_Config.m_ClShowpred)
				MaxTees = (int)(9.5f * (m_Width / m_Height) * 13.0f / TeeSize);
			int MaxRows = g_Config.m_TcFrozenMaxRows;
			float StartPos = m_Width / 2.0f + 38.0f * (m_Width / m_Height) / 1.78f;

			int TotalRows = std::min(MaxRows, (NumInTeam + MaxTees - 1) / MaxTees);
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.4f);
			Graphics()->DrawRectExt(StartPos - TeeSize / 2.0f, 0.0f, TeeSize * std::min(NumInTeam, MaxTees), TeeSize + 3.0f + (TotalRows - 1) * TeeSize, 5.0f, IGraphics::CORNER_B);
			Graphics()->QuadsEnd();

			bool Overflow = NumInTeam > MaxTees * MaxRows;

			int NumDisplayed = 0;
			int NumInRow = 0;
			int CurrentRow = 0;

			for(int OverflowIndex = 0; OverflowIndex < 1 + Overflow; OverflowIndex++)
			{
				for(int i = 0; i < MAX_CLIENTS && NumDisplayed < MaxTees * MaxRows; i++)
				{
					if(!GameClient()->m_Snap.m_apPlayerInfos[i])
						continue;
					if(GameClient()->m_Teams.Team(i) == LocalTeamID)
					{
						bool Frozen = false;
						CTeeRenderInfo TeeInfo = GameClient()->m_aClients[i].m_RenderInfo;
						if(GameClient()->m_aClients[i].m_FreezeEnd > 0 || GameClient()->m_aClients[i].m_DeepFrozen)
						{
							if(!g_Config.m_TcShowFrozenHudSkins)
								TeeInfo = FreezeInfo;
							Frozen = true;
						}

						if(Overflow && Frozen && OverflowIndex == 0)
							continue;
						if(Overflow && !Frozen && OverflowIndex == 1)
							continue;

						NumDisplayed++;
						NumInRow++;
						if(NumInRow > MaxTees)
						{
							NumInRow = 1;
							ProgressiveOffset = 0.0f;
							CurrentRow++;
						}

						TeeInfo.m_Size = TeeSize;
						const CAnimState *pIdleState = CAnimState::GetIdle();
						vec2 OffsetToMid;
						CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeInfo, OffsetToMid);
						vec2 TeeRenderPos(StartPos + ProgressiveOffset, TeeSize * (0.7f) + CurrentRow * TeeSize);
						float Alpha = 1.0f;
						CNetObj_Character CurChar = GameClient()->m_aClients[i].m_RenderCur;
						if(g_Config.m_TcShowFrozenHudSkins && Frozen)
						{
							Alpha = 0.6f;
							TeeInfo.m_ColorBody.r *= 0.4f;
							TeeInfo.m_ColorBody.g *= 0.4f;
							TeeInfo.m_ColorBody.b *= 0.4f;
							TeeInfo.m_ColorFeet.r *= 0.4f;
							TeeInfo.m_ColorFeet.g *= 0.4f;
							TeeInfo.m_ColorFeet.b *= 0.4f;
						}
						if(Frozen)
							RenderTools()->RenderTee(pIdleState, &TeeInfo, EMOTE_PAIN, vec2(1.0f, 0.0f), TeeRenderPos, Alpha);
						else
							RenderTools()->RenderTee(pIdleState, &TeeInfo, CurChar.m_Emote, vec2(1.0f, 0.0f), TeeRenderPos);
						ProgressiveOffset += TeeSize;
					}
				}
			}
		}
	}
}

void CHud::RenderConnectionWarning()
{
	if(Client()->ConnectionProblems())
	{
		const char *pText = Localize("Connection Problems…");
		float w = TextRender()->TextWidth(24, pText, -1, -1.0f);
		TextRender()->Text(150 * Graphics()->ScreenAspect() - w / 2, 50, 24, pText, -1.0f);
	}
}

void CHud::RenderTeambalanceWarning()
{
	// render prompt about team-balance
	bool Flash = time() / (time_freq() / 2) % 2 == 0;
	if(GameClient()->IsTeamPlay())
	{
		int TeamDiff = GameClient()->m_Snap.m_aTeamSize[TEAM_RED] - GameClient()->m_Snap.m_aTeamSize[TEAM_BLUE];
		if(g_Config.m_ClWarningTeambalance && (TeamDiff >= 2 || TeamDiff <= -2))
		{
			const char *pText = Localize("Please balance teams!");
			if(Flash)
				TextRender()->TextColor(1, 1, 0.5f, 1);
			else
				TextRender()->TextColor(0.7f, 0.7f, 0.2f, 1.0f);
			TextRender()->Text(5, 50, 6, pText, -1.0f);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
	}
}

void CHud::RenderCursor()
{
	const float Scale = (float)g_Config.m_TcCursorScale / 100.0f;
	if(Scale <= 0.0f)
		return;

	int CurWeapon = 0;
	vec2 TargetPos;
	float Alpha = 1.0f;

	const vec2 Center = GameClient()->m_Camera.m_Center;
	float aScreen[4];
	Graphics()->MapScreenToWorld(Center.x, Center.y, 100.0f, 100.0f, 100.0f, 0, 0, Graphics()->ScreenAspect(), 1.0f, aScreen);
	Graphics()->MapScreen(aScreen[0], aScreen[1], aScreen[2], aScreen[3]);

	if(Client()->State() != IClient::STATE_DEMOPLAYBACK && GameClient()->m_Snap.m_LocalClientId >= 0 && GameClient()->m_Snap.m_pLocalCharacter)
	{
		// Render local cursor
		CurWeapon = std::max(0, GameClient()->m_aClients[GameClient()->m_Snap.m_LocalClientId].m_Predicted.m_ActiveWeapon);
		TargetPos = GameClient()->m_Controls.m_aTargetPos[g_Config.m_ClDummy];
	}
	else
	{
		// Render spec cursor
		if(!g_Config.m_ClSpecCursor || !GameClient()->m_CursorInfo.IsAvailable())
			return;

		bool RenderSpecCursor = (GameClient()->m_Snap.m_SpecInfo.m_Active && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW) || Client()->State() == IClient::STATE_DEMOPLAYBACK;

		if(!RenderSpecCursor)
			return;

		// Calculate factor to keep cursor on screen
		const vec2 HalfSize = Center - vec2(aScreen[0], aScreen[1]);
		const vec2 ScreenPos = (GameClient()->m_CursorInfo.WorldTarget() - Center) / GameClient()->m_Camera.m_Zoom;
		const float ClampFactor = std::max({
			1.0f,
			absolute(ScreenPos.x / HalfSize.x),
			absolute(ScreenPos.y / HalfSize.y),
		});

		CurWeapon = std::max(0, GameClient()->m_CursorInfo.Weapon() % NUM_WEAPONS);
		TargetPos = ScreenPos / ClampFactor + Center;
		if(ClampFactor != 1.0f)
			Alpha /= 2.0f;
	}

	Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
	Graphics()->TextureSet(GameClient()->m_GameSkin.m_aSpriteWeaponCursors[CurWeapon]);
	Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_aCursorOffset[CurWeapon], TargetPos.x, TargetPos.y, Scale, Scale);
}

void CHud::PrepareAmmoHealthAndArmorQuads()
{
	float x = 5;
	float y = 5;
	IGraphics::CQuadItem Array[10];

	// ammo of the different weapons
	for(int i = 0; i < NUM_WEAPONS; ++i)
	{
		// 0.6
		for(int n = 0; n < 10; n++)
			Array[n] = IGraphics::CQuadItem(x + n * 12, y, 10, 10);

		m_aAmmoOffset[i] = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

		// 0.7
		if(i == WEAPON_GRENADE)
		{
			// special case for 0.7 grenade
			for(int n = 0; n < 10; n++)
				Array[n] = IGraphics::CQuadItem(1 + x + n * 12, y, 10, 10);
		}
		else
		{
			for(int n = 0; n < 10; n++)
				Array[n] = IGraphics::CQuadItem(x + n * 12, y, 12, 12);
		}

		Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);
	}

	// health
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y, 10, 10);
	m_HealthOffset = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// 0.7
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y, 12, 12);
	Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// empty health
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y, 10, 10);
	m_EmptyHealthOffset = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// 0.7
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y, 12, 12);
	Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// armor meter
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y + 12, 10, 10);
	m_ArmorOffset = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// 0.7
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y + 12, 12, 12);
	Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// empty armor meter
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y + 12, 10, 10);
	m_EmptyArmorOffset = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// 0.7
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y + 12, 12, 12);
	Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);
}

void CHud::RenderAmmoHealthAndArmor(const CNetObj_Character *pCharacter)
{
	if(!pCharacter)
		return;

	bool IsSixupGameSkin = GameClient()->m_GameSkin.IsSixup();
	int QuadOffsetSixup = (IsSixupGameSkin ? 10 : 0);

	if(GameClient()->m_GameInfo.m_HudAmmo)
	{
		// ammo display
		float AmmoOffsetY = GameClient()->m_GameInfo.m_HudHealthArmor ? 24 : 0;
		int CurWeapon = pCharacter->m_Weapon % NUM_WEAPONS;
		// 0.7 only
		if(CurWeapon == WEAPON_NINJA)
		{
			if(!GameClient()->m_GameInfo.m_HudDDRace && Client()->IsSixup())
			{
				const int Max = g_pData->m_Weapons.m_Ninja.m_Duration * Client()->GameTickSpeed() / 1000;
				float NinjaProgress = std::clamp(pCharacter->m_AmmoCount - Client()->GameTick(g_Config.m_ClDummy), 0, Max) / (float)Max;
				RenderNinjaBarPos(5 + 10 * 12, 5, 6.f, 24.f, NinjaProgress);
			}
		}
		else if(CurWeapon >= 0 && GameClient()->m_GameSkin.m_aSpriteWeaponProjectiles[CurWeapon].IsValid())
		{
			Graphics()->TextureSet(GameClient()->m_GameSkin.m_aSpriteWeaponProjectiles[CurWeapon]);
			if(AmmoOffsetY > 0)
			{
				Graphics()->RenderQuadContainerEx(m_HudQuadContainerIndex, m_aAmmoOffset[CurWeapon] + QuadOffsetSixup, std::clamp(pCharacter->m_AmmoCount, 0, 10), 0, AmmoOffsetY);
			}
			else
			{
				Graphics()->RenderQuadContainer(m_HudQuadContainerIndex, m_aAmmoOffset[CurWeapon] + QuadOffsetSixup, std::clamp(pCharacter->m_AmmoCount, 0, 10));
			}
		}
	}

	if(GameClient()->m_GameInfo.m_HudHealthArmor)
	{
		// health display
		const int DisplayHealth = std::min(pCharacter->m_Health, 10);
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteHealthFull);
		Graphics()->RenderQuadContainer(m_HudQuadContainerIndex, m_HealthOffset + QuadOffsetSixup, DisplayHealth);
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteHealthEmpty);
		Graphics()->RenderQuadContainer(m_HudQuadContainerIndex, m_EmptyHealthOffset + QuadOffsetSixup + DisplayHealth, 10 - DisplayHealth);

		// armor display
		const int DisplayArmor = std::min(pCharacter->m_Armor, 10);
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteArmorFull);
		Graphics()->RenderQuadContainer(m_HudQuadContainerIndex, m_ArmorOffset + QuadOffsetSixup, DisplayArmor);
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteArmorEmpty);
		Graphics()->RenderQuadContainer(m_HudQuadContainerIndex, m_ArmorOffset + QuadOffsetSixup + DisplayArmor, 10 - DisplayArmor);
	}
}

void CHud::PreparePlayerStateQuads()
{
	float x = 5;
	float y = 5 + 24;
	IGraphics::CQuadItem Array[10];

	// Quads for displaying the available and used jumps
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y, 12, 12);
	m_AirjumpOffset = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y, 12, 12);
	m_AirjumpEmptyOffset = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// Quads for displaying weapons
	for(int Weapon = 0; Weapon < NUM_WEAPONS; ++Weapon)
	{
		const CDataWeaponspec &WeaponSpec = g_pData->m_Weapons.m_aId[Weapon];
		float ScaleX, ScaleY;
		Graphics()->GetSpriteScale(WeaponSpec.m_pSpriteBody, ScaleX, ScaleY);
		constexpr float HudWeaponScale = 0.25f;
		float Width = WeaponSpec.m_VisualSize * ScaleX * HudWeaponScale;
		float Height = WeaponSpec.m_VisualSize * ScaleY * HudWeaponScale;
		m_aWeaponOffset[Weapon] = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, Width, Height);
	}

	// Quads for displaying capabilities
	m_EndlessJumpOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_EndlessHookOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_JetpackOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_TeleportGrenadeOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_TeleportGunOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_TeleportLaserOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);

	// Quads for displaying prohibited capabilities
	m_SoloOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_CollisionDisabledOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_HookHitDisabledOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_HammerHitDisabledOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_GunHitDisabledOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_ShotgunHitDisabledOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_GrenadeHitDisabledOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_LaserHitDisabledOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);

	// Quads for displaying freeze status
	m_DeepFrozenOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_LiveFrozenOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);

	// Quads for displaying dummy actions
	m_DummyHammerOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_DummyCopyOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);

	// Quads for displaying team modes
	m_PracticeModeOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_LockModeOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_Team0ModeOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
}

void CHud::RenderPlayerState(const int ClientId)
{
	Graphics()->SetColor(1.f, 1.f, 1.f, 1.f);
	m_PlayerStateBounds = {};
	if(!HudLayout::IsEnabled(HudLayout::MODULE_PLAYER_STATE))
		return;
	const auto Layout = HudLayout::Get(HudLayout::MODULE_PLAYER_STATE, m_Width, m_Height);
	const float ModuleScale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	float OffsetX = 0.0f;
	float OffsetY = 0.0f;
	if(HudLayout::HasPositionOverride(HudLayout::MODULE_PLAYER_STATE))
	{
		OffsetX = Layout.m_X - 5.0f;
		OffsetY = Layout.m_Y - 5.0f;
	}
	const float BaseX = 5.0f + OffsetX;
	const float BaseY = 5.0f + OffsetY;
	MapHudModuleScale(Graphics(), m_Width, m_Height, ModuleScale, BaseX, BaseY);
	float MaxX = BaseX;
	float MaxY = BaseY;

	// pCharacter contains the predicted character for local players or the last snap for players who are spectated
	CCharacterCore *pCharacter = &GameClient()->m_aClients[ClientId].m_Predicted;
	CNetObj_Character *pPlayer = &GameClient()->m_aClients[ClientId].m_RenderCur;
	int TotalJumpsToDisplay = 0;
	if(g_Config.m_ClShowhudJumpsIndicator)
	{
		int AvailableJumpsToDisplay;
		if(GameClient()->m_Snap.m_aCharacters[ClientId].m_HasExtendedDisplayInfo)
		{
			const bool Grounded = Collision()->IsOnGround(vec2(pPlayer->m_X, pPlayer->m_Y), CCharacterCore::PhysicalSize());
			int UsedJumps = pCharacter->m_JumpedTotal;
			if(pCharacter->m_Jumps > 1)
			{
				UsedJumps += !Grounded;
			}
			else if(pCharacter->m_Jumps == 1)
			{
				// If the player has only one jump, each jump is the last one
				UsedJumps = pPlayer->m_Jumped & 2;
			}
			else if(pCharacter->m_Jumps == -1)
			{
				// The player has only one ground jump
				UsedJumps = !Grounded;
			}

			if(pCharacter->m_EndlessJump && UsedJumps >= absolute(pCharacter->m_Jumps))
			{
				UsedJumps = absolute(pCharacter->m_Jumps) - 1;
			}

			int UnusedJumps = absolute(pCharacter->m_Jumps) - UsedJumps;
			if(!(pPlayer->m_Jumped & 2) && UnusedJumps <= 0)
			{
				// In some edge cases when the player just got another number of jumps, UnusedJumps is not correct
				UnusedJumps = 1;
			}
			TotalJumpsToDisplay = std::clamp(absolute(pCharacter->m_Jumps), 0, 10);
			AvailableJumpsToDisplay = std::clamp(UnusedJumps, 0, TotalJumpsToDisplay);
		}
		else
		{
			TotalJumpsToDisplay = AvailableJumpsToDisplay = absolute(GameClient()->m_Snap.m_aCharacters[ClientId].m_ExtendedData.m_Jumps);
		}

		// render available and used jumps
		int JumpsOffsetY = ((GameClient()->m_GameInfo.m_HudHealthArmor && g_Config.m_ClShowhudHealthAmmo ? 24 : 0) +
				    (GameClient()->m_GameInfo.m_HudAmmo && g_Config.m_ClShowhudHealthAmmo ? 12 : 0));
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudAirjump);
		Graphics()->RenderQuadContainerEx(m_HudQuadContainerIndex, m_AirjumpOffset, AvailableJumpsToDisplay, OffsetX, JumpsOffsetY + OffsetY);
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudAirjumpEmpty);
		Graphics()->RenderQuadContainerEx(m_HudQuadContainerIndex, m_AirjumpEmptyOffset + AvailableJumpsToDisplay, TotalJumpsToDisplay - AvailableJumpsToDisplay, OffsetX, JumpsOffsetY + OffsetY);
		MaxX = std::max(MaxX, BaseX + TotalJumpsToDisplay * 12.0f);
		MaxY = std::max(MaxY, BaseY + 36.0f + JumpsOffsetY);
	}

	float x = BaseX + 12.0f;
	float y = (BaseY + 12.0f + (GameClient()->m_GameInfo.m_HudHealthArmor && g_Config.m_ClShowhudHealthAmmo ? 24 : 0) +
		   (GameClient()->m_GameInfo.m_HudAmmo && g_Config.m_ClShowhudHealthAmmo ? 12 : 0));

	// render weapons
	{
		constexpr float aWeaponWidth[NUM_WEAPONS] = {16, 12, 12, 12, 12, 12};
		constexpr float aWeaponInitialOffset[NUM_WEAPONS] = {-3, -4, -1, -1, -2, -4};
		bool InitialOffsetAdded = false;
		for(int Weapon = 0; Weapon < NUM_WEAPONS; ++Weapon)
		{
			if(!pCharacter->m_aWeapons[Weapon].m_Got)
				continue;
			if(!InitialOffsetAdded)
			{
				x += aWeaponInitialOffset[Weapon];
				InitialOffsetAdded = true;
			}
			if(pPlayer->m_Weapon != Weapon)
				Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.4f);
			Graphics()->QuadsSetRotation(pi * 7 / 4);
			Graphics()->TextureSet(GameClient()->m_GameSkin.m_aSpritePickupWeapons[Weapon]);
			Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_aWeaponOffset[Weapon], x, y);
			Graphics()->QuadsSetRotation(0);
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
			x += aWeaponWidth[Weapon];
		}
		if(pCharacter->m_aWeapons[WEAPON_NINJA].m_Got)
		{
			const int Max = g_pData->m_Weapons.m_Ninja.m_Duration * Client()->GameTickSpeed() / 1000;
			float NinjaProgress = std::clamp(pCharacter->m_Ninja.m_ActivationTick + g_pData->m_Weapons.m_Ninja.m_Duration * Client()->GameTickSpeed() / 1000 - Client()->GameTick(g_Config.m_ClDummy), 0, Max) / (float)Max;
			if(NinjaProgress > 0.0f && GameClient()->m_Snap.m_aCharacters[ClientId].m_HasExtendedDisplayInfo)
			{
				RenderNinjaBarPos(x, y - 12, 6.f, 24.f, NinjaProgress);
			}
		}
		MaxX = std::max(MaxX, x);
		MaxY = std::max(MaxY, y + 12.0f);
	}

	// render capabilities
	x = BaseX;
	y += 12;
	if(TotalJumpsToDisplay > 0)
	{
		y += 12;
	}
	bool HasCapabilities = false;
	if(pCharacter->m_EndlessJump)
	{
		HasCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudEndlessJump);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_EndlessJumpOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_EndlessHook)
	{
		HasCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudEndlessHook);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_EndlessHookOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_Jetpack)
	{
		HasCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudJetpack);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_JetpackOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_HasTelegunGun && pCharacter->m_aWeapons[WEAPON_GUN].m_Got)
	{
		HasCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudTeleportGun);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_TeleportGunOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_HasTelegunGrenade && pCharacter->m_aWeapons[WEAPON_GRENADE].m_Got)
	{
		HasCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudTeleportGrenade);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_TeleportGrenadeOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_HasTelegunLaser && pCharacter->m_aWeapons[WEAPON_LASER].m_Got)
	{
		HasCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudTeleportLaser);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_TeleportLaserOffset, x, y);
	}
	MaxX = std::max(MaxX, x + 12.0f);
	MaxY = std::max(MaxY, y + 12.0f);

	// render prohibited capabilities
	x = BaseX;
	if(HasCapabilities)
	{
		y += 12;
	}
	bool HasProhibitedCapabilities = false;
	if(pCharacter->m_Solo)
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudSolo);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_SoloOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_CollisionDisabled)
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudCollisionDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_CollisionDisabledOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_HookHitDisabled)
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudHookHitDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_HookHitDisabledOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_HammerHitDisabled)
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudHammerHitDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_HammerHitDisabledOffset, x, y);
		x += 12;
	}
	if((pCharacter->m_GrenadeHitDisabled && pCharacter->m_HasTelegunGun && pCharacter->m_aWeapons[WEAPON_GUN].m_Got))
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudGunHitDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_LaserHitDisabledOffset, x, y);
		x += 12;
	}
	if((pCharacter->m_ShotgunHitDisabled && pCharacter->m_aWeapons[WEAPON_SHOTGUN].m_Got))
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudShotgunHitDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_ShotgunHitDisabledOffset, x, y);
		x += 12;
	}
	if((pCharacter->m_GrenadeHitDisabled && pCharacter->m_aWeapons[WEAPON_GRENADE].m_Got))
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudGrenadeHitDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_GrenadeHitDisabledOffset, x, y);
		x += 12;
	}
	if((pCharacter->m_LaserHitDisabled && pCharacter->m_aWeapons[WEAPON_LASER].m_Got))
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudLaserHitDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_LaserHitDisabledOffset, x, y);
	}
	MaxX = std::max(MaxX, x + 12.0f);
	MaxY = std::max(MaxY, y + 12.0f);

	// render dummy actions and freeze state
	x = BaseX;
	if(HasProhibitedCapabilities)
	{
		y += 12;
	}
	if(GameClient()->m_Snap.m_aCharacters[ClientId].m_HasExtendedDisplayInfo && GameClient()->m_Snap.m_aCharacters[ClientId].m_ExtendedData.m_Flags & CHARACTERFLAG_LOCK_MODE)
	{
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudLockMode);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_LockModeOffset, x, y);
		x += 12;
	}
	if(GameClient()->m_Snap.m_aCharacters[ClientId].m_HasExtendedDisplayInfo && GameClient()->m_Snap.m_aCharacters[ClientId].m_ExtendedData.m_Flags & CHARACTERFLAG_PRACTICE_MODE)
	{
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudPracticeMode);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_PracticeModeOffset, x, y);
		x += 12;
	}
	if(GameClient()->m_Snap.m_aCharacters[ClientId].m_HasExtendedDisplayInfo && GameClient()->m_Snap.m_aCharacters[ClientId].m_ExtendedData.m_Flags & CHARACTERFLAG_TEAM0_MODE)
	{
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudTeam0Mode);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_Team0ModeOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_DeepFrozen)
	{
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudDeepFrozen);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_DeepFrozenOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_LiveFrozen)
	{
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudLiveFrozen);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_LiveFrozenOffset, x, y);
	}
	MaxX = std::max(MaxX, x + 12.0f);
	MaxY = std::max(MaxY, y + 12.0f);
	m_PlayerStateBounds = HudLayout::ClampRectToScreen({BaseX, BaseY, std::max(12.0f, MaxX - BaseX) * ModuleScale, std::max(12.0f, MaxY - BaseY) * ModuleScale, 0.0f}, m_Width, m_Height);
	Graphics()->MapScreen(0.0f, 0.0f, m_Width, m_Height);
}

void CHud::RenderNinjaBarPos(const float x, float y, const float Width, const float Height, float Progress, const float Alpha)
{
	Progress = std::clamp(Progress, 0.0f, 1.0f);

	// what percentage of the end pieces is used for the progress indicator and how much is the rest
	// half of the ends are used for the progress display
	const float RestPct = 0.5f;
	const float ProgPct = 0.5f;

	const float EndHeight = Width; // to keep the correct scale - the width of the sprite is as long as the height
	const float BarWidth = Width;
	const float WholeBarHeight = Height;
	const float MiddleBarHeight = WholeBarHeight - (EndHeight * 2.0f);
	const float EndProgressHeight = EndHeight * ProgPct;
	const float EndRestHeight = EndHeight * RestPct;
	const float ProgressBarHeight = WholeBarHeight - (EndProgressHeight * 2.0f);
	const float EndProgressProportion = EndProgressHeight / ProgressBarHeight;
	const float MiddleProgressProportion = MiddleBarHeight / ProgressBarHeight;

	// beginning piece
	float BeginningPieceProgress = 1;
	if(Progress <= 1)
	{
		if(Progress <= (EndProgressProportion + MiddleProgressProportion))
		{
			BeginningPieceProgress = 0;
		}
		else
		{
			BeginningPieceProgress = (Progress - EndProgressProportion - MiddleProgressProportion) / EndProgressProportion;
		}
	}
	// empty
	Graphics()->WrapClamp();
	Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudNinjaBarEmptyRight);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.f, 1.f, 1.f, Alpha);
	// Subset: btm_r, top_r, top_m, btm_m | it is mirrored on the horizontal axe and rotated 90 degrees counterclockwise
	Graphics()->QuadsSetSubsetFree(1, 1, 1, 0, ProgPct - ProgPct * (1.0f - BeginningPieceProgress), 0, ProgPct - ProgPct * (1.0f - BeginningPieceProgress), 1);
	IGraphics::CQuadItem QuadEmptyBeginning(x, y, BarWidth, EndRestHeight + EndProgressHeight * (1.0f - BeginningPieceProgress));
	Graphics()->QuadsDrawTL(&QuadEmptyBeginning, 1);
	Graphics()->QuadsEnd();
	// full
	if(BeginningPieceProgress > 0.0f)
	{
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudNinjaBarFullLeft);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.f, 1.f, 1.f, Alpha);
		// Subset: btm_m, top_m, top_r, btm_r | it is rotated 90 degrees clockwise
		Graphics()->QuadsSetSubsetFree(RestPct + ProgPct * (1.0f - BeginningPieceProgress), 1, RestPct + ProgPct * (1.0f - BeginningPieceProgress), 0, 1, 0, 1, 1);
		IGraphics::CQuadItem QuadFullBeginning(x, y + (EndRestHeight + EndProgressHeight * (1.0f - BeginningPieceProgress)), BarWidth, EndProgressHeight * BeginningPieceProgress);
		Graphics()->QuadsDrawTL(&QuadFullBeginning, 1);
		Graphics()->QuadsEnd();
	}

	// middle piece
	y += EndHeight;

	float MiddlePieceProgress = 1;
	if(Progress <= EndProgressProportion + MiddleProgressProportion)
	{
		if(Progress <= EndProgressProportion)
		{
			MiddlePieceProgress = 0;
		}
		else
		{
			MiddlePieceProgress = (Progress - EndProgressProportion) / MiddleProgressProportion;
		}
	}

	const float FullMiddleBarHeight = MiddleBarHeight * MiddlePieceProgress;
	const float EmptyMiddleBarHeight = MiddleBarHeight - FullMiddleBarHeight;

	// empty ninja bar
	if(EmptyMiddleBarHeight > 0.0f)
	{
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudNinjaBarEmpty);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.f, 1.f, 1.f, Alpha);
		// select the middle portion of the sprite so we don't get edge bleeding
		if(EmptyMiddleBarHeight <= EndHeight)
		{
			// prevent pixel puree, select only a small slice
			// Subset: btm_r, top_r, top_m, btm_m | it is mirrored on the horizontal axe and rotated 90 degrees counterclockwise
			Graphics()->QuadsSetSubsetFree(1, 1, 1, 0, 1.0f - (EmptyMiddleBarHeight / EndHeight), 0, 1.0f - (EmptyMiddleBarHeight / EndHeight), 1);
		}
		else
		{
			// Subset: btm_r, top_r, top_l, btm_l | it is mirrored on the horizontal axe and rotated 90 degrees counterclockwise
			Graphics()->QuadsSetSubsetFree(1, 1, 1, 0, 0, 0, 0, 1);
		}
		IGraphics::CQuadItem QuadEmpty(x, y, BarWidth, EmptyMiddleBarHeight);
		Graphics()->QuadsDrawTL(&QuadEmpty, 1);
		Graphics()->QuadsEnd();
	}

	// full ninja bar
	Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudNinjaBarFull);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.f, 1.f, 1.f, Alpha);
	// select the middle portion of the sprite so we don't get edge bleeding
	if(FullMiddleBarHeight <= EndHeight)
	{
		// prevent pixel puree, select only a small slice
		// Subset: btm_m, top_m, top_r, btm_r | it is rotated 90 degrees clockwise
		Graphics()->QuadsSetSubsetFree(1.0f - (FullMiddleBarHeight / EndHeight), 1, 1.0f - (FullMiddleBarHeight / EndHeight), 0, 1, 0, 1, 1);
	}
	else
	{
		// Subset: btm_l, top_l, top_r, btm_r | it is rotated 90 degrees clockwise
		Graphics()->QuadsSetSubsetFree(0, 1, 0, 0, 1, 0, 1, 1);
	}
	IGraphics::CQuadItem QuadFull(x, y + EmptyMiddleBarHeight, BarWidth, FullMiddleBarHeight);
	Graphics()->QuadsDrawTL(&QuadFull, 1);
	Graphics()->QuadsEnd();

	// ending piece
	y += MiddleBarHeight;
	float EndingPieceProgress = 1;
	if(Progress <= EndProgressProportion)
	{
		EndingPieceProgress = Progress / EndProgressProportion;
	}
	// empty
	if(EndingPieceProgress < 1.0f)
	{
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudNinjaBarEmptyRight);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.f, 1.f, 1.f, Alpha);
		// Subset: btm_l, top_l, top_m, btm_m | it is rotated 90 degrees clockwise
		Graphics()->QuadsSetSubsetFree(0, 1, 0, 0, ProgPct - ProgPct * EndingPieceProgress, 0, ProgPct - ProgPct * EndingPieceProgress, 1);
		IGraphics::CQuadItem QuadEmptyEnding(x, y, BarWidth, EndProgressHeight * (1.0f - EndingPieceProgress));
		Graphics()->QuadsDrawTL(&QuadEmptyEnding, 1);
		Graphics()->QuadsEnd();
	}
	// full
	Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudNinjaBarFullLeft);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.f, 1.f, 1.f, Alpha);
	// Subset: btm_m, top_m, top_l, btm_l | it is mirrored on the horizontal axe and rotated 90 degrees counterclockwise
	Graphics()->QuadsSetSubsetFree(RestPct + ProgPct * EndingPieceProgress, 1, RestPct + ProgPct * EndingPieceProgress, 0, 0, 0, 0, 1);
	IGraphics::CQuadItem QuadFullEnding(x, y + (EndProgressHeight * (1.0f - EndingPieceProgress)), BarWidth, EndRestHeight + EndProgressHeight * EndingPieceProgress);
	Graphics()->QuadsDrawTL(&QuadFullEnding, 1);
	Graphics()->QuadsEnd();

	Graphics()->QuadsSetSubset(0, 0, 1, 1);
	Graphics()->SetColor(1.f, 1.f, 1.f, 1.f);
	Graphics()->WrapNormal();
}

void CHud::RenderSpectatorCount()
{
	if(!g_Config.m_ClShowhudSpectatorCount)
	{
		return;
	}

	int Count = 0;
	if(Client()->IsSixup())
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(i == GameClient()->m_aLocalIds[0] || (GameClient()->Client()->DummyConnected() && i == GameClient()->m_aLocalIds[1]))
				continue;

			if(Client()->m_TranslationContext.m_aClients[i].m_PlayerFlags7 & protocol7::PLAYERFLAG_WATCHING)
			{
				Count++;
			}
		}
	}
	else
	{
		const CNetObj_SpectatorCount *pSpectatorCount = GameClient()->m_Snap.m_pSpectatorCount;
		if(!pSpectatorCount)
		{
			m_LastSpectatorCountTick = Client()->GameTick(g_Config.m_ClDummy);
			return;
		}
		Count = pSpectatorCount->m_NumSpectators;
	}

	if(Count == 0)
	{
		m_LastSpectatorCountTick = Client()->GameTick(g_Config.m_ClDummy);
		return;
	}

	// 1 second delay
	if(Client()->GameTick(g_Config.m_ClDummy) < m_LastSpectatorCountTick + Client()->GameTickSpeed())
		return;

	char aBuf[16];
	str_format(aBuf, sizeof(aBuf), "%d", Count);

	const float Fontsize = 6.0f;
	const float BoxHeight = 14.f;
	const float BoxWidth = 13.f + TextRender()->TextWidth(Fontsize, aBuf);

	float StartX = m_Width - BoxWidth;
	float StartY = 285.0f - BoxHeight - 4; // 4 units distance to the next display;
	if(g_Config.m_ClShowhudPlayerPosition || g_Config.m_ClShowhudPlayerSpeed || g_Config.m_ClShowhudPlayerAngle)
	{
		StartY -= 4;
	}
	StartY -= GetMovementInformationBoxHeight();

	if(g_Config.m_ClShowhudScore)
	{
		StartY -= 56;
	}

	if(g_Config.m_ClShowhudDummyActions && !(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER) && Client()->DummyConnected())
	{
		StartY = StartY - 29.0f - 4; // dummy actions height and padding
	}
	Graphics()->DrawRect(StartX, StartY, BoxWidth, BoxHeight, ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_L, 5.0f);

	float y = StartY + BoxHeight / 3;
	float x = StartX + 2;

	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->Text(x, y, Fontsize, FontIcon::EYE, -1.0f);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	TextRender()->Text(x + Fontsize + 3.f, y, Fontsize, aBuf, -1.0f);
}

void CHud::RenderDummyActions()
{
	m_DummyActionsBounds = {};
	if(!HudLayout::IsEnabled(HudLayout::MODULE_DUMMY_ACTIONS))
		return;
	if((GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER) || !Client()->DummyConnected())
	{
		return;
	}
	// render small dummy actions hud
	const auto Layout = HudLayout::Get(HudLayout::MODULE_DUMMY_ACTIONS, m_Width, m_Height);
	const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	const float BoxHeight = 29.0f * Scale;
	const float BoxWidth = 16.0f * Scale;

	const float StartX = Layout.m_X;
	const float StartY = Layout.m_Y;

	m_DummyActionsBounds = HudLayout::ClampRectToScreen({StartX, StartY, BoxWidth, BoxHeight, 5.0f * Scale}, m_Width, m_Height);
	const float DrawX = m_DummyActionsBounds.m_X;
	const float DrawY = m_DummyActionsBounds.m_Y;
	Graphics()->DrawRect(DrawX, DrawY, BoxWidth, BoxHeight, ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_L, 5.0f * Scale);

	float y = DrawY + 2.0f * Scale;
	float x = DrawX + 2.0f * Scale;
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.4f);
	if(g_Config.m_ClDummyHammer)
	{
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	}
	Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudDummyHammer);
	Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_DummyHammerOffset, x, y, Scale, Scale);
	y += 13.0f * Scale;
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.4f);
	if(g_Config.m_ClDummyCopyMoves)
	{
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	}
	Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudDummyCopy);
	Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_DummyCopyOffset, x, y, Scale, Scale);
}

inline int CHud::GetDigitsIndex(int Value, int Max)
{
	if(Value < 0)
	{
		Value *= -1;
	}
	int DigitsIndex = std::log10((Value ? Value : 1));
	if(DigitsIndex > Max)
	{
		DigitsIndex = Max;
	}
	if(DigitsIndex < 0)
	{
		DigitsIndex = 0;
	}
	return DigitsIndex;
}

inline float CHud::GetMovementInformationBoxHeight()
{
	if(GameClient()->m_Snap.m_SpecInfo.m_Active && (GameClient()->m_Snap.m_SpecInfo.m_SpectatorId == SPEC_FREEVIEW || GameClient()->m_aClients[GameClient()->m_Snap.m_SpecInfo.m_SpectatorId].m_SpecCharPresent))
		return g_Config.m_ClShowhudPlayerPosition ? 3.0f * MOVEMENT_INFORMATION_LINE_HEIGHT + 2.0f : 0.0f;
	float BoxHeight = 3.0f * MOVEMENT_INFORMATION_LINE_HEIGHT * (g_Config.m_ClShowhudPlayerPosition + g_Config.m_ClShowhudPlayerSpeed) + 2.0f * MOVEMENT_INFORMATION_LINE_HEIGHT * g_Config.m_ClShowhudPlayerAngle;
	if(g_Config.m_ClShowhudPlayerPosition || g_Config.m_ClShowhudPlayerSpeed || g_Config.m_ClShowhudPlayerAngle)
	{
		BoxHeight += 2.0f;
	}
	return BoxHeight;
}

void CHud::UpdateMovementInformationTextContainer(STextContainerIndex &TextContainer, float FontSize, float Value, float &PrevValue)
{
	Value = std::round(Value * 100.0f) / 100.0f; // Round to 2dp
	if(TextContainer.Valid() && PrevValue == Value)
		return;
	PrevValue = Value;

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "%.2f", Value);

	CTextCursor Cursor;
	Cursor.m_FontSize = FontSize;
	TextRender()->RecreateTextContainer(TextContainer, &Cursor, aBuf);
}

void CHud::RenderMovementInformationTextContainer(STextContainerIndex &TextContainer, const ColorRGBA &Color, float X, float Y)
{
	if(TextContainer.Valid())
	{
		TextRender()->RenderTextContainer(TextContainer, Color, TextRender()->DefaultTextOutlineColor(), X - TextRender()->GetBoundingBoxTextContainer(TextContainer).m_W, Y);
	}
}

CHud::CMovementInformation CHud::GetMovementInformation(int ClientId, int Conn) const
{
	CMovementInformation Out;
	if(ClientId == SPEC_FREEVIEW)
	{
		Out.m_Pos = GameClient()->m_Camera.m_Center / 32.0f;
	}
	else if(GameClient()->m_aClients[ClientId].m_SpecCharPresent)
	{
		Out.m_Pos = GameClient()->m_aClients[ClientId].m_SpecChar / 32.0f;
	}
	else
	{
		const CNetObj_Character *pPrevChar = &GameClient()->m_Snap.m_aCharacters[ClientId].m_Prev;
		const CNetObj_Character *pCurChar = &GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur;
		const float IntraTick = Client()->IntraGameTick(Conn);

		// To make the player position relative to blocks we need to divide by the block size
		Out.m_Pos = mix(vec2(pPrevChar->m_X, pPrevChar->m_Y), vec2(pCurChar->m_X, pCurChar->m_Y), IntraTick) / 32.0f;

		const vec2 Vel = mix(vec2(pPrevChar->m_VelX, pPrevChar->m_VelY), vec2(pCurChar->m_VelX, pCurChar->m_VelY), IntraTick);

		float VelspeedX = Vel.x / 256.0f * Client()->GameTickSpeed();
		if(Vel.x >= -1.0f && Vel.x <= 1.0f)
		{
			VelspeedX = 0.0f;
		}
		float VelspeedY = Vel.y / 256.0f * Client()->GameTickSpeed();
		if(Vel.y >= -128.0f && Vel.y <= 128.0f)
		{
			VelspeedY = 0.0f;
		}
		// We show the speed in Blocks per Second (Bps) and therefore have to divide by the block size
		Out.m_Speed.x = VelspeedX / 32.0f;
		float VelspeedLength = length(vec2(Vel.x, Vel.y) / 256.0f) * Client()->GameTickSpeed();
		// Todo: Use Velramp tuning of each individual player
		// Since these tuning parameters are almost never changed, the default values are sufficient in most cases
		float Ramp = VelocityRamp(VelspeedLength, GameClient()->m_aTuning[Conn].m_VelrampStart, GameClient()->m_aTuning[Conn].m_VelrampRange, GameClient()->m_aTuning[Conn].m_VelrampCurvature);
		Out.m_Speed.x *= Ramp;
		Out.m_Speed.y = VelspeedY / 32.0f;

		float Angle = GameClient()->m_Players.GetPlayerTargetAngle(pPrevChar, pCurChar, ClientId, IntraTick);
		if(Angle < 0.0f)
		{
			Angle += 2.0f * pi;
		}
		Out.m_Angle = Angle * 180.0f / pi;
	}
	return Out;
}

void CHud::RenderMovementInformation()
{
	m_MovementInformationBounds = {};
	if(!HudLayout::IsEnabled(HudLayout::MODULE_MOVEMENT_INFO))
		return;
	const auto Layout = HudLayout::Get(HudLayout::MODULE_MOVEMENT_INFO, m_Width, m_Height);
	const float ModuleScale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	const int ClientId = GameClient()->m_Snap.m_SpecInfo.m_Active ? GameClient()->m_Snap.m_SpecInfo.m_SpectatorId : GameClient()->m_Snap.m_LocalClientId;
	const bool PosOnly = ClientId == SPEC_FREEVIEW || (GameClient()->m_aClients[ClientId].m_SpecCharPresent);
	// Draw the information depending on settings: Position, speed and target angle
	// This display is only to present the available information from the last snapshot, not to interpolate or predict
	if(!g_Config.m_ClShowhudPlayerPosition && (PosOnly || (!g_Config.m_ClShowhudPlayerSpeed && !g_Config.m_ClShowhudPlayerAngle)))
	{
		return;
	}
	const float LineSpacer = 1.0f; // above and below each entry
	const float Fontsize = 6.0f;

	float BoxHeight = GetMovementInformationBoxHeight();
	bool HasDummyInfo = false;
	CMovementInformation DummyInfo{};

	if(Client()->DummyConnected())
	{
		int DummyClientId = -1;

		if(GameClient()->m_Snap.m_SpecInfo.m_Active)
		{
			const int SpectId = GameClient()->m_Snap.m_SpecInfo.m_SpectatorId;

			if(SpectId == GameClient()->m_aLocalIds[0])
			{
				DummyClientId = GameClient()->m_aLocalIds[1];
			}
			else if(SpectId == GameClient()->m_aLocalIds[1])
			{
				DummyClientId = GameClient()->m_aLocalIds[0];
			}
			else
			{
				DummyClientId = GameClient()->m_aLocalIds[1 - (g_Config.m_ClDummy ? 1 : 0)];
			}
		}
		else
		{
			DummyClientId = GameClient()->m_aLocalIds[1 - (g_Config.m_ClDummy ? 1 : 0)];
		}

		if(DummyClientId >= 0 && DummyClientId < MAX_CLIENTS &&
			GameClient()->m_aClients[DummyClientId].m_Active)
		{
			DummyInfo = GetMovementInformation(
				DummyClientId,
				DummyClientId == GameClient()->m_aLocalIds[1]);
			HasDummyInfo = true;
		}
	}

	const bool ShowDummyPos = HasDummyInfo && g_Config.m_ClShowhudPlayerPosition && g_Config.m_TcShowhudDummyPosition;
	const bool ShowDummySpeed = HasDummyInfo && !PosOnly && g_Config.m_ClShowhudPlayerSpeed && g_Config.m_TcShowhudDummySpeed;
	const bool ShowDummyAngle = HasDummyInfo && !PosOnly && g_Config.m_ClShowhudPlayerAngle && g_Config.m_TcShowhudDummyAngle;

	if(ShowDummyPos)
		BoxHeight += 2.0f * MOVEMENT_INFORMATION_LINE_HEIGHT;
	if(ShowDummySpeed)
		BoxHeight += 2.0f * MOVEMENT_INFORMATION_LINE_HEIGHT;
	if(ShowDummyAngle)
		BoxHeight += 1.0f * MOVEMENT_INFORMATION_LINE_HEIGHT;

	const float BoxWidth = 62.0f;

	// The renderer and editor both consume the same resolved layout, including
	// scale-aware dynamic defaults. This prevents Scale from moving the visual
	// panel to a different anchor than its editor frame.
	float StartX = Layout.m_X;
	float StartY = Layout.m_Y;

	m_MovementInformationBounds = HudLayout::ClampRectToScreen({StartX, StartY, BoxWidth * ModuleScale, BoxHeight * ModuleScale, 5.0f * ModuleScale}, m_Width, m_Height);
	StartX = m_MovementInformationBounds.m_X;
	StartY = m_MovementInformationBounds.m_Y;
	MapHudModuleScale(Graphics(), m_Width, m_Height, ModuleScale, StartX, StartY);
	const int Corners = HudLayout::BackgroundCorners(IGraphics::CORNER_ALL, StartX, StartY, BoxWidth * ModuleScale, BoxHeight * ModuleScale, m_Width, m_Height);
	Graphics()->DrawRect(StartX, StartY, BoxWidth, BoxHeight, ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), Corners, 5.0f);

	const CMovementInformation Info = GetMovementInformation(ClientId, g_Config.m_ClDummy);

	float y = StartY + LineSpacer * 2.0f;
	const float LeftX = StartX + 2.0f;
	const float RightX = StartX + BoxWidth - 2.0f;

	if(g_Config.m_ClShowhudPlayerPosition)
	{
		TextRender()->Text(LeftX, y, Fontsize, Localize("Position:"), -1.0f);
		y += MOVEMENT_INFORMATION_LINE_HEIGHT;

		TextRender()->Text(LeftX, y, Fontsize, "X:", -1.0f);
		UpdateMovementInformationTextContainer(m_aPlayerPositionContainers[0], Fontsize, Info.m_Pos.x, m_aPlayerPrevPosition[0]);

		ColorRGBA TextColor = TextRender()->DefaultTextColor();
		if(ShowDummyPos && fabsf(Info.m_Pos.x - DummyInfo.m_Pos.x) < 0.01f)
			TextColor = ColorRGBA(0.2f, 1.0f, 0.2f, 1.0f);

		RenderMovementInformationTextContainer(m_aPlayerPositionContainers[0], TextColor, RightX, y);
		y += MOVEMENT_INFORMATION_LINE_HEIGHT;

		TextRender()->Text(LeftX, y, Fontsize, "Y:", -1.0f);
		UpdateMovementInformationTextContainer(m_aPlayerPositionContainers[1], Fontsize, Info.m_Pos.y, m_aPlayerPrevPosition[1]);
		RenderMovementInformationTextContainer(m_aPlayerPositionContainers[1], TextRender()->DefaultTextColor(), RightX, y);
		y += MOVEMENT_INFORMATION_LINE_HEIGHT;

		if(ShowDummyPos)
		{
			char aBuf[32];

			TextRender()->Text(LeftX, y, Fontsize, "DX:", -1.0f);
			str_format(aBuf, sizeof(aBuf), "%.2f", DummyInfo.m_Pos.x);

			ColorRGBA DummyTextColor = TextRender()->DefaultTextColor();
			if(fabsf(Info.m_Pos.x - DummyInfo.m_Pos.x) < 0.01f)
				DummyTextColor = ColorRGBA(0.2f, 1.0f, 0.2f, 1.0f);

			TextRender()->TextColor(DummyTextColor);
			TextRender()->Text(RightX - TextRender()->TextWidth(Fontsize, aBuf), y, Fontsize, aBuf, -1.0f);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
			y += MOVEMENT_INFORMATION_LINE_HEIGHT;

			TextRender()->Text(LeftX, y, Fontsize, "DY:", -1.0f);
			str_format(aBuf, sizeof(aBuf), "%.2f", DummyInfo.m_Pos.y);
			TextRender()->Text(RightX - TextRender()->TextWidth(Fontsize, aBuf), y, Fontsize, aBuf, -1.0f);
			y += MOVEMENT_INFORMATION_LINE_HEIGHT;
		}
	}

	if(PosOnly)
	{
		Graphics()->MapScreen(0.0f, 0.0f, m_Width, m_Height);
		return;
	}

	if(g_Config.m_ClShowhudPlayerSpeed)
	{
		TextRender()->Text(LeftX, y, Fontsize, Localize("Speed:"), -1.0f);
		y += MOVEMENT_INFORMATION_LINE_HEIGHT;

		const char aaCoordinates[][4] = {"X:", "Y:"};
		for(int i = 0; i < 2; i++)
		{
			ColorRGBA Color(1.0f, 1.0f, 1.0f, 1.0f);
			if(m_aLastPlayerSpeedChange[i] == ESpeedChange::INCREASE)
				Color = ColorRGBA(0.0f, 1.0f, 0.0f, 1.0f);
			if(m_aLastPlayerSpeedChange[i] == ESpeedChange::DECREASE)
				Color = ColorRGBA(1.0f, 0.5f, 0.5f, 1.0f);
			TextRender()->Text(LeftX, y, Fontsize, aaCoordinates[i], -1.0f);
			UpdateMovementInformationTextContainer(m_aPlayerSpeedTextContainers[i], Fontsize, i == 0 ? Info.m_Speed.x : Info.m_Speed.y, m_aPlayerPrevSpeed[i]);
			RenderMovementInformationTextContainer(m_aPlayerSpeedTextContainers[i], Color, RightX, y);
			y += MOVEMENT_INFORMATION_LINE_HEIGHT;
		}

		if(ShowDummySpeed)
		{
			char aBuf[32];

			TextRender()->Text(LeftX, y, Fontsize, "DX:", -1.0f);
			str_format(aBuf, sizeof(aBuf), "%.2f", DummyInfo.m_Speed.x);
			TextRender()->Text(RightX - TextRender()->TextWidth(Fontsize, aBuf), y, Fontsize, aBuf, -1.0f);
			y += MOVEMENT_INFORMATION_LINE_HEIGHT;

			TextRender()->Text(LeftX, y, Fontsize, "DY:", -1.0f);
			str_format(aBuf, sizeof(aBuf), "%.2f", DummyInfo.m_Speed.y);
			TextRender()->Text(RightX - TextRender()->TextWidth(Fontsize, aBuf), y, Fontsize, aBuf, -1.0f);
			y += MOVEMENT_INFORMATION_LINE_HEIGHT;
		}

		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	}

	if(g_Config.m_ClShowhudPlayerAngle)
	{
		TextRender()->Text(LeftX, y, Fontsize, Localize("Angle:"), -1.0f);
		y += MOVEMENT_INFORMATION_LINE_HEIGHT;

		UpdateMovementInformationTextContainer(m_PlayerAngleTextContainerIndex, Fontsize, Info.m_Angle, m_PlayerPrevAngle);
		RenderMovementInformationTextContainer(m_PlayerAngleTextContainerIndex, TextRender()->DefaultTextColor(), RightX, y);
		y += MOVEMENT_INFORMATION_LINE_HEIGHT;

		if(ShowDummyAngle)
		{
			char aBuf[32];

			TextRender()->Text(LeftX, y, Fontsize, "DA:", -1.0f);
			str_format(aBuf, sizeof(aBuf), "%.2f", DummyInfo.m_Angle);
			TextRender()->Text(RightX - TextRender()->TextWidth(Fontsize, aBuf), y, Fontsize, aBuf, -1.0f);
		}
	}
	Graphics()->MapScreen(0.0f, 0.0f, m_Width, m_Height);
}

void CHud::RenderSpectatorHud()
{
	if(!g_Config.m_ClShowhudSpectator)
		return;

	// TClient
	float AdjustedHeight = m_Height - (g_Config.m_TcStatusBar ? g_Config.m_TcStatusBarHeight : 0.0f);
	// draw the box
	Graphics()->DrawRect(m_Width - 180.0f, AdjustedHeight - 15.0f, 180.0f, 15.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_TL, 5.0f);

	// draw the text
	char aBuf[128];
	if(GameClient()->m_MultiViewActivated)
	{
		str_copy(aBuf, Localize("Multi-View"));
	}
	else if(GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW)
	{
		const auto &Player = GameClient()->m_aClients[GameClient()->m_Snap.m_SpecInfo.m_SpectatorId];
		if(g_Config.m_ClShowIds)
			str_format(aBuf, sizeof(aBuf), Localize("Following %d: %s", "Spectating"), Player.ClientId(), Player.m_aName);
		else
			str_format(aBuf, sizeof(aBuf), Localize("Following %s", "Spectating"), Player.m_aName);
	}
	else
	{
		str_copy(aBuf, Localize("Free-View"));
	}
	TextRender()->Text(m_Width - 174.0f, AdjustedHeight - 15.0f + (15.f - 8.f) / 2.f, 8.0f, aBuf, -1.0f);

	// draw the camera info
	if(Client()->State() != IClient::STATE_DEMOPLAYBACK && GameClient()->m_Camera.SpectatingPlayer() && GameClient()->m_Camera.CanUseAutoSpecCamera() && g_Config.m_ClSpecAutoSync)
	{
		bool AutoSpecCameraEnabled = GameClient()->m_Camera.m_AutoSpecCamera;
		const char *pLabelText = Localize("AUTO", "Spectating Camera Mode Icon");
		const float TextWidth = TextRender()->TextWidth(6.0f, pLabelText);

		constexpr float RightMargin = 4.0f;
		constexpr float IconWidth = 6.0f;
		constexpr float Padding = 3.0f;
		const float TagWidth = IconWidth + TextWidth + Padding * 3.0f;
		const float TagX = m_Width - RightMargin - TagWidth;
		Graphics()->DrawRect(TagX, m_Height - 12.0f, TagWidth, 10.0f, ColorRGBA(1.0f, 1.0f, 1.0f, AutoSpecCameraEnabled ? 0.50f : 0.10f), IGraphics::CORNER_ALL, 2.5f);
		TextRender()->TextColor(1, 1, 1, AutoSpecCameraEnabled ? 1.0f : 0.65f);
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->Text(TagX + Padding, m_Height - 10.0f, 6.0f, FontIcon::CAMERA, -1.0f);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		TextRender()->Text(TagX + Padding + IconWidth + Padding, m_Height - 10.0f, 6.0f, pLabelText, -1.0f);
		TextRender()->TextColor(1, 1, 1, 1);
	}
}

HudLayout::SModuleRect CHud::GetLocalTimeBounds() const
{
	const bool Seconds = g_Config.m_TcShowLocalTimeSeconds; // TClient
	char aTimeStr[16];
	str_timestamp_format(aTimeStr, sizeof(aTimeStr), Seconds ? "%H:%M.%S" : "%H:%M");
	const float TextWidth = std::round(TextRender()->TextBoundingBox(5.0f, aTimeStr).m_W);
	const auto Layout = HudLayout::Get(HudLayout::MODULE_LOCAL_TIME, m_Width, m_Height);
	const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	const float Width = (TextWidth + 10.0f) * Scale;
	const float Height = 12.5f * Scale;

	// The old renderer was right-anchored at 3/7 of the HUD width. Preserve
	// that natural default, while every editor drag uses the same top-left
	// position semantics as Key Indicator and the other regular HUD modules.
	const float X = HudLayout::HasPositionOverride(HudLayout::MODULE_LOCAL_TIME) ?
		Layout.m_X :
		(m_Width / 7.0f) * 3.0f - (TextWidth + 15.0f) * Scale;
	const float Y = HudLayout::HasPositionOverride(HudLayout::MODULE_LOCAL_TIME) ? Layout.m_Y : 0.0f;
	return HudLayout::ClampRectToScreen({X, Y, Width, Height, 3.75f * Scale}, m_Width, m_Height);
}

void CHud::RenderLocalTime(float x)
{
	const bool EditorPreview = GameClient()->m_AmfHudEditor.IsActive();
	if(!g_Config.m_ClShowLocalTimeAlways && !GameClient()->m_Scoreboard.IsActive() && !EditorPreview)
		return;

	(void)x;
	const bool Seconds = g_Config.m_TcShowLocalTimeSeconds; // TClient
	char aTimeStr[16];
	str_timestamp_format(aTimeStr, sizeof(aTimeStr), Seconds ? "%H:%M.%S" : "%H:%M");
	const HudLayout::SModuleRect Bounds = GetLocalTimeBounds();
	const auto Layout = HudLayout::Get(HudLayout::MODULE_LOCAL_TIME, m_Width, m_Height);
	const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);

	// Map the existing renderer around the same final top-left rect that is
	// published to the HUD editor. One layout therefore owns runtime drawing,
	// hit-testing and the selection frame.
	MapHudModuleScale(Graphics(), m_Width, m_Height, Scale, Bounds.m_X, Bounds.m_Y);
	const float BaseWidth = Bounds.m_W / Scale;
	const float BaseHeight = Bounds.m_H / Scale;
	// Use the same edge-aware corner mask as Movement Info and Score. The
	// authoritative final bounds decide which side touches the screen; a free
	// Local Time pill is fully rounded, while a touching side is square.
	const int Corners = HudLayout::BackgroundCorners(IGraphics::CORNER_ALL, Bounds.m_X, Bounds.m_Y, Bounds.m_W, Bounds.m_H, m_Width, m_Height);
	Graphics()->DrawRect(Bounds.m_X, Bounds.m_Y, BaseWidth, BaseHeight, ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), Corners, 3.75f);
	TextRender()->Text(Bounds.m_X + 5.0f, Bounds.m_Y + (BaseHeight - 5.0f) * 0.5f, 5.0f, aTimeStr, -1.0f);
	Graphics()->MapScreen(0.0f, 0.0f, m_Width, m_Height);
}

void CHud::OnNewSnapshot()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(!GameClient()->m_Snap.m_pGameInfoObj)
		return;

	int ClientId = -1;
	if(GameClient()->m_Snap.m_pLocalCharacter && !GameClient()->m_Snap.m_SpecInfo.m_Active && !(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER))
		ClientId = GameClient()->m_Snap.m_LocalClientId;
	else if(GameClient()->m_Snap.m_SpecInfo.m_Active)
		ClientId = GameClient()->m_Snap.m_SpecInfo.m_SpectatorId;

	if(ClientId == -1)
		return;

	const CNetObj_Character *pPrevChar = &GameClient()->m_Snap.m_aCharacters[ClientId].m_Prev;
	const CNetObj_Character *pCurChar = &GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur;
	const float IntraTick = Client()->IntraGameTick(g_Config.m_ClDummy);
	ivec2 Vel = mix(ivec2(pPrevChar->m_VelX, pPrevChar->m_VelY), ivec2(pCurChar->m_VelX, pCurChar->m_VelY), IntraTick);

	CCharacter *pChar = GameClient()->m_PredictedWorld.GetCharacterById(ClientId);
	if(pChar && pChar->IsGrounded())
		Vel.y = 0;

	int aVels[2] = {Vel.x, Vel.y};

	for(int i = 0; i < 2; i++)
	{
		int AbsVel = abs(aVels[i]);
		if(AbsVel > m_aPlayerSpeed[i])
		{
			m_aLastPlayerSpeedChange[i] = ESpeedChange::INCREASE;
		}
		if(AbsVel < m_aPlayerSpeed[i])
		{
			m_aLastPlayerSpeedChange[i] = ESpeedChange::DECREASE;
		}
		if(AbsVel < 2)
		{
			m_aLastPlayerSpeedChange[i] = ESpeedChange::NONE;
		}
		m_aPlayerSpeed[i] = AbsVel;
	}
}

void CHud::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(!GameClient()->m_Snap.m_pGameInfoObj)
		return;

	m_Width = 300.0f * Graphics()->ScreenAspect();
	m_Height = 300.0f;
	Graphics()->MapScreen(0.0f, 0.0f, m_Width, m_Height);

#if defined(CONF_VIDEORECORDER)
	if((IVideo::Current() && g_Config.m_ClVideoShowhud) || (!IVideo::Current() && g_Config.m_ClShowhud))
#else
	if(g_Config.m_ClShowhud)
#endif
	{
		const bool FocusModeActive = g_Config.m_AmfFocusMode != 0;
		const bool HideHudInFocusMode = g_Config.m_AmfFocusModeHideHud != 0;
		const bool HideUiInFocusMode = g_Config.m_AmfFocusModeHideUi != 0;
		if(FocusModeActive && HideHudInFocusMode)
		{
			RenderCursor();
			return;
		}

		if(GameClient()->m_Snap.m_pLocalCharacter && !GameClient()->m_Snap.m_SpecInfo.m_Active && !(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER))
		{
			if(g_Config.m_ClShowhudHealthAmmo)
			{
				RenderAmmoHealthAndArmor(GameClient()->m_Snap.m_pLocalCharacter);
			}
			if(GameClient()->m_Snap.m_aCharacters[GameClient()->m_Snap.m_LocalClientId].m_HasExtendedData && g_Config.m_ClShowhudDDRace && GameClient()->m_GameInfo.m_HudDDRace)
			{
				RenderPlayerState(GameClient()->m_Snap.m_LocalClientId);
			}
			if(!(FocusModeActive && HideUiInFocusMode))
				RenderSpectatorCount();
			RenderMovementInformation();
			RenderDDRaceEffects();
		}
		else if(GameClient()->m_Snap.m_SpecInfo.m_Active)
		{
			int SpectatorId = GameClient()->m_Snap.m_SpecInfo.m_SpectatorId;
			if(SpectatorId != SPEC_FREEVIEW && g_Config.m_ClShowhudHealthAmmo)
			{
				RenderAmmoHealthAndArmor(&GameClient()->m_Snap.m_aCharacters[SpectatorId].m_Cur);
			}
			if(SpectatorId != SPEC_FREEVIEW &&
				GameClient()->m_Snap.m_aCharacters[SpectatorId].m_HasExtendedData &&
				g_Config.m_ClShowhudDDRace &&
				(!GameClient()->m_MultiViewActivated || GameClient()->m_MultiViewShowHud) &&
				GameClient()->m_GameInfo.m_HudDDRace)
			{
				RenderPlayerState(SpectatorId);
			}
			RenderMovementInformation();
			RenderSpectatorHud();
		}

		if(g_Config.m_ClShowhudTimer)
			RenderGameTimer();
		RenderPauseNotification();
		RenderSuddenDeath();
		if(g_Config.m_ClShowhudScore)
			RenderScoreHud();
		if(!(FocusModeActive && HideUiInFocusMode))
			RenderDummyActions();
		RenderWarmupTimer();
		RenderTextInfo();
		if(!(FocusModeActive && HideUiInFocusMode))
		{
			GameClient()->m_TClient.RenderCenterLines();
			RenderLocalTime((m_Width / 7) * 3);
		}
		if(Client()->State() != IClient::STATE_DEMOPLAYBACK)
			RenderConnectionWarning();
		RenderTeambalanceWarning();
		GameClient()->m_Voting.Render();
		if(g_Config.m_ClShowRecord)
			RenderRecord();
		if(!(FocusModeActive && HideUiInFocusMode))
			RenderAmfKeyIndicator();
		if(!(FocusModeActive && HideUiInFocusMode))
			RenderAmfMouseIndicator();
	}
	RenderCursor();
}

void CHud::OnMessage(int MsgType, void *pRawMsg)
{
	if(MsgType == NETMSGTYPE_SV_DDRACETIME || MsgType == NETMSGTYPE_SV_DDRACETIMELEGACY)
	{
		CNetMsg_Sv_DDRaceTime *pMsg = (CNetMsg_Sv_DDRaceTime *)pRawMsg;

		m_DDRaceTime = pMsg->m_Time;

		m_ShowFinishTime = pMsg->m_Finish != 0;

		if(!m_ShowFinishTime)
		{
			m_TimeCpDiff = (float)pMsg->m_Check / 100;
			m_TimeCpLastReceivedTick = Client()->GameTick(g_Config.m_ClDummy);
		}
		else
		{
			m_FinishTimeDiff = (float)pMsg->m_Check / 100;
			m_FinishTimeLastReceivedTick = Client()->GameTick(g_Config.m_ClDummy);
		}
	}
	else if(MsgType == NETMSGTYPE_SV_RECORD || MsgType == NETMSGTYPE_SV_RECORDLEGACY)
	{
		CNetMsg_Sv_Record *pMsg = (CNetMsg_Sv_Record *)pRawMsg;

		// NETMSGTYPE_SV_RACETIME on old race servers
		if(MsgType == NETMSGTYPE_SV_RECORDLEGACY && GameClient()->m_GameInfo.m_DDRaceRecordMessage)
		{
			m_DDRaceTime = pMsg->m_ServerTimeBest; // First value: m_Time

			m_FinishTimeLastReceivedTick = Client()->GameTick(g_Config.m_ClDummy);

			if(pMsg->m_PlayerTimeBest) // Second value: m_Check
			{
				m_TimeCpDiff = (float)pMsg->m_PlayerTimeBest / 100;
				m_TimeCpLastReceivedTick = Client()->GameTick(g_Config.m_ClDummy);
			}
		}
		else if(MsgType == NETMSGTYPE_SV_RECORD || GameClient()->m_GameInfo.m_RaceRecordMessage)
		{
			// ignore m_ServerTimeBest, it's handled by the game client
			m_aPlayerRecord[g_Config.m_ClDummy] = (float)pMsg->m_PlayerTimeBest / 100;
		}
	}
}

void CHud::RenderDDRaceEffects()
{
	if(m_DDRaceTime)
	{
		char aBuf[64];
		char aTime[32];
		if(m_ShowFinishTime && m_FinishTimeLastReceivedTick + Client()->GameTickSpeed() * 6 > Client()->GameTick(g_Config.m_ClDummy))
		{
			str_time(m_DDRaceTime, ETimeFormat::HOURS_CENTISECS, aTime, sizeof(aTime));
			str_format(aBuf, sizeof(aBuf), "Finish time: %s", aTime);

			// calculate alpha (4 sec 1 than get lower the next 2 sec)
			float Alpha = 1.0f;
			if(m_FinishTimeLastReceivedTick + Client()->GameTickSpeed() * 4 < Client()->GameTick(g_Config.m_ClDummy) && m_FinishTimeLastReceivedTick + Client()->GameTickSpeed() * 6 > Client()->GameTick(g_Config.m_ClDummy))
			{
				// lower the alpha slowly to blend text out
				Alpha = ((float)(m_FinishTimeLastReceivedTick + Client()->GameTickSpeed() * 6) - (float)Client()->GameTick(g_Config.m_ClDummy)) / (float)(Client()->GameTickSpeed() * 2);
			}

			TextRender()->TextColor(1, 1, 1, Alpha);
			CTextCursor Cursor;
			Cursor.SetPosition(vec2(150 * Graphics()->ScreenAspect() - TextRender()->TextWidth(12, aBuf) / 2, 20));
			Cursor.m_FontSize = 12.0f;
			TextRender()->RecreateTextContainer(m_DDRaceEffectsTextContainerIndex, &Cursor, aBuf);
			if(m_FinishTimeDiff != 0.0f && m_DDRaceEffectsTextContainerIndex.Valid())
			{
				if(m_FinishTimeDiff < 0)
				{
					str_time_float(-m_FinishTimeDiff, ETimeFormat::HOURS_CENTISECS, aTime, sizeof(aTime));
					str_format(aBuf, sizeof(aBuf), "-%s", aTime);
					TextRender()->TextColor(0.5f, 1.0f, 0.5f, Alpha); // green
				}
				else
				{
					str_time_float(m_FinishTimeDiff, ETimeFormat::HOURS_CENTISECS, aTime, sizeof(aTime));
					str_format(aBuf, sizeof(aBuf), "+%s", aTime);
					TextRender()->TextColor(1.0f, 0.5f, 0.5f, Alpha); // red
				}
				CTextCursor DiffCursor;
				DiffCursor.SetPosition(vec2(150 * Graphics()->ScreenAspect() - TextRender()->TextWidth(10, aBuf) / 2, 34));
				DiffCursor.m_FontSize = 10.0f;
				TextRender()->AppendTextContainer(m_DDRaceEffectsTextContainerIndex, &DiffCursor, aBuf);
			}
			if(m_DDRaceEffectsTextContainerIndex.Valid())
			{
				auto OutlineColor = TextRender()->DefaultTextOutlineColor();
				OutlineColor.a *= Alpha;
				TextRender()->RenderTextContainer(m_DDRaceEffectsTextContainerIndex, TextRender()->DefaultTextColor(), OutlineColor);
			}
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
		else if(g_Config.m_ClShowhudTimeCpDiff && !m_ShowFinishTime && m_TimeCpLastReceivedTick + Client()->GameTickSpeed() * 6 > Client()->GameTick(g_Config.m_ClDummy))
		{
			if(m_TimeCpDiff < 0)
			{
				str_time_float(-m_TimeCpDiff, ETimeFormat::HOURS_CENTISECS, aTime, sizeof(aTime));
				str_format(aBuf, sizeof(aBuf), "-%s", aTime);
			}
			else
			{
				str_time_float(m_TimeCpDiff, ETimeFormat::HOURS_CENTISECS, aTime, sizeof(aTime));
				str_format(aBuf, sizeof(aBuf), "+%s", aTime);
			}

			// calculate alpha (4 sec 1 than get lower the next 2 sec)
			float Alpha = 1.0f;
			if(m_TimeCpLastReceivedTick + Client()->GameTickSpeed() * 4 < Client()->GameTick(g_Config.m_ClDummy) && m_TimeCpLastReceivedTick + Client()->GameTickSpeed() * 6 > Client()->GameTick(g_Config.m_ClDummy))
			{
				// lower the alpha slowly to blend text out
				Alpha = ((float)(m_TimeCpLastReceivedTick + Client()->GameTickSpeed() * 6) - (float)Client()->GameTick(g_Config.m_ClDummy)) / (float)(Client()->GameTickSpeed() * 2);
			}

			if(m_TimeCpDiff > 0)
				TextRender()->TextColor(1.0f, 0.5f, 0.5f, Alpha); // red
			else if(m_TimeCpDiff < 0)
				TextRender()->TextColor(0.5f, 1.0f, 0.5f, Alpha); // green
			else if(!m_TimeCpDiff)
				TextRender()->TextColor(1, 1, 1, Alpha); // white

			CTextCursor Cursor;
			Cursor.SetPosition(vec2(150 * Graphics()->ScreenAspect() - TextRender()->TextWidth(10, aBuf) / 2, 20));
			Cursor.m_FontSize = 10.0f;
			TextRender()->RecreateTextContainer(m_DDRaceEffectsTextContainerIndex, &Cursor, aBuf);

			if(m_DDRaceEffectsTextContainerIndex.Valid())
			{
				auto OutlineColor = TextRender()->DefaultTextOutlineColor();
				OutlineColor.a *= Alpha;
				TextRender()->RenderTextContainer(m_DDRaceEffectsTextContainerIndex, TextRender()->DefaultTextColor(), OutlineColor);
			}
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
	}
}

void CHud::RenderRecord()
{
	if(GameClient()->m_MapBestTimeSeconds != FinishTime::UNSET && GameClient()->m_MapBestTimeSeconds != FinishTime::NOT_FINISHED_MILLIS)
	{
		char aBuf[64];
		TextRender()->Text(5, 75, 6, Localize("Server best:"), -1.0f);
		char aTime[32];
		int64_t TimeCentiseconds = static_cast<int64_t>(GameClient()->m_MapBestTimeSeconds) * 100 + static_cast<int64_t>(GameClient()->m_MapBestTimeMillis) / 10;
		str_time(TimeCentiseconds, ETimeFormat::HOURS_CENTISECS, aTime, sizeof(aTime));
		str_format(aBuf, sizeof(aBuf), "%s%s", GameClient()->m_MapBestTimeSeconds > 3600 ? "" : "   ", aTime);
		TextRender()->Text(53, 75, 6, aBuf, -1.0f);
	}

	if(GameClient()->m_ReceivedDDNetPlayerFinishTimes)
	{
		const int PlayerTimeSeconds = GameClient()->m_aClients[GameClient()->m_aLocalIds[g_Config.m_ClDummy]].m_FinishTimeSeconds;
		if(PlayerTimeSeconds != FinishTime::NOT_FINISHED_MILLIS)
		{
			char aBuf[64];
			TextRender()->Text(5, 82, 6, Localize("Personal best:"), -1.0f);
			char aTime[32];
			const int PlayerTimeMillis = GameClient()->m_aClients[GameClient()->m_aLocalIds[g_Config.m_ClDummy]].m_FinishTimeMillis;
			int64_t TimeCentiseconds = static_cast<int64_t>(PlayerTimeSeconds) * 100 + static_cast<int64_t>(PlayerTimeMillis) / 10;
			str_time(TimeCentiseconds, ETimeFormat::HOURS_CENTISECS, aTime, sizeof(aTime));
			str_format(aBuf, sizeof(aBuf), "%s%s", PlayerTimeSeconds > 3600 ? "" : "   ", aTime);
			TextRender()->Text(53, 82, 6, aBuf, -1.0f);
		}
	}
	else
	{
		const float PlayerRecord = m_aPlayerRecord[g_Config.m_ClDummy];
		if(PlayerRecord > 0.0f)
		{
			char aBuf[64];
			TextRender()->Text(5, 82, 6, Localize("Personal best:"), -1.0f);
			char aTime[32];
			str_time_float(PlayerRecord, ETimeFormat::HOURS_CENTISECS, aTime, sizeof(aTime));
			str_format(aBuf, sizeof(aBuf), "%s%s", PlayerRecord > 3600 ? "" : "   ", aTime);
			TextRender()->Text(53, 82, 6, aBuf, -1.0f);
		}
	}
}
