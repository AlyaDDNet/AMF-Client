#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_IMPACT_IMPACT_REPLAY_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_IMPACT_IMPACT_REPLAY_H

/*
 * Copyright (c) 2026 alya?
 * Original AMF Impact / Run Statistics code, developed for AMF Client.
 * Licensed under licenses/AMF-Impact-LICENSE.txt.
 */

#include "impact_run.h"
#include "impact_phase2.h"

namespace AmfImpact
{
	// Small deterministic harness used by tests and future recorded-state
	// playback. It has no dependency on Render(), live snapshots or UI.
	class CReplay
	{
		CImpactRun m_Run;
		CImpactPhase2 m_Phase2;
		SImpactPhase2Output m_LastOutput;
		uint64_t m_NextPhase2EventId = 1000000;

	public:
		CReplay(uint64_t RunId, const SRunContext &Context)
		{
			m_Run.Initialize(RunId, Context);
			m_Phase2.SetRunContext(RunId, Context.m_TeamId);
		}

		bool Submit(const STickEventBatch &Batch) { return m_Run.ApplyTickEventBatch(Batch); }
		bool SubmitGameplay(const std::vector<SImpactGameplayFrame> &vFrames, const std::vector<SImpactGameplayMessage> &vMessages = {})
		{
			m_LastOutput = {};
			std::vector<SImpactGameplayFrame> vCurrentFrames = vFrames;
			for(SImpactGameplayFrame &Frame : vCurrentFrames)
			{
				if(Frame.m_Eligible && Frame.m_Confirmed && Frame.m_Present && Frame.m_Participant.IsValid() &&
					Frame.m_TeamId == m_Run.Context().m_TeamId && !m_Run.HasParticipant(Frame.m_Participant))
					m_Run.AddRosterMember(Frame.m_Participant, "replay roster member", Frame.m_ServerTick, "explicit replay roster input");
				Frame.m_Eligible = Frame.m_Eligible && m_Run.HasParticipant(Frame.m_Participant);
				Frame.m_Engaged = m_Run.IsParticipantEngaged(Frame.m_Participant);
				Frame.m_Finalized = m_Run.IsParticipantFinalized(Frame.m_Participant);
			}
			bool ProcessingAllowed = CImpactRun::IsGameplayProcessingAllowed(m_Run.State(), m_Run.ActiveParticipantCount());
			bool OutputAlreadyProcessed = false;
			if(!ProcessingAllowed && CImpactPhase2::HasPotentialEngagementEvidence(vCurrentFrames, m_Run.Context().m_TeamId, vMessages))
			{
				CImpactPhase2 Phase2Probe = m_Phase2;
				SImpactPhase2Output ProbeOutput = Phase2Probe.Update(m_Run.RunId(), m_Run.Context().m_TeamId, m_Run.Context().m_TickSpeed, vCurrentFrames, vMessages);
				for(const SImpactPhase2Output::SEngagementProof &Proof : ProbeOutput.m_vEngagementProofs)
					m_Run.ProveParticipantEngaged(Proof.m_Participant, Proof.m_Tick, Proof.m_Reason.c_str());
				ProcessingAllowed = CImpactRun::IsGameplayProcessingAllowed(m_Run.State(), m_Run.ActiveParticipantCount());
				if(ProcessingAllowed)
				{
					m_Phase2 = std::move(Phase2Probe);
					m_LastOutput = std::move(ProbeOutput);
					OutputAlreadyProcessed = true;
				}
			}
			if(!ProcessingAllowed)
			{
				std::vector<SImpactGameplayMessage> vBoundaryMessages;
				for(const SImpactGameplayMessage &Message : vMessages)
					if(Message.m_Confirmed && (Message.m_Type == EImpactGameplayMessageType::TEAM_WIPE || Message.m_Type == EImpactGameplayMessageType::TEAM_RESET) &&
						(Message.m_TeamId == 0 || Message.m_TeamId == m_Run.Context().m_TeamId))
						vBoundaryMessages.push_back(Message);
				if(vBoundaryMessages.empty())
					return false;
				const std::vector<SImpactGameplayFrame> vNoGameplayFrames;
				m_LastOutput = m_Phase2.Update(m_Run.RunId(), m_Run.Context().m_TeamId, m_Run.Context().m_TickSpeed, vNoGameplayFrames, vBoundaryMessages);
			}
			else if(!OutputAlreadyProcessed)
				m_LastOutput = m_Phase2.Update(m_Run.RunId(), m_Run.Context().m_TeamId, m_Run.Context().m_TickSpeed, vCurrentFrames, vMessages);
			for(const SImpactPhase2Output::SEngagementProof &Proof : m_LastOutput.m_vEngagementProofs)
				m_Run.ProveParticipantEngaged(Proof.m_Participant, Proof.m_Tick, Proof.m_Reason.c_str());
			m_Run.SetPendingEpisodes(m_LastOutput.m_vPendingEpisodes);
			if(m_LastOutput.m_vEvents.empty())
			{
				m_Run.AppendForensicEvents(m_LastOutput.m_vForensic,
					m_LastOutput.m_ForensicTraceTruncated, m_LastOutput.m_ForensicDroppedEvents);
				return false;
			}
			STickEventBatch Batch;
			Batch.m_ServerTick = !vCurrentFrames.empty() ? vCurrentFrames.front().m_ServerTick : 0;
			Batch.m_vCandidates = m_LastOutput.m_vEvents;
			for(SImpactEvent &Event : Batch.m_vCandidates)
				if(Event.m_EventId == 0)
					Event.m_EventId = m_NextPhase2EventId++;
			for(SImpactPhase2Debug &Debug : m_LastOutput.m_vDebug)
			{
				if(Debug.m_Stage != EImpactPhase2DebugStage::CONFIRMED || Debug.m_EventId != 0)
					continue;
				for(const SImpactEvent &Event : Batch.m_vCandidates)
					if(Event.m_Type == Debug.m_EventType && Event.m_Target == Debug.m_Target && Event.m_StartTick == Debug.m_StartTick)
					{
						Debug.m_EventId = Event.m_EventId;
						break;
					}
			}
			for(SImpactForensicEvent &Forensic : m_LastOutput.m_vForensic)
			{
				if(Forensic.m_EventId != 0)
					continue;
				for(const SImpactPhase2Debug &Debug : m_LastOutput.m_vDebug)
				{
					if(Debug.m_EventId == 0 || Debug.m_Target != Forensic.m_Target)
						continue;
					const bool FallMatch = Forensic.m_Type == EImpactForensicType::FALL_CONFIRMED &&
						Debug.m_DebugLabel == "FALL_CONFIRM" && Forensic.m_FallEpisodeId == Debug.m_EpisodeId;
					const bool SaveMatch = (Forensic.m_Type == EImpactForensicType::SAVE_CONFIRMED ||
						Forensic.m_Type == EImpactForensicType::CLUTCH_CONFIRMED) &&
						Debug.m_DebugLabel == "SAVE_CONFIRMED" && Forensic.m_SaveCandidateId == Debug.m_EpisodeId &&
						Forensic.m_Actor == Debug.m_PotentialSaver;
					if(FallMatch || SaveMatch)
					{
						Forensic.m_EventId = Debug.m_EventId;
						break;
					}
				}
			}
			m_Run.AppendForensicEvents(m_LastOutput.m_vForensic,
				m_LastOutput.m_ForensicTraceTruncated, m_LastOutput.m_ForensicDroppedEvents);
			return m_Run.ApplyTickEventBatch(Batch);
		}
		bool SubmitGameplayInputs(const std::vector<SImpactGameplayInput> &vInputs, const std::vector<SImpactGameplayMessage> &vMessages = {})
		{
			std::vector<SImpactGameplayFrame> vFrames;
			vFrames.reserve(vInputs.size());
			for(const SImpactGameplayInput &Input : vInputs)
				vFrames.push_back(NormalizeGameplayFrame(Input));
			return SubmitGameplay(vFrames, vMessages);
		}
		CImpactPhase2 &Phase2() { return m_Phase2; }
		const CImpactPhase2 &Phase2() const { return m_Phase2; }
		void SetPendingEpisodes(const std::vector<SImpactPendingEpisode> &vEpisodes)
		{
			m_Phase2.SetPendingEpisodes(vEpisodes);
			m_Run.SetPendingEpisodes(vEpisodes);
		}
		CImpactRun &Run() { return m_Run; }
		const CImpactRun &Run() const { return m_Run; }
		const SImpactPhase2Output &LastOutput() const { return m_LastOutput; }
	};
}

#endif
