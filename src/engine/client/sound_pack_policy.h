#ifndef ENGINE_CLIENT_SOUND_PACK_POLICY_H
#define ENGINE_CLIENT_SOUND_PACK_POLICY_H

#include <cstring>

namespace SoundPackPolicy
{
inline const char *ResolveEffectivePack(const char *pPreference, bool PreferenceExists, bool DefaultExists)
{
	if(!pPreference || !pPreference[0] || std::strcmp(pPreference, "default") == 0)
		return DefaultExists ? "default" : nullptr;
	if(PreferenceExists)
		return pPreference;
	return DefaultExists ? "default" : nullptr;
}
}

#endif
