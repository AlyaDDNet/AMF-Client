#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_IMPACT_IMPACT_TYPES_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_IMPACT_IMPACT_TYPES_H

/*
 * Copyright (c) 2026 alya?
 * Original AMF Impact / Run Statistics code, developed for AMF Client.
 * Licensed under licenses/AMF-Impact-LICENSE.txt.
 */

#include <cstddef>
#include <cstdint>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace AmfImpact
{
	constexpr int IMPACT_SCHEMA_VERSION = 1;
	// Phase 2.6R changes recovery classification, fall linkage and hook-span
	// semantics. Keep persisted history readable, but reject active checkpoints
	// produced by older algorithms so stale pending episodes cannot be resumed.
	// Keep persisted history readable, but reject active checkpoints produced by
	// the older algorithm so stale pending episodes cannot be resumed under the
	// repaired rules.
	constexpr int IMPACT_ALGORITHM_VERSION = 7;
	constexpr int IMPACT_STATIC_ANALYSIS_VERSION = 0;
	constexpr int IMPACT_RATING_VERSION = 1;
	// Zero means that no approved Phase 4 scoring algorithm has produced a score.
	constexpr int IMPACT_SCORING_VERSION_UNSET = 0;
	// Version of the combined finalized Stability/Impact/Rating snapshot format.
	// This is intentionally independent from the Phase 2 event algorithm version.
	constexpr int IMPACT_SCORING_VERSION = 1;
	constexpr int IMPACT_SCORING_EVIDENCE_VERSION = 1;
	constexpr int IMPACT_FORENSIC_VERSION = 2;
	constexpr int IMPACT_FORENSIC_LEGACY_VERSION = 1;
	constexpr int SHORT_RUN_SECONDS = 5 * 60;
	// Unfinished runs active for at least five minutes are committed to History
	// automatically. SHORT_RUN_SECONDS is the separate long-run display gate.
	constexpr int MIN_AUTO_SAVE_SECONDS = 5 * 60;
	constexpr int SHORT_RUN_DECISION_SECONDS = 60;
	constexpr size_t MAX_PARTICIPANTS = 16;
	constexpr size_t MAX_LEDGER_EVENTS = 4096;
	constexpr size_t MAX_SCORING_EVIDENCE_EVENTS = MAX_LEDGER_EVENTS;
	constexpr size_t MAX_EVENT_TAGS = 8;
	constexpr size_t MAX_EVENT_TAG_LENGTH = 48;
	constexpr size_t MAX_EVENT_REASON_LENGTH = 128;
	constexpr size_t MAX_CONTEXT_LENGTH = 256;
	constexpr size_t MAX_PENDING_EPISODES = 32;
	// Forensic records are emitted only on observed state/decision transitions.
	// This cap bounds history growth; reaching it is always explicitly persisted.
	constexpr size_t MAX_FORENSIC_EVENTS = 4096;
	constexpr size_t MAX_FORENSIC_DETAIL_LENGTH = 256;
	constexpr size_t MAX_EVENT_HELPERS = 8;
	constexpr size_t MAX_PHASE2_DEBUG_RECORDS = 64;
	// Storage bound only; this is not a gameplay duration or evidence threshold.
	constexpr size_t MAX_HOOK_SPANS = 64;
	// This is a presentation limit only. Persisted history deliberately has no
	// automatic retention cap; users can remove individual runs explicitly.
	constexpr size_t MAX_RECENT_HISTORY_RUNS = 30;

	constexpr size_t RecentHistoryStartIndex(size_t HistorySize)
	{
		return HistorySize > MAX_RECENT_HISTORY_RUNS ? HistorySize - MAX_RECENT_HISTORY_RUNS : 0;
	}

	constexpr size_t HistoryViewStartIndex(size_t HistorySize, bool FullHistory)
	{
		return FullHistory ? 0 : RecentHistoryStartIndex(HistorySize);
	}

	enum class EAnalysisState
	{
		WAITING,
		ANALYZING,
		ACTIVE,
		PAUSED_NO_TEAM,
		FINALIZED,
	};

	inline const char *RunStatusLocalizationKey(EAnalysisState State, int TeamMemberCount, int PresentParticipantCount)
	{
		switch(State)
		{
		case EAnalysisState::WAITING:
			return TeamMemberCount < 2 ? "Waiting for another team member" : "Waiting for participants to start";
		case EAnalysisState::ANALYZING:
			return TeamMemberCount < 2 ? "Waiting for another team member" : "Analyzing...";
		case EAnalysisState::ACTIVE:
			return "Analyzing...";
		case EAnalysisState::PAUSED_NO_TEAM:
			if(TeamMemberCount == 1)
				return "Analysis paused — one participant remains in the team";
			if(TeamMemberCount >= 2 && PresentParticipantCount < 2)
				return "Waiting for the team to resume activity";
			return "Analysis paused";
		case EAnalysisState::FINALIZED:
			return "Finalized";
		}
		return "Analysis paused";
	}

	enum class ERunResult
	{
		NONE,
		FINISHED,
		WIPED,
		TEAM_RESET,
		ABORTED_MAP_CHANGE,
		ABORTED_SERVER_CHANGE,
		ABORTED_CONNECTION,
	};

	enum class EImpactEventType
	{
		START_CROSSING,
		PARTICIPANT_PRESENCE,
		SAVE_CONFIRMED,
		FALL_CONFIRMED,
		ALED_CONFIRMED,
		// Internal recovery-context classification retained for causal history;
		// it must not create a separate user-facing/raw counter.
		UPDOWN_CONFIRMED,
		MISSED_CATCH_CONFIRMED,
		TEAM_MISTAKE_CONFIRMED,
		TEAM_HARM_CONFIRMED,
		PREVENTED_PROGRESS_LOSS_CONFIRMED,
		RECOVERY_DIFFICULTY_INCREASE_CONFIRMED,
		JUMP_RESET_CONFIRMED,
		FINISH_CONFIRMED,
		WIPE_CONFIRMED,
		TEAM_RESET_CONFIRMED,
		MAP_CHANGE,
		SERVER_CHANGE,
		CONNECTION_LOST,
		PARTICIPANT_FINALIZED,
	};

	enum class EImpactForensicType
	{
		PROBLEM_EPISODE_START,
		PROBLEM_EPISODE_END,
		FALL_CANDIDATE_CREATED,
		FALL_CONFIRMED,
		FALL_REJECTED,
		SELF_SAFE_PROVEN,
		SELF_SAFE_INVALIDATED,
		INTERACTION_OBSERVED,
		ACTOR_CAUSAL_EVIDENCE_POSITIVE,
		ACTOR_CAUSAL_EVIDENCE_UNKNOWN,
		ACTOR_CAUSAL_EVIDENCE_REJECTED,
		RESTORATION_OBSERVED,
		SAVE_CANDIDATE_CREATED,
		SAVE_CANDIDATE_REJECTED,
		SAVE_CONFIRMATION_STARTED,
		SAVE_CONFIRMED,
		SAVE_CONFIRMATION_FAILED,
		REFREEZE_BEFORE_CONFIRMATION,
		CLUTCH_EPISODE_START,
		CLUTCH_CANDIDATE_CREATED,
		CLUTCH_CANDIDATE_REJECTED,
		CLUTCH_CONFIRMED,
		CLUTCH_EPISODE_END,
		PROCESSING_PAUSED,
		PROCESSING_RESUMED,
		PARTICIPANT_FINALIZED,
		TEAM_BOUNDARY,
		CANONICAL_EVENT,
		CLUTCH_ROSTER_EVALUATED,
		ROSTER_MEMBER_ADDED,
		ENGAGED_PROVEN,
		ENGAGED_FINALIZED,
	};

	enum class EImpactForensicReason
	{
		NONE,
		NO_RELIABLE_ACTOR,
		SELF_SAFE_ALREADY_PROVEN,
		NON_CAUSAL_INTERACTION,
		RESTORATION_NOT_OBSERVED,
		REFROZEN_BEFORE_CONFIRMATION,
		TARGET_GENERATION_CHANGED,
		ACTOR_GENERATION_CHANGED,
		PARTICIPANT_FINALIZED,
		TEAM_BOUNDARY,
		PROCESSING_PAUSED,
		QUORUM_LOST,
		STALE_EPISODE,
		UNKNOWN_EVIDENCE,
		EPISODE_ALREADY_CONSUMED,
		OTHER_FUNCTIONAL_PARTICIPANT,
		ROSTER_NOT_READY,
		EXISTING_GENERIC_REJECT,
	};

	enum class EImpactFunctionalClass
	{
		UNKNOWN,
		FUNCTIONAL,
		NON_FUNCTIONAL,
		IGNORED,
		EXCLUDED,
	};

	enum class EImpactEpisodeState
	{
		NONE,
		NORMAL,
		DANGER,
		RECOVERED_SELF,
		RECOVERED_BY_TEAM,
		CONFIRMED_FALL,
		CANCELLED,
		CONFIRMED_RECOVERY,
	};

	enum class ECausalConfidence
	{
		INSUFFICIENT,
		AMBIGUOUS,
		HIGH,
		CONFIRMED,
	};

	enum class EOwnershipStatus
	{
		NONE,
		ACTOR,
		AMBIGUOUS,
	};

	enum class EImpactUnfreezeCause
	{
		UNKNOWN,
		NATURAL,
		HAMMER,
	};

	enum class EImpactRecoveryNeed
	{
		UNKNOWN,
		SELF_SAFE,
		EXTERNAL_REQUIRED,
	};

	// Physical outcome is deliberately independent from harm origin and from
	// recovery ownership. UNKNOWN means that the currently observable state has
	// not yet established either a safe or a harmful physical result.
	enum class EImpactPhysicalOutcome
	{
		UNKNOWN,
		SAFE,
		HARMFUL,
	};

	// Jump availability is a server-confirmed gameplay state. UNKNOWN is kept
	// distinct from UNAVAILABLE: a missing DDNet character extension or missing
	// current character must never be interpreted as a spent jump.
	enum class EImpactJumpState
	{
		UNKNOWN,
		AVAILABLE,
		UNAVAILABLE,
	};

	// Harm attribution is deliberately separate from RecoveryNeed. A target can
	// require external recovery because of a teammate-caused or intentional
	// mechanic, and that must not become a victim Fall. UNKNOWN is the safe
	// default when the current protocol cannot prove who caused the harm.
	enum class EImpactHarmCause
	{
		UNKNOWN,
		SELF_CAUSED,
		TEAMMATE_CAUSED,
		INTENTIONAL,
	};

	struct SParticipantId
	{
		// This identifies a Tee inside the CImpactRun/SRunSnapshot that owns it.
		// The owning run's RunId is the outer identity scope. m_Dummy is local
		// slot metadata, not part of the server participant identity.
		int m_ClientId = -1;
		uint32_t m_SlotGeneration = 0;
		bool m_Dummy = false;

		bool IsValid() const
		{
			return m_ClientId >= 0 && m_SlotGeneration != 0;
		}

		friend bool operator==(const SParticipantId &Left, const SParticipantId &Right)
		{
			return Left.m_ClientId == Right.m_ClientId &&
				Left.m_SlotGeneration == Right.m_SlotGeneration;
		}

		friend bool operator!=(const SParticipantId &Left, const SParticipantId &Right)
		{
			return !(Left == Right);
		}
	};

	inline bool ParticipantIdLess(const SParticipantId &Left, const SParticipantId &Right)
	{
		if(Left.m_ClientId != Right.m_ClientId)
			return Left.m_ClientId < Right.m_ClientId;
		if(Left.m_SlotGeneration != Right.m_SlotGeneration)
			return Left.m_SlotGeneration < Right.m_SlotGeneration;
		return false;
	}

	struct SRunContext
	{
		std::string m_ServerKey;
		std::string m_ServerName;
		std::string m_CommunityId;
		std::string m_Map;
		// Captured from the authoritative DDNet SV_MAPINFO description when it
		// follows the canonical map-info format. An empty category or -1 stars
		// means that the server did not provide trustworthy metadata.
		std::string m_MapDifficulty;
		int m_MapStars = -1;
		int m_TickSpeed = 50;
		// Authoritative DDNet team scope. Team 0 is not a valid Impact run.
		// Older persisted snapshots may leave this at 0 and remain readable as
		// history, but they must not be resumed as active analysis.
		int m_TeamId = 0;
	};

	constexpr int MAX_MAP_DIFFICULTY_STARS = 5;

	struct SMapDifficultyMetadata
	{
		std::string m_Category;
		int m_Stars = -1;
	};

	inline int ParseMapDifficultyStars(const std::string &Stars)
	{
		static constexpr const char *pFilledStar = "★";
		static constexpr const char *pEmptyStar = "✰";
		size_t Offset = 0;
		int FilledStars = 0;
		for(int Index = 0; Index < MAX_MAP_DIFFICULTY_STARS; ++Index)
		{
			if(Stars.compare(Offset, std::strlen(pFilledStar), pFilledStar) == 0)
			{
				++FilledStars;
				Offset += std::strlen(pFilledStar);
			}
			else if(Stars.compare(Offset, std::strlen(pEmptyStar), pEmptyStar) == 0)
			{
				Offset += std::strlen(pEmptyStar);
			}
			else
			{
				return -1;
			}
		}
		return Offset == Stars.size() ? FilledStars : -1;
	}

	inline int ParseMapDifficultyRating(const std::string &Rating)
	{
		const int StarRating = ParseMapDifficultyStars(Rating);
		if(StarRating >= 0)
			return StarRating;
		if(Rating.size() != 1 || Rating[0] < '0' || Rating[0] > '5')
			return -1;
		return Rating[0] - '0';
	}

	inline bool MapMetadataAsciiEquals(const std::string &Value, const char *pExpected)
	{
		if(pExpected == nullptr || Value.size() != std::strlen(pExpected))
			return false;
		for(size_t Index = 0; Index < Value.size(); ++Index)
			if(std::tolower(static_cast<unsigned char>(Value[Index])) != std::tolower(static_cast<unsigned char>(pExpected[Index])))
				return false;
		return true;
	}

	inline std::string NormalizeKogMapMetadataKey(const char *pMapName)
	{
		if(pMapName == nullptr)
			return {};
		std::string Result(pMapName);
		for(char &Character : Result)
			Character = static_cast<char>(std::tolower(static_cast<unsigned char>(Character)));
		return Result;
	}

	inline bool ConsumeMapInfoLiteral(const std::string &Text, size_t *pOffset, const char *pLiteral)
	{
		if(pOffset == nullptr || pLiteral == nullptr || Text.compare(*pOffset, std::strlen(pLiteral), pLiteral) != 0)
			return false;
		*pOffset += std::strlen(pLiteral);
		return true;
	}

	inline bool ConsumeMapInfoUnsigned(const std::string &Text, size_t *pOffset, bool *pIsOne = nullptr, bool *pIsNonZero = nullptr)
	{
		if(pOffset == nullptr)
			return false;
		const size_t Begin = *pOffset;
		bool IsNonZero = false;
		while(*pOffset < Text.size() && Text[*pOffset] >= '0' && Text[*pOffset] <= '9')
		{
			IsNonZero |= Text[*pOffset] != '0';
			++*pOffset;
		}
		if(*pOffset == Begin || (*pOffset - Begin > 1 && Text[Begin] == '0'))
			return false;
		if(pIsOne != nullptr)
			*pIsOne = *pOffset - Begin == 1 && Text[Begin] == '1';
		if(pIsNonZero != nullptr)
			*pIsNonZero = IsNonZero;
		return true;
	}

	inline bool ConsumeMapInfoCountedWord(const std::string &Text, size_t *pOffset, const char *pSingular, const char *pPlural)
	{
		bool IsOne = false;
		if(!ConsumeMapInfoUnsigned(Text, pOffset, &IsOne) || !ConsumeMapInfoLiteral(Text, pOffset, " "))
			return false;
		return ConsumeMapInfoLiteral(Text, pOffset, IsOne ? pSingular : pPlural);
	}

	inline bool ConsumeMapInfoAgoPart(const std::string &Text, size_t *pOffset, int *pUnitIndex = nullptr)
	{
		bool IsOne = false;
		bool IsNonZero = false;
		if(!ConsumeMapInfoUnsigned(Text, pOffset, &IsOne, &IsNonZero) || !IsNonZero || !ConsumeMapInfoLiteral(Text, pOffset, " "))
			return false;
		static constexpr const char *apSingular[] = {"year", "month", "week", "day", "hour", "min", "sec"};
		static constexpr const char *apPlural[] = {"years", "months", "weeks", "days", "hours", "mins", "secs"};
		for(size_t Index = 0; Index < sizeof(apSingular) / sizeof(apSingular[0]); ++Index)
		{
			const size_t SavedOffset = *pOffset;
			if(ConsumeMapInfoLiteral(Text, pOffset, IsOne ? apSingular[Index] : apPlural[Index]))
			{
				if(pUnitIndex != nullptr)
					*pUnitIndex = static_cast<int>(Index);
				return true;
			}
			*pOffset = SavedOffset;
		}
		return false;
	}

	inline bool ConsumeMapInfoAgo(const std::string &Text, size_t *pOffset)
	{
		if(ConsumeMapInfoLiteral(Text, pOffset, "moments"))
			return true;
		int FirstUnit = -1;
		if(!ConsumeMapInfoAgoPart(Text, pOffset, &FirstUnit))
			return false;
		if(ConsumeMapInfoLiteral(Text, pOffset, " and "))
		{
			int SecondUnit = -1;
			return ConsumeMapInfoAgoPart(Text, pOffset, &SecondUnit) && SecondUnit == FirstUnit + 1;
		}
		return true;
	}

	inline bool ConsumeMapInfoTime(const std::string &Text, size_t *pOffset, bool Centiseconds)
	{
		if(pOffset == nullptr)
			return false;
		const size_t Begin = *pOffset;
		while(*pOffset < Text.size() && ((Text[*pOffset] >= '0' && Text[*pOffset] <= '9') || Text[*pOffset] == ':' || Text[*pOffset] == '.'))
			++*pOffset;
		if(*pOffset == Begin)
			return false;

		const std::string Token = Text.substr(Begin, *pOffset - Begin);
		const size_t Dot = Token.find('.');
		if(Centiseconds)
		{
			if(Dot == std::string::npos || Dot + 3 != Token.size() || Token[Dot + 1] < '0' || Token[Dot + 1] > '9' || Token[Dot + 2] < '0' || Token[Dot + 2] > '9')
				return false;
		}
		else if(Dot != std::string::npos)
			return false;

		const size_t TimeEnd = Dot == std::string::npos ? Token.size() : Dot;
		size_t ComponentBegin = 0;
		int ComponentCount = 0;
		size_t FirstComponentLength = 0;
		while(ComponentBegin < TimeEnd)
		{
			const size_t ComponentEnd = Token.find(':', ComponentBegin);
			const size_t End = ComponentEnd == std::string::npos || ComponentEnd > TimeEnd ? TimeEnd : ComponentEnd;
			const size_t ComponentLength = End - ComponentBegin;
			if(End == ComponentBegin || End > TimeEnd || (ComponentCount == 0 && ComponentLength < 2) || (ComponentCount > 0 && ComponentLength != 2))
				return false;
			for(size_t Index = ComponentBegin; Index < End; ++Index)
				if(Token[Index] < '0' || Token[Index] > '9')
					return false;
			if(ComponentCount == 0)
				FirstComponentLength = ComponentLength;
			else if((Token[ComponentBegin] - '0') * 10 + (Token[ComponentBegin + 1] - '0') > 59)
				return false;
			++ComponentCount;
			if(End == TimeEnd)
				break;
			ComponentBegin = End + 1;
		}
		if(ComponentCount < (Centiseconds ? 1 : 2) || ComponentCount > 3)
			return false;
		if(Centiseconds && ComponentCount == 1 && FirstComponentLength != 2)
			return false;
		if(ComponentCount == 2 && FirstComponentLength != 2)
			return false;
		return true;
	}

	// DDNet's standard map-info message is the only client-visible source that
	// carries both the map category and star rating. Keep the parser strict:
	// non-standard server descriptions remain unknown instead of becoming map
	// name or server-name heuristics.
	inline bool ParseMapInfoDifficulty(const char *pDescription, const char *pMapName, SMapDifficultyMetadata *pMetadata)
	{
		if(pMetadata == nullptr)
			return false;
		*pMetadata = {};
		if(pDescription == nullptr || pMapName == nullptr || pDescription[0] == '\0' || pMapName[0] == '\0')
			return false;

		const std::string Description(pDescription);
		const std::string Prefix = std::string("\"") + pMapName + "\" by ";
		if(Description.compare(0, Prefix.size(), Prefix) != 0)
			return false;

		const size_t FilledStarsMarker = Description.find(", ★", Prefix.size());
		const size_t EmptyStarsMarker = Description.find(", ✰", Prefix.size());
		size_t StarsMarker = FilledStarsMarker;
		if(StarsMarker == std::string::npos || (EmptyStarsMarker != std::string::npos && EmptyStarsMarker < StarsMarker))
			StarsMarker = EmptyStarsMarker;
		if(StarsMarker == std::string::npos)
			return false;

		const size_t OnMarker = Description.rfind(" on ", StarsMarker);
		if(OnMarker == std::string::npos || OnMarker < Prefix.size() || OnMarker + 4 >= StarsMarker)
			return false;
		const std::string Category = Description.substr(OnMarker + 4, StarsMarker - (OnMarker + 4));
		if(Category.empty() || Category.size() > MAX_CONTEXT_LENGTH)
			return false;

		const size_t StarsStart = StarsMarker + 2;
		const size_t StarsEnd = Description.find(", ", StarsStart);
		if(StarsEnd == std::string::npos || StarsEnd == StarsStart)
			return false;
		const int Stars = ParseMapDifficultyStars(Description.substr(StarsStart, StarsEnd - StarsStart));
		if(Stars < 0)
			return false;

		// Validate the complete canonical tail emitted by CScoreWorker::MapInfo.
		// This prevents an arbitrary description containing a comma and star
		// glyphs from being treated as map metadata.
		size_t TailOffset = StarsEnd + 2;
		if(!ConsumeMapInfoCountedWord(Description, &TailOffset, "point", "points"))
			return false;
		if(Description.compare(TailOffset, std::strlen(", released "), ", released ") == 0)
		{
			TailOffset += std::strlen(", released ");
			if(!ConsumeMapInfoAgo(Description, &TailOffset) || !ConsumeMapInfoLiteral(Description, &TailOffset, " ago"))
				return false;
		}
		if(!ConsumeMapInfoLiteral(Description, &TailOffset, ", ") ||
			!ConsumeMapInfoCountedWord(Description, &TailOffset, "finish", "finishes") ||
			!ConsumeMapInfoLiteral(Description, &TailOffset, " by ") ||
			!ConsumeMapInfoCountedWord(Description, &TailOffset, "tee", "tees"))
			return false;
		if(Description.compare(TailOffset, std::strlen(" in "), " in ") == 0)
		{
			TailOffset += std::strlen(" in ");
			if(!ConsumeMapInfoTime(Description, &TailOffset, false) || !ConsumeMapInfoLiteral(Description, &TailOffset, " median"))
				return false;
		}
		if(Description.compare(TailOffset, std::strlen(", your time: "), ", your time: ") == 0)
		{
			TailOffset += std::strlen(", your time: ");
			if(!ConsumeMapInfoTime(Description, &TailOffset, true))
				return false;
		}
		if(TailOffset != Description.size())
			return false;

		pMetadata->m_Category = Category;
		pMetadata->m_Stars = Stars;
		return true;
	}

	// KoG's server-side /mapinfo response uses the same leading map/mapper
	// structure as DDNet, but exposes a short canonical category code (for
	// example INS, SOL, and ESY) and a five-position rating. This parser is only
	// used for a response known to be a server/system map-info message; it does
	// not scan arbitrary chat text.
	inline bool ParseKogMapInfoDifficulty(const char *pDescription, const char *pMapName, SMapDifficultyMetadata *pMetadata)
	{
		if(pMetadata == nullptr)
			return false;
		*pMetadata = {};
		if(pDescription == nullptr || pMapName == nullptr || pDescription[0] == '\0' || pMapName[0] == '\0')
			return false;

		const std::string Description(pDescription);
		const std::string Prefix = std::string("\"") + pMapName + "\" by ";
		if(Description.compare(0, Prefix.size(), Prefix) != 0)
			return false;
		const size_t OnMarker = Description.find(" on ", Prefix.size());
		if(OnMarker == std::string::npos || OnMarker + 4 >= Description.size())
			return false;
		const size_t CategoryEnd = Description.find(", ", OnMarker + 4);
		if(CategoryEnd == std::string::npos || CategoryEnd <= OnMarker + 4)
			return false;

		const std::string CategoryCode = Description.substr(OnMarker + 4, CategoryEnd - (OnMarker + 4));
		std::string Category;
		if(MapMetadataAsciiEquals(CategoryCode, "ESY"))
			Category = "Easy";
		else if(MapMetadataAsciiEquals(CategoryCode, "MAIN") || MapMetadataAsciiEquals(CategoryCode, "MNS"))
			Category = "Main";
		else if(MapMetadataAsciiEquals(CategoryCode, "HRD"))
			Category = "Hard";
		else if(MapMetadataAsciiEquals(CategoryCode, "INS"))
			Category = "Insane";
		else if(MapMetadataAsciiEquals(CategoryCode, "EXT"))
			Category = "Extreme";
		else if(MapMetadataAsciiEquals(CategoryCode, "SOL"))
			Category = "Solo";
		else
			return false;

		const size_t RatingStart = CategoryEnd + 2;
		const size_t RatingEnd = Description.find(", ", RatingStart);
		if(RatingEnd == std::string::npos || RatingEnd <= RatingStart)
			return false;
		const int Rating = ParseMapDifficultyRating(Description.substr(RatingStart, RatingEnd - RatingStart));
		if(Rating < 0 || Rating > MAX_MAP_DIFFICULTY_STARS)
			return false;

		// The next field is the authoritative points field emitted by KoG. Requiring
		// it prevents a coincidental "on INS, 3" fragment from becoming metadata.
		size_t TailOffset = RatingEnd + 2;
		if(!ConsumeMapInfoCountedWord(Description, &TailOffset, "point", "points"))
			return false;
		if(TailOffset != Description.size() &&
			(Description.compare(TailOffset, 2, ", ") != 0 || TailOffset + 2 >= Description.size()))
			return false;

		pMetadata->m_Category = std::move(Category);
		pMetadata->m_Stars = Rating;
		return true;
	}

	// KoG map vote options expose a rating-only record in the canonical form
	//   <map> | <five rating glyphs> | <decimal points> ⚐ | <mm:ss> ◷
	// Older servers used the same four fields without the two decorative
	// suffixes. Both grammars are explicit and strict; arbitrary suffixes are
	// never accepted.
	// This parser is intentionally independent from the current-map/category
	// resolver. It only accepts an exact current-map field and a complete,
	// bounded four-field option; arbitrary vote text can never become metadata.
	inline bool ParseKogVoteMapRating(const char *pOption, const char *pMapName, int *pRating, const char **ppReason = nullptr)
	{
		if(pRating != nullptr)
			*pRating = -1;
		if(ppReason != nullptr)
			*ppReason = nullptr;
		const auto Fail = [ppReason](const char *pReason) {
			if(ppReason != nullptr)
				*ppReason = pReason;
			return false;
		};
		if(pOption == nullptr || pMapName == nullptr || pRating == nullptr || pOption[0] == '\0' || pMapName[0] == '\0')
			return Fail("INVALID_INPUT");

		const std::string Option(pOption);
		const auto Trim = [](const std::string &Value) {
			size_t Begin = 0;
			while(Begin < Value.size() && std::isspace(static_cast<unsigned char>(Value[Begin])))
				++Begin;
			size_t End = Value.size();
			while(End > Begin && std::isspace(static_cast<unsigned char>(Value[End - 1])))
				--End;
			return Value.substr(Begin, End - Begin);
		};

		const size_t Separator0 = Option.find('|');
		const size_t Separator1 = Separator0 == std::string::npos ? std::string::npos : Option.find('|', Separator0 + 1);
		const size_t Separator2 = Separator1 == std::string::npos ? std::string::npos : Option.find('|', Separator1 + 1);
		if(Separator0 == std::string::npos || Separator1 == std::string::npos || Separator2 == std::string::npos || Option.find('|', Separator2 + 1) != std::string::npos)
			return Fail("MALFORMED_OPTION_FIELDS");

		const std::string MapField = Trim(Option.substr(0, Separator0));
		if(MapField != pMapName)
			return Fail("MAP_NAME_MISMATCH");
		const std::string RatingField = Trim(Option.substr(Separator0 + 1, Separator1 - Separator0 - 1));
		const std::string PointsField = Trim(Option.substr(Separator1 + 1, Separator2 - Separator1 - 1));
		const std::string TimeField = Trim(Option.substr(Separator2 + 1));

		// KoG has been observed using both the hollow white star (U+2606) and
		// the hollow star (U+2730); DDNet's map-info uses the latter. Both are
		// explicit empty positions, never arbitrary Unicode text.
		static constexpr const char *pFilledStar = "★";
		static constexpr const char *pEmptyStar = "☆";
		static constexpr const char *pAlternateEmptyStar = "✰";
		size_t RatingOffset = 0;
		int FilledStars = 0;
		for(int Index = 0; Index < MAX_MAP_DIFFICULTY_STARS; ++Index)
		{
			if(RatingField.compare(RatingOffset, std::strlen(pFilledStar), pFilledStar) == 0)
			{
				++FilledStars;
				RatingOffset += std::strlen(pFilledStar);
			}
			else if(RatingField.compare(RatingOffset, std::strlen(pEmptyStar), pEmptyStar) == 0)
				RatingOffset += std::strlen(pEmptyStar);
			else if(RatingField.compare(RatingOffset, std::strlen(pAlternateEmptyStar), pAlternateEmptyStar) == 0)
				RatingOffset += std::strlen(pAlternateEmptyStar);
			else
				return Fail("MALFORMED_RATING_FIELD");
		}
		if(RatingOffset != RatingField.size())
			return Fail("MALFORMED_RATING_FIELD");

		static constexpr const char *pPointsSuffix = " ⚐";
		static constexpr const char *pTimeSuffix = " ◷";
		const auto IsDecimal = [](const std::string &Value) {
			if(Value.empty())
				return false;
			for(const char Character : Value)
				if(Character < '0' || Character > '9')
					return false;
			return true;
		};
		const bool HasPointsSuffix = PointsField.size() > std::strlen(pPointsSuffix) &&
			PointsField.compare(PointsField.size() - std::strlen(pPointsSuffix), std::strlen(pPointsSuffix), pPointsSuffix) == 0;
		const std::string NumericPointsField = HasPointsSuffix ? PointsField.substr(0, PointsField.size() - std::strlen(pPointsSuffix)) : PointsField;
		if(!IsDecimal(NumericPointsField))
			return Fail("MALFORMED_POINTS_FIELD");
		const bool HasTimeSuffix = TimeField.size() > std::strlen(pTimeSuffix) &&
			TimeField.compare(TimeField.size() - std::strlen(pTimeSuffix), std::strlen(pTimeSuffix), pTimeSuffix) == 0;
		const std::string ClockField = HasTimeSuffix ? TimeField.substr(0, TimeField.size() - std::strlen(pTimeSuffix)) : TimeField;
		if(ClockField.size() != 5 || ClockField[2] != ':' || ClockField[0] < '0' || ClockField[0] > '9' ||
			ClockField[1] < '0' || ClockField[1] > '9' || ClockField[3] < '0' || ClockField[3] > '9' || ClockField[4] < '0' || ClockField[4] > '9' ||
			(ClockField[3] - '0') * 10 + (ClockField[4] - '0') > 59)
			return Fail("MALFORMED_TIME_FIELD");

		*pRating = FilledStars;
		if(ppReason != nullptr)
			*ppReason = "ACCEPTED";
		return true;
	}

	inline bool ParseKogServerCategory(const char *pServerName, SMapDifficultyMetadata *pMetadata)
	{
		if(pMetadata == nullptr)
			return false;
		*pMetadata = {};
		if(pServerName == nullptr || pServerName[0] == '\0')
			return false;

		std::string LowerName(pServerName);
		for(char &Character : LowerName)
			Character = static_cast<char>(std::tolower(static_cast<unsigned char>(Character)));
		static constexpr const char *apServerCategories[] = {
			"easy gores", "main gores", "hard gores", "insane gores", "extreme gores", "solo gores"};
		static constexpr const char *apCategories[] = {"Easy", "Main", "Hard", "Insane", "Extreme", "Solo"};
		int Found = -1;
		for(size_t Index = 0; Index < sizeof(apServerCategories) / sizeof(apServerCategories[0]); ++Index)
		{
			const std::string CategoryPhrase = apServerCategories[Index];
			size_t PhraseOffset = LowerName.find(CategoryPhrase);
			bool PhraseFound = false;
			while(PhraseOffset != std::string::npos)
			{
				const bool HasLeftWordBoundary = PhraseOffset == 0 || !std::isalnum(static_cast<unsigned char>(LowerName[PhraseOffset - 1]));
				const size_t PhraseEnd = PhraseOffset + CategoryPhrase.size();
				const bool HasRightWordBoundary = PhraseEnd == LowerName.size() || !std::isalnum(static_cast<unsigned char>(LowerName[PhraseEnd]));
				if(HasLeftWordBoundary && HasRightWordBoundary)
				{
					PhraseFound = true;
					break;
				}
				PhraseOffset = LowerName.find(CategoryPhrase, PhraseOffset + 1);
			}
			if(!PhraseFound)
				continue;
			if(Found != -1)
				return false;
			Found = static_cast<int>(Index);
		}
		if(Found == -1)
			return false;
		pMetadata->m_Category = apCategories[Found];
		return true;
	}

	inline bool IsMapDifficultyMetadataUsable(const SMapDifficultyMetadata &Metadata)
	{
		return Metadata.m_Category.size() <= MAX_CONTEXT_LENGTH &&
			((!Metadata.m_Category.empty()) || (Metadata.m_Stars >= 0 && Metadata.m_Stars <= MAX_MAP_DIFFICULTY_STARS));
	}

	inline bool IsMapDifficultyMetadataComplete(const SMapDifficultyMetadata &Metadata)
	{
		return !Metadata.m_Category.empty() && Metadata.m_Stars >= 0 && Metadata.m_Stars <= MAX_MAP_DIFFICULTY_STARS;
	}

	inline std::string MapDifficultyDisplayName(const SRunContext &Context)
	{
		if(Context.m_MapDifficulty.empty())
			return "—";
		if(Context.m_MapStars <= 0 || Context.m_MapStars > MAX_MAP_DIFFICULTY_STARS)
			return Context.m_MapDifficulty;
		std::string Result = Context.m_MapDifficulty + " · ";
		for(int Index = 0; Index < Context.m_MapStars; ++Index)
			Result += "★";
		return Result;
	}

	// Raw counters are deliberately only a data contract in Phase 1. They are
	// changed by explicitly confirmed event deltas, never by a guessed detector.
	struct SRawStats
	{
		int m_ConfirmedEvents = 0;
		int m_Falls = 0;
		int m_Wipes = 0;
		int m_Saves = 0;
		int m_UsefulSaves = 0;
		int m_MeaningfulSkips = 0;
		int m_BackRecoveries = 0;
		int m_HarmfulEvents = 0;
		int m_PositiveEvents = 0;
		int m_Aleds = 0;
		int m_Clutches = 0;
		// Deprecated compatibility-only field. New events do not populate this
		// legacy counter and the current persistence writer omits it. The member
		// remains so older history documents can still be read safely without a
		// format migration; it is not a user-facing statistic or scoring input.
		int m_UpDowns = 0;
		int m_MissedCatches = 0;
		int m_TeamMistakes = 0;
		int m_PreventedProgressLoss = 0;
		int m_RecoveryDifficultyIncreases = 0;
	};

	struct SImpactFunctionalRosterEntry
	{
		SParticipantId m_Participant;
		EImpactFunctionalClass m_Classification = EImpactFunctionalClass::UNKNOWN;
		std::string m_Reason;
		bool m_AliveKnown = false;
		bool m_Alive = false;
		bool m_Frozen = false;
		bool m_AutonomousRecoveryKnown = false;
		bool m_AutonomousRecoveryPossible = false;
		bool m_RosterMember = false;
		bool m_Engaged = false;
		bool m_Finalized = false;
	};

	// Append-only, presentation-independent record of a production decision or
	// state transition. These fields are copied to history and never read by the
	// gameplay/event resolver.
	struct SImpactForensicEvent
	{
		uint64_t m_Sequence = 0;
		uint64_t m_RunId = 0;
		uint64_t m_EventId = 0;
		uint64_t m_ProblemEpisodeId = 0;
		uint64_t m_FallEpisodeId = 0;
		uint64_t m_SaveCandidateId = 0;
		uint64_t m_ClutchEpisodeId = 0;
		uint64_t m_ActorInteractionId = 0;
		int64_t m_Tick = -1;
		EImpactForensicType m_Type = EImpactForensicType::CANONICAL_EVENT;
		int m_TeamId = 0;
		SParticipantId m_Target;
		SParticipantId m_Actor;
		EImpactForensicReason m_Reason = EImpactForensicReason::NONE;
		std::string m_ReasonDetail;
		EAnalysisState m_RunStateBefore = EAnalysisState::WAITING;
		EAnalysisState m_RunStateAfter = EAnalysisState::WAITING;
		EImpactEpisodeState m_EpisodeStateBefore = EImpactEpisodeState::NONE;
		EImpactEpisodeState m_EpisodeStateAfter = EImpactEpisodeState::NONE;
		EImpactPhysicalOutcome m_PhysicalOutcome = EImpactPhysicalOutcome::UNKNOWN;
		int m_ActiveParticipantCount = -1;
		int m_PresentParticipantCount = -1;
		int m_HistoricalParticipantCount = -1;
		bool m_TargetSnapshotAvailable = false;
		bool m_ActorSnapshotAvailable = false;
		bool m_TargetPredictedStateAvailable = false;
		bool m_ActorTargetInteractionObservable = false;
		bool m_PhysicalChangeEvidence = false;
		bool m_DecisivePhysicalEvidence = false;
		bool m_DecisionEligible = false;
		bool m_DecisionAccepted = false;
		bool m_AutonomousEvidenceEvaluated = false;
		bool m_AutonomousEvidenceKnown = false;
		bool m_AutonomousRecoveryPossible = false;
		bool m_SelfSafeProven = false;
		int64_t m_SelfSafeEvidenceTick = -1;
		int64_t m_RestorationTick = -1;
		int64_t m_ConfirmationStartTick = -1;
		int64_t m_ConfirmationDeadlineTick = -1;
		int64_t m_ConfirmedTicks = -1;
		int64_t m_RequiredTicks = -1;
		SRawStats m_RawDelta;
		std::vector<SImpactFunctionalRosterEntry> m_vFunctionalRoster;
	};

	struct SScoreSnapshot
	{
		// Frozen distinguishes a finalized unavailable score from an active score
		// that has not reached its terminal scoring boundary yet.
		bool m_Frozen = false;
		bool m_Available = false;
		double m_Rating = 0.0;
		double m_Impact = 0.0;
		double m_Stability = 0.0;
		double m_Consistency = 50.0;
		int m_Tier = 0;
		int m_RatingVersion = IMPACT_RATING_VERSION;
		int m_ScoringVersion = IMPACT_SCORING_VERSION_UNSET;
	};

	inline std::string FormatTruncatedRating(double Value)
	{
		if(!std::isfinite(Value))
			return "—";
		const double Truncated = std::abs(Value) > std::numeric_limits<double>::max() / 100.0 ?
			Value : std::trunc(Value * 100.0) / 100.0;
		if(Truncated == 0.0)
			return "0.00";
		char aBuffer[384];
		const int Length = std::snprintf(aBuffer, sizeof(aBuffer), "%.2f", Truncated);
		return Length < 0 || static_cast<size_t>(Length) >= sizeof(aBuffer) ? "—" : aBuffer;
	}

	inline std::string FormatTruncatedImpact(double Value)
	{
		if(!std::isfinite(Value))
			return "—";
		const double Truncated = std::abs(Value) > std::numeric_limits<double>::max() / 100.0 ?
			Value : std::trunc(Value * 100.0) / 100.0;
		if(Truncated == 0.0)
			return "0.00%";
		char aBuffer[384];
		const int Length = std::snprintf(aBuffer, sizeof(aBuffer), Truncated > 0.0 ? "+%.2f%%" : "%.2f%%", Truncated);
		return Length < 0 || static_cast<size_t>(Length) >= sizeof(aBuffer) ? "—" : aBuffer;
	}

	inline std::string FormatTruncatedStability(double Value)
	{
		if(!std::isfinite(Value))
			return "—";
		char aBuffer[384];
		const int Length = std::snprintf(aBuffer, sizeof(aBuffer), "%.0f%%", std::trunc(Value));
		return Length < 0 || static_cast<size_t>(Length) >= sizeof(aBuffer) ? "—" : aBuffer;
	}

	enum class EParticipantTerminalStatus
	{
		NOT_FINALIZED,
		FINISHED,
		SELF_RESET,
		LEFT,
		RUN_ENDED,
		UNKNOWN,
		// Appended to preserve persisted enum values from earlier Phase 4 work.
		OTHER,
	};

	// Optional presentation-only native DDNet skin snapshot. This is tied to the
	// enclosing participant identity/generation and is never scoring input.
	struct SParticipantCosmeticSnapshot
	{
		bool m_Available = false;
		std::string m_SkinName;
		bool m_UseCustomColor = false;
		int m_ColorBody = 0;
		int m_ColorFeet = 0;
	};

	struct SParticipant
	{
		SParticipantId m_Id;
		std::string m_Name;
		SParticipantCosmeticSnapshot m_Cosmetics;
		int64_t m_FirstStartTick = -1;
		int64_t m_PersonalAnalysisStartTick = -1;
		// This is the cumulative ACTIVE-time baseline at which this participant
		// became eligible. It is separate from the wall/server tick so pauses do
		// not consume the personal display gate.
		int64_t m_PersonalAnalysisStartActiveTicks = -1;
		// Engagement is positive, generation-scoped evidence that this roster
		// member is participating in this run. It is deliberately independent from
		// personal START and from current physical functionality.
		bool m_Engaged = false;
		int64_t m_EngagedTick = -1;
		bool m_Late = false;
		bool m_Present = false;
		// A finalized participant remains in the historical team roster, but its
		// last confirmed statistics are immutable for the rest of this run.
		bool m_Finalized = false;
		// Kept separate from m_Finalized: a team finish finalizes every row,
		// while a prior self-reset or authoritative departure must remain distinct.
		EParticipantTerminalStatus m_TerminalStatus = EParticipantTerminalStatus::NOT_FINALIZED;
		// Event-driven scoring-sample duration. New rows start known at zero;
		// legacy rows without these fields remain unknown. While engaged, the open
		// interval is represented by m_ParticipationStartTick and is materialized
		// only when queried or closed at a terminal transition.
		int64_t m_ParticipationTicks = 0;
		bool m_ParticipationTicksKnown = false;
		int64_t m_ParticipationStartTick = -1;
		int64_t m_ParticipationTerminalTick = -1;
		SRawStats m_RawStats;
		// Future finalized per-player Phase 4 values. Kept unavailable until an
		// approved scoring version computes and freezes them.
		SScoreSnapshot m_Score;
		// Hidden target-side accounting derived from accepted SAVE_CONFIRMED
		// actor/target pairs. Legacy records without these fields remain unknown.
		int m_RescuesReceived = 0;
		bool m_RescuesReceivedKnown = true;
	};

	inline bool ParticipantParticipationTicksAt(const SParticipant &Participant, int64_t Tick, int64_t *pTicks)
	{
		if(pTicks == nullptr || !Participant.m_ParticipationTicksKnown || Participant.m_ParticipationTicks < 0)
			return false;
		int64_t Total = Participant.m_ParticipationTicks;
		if(Participant.m_ParticipationStartTick >= 0)
		{
			if(Tick < Participant.m_ParticipationStartTick)
				return false;
			const int64_t Elapsed = Tick - Participant.m_ParticipationStartTick;
			if(Total > std::numeric_limits<int64_t>::max() - Elapsed)
				Total = std::numeric_limits<int64_t>::max();
			else
				Total += Elapsed;
		}
		*pTicks = Total;
		return true;
	}

	// This is descriptive evidence only. No anti-farm score or counter adjustment
	// consumes the classification in the current implementation.
	enum class ERepeatedContextClass
	{
		INDEPENDENT,
		POSSIBLE_REPEATED_CONTEXT,
		UNKNOWN,
	};

	struct SImpactScoringEvidence
	{
		uint64_t m_RunId = 0;
		uint64_t m_EventId = 0;
		EImpactEventType m_Type = EImpactEventType::SAVE_CONFIRMED;
		int64_t m_Tick = -1;
		SParticipantId m_Actor;
		SParticipantId m_Target;
		uint64_t m_ProblemEpisodeId = 0;
		uint64_t m_FallEpisodeId = 0;
		uint64_t m_RescueEpisodeId = 0;
		uint64_t m_ClutchEpisodeId = 0;
		uint64_t m_ActorInteractionId = 0;
		uint64_t m_LinkedSaveEventId = 0;
		bool m_IsCanonicalSave = false;
		bool m_ClutchConfirmed = false;
		bool m_AledConfirmed = false;
	};

	inline ERepeatedContextClass ClassifyRepeatedSaveContext(const SImpactScoringEvidence &Previous, const SImpactScoringEvidence &Current, uint64_t MaxTemporalSeparationTicks)
	{
		// Retained in the signature for compatibility with existing aggregation
		// callers. Time proximity is never evidence of either repetition or
		// independence.
		(void)MaxTemporalSeparationTicks;
		if(Previous.m_RunId == 0 || Previous.m_RunId != Current.m_RunId ||
			Previous.m_EventId == 0 || Current.m_EventId == 0 || Previous.m_EventId == Current.m_EventId ||
			!Previous.m_IsCanonicalSave || !Current.m_IsCanonicalSave ||
			Previous.m_LinkedSaveEventId == 0 || Current.m_LinkedSaveEventId == 0 ||
			!Previous.m_Actor.IsValid() || !Current.m_Actor.IsValid() ||
			!Previous.m_Target.IsValid() || !Current.m_Target.IsValid() ||
			Previous.m_Tick < 0 || Current.m_Tick < 0)
			return ERepeatedContextClass::UNKNOWN;

		// A different target is a distinct rescue subject, unless the evidence
		// claims the same problem episode for both targets (which is inconsistent).
		if(Previous.m_Target != Current.m_Target)
		{
			if(Previous.m_ProblemEpisodeId != 0 && Previous.m_ProblemEpisodeId == Current.m_ProblemEpisodeId)
				return ERepeatedContextClass::UNKNOWN;
			return ERepeatedContextClass::INDEPENDENT;
		}

		// A saver change on the same target may be cooperative work in one crisis;
		// it is not proof of an independent rescue context.
		if(Previous.m_Actor != Current.m_Actor)
			return ERepeatedContextClass::UNKNOWN;

		const bool HasBothProblemIds = Previous.m_ProblemEpisodeId != 0 && Current.m_ProblemEpisodeId != 0;
		const bool HasBothFallIds = Previous.m_FallEpisodeId != 0 && Current.m_FallEpisodeId != 0;
		const bool SameFallId = HasBothFallIds && Previous.m_FallEpisodeId == Current.m_FallEpisodeId;
		const bool DifferentFallIds = HasBothFallIds && Previous.m_FallEpisodeId != Current.m_FallEpisodeId;
		const bool SameRescueId = Previous.m_RescueEpisodeId != 0 && Previous.m_RescueEpisodeId == Current.m_RescueEpisodeId;
		const bool SameInteractionId = Previous.m_ActorInteractionId != 0 && Previous.m_ActorInteractionId == Current.m_ActorInteractionId;

		if(HasBothProblemIds && Previous.m_ProblemEpisodeId == Current.m_ProblemEpisodeId)
			return DifferentFallIds ? ERepeatedContextClass::UNKNOWN : ERepeatedContextClass::POSSIBLE_REPEATED_CONTEXT;

		// A distinct authoritative problem identity between two ordered,
		// accepted canonical Saves proves two separate rescue contexts. A Fall ID,
		// candidate ID, interaction ID, or elapsed time alone cannot do so.
		if(HasBothProblemIds && Previous.m_ProblemEpisodeId != Current.m_ProblemEpisodeId)
		{
			if(Current.m_Tick <= Previous.m_Tick || SameFallId || SameRescueId || SameInteractionId)
				return ERepeatedContextClass::UNKNOWN;
			return ERepeatedContextClass::INDEPENDENT;
		}

		// Exact shared rescue/fall/interaction identity can establish a repeated
		// context when the broader problem ID is unavailable. Conflicting Fall
		// identities remain unknown rather than being resolved by whichever ID
		// happens to match.
		if((SameFallId || SameRescueId || SameInteractionId) && !DifferentFallIds)
			return ERepeatedContextClass::POSSIBLE_REPEATED_CONTEXT;
		return ERepeatedContextClass::UNKNOWN;
	}

	// Statistics list the complete run roster. Personal START and positive
	// engagement are separate authorities and do not control row existence.
	inline bool IsImpactStatisticsParticipant(const SParticipant &Participant)
	{
		return Participant.m_Id.IsValid();
	}

	inline size_t ImpactStatisticsParticipantCount(const std::vector<SParticipant> &vParticipants)
	{
		size_t Count = 0;
		for(const SParticipant &Participant : vParticipants)
			Count += IsImpactStatisticsParticipant(Participant) ? 1 : 0;
		return Count;
	}

	struct SImpactEvent
	{
		uint64_t m_EventId = 0;
		EImpactEventType m_Type = EImpactEventType::START_CROSSING;
		SParticipantId m_Actor;
		SParticipantId m_Target;
		// Set for START_CROSSING events from the authoritative DDNet team
		// snapshot. Other event types inherit the current run's scope.
		int m_TeamId = 0;
		std::string m_ActorName;
		int64_t m_StartTick = -1;
		int64_t m_ResolveTick = -1;
		// Exact team-race duration received in SV_RACEFINISH. This is kept on the
		// finish event so the run snapshot can use the server's value rather than
		// deriving a duration from the local Impact analysis clock.
		int m_AuthoritativeFinishTimeMillis = -1;
		bool m_Confirmed = false;
		bool m_Present = false;
		bool m_AntiFarm = false;
		int m_TeamUtility = 0;
		int m_Difficulty = 0;
		int m_TraversalSaved = 0;
		int m_RecoverySaved = 0;
		int m_RunSavingValue = 0;
		int m_ExecutionQuality = 0;
		int m_PositiveContribution = 0;
		int m_HarmfulContribution = 0;
		std::string m_CausalReason;
		std::vector<std::string> m_vTags;
		EImpactEpisodeState m_EpisodeState = EImpactEpisodeState::NONE;
		ECausalConfidence m_CausalConfidence = ECausalConfidence::INSUFFICIENT;
		EOwnershipStatus m_OwnershipStatus = EOwnershipStatus::NONE;
		std::string m_BeforeContext;
		std::string m_AfterContext;
		std::vector<SParticipantId> m_vHelpers;
		bool m_AppliedExactlyOnce = false;
		SRawStats m_RawDelta;
	};

	// One immutable, episode-scoped Save record carries the minimum causal
	// recovery facts plus bounded diagnostics. It may carry an optional Fall id
	// for history association, but it is never a Fall producer or a Fall decision
	// input. The record is created only after a target-specific action has
	// produced an observed physical change and a subsequent safe boundary. The
	// 0.7 second confirmation is the only later Save step.
	struct SImpactCausalRecoveryProof
	{
		bool m_Valid = false;
		uint64_t m_ProofId = 0;
		uint64_t m_FallEpisodeId = 0;
		uint64_t m_RecoveryEpisodeId = 0;
		SParticipantId m_Target;
		SParticipantId m_DecisiveActor;
		int m_TeamId = 0;
		int64_t m_HarmStartTick = -1;
		int64_t m_ActionTick = -1;
		int64_t m_FirstProvenTick = -1;
		int64_t m_RestoreTick = -1;
		int64_t m_HookFirstTick = -1;
		int64_t m_HookLastTick = -1;
		EImpactPhysicalOutcome m_PreActionOutcome = EImpactPhysicalOutcome::UNKNOWN;
		EImpactPhysicalOutcome m_PostActionOutcome = EImpactPhysicalOutcome::UNKNOWN;
		bool m_TargetAlreadySafeBeforeAction = false;
		bool m_TargetSpecificEvidence = false;
		bool m_PhysicalChangeEvidence = false;
		bool m_SafeOutcomeEvidence = false;
		bool m_HookEvidence = false;
		bool m_HammerEvidence = false;
		EOwnershipStatus m_OwnershipStatus = EOwnershipStatus::NONE;
		ECausalConfidence m_Confidence = ECausalConfidence::INSUFFICIENT;
		std::string m_Reason;
		std::vector<SParticipantId> m_vContributors;
	};

	struct SImpactPendingEpisode
	{
		uint64_t m_EpisodeId = 0;
		EImpactEventType m_Type = EImpactEventType::SAVE_CONFIRMED;
		SParticipantId m_Actor;
		SParticipantId m_Target;
		int m_TeamId = 0;
		int64_t m_StartTick = -1;
		int64_t m_RestoreTick = -1;
		int64_t m_RequiredTicks = 0;
		// The presence of this record is the active lifecycle marker. NONE is used
		// until a concrete Fall or recovery result is emitted; no pending/unknown
		// episode state is stored.
		EImpactEpisodeState m_State = EImpactEpisodeState::NONE;
		ECausalConfidence m_CausalConfidence = ECausalConfidence::INSUFFICIENT;
		EImpactHarmCause m_HarmCause = EImpactHarmCause::UNKNOWN;
		ECausalConfidence m_HarmCausalConfidence = ECausalConfidence::INSUFFICIENT;
		EOwnershipStatus m_OwnershipStatus = EOwnershipStatus::NONE;
		bool m_TargetAlive = false;
		bool m_TargetUnfrozen = false;
		bool m_HammerEvidence = false;
		bool m_HookEvidence = false;
		bool m_UpDownEvidence = false;
		EImpactUnfreezeCause m_UnfreezeCause = EImpactUnfreezeCause::UNKNOWN;
		EImpactRecoveryNeed m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
		// Physical resolution is the only outcome that can confirm a Fall. It is
		// intentionally independent from the origin and recovery classifications.
		EImpactPhysicalOutcome m_PhysicalOutcome = EImpactPhysicalOutcome::UNKNOWN;
		// Recovery attribution is retained at episode scope. These flags describe
		// observed physical evidence only; they are not inferred from actor count or
		// from the presence of a hook span.
		bool m_PhysicalChangeEvidence = false;
		bool m_DecisivePhysicalEvidence = false;
		// Immutable context captured at harmful-entry time. Later sparse or
		// supported snapshots must not rewrite the recovery need that justified the
		// Fall episode.
		EImpactRecoveryNeed m_InitialRecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
		// Harm and recovery use separate episode objects, but a physical episode
		// must have an explicit bidirectional binding. This id is populated on the
		// retained Fall when its recovery episode is created.
		uint64_t m_RelatedRecoveryEpisodeId = 0;
		uint64_t m_RelatedFallEpisodeId = 0;
		// Save diagnostics retain the Fall semantic result after the Fall storage
		// is cleaned up. This is association metadata only; it never affects either
		// raw decision.
		bool m_RelatedFallAlreadyConfirmed = false;
		// A harmful freeze remains in the active episode store until a concrete
		// outcome is observed. Once a Fall is confirmed, this flag and the lifecycle
		// state become immutable so cleanup cannot create a duplicate or erase it.
		bool m_FallAlreadyConfirmed = false;
		// A successful bound recovery closes the retained lifecycle context. It is
		// not a Fall confirmation and must never be inferred from a Save alone.
		bool m_RecoveryOutcomeConfirmed = false;
		// Side-car metadata for a confirmed Save. This never participates in Save
		// eligibility or confirmation; it only carries a latched last-unfrozen
		// context to the existing canonical Save result.
		bool m_ClutchCandidate = false;
		int64_t m_ClutchArmedTick = -1;
		// Diagnostic identity for the Clutch side-car. It is independent of the
		// canonical Save/Fall episode id and is only used to trace one continuous
		// Clutch crisis through pending Save confirmation.
		uint64_t m_ClutchEpisodeId = 0;
		// Origin-only causal evidence. These fields are never rewritten by later
		// recovery interactions; they keep an unresolved entry from being promoted
		// to self-caused after an external cause was already observed at origin.
		bool m_OriginExternalCauseEvidence = false;
		bool m_OriginSelfOriginEvidence = false;
		int64_t m_HookFirstTick = -1;
		int64_t m_HookLastTick = -1;
		bool m_HookControlContinuity = false;
		// Start of the currently continuous, authoritative alive+unfrozen
		// confirmation interval. -1 means that an unavailable snapshot interrupted
		// the interval; unavailable time is never counted toward Save confirmation.
		int64_t m_ConfirmedRecoveryStartTick = -1;
		bool m_TargetWasEligible = false;
		std::string m_BeforeContext;
		std::string m_AfterContext;
		std::string m_DebugReason;
		std::vector<SParticipantId> m_vHelpers;
		SImpactCausalRecoveryProof m_CausalRecoveryProof;
	};

	struct STickEventBatch
	{
		int64_t m_ServerTick = -1;
		std::vector<SImpactEvent> m_vCandidates;
};

	struct SRunSnapshot
	{
		int m_SchemaVersion = IMPACT_SCHEMA_VERSION;
		int m_AlgorithmVersion = IMPACT_ALGORITHM_VERSION;
		int m_StaticAnalysisVersion = IMPACT_STATIC_ANALYSIS_VERSION;
		uint64_t m_RunId = 0;
		// Local immutable identity for this archived run record. Empty only for
		// legacy records that predate run_uid.
		std::string m_RunUid;
		bool m_HasGlobalRunNumber = false;
		uint64_t m_GlobalRunNumber = 0;
		SRunContext m_Context;
		EAnalysisState m_State = EAnalysisState::WAITING;
		ERunResult m_Result = ERunResult::NONE;
		int64_t m_RunStartedTick = -1;
		int64_t m_AnalysisStartedTick = -1;
		int64_t m_LastConfirmedTick = -1;
		int64_t m_LastStateTick = -1;
		int64_t m_ActiveAnalysisTicks = 0;
		int64_t m_EndTick = -1;
		// -1 means that this run was not finalized from an authoritative
		// SV_RACEFINISH time (legacy history and non-finish results).
		int m_AuthoritativeFinishTimeMillis = -1;
		int64_t m_SavedAt = 0;
		std::vector<SParticipant> m_vParticipants;
		SRawStats m_RawStats;
		SScoreSnapshot m_Score;
		std::vector<SImpactEvent> m_vEvents;
		int m_ScoringEvidenceVersion = 0;
		bool m_ScoringEvidenceComplete = false;
		uint64_t m_ScoringEvidenceDroppedEvents = 0;
		std::vector<SImpactScoringEvidence> m_vScoringEvidence;
		std::vector<SImpactPendingEpisode> m_vPendingEpisodes;
		int m_ForensicVersion = IMPACT_FORENSIC_VERSION;
		bool m_ForensicTraceEnabled = true;
		bool m_ForensicTraceComplete = true;
		bool m_ForensicTraceTruncated = false;
		uint64_t m_ForensicDroppedEvents = 0;
		std::vector<SImpactForensicEvent> m_vForensicEvents;
	};

	// Validate the telemetry projection against the already accepted canonical
	// ledger records. This is persistence/restore validation only; it is not used
	// by event detection or scoring.
	inline bool IsScoringEvidenceConsistent(const SRunSnapshot &Snapshot)
	{
		if(Snapshot.m_ScoringEvidenceVersion == 0)
			return Snapshot.m_vScoringEvidence.empty();
		if(Snapshot.m_ScoringEvidenceVersion != IMPACT_SCORING_EVIDENCE_VERSION)
			return false;

		std::unordered_map<uint64_t, const SImpactEvent *> EventsById;
		EventsById.reserve(Snapshot.m_vEvents.size());
		for(const SImpactEvent &Event : Snapshot.m_vEvents)
			if(Event.m_EventId == 0 || !EventsById.emplace(Event.m_EventId, &Event).second)
				return false;

		std::unordered_map<uint64_t, const SImpactScoringEvidence *> EvidenceById;
		EvidenceById.reserve(Snapshot.m_vScoringEvidence.size());
		for(const SImpactScoringEvidence &Evidence : Snapshot.m_vScoringEvidence)
			if(Evidence.m_EventId == 0 || !EvidenceById.emplace(Evidence.m_EventId, &Evidence).second)
				return false;

		for(const SImpactScoringEvidence &Evidence : Snapshot.m_vScoringEvidence)
		{
			const auto EventIt = EventsById.find(Evidence.m_EventId);
			if(EventIt == EventsById.end())
				return false;
			const SImpactEvent &Event = *EventIt->second;
			const int64_t EventTick = Event.m_ResolveTick >= 0 ? Event.m_ResolveTick : Event.m_StartTick;
			const bool ExpectedSave = Event.m_RawDelta.m_Saves > 0 &&
				(Event.m_Type == EImpactEventType::SAVE_CONFIRMED || Event.m_Type == EImpactEventType::ALED_CONFIRMED);
			const bool ExpectedClutch = ExpectedSave && Event.m_RawDelta.m_Clutches > 0;
			const bool ExpectedAled = Event.m_Type == EImpactEventType::ALED_CONFIRMED && Event.m_RawDelta.m_Aleds > 0;
			if(Event.m_Type != Evidence.m_Type || Event.m_Actor != Evidence.m_Actor || Event.m_Target != Evidence.m_Target ||
				EventTick != Evidence.m_Tick || ExpectedSave != Evidence.m_IsCanonicalSave ||
				ExpectedClutch != Evidence.m_ClutchConfirmed || ExpectedAled != Evidence.m_AledConfirmed ||
				(Evidence.m_IsCanonicalSave && Evidence.m_LinkedSaveEventId != Evidence.m_EventId) ||
				(!Evidence.m_IsCanonicalSave && Evidence.m_LinkedSaveEventId == Evidence.m_EventId))
				return false;

			if(Evidence.m_LinkedSaveEventId == 0 || Evidence.m_LinkedSaveEventId == Evidence.m_EventId)
				continue;
			if(Evidence.m_Type != EImpactEventType::ALED_CONFIRMED || !Evidence.m_AledConfirmed)
				return false;
			const auto SaveEventIt = EventsById.find(Evidence.m_LinkedSaveEventId);
			const auto SaveEvidenceIt = EvidenceById.find(Evidence.m_LinkedSaveEventId);
			if(SaveEventIt == EventsById.end() || SaveEvidenceIt == EvidenceById.end())
				return false;
			const SImpactEvent &SaveEvent = *SaveEventIt->second;
			const SImpactScoringEvidence &SaveEvidence = *SaveEvidenceIt->second;
			if(SaveEvent.m_Type != EImpactEventType::SAVE_CONFIRMED || SaveEvent.m_RawDelta.m_Saves <= 0 ||
				SaveEvent.m_Actor != Event.m_Actor || SaveEvent.m_Target != Event.m_Target ||
				SaveEvent.m_StartTick != Event.m_StartTick || SaveEvent.m_ResolveTick != Event.m_ResolveTick ||
				!SaveEvidence.m_IsCanonicalSave)
				return false;
		}
		return true;
	}

	struct SFinalRankingEntry
	{
		size_t m_ParticipantIndex = 0;
		int m_Rank = 0;
		bool m_IsMvp = false;
	};

	inline const char *AnalysisStateName(EAnalysisState State)
	{
		switch(State)
		{
		case EAnalysisState::WAITING: return "WAITING";
		case EAnalysisState::ANALYZING: return "ANALYZING";
		case EAnalysisState::ACTIVE: return "ACTIVE";
		case EAnalysisState::PAUSED_NO_TEAM: return "PAUSED_NO_TEAM";
		case EAnalysisState::FINALIZED: return "FINALIZED";
		}
		return "WAITING";
	}

	inline EAnalysisState AnalysisStateFromName(const char *pName)
	{
		if(!pName) return EAnalysisState::WAITING;
		if(!std::strcmp(pName, "ANALYZING")) return EAnalysisState::ANALYZING;
		if(!std::strcmp(pName, "ACTIVE")) return EAnalysisState::ACTIVE;
		if(!std::strcmp(pName, "PAUSED_NO_TEAM")) return EAnalysisState::PAUSED_NO_TEAM;
		if(!std::strcmp(pName, "FINALIZED")) return EAnalysisState::FINALIZED;
		return EAnalysisState::WAITING;
	}

	inline const char *RunResultName(ERunResult Result)
	{
		switch(Result)
		{
		case ERunResult::NONE: return "NONE";
		case ERunResult::FINISHED: return "FINISHED";
		case ERunResult::WIPED: return "WIPED";
		case ERunResult::TEAM_RESET: return "TEAM_RESET";
		case ERunResult::ABORTED_MAP_CHANGE: return "ABORTED_MAP_CHANGE";
		case ERunResult::ABORTED_SERVER_CHANGE: return "ABORTED_SERVER_CHANGE";
		case ERunResult::ABORTED_CONNECTION: return "ABORTED_CONNECTION";
		}
		return "NONE";
	}

	inline const char *RunResultDisplayName(ERunResult Result)
	{
		switch(Result)
		{
		case ERunResult::NONE: return "In progress";
		case ERunResult::FINISHED: return "Finished";
		case ERunResult::WIPED: return "Wiped";
		case ERunResult::TEAM_RESET: return "Reset";
		case ERunResult::ABORTED_MAP_CHANGE: return "Map changed";
		case ERunResult::ABORTED_SERVER_CHANGE: return "Server changed";
		case ERunResult::ABORTED_CONNECTION: return "Disconnected";
		}
		return "In progress";
	}

	inline ERunResult RunResultFromName(const char *pName)
	{
		if(!pName) return ERunResult::NONE;
		if(!std::strcmp(pName, "FINISHED")) return ERunResult::FINISHED;
		if(!std::strcmp(pName, "WIPED")) return ERunResult::WIPED;
		if(!std::strcmp(pName, "TEAM_RESET")) return ERunResult::TEAM_RESET;
		if(!std::strcmp(pName, "ABORTED_MAP_CHANGE")) return ERunResult::ABORTED_MAP_CHANGE;
		if(!std::strcmp(pName, "ABORTED_SERVER_CHANGE")) return ERunResult::ABORTED_SERVER_CHANGE;
		if(!std::strcmp(pName, "ABORTED_CONNECTION")) return ERunResult::ABORTED_CONNECTION;
		return ERunResult::NONE;
	}

	inline const char *ImpactEventTypeName(EImpactEventType Type)
	{
		switch(Type)
		{
		case EImpactEventType::START_CROSSING: return "START_CROSSING";
		case EImpactEventType::PARTICIPANT_PRESENCE: return "PARTICIPANT_PRESENCE";
		case EImpactEventType::SAVE_CONFIRMED: return "SAVE_CONFIRMED";
		case EImpactEventType::FALL_CONFIRMED: return "FALL_CONFIRMED";
		case EImpactEventType::ALED_CONFIRMED: return "ALED_CONFIRMED";
		case EImpactEventType::UPDOWN_CONFIRMED: return "UPDOWN_CONFIRMED";
		case EImpactEventType::MISSED_CATCH_CONFIRMED: return "MISSED_CATCH_CONFIRMED";
		case EImpactEventType::TEAM_MISTAKE_CONFIRMED: return "TEAM_MISTAKE_CONFIRMED";
		case EImpactEventType::TEAM_HARM_CONFIRMED: return "TEAM_HARM_CONFIRMED";
		case EImpactEventType::PREVENTED_PROGRESS_LOSS_CONFIRMED: return "PREVENTED_PROGRESS_LOSS_CONFIRMED";
		case EImpactEventType::RECOVERY_DIFFICULTY_INCREASE_CONFIRMED: return "RECOVERY_DIFFICULTY_INCREASE_CONFIRMED";
		case EImpactEventType::JUMP_RESET_CONFIRMED: return "JUMP_RESET_CONFIRMED";
		case EImpactEventType::FINISH_CONFIRMED: return "FINISH_CONFIRMED";
		case EImpactEventType::WIPE_CONFIRMED: return "WIPE_CONFIRMED";
		case EImpactEventType::TEAM_RESET_CONFIRMED: return "TEAM_RESET_CONFIRMED";
		case EImpactEventType::MAP_CHANGE: return "MAP_CHANGE";
		case EImpactEventType::SERVER_CHANGE: return "SERVER_CHANGE";
		case EImpactEventType::CONNECTION_LOST: return "CONNECTION_LOST";
		case EImpactEventType::PARTICIPANT_FINALIZED: return "PARTICIPANT_FINALIZED";
		}
		return "START_CROSSING";
	}

	inline EImpactEventType ImpactEventTypeFromName(const char *pName)
	{
		if(!pName) return EImpactEventType::START_CROSSING;
		if(!std::strcmp(pName, "PARTICIPANT_PRESENCE")) return EImpactEventType::PARTICIPANT_PRESENCE;
		if(!std::strcmp(pName, "SAVE_CONFIRMED")) return EImpactEventType::SAVE_CONFIRMED;
		if(!std::strcmp(pName, "FALL_CONFIRMED")) return EImpactEventType::FALL_CONFIRMED;
		if(!std::strcmp(pName, "ALED_CONFIRMED")) return EImpactEventType::ALED_CONFIRMED;
		if(!std::strcmp(pName, "UPDOWN_CONFIRMED")) return EImpactEventType::UPDOWN_CONFIRMED;
		if(!std::strcmp(pName, "MISSED_CATCH_CONFIRMED")) return EImpactEventType::MISSED_CATCH_CONFIRMED;
		if(!std::strcmp(pName, "TEAM_MISTAKE_CONFIRMED")) return EImpactEventType::TEAM_MISTAKE_CONFIRMED;
		if(!std::strcmp(pName, "TEAM_HARM_CONFIRMED")) return EImpactEventType::TEAM_HARM_CONFIRMED;
		if(!std::strcmp(pName, "PREVENTED_PROGRESS_LOSS_CONFIRMED")) return EImpactEventType::PREVENTED_PROGRESS_LOSS_CONFIRMED;
		if(!std::strcmp(pName, "RECOVERY_DIFFICULTY_INCREASE_CONFIRMED")) return EImpactEventType::RECOVERY_DIFFICULTY_INCREASE_CONFIRMED;
		if(!std::strcmp(pName, "JUMP_RESET_CONFIRMED")) return EImpactEventType::JUMP_RESET_CONFIRMED;
		if(!std::strcmp(pName, "FINISH_CONFIRMED")) return EImpactEventType::FINISH_CONFIRMED;
		if(!std::strcmp(pName, "WIPE_CONFIRMED")) return EImpactEventType::WIPE_CONFIRMED;
		if(!std::strcmp(pName, "TEAM_RESET_CONFIRMED")) return EImpactEventType::TEAM_RESET_CONFIRMED;
		if(!std::strcmp(pName, "MAP_CHANGE")) return EImpactEventType::MAP_CHANGE;
		if(!std::strcmp(pName, "SERVER_CHANGE")) return EImpactEventType::SERVER_CHANGE;
		if(!std::strcmp(pName, "CONNECTION_LOST")) return EImpactEventType::CONNECTION_LOST;
		if(!std::strcmp(pName, "PARTICIPANT_FINALIZED")) return EImpactEventType::PARTICIPANT_FINALIZED;
		return EImpactEventType::START_CROSSING;
	}

	inline const char *RecoveryNeedName(EImpactRecoveryNeed Need)
	{
		switch(Need)
		{
		case EImpactRecoveryNeed::UNKNOWN: return "UNKNOWN";
		case EImpactRecoveryNeed::SELF_SAFE: return "SELF_SAFE";
		case EImpactRecoveryNeed::EXTERNAL_REQUIRED: return "EXTERNAL_REQUIRED";
		}
		return "UNKNOWN";
	}

	inline EImpactRecoveryNeed RecoveryNeedFromName(const char *pName)
	{
		if(!pName)
			return EImpactRecoveryNeed::UNKNOWN;
		if(!std::strcmp(pName, "SELF_SAFE"))
			return EImpactRecoveryNeed::SELF_SAFE;
		if(!std::strcmp(pName, "EXTERNAL_REQUIRED"))
			return EImpactRecoveryNeed::EXTERNAL_REQUIRED;
		return EImpactRecoveryNeed::UNKNOWN;
	}

	inline const char *PhysicalOutcomeName(EImpactPhysicalOutcome Outcome)
	{
		switch(Outcome)
		{
		case EImpactPhysicalOutcome::UNKNOWN: return "UNKNOWN";
		case EImpactPhysicalOutcome::SAFE: return "SAFE";
		case EImpactPhysicalOutcome::HARMFUL: return "HARMFUL";
		}
		return "UNKNOWN";
	}

	inline EImpactPhysicalOutcome PhysicalOutcomeFromName(const char *pName)
	{
		if(!pName)
			return EImpactPhysicalOutcome::UNKNOWN;
		if(!std::strcmp(pName, "SAFE"))
			return EImpactPhysicalOutcome::SAFE;
		if(!std::strcmp(pName, "HARMFUL"))
			return EImpactPhysicalOutcome::HARMFUL;
		return EImpactPhysicalOutcome::UNKNOWN;
	}

	inline const char *JumpStateName(EImpactJumpState State)
	{
		switch(State)
		{
		case EImpactJumpState::UNKNOWN: return "UNKNOWN";
		case EImpactJumpState::AVAILABLE: return "AVAILABLE";
		case EImpactJumpState::UNAVAILABLE: return "UNAVAILABLE";
		}
		return "UNKNOWN";
	}

	inline const char *HarmCauseName(EImpactHarmCause Cause)
	{
		switch(Cause)
		{
		case EImpactHarmCause::UNKNOWN: return "UNKNOWN";
		case EImpactHarmCause::SELF_CAUSED: return "SELF_CAUSED";
		case EImpactHarmCause::TEAMMATE_CAUSED: return "TEAMMATE_CAUSED";
		case EImpactHarmCause::INTENTIONAL: return "INTENTIONAL";
		}
		return "UNKNOWN";
	}

	inline EImpactHarmCause HarmCauseFromName(const char *pName)
	{
		if(!pName)
			return EImpactHarmCause::UNKNOWN;
		if(!std::strcmp(pName, "SELF_CAUSED"))
			return EImpactHarmCause::SELF_CAUSED;
		if(!std::strcmp(pName, "TEAMMATE_CAUSED"))
			return EImpactHarmCause::TEAMMATE_CAUSED;
		if(!std::strcmp(pName, "INTENTIONAL"))
			return EImpactHarmCause::INTENTIONAL;
		return EImpactHarmCause::UNKNOWN;
	}

	inline const char *UnfreezeCauseName(EImpactUnfreezeCause Cause)
	{
		switch(Cause)
		{
		case EImpactUnfreezeCause::UNKNOWN: return "UNKNOWN";
		case EImpactUnfreezeCause::NATURAL: return "NATURAL";
		case EImpactUnfreezeCause::HAMMER: return "HAMMER";
		}
		return "UNKNOWN";
	}

	inline EImpactUnfreezeCause UnfreezeCauseFromName(const char *pName)
	{
		if(!pName)
			return EImpactUnfreezeCause::UNKNOWN;
		if(!std::strcmp(pName, "NATURAL"))
			return EImpactUnfreezeCause::NATURAL;
		if(!std::strcmp(pName, "HAMMER"))
			return EImpactUnfreezeCause::HAMMER;
		return EImpactUnfreezeCause::UNKNOWN;
	}

	inline const char *ImpactEpisodeStateName(EImpactEpisodeState State)
	{
		switch(State)
		{
		case EImpactEpisodeState::NONE: return "NONE";
		case EImpactEpisodeState::NORMAL: return "NORMAL";
		case EImpactEpisodeState::DANGER: return "DANGER";
		case EImpactEpisodeState::RECOVERED_SELF: return "RECOVERED_SELF";
		case EImpactEpisodeState::RECOVERED_BY_TEAM: return "RECOVERED_BY_TEAM";
		case EImpactEpisodeState::CONFIRMED_FALL: return "CONFIRMED_FALL";
		case EImpactEpisodeState::CANCELLED: return "CANCELLED";
		case EImpactEpisodeState::CONFIRMED_RECOVERY: return "CONFIRMED_RECOVERY";
		}
		return "NONE";
	}

	inline EImpactEpisodeState ImpactEpisodeStateFromName(const char *pName)
	{
		if(!pName) return EImpactEpisodeState::NONE;
		if(!std::strcmp(pName, "NORMAL")) return EImpactEpisodeState::NORMAL;
		if(!std::strcmp(pName, "DANGER")) return EImpactEpisodeState::DANGER;
		if(!std::strcmp(pName, "RECOVERED_SELF")) return EImpactEpisodeState::RECOVERED_SELF;
		if(!std::strcmp(pName, "RECOVERED_BY_TEAM")) return EImpactEpisodeState::RECOVERED_BY_TEAM;
		if(!std::strcmp(pName, "CONFIRMED_FALL")) return EImpactEpisodeState::CONFIRMED_FALL;
		if(!std::strcmp(pName, "CANCELLED")) return EImpactEpisodeState::CANCELLED;
		if(!std::strcmp(pName, "CONFIRMED_RECOVERY")) return EImpactEpisodeState::CONFIRMED_RECOVERY;
		return EImpactEpisodeState::NONE;
	}

	inline const char *CausalConfidenceName(ECausalConfidence Confidence)
	{
		switch(Confidence)
		{
		case ECausalConfidence::INSUFFICIENT: return "INSUFFICIENT";
		case ECausalConfidence::AMBIGUOUS: return "AMBIGUOUS";
		case ECausalConfidence::HIGH: return "HIGH";
		case ECausalConfidence::CONFIRMED: return "CONFIRMED";
		}
		return "INSUFFICIENT";
	}

	inline ECausalConfidence CausalConfidenceFromName(const char *pName)
	{
		if(!pName) return ECausalConfidence::INSUFFICIENT;
		if(!std::strcmp(pName, "AMBIGUOUS")) return ECausalConfidence::AMBIGUOUS;
		if(!std::strcmp(pName, "HIGH")) return ECausalConfidence::HIGH;
		if(!std::strcmp(pName, "CONFIRMED")) return ECausalConfidence::CONFIRMED;
		return ECausalConfidence::INSUFFICIENT;
	}

	inline const char *OwnershipStatusName(EOwnershipStatus Status)
	{
		switch(Status)
		{
		case EOwnershipStatus::NONE: return "NONE";
		case EOwnershipStatus::ACTOR: return "ACTOR";
		case EOwnershipStatus::AMBIGUOUS: return "AMBIGUOUS";
		}
		return "NONE";
	}

	inline EOwnershipStatus OwnershipStatusFromName(const char *pName)
	{
		if(!pName) return EOwnershipStatus::NONE;
		if(!std::strcmp(pName, "ACTOR")) return EOwnershipStatus::ACTOR;
		if(!std::strcmp(pName, "AMBIGUOUS")) return EOwnershipStatus::AMBIGUOUS;
		return EOwnershipStatus::NONE;
	}

	inline bool IsTerminalResult(ERunResult Result)
	{
		return Result != ERunResult::NONE;
	}

	inline void NormalizeEvent(SImpactEvent &Event)
	{
		if(Event.m_ActorName.size() > MAX_CONTEXT_LENGTH)
			Event.m_ActorName.resize(MAX_CONTEXT_LENGTH);
		if(Event.m_CausalReason.size() > MAX_EVENT_REASON_LENGTH)
			Event.m_CausalReason.resize(MAX_EVENT_REASON_LENGTH);
		if(Event.m_vTags.size() > MAX_EVENT_TAGS)
			Event.m_vTags.resize(MAX_EVENT_TAGS);
		for(std::string &Tag : Event.m_vTags)
			if(Tag.size() > MAX_EVENT_TAG_LENGTH)
				Tag.resize(MAX_EVENT_TAG_LENGTH);
		if(Event.m_BeforeContext.size() > MAX_CONTEXT_LENGTH)
			Event.m_BeforeContext.resize(MAX_CONTEXT_LENGTH);
		if(Event.m_AfterContext.size() > MAX_CONTEXT_LENGTH)
			Event.m_AfterContext.resize(MAX_CONTEXT_LENGTH);
		if(Event.m_vHelpers.size() > MAX_EVENT_HELPERS)
			Event.m_vHelpers.resize(MAX_EVENT_HELPERS);
	}

	inline void NormalizePendingEpisode(SImpactPendingEpisode &Episode)
	{
		// The episode state is the durable lifecycle state. Keep the legacy
		// diagnostic flag in sync when an already-confirmed Fall crosses a checkpoint
		// or a run/phase2 hand-off; a confirmed Fall must never look pending again.
		if(Episode.m_Type == EImpactEventType::FALL_CONFIRMED && Episode.m_State == EImpactEpisodeState::CONFIRMED_FALL)
			Episode.m_FallAlreadyConfirmed = true;
		if(Episode.m_Type == EImpactEventType::FALL_CONFIRMED && Episode.m_FallAlreadyConfirmed)
			Episode.m_State = EImpactEpisodeState::CONFIRMED_FALL;
		if(Episode.m_Type == EImpactEventType::FALL_CONFIRMED &&
			Episode.m_InitialRecoveryNeed == EImpactRecoveryNeed::UNKNOWN)
			Episode.m_InitialRecoveryNeed = Episode.m_RecoveryNeed;
		if(Episode.m_Type == EImpactEventType::FALL_CONFIRMED)
		{
			Episode.m_OriginSelfOriginEvidence |= Episode.m_HarmCause == EImpactHarmCause::SELF_CAUSED;
			Episode.m_OriginExternalCauseEvidence |= Episode.m_HarmCause == EImpactHarmCause::TEAMMATE_CAUSED;
		}
		if(Episode.m_BeforeContext.size() > MAX_CONTEXT_LENGTH)
			Episode.m_BeforeContext.resize(MAX_CONTEXT_LENGTH);
		if(Episode.m_AfterContext.size() > MAX_CONTEXT_LENGTH)
			Episode.m_AfterContext.resize(MAX_CONTEXT_LENGTH);
		if(Episode.m_DebugReason.size() > MAX_EVENT_REASON_LENGTH)
			Episode.m_DebugReason.resize(MAX_EVENT_REASON_LENGTH);
		if(Episode.m_vHelpers.size() > MAX_EVENT_HELPERS)
			Episode.m_vHelpers.resize(MAX_EVENT_HELPERS);
		if(Episode.m_CausalRecoveryProof.m_Reason.size() > MAX_EVENT_REASON_LENGTH)
			Episode.m_CausalRecoveryProof.m_Reason.resize(MAX_EVENT_REASON_LENGTH);
		if(Episode.m_CausalRecoveryProof.m_vContributors.size() > MAX_EVENT_HELPERS)
			Episode.m_CausalRecoveryProof.m_vContributors.resize(MAX_EVENT_HELPERS);
		if(!Episode.m_CausalRecoveryProof.m_Valid)
			Episode.m_CausalRecoveryProof = {};
	}
}

#endif
