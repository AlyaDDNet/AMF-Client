/*
 * Copyright (c) 2026 alya?
 * Original AMF Impact / Run Statistics code, developed for AMF Client.
 * Licensed under licenses/AMF-Impact-LICENSE.txt.
 */
#include "impact_episode_resolver.h"

#include <algorithm>
#include <utility>

namespace AmfImpact
{
	std::vector<SImpactEvent> CEpisodeResolver::Resolve(const STickEventBatch &Batch) const
	{
		std::vector<SImpactEvent> vResolved;
		vResolved.reserve(std::min(Batch.m_vCandidates.size(), MAX_LEDGER_EVENTS));
		for(const SImpactEvent &Candidate : Batch.m_vCandidates)
		{
			if(!Candidate.m_Confirmed || Candidate.m_EventId == 0 || vResolved.size() >= MAX_LEDGER_EVENTS)
				continue;

			SImpactEvent Event = Candidate;
			if(Event.m_StartTick < 0)
				Event.m_StartTick = Batch.m_ServerTick;
			if(Event.m_ResolveTick < 0)
				Event.m_ResolveTick = Batch.m_ServerTick;
			NormalizeEvent(Event);
			vResolved.push_back(std::move(Event));
		}

		// A server tick is a semantic batch. Canonical ordering makes replay and
		// live application independent of the order in which candidate detectors
		// happened to produce their results.
		std::sort(vResolved.begin(), vResolved.end(), [](const SImpactEvent &Left, const SImpactEvent &Right) {
			if(Left.m_ResolveTick != Right.m_ResolveTick)
				return Left.m_ResolveTick < Right.m_ResolveTick;
			if(Left.m_Type != Right.m_Type)
				return static_cast<int>(Left.m_Type) < static_cast<int>(Right.m_Type);
			if(ParticipantIdLess(Left.m_Actor, Right.m_Actor))
				return true;
			if(ParticipantIdLess(Right.m_Actor, Left.m_Actor))
				return false;
			if(ParticipantIdLess(Left.m_Target, Right.m_Target))
				return true;
			if(ParticipantIdLess(Right.m_Target, Left.m_Target))
				return false;
			if(Left.m_StartTick != Right.m_StartTick)
				return Left.m_StartTick < Right.m_StartTick;
			return Left.m_EventId < Right.m_EventId;
		});
		return vResolved;
	}
}
