#include "settings_status_indicator.h"

#include <base/color.h>
#include <base/log.h>

#include <engine/shared/config.h>

#include <game/client/components/menus.h>
#include <game/client/gameclient.h>

namespace
{
constexpr const char *SETTINGS_STATUS_TEXTURE_PATH = "amfclient/gui_settings.png";
constexpr float SETTINGS_STATUS_SIZE = 24.0f;
constexpr ColorRGBA FAST_PRACTICE_GEAR_COLOR(0.55f, 0.78f, 0.96f, 1.0f);
}

void CSettingsStatusIndicator::OnInit()
{
	CImageInfo Image;
	if(!Graphics()->LoadPng(Image, SETTINGS_STATUS_TEXTURE_PATH, IStorage::TYPE_ALL))
	{
		log_error("amf_client", "Failed to load settings status texture '%s'.", SETTINGS_STATUS_TEXTURE_PATH);
		return;
	}
	m_Texture = Graphics()->LoadTextureRawMove(Image, 0, SETTINGS_STATUS_TEXTURE_PATH);
	if(g_Config.m_AmfPresenceDebug && m_Texture.IsValid())
		log_info("amf_presence", "settings gear PNG loaded path='%s'", SETTINGS_STATUS_TEXTURE_PATH);
}

void CSettingsStatusIndicator::OnShutdown()
{
	if(m_Texture.IsValid())
		Graphics()->UnloadTexture(&m_Texture);
}

void CSettingsStatusIndicator::OnReset()
{
	m_aPresenceSettingsKnown.fill(false);
	m_aPresenceSettingsOpen.fill(false);
	m_aPresenceFastPractice.fill(false);
	m_aDebugRendered.fill(false);
}

void CSettingsStatusIndicator::OnStateChange(int NewState, int OldState)
{
	(void)OldState;
	if(NewState == IClient::STATE_OFFLINE)
		OnReset();
}

void CSettingsStatusIndicator::SetPresenceSettingsOpen(int ClientId, bool Open)
{
	if(ClientId >= 0 && ClientId < MAX_CLIENTS)
	{
		m_aPresenceSettingsKnown[ClientId] = true;
		m_aPresenceSettingsOpen[ClientId] = Open;
		if(!Open)
			m_aDebugRendered[ClientId] = false;
	}
}

void CSettingsStatusIndicator::ClearPresenceSettingsOpen(int ClientId)
{
	if(ClientId >= 0 && ClientId < MAX_CLIENTS)
	{
		m_aPresenceSettingsKnown[ClientId] = false;
		m_aPresenceSettingsOpen[ClientId] = false;
		m_aDebugRendered[ClientId] = false;
	}
}

void CSettingsStatusIndicator::ClearPresenceSettingsOpen()
{
	m_aPresenceSettingsKnown.fill(false);
	m_aPresenceSettingsOpen.fill(false);
	m_aDebugRendered.fill(false);
}

void CSettingsStatusIndicator::SetPresenceFastPractice(int ClientId, bool Active)
{
	if(ClientId >= 0 && ClientId < MAX_CLIENTS)
		m_aPresenceFastPractice[ClientId] = Active;
}

void CSettingsStatusIndicator::ClearPresenceFastPractice(int ClientId)
{
	if(ClientId >= 0 && ClientId < MAX_CLIENTS)
		m_aPresenceFastPractice[ClientId] = false;
}

void CSettingsStatusIndicator::ClearPresenceFastPractice()
{
	m_aPresenceFastPractice.fill(false);
}

bool CSettingsStatusIndicator::IsLocalSettingsOpen(int ClientId) const
{
	if(Client()->State() != IClient::STATE_ONLINE || !GameClient()->m_Menus.IsActive())
		return false;

	return GameClient()->m_Snap.m_LocalClientId == ClientId;
}

bool CSettingsStatusIndicator::IsSettingsOpen(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	const bool IsLocalClient = ClientId == GameClient()->m_Snap.m_LocalClientId ||
		ClientId == GameClient()->m_aLocalIds[IClient::CONN_MAIN] ||
		(Client()->DummyConnected() && ClientId == GameClient()->m_aLocalIds[IClient::CONN_DUMMY]);
	if(IsLocalClient)
	{
		// The live menu owns every local slot. This intentionally never falls
		// through to delayed snapshot or Presence mirrors after an ESC close.
		return IsLocalSettingsOpen(ClientId);
	}
	// Every DDNet player publishes PLAYERFLAG_IN_MENU through regular snapshots.
	// AMF Presence is more current when it exists, especially for a just-closed
	// menu where the server-side flag can linger for a snapshot.
	if(m_aPresenceSettingsKnown[ClientId])
		return m_aPresenceSettingsOpen[ClientId];
	return (GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur.m_PlayerFlags & PLAYERFLAG_IN_MENU) != 0;
}

bool CSettingsStatusIndicator::IsFastPracticeActive(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || Client()->State() != IClient::STATE_ONLINE)
		return false;
	for(const int LocalId : GameClient()->m_aLocalIds)
	{
		if(ClientId == LocalId)
			return GameClient()->m_FastPractice.Active() && GameClient()->m_FastPractice.IsPracticeParticipant(ClientId);
	}
	return m_aPresenceFastPractice[ClientId] && GameClient()->m_AmfClientIndicator.IsPlayerAmf(ClientId);
}

void CSettingsStatusIndicator::OnRender()
{
	if(!m_Texture.IsValid() || Client()->State() != IClient::STATE_ONLINE)
		return;

	Graphics()->TextureSet(m_Texture);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	Graphics()->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		if(g_Config.m_AmfClientIndicatorPosition == 2)
		{
			bool IsLocalClient = false;
			for(const int LocalId : GameClient()->m_aLocalIds)
			{
				if(ClientId == LocalId)
				{
					IsLocalClient = true;
					break;
				}
			}
			if(IsLocalClient)
			{
				m_aDebugRendered[ClientId] = false;
				continue;
			}
		}
		const bool FastPracticeActive = IsFastPracticeActive(ClientId);
		if((!FastPracticeActive && !IsSettingsOpen(ClientId)) || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
		{
			m_aDebugRendered[ClientId] = false;
			continue;
		}
		if(g_Config.m_AmfPresenceDebug && !m_aDebugRendered[ClientId])
		{
			m_aDebugRendered[ClientId] = true;
			log_info("amf_presence", "settings gear renderer drew client_id=%d remote=%d fast_practice=%d texture_loaded=%d", ClientId, m_aPresenceSettingsOpen[ClientId], FastPracticeActive, m_Texture.IsValid());
		}
		const ColorRGBA GearColor = FastPracticeActive ? FAST_PRACTICE_GEAR_COLOR : ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		Graphics()->SetColor(GearColor.r, GearColor.g, GearColor.b, GearColor.a);
		vec2 Pos = GameClient()->m_aClients[ClientId].m_RenderPos;
		// Fast Practice replaces a local participant's m_RenderPos with its
		// practice-world Tee. Its gear belongs to the real server-world marker.
		if(FastPracticeActive && GameClient()->m_FastPractice.IsPracticeParticipant(ClientId) &&
			!GameClient()->m_FastPractice.GetRegularWorldMarkerPosition(ClientId, Pos))
			continue;
		const IGraphics::CQuadItem Quad(Pos.x - SETTINGS_STATUS_SIZE / 2.0f, Pos.y - SETTINGS_STATUS_SIZE / 2.0f - 5.0f, SETTINGS_STATUS_SIZE, SETTINGS_STATUS_SIZE);
		Graphics()->QuadsDrawTL(&Quad, 1);
	}
	Graphics()->QuadsEnd();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}
