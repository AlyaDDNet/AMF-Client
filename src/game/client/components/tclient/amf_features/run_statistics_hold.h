#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_AMF_FEATURES_RUN_STATISTICS_HOLD_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_AMF_FEATURES_RUN_STATISTICS_HOLD_H

#include <algorithm>
#include <cstdint>

#include <engine/keys.h>

inline constexpr char AMF_RUN_STATISTICS_BIND_COMMAND[] = "+amf_show_run_statistics";
inline constexpr int AMF_RUN_STATISTICS_DEFAULT_KEY = KEY_I;
inline constexpr int AMF_RUN_STATISTICS_QUICK_OVERLAY_MAX_ROWS = 8;
inline constexpr float AMF_RUN_STATISTICS_QUICK_OVERLAY_EMPTY_MIN_WIDTH = 360.0f;
inline constexpr float AMF_RUN_STATISTICS_QUICK_OVERLAY_MIN_WIDTH = 540.0f;
inline constexpr float AMF_RUN_STATISTICS_QUICK_OVERLAY_MAX_WIDTH = 900.0f;
inline constexpr float AMF_RUN_STATISTICS_QUICK_OVERLAY_SCREEN_WIDTH_FRACTION = 0.88f;
inline constexpr float AMF_RUN_STATISTICS_QUICK_OVERLAY_FIRST_COLUMN_FRACTION = 0.24f;
inline constexpr float AMF_RUN_STATISTICS_QUICK_OVERLAY_ACCENT_HEIGHT = 2.0f;
inline constexpr float AMF_RUN_STATISTICS_QUICK_OVERLAY_PADDING = 10.0f;
inline constexpr float AMF_RUN_STATISTICS_QUICK_OVERLAY_TITLE_HEIGHT = 25.0f;
inline constexpr float AMF_RUN_STATISTICS_QUICK_OVERLAY_TITLE_GAP = 6.0f;
inline constexpr float AMF_RUN_STATISTICS_QUICK_OVERLAY_HEADER_HEIGHT = 22.0f;
inline constexpr float AMF_RUN_STATISTICS_QUICK_OVERLAY_ROW_HEIGHT = 27.0f;
inline constexpr float AMF_RUN_STATISTICS_QUICK_OVERLAY_EMPTY_HEIGHT = 32.0f;

struct SAmfRunStatisticsQuickOverlayLayout
{
	float m_Width = 0.0f;
	float m_Height = 0.0f;
	int m_VisibleRows = 1;
	bool m_NeedsScroll = false;
};

inline bool AmfRunStatisticsQuickOverlayRequested(bool Held, int StatisticsEnabled, bool InGame)
{
	return Held && StatisticsEnabled != 0 && InGame;
}

inline bool AmfRunStatisticsQuickOverlayVisible(bool Requested, bool AnimationVisible, int StatisticsEnabled, bool InGame)
{
	return InGame && StatisticsEnabled != 0 && (Requested || AnimationVisible);
}

inline bool AmfRunStatisticsHoldSettingVisible(bool StatisticsEnabled)
{
	return StatisticsEnabled;
}

inline SAmfRunStatisticsQuickOverlayLayout MakeAmfRunStatisticsQuickOverlayLayout(float ScreenWidth, int ParticipantRows, float MaxPlayerNameWidth)
{
	SAmfRunStatisticsQuickOverlayLayout Layout;
	const float ScreenBound = std::max(0.0f, ScreenWidth * AMF_RUN_STATISTICS_QUICK_OVERLAY_SCREEN_WIDTH_FRACTION);
	const float NameBound = (std::max(0.0f, MaxPlayerNameWidth) + 20.0f) / AMF_RUN_STATISTICS_QUICK_OVERLAY_FIRST_COLUMN_FRACTION;
	const float MinimumWidth = ParticipantRows > 0 ? AMF_RUN_STATISTICS_QUICK_OVERLAY_MIN_WIDTH : AMF_RUN_STATISTICS_QUICK_OVERLAY_EMPTY_MIN_WIDTH;
	const float DesiredWidth = std::max(MinimumWidth, NameBound);
	Layout.m_Width = std::min({AMF_RUN_STATISTICS_QUICK_OVERLAY_MAX_WIDTH, ScreenBound, DesiredWidth});
	Layout.m_VisibleRows = std::clamp(ParticipantRows, 0, AMF_RUN_STATISTICS_QUICK_OVERLAY_MAX_ROWS);
	Layout.m_NeedsScroll = ParticipantRows > AMF_RUN_STATISTICS_QUICK_OVERLAY_MAX_ROWS;
	const float TableHeight = Layout.m_VisibleRows > 0 ?
		AMF_RUN_STATISTICS_QUICK_OVERLAY_HEADER_HEIGHT + Layout.m_VisibleRows * AMF_RUN_STATISTICS_QUICK_OVERLAY_ROW_HEIGHT :
		AMF_RUN_STATISTICS_QUICK_OVERLAY_EMPTY_HEIGHT;
	Layout.m_Height = AMF_RUN_STATISTICS_QUICK_OVERLAY_ACCENT_HEIGHT + 2.0f * AMF_RUN_STATISTICS_QUICK_OVERLAY_PADDING +
		AMF_RUN_STATISTICS_QUICK_OVERLAY_TITLE_HEIGHT + AMF_RUN_STATISTICS_QUICK_OVERLAY_TITLE_GAP + TableHeight;
	return Layout;
}

class CAmfRunStatisticsQuickScrollIdentity
{
	uint64_t m_RunId = 0;
	int m_ParticipantRows = -1;

public:
	bool Update(uint64_t RunId, int ParticipantRows)
	{
		if(m_RunId == RunId && m_ParticipantRows == ParticipantRows)
			return false;
		m_RunId = RunId;
		m_ParticipantRows = ParticipantRows;
		return true;
	}
};

class CAmfRunStatisticsHoldState
{
	bool m_Held = false;

public:
	void SetFromBind(int Pressed) { m_Held = Pressed != 0; }
	void Release() { m_Held = false; }
	bool IsHeld() const { return m_Held; }
};

#endif
