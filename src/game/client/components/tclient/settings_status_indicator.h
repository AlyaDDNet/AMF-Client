#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_SETTINGS_STATUS_INDICATOR_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_SETTINGS_STATUS_INDICATOR_H

#include <engine/graphics.h>
#include <engine/client/enums.h>
#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/client/component.h>

#include <array>

// Rendering stays independent from the AMF Presence transport. The resolver
// only publishes validated per-slot state through this component's API.
class CSettingsStatusIndicator : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }

	void OnInit() override;
	void OnShutdown() override;
	void OnReset() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnRender() override;

	void SetPresenceSettingsOpen(int ClientId, bool Open);
	void ClearPresenceSettingsOpen(int ClientId);
	void ClearPresenceSettingsOpen();
	void SetPresenceFastPractice(int ClientId, bool Active);
	void ClearPresenceFastPractice(int ClientId);
	void ClearPresenceFastPractice();
	bool IsSettingsOpen(int ClientId) const;
	bool IsFastPracticeActive(int ClientId) const;

private:
	bool IsLocalSettingsOpen(int ClientId) const;

	IGraphics::CTextureHandle m_Texture;
	// Presence must distinguish a received `settingsOpen: false` from no
	// Presence state at all, so it can take precedence over a briefly stale
	// server-side PLAYERFLAG_IN_MENU snapshot.
	std::array<bool, MAX_CLIENTS> m_aPresenceSettingsKnown{};
	std::array<bool, MAX_CLIENTS> m_aPresenceSettingsOpen{};
	// Set only by the resolver after a connection was matched and confirmed as AMF.
	std::array<bool, MAX_CLIENTS> m_aPresenceFastPractice{};
	std::array<bool, MAX_CLIENTS> m_aDebugRendered{};
};

#endif
