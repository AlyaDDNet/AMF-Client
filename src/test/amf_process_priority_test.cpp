#include "../../other/helpers/amf_priority_restore_protocol.h"

#include <gtest/gtest.h>

TEST(AmfProcessPriority, FeatureMasksAreIndependent)
{
	using namespace AmfPriorityRestore;
	EXPECT_EQ(RequestedTargets(false, false), 0u);
	EXPECT_EQ(RequestedTargets(true, false), TARGET_DISCORD);
	EXPECT_EQ(RequestedTargets(false, true), TARGET_SPOTIFY | TARGET_SOUNDCLOUD);
	EXPECT_EQ(RequestedTargets(true, true), TARGET_DISCORD | TARGET_SPOTIFY | TARGET_SOUNDCLOUD);
	EXPECT_TRUE(IsTargetRequested(RequestedTargets(false, true), TARGET_SPOTIFY));
	EXPECT_TRUE(IsTargetRequested(RequestedTargets(false, true), TARGET_SOUNDCLOUD));
	EXPECT_FALSE(IsTargetRequested(RequestedTargets(false, true), TARGET_DISCORD));
}

TEST(AmfProcessPriority, SteadyStateDoesNotReapplySameClass)
{
	using namespace AmfPriorityRestore;
	constexpr std::uint32_t BELOW_NORMAL = 0x00004000;
	constexpr std::uint32_t NORMAL = 0x00000020;
	EXPECT_FALSE(ShouldApplyPriority(BELOW_NORMAL, BELOW_NORMAL));
	EXPECT_TRUE(ShouldApplyPriority(NORMAL, BELOW_NORMAL));
}

TEST(AmfProcessPriority, RestoreOnlyTouchesPriorityOwnedByAmf)
{
	using namespace AmfPriorityRestore;
	constexpr std::uint32_t BELOW_NORMAL = 0x00004000;
	constexpr std::uint32_t NORMAL = 0x00000020;
	EXPECT_TRUE(ShouldRestorePriority(true, BELOW_NORMAL, BELOW_NORMAL));
	EXPECT_FALSE(ShouldRestorePriority(false, BELOW_NORMAL, BELOW_NORMAL));
	EXPECT_FALSE(ShouldRestorePriority(true, NORMAL, BELOW_NORMAL));
}

TEST(AmfProcessPriority, RepeatedFeatureTogglesNeverCombineUnrequestedTargets)
{
	using namespace AmfPriorityRestore;
	for(int Cycle = 0; Cycle < 64; ++Cycle)
	{
		EXPECT_EQ(RequestedTargets(true, false), TARGET_DISCORD);
		EXPECT_EQ(RequestedTargets(false, true), TARGET_SPOTIFY | TARGET_SOUNDCLOUD);
		EXPECT_EQ(RequestedTargets(true, true), TARGET_DISCORD | TARGET_SPOTIFY | TARGET_SOUNDCLOUD);
		EXPECT_EQ(RequestedTargets(false, false), 0u);
	}
}
