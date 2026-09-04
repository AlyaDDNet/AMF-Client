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
	PRESET_COUNT,
};

// DDNet's previous first-zone runtime color: ColorHSLA(0.0f, 0.75f, 0.5f, 1.0f).
constexpr ColorRGBA RED(0.875f, 0.125f, 0.125f, 1.0f);

// Calm alternatives chosen for readable, non-neon tune zones.
constexpr ColorRGBA GREEN(0.20f, 0.62f, 0.42f, 1.0f);
constexpr ColorRGBA PURPLE(0.53f, 0.35f, 0.78f, 1.0f);

inline int NormalizePreset(int Preset)
{
	return Preset >= PRESET_BLUE && Preset < PRESET_COUNT ? Preset : PRESET_BLUE;
}

// Blue preserves the raw Tune tile path from Best Client 2.1. Only the
// other presets use a uniform AMF recolor.
inline bool UsesRawBlue()
{
	return NormalizePreset(g_Config.m_AmfTuneZoneColorPreset) == PRESET_BLUE;
}

inline const char *PresetName(int Preset)
{
	switch(NormalizePreset(Preset))
	{
	case PRESET_RED: return "red";
	case PRESET_GREEN: return "green";
	case PRESET_PURPLE: return "purple";
	default: return "default";
	}
}

inline ColorRGBA CurrentCustom()
{
	switch(NormalizePreset(g_Config.m_AmfTuneZoneColorPreset))
	{
	case PRESET_RED: return RED;
	case PRESET_GREEN: return GREEN;
	case PRESET_PURPLE: return PURPLE;
	default: return ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	}
}
}

#endif
