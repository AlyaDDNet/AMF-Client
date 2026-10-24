#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_IMPACT_IMPACT_EPISODE_RESOLVER_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_IMPACT_IMPACT_EPISODE_RESOLVER_H

/*
 * Copyright (c) 2026 alya?
 * Original AMF Impact / Run Statistics code, developed for AMF Client.
 * Licensed under licenses/AMF-Impact-LICENSE.txt.
 */

#include "impact_types.h"

namespace AmfImpact
{
	// Phase 1 deliberately resolves only confirmed, explicitly supplied
	// candidates. Gameplay detectors can be added later without coupling them
	// to the ledger, UI or renderer.
	class CEpisodeResolver
	{
	public:
		std::vector<SImpactEvent> Resolve(const STickEventBatch &Batch) const;
	};
}

#endif
