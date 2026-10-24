#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_IMPACT_IMPACT_IMPACT_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_IMPACT_IMPACT_IMPACT_H

/*
 * Copyright (c) 2026 alya?
 * Original AMF Impact / Run Statistics code, developed for AMF Client.
 * Licensed under licenses/AMF-Impact-LICENSE.txt.
 */

#include "impact_stability.h"

namespace AmfImpact
{
	struct SImpactV1Constants
	{
		static constexpr double m_IndependentSaveWeight = 1.00;
		static constexpr double m_RepeatedSaveWeight = 0.40;
		static constexpr double m_UnknownPatternSaveWeight = 1.00;
		static constexpr double m_FallWeight = 0.90;
		static constexpr double m_FirstClutchEvidence = 1.25;
		static constexpr double m_FirstAledEvidence = 0.30;
		static constexpr double m_NaturalLogOfTwo = 0.693147180559945309417232121458176568;
		static constexpr double m_ClutchEvidenceScale = m_FirstClutchEvidence / m_NaturalLogOfTwo;
		static constexpr double m_AledEvidenceScale = m_FirstAledEvidence / m_NaturalLogOfTwo;

		static constexpr double m_PositiveImpactScale = 4.40;
		static constexpr double m_PositiveImpactSoftness = 4.00;
		static constexpr double m_NegativeHarmScale = 6.00;
		static constexpr double m_NegativeHarmSoftness = 4.00;

		static constexpr double m_ParticipationFullConfidenceSeconds = 300.0;
		static constexpr double m_ShortRunConfidenceFloor = 0.75;
		static constexpr double m_CompleteEvidenceConfidence = 1.0;
		static constexpr double m_IncompleteEvidenceConfidence = 0.80;
		static constexpr double m_UnknownEvidenceConfidence = 0.75;
	};

	struct SImpactResult
	{
		bool m_Available = false;
		SParticipantId m_Id;
		EPhase4EvidenceStatus m_EvidenceStatus = EPhase4EvidenceStatus::UNAVAILABLE;
		uint64_t m_EvidenceDroppedEvents = 0;

		int64_t m_RawCanonicalSaves = 0;
		int64_t m_IndependentSaves = 0;
		int64_t m_RepeatedSaves = 0;
		int64_t m_UnknownPatternSaves = 0;
		bool m_SavePatternEvidenceCoverageComplete = false;
		bool m_SavePatternClassificationComplete = false;
		double m_EffectiveSaveEvidence = 0.0;

		int64_t m_Clutches = 0;
		int64_t m_Aleds = 0;
		size_t m_FinalizedEngagedScoringCohortSize = 0;
		double m_ClutchEvidence = 0.0;
		double m_AledEvidence = 0.0;
		double m_PositiveEvidence = 0.0;

		int64_t m_Falls = 0;
		int64_t m_RescuesReceived = 0;
		bool m_RescuesReceivedKnown = false;
		double m_NegativeEvidence = 0.0;
		double m_NetEvidence = 0.0;
		double m_HarmSeverity = 0.0;

		bool m_ParticipationKnown = false;
		int64_t m_ParticipationTicks = 0;
		double m_ParticipationSeconds = 0.0;
		double m_RawImpact = 0.0;
		double m_ParticipationConfidence = SImpactV1Constants::m_ShortRunConfidenceFloor;
		double m_EvidenceConfidence = SImpactV1Constants::m_UnknownEvidenceConfidence;
		double m_CombinedConfidence = 0.0;
		double m_FinalImpact = 0.0;

		EPhase4FeatureSupport m_ProvenRescueAssistSupport = EPhase4FeatureSupport::UNSUPPORTED;
		bool m_ProvenRescueAssistsKnown = false;
		bool m_StabilityDiagnosticKnown = false;
		double m_StabilityDiagnosticValue = 0.0;
	};

	// Positive evidence uses its own logarithmic branch. Negative evidence is
	// first reduced to balance-aware HarmSeverity; invalid input returns NaN.
	double TransformImpactEvidenceV1(double PositiveEvidence, double NegativeEvidence, double *pHarmSeverity = nullptr);

	// Calibration-stage only. Optional Stability is copied into diagnostics and
	// never participates in Impact evidence, confidence, or final value. Future
	// Rating must consume the resulting dimensions and must not add raw Clutch
	// counts as a separate bonus, which would bypass this scorer's duo rule.
	bool CalculateImpactV1(const SPlayerScoringFeatures &Player, const CRunScoringContext &Context,
		SImpactResult *pResult, const SStabilityResult *pOptionalStabilityDiagnostic = nullptr);
	std::string FormatImpactV1Result(const SImpactResult &Result);
}

#endif
