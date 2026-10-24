#include "amf_auto_margin.h"

#include <engine/client.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>

namespace
{
constexpr int BASE_MARGIN = 9;
constexpr int MAX_MARGIN = 300;
constexpr float CHECK_INTERVAL_SECONDS = 0.5f;
constexpr float PING_SMOOTH_FACTOR_RISE = 0.45f;
constexpr float PING_SMOOTH_FACTOR_FALL = 0.20f;
constexpr float JITTER_SMOOTH_FACTOR_RISE = 0.35f;
constexpr float JITTER_SMOOTH_FACTOR_FALL = 0.15f;
constexpr int HIGH_PING_ENTER_MS = 50;
constexpr int HIGH_PING_EXIT_MS = 42;
constexpr float JITTER_ENTER_MS = 4.0f;
constexpr float JITTER_EXIT_MS = 2.0f;
constexpr int MARGIN_DEADZONE = 0; // Whole-millisecond targets do not need a fractional deadzone.
constexpr int MARGIN_STEP_DOWN = 1; // Preserve the gradual reduction in canonical integer-ms steps.

float MarginFromPing(float Ping)
{
	// Keep the runtime target in the same whole-millisecond units as DDNet.
	return std::max((float)BASE_MARGIN, Ping * 0.25f);
}
}

void CAmfAutoMargin::ResetState()
{
	m_RuntimeMargin = -1;
	m_CheckTimer = 0.0f;
	m_LatestPing = -1.0f;
	m_IntervalPingSum = 0.0f;
	m_IntervalPingSamples = 0;
	m_IntervalMinPing = -1.0f;
	m_IntervalPeakPing = -1.0f;
	m_SmoothedPing = -1.0f;
	m_SmoothedJitter = 0.0f;
	m_HighPing = false;
}

void CAmfAutoMargin::OnReset()
{
	ResetState();
	Client()->SetAutoPredictionMargin(-1);
}

void CAmfAutoMargin::OnStateChange(int NewState, int OldState)
{
	if(NewState != OldState)
	{
		ResetState();
		Client()->SetAutoPredictionMargin(-1);
	}
}

void CAmfAutoMargin::OnNewSnapshot()
{
	if(Client()->State() != IClient::STATE_ONLINE || !GameClient()->m_Snap.m_pLocalInfo)
	{
		ResetState();
		Client()->SetAutoPredictionMargin(-1);
		return;
	}

	const float CurrentPing = (float)std::max(0, GameClient()->m_Snap.m_pLocalInfo->m_Latency);
	m_LatestPing = CurrentPing;
	m_IntervalPingSum += CurrentPing;
	++m_IntervalPingSamples;
	if(m_IntervalMinPing < 0.0f || CurrentPing < m_IntervalMinPing)
		m_IntervalMinPing = CurrentPing;
	if(m_IntervalPeakPing < 0.0f || CurrentPing > m_IntervalPeakPing)
		m_IntervalPeakPing = CurrentPing;
}

void CAmfAutoMargin::OnUpdate()
{
	const bool AutoMarginEnabled = g_Config.m_AmfAutoMargin != 0;
	if(!m_ManualMarginInitialized)
	{
		const int CurrentMargin = std::clamp(g_Config.m_ClPredictionMargin, 1, MAX_MARGIN);
		// cl_prediction_margin is the canonical persisted manual value when
		// Auto Margin is disabled. Never replace it from the backup field during
		// startup: stale backup data used to turn 100 (10.0 ms) into 1000 (100 ms).
		// When Auto Margin is enabled, the backup is the manual value to restore;
		// initialize it only when it has not been persisted yet.
		if(g_Config.m_AmfAutoMarginManualValue <= 0 || !AutoMarginEnabled)
			g_Config.m_AmfAutoMarginManualValue = CurrentMargin;

		m_LastManualMargin = CurrentMargin;
		m_ManualMarginInitialized = true;
	}

	if(!AutoMarginEnabled)
	{
		m_WasEnabled = false;
		m_RuntimeMargin = -1;
		Client()->SetAutoPredictionMargin(-1);

		const int CurrentManualMargin = std::clamp(g_Config.m_ClPredictionMargin, 1, MAX_MARGIN);
		if(CurrentManualMargin != m_LastManualMargin)
		{
			g_Config.m_AmfAutoMarginManualValue = CurrentManualMargin;
			m_LastManualMargin = CurrentManualMargin;
		}
		ResetState();
		return;
	}

	if(!m_WasEnabled)
	{
		const int CurrentConfiguredMargin = std::clamp(g_Config.m_ClPredictionMargin, 1, MAX_MARGIN);
		// A manual slider change can happen in the same frame as enabling Auto
		// Margin, before the next OnUpdate has mirrored it to the backup field.
		// Treat that changed canonical value as the new manual margin.
		if(CurrentConfiguredMargin != m_LastManualMargin)
			g_Config.m_AmfAutoMarginManualValue = CurrentConfiguredMargin;
		m_WasEnabled = true;
	}
	if(g_Config.m_ClPredictionMargin != m_LastManualMargin)
	{
		m_LastManualMargin = std::clamp(g_Config.m_ClPredictionMargin, 1, MAX_MARGIN);
		g_Config.m_AmfAutoMarginManualValue = m_LastManualMargin;
	}

	if(Client()->State() != IClient::STATE_ONLINE || !GameClient()->m_Snap.m_pLocalInfo)
	{
		ResetState();
		Client()->SetAutoPredictionMargin(-1);
		return;
	}

	m_CheckTimer -= Client()->RenderFrameTime();
	if(m_CheckTimer > 0.0f)
		return;
	m_CheckTimer += CHECK_INTERVAL_SECONDS;
	if(m_CheckTimer < 0.0f)
		m_CheckTimer = 0.0f;

	const float CurrentPing = m_LatestPing >= 0.0f ? m_LatestPing : (float)std::max(0, GameClient()->m_Snap.m_pLocalInfo->m_Latency);
	const float IntervalAveragePing = m_IntervalPingSamples > 0 ? m_IntervalPingSum / (float)m_IntervalPingSamples : CurrentPing;
	const float IntervalMinPing = m_IntervalMinPing >= 0.0f ? m_IntervalMinPing : CurrentPing;
	const float SamplePing = m_IntervalPeakPing >= 0.0f ? m_IntervalPeakPing : CurrentPing;
	m_IntervalPingSum = 0.0f;
	m_IntervalPingSamples = 0;
	m_IntervalMinPing = -1.0f;
	m_IntervalPeakPing = -1.0f;
	const float IntervalJitter = std::max(0.0f, SamplePing - IntervalMinPing);

	if(m_SmoothedPing < 0.0f)
		m_SmoothedPing = IntervalAveragePing;
	else
	{
		const float SmoothFactor = IntervalAveragePing > m_SmoothedPing ? PING_SMOOTH_FACTOR_RISE : PING_SMOOTH_FACTOR_FALL;
		m_SmoothedPing += (IntervalAveragePing - m_SmoothedPing) * SmoothFactor;
	}

	const float JitterSmoothFactor = IntervalJitter > m_SmoothedJitter ? JITTER_SMOOTH_FACTOR_RISE : JITTER_SMOOTH_FACTOR_FALL;
	m_SmoothedJitter += (IntervalJitter - m_SmoothedJitter) * JitterSmoothFactor;

	const float EffectivePing = std::max(std::max(m_SmoothedPing, CurrentPing), IntervalAveragePing + m_SmoothedJitter * 0.25f);
	const bool WantsDynamicMargin = EffectivePing >= (float)HIGH_PING_ENTER_MS || SamplePing >= (float)HIGH_PING_ENTER_MS || m_SmoothedJitter >= JITTER_ENTER_MS;

	if(m_HighPing)
	{
		if(EffectivePing < (float)HIGH_PING_EXIT_MS && m_SmoothedJitter <= JITTER_EXIT_MS)
			m_HighPing = false;
	}
	else if(WantsDynamicMargin)
	{
		m_HighPing = true;
	}

	int TargetMargin = BASE_MARGIN;
	if(m_HighPing)
	{
		const float StableMargin = MarginFromPing(EffectivePing);
		const float JitterMargin = std::max(m_SmoothedJitter * 0.9f, IntervalJitter * 0.35f);
		TargetMargin = std::clamp((int)std::round(StableMargin + JitterMargin), BASE_MARGIN, MAX_MARGIN);
	}

	int CurrentMargin = m_RuntimeMargin >= 0 ? m_RuntimeMargin : BASE_MARGIN;
	const int MarginDelta = TargetMargin - CurrentMargin;
	if(std::abs(MarginDelta) <= MARGIN_DEADZONE)
	{
		m_RuntimeMargin = CurrentMargin;
		Client()->SetAutoPredictionMargin(CurrentMargin);
		return;
	}

	if(MarginDelta > 0)
		CurrentMargin = TargetMargin;
	else
		CurrentMargin -= std::min(-MarginDelta, MARGIN_STEP_DOWN);

	CurrentMargin = std::clamp(CurrentMargin, BASE_MARGIN, MAX_MARGIN);
	m_RuntimeMargin = CurrentMargin;
	Client()->SetAutoPredictionMargin(CurrentMargin);
}
