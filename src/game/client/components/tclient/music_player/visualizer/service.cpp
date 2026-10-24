/* Copyright © 2026 BestProject Team */
#include "service.h"

#include "source.h"

#include <base/math.h>

#include <engine/shared/config.h>

#include <algorithm>
#include <cmath>

namespace BestClientVisualizer
{

CRealtimeMusicVisualizer::CRealtimeMusicVisualizer()
{
	RefreshConfig();
}

CRealtimeMusicVisualizer::~CRealtimeMusicVisualizer() = default;

bool CRealtimeMusicVisualizer::SetEnabled(bool Enabled)
{
	const bool WasEnabled = m_Source.IsEnabled();
	if(Enabled == WasEnabled)
		return WasEnabled;

	const bool IsEnabled = m_Source.SetEnabled(Enabled, []() -> std::unique_ptr<IVisualizerSource> {
#if defined(CONF_PLATFORM_LINUX) && defined(BC_MUSICPLAYER_HAS_PULSE) && BC_MUSICPLAYER_HAS_PULSE
		return CreatePulseVisualizerSource();
#elif defined(CONF_FAMILY_WINDOWS)
		return CreateWasapiVisualizerSource();
#else
		return CreatePassiveVisualizerSource();
#endif
	});
	RefreshConfig();
	return IsEnabled;
}

void CRealtimeMusicVisualizer::RefreshConfig()
{
	SVisualizerConfig Config;
	Config.m_SampleRate = std::max(8000, m_ConfigInitialized ? m_Config.m_SampleRate : 48000);
	Config.m_BandCount = MAX_VISUALIZER_BANDS;
	Config.m_LowCutHz = 50;
	Config.m_HighCutHz = 10000;
	Config.m_BassSplitHz = 100;
	Config.m_NoiseReduction = 0.0f;
	Config.m_Sensitivity = 1.0f;
	m_Config = Config;
	m_ConfigInitialized = true;
	if(IVisualizerSource *pSource = m_Source.Get())
		pSource->SetConfig(m_Config);
}

void CRealtimeMusicVisualizer::SetPlaybackHint(const SVisualizerPlaybackHint &Hint)
{
	RefreshConfig();
	if(IVisualizerSource *pSource = m_Source.Get())
		pSource->SetPlaybackHint(Hint);
}

bool CRealtimeMusicVisualizer::PollFrame(SVisualizerFrame &OutFrame)
{
	RefreshConfig();
	IVisualizerSource *pSource = m_Source.Get();
	if(!pSource)
	{
		OutFrame = SVisualizerFrame();
		return false;
	}
	return pSource->PollFrame(OutFrame);
}

} // namespace BestClientVisualizer
