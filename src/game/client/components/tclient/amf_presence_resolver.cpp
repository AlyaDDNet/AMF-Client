#include "amf_presence_resolver.h"

#include <base/log.h>
#include <base/net.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

namespace
{
constexpr int PLAYER_VALIDATION_HZ = 4;

const char *InstallationIdSuffix(const char *pClientId)
{
	static constexpr size_t SUFFIX_LENGTH = 8;
	const size_t Length = str_length(pClientId);
	return Length > SUFFIX_LENGTH ? pClientId + Length - SUFFIX_LENGTH : pClientId;
}
}

void CAmfPresenceResolver::OnReset()
{
	ClearRemoteState();
}

void CAmfPresenceResolver::OnStateChange(int NewState, int OldState)
{
	(void)OldState;
	if(NewState == IClient::STATE_OFFLINE)
		ClearRemoteState();
}

void CAmfPresenceResolver::OnUpdate()
{
	const bool PresenceEnabled = g_Config.m_AmfPresenceEnabled != 0;
	if(PresenceEnabled != m_PresenceWasEnabled)
	{
		m_PresenceWasEnabled = PresenceEnabled;
		if(!PresenceEnabled)
			ClearRemoteState();
		else
			m_Dirty = true;
	}
	const bool LiveStateChanged = ConsumeTransportEvents();
	// A fresh snapshot or delta must be matched against the current client slots
	// immediately. Otherwise it can land just after the 4 Hz validation pass and
	// unnecessarily delay a correct indicator by up to one validation interval.
	RefreshPlayerSnapshot(LiveStateChanged);
	if(m_Dirty)
		RebuildIndicators();
}

bool CAmfPresenceResolver::GetPlayerGradient(int ClientId, SPlayerGradient &Gradient) const
{
	Gradient = {};
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	const SPublishedClaim &Claim = m_aPublishedClaims[ClientId];
	if(!Claim.m_Active || !Claim.m_Gradient.m_Active)
		return false;
	Gradient = Claim.m_Gradient;
	return true;
}

bool CAmfPresenceResolver::IsConfirmedAmf(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	const SPublishedClaim &Claim = m_aPublishedClaims[ClientId];
	if(!Claim.m_Active || Claim.m_SlotGeneration != m_aPlayerGenerations[ClientId])
		return false;
	// A newly populated slot can be observed before the 4 Hz snapshot pass. Do
	// not expose the previous confirmed identity for that frame when the server
	// already supplied a different player name.
	const auto &ClientData = GameClient()->m_aClients[ClientId];
	return !ClientData.m_Active || str_comp(ClientData.m_aName, Claim.m_PlayerName.c_str()) == 0;
}

bool CAmfPresenceResolver::ConsumeTransportEvents()
{
	bool LiveStateChanged = false;
	CAmfPresenceTransport::SRemoteEvent Event;
	while(GameClient()->m_AmfPresenceTransport.PollRemoteEvent(Event))
	{
		switch(Event.m_Type)
		{
		case CAmfPresenceTransport::ERemoteEventType::CONNECTED:
		case CAmfPresenceTransport::ERemoteEventType::DISCONNECTED:
			m_RemotePresence.clear();
			m_RemotePresenceAuthoritative.clear();
			// The websocket session no longer has an authoritative member list.
			// Keep already validated claims only until its next snapshot, so a
			// harmless reconnect cannot produce a false -> true cat flicker.
			m_AwaitingSnapshot = true;
			m_LastPresenceEvent = Event.m_Type == CAmfPresenceTransport::ERemoteEventType::CONNECTED ? "transport-connected" : "transport-disconnected";
			if(g_Config.m_AmfPresenceDebug)
				log_info("amf_presence", "resolver session changed; awaiting authoritative snapshot");
			m_Dirty = true;
			LiveStateChanged = true;
			break;
		case CAmfPresenceTransport::ERemoteEventType::SNAPSHOT:
			if(Event.m_SnapshotComplete)
			{
				m_RemotePresence.clear();
				m_RemotePresenceAuthoritative.clear();
			}
			for(auto &Presence : Event.m_vPresence)
			{
				const std::string PresenceId = Presence.m_PresenceId;
				m_RemotePresence.insert_or_assign(PresenceId, std::move(Presence));
				m_RemotePresenceAuthoritative[PresenceId] = Event.m_SnapshotComplete;
			}
			// Keep waiting through an incomplete payload. A complete snapshot is the
			// only member-list event allowed to invalidate a confirmed identity.
			m_AwaitingSnapshot = !Event.m_SnapshotComplete;
			m_LastPresenceEvent = Event.m_SnapshotComplete ? "presence-snapshot-complete" : "presence-snapshot-incomplete";
			if(g_Config.m_AmfPresenceDebug)
				log_info("amf_presence", "resolver replaced snapshot entries=%d", (int)m_RemotePresence.size());
			m_Dirty = true;
			LiveStateChanged = true;
			break;
		case CAmfPresenceTransport::ERemoteEventType::DELTA:
			if(Event.m_vPresence.size() == 1 && Event.m_vPresence.front().m_PresenceId == Event.m_PresenceId)
			{
				m_RemotePresence.insert_or_assign(Event.m_PresenceId, std::move(Event.m_vPresence.front()));
				// A delta is not a complete member-list assertion. A transient game
				// snapshot can therefore publish connected=false without invalidating
				// the already confirmed identity.
				m_RemotePresenceAuthoritative[Event.m_PresenceId] = false;
				m_LastPresenceEvent = "presence-delta";
				if(g_Config.m_AmfPresenceDebug)
					log_info("amf_presence", "resolver applied delta installation_suffix=%s", InstallationIdSuffix(Event.m_ClientId.c_str()));
				m_Dirty = true;
				LiveStateChanged = true;
			}
			break;
		case CAmfPresenceTransport::ERemoteEventType::OFFLINE:
		{
			const bool RemovedRemotePresence = m_RemotePresence.erase(Event.m_PresenceId) != 0;
			m_RemotePresenceAuthoritative.erase(Event.m_PresenceId);
			const bool InvalidatedPublishedClaim = InvalidatePublishedPresence(Event.m_PresenceId);
			m_LastPresenceEvent = "presence-offline";
			if(RemovedRemotePresence || InvalidatedPublishedClaim)
			{
				if(g_Config.m_AmfPresenceDebug)
					log_info("amf_presence", "resolver removed offline installation_suffix=%s", InstallationIdSuffix(Event.m_ClientId.c_str()));
				m_Dirty = true;
				LiveStateChanged = true;
			}
			break;
		}
		case CAmfPresenceTransport::ERemoteEventType::BROWSER_SNAPSHOT:
		{
			// Do not partially publish a malformed/incomplete browser response.
			// The previous confirmed metadata remains visible until a complete
			// replacement is ready.
			if(!Event.m_SnapshotComplete)
			{
				if(g_Config.m_AmfPresenceDebug)
					log_info("amf_presence", "browser snapshot ignored: incomplete; published metadata retained");
				break;
			}
			IServerBrowser *pServerBrowser = GameClient()->ServerBrowser();
			pServerBrowser->ClearAmfClientCounts();
			pServerBrowser->ClearAmfClientPlayerNames();
			for(const auto &Server : Event.m_vBrowserServers)
			{
				NETADDR Address;
				if(net_addr_from_str(&Address, Server.m_ServerKey.c_str()) == 0)
				{
					pServerBrowser->SetAmfClientCount(Address, Server.m_ActiveCount);
					pServerBrowser->SetAmfClientPlayerNames(Address, Server.m_vPlayerNames);
				}
			}
			// Publish the total only with the same complete replacement as the
			// per-server metadata, so the browser never shows a mixed generation.
			m_BrowserActiveUserCount = Event.m_TotalActiveUsers;
			if(g_Config.m_AmfPresenceDebug)
				log_info("amf_presence", "resolver applied browser snapshot servers=%d active_users=%d", (int)Event.m_vBrowserServers.size(), m_BrowserActiveUserCount);
			break;
		}
		}
	}
	return LiveStateChanged;
}

void CAmfPresenceResolver::RefreshPlayerSnapshot(bool Force)
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;
	const int64_t Now = time_get();
	if(!Force && m_LastPlayerValidationTick != 0 && Now - m_LastPlayerValidationTick < time_freq() / PLAYER_VALIDATION_HZ)
		return;
	m_LastPlayerValidationTick = Now;
	for(int Connection = 0; Connection < NUM_DUMMIES; ++Connection)
	{
		if(m_aLocalClientIds[Connection] == GameClient()->m_aLocalIds[Connection])
			continue;
		m_aLocalClientIds[Connection] = GameClient()->m_aLocalIds[Connection];
		m_Dirty = true;
	}

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		const bool Active = GameClient()->m_aClients[ClientId].m_Active;
		if(!Active)
		{
			// A missing player-info record can be a transient snapshot gap. Keep
			// the last confirmed name/generation so this alone cannot invalidate a
			// sticky AMF claim. Authoritative Presence/offline data still removes
			// the claim, and a different active name below advances the generation.
			if(m_aPlayerActive[ClientId])
			{
				m_aPlayerActive[ClientId] = false;
				m_LastPresenceEvent = "player-snapshot-inactive";
				m_Dirty = true;
			}
			continue;
		}

		const char *pName = GameClient()->m_aClients[ClientId].m_aName;
		// An active player slot can precede its ClientInfo/name by one snapshot.
		// Treat an empty name as incomplete metadata, never as a new identity.
		if(pName[0] == '\0')
			continue;
		const bool NameChanged = str_comp(m_aaPlayerNames[ClientId].data(), pName) != 0;
		if(m_aPlayerActive[ClientId] && !NameChanged)
			continue;
		if(NameChanged && m_aaPlayerNames[ClientId][0] != '\0')
			++m_aPlayerGenerations[ClientId];
		m_aPlayerActive[ClientId] = true;
		str_copy(m_aaPlayerNames[ClientId].data(), pName, m_aaPlayerNames[ClientId].size());
		m_LastPresenceEvent = "player-snapshot";
		m_Dirty = true;
	}
}

void CAmfPresenceResolver::RebuildIndicators()
{
	m_Dirty = false;
	if(Client()->State() != IClient::STATE_ONLINE || !g_Config.m_AmfPresenceEnabled)
	{
		m_aPublishedClaims.fill(SPublishedClaim{});
		ClearPublishedIndicators();
		return;
	}

	std::array<unsigned char, MAX_CLIENTS> aClaimCounts{};
	std::array<SPublishedClaim, MAX_CLIENTS> aResolvedClaims{};
	const auto AddClaim = [&](const std::string &PresenceId, const char *pInstallationId, const char *pConnectionName, bool Dummy, const CAmfPresenceTransport::SRemoteEvent::SConnection &Connection) {
		const int ClientId = Connection.m_GameClientId;
		if(!Connection.m_Connected)
			return;
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		{
			if(g_Config.m_AmfPresenceDebug)
				log_info("amf_presence", "resolver rejected installation_suffix=%s connection=%s reason=invalid_client_id value=%d", InstallationIdSuffix(pInstallationId), pConnectionName, ClientId);
			return;
		}
		if(!m_aPlayerActive[ClientId])
		{
			if(g_Config.m_AmfPresenceDebug)
				log_info("amf_presence", "resolver rejected installation_suffix=%s connection=%s client_id=%d reason=inactive_slot", InstallationIdSuffix(pInstallationId), pConnectionName, ClientId);
			return;
		}
		if(Connection.m_PlayerName != m_aaPlayerNames[ClientId].data())
		{
			if(g_Config.m_AmfPresenceDebug)
				log_info("amf_presence", "resolver rejected installation_suffix=%s connection=%s client_id=%d reason=player_name_mismatch", InstallationIdSuffix(pInstallationId), pConnectionName, ClientId);
			return;
		}
		if(IsLocalClientId(ClientId))
		{
			if(g_Config.m_AmfPresenceDebug)
				log_info("amf_presence", "resolver rejected installation_suffix=%s connection=%s client_id=%d reason=local_slot", InstallationIdSuffix(pInstallationId), pConnectionName, ClientId);
			return;
		}
		if(aClaimCounts[ClientId] == 0)
		{
			SPublishedClaim &Claim = aResolvedClaims[ClientId];
			Claim.m_Active = true;
			Claim.m_Dummy = Dummy;
			Claim.m_PresenceId = PresenceId;
			Claim.m_InstallationId = pInstallationId;
			Claim.m_PlayerName = Connection.m_PlayerName;
			Claim.m_SlotGeneration = m_aPlayerGenerations[ClientId];
			Claim.m_SettingsOpen = Connection.m_SettingsOpen;
			Claim.m_FastPractice = Connection.m_FastPractice;
			Claim.m_Gradient.m_Active = Connection.m_CustomGradient;
			Claim.m_Gradient.m_Color1 = Connection.m_GradientColor1;
			Claim.m_Gradient.m_Color2 = Connection.m_GradientColor2;
			Claim.m_Gradient.m_Position = Connection.m_GradientPosition;
			Claim.m_Gradient.m_Strength = Connection.m_GradientStrength;
		}
		if(aClaimCounts[ClientId] < 2)
			++aClaimCounts[ClientId];
	};

	for(const auto &[PresenceId, Presence] : m_RemotePresence)
	{
		AddClaim(PresenceId, Presence.m_ClientId.c_str(), "main", false, Presence.m_Main);
		AddClaim(PresenceId, Presence.m_ClientId.c_str(), "dummy", true, Presence.m_Dummy);
	}

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		// Ambiguous claims are deliberately rejected instead of attributing a
		// remote installation to the wrong DDNet player slot.
		if(aClaimCounts[ClientId] != 1)
		{
			if(g_Config.m_AmfPresenceDebug && aClaimCounts[ClientId] > 1)
				log_info("amf_presence", "resolver rejected client_id=%d reason=ambiguous_claims", ClientId);
			aResolvedClaims[ClientId] = SPublishedClaim{};
			continue;
		}
	}

	// A newly accepted claim replaces the old one immediately. If the player
	// snapshot briefly lags a live Presence update, retain the exact previous
	// binding instead of clearing the cat for one frame. It is removed as soon
	// as the authoritative snapshot, offline event, slot identity, or room reset
	// disproves that binding.
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		if(aClaimCounts[ClientId] == 0 && !aResolvedClaims[ClientId].m_Active && CanPreservePublishedClaim(ClientId, m_aPublishedClaims[ClientId]))
		{
			aResolvedClaims[ClientId] = m_aPublishedClaims[ClientId];
			if(g_Config.m_AmfPresenceDebug)
			{
				const SPublishedClaim &Claim = m_aPublishedClaims[ClientId];
				const auto RemoteIt = m_RemotePresence.find(Claim.m_PresenceId);
				const auto AuthoritativeIt = m_RemotePresenceAuthoritative.find(Claim.m_PresenceId);
				log_info("amf_presence", "claim transition=KEEP source=%s client_id=%d generation=%llu name='%s' presence=%s awaiting_snapshot=%d remote=%d authoritative=%d",
					m_LastPresenceEvent.c_str(), ClientId, (unsigned long long)Claim.m_SlotGeneration, Claim.m_PlayerName.c_str(), Claim.m_PresenceId.c_str(), m_AwaitingSnapshot ? 1 : 0,
					RemoteIt != m_RemotePresence.end() ? 1 : 0, AuthoritativeIt != m_RemotePresenceAuthoritative.end() && AuthoritativeIt->second ? 1 : 0);
			}
		}
	}

	ApplyPublishedClaims(aResolvedClaims);
}

bool CAmfPresenceResolver::IsLocalClientId(int ClientId) const
{
	if(ClientId == GameClient()->m_Snap.m_LocalClientId)
		return true;
	for(const int LocalId : GameClient()->m_aLocalIds)
	{
		if(ClientId == LocalId)
			return true;
	}
	return false;
}

bool CAmfPresenceResolver::CanPreservePublishedClaim(int ClientId, const SPublishedClaim &Claim) const
{
	if(!Claim.m_Active || IsLocalClientId(ClientId))
		return false;
	if(Claim.m_SlotGeneration != m_aPlayerGenerations[ClientId])
		return false;

	// A confirmed identity must not flicker because DDNet has not populated a
	// slot for a transient frame. A real active slot with a different name is an
	// identity change, while an inactive slot is merely incomplete until an
	// authoritative Presence snapshot or offline event says otherwise.
	if(m_aPlayerActive[ClientId] && str_comp(m_aaPlayerNames[ClientId].data(), Claim.m_PlayerName.c_str()) != 0)
		return false;

	// The reconnect gap has no member list to validate against. An incomplete
	// snapshot is the same non-authoritative state, so retain the prior claim
	// while the local slot identity is unchanged. OnReset still clears on a
	// room/server change and a complete snapshot reconciles it immediately.
	if(m_AwaitingSnapshot)
		return true;

	const auto It = m_RemotePresence.find(Claim.m_PresenceId);
	if(It == m_RemotePresence.end() || It->second.m_ClientId != Claim.m_InstallationId)
		return false;
	const auto &Connection = Claim.m_Dummy ? It->second.m_Dummy : It->second.m_Main;
	// ParseRemoteConnection deliberately treats a disconnected connection as
	// incomplete and does not require clientId/playerName. A delta can therefore
	// report connected=false while the exact Presence identity is still valid.
	// Do not compare the unset fields with the published claim in that case.
	if(!Connection.m_Connected)
	{
		// A disconnected connection in a delta/incomplete update is not an
		// authoritative member-list removal. Only a complete snapshot (or the
		// explicit OFFLINE event handled above) can invalidate the identity.
		const auto AuthoritativeIt = m_RemotePresenceAuthoritative.find(Claim.m_PresenceId);
		return AuthoritativeIt == m_RemotePresenceAuthoritative.end() || !AuthoritativeIt->second;
	}
	if(Connection.m_GameClientId != ClientId || Connection.m_PlayerName != Claim.m_PlayerName)
		return false;
	return true;
}

void CAmfPresenceResolver::ApplyPublishedClaims(const std::array<SPublishedClaim, MAX_CLIENTS> &aClaims)
{
	const auto SameIdentity = [](const SPublishedClaim &Left, const SPublishedClaim &Right) {
		return Left.m_Dummy == Right.m_Dummy && Left.m_PresenceId == Right.m_PresenceId && Left.m_InstallationId == Right.m_InstallationId && Left.m_PlayerName == Right.m_PlayerName && Left.m_SlotGeneration == Right.m_SlotGeneration;
	};
	const auto SameGradient = [](const SPlayerGradient &Left, const SPlayerGradient &Right) {
		return Left.m_Active == Right.m_Active && Left.m_Color1 == Right.m_Color1 && Left.m_Color2 == Right.m_Color2 &&
			Left.m_Position == Right.m_Position && Left.m_Strength == Right.m_Strength;
	};
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		const SPublishedClaim &OldClaim = m_aPublishedClaims[ClientId];
		const SPublishedClaim &NewClaim = aClaims[ClientId];
		const bool HadOldClaim = OldClaim.m_Active;
		const bool HasNewClaim = NewClaim.m_Active;
		const bool IdentityChanged = HadOldClaim && HasNewClaim && !SameIdentity(OldClaim, NewClaim);
		const bool StateChanged = HadOldClaim && HasNewClaim && (OldClaim.m_SettingsOpen != NewClaim.m_SettingsOpen || OldClaim.m_FastPractice != NewClaim.m_FastPractice || !SameGradient(OldClaim.m_Gradient, NewClaim.m_Gradient));

		if(HadOldClaim && (!HasNewClaim || IdentityChanged))
		{
			if(g_Config.m_AmfPresenceDebug)
				log_info("amf_presence", "claim transition=INVALIDATE source=%s client_id=%d generation=%llu name='%s' presence=%s reason=%s awaiting_snapshot=%d",
					m_LastPresenceEvent.c_str(), ClientId, (unsigned long long)OldClaim.m_SlotGeneration, OldClaim.m_PlayerName.c_str(), OldClaim.m_PresenceId.c_str(), IdentityChanged ? "slot_identity_changed" : "claim_not_present", m_AwaitingSnapshot ? 1 : 0);
			ClearPublishedClaim(ClientId);
		}
		if(!HasNewClaim)
			continue;

		if(!HadOldClaim || IdentityChanged)
		{
			GameClient()->m_SettingsStatusIndicator.SetPresenceSettingsOpen(ClientId, NewClaim.m_SettingsOpen);
			GameClient()->m_SettingsStatusIndicator.SetPresenceFastPractice(ClientId, NewClaim.m_FastPractice);
			if(g_Config.m_AmfPresenceDebug)
				log_info("amf_presence", "claim transition=CONFIRM source=%s client_id=%d generation=%llu name='%s' presence=%s settings_open=%d fast_practice=%d",
					m_LastPresenceEvent.c_str(), ClientId, (unsigned long long)NewClaim.m_SlotGeneration, NewClaim.m_PlayerName.c_str(), NewClaim.m_PresenceId.c_str(), NewClaim.m_SettingsOpen, NewClaim.m_FastPractice);
		}
		else if(StateChanged)
		{
			GameClient()->m_SettingsStatusIndicator.SetPresenceSettingsOpen(ClientId, NewClaim.m_SettingsOpen);
			GameClient()->m_SettingsStatusIndicator.SetPresenceFastPractice(ClientId, NewClaim.m_FastPractice);
			if(g_Config.m_AmfPresenceDebug)
				log_info("amf_presence", "resolver updated client_id=%d settings_open=%d fast_practice=%d", ClientId, NewClaim.m_SettingsOpen, NewClaim.m_FastPractice);
		}
		m_aPublishedClaims[ClientId] = NewClaim;
	}
}

void CAmfPresenceResolver::ClearPublishedClaim(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	m_aPublishedClaims[ClientId] = SPublishedClaim{};
	GameClient()->m_SettingsStatusIndicator.ClearPresenceSettingsOpen(ClientId);
	GameClient()->m_SettingsStatusIndicator.ClearPresenceFastPractice(ClientId);
}

bool CAmfPresenceResolver::InvalidatePublishedPresence(const std::string &PresenceId)
{
	bool Invalidated = false;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		if(m_aPublishedClaims[ClientId].m_Active && m_aPublishedClaims[ClientId].m_PresenceId == PresenceId)
		{
			if(g_Config.m_AmfPresenceDebug)
			{
				const SPublishedClaim &Claim = m_aPublishedClaims[ClientId];
				log_info("amf_presence", "claim transition=INVALIDATE source=presence-offline client_id=%d generation=%llu name='%s' presence=%s reason=explicit_offline",
					ClientId, (unsigned long long)Claim.m_SlotGeneration, Claim.m_PlayerName.c_str(), Claim.m_PresenceId.c_str());
			}
			ClearPublishedClaim(ClientId);
			Invalidated = true;
		}
	}
	return Invalidated;
}

void CAmfPresenceResolver::ClearRemoteState()
{
	m_RemotePresence.clear();
	m_RemotePresenceAuthoritative.clear();
	m_aPublishedClaims.fill(SPublishedClaim{});
	m_aPlayerActive.fill(false);
	for(auto &aName : m_aaPlayerNames)
		aName.fill('\0');
	m_aPlayerGenerations.fill(0);
	m_aLocalClientIds.fill(-1);
	m_LastPlayerValidationTick = 0;
	m_PresenceWasEnabled = g_Config.m_AmfPresenceEnabled != 0;
	m_AwaitingSnapshot = false;
	m_LastPresenceEvent = "reset";
	// Browser totals belong to the same Presence lifecycle as the per-server
	// materialized cache. Do not carry a stale count across a reset, room
	// change, or an explicit Presence disable.
	GameClient()->ServerBrowser()->ClearAmfClientCounts();
	GameClient()->ServerBrowser()->ClearAmfClientPlayerNames();
	m_BrowserActiveUserCount = 0;
	m_Dirty = true;
	ClearPublishedIndicators();
}

void CAmfPresenceResolver::ClearPublishedIndicators()
{
	GameClient()->m_SettingsStatusIndicator.ClearPresenceSettingsOpen();
	GameClient()->m_SettingsStatusIndicator.ClearPresenceFastPractice();
}
