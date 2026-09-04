#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_AMF_GRADIENT_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_AMF_GRADIENT_H

#include <base/color.h>

#include <engine/textrender.h>

class CGameClient;

// Shared, render-only gradient color calculations. The individual renderers
// remain authoritative for their geometry and only ask this helper for colors.
namespace AmfGradient
{
struct CNicknameGradientSource
{
	ColorRGBA m_SkinBody;
	ColorRGBA m_SkinFeet;
	ColorRGBA m_TeamOrWarColor;
	bool m_HasSkinColors = false;
	bool m_HasTeamOrWarColor = false;
	bool m_IsLocalPlayer = false;
	bool m_HasPublishedCustomGradient = false;
	ColorRGBA m_PublishedGradientColor1;
	ColorRGBA m_PublishedGradientColor2;
	int m_PublishedGradientPosition = 0;
	int m_PublishedGradientStrength = 100;
};

bool NicknamesEnabled();
void GetNicknameColors(const CNicknameGradientSource &Source, const ColorRGBA &BaseColor, ColorRGBA &Left, ColorRGBA &Right);
void GetNicknameColors(int ClientId, CGameClient *pGameClient, const ColorRGBA &BaseColor, ColorRGBA &Left, ColorRGBA &Right);
void AppendNicknameColorSplits(CTextCursor &Cursor, const char *pText, const CNicknameGradientSource &Source, const ColorRGBA &BaseColor);
void AppendNicknameColorSplits(CTextCursor &Cursor, const char *pText, int ClientId, CGameClient *pGameClient, const ColorRGBA &BaseColor);
void GetBackgroundGradient(const ColorRGBA &BaseColor, ColorRGBA &TopLeft, ColorRGBA &TopRight, ColorRGBA &BottomLeft, ColorRGBA &BottomRight);
}

#endif
