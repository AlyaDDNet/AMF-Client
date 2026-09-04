/* HUD layout storage adapted for AMF Client. */
#ifndef GAME_CLIENT_COMPONENTS_HUD_LAYOUT_H
#define GAME_CLIENT_COMPONENTS_HUD_LAYOUT_H

#include <engine/graphics.h>

class IConsole;
class IConfigManager;

// Shared position/scale/enabled storage for HUD modules that can be repositioned
// with the HUD editor (see bestclient/hud_editor.h). Every module gets a slot here
// even if no editor/renderer wires it up yet, so adding a new draggable HUD element
// later only needs: a case in the enum below (already reserved), a rect getter +
// preview renderer in the owning component, and one line in CHudEditor::CollectModuleVisuals.
namespace HudLayout
{

	enum EModule
	{
		MODULE_MINI_VOTE = 0,
		MODULE_FROZEN_HUD,
		MODULE_MOVEMENT_INFO,
		MODULE_NOTIFY_LAST,
		MODULE_FPS,
		MODULE_PING,
		MODULE_GAME_TIMER,
		MODULE_HOOK_COMBO,
		MODULE_LOCAL_TIME,
		MODULE_SPECTATOR_COUNT,
		MODULE_SCORE,
		MODULE_MUSIC_PLAYER,
		MODULE_VOICE_TALKERS,
		MODULE_VOICE_STATUS,
		MODULE_CHAT,
		MODULE_VOTES,
		MODULE_LOCK_CAM,
		MODULE_KILLFEED,
		MODULE_FINISH_PREDICTION,
		MODULE_KEYSTROKES_KEYBOARD,
		MODULE_KEYSTROKES_MOUSE,
		MODULE_DUMMY_ACTIONS,
		MODULE_PLAYER_STATE,
		MODULE_COUNT,
	};

	// The on-disk `hud_layout_set` format stores these numeric IDs. Keep every
	// pre-1.1 module fixed: new modules must be appended, never inserted.
	static_assert(MODULE_MINI_VOTE == 0);
	static_assert(MODULE_PING == 5);
	static_assert(MODULE_KEYSTROKES_KEYBOARD == 19);
	static_assert(MODULE_DUMMY_ACTIONS == 21);
	static_assert(MODULE_PLAYER_STATE == 22);

	struct SModuleLayout
	{
		float m_X;
		float m_Y;
		int m_Scale;
		int m_Mode;
		bool m_Enabled;
		bool m_BackgroundEnabled;
		unsigned m_BackgroundColor;
		int m_Style;
	};

	struct SModuleRect
	{
		float m_X;
		float m_Y;
		float m_W;
		float m_H;
		float m_Rounding;
	};

	constexpr float CANVAS_WIDTH = 500.0f;
	constexpr float CANVAS_HEIGHT = 300.0f;

	enum EPositionMode
	{
		POSITION_MODE_TOP_LEFT = 0,
		POSITION_MODE_BOTTOM_RIGHT,
	};

	enum EStyle
	{
		STYLE_CLASSIC = 0,
		STYLE_CLEAN,
		STYLE_ROUNDED,
		STYLE_MINIMAL,
		STYLE_COUNT,
	};

	// Modules whose position/scale/enabled state can be edited in the HUD editor.
	bool IsEditableModule(EModule Module);
	const char *Name(EModule Module);
	// Resolves a module's layout for the given HUD canvas size: dynamic aspect-relative
	// default position until the user drags it once, then the stored runtime position.
	SModuleLayout Get(EModule Module, float HudWidth, float HudHeight);
	// The layout a module would have if it had never been overridden (position, scale
	// and everything else), regardless of its *current* stored position/scale. Used by
	// the HUD editor as the target for animated "reset position"/"reset scale".
	SModuleLayout GetDefault(EModule Module, float HudWidth, float HudHeight);
	bool HasRuntimeOverride(EModule Module);
	bool HasPositionOverride(EModule Module);
	void SetPosition(EModule Module, float X, float Y);
	void SetPosition(EModule Module, float X, float Y, EPositionMode PositionMode);
	void SetScale(EModule Module, int Scale);
	void SetStyle(EModule Module, int Style);
	int GetStyle(EModule Module);
	bool SupportsStyle(EModule Module);
	const char *StyleName(int Style);
	void SetEnabled(EModule Module, bool Enabled);
	bool IsEnabled(EModule Module);
	void ResetPosition(EModule Module);
	void ResetScale(EModule Module);
	void ResetSettings(EModule Module);
	void ResetEditableModules();
	SModuleRect ClampRectToScreen(const SModuleRect &Rect, float HudWidth, float HudHeight);
	float CanvasXToHud(float CanvasX, float HudWidth);
	int BackgroundCorners(int DefaultCorners, float RectX, float RectY, float RectW, float RectH, float CanvasWidth, float CanvasHeight);
	void OnConsoleInit(IConsole *pConsole, IConfigManager *pConfigManager);

} // namespace HudLayout

#endif
