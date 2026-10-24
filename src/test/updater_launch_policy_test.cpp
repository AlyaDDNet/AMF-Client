#include <engine/client/updater_launch_policy.h>

#include <gtest/gtest.h>

TEST(UpdaterLaunchPolicy, NormalStartupAndNoUpdateNeverLaunchAnUpdateJob)
{
	EXPECT_FALSE(UpdaterLaunchPolicy::ShouldLaunchUpdateTransaction(false, false));
	EXPECT_FALSE(UpdaterLaunchPolicy::ShouldLaunchUpdateTransaction(false, true));
	EXPECT_FALSE(UpdaterLaunchPolicy::ShouldLaunchUpdateTransaction(true, true));
}

TEST(UpdaterLaunchPolicy, RealUpdateUsesTheCompleteTransactionArguments)
{
	EXPECT_TRUE(UpdaterLaunchPolicy::ShouldLaunchUpdateTransaction(true, false));
	const auto Arguments = UpdaterLaunchPolicy::MakeUpdateTransactionArguments(
		"1234", "C:/client/update/release.zip", "C:/client", "C:/client/DDNet.exe");
	ASSERT_EQ(Arguments.size(), 4U);
	EXPECT_STREQ(Arguments[0], "1234");
	EXPECT_STREQ(Arguments[1], "C:/client/update/release.zip");
	EXPECT_STREQ(Arguments[2], "C:/client");
	EXPECT_STREQ(Arguments[3], "C:/client/DDNet.exe");
}
