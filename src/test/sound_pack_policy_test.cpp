#include <engine/client/sound_pack_policy.h>

#include <gtest/gtest.h>

TEST(SoundPackPolicy, TemporarilyMissingPreferenceFallsBackWithoutMutatingIt)
{
	char aPreference[] = "MyPack";
	EXPECT_STREQ(SoundPackPolicy::ResolveEffectivePack(aPreference, false, true), "default");
	EXPECT_STREQ(aPreference, "MyPack");
}

TEST(SoundPackPolicy, ExistingPreferenceRemainsEffective)
{
	EXPECT_STREQ(SoundPackPolicy::ResolveEffectivePack("MyPack", true, true), "MyPack");
}

TEST(SoundPackPolicy, MissingPreferenceWithoutDefaultIsSafe)
{
	EXPECT_EQ(SoundPackPolicy::ResolveEffectivePack("MyPack", false, false), nullptr);
	EXPECT_STREQ(SoundPackPolicy::ResolveEffectivePack("", false, true), "default");
}
