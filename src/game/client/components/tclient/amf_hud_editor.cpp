/* HUD editor integration for AMF Client. */
#include "amf_hud_editor.h"

#include <game/client/components/hud.h>
#include <game/client/components/infomessages.h>

#include <base/color.h>
#include <base/math.h>
#include <base/str.h>

#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/smooth_ui.h>
#include <game/client/components/chat.h>
#include <game/client/components/voting.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>

namespace AmfHudEditorAnimations
{
inline bool Enabled() { return false; }
inline float EaseOutCubic(float Value) { return SmoothUiEaseOutCubic(Value); }
inline float EaseInOutQuad(float Value) { return SmoothUiEaseInOutQuad(Value); }
inline float UpdatePhase(float &Phase, float Target, float, float) { return Phase = Target; }
inline float MsToSeconds(int Milliseconds) { return Milliseconds / 1000.0f; }
}

namespace
{
	constexpr float SNAP_THRESHOLD = 6.0f;
	constexpr float SETTINGS_POPUP_WIDTH = 210.0f;
	constexpr float SETTINGS_POPUP_HEIGHT = 174.0f;
	constexpr float KEY_INDICATOR_SETTINGS_POPUP_HEIGHT = 194.0f;

	// Mirrors CUi::SPopupMenu::POPUP_BORDER + POPUP_MARGIN, which we can't reach directly
	// since that struct is a private implementation detail of CUi.
	constexpr float POPUP_FRAME_MARGIN = 5.0f;
	constexpr float POPUP_FRAME_ROUNDING = 3.0f;

	// Growth animation used by both the opening popup (PopupModuleSettings) and the
	// closing ghost frame (CAmfHudEditor::RenderClosingPopupFrame), so they stay visually
	// identical when played forwards vs. backwards.
	CUIRect ComputeAnimRect(const CUIRect &OuterRect, bool GrowFromRight, float Phase)
	{
		CUIRect AnimRect;
		AnimRect.w = OuterRect.w * Phase;
		AnimRect.h = OuterRect.h * Phase;
		AnimRect.y = OuterRect.y;
		AnimRect.x = GrowFromRight ? (OuterRect.x + OuterRect.w - AnimRect.w) : OuterRect.x;
		return AnimRect;
	}

	void DrawPopupFrame(const CUIRect &AnimRect, float Phase)
	{
		AnimRect.Draw(ColorRGBA(0.5f, 0.5f, 0.5f, 0.75f * Phase), IGraphics::CORNER_ALL, POPUP_FRAME_ROUNDING);
		CUIRect InnerAnimRect;
		AnimRect.Margin(1.0f, &InnerAnimRect);
		InnerAnimRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.75f * Phase), IGraphics::CORNER_ALL, POPUP_FRAME_ROUNDING);
	}

	// Kinds accepted by CAmfHudEditor::StartResetAnimation().
	constexpr int RESET_KIND_POSITION = 1;
	constexpr int RESET_KIND_SCALE = 2;
	constexpr int RESET_KIND_ALL = 3;
	constexpr int RESET_ANIM_DURATION_MS = 260;

	CUIRect LerpRect(const CUIRect &A, const CUIRect &B, float T)
	{
		CUIRect Result;
		Result.x = mix(A.x, B.x, T);
		Result.y = mix(A.y, B.y, T);
		Result.w = mix(A.w, B.w, T);
		Result.h = mix(A.h, B.h, T);
		return Result;
	}

	// Modules the editor currently knows how to draw a drag handle for and render
	// a live preview of. Add a case here plus a GetModuleVisual() branch and one
	// CollectModuleVisuals() line to wire up a new module.
	bool IsEditorModule(HudLayout::EModule Module)
	{
	return Module == HudLayout::MODULE_KEYSTROKES_KEYBOARD ||
	       Module == HudLayout::MODULE_MUSIC_PLAYER ||
	       Module == HudLayout::MODULE_PING ||
	       Module == HudLayout::MODULE_SCORE ||
	       Module == HudLayout::MODULE_FROZEN_HUD ||
	       Module == HudLayout::MODULE_PLAYER_STATE ||
	       Module == HudLayout::MODULE_VOTES ||
	       Module == HudLayout::MODULE_CHAT ||
	       Module == HudLayout::MODULE_MOVEMENT_INFO ||
	       Module == HudLayout::MODULE_DUMMY_ACTIONS ||
	       Module == HudLayout::MODULE_LOCAL_TIME ||
	       Module == HudLayout::MODULE_GAME_TIMER ||
	       Module == HudLayout::MODULE_KILLFEED;
	}

	bool SupportsModuleScale(HudLayout::EModule Module)
	{
		// Expose the RMB Scale control only when the real renderer consumes this
		// module's layout scale. Never offer a cosmetic setting for fixed geometry.
		return Module == HudLayout::MODULE_KEYSTROKES_KEYBOARD ||
		       Module == HudLayout::MODULE_MUSIC_PLAYER ||
		       Module == HudLayout::MODULE_CHAT ||
		       Module == HudLayout::MODULE_VOTES ||
		       Module == HudLayout::MODULE_PING ||
		       Module == HudLayout::MODULE_GAME_TIMER ||
		       Module == HudLayout::MODULE_FROZEN_HUD ||
		       Module == HudLayout::MODULE_PLAYER_STATE ||
		       Module == HudLayout::MODULE_MOVEMENT_INFO ||
		       Module == HudLayout::MODULE_LOCAL_TIME ||
		       Module == HudLayout::MODULE_SCORE ||
		       Module == HudLayout::MODULE_DUMMY_ACTIONS ||
		       Module == HudLayout::MODULE_KILLFEED;
	}

	bool PointInRect(vec2 Point, const CUIRect &Rect)
	{
		return Point.x >= Rect.x && Point.x <= Rect.x + Rect.w &&
		       Point.y >= Rect.y && Point.y <= Rect.y + Rect.h;
	}

	CUIRect ResetAllRect(float HudWidth, float HudHeight)
	{
		// The editor footer is outside the normal top-left HUD work area and keeps
		// this persistent action away from the modules being arranged.
		return {(HudWidth - 66.0f) * 0.5f, HudHeight - 22.0f, 66.0f, 16.0f};
	}

	void DrawRoundedRectOutline(IGraphics *pGraphics, const CUIRect &Rect, int Corners, float Rounding, ColorRGBA Color)
	{
		if(Rect.w <= 0.0f || Rect.h <= 0.0f || Color.a <= 0.0f)
			return;

		const float Radius = std::clamp(Rounding, 0.0f, std::min(Rect.w, Rect.h) * 0.5f);
		if(Radius <= 0.01f || Corners == IGraphics::CORNER_NONE)
		{
			Rect.DrawOutline(Color);
			return;
		}

		constexpr int SegmentsPerCorner = 8;
		IGraphics::CLineItem aLines[SegmentsPerCorner * 4 + 4];
		int NumLines = 0;

		auto AddLine = [&](vec2 From, vec2 To) {
			aLines[NumLines++] = IGraphics::CLineItem(From, To);
		};

		auto AddArc = [&](vec2 Center, float StartAngle, float EndAngle) {
			vec2 Prev = vec2(
				Center.x + std::cos(StartAngle) * Radius,
				Center.y + std::sin(StartAngle) * Radius);
			for(int i = 1; i <= SegmentsPerCorner; ++i)
			{
				const float T = i / (float)SegmentsPerCorner;
				const float Angle = mix(StartAngle, EndAngle, T);
				const vec2 Cur(
					Center.x + std::cos(Angle) * Radius,
					Center.y + std::sin(Angle) * Radius);
				AddLine(Prev, Cur);
				Prev = Cur;
			}
		};

		const bool TopLeftRounded = (Corners & IGraphics::CORNER_TL) != 0;
		const bool TopRightRounded = (Corners & IGraphics::CORNER_TR) != 0;
		const bool BottomLeftRounded = (Corners & IGraphics::CORNER_BL) != 0;
		const bool BottomRightRounded = (Corners & IGraphics::CORNER_BR) != 0;
		const float Left = Rect.x;
		const float Right = Rect.x + Rect.w;
		const float Top = Rect.y;
		const float Bottom = Rect.y + Rect.h;

		AddLine(
			vec2(Left + (TopLeftRounded ? Radius : 0.0f), Top),
			vec2(Right - (TopRightRounded ? Radius : 0.0f), Top));
		if(TopRightRounded)
			AddArc(vec2(Right - Radius, Top + Radius), -pi / 2.0f, 0.0f);

		AddLine(
			vec2(Right, Top + (TopRightRounded ? Radius : 0.0f)),
			vec2(Right, Bottom - (BottomRightRounded ? Radius : 0.0f)));
		if(BottomRightRounded)
			AddArc(vec2(Right - Radius, Bottom - Radius), 0.0f, pi / 2.0f);

		AddLine(
			vec2(Right - (BottomRightRounded ? Radius : 0.0f), Bottom),
			vec2(Left + (BottomLeftRounded ? Radius : 0.0f), Bottom));
		if(BottomLeftRounded)
			AddArc(vec2(Left + Radius, Bottom - Radius), pi / 2.0f, pi);

		AddLine(
			vec2(Left, Bottom - (BottomLeftRounded ? Radius : 0.0f)),
			vec2(Left, Top + (TopLeftRounded ? Radius : 0.0f)));
		if(TopLeftRounded)
			AddArc(vec2(Left + Radius, Top + Radius), pi, 3.0f * pi / 2.0f);

		pGraphics->TextureClear();
		pGraphics->LinesBegin();
		pGraphics->SetColor(Color);
		pGraphics->LinesDraw(aLines, NumLines);
		pGraphics->LinesEnd();
	}

	void DrawHudEditorBox(IGraphics *pGraphics, const CUIRect &Rect, ColorRGBA Color)
	{
		if(Rect.w <= 0.0f || Rect.h <= 0.0f || Color.a <= 0.0f)
			return;

		// This is deliberately just an editor decoration. It has no hitbox and
		// does not participate in scaling or resizing. Keep every corner derived
		// from the same four dimensions: that makes the frame symmetric even when
		// the HUD element is very wide, tall or tiny.
		DrawRoundedRectOutline(pGraphics, Rect, IGraphics::CORNER_NONE, 0.0f, Color);

	const float ShortSide = std::min(Rect.w, Rect.h);
	const float CornerSize = std::min(std::clamp(ShortSide * 0.14f, 0.8f, 5.0f), ShortSide * 0.25f);
	const float Outset = std::min(CornerSize * 0.22f, 0.8f);
	if(CornerSize <= 0.0f)
		return;
		const float Right = Rect.x + Rect.w;
		const float Bottom = Rect.y + Rect.h;

		pGraphics->TextureClear();
	pGraphics->QuadsBegin();
	pGraphics->SetColor(Color);
	// Four identical, mirrored triangular markers. They are decoration only;
	// no input state or resize hitbox is attached to any corner.
	IGraphics::CFreeformItem aCorners[] = {
		{Rect.x - Outset, Rect.y - Outset, Rect.x - Outset + CornerSize, Rect.y - Outset, Rect.x - Outset, Rect.y - Outset + CornerSize, Rect.x - Outset, Rect.y - Outset + CornerSize},
		{Right + Outset, Rect.y - Outset, Right + Outset - CornerSize, Rect.y - Outset, Right + Outset, Rect.y - Outset + CornerSize, Right + Outset, Rect.y - Outset + CornerSize},
		{Rect.x - Outset, Bottom + Outset, Rect.x - Outset + CornerSize, Bottom + Outset, Rect.x - Outset, Bottom + Outset - CornerSize, Rect.x - Outset, Bottom + Outset - CornerSize},
		{Right + Outset, Bottom + Outset, Right + Outset - CornerSize, Bottom + Outset, Right + Outset, Bottom + Outset - CornerSize, Right + Outset, Bottom + Outset - CornerSize},
	};
	pGraphics->QuadsDrawFreeform(aCorners, sizeof(aCorners) / sizeof(aCorners[0]));
	pGraphics->QuadsEnd();
	}

	CUIRect ClampToBounds(CUIRect Rect, float Width, float Height)
	{
		Rect.x = std::clamp(Rect.x, 0.0f, std::max(0.0f, Width - Rect.w));
		Rect.y = std::clamp(Rect.y, 0.0f, std::max(0.0f, Height - Rect.h));
		return Rect;
	}

	float ChatInputBottomExtra(const CChat &Chat)
	{
		const float ScaledFontSize = Chat.FontSize() * (8.0f / 6.0f);
		return std::max(2.25f * ScaledFontSize, std::max(ScaledFontSize + 4.0f, 16.0f));
	}

} // namespace

void CAmfHudEditor::OnConsoleInit()
{
	HudLayout::OnConsoleInit(Console(), ConfigManager());
}

void CAmfHudEditor::Activate()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	m_Active = true;
	GameClient()->m_Menus.SetActive(false);
	m_MouseDownLast = false;
	m_RightMouseDownLast = false;
	m_Dragging = false;
	m_PressedModule = HudLayout::MODULE_COUNT;
	m_HoveredModule = HudLayout::MODULE_COUNT;
	m_SelectedModule = HudLayout::MODULE_COUNT;
	m_PopupRevealPhase = 0.0f;
	m_SuppressCloseAnim = false;
	m_PopupClosing = false;
	m_PopupClosePhase = 0.0f;
	m_ResetAnimCount = 0;
	m_PressedOnReset = false;
	Ui()->ClosePopupMenus();
}

void CAmfHudEditor::Deactivate()
{
	m_Active = false;
	m_MouseDownLast = false;
	m_RightMouseDownLast = false;
	m_Dragging = false;
	m_PressedModule = HudLayout::MODULE_COUNT;
	m_HoveredModule = HudLayout::MODULE_COUNT;
	m_SelectedModule = HudLayout::MODULE_COUNT;
	m_PopupRevealPhase = 0.0f;
	m_SuppressCloseAnim = false;
	m_PopupClosing = false;
	m_PopupClosePhase = 0.0f;
	m_ResetAnimCount = 0;
	m_PressedOnReset = false;
	Ui()->ClosePopupMenus();
}

void CAmfHudEditor::OnStateChange(int NewState, int OldState)
{
	(void)OldState;
	if(NewState != IClient::STATE_ONLINE && NewState != IClient::STATE_DEMOPLAYBACK)
		Deactivate();
}

bool CAmfHudEditor::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!m_Active)
		return false;

	Ui()->ConvertMouseMove(&x, &y, CursorType);
	Ui()->OnCursorMove(x, y);
	return true;
}

bool CAmfHudEditor::OnInput(const IInput::CEvent &Event)
{
	if(!m_Active)
		return false;
	if((Event.m_Flags & IInput::FLAG_PRESS) != 0 && Event.m_Key == KEY_ESCAPE)
	{
		Deactivate();
		return true;
	}
	return true;
}

float CAmfHudEditor::HudWidth() const
{
	return HudLayout::CANVAS_HEIGHT * Graphics()->ScreenAspect();
}

float CAmfHudEditor::HudHeight() const
{
	return HudLayout::CANVAS_HEIGHT;
}

bool CAmfHudEditor::IsEditableModule(HudLayout::EModule Module) const
{
	return IsEditorModule(Module) && HudLayout::IsEditableModule(Module);
}

bool CAmfHudEditor::IsModuleEnabled(HudLayout::EModule Module) const
{
	return HudLayout::IsEnabled(Module);
}

void CAmfHudEditor::SetModuleEnabled(HudLayout::EModule Module, bool Enabled)
{
	// HudLayout owns the single module visibility API. For modules with a
	// feature-level setting it writes that canonical config; modules without one
	// keep their layout entry as the canonical state.
	HudLayout::SetEnabled(Module, Enabled);
}

CUIRect CAmfHudEditor::GetFallbackModuleRect(HudLayout::EModule Module, bool *pHasLiveRect) const
{
	const float Width = HudWidth();
	const float Height = HudHeight();
	const auto Layout = HudLayout::Get(Module, Width, Height);
	CUIRect Rect{};

	switch(Module)
	{
	case HudLayout::MODULE_MUSIC_PLAYER:
		Rect = GameClient()->m_MusicPlayer.GetHudEditorRect(false);
		if(Rect.w <= 0.0f || Rect.h <= 0.0f)
			Rect = GameClient()->m_MusicPlayer.GetHudDefaultRect();
		break;
	case HudLayout::MODULE_KEYSTROKES_KEYBOARD:
	{
		const auto Bounds = GameClient()->m_Hud.GetAmfKeyIndicatorBounds();
		Rect = {Bounds.m_X, Bounds.m_Y, Bounds.m_W, Bounds.m_H};
		break;
	}
	case HudLayout::MODULE_DUMMY_ACTIONS:
	{
		const auto Bounds = GameClient()->m_Hud.GetDummyActionsBounds();
		Rect = {Bounds.m_X, Bounds.m_Y, Bounds.m_W, Bounds.m_H};
		break;
	}
	case HudLayout::MODULE_MOVEMENT_INFO:
	{
		const auto Bounds = GameClient()->m_Hud.GetMovementInformationBounds();
		Rect = {Bounds.m_X, Bounds.m_Y, Bounds.m_W, Bounds.m_H};
		break;
	}
	case HudLayout::MODULE_LOCAL_TIME:
	{
		const auto Bounds = GameClient()->m_Hud.GetLocalTimeBounds();
		Rect = {Bounds.m_X, Bounds.m_Y, Bounds.m_W, Bounds.m_H};
		break;
	}
	case HudLayout::MODULE_GAME_TIMER:
	{
		const auto Bounds = GameClient()->m_Hud.GetGameTimerBounds();
		Rect = {Bounds.m_X, Bounds.m_Y, Bounds.m_W, Bounds.m_H};
		break;
	}
	case HudLayout::MODULE_FPS:
		Rect = {Layout.m_X, Layout.m_Y, 26.0f, 9.0f};
		break;
	case HudLayout::MODULE_PING:
	{
		const auto Bounds = GameClient()->m_Hud.GetPredictionTimeBounds();
		Rect = {Bounds.m_X, Bounds.m_Y, Bounds.m_W, Bounds.m_H};
		break;
	}
	case HudLayout::MODULE_SCORE:
	{
		const auto Bounds = GameClient()->m_Hud.GetScoreHudBounds();
		Rect = {Bounds.m_X, Bounds.m_Y, Bounds.m_W, Bounds.m_H};
		break;
	}
	case HudLayout::MODULE_PLAYER_STATE:
	{
		const auto Bounds = GameClient()->m_Hud.GetPlayerStateBounds();
		Rect = {Bounds.m_X, Bounds.m_Y, Bounds.m_W, Bounds.m_H};
		break;
	}
	case HudLayout::MODULE_FROZEN_HUD:
	{
		const auto Bounds = GameClient()->m_Hud.GetRemainingPlayersBounds();
		Rect = {Bounds.m_X, Bounds.m_Y, Bounds.m_W, Bounds.m_H};
		break;
	}
	case HudLayout::MODULE_VOTES:
	{
		const auto Bounds = GameClient()->m_Voting.GetHudBounds();
		Rect = {Bounds.m_X, Bounds.m_Y, Bounds.m_W, Bounds.m_H};
		break;
	}
	case HudLayout::MODULE_CHAT:
		// The editor needs Chat's persistent input/layout area, not its largest
		// possible message-history preview. The real renderer remains untouched.
		Rect = GameClient()->m_Chat.GetHudRect(Width, Height, false);
		break;
	case HudLayout::MODULE_HOOK_COMBO:
	{
		const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
		const float FontSize = 13.0f * Scale;
		const float BoxWidth = TextRender()->TextWidth(FontSize, "fantastic (x7)", -1, -1.0f) + 8.0f * Scale;
		const float BoxHeight = FontSize + 4.0f * Scale;
		Rect = {Layout.m_X, Layout.m_Y, BoxWidth, BoxHeight};
		break;
	}
	case HudLayout::MODULE_MINI_VOTE:
		Rect = {Layout.m_X, Layout.m_Y, 70.0f, 35.0f};
		break;
	case HudLayout::MODULE_NOTIFY_LAST:
		Rect = {Layout.m_X, Layout.m_Y, 185.0f, 16.0f};
		break;
	case HudLayout::MODULE_LOCK_CAM:
		Rect = {Layout.m_X, Layout.m_Y, 16.0f, 16.0f};
		break;
	case HudLayout::MODULE_KILLFEED:
	{
		// Killfeed messages are intentionally transient. Its editor frame must
		// instead use the renderer's stable full-layout reservation.
		const auto Bounds = GameClient()->m_InfoMessages.GetKillFeedEditorBounds();
		Rect = {Bounds.m_X, Bounds.m_Y, Bounds.m_W, Bounds.m_H};
		break;
	}
	default:
		Rect = {Layout.m_X, Layout.m_Y, 78.0f, 18.0f};
		break;
	}

	const bool HasLiveRect = Rect.w > 0.0f && Rect.h > 0.0f;
	if(pHasLiveRect)
		*pHasLiveRect = HasLiveRect;

	// A disabled module can legitimately have no live bounds this frame. The
	// editor still needs its real logical layout rectangle to make it selectable
	// and re-enableable; these are geometry-only fallbacks, never fake content.
	if(Rect.w <= 0.0f || Rect.h <= 0.0f)
	{
		const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
		switch(Module)
		{
		case HudLayout::MODULE_MUSIC_PLAYER:
			Rect = {Layout.m_X, Layout.m_Y, 80.0f * Scale, 18.0f * Scale};
			break;
		case HudLayout::MODULE_KEYSTROKES_KEYBOARD:
			Rect = {Layout.m_X, Layout.m_Y, 65.0f * Scale, 88.0f * Scale};
			break;
		case HudLayout::MODULE_PING:
			Rect = {Layout.m_X, Layout.m_Y, 20.0f * Scale, 12.0f * Scale};
			break;
		case HudLayout::MODULE_SCORE:
			Rect = {Width - 46.0f * Scale, 229.0f, 46.0f * Scale, 54.0f * Scale};
			if(HudLayout::HasPositionOverride(Module))
				Rect = {Layout.m_X, Layout.m_Y, Rect.w, Rect.h};
			break;
		case HudLayout::MODULE_FROZEN_HUD:
			Rect = {Width * 0.5f - 18.0f * Scale, 12.0f, 36.0f * Scale, 12.0f * Scale};
			if(HudLayout::HasPositionOverride(Module))
				Rect = {Layout.m_X, Layout.m_Y, Rect.w, Rect.h};
			break;
		case HudLayout::MODULE_PLAYER_STATE:
			Rect = {Layout.m_X, Layout.m_Y, 48.0f * Scale, 48.0f * Scale};
			break;
		case HudLayout::MODULE_VOTES:
			Rect = {Layout.m_X, Layout.m_Y, 125.0f * Scale, 42.0f * Scale};
			break;
		case HudLayout::MODULE_MOVEMENT_INFO:
			Rect = {Layout.m_X, Layout.m_Y, 62.0f * Scale, 48.0f * Scale};
			break;
		case HudLayout::MODULE_DUMMY_ACTIONS:
			Rect = {Layout.m_X, Layout.m_Y, 16.0f * Scale, 29.0f * Scale};
			break;
		case HudLayout::MODULE_GAME_TIMER:
			Rect = {Layout.m_X, Layout.m_Y, 44.0f * Scale, 12.0f * Scale};
			break;
		case HudLayout::MODULE_KILLFEED:
			Rect = {Width - CInfoMessages::KILLFEED_EDITOR_BASE_WIDTH * Scale, 5.0f, CInfoMessages::KILLFEED_EDITOR_BASE_WIDTH * Scale, CInfoMessages::KILLFEED_EDITOR_BASE_HEIGHT * Scale};
			if(HudLayout::HasPositionOverride(Module))
				Rect = {Layout.m_X - Rect.w, Layout.m_Y, Rect.w, Rect.h};
			break;
		default:
			break;
		}
	}

	return ClampToBounds(Rect, Width, Height);
}

CAmfHudEditor::SModuleVisual CAmfHudEditor::GetModuleVisual(HudLayout::EModule Module) const
{
	SModuleVisual Visual;
	Visual.m_Module = Module;
	Visual.m_Editable = IsEditableModule(Module);
	Visual.m_Enabled = IsModuleEnabled(Module);
	Visual.m_IsFallbackPreview = false;

	const float Width = HudWidth();
	const float Height = HudHeight();

	bool HasLiveRect = false;
	Visual.m_Rect = GetFallbackModuleRect(Module, &HasLiveRect);
	const int CurrentScale = HudLayout::Get(Module, Width, Height).m_Scale;
	// Killfeed publishes a stable layout rect even while disabled, so use it
	// directly instead of an old cache. This preserves its right-edge anchor
	// when Scale is changed in the settings popup while the module is off.
	const bool HasStableEditorRect = Module == HudLayout::MODULE_KILLFEED;
	if(HasLiveRect && (Visual.m_Enabled || HasStableEditorRect))
	{
		m_aLastLiveRects[Module] = Visual.m_Rect;
		m_aLastLiveScales[Module] = CurrentScale;
		m_aHasLastLiveRect[Module] = true;
	}
	else if(m_aHasLastLiveRect[Module])
	{
		Visual.m_Rect = m_aLastLiveRects[Module];
		const int CachedScale = std::max(m_aLastLiveScales[Module], 1);
		const float ScaleRatio = CurrentScale / (float)CachedScale;
		Visual.m_Rect.w *= ScaleRatio;
		Visual.m_Rect.h *= ScaleRatio;
	}
	Visual.m_Rounding = 4.0f;
	// Every module admitted by CollectModuleVisuals is backed by its real renderer.
	// The editor therefore paints only a selection tint over live content, never a
	// substitute placeholder box.
	Visual.m_IsFallbackPreview = false;

	if(Visual.m_Rect.w <= 0.0f || Visual.m_Rect.h <= 0.0f)
	{
		Visual.m_Rect = GetFallbackModuleRect(Module);
		Visual.m_Rounding = 4.0f;
		Visual.m_IsFallbackPreview = false;
	}

	Visual.m_Rounding = std::clamp(Visual.m_Rounding, 2.0f, 12.0f);
	Visual.m_Rect = ClampToBounds(Visual.m_Rect, Width, Height);
	Visual.m_Corners = HudLayout::BackgroundCorners(IGraphics::CORNER_ALL, Visual.m_Rect.x, Visual.m_Rect.y, Visual.m_Rect.w, Visual.m_Rect.h, Width, Height);
	return Visual;
}

void CAmfHudEditor::CollectModuleVisuals(SModuleVisual *pOut, int &Count) const
{
	Count = 0;

	auto AddModule = [&](HudLayout::EModule Module) {
		if(Count >= MAX_MODULE_VISUALS)
			return;
		pOut[Count++] = GetModuleVisual(Module);
	};

	// Editor availability must not follow runtime visibility. A disabled HUD is
	// still a registered editable module so the player can find and enable it.
	// Keep chat at the very bottom so overlapping HUD modules remain easy to select.
	AddModule(HudLayout::MODULE_KEYSTROKES_KEYBOARD);
	AddModule(HudLayout::MODULE_MUSIC_PLAYER);
	AddModule(HudLayout::MODULE_PING);
	AddModule(HudLayout::MODULE_SCORE);
	AddModule(HudLayout::MODULE_PLAYER_STATE);
	AddModule(HudLayout::MODULE_FROZEN_HUD);
	AddModule(HudLayout::MODULE_VOTES);
	AddModule(HudLayout::MODULE_CHAT);
	AddModule(HudLayout::MODULE_DUMMY_ACTIONS);
	AddModule(HudLayout::MODULE_MOVEMENT_INFO);
	AddModule(HudLayout::MODULE_LOCAL_TIME);
	AddModule(HudLayout::MODULE_GAME_TIMER);
	AddModule(HudLayout::MODULE_KILLFEED);
}

HudLayout::EModule CAmfHudEditor::HitTestModule(vec2 MousePos) const
{
	SModuleVisual aVisuals[MAX_MODULE_VISUALS];
	int Count = 0;
	CollectModuleVisuals(aVisuals, Count);

	// Editable modules should always win hit-tests over locked preview modules.
	for(int i = Count - 1; i >= 0; --i)
	{
		if(!aVisuals[i].m_Editable)
			continue;
		const CUIRect &Rect = aVisuals[i].m_Rect;
		if(PointInRect(MousePos, Rect))
			return aVisuals[i].m_Module;
	}

	for(int i = Count - 1; i >= 0; --i)
	{
		const CUIRect &Rect = aVisuals[i].m_Rect;
		if(PointInRect(MousePos, Rect))
			return aVisuals[i].m_Module;
	}
	return HudLayout::MODULE_COUNT;
}

void CAmfHudEditor::ApplyDraggedPosition(HudLayout::EModule Module, const CUIRect &Rect)
{
	if(!IsEditableModule(Module))
		return;

	const float CanvasScale = HudLayout::CANVAS_WIDTH / std::max(HudWidth(), 1.0f);
	if(Module == HudLayout::MODULE_CHAT)
	{
		// Chat is rendered upwards from its input/baseline Y. The editor rect is
		// top-left based, so persist its bottom edge to keep the grabbed point
		// under the cursor on the first frame of a drag.
		HudLayout::SetPosition(Module, Rect.x * CanvasScale, Rect.y + Rect.h, HudLayout::POSITION_MODE_TOP_LEFT);
	}
	else if(Module == HudLayout::MODULE_KILLFEED)
	{
		// Killfeed renders rows from their right edge. Persist that same pivot,
		// not the editor rectangle's left side, otherwise the first drag frame
		// shifts a row by its entire width.
		HudLayout::SetPosition(Module, (Rect.x + Rect.w) * CanvasScale, Rect.y, HudLayout::POSITION_MODE_TOP_LEFT);
	}
	else
		HudLayout::SetPosition(Module, Rect.x * CanvasScale, Rect.y, HudLayout::POSITION_MODE_TOP_LEFT);
	CacheModuleRect(Module, Rect);
}

void CAmfHudEditor::CacheModuleRect(HudLayout::EModule Module, const CUIRect &Rect)
{
	if(Module < 0 || Module >= HudLayout::MODULE_COUNT || Rect.w <= 0.0f || Rect.h <= 0.0f)
		return;
	m_aLastLiveRects[Module] = Rect;
	m_aLastLiveScales[Module] = HudLayout::Get(Module, HudWidth(), HudHeight()).m_Scale;
	m_aHasLastLiveRect[Module] = true;
}

void CAmfHudEditor::InvalidateCachedModuleRect(HudLayout::EModule Module)
{
	if(Module < 0 || Module >= HudLayout::MODULE_COUNT)
		return;
	m_aHasLastLiveRect[Module] = false;
}

CUIRect CAmfHudEditor::SnapRect(const CUIRect &Rect, HudLayout::EModule DraggedModule) const
{
	CUIRect Result = Rect;
	SModuleVisual aVisuals[MAX_MODULE_VISUALS];
	int Count = 0;
	CollectModuleVisuals(aVisuals, Count);

	auto TrySnap = [](float Candidate, float Target, float &BestDelta) {
		const float Delta = Target - Candidate;
		if(absolute(Delta) <= SNAP_THRESHOLD && absolute(Delta) < absolute(BestDelta))
			BestDelta = Delta;
	};

	float BestDeltaX = SNAP_THRESHOLD + 1.0f;
	float BestDeltaY = SNAP_THRESHOLD + 1.0f;
	const float Width = HudWidth();
	const float Height = HudHeight();

	TrySnap(Result.x, 0.0f, BestDeltaX);
	TrySnap(Result.x + Result.w, Width, BestDeltaX);
	TrySnap(Result.x + Result.w * 0.5f, Width * 0.5f, BestDeltaX);
	TrySnap(Result.y, 0.0f, BestDeltaY);
	TrySnap(Result.y + Result.h, Height, BestDeltaY);
	TrySnap(Result.y + Result.h * 0.5f, Height * 0.5f, BestDeltaY);

	for(int i = 0; i < Count; ++i)
	{
		if(aVisuals[i].m_Module == DraggedModule)
			continue;
		const CUIRect &Other = aVisuals[i].m_Rect;
		TrySnap(Result.x, Other.x, BestDeltaX);
		TrySnap(Result.x + Result.w, Other.x + Other.w, BestDeltaX);
		TrySnap(Result.x, Other.x + Other.w, BestDeltaX);
		TrySnap(Result.x + Result.w, Other.x, BestDeltaX);
		TrySnap(Result.x + Result.w * 0.5f, Other.x + Other.w * 0.5f, BestDeltaX);
		TrySnap(Result.y, Other.y, BestDeltaY);
		TrySnap(Result.y + Result.h, Other.y + Other.h, BestDeltaY);
		TrySnap(Result.y, Other.y + Other.h, BestDeltaY);
		TrySnap(Result.y + Result.h, Other.y, BestDeltaY);
		TrySnap(Result.y + Result.h * 0.5f, Other.y + Other.h * 0.5f, BestDeltaY);
	}

	if(absolute(BestDeltaX) <= SNAP_THRESHOLD)
		Result.x += BestDeltaX;
	if(absolute(BestDeltaY) <= SNAP_THRESHOLD)
		Result.y += BestDeltaY;

	return ClampToBounds(Result, Width, Height);
}

void CAmfHudEditor::UpdateDragging(vec2 MousePos)
{
	if(!m_Dragging || m_PressedModule == HudLayout::MODULE_COUNT || !IsEditableModule(m_PressedModule))
		return;
	CUIRect NewRect = m_DragAnchorRect;
	NewRect.x = MousePos.x - m_DragMouseOffset.x;
	NewRect.y = MousePos.y - m_DragMouseOffset.y;
	if(!Input()->ShiftIsPressed())
		NewRect = SnapRect(NewRect, m_PressedModule);
	else
		NewRect = ClampToBounds(NewRect, HudWidth(), HudHeight());
	ApplyDraggedPosition(m_PressedModule, NewRect);
}

CAmfHudEditor::SResetAnim *CAmfHudEditor::FindOrAddResetAnim(HudLayout::EModule Module)
{
	for(int i = 0; i < m_ResetAnimCount; ++i)
	{
		if(m_aResetAnims[i].m_Module == Module)
			return &m_aResetAnims[i];
	}
	if(m_ResetAnimCount >= MAX_MODULE_VISUALS)
		return nullptr;
	SResetAnim &NewAnim = m_aResetAnims[m_ResetAnimCount++];
	NewAnim = SResetAnim{};
	NewAnim.m_Module = Module;
	return &NewAnim;
}

void CAmfHudEditor::CancelResetAnimation(HudLayout::EModule Module)
{
	for(int i = 0; i < m_ResetAnimCount; ++i)
	{
		if(m_aResetAnims[i].m_Module == Module)
		{
			m_aResetAnims[i] = m_aResetAnims[m_ResetAnimCount - 1];
			--m_ResetAnimCount;
			return;
		}
	}
}

void CAmfHudEditor::StartResetAnimation(HudLayout::EModule Module, int Kind)
{
	if(!IsEditableModule(Module))
		return;

	// Reset animation is disabled in this editor. The former animation setup reset a
	// module, read a one-frame-old live rect, and then wrote that stale rect back as a
	// position override. This is why Killfeed and Dummy Actions could survive Reset All.
	if(Kind == RESET_KIND_SCALE)
		HudLayout::ResetScale(Module);
	else if(Kind == RESET_KIND_POSITION)
		HudLayout::ResetPosition(Module);
	else
		HudLayout::ResetSettings(Module);
	InvalidateCachedModuleRect(Module);
}

void CAmfHudEditor::UpdateResetAnimation()
{
	if(m_ResetAnimCount == 0)
		return;

	const bool AnimEnabled = AmfHudEditorAnimations::Enabled() && false;
	const float Dt = Client()->RenderFrameTime();
	for(int i = 0; i < m_ResetAnimCount; ++i)
	{
		SResetAnim &Anim = m_aResetAnims[i];
		if(AnimEnabled)
			AmfHudEditorAnimations::UpdatePhase(Anim.m_Phase, 1.0f, Dt, AmfHudEditorAnimations::MsToSeconds(RESET_ANIM_DURATION_MS));
		else
			Anim.m_Phase = 1.0f;

		const float Eased = AmfHudEditorAnimations::EaseOutCubic(Anim.m_Phase);
		const CUIRect CurRect = LerpRect(Anim.m_StartRect, Anim.m_TargetRect, Eased);
		const int CurScale = (int)std::lround(mix((float)Anim.m_StartScale, (float)Anim.m_TargetScale, Eased));

		HudLayout::SetScale(Anim.m_Module, CurScale);
		if(Anim.m_Phase >= 1.0f && Anim.m_Kind != RESET_KIND_SCALE)
		{
			// Leave position at the true default (no runtime override) so dynamic
			// right-edge stacking - Dummy Actions appearing/disappearing, etc. -
			// keeps working instead of baking a temporary stack gap into the save.
			HudLayout::ResetPosition(Anim.m_Module);
			HudLayout::SetScale(Anim.m_Module, Anim.m_TargetScale);
		}
		else
		{
			ApplyDraggedPosition(Anim.m_Module, CurRect);
		}
	}

	// Compact away finished entries (swap-remove, order doesn't matter here).
	int Write = 0;
	for(int Read = 0; Read < m_ResetAnimCount; ++Read)
	{
		if(m_aResetAnims[Read].m_Phase < 1.0f)
		{
			if(Write != Read)
				m_aResetAnims[Write] = m_aResetAnims[Read];
			++Write;
		}
	}
	m_ResetAnimCount = Write;
}

CUi::EPopupMenuFunctionResult CAmfHudEditor::PopupModuleSettings(void *pContext, CUIRect View, bool Active)
{
	(void)Active;
	CAmfHudEditor *pThis = static_cast<CAmfHudEditor *>(pContext);
	if(pThis->m_SelectedModule == HudLayout::MODULE_COUNT)
		return CUi::POPUP_CLOSE_CURRENT;

	// Smoothly grow the popup out of the module it belongs to instead of popping in at
	// full size: the outer frame (border+background) is drawn here manually - growing
	// from the corner nearest the module - since DoPopupMenu() already drew a transparent
	// one for us (see OpenModuleSettings), and the real content below is clipped to match.
	if(AmfHudEditorAnimations::Enabled() && false)
		AmfHudEditorAnimations::UpdatePhase(pThis->m_PopupRevealPhase, 1.0f, pThis->Client()->RenderFrameTime(), 0.0f);
	else
		pThis->m_PopupRevealPhase = 1.0f;
	const float Phase = AmfHudEditorAnimations::EaseOutCubic(pThis->m_PopupRevealPhase);

	CUIRect OuterRect = View;
	OuterRect.x -= POPUP_FRAME_MARGIN;
	OuterRect.y -= POPUP_FRAME_MARGIN;
	OuterRect.w += POPUP_FRAME_MARGIN * 2.0f;
	OuterRect.h += POPUP_FRAME_MARGIN * 2.0f;
	// Cached so RenderClosingPopupFrame() can replay the same box, shrinking, after this
	// popup is gone from CUi's popup stack (see the close-detection in OnRender()).
	pThis->m_LastPopupOuterRect = OuterRect;

	const CUIRect AnimRect = ComputeAnimRect(OuterRect, pThis->m_PopupGrowFromRight, Phase);
	DrawPopupFrame(AnimRect, Phase);
	pThis->Ui()->ClipEnable(&AnimRect);

	const bool Enabled = pThis->IsModuleEnabled(pThis->m_SelectedModule);
	CUIRect Title, ToggleButton, ScaleLabel, ScaleSlider, ResetScaleButton, ResetPositionButton, ResetAllButton;
	View.HSplitTop(16.0f, &Title, &View);
	pThis->Ui()->DoLabel(&Title, Localize(HudLayout::Name(pThis->m_SelectedModule)), 10.0f, TEXTALIGN_MC);
	View.HSplitTop(4.0f, nullptr, &View);
	View.HSplitTop(16.0f, &ToggleButton, &View);
	if(pThis->GameClient()->m_Menus.DoButton_CheckBox(&pThis->m_ToggleModuleButton, Localize("Enabled"), Enabled ? 1 : 0, &ToggleButton))
		pThis->SetModuleEnabled(pThis->m_SelectedModule, !Enabled);

	View.HSplitTop(4.0f, nullptr, &View);
	View.HSplitTop(12.0f, &ScaleLabel, &View);
	const int Scale = HudLayout::Get(pThis->m_SelectedModule, pThis->HudWidth(), pThis->HudHeight()).m_Scale;
	char aScale[32];
	str_format(aScale, sizeof(aScale), "%s %d%%", Localize("Scale"), Scale);
	pThis->Ui()->DoLabel(&ScaleLabel, aScale, 8.0f, TEXTALIGN_ML);

	View.HSplitTop(14.0f, &ScaleSlider, &View);
	const float Relative = CUi::ms_LinearScrollbarScale.ToRelative(Scale, 25, 300);
	const float NewRelative = pThis->Ui()->DoScrollbarH(&pThis->m_SelectedModule, &ScaleSlider, Relative);
	const int NewScale = CUi::ms_LinearScrollbarScale.ToAbsolute(NewRelative, 25, 300);
	const bool SupportsScale = SupportsModuleScale(pThis->m_SelectedModule);
	if(NewScale != Scale && SupportsScale)
	{
		// Scale is visual only: never rewrite a module's saved anchor from a
		// settings change, so Key Indicator style/scale/position stay independent.
		HudLayout::SetScale(pThis->m_SelectedModule, NewScale);
	}

	const bool IsKeyIndicator = pThis->m_SelectedModule == HudLayout::MODULE_KEYSTROKES_KEYBOARD;
	if(HudLayout::SupportsStyle(pThis->m_SelectedModule) && !IsKeyIndicator)
	{
		CUIRect StyleLabel, StyleButtons;
		View.HSplitTop(6.0f, nullptr, &View);
		View.HSplitTop(12.0f, &StyleLabel, &View);
		pThis->Ui()->DoLabel(&StyleLabel, Localize("Style"), 8.0f, TEXTALIGN_ML);
		View.HSplitTop(16.0f, &StyleButtons, &View);
		const int CurrentStyle = HudLayout::GetStyle(pThis->m_SelectedModule);
		constexpr float StyleButtonGap = 2.0f;
		const int StyleCount = HudLayout::STYLE_COUNT - HudLayout::STYLE_CLASSIC;
		const float StyleButtonWidth = std::max(0.0f, (StyleButtons.w - StyleButtonGap * (StyleCount - 1)) / StyleCount);
		for(int Style = HudLayout::STYLE_CLASSIC; Style < HudLayout::STYLE_COUNT; ++Style)
		{
			CUIRect Button;
			StyleButtons.VSplitLeft(StyleButtonWidth, &Button, &StyleButtons);
			if(Style + 1 < HudLayout::STYLE_COUNT)
				StyleButtons.VSplitLeft(StyleButtonGap, nullptr, &StyleButtons);
			if(pThis->Ui()->DoButton_PopupMenu(&pThis->m_aStyleButtons[Style], Localize(HudLayout::StyleName(Style)), &Button, 7.0f, TEXTALIGN_MC, 0.0f, CurrentStyle == Style))
				HudLayout::SetStyle(pThis->m_SelectedModule, Style);
		}
	}
	if(IsKeyIndicator)
	{
		// These two controls use exactly the same persistent config values as the
		// Visuals page. They are deliberately not HudLayout settings: style and
		// CPS must never alter this module's saved position or user scale.
		CUIRect KeyStyleLabel, KeyStyleButtons, ShowCpsButton;
		View.HSplitTop(6.0f, nullptr, &View);
		View.HSplitTop(12.0f, &KeyStyleLabel, &View);
		pThis->Ui()->DoLabel(&KeyStyleLabel, TCLocalize("Style", "AMF Client"), 8.0f, TEXTALIGN_ML);
		View.HSplitTop(16.0f, &KeyStyleButtons, &View);
		CUIRect DefaultStyleButton, MinecraftStyleButton;
		KeyStyleButtons.VSplitMid(&DefaultStyleButton, &MinecraftStyleButton, 1.0f);
		if(pThis->Ui()->DoButton_PopupMenu(&pThis->m_KeyIndicatorDefaultStyleButton, TCLocalize("Default", "AMF Client"), &DefaultStyleButton, 7.0f, TEXTALIGN_MC, 0.0f, g_Config.m_AmfKeyIndicatorStyle == 0))
			g_Config.m_AmfKeyIndicatorStyle = 0;
		if(pThis->Ui()->DoButton_PopupMenu(&pThis->m_KeyIndicatorMinecraftStyleButton, TCLocalize("Minecraft", "AMF Client"), &MinecraftStyleButton, 7.0f, TEXTALIGN_MC, 0.0f, g_Config.m_AmfKeyIndicatorStyle == 1))
			g_Config.m_AmfKeyIndicatorStyle = 1;

		View.HSplitTop(4.0f, nullptr, &View);
		View.HSplitTop(16.0f, &ShowCpsButton, &View);
		if(pThis->GameClient()->m_Menus.DoButton_CheckBox(&pThis->m_KeyIndicatorShowCpsButton, TCLocalize("Show CPS", "AMF Client"), g_Config.m_AmfKeyIndicatorShowCps, &ShowCpsButton))
			g_Config.m_AmfKeyIndicatorShowCps ^= 1;
	}

	View.HSplitTop(6.0f, nullptr, &View);
	View.HSplitTop(16.0f, &ResetScaleButton, &View);
	if(pThis->Ui()->DoButton_PopupMenu(&pThis->m_ResetScaleButton, Localize("Reset scale"), &ResetScaleButton, 8.0f, TEXTALIGN_MC))
		pThis->StartResetAnimation(pThis->m_SelectedModule, RESET_KIND_SCALE);

	View.HSplitTop(3.0f, nullptr, &View);
	View.HSplitTop(16.0f, &ResetPositionButton, &View);
	if(pThis->Ui()->DoButton_PopupMenu(&pThis->m_ResetPositionButton, Localize("Reset position"), &ResetPositionButton, 8.0f, TEXTALIGN_MC))
		pThis->StartResetAnimation(pThis->m_SelectedModule, RESET_KIND_POSITION);

	View.HSplitTop(3.0f, nullptr, &View);
	View.HSplitTop(16.0f, &ResetAllButton, &View);
	const ColorRGBA ResetAllColor(1.0f, 0.32f, 0.32f, 0.85f * pThis->Ui()->ButtonColorMul(&pThis->m_ResetSettingsButton));
	if(pThis->Ui()->DoButton_PopupMenu(&pThis->m_ResetSettingsButton, Localize("Reset all"), &ResetAllButton, 8.0f, TEXTALIGN_MC, 0.0f, false, true, ResetAllColor))
		pThis->StartResetAnimation(pThis->m_SelectedModule, RESET_KIND_ALL);

	pThis->Ui()->ClipDisable();
	return CUi::POPUP_KEEP_OPEN;
}

void CAmfHudEditor::OpenModuleSettings(const SModuleVisual &Visual)
{
	if(!IsEditableModule(Visual.m_Module))
		return;

	m_SelectedModule = Visual.m_Module;
	const float Width = HudWidth();
	const float Height = HudHeight();
	const float UiScaleX = Ui()->Screen()->w / std::max(Width, 1.0f);
	const float UiScaleY = Ui()->Screen()->h / std::max(Height, 1.0f);
	constexpr float PopupMargin = 5.0f;
	constexpr float PopupGap = 6.0f;
	const float PopupWidth = SETTINGS_POPUP_WIDTH;
	const float PopupHeight = Visual.m_Module == HudLayout::MODULE_KEYSTROKES_KEYBOARD ? KEY_INDICATOR_SETTINGS_POPUP_HEIGHT : SETTINGS_POPUP_HEIGHT;
	const CUIRect ModuleRectUi = {
		Visual.m_Rect.x * UiScaleX,
		Visual.m_Rect.y * UiScaleY,
		Visual.m_Rect.w * UiScaleX,
		Visual.m_Rect.h * UiScaleY};
	float PopupX = ModuleRectUi.x + ModuleRectUi.w + PopupGap;
	m_PopupGrowFromRight = false;
	if(PopupX + PopupWidth > Ui()->Screen()->w - PopupMargin)
	{
		PopupX = ModuleRectUi.x - PopupWidth - PopupGap;
		m_PopupGrowFromRight = true;
	}
	PopupX = std::clamp(PopupX, PopupMargin, std::max(PopupMargin, Ui()->Screen()->w - PopupWidth - PopupMargin));
	const float PopupY = std::clamp(ModuleRectUi.y, PopupMargin, std::max(PopupMargin, Ui()->Screen()->h - PopupHeight - PopupMargin));
	m_PopupRevealPhase = 0.0f;
	Ui()->ClosePopupMenus();
	// The popup's own border/background are drawn manually inside PopupModuleSettings so
	// they can grow in sync with the reveal animation, so make DoPopupMenu's copy invisible.
	SPopupMenuProperties Props;
	Props.m_BorderColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);
	Props.m_BackgroundColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);
	Ui()->DoPopupMenu(&m_SettingsPopupId, PopupX, PopupY, PopupWidth, PopupHeight, this, PopupModuleSettings, Props);
}

void CAmfHudEditor::RenderModuleOutline(const SModuleVisual &Visual, bool Hovered, bool Selected) const
{
	const CUIRect &Rect = Visual.m_Rect;
	ColorRGBA Color = Selected ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.92f) : (Hovered ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.78f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.46f));
	if(!Visual.m_Editable)
		Color.a *= 0.72f;
	if(!Visual.m_Enabled)
	{
		// Same real geometry, visibly muted. No preview renderer is created here.
		Graphics()->DrawRect(Rect.x, Rect.y, Rect.w, Rect.h, ColorRGBA(0.0f, 0.0f, 0.0f, 0.24f), IGraphics::CORNER_NONE, 0.0f);
		Color.a *= 0.46f;
	}

	DrawHudEditorBox(Graphics(), Rect, Color);
}

void CAmfHudEditor::RenderClosingPopupFrame() const
{
	// The real popup is already gone from CUi's stack by the time this runs (see
	// OnRender()'s close-detection), so this just replays its frame shrinking back
	// into the corner it grew from - the mirror image of PopupModuleSettings's opening draw.
	const float Phase = AmfHudEditorAnimations::EaseOutCubic(m_PopupClosePhase);
	if(Phase <= 0.0f)
		return;
	const CUIRect AnimRect = ComputeAnimRect(m_LastPopupOuterRect, m_PopupGrowFromRight, Phase);
	DrawPopupFrame(AnimRect, Phase);
}

void CAmfHudEditor::RenderModuleLabel(const SModuleVisual &Visual) const
{
	char aName[64];
	str_format(aName, sizeof(aName), "%s", Localize(HudLayout::Name(Visual.m_Module)));
	const char *pStatus = nullptr;
	if(Visual.m_Editable && !Visual.m_Enabled)
		pStatus = Localize("disabled");
	else if(!Visual.m_Editable)
		pStatus = Localize("preview");

	const float Width = HudWidth();
	const float Height = HudHeight();
	const float FontSize = 6.6f;
	const float StatusFontSize = 5.6f;
	const float NameWidth = TextRender()->TextWidth(FontSize, aName, -1, -1.0f);
	const float StatusWidth = pStatus ? TextRender()->TextWidth(StatusFontSize, pStatus, -1, -1.0f) + 6.0f : 0.0f;
	const float LabelW = 10.0f + NameWidth + StatusWidth;
	const float LabelH = 13.0f;
	constexpr float TailSize = 2.6f;
	float X = std::clamp(Visual.m_Rect.x + (Visual.m_Rect.w - LabelW) * 0.5f, 2.0f, Width - LabelW - 2.0f);
	float Y = Visual.m_Rect.y - LabelH - TailSize - 2.0f;
	const bool PointingDown = Y >= 2.0f;
	if(!PointingDown)
		Y = std::min(Height - LabelH - TailSize - 2.0f, Visual.m_Rect.y + Visual.m_Rect.h + TailSize + 2.0f);

	// Soft border first, then an inset fill, so the tooltip reads as a distinct floating
	// chip instead of a flat black box that blends into a dark HUD background.
	CUIRect LabelRect = {X, Y, LabelW, LabelH};
	Graphics()->DrawRect(LabelRect.x - 0.6f, LabelRect.y - 0.6f, LabelRect.w + 1.2f, LabelRect.h + 1.2f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.14f), IGraphics::CORNER_ALL, 6.0f);
	Graphics()->DrawRect(LabelRect.x, LabelRect.y, LabelRect.w, LabelRect.h, ColorRGBA(0.04f, 0.04f, 0.05f, 0.88f), IGraphics::CORNER_ALL, 5.5f);

	// Small tail connecting the chip to the element it describes, like a speech bubble.
	const float TailCenterX = std::clamp(Visual.m_Rect.x + Visual.m_Rect.w * 0.5f, LabelRect.x + TailSize * 2.0f, LabelRect.x + LabelRect.w - TailSize * 2.0f);
	const float TailBaseY = PointingDown ? LabelRect.y + LabelRect.h : LabelRect.y;
	const float TailDir = PointingDown ? 1.0f : -1.0f;
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(0.04f, 0.04f, 0.05f, 0.88f);
	IGraphics::CFreeformItem Tail(
		TailCenterX - TailSize, TailBaseY,
		TailCenterX + TailSize, TailBaseY,
		TailCenterX, TailBaseY + TailDir * TailSize * 1.5f,
		TailCenterX, TailBaseY + TailDir * TailSize * 1.5f);
	Graphics()->QuadsDrawFreeform(&Tail, 1);
	Graphics()->QuadsEnd();

	CUIRect NameRect, StatusRect;
	if(pStatus)
		LabelRect.VSplitRight(StatusWidth, &NameRect, &StatusRect);
	else
		NameRect = LabelRect;
	Ui()->DoLabel(&NameRect, aName, FontSize, TEXTALIGN_MC);
	if(pStatus)
	{
		TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.55f));
		Ui()->DoLabel(&StatusRect, pStatus, StatusFontSize, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
}

void CAmfHudEditor::RenderOverlay(vec2 MousePos)
{
	const float Width = HudWidth();
	const float Height = HudHeight();
	Graphics()->MapScreen(0.0f, 0.0f, Width, Height);
	Graphics()->TextureClear();
	Graphics()->DrawRect(0.0f, 0.0f, Width, Height, ColorRGBA(0.0f, 0.0f, 0.0f, 0.38f), IGraphics::CORNER_ALL, 0.0f);

	// The live HUD remains underneath the editor. Reservations are drawn below even
	// when a game-state dependent module is temporarily invisible.

	SModuleVisual aVisuals[MAX_MODULE_VISUALS];
	int Count = 0;
	CollectModuleVisuals(aVisuals, Count);
	for(int Pass = 0; Pass < 2; ++Pass)
	{
		const bool RenderEditable = Pass == 1;
		for(int i = 0; i < Count; ++i)
		{
			if(aVisuals[i].m_Editable != RenderEditable)
				continue;
			const bool Hovered = aVisuals[i].m_Module == m_HoveredModule;
			const bool Selected = aVisuals[i].m_Module == m_SelectedModule || aVisuals[i].m_Module == m_PressedModule;
			RenderModuleOutline(aVisuals[i], Hovered, Selected);
			if(Hovered)
				RenderModuleLabel(aVisuals[i]);
		}
	}

	CUIRect ResetRect = ResetAllRect(Width, Height);
	const bool ResetHovered = PointInRect(MousePos, ResetRect);
	const ColorRGBA ResetColor = m_PressedOnReset ? ColorRGBA(0.95f, 0.48f, 0.48f, 0.90f) :
							(ResetHovered ? ColorRGBA(0.95f, 0.48f, 0.48f, 0.55f) : ColorRGBA(0.95f, 0.48f, 0.48f, 0.36f));
	Graphics()->DrawRect(ResetRect.x, ResetRect.y, ResetRect.w, ResetRect.h, ResetColor, IGraphics::CORNER_ALL, 4.0f);
	Ui()->DoLabel(&ResetRect, Localize("Reset All"), 6.5f, TEXTALIGN_MC);

	Ui()->MapScreen();
	Ui()->RenderPopupMenus();
	if(m_PopupClosing)
	{
		if(AmfHudEditorAnimations::Enabled() && false)
			AmfHudEditorAnimations::UpdatePhase(m_PopupClosePhase, 0.0f, Client()->RenderFrameTime(), 0.0f);
		else
			m_PopupClosePhase = 0.0f;
		RenderClosingPopupFrame();
		if(m_PopupClosePhase <= 0.001f)
			m_PopupClosing = false;
	}
	Graphics()->MapScreen(0.0f, 0.0f, Width, Height);
	RenderTools()->RenderCursor(MousePos, 12.0f);
}

void CAmfHudEditor::OnRender()
{
	if(!m_Active)
		return;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		Deactivate();
		return;
	}

	Ui()->StartCheck();
	Ui()->Update();
	Ui()->MapScreen();
	UpdateResetAnimation();

	// Use the exact cursor coordinate CUi uses for all UI input, then map that
	// one point into the HUD canvas. This keeps hit-test, grab offset, drag and
	// the editor's cursor rendering in one coordinate space even with UI scaling.
	const CUIRect UiScreen = *Ui()->Screen();
	const vec2 UiMousePos = Ui()->MousePos();
	const vec2 MousePos(
		(UiMousePos.x - UiScreen.x) * HudWidth() / std::max(UiScreen.w, 1.0f),
		(UiMousePos.y - UiScreen.y) * HudHeight() / std::max(UiScreen.h, 1.0f));
	const bool LeftDown = Input()->KeyIsPressed(KEY_MOUSE_1);
	const bool RightDown = Input()->KeyIsPressed(KEY_MOUSE_2);
	const bool LeftClicked = LeftDown && !m_MouseDownLast;
	const bool RightClicked = RightDown && !m_RightMouseDownLast;
	const bool PopupOpen = Ui()->IsPopupOpen(&m_SettingsPopupId);

	m_HoveredModule = HitTestModule(MousePos);
	CUIRect ResetRect = ResetAllRect(HudWidth(), HudHeight());
	const bool ResetHovered = PointInRect(MousePos, ResetRect);

	if(RightClicked && m_HoveredModule != HudLayout::MODULE_COUNT && IsEditableModule(m_HoveredModule))
	{
		m_SelectedModule = m_HoveredModule;
		OpenModuleSettings(GetModuleVisual(m_HoveredModule));
	}

	if(PopupOpen)
	{
		m_Dragging = false;
		m_PressedModule = HudLayout::MODULE_COUNT;
		m_PressedOnReset = false;
	}
	else if(LeftClicked && ResetHovered)
	{
		SModuleVisual aVisuals[MAX_MODULE_VISUALS];
		int Count = 0;
		CollectModuleVisuals(aVisuals, Count);
		for(int i = 0; i < Count; ++i)
		{
			if(IsEditableModule(aVisuals[i].m_Module))
				StartResetAnimation(aVisuals[i].m_Module, RESET_KIND_ALL);
		}
		m_Dragging = false;
		m_PressedModule = HudLayout::MODULE_COUNT;
		m_SelectedModule = HudLayout::MODULE_COUNT;
		m_PressedOnReset = true;
		m_SuppressCloseAnim = true;
		Ui()->ClosePopupMenus();
	}
	else if(LeftClicked)
	{
		m_PressedOnReset = false;
		m_PressMousePos = MousePos;
		m_SelectedModule = m_HoveredModule;
		m_PressedModule = (m_HoveredModule != HudLayout::MODULE_COUNT && IsEditableModule(m_HoveredModule)) ? m_HoveredModule : HudLayout::MODULE_COUNT;
		if(m_PressedModule != HudLayout::MODULE_COUNT)
		{
			CancelResetAnimation(m_PressedModule);
			const SModuleVisual Visual = GetModuleVisual(m_PressedModule);
			m_DragAnchorRect = Visual.m_Rect;
			m_DragMouseOffset = MousePos - vec2(Visual.m_Rect.x, Visual.m_Rect.y);
		}
	}
	else if(LeftDown && m_MouseDownLast && m_PressedModule != HudLayout::MODULE_COUNT)
	{
		if(!m_Dragging && distance(m_PressMousePos, MousePos) > 2.0f)
			m_Dragging = true;
		UpdateDragging(MousePos);
	}
	else if(!LeftDown && m_MouseDownLast)
	{
		m_Dragging = false;
		m_PressedModule = HudLayout::MODULE_COUNT;
		m_PressedOnReset = false;
	}

	m_MouseDownLast = LeftDown;
	m_RightMouseDownLast = RightDown;

	RenderOverlay(MousePos);

	// CUi closes the settings popup on its own (click elsewhere, Escape) during the
	// RenderPopupMenus() call inside RenderOverlay(), without going through any of our own
	// ClosePopupMenus() calls above, so catch that transition here (comparing against
	// PopupOpen from the top of this same frame) and play a closing animation instead of
	// just having it vanish.
	if(PopupOpen && !Ui()->IsPopupOpen(&m_SettingsPopupId) && !m_SuppressCloseAnim)
	{
		m_PopupClosing = true;
		m_PopupClosePhase = 1.0f;
	}
	m_SuppressCloseAnim = false;

	Ui()->FinishCheck();
	Ui()->ClearHotkeys();
}
