/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_VISUALIZER_SERVICE_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_VISUALIZER_SERVICE_H

#include "source.h"

#include <memory>

namespace BestClientVisualizer
{

// Owns the capture source on the client/update thread. Stopping releases the
// source synchronously; each source is responsible for joining its workers in
// its destructor before releasing its state.
class CVisualizerSourceLifecycle
{
	std::unique_ptr<IVisualizerSource> m_pSource;

public:
	template<typename TFactory>
	bool SetEnabled(bool Enabled, TFactory &&Factory)
	{
		if(!Enabled)
		{
			m_pSource.reset();
			return false;
		}
		if(!m_pSource)
			m_pSource = Factory();
		return m_pSource != nullptr;
	}

	bool IsEnabled() const { return m_pSource != nullptr; }
	IVisualizerSource *Get() const { return m_pSource.get(); }
};

class CRealtimeMusicVisualizer
{
	CVisualizerSourceLifecycle m_Source;
	SVisualizerConfig m_Config;
	bool m_ConfigInitialized = false;

	void RefreshConfig();

public:
	CRealtimeMusicVisualizer();
	~CRealtimeMusicVisualizer();

	bool SetEnabled(bool Enabled);
	bool IsEnabled() const { return m_Source.IsEnabled(); }
	void SetPlaybackHint(const SVisualizerPlaybackHint &Hint);
	bool PollFrame(SVisualizerFrame &OutFrame);
};

} // namespace BestClientVisualizer

#endif
