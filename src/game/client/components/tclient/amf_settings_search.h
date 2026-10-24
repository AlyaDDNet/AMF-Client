#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_AMF_SETTINGS_SEARCH_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_AMF_SETTINGS_SEARCH_H

#include <game/client/ui_rect.h>

class CMenus;
class CScrollRegion;
class CGameClient;
class CUi;
class IInput;

namespace AmfSettingsSearch
{
void RenderButton(CMenus *pMenus, CUi *pUi, CGameClient *pGameClient, IInput *pInput, const CUIRect &Header);
void Update(float DeltaTime);
void DrawItemHighlight(const void *pId, const CUIRect *pRect);
void DrawBlockHighlight(const void *pId, const CUIRect *pRect);
void SetActiveScrollRegion(CScrollRegion *pRegion);
void ClearActiveScrollRegion(CScrollRegion *pRegion);
} // namespace AmfSettingsSearch

#endif // GAME_CLIENT_COMPONENTS_TCLIENT_AMF_SETTINGS_SEARCH_H
