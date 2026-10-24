#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_IMPACT_IMPACT_VIEW_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_IMPACT_IMPACT_VIEW_H

/*
 * Copyright (c) 2026 alya?
 * Original AMF Impact / Run Statistics code, developed for AMF Client.
 * Licensed under licenses/AMF-Impact-LICENSE.txt.
 */

#include "impact_presentation.h"
#include "impact_run.h"

namespace AmfImpact
{
	struct SImpactScoreDisplay
	{
		bool m_Available = false;
		bool m_HasImpactColor = false;
		EImpactDisplayColor m_ImpactColor = EImpactDisplayColor::WHITE;
		std::string m_Rating = "—";
		std::string m_Impact = "—";
		std::string m_Stability = "—";
	};

	struct SImpactRunPresentation
	{
		bool m_Finalized = false;
		std::vector<SFinalRankingEntry> m_vRanking;
		std::vector<size_t> m_vDisplayOrder;
		std::vector<int> m_vPlaces;
		bool m_HasMvp = false;
		size_t m_MvpParticipantIndex = 0;
	};

#if defined(CONF_DEBUG)
	inline constexpr uint64_t DEBUG_HISTORY_PREVIEW_RUN_ID = std::numeric_limits<uint64_t>::max();
	inline constexpr const char *DEBUG_HISTORY_PREVIEW_RUN_UID = "amf-debug-preview-square-of-gores-3";
	// Unix timestamp for 2026-06-26 04:30 in the user's Baghdad timezone (UTC+3).
	inline constexpr int64_t DEBUG_HISTORY_PREVIEW_SAVED_AT = 1782437400;

	inline bool IsDebugHistoryPreview(const SRunSnapshot &SnapshotData)
	{
		return SnapshotData.m_RunId == DEBUG_HISTORY_PREVIEW_RUN_ID && SnapshotData.m_RunUid == DEBUG_HISTORY_PREVIEW_RUN_UID;
	}

	// A presentation fixture only: frozen values are assigned directly and this
	// snapshot is never passed to scoring or persistence code.
	inline SRunSnapshot BuildDebugHistoryPreviewSnapshot()
	{
		SRunSnapshot SnapshotData;
		SnapshotData.m_RunId = DEBUG_HISTORY_PREVIEW_RUN_ID;
		SnapshotData.m_RunUid = DEBUG_HISTORY_PREVIEW_RUN_UID;
		SnapshotData.m_State = EAnalysisState::FINALIZED;
		SnapshotData.m_Result = ERunResult::FINISHED;
		SnapshotData.m_Context.m_Map = "Square_of_Gores_3";
		SnapshotData.m_Context.m_MapDifficulty = "Extreme";
		SnapshotData.m_Context.m_MapStars = 3;
		SnapshotData.m_Context.m_ServerName = "DEV PREVIEW (in-memory)";
		SnapshotData.m_Context.m_TickSpeed = 50;
		SnapshotData.m_ActiveAnalysisTicks = 4773LL * SnapshotData.m_Context.m_TickSpeed;
		SnapshotData.m_EndTick = SnapshotData.m_ActiveAnalysisTicks;
		SnapshotData.m_AuthoritativeFinishTimeMillis = 4773000;
		SnapshotData.m_SavedAt = DEBUG_HISTORY_PREVIEW_SAVED_AT;

		auto AddParticipant = [&SnapshotData](int ClientId, const char *pName, double Rating, double Impact, double Stability,
			int Saves, int Aleds, int Clutches, int Falls) {
			SParticipant Participant;
			Participant.m_Id.m_ClientId = ClientId;
			Participant.m_Id.m_SlotGeneration = 1;
			Participant.m_Name = pName;
			Participant.m_Cosmetics = {true, "default", false, 0, 0};
			Participant.m_Engaged = true;
			Participant.m_EngagedTick = 0;
			Participant.m_Finalized = true;
			Participant.m_TerminalStatus = EParticipantTerminalStatus::FINISHED;
			Participant.m_ParticipationTicks = SnapshotData.m_ActiveAnalysisTicks;
			Participant.m_ParticipationTicksKnown = true;
			Participant.m_RawStats.m_Saves = Saves;
			Participant.m_RawStats.m_Aleds = Aleds;
			Participant.m_RawStats.m_Clutches = Clutches;
			Participant.m_RawStats.m_Falls = Falls;
			Participant.m_Score.m_Frozen = true;
			Participant.m_Score.m_Available = true;
			Participant.m_Score.m_Rating = Rating;
			Participant.m_Score.m_Impact = Impact;
			Participant.m_Score.m_Stability = Stability;
			Participant.m_Score.m_RatingVersion = IMPACT_RATING_VERSION;
			Participant.m_Score.m_ScoringVersion = IMPACT_SCORING_VERSION;
			SnapshotData.m_vParticipants.push_back(std::move(Participant));
		};

		SnapshotData.m_vParticipants.reserve(3);
		AddParticipant(0, "alya?", 2.11, 11.24, 85.0, 143, 13, 51, 45);
		AddParticipant(1, "AshuraHX", 0.57, -2.32, 44.0, 52, 1, 10, 79);
		AddParticipant(2, "mordıser", 0.96, 1.34, 51.0, 57, 0, 11, 65);
		return SnapshotData;
	}
#endif

	inline SImpactScoreDisplay BuildImpactScoreDisplay(const SParticipant &Participant, bool FinalizedView)
	{
		SImpactScoreDisplay Display;
		if(!FinalizedView || !HasUsableFrozenFinalScore(Participant))
			return Display;

		Display.m_Available = true;
		Display.m_Rating = FormatTruncatedRating(Participant.m_Score.m_Rating);
		Display.m_Impact = FormatTruncatedImpact(Participant.m_Score.m_Impact);
		Display.m_Stability = FormatTruncatedStability(Participant.m_Score.m_Stability);
		Display.m_HasImpactColor = true;
		Display.m_ImpactColor = ClassifyImpactDisplayColor(Participant.m_Score.m_Impact);
		return Display;
	}

	// Build a rendering-only order from the existing canonical result helper.
	// The history/run snapshot and every frozen score remain read-only.
	inline SImpactRunPresentation BuildImpactRunPresentation(const SRunSnapshot &SnapshotData)
	{
		SImpactRunPresentation Presentation;
		if(SnapshotData.m_State != EAnalysisState::FINALIZED || !IsTerminalResult(SnapshotData.m_Result))
			return Presentation;

		Presentation.m_Finalized = true;
		Presentation.m_vPlaces.resize(SnapshotData.m_vParticipants.size());
		std::vector<bool> vRanked(SnapshotData.m_vParticipants.size(), false);
		Presentation.m_vRanking = BuildFinalRanking(SnapshotData, true);
		Presentation.m_vDisplayOrder.reserve(SnapshotData.m_vParticipants.size());
		for(const SFinalRankingEntry &Entry : Presentation.m_vRanking)
		{
			if(Entry.m_ParticipantIndex >= SnapshotData.m_vParticipants.size())
				continue;
			vRanked[Entry.m_ParticipantIndex] = true;
			Presentation.m_vPlaces[Entry.m_ParticipantIndex] = Entry.m_Rank;
			Presentation.m_vDisplayOrder.push_back(Entry.m_ParticipantIndex);
			if(Entry.m_IsMvp)
			{
				Presentation.m_HasMvp = true;
				Presentation.m_MvpParticipantIndex = Entry.m_ParticipantIndex;
			}
		}

		// Preserve all roster rows. Participants excluded by the canonical ranking
		// remain visible after rankable results and receive no fabricated place.
		for(size_t i = 0; i < SnapshotData.m_vParticipants.size(); ++i)
			if(!vRanked[i] && IsImpactStatisticsParticipant(SnapshotData.m_vParticipants[i]))
				Presentation.m_vDisplayOrder.push_back(i);

		return Presentation;
	}
}

#endif
