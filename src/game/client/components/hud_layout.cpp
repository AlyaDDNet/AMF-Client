/* HUD layout storage adapted for AMF Client. */
#include "hud_layout.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/config.h>
#include <engine/console.h>
#include <engine/shared/config.h>

#include <game/client/components/hud.h>

#include <algorithm>

namespace HudLayout
{

	namespace
	{
		// These defaults mirror the authored HUD Editor layout. Movement Info,
		// Dummy Actions and the Key Indicator retain their right-edge anchor at
		// every aspect ratio through DynamicDefaultLayout().
		constexpr float MOVEMENT_INFO_DEFAULT_Y = 196.389f;
		constexpr float DUMMY_ACTIONS_DEFAULT_Y = 156.0f;
		constexpr float KEY_INDICATOR_DEFAULT_Y = 88.571f;
		constexpr float KEY_INDICATOR_BASE_WIDTH = 65.24f;

		// Default layout per module, indexed by EModule. Positions are on the
		// CANVAS_WIDTH x CANVAS_HEIGHT canvas unless HasDynamicDefault() applies.
		static const SModuleLayout gs_aModuleLayouts[MODULE_COUNT] = {
			{0.0f, 60.0f, 100, 0, true, true, 0x66000000U, STYLE_CLASSIC}, // MINI_VOTE
			{250.0f, 44.0f, 100, 0, true, false, 0x66000000U, STYLE_CLASSIC}, // REMAINING_PLAYERS
			{441.875f, MOVEMENT_INFO_DEFAULT_Y, 100, 0, true, true, 0x66000000U, STYLE_CLASSIC}, // MOVEMENT_INFO
			{100.0f, 3.0f, 100, 0, true, false, 0x66000000U, STYLE_CLASSIC}, // NOTIFY_LAST
			{500.0f, 5.0f, 100, 0, true, false, 0x66000000U, STYLE_CLASSIC}, // FPS
			{500.0f, 20.0f, 100, 0, true, false, 0x66000000U, STYLE_CLASSIC}, // PING
			{233.0f, 64.0f, 100, 0, true, false, 0x66000000U, STYLE_CLASSIC}, // GAME_TIMER
			{220.0f, 240.0f, 100, 0, true, false, 0x66000000U, STYLE_CLASSIC}, // HOOK_COMBO
			{110.0f, 2.0f, 100, 0, true, true, 0x66000000U, STYLE_CLASSIC}, // LOCAL_TIME
			{500.0f, 141.0f, 100, 0, true, true, 0x66000000U, STYLE_CLASSIC}, // SPECTATOR_COUNT
			{460.0f, 229.0f, 100, 0, true, true, 0x40000000U, STYLE_CLASSIC}, // SCORE
			{198.0f, 20.0f, 100, 0, true, false, 0x1E59A36BU, STYLE_CLASSIC}, // MUSIC_PLAYER
			{0.0f, 100.0f, 100, 0, true, false, 0x66000000U, STYLE_CLASSIC}, // VOICE_TALKERS
			{136.0f, 0.0f, 100, 0, true, false, 0x66000000U, STYLE_CLASSIC}, // VOICE_STATUS
			{5.0f, 278.0f, 100, 0, true, false, 0x66000000U, STYLE_CLASSIC}, // CHAT
			{0.0f, 60.0f, 100, 0, true, true, 0x66000000U, STYLE_CLASSIC}, // VOTES
			{250.0f, 200.0f, 65, 0, true, true, 0x66000000U, STYLE_CLASSIC}, // LOCK_CAM
			{490.0f, 5.0f, 100, 0, true, false, 0x66000000U, STYLE_CLASSIC}, // KILLFEED
			{0.0f, 129.0f, 100, 0, true, true, 0x66000000U, STYLE_CLASSIC}, // FINISH_PREDICTION
			{457.186f, KEY_INDICATOR_DEFAULT_Y, 70, 0, true, false, 0x66000000U, STYLE_CLASSIC}, // KEYSTROKES_KEYBOARD
			// KEYSTROKES_MOUSE: X/Y here are unused - see HasDynamicDefault()/DynamicDefaultLayout()
			// below, which computes its default position directly in HUD-pixel space (not
			// canvas-relative like most modules), offset past the keyboard's *current* preset
			// width so it doesn't drift into the keyboard at non-widescreen aspect ratios or
			// when the keyboard preset changes.
			{84.5f, 152.0f, 100, 0, true, false, 0x66000000U, STYLE_CLASSIC}, // KEYSTROKES_MOUSE
			{485.0f, DUMMY_ACTIONS_DEFAULT_Y, 100, 0, true, true, 0x66000000U, STYLE_CLASSIC}, // DUMMY_ACTIONS
			{5.0f, 5.0f, 100, 0, true, false, 0x66000000U, STYLE_CLASSIC}, // PLAYER_STATE
		};

		static const char *gs_apModuleNames[MODULE_COUNT] = {
			"Mini Vote",
			"Remaining Players",
			"Movement Info",
			"Notify Last",
			"FPS",
			"Prediction Time",
			"Game Timer",
			"Hook Combo",
			"Local Time",
			"Spectator Count",
			"Score",
			"Music Player",
			"Voice HUD",
			"Voice Mute Icons",
			"Ingame Chat",
			"Votes",
			"Lock Cam",
			"Killfeed",
			"Finish Prediction",
			"Keyboard",
			"Mouse",
			"Dummy Actions",
			"DDNet Player HUD",
		};

		static SModuleLayout gs_aRuntimeModuleLayouts[MODULE_COUNT];
		static bool gs_RuntimeLayoutsInitialized = false;
		static bool gs_ConfigCallbackRegistered = false;
		static bool gs_ConsoleCommandRegistered = false;

		// A HUD module that already has a feature-level show/hide setting must not
		// acquire a second visibility state in the layout file. These mappings keep
		// the editor, renderer and Settings page on the same config value.
		bool HasCanonicalEnableConfig(EModule Module)
		{
			switch(Module)
			{
			case MODULE_GAME_TIMER:
			case MODULE_PING:
			case MODULE_SCORE:
			case MODULE_FROZEN_HUD:
			case MODULE_LOCAL_TIME:
			case MODULE_CHAT:
			case MODULE_DUMMY_ACTIONS:
			case MODULE_MUSIC_PLAYER:
			case MODULE_KEYSTROKES_KEYBOARD:
			case MODULE_MOVEMENT_INFO:
			case MODULE_PLAYER_STATE:
			case MODULE_KILLFEED:
				return true;
			default:
				return false;
			}
		}

		bool CanonicalEnabled(EModule Module, bool LayoutEnabled)
		{
			switch(Module)
			{
			case MODULE_GAME_TIMER:
				return g_Config.m_ClShowhudTimer != 0;
			case MODULE_PING:
				return g_Config.m_ClShowpred != 0;
			case MODULE_SCORE:
				return g_Config.m_ClShowhudScore != 0;
			case MODULE_FROZEN_HUD:
				return g_Config.m_TcShowFrozenText > 0;
			case MODULE_LOCAL_TIME:
				return g_Config.m_ClShowLocalTimeAlways != 0;
			case MODULE_CHAT:
				return g_Config.m_ClShowChat != 0;
			case MODULE_DUMMY_ACTIONS:
				return g_Config.m_ClShowhudDummyActions != 0;
			case MODULE_MUSIC_PLAYER:
				return g_Config.m_AmfMusicPlayer != 0;
			case MODULE_KEYSTROKES_KEYBOARD:
				return g_Config.m_AmfKeyIndicator != 0;
			case MODULE_MOVEMENT_INFO:
				return g_Config.m_ClShowhudPlayerPosition != 0 || g_Config.m_ClShowhudPlayerSpeed != 0 || g_Config.m_ClShowhudPlayerAngle != 0;
			case MODULE_PLAYER_STATE:
				return g_Config.m_ClShowhudHealthAmmo != 0 || g_Config.m_ClShowhudDDRace != 0;
			case MODULE_KILLFEED:
				return g_Config.m_ClShowKillMessages != 0 || g_Config.m_ClShowFinishMessages != 0;
			default:
				return LayoutEnabled;
			}
		}

		void SetCanonicalEnabled(EModule Module, bool Enabled)
		{
			switch(Module)
			{
			case MODULE_GAME_TIMER:
				g_Config.m_ClShowhudTimer = Enabled;
				break;
			case MODULE_PING:
				g_Config.m_ClShowpred = Enabled;
				break;
			case MODULE_SCORE:
				g_Config.m_ClShowhudScore = Enabled;
				break;
			case MODULE_FROZEN_HUD:
				g_Config.m_TcShowFrozenText = Enabled ? std::max(g_Config.m_TcShowFrozenText, 1) : 0;
				break;
			case MODULE_LOCAL_TIME:
				g_Config.m_ClShowLocalTimeAlways = Enabled;
				break;
			case MODULE_CHAT:
				g_Config.m_ClShowChat = Enabled ? std::max(g_Config.m_ClShowChat, 1) : 0;
				break;
			case MODULE_DUMMY_ACTIONS:
				g_Config.m_ClShowhudDummyActions = Enabled;
				break;
			case MODULE_MUSIC_PLAYER:
				g_Config.m_AmfMusicPlayer = Enabled;
				break;
			case MODULE_KEYSTROKES_KEYBOARD:
				g_Config.m_AmfKeyIndicator = Enabled;
				break;
			case MODULE_MOVEMENT_INFO:
				if(Enabled)
				{
					if(!CanonicalEnabled(Module, false))
						g_Config.m_ClShowhudPlayerPosition = 1;
				}
				else
				{
					g_Config.m_ClShowhudPlayerPosition = 0;
					g_Config.m_ClShowhudPlayerSpeed = 0;
					g_Config.m_ClShowhudPlayerAngle = 0;
				}
				break;
			case MODULE_PLAYER_STATE:
				if(Enabled)
				{
					if(!CanonicalEnabled(Module, false))
						g_Config.m_ClShowhudHealthAmmo = 1;
				}
				else
				{
					g_Config.m_ClShowhudHealthAmmo = 0;
					g_Config.m_ClShowhudDDRace = 0;
				}
				break;
			case MODULE_KILLFEED:
				if(Enabled)
				{
					if(!CanonicalEnabled(Module, false))
						g_Config.m_ClShowKillMessages = 1;
				}
				else
				{
					g_Config.m_ClShowKillMessages = 0;
					g_Config.m_ClShowFinishMessages = 0;
				}
				break;
			default:
				break;
			}
		}

		void EnsureRuntimeLayouts()
		{
			if(gs_RuntimeLayoutsInitialized)
				return;
			for(int i = 0; i < MODULE_COUNT; ++i)
				gs_aRuntimeModuleLayouts[i] = gs_aModuleLayouts[i];
			gs_RuntimeLayoutsInitialized = true;
		}

		bool HasRuntimeOverrideInternal(EModule Module)
		{
			EnsureRuntimeLayouts();
			const SModuleLayout &Runtime = gs_aRuntimeModuleLayouts[Module];
			const SModuleLayout &Default = gs_aModuleLayouts[Module];
			// 1.1 briefly changed Ping's authored default from Y=20 to Y=40.
			// Saved 1.0.4 layouts therefore looked like a manual position override,
			// which converted X=500 from the old dynamic right edge to the canvas
			// edge and pushed the prediction text right. Both serialized defaults are
			// defaults, not user moves.
			const bool LegacyPingDefault = Module == MODULE_PING && Runtime.m_X == 500.0f && (Runtime.m_Y == 20.0f || Runtime.m_Y == 40.0f) && Runtime.m_Mode == POSITION_MODE_TOP_LEFT;
			const bool RuntimeEnabled = CanonicalEnabled(Module, Runtime.m_Enabled);
			const bool DefaultEnabled = CanonicalEnabled(Module, Default.m_Enabled);
			return (!LegacyPingDefault && (Runtime.m_X != Default.m_X || Runtime.m_Y != Default.m_Y || Runtime.m_Mode != Default.m_Mode)) ||
			       Runtime.m_Scale != Default.m_Scale ||
			       RuntimeEnabled != DefaultEnabled ||
			       Runtime.m_BackgroundEnabled != Default.m_BackgroundEnabled ||
				       Runtime.m_BackgroundColor != Default.m_BackgroundColor ||
			       Runtime.m_Style != Default.m_Style;
		}

		bool HasPositionOverrideInternal(EModule Module)
		{
			EnsureRuntimeLayouts();
			const SModuleLayout &Runtime = gs_aRuntimeModuleLayouts[Module];
			if(Module == MODULE_PING && Runtime.m_X == 500.0f && (Runtime.m_Y == 20.0f || Runtime.m_Y == 40.0f) && Runtime.m_Mode == POSITION_MODE_TOP_LEFT)
				return false;
			const SModuleLayout &Default = gs_aModuleLayouts[Module];
			return Runtime.m_X != Default.m_X || Runtime.m_Y != Default.m_Y || Runtime.m_Mode != Default.m_Mode;
		}

		// Modules whose default position is computed directly in HUD-pixel space (as
		// opposed to a fixed canvas coordinate that scales with aspect ratio via
		// CanvasXToHud()) until the user drags them for the first time. Most of these are
		// right/center anchored; KEYSTROKES_MOUSE instead needs an aspect-independent
		// offset from the keyboard so the two don't drift into each other - see
		// DynamicDefaultLayout() below.
		bool HasDynamicDefault(EModule Module)
		{
			switch(Module)
			{
			case MODULE_MINI_VOTE:
			case MODULE_FROZEN_HUD:
			case MODULE_MOVEMENT_INFO:
			case MODULE_NOTIFY_LAST:
			case MODULE_FPS:
			case MODULE_PING:
			case MODULE_GAME_TIMER:
			case MODULE_HOOK_COMBO:
			case MODULE_LOCAL_TIME:
			case MODULE_KEYSTROKES_MOUSE:
			case MODULE_KEYSTROKES_KEYBOARD:
			case MODULE_DUMMY_ACTIONS:
				return true;
			default:
				return false;
			}
		}

		// The default layout for a module at a given HUD size, as if it had never been
		// overridden - used both by ResolveBaseLayout() below and by the public GetDefault()
		// (see hud_layout.h), which the HUD editor uses as the animation target for
		// "reset position"/"reset scale". Modules outside HasDynamicDefault() just keep
		// gs_aModuleLayouts[Module] unchanged (the switch's default case).
		SModuleLayout DynamicDefaultLayout(EModule Module, float HudWidth, float HudHeight)
		{
			SModuleLayout Layout = gs_aModuleLayouts[Module];
			switch(Module)
			{
			case MODULE_MINI_VOTE:
				Layout.m_X = 0.0f;
				Layout.m_Y = 60.0f;
				break;
			case MODULE_FROZEN_HUD:
				Layout.m_X = HudWidth * 0.5f;
				// Keep the Reset All default below the compact Music Player instead of
				// sharing its top HUD band. The 20 HUD-unit separation remains well
				// above the requested three physical pixels at every supported scale.
				Layout.m_Y = 44.0f;
				break;
			case MODULE_MOVEMENT_INFO:
				Layout.m_X = HudWidth - 62.0f * (Layout.m_Scale / 100.0f);
				Layout.m_Y = MOVEMENT_INFO_DEFAULT_Y;
				break;
			case MODULE_NOTIFY_LAST:
				Layout.m_X = (float)round_to_int(HudWidth * 0.2f);
				Layout.m_Y = (float)round_to_int(HudHeight * 0.01f);
				break;
			case MODULE_FPS:
				Layout.m_X = (float)round_to_int(HudWidth - 26.0f);
				Layout.m_Y = 5.0f;
				break;
			case MODULE_PING:
				Layout.m_X = (float)round_to_int(HudWidth - 26.0f);
				Layout.m_Y = 20.0f;
				break;
			case MODULE_GAME_TIMER:
				Layout.m_X = (float)round_to_int(HudWidth * 0.5f - 22.0f);
				Layout.m_Y = 0.0f;
				break;
			case MODULE_LOCAL_TIME:
				Layout.m_X = HudWidth / 7.0f * 3.0f;
				Layout.m_Y = 0.0f;
				break;
			case MODULE_KEYSTROKES_MOUSE:
				// A fixed HUD-pixel gap past the keyboard's own default (100% scale) width for
				// whichever keyboard preset is currently selected, not canvas-relative -
				// CanvasXToHud() would shrink this offset at narrower aspect ratios while the
				// keyboard's own pixel width stays fixed, drifting the two into each other.
				// Recomputed from the live preset (rather than assuming the minimal preset) so
				// switching keyboard presets doesn't make the mouse module overlap or drift away.
				// Only chases the keyboard like this while the keyboard is also still at its own
				// default (X = 0) - once it's been dragged, this offset is no longer measured
				// from where the keyboard actually renders, so keep the flat fallback instead of
				// sending the mouse to an unrelated spot.
				if(!HasRuntimeOverrideInternal(MODULE_KEYSTROKES_KEYBOARD))
					Layout.m_X = 72.0f;
				Layout.m_Y = 152.0f;
				break;
			case MODULE_KEYSTROKES_KEYBOARD:
				Layout.m_X = HudWidth - KEY_INDICATOR_BASE_WIDTH * (Layout.m_Scale / 100.0f);
				Layout.m_Y = KEY_INDICATOR_DEFAULT_Y;
				break;
			case MODULE_DUMMY_ACTIONS:
				Layout.m_X = HudWidth - 16.0f * (Layout.m_Scale / 100.0f);
				Layout.m_Y = DUMMY_ACTIONS_DEFAULT_Y;
				break;
			default:
				break;
			}
			return Layout;
		}

		SModuleLayout ResolveBaseLayout(EModule Module, float HudWidth, float HudHeight)
		{
			SModuleLayout Layout;
			if(HasDynamicDefault(Module) && !HasPositionOverrideInternal(Module))
			{
				Layout = DynamicDefaultLayout(Module, HudWidth, HudHeight);
				// Scale/enabled/style are independently persisted properties. Changing
				// any of them must not turn a right/center dynamic default into the
				// static canvas fallback and visually move the element.
				EnsureRuntimeLayouts();
				const SModuleLayout &Runtime = gs_aRuntimeModuleLayouts[Module];
				Layout.m_Scale = Runtime.m_Scale;
				Layout.m_Enabled = Runtime.m_Enabled;
				Layout.m_BackgroundEnabled = Runtime.m_BackgroundEnabled;
				Layout.m_BackgroundColor = Runtime.m_BackgroundColor;
				Layout.m_Style = Runtime.m_Style;
				// Right-aligned dynamic modules must calculate their default anchor
				// after the persisted scale has been restored. Otherwise increasing
				// Scale starts from a 100% right edge and ClampRectToScreen pushes the
				// visual box left while its editor frame stays elsewhere.
				if(Module == MODULE_MOVEMENT_INFO)
					Layout.m_X = HudWidth - 62.0f * (Layout.m_Scale / 100.0f);
				else if(Module == MODULE_KEYSTROKES_KEYBOARD)
					Layout.m_X = HudWidth - KEY_INDICATOR_BASE_WIDTH * (Layout.m_Scale / 100.0f);
				else if(Module == MODULE_DUMMY_ACTIONS)
					Layout.m_X = HudWidth - 16.0f * (Layout.m_Scale / 100.0f);
			}
			else
			{
				EnsureRuntimeLayouts();
				Layout = gs_aRuntimeModuleLayouts[Module];
				Layout.m_X = CanvasXToHud(Layout.m_X, HudWidth);
			}
			Layout.m_Enabled = CanonicalEnabled(Module, Layout.m_Enabled);
			return Layout;
		}

		void ConHudLayoutSet(IConsole::IResult *pResult, void *pUserData)
		{
			(void)pUserData;

			const int ModuleIndex = pResult->GetInteger(0);
			if(ModuleIndex < 0 || ModuleIndex >= MODULE_COUNT)
				return;
			if(ModuleIndex == MODULE_GAME_TIMER)
				return;

			EnsureRuntimeLayouts();
			const EModule Module = (EModule)ModuleIndex;
			SModuleLayout Layout = gs_aRuntimeModuleLayouts[Module];
			Layout.m_X = pResult->GetFloat(1);
			Layout.m_Y = pResult->GetFloat(2);
			Layout.m_Scale = std::clamp(pResult->GetInteger(3), 25, 300);
			Layout.m_Mode = pResult->GetInteger(4);
			Layout.m_BackgroundEnabled = pResult->GetInteger(5) != 0;
			Layout.m_BackgroundColor = (unsigned)pResult->GetInteger(6);
			if(pResult->NumArguments() > 7 && !HasCanonicalEnableConfig(Module))
				Layout.m_Enabled = pResult->GetInteger(7) != 0;
			if(pResult->NumArguments() > 8)
				Layout.m_Style = std::clamp(pResult->GetInteger(8), (int)STYLE_CLASSIC, (int)STYLE_MINIMAL);

			gs_aRuntimeModuleLayouts[Module] = Layout;
		}

		void ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData)
		{
			(void)pUserData;
			EnsureRuntimeLayouts();

			char aLine[256];
			for(int Module = 0; Module < MODULE_COUNT; ++Module)
			{
				if(Module == MODULE_GAME_TIMER)
					continue;
				const SModuleLayout &Layout = gs_aRuntimeModuleLayouts[Module];
				str_format(
					aLine,
					sizeof(aLine),
					"hud_layout_set %d %.3f %.3f %d %d %d %u %d %d",
					Module,
					Layout.m_X,
					Layout.m_Y,
					Layout.m_Scale,
					Layout.m_Mode,
					Layout.m_BackgroundEnabled ? 1 : 0,
					Layout.m_BackgroundColor,
					CanonicalEnabled((EModule)Module, Layout.m_Enabled) ? 1 : 0,
					Layout.m_Style);
				pConfigManager->WriteLine(aLine, ConfigDomain::AMFHUDLAYOUT);
			}
		}

	} // namespace

	SModuleLayout Get(EModule Module, float HudWidth, float HudHeight)
	{
		SModuleLayout Layout = ResolveBaseLayout(Module, HudWidth, HudHeight);
		// Per-module scale is already stored in the AMF layout entry.
		return Layout;
	}

	SModuleLayout GetDefault(EModule Module, float HudWidth, float HudHeight)
	{
		SModuleLayout Layout = DynamicDefaultLayout(Module, HudWidth, HudHeight);
		if(!HasDynamicDefault(Module))
			Layout.m_X = CanvasXToHud(Layout.m_X, HudWidth);
		return Layout;
	}

	bool HasRuntimeOverride(EModule Module)
	{
		return Module >= 0 && Module < MODULE_COUNT && HasRuntimeOverrideInternal(Module);
	}

	bool HasPositionOverride(EModule Module)
	{
		if(Module < 0 || Module >= MODULE_COUNT)
			return false;
		return HasPositionOverrideInternal(Module);
	}

	bool IsEditableModule(EModule Module)
	{
		return Module >= 0 && Module < MODULE_COUNT && Module != MODULE_SPECTATOR_COUNT;
	}

	const char *Name(EModule Module)
	{
		return Module >= 0 && Module < MODULE_COUNT ? gs_apModuleNames[Module] : "HUD Module";
	}

	void SetPosition(EModule Module, float X, float Y)
	{
		EnsureRuntimeLayouts();
		gs_aRuntimeModuleLayouts[Module].m_X = X;
		gs_aRuntimeModuleLayouts[Module].m_Y = Y;
	}

	void SetPosition(EModule Module, float X, float Y, EPositionMode PositionMode)
	{
		EnsureRuntimeLayouts();
		gs_aRuntimeModuleLayouts[Module].m_X = X;
		gs_aRuntimeModuleLayouts[Module].m_Y = Y;
		gs_aRuntimeModuleLayouts[Module].m_Mode = PositionMode;
	}

	void SetScale(EModule Module, int Scale)
	{
		EnsureRuntimeLayouts();
		gs_aRuntimeModuleLayouts[Module].m_Scale = std::clamp(Scale, 25, 300);
	}

	void SetStyle(EModule Module, int Style)
	{
		if(!SupportsStyle(Module))
			return;
		EnsureRuntimeLayouts();
		gs_aRuntimeModuleLayouts[Module].m_Style = std::clamp(Style, (int)STYLE_CLASSIC, (int)STYLE_MINIMAL);
	}

	int GetStyle(EModule Module)
	{
		return Get(Module, CANVAS_WIDTH, CANVAS_HEIGHT).m_Style;
	}

	bool SupportsStyle(EModule Module)
	{
		return Module == MODULE_KEYSTROKES_KEYBOARD || Module == MODULE_VOTES;
	}

	const char *StyleName(int Style)
	{
		static const char *s_apStyleNames[STYLE_COUNT] = {"Classic", "Clean", "Rounded", "Minimal"};
		return Style >= STYLE_CLASSIC && Style < STYLE_COUNT ? s_apStyleNames[Style] : s_apStyleNames[STYLE_CLASSIC];
	}

	void SetEnabled(EModule Module, bool Enabled)
	{
		EnsureRuntimeLayouts();
		if(HasCanonicalEnableConfig(Module))
			SetCanonicalEnabled(Module, Enabled);
		gs_aRuntimeModuleLayouts[Module].m_Enabled = Enabled;
	}

	bool IsEnabled(EModule Module)
	{
		EnsureRuntimeLayouts();
		return CanonicalEnabled(Module, gs_aRuntimeModuleLayouts[Module].m_Enabled);
	}

	void ResetPosition(EModule Module)
	{
		EnsureRuntimeLayouts();
		gs_aRuntimeModuleLayouts[Module].m_X = gs_aModuleLayouts[Module].m_X;
		gs_aRuntimeModuleLayouts[Module].m_Y = gs_aModuleLayouts[Module].m_Y;
		gs_aRuntimeModuleLayouts[Module].m_Mode = gs_aModuleLayouts[Module].m_Mode;
	}

	void ResetScale(EModule Module)
	{
		EnsureRuntimeLayouts();
		gs_aRuntimeModuleLayouts[Module].m_Scale = gs_aModuleLayouts[Module].m_Scale;
	}

	void ResetSettings(EModule Module)
	{
		EnsureRuntimeLayouts();
		gs_aRuntimeModuleLayouts[Module] = gs_aModuleLayouts[Module];
	}

	void ResetEditableModules()
	{
		for(int Module = 0; Module < MODULE_COUNT; ++Module)
		{
			if(IsEditableModule((EModule)Module))
				ResetSettings((EModule)Module);
		}
	}

	SModuleRect ClampRectToScreen(const SModuleRect &Rect, float HudWidth, float HudHeight)
	{
		SModuleRect Result = Rect;
		Result.m_X = std::clamp(Result.m_X, 0.0f, std::max(0.0f, HudWidth - Result.m_W));
		Result.m_Y = std::clamp(Result.m_Y, 0.0f, std::max(0.0f, HudHeight - Result.m_H));
		return Result;
	}

	float CanvasXToHud(float CanvasX, float HudWidth)
	{
		return CanvasX * (HudWidth / CANVAS_WIDTH);
	}

	int BackgroundCorners(int DefaultCorners, float RectX, float RectY, float RectW, float RectH, float CanvasWidth, float CanvasHeight)
	{
		int Corners = DefaultCorners;
		const float Eps = 0.01f;
		if(RectW <= 0.0f || RectH <= 0.0f)
			return Corners;

		if(RectX <= Eps)
			Corners &= ~IGraphics::CORNER_L;
		if(RectX + RectW >= CanvasWidth - Eps)
			Corners &= ~IGraphics::CORNER_R;
		if(RectY <= Eps)
			Corners &= ~IGraphics::CORNER_T;
		if(RectY + RectH >= CanvasHeight - Eps)
			Corners &= ~IGraphics::CORNER_B;
		return Corners;
	}

	void OnConsoleInit(IConsole *pConsole, IConfigManager *pConfigManager)
	{
		if(!gs_ConsoleCommandRegistered && pConsole)
		{
			pConsole->Register(
				"hud_layout_set",
				"i[module] f[x] f[y] i[scale] i[mode] i[background_enabled] i[background_color] ?i[enabled] ?i[style]",
				CFGFLAG_CLIENT,
				ConHudLayoutSet,
				nullptr,
				"Set HUD module layout entry");
			gs_ConsoleCommandRegistered = true;
		}

		if(!gs_ConfigCallbackRegistered && pConfigManager)
		{
			pConfigManager->RegisterCallback(ConfigSaveCallback, nullptr, ConfigDomain::AMFHUDLAYOUT);
			gs_ConfigCallbackRegistered = true;
		}
	}

} // namespace HudLayout
