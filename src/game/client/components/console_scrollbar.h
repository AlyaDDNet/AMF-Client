#ifndef GAME_CLIENT_COMPONENTS_CONSOLE_SCROLLBAR_H
#define GAME_CLIENT_COMPONENTS_CONSOLE_SCROLLBAR_H

#include <algorithm>
#include <cmath>

namespace ConsoleScrollBar
{

struct SGeometry
{
	float m_TrackTop = 0.0f;
	float m_TrackHeight = 0.0f;
	float m_ThumbTop = 0.0f;
	float m_ThumbHeight = 0.0f;
	int m_MaxOffset = 0;
	bool m_Visible = false;
};

inline SGeometry CalculateGeometry(float TrackTop, float TrackHeight, int TotalLines, int VisibleLines, int ScrollOffset, float MinThumbHeight)
{
	SGeometry Geometry;
	Geometry.m_TrackTop = TrackTop;
	Geometry.m_TrackHeight = std::max(0.0f, TrackHeight);
	VisibleLines = std::max(1, VisibleLines);
	Geometry.m_MaxOffset = std::max(0, TotalLines - VisibleLines);
	if(Geometry.m_TrackHeight <= 0.0f || Geometry.m_MaxOffset == 0)
		return Geometry;

	const float ProportionalHeight = Geometry.m_TrackHeight * (float)VisibleLines / (float)TotalLines;
	Geometry.m_ThumbHeight = std::clamp(std::max(ProportionalHeight, MinThumbHeight), 0.0f, Geometry.m_TrackHeight);
	const float ScrollableHeight = Geometry.m_TrackHeight - Geometry.m_ThumbHeight;
	const int ClampedOffset = std::clamp(ScrollOffset, 0, Geometry.m_MaxOffset);
	const float Position = 1.0f - (float)ClampedOffset / (float)Geometry.m_MaxOffset;
	Geometry.m_ThumbTop = TrackTop + ScrollableHeight * Position;
	Geometry.m_Visible = ScrollableHeight > 0.0f;
	return Geometry;
}

inline int OffsetFromPointer(float PointerY, float GrabOffset, const SGeometry &Geometry)
{
	if(Geometry.m_MaxOffset <= 0)
		return 0;

	const float ScrollableHeight = Geometry.m_TrackHeight - Geometry.m_ThumbHeight;
	if(ScrollableHeight <= 0.0f)
		return 0;

	const float ThumbTop = std::clamp(PointerY - GrabOffset, Geometry.m_TrackTop, Geometry.m_TrackTop + ScrollableHeight);
	const float Position = (ThumbTop - Geometry.m_TrackTop) / ScrollableHeight;
	return std::clamp((int)std::round((1.0f - Position) * Geometry.m_MaxOffset), 0, Geometry.m_MaxOffset);
}

} // namespace ConsoleScrollBar

#endif
