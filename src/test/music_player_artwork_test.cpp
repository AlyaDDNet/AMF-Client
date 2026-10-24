#include <game/client/components/tclient/music_player/music_player_artwork.h>
#include <game/client/components/tclient/music_player/media_decoder.h>

#include <base/str.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>

TEST(MusicPlayerArtwork, JpegThumbnailDecodesThroughProductionStaticImagePath)
{
#if defined(CONF_FAMILY_WINDOWS) || defined(CONF_VIDEORECORDER)
	// A deterministic 8x8 red baseline JPEG fixture, encoded once and kept as bytes.
	static constexpr char s_aJpegBase64[] =
		"/9j/4AAQSkZJRgABAQEAYABgAAD/2wBDAAQCAwMDAgQDAwMEBAQEBQkGBQUFBQsICAYJDQsNDQ0LDAwOEBQRDg8TDwwMEhgSExUWFxcXDhEZGxkWGhQWFxb/2wBDAQQEBAUFBQoGBgoWDwwPFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhb/wAARCAAIAAgDASIAAhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwDzeiiivyk/tI//2Q==";
	std::array<unsigned char, 1024> aJpegBytes{};
	const int JpegSize = str_base64_decode(aJpegBytes.data(), (int)aJpegBytes.size(), s_aJpegBase64);
	ASSERT_GT(JpegSize, 0);
	ASSERT_EQ(aJpegBytes[0], 0xff);
	ASSERT_EQ(aJpegBytes[1], 0xd8);

	SMediaDecodedFrames Decoded;
	ASSERT_TRUE(MediaDecoder::DecodeStaticImageCpu(nullptr, aJpegBytes.data(), (size_t)JpegSize, "music_player_test_jpeg", Decoded, 64));
	ASSERT_EQ(Decoded.m_Width, 8);
	ASSERT_EQ(Decoded.m_Height, 8);
	ASSERT_EQ(Decoded.m_vFrames.size(), 1u);
	const CImageInfo &Image = Decoded.m_vFrames.front().m_Image;
	ASSERT_NE(Image.m_pData, nullptr);
	ASSERT_EQ(Image.m_Format, CImageInfo::FORMAT_RGBA);
	EXPECT_GT(Image.m_pData[0], 200);
	EXPECT_LT(Image.m_pData[1], 80);
	EXPECT_LT(Image.m_pData[2], 80);
	EXPECT_EQ(Image.m_pData[3], 255);
	EXPECT_TRUE(AmfMusicPlayerArtwork::IsLoadable(!Decoded.Empty(), false));
#else
	GTEST_SKIP() << "JPEG decoding requires Windows Imaging Component or the existing FFmpeg image decoder";
#endif
}

TEST(MusicPlayerArtwork, PngThumbnailStillDecodesThroughProductionStaticImagePath)
{
	static constexpr char s_aPngBase64[] = "iVBORw0KGgoAAAANSUhEUgAAAAgAAAAICAYAAADED76LAAAAAXNSR0IArs4c6QAAAARnQU1BAACxjwv8YQUAAAAJcEhZcwAADsMAAA7DAcdvqGQAAAAWSURBVChTYxDRePYfH2ZAF0DHw0MBANYziEE9c/tXAAAAAElFTkSuQmCC";
	std::array<unsigned char, 256> aPngBytes{};
	const int PngSize = str_base64_decode(aPngBytes.data(), (int)aPngBytes.size(), s_aPngBase64);
	ASSERT_GT(PngSize, 0);

	SMediaDecodedFrames Decoded;
	ASSERT_TRUE(MediaDecoder::DecodeStaticImageCpu(nullptr, aPngBytes.data(), (size_t)PngSize, "music_player_test_png", Decoded, 64));
	ASSERT_EQ(Decoded.m_Width, 8);
	ASSERT_EQ(Decoded.m_Height, 8);
	ASSERT_EQ(Decoded.m_vFrames.size(), 1u);
	const CImageInfo &Image = Decoded.m_vFrames.front().m_Image;
	ASSERT_NE(Image.m_pData, nullptr);
	ASSERT_EQ(Image.m_Format, CImageInfo::FORMAT_RGBA);
	EXPECT_LT(Image.m_pData[0], 80);
	EXPECT_LT(Image.m_pData[1], 100);
	EXPECT_GT(Image.m_pData[2], 180);
	EXPECT_EQ(Image.m_pData[3], 255);
}

TEST(MusicPlayerArtwork, ImmediateThumbnailCreatesLoadableArtworkKey)
{
	constexpr std::array<unsigned char, 4> aArtwork = {0x89, 'P', 'N', 'G'};
	const std::string Key = AmfMusicPlayerArtwork::CacheKey("SoundCloud", "Track", "Artist", aArtwork.data(), aArtwork.size());
	EXPECT_TRUE(AmfMusicPlayerArtwork::IsLoadable(true, false));
	EXPECT_TRUE(AmfMusicPlayerArtwork::ShouldProcessKey(Key, "", 1, 0));
}

TEST(MusicPlayerArtwork, SameTrackThumbnailRefreshIsBoundedAndNewTracksRefreshImmediately)
{
	constexpr int64_t FirstRead = 1000;
	EXPECT_FALSE(AmfMusicPlayerArtwork::ShouldRefreshThumbnail("session|track-a", "session|track-a", FirstRead + 999, FirstRead));
	EXPECT_TRUE(AmfMusicPlayerArtwork::ShouldRefreshThumbnail("session|track-a", "session|track-a", FirstRead + 1000, FirstRead));
	EXPECT_TRUE(AmfMusicPlayerArtwork::ShouldRefreshThumbnail("session|track-b", "session|track-a", FirstRead + 1, FirstRead));
	EXPECT_TRUE(AmfMusicPlayerArtwork::ShouldRefreshThumbnail("session|track-a", "", FirstRead + 1, 0));
}

TEST(MusicPlayerArtwork, MissingThumbnailFallsBackAndLateThumbnailIsProcessed)
{
	const std::string NoArtworkKey = AmfMusicPlayerArtwork::CacheKey("SoundCloud", "Track", "Artist", nullptr, 0);
	constexpr std::array<unsigned char, 4> aArtwork = {0x89, 'P', 'N', 'G'};
	const std::string ArtworkKey = AmfMusicPlayerArtwork::CacheKey("SoundCloud", "Track", "Artist", aArtwork.data(), aArtwork.size());
	EXPECT_FALSE(AmfMusicPlayerArtwork::IsLoadable(false, false));
	EXPECT_NE(NoArtworkKey, ArtworkKey);
	EXPECT_TRUE(AmfMusicPlayerArtwork::ShouldProcessKey(ArtworkKey, NoArtworkKey, 100, 0));
}

TEST(MusicPlayerArtwork, SameLengthArtworkRevisionInvalidatesCache)
{
	constexpr std::array<unsigned char, 4> aFirst = {0x89, 'P', 'N', 'G'};
	constexpr std::array<unsigned char, 4> aSecond = {0x89, 'J', 'P', 'G'};
	EXPECT_NE(
		AmfMusicPlayerArtwork::CacheKey("SoundCloud", "Track", "Artist", aFirst.data(), aFirst.size()),
		AmfMusicPlayerArtwork::CacheKey("SoundCloud", "Track", "Artist", aSecond.data(), aSecond.size()));
}

TEST(MusicPlayerArtwork, TrackAndSessionSwitchInvalidateArtworkCache)
{
	constexpr std::array<unsigned char, 3> aArtwork = {1, 2, 3};
	const std::string Key = AmfMusicPlayerArtwork::CacheKey("Chrome", "Track A", "Artist", aArtwork.data(), aArtwork.size());
	EXPECT_NE(Key, AmfMusicPlayerArtwork::CacheKey("Chrome", "Track B", "Artist", aArtwork.data(), aArtwork.size()));
	EXPECT_NE(Key, AmfMusicPlayerArtwork::CacheKey("Spotify", "Track A", "Artist", aArtwork.data(), aArtwork.size()));
}

TEST(MusicPlayerArtwork, FailedArtworkCanRetryWithBoundedBackoff)
{
	constexpr std::array<unsigned char, 3> aArtwork = {1, 2, 3};
	const std::string Key = AmfMusicPlayerArtwork::CacheKey("SoundCloud", "Track", "Artist", aArtwork.data(), aArtwork.size());
	const int64_t RetryAt = 1000 + AmfMusicPlayerArtwork::RetryDelayMs(1);
	EXPECT_FALSE(AmfMusicPlayerArtwork::ShouldProcessKey(Key, Key, 1000, RetryAt));
	EXPECT_TRUE(AmfMusicPlayerArtwork::ShouldProcessKey(Key, Key, RetryAt, RetryAt));
	EXPECT_EQ(AmfMusicPlayerArtwork::RetryDelayMs(1), 350);
	EXPECT_EQ(AmfMusicPlayerArtwork::RetryDelayMs(2), 1200);
	EXPECT_EQ(AmfMusicPlayerArtwork::RetryDelayMs(3), 4000);
	EXPECT_EQ(AmfMusicPlayerArtwork::RetryDelayMs(4), 0);
	EXPECT_FALSE(AmfMusicPlayerArtwork::ShouldProcessKey(Key, Key, RetryAt + 10000, 0));
}

TEST(MusicPlayerArtwork, UnavailableArtworkLeavesFallbackAvailable)
{
	const std::string NoArtworkKey = AmfMusicPlayerArtwork::CacheKey("Chrome", "SoundCloud track", "Artist", nullptr, 0);
	EXPECT_FALSE(AmfMusicPlayerArtwork::IsLoadable(false, false));
	EXPECT_TRUE(AmfMusicPlayerArtwork::ShouldProcessKey(NoArtworkKey, "", 1, 0));
	EXPECT_FALSE(AmfMusicPlayerArtwork::IsLoadable(false, false));

	// If GSMTC publishes artwork after the first no-reference snapshot, copied
	// bytes produce a new content-addressed key and replace the fallback.
	constexpr std::array<unsigned char, 4> aLateArtwork = {0xff, 0xd8, 0xff, 0xd9};
	const std::string ArtworkKey = AmfMusicPlayerArtwork::CacheKey("Chrome", "SoundCloud track", "Artist", aLateArtwork.data(), aLateArtwork.size());
	EXPECT_TRUE(AmfMusicPlayerArtwork::IsLoadable(true, false));
	EXPECT_TRUE(AmfMusicPlayerArtwork::ShouldProcessKey(ArtworkKey, NoArtworkKey, 100, 0));
}
