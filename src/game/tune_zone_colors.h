/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_TUNE_ZONE_COLORS_H
#define GAME_TUNE_ZONE_COLORS_H

#include <base/color.h>

#include <engine/shared/config.h>

namespace TuneZoneColor
{
enum EPreset
{
	PRESET_BLUE = 0,
	PRESET_RED,
	PRESET_GREEN,
	PRESET_PURPLE,
	PRESET_YELLOW,
	PRESET_WHITE,
	PRESET_BLACK,
	PRESET_COUNT,
};

// DDNet's previous first-zone runtime color: ColorHSLA(0.0f, 0.75f, 0.5f, 1.0f).
constexpr ColorRGBA RED(0.875f, 0.125f, 0.125f, 1.0f);

// Calm alternatives chosen for readable, non-neon tune zones.
constexpr ColorRGBA GREEN(0.20f, 0.62f, 0.42f, 1.0f);
constexpr ColorRGBA PURPLE(0.53f, 0.35f, 0.78f, 1.0f);
// Rotate the canonical red HSL base by 60 degrees, preserving its saturation
// and lightness instead of using a full-saturation primary yellow.
constexpr ColorRGBA YELLOW(0.875f, 0.875f, 0.125f, 1.0f);
constexpr ColorRGBA WHITE(1.0f, 1.0f, 1.0f, 1.0f);
constexpr ColorRGBA BLACK(0.0f, 0.0f, 0.0f, 1.0f);
constexpr ColorRGBA RENDER_MULTIPLIER(1.0f, 1.0f, 1.0f, 1.0f);

inline int NormalizePreset(int Preset)
{
	return Preset >= PRESET_BLUE && Preset < PRESET_COUNT ? Preset : PRESET_BLUE;
}

// All presets use the same tune texture/index path. The selected color is a
// render tint only; it must never become a tile/texture index.
constexpr ColorRGBA BLUE(0.25f, 0.50f, 0.95f, 1.0f);

inline const char *PresetName(int Preset)
{
	switch(NormalizePreset(Preset))
	{
	case PRESET_RED: return "red";
	case PRESET_GREEN: return "green";
	case PRESET_PURPLE: return "purple";
	case PRESET_YELLOW: return "yellow";
	case PRESET_WHITE: return "white";
	case PRESET_BLACK: return "black";
	default: return "standard";
	}
}

inline ColorRGBA PresetColor(int Preset)
{
	switch(NormalizePreset(Preset))
	{
	case PRESET_BLUE: return BLUE;
	case PRESET_RED: return RED;
	case PRESET_GREEN: return GREEN;
	case PRESET_PURPLE: return PURPLE;
	case PRESET_YELLOW: return YELLOW;
	case PRESET_WHITE: return WHITE;
	case PRESET_BLACK: return BLACK;
	default: return WHITE;
	}
}

inline ColorRGBA CurrentCustom()
{
	if(!g_Config.m_AmfTuneZoneColors)
		return WHITE;

	return PresetColor(g_Config.m_AmfTuneZoneColorPreset);
}

// AMF 19.9 applies the selected preset uniformly to every non-default tune
// color index. Keep this shared by the texture-array and immediate render paths.
inline ColorRGBA ColorForIndex(int TuneColorIndex)
{
	if(TuneColorIndex == 0)
		return ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	return CurrentCustom();
}
}

#endif
