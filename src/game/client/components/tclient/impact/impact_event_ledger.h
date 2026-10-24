#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_IMPACT_IMPACT_EVENT_LEDGER_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_IMPACT_IMPACT_EVENT_LEDGER_H

/*
 * Copyright (c) 2026 alya?
 * Original AMF Impact / Run Statistics code, developed for AMF Client.
 * Licensed under licenses/AMF-Impact-LICENSE.txt.
 */

#include "impact_episode_resolver.h"

#include <unordered_set>

namespace AmfImpact
{
	class CEventLedger
	{
		CEpisodeResolver m_Resolver;
		std::vector<SImpactEvent> m_vEvents;
		std::unordered_set<uint64_t> m_EventIds;

	public:
		std::vector<SImpactEvent> Resolve(const STickEventBatch &Batch) const { return m_Resolver.Resolve(Batch); }
		bool Apply(const STickEventBatch &Batch, std::vector<SImpactEvent> *pAccepted);
		bool Restore(const std::vector<SImpactEvent> &vEvents);
		void Clear();

		bool Contains(uint64_t EventId) const { return m_EventIds.find(EventId) != m_EventIds.end(); }
		const std::vector<SImpactEvent> &Events() const { return m_vEvents; }
	};
}

#endif
