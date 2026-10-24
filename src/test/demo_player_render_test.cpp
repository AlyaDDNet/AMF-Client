#include "test.h"

#include <game/client/components/player_render_state.h>

#include <gtest/gtest.h>

TEST(DemoPlayerRender, DemoPlaybackUsesSnapshotFreezeStateForRecordedLocalPlayer)
{
	EXPECT_FALSE(PlayerRenderState::ShouldUsePredictedFreezeState(true, true));
	EXPECT_FALSE(PlayerRenderState::ShouldUsePredictedFreezeState(true, false));
}

TEST(DemoPlayerRender, LiveLocalPlayerKeepsPredictedFreezeState)
{
	EXPECT_TRUE(PlayerRenderState::ShouldUsePredictedFreezeState(false, true));
	EXPECT_FALSE(PlayerRenderState::ShouldUsePredictedFreezeState(false, false));
}

TEST(DemoPlayerRender, WorldIndicatorUsesTheSameSnapshotTeeVisibility)
{
	EXPECT_TRUE(PlayerRenderState::HasVisibleServerTee(true, true, true));
	EXPECT_FALSE(PlayerRenderState::HasVisibleServerTee(false, true, true));
	EXPECT_FALSE(PlayerRenderState::HasVisibleServerTee(true, false, true));
	EXPECT_FALSE(PlayerRenderState::HasVisibleServerTee(true, true, false));
	EXPECT_FALSE(PlayerRenderState::HasVisibleWorldTee(true, true, 0));
	EXPECT_TRUE(PlayerRenderState::HasVisibleWorldTee(true, true, 25));
	EXPECT_TRUE(PlayerRenderState::HasVisibleWorldTee(true, false, 0));
	EXPECT_FALSE(PlayerRenderState::HasVisibleWorldTee(false, false, 100));
}
