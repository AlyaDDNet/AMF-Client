#include <gtest/gtest.h>

#include <game/client/components/tclient/amf_features/run_statistics_hold.h>
#include <game/client/components/tclient/amf_features/run_statistics_badge.h>
#include <game/client/smooth_ui.h>

TEST(AmfRunStatisticsHold, StartsClosed)
{
	CAmfRunStatisticsHoldState State;
	EXPECT_FALSE(State.IsHeld());
}

TEST(AmfRunStatisticsHold, PressHeldRepeatAndReleaseAreNotToggle)
{
	CAmfRunStatisticsHoldState State;
	State.SetFromBind(1);
	EXPECT_TRUE(State.IsHeld());
	State.SetFromBind(1); // Key repeat/duplicate press keeps the overlay held.
	EXPECT_TRUE(State.IsHeld());
	State.SetFromBind(0);
	EXPECT_FALSE(State.IsHeld());
}

TEST(AmfRunStatisticsHold, FocusReleaseClosesHeldOverlay)
{
	CAmfRunStatisticsHoldState State;
	State.SetFromBind(1);
	State.Release();
	EXPECT_FALSE(State.IsHeld());
}

TEST(AmfRunStatisticsHold, UsesRegisteredHoldAction)
{
	EXPECT_STREQ(AMF_RUN_STATISTICS_BIND_COMMAND, "+amf_show_run_statistics");
	EXPECT_EQ(AMF_RUN_STATISTICS_DEFAULT_KEY, KEY_I);
}

TEST(AmfRunStatisticsHold, QuickOverlayRequiresEnabledHoldActionAndInGame)
{
	EXPECT_TRUE(AmfRunStatisticsQuickOverlayRequested(true, 1, true));
	EXPECT_FALSE(AmfRunStatisticsQuickOverlayRequested(false, 1, true));
	EXPECT_FALSE(AmfRunStatisticsQuickOverlayRequested(true, 0, true));
	EXPECT_FALSE(AmfRunStatisticsQuickOverlayRequested(true, false, true));
	EXPECT_FALSE(AmfRunStatisticsQuickOverlayVisible(false, true, 1, false));
	EXPECT_FALSE(AmfRunStatisticsQuickOverlayVisible(true, true, 1, false));
	EXPECT_TRUE(AmfRunStatisticsQuickOverlayVisible(false, true, 1, true));
	EXPECT_FALSE(AmfRunStatisticsHoldSettingVisible(false));
	EXPECT_TRUE(AmfRunStatisticsHoldSettingVisible(true));
}

TEST(AmfRunStatisticsHold, QuickOverlayHoldActionDoesNotRequireScoreboard)
{
	CAmfRunStatisticsHoldState Hold;
	EXPECT_FALSE(AmfRunStatisticsQuickOverlayRequested(Hold.IsHeld(), 1, true));
	Hold.SetFromBind(1);
	// The hold request contract contains no scoreboard visibility/state input.
	EXPECT_TRUE(AmfRunStatisticsQuickOverlayRequested(Hold.IsHeld(), 1, true));
}

TEST(AmfRunStatisticsHold, QuickOverlayStaysCompactAndBoundsRows)
{
	const auto FewPlayers = MakeAmfRunStatisticsQuickOverlayLayout(1920.0f, 3, 40.0f);
	EXPECT_FLOAT_EQ(FewPlayers.m_Width, AMF_RUN_STATISTICS_QUICK_OVERLAY_MIN_WIDTH);
	EXPECT_EQ(FewPlayers.m_VisibleRows, 3);
	EXPECT_FALSE(FewPlayers.m_NeedsScroll);
	EXPECT_FLOAT_EQ(FewPlayers.m_Height, AMF_RUN_STATISTICS_QUICK_OVERLAY_ACCENT_HEIGHT + 2.0f * AMF_RUN_STATISTICS_QUICK_OVERLAY_PADDING +
		AMF_RUN_STATISTICS_QUICK_OVERLAY_TITLE_HEIGHT + AMF_RUN_STATISTICS_QUICK_OVERLAY_TITLE_GAP + AMF_RUN_STATISTICS_QUICK_OVERLAY_HEADER_HEIGHT + 3.0f * AMF_RUN_STATISTICS_QUICK_OVERLAY_ROW_HEIGHT);

	const auto ManyPlayers = MakeAmfRunStatisticsQuickOverlayLayout(900.0f, 64, 180.0f);
	EXPECT_FLOAT_EQ(ManyPlayers.m_Width, 900.0f * AMF_RUN_STATISTICS_QUICK_OVERLAY_SCREEN_WIDTH_FRACTION);
	EXPECT_EQ(ManyPlayers.m_VisibleRows, AMF_RUN_STATISTICS_QUICK_OVERLAY_MAX_ROWS);
	EXPECT_TRUE(ManyPlayers.m_NeedsScroll);
	EXPECT_FLOAT_EQ(ManyPlayers.m_Height, AMF_RUN_STATISTICS_QUICK_OVERLAY_ACCENT_HEIGHT + 2.0f * AMF_RUN_STATISTICS_QUICK_OVERLAY_PADDING +
		AMF_RUN_STATISTICS_QUICK_OVERLAY_TITLE_HEIGHT + AMF_RUN_STATISTICS_QUICK_OVERLAY_TITLE_GAP + AMF_RUN_STATISTICS_QUICK_OVERLAY_HEADER_HEIGHT +
		AMF_RUN_STATISTICS_QUICK_OVERLAY_MAX_ROWS * AMF_RUN_STATISTICS_QUICK_OVERLAY_ROW_HEIGHT);
	EXPECT_FLOAT_EQ(ManyPlayers.m_Height, 291.0f);

	const auto Empty = MakeAmfRunStatisticsQuickOverlayLayout(1920.0f, 0, 0.0f);
	EXPECT_FLOAT_EQ(Empty.m_Width, AMF_RUN_STATISTICS_QUICK_OVERLAY_EMPTY_MIN_WIDTH);
	EXPECT_FLOAT_EQ(Empty.m_Height, AMF_RUN_STATISTICS_QUICK_OVERLAY_ACCENT_HEIGHT + 2.0f * AMF_RUN_STATISTICS_QUICK_OVERLAY_PADDING +
		AMF_RUN_STATISTICS_QUICK_OVERLAY_TITLE_HEIGHT + AMF_RUN_STATISTICS_QUICK_OVERLAY_TITLE_GAP + AMF_RUN_STATISTICS_QUICK_OVERLAY_EMPTY_HEIGHT);
}

TEST(AmfRunStatisticsHold, ScrollStateResetsAcrossRunsAndChangingRowCounts)
{
	CAmfRunStatisticsQuickScrollIdentity Identity;
	EXPECT_TRUE(Identity.Update(101, 18));
	EXPECT_FALSE(Identity.Update(101, 18));
	EXPECT_TRUE(Identity.Update(102, 18));
	EXPECT_TRUE(Identity.Update(102, 3));
}

TEST(AmfRunStatisticsHold, QuickOverlayTransitionsUseExistingSmoothSectionState)
{
	CSmoothUiSectionAnimation Animation;
	EXPECT_FLOAT_EQ(Animation.Update(false, 0.016f, 180, true), 0.0f);
	const float OpeningProgress = Animation.Update(true, 0.09f, 180, true);
	EXPECT_GT(OpeningProgress, 0.0f);
	EXPECT_LT(OpeningProgress, 1.0f);
	EXPECT_TRUE(Animation.IsVisible());

	const float ClosingProgress = Animation.Update(false, 0.045f, 180, true);
	EXPECT_GT(ClosingProgress, 0.0f);
	EXPECT_LT(ClosingProgress, OpeningProgress);
	EXPECT_TRUE(Animation.IsVisible()); // Release closes the logical hold; only the visual transition remains.
	EXPECT_FLOAT_EQ(Animation.Update(false, 0.016f, 180, false), 0.0f);
	EXPECT_FALSE(Animation.IsVisible());
}

TEST(AmfRunStatisticsBadge, UsesDonorRedBETAStyleAndTopRightPlacement)
{
	EXPECT_STREQ(AmfRunStatisticsBadge::LABEL, "BETA");
	EXPECT_EQ(AmfRunStatisticsBadge::FONT_SIZE, 12.0f);
	EXPECT_FLOAT_EQ(AmfRunStatisticsBadge::LEFT.r, 0.95f);
	EXPECT_FLOAT_EQ(AmfRunStatisticsBadge::RIGHT.r, 0.75f);
	EXPECT_GT(AmfRunStatisticsBadge::LEFT.r, AmfRunStatisticsBadge::LEFT.g);
	EXPECT_GT(AmfRunStatisticsBadge::RIGHT.r, AmfRunStatisticsBadge::RIGHT.g);

	const auto Placement = AmfRunStatisticsBadge::Place(10.0f, 20.0f, 200.0f, 24.0f, 30.0f, 5.0f);
	EXPECT_FLOAT_EQ(Placement.m_BadgeX + Placement.m_BadgeWidth, 210.0f);
	EXPECT_FLOAT_EQ(Placement.m_BadgeY, 22.0f);
	EXPECT_FLOAT_EQ(Placement.m_BadgeHeight, 20.0f);
	EXPECT_FLOAT_EQ(10.0f + Placement.m_TitleWidth + 5.0f, Placement.m_BadgeX);
}
