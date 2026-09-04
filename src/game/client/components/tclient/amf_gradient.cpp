#include "amf_gradient.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <cstddef>

namespace
{
ColorRGBA MixColor(const ColorRGBA &From, const ColorRGBA &To, float Amount)
{
	Amount = std::clamp(Amount, 0.0f, 1.0f);
	return ColorRGBA(
		mix(From.r, To.r, Amount),
		mix(From.g, To.g, Amount),
		mix(From.b, To.b, Amount),
		mix(From.a, To.a, Amount));
}

ColorRGBA ConfigColor(unsigned Value)
{
	return color_cast<ColorRGBA>(ColorHSLA(Value, true));
}

bool IsLocalPlayer(CGameClient *pGameClient, int ClientId)
{
	return pGameClient != nullptr &&
		(ClientId == pGameClient->m_aLocalIds[0] || ClientId == pGameClient->m_aLocalIds[1]);
}

bool GetTeamOrWarColor(CGameClient *pGameClient, int ClientId, ColorRGBA &Color)
{
	if(pGameClient == nullptr || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;

	if(g_Config.m_TcWarList && pGameClient->m_WarList.GetAnyWar(ClientId))
	{
		Color = pGameClient->m_WarList.GetPriorityColor(ClientId);
		return true;
	}

	if(pGameClient->IsTeamPlay())
	{
		const int Team = pGameClient->m_aClients[ClientId].m_Team;
		if(Team == TEAM_RED)
		{
			Color = ColorRGBA(1.0f, 0.5f, 0.5f);
			return true;
		}
		if(Team == TEAM_BLUE)
		{
			Color = ColorRGBA(0.7f, 0.7f, 1.0f);
			return true;
		}
	}
	else
	{
		const int Team = pGameClient->m_Teams.Team(ClientId);
		if(Team != TEAM_FLOCK)
		{
			Color = pGameClient->GetDDTeamColor(Team);
			return true;
		}
	}

	return false;
}

void GetResolvedSkinColors(const CGameClient::CClientData &Client, ColorRGBA &Body, ColorRGBA &Feet)
{
	// m_RenderInfo is intentionally overwritten with red/blue colors in team games.
	// Skin-source gradients must instead use the player's original resolved skin.
	const CTeeRenderInfo &RenderInfo = Client.m_pSkinInfo ? Client.m_pSkinInfo->TeeRenderInfo() : Client.m_RenderInfo;
	const bool IsSixupSkin = Client.m_pSkinInfo != nullptr &&
		(Client.m_pSkinInfo->SkinDescriptor().m_Flags & CSkinDescriptor::FLAG_SEVEN) != 0;

	if(IsSixupSkin)
	{
		const CTeeRenderInfo::CSixup &Sixup = RenderInfo.m_aSixup[g_Config.m_ClDummy];
		Body = Sixup.m_aUseCustomColors[protocol7::SKINPART_BODY] ?
			Sixup.m_aColors[protocol7::SKINPART_BODY] : Sixup.m_aOriginalColors[protocol7::SKINPART_BODY];
		Feet = Sixup.m_aUseCustomColors[protocol7::SKINPART_FEET] ?
			Sixup.m_aColors[protocol7::SKINPART_FEET] : Sixup.m_aOriginalColors[protocol7::SKINPART_FEET];
	}
	else if(RenderInfo.m_CustomColoredSkin)
	{
		Body = RenderInfo.m_ColorBody;
		Feet = RenderInfo.m_ColorFeet;
	}
	else
	{
		Body = RenderInfo.m_OriginalBodyColor;
		Feet = RenderInfo.m_OriginalFeetColor;
	}
}

void CalculateNicknameColors(const AmfGradient::CNicknameGradientSource &Source, const ColorRGBA &BaseColor, ColorRGBA &Left, ColorRGBA &Right)
{
	float Strength = std::clamp(g_Config.m_AmfGradientStrength / 100.0f, 0.0f, 1.0f);
	bool Reverse = g_Config.m_AmfGradientPosition != 0;
	if(g_Config.m_AmfGradientCustomSelf && Source.m_IsLocalPlayer)
	{
		Left = ConfigColor(g_Config.m_AmfGradientColor1).WithAlpha(BaseColor.a);
		Right = ConfigColor(g_Config.m_AmfGradientColor2).WithAlpha(BaseColor.a);
	}
	else if(Source.m_HasPublishedCustomGradient)
	{
		Left = Source.m_PublishedGradientColor1.WithAlpha(BaseColor.a);
		Right = Source.m_PublishedGradientColor2.WithAlpha(BaseColor.a);
		Strength = std::clamp(Source.m_PublishedGradientStrength / 100.0f, 0.0f, 1.0f);
		Reverse = Source.m_PublishedGradientPosition != 0;
	}
	else if(g_Config.m_AmfGradientNicknameSource == 0 && Source.m_HasSkinColors)
	{
		Left = Source.m_SkinBody;
		Right = Source.m_SkinFeet;
		Left = Left.WithAlpha(BaseColor.a);
		Right = Right.WithAlpha(BaseColor.a);
	}
	else
	{
		const ColorRGBA TeamOrWarColor = Source.m_HasTeamOrWarColor ? Source.m_TeamOrWarColor : BaseColor;
		Left = ColorRGBA(TeamOrWarColor.r * 0.48f, TeamOrWarColor.g * 0.48f, TeamOrWarColor.b * 0.48f, BaseColor.a);
		Right = ColorRGBA(
			std::min(1.0f, TeamOrWarColor.r * 0.62f + 0.28f),
			std::min(1.0f, TeamOrWarColor.g * 0.62f + 0.28f),
			std::min(1.0f, TeamOrWarColor.b * 0.62f + 0.28f),
			BaseColor.a);
	}

	Left = MixColor(BaseColor, Left, Strength);
	Right = MixColor(BaseColor, Right, Strength);
	if(Reverse)
		std::swap(Left, Right);
}

void PopulateNicknameGradientSource(AmfGradient::CNicknameGradientSource &Source, int ClientId, CGameClient *pGameClient)
{
	Source.m_IsLocalPlayer = IsLocalPlayer(pGameClient, ClientId);
	if(pGameClient == nullptr || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	GetResolvedSkinColors(pGameClient->m_aClients[ClientId], Source.m_SkinBody, Source.m_SkinFeet);
	Source.m_HasSkinColors = true;
	Source.m_HasTeamOrWarColor = GetTeamOrWarColor(pGameClient, ClientId, Source.m_TeamOrWarColor);

	// The remote value has already been identity-validated and cached by the
	// Presence resolver. The viewer preference controls rendering only; it
	// never affects the publisher's Presence membership or state.
	CAmfPresenceResolver::SPlayerGradient Gradient;
	if(!Source.m_IsLocalPlayer && g_Config.m_AmfGradientShowPlayerCustom != 0 && pGameClient->m_AmfPresenceResolver.GetPlayerGradient(ClientId, Gradient))
	{
		Source.m_HasPublishedCustomGradient = true;
		Source.m_PublishedGradientColor1 = ConfigColor(Gradient.m_Color1);
		Source.m_PublishedGradientColor2 = ConfigColor(Gradient.m_Color2);
		Source.m_PublishedGradientPosition = Gradient.m_Position;
		Source.m_PublishedGradientStrength = Gradient.m_Strength;
	}
}
}

namespace AmfGradient
{
bool NicknamesEnabled()
{
	return g_Config.m_AmfGradientNicknames != 0;
}

void GetNicknameColors(int ClientId, CGameClient *pGameClient, const ColorRGBA &BaseColor, ColorRGBA &Left, ColorRGBA &Right)
{
	CNicknameGradientSource Source;
	PopulateNicknameGradientSource(Source, ClientId, pGameClient);
	GetNicknameColors(Source, BaseColor, Left, Right);
}

void GetNicknameColors(const CNicknameGradientSource &Source, const ColorRGBA &BaseColor, ColorRGBA &Left, ColorRGBA &Right)
{
	CalculateNicknameColors(Source, BaseColor, Left, Right);
}

void AppendNicknameColorSplits(CTextCursor &Cursor, const char *pText, const CNicknameGradientSource &Source, const ColorRGBA &BaseColor)
{
	if(!NicknamesEnabled() || pText == nullptr || pText[0] == '\0')
		return;

	ColorRGBA Left, Right;
	GetNicknameColors(Source, BaseColor, Left, Right);

	size_t TextSize = 0;
	size_t CharacterCount = 0;
	str_utf8_stats(pText, str_length(pText) + 1, SIZE_MAX, &TextSize, &CharacterCount);
	if(CharacterCount == 0)
		return;

	Cursor.m_vColorSplits.reserve(Cursor.m_vColorSplits.size() + CharacterCount);
	const char *pCurrent = pText;
	for(size_t Character = 0; Character < CharacterCount; ++Character)
	{
		const int ByteOffset = (int)(pCurrent - pText);
		const char *pPrevious = pCurrent;
		str_utf8_decode(&pCurrent);
		const int ByteLength = (int)(pCurrent - pPrevious);
		const float Amount = CharacterCount == 1 ? 0.5f : (float)Character / (float)(CharacterCount - 1);
		Cursor.m_vColorSplits.emplace_back(
			Cursor.m_CharCount + ByteOffset,
			ByteLength,
			MixColor(Left, Right, Amount));
	}
}

void AppendNicknameColorSplits(CTextCursor &Cursor, const char *pText, int ClientId, CGameClient *pGameClient, const ColorRGBA &BaseColor)
{
	if(!NicknamesEnabled() || pText == nullptr || pText[0] == '\0')
		return;
	CNicknameGradientSource Source;
	PopulateNicknameGradientSource(Source, ClientId, pGameClient);
	AppendNicknameColorSplits(Cursor, pText, Source, BaseColor);
}

void GetBackgroundGradient(const ColorRGBA &BaseColor, ColorRGBA &TopLeft, ColorRGBA &TopRight, ColorRGBA &BottomLeft, ColorRGBA &BottomRight)
{
	const ColorRGBA Dark(BaseColor.r * 0.55f, BaseColor.g * 0.55f, BaseColor.b * 0.55f, BaseColor.a);
	const ColorRGBA Light(
		std::min(1.0f, BaseColor.r * 0.70f + 0.20f),
		std::min(1.0f, BaseColor.g * 0.70f + 0.20f),
		std::min(1.0f, BaseColor.b * 0.70f + 0.20f),
		BaseColor.a);
	const float Strength = std::clamp(g_Config.m_AmfGradientStrength / 100.0f, 0.0f, 1.0f);
	const ColorRGBA Left = MixColor(BaseColor, Dark, Strength);
	const ColorRGBA Right = MixColor(BaseColor, Light, Strength);
	if(g_Config.m_AmfGradientPosition == 0)
	{
		TopLeft = BottomLeft = Left;
		TopRight = BottomRight = Right;
	}
	else
	{
		TopLeft = BottomLeft = Right;
		TopRight = BottomRight = Left;
	}
}
}
