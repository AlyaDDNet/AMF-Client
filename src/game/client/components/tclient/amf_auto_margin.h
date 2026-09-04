#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_AMF_AUTO_MARGIN_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_AMF_AUTO_MARGIN_H

#include <game/client/component.h>

// Keeps the normal DDNet prediction margin config in sync with the measured
// server latency when the optional AMF automatic mode is enabled.
class CAmfAutoMargin : public CComponent
{
	float m_CheckTimer = 0.0f;
	float m_LatestPing = -1.0f;
	float m_IntervalPingSum = 0.0f;
	int m_IntervalPingSamples = 0;
	float m_IntervalMinPing = -1.0f;
	float m_IntervalPeakPing = -1.0f;
	float m_SmoothedPing = -1.0f;
	float m_SmoothedJitter = 0.0f;
	bool m_HighPing = false;
	bool m_WasEnabled = false;
	int m_SavedMargin = -1;
	bool m_ManualMarginInitialized = false;
	int m_LastManualMargin = -1;

	void ResetState();

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnNewSnapshot() override;
	void OnUpdate() override;
};

#endif
