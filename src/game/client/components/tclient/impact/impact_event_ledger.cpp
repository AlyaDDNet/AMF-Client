/*
 * Copyright (c) 2026 alya?
 * Original AMF Impact / Run Statistics code, developed for AMF Client.
 * Licensed under licenses/AMF-Impact-LICENSE.txt.
 */
#include "impact_event_ledger.h"

#include <algorithm>

namespace AmfImpact
{
	bool CEventLedger::Apply(const STickEventBatch &Batch, std::vector<SImpactEvent> *pAccepted)
	{
		if(pAccepted == nullptr)
			return false;
		pAccepted->clear();
		const std::vector<SImpactEvent> vResolved = m_Resolver.Resolve(Batch);
		for(const SImpactEvent &Event : vResolved)
		{
			if(Contains(Event.m_EventId) || m_vEvents.size() >= MAX_LEDGER_EVENTS)
				continue;
			SImpactEvent Canonical = Event;
			Canonical.m_AppliedExactlyOnce = true;
			m_EventIds.insert(Canonical.m_EventId);
			m_vEvents.push_back(Canonical);
			pAccepted->push_back(std::move(Canonical));
		}
		return !pAccepted->empty();
	}

	bool CEventLedger::Restore(const std::vector<SImpactEvent> &vEvents)
	{
		Clear();
		if(vEvents.size() > MAX_LEDGER_EVENTS)
			return false;

		std::vector<SImpactEvent> vSorted = vEvents;
		std::sort(vSorted.begin(), vSorted.end(), [](const SImpactEvent &Left, const SImpactEvent &Right) {
			return Left.m_EventId < Right.m_EventId;
		});
		for(SImpactEvent Event : vSorted)
		{
			NormalizeEvent(Event);
			if(Event.m_EventId == 0 || Contains(Event.m_EventId))
				return false;
			Event.m_AppliedExactlyOnce = true;
			m_EventIds.insert(Event.m_EventId);
			m_vEvents.push_back(std::move(Event));
		}
		return true;
	}

	void CEventLedger::Clear()
	{
		m_vEvents.clear();
		m_EventIds.clear();
	}
}
