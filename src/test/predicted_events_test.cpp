#include <game/client/prediction/gameworld.h>
#include <generated/protocol.h>

#include <gtest/gtest.h>

TEST(PredictedEvents, PendingSnapshotMatchIsConsumedAndPlayedOnce)
{
	CGameWorld World;
	const vec2 Position(128.0f, 256.0f);
	World.m_PredictedEvents.emplace_back(NETEVENTTYPE_SOUNDWORLD, Position, 4, 100, SOUND_HOOK_ATTACH_PLAYER);

	// The authoritative event is allowed to play, but the pending prediction is
	// removed so it cannot play the same sound on a later prediction pass.
	EXPECT_FALSE(World.CheckPredictedEventHandled(CGameWorld::CPredictedEvent(NETEVENTTYPE_SOUNDWORLD, Position, -1, 101, SOUND_HOOK_ATTACH_PLAYER)));
	EXPECT_TRUE(World.m_PredictedEvents.empty());
}

TEST(PredictedEvents, AlreadyPlayedSnapshotMatchIsSuppressedAndConsumed)
{
	CGameWorld World;
	const vec2 Position(128.0f, 256.0f);
	World.m_PredictedEvents.emplace_back(NETEVENTTYPE_SOUNDWORLD, Position, 4, 100, SOUND_HOOK_ATTACH_PLAYER);
	World.m_PredictedEvents.back().m_Handled = true;

	EXPECT_TRUE(World.CheckPredictedEventHandled(CGameWorld::CPredictedEvent(NETEVENTTYPE_SOUNDWORLD, Position, -1, 101, SOUND_HOOK_ATTACH_PLAYER)));
	EXPECT_TRUE(World.m_PredictedEvents.empty());
}
