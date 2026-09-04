#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_AMF_PRESENCE_RESOLVER_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_AMF_PRESENCE_RESOLVER_H

#include "amf_presence_transport.h"

#include <engine/client/enums.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

class CAmfPresenceResolver final : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnUpdate() override;
	int BrowserActiveUserCount() const { return m_BrowserActiveUserCount; }
	// Canonical, already validated AMF identity for in-game consumers.
	bool IsConfirmedAmf(int ClientId) const;

	// Already validated numeric gradient data for a confirmed remote AMF slot.
	// Renderers only read this cache; they never parse transport JSON per frame.
	struct SPlayerGradient
	{
		bool m_Active = false;
		unsigned m_Color1 = 0;
		unsigned m_Color2 = 0;
		int m_Position = 0;
		int m_Strength = 100;
	};
	bool GetPlayerGradient(int ClientId, SPlayerGradient &Gradient) const;

private:
	using SPresence = CAmfPresenceTransport::SRemoteEvent::SPresence;
	// A validated live Presence claim is kept separately from the latest raw
	// websocket snapshot. This lets a just-received member survive the short
	// interval where DDNet has not populated its player slot yet, without ever
	// attributing it to a different slot or session.
	struct SPublishedClaim
	{
		bool m_Active = false;
		bool m_Dummy = false;
		std::string m_PresenceId;
		std::string m_InstallationId;
		std::string m_PlayerName;
		uint64_t m_SlotGeneration = 0;
		bool m_SettingsOpen = false;
		bool m_FastPractice = false;
		SPlayerGradient m_Gradient;
	};

	bool ConsumeTransportEvents();
	void RefreshPlayerSnapshot(bool Force = false);
	void RebuildIndicators();
	bool IsLocalClientId(int ClientId) const;
	bool CanPreservePublishedClaim(int ClientId, const SPublishedClaim &Claim) const;
	void ApplyPublishedClaims(const std::array<SPublishedClaim, MAX_CLIENTS> &aClaims);
	void ClearPublishedClaim(int ClientId);
	bool InvalidatePublishedPresence(const std::string &PresenceId);
	void ClearRemoteState();
	void ClearPublishedIndicators();

	std::unordered_map<std::string, SPresence> m_RemotePresence;
	// Values received from a complete member snapshot are authoritative. Delta
	// and incomplete values remain useful for updates, but cannot by themselves
	// evict an already confirmed identity when a connection briefly reports
	// disconnected.
	std::unordered_map<std::string, bool> m_RemotePresenceAuthoritative;
	std::array<SPublishedClaim, MAX_CLIENTS> m_aPublishedClaims{};
	std::array<bool, MAX_CLIENTS> m_aPlayerActive{};
	std::array<std::array<char, MAX_NAME_LENGTH>, MAX_CLIENTS> m_aaPlayerNames{};
	std::array<uint64_t, MAX_CLIENTS> m_aPlayerGenerations{};
	std::array<int, NUM_DUMMIES> m_aLocalClientIds{};
	int64_t m_LastPlayerValidationTick = 0;
	bool m_PresenceWasEnabled = false;
	// A reconnect has no authoritative member list until the next snapshot.
	// Previously resolved claims may remain visible only while their local slot
	// identity has not changed; the next snapshot reconciles them immediately.
	bool m_AwaitingSnapshot = false;
	std::string m_LastPresenceEvent = "reset";
	bool m_Dirty = true;
	int m_BrowserActiveUserCount = 0;
};

#endif
