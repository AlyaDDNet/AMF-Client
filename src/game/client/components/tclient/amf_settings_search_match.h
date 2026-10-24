#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_AMF_SETTINGS_SEARCH_MATCH_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_AMF_SETTINGS_SEARCH_MATCH_H

#include <base/str.h>

namespace AmfSettingsSearch
{
inline bool MatchesText(const char *pText, const char *pQuery)
{
	return pText != nullptr && pText[0] != '\0' && pQuery != nullptr && pQuery[0] != '\0' && str_utf8_find_nocase(pText, pQuery) != nullptr;
}
} // namespace AmfSettingsSearch

#endif // GAME_CLIENT_COMPONENTS_TCLIENT_AMF_SETTINGS_SEARCH_MATCH_H
