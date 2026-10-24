#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_IMPACT_IMPACT_SCORING_FEATURES_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_IMPACT_IMPACT_SCORING_FEATURES_H

/*
 * Copyright (c) 2026 alya?
 * Original AMF Impact / Run Statistics code, developed for AMF Client.
 * Licensed under licenses/AMF-Impact-LICENSE.txt.
 */

#include "impact_types.h"

namespace AmfImpact
{
	enum class EPhase4EvidenceStatus
	{
		UNAVAILABLE,
		COMPLETE,
		INCOMPLETE,
		UNSUPPORTED_VERSION,
		INVALID,
	};

	enum class EPhase4FeatureSupport
	{
		SUPPORTED,
		UNSUPPORTED,
	};

	struct SPhase4ScoringAggregationOptions
	{
		// Legacy diagnostic/config field retained for compatibility. The classifier
		// deliberately ignores elapsed time; time alone is never anti-farm evidence.
		uint64_t m_MaxRepeatedSaveSeparationTicks = 0;
	};

	struct SExactCountRatio
	{
		int64_t m_Numerator = 0;
		int64_t m_Denominator = 0;
		bool m_Known = false;
	};

	struct SPlayerScoringFeatures
	{
		SParticipantId m_Id;
		std::string m_Name;
		EParticipantTerminalStatus m_TerminalStatus = EParticipantTerminalStatus::UNKNOWN;
		bool m_Engaged = false;
		bool m_Finalized = false;
		bool m_InFinalizedScoringCohort = false;

		int64_t m_ParticipationTicks = 0;
		bool m_ParticipationKnown = false;

		// These are the already accepted canonical participant counters. Aled and
		// Clutch remain side-counters and are never added to Saves.
		int64_t m_Saves = 0;
		int64_t m_Falls = 0;
		int64_t m_Aleds = 0;
		int64_t m_Clutches = 0;

		int64_t m_RescuesReceived = 0;
		bool m_RescuesReceivedKnown = false;

		int64_t m_IndependentSaveCount = 0;
		int64_t m_RepeatedContextSaveCount = 0;
		int64_t m_UnknownSavePatternCount = 0;
		bool m_SavePatternEvidenceCoverageComplete = false;
		bool m_SavePatternClassificationComplete = false;

		// Current durable records cannot strictly distinguish a useful assist from
		// a merely target-specific contributor. Keep unavailable distinct from 0.
		EPhase4FeatureSupport m_ProvenRescueAssistSupport = EPhase4FeatureSupport::UNSUPPORTED;
		int64_t m_ProvenRescueAssists = 0;
		bool m_ProvenRescueAssistsKnown = false;

		SExactCountRatio m_SavesToTeamSaves;
		SExactCountRatio m_FallsToTeamFalls;
	};

	struct CRunScoringContext
	{
		bool m_Available = false;
		uint64_t m_RunId = 0;
		std::string m_RunUid;
		bool m_HasGlobalRunNumber = false;
		uint64_t m_GlobalRunNumber = 0;
		int m_TeamId = 0;
		std::string m_ServerKey;
		std::string m_Map;
		ERunResult m_Result = ERunResult::NONE;
		int64_t m_EndTick = -1;
		int m_TickSpeed = 0;
		uint64_t m_MaxRepeatedSaveSeparationTicks = 0;

		size_t m_RosterParticipantCount = 0;
		size_t m_NotEngagedRosterParticipantCount = 0;
		size_t m_FinalizedEngagedParticipantCount = 0;
		int64_t m_TotalSaves = 0;
		int64_t m_TotalFalls = 0;
		int64_t m_TotalAleds = 0;
		int64_t m_TotalClutches = 0;
		int64_t m_IndependentSaveCount = 0;
		int64_t m_RepeatedContextSaveCount = 0;
		int64_t m_UnknownSavePatternCount = 0;
		bool m_SavePatternEvidenceCoverageComplete = false;
		bool m_SavePatternClassificationComplete = false;

		int64_t m_TotalKnownRescuesReceived = 0;
		size_t m_RescuesReceivedKnownParticipantCount = 0;
		size_t m_RescuesReceivedUnknownParticipantCount = 0;
		bool m_TotalRescuesReceivedKnown = false;

		int64_t m_TotalKnownParticipationTicks = 0;
		size_t m_ParticipationKnownParticipantCount = 0;
		size_t m_ParticipationUnknownParticipantCount = 0;
		bool m_ParticipationSampleComplete = false;

		EPhase4EvidenceStatus m_ScoringEvidenceStatus = EPhase4EvidenceStatus::UNAVAILABLE;
		int m_ScoringEvidenceVersion = 0;
		uint64_t m_ScoringEvidenceDroppedEvents = 0;
		EPhase4FeatureSupport m_ProvenRescueAssistSupport = EPhase4FeatureSupport::UNSUPPORTED;
		std::vector<SPlayerScoringFeatures> m_vPlayers;
	};

	// Pure aggregation. Active or malformed snapshots are rejected; the caller
	// supplies the descriptive anti-farm window explicitly, so no hidden threshold
	// or score formula is introduced here.
	bool BuildPhase4ScoringFeatures(const SRunSnapshot &Snapshot, const SPhase4ScoringAggregationOptions &Options, CRunScoringContext *pFeatures);
	std::string FormatPhase4ScoringFeatures(const CRunScoringContext &Features);

	const char *Phase4EvidenceStatusName(EPhase4EvidenceStatus Status);
	const char *Phase4FeatureSupportName(EPhase4FeatureSupport Support);
}

#endif
