#include "test.h"

#include <game/client/components/console_scrollbar.h>

TEST(ConsoleScrollBar, HidesWhenBacklogDoesNotScroll)
{
	EXPECT_FALSE(ConsoleScrollBar::CalculateGeometry(10.0f, 200.0f, 0, 10, 0, 24.0f).m_Visible);
	EXPECT_FALSE(ConsoleScrollBar::CalculateGeometry(10.0f, 200.0f, 10, 10, 0, 24.0f).m_Visible);
}

TEST(ConsoleScrollBar, ThumbTracksVisibleBacklogRatio)
{
	const auto Geometry = ConsoleScrollBar::CalculateGeometry(20.0f, 200.0f, 20, 10, 0, 24.0f);
	EXPECT_TRUE(Geometry.m_Visible);
	EXPECT_FLOAT_EQ(Geometry.m_ThumbHeight, 100.0f);
	EXPECT_FLOAT_EQ(Geometry.m_ThumbTop, 120.0f);
}

TEST(ConsoleScrollBar, LargeBacklogUsesGrabbableMinimumThumb)
{
	const auto Geometry = ConsoleScrollBar::CalculateGeometry(0.0f, 200.0f, 1000, 10, 0, 24.0f);
	EXPECT_FLOAT_EQ(Geometry.m_ThumbHeight, 24.0f);
	EXPECT_TRUE(Geometry.m_Visible);
}

TEST(ConsoleScrollBar, ScrollOffsetsClampAtBothEnds)
{
	const auto Bottom = ConsoleScrollBar::CalculateGeometry(0.0f, 200.0f, 20, 10, -5, 24.0f);
	const auto Top = ConsoleScrollBar::CalculateGeometry(0.0f, 200.0f, 20, 10, 50, 24.0f);
	EXPECT_FLOAT_EQ(Bottom.m_ThumbTop, 100.0f);
	EXPECT_FLOAT_EQ(Top.m_ThumbTop, 0.0f);
}

TEST(ConsoleScrollBar, DragMapsTopMiddleBottomAndClamps)
{
	const auto Geometry = ConsoleScrollBar::CalculateGeometry(0.0f, 100.0f, 100, 10, 0, 20.0f);
	const float GrabOffset = Geometry.m_ThumbHeight / 2.0f;
	EXPECT_EQ(ConsoleScrollBar::OffsetFromPointer(GrabOffset, GrabOffset, Geometry), 90);
	EXPECT_EQ(ConsoleScrollBar::OffsetFromPointer(50.0f, GrabOffset, Geometry), 45);
	EXPECT_EQ(ConsoleScrollBar::OffsetFromPointer(100.0f - GrabOffset, GrabOffset, Geometry), 0);
	EXPECT_EQ(ConsoleScrollBar::OffsetFromPointer(-100.0f, GrabOffset, Geometry), 90);
	EXPECT_EQ(ConsoleScrollBar::OffsetFromPointer(200.0f, GrabOffset, Geometry), 0);
}

TEST(ConsoleScrollBar, RecalculatesGeometryAfterResize)
{
	const auto Large = ConsoleScrollBar::CalculateGeometry(0.0f, 400.0f, 100, 20, 40, 24.0f);
	const auto Small = ConsoleScrollBar::CalculateGeometry(0.0f, 200.0f, 100, 20, 40, 24.0f);
	EXPECT_FLOAT_EQ(Large.m_ThumbHeight, 80.0f);
	EXPECT_FLOAT_EQ(Small.m_ThumbHeight, 40.0f);
	EXPECT_NE(Large.m_ThumbTop, Small.m_ThumbTop);
}
