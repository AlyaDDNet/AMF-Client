#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_MUSIC_PLAYER_ARTWORK_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_MUSIC_PLAYER_ARTWORK_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace AmfMusicPlayerArtwork
{
constexpr int64_t THUMBNAIL_REFRESH_INTERVAL_MS = 1000;

inline bool ShouldRefreshThumbnail(std::string_view CurrentTrackKey, std::string_view CachedTrackKey, int64_t NowMs, int64_t LastRefreshMs)
{
	return CurrentTrackKey != CachedTrackKey || LastRefreshMs <= 0 || NowMs < LastRefreshMs || NowMs - LastRefreshMs >= THUMBNAIL_REFRESH_INTERVAL_MS;
}

inline std::string CacheKey(std::string_view ServiceId, std::string_view Title, std::string_view Artist, const unsigned char *pBytes, size_t Size)
{
	std::string Key;
	Key.reserve(ServiceId.size() + Title.size() + Artist.size() + 48);
	Key.append(ServiceId);
	Key.push_back('|');
	Key.append(Title);
	Key.push_back('|');
	Key.append(Artist);
	if(pBytes != nullptr && Size > 0)
	{
		uint64_t Hash = 14695981039346656037ull;
		for(size_t i = 0; i < Size; ++i)
		{
			Hash ^= pBytes[i];
			Hash *= 1099511628211ull;
		}

		static constexpr char s_aHex[] = "0123456789abcdef";
		char aHash[16];
		for(int i = 15; i >= 0; --i)
		{
			aHash[i] = s_aHex[Hash & 0xf];
			Hash >>= 4;
		}

		Key.push_back('|');
		Key.append(std::to_string(Size));
		Key.push_back('|');
		Key.append(aHash, sizeof(aHash));
	}
	return Key;
}

inline bool ShouldProcessKey(std::string_view CurrentKey, std::string_view LastKey, int64_t NowTicks, int64_t RetryAtTicks)
{
	return CurrentKey != LastKey || (RetryAtTicks > 0 && NowTicks >= RetryAtTicks);
}

inline int RetryDelayMs(unsigned FailureCount)
{
	constexpr int s_aRetryDelayMs[] = {350, 1200, 4000};
	return FailureCount > 0 && FailureCount <= sizeof(s_aRetryDelayMs) / sizeof(s_aRetryDelayMs[0]) ? s_aRetryDelayMs[FailureCount - 1] : 0;
}

inline bool IsLoadable(bool HasBytes, bool HasUrl)
{
	return HasBytes || HasUrl;
}
} // namespace AmfMusicPlayerArtwork

#endif
