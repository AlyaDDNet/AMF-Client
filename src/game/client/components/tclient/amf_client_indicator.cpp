#include "amf_client_indicator.h"

#include <base/log.h>
#include <base/mem.h>

#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/client/ui_rect.h>

static constexpr const char *AMF_INDICATOR_TEXTURE_PATH = "amfclient/amf_cat.png";
static constexpr float AMF_INDICATOR_ASPECT = 327.0f / 287.0f;
static constexpr float AMF_INDICATOR_U1 = 378.0f / 1024.0f;
static constexpr float AMF_INDICATOR_V1 = 619.0f / 1536.0f;
static constexpr float AMF_INDICATOR_U2 = 705.0f / 1024.0f;
static constexpr float AMF_INDICATOR_V2 = 906.0f / 1536.0f;

CAmfClientIndicator::CAmfClientIndicator()
{
	OnReset();
}

void CAmfClientIndicator::OnInit()
{
	CImageInfo Image;
	if(!Graphics()->LoadPng(Image, AMF_INDICATOR_TEXTURE_PATH, IStorage::TYPE_ALL))
	{
		log_error("amf_client", "Failed to load AMF Client indicator texture '%s'; the indicator will be disabled.", AMF_INDICATOR_TEXTURE_PATH);
		return;
	}

	m_Texture = Graphics()->LoadTextureRawMove(Image, 0, AMF_INDICATOR_TEXTURE_PATH);
	if(!m_Texture.IsValid())
		log_error("amf_client", "Failed to create AMF Client indicator texture '%s'; the indicator will be disabled.", AMF_INDICATOR_TEXTURE_PATH);
	else if(g_Config.m_AmfPresenceDebug)
		log_info("amf_presence", "AMF indicator PNG loaded path='%s'", AMF_INDICATOR_TEXTURE_PATH);
}

void CAmfClientIndicator::OnShutdown()
{
	if(m_Texture.IsValid())
		Graphics()->UnloadTexture(&m_Texture);
}

void CAmfClientIndicator::OnReset()
{
	mem_zero(m_aDebugRenderedWorld, sizeof(m_aDebugRenderedWorld));
}

void CAmfClientIndicator::OnStateChange(int NewState, int OldState)
{
	(void)OldState;
	if(NewState == IClient::STATE_OFFLINE)
		mem_zero(m_aDebugRenderedWorld, sizeof(m_aDebugRenderedWorld));
}

bool CAmfClientIndicator::IsPlayerAmf(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	if(Client()->State() != IClient::STATE_ONLINE)
		return false;

	for(const int LocalId : GameClient()->m_aLocalIds)
	{
		if(ClientId == LocalId)
			return true;
	}
	return GameClient()->m_AmfPresenceResolver.IsConfirmedAmf(ClientId);
}

bool CAmfClientIndicator::CanRender(int ClientId) const
{
	if(!g_Config.m_AmfClientIndicator || !m_Texture.IsValid() || !IsPlayerAmf(ClientId))
		return false;

	// Position mode 2 is a local display preference, not a Presence opt-out.
	// Use the canonical local-id list so TAB, nameplates, spectator and player
	// info all agree without introducing a second identity heuristic.
	if(g_Config.m_AmfClientIndicatorPosition == 2)
	{
		for(const int LocalId : GameClient()->m_aLocalIds)
		{
			if(ClientId == LocalId)
				return false;
		}
	}

	return true;
}

bool CAmfClientIndicator::CanRenderInWorld(int ClientId) const
{
	if(!CanRender(ClientId))
		return false;

	if(g_Config.m_AmfPresenceDebug && IsPlayerAmf(ClientId) && !m_aDebugRenderedWorld[ClientId])
	{
		m_aDebugRenderedWorld[ClientId] = true;
		log_info("amf_presence", "AMF world indicator renderer accepted client_id=%d texture_loaded=%d", ClientId, m_Texture.IsValid());
	}
	return true;
}

bool CAmfClientIndicator::LayoutName(const CUIRect &Available, int ClientId, float FontSize, float NameWidth, CUIRect &NameRect, CUIRect &IconRect, bool ForceRight) const
{
	NameRect = Available;
	if(!CanRender(ClientId))
		return false;

	const float IconHeight = std::min(Available.h, std::max(12.0f, FontSize * 1.25f));
	const float IconWidth = IconHeight * AMF_INDICATOR_ASPECT;
	const float Spacing = std::max(2.0f, FontSize * 0.25f);
	if(!ForceRight && g_Config.m_AmfClientIndicatorPosition == 0)
	{
		IconRect = {
			Available.x - Spacing - IconWidth,
			Available.y + (Available.h - IconHeight) / 2.0f,
			IconWidth,
			IconHeight};
	}
	else
	{
		const float VisibleNameWidth = std::min(NameWidth, Available.w);
		IconRect = {
			Available.x + VisibleNameWidth + Spacing,
			Available.y + (Available.h - IconHeight) / 2.0f,
			IconWidth,
			IconHeight};
	}
	return true;
}

void CAmfClientIndicator::RenderIcon(const CUIRect &Rect, float Alpha) const
{
	if(!m_Texture.IsValid())
		return;

	Graphics()->TextureSet(m_Texture);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
	Graphics()->QuadsSetSubset(AMF_INDICATOR_U1, AMF_INDICATOR_V1, AMF_INDICATOR_U2, AMF_INDICATOR_V2);
	float Width = Rect.h * AMF_INDICATOR_ASPECT;
	float Height = Rect.h;
	if(Width > Rect.w)
	{
		Width = Rect.w;
		Height = Width / AMF_INDICATOR_ASPECT;
	}
	const IGraphics::CQuadItem Quad(
		Rect.x + (Rect.w - Width) / 2.0f,
		Rect.y + (Rect.h - Height) / 2.0f,
		Width,
		Height);
	Graphics()->QuadsDrawTL(&Quad, 1);
	Graphics()->QuadsEnd();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}
