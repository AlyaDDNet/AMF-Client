/*
 * Copyright (c) 2026 alya?
 * Original AMF Impact / Run Statistics code, developed for AMF Client.
 * Licensed under licenses/AMF-Impact-LICENSE.txt.
 */
#include "impact_system.h"
#include "impact_context_policy.h"

#include <base/log.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/engine.h>
#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>
#include <engine/shared/jobs.h>
#include <engine/serverbrowser.h>
#include <engine/storage.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>

#include <game/client/gameclient.h>
#include <game/client/skin.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/race.h>
#include <game/collision.h>
#include <game/teamscore.h>
#include <game/mapitems.h>
#include <game/gamecore.h>

namespace AmfImpact
{
	class CActivePersistenceQueue
	{
		struct SOperation
		{
			bool m_Remove = false;
			SRunSnapshot m_Snapshot;
			uint64_t m_NextRunId = 0;
			uint64_t m_NextEventId = 0;
		};
		class CDrainJob final : public IJob
		{
			std::shared_ptr<CLatestAsyncOperationQueue<SOperation>> m_pState;
			IStorage *m_pStorage;

		protected:
			void Run() override
			{
				while(true)
				{
					SOperation Operation;
					if(!m_pState->TakeNext(&Operation))
						return;

					const int64_t Started = time_get();
					if(Operation.m_Remove)
						CPersistence::RemoveActive(m_pStorage);
					else if(!CPersistence::SaveActive(m_pStorage, Operation.m_Snapshot, Operation.m_NextRunId, Operation.m_NextEventId))
						log_warn("amf-impact", "failed to persist active checkpoint run %llu", static_cast<unsigned long long>(Operation.m_Snapshot.m_RunId));
#if defined(CONF_DEBUG)
					const int64_t Duration = time_get() - Started;
					if(Duration >= time_freq() / 20)
						log_warn("amf-perf", "slow worker operation=impact.active_checkpoint_%s duration_ms=%.2f run_id=%llu",
							Operation.m_Remove ? "remove" : "write", static_cast<double>(Duration) * 1000.0 / time_freq(),
							static_cast<unsigned long long>(Operation.m_Snapshot.m_RunId));
#endif
				}
			}

		public:
			CDrainJob(std::shared_ptr<CLatestAsyncOperationQueue<SOperation>> pState, IStorage *pStorage) :
				m_pState(std::move(pState)),
				m_pStorage(pStorage)
			{
			}
		};

		IEngine *m_pEngine;
		IStorage *m_pStorage;
		std::shared_ptr<CLatestAsyncOperationQueue<SOperation>> m_pState = std::make_shared<CLatestAsyncOperationQueue<SOperation>>();

		void Enqueue(SOperation Operation)
		{
			// Only the latest desired active.json state remains pending. Operations
			// already taken by the worker finish before that pending state is applied.
			const bool ScheduleJob = m_pState->Enqueue(std::move(Operation));
			if(ScheduleJob)
				m_pEngine->AddJob(std::make_shared<CDrainJob>(m_pState, m_pStorage));
		}

	public:
		CActivePersistenceQueue(IEngine *pEngine, IStorage *pStorage) :
			m_pEngine(pEngine),
			m_pStorage(pStorage)
		{
		}

		void Save(SRunSnapshot SnapshotData, uint64_t NextRunId, uint64_t NextEventId)
		{
			SOperation Operation;
			Operation.m_Snapshot = std::move(SnapshotData);
			Operation.m_NextRunId = NextRunId;
			Operation.m_NextEventId = NextEventId;
			Enqueue(std::move(Operation));
		}

		void Remove()
		{
			SOperation Operation;
			Operation.m_Remove = true;
			Enqueue(std::move(Operation));
		}
	};

	static constexpr const char *KOG_MAP_METADATA_FILE = "amf_impact/kog_map_metadata.json";
	static constexpr const char *KOG_MAP_METADATA_TEMP_FILE = "amf_impact/kog_map_metadata.json.tmp";
	static constexpr const char *KOG_MAP_METADATA_BACKUP_FILE = "amf_impact/kog_map_metadata.json.bak";
	static constexpr int MAPINFO_REQUEST_TIMEOUT_SECONDS = 4;
	static constexpr int MAPINFO_RETRY_DELAY_SECONDS = 5;
	static constexpr size_t MAX_KOG_MAP_METADATA_CACHE_ENTRIES = 512;

	static void AttachForensicEventIds(SImpactPhase2Output &Output)
	{
		for(SImpactForensicEvent &Forensic : Output.m_vForensic)
		{
			if(Forensic.m_EventId != 0)
				continue;
			for(const SImpactPhase2Debug &Debug : Output.m_vDebug)
			{
				if(Debug.m_EventId == 0 || Debug.m_Target != Forensic.m_Target)
					continue;
				const bool FallMatch = Forensic.m_Type == EImpactForensicType::FALL_CONFIRMED &&
					Debug.m_DebugLabel == "FALL_CONFIRM" && Forensic.m_FallEpisodeId != 0 &&
					Forensic.m_FallEpisodeId == Debug.m_EpisodeId;
				const bool SaveMatch = (Forensic.m_Type == EImpactForensicType::SAVE_CONFIRMED ||
					Forensic.m_Type == EImpactForensicType::CLUTCH_CONFIRMED) &&
					Debug.m_DebugLabel == "SAVE_CONFIRMED" && Forensic.m_SaveCandidateId != 0 &&
					Forensic.m_SaveCandidateId == Debug.m_EpisodeId && Forensic.m_Actor == Debug.m_PotentialSaver;
				if(FallMatch || SaveMatch)
				{
					Forensic.m_EventId = Debug.m_EventId;
					break;
				}
			}
		}
	}

	static bool IsKogCommunity(const char *pCommunityId)
	{
		return pCommunityId != nullptr && str_comp_nocase(pCommunityId, "kog") == 0;
	}

	static const SRawAuthoritativeCharacterObservation &RawCurrentObservation(const CGameClient *pGameClient, int ClientId)
	{
		return pGameClient->m_RawAuthoritativeSnapshotAdapter.Current()[ClientId];
	}

	static const SRawAuthoritativeCharacterObservation &RawPreviousObservation(const CGameClient *pGameClient, int ClientId)
	{
		return pGameClient->m_RawAuthoritativeSnapshotAdapter.Previous()[ClientId];
	}

	struct SKogVoteRatingLookup
	{
		int m_OptionsTotal = 0;
		int m_ExactNameMatches = 0;
		bool m_Accepted = false;
		int m_Rating = -1;
		std::string m_RawOption;
		std::string m_Reason = "NO_EXACT_MAP_MATCH";
	};

	static std::string TrimKogVoteField(const std::string &Value)
	{
		size_t Begin = 0;
		while(Begin < Value.size() && std::isspace(static_cast<unsigned char>(Value[Begin])))
			++Begin;
		size_t End = Value.size();
		while(End > Begin && std::isspace(static_cast<unsigned char>(Value[End - 1])))
			--End;
		return Value.substr(Begin, End - Begin);
	}

	static bool KogVoteFirstFieldMatchesMap(const char *pOption, const char *pMapName)
	{
		if(pOption == nullptr || pMapName == nullptr || pOption[0] == '\0' || pMapName[0] == '\0')
			return false;
		const std::string Option(pOption);
		const size_t Separator = Option.find('|');
		return Separator != std::string::npos && TrimKogVoteField(Option.substr(0, Separator)) == pMapName;
	}

	static SKogVoteRatingLookup LookupKogVoteListRating(const CVoting &Voting, const char *pMapName)
	{
		SKogVoteRatingLookup Lookup;
		for(const CVoteOptionClient *pOption = Voting.FirstOption(); pOption != nullptr; pOption = pOption->m_pNext)
		{
			++Lookup.m_OptionsTotal;
			int Rating = -1;
			const char *pReason = nullptr;
			const bool Parsed = ParseKogVoteMapRating(pOption->m_aDescription, pMapName, &Rating, &pReason);
			if(KogVoteFirstFieldMatchesMap(pOption->m_aDescription, pMapName))
			{
				++Lookup.m_ExactNameMatches;
				if(!Parsed && Lookup.m_RawOption.empty())
				{
					Lookup.m_RawOption = pOption->m_aDescription;
					Lookup.m_Reason = pReason != nullptr ? pReason : "REJECTED";
				}
			}
			if(!Parsed)
				continue;
			Lookup.m_Accepted = true;
			Lookup.m_Rating = Rating;
			Lookup.m_RawOption = pOption->m_aDescription;
			Lookup.m_Reason = pReason != nullptr ? pReason : "ACCEPTED";
			return Lookup;
		}
		return Lookup;
	}

	static const json_value *MapMetadataField(const json_value *pObject, const char *pName)
	{
		return pObject != nullptr && pObject->type == json_object ? json_object_get(pObject, pName) : nullptr;
	}

	static bool ReadMapMetadataString(const json_value *pObject, const char *pName, std::string *pValue)
	{
		const json_value *pField = MapMetadataField(pObject, pName);
		if(pField == nullptr || pField->type != json_string || pValue == nullptr)
			return false;
		const char *pString = json_string_get(pField);
		if(pString == nullptr)
			return false;
		*pValue = pString;
		return true;
	}

	static bool WriteMapMetadataCacheAtomic(IStorage *pStorage, const std::string &Data)
	{
		if(pStorage == nullptr || !pStorage->CreateFolder("amf_impact", IStorage::TYPE_SAVE))
			return false;
		IOHANDLE File = pStorage->OpenFile(KOG_MAP_METADATA_TEMP_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(!File)
			return false;
		const bool Written = io_write(File, Data.data(), static_cast<unsigned>(Data.size())) == Data.size();
		const bool Synced = Written && io_sync(File) == 0 && io_error(File) == 0;
		const bool Closed = io_close(File) == 0;
		if(!Written || !Synced || !Closed)
		{
			pStorage->RemoveFile(KOG_MAP_METADATA_TEMP_FILE, IStorage::TYPE_SAVE);
			return false;
		}

		const bool HadTarget = pStorage->FileExists(KOG_MAP_METADATA_FILE, IStorage::TYPE_SAVE);
		if(HadTarget)
		{
			pStorage->RemoveFile(KOG_MAP_METADATA_BACKUP_FILE, IStorage::TYPE_SAVE);
			if(!pStorage->RenameFile(KOG_MAP_METADATA_FILE, KOG_MAP_METADATA_BACKUP_FILE, IStorage::TYPE_SAVE))
			{
				pStorage->RemoveFile(KOG_MAP_METADATA_TEMP_FILE, IStorage::TYPE_SAVE);
				return false;
			}
		}
		if(!pStorage->RenameFile(KOG_MAP_METADATA_TEMP_FILE, KOG_MAP_METADATA_FILE, IStorage::TYPE_SAVE))
		{
			if(HadTarget)
				pStorage->RenameFile(KOG_MAP_METADATA_BACKUP_FILE, KOG_MAP_METADATA_FILE, IStorage::TYPE_SAVE);
			pStorage->RemoveFile(KOG_MAP_METADATA_TEMP_FILE, IStorage::TYPE_SAVE);
			return false;
		}
		return true;
	}
	static bool IsValidImpactTeam(int TeamId)
	{
		return CImpactSystem::IsValidRunTeamId(TeamId);
	}

	static bool IsSoloMap(const CGameClient *pGameClient)
	{
		// These are existing authoritative game/map settings. Do not infer the
		// map category from its name. Older servers that provide neither setting
		// remain an unknown category and are handled by the existing gates.
		return g_Config.m_SvSoloServer != 0 ||
			(g_Config.m_SvTeam == SV_TEAM_FORCED_SOLO && pGameClient->m_GameInfo.m_DDRaceTeam);
	}

	static const char *ImpactPhase2DebugStageName(EImpactPhase2DebugStage Stage)
	{
		switch(Stage)
		{
		case EImpactPhase2DebugStage::CANDIDATE: return "candidate";
		case EImpactPhase2DebugStage::CONFIRMED: return "confirmed";
		case EImpactPhase2DebugStage::CANCELLED: return "cancelled";
		}
		return "unknown";
	}

	static const char *ImpactRawFreezeStateName(int State)
	{
		switch(State)
		{
		case 0: return "UNKNOWN";
		case 1: return "UNFROZEN";
		case 2: return "FROZEN";
		default: return "INITIAL";
		}
	}

	static const char *Phase2DebugEventTypeName(const SImpactPhase2Debug &Debug)
	{
		// The public event enum is shared by the legacy pending-episode container,
		// so a FALL_CONFIRMED value can still be present on a diagnostic record that
		// is only a candidate or an observation. Prefer the lifecycle label whenever
		// it carries a more precise meaning; diagnostics must not call an active
		// problem episode a confirmed event.
		if(Debug.m_DebugLabel == "SAVE_INTERACTION")
			return "SAVE_INTERACTION";
		if(Debug.m_DebugLabel == "SAVE_REJECTED" || Debug.m_DebugLabel == "SAVE_REJECTED_REFREEZE")
			return Debug.m_DebugLabel.c_str();
		if(Debug.m_DebugLabel == "SAVE_RESTORE")
			return "SAVE_RESTORE";
		if(Debug.m_DebugLabel == "SAVE_PENDING")
			return "SAVE_PENDING";
		if(Debug.m_DebugLabel == "SAVE_CONFIRMED")
			return "SAVE_CONFIRMED";
		if(Debug.m_DebugLabel == "PROBLEM_EPISODE")
			return "PROBLEM_EPISODE";
		if(Debug.m_DebugLabel == "FALL_TRACE")
			return "PROBLEM_EPISODE";
		if(Debug.m_DebugLabel == "FALL_NOT_EMITTED" ||
			(Debug.m_DebugLabel == "FALL_CLEANUP" && !Debug.m_FallAlreadyConfirmed))
			return "FALL_NO_FALL";
		if(Debug.m_DebugLabel == "FALL_EPISODE_CLOSED")
			return Debug.m_FallAlreadyConfirmed ? "FALL_CONFIRMED" : "FALL_NO_FALL";
		if(Debug.m_DebugLabel == "FALL_CONFIRM" || Debug.m_DebugLabel == "FALL_DUPLICATE_SUPPRESSED" ||
			(Debug.m_DebugLabel == "FALL_CLEANUP" && Debug.m_FallAlreadyConfirmed))
			return "FALL_CONFIRMED";
		return ImpactEventTypeName(Debug.m_EventType);
	}

	static std::string Phase2ContributorList(const std::vector<SParticipantId> &vContributors)
	{
		std::string Result;
		for(const SParticipantId &Contributor : vContributors)
		{
			if(!Result.empty())
				Result += ',';
			Result += std::to_string(Contributor.m_ClientId);
			Result += ':';
			Result += std::to_string(Contributor.m_SlotGeneration);
		}
		return Result.empty() ? "-" : Result;
	}

	static bool IsAledResearchRelevantLabel(const std::string &Label)
	{
		return Label == "PROBLEM_EPISODE" || Label == "FALL_TRACE" || Label == "SAVE_INTERACTION" ||
			Label == "SAVE_RESTORE" || Label == "SAVE_PENDING" || Label == "SAVE_CONFIRMED" ||
			Label == "SAVE_REJECTED" || Label == "SAVE_REJECTED_REFREEZE";
	}

	static bool IsAledResearchFreezeTile(int Tile)
	{
		return Tile == TILE_FREEZE || Tile == TILE_DFREEZE || Tile == TILE_LFREEZE;
	}

	static const char *AledGeometryProbeAxisName(int Axis)
	{
		return Axis == 1 ? "HORIZONTAL" : Axis == 2 ? "VERTICAL" : Axis == 3 ? "MIXED" : "UNKNOWN";
	}

	static const char *AledGeometryProbeSideName(int Side)
	{
		return Side == 1 ? "NEGATIVE" : Side == 2 ? "POSITIVE" : Side == 3 ? "ON_BARRIER" : "UNKNOWN";
	}

	static const char *AledGeometryV2TopologyName(CImpactSystem::EAledGeometryV2Topology Topology)
	{
		switch(Topology)
		{
		case CImpactSystem::EAledGeometryV2Topology::VERTICAL: return "VERTICAL";
		case CImpactSystem::EAledGeometryV2Topology::HORIZONTAL: return "HORIZONTAL";
		case CImpactSystem::EAledGeometryV2Topology::DIAGONAL: return "DIAGONAL";
		case CImpactSystem::EAledGeometryV2Topology::AMBIGUOUS: return "AMBIGUOUS";
		default: return "UNKNOWN";
		}
	}

	static const char *AledGeometryV2StatusName(CImpactSystem::EAledGeometryV2Status Status)
	{
		switch(Status)
		{
		case CImpactSystem::EAledGeometryV2Status::VALID: return "VALID";
		case CImpactSystem::EAledGeometryV2Status::AMBIGUOUS: return "AMBIGUOUS";
		case CImpactSystem::EAledGeometryV2Status::INSUFFICIENT_EVIDENCE: return "INSUFFICIENT_EVIDENCE";
		default: return "SKIPPED";
		}
	}

	static const char *AledGeometryV2DiagonalOrientationName(int Orientation)
	{
		return Orientation == 1 ? "NW_SE" : Orientation == 2 ? "NE_SW" : "UNKNOWN";
	}

	static bool IsAledResearchRawPositionUsable(const SRawAuthoritativeCharacterObservation &Raw, bool Previous)
	{
		return Raw.m_Valid && (Previous ? Raw.m_FromRawPreviousSnapshot : Raw.m_FromRawCurrentSnapshot) && Raw.m_Present &&
			Raw.m_Alive && Raw.m_SnapshotServerTick >= 0 && Raw.m_ObjectTick >= 0;
	}

	static bool IsAledResearchRawFrozenAtSample(const SRawAuthoritativeCharacterObservation &Raw)
	{
		if(!Raw.m_HasExtendedData || Raw.m_SnapshotServerTick < 0)
			return false;
		const bool DeepFrozen = Raw.m_ExtendedData.m_FreezeEnd == -1;
		const bool LiveFrozen = (Raw.m_ExtendedData.m_Flags & CHARACTERFLAG_MOVEMENTS_DISABLED) != 0;
		const bool OrdinaryFrozen = Raw.m_ExtendedData.m_FreezeEnd > Raw.m_SnapshotServerTick && Raw.m_ExtendedData.m_FreezeEnd > 0;
		return DeepFrozen || LiveFrozen || OrdinaryFrozen;
	}

	static bool IsAledResearchPositionValid(const vec2 &Position)
	{
		return CImpactSystem::IsAledResearchPositionFinite(Position.x, Position.y);
	}

	static const char *AledResearchPhysicalSampleSourceName(const CImpactSystem::SAledResearchPhysicalSample &Sample)
	{
		return Sample.m_NativeSnapshotReckoned ? "DDNET_SNAPSHOT_RECKONED" : "RAW_AUTHORITATIVE_EXACT";
	}

	static bool IsAledResearchGeometryPositionSourceUsable(const std::string &Source)
	{
		return Source == "DDNET_SNAPSHOT_RECKONED" || Source == "RAW_AUTHORITATIVE_EXACT" || Source == "RAW_AUTHORITATIVE";
	}

	static const char *Phase2FallDecisionResult(const SImpactPhase2Debug &Debug)
	{
		if(Debug.m_DebugLabel == "FALL_CONFIRM" || Debug.m_FallAlreadyConfirmed)
			return "CONFIRMED";
		if(Debug.m_DebugLabel == "FALL_NOT_EMITTED" || Debug.m_FallSuppressed ||
			(Debug.m_DebugLabel == "FALL_CLEANUP" && !Debug.m_ConfirmedFall))
			return "NO_FALL";
		return "ACTIVE";
	}

	static const char *Phase2FallDecisionBasis(const SImpactPhase2Debug &Debug)
	{
		if(Debug.m_TeammateCause || Debug.m_ExternalCauseEvidence)
			return "PRE_FALL_TEAMMATE_CAUSE";
		if(Debug.m_Reset)
			return "RESET";
		// A closed episode carries its concrete reason even when the compatibility
		// physical-outcome field is intentionally UNKNOWN. Do not infer ALREADY_SAFE
		// from a blanket closure assignment: only the explicit positive reason may
		// describe the no-Fall result.
		if(Debug.m_Reason == "NO_FALL_SELF_SAFE")
			return "ALREADY_SAFE";
		if(Debug.m_Reason == "NO_FALL_SELF_RECOVERY")
			return "SELF_RECOVERY";
		// HARMFUL is retained on the normalized frame for compatibility/debug
		// readers, but the raw Fall resolver emits only after the complete physical
		// problem predicate has passed. Do not present that metadata as a separate
		// Fall producer in the live trace.
		if(Debug.m_DebugLabel == "FALL_CONFIRM" &&
			(Debug.m_Reason.find("no autonomous recovery") != std::string::npos ||
				Debug.m_Reason.find("frozen problem") != std::string::npos))
			return "FALL";
		// A compatibility HARMFUL label is not a raw Fall producer. If the
		// complete physical problem predicate confirmed a Fall, the branch above
		// already reports FALL; otherwise keep this diagnostic from inventing a
		// direct-harm basis that the raw resolver never used.
		if(Debug.m_PhysicalOutcome == EImpactPhysicalOutcome::SAFE || Debug.m_AutonomousRecoveryPossible)
		{
			if(Debug.m_Reason.find("recovered") != std::string::npos || Debug.m_Reason.find("unfreeze") != std::string::npos)
				return "SELF_RECOVERY";
			return "ALREADY_SAFE";
		}
		return Debug.m_DebugLabel == "PROBLEM_EPISODE" ? "ACTIVE_PROBLEM" : "UNCLASSIFIED";
	}

	static const char *Phase2SaveDecisionResult(const SImpactPhase2Debug &Debug)
	{
		if(Debug.m_DebugLabel == "SAVE_CONFIRMED" && Debug.m_Stage == EImpactPhase2DebugStage::CONFIRMED)
			return "CONFIRMED";
		if(Debug.m_DebugLabel == "SAVE_REJECTED" || Debug.m_DebugLabel == "SAVE_REJECTED_REFREEZE")
			return "REJECTED";
		if(Debug.m_DebugLabel == "SAVE_RESTORE" || Debug.m_DebugLabel == "SAVE_PENDING")
			return "PENDING";
		return "ACTIVE";
	}

	static bool IsSafeImpactTile(int Tile)
	{
		return Tile != TILE_DEATH && Tile != TILE_FREEZE && Tile != TILE_DFREEZE && Tile != TILE_LFREEZE;
	}

	static bool IsSafeImpactPosition(const CGameClient *pGameClient, const vec2 &Pos)
	{
		const float HalfSize = CCharacterCore::PhysicalSize() * 0.5f;
		const CCollision *pCollision = pGameClient->Collision();
		if(pCollision->TestBox(Pos, CCharacterCore::PhysicalSizeVec2()))
			return false;

		const vec2 aSamplePoints[] = {
			Pos,
			Pos + vec2(-HalfSize, -HalfSize),
			Pos + vec2(HalfSize, -HalfSize),
			Pos + vec2(-HalfSize, HalfSize),
			Pos + vec2(HalfSize, HalfSize),
		};
		for(const vec2 &SamplePos : aSamplePoints)
		{
			const int X = round_to_int(SamplePos.x);
			const int Y = round_to_int(SamplePos.y);
			if(!IsSafeImpactTile(pCollision->GetTile(X, Y)) || !IsSafeImpactTile(pCollision->GetFrontTile(X, Y)))
				return false;
		}
		return pCollision->IsOnGround(Pos, CCharacterCore::PhysicalSize());
	}

	static bool IsDeathImpactPosition(const CGameClient *pGameClient, const vec2 &Pos)
	{
		const float HalfSize = CCharacterCore::PhysicalSize() * 0.5f;
		const CCollision *pCollision = pGameClient->Collision();
		const vec2 aSamplePoints[] = {
			Pos,
			Pos + vec2(-HalfSize, -HalfSize),
			Pos + vec2(HalfSize, -HalfSize),
			Pos + vec2(-HalfSize, HalfSize),
			Pos + vec2(HalfSize, HalfSize),
		};
		for(const vec2 &SamplePos : aSamplePoints)
		{
			const int X = round_to_int(SamplePos.x);
			const int Y = round_to_int(SamplePos.y);
			if(pCollision->GetTile(X, Y) == TILE_DEATH || pCollision->GetFrontTile(X, Y) == TILE_DEATH)
				return true;
		}
		return false;
	}

	static bool HasStableDynamicTeeSupport(const CGameClient *pGameClient, int TargetId, int TeamId, int64_t ServerTick)
	{
		if(pGameClient == nullptr || TargetId < 0 || TargetId >= MAX_CLIENTS ||
			!IsValidImpactTeam(TeamId) || pGameClient->m_Teams.Team(TargetId) != TeamId)
			return false;

		const auto &TargetCharacter = pGameClient->m_Snap.m_aCharacters[TargetId];
		const auto &TargetRawCurrent = RawCurrentObservation(pGameClient, TargetId);
		const auto &TargetRawPrevious = RawPreviousObservation(pGameClient, TargetId);
		if(!TargetRawCurrent.m_FromRawCurrentSnapshot || !TargetRawPrevious.m_FromRawPreviousSnapshot ||
			!TargetRawCurrent.m_HasExtendedData || !TargetRawPrevious.m_HasExtendedData)
			return false;

		// Dynamic support is only an ordinary-freeze outcome. A finite, still-active
		// timer and an unchanged freeze start prove that the timer is running rather
		// than being refreshed by a freeze tile. The comparison uses the authoritative
		// DDNet end tick, not a new gameplay timeout.
		const CNetObj_DDNetCharacter &TargetExtended = TargetRawCurrent.m_ExtendedData;
		const CNetObj_DDNetCharacter &PreviousTargetExtended = TargetRawPrevious.m_ExtendedData;
		if(TargetExtended.m_FreezeEnd <= ServerTick || TargetExtended.m_FreezeEnd <= 0 ||
			PreviousTargetExtended.m_FreezeEnd <= 0 || TargetExtended.m_FreezeEnd > PreviousTargetExtended.m_FreezeEnd ||
			TargetExtended.m_FreezeStart < 0 || PreviousTargetExtended.m_FreezeStart != TargetExtended.m_FreezeStart)
			return false;

		// These are the same collision enablement sources used by the live character
		// core. The map's current tuning is the authoritative player-collision rule;
		// solo/collision-disabled/team barriers are applied per character.
		const bool PlayerCollision = pGameClient->m_GameWorld.GetTuning(0)->m_PlayerCollision != 0;
		if(!PlayerCollision)
			return false;

		const CGameClient::CClientData &TargetData = pGameClient->m_aClients[TargetId];
		const vec2 TargetPos(TargetRawCurrent.m_Character.m_X, TargetRawCurrent.m_Character.m_Y);
		const vec2 PreviousTargetPos(TargetCharacter.m_Prev.m_X, TargetCharacter.m_Prev.m_Y);
		const float PhysicalSize = CCharacterCore::PhysicalSize();
		for(int OtherId = 0; OtherId < MAX_CLIENTS; ++OtherId)
		{
			if(OtherId == TargetId || pGameClient->m_Teams.Team(OtherId) != TeamId)
				continue;
			const auto &OtherCharacter = pGameClient->m_Snap.m_aCharacters[OtherId];
			const auto &OtherRawCurrent = RawCurrentObservation(pGameClient, OtherId);
			const auto &OtherRawPrevious = RawPreviousObservation(pGameClient, OtherId);
			if(!OtherRawCurrent.m_FromRawCurrentSnapshot || !OtherRawPrevious.m_FromRawPreviousSnapshot)
				continue;

			const CGameClient::CClientData &OtherData = pGameClient->m_aClients[OtherId];
			const bool SuperCollision = TargetData.m_Super || OtherData.m_Super;
			if((!SuperCollision && !pGameClient->m_Teams.CanCollide(TargetId, OtherId)) ||
				(!SuperCollision && (TargetData.m_Solo || OtherData.m_Solo || TargetData.m_CollisionDisabled || OtherData.m_CollisionDisabled)))
				continue;

			const vec2 OtherPos(OtherRawCurrent.m_Character.m_X, OtherRawCurrent.m_Character.m_Y);
			const vec2 PreviousOtherPos(OtherCharacter.m_Prev.m_X, OtherCharacter.m_Prev.m_Y);
			// CCharacterCore::Move stops a character at D < PhysicalSize(). Requiring
			// that exact contact in both authoritative snapshots excludes a Tee that is
			// merely nearby or crosses the target for one snapshot.
			if(distance(TargetPos, OtherPos) >= PhysicalSize || distance(PreviousTargetPos, PreviousOtherPos) >= PhysicalSize)
				continue;

			const vec2 CurrentRelativePosition = TargetPos - OtherPos;
			const vec2 PreviousRelativePosition = PreviousTargetPos - PreviousOtherPos;
			const vec2 RelativeMovement = CurrentRelativePosition - PreviousRelativePosition;
			const vec2 RelativeVelocity(
				TargetRawCurrent.m_Character.m_VelX - OtherRawCurrent.m_Character.m_VelX,
				TargetRawCurrent.m_Character.m_VelY - OtherRawCurrent.m_Character.m_VelY);
			// Stable relative position is the observed support/stationary state. If
			// quantized positions remain at contact while velocities differ, require the
			// velocity to be non-closing instead of treating a pass-by as support.
			const bool RelativePositionStable = RelativeMovement == vec2(0, 0);
			const bool RelativeVelocityStable = RelativeVelocity == vec2(0, 0);
			const bool RelativeVelocityNonClosing = dot(CurrentRelativePosition, RelativeVelocity) >= 0.0f;
			if(RelativePositionStable && (RelativeVelocityStable || RelativeVelocityNonClosing))
				return true;
		}
		return false;
	}

	static bool HasImpactTileAtPosition(const CGameClient *pGameClient, const vec2 &Pos, int Tile)
	{
		const float HalfSize = CCharacterCore::PhysicalSize() * 0.5f;
		const CCollision *pCollision = pGameClient->Collision();
		const vec2 aSamplePoints[] = {
			Pos,
			Pos + vec2(-HalfSize, -HalfSize),
			Pos + vec2(HalfSize, -HalfSize),
			Pos + vec2(-HalfSize, HalfSize),
			Pos + vec2(HalfSize, HalfSize),
		};
		for(const vec2 &SamplePos : aSamplePoints)
		{
			const int X = round_to_int(SamplePos.x);
			const int Y = round_to_int(SamplePos.y);
			if(pCollision->GetTile(X, Y) == Tile || pCollision->GetFrontTile(X, Y) == Tile)
				return true;
		}
		return false;
	}

	static bool HasImpactCenterTileAtPosition(const CGameClient *pGameClient, const vec2 &Pos, int Tile)
	{
		const CCollision *pCollision = pGameClient->Collision();
		const int MapIndex = pCollision->GetPureMapIndex(Pos);
		return pCollision->GetTileIndex(MapIndex) == Tile || pCollision->GetFrontTileIndex(MapIndex) == Tile;
	}

	struct SImpactTeleOutputClassification
	{
		bool m_Safe = false;
		bool m_Harmful = false;
		bool m_ClearsDeepFreeze = false;
	};

	static SImpactTeleOutputClassification ClassifyPositionalTeleOutput(const CGameClient *pGameClient, const vec2 &Pos, bool DeepFrozen)
	{
		SImpactTeleOutputClassification Classification;
		Classification.m_Harmful = IsDeathImpactPosition(pGameClient, Pos);
		if(Classification.m_Harmful)
			return Classification;

		Classification.m_ClearsDeepFreeze = HasImpactCenterTileAtPosition(pGameClient, Pos, TILE_DUNFREEZE);
		if(DeepFrozen)
		{
			// An ordinary platform or ordinary UNFREEZE tile cannot clear a deep
			// freeze. DUNFREEZE is the authoritative map mechanic that can do so.
			Classification.m_Safe = Classification.m_ClearsDeepFreeze;
			return Classification;
		}

		// A direct UNFREEZE/DUNFREEZE output is safe even before the next
		// authoritative character snapshot reports the cleared freeze bit. A
		// stable non-freeze platform is also safe for an ordinary frozen target.
		Classification.m_Safe = HasImpactCenterTileAtPosition(pGameClient, Pos, TILE_UNFREEZE) ||
			Classification.m_ClearsDeepFreeze || IsSafeImpactPosition(pGameClient, Pos);
		return Classification;
	}

	struct SImpactPositionalTeleIn
	{
		EImpactPositionalTeleportType m_Type = EImpactPositionalTeleportType::NONE;
		int m_Number = 0;
		bool m_Ambiguous = false;
	};

	static bool FindPositionalTeleIn(const CGameClient *pGameClient, const vec2 &PreviousPos, const vec2 &CurrentPos, SImpactPositionalTeleIn *pResult)
	{
		if(pResult == nullptr)
			return false;
		const CCollision *pCollision = pGameClient->Collision();
		SImpactPositionalTeleIn Found;
		const bool NormalCharacterTeleportEnabled = !g_Config.m_SvOldTeleportHook && !g_Config.m_SvOldTeleportWeapons;
		auto ConsiderMapIndex = [&](int MapIndex) {
			if(MapIndex < 0)
				return;
			int Number = NormalCharacterTeleportEnabled ? pCollision->IsTeleport(MapIndex) : 0;
			EImpactPositionalTeleportType Type = Number > 0 ? EImpactPositionalTeleportType::NORMAL : EImpactPositionalTeleportType::NONE;
			if(Number <= 0)
			{
				Number = pCollision->IsEvilTeleport(MapIndex);
				if(Number > 0)
					Type = EImpactPositionalTeleportType::EVIL;
			}
			if(Number <= 0)
				return;
			if(Found.m_Number == 0)
			{
				Found.m_Number = Number;
				Found.m_Type = Type;
			}
			else if(Found.m_Number != Number || Found.m_Type != Type)
				Found.m_Ambiguous = true;
		};

		// GetMapIndices follows the same local movement segment used by the
		// collision code, so a tele-in crossed between snapshots is not lost just
		// because the previous snapshot was one tile before the entry.
		for(const int MapIndex : pCollision->GetMapIndices(PreviousPos, CurrentPos))
			ConsiderMapIndex(MapIndex);
		// Include stationary/empty-map-index endpoint cases as well. These are
		// exact map lookups, not a coordinate-distance heuristic.
		ConsiderMapIndex(pCollision->GetPureMapIndex(PreviousPos));
		ConsiderMapIndex(pCollision->GetPureMapIndex(CurrentPos));
		if(Found.m_Ambiguous || Found.m_Number <= 0)
			return false;
		*pResult = Found;
		return true;
	}

	static bool HasImpactSlotObservation(const CGameClient *pGameClient, int ClientId)
	{
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			return false;
		const auto &RawCurrent = RawCurrentObservation(pGameClient, ClientId);
		const auto &RawPrevious = RawPreviousObservation(pGameClient, ClientId);
		// PlayerInfo and Character are independent parts of a snapshot. Either
		// one is enough to keep the slot/generation observable for Impact. In
		// particular, a current character must not be discarded merely because a
		// sparse snapshot omitted PlayerInfo or the previous character object.
		return CImpactSystem::IsParticipantSlotObserved(
			pGameClient->m_Snap.m_apPlayerInfos[ClientId] != nullptr,
			RawCurrent.m_FromRawCurrentSnapshot,
			RawPrevious.m_FromRawPreviousSnapshot);
	}

	static bool IsImpactSpectator(const CGameClient *pGameClient, int ClientId)
	{
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			return false;
		const CNetObj_PlayerInfo *pInfo = pGameClient->m_Snap.m_apPlayerInfos[ClientId];
		const bool PlayerInfoSpectator = pInfo != nullptr && pInfo->m_Team == TEAM_SPECTATORS;
		const bool CachedSpectator = pGameClient->m_aClients[ClientId].m_Team == TEAM_SPECTATORS;
		const bool CurrentTeeKnown = RawCurrentObservation(pGameClient, ClientId).m_FromRawCurrentSnapshot ||
			pGameClient->m_aClients[ClientId].m_SpecCharPresent;
		return CImpactSystem::IsImpactSpectatorObservation(pInfo != nullptr, PlayerInfoSpectator,
			CurrentTeeKnown, CachedSpectator);
	}

	static EImpactRecoveryNeed ClassifyLiveRecoveryNeed(const CGameClient *pGameClient, const SImpactGameplayInput &Input)
	{
		return CImpactSystem::ClassifyRecoveryNeedFromAuthoritativeState(Input,
			Input.m_SafeCollisionPositionKnown && Input.m_SafeCollisionPosition);
	}

	static std::vector<SImpactGameplayFrame> BuildGameplayFrames(const CGameClient *pGameClient, const CImpactRun &Run, const std::array<uint32_t, MAX_CLIENTS> &aSlotGeneration, int64_t ServerTick)
	{
		std::vector<SImpactGameplayFrame> vFrames;
		vFrames.reserve(MAX_CLIENTS);
		const int RunTeamId = Run.Context().m_TeamId;
		for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
		{
			if(aSlotGeneration[ClientId] == 0 || !HasImpactSlotObservation(pGameClient, ClientId) ||
				IsImpactSpectator(pGameClient, ClientId) || pGameClient->m_Teams.Team(ClientId) != RunTeamId)
				continue;

			const bool IsDummy = ClientId == pGameClient->m_aLocalIds[1] && ClientId != pGameClient->m_aLocalIds[0];
			const SParticipantId Participant{ClientId, aSlotGeneration[ClientId], IsDummy};
			if(Run.IsParticipantFinalized(Participant))
				continue;
			const auto &Character = pGameClient->m_Snap.m_aCharacters[ClientId];
			const auto &RawCurrent = RawCurrentObservation(pGameClient, ClientId);
			const auto &RawPrevious = RawPreviousObservation(pGameClient, ClientId);
			const auto &ClientData = pGameClient->m_aClients[ClientId];
			const CNetObj_Character &ServerCharacter = RawCurrent.m_Character;
			SImpactGameplayInput Input;
			Input.m_Participant = Participant;
			Input.m_TeamId = RunTeamId;
			Input.m_ServerTick = ServerTick;
			Input.m_Present = true;
			Input.m_Confirmed = true;
			Input.m_CurrentCharacterKnown = RawCurrent.m_FromRawCurrentSnapshot;
			Input.m_PreviousCharacterKnown = RawPrevious.m_FromRawPreviousSnapshot;
			Input.m_PhysicallyPausedCharacter = ClientData.m_SpecCharPresent;
			Input.m_PositionKnown = RawCurrent.m_FromRawCurrentSnapshot;
			Input.m_VelocityKnown = RawCurrent.m_FromRawCurrentSnapshot;
			Input.m_AliveKnown = RawCurrent.m_FromRawCurrentSnapshot;
			Input.m_Alive = RawCurrent.m_FromRawCurrentSnapshot;
			Input.m_SoloKnown = RawCurrent.m_HasExtendedData && RawCurrent.m_FromRawCurrentSnapshot;
			Input.m_Solo = pGameClient->m_Teams.GetSolo(ClientId);
			Input.m_Eligible = Run.HasParticipant(Input.m_Participant);
			Input.m_Engaged = Run.IsParticipantEngaged(Input.m_Participant);
			Input.m_Finalized = Run.IsParticipantFinalized(Input.m_Participant);
			Input.m_Finished = ClientData.m_FinishTimeSeconds >= 0;
			// DDNetPlayer exposes a score/best-time presentation field, not the
			// current team-finish message. Do not use it as authoritative finish
			// duration; the live finish producer consumes SV_RACEFINISH below.
			Input.m_FinishTimeKnown = false;
			Input.m_FinishTimeMillis = -1;
			// Freeze state is authoritative only while the current snapshot carries
			// the DDNet character extension. Missing extension data is UNKNOWN.
			Input.m_FreezeKnown = RawCurrent.m_HasExtendedData && RawCurrent.m_FromRawCurrentSnapshot;
			Input.m_FreezeEnd = Input.m_FreezeKnown ? ClientData.m_FreezeEnd : 0;
			Input.m_FreezeStart = Input.m_FreezeKnown ? RawCurrent.m_ExtendedData.m_FreezeStart : -1;
			Input.m_DeepFrozen = Input.m_FreezeKnown && ClientData.m_DeepFrozen;
			Input.m_LiveFrozen = Input.m_FreezeKnown && ClientData.m_LiveFrozen;
			Input.m_Frozen = Input.m_FreezeKnown && (Input.m_DeepFrozen || Input.m_LiveFrozen || Input.m_FreezeEnd > ServerTick);
			Input.m_X = ServerCharacter.m_X;
			Input.m_Y = ServerCharacter.m_Y;
			Input.m_VelX = ServerCharacter.m_VelX;
			Input.m_VelY = ServerCharacter.m_VelY;
			Input.m_SupportKnown = Input.m_PositionKnown;
			Input.m_Supported = Input.m_SupportKnown && pGameClient->Collision()->IsOnGround(
				vec2(Input.m_X, Input.m_Y), CCharacterCore::PhysicalSize());
			Input.m_DynamicSupportKnown = Input.m_CurrentCharacterKnown && Input.m_PreviousCharacterKnown &&
				RawCurrent.m_HasExtendedData && RawPrevious.m_HasExtendedData;
			Input.m_DynamicSupport = Input.m_DynamicSupportKnown &&
				HasStableDynamicTeeSupport(pGameClient, ClientId, RunTeamId, ServerTick);
			Input.m_SafeCollisionPositionKnown = Input.m_PositionKnown;
			Input.m_SafeCollisionPosition = Input.m_SafeCollisionPositionKnown && IsSafeImpactPosition(pGameClient, vec2(Input.m_X, Input.m_Y));
			Input.m_OrdinaryFreezeTileKnown = Input.m_PositionKnown;
			Input.m_OrdinaryFreezeTile = Input.m_OrdinaryFreezeTileKnown &&
				HasImpactTileAtPosition(pGameClient, vec2(Input.m_X, Input.m_Y), TILE_FREEZE);
			Input.m_HarmfulOutcomeCandidateKnown = Input.m_PositionKnown;
			Input.m_HarmfulOutcomeCandidate = Input.m_HarmfulOutcomeCandidateKnown && IsDeathImpactPosition(pGameClient, vec2(Input.m_X, Input.m_Y));
			// Resolve only character positional TELEIN/TELEINEVIL. Their numbered
			// outputs are authoritative map data and are intentionally separate from
			// TELEINWEAPON, TELEINHOOK and checkpoint teleports.
			if(Input.m_PreviousCharacterKnown)
			{
				SImpactPositionalTeleIn TeleIn;
				const vec2 PreviousPos(Character.m_Prev.m_X, Character.m_Prev.m_Y);
				const vec2 CurrentPos(Input.m_X, Input.m_Y);
				if(FindPositionalTeleIn(pGameClient, PreviousPos, CurrentPos, &TeleIn))
				{
					Input.m_TeleportType = TeleIn.m_Type;
					Input.m_TeleportNumber = TeleIn.m_Number;
					const std::vector<vec2> &vTeleOuts = pGameClient->Collision()->TeleOuts(TeleIn.m_Number - 1);
					if(!vTeleOuts.empty())
					{
						Input.m_TeleportOutputSetKnown = true;
						bool AllSafe = true;
						bool AllHarmful = true;
						bool AllClearDeepFreeze = true;
						for(const vec2 &TeleOut : vTeleOuts)
						{
							const SImpactTeleOutputClassification Classification = ClassifyPositionalTeleOutput(pGameClient, TeleOut, Input.m_DeepFrozen);
							AllSafe &= Classification.m_Safe;
							AllHarmful &= Classification.m_Harmful;
							AllClearDeepFreeze &= Classification.m_ClearsDeepFreeze;
						}
						Input.m_TeleportOutputSetAllSafe = AllSafe;
						Input.m_TeleportOutputSetAllHarmful = AllHarmful;
						Input.m_TeleportOutputSetClearsDeepFreeze = AllClearDeepFreeze;
						if(vTeleOuts.size() == 1)
						{
							const SImpactTeleOutputClassification Classification = ClassifyPositionalTeleOutput(pGameClient, vTeleOuts.front(), Input.m_DeepFrozen);
							Input.m_TeleportDestinationKnown = true;
							Input.m_TeleportDestinationSafe = Classification.m_Safe;
							Input.m_TeleportDestinationHarmful = Classification.m_Harmful;
							Input.m_TeleportDestinationClearsDeepFreeze = Classification.m_ClearsDeepFreeze;
						}

						// Once the authoritative current position is on one of the output
						// tiles, resolve the actual destination. For duplicate outputs on
						// one tile the physical outcome is identical, so tile identity is
						// sufficient and does not select a random output.
						const int CurrentMapIndex = pGameClient->Collision()->GetPureMapIndex(CurrentPos);
						for(const vec2 &TeleOut : vTeleOuts)
						{
							if(pGameClient->Collision()->GetPureMapIndex(TeleOut) != CurrentMapIndex)
								continue;
							const SImpactTeleOutputClassification Classification = ClassifyPositionalTeleOutput(pGameClient, CurrentPos, Input.m_DeepFrozen);
							Input.m_TeleportDestinationKnown = true;
							Input.m_TeleportDestinationSafe = Classification.m_Safe;
							Input.m_TeleportDestinationHarmful = Classification.m_Harmful;
							Input.m_TeleportDestinationClearsDeepFreeze = Classification.m_ClearsDeepFreeze;
							break;
						}
					}
				}
			}
			Input.m_HookKnown = RawCurrent.m_FromRawCurrentSnapshot;
			Input.m_HookedPlayer = ServerCharacter.m_HookedPlayer;
			Input.m_HookState = ServerCharacter.m_HookState;
			Input.m_AttackKnown = RawCurrent.m_FromRawCurrentSnapshot;
			Input.m_AttackTick = ServerCharacter.m_AttackTick;
			Input.m_Weapon = ServerCharacter.m_Weapon;
			if(RawCurrent.m_HasExtendedData)
			{
				Input.m_TargetX = RawCurrent.m_ExtendedData.m_TargetX;
				Input.m_TargetY = RawCurrent.m_ExtendedData.m_TargetY;
			}
			const bool GroundedKnown = Input.m_SupportKnown;
			const bool EndlessJump = RawCurrent.m_HasExtendedData &&
				(RawCurrent.m_ExtendedData.m_Flags & CHARACTERFLAG_ENDLESS_JUMP) != 0;
			Input.m_JumpState = CImpactSystem::ClassifyUsableJumpStateFromAuthoritativeState(
				Input.m_CurrentCharacterKnown,
				Input.m_AliveKnown,
				Input.m_Alive,
				Input.m_FreezeKnown,
				Input.m_Frozen,
				RawCurrent.m_HasExtendedData,
				RawCurrent.m_HasExtendedData ? RawCurrent.m_ExtendedData.m_Jumps : 0,
				ServerCharacter.m_Jumped,
				GroundedKnown,
				Input.m_Supported,
				EndlessJump);
			bool DownwardShot = false;
			Input.m_DownwardShotKnown = CImpactSystem::ClassifySelfDownwardShotFromAuthoritativeState(
				Input.m_CurrentCharacterKnown,
				Input.m_PreviousCharacterKnown,
				Input.m_AttackKnown,
				Input.m_AttackTick,
				Input.m_PreviousCharacterKnown ? Character.m_Prev.m_AttackTick : -1,
				RawCurrent.m_HasExtendedData && Input.m_CurrentCharacterKnown,
				Input.m_TargetY,
				Input.m_Weapon,
				&DownwardShot);
			Input.m_DownwardShot = Input.m_DownwardShotKnown && DownwardShot;
			// DDNet 19.9 does not expose whether a freeze entry was self-initiated
			// or caused by an unobserved world/teammate interaction. Keep this
			// evidence UNKNOWN; the Phase 2 producer must not infer intent.
			Input.m_SelfInitiatedFreezeKnown = false;
			Input.m_SelfInitiatedFreeze = false;
			// CNetEvent_HammerHit contains only a position, not the actor/target
			// pair. Do not turn a generic attack tick into target-specific evidence.
			Input.m_HammerTargetEvidenceKnown = false;
			Input.m_HammerTargetEvidence = false;
			Input.m_RecoveryNeed = ClassifyLiveRecoveryNeed(pGameClient, Input);
			SImpactGameplayFrame Frame = NormalizeGameplayFrame(Input);
			// This is an exact raw-snapshot continuity fact, not an age heuristic:
			// the authoritative snapshot advanced, but the character object's own tick
			// repeated. Freeze lifecycle fields from the current snapshot may therefore
			// be newer than the physical position object used for trajectory predicates.
			Frame.m_AuthoritativePhysicalStateStale = RawPrevious.m_Valid && RawCurrent.m_Valid &&
				RawPrevious.m_FromRawPreviousSnapshot && RawCurrent.m_FromRawCurrentSnapshot &&
				RawPrevious.m_SnapshotServerTick >= 0 && RawCurrent.m_SnapshotServerTick > RawPrevious.m_SnapshotServerTick &&
				RawPrevious.m_ObjectTick >= 0 && RawCurrent.m_ObjectTick >= 0 &&
				RawPrevious.m_ObjectTick == RawCurrent.m_ObjectTick;
			vFrames.push_back(std::move(Frame));
		}
		return vFrames;
	}

	int64_t CImpactSystem::CurrentTick() const
	{
		return Client()->GameTick(g_Config.m_ClDummy);
	}

	void CImpactSystem::LoadKogMapMetadataCache()
	{
		m_vKogMapMetadataCache.clear();
		if(Storage() == nullptr)
			return;
		void *pData = nullptr;
		unsigned Length = 0;
		if(!Storage()->ReadFile(KOG_MAP_METADATA_FILE, IStorage::TYPE_SAVE, &pData, &Length) || pData == nullptr)
			return;
		json_value *pRoot = JsonParse(static_cast<const json_char *>(pData), Length);
		if(pRoot != nullptr)
		{
			const json_value *pEntries = MapMetadataField(pRoot, "entries");
			if(pRoot->type == json_object && pEntries != nullptr && pEntries->type == json_array)
			{
				for(int Index = 0; Index < json_array_length(pEntries) && m_vKogMapMetadataCache.size() < MAX_KOG_MAP_METADATA_CACHE_ENTRIES; ++Index)
				{
					const json_value *pEntry = json_array_get(pEntries, Index);
					std::string Map;
					std::string Category;
					if(!ReadMapMetadataString(pEntry, "map", &Map) || !ReadMapMetadataString(pEntry, "category", &Category))
						continue;
					int Stars = -1;
					const json_value *pStars = MapMetadataField(pEntry, "rating");
					if(pStars != nullptr && pStars->type == json_integer)
						Stars = json_int_get(pStars);
					SMapDifficultyMetadata Metadata;
					Metadata.m_Category = Category;
					Metadata.m_Stars = Stars;
					const std::string MapKey = NormalizeKogMapMetadataKey(Map.c_str());
					if(MapKey.empty() || !IsMapDifficultyMetadataUsable(Metadata) || Metadata.m_Stars < -1 || Metadata.m_Stars > MAX_MAP_DIFFICULTY_STARS)
						continue;
					bool Duplicate = false;
					for(const SKogMapMetadataCacheEntry &Cached : m_vKogMapMetadataCache)
						Duplicate |= Cached.m_MapKey == MapKey;
					if(!Duplicate)
						m_vKogMapMetadataCache.push_back({MapKey, std::move(Metadata)});
				}
			}
			json_value_free(pRoot);
		}
		free(pData);
	}

	bool CImpactSystem::SaveKogMapMetadataCache() const
	{
		CJsonStringWriter Writer;
		Writer.BeginObject();
		Writer.WriteAttribute("version");
		Writer.WriteIntValue(1);
		Writer.WriteAttribute("entries");
		Writer.BeginArray();
		for(const SKogMapMetadataCacheEntry &Entry : m_vKogMapMetadataCache)
		{
			Writer.BeginObject();
			Writer.WriteAttribute("map");
			Writer.WriteStrValue(Entry.m_MapKey.c_str());
			Writer.WriteAttribute("category");
			Writer.WriteStrValue(Entry.m_Metadata.m_Category.c_str());
			Writer.WriteAttribute("rating");
			Writer.WriteIntValue(Entry.m_Metadata.m_Stars);
			Writer.EndObject();
		}
		Writer.EndArray();
		Writer.EndObject();
		return WriteMapMetadataCacheAtomic(Storage(), Writer.GetOutputString());
	}

	const SMapDifficultyMetadata *CImpactSystem::FindKogMapMetadata(const char *pMapName) const
	{
		const std::string MapKey = NormalizeKogMapMetadataKey(pMapName);
		if(MapKey.empty())
			return nullptr;
		for(const SKogMapMetadataCacheEntry &Entry : m_vKogMapMetadataCache)
			if(Entry.m_MapKey == MapKey)
				return &Entry.m_Metadata;
		return nullptr;
	}

	void CImpactSystem::StoreKogMapMetadata(const char *pMapName, const SMapDifficultyMetadata &Metadata, bool Authoritative)
	{
		const std::string MapKey = NormalizeKogMapMetadataKey(pMapName);
		if(MapKey.empty() || Metadata.m_Category.empty() || Metadata.m_Category.size() > MAX_CONTEXT_LENGTH ||
			Metadata.m_Stars < -1 || Metadata.m_Stars > MAX_MAP_DIFFICULTY_STARS)
			return;
		for(SKogMapMetadataCacheEntry &Entry : m_vKogMapMetadataCache)
		{
			if(Entry.m_MapKey != MapKey)
				continue;
			SMapDifficultyMetadata Merged;
			if(!MergeKogMapMetadata(Entry.m_Metadata, Metadata, &Merged))
			{
				if(g_Config.m_Debug)
					ImpactDebugLog("IMPACT_MAP_METADATA_CACHE_CONFLICT map=\"%s\" cached_category=\"%s\" cached_rating=%d incoming_category=\"%s\" incoming_rating=%d action=%s",
						MapKey.c_str(), Entry.m_Metadata.m_Category.c_str(), Entry.m_Metadata.m_Stars, Metadata.m_Category.c_str(), Metadata.m_Stars,
						Authoritative ? "REPLACE_AUTHORITATIVE" : "KEEP_CACHED");
				if(!Authoritative)
					return;
				Entry.m_Metadata = Metadata;
				SaveKogMapMetadataCache();
				return;
			}
			if(Merged.m_Category != Entry.m_Metadata.m_Category || Merged.m_Stars != Entry.m_Metadata.m_Stars)
			{
				Entry.m_Metadata = std::move(Merged);
				SaveKogMapMetadataCache();
			}
			return;
		}
		if(m_vKogMapMetadataCache.size() >= MAX_KOG_MAP_METADATA_CACHE_ENTRIES)
			m_vKogMapMetadataCache.erase(m_vKogMapMetadataCache.begin());
		m_vKogMapMetadataCache.push_back({MapKey, Metadata});
		SaveKogMapMetadataCache();
	}

	void CImpactSystem::ResetMapInfoRequestState()
	{
		m_MapInfoCapturePending = false;
		m_MapInfoRequestAutomatic = false;
		m_MapInfoRequestAttempts = 0;
		m_MapInfoCaptureDeadlineTick = -1;
		m_MapInfoNextRequestTick = -1;
		m_MapInfoRequestExhausted = false;
		m_MapInfoSessionKey.clear();
		m_MapMetadataLastDiagnosticKey.clear();
		m_KogCategoryLastDiagnosticKey.clear();
		m_KogCategoryContextLastDiagnosticKey.clear();
		m_KogVoteRatingLastDiagnosticKey.clear();
	}

	void CImpactSystem::PumpMapInfoRequest()
	{
		if(!m_MapInfoCapturePending || m_MapInfoCaptureDeadlineTick < 0 ||
			(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK))
			return;
		const int64_t ServerTick = Client()->GameTick(0);
		if(ServerTick <= m_MapInfoCaptureDeadlineTick)
			return;
		const bool Automatic = m_MapInfoRequestAutomatic;
		m_MapInfoCapturePending = false;
		m_MapInfoRequestAutomatic = false;
		m_MapInfoCaptureDeadlineTick = -1;
		if(!Automatic)
		{
			ImpactDebugLog("IMPACT_MAPINFO_CAPTURE status=TIMEOUT server_tick=%lld", static_cast<long long>(ServerTick));
			return;
		}
		if(m_MapInfoRequestAttempts >= MAX_AUTOMATIC_MAPINFO_REQUESTS)
		{
			m_MapInfoRequestExhausted = true;
			m_MapInfoNextRequestTick = -1;
			ImpactDebugLog("IMPACT_MAPINFO_REQUEST attempt=%d status=TIMEOUT reason=MAX_RETRIES", m_MapInfoRequestAttempts);
		}
		else
		{
			m_MapInfoNextRequestTick = ServerTick + std::max(1, Client()->GameTickSpeed()) * MAPINFO_RETRY_DELAY_SECONDS;
			ImpactDebugLog("IMPACT_MAPINFO_REQUEST attempt=%d status=TIMEOUT next_retry_tick=%lld", m_MapInfoRequestAttempts, static_cast<long long>(m_MapInfoNextRequestTick));
		}
	}

	void CImpactSystem::MaybeRequestKogMapInfo(const SRunContext &Context, int64_t CurrentServerTick)
	{
		if(!IsKogCommunity(Context.m_CommunityId.c_str()) || Context.m_Map.empty() || Client()->State() != IClient::STATE_ONLINE)
			return;
		SMapDifficultyMetadata CurrentMetadata;
		CurrentMetadata.m_Category = Context.m_MapDifficulty;
		CurrentMetadata.m_Stars = Context.m_MapStars;
		if(IsMapDifficultyMetadataComplete(CurrentMetadata))
		{
			if(m_MapInfoCapturePending && m_MapInfoRequestAutomatic)
			{
				m_MapInfoCapturePending = false;
				m_MapInfoRequestAutomatic = false;
				m_MapInfoCaptureDeadlineTick = -1;
			}
			return;
		}
		if(m_MapInfoRequestExhausted)
		{
			LogMapMetadataResolution("NONE", Context, 0, "MAPINFO_TIMEOUT");
			return;
		}
		if(!ShouldRequestKogMapInfo(true, CurrentMetadata, m_MapInfoCapturePending, m_MapInfoRequestAttempts, CurrentServerTick, m_MapInfoNextRequestTick))
			return;
		++m_MapInfoRequestAttempts;
		m_MapInfoCapturePending = true;
		m_MapInfoRequestAutomatic = true;
		m_MapInfoCaptureDeadlineTick = CurrentServerTick + std::max(1, Client()->GameTickSpeed()) * MAPINFO_REQUEST_TIMEOUT_SECONDS;
		m_MapInfoNextRequestTick = -1;
		GameClient()->m_Chat.SendChat(0, "/mapinfo", 0);
		ImpactDebugLog("IMPACT_MAPINFO_REQUEST server_tick=%lld attempt=%d connection=0", static_cast<long long>(CurrentServerTick), m_MapInfoRequestAttempts);
	}

	bool CImpactSystem::IsCurrentServerContext() const
	{
		if(!m_ServerContextKnown || !HasLiveServerAddress(Client()->State()))
			return false;
		const CServerInfo &ServerInfo = Client()->ServerInfo();
		char aAddress[NETADDR_MAXSTRSIZE];
		net_addr_str(&Client()->ServerAddress(), aAddress, sizeof(aAddress), true);
		const std::string ServerKey = std::string(aAddress) + "|" + ServerInfo.m_aCommunityId;
		return m_ServerContext.m_ServerKey == ServerKey && m_ServerContext.m_Map == ServerInfo.m_aMap;
	}

	void CImpactSystem::ApplyResolvedMapMetadata(const SMapDifficultyMetadata &Metadata, const char *pSource)
	{
		// Metadata responses can arrive between the authoritative server-info map
		// update and the next snapshot refresh. Rebind the runtime context first so
		// a response for the new map cannot mutate the old map's metadata.
		if(!IsCurrentServerContext())
			RefreshServerContext(nullptr, nullptr);
		if(!IsCurrentServerContext() || !IsMapDifficultyMetadataUsable(Metadata))
			return;
		const std::string PreviousCategory = m_ServerContext.m_MapDifficulty;
		const int PreviousRating = m_ServerContext.m_MapStars;
		if(!Metadata.m_Category.empty())
			m_ServerContext.m_MapDifficulty = Metadata.m_Category;
		if(Metadata.m_Stars >= 0)
			m_ServerContext.m_MapStars = Metadata.m_Stars;
		if(g_Config.m_Debug && (PreviousCategory != m_ServerContext.m_MapDifficulty || PreviousRating != m_ServerContext.m_MapStars))
		{
			LogKogCategoryResolve(m_ServerContext, m_ServerContext.m_Map.c_str(), PreviousCategory.c_str(), "RUNTIME", Metadata.m_Category.c_str(), pSource != nullptr ? pSource : "UNKNOWN", "ACCEPTED", "LATE_PROVIDER_UPDATE");
			ImpactDebugLog("KOG_DIFFICULTY_RUNTIME event=LATE_PROVIDER server_key=\"%s\" map=\"%s\" previous_difficulty=\"%s\" candidate_difficulty=\"%s\" new_difficulty=\"%s\" rating=%d source=%s status=ACCEPTED reason=LATE_PROVIDER_UPDATE",
				m_ServerContext.m_ServerKey.c_str(), m_ServerContext.m_Map.c_str(), PreviousCategory.c_str(), Metadata.m_Category.c_str(), m_ServerContext.m_MapDifficulty.c_str(), m_ServerContext.m_MapStars,
				pSource != nullptr ? pSource : "UNKNOWN");
		}
		std::string PreviousRunCategory;
		const bool RunIdentityMatches = m_HasCurrentRun && m_CurrentRun.Context().m_ServerKey == m_ServerContext.m_ServerKey && m_CurrentRun.Context().m_Map == m_ServerContext.m_Map;
		bool RunMetadataChanged = false;
		if(RunIdentityMatches)
		{
			PreviousRunCategory = m_CurrentRun.Context().m_MapDifficulty;
			RunMetadataChanged = m_CurrentRun.UpdateAuthoritativeMapDifficultyMetadata(Metadata);
		}
		if(RunMetadataChanged && g_Config.m_Debug)
		{
			ImpactDebugLog("KOG_CATEGORY_ACTIVE_RUN map=\"%s\" category=\"%s\" rating=%d source=%s reason=LATE_PROVIDER_UPDATE",
				m_ServerContext.m_Map.c_str(), m_CurrentRun.Context().m_MapDifficulty.c_str(), m_CurrentRun.Context().m_MapStars,
				pSource != nullptr ? pSource : "UNKNOWN");
			ImpactDebugLog("KOG_DIFFICULTY_ACTIVE_RUN event=LATE_PROVIDER run_id=%llu server_key=\"%s\" map=\"%s\" previous_difficulty=\"%s\" candidate_difficulty=\"%s\" new_difficulty=\"%s\" rating=%d source=%s status=ACCEPTED reason=LATE_PROVIDER_UPDATE",
				static_cast<unsigned long long>(m_CurrentRun.RunId()), m_ServerContext.m_ServerKey.c_str(), m_ServerContext.m_Map.c_str(), PreviousRunCategory.c_str(), Metadata.m_Category.c_str(),
				m_CurrentRun.Context().m_MapDifficulty.c_str(), m_CurrentRun.Context().m_MapStars, pSource != nullptr ? pSource : "UNKNOWN");
		}
		RefreshImpactEligibility();
	}

	void CImpactSystem::ApplyKogVoteListRating()
	{
		if(!IsCurrentServerContext())
			RefreshServerContext(nullptr, nullptr);
		if(!IsCurrentServerContext() || !IsKogCommunity(m_ServerContext.m_CommunityId.c_str()) || m_ServerContext.m_Map.empty() || m_ServerContext.m_MapStars >= 0)
			return;

		const SKogVoteRatingLookup Lookup = LookupKogVoteListRating(GameClient()->m_Voting, m_ServerContext.m_Map.c_str());
		const char *pLookupStatus = Lookup.m_Accepted ? "ACCEPTED" : (Lookup.m_ExactNameMatches > 0 ? "REJECTED" : "NOT_FOUND");
		LogKogVoteRatingLookup(m_ServerContext.m_Map.c_str(), Lookup.m_OptionsTotal, Lookup.m_ExactNameMatches, pLookupStatus,
			Lookup.m_Reason.c_str(), Lookup.m_RawOption.c_str(), Lookup.m_Accepted ? Lookup.m_Rating : -1);
		if(!Lookup.m_Accepted)
			return;
		const int Rating = Lookup.m_Rating;

		if(const SMapDifficultyMetadata *pCached = FindKogMapMetadata(m_ServerContext.m_Map.c_str()); pCached != nullptr && pCached->m_Stars >= 0)
		{
			if(pCached->m_Stars != Rating && g_Config.m_Debug)
				ImpactDebugLog("KOG_METADATA_CONFLICT map=\"%s\" strong_source=CACHE strong_rating=%d vote_rating=%d action=KEEP_STRONG_SOURCE",
					m_ServerContext.m_Map.c_str(), pCached->m_Stars, Rating);
			return;
		}

		SMapDifficultyMetadata Metadata;
		Metadata.m_Category = m_ServerContext.m_MapDifficulty;
		Metadata.m_Stars = Rating;
		if(!Metadata.m_Category.empty())
			StoreKogMapMetadata(m_ServerContext.m_Map.c_str(), Metadata, false);
		m_ServerContext.m_MapStars = Rating;
		if(m_HasCurrentRun && m_CurrentRun.Context().m_ServerKey == m_ServerContext.m_ServerKey && m_CurrentRun.Context().m_Map == m_ServerContext.m_Map)
			m_CurrentRun.UpdateMapDifficultyMetadata(Metadata);

		SRunContext Context = m_ServerContext;
		LogMapMetadataResolution("KOG_VOTE_LIST", Context, 0);
		if(g_Config.m_Debug)
			ImpactDebugLog("KOG_VOTE_METADATA_MATCH map=\"%s\" raw_option=\"%s\" rating=%d status=ACCEPTED",
				m_ServerContext.m_Map.c_str(), Lookup.m_RawOption.c_str(), Rating);

		// A complete vote-list result makes an automatic /mapinfo request
		// unnecessary. A later authoritative /mapinfo response may still replace
		// this rating through ApplyResolvedMapMetadata.
		if(!m_ServerContext.m_MapDifficulty.empty() && m_MapInfoCapturePending && m_MapInfoRequestAutomatic)
		{
			m_MapInfoCapturePending = false;
			m_MapInfoRequestAutomatic = false;
			m_MapInfoCaptureDeadlineTick = -1;
			m_MapInfoNextRequestTick = -1;
		}
		RefreshImpactEligibility();
	}

	void CImpactSystem::RefreshImpactEligibility()
	{
		if(!m_ServerContextKnown)
		{
			m_EngineActive = false;
			return;
		}
		const bool IsKog = IsKogCommunity(m_ServerContext.m_CommunityId.c_str());
		// KoG has a client-visible category provider (passive metadata, cache, or
		// the bounded /mapinfo request). Do not start production Impact on an
		// unknown KoG category: a late SOL response must never turn an already
		// collected run into an invalid Solo-map run. EGO retains its existing
		// community gate when no equivalent category source is available.
		const bool MapEligibilityKnown = !IsKog || !m_ServerContext.m_MapDifficulty.empty();
		SMapDifficultyMetadata ServerCategoryMetadata;
		const bool ServerCategoryIsSolo = IsKog && ParseKogServerCategory(m_ServerContext.m_ServerName.c_str(), &ServerCategoryMetadata) &&
			IsSoloMapCategory(ServerCategoryMetadata.m_Category.c_str());
		// A canonical Solo Gores server label is a safety override for a stale or
		// conflicting cached category. It can only disable Impact; it never
		// invents a non-Solo category or a rating.
		const bool SoloMap = IsSoloMap(GameClient()) || IsSoloMapCategory(m_ServerContext.m_MapDifficulty.c_str()) || ServerCategoryIsSolo;
		m_EngineActive = IsImpactMapEligibilityAllowed(
			IsSupportedCommunity(m_ServerContext.m_CommunityId.c_str()), MapEligibilityKnown, SoloMap);

		// A late authoritative Solo result invalidates only a run belonging to this
		// same server/map. A map/server transition is still finalized by the normal
		// lifecycle in OnNewSnapshot and must not be discarded here.
		if(SoloMap && m_HasCurrentRun && m_CurrentRun.Context().m_ServerKey == m_ServerContext.m_ServerKey &&
			m_CurrentRun.Context().m_Map == m_ServerContext.m_Map)
			DiscardActiveAnalysis();
	}

	void CImpactSystem::LogMapMetadataResolution(const char *pSource, const SRunContext &Context, int Attempt, const char *pReason)
	{
		if(!g_Config.m_Debug || pSource == nullptr)
			return;
		const std::string DiagnosticKey = std::string(pSource) + "|" + Context.m_Map + "|" + Context.m_MapDifficulty + "|" + std::to_string(Context.m_MapStars) + "|" + (pReason != nullptr ? pReason : "") + "|" + std::to_string(Attempt);
		if(m_MapMetadataLastDiagnosticKey == DiagnosticKey)
			return;
		m_MapMetadataLastDiagnosticKey = DiagnosticKey;
		const char *pProvider = IsKogCommunity(Context.m_CommunityId.c_str()) ? "KOG" : (Context.m_CommunityId.empty() ? "UNKNOWN" : Context.m_CommunityId.c_str());
		ImpactDebugLog("IMPACT_MAP_METADATA provider=%s map=\"%s\" server=\"%s\" source=%s category=\"%s\" rating=%d status=%s attempt=%d reason=\"%s\"",
			pProvider, Context.m_Map.c_str(), Context.m_ServerName.c_str(), pSource, Context.m_MapDifficulty.c_str(), Context.m_MapStars,
			Context.m_MapDifficulty.empty() ? "UNKNOWN" : "RESOLVED", Attempt, pReason != nullptr ? pReason : "NONE");
	}

	void CImpactSystem::LogKogCategoryResolve(const SRunContext &Context, const char *pCandidateMap, const char *pExistingCategory, const char *pExistingSource,
		const char *pCandidateCategory, const char *pCandidateSource, const char *pStatus, const char *pReason)
	{
		if(!g_Config.m_Debug || pCandidateMap == nullptr || pExistingCategory == nullptr || pExistingSource == nullptr ||
			pCandidateCategory == nullptr || pCandidateSource == nullptr || pStatus == nullptr || pReason == nullptr)
			return;
		const std::string DiagnosticKey = Context.m_ServerKey + "|" + Context.m_Map + "|" + pCandidateMap + "|" + pExistingCategory + "|" + pExistingSource + "|" +
			pCandidateCategory + "|" + pCandidateSource + "|" + pStatus + "|" + pReason;
		if(m_KogCategoryLastDiagnosticKey == DiagnosticKey)
			return;
		m_KogCategoryLastDiagnosticKey = DiagnosticKey;
		ImpactDebugLog("KOG_CATEGORY_RESOLVE server_key=\"%s\" current_map=\"%s\" candidate_map=\"%s\" existing_category=\"%s\" existing_source=%s candidate_category=\"%s\" candidate_source=%s status=%s reason=%s",
			Context.m_ServerKey.c_str(), Context.m_Map.c_str(), pCandidateMap, pExistingCategory, pExistingSource, pCandidateCategory, pCandidateSource, pStatus, pReason);
	}

	void CImpactSystem::LogKogVoteRatingLookup(const char *pMapName, int OptionsTotal, int ExactNameMatches, const char *pStatus, const char *pReason, const char *pRawOption, int Rating)
	{
		if(!g_Config.m_Debug || pMapName == nullptr || pStatus == nullptr || pReason == nullptr)
			return;
		const std::string DiagnosticKey = m_MapInfoSessionKey + "|" + pMapName + "|" + std::to_string(OptionsTotal) + "|" +
			std::to_string(ExactNameMatches) + "|" + pStatus + "|" + pReason + "|" + (pRawOption != nullptr ? pRawOption : "");
		if(m_KogVoteRatingLastDiagnosticKey == DiagnosticKey)
			return;
		m_KogVoteRatingLastDiagnosticKey = DiagnosticKey;
		ImpactDebugLog("KOG_VOTE_RATING_LOOKUP map=\"%s\" options_total=%d exact_name_matches=%d status=%s reason=%s",
			pMapName, OptionsTotal, ExactNameMatches, pStatus, pReason);
		if(ExactNameMatches > 0)
			ImpactDebugLog("KOG_VOTE_RATING_OPTION map=\"%s\" raw=\"%s\" parse_status=%s reason=%s rating=%d",
				pMapName, pRawOption != nullptr ? pRawOption : "", pStatus, pReason, Rating);
	}

	void CImpactSystem::LogDifficultyResult(const char *pEvent, const SRunSnapshot &SnapshotData, const char *pSource, const char *pStatus, const char *pReason)
	{
		if(!g_Config.m_Debug || pEvent == nullptr || pSource == nullptr || pStatus == nullptr || pReason == nullptr)
			return;
		const std::string DiagnosticKey = std::string(pEvent) + "|" + std::to_string(SnapshotData.m_RunId) + "|" + SnapshotData.m_Context.m_ServerKey + "|" +
			SnapshotData.m_Context.m_Map + "|" + SnapshotData.m_Context.m_MapDifficulty + "|" + std::to_string(SnapshotData.m_Context.m_MapStars) + "|" + pSource + "|" + pStatus + "|" + pReason;
		if(m_KogDifficultyResultLastDiagnosticKey == DiagnosticKey)
			return;
		m_KogDifficultyResultLastDiagnosticKey = DiagnosticKey;
		ImpactDebugLog("KOG_DIFFICULTY_RESULT event=%s run_id=%llu server_key=\"%s\" map=\"%s\" previous_difficulty=\"\" new_difficulty=\"%s\" rating=%d source=%s status=%s reason=%s result=%s",
			pEvent, static_cast<unsigned long long>(SnapshotData.m_RunId), SnapshotData.m_Context.m_ServerKey.c_str(), SnapshotData.m_Context.m_Map.c_str(),
			SnapshotData.m_Context.m_MapDifficulty.c_str(), SnapshotData.m_Context.m_MapStars, pSource, pStatus, pReason, RunResultName(SnapshotData.m_Result));
	}

	void CImpactSystem::LogDifficultyUiSource(const char *pSource, uint64_t RunId, const SRunContext &Context, const char *pReason)
	{
		if(!g_Config.m_Debug || pSource == nullptr || pReason == nullptr)
			return;
		const std::string DiagnosticKey = std::string(pSource) + "|" + std::to_string(RunId) + "|" + Context.m_ServerKey + "|" + Context.m_Map + "|" +
			Context.m_MapDifficulty + "|" + std::to_string(Context.m_MapStars) + "|" + pReason;
		if(m_KogDifficultyUiLastDiagnosticKey == DiagnosticKey)
			return;
		m_KogDifficultyUiLastDiagnosticKey = DiagnosticKey;
		const std::string Display = MapDifficultyDisplayName(Context);
		ImpactDebugLog("KOG_DIFFICULTY_UI_SOURCE source=%s run_id=%llu server_key=\"%s\" map=\"%s\" previous_difficulty=\"\" candidate_difficulty=\"%s\" displayed_difficulty=\"%s\" rating=%d status=READ reason=%s",
			pSource, static_cast<unsigned long long>(RunId), Context.m_ServerKey.c_str(), Context.m_Map.c_str(), Context.m_MapDifficulty.c_str(),
			Display.c_str(), Context.m_MapStars, pReason);
	}

	void CImpactSystem::OnConsoleInit()
	{
		Console()->Register("amf_impact_delete_run", "i[run-id]", CFGFLAG_CLIENT, ConDeleteRun, this, "Delete an AMF Impact history run");
#if defined(CONF_DEBUG)
		Console()->Register("amf_impact_capture_mapinfo", "", CFGFLAG_CLIENT, ConCaptureMapInfo, this,
			"Request one server /mapinfo response and capture it in the Impact debug log");
#endif
	}

	void CImpactSystem::ConDeleteRun(IConsole::IResult *pResult, void *pUserData)
	{
		CImpactSystem *pThis = static_cast<CImpactSystem *>(pUserData);
		if(pThis != nullptr && pResult->GetInteger(0) > 0)
			pThis->DeleteHistoryRun(static_cast<uint64_t>(pResult->GetInteger(0)));
	}

#if defined(CONF_DEBUG)
	void CImpactSystem::ConCaptureMapInfo(IConsole::IResult *pResult, void *pUserData)
	{
		(void)pResult;
		CImpactSystem *pThis = static_cast<CImpactSystem *>(pUserData);
		if(pThis == nullptr)
			return;
		if(!g_Config.m_Debug)
		{
			log_warn("amf-impact", "amf_impact_capture_mapinfo requires debug=1");
			return;
		}
		if(pThis->Client()->State() != IClient::STATE_ONLINE && pThis->Client()->State() != IClient::STATE_DEMOPLAYBACK)
		{
			log_warn("amf-impact", "amf_impact_capture_mapinfo requires an active connection");
			return;
		}

		// The command is deliberately explicit and one-shot. It uses the normal
		// client command path, while the server executes /mapinfo privately for
		// this client. Manual capture never consumes the bounded automatic retry
		// budget.
		pThis->m_MapInfoCapturePending = true;
		pThis->m_MapInfoRequestAutomatic = false;
		pThis->m_MapInfoCaptureDeadlineTick = pThis->Client()->GameTick(0) + std::max(1, pThis->Client()->GameTickSpeed()) * 10;
		pThis->GameClient()->m_Chat.SendChat(0, "/mapinfo", 0);
		pThis->ImpactDebugLog("IMPACT_MAPINFO_REQUEST server_tick=%lld connection=0", static_cast<long long>(pThis->Client()->GameTick(0)));
	}
#endif

	void CImpactSystem::ImpactDebugLog(const char *pFmt, ...)
	{
		if(!g_Config.m_Debug || pFmt == nullptr)
			return;
		if(m_hImpactDebugLog == nullptr)
		{
			// The file is intentionally relative to the client working directory.
			// Launching build\\Release\\DDNet.exe from that directory creates the
			// requested build\\Release\\amf_impact_debug.log file.
			m_hImpactDebugLog = io_open("amf_impact_debug.log", IOFLAG_APPEND);
			if(m_hImpactDebugLog == nullptr)
			{
				if(!m_ImpactDebugLogOpenFailed)
				{
					log_warn("amf-impact", "failed to open amf_impact_debug.log");
					m_ImpactDebugLogOpenFailed = true;
				}
				return;
			}
			m_ImpactDebugLogOpenFailed = false;
		}
		if(!m_ImpactDebugSessionStarted)
		{
			char aSession[128];
			str_format(aSession, sizeof(aSession), "IMPACT_DEBUG_SESSION_START timestamp=%lld", static_cast<long long>(time_timestamp()));
			WriteImpactDebugLine(aSession);
			m_ImpactDebugSessionStarted = true;
		}
		if(!m_ImpactDebugEnabled)
		{
			char aEnabled[128];
			str_format(aEnabled, sizeof(aEnabled), "IMPACT_DEBUG_ENABLED timestamp=%lld", static_cast<long long>(time_timestamp()));
			WriteImpactDebugLine(aEnabled);
			m_ImpactDebugEnabled = true;
		}

		char aMessage[4096];
		va_list Args;
		va_start(Args, pFmt);
		str_format_v(aMessage, sizeof(aMessage), pFmt, Args);
		va_end(Args);
		WriteImpactDebugLine(aMessage);
	}

	void CImpactSystem::WriteImpactDebugLine(const char *pLine)
	{
		if(m_hImpactDebugLog == nullptr || pLine == nullptr)
			return;
		const unsigned Length = static_cast<unsigned>(str_length(pLine));
		io_write(m_hImpactDebugLog, pLine, Length);
		io_write_newline(m_hImpactDebugLog);
		io_flush(m_hImpactDebugLog);
	}

	void CImpactSystem::CloseImpactDebugLog()
	{
		if(m_hImpactDebugLog != nullptr)
		{
			io_flush(m_hImpactDebugLog);
			io_close(m_hImpactDebugLog);
			m_hImpactDebugLog = nullptr;
		}
	}

	void CImpactSystem::LogPhase2Debug(const SImpactPhase2Debug &Debug)
	{
		if(!g_Config.m_Debug)
			return;
		const char *pLabel = Debug.m_DebugLabel.empty() ? "PHASE2_DEBUG" : Debug.m_DebugLabel.c_str();
		const int64_t ServerTick = Debug.m_ServerTick >= 0 ? Debug.m_ServerTick :
			(Debug.m_RestorationTick >= 0 ? Debug.m_RestorationTick : Debug.m_StartTick);
		const int64_t EmissionTick = Debug.m_EmissionTick >= 0 ? Debug.m_EmissionTick : ServerTick;
		const SParticipantId &Target = Debug.m_Target;
		const SParticipantId &Saver = Debug.m_PotentialSaver;
		const std::string Contributors = Phase2ContributorList(Debug.m_vContributors);

		if(Debug.m_DebugLabel == "PROBLEM_EPISODE")
		{
			ImpactDebugLog("PROBLEM_EPISODE server_tick=%lld client_id=%d generation=%u team_id=%d jump_state_before_shot=%s downward_shot_known=%d downward_shot=%d shot_tick=%lld jump_reset_shot_seen=%d previous_recovery_need=%s current_recovery_need=%s initial_recovery_need=%s physical_outcome=%s self_origin_evidence=%d external_cause_evidence=%d teammate_cause=%d harm_cause=%s harm_confidence=%s fall_episode_id=%llu fall_suppressed=%d fall_confirmed=%d fall_already_confirmed=%d reason='%s'",
				static_cast<long long>(ServerTick), Target.m_ClientId, Target.m_SlotGeneration, Debug.m_TeamId,
				JumpStateName(Debug.m_JumpStateBeforeShot), Debug.m_DownwardShotKnown ? 1 : 0, Debug.m_DownwardShot ? 1 : 0,
				static_cast<long long>(Debug.m_JumpResetShotTick), Debug.m_JumpResetShotSeen ? 1 : 0,
				RecoveryNeedName(Debug.m_PreviousRecoveryNeed), RecoveryNeedName(Debug.m_RecoveryNeed), RecoveryNeedName(Debug.m_InitialRecoveryNeed), PhysicalOutcomeName(Debug.m_PhysicalOutcome), Debug.m_SelfOriginEvidence ? 1 : 0,
				Debug.m_ExternalCauseEvidence ? 1 : 0, Debug.m_TeammateCause ? 1 : 0, HarmCauseName(Debug.m_HarmCause),
				CausalConfidenceName(Debug.m_HarmCausalConfidence), static_cast<unsigned long long>(Debug.m_EpisodeId),
				Debug.m_FallSuppressed ? 1 : 0, Debug.m_ConfirmedFall ? 1 : 0,
				Debug.m_FallAlreadyConfirmed ? 1 : 0,
				Debug.m_Reason.c_str());
		}
		else if(Debug.m_DebugLabel == "FALL_TRACE")
		{
			ImpactDebugLog("FALL_TRACE server_tick=%lld target=%d generation=%u team_id=%d episode_id=%llu physical_outcome=%s frozen=%d deep_frozen=%d position_known=%d velocity_known=%d supported=%d safe_position=%d freeze_start=%lld freeze_end=%lld confirmed_fall=%d fall_already_confirmed=%d reason='%s'",
				static_cast<long long>(ServerTick), Target.m_ClientId, Target.m_SlotGeneration, Debug.m_TeamId,
				static_cast<unsigned long long>(Debug.m_EpisodeId), PhysicalOutcomeName(Debug.m_PhysicalOutcome),
				Debug.m_FrozenNow ? 1 : 0, Debug.m_DeepFrozen ? 1 : 0, Debug.m_PositionKnown ? 1 : 0,
				Debug.m_VelocityKnown ? 1 : 0, Debug.m_Supported ? 1 : 0, Debug.m_SafeCollisionPosition ? 1 : 0,
				static_cast<long long>(Debug.m_FreezeStart), static_cast<long long>(Debug.m_FreezeEnd),
				Debug.m_ConfirmedFall ? 1 : 0, Debug.m_FallAlreadyConfirmed ? 1 : 0, Debug.m_Reason.c_str());
		}
		else if(Debug.m_DebugLabel == "SAVE_REJECTED" || Debug.m_DebugLabel == "SAVE_REJECTED_REFREEZE")
		{
			ImpactDebugLog("%s server_tick=%lld target=%d generation=%u team_id=%d started=%d eligible=%d recovery_need=%s episode_id=%llu actor=%d self_safe_tick=%lld first_physical_change_tick=%lld reason='%s'", pLabel,
				static_cast<long long>(ServerTick), Target.m_ClientId, Target.m_SlotGeneration, Debug.m_TeamId,
				Debug.m_Started ? 1 : 0, Debug.m_Eligible ? 1 : 0, RecoveryNeedName(Debug.m_RecoveryNeed),
				static_cast<unsigned long long>(Debug.m_EpisodeId), Saver.m_ClientId,
				static_cast<long long>(Debug.m_SelfSafeEvidenceTick), static_cast<long long>(Debug.m_FirstPhysicalChangeTick),
				Debug.m_Reason.c_str());
		}
		else if(Debug.m_DebugLabel == "SELF_SAFE_INVALIDATED")
		{
			ImpactDebugLog("SELF_SAFE_INVALIDATED source_tick=%lld event_tick=%lld target=%d generation=%u team_id=%d episode_id=%llu start_tick=%lld episode_state=%s physical_outcome=%s frozen=%d support_known=%d supported=%d safe_position_known=%d safe_position=%d recovery_evaluated=%d recovery_known=%d recovery_possible=%d self_safe_evidence_tick=%lld reason='%s'",
				static_cast<long long>(ServerTick), static_cast<long long>(EmissionTick), Target.m_ClientId, Target.m_SlotGeneration,
				Debug.m_TeamId, static_cast<unsigned long long>(Debug.m_EpisodeId), static_cast<long long>(Debug.m_StartTick),
				ImpactEpisodeStateName(Debug.m_EpisodeState), PhysicalOutcomeName(Debug.m_PhysicalOutcome), Debug.m_FrozenNow ? 1 : 0,
				Debug.m_SupportKnown ? 1 : 0, Debug.m_Supported ? 1 : 0,
				Debug.m_SafeCollisionPositionKnown ? 1 : 0, Debug.m_SafeCollisionPosition ? 1 : 0,
				Debug.m_AutonomousRecoveryEvaluated ? 1 : 0, Debug.m_AutonomousRecoveryKnown ? 1 : 0,
				Debug.m_AutonomousRecoveryPossible ? 1 : 0, static_cast<long long>(Debug.m_SelfSafeEvidenceTick), Debug.m_Reason.c_str());
		}
		else if(Debug.m_DebugLabel == "FALL_CONFIRM")
		{
			ImpactDebugLog("FALL_EMITTED source_tick=%lld event_tick=%lld target=%d generation=%u team_id=%d episode_id=%llu fall_confirmed=1 fall_already_confirmed=1 confirmation_reason='%s'",
				static_cast<long long>(ServerTick), static_cast<long long>(EmissionTick), Target.m_ClientId, Target.m_SlotGeneration, Debug.m_TeamId,
				static_cast<unsigned long long>(Debug.m_EpisodeId), Debug.m_Reason.c_str());
			ImpactDebugLog("FALL_RAW_EVENT source_tick=%lld event_tick=%lld target=%d generation=%u team_id=%d episode_id=%llu raw_falls=1 reason='%s'",
				static_cast<long long>(ServerTick), static_cast<long long>(EmissionTick), Target.m_ClientId, Target.m_SlotGeneration, Debug.m_TeamId,
				static_cast<unsigned long long>(Debug.m_EpisodeId), Debug.m_Reason.c_str());
		}
		else if(Debug.m_DebugLabel == "FALL_DUPLICATE_SUPPRESSED")
		{
			ImpactDebugLog("FALL_NOT_EMITTED source_tick=%lld event_tick=%lld target=%d generation=%u team_id=%d episode_id=%llu fall_confirmed=0 fall_already_confirmed=1 suppress_reason='DUPLICATE' reason='%s'",
				static_cast<long long>(ServerTick), static_cast<long long>(EmissionTick), Target.m_ClientId, Target.m_SlotGeneration, Debug.m_TeamId,
				static_cast<unsigned long long>(Debug.m_EpisodeId), Debug.m_Reason.c_str());
		}
		else if(Debug.m_DebugLabel == "FALL_CLEANUP")
		{
			ImpactDebugLog("FALL_CLEANUP source_tick=%lld event_tick=%lld target=%d generation=%u team_id=%d episode_id=%llu fall_already_confirmed=%d related_recovery_episode_id=%llu reason='%s'",
				static_cast<long long>(ServerTick), static_cast<long long>(EmissionTick), Target.m_ClientId, Target.m_SlotGeneration, Debug.m_TeamId,
				static_cast<unsigned long long>(Debug.m_EpisodeId), Debug.m_FallAlreadyConfirmed ? 1 : 0,
				static_cast<unsigned long long>(Debug.m_RelatedRecoveryEpisodeId), Debug.m_Reason.c_str());
		}
		else if(Debug.m_DebugLabel == "FALL_NOT_EMITTED")
		{
			ImpactDebugLog("FALL_NOT_EMITTED source_tick=%lld event_tick=%lld target=%d generation=%u team_id=%d episode_id=%llu fall_confirmed=0 fall_already_confirmed=%d suppress_reason='%s'",
				static_cast<long long>(ServerTick), static_cast<long long>(EmissionTick), Target.m_ClientId, Target.m_SlotGeneration, Debug.m_TeamId,
				static_cast<unsigned long long>(Debug.m_EpisodeId), Debug.m_FallAlreadyConfirmed ? 1 : 0, Debug.m_Reason.c_str());
		}
		else if(Debug.m_DebugLabel == "SAVE_INTERACTION")
		{
			ImpactDebugLog("SAVE_INTERACTION server_tick=%lld episode_id=%llu actor=%d actor_generation=%u target=%d target_generation=%u team_id=%d started=%d eligible=%d hook_state=%d hooked_player=%d hook_evidence=%d hammer_target_evidence_known=%d hammer_evidence=%d explicit_support_evidence=%d target_specific=%d hook_span_count=%zu ownership=%s pre_action_safe=%d pre_action_safe_source=%s native_pre_action_result=%s platform_preexisted_before_action=%d freeze_refresh_observed_before_action=%d reason='%s'",
				static_cast<long long>(ServerTick), static_cast<unsigned long long>(Debug.m_EpisodeId), Saver.m_ClientId, Saver.m_SlotGeneration,
				Target.m_ClientId, Target.m_SlotGeneration, Debug.m_TeamId, Debug.m_Started ? 1 : 0, Debug.m_Eligible ? 1 : 0,
				Debug.m_HookState, Debug.m_HookedPlayer, Debug.m_HookEvidence ? 1 : 0, Debug.m_HammerTargetEvidenceKnown ? 1 : 0, Debug.m_HammerEvidence ? 1 : 0,
				Debug.m_HookControlContinuity ? 1 : 0, Debug.m_TargetSpecificEvidence ? 1 : 0, Debug.m_vHookSpans.size(),
				OwnershipStatusName(Debug.m_OwnershipStatus), Debug.m_PreActionSafe ? 1 : 0,
				Debug.m_PreActionSafeSource.empty() ? "NONE" : Debug.m_PreActionSafeSource.c_str(),
				Debug.m_NativePreActionResult.empty() ? "NOT_EVALUATED" : Debug.m_NativePreActionResult.c_str(),
				Debug.m_PlatformPreexistedBeforeAction ? 1 : 0, Debug.m_FreezeRefreshObservedBeforeAction ? 1 : 0, Debug.m_Reason.c_str());
		}
		else if(Debug.m_DebugLabel == "SAVE_RESTORE")
		{
			ImpactDebugLog("SAVE_RESTORE source_tick=%lld event_tick=%lld episode_id=%llu target=%d generation=%u team_id=%d actor=%d actor_generation=%u owner_status=%s action_tick=%lld restore_tick=%lld pre_action_safe=%d pre_action_safe_source=%s native_pre_action_result=%s platform_preexisted_before_action=%d freeze_refresh_observed_before_action=%d physical_change=%d alive_known=%d alive=%d freeze_known=%d frozen=%d present=%d reason='%s'",
				static_cast<long long>(ServerTick), static_cast<long long>(EmissionTick), static_cast<unsigned long long>(Debug.m_EpisodeId), Target.m_ClientId, Target.m_SlotGeneration, Debug.m_TeamId,
				Saver.m_ClientId, Saver.m_SlotGeneration, OwnershipStatusName(Debug.m_OwnershipStatus),
				static_cast<long long>(Debug.m_ActionTick), static_cast<long long>(Debug.m_RestorationTick), Debug.m_PreActionSafe ? 1 : 0,
				Debug.m_PreActionSafeSource.empty() ? "NONE" : Debug.m_PreActionSafeSource.c_str(),
				Debug.m_NativePreActionResult.empty() ? "NOT_EVALUATED" : Debug.m_NativePreActionResult.c_str(),
				Debug.m_PlatformPreexistedBeforeAction ? 1 : 0, Debug.m_FreezeRefreshObservedBeforeAction ? 1 : 0,
				Debug.m_PhysicalChangeEvidence ? 1 : 0, Debug.m_AliveKnown ? 1 : 0, Debug.m_Alive ? 1 : 0,
				Debug.m_FreezeKnown ? 1 : 0, Debug.m_FrozenNow ? 1 : 0, Debug.m_Present ? 1 : 0, Debug.m_Reason.c_str());
		}
		else if(Debug.m_DebugLabel == "CLUTCH_CRISIS_EVAL")
		{
			const char *pProofStatus = Debug.m_Reason.find("proof_status=PROVEN") != std::string::npos ? "PROVEN" :
				Debug.m_Reason.find("proof_status=UNKNOWN") != std::string::npos ? "UNKNOWN" : "NOT_PROVEN";
			ImpactDebugLog("CLUTCH_CRISIS_EVAL server_tick=%lld team_id=%d clutch_episode_id=%llu actor_candidate=%d actor_generation=%u participant_count=%d armed=%d armed_tick=%lld proof_status=%s reason='%s'",
				static_cast<long long>(ServerTick), Debug.m_TeamId, static_cast<unsigned long long>(Debug.m_ClutchEpisodeId), Saver.m_ClientId, Saver.m_SlotGeneration, Debug.m_UnfrozenCount,
				Debug.m_Started ? 1 : 0, static_cast<long long>(Debug.m_ClutchArmedTick),
				pProofStatus, Debug.m_Reason.c_str());
		}
		else if(Debug.m_DebugLabel == "CLUTCH_EPISODE_ARM" || Debug.m_DebugLabel == "CLUTCH_EPISODE_ARMED" || Debug.m_DebugLabel == "CLUTCH_EPISODE_ENDED" || Debug.m_DebugLabel == "CLUTCH_EPISODE_CLEAR")
		{
			ImpactDebugLog("%s server_tick=%lld episode_id=%llu clutch_episode_id=%llu actor=%d generation=%u team_id=%d participant_count=%d armed_tick=%lld reason='%s'",
				pLabel, static_cast<long long>(ServerTick), static_cast<unsigned long long>(Debug.m_EpisodeId), static_cast<unsigned long long>(Debug.m_ClutchEpisodeId), Saver.m_ClientId, Saver.m_SlotGeneration, Debug.m_TeamId,
				Debug.m_UnfrozenCount, static_cast<long long>(Debug.m_ClutchArmedTick), Debug.m_Reason.c_str());
		}
		else if(Debug.m_DebugLabel == "CLUTCH_PENDING_ATTACH")
		{
			ImpactDebugLog("CLUTCH_PENDING_ATTACH server_tick=%lld episode_id=%llu clutch_episode_id=%llu actor=%d actor_generation=%u target=%d target_generation=%u team_id=%d armed_tick=%lld reason='%s'",
				static_cast<long long>(ServerTick), static_cast<unsigned long long>(Debug.m_EpisodeId), static_cast<unsigned long long>(Debug.m_ClutchEpisodeId), Saver.m_ClientId, Saver.m_SlotGeneration,
				Target.m_ClientId, Target.m_SlotGeneration, Debug.m_TeamId, static_cast<long long>(Debug.m_ClutchArmedTick), Debug.m_Reason.c_str());
		}
		else if(Debug.m_DebugLabel == "CLUTCH_FINAL_PROOF")
		{
			ImpactDebugLog("CLUTCH_FINAL_PROOF server_tick=%lld episode_id=%llu clutch_episode_id=%llu actor=%d actor_generation=%u target=%d target_generation=%u team_id=%d armed_tick=%lld result=%s reason='%s'",
				static_cast<long long>(ServerTick), static_cast<unsigned long long>(Debug.m_EpisodeId), static_cast<unsigned long long>(Debug.m_ClutchEpisodeId), Saver.m_ClientId, Saver.m_SlotGeneration,
				Target.m_ClientId, Target.m_SlotGeneration, Debug.m_TeamId, static_cast<long long>(Debug.m_ClutchArmedTick),
				Debug.m_Stage == EImpactPhase2DebugStage::CANDIDATE ? "ACCEPTED" : "REJECTED", Debug.m_Reason.c_str());
		}
		else if(Debug.m_DebugLabel == "CLUTCH_SAVE_CONFIRM")
		{
			ImpactDebugLog("CLUTCH_SAVE_CONFIRM server_tick=%lld episode_id=%llu clutch_episode_id=%llu actor=%d actor_generation=%u target=%d target_generation=%u team_id=%d save_confirmed=YES armed_tick=%lld reason='%s'",
				static_cast<long long>(ServerTick), static_cast<unsigned long long>(Debug.m_EpisodeId), static_cast<unsigned long long>(Debug.m_ClutchEpisodeId), Saver.m_ClientId, Saver.m_SlotGeneration,
				Target.m_ClientId, Target.m_SlotGeneration, Debug.m_TeamId, static_cast<long long>(Debug.m_ClutchArmedTick), Debug.m_Reason.c_str());
		}
		else if(Debug.m_DebugLabel == "CLUTCH_RAW_DELTA")
		{
			ImpactDebugLog("CLUTCH_RAW_DELTA server_tick=%lld episode_id=%llu clutch_episode_id=%llu actor=%d actor_generation=%u target=%d target_generation=%u team_id=%d reason='%s'",
				static_cast<long long>(ServerTick), static_cast<unsigned long long>(Debug.m_EpisodeId), static_cast<unsigned long long>(Debug.m_ClutchEpisodeId), Saver.m_ClientId, Saver.m_SlotGeneration,
				Target.m_ClientId, Target.m_SlotGeneration, Debug.m_TeamId, Debug.m_Reason.c_str());
		}
		else if(Debug.m_DebugLabel == "CLUTCH_CANDIDATE")
		{
			ImpactDebugLog("CLUTCH_CANDIDATE server_tick=%lld episode_id=%llu clutch_episode_id=%llu actor=%d actor_generation=%u target=%d target_generation=%u team_id=%d armed_tick=%lld reason='%s'",
				static_cast<long long>(ServerTick), static_cast<unsigned long long>(Debug.m_EpisodeId), static_cast<unsigned long long>(Debug.m_ClutchEpisodeId), Saver.m_ClientId, Saver.m_SlotGeneration,
				Target.m_ClientId, Target.m_SlotGeneration, Debug.m_TeamId, static_cast<long long>(Debug.m_ClutchArmedTick), Debug.m_Reason.c_str());
		}
		else if(Debug.m_DebugLabel == "SAVE_PENDING" || Debug.m_DebugLabel == "SAVE_CONFIRMED")
		{
			ImpactDebugLog("SAVE_PENDING source_tick=%lld event_tick=%lld episode_id=%llu target=%d generation=%u actor=%d contributors=%s action_tick=%lld restore_tick=%lld confirmed_ticks=%lld required_ticks=%lld pre_action_safe=%d pre_action_safe_source=%s native_pre_action_result=%s platform_preexisted_before_action=%d freeze_refresh_observed_before_action=%d physical_change=%d fall_already_confirmed=%d alive_known=%d alive=%d freeze_known=%d frozen=%d unknown_interruption=%d reason='%s'",
				static_cast<long long>(ServerTick), static_cast<long long>(EmissionTick), static_cast<unsigned long long>(Debug.m_EpisodeId), Target.m_ClientId, Target.m_SlotGeneration,
				Saver.m_ClientId, Contributors.c_str(), static_cast<long long>(Debug.m_ActionTick), static_cast<long long>(Debug.m_RestorationTick),
				static_cast<long long>(Debug.m_ConfirmedTicks), static_cast<long long>(Debug.m_RequiredTicks), Debug.m_PreActionSafe ? 1 : 0,
				Debug.m_PreActionSafeSource.empty() ? "NONE" : Debug.m_PreActionSafeSource.c_str(),
				Debug.m_NativePreActionResult.empty() ? "NOT_EVALUATED" : Debug.m_NativePreActionResult.c_str(),
				Debug.m_PlatformPreexistedBeforeAction ? 1 : 0, Debug.m_FreezeRefreshObservedBeforeAction ? 1 : 0,
				Debug.m_PhysicalChangeEvidence ? 1 : 0, Debug.m_FallAlreadyConfirmed ? 1 : 0, Debug.m_AliveKnown ? 1 : 0, Debug.m_Alive ? 1 : 0,
				Debug.m_FreezeKnown ? 1 : 0, Debug.m_FrozenNow ? 1 : 0, Debug.m_UnknownInterruption ? 1 : 0, Debug.m_Reason.c_str());
			if(Debug.m_Stage == EImpactPhase2DebugStage::CONFIRMED &&
				(Debug.m_EventType == EImpactEventType::SAVE_CONFIRMED || Debug.m_EventType == EImpactEventType::UPDOWN_CONFIRMED || Debug.m_EventType == EImpactEventType::ALED_CONFIRMED))
			ImpactDebugLog("SAVE_CONFIRMED_RESULT source_tick=%lld event_tick=%lld episode_id=%llu target=%d generation=%u save=1 owner=%d owner_generation=%u mechanic=%s ownership=%s proof_id=%llu harm_episode_id=%llu start_tick=%lld restore_tick=%lld confirm_tick=%lld fall_already_confirmed=%d reason='%s'",
				static_cast<long long>(ServerTick), static_cast<long long>(EmissionTick), static_cast<unsigned long long>(Debug.m_EpisodeId), Target.m_ClientId, Target.m_SlotGeneration,
				Saver.m_ClientId, Saver.m_SlotGeneration, Phase2DebugEventTypeName(Debug), OwnershipStatusName(Debug.m_OwnershipStatus), static_cast<unsigned long long>(Debug.m_RescueProofId),
				static_cast<unsigned long long>(Debug.m_RelatedFallEpisodeId), static_cast<long long>(Debug.m_StartTick), static_cast<long long>(Debug.m_RestorationTick), static_cast<long long>(ServerTick),
				Debug.m_FallAlreadyConfirmed ? 1 : 0, Debug.m_Reason.c_str());
		}
		else if(Debug.m_DebugLabel == "SAVE_RESULT")
		{
			const bool Save = Debug.m_Stage == EImpactPhase2DebugStage::CONFIRMED &&
				(Debug.m_EventType == EImpactEventType::SAVE_CONFIRMED || Debug.m_EventType == EImpactEventType::UPDOWN_CONFIRMED || Debug.m_EventType == EImpactEventType::ALED_CONFIRMED);
			ImpactDebugLog("SAVE_RESULT source_tick=%lld event_tick=%lld episode_id=%llu target=%d generation=%u save=%d owner=%d owner_generation=%u mechanic=%s ownership=%s proof_id=%llu harm_episode_id=%llu start_tick=%lld restore_tick=%lld reason='%s'",
				static_cast<long long>(ServerTick), static_cast<long long>(EmissionTick), static_cast<unsigned long long>(Debug.m_EpisodeId), Target.m_ClientId, Target.m_SlotGeneration,
				Save ? 1 : 0, Saver.m_ClientId, Saver.m_SlotGeneration, Phase2DebugEventTypeName(Debug), OwnershipStatusName(Debug.m_OwnershipStatus), static_cast<unsigned long long>(Debug.m_RescueProofId),
				static_cast<unsigned long long>(Debug.m_RelatedFallEpisodeId), static_cast<long long>(Debug.m_StartTick), static_cast<long long>(Debug.m_RestorationTick), Debug.m_Reason.c_str());
		}
		else if(Debug.m_DebugLabel == "HARM_DEATH_RESOLVE")
		{
			ImpactDebugLog("HARM_DEATH_RESOLVE server_tick=%lld episode_id=%llu target=%d generation=%u killer=%d weapon=%d manual_kill=%d reset=%d disconnect=%d teammate_cause=%d confirmed_fall=%d fall_already_confirmed=%d participant_found=%d last_character_context=%d recovery_need=%s initial_recovery_need=%s reason='%s'",
				static_cast<long long>(ServerTick), static_cast<unsigned long long>(Debug.m_EpisodeId), Target.m_ClientId, Target.m_SlotGeneration,
				Debug.m_Killer, Debug.m_Weapon, Debug.m_ManualKill ? 1 : 0, Debug.m_Reset ? 1 : 0, Debug.m_Disconnect ? 1 : 0,
				Debug.m_TeammateCause ? 1 : 0, Debug.m_ConfirmedFall ? 1 : 0, Debug.m_FallAlreadyConfirmed ? 1 : 0, Debug.m_ParticipantFound ? 1 : 0,
				Debug.m_LastCharacterContext ? 1 : 0, RecoveryNeedName(Debug.m_RecoveryNeed), RecoveryNeedName(Debug.m_InitialRecoveryNeed), Debug.m_Reason.c_str());
			const bool LinkedToHarm = Debug.m_EpisodeId != 0 || Debug.m_ConfirmedFall;
			ImpactDebugLog("%s server_tick=%lld target=%d generation=%u team_id=%d started=%d eligible=%d episode_id=%llu reason='%s'",
				LinkedToHarm ? "DEATH_LINKED_TO_HARM" : "DEATH_REJECTED", static_cast<long long>(ServerTick), Target.m_ClientId,
				Target.m_SlotGeneration, Debug.m_TeamId, Debug.m_Started ? 1 : 0, Debug.m_Eligible ? 1 : 0,
				static_cast<unsigned long long>(Debug.m_EpisodeId), Debug.m_Reason.c_str());
			if(Debug.m_FallAlreadyConfirmed)
				ImpactDebugLog("FALL_ALREADY_CONFIRMED server_tick=%lld target=%d generation=%u team_id=%d started=%d eligible=%d episode_id=%llu reason='%s'",
					static_cast<long long>(ServerTick), Target.m_ClientId, Target.m_SlotGeneration, Debug.m_TeamId, Debug.m_Started ? 1 : 0, Debug.m_Eligible ? 1 : 0,
					static_cast<unsigned long long>(Debug.m_EpisodeId), Debug.m_Reason.c_str());
			else
				ImpactDebugLog("%s server_tick=%lld target=%d generation=%u team_id=%d started=%d eligible=%d episode_id=%llu reason='%s'",
					Debug.m_ConfirmedFall ? "FALL_EMITTED" : "FALL_NOT_EMITTED", static_cast<long long>(ServerTick), Target.m_ClientId,
					Target.m_SlotGeneration, Debug.m_TeamId, Debug.m_Started ? 1 : 0, Debug.m_Eligible ? 1 : 0,
					static_cast<unsigned long long>(Debug.m_EpisodeId), Debug.m_Reason.c_str());
		}
		else
		{
			ImpactDebugLog("%s server_tick=%lld episode_id=%llu event_id=%llu type=%s target=%d generation=%u team_id=%d state=%s ownership=%s reason='%s'",
				pLabel, static_cast<long long>(ServerTick), static_cast<unsigned long long>(Debug.m_EpisodeId), static_cast<unsigned long long>(Debug.m_EventId),
				ImpactEventTypeName(Debug.m_EventType), Target.m_ClientId, Target.m_SlotGeneration, Debug.m_TeamId,
				ImpactEpisodeStateName(Debug.m_EpisodeState), OwnershipStatusName(Debug.m_OwnershipStatus), Debug.m_Reason.c_str());
		}

		// These two records are the compact, authoritative lifecycle decisions. They
		// deliberately carry both the source tick (the tick of the observed evidence)
		// and the emission tick (the Update call that published the decision), so a
		// sparse-frame cleanup cannot make an old decision look like a new event.
		if(Debug.m_DebugLabel == "PROBLEM_EPISODE" || Debug.m_DebugLabel == "FALL_CONFIRM" ||
			Debug.m_DebugLabel == "FALL_DUPLICATE_SUPPRESSED" || Debug.m_DebugLabel == "FALL_NOT_EMITTED" ||
			Debug.m_DebugLabel == "FALL_CLEANUP")
		{
			ImpactDebugLog("FALL_DECISION source_tick=%lld event_tick=%lld target=%d generation=%u team_id=%d episode_id=%llu result=%s basis=%s physical_outcome=%s position_known=%d x=%d y=%d velocity_known=%d vel_x=%d vel_y=%d support_known=%d supported=%d safe_collision_position_known=%d safe_collision_position=%d ordinary_freeze_tile_known=%d ordinary_freeze_tile=%d freeze_start=%lld freeze_end=%lld origin_actor=%d contributors=%s fall_confirmed=%d fall_already_confirmed=%d recovery_evaluated=%d recovery_known=%d recovery_possible=%d episode_self_safe=%d self_safe_tick=%lld first_physical_change_tick=%lld reason='%s'",
				static_cast<long long>(ServerTick), static_cast<long long>(EmissionTick), Target.m_ClientId, Target.m_SlotGeneration, Debug.m_TeamId,
				static_cast<unsigned long long>(Debug.m_EpisodeId), Phase2FallDecisionResult(Debug), Phase2FallDecisionBasis(Debug),
				PhysicalOutcomeName(Debug.m_PhysicalOutcome), Debug.m_PositionKnown ? 1 : 0, Debug.m_X, Debug.m_Y,
				Debug.m_VelocityKnown ? 1 : 0, Debug.m_VelX, Debug.m_VelY,
				Debug.m_SupportKnown ? 1 : 0, Debug.m_Supported ? 1 : 0,
				Debug.m_SafeCollisionPositionKnown ? 1 : 0, Debug.m_SafeCollisionPosition ? 1 : 0,
				Debug.m_OrdinaryFreezeTileKnown ? 1 : 0, Debug.m_OrdinaryFreezeTile ? 1 : 0,
				static_cast<long long>(Debug.m_FreezeStart), static_cast<long long>(Debug.m_FreezeEnd),
				Debug.m_PotentialSaver.m_ClientId, Contributors.c_str(),
				Debug.m_ConfirmedFall ? 1 : 0, Debug.m_FallAlreadyConfirmed ? 1 : 0,
				Debug.m_AutonomousRecoveryEvaluated ? 1 : 0, Debug.m_AutonomousRecoveryKnown ? 1 : 0,
				Debug.m_AutonomousRecoveryPossible ? 1 : 0, Debug.m_AutonomousSelfRecoveryProven ? 1 : 0,
				static_cast<long long>(Debug.m_SelfSafeEvidenceTick), static_cast<long long>(Debug.m_FirstPhysicalChangeTick), Debug.m_Reason.c_str());
		}
		if(Debug.m_DebugLabel == "SAVE_INTERACTION" || Debug.m_DebugLabel == "SAVE_RESTORE" ||
			Debug.m_DebugLabel == "SAVE_PENDING" || Debug.m_DebugLabel == "SAVE_CONFIRMED" ||
			Debug.m_DebugLabel == "SAVE_REJECTED" || Debug.m_DebugLabel == "SAVE_REJECTED_REFREEZE")
		{
			ImpactDebugLog("SAVE_DECISION source_tick=%lld event_tick=%lld target=%d generation=%u team_id=%d episode_id=%llu actor=%d contributors=%s lifecycle=%s pre_action_safe=%d pre_action_safe_source=%s native_pre_action_result=%s self_safe_tick=%lld first_physical_change_tick=%lld episode_self_safe=%d recovery_evaluated=%d recovery_known=%d recovery_possible=%d platform_preexisted_before_action=%d freeze_refresh_observed_before_action=%d physical_change=%d action_tick=%lld restore_tick=%lld confirmed_ticks=%lld required_ticks=%lld result=%s reason='%s'",
				static_cast<long long>(ServerTick), static_cast<long long>(EmissionTick), Target.m_ClientId, Target.m_SlotGeneration, Debug.m_TeamId,
				static_cast<unsigned long long>(Debug.m_RelatedRecoveryEpisodeId != 0 ? Debug.m_RelatedRecoveryEpisodeId : Debug.m_EpisodeId),
				Saver.m_ClientId, Contributors.c_str(), Phase2DebugEventTypeName(Debug), Debug.m_PreActionSafe ? 1 : 0,
				Debug.m_PreActionSafeSource.empty() ? "NONE" : Debug.m_PreActionSafeSource.c_str(),
				Debug.m_NativePreActionResult.empty() ? "NOT_EVALUATED" : Debug.m_NativePreActionResult.c_str(),
				static_cast<long long>(Debug.m_SelfSafeEvidenceTick), static_cast<long long>(Debug.m_FirstPhysicalChangeTick),
				Debug.m_AutonomousSelfRecoveryProven ? 1 : 0, Debug.m_AutonomousRecoveryEvaluated ? 1 : 0,
				Debug.m_AutonomousRecoveryKnown ? 1 : 0, Debug.m_AutonomousRecoveryPossible ? 1 : 0,
				Debug.m_PlatformPreexistedBeforeAction ? 1 : 0, Debug.m_FreezeRefreshObservedBeforeAction ? 1 : 0,
				Debug.m_PhysicalChangeEvidence ? 1 : 0, static_cast<long long>(Debug.m_ActionTick), static_cast<long long>(Debug.m_RestorationTick),
				static_cast<long long>(Debug.m_ConfirmedTicks), static_cast<long long>(Debug.m_RequiredTicks), Phase2SaveDecisionResult(Debug), Debug.m_Reason.c_str());
		}

		if(Debug.m_RelatedFallEpisodeId != 0 || Debug.m_RelatedRecoveryEpisodeId != 0)
		{
			ImpactDebugLog("EPISODE_BINDING source_tick=%lld event_tick=%lld target=%d generation=%u harm_episode_id=%llu recovery_episode_id=%llu episode_id=%llu event_type=%s reason='%s'",
				static_cast<long long>(ServerTick), static_cast<long long>(EmissionTick), Target.m_ClientId, Target.m_SlotGeneration,
				static_cast<unsigned long long>(Debug.m_RelatedFallEpisodeId),
				static_cast<unsigned long long>(Debug.m_RelatedRecoveryEpisodeId),
				static_cast<unsigned long long>(Debug.m_EpisodeId), Phase2DebugEventTypeName(Debug), Debug.m_Reason.c_str());
		}

		if(Debug.m_HookEvidence || !Debug.m_vHookSpans.empty())
		{
			ImpactDebugLog("SAVE_STATE server_tick=%lld episode_id=%llu target=%d generation=%u actor=%d actor_generation=%u recovery_need=%s frozen=%d natural_unfreeze=%d hammer_unfreeze=%d unknown_unfreeze=%d hammer_target_evidence_known=%d hook_span_count=%zu hook_control_continuity=%d",
				static_cast<long long>(ServerTick), static_cast<unsigned long long>(Debug.m_EpisodeId), Target.m_ClientId, Target.m_SlotGeneration,
				Saver.m_ClientId, Saver.m_SlotGeneration, RecoveryNeedName(Debug.m_RecoveryNeed), Debug.m_FrozenNow ? 1 : 0,
				Debug.m_UnfreezeCause == EImpactUnfreezeCause::NATURAL ? 1 : 0, Debug.m_UnfreezeCause == EImpactUnfreezeCause::HAMMER ? 1 : 0,
				Debug.m_UnfreezeCause == EImpactUnfreezeCause::UNKNOWN ? 1 : 0, Debug.m_HammerTargetEvidenceKnown ? 1 : 0, Debug.m_vHookSpans.size(), Debug.m_HookControlContinuity ? 1 : 0);
			for(size_t SpanIndex = 0; SpanIndex < Debug.m_vHookSpans.size(); ++SpanIndex)
			{
				const SImpactHookSpanDebug &Span = Debug.m_vHookSpans[SpanIndex];
				ImpactDebugLog("HOOK_SPAN server_tick=%lld episode_id=%llu span_id=%zu start_tick=%lld end_tick=%lld last_grabbed_tick=%lld actor=%d actor_generation=%u target=%d target_generation=%u grab_count=%u release=%d rehook=%d explicit_control_evidence=%d active=%d",
					static_cast<long long>(ServerTick), static_cast<unsigned long long>(Debug.m_EpisodeId), SpanIndex + 1,
					static_cast<long long>(Span.m_StartTick), static_cast<long long>(Span.m_EndTick), static_cast<long long>(Span.m_LastGrabbedTick),
					Span.m_Actor.m_ClientId, Span.m_Actor.m_SlotGeneration, Span.m_Target.m_ClientId, Span.m_Target.m_SlotGeneration,
					Span.m_ConfirmedObservations, Span.m_EndTick >= 0 ? 1 : 0, Span.m_Rehook ? 1 : 0,
					Span.m_MeaningfulControl ? 1 : 0, Span.m_Active ? 1 : 0);
			}
		}
		if((Debug.m_DebugLabel == "SAVE_RESULT" && (Debug.m_HookEvidence || !Debug.m_vHookSpans.empty())) ||
			((Debug.m_DebugLabel == "SAVE_PENDING" || Debug.m_DebugLabel == "SAVE_CONFIRMED") &&
				(Debug.m_EventType == EImpactEventType::UPDOWN_CONFIRMED || Debug.m_HookEvidence)))
		{
			const bool Save = Debug.m_Stage == EImpactPhase2DebugStage::CONFIRMED &&
				(Debug.m_EventType == EImpactEventType::SAVE_CONFIRMED || Debug.m_EventType == EImpactEventType::ALED_CONFIRMED);
			ImpactDebugLog("SAVE_RESULT server_tick=%lld episode_id=%llu target=%d actor=%d save=%d reason='%s'",
				static_cast<long long>(ServerTick), static_cast<unsigned long long>(Debug.m_EpisodeId), Target.m_ClientId, Saver.m_ClientId, Save ? 1 : 0, Debug.m_Reason.c_str());
		}
	}

	void CImpactSystem::LogLiveFrameTransitions(const std::vector<SImpactGameplayFrame> &vFrames)
	{
		if(!g_Config.m_Debug)
			return;

		if(m_LastDebugFrameCount != static_cast<int>(vFrames.size()))
		{
			ImpactDebugLog("PHASE2_FRAME_SET server_tick=%lld frame_count=%zu", static_cast<long long>(CurrentTick()), vFrames.size());
			m_LastDebugFrameCount = static_cast<int>(vFrames.size());
		}

		std::array<bool, MAX_CLIENTS> aSeen{};
		for(const SImpactGameplayFrame &Frame : vFrames)
		{
			const int ClientId = Frame.m_Participant.m_ClientId;
			if(ClientId < 0 || ClientId >= MAX_CLIENTS)
				continue;
			aSeen[ClientId] = true;

			const int FrameState =
				(Frame.m_Present ? 1 : 0) |
				(Frame.m_CurrentCharacterKnown ? 2 : 0) |
				(Frame.m_PreviousCharacterKnown ? 4 : 0) |
				(Frame.m_PositionKnown ? 8 : 0) |
				(Frame.m_VelocityKnown ? 16 : 0) |
				(Frame.m_AliveKnown ? 32 : 0) |
				(Frame.m_Alive ? 64 : 0) |
				(Frame.m_FreezeKnown ? 128 : 0) |
				(Frame.m_Frozen ? 256 : 0) |
				(Frame.m_DeepFrozen ? 512 : 0) |
				(Frame.m_SupportKnown ? 1024 : 0) |
				(Frame.m_Supported ? 2048 : 0) |
				(Frame.m_SoloKnown ? 4096 : 0) |
				(Frame.m_Solo ? 8192 : 0) |
				(Frame.m_Eligible ? 16384 : 0) |
				(Frame.m_AutonomousRecoveryKnown ? 32768 : 0) |
				(Frame.m_AutonomousRecoveryPossible ? 65536 : 0);
			const bool FrameChanged = m_aLastFrameDebugState[ClientId] != FrameState || m_aLastDebugFrameTeam[ClientId] != Frame.m_TeamId;

			int HookedByCount = 0;
			for(const SImpactGameplayFrame &Actor : vFrames)
				if(Actor.m_HookKnown && Actor.m_HookState == HOOK_GRABBED && Actor.m_HookedPlayer == ClientId)
					++HookedByCount;

			if(FrameChanged)
			{
				ImpactDebugLog("FRAME_STATE server_tick=%lld client_id=%d generation=%u team_id=%d present=%d current_character=%d previous_character=%d alive_known=%d alive=%d freeze_known=%d frozen=%d deep_frozen=%d supported=%d position=%d,%d eligible=%d solo_known=%d solo=%d",
					static_cast<long long>(Frame.m_ServerTick), ClientId, Frame.m_Participant.m_SlotGeneration, Frame.m_TeamId,
					Frame.m_Present ? 1 : 0, Frame.m_CurrentCharacterKnown ? 1 : 0, Frame.m_PreviousCharacterKnown ? 1 : 0,
					Frame.m_AliveKnown ? 1 : 0, Frame.m_Alive ? 1 : 0, Frame.m_FreezeKnown ? 1 : 0, Frame.m_Frozen ? 1 : 0,
					Frame.m_DeepFrozen ? 1 : 0, Frame.m_Supported ? 1 : 0, Frame.m_X, Frame.m_Y, Frame.m_Eligible ? 1 : 0,
					Frame.m_SoloKnown ? 1 : 0, Frame.m_Solo ? 1 : 0);
				ImpactDebugLog("PHASE2_FRAME server_tick=%lld client_id=%d generation=%u team_id=%d eligible=%d frozen=%d deep_frozen=%d recovery_need=%s hooked_by_count=%d interaction_available=%d",
					static_cast<long long>(Frame.m_ServerTick), ClientId, Frame.m_Participant.m_SlotGeneration, Frame.m_TeamId,
					Frame.m_Eligible ? 1 : 0, Frame.m_Frozen ? 1 : 0, Frame.m_DeepFrozen ? 1 : 0,
					RecoveryNeedName(Frame.m_RecoveryNeed), HookedByCount, HookedByCount > 0 ? 1 : 0);
			}

			const int FreezeState = !Frame.m_FreezeKnown ? 0 : Frame.m_Frozen ? 2 : 1;
			if(m_aLastDebugFreezeState[ClientId] != FreezeState)
			{
				ImpactDebugLog("FREEZE_RAW server_tick=%lld client_id=%d generation=%u team_id=%d old_state=%s new_state=%s freeze_end=%lld deep_frozen=%d supported=%d position=%d,%d",
					static_cast<long long>(Frame.m_ServerTick), ClientId, Frame.m_Participant.m_SlotGeneration, Frame.m_TeamId,
					ImpactRawFreezeStateName(m_aLastDebugFreezeState[ClientId]), ImpactRawFreezeStateName(FreezeState),
					static_cast<long long>(Frame.m_FreezeEnd), Frame.m_DeepFrozen ? 1 : 0, Frame.m_Supported ? 1 : 0, Frame.m_X, Frame.m_Y);
			}

			const int HookKnown = Frame.m_HookKnown ? 1 : 0;
			const bool HookChanged = m_aLastDebugHookKnown[ClientId] != HookKnown ||
				m_aLastDebugHookState[ClientId] != Frame.m_HookState || m_aLastDebugHookedPlayer[ClientId] != Frame.m_HookedPlayer;
			if(HookChanged)
			{
				const SImpactGameplayFrame *pTargetFrame = nullptr;
				for(const SImpactGameplayFrame &Target : vFrames)
					if(Target.m_Participant.m_ClientId == Frame.m_HookedPlayer)
					{
						pTargetFrame = &Target;
						break;
					}
				const bool TargetExists = pTargetFrame != nullptr && pTargetFrame->m_Present;
				const bool SameTeam = TargetExists && pTargetFrame->m_TeamId == Frame.m_TeamId;
				const int TargetGeneration = pTargetFrame != nullptr ? static_cast<int>(pTargetFrame->m_Participant.m_SlotGeneration) :
					(Frame.m_HookedPlayer >= 0 && Frame.m_HookedPlayer < MAX_CLIENTS ? static_cast<int>(m_aSlotGeneration[Frame.m_HookedPlayer]) : 0);
				const bool WasGrabbed = m_aLastDebugHookKnown[ClientId] == 1 && m_aLastDebugHookState[ClientId] == HOOK_GRABBED && m_aLastDebugHookedPlayer[ClientId] >= 0;
				const bool IsGrabbed = HookKnown && Frame.m_HookState == HOOK_GRABBED && Frame.m_HookedPlayer >= 0;
				const char *pTransition = "INITIAL";
				if(!WasGrabbed && IsGrabbed)
					pTransition = "HOOK_GRABBED";
				else if(WasGrabbed && !IsGrabbed)
					pTransition = "HOOK_RELEASED";
				else if(m_aLastDebugHookedPlayer[ClientId] != Frame.m_HookedPlayer)
					pTransition = "HOOKED_PLAYER_CHANGED";
				else if(m_aLastDebugHookKnown[ClientId] != HookKnown || m_aLastDebugHookState[ClientId] != Frame.m_HookState)
					pTransition = "HOOK_STATE_CHANGED";
				ImpactDebugLog("HOOK_RAW server_tick=%lld actor_client_id=%d actor_generation=%u team_id=%d hook_state=%d hooked_player=%d target_exists=%d target_generation=%d same_team=%d actor_eligible=%d target_eligible=%d transition=%s",
					static_cast<long long>(Frame.m_ServerTick), ClientId, Frame.m_Participant.m_SlotGeneration, Frame.m_TeamId,
					Frame.m_HookState, Frame.m_HookedPlayer, TargetExists ? 1 : 0, TargetGeneration, SameTeam ? 1 : 0,
					Frame.m_Eligible ? 1 : 0, pTargetFrame != nullptr && pTargetFrame->m_Eligible ? 1 : 0, pTransition);
				if(!WasGrabbed && IsGrabbed && pTargetFrame != nullptr && SameTeam)
					LogRawHookActionObservation(Frame, *pTargetFrame);
			}

			m_aLastFrameDebugState[ClientId] = FrameState;
			m_aLastDebugFrameTeam[ClientId] = Frame.m_TeamId;
			m_aLastDebugFreezeState[ClientId] = FreezeState;
			m_aLastDebugHookKnown[ClientId] = HookKnown;
			m_aLastDebugHookState[ClientId] = Frame.m_HookState;
			m_aLastDebugHookedPlayer[ClientId] = Frame.m_HookedPlayer;
		}

		for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
		{
			if(aSeen[ClientId])
				continue;
			if(m_aLastFrameDebugState[ClientId] != -1)
			{
				ImpactDebugLog("FRAME_STATE server_tick=%lld client_id=%d generation=%u team_id=%d present=0 current_character=0 previous_character=0 alive_known=0 alive=0 freeze_known=0 frozen=0 deep_frozen=0 supported=0 position=0,0 eligible=0 reason='frame absent from current team snapshot'",
					static_cast<long long>(CurrentTick()), ClientId, m_aSlotGeneration[ClientId], m_aLastDebugFrameTeam[ClientId]);
			}
			if(m_aLastDebugFreezeState[ClientId] != -1 && m_aLastDebugFreezeState[ClientId] != 0)
				ImpactDebugLog("FREEZE_RAW server_tick=%lld client_id=%d generation=%u team_id=%d old_state=%s new_state=UNKNOWN freeze_end=0 deep_frozen=0 supported=0 position=0,0 reason='frame absent from current team snapshot'",
					static_cast<long long>(CurrentTick()), ClientId, m_aSlotGeneration[ClientId], m_aLastDebugFrameTeam[ClientId],
					ImpactRawFreezeStateName(m_aLastDebugFreezeState[ClientId]));
			if(m_aLastDebugHookKnown[ClientId] == 1 && m_aLastDebugHookState[ClientId] == HOOK_GRABBED && m_aLastDebugHookedPlayer[ClientId] >= 0)
				ImpactDebugLog("HOOK_RAW server_tick=%lld actor_client_id=%d actor_generation=%u team_id=%d hook_state=%d hooked_player=-1 target_exists=0 target_generation=0 same_team=0 actor_eligible=0 target_eligible=0 transition=HOOK_RELEASED reason='actor frame absent from current team snapshot'",
					static_cast<long long>(CurrentTick()), ClientId, m_aSlotGeneration[ClientId], m_aLastDebugFrameTeam[ClientId], HOOK_IDLE);
			m_aLastFrameDebugState[ClientId] = -1;
			m_aLastDebugFrameTeam[ClientId] = -1;
			m_aLastDebugFreezeState[ClientId] = -1;
			m_aLastDebugHookKnown[ClientId] = -1;
			m_aLastDebugHookState[ClientId] = -1;
			m_aLastDebugHookedPlayer[ClientId] = -1;
		}
	}

	void CImpactSystem::LogRawHookActionObservation(const SImpactGameplayFrame &Actor, const SImpactGameplayFrame &Target)
	{
		const int ActorId = Actor.m_Participant.m_ClientId;
		const int TargetId = Target.m_Participant.m_ClientId;
		if(ActorId < 0 || ActorId >= MAX_CLIENTS || TargetId < 0 || TargetId >= MAX_CLIENTS)
			return;
		const SRawAuthoritativeCharacterObservation &ActorPrevious = GameClient()->m_RawAuthoritativeSnapshotAdapter.Previous()[ActorId];
		const SRawAuthoritativeCharacterObservation &ActorCurrent = GameClient()->m_RawAuthoritativeSnapshotAdapter.Current()[ActorId];
		const SRawAuthoritativeCharacterObservation &TargetPrevious = GameClient()->m_RawAuthoritativeSnapshotAdapter.Previous()[TargetId];
		const SRawAuthoritativeCharacterObservation &TargetCurrent = GameClient()->m_RawAuthoritativeSnapshotAdapter.Current()[TargetId];
		const bool StrictlyBeforeObservedAction = TargetPrevious.m_Valid && TargetCurrent.m_Valid &&
			TargetPrevious.m_ObjectTick < TargetCurrent.m_ObjectTick;
		const int64_t PreviousTick = TargetPrevious.m_Valid ? TargetPrevious.m_ObjectTick : -1;
		const int64_t CurrentTick = TargetCurrent.m_Valid ? TargetCurrent.m_ObjectTick : -1;
		const int64_t TickGap = TargetPrevious.m_Valid && TargetCurrent.m_Valid ? CurrentTick - PreviousTick : -1;
		const bool PreviousRawOrdinaryFrozen = TargetPrevious.m_Valid && TargetPrevious.m_HasExtendedData &&
			TargetPrevious.m_ExtendedData.m_FreezeEnd > PreviousTick && TargetPrevious.m_ExtendedData.m_FreezeEnd > 0 &&
			TargetPrevious.m_ExtendedData.m_FreezeEnd != -1;
		const vec2 PreviousRawPosition(TargetPrevious.m_Character.m_X, TargetPrevious.m_Character.m_Y);
		const bool PreviousRawSupported = PreviousRawOrdinaryFrozen && GameClient()->Collision() != nullptr &&
			GameClient()->Collision()->IsOnGround(PreviousRawPosition, CCharacterCore::PhysicalSize());
		const bool PreviousRawSafe = PreviousRawSupported && IsSafeImpactPosition(GameClient(), PreviousRawPosition);
		const bool PreviousRawFreezeTile = TargetPrevious.m_Valid && GameClient()->Collision() != nullptr &&
			HasImpactTileAtPosition(GameClient(), PreviousRawPosition, TILE_FREEZE);
		const bool ActorWasNotHookingTarget = !ActorPrevious.m_Valid ||
			(ActorPrevious.m_Character.m_HookState != HOOK_GRABBED && ActorPrevious.m_Character.m_HookedPlayer != TargetId);
		const bool PlatformPreexistedAction = StrictlyBeforeObservedAction && TargetPrevious.m_Present && TargetPrevious.m_Alive &&
			PreviousRawOrdinaryFrozen && PreviousRawSafe && !PreviousRawFreezeTile &&
			TargetPrevious.m_Character.m_HookState != HOOK_GRABBED && ActorWasNotHookingTarget;
		const int PreviousHookState = ActorPrevious.m_Valid ? ActorPrevious.m_Character.m_HookState : -1;
		const int PreviousHookedPlayer = ActorPrevious.m_Valid ? ActorPrevious.m_Character.m_HookedPlayer : -1;
		const int CurrentHookState = ActorCurrent.m_Valid ? ActorCurrent.m_Character.m_HookState : -1;
		const int CurrentHookedPlayer = ActorCurrent.m_Valid ? ActorCurrent.m_Character.m_HookedPlayer : -1;
		const int64_t FirstObservedTick = Actor.m_ServerTick;
		ImpactDebugLog("HOOK_ACTION_OBSERVATION actor=%d target=%d server_tick=%lld raw_previous_tick=%lld raw_current_tick=%lld raw_tick_gap=%lld actor_raw_hook_state_previous=%d actor_raw_hook_state_current=%d actor_raw_hooked_player_previous=%d actor_raw_hooked_player_current=%d target_raw_position_previous=%d,%d target_raw_position_current=%d,%d target_raw_velocity_previous=%d,%d target_raw_velocity_current=%d,%d target_supported_current=%d target_safe_collision_current=%d target_freeze_tile_current=%d target_frozen_current=%d hook_transition_first_observed_tick=%lld intra_tick_order_known=0 strictly_before_observed_action=%d authoritative_pre_action=%d platform_preexisted_action_from_prior_authoritative_frame=%d",
			ActorId, TargetId, static_cast<long long>(Actor.m_ServerTick), PreviousTick, CurrentTick, TickGap,
			PreviousHookState, CurrentHookState, PreviousHookedPlayer, CurrentHookedPlayer,
			TargetPrevious.m_Valid ? TargetPrevious.m_Character.m_X : 0, TargetPrevious.m_Valid ? TargetPrevious.m_Character.m_Y : 0,
			TargetCurrent.m_Valid ? TargetCurrent.m_Character.m_X : 0, TargetCurrent.m_Valid ? TargetCurrent.m_Character.m_Y : 0,
			TargetPrevious.m_Valid ? TargetPrevious.m_Character.m_VelX : 0, TargetPrevious.m_Valid ? TargetPrevious.m_Character.m_VelY : 0,
			TargetCurrent.m_Valid ? TargetCurrent.m_Character.m_VelX : 0, TargetCurrent.m_Valid ? TargetCurrent.m_Character.m_VelY : 0,
			Target.m_Supported ? 1 : 0, Target.m_SafeCollisionPosition ? 1 : 0, Target.m_OrdinaryFreezeTile ? 1 : 0,
			Target.m_Frozen ? 1 : 0, static_cast<long long>(FirstObservedTick), StrictlyBeforeObservedAction ? 1 : 0,
			StrictlyBeforeObservedAction ? 1 : 0, PlatformPreexistedAction ? 1 : 0);
		ImpactDebugLog("PRE_ACTION_EVIDENCE actor=%d target=%d action_first_observed_tick=%lld last_clean_raw_tick=%lld last_clean_raw_position=%d,%d last_clean_raw_velocity=%d,%d tick_gap_to_action=%lld strictly_before_observed_action=%d authoritative=%d platform_preexisted_action_from_prior_authoritative_frame=%d reason='raw previous snapshot precedes the first observed hook state; it does not prove intra-tick server causality'",
			ActorId, TargetId, static_cast<long long>(FirstObservedTick), PreviousTick,
			TargetPrevious.m_Valid ? TargetPrevious.m_Character.m_X : 0, TargetPrevious.m_Valid ? TargetPrevious.m_Character.m_Y : 0,
			TargetPrevious.m_Valid ? TargetPrevious.m_Character.m_VelX : 0, TargetPrevious.m_Valid ? TargetPrevious.m_Character.m_VelY : 0,
			TickGap, StrictlyBeforeObservedAction ? 1 : 0, TargetPrevious.m_Valid ? 1 : 0, PlatformPreexistedAction ? 1 : 0);
		if(Actor.m_ServerTick == Target.m_ServerTick && Target.m_Supported && Target.m_SafeCollisionPosition)
		{
			ImpactDebugLog("SAME_TICK_CAUSALITY actor=%d target=%d tick=%lld current_supported=%d current_safe_collision=%d current_freeze_tile=%d previous_raw_tick=%lld previous_raw_position=%d,%d previous_raw_velocity=%d,%d current_raw_position=%d,%d current_raw_velocity=%d,%d hook_first_observed_tick=%lld intra_tick_order_known=0 can_prove_platform_preexisted_hook=0 can_prove_hook_created_platform=0 platform_preexisted_action_from_prior_authoritative_frame=%d dual_replay_skipped=1 reason='client snapshot does not expose movement/collision/hook ordering inside the server tick'",
				ActorId, TargetId, static_cast<long long>(Actor.m_ServerTick), Target.m_Supported ? 1 : 0, Target.m_SafeCollisionPosition ? 1 : 0,
				Target.m_OrdinaryFreezeTile ? 1 : 0, PreviousTick,
				TargetPrevious.m_Valid ? TargetPrevious.m_Character.m_X : 0, TargetPrevious.m_Valid ? TargetPrevious.m_Character.m_Y : 0,
				TargetPrevious.m_Valid ? TargetPrevious.m_Character.m_VelX : 0, TargetPrevious.m_Valid ? TargetPrevious.m_Character.m_VelY : 0,
				TargetCurrent.m_Valid ? TargetCurrent.m_Character.m_X : 0, TargetCurrent.m_Valid ? TargetCurrent.m_Character.m_Y : 0,
				TargetCurrent.m_Valid ? TargetCurrent.m_Character.m_VelX : 0, TargetCurrent.m_Valid ? TargetCurrent.m_Character.m_VelY : 0,
				static_cast<long long>(FirstObservedTick), PlatformPreexistedAction ? 1 : 0);
		}
	}

	void CImpactSystem::RunShadowAutonomousDiagnostics(std::vector<SImpactGameplayFrame> &vFrames)
	{
		// This bound is only a safety boundary for a non-positive diagnostic result.
		// It is never a Fall/Save timeout; only SELF_SAFE can affect Phase2.
		constexpr int MAX_SAFE_SIMULATION_TICKS = SERVER_TICK_SPEED * 12;
		for(SImpactGameplayFrame &Frame : vFrames)
		{
			if(!Frame.m_Frozen || Frame.m_AutonomousRecoveryKnown || !Frame.m_Participant.IsValid() ||
				!m_Phase2.DebugHasActiveFrozenEpisode(Frame.m_Participant))
				continue;

			const int ClientId = Frame.m_Participant.m_ClientId;
			const SRawAuthoritativeCharacterObservation &RawCurrent = GameClient()->m_RawAuthoritativeSnapshotAdapter.Current()[ClientId];
			const SRawAuthoritativeCharacterObservation &RawPrevious = GameClient()->m_RawAuthoritativeSnapshotAdapter.Previous()[ClientId];
			bool ExternalInteractionActive = false;
			bool CurrentHammerInteraction = false;
			int NewHookActorCount = 0;
			for(const SImpactGameplayFrame &Actor : vFrames)
			{
				if(Actor.m_Participant == Frame.m_Participant)
					continue;
				const bool HookEvidence = Actor.m_HookKnown && Actor.m_HookState == HOOK_GRABBED && Actor.m_HookedPlayer == ClientId;
				const bool HammerEvidence = Actor.m_HammerTargetEvidenceKnown && Actor.m_HammerTargetEvidence;
				ExternalInteractionActive |= HookEvidence || HammerEvidence;
				CurrentHammerInteraction |= HammerEvidence;
				if(!HookEvidence || !IsAledResearchClientIdValid(Actor.m_Participant.m_ClientId))
					continue;
				const SRawAuthoritativeCharacterObservation &ActorCurrent = GameClient()->m_RawAuthoritativeSnapshotAdapter.Current()[Actor.m_Participant.m_ClientId];
				const SRawAuthoritativeCharacterObservation &ActorPrevious = GameClient()->m_RawAuthoritativeSnapshotAdapter.Previous()[Actor.m_Participant.m_ClientId];
				const bool AlignedSnapshots = ActorPrevious.m_Valid && ActorPrevious.m_FromRawPreviousSnapshot &&
					ActorCurrent.m_Valid && ActorCurrent.m_FromRawCurrentSnapshot &&
					ActorPrevious.m_SnapshotServerTick == RawPrevious.m_SnapshotServerTick &&
					ActorCurrent.m_SnapshotServerTick == RawCurrent.m_SnapshotServerTick &&
					ActorCurrent.m_SnapshotServerTick > ActorPrevious.m_SnapshotServerTick &&
					ActorCurrent.m_ObjectTick > ActorPrevious.m_ObjectTick;
				const bool WasAlreadyHookingTarget = ActorPrevious.m_Character.m_HookState == HOOK_GRABBED &&
					ActorPrevious.m_Character.m_HookedPlayer == ClientId;
				const bool RawCurrentHooksTarget = ActorCurrent.m_Character.m_HookState == HOOK_GRABBED &&
					ActorCurrent.m_Character.m_HookedPlayer == ClientId;
				if(AlignedSnapshots && !WasAlreadyHookingTarget && RawCurrentHooksTarget)
					++NewHookActorCount;
			}
			bool PreviousTargetHookActive = false;
			for(int ActorId = 0; ActorId < MAX_CLIENTS; ++ActorId)
			{
				const SRawAuthoritativeCharacterObservation &ActorPrevious = GameClient()->m_RawAuthoritativeSnapshotAdapter.Previous()[ActorId];
				if(ActorPrevious.m_Valid && ActorPrevious.m_FromRawPreviousSnapshot && ActorPrevious.m_Present && ActorPrevious.m_Alive &&
					ActorPrevious.m_Character.m_HookState == HOOK_GRABBED && ActorPrevious.m_Character.m_HookedPlayer == ClientId)
				{
					PreviousTargetHookActive = true;
					break;
				}
			}
			// If exactly one teammate begins a hook in this snapshot, and no teammate
			// hook was active in the preceding raw snapshot, replay the target's prior
			// authoritative state without the new interaction. This native DDNet
			// counterfactual can prove a trajectory was already SELF_SAFE before that
			// actor touched it. Multiple/uncertain interactions remain UNKNOWN.
			const bool PreActionReplay = ExternalInteractionActive && !CurrentHammerInteraction &&
				NewHookActorCount == 1 && !PreviousTargetHookActive;
			const SRawAuthoritativeCharacterObservation &Raw = PreActionReplay ? RawPrevious : RawCurrent;
			const auto LogSkipped = [&](const char *pReason) {
				if(!g_Config.m_Debug)
					return;
				ImpactDebugLog("SHADOW_AUTONOMOUS target=%d generation=%u source_raw_tick=%lld simulation_used=0 simulation_skipped=1 simulation_ticks=0 terminal=AMBIGUOUS terminal_position=0,0 terminal_velocity=0,0 terminal_supported=0 terminal_tile=-1 result=UNRESOLVED reason='%s' shadow_simulations=%llu shadow_total_ticks=%llu",
					ClientId, Frame.m_Participant.m_SlotGeneration, static_cast<long long>(Raw.m_ObjectTick), pReason,
					static_cast<unsigned long long>(m_ShadowSimulationCount), static_cast<unsigned long long>(m_ShadowSimulationTotalTicks));
			};

			if(!Raw.m_Valid || (PreActionReplay ? !Raw.m_FromRawPreviousSnapshot : !Raw.m_FromRawCurrentSnapshot))
			{
				LogSkipped(PreActionReplay ? "pre_action_raw_previous_snapshot_missing" : "raw_current_snapshot_missing");
				continue;
			}
			if(PreActionReplay && (!RawCurrent.m_Valid || !RawCurrent.m_FromRawCurrentSnapshot ||
				RawCurrent.m_SnapshotServerTick <= Raw.m_SnapshotServerTick || RawCurrent.m_ObjectTick <= Raw.m_ObjectTick ||
				Raw.m_SnapshotServerTick != GameClient()->m_RawAuthoritativeSnapshotAdapter.Previous()[ClientId].m_SnapshotServerTick ||
				RawCurrent.m_TeamId != Frame.m_TeamId || Raw.m_TeamId != Frame.m_TeamId ||
				!RawCurrent.m_HasExtendedData || RawCurrent.m_ExtendedData.m_FreezeEnd != Raw.m_ExtendedData.m_FreezeEnd ||
				RawCurrent.m_ExtendedData.m_FreezeEnd <= RawCurrent.m_ObjectTick))
			{
				LogSkipped("pre_action_raw_snapshots_not_aligned_or_freeze_changed");
				continue;
			}
			if(!Raw.m_HasExtendedData)
			{
				LogSkipped("raw_extended_character_missing");
				continue;
			}
			if(Raw.m_ObjectTick < 0)
			{
				LogSkipped("raw_object_tick_missing");
				continue;
			}
		const bool DeepFrozen = Raw.m_ExtendedData.m_FreezeEnd == -1;
		const bool LiveFrozen = (Raw.m_ExtendedData.m_Flags & CHARACTERFLAG_MOVEMENTS_DISABLED) != 0;
		if(DeepFrozen || LiveFrozen)
		{
				LogSkipped(DeepFrozen ? "deep_freeze_transition_or_state" : "live_freeze_input_can_matter");
				continue;
		}
		if(Raw.m_ExtendedData.m_FreezeEnd <= Raw.m_ObjectTick)
			{
				LogSkipped("finite_authoritative_freeze_end_missing");
				continue;
			}
			if(Raw.m_Character.m_HookState == HOOK_GRABBED || Raw.m_Character.m_HookedPlayer >= 0)
			{
				LogSkipped("target_has_own_hook_state");
				continue;
			}
			const CCharacter *pLiveCharacter = GameClient()->m_GameWorld.GetCharacterById(ClientId);
			if(!PreActionReplay && pLiveCharacter == nullptr)
			{
				LogSkipped("target_native_character_unavailable");
				continue;
			}
			if(!PreActionReplay && pLiveCharacter->m_CanMoveInFreeze)
			{
				LogSkipped("native_character_can_move_in_freeze");
				continue;
			}
			if(ExternalInteractionActive && !PreActionReplay)
		{
			LogSkipped("external_target_interaction_already_affects_target");
			continue;
		}
		if(GameClient()->m_GameWorld.Collision() == nullptr)
		{
			LogSkipped("target_native_collision_unavailable");
			continue;
		}
		if(GameClient()->m_GameWorld.m_WorldConfig.m_PredictFreeze == 0)
		{
			LogSkipped("native_freeze_prediction_disabled");
			continue;
		}
		// In AMF 19.9 CGameClient::UpdatePrediction explicitly set the main
		// authoritative world's m_PredictTeleports=false before component snapshot
		// processing. DDNet 20.1 removed that field; the legacy guard was therefore
		// unreachable on this production path.

		CGameWorld ShadowWorld;
		ShadowWorld.CopyWorldClean(&GameClient()->m_GameWorld);
		ShadowWorld.Clear();
		ShadowWorld.m_GameTick = Raw.m_ObjectTick;
		ShadowWorld.m_WorldConfig.m_PredictEvents = false;
		CNetObj_Character ShadowCharacter = Raw.m_Character;
		CNetObj_DDNetCharacter ShadowExtended = Raw.m_ExtendedData;
		// NetCharAdd only admits characters belonging to the clone's local team.
		// Make the isolated target slot the clone-local id; the CCharacter itself
		// remains non-local, so this changes only clone admission, not gameplay.
		ShadowWorld.NetObjBegin(GameClient()->m_Teams, ClientId);
		ShadowWorld.NetCharAdd(ClientId, &ShadowCharacter, &ShadowExtended, Frame.m_TeamId, false);
		ShadowWorld.NetObjEnd();
		CCharacter *pCharacter = ShadowWorld.GetCharacterById(ClientId);
		if(pCharacter == nullptr)
		{
			LogSkipped("native_target_clone_missing");
			continue;
		}
		if(PreActionReplay && pCharacter->m_CanMoveInFreeze)
		{
			LogSkipped("pre_action_native_character_can_move_in_freeze");
			continue;
		}

		const int StartTick = Raw.m_ObjectTick;
		const vec2 StartPosition = pCharacter->Core()->m_Pos;
		const vec2 StartVelocity = pCharacter->Core()->m_Vel;
		bool SafeSupportObserved = false;
		bool FreezeRefreshObserved = false;
		int PreviousFreezeTime = pCharacter->m_FreezeTime;
		int PreviousFreezeEnd = pCharacter->Core()->m_FreezeEnd;
		int SimulationTicks = 0;
		const char *pTerminal = "MAX_SAFE_SIMULATION_BOUNDARY";
		const char *pResult = "UNRESOLVED";
		const char *pReason = "native frozen-only simulation reached its diagnostic boundary";
		int TerminalTile = -1;
		bool TerminalSupported = false;
		vec2 TerminalPosition = StartPosition;
		vec2 TerminalVelocity = StartVelocity;
		const int FreezeEnd = Raw.m_ExtendedData.m_FreezeEnd;
		// Include the first state observation after the authoritative freeze end.
		// The previous limit stopped one tick before that boundary, turning an
		// otherwise deterministic natural unfreeze into UNRESOLVED.
		const int SimulationLimit = std::min(MAX_SAFE_SIMULATION_TICKS, std::max(1, FreezeEnd - StartTick + 3));

		for(int Step = 0; Step < SimulationLimit; ++Step)
		{
			pCharacter = ShadowWorld.GetCharacterById(ClientId);
			if(pCharacter == nullptr)
			{
				pTerminal = "DEATH";
				pResult = "UNRESOLVED";
				pReason = "native clone character disappeared";
				break;
			}
			const vec2 Position = pCharacter->Core()->m_Pos;
			const vec2 Velocity = pCharacter->Core()->m_Vel;
			const bool Frozen = pCharacter->m_FreezeTime > 0 || pCharacter->Core()->m_DeepFrozen || pCharacter->Core()->m_LiveFrozen;
			const bool Supported = ShadowWorld.Collision()->IsOnGround(Position, CCharacterCore::PhysicalSize());
			const bool SafePosition = IsSafeImpactPosition(GameClient(), Position);
			const bool FreezeTile = HasImpactTileAtPosition(GameClient(), Position, TILE_FREEZE);
			TerminalPosition = Position;
			TerminalVelocity = Velocity;
			TerminalSupported = Supported;
			TerminalTile = ShadowWorld.Collision()->GetPureMapIndex(Position);
			if(IsDeathImpactPosition(GameClient(), Position))
			{
				pTerminal = "PERSISTENT_HARMFUL_FREEZE";
				pResult = "UNRESOLVED";
				pReason = "native clone reached an authoritative death tile";
				break;
			}
			if(Frozen && Supported && SafePosition && !FreezeTile && !FreezeRefreshObserved)
				SafeSupportObserved = true;
			if(Frozen && (!Supported || !SafePosition || FreezeTile))
				SafeSupportObserved = false;
			if(!Frozen)
			{
				pTerminal = "NATURAL_UNFREEZE";
				// A native clone can first become supported on the same step on which
				// its ordinary freeze expires. That is already a decisive autonomous
				// safe boundary when the terminal position is safe and no freeze refresh
				// was observed; requiring a prior frozen supported frame loses the live
				// airborne-to-platform case. Active refresh remains non-self-safe.
				if(Supported && SafePosition && !FreezeTile && !FreezeRefreshObserved &&
					(SafeSupportObserved || !Frozen))
				{
					pResult = "SELF_SAFE";
					pReason = SafeSupportObserved ?
						"native clone reached natural unfreeze after safe autonomous support" :
						"native clone reached natural unfreeze at a safe supported autonomous boundary";
				}
				else
				{
					pResult = "UNRESOLVED";
					pReason = "native clone reached unfreeze before a safe autonomous boundary; future input may matter";
				}
				break;
			}

			ShadowWorld.m_GameTick = StartTick + SimulationTicks + 1;
			ShadowWorld.Tick();
			++SimulationTicks;
			pCharacter = ShadowWorld.GetCharacterById(ClientId);
			if(pCharacter == nullptr)
			{
				pTerminal = "DEATH";
				pResult = "UNRESOLVED";
				pReason = "native clone character disappeared during Tick";
				break;
			}
			const int CurrentFreezeTime = pCharacter->m_FreezeTime;
			const int CurrentFreezeEnd = pCharacter->Core()->m_FreezeEnd;
			if(CurrentFreezeTime > PreviousFreezeTime + 1 || CurrentFreezeEnd > PreviousFreezeEnd + 1)
			{
				FreezeRefreshObserved = true;
				SafeSupportObserved = false;
			}
			PreviousFreezeTime = CurrentFreezeTime;
			PreviousFreezeEnd = CurrentFreezeEnd;
		}

		m_ShadowSimulationCount++;
		m_ShadowSimulationTotalTicks += static_cast<uint64_t>(SimulationTicks);
		const bool PositiveSelfSafe = str_comp(pResult, "SELF_SAFE") == 0;
		// A previous-snapshot counterfactual is Save attribution metadata only. It
		// must not rewrite current RecoveryNeed or feed Fall/Clutch processing.
		if(PreActionReplay)
		{
			if(PositiveSelfSafe)
				Frame.m_PreActionAutonomousSelfSafeTick = Raw.m_ObjectTick;
		}
		else
		{
			// Preserve normal current-snapshot behavior for existing SELF_SAFE
			// diagnostics and consumers.
			Frame.m_AutonomousRecoveryEvaluated = true;
			if(PositiveSelfSafe)
			{
				Frame.m_AutonomousRecoveryKnown = true;
				Frame.m_AutonomousRecoveryPossible = true;
				Frame.m_RecoveryNeed = EImpactRecoveryNeed::SELF_SAFE;
			}
		}
		if(g_Config.m_Debug)
			ImpactDebugLog("SHADOW_AUTONOMOUS target=%d generation=%u source_raw_tick=%lld replay=%s start_position=%d,%d start_velocity=%d,%d ordinary_frozen=1 simulation_used=1 simulation_skipped=0 simulation_ticks=%d terminal=%s terminal_position=%d,%d terminal_velocity=%d,%d terminal_supported=%d terminal_tile=%d result=%s gameplay_self_safe_veto=%d save_actor_veto=%d reason='%s' shadow_simulations=%llu shadow_total_ticks=%llu",
				ClientId, Frame.m_Participant.m_SlotGeneration, static_cast<long long>(Raw.m_ObjectTick), PreActionReplay ? "PRE_ACTION" : "CURRENT",
				static_cast<int>(StartPosition.x), static_cast<int>(StartPosition.y), static_cast<int>(StartVelocity.x), static_cast<int>(StartVelocity.y), SimulationTicks, pTerminal,
				static_cast<int>(TerminalPosition.x), static_cast<int>(TerminalPosition.y), static_cast<int>(TerminalVelocity.x), static_cast<int>(TerminalVelocity.y),
				TerminalSupported ? 1 : 0, TerminalTile, pResult, !PreActionReplay && PositiveSelfSafe ? 1 : 0,
				PreActionReplay && PositiveSelfSafe ? 1 : 0, pReason,
				static_cast<unsigned long long>(m_ShadowSimulationCount), static_cast<unsigned long long>(m_ShadowSimulationTotalTicks));
		}
	}

	void CImpactSystem::ResetDiagnosticLogDeduplication()
	{
		m_KogDifficultyResultLastDiagnosticKey.clear();
		m_KogDifficultyUiLastDiagnosticKey.clear();
		for(std::string &Key : m_aLastAledActionLogKey)
			Key.clear();
		for(std::string &Key : m_aLastAledStateLogKey)
			Key.clear();
		for(std::string &Key : m_aLastAledRestoreLogKey)
			Key.clear();
		for(SAledResearchGeometryProbeRecord &Record : m_aAledGeometryProbe)
			Record = {};
		for(SAledHammerObservation &Observation : m_aLastAledHammerObservation)
			Observation = {};
	}

	void CImpactSystem::CaptureAledHammerObservations(const std::vector<SImpactGameplayFrame> &vFrames)
	{
		CGameClient *pGameClient = GameClient();
		if(pGameClient == nullptr)
			return;
		for(const SImpactGameplayFrame &Frame : vFrames)
		{
			const int ActorId = Frame.m_Participant.m_ClientId;
			if(!Frame.m_Participant.IsValid() || !IsAledResearchClientIdValid(ActorId) ||
				!Frame.m_AttackKnown || Frame.m_Weapon != WEAPON_HAMMER || Frame.m_AttackTick < 0)
				continue;
			const SRawAuthoritativeCharacterObservation &Current = pGameClient->m_RawAuthoritativeSnapshotAdapter.Current()[ActorId];
			const SRawAuthoritativeCharacterObservation &Previous = pGameClient->m_RawAuthoritativeSnapshotAdapter.Previous()[ActorId];
			const bool AttackEdge = Current.m_Valid && Current.m_FromRawCurrentSnapshot &&
				(!Previous.m_Valid || Current.m_Character.m_AttackTick != Previous.m_Character.m_AttackTick);
			if(!AttackEdge)
				continue;
			SAledHammerObservation &Observation = m_aLastAledHammerObservation[ActorId];
			Observation.m_Valid = true;
			Observation.m_ActorGeneration = Frame.m_Participant.m_SlotGeneration;
			Observation.m_AttackTick = Frame.m_AttackTick;
			Observation.m_ObjectTick = Current.m_ObjectTick;
			Observation.m_SnapshotSequence = Current.m_SnapshotSequence;
		}
	}

	void CImpactSystem::CaptureAledCandidate(const SImpactPhase2Debug &Debug)
	{
		if(Debug.m_DebugLabel != "SAVE_RESTORE" || Debug.m_EventType != EImpactEventType::SAVE_CONFIRMED ||
			!IsAledResearchClientIdValid(Debug.m_Target.m_ClientId))
			return;
		SAledResearchGeometryProbeRecord &Record = m_aAledGeometryProbe[Debug.m_Target.m_ClientId];
		if(!Record.m_Recorded || Record.m_ProblemEpisodeId != Debug.m_EpisodeId ||
			Record.m_RecoveryEpisodeId != Debug.m_RelatedRecoveryEpisodeId ||
			Record.m_TargetGeneration != Debug.m_Target.m_SlotGeneration)
		{
			Record = BuildAledResearchGeometryProbe(Debug);
			Record.m_Recorded = true;
		}
		if(!Record.m_V2Computed)
		{
			LogAledGeometryV2(Record);
			if(g_Config.m_Debug)
				Record.m_V2ProbeLogged = true;
		}
		Record.m_ActorGeneration = Debug.m_PotentialSaver.m_SlotGeneration;
		const int ActorId = Debug.m_PotentialSaver.m_ClientId;
		const bool ActorValid = Debug.m_PotentialSaver.IsValid() && IsAledResearchClientIdValid(ActorId) &&
			Debug.m_PotentialSaver != Debug.m_Target;
		const bool TargetValid = Debug.m_Target.IsValid() && IsAledResearchClientIdValid(Debug.m_Target.m_ClientId);
		const bool RecoveryValid = Debug.m_EpisodeId != 0 && Debug.m_RelatedRecoveryEpisodeId != 0 &&
			Debug.m_EpisodeId == Debug.m_RelatedRecoveryEpisodeId;
		if(!ActorValid || !TargetValid || !RecoveryValid)
			return;
		const SAledHammerObservation &Hammer = m_aLastAledHammerObservation[ActorId];
		const bool Candidate = IsAledV2CandidateEvidenceSufficient(
			ActorValid, TargetValid, RecoveryValid, Record.m_V2IsAledGeometryValid,
			Hammer.m_Valid && Hammer.m_ActorGeneration == Debug.m_PotentialSaver.m_SlotGeneration,
			Hammer.m_AttackTick, Debug.m_RestorationTick);
		if(g_Config.m_Debug && !Record.m_V2DecisionLogged)
		{
			ImpactDebugLog("ALED_V2_DECISION problem_episode_id=%llu recovery_episode_id=%llu actor=%d target=%d restore_tick=%lld hammer_tick=%lld sample_tick=%lld v2_is_aled=%d orientation=%s topology=%s status=%s reason=%s freeze_between=%d opposite_sides=%d corner_freeze_pair=%d legacy_axis=%s candidate_created=%d production_source=V2",
				static_cast<unsigned long long>(Record.m_ProblemEpisodeId), static_cast<unsigned long long>(Record.m_RecoveryEpisodeId),
				Record.m_ActorId, Record.m_TargetId, static_cast<long long>(Record.m_RestoreTick), static_cast<long long>(Hammer.m_AttackTick),
				static_cast<long long>(Record.m_TargetSampleTick), Record.m_V2IsAledGeometryValid ? 1 : 0,
				AledGeometryV2TopologyName(Record.m_V2Orientation), AledGeometryV2TopologyName(Record.m_V2Topology),
				AledGeometryV2StatusName(Record.m_V2Status), Record.m_V2Reason.c_str(), Record.m_V2Result.m_FreezeBetween,
				Record.m_V2Result.m_OppositeSides, Record.m_V2Result.m_CornerFreezePair, AledGeometryProbeAxisName(Record.m_BarrierAxis), Candidate ? 1 : 0);
			Record.m_V2DecisionLogged = true;
		}
		if(!Candidate)
			return;
		Record.m_HammerAttackTick = Hammer.m_AttackTick;
		Record.m_HammerObjectTick = Hammer.m_ObjectTick;
		Record.m_AledCandidate = true;
		if(g_Config.m_Debug && !Record.m_CandidateLogged)
		{
			LogAledCandidate(Record);
			Record.m_CandidateLogged = true;
		}
	}

	void CImpactSystem::QueueAledConfirmedEvent(const SImpactPhase2Debug &Debug, STickEventBatch &Batch)
	{
		if(Debug.m_Stage != EImpactPhase2DebugStage::CONFIRMED || Debug.m_EventType != EImpactEventType::SAVE_CONFIRMED ||
			!IsAledResearchClientIdValid(Debug.m_Target.m_ClientId))
			return;
		SAledResearchGeometryProbeRecord &Record = m_aAledGeometryProbe[Debug.m_Target.m_ClientId];
		if(!Record.m_Recorded || !Record.m_AledCandidate || Record.m_AledEventQueued || Record.m_AledConfirmed ||
			Record.m_ProblemEpisodeId != Debug.m_EpisodeId || Record.m_RecoveryEpisodeId != Debug.m_RelatedRecoveryEpisodeId ||
			Record.m_TargetGeneration != Debug.m_Target.m_SlotGeneration || Record.m_ActorId != Debug.m_PotentialSaver.m_ClientId ||
			Record.m_ActorGeneration != Debug.m_PotentialSaver.m_SlotGeneration)
			return;
		SImpactEvent *pSaveEvent = nullptr;
		for(SImpactEvent &Event : Batch.m_vCandidates)
		{
			if(Event.m_Type == EImpactEventType::SAVE_CONFIRMED && Event.m_Target == Debug.m_Target &&
				Event.m_Actor == Debug.m_PotentialSaver && Event.m_StartTick == Debug.m_StartTick)
			{
				pSaveEvent = &Event;
				break;
			}
		}
		if(pSaveEvent == nullptr)
			return;
		SImpactEvent AledEvent = *pSaveEvent;
		AledEvent.m_EventId = 0;
		AledEvent.m_Type = EImpactEventType::ALED_CONFIRMED;
		AledEvent.m_RawDelta = {};
		AledEvent.m_RawDelta.m_Aleds = 1;
		AledEvent.m_vTags.clear();
		AledEvent.m_vTags.push_back("ALED");
		AledEvent.m_CausalReason = "ALED side-car: confirmed Save with hammer context through freeze geometry";
		AledEvent.m_AppliedExactlyOnce = false;
		Batch.m_vCandidates.push_back(std::move(AledEvent));
		Record.m_AledEventQueued = true;
	}

	void CImpactSystem::CommitAledConfirmedEvents(const STickEventBatch &Batch, bool Applied)
	{
		for(SAledResearchGeometryProbeRecord &Record : m_aAledGeometryProbe)
		{
			if(!Record.m_AledEventQueued)
				continue;
			bool Accepted = false;
			if(Applied)
			{
				for(const SImpactEvent &Event : Batch.m_vCandidates)
				{
					if(Event.m_Type == EImpactEventType::ALED_CONFIRMED && Event.m_Actor.m_ClientId == Record.m_ActorId &&
						Event.m_Actor.m_SlotGeneration == Record.m_ActorGeneration && Event.m_Target.m_ClientId == Record.m_TargetId &&
						Event.m_Target.m_SlotGeneration == Record.m_TargetGeneration)
					{
						Accepted = true;
						Record.m_ConfirmTick = Event.m_ResolveTick;
						break;
					}
				}
			}
			Record.m_AledEventQueued = false;
			if(!Accepted)
				continue;
			Record.m_AledConfirmed = true;
			if(g_Config.m_Debug && !Record.m_ConfirmedLogged)
			{
				const int AledCountAfter = m_HasCurrentRun ? m_CurrentRun.RawStats().m_Aleds : 0;
				LogAledConfirmed(Record, AledCountAfter);
				Record.m_ConfirmedLogged = true;
			}
		}
	}

	void CImpactSystem::LogAledCandidate(const SAledResearchGeometryProbeRecord &Record)
	{
		ImpactDebugLog("ALED_CANDIDATE problem_episode_id=%llu recovery_episode_id=%llu actor=%d actor_generation=%u target=%d target_generation=%u hammer_tick=%lld restore_tick=%lld geometry_sample_tick=%lld freeze_between=%d opposite_sides=%d barrier_axis=%s actor_side=%s target_side=%s status=VALID reason='existing_save_owner_geometry_and_hammer_context' v2_topology=%s v2_orientation=%s v2_status=%s v2_is_aled=%d production_source=V2",
			static_cast<unsigned long long>(Record.m_ProblemEpisodeId), static_cast<unsigned long long>(Record.m_RecoveryEpisodeId),
			Record.m_ActorId, Record.m_ActorGeneration, Record.m_TargetId, Record.m_TargetGeneration,
			static_cast<long long>(Record.m_HammerAttackTick), static_cast<long long>(Record.m_RestoreTick), static_cast<long long>(Record.m_TargetSampleTick),
			Record.m_FreezeBetween, Record.m_OppositeSides, AledGeometryProbeAxisName(Record.m_BarrierAxis),
			AledGeometryProbeSideName(Record.m_ActorSide), AledGeometryProbeSideName(Record.m_TargetSide),
			AledGeometryV2TopologyName(Record.m_V2Topology), AledGeometryV2TopologyName(Record.m_V2Orientation),
			AledGeometryV2StatusName(Record.m_V2Status), Record.m_V2IsAledGeometryValid ? 1 : 0);
	}

	void CImpactSystem::LogAledConfirmed(const SAledResearchGeometryProbeRecord &Record, int AledCountAfter)
	{
		ImpactDebugLog("ALED_CONFIRMED recovery_episode_id=%llu actor=%d target=%d save_confirm_tick=%lld hammer_tick=%lld restore_tick=%lld barrier_axis=%s actor_side=%s target_side=%s freeze_between=%d opposite_sides=%d aled_count_after=%d reason='canonical_save_confirmed_side_car' v2_topology=%s v2_orientation=%s v2_status=%s production_source=V2",
			static_cast<unsigned long long>(Record.m_RecoveryEpisodeId), Record.m_ActorId, Record.m_TargetId,
			static_cast<long long>(Record.m_ConfirmTick), static_cast<long long>(Record.m_HammerAttackTick), static_cast<long long>(Record.m_RestoreTick),
			AledGeometryProbeAxisName(Record.m_BarrierAxis), AledGeometryProbeSideName(Record.m_ActorSide),
			AledGeometryProbeSideName(Record.m_TargetSide), Record.m_FreezeBetween, Record.m_OppositeSides, AledCountAfter,
			AledGeometryV2TopologyName(Record.m_V2Topology), AledGeometryV2TopologyName(Record.m_V2Orientation), AledGeometryV2StatusName(Record.m_V2Status));
	}

	CImpactSystem::SAledResearchGeometryProbeRecord CImpactSystem::BuildAledResearchGeometryProbe(const SImpactPhase2Debug &Debug)
	{
		constexpr int MAX_ALED_GEOMETRY_TILES_TO_SCAN = 128;
		SAledResearchGeometryProbeRecord Record;
		Record.m_ProblemEpisodeId = Debug.m_EpisodeId;
		Record.m_RecoveryEpisodeId = Debug.m_RelatedRecoveryEpisodeId;
		Record.m_ActorGeneration = Debug.m_PotentialSaver.m_SlotGeneration;
		Record.m_TargetGeneration = Debug.m_Target.m_SlotGeneration;
		Record.m_ActorId = Debug.m_PotentialSaver.m_ClientId;
		Record.m_TargetId = Debug.m_Target.m_ClientId;
		Record.m_SnapshotSequence = 0;
		Record.m_ServerTick = Debug.m_ServerTick;
		Record.m_RestoreTick = Debug.m_RestorationTick;
		Record.m_Status = "SKIPPED";
		Record.m_Reason = "UNKNOWN";

		if(!Debug.m_PotentialSaver.IsValid() || !IsAledResearchClientIdValid(Record.m_ActorId))
		{
			Record.m_Reason = "NO_ACTOR";
			return Record;
		}
		if(!IsAledResearchClientIdValid(Record.m_TargetId))
		{
			Record.m_Reason = "NO_TARGET";
			return Record;
		}
		CGameClient *pGameClient = GameClient();
		if(pGameClient == nullptr)
		{
			Record.m_Reason = "COLLISION_UNINITIALIZED";
			return Record;
		}
		const int64_t GeometrySampleTick = Client()->PrevGameTick(g_Config.m_ClDummy);
		if(GeometrySampleTick < 0)
		{
			Record.m_Reason = "INSUFFICIENT_AUTHORITATIVE_HISTORY";
			Record.m_SampleSelectionReason = "NO_PREVIOUS_SNAPSHOT_TICK";
			return Record;
		}
		const SRawAuthoritativeCharacterObservation &TargetRaw = pGameClient->m_RawAuthoritativeSnapshotAdapter.Previous()[Record.m_TargetId];
		if(!IsAledResearchRawPositionUsable(TargetRaw, true))
		{
			Record.m_Reason = "TARGET_RAW_UNAVAILABLE";
			return Record;
		}
		if(TargetRaw.m_SnapshotServerTick != GeometrySampleTick)
		{
			Record.m_Reason = "INSUFFICIENT_AUTHORITATIVE_HISTORY";
			Record.m_SampleSelectionReason = "TARGET_RAW_SNAPSHOT_TICK_MISMATCH";
			return Record;
		}
		if(!IsAledResearchRawFrozenAtSample(TargetRaw))
		{
			Record.m_Reason = "TARGET_PRE_UNFREEZE_POSITION_UNAVAILABLE";
			return Record;
		}

		const auto &TargetCharacter = pGameClient->m_Snap.m_aCharacters[Record.m_TargetId];
		const auto &TargetRawPrevious = RawPreviousObservation(pGameClient, Record.m_TargetId);
		SAledResearchPhysicalSample TargetNativeSample;
		TargetNativeSample.m_Valid = TargetCharacter.m_Active && TargetRawPrevious.m_FromRawPreviousSnapshot &&
			TargetCharacter.m_Prev.m_Tick == GeometrySampleTick;
		TargetNativeSample.m_Frozen = true;
		TargetNativeSample.m_NativeSnapshotReckoned = true;
		TargetNativeSample.m_SnapshotTick = GeometrySampleTick;
		TargetNativeSample.m_PhysicalTick = TargetCharacter.m_Prev.m_Tick;
		TargetNativeSample.m_ObjectTick = TargetRaw.m_ObjectTick;
		TargetNativeSample.m_X = TargetCharacter.m_Prev.m_X;
		TargetNativeSample.m_Y = TargetCharacter.m_Prev.m_Y;

		SAledResearchPhysicalSample TargetExactRawSample;
		TargetExactRawSample.m_Valid = IsAledResearchRawPositionUsable(TargetRaw, true);
		TargetExactRawSample.m_Frozen = true;
		TargetExactRawSample.m_SnapshotTick = TargetRaw.m_SnapshotServerTick;
		TargetExactRawSample.m_PhysicalTick = TargetRaw.m_ObjectTick;
		TargetExactRawSample.m_ObjectTick = TargetRaw.m_ObjectTick;
		TargetExactRawSample.m_X = TargetRaw.m_Character.m_X;
		TargetExactRawSample.m_Y = TargetRaw.m_Character.m_Y;

		SAledResearchPhysicalSample TargetSample;
		if(!SelectAledResearchPhysicalSample(TargetNativeSample, TargetExactRawSample, GeometrySampleTick, true, TargetSample))
		{
			Record.m_Reason = "INSUFFICIENT_AUTHORITATIVE_HISTORY";
			Record.m_SampleSelectionReason = "TARGET_FRESH_NATIVE_SAMPLE_UNAVAILABLE";
			return Record;
		}
		Record.m_TargetPositionSource = AledResearchPhysicalSampleSourceName(TargetSample);
		Record.m_SnapshotSequence = TargetRaw.m_SnapshotSequence;
		Record.m_TargetSampleTick = TargetSample.m_SnapshotTick;
		Record.m_TargetPhysicalTick = TargetSample.m_PhysicalTick;
		Record.m_TargetObjectTick = TargetSample.m_ObjectTick;
		Record.m_TargetRestoreTickGap = Debug.m_RestorationTick >= 0 ? Debug.m_RestorationTick - Record.m_TargetSampleTick : -1;
		Record.m_TargetX = TargetSample.m_X;
		Record.m_TargetY = TargetSample.m_Y;

		const auto &ActorCharacter = pGameClient->m_Snap.m_aCharacters[Record.m_ActorId];
		const SRawAuthoritativeCharacterObservation &ActorRawPrevious = pGameClient->m_RawAuthoritativeSnapshotAdapter.Previous()[Record.m_ActorId];
		SAledResearchPhysicalSample ActorNativeSample;
		ActorNativeSample.m_Valid = IsAledResearchRawPositionUsable(ActorRawPrevious, true) && ActorRawPrevious.m_SnapshotServerTick == GeometrySampleTick &&
			ActorCharacter.m_Active && ActorRawPrevious.m_FromRawPreviousSnapshot &&
			ActorCharacter.m_Prev.m_Tick == GeometrySampleTick;
		ActorNativeSample.m_NativeSnapshotReckoned = true;
		ActorNativeSample.m_SnapshotTick = GeometrySampleTick;
		ActorNativeSample.m_PhysicalTick = ActorCharacter.m_Prev.m_Tick;
		ActorNativeSample.m_ObjectTick = ActorRawPrevious.m_ObjectTick;
		ActorNativeSample.m_X = ActorCharacter.m_Prev.m_X;
		ActorNativeSample.m_Y = ActorCharacter.m_Prev.m_Y;

		SAledResearchPhysicalSample ActorExactRawSample;
		ActorExactRawSample.m_Valid = IsAledResearchRawPositionUsable(ActorRawPrevious, true);
		ActorExactRawSample.m_SnapshotTick = ActorRawPrevious.m_SnapshotServerTick;
		ActorExactRawSample.m_PhysicalTick = ActorRawPrevious.m_ObjectTick;
		ActorExactRawSample.m_ObjectTick = ActorRawPrevious.m_ObjectTick;
		ActorExactRawSample.m_X = ActorRawPrevious.m_Character.m_X;
		ActorExactRawSample.m_Y = ActorRawPrevious.m_Character.m_Y;

		SAledResearchPhysicalSample ActorSample;
		if(!SelectAledResearchPhysicalSample(ActorNativeSample, ActorExactRawSample, GeometrySampleTick, false, ActorSample))
		{
			Record.m_Reason = "INSUFFICIENT_AUTHORITATIVE_HISTORY";
			Record.m_SampleSelectionReason = "ACTOR_FRESH_NATIVE_SAMPLE_UNAVAILABLE";
			return Record;
		}
		if(ActorSample.m_SnapshotTick != TargetSample.m_SnapshotTick || ActorSample.m_PhysicalTick != TargetSample.m_PhysicalTick)
		{
			Record.m_Status = "INSUFFICIENT_EVIDENCE";
			Record.m_Reason = "MISALIGNED_SAMPLE";
			Record.m_SampleSelectionReason = "ACTOR_TARGET_PHYSICAL_TICKS_DIFFER";
			return Record;
		}
		Record.m_ActorPositionSource = AledResearchPhysicalSampleSourceName(ActorSample);
		Record.m_ActorSampleTick = ActorSample.m_SnapshotTick;
		Record.m_ActorPhysicalTick = ActorSample.m_PhysicalTick;
		Record.m_ActorObjectTick = ActorSample.m_ObjectTick;
		Record.m_ActorTargetTickGap = std::llabs(ActorSample.m_PhysicalTick - TargetSample.m_PhysicalTick);
		Record.m_ActorTargetObjectTickGap = std::llabs(ActorSample.m_ObjectTick - TargetSample.m_ObjectTick);
		Record.m_ActorX = ActorSample.m_X;
		Record.m_ActorY = ActorSample.m_Y;
		Record.m_SampleSelectionReason = ActorSample.m_NativeSnapshotReckoned && TargetSample.m_NativeSnapshotReckoned ?
			"NATIVE_SNAPSHOT_PREVIOUS_PAIR" : "NATIVE_SNAPSHOT_WITH_EXACT_RAW_FALLBACK";

		if(Record.m_ActorTargetTickGap != 0 || (Debug.m_RestorationTick >= 0 && Record.m_TargetRestoreTickGap < 0))
		{
			Record.m_Status = "INSUFFICIENT_EVIDENCE";
			Record.m_Reason = "MISALIGNED_SAMPLE";
			return Record;
		}

		const vec2 ActorPosition(static_cast<float>(Record.m_ActorX), static_cast<float>(Record.m_ActorY));
		const vec2 TargetPosition(static_cast<float>(Record.m_TargetX), static_cast<float>(Record.m_TargetY));
		if(!IsAledResearchPositionValid(ActorPosition) || !IsAledResearchPositionValid(TargetPosition))
		{
			Record.m_Reason = "NONFINITE_POSITION";
			return Record;
		}
		const CCollision *pCollision = pGameClient->Collision();
		if(pCollision == nullptr)
		{
			Record.m_Reason = "COLLISION_UNINITIALIZED";
			return Record;
		}
		const int Width = pCollision->GetWidth();
		const int Height = pCollision->GetHeight();
		const int64_t TileCount = static_cast<int64_t>(Width) * Height;
		if(Width <= 0 || Height <= 0 || TileCount <= 0 || TileCount > std::numeric_limits<int>::max())
		{
			Record.m_Reason = "INVALID_MAP_DIMENSIONS";
			return Record;
		}
		const CTile *pGameLayer = pCollision->GameLayer();
		if(pGameLayer == nullptr)
		{
			Record.m_Reason = "MAP_UNAVAILABLE";
			return Record;
		}
		const auto WorldToTile = [](int Coordinate, int &Tile) {
			const double Value = std::floor(static_cast<double>(Coordinate) / 32.0);
			if(!std::isfinite(Value) || Value < std::numeric_limits<int>::min() || Value > std::numeric_limits<int>::max())
				return false;
			Tile = static_cast<int>(Value);
			return true;
		};
		if(!WorldToTile(Record.m_ActorX, Record.m_ActorTileX) || !WorldToTile(Record.m_ActorY, Record.m_ActorTileY))
		{
			Record.m_Reason = "UNKNOWN";
			return Record;
		}
		if(!WorldToTile(Record.m_TargetX, Record.m_TargetTileX) || !WorldToTile(Record.m_TargetY, Record.m_TargetTileY))
		{
			Record.m_Reason = "UNKNOWN";
			return Record;
		}
		Record.m_ScanStartX = Record.m_ActorTileX;
		Record.m_ScanStartY = Record.m_ActorTileY;
		Record.m_ScanEndX = Record.m_TargetTileX;
		Record.m_ScanEndY = Record.m_TargetTileY;
		const auto InBounds = [Width, Height](int X, int Y) { return X >= 0 && Y >= 0 && X < Width && Y < Height; };
		if(!InBounds(Record.m_ActorTileX, Record.m_ActorTileY))
		{
			Record.m_Reason = "ACTOR_TILE_OUT_OF_BOUNDS";
			return Record;
		}
		if(!InBounds(Record.m_TargetTileX, Record.m_TargetTileY))
		{
			Record.m_Reason = "TARGET_TILE_OUT_OF_BOUNDS";
			return Record;
		}
		const int64_t DeltaX = static_cast<int64_t>(Record.m_TargetTileX) - Record.m_ActorTileX;
		const int64_t DeltaY = static_cast<int64_t>(Record.m_TargetTileY) - Record.m_ActorTileY;
		const int64_t AbsDeltaX = DeltaX < 0 ? -DeltaX : DeltaX;
		const int64_t AbsDeltaY = DeltaY < 0 ? -DeltaY : DeltaY;
		if(std::max(AbsDeltaX, AbsDeltaY) + 1 > MAX_ALED_GEOMETRY_TILES_TO_SCAN)
		{
			Record.m_Reason = "SEGMENT_TOO_LARGE";
			return Record;
		}

		const CTile *pFrontLayer = pCollision->FrontLayer();
		const CSwitchTile *pSwitchLayer = pCollision->SwitchLayer();
		const int TargetTeam = TargetRaw.m_TeamId;
		std::vector<std::pair<int, int>> vFreezeTiles;
		bool SwitchStateUnknown = false;
		int64_t X = Record.m_ActorTileX;
		int64_t Y = Record.m_ActorTileY;
		const int64_t StepX = DeltaX < 0 ? -1 : 1;
		const int64_t StepY = DeltaY < 0 ? -1 : 1;
		int64_t Error = AbsDeltaX - AbsDeltaY;
		for(int Step = 0; Step < MAX_ALED_GEOMETRY_TILES_TO_SCAN; ++Step)
		{
			if(!InBounds(static_cast<int>(X), static_cast<int>(Y)))
			{
				Record.m_Reason = "INVALID_TILE_INDEX";
				return Record;
			}
			const int64_t MapIndex64 = Y * Width + X;
			if(MapIndex64 < 0 || MapIndex64 >= TileCount)
			{
				Record.m_Reason = "INVALID_TILE_INDEX";
				return Record;
			}
			const int MapIndex = static_cast<int>(MapIndex64);
			++Record.m_TilesScanned;
			const int GameTile = pGameLayer[MapIndex].m_Index;
			const int FrontTile = pFrontLayer != nullptr ? pFrontLayer[MapIndex].m_Index : -1;
			if(IsAledResearchFreezeTile(GameTile))
				++Record.m_GameFreezeCount;
			if(IsAledResearchFreezeTile(FrontTile))
				++Record.m_FrontFreezeCount;
			bool ActiveSwitchFreeze = false;
			if(pSwitchLayer != nullptr && IsAledResearchFreezeTile(pSwitchLayer[MapIndex].m_Type))
			{
				const int SwitchNumber = pSwitchLayer[MapIndex].m_Number;
				if(SwitchNumber == 0 || TargetTeam == TEAM_SUPER)
					ActiveSwitchFreeze = true;
				else if(IsAledResearchSwitchAccessValid(SwitchNumber, TargetTeam, static_cast<int>(pGameClient->Switchers().size())))
					ActiveSwitchFreeze = pGameClient->Switchers()[SwitchNumber].m_aStatus[TargetTeam];
				else
					SwitchStateUnknown = true;
				if(ActiveSwitchFreeze)
					++Record.m_SwitchFreezeCount;
			}
			if(IsAledResearchFreezeTile(GameTile) || IsAledResearchFreezeTile(FrontTile) || ActiveSwitchFreeze)
				vFreezeTiles.emplace_back(static_cast<int>(X), static_cast<int>(Y));
			if(X == Record.m_TargetTileX && Y == Record.m_TargetTileY)
				break;
			const int64_t DoubleError = 2 * Error;
			if(DoubleError > -AbsDeltaY)
			{
				Error -= AbsDeltaY;
				X += StepX;
			}
			if(DoubleError < AbsDeltaX)
			{
				Error += AbsDeltaX;
				Y += StepY;
			}
			if(Step == MAX_ALED_GEOMETRY_TILES_TO_SCAN - 1)
			{
				Record.m_Reason = "SEGMENT_TOO_LARGE";
				return Record;
			}
		}
		if(X != Record.m_TargetTileX || Y != Record.m_TargetTileY)
		{
			Record.m_Reason = "SEGMENT_TOO_LARGE";
			return Record;
		}
		SAledResearchGeometryGridResult GridResult;
		if(!AnalyzeAledResearchGeometryGrid(Width, Height, Record.m_ActorTileX, Record.m_ActorTileY, Record.m_TargetTileX, Record.m_TargetTileY,
			vFreezeTiles, MAX_ALED_GEOMETRY_TILES_TO_SCAN, GridResult))
		{
			Record.m_Reason = "INVALID_TILE_INDEX";
			return Record;
		}
		Record.m_TilesScanned = GridResult.m_TilesScanned;
		Record.m_FreezeTileCount = GridResult.m_FreezeTileCount;
		Record.m_NearestFreezeX = GridResult.m_NearestFreezeX;
		Record.m_NearestFreezeY = GridResult.m_NearestFreezeY;
		Record.m_BarrierAxis = GridResult.m_BarrierAxis;
		Record.m_ActorSide = GridResult.m_ActorSide;
		Record.m_TargetSide = GridResult.m_TargetSide;
		Record.m_FreezeBetween = GridResult.m_FreezeBetween;
		Record.m_OppositeSides = GridResult.m_OppositeSides;
		if(SwitchStateUnknown)
		{
			Record.m_SwitchFreezeCount = -1;
			Record.m_Status = "INSUFFICIENT_EVIDENCE";
			Record.m_Reason = "UNSUPPORTED_FREEZE_LAYER";
		}
		else if(Record.m_FreezeTileCount > 0 && (Record.m_BarrierAxis == 3 || Record.m_OppositeSides < 0))
		{
			Record.m_Status = "INSUFFICIENT_EVIDENCE";
			Record.m_Reason = "AMBIGUOUS_BARRIER_GEOMETRY";
		}
		else
		{
			Record.m_Status = "VALID";
			Record.m_Reason = "NONE";
		}
		return Record;
	}

	void CImpactSystem::LogAledGeometryProbe(const SAledResearchGeometryProbeRecord &Record)
	{
		ImpactDebugLog("ALED_GEOMETRY_PROBE problem_episode_id=%llu recovery_episode_id=%llu actor_id=%d target_id=%d snapshot_sequence=%llu server_tick=%lld restore_tick=%lld geometry_status=%s geometry_reason=%s actor_position_source=%s actor_sample_tick=%lld actor_object_tick=%lld actor_pos=%d,%d target_position_source=%s target_sample_tick=%lld target_object_tick=%lld target_frozen_pos=%d,%d actor_target_tick_gap=%lld target_restore_tick_gap=%lld",
			static_cast<unsigned long long>(Record.m_ProblemEpisodeId), static_cast<unsigned long long>(Record.m_RecoveryEpisodeId), Record.m_ActorId, Record.m_TargetId,
			static_cast<unsigned long long>(Record.m_SnapshotSequence), static_cast<long long>(Record.m_ServerTick), static_cast<long long>(Record.m_RestoreTick),
			Record.m_Status.c_str(), Record.m_Reason.c_str(), Record.m_ActorPositionSource.c_str(),
			static_cast<long long>(Record.m_ActorSampleTick), static_cast<long long>(Record.m_ActorObjectTick), Record.m_ActorX, Record.m_ActorY,
			Record.m_TargetPositionSource.c_str(), static_cast<long long>(Record.m_TargetSampleTick), static_cast<long long>(Record.m_TargetObjectTick),
			Record.m_TargetX, Record.m_TargetY, static_cast<long long>(Record.m_ActorTargetTickGap), static_cast<long long>(Record.m_TargetRestoreTickGap));
		ImpactDebugLog("ALED_GEOMETRY_SAMPLE_ALIGNMENT problem_episode_id=%llu recovery_episode_id=%llu actor_id=%d target_id=%d selection=%s actor_sample_tick=%lld actor_physical_tick=%lld actor_object_tick=%lld target_sample_tick=%lld target_physical_tick=%lld target_object_tick=%lld physical_tick_gap=%lld object_tick_gap=%lld",
			static_cast<unsigned long long>(Record.m_ProblemEpisodeId), static_cast<unsigned long long>(Record.m_RecoveryEpisodeId), Record.m_ActorId, Record.m_TargetId,
			Record.m_SampleSelectionReason.c_str(), static_cast<long long>(Record.m_ActorSampleTick), static_cast<long long>(Record.m_ActorPhysicalTick),
			static_cast<long long>(Record.m_ActorObjectTick), static_cast<long long>(Record.m_TargetSampleTick), static_cast<long long>(Record.m_TargetPhysicalTick),
			static_cast<long long>(Record.m_TargetObjectTick), static_cast<long long>(Record.m_ActorTargetTickGap), static_cast<long long>(Record.m_ActorTargetObjectTickGap));
		ImpactDebugLog("ALED_GEOMETRY_PROBE_TILES problem_episode_id=%llu recovery_episode_id=%llu actor_tile=%d,%d target_tile=%d,%d scan_start_tile=%d,%d scan_end_tile=%d,%d tiles_scanned=%d freeze_between=%d freeze_tile_count=%d game_freeze_count=%d front_freeze_count=%d switch_freeze_count=%d nearest_freeze_tile=%d,%d",
			static_cast<unsigned long long>(Record.m_ProblemEpisodeId), static_cast<unsigned long long>(Record.m_RecoveryEpisodeId), Record.m_ActorTileX, Record.m_ActorTileY,
			Record.m_TargetTileX, Record.m_TargetTileY, Record.m_ScanStartX, Record.m_ScanStartY, Record.m_ScanEndX, Record.m_ScanEndY, Record.m_TilesScanned,
			Record.m_FreezeBetween, Record.m_FreezeTileCount, Record.m_GameFreezeCount, Record.m_FrontFreezeCount, Record.m_SwitchFreezeCount,
			Record.m_NearestFreezeX, Record.m_NearestFreezeY);
		ImpactDebugLog("ALED_GEOMETRY_PROBE_BARRIER problem_episode_id=%llu recovery_episode_id=%llu barrier_axis=%s actor_side=%s target_side=%s opposite_sides=%d",
			static_cast<unsigned long long>(Record.m_ProblemEpisodeId), static_cast<unsigned long long>(Record.m_RecoveryEpisodeId),
			AledGeometryProbeAxisName(Record.m_BarrierAxis), AledGeometryProbeSideName(Record.m_ActorSide), AledGeometryProbeSideName(Record.m_TargetSide), Record.m_OppositeSides);
	}

	void CImpactSystem::LogAledGeometryResult(const SAledResearchGeometryProbeRecord &Record, const SImpactPhase2Debug &Debug)
	{
		ImpactDebugLog("ALED_GEOMETRY_RESULT problem_episode_id=%llu recovery_episode_id=%llu actor_id=%d target_id=%d save_confirmed=1 confirmed_ticks=%lld required_ticks=%lld geometry_status=%s geometry_reason=%s freeze_between=%d opposite_sides=%d barrier_axis=%s v2_is_aled=%d v2_orientation=%s v2_topology=%s v2_status=%s production_source=V2",
			static_cast<unsigned long long>(Record.m_ProblemEpisodeId), static_cast<unsigned long long>(Record.m_RecoveryEpisodeId), Record.m_ActorId, Record.m_TargetId,
			static_cast<long long>(Debug.m_ConfirmedTicks), Debug.m_RequiredTicks >= 0 ? static_cast<long long>(Debug.m_RequiredTicks) : 35LL,
			Record.m_Status.c_str(), Record.m_Reason.c_str(), Record.m_FreezeBetween, Record.m_OppositeSides, AledGeometryProbeAxisName(Record.m_BarrierAxis),
			Record.m_V2IsAledGeometryValid ? 1 : 0, AledGeometryV2TopologyName(Record.m_V2Orientation),
			AledGeometryV2TopologyName(Record.m_V2Topology), AledGeometryV2StatusName(Record.m_V2Status));
	}

	void CImpactSystem::LogAledGeometryV2(SAledResearchGeometryProbeRecord &Record)
	{
		if(Record.m_V2Computed)
			return;
		constexpr int MAX_ALED_GEOMETRY_V2_TILES = 128;
		constexpr int MAX_ALED_GEOMETRY_V2_LOCAL_READS = 64;
		constexpr int ALED_GEOMETRY_V2_LOCAL_RADIUS = 2;
		SAledResearchGeometryV2GridResult Result;
		std::string Reason = "UNKNOWN";

		const auto LogSkipped = [this, &Record, &Reason]() {
			Record.m_V2Computed = true;
			Record.m_V2IsAledGeometryValid = false;
			Record.m_V2Topology = EAledGeometryV2Topology::UNKNOWN;
			Record.m_V2Orientation = EAledGeometryV2Topology::UNKNOWN;
			Record.m_V2Status = EAledGeometryV2Status::SKIPPED;
			Record.m_V2Result = {};
			Record.m_V2Reason = Reason;
			ImpactDebugLog("ALED_GEOMETRY_V2 problem_episode_id=%llu recovery_episode_id=%llu actor=%d target=%d sample_tick=%lld actor_tile=%d,%d target_tile=%d,%d legacy_axis=%s v2_topology=UNKNOWN v2_status=SKIPPED reason=%s tile_reads=0 segment_tiles=0 freeze_between=-1 opposite_sides=-1 actor_on_freeze=0 target_on_freeze=0 corner_freeze_pair=0",
				static_cast<unsigned long long>(Record.m_ProblemEpisodeId), static_cast<unsigned long long>(Record.m_RecoveryEpisodeId),
				Record.m_ActorId, Record.m_TargetId, static_cast<long long>(Record.m_TargetSampleTick), Record.m_ActorTileX, Record.m_ActorTileY,
				Record.m_TargetTileX, Record.m_TargetTileY, AledGeometryProbeAxisName(Record.m_BarrierAxis), Reason.c_str());
			ImpactDebugLog("ALED_GEOMETRY_V2_RESULT problem_episode_id=%llu recovery_episode_id=%llu legacy=%s v2=UNKNOWN confidence=UNKNOWN reason=%s",
				static_cast<unsigned long long>(Record.m_ProblemEpisodeId), static_cast<unsigned long long>(Record.m_RecoveryEpisodeId),
				AledGeometryProbeAxisName(Record.m_BarrierAxis), Reason.c_str());
		};

		if(!IsAledResearchClientIdValid(Record.m_ActorId))
		{
			Reason = "NO_ACTOR";
			LogSkipped();
			return;
		}
		if(!IsAledResearchClientIdValid(Record.m_TargetId))
		{
			Reason = "NO_TARGET";
			LogSkipped();
			return;
		}
		if(Record.m_Reason == "INSUFFICIENT_AUTHORITATIVE_HISTORY" || Record.m_Reason == "MISALIGNED_SAMPLE")
		{
			Reason = Record.m_Reason;
			LogSkipped();
			return;
		}
		if(!IsAledResearchGeometryPositionSourceUsable(Record.m_ActorPositionSource))
		{
			Reason = "ACTOR_RAW_UNAVAILABLE";
			LogSkipped();
			return;
		}
		if(!IsAledResearchGeometryPositionSourceUsable(Record.m_TargetPositionSource))
		{
			Reason = "TARGET_PRE_UNFREEZE_POSITION_UNAVAILABLE";
			LogSkipped();
			return;
		}
		if(Record.m_ActorTargetTickGap != 0 || Record.m_TargetRestoreTickGap < 0)
		{
			Reason = "TEMPORAL_ALIGNMENT_TOO_WEAK";
			LogSkipped();
			return;
		}
		if(!IsAledResearchPositionFinite(static_cast<float>(Record.m_ActorX), static_cast<float>(Record.m_ActorY)) ||
			!IsAledResearchPositionFinite(static_cast<float>(Record.m_TargetX), static_cast<float>(Record.m_TargetY)))
		{
			Reason = "NONFINITE_POSITION";
			LogSkipped();
			return;
		}

		CGameClient *pGameClient = GameClient();
		if(pGameClient == nullptr || pGameClient->Collision() == nullptr)
		{
			Reason = "COLLISION_UNINITIALIZED";
			LogSkipped();
			return;
		}
		const CCollision *pCollision = pGameClient->Collision();
		const int Width = pCollision->GetWidth();
		const int Height = pCollision->GetHeight();
		const int64_t TileCount = static_cast<int64_t>(Width) * Height;
		if(Width <= 0 || Height <= 0 || TileCount <= 0 || TileCount > std::numeric_limits<int>::max())
		{
			Reason = "INVALID_MAP_DIMENSIONS";
			LogSkipped();
			return;
		}
		const CTile *pGameLayer = pCollision->GameLayer();
		if(pGameLayer == nullptr)
		{
			Reason = "MAP_UNAVAILABLE";
			LogSkipped();
			return;
		}
		const auto TileInBounds = [Width, Height](int X, int Y) {
			return X >= 0 && Y >= 0 && X < Width && Y < Height;
		};
		if(!TileInBounds(Record.m_ActorTileX, Record.m_ActorTileY))
		{
			Reason = "ACTOR_TILE_OUT_OF_BOUNDS";
			LogSkipped();
			return;
		}
		if(!TileInBounds(Record.m_TargetTileX, Record.m_TargetTileY))
		{
			Reason = "TARGET_TILE_OUT_OF_BOUNDS";
			LogSkipped();
			return;
		}

		const int64_t WindowMinX64 = std::max<int64_t>(0, static_cast<int64_t>(std::min(Record.m_ActorTileX, Record.m_TargetTileX)) - ALED_GEOMETRY_V2_LOCAL_RADIUS);
		const int64_t WindowMaxX64 = std::min<int64_t>(Width - 1, static_cast<int64_t>(std::max(Record.m_ActorTileX, Record.m_TargetTileX)) + ALED_GEOMETRY_V2_LOCAL_RADIUS);
		const int64_t WindowMinY64 = std::max<int64_t>(0, static_cast<int64_t>(std::min(Record.m_ActorTileY, Record.m_TargetTileY)) - ALED_GEOMETRY_V2_LOCAL_RADIUS);
		const int64_t WindowMaxY64 = std::min<int64_t>(Height - 1, static_cast<int64_t>(std::max(Record.m_ActorTileY, Record.m_TargetTileY)) + ALED_GEOMETRY_V2_LOCAL_RADIUS);
		const int64_t WindowWidth = WindowMaxX64 - WindowMinX64 + 1;
		const int64_t WindowHeight = WindowMaxY64 - WindowMinY64 + 1;
		if(WindowWidth <= 0 || WindowHeight <= 0 || WindowWidth > MAX_ALED_GEOMETRY_V2_LOCAL_READS ||
			WindowHeight > MAX_ALED_GEOMETRY_V2_LOCAL_READS || WindowWidth * WindowHeight > MAX_ALED_GEOMETRY_V2_LOCAL_READS)
		{
			Reason = "SEGMENT_TOO_LARGE";
			LogSkipped();
			return;
		}

		const CTile *pFrontLayer = pCollision->FrontLayer();
		const CSwitchTile *pSwitchLayer = pCollision->SwitchLayer();
		const SRawAuthoritativeCharacterObservation &TargetRaw = pGameClient->m_RawAuthoritativeSnapshotAdapter.Previous()[Record.m_TargetId];
		const int TargetTeam = TargetRaw.m_Valid ? TargetRaw.m_TeamId : TEAM_FLOCK;
		std::vector<std::pair<int, int>> vFreezeTiles;
		vFreezeTiles.reserve(static_cast<size_t>(WindowWidth * WindowHeight));
		bool SwitchStateUnknown = false;
		int GameFreezeCount = 0;
		int FrontFreezeCount = 0;
		int SwitchFreezeCount = 0;
		int TileReads = 0;
		for(int Y = static_cast<int>(WindowMinY64); Y <= static_cast<int>(WindowMaxY64); ++Y)
		{
			for(int X = static_cast<int>(WindowMinX64); X <= static_cast<int>(WindowMaxX64); ++X)
			{
				if(++TileReads > MAX_ALED_GEOMETRY_V2_LOCAL_READS)
				{
					Reason = "SEGMENT_TOO_LARGE";
					LogSkipped();
					return;
				}
				const int64_t MapIndex64 = static_cast<int64_t>(Y) * Width + X;
				if(MapIndex64 < 0 || MapIndex64 >= TileCount)
				{
					Reason = "INVALID_TILE_INDEX";
					LogSkipped();
					return;
				}
				const int MapIndex = static_cast<int>(MapIndex64);
				const int GameTile = pGameLayer[MapIndex].m_Index;
				const int FrontTile = pFrontLayer != nullptr ? pFrontLayer[MapIndex].m_Index : -1;
				const bool GameFreeze = IsAledResearchFreezeTile(GameTile);
				const bool FrontFreeze = IsAledResearchFreezeTile(FrontTile);
				if(GameFreeze)
					++GameFreezeCount;
				if(FrontFreeze)
					++FrontFreezeCount;
				bool ActiveSwitchFreeze = false;
				if(pSwitchLayer != nullptr && IsAledResearchFreezeTile(pSwitchLayer[MapIndex].m_Type))
				{
					const int SwitchNumber = pSwitchLayer[MapIndex].m_Number;
					if(SwitchNumber == 0 || TargetTeam == TEAM_SUPER)
						ActiveSwitchFreeze = true;
					else if(IsAledResearchSwitchAccessValid(SwitchNumber, TargetTeam, static_cast<int>(pGameClient->Switchers().size())))
						ActiveSwitchFreeze = pGameClient->Switchers()[SwitchNumber].m_aStatus[TargetTeam];
					else
						SwitchStateUnknown = true;
					if(ActiveSwitchFreeze)
						++SwitchFreezeCount;
				}
				if(GameFreeze || FrontFreeze || ActiveSwitchFreeze)
					vFreezeTiles.emplace_back(X, Y);
			}
		}
		if(SwitchStateUnknown)
		{
			Reason = "UNSUPPORTED_FREEZE_LAYER";
			LogSkipped();
			return;
		}
		if(!AnalyzeAledResearchGeometryV2Grid(Width, Height, Record.m_ActorTileX, Record.m_ActorTileY,
			Record.m_TargetTileX, Record.m_TargetTileY, vFreezeTiles, MAX_ALED_GEOMETRY_V2_TILES,
			MAX_ALED_GEOMETRY_V2_LOCAL_READS, Result))
		{
			Reason = "INVALID_TILE_INDEX";
			LogSkipped();
			return;
		}
		Result.m_TileReads = TileReads;

		if(Result.m_Status == EAledGeometryV2Status::VALID)
		{
			if(Result.m_Topology == EAledGeometryV2Topology::DIAGONAL)
				Reason = Result.m_CornerFreezePair != 0 ? "DOUBLE_FREEZE_CORNER" : "DIAGONAL_STAIRCASE_TOPOLOGY";
			else if(Result.m_Topology == EAledGeometryV2Topology::VERTICAL)
				Reason = "VERTICAL_NEIGHBORING_FREEZE";
			else if(Result.m_Topology == EAledGeometryV2Topology::HORIZONTAL)
				Reason = "HORIZONTAL_NEIGHBORING_FREEZE";
			else
				Reason = "NONE";
		}
		else if(Result.m_Status == EAledGeometryV2Status::AMBIGUOUS)
			Reason = "AMBIGUOUS_LOCAL_TOPOLOGY";
		else if(Result.m_FreezeTileCount == 0)
			Reason = "NO_FREEZE_BETWEEN";
		else if(Result.m_ActorOnFreeze != 0 || Result.m_TargetOnFreeze != 0)
			Reason = "ENDPOINT_ONLY_FREEZE";
		else
			Reason = "INSUFFICIENT_LOCAL_TOPOLOGY";
		Record.m_V2Computed = true;
		Record.m_V2Result = Result;
		Record.m_V2IsAledGeometryValid = IsAledGeometryV2ProductionValid(Result);
		Record.m_V2Topology = Result.m_Topology;
		Record.m_V2Orientation = AledGeometryV2ProductionOrientation(Result);
		Record.m_V2Status = Result.m_Status;
		Record.m_V2GameFreezeCount = GameFreezeCount;
		Record.m_V2FrontFreezeCount = FrontFreezeCount;
		Record.m_V2SwitchFreezeCount = SwitchFreezeCount;
		Record.m_V2Reason = Reason;

		ImpactDebugLog("ALED_GEOMETRY_V2 problem_episode_id=%llu recovery_episode_id=%llu actor=%d target=%d sample_tick=%lld actor_tile=%d,%d target_tile=%d,%d legacy_axis=%s v2_topology=%s v2_status=%s reason=%s tile_reads=%d segment_tiles=%d freeze_between=%d opposite_sides=%d actor_on_freeze=%d target_on_freeze=%d corner_freeze_pair=%d",
			static_cast<unsigned long long>(Record.m_ProblemEpisodeId), static_cast<unsigned long long>(Record.m_RecoveryEpisodeId),
			Record.m_ActorId, Record.m_TargetId, static_cast<long long>(Record.m_TargetSampleTick), Record.m_ActorTileX, Record.m_ActorTileY,
			Record.m_TargetTileX, Record.m_TargetTileY, AledGeometryProbeAxisName(Record.m_BarrierAxis), AledGeometryV2TopologyName(Result.m_Topology),
			AledGeometryV2StatusName(Result.m_Status), Reason.c_str(), Result.m_TileReads, Result.m_TilesScanned, Result.m_FreezeBetween,
			Result.m_OppositeSides, Result.m_ActorOnFreeze, Result.m_TargetOnFreeze, Result.m_CornerFreezePair);
		if(Result.m_CornerFreezePair != 0)
		{
			ImpactDebugLog("ALED_GEOMETRY_V2_CORNER from_tile=%d,%d to_tile=%d,%d side_a_tile=%d,%d side_b_tile=%d,%d side_a_freeze=1 side_b_freeze=1 corner_pair_valid=1 orientation=%s",
				Result.m_CornerFromX, Result.m_CornerFromY, Result.m_CornerToX, Result.m_CornerToY,
				Result.m_CornerSideAX, Result.m_CornerSideAY, Result.m_CornerSideBX, Result.m_CornerSideBY,
				AledGeometryV2DiagonalOrientationName(Result.m_DiagonalOrientation));
		}
		const char *pConfidence = Result.m_Status == EAledGeometryV2Status::VALID ? "VALID" :
			Result.m_Status == EAledGeometryV2Status::AMBIGUOUS ? "AMBIGUOUS" : "UNKNOWN";
		ImpactDebugLog("ALED_GEOMETRY_V2_RESULT problem_episode_id=%llu recovery_episode_id=%llu legacy=%s v2=%s confidence=%s reason=%s game_freeze_count=%d front_freeze_count=%d switch_freeze_count=%d",
			static_cast<unsigned long long>(Record.m_ProblemEpisodeId), static_cast<unsigned long long>(Record.m_RecoveryEpisodeId),
			AledGeometryProbeAxisName(Record.m_BarrierAxis), AledGeometryV2TopologyName(Result.m_Topology), pConfidence, Reason.c_str(),
			GameFreezeCount, FrontFreezeCount, SwitchFreezeCount);
	}

	void CImpactSystem::LogAledResearch(const SImpactPhase2Debug &Debug, const std::vector<SImpactGameplayFrame> &vFrames)
	{
		if(!g_Config.m_Debug || !Debug.m_Target.IsValid() || !IsAledResearchRelevantLabel(Debug.m_DebugLabel))
			return;
		const int TargetId = Debug.m_Target.m_ClientId;
		if(!CImpactSystem::IsAledResearchClientIdValid(TargetId))
			return;
		CGameClient *pGameClient = GameClient();
		if(pGameClient == nullptr)
			return;
		const SImpactGameplayFrame *pTargetFrame = nullptr;
		const SImpactGameplayFrame *pActorFrame = nullptr;
		for(const SImpactGameplayFrame &Frame : vFrames)
		{
			if(Frame.m_Participant == Debug.m_Target)
				pTargetFrame = &Frame;
			if(Debug.m_PotentialSaver.IsValid() && Frame.m_Participant == Debug.m_PotentialSaver)
				pActorFrame = &Frame;
		}
		const SRawAuthoritativeCharacterObservation &TargetCurrent = pGameClient->m_RawAuthoritativeSnapshotAdapter.Current()[TargetId];
		const SRawAuthoritativeCharacterObservation &TargetPrevious = pGameClient->m_RawAuthoritativeSnapshotAdapter.Previous()[TargetId];
		const int64_t TargetObjectTick = TargetCurrent.m_Valid ? TargetCurrent.m_ObjectTick : -1;
		const int64_t TargetPreviousTick = TargetPrevious.m_Valid ? TargetPrevious.m_ObjectTick : -1;
		const int64_t RawTickGap = TargetCurrent.m_Valid && TargetPrevious.m_Valid ? TargetObjectTick - TargetPreviousTick : -1;
		const bool TargetRawAuthoritative = TargetCurrent.m_Valid && TargetCurrent.m_FromRawCurrentSnapshot;
		const char *pPositionSource = TargetRawAuthoritative ? "RAW_AUTHORITATIVE" : "UNKNOWN";
		int64_t HookReleaseTick = -1;
		int RehookCount = -1;
		if(!Debug.m_vHookSpans.empty())
		{
			RehookCount = 0;
			for(const SImpactHookSpanDebug &Span : Debug.m_vHookSpans)
			{
				if(Span.m_EndTick >= 0)
					HookReleaseTick = std::max(HookReleaseTick, Span.m_EndTick);
				if(Span.m_Rehook)
					++RehookCount;
			}
		}
		const int TargetX = TargetCurrent.m_Valid ? TargetCurrent.m_Character.m_X : (pTargetFrame != nullptr ? pTargetFrame->m_X : 0);
		const int TargetY = TargetCurrent.m_Valid ? TargetCurrent.m_Character.m_Y : (pTargetFrame != nullptr ? pTargetFrame->m_Y : 0);
		const int TargetVelX = TargetCurrent.m_Valid ? TargetCurrent.m_Character.m_VelX : (pTargetFrame != nullptr ? pTargetFrame->m_VelX : 0);
		const int TargetVelY = TargetCurrent.m_Valid ? TargetCurrent.m_Character.m_VelY : (pTargetFrame != nullptr ? pTargetFrame->m_VelY : 0);
		const bool TargetFrozen = pTargetFrame != nullptr && pTargetFrame->m_Frozen;
		const bool TargetDeepFrozen = pTargetFrame != nullptr && pTargetFrame->m_DeepFrozen;
		const bool TargetLiveFrozen = pTargetFrame != nullptr && pTargetFrame->m_LiveFrozen;
		const bool TargetAlive = pTargetFrame != nullptr && pTargetFrame->m_AliveKnown && pTargetFrame->m_Alive;
		const bool TargetPresent = pTargetFrame != nullptr && pTargetFrame->m_Present;
		char StateKey[1536];
		str_format(StateKey, sizeof(StateKey), "%s|%llu|%llu|%d|%u|%llu|%lld|%lld|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%lld|%lld|%lld|%lld|%lld|%d|%d|%d|%d|%d|%d|%lld|%lld|%d|%s",
			Debug.m_DebugLabel.c_str(), static_cast<unsigned long long>(Debug.m_EpisodeId), static_cast<unsigned long long>(Debug.m_RelatedRecoveryEpisodeId),
			TargetId, Debug.m_Target.m_SlotGeneration, static_cast<unsigned long long>(TargetCurrent.m_SnapshotSequence), TargetObjectTick, RawTickGap,
			TargetRawAuthoritative ? 1 : 0, TargetX, TargetY, TargetVelX, TargetVelY, TargetFrozen ? 1 : 0, TargetDeepFrozen ? 1 : 0,
			TargetLiveFrozen ? 1 : 0, TargetAlive ? 1 : 0, TargetPresent ? 1 : 0, Debug.m_HookState, Debug.m_HookedPlayer,
			Debug.m_HookEvidence ? 1 : 0, Debug.m_HammerTargetEvidenceKnown ? 1 : 0, Debug.m_HammerEvidence ? 1 : 0,
			Debug.m_TargetSpecificEvidence ? 1 : 0, Debug.m_FreezeKnown ? 1 : 0, static_cast<long long>(Debug.m_ServerTick),
			static_cast<long long>(Debug.m_RestorationTick), static_cast<long long>(Debug.m_ConfirmationStartTick), static_cast<long long>(Debug.m_ConfirmedTicks),
			static_cast<long long>(Debug.m_RequiredTicks), Debug.m_UnknownInterruption ? 1 : 0, Debug.m_FallAlreadyConfirmed ? 1 : 0,
			Debug.m_ConfirmedFall ? 1 : 0, Debug.m_Present ? 1 : 0, Debug.m_AliveKnown ? 1 : 0, Debug.m_Alive ? 1 : 0,
			static_cast<long long>(Debug.m_FreezeStart), static_cast<long long>(Debug.m_FreezeEnd), Debug.m_HookControlContinuity ? 1 : 0, Debug.m_Reason.c_str());
		if(m_aLastAledStateLogKey[TargetId] != StateKey)
		{
			m_aLastAledStateLogKey[TargetId] = StateKey;
			ImpactDebugLog("ALED_RESEARCH_STATE problem_episode_id=%llu recovery_episode_id=%llu snapshot_sequence=%llu server_tick=%lld actor_id=%d target_id=%d target_raw_position=%d,%d target_raw_velocity=%d,%d target_object_tick=%lld target_frozen=%d target_deep_frozen=%d target_live_frozen=%d target_alive=%d target_present=%d raw_tick_gap=%lld authoritative_physical_state_stale=%d hook_state=%d hooked_player=%d hook_first_tick=%lld hook_last_tick=%lld hook_release_tick=%lld rehook_count=%d position_source=%s",
				static_cast<unsigned long long>(Debug.m_EpisodeId), static_cast<unsigned long long>(Debug.m_RelatedRecoveryEpisodeId), static_cast<unsigned long long>(TargetCurrent.m_SnapshotSequence),
				static_cast<long long>(Debug.m_ServerTick), Debug.m_PotentialSaver.m_ClientId, TargetId, TargetX, TargetY, TargetVelX, TargetVelY,
				static_cast<long long>(TargetObjectTick), TargetFrozen ? 1 : 0, TargetDeepFrozen ? 1 : 0, TargetLiveFrozen ? 1 : 0,
				TargetAlive ? 1 : 0, TargetPresent ? 1 : 0, static_cast<long long>(RawTickGap), pTargetFrame != nullptr && pTargetFrame->m_AuthoritativePhysicalStateStale ? 1 : 0,
				pActorFrame != nullptr ? pActorFrame->m_HookState : Debug.m_HookState, pActorFrame != nullptr ? pActorFrame->m_HookedPlayer : Debug.m_HookedPlayer,
				static_cast<long long>(Debug.m_HookFirstTick), static_cast<long long>(Debug.m_HookLastTick), static_cast<long long>(HookReleaseTick), RehookCount, pPositionSource);
		}

		if(Debug.m_DebugLabel == "SAVE_RESTORE")
		{
			SAledResearchGeometryProbeRecord &Probe = m_aAledGeometryProbe[TargetId];
			if(!Probe.m_Recorded || Probe.m_ProblemEpisodeId != Debug.m_EpisodeId || Probe.m_RecoveryEpisodeId != Debug.m_RelatedRecoveryEpisodeId ||
				Probe.m_TargetGeneration != Debug.m_Target.m_SlotGeneration)
			{
				Probe = BuildAledResearchGeometryProbe(Debug);
				Probe.m_Recorded = true;
			}
			if(!Probe.m_ProbeLogged)
			{
				LogAledGeometryProbe(Probe);
				Probe.m_ProbeLogged = true;
			}
			if(!Probe.m_V2ProbeLogged)
			{
				LogAledGeometryV2(Probe);
				Probe.m_V2ProbeLogged = true;
			}
		}

		bool HasHammerAction = false;
		int64_t AssociatedAttackTick = -1;
		for(const SImpactGameplayFrame &ActionFrame : vFrames)
		{
			const int ActorId = ActionFrame.m_Participant.m_ClientId;
			if(!ActionFrame.m_Participant.IsValid() || !CImpactSystem::IsAledResearchClientIdValid(ActorId) || ActorId == TargetId || !ActionFrame.m_AttackKnown)
				continue;
			const SRawAuthoritativeCharacterObservation &ActorCurrent = pGameClient->m_RawAuthoritativeSnapshotAdapter.Current()[ActorId];
			const SRawAuthoritativeCharacterObservation &ActorPrevious = pGameClient->m_RawAuthoritativeSnapshotAdapter.Previous()[ActorId];
			const bool AttackEdge = ActorCurrent.m_Valid && (!ActorPrevious.m_Valid || ActorCurrent.m_Character.m_AttackTick != ActorPrevious.m_Character.m_AttackTick);
			const bool HammerAction = AttackEdge && ActionFrame.m_Weapon == WEAPON_HAMMER;
			if(ActionFrame.m_Participant == Debug.m_PotentialSaver)
			{
				HasHammerAction = HammerAction;
				AssociatedAttackTick = AttackEdge ? ActionFrame.m_AttackTick : -1;
			}
			if(AttackEdge)
			{
				char ActionKey[1024];
				str_format(ActionKey, sizeof(ActionKey), "%llu|%llu|%d|%u|%llu|%lld|%lld|%d|%d|%d|%d",
					static_cast<unsigned long long>(Debug.m_EpisodeId), static_cast<unsigned long long>(Debug.m_RelatedRecoveryEpisodeId), ActorId,
					ActionFrame.m_Participant.m_SlotGeneration, static_cast<unsigned long long>(ActorCurrent.m_SnapshotSequence),
					static_cast<long long>(ActorCurrent.m_ObjectTick), static_cast<long long>(ActionFrame.m_AttackTick), ActionFrame.m_Weapon,
					Debug.m_Target.m_ClientId, Debug.m_Target.m_SlotGeneration, HammerAction ? 1 : 0);
				if(m_aLastAledActionLogKey[ActorId] != ActionKey)
				{
					m_aLastAledActionLogKey[ActorId] = ActionKey;
					ImpactDebugLog("ALED_RESEARCH_ACTION problem_episode_id=%llu recovery_episode_id=%llu actor_id=%d candidate_target_id=%d hammer_target_attribution=PROTOCOL_UNAVAILABLE attack_tick=%lld weapon=%d hammer_event_x=-1 hammer_event_y=-1 hammer_event_source=UNAVAILABLE snapshot_sequence=%llu server_tick=%lld actor_object_tick=%lld target_object_tick=%lld",
						static_cast<unsigned long long>(Debug.m_EpisodeId), static_cast<unsigned long long>(Debug.m_RelatedRecoveryEpisodeId), ActorId, TargetId,
						static_cast<long long>(ActionFrame.m_AttackTick), ActionFrame.m_Weapon, static_cast<unsigned long long>(ActorCurrent.m_SnapshotSequence),
						static_cast<long long>(Debug.m_ServerTick), static_cast<long long>(ActorCurrent.m_ObjectTick), static_cast<long long>(TargetObjectTick));
				}
			}
		}

		const bool HasSaveResult = Debug.m_Stage == EImpactPhase2DebugStage::CONFIRMED &&
			(Debug.m_EventType == EImpactEventType::SAVE_CONFIRMED || Debug.m_EventType == EImpactEventType::UPDOWN_CONFIRMED || Debug.m_EventType == EImpactEventType::ALED_CONFIRMED);
		if(Debug.m_RestorationTick >= 0 && (Debug.m_DebugLabel == "SAVE_RESTORE" || Debug.m_DebugLabel == "SAVE_PENDING" || HasSaveResult))
		{
			char RestoreKey[1536];
			str_format(RestoreKey, sizeof(RestoreKey), "%llu|%llu|%d|%u|%llu|%lld|%lld|%lld|%lld|%d|%d|%d|%d|%d",
				static_cast<unsigned long long>(Debug.m_EpisodeId), static_cast<unsigned long long>(Debug.m_RelatedRecoveryEpisodeId), TargetId, Debug.m_Target.m_SlotGeneration,
				static_cast<unsigned long long>(TargetCurrent.m_SnapshotSequence), static_cast<long long>(Debug.m_RestorationTick), static_cast<long long>(Debug.m_ConfirmationStartTick),
				static_cast<long long>(Debug.m_ConfirmedTicks), static_cast<long long>(Debug.m_RequiredTicks), Debug.m_FrozenNow ? 1 : 0, Debug.m_Alive ? 1 : 0,
				HasHammerAction ? 1 : 0, AssociatedAttackTick >= 0 ? 1 : 0, Debug.m_UnknownInterruption ? 1 : 0);
			if(m_aLastAledRestoreLogKey[TargetId] != RestoreKey)
			{
				m_aLastAledRestoreLogKey[TargetId] = RestoreKey;
				const int PreX = TargetPrevious.m_Valid ? TargetPrevious.m_Character.m_X : 0;
				const int PreY = TargetPrevious.m_Valid ? TargetPrevious.m_Character.m_Y : 0;
				ImpactDebugLog("ALED_RESEARCH_RESTORE problem_episode_id=%llu recovery_episode_id=%llu actor_id=%d target_id=%d restoration_tick=%lld confirmation_start_tick=%lld required_ticks=%lld target_pre_position=%d,%d target_restore_position=%d,%d target_post_position=%d,%d refreeze_seen=%d target_alive=%d target_unfrozen=%d associated_attack_tick=%lld associated_hammer_event_position=-1,-1",
					static_cast<unsigned long long>(Debug.m_EpisodeId), static_cast<unsigned long long>(Debug.m_RelatedRecoveryEpisodeId), Debug.m_PotentialSaver.m_ClientId, TargetId,
					static_cast<long long>(Debug.m_RestorationTick), static_cast<long long>(Debug.m_ConfirmationStartTick), Debug.m_RequiredTicks >= 0 ? static_cast<long long>(Debug.m_RequiredTicks) : 35LL,
					PreX, PreY, TargetX, TargetY, TargetX, TargetY, Debug.m_DebugLabel == "SAVE_REJECTED_REFREEZE" ? 1 : 0, TargetAlive ? 1 : 0, Debug.m_FrozenNow ? 0 : 1,
					static_cast<long long>(AssociatedAttackTick));
			}
		}

		if(HasSaveResult)
		{
			SAledResearchGeometryProbeRecord &Probe = m_aAledGeometryProbe[TargetId];
			if(Probe.m_Recorded && Probe.m_ProblemEpisodeId == Debug.m_EpisodeId && Probe.m_RecoveryEpisodeId == Debug.m_RelatedRecoveryEpisodeId &&
				Probe.m_TargetGeneration == Debug.m_Target.m_SlotGeneration && !Probe.m_ResultLogged)
			{
				LogAledGeometryResult(Probe, Debug);
				Probe.m_ResultLogged = true;
			}
		}
	}

	void CImpactSystem::OnInit()
	{
		m_LocalProcessingToggleInitialized = true;
		m_LocalRunEnableGate.Reset();
		ResetDisabledStartObservations();
		if(!g_Config.m_AmfImpactStatisticsAccessMigration)
		{
			// Preserve either previous visible-statistics preference as access to
			// the Statistics page. The new setting is the only active value after
			// this one-time migration.
			if(g_Config.m_AmfImpactShowOthersLegacy != 0 || g_Config.m_AmfImpactEnabledLegacy != 0)
				g_Config.m_AmfImpactStatisticsAccess = 1;
			g_Config.m_AmfImpactShowOthersLegacy = 0;
			g_Config.m_AmfImpactEnabledLegacy = 0;
			g_Config.m_AmfImpactStatisticsAccessMigration = 1;
		}
		m_LastLocalProcessingEnabled = g_Config.m_AmfImpactStatisticsAccess != 0;
		m_Phase2.Reset();
		m_vGameplayMessages.clear();
		m_vRecentNetworkMessages.clear();
		m_NextGameplayMessageId = 1;
		m_aLastFrameDebugState.fill(-1);
		m_aLastDebugFrameTeam.fill(-1);
		m_aLastDebugFreezeState.fill(-1);
		m_aLastDebugHookKnown.fill(-1);
		m_aLastDebugHookState.fill(-1);
		m_aLastDebugHookedPlayer.fill(-1);
		m_LastDebugFrameCount = -1;
		ResetMapInfoRequestState();
		LoadKogMapMetadataCache();
		m_aLastDebugUiFallGeneration.fill(0);
		m_aLastDebugUiFalls.fill(-1);
		m_ShadowSimulationCount = 0;
		m_ShadowSimulationTotalTicks = 0;
		ResetDiagnosticLogDeduplication();
		ResetStartTracking();
		m_vHistory.clear();
		m_vPendingSaveRuns.clear();
		m_LocalResetSnapshot = {};
		m_HistoryLoadSucceeded = CPersistence::LoadHistory(Storage(), &m_vHistory);
		if(!m_HistoryLoadSucceeded)
			log_warn("amf-impact", "failed to load run history; history writes disabled to preserve existing data");

		uint64_t MaxRunId = 0;
		for(const SRunSnapshot &SnapshotData : m_vHistory)
			MaxRunId = std::max(MaxRunId, SnapshotData.m_RunId);
		m_NextRunId = MaxRunId + 1;

		SRunSnapshot RestoredSnapshot;
		uint64_t RestoredNextRunId = 0;
		uint64_t RestoredNextEventId = 0;
		if(m_LastLocalProcessingEnabled && CPersistence::LoadActive(Storage(), &RestoredSnapshot, &RestoredNextRunId, &RestoredNextEventId))
		{
			m_NextRunId = std::max(m_NextRunId, RestoredNextRunId);
			m_NextEventId = std::max<uint64_t>(1, RestoredNextEventId);
			if(RestoredSnapshot.m_State == EAnalysisState::FINALIZED)
			{
				// Older clients could leave a finalized snapshot in the active
				// checkpoint. Migrate it to the current policy: a finished or
				// already-long-enough run is saved automatically, while a legacy
				// short snapshot gets one local decision window.
				if(ShouldAutoSaveFinalizedRun(RestoredSnapshot.m_Result, RestoredSnapshot.m_ActiveAnalysisTicks, RestoredSnapshot.m_Context.m_TickSpeed))
				{
					if(AppendHistoryRun(RestoredSnapshot))
						CPersistence::RemoveActive(Storage());
				}
				else
				{
					QueuePendingSaveRun(RestoredSnapshot);
				}
			}
			else if(m_CurrentRun.RestoreSnapshot(RestoredSnapshot))
			{
				m_HasCurrentRun = true;
				RestoreParticipantCosmeticsFromSnapshot(RestoredSnapshot);
				m_Phase2.SetRunContext(m_CurrentRun.RunId(), m_CurrentRun.Context().m_TeamId);
				m_Phase2.SetPendingEpisodes(m_CurrentRun.PendingEpisodes());
			}
			else
			{
				log_warn("amf-impact", "ignored invalid active run checkpoint");
				CPersistence::RemoveActive(Storage());
			}
		}
		else if(!m_LastLocalProcessingEnabled)
		{
			// A disabled local feature must not reopen a previously checkpointed
			// partial run on a later enable/restart. Finalized history is untouched.
			CPersistence::RemoveActive(Storage());
		}
		m_bImpactPersistenceShuttingDown = false;
		if(Engine() && Storage())
			m_pActivePersistenceQueue = std::make_shared<CActivePersistenceQueue>(Engine(), Storage());
	}

	void CImpactSystem::OnShutdown()
	{
		// Client shutdown drains the engine job pool before component shutdown.
		// Keep the final checkpoint synchronous after that drain.
		m_bImpactPersistenceShuttingDown = true;
		const bool LocalProcessingEnabled = SynchronizeLocalProcessingToggle();
		if(LocalProcessingEnabled && m_HasCurrentRun && !m_CurrentRun.IsFinalized() &&
			(m_CurrentRun.State() != EAnalysisState::WAITING || !m_CurrentRun.Participants().empty()))
			SaveActiveCheckpoint(true);
		if(LocalProcessingEnabled && g_Config.m_Debug)
			ImpactDebugLog("SHADOW_SUMMARY simulations=%llu total_ticks=%llu", static_cast<unsigned long long>(m_ShadowSimulationCount), static_cast<unsigned long long>(m_ShadowSimulationTotalTicks));
		CloseImpactDebugLog();
	}

	void CImpactSystem::OnReset()
	{
		SynchronizeLocalProcessingToggle();
		m_RunCompletionToast = {};
		m_LocalResetSnapshot = {};
		m_Phase2.Reset();
		m_vGameplayMessages.clear();
		m_vRecentNetworkMessages.clear();
		m_aSlotGeneration.fill(0);
		m_aSlotActive.fill(false);
		m_aSlotMissingSnapshots.fill(0);
		m_aSlotMissingSinceTick.fill(-1);
		m_aSlotMissingLastTick.fill(-1);
		m_aSlotDisconnectConfirmed.fill(false);
		m_aPendingResetGenerationAdvance.fill(false);
		m_aResetSawCharacterAbsence.fill(false);
		m_aLastFrameDebugState.fill(-1);
		m_aLastDebugFrameTeam.fill(-1);
		m_aLastDebugFreezeState.fill(-1);
		m_aLastDebugHookKnown.fill(-1);
		m_aLastDebugHookState.fill(-1);
		m_aLastDebugHookedPlayer.fill(-1);
		m_LastDebugFrameCount = -1;
		ResetMapInfoRequestState();
		m_aLastDebugUiFallGeneration.fill(0);
		m_aLastDebugUiFalls.fill(-1);
		m_ShadowSimulationCount = 0;
		m_ShadowSimulationTotalTicks = 0;
		ResetDiagnosticLogDeduplication();
		ResetStartTracking();
		ResetDisabledStartObservations();
	}

	void CImpactSystem::OnStateChange(int NewState, int OldState)
	{
		SynchronizeLocalProcessingToggle();
		if(NewState < IClient::STATE_ONLINE)
			m_RunCompletionToast = {};
		if(m_HasCurrentRun && OldState >= IClient::STATE_ONLINE && NewState < IClient::STATE_ONLINE)
		{
			const ERunResult Result = NewState == IClient::STATE_OFFLINE ? ERunResult::ABORTED_CONNECTION : ERunResult::ABORTED_SERVER_CHANGE;
			HandleInterruptedRun(Result);
		}
		if(NewState < IClient::STATE_ONLINE)
		{
			m_LocalRunEnableGate.Reset();
			ResetDisabledStartObservations();
			m_Phase2.Reset();
			m_vGameplayMessages.clear();
			m_vRecentNetworkMessages.clear();
			m_EngineActive = false;
			m_ServerContextKnown = false;
			for(bool &Active : m_aSlotActive)
				Active = false;
			m_aSlotMissingSnapshots.fill(0);
			m_aSlotMissingSinceTick.fill(-1);
			m_aSlotMissingLastTick.fill(-1);
			m_aSlotDisconnectConfirmed.fill(false);
			m_aPendingResetGenerationAdvance.fill(false);
			m_aResetSawCharacterAbsence.fill(false);
			m_aLastFrameDebugState.fill(-1);
			m_aLastDebugFrameTeam.fill(-1);
			m_aLastDebugFreezeState.fill(-1);
			m_aLastDebugHookKnown.fill(-1);
			m_aLastDebugHookState.fill(-1);
			m_aLastDebugHookedPlayer.fill(-1);
			m_LastDebugFrameCount = -1;
			ResetMapInfoRequestState();
			m_aLastDebugUiFallGeneration.fill(0);
			m_aLastDebugUiFalls.fill(-1);
			ResetDiagnosticLogDeduplication();
		}
	}

	void CImpactSystem::OnMapLoad()
	{
		SynchronizeLocalProcessingToggle();
		if(g_Config.m_AmfImpactStatisticsAccess == 0)
		{
			m_LocalRunEnableGate.Reset();
			ResetDisabledStartObservations();
			m_Phase2.Reset();
			m_vGameplayMessages.clear();
			m_vRecentNetworkMessages.clear();
			m_EngineActive = false;
			m_ServerContextKnown = false;
			return;
		}
		m_Phase2.Reset();
		m_vGameplayMessages.clear();
		m_vRecentNetworkMessages.clear();
		m_aSlotMissingSnapshots.fill(0);
		m_aSlotMissingSinceTick.fill(-1);
		m_aSlotMissingLastTick.fill(-1);
		m_aSlotDisconnectConfirmed.fill(false);
		m_aPendingResetGenerationAdvance.fill(false);
		m_aResetSawCharacterAbsence.fill(false);
		m_aLastFrameDebugState.fill(-1);
		m_aLastDebugFrameTeam.fill(-1);
		m_aLastDebugFreezeState.fill(-1);
		m_aLastDebugHookKnown.fill(-1);
		m_aLastDebugHookState.fill(-1);
		m_aLastDebugHookedPlayer.fill(-1);
		m_LastDebugFrameCount = -1;
		ResetMapInfoRequestState();
		ResetDiagnosticLogDeduplication();
		if(m_HasCurrentRun)
			HandleInterruptedRun(ERunResult::ABORTED_MAP_CHANGE);
	}

	void CImpactSystem::RefreshServerContext(bool *pChanged, bool *pMapChanged)
	{
		if(pChanged)
			*pChanged = false;
		if(pMapChanged)
			*pMapChanged = false;
		if(!HasLiveServerAddress(Client()->State()))
		{
			m_EngineActive = false;
			m_ServerContextKnown = false;
			return;
		}

		const CServerInfo &ServerInfo = Client()->ServerInfo();
		char aAddress[NETADDR_MAXSTRSIZE];
		net_addr_str(&Client()->ServerAddress(), aAddress, sizeof(aAddress), true);
		SRunContext NewContext;
		NewContext.m_ServerKey = std::string(aAddress) + "|" + ServerInfo.m_aCommunityId;
		NewContext.m_ServerName = ServerInfo.m_aName;
		NewContext.m_CommunityId = ServerInfo.m_aCommunityId;
		NewContext.m_Map = ServerInfo.m_aMap;
		NewContext.m_TickSpeed = Client()->GameTickSpeed();
		const bool IsKog = IsKogCommunity(NewContext.m_CommunityId.c_str());
		const bool HadPreviousContext = m_ServerContextKnown;
		const std::string PreviousServerKey = HadPreviousContext ? m_ServerContext.m_ServerKey : "";
		const std::string PreviousServerName = HadPreviousContext ? m_ServerContext.m_ServerName : "";
		const std::string PreviousMap = HadPreviousContext ? m_ServerContext.m_Map : "";
		const std::string PreviousCategory = HadPreviousContext ? m_ServerContext.m_MapDifficulty : "";
		const int PreviousRating = HadPreviousContext ? m_ServerContext.m_MapStars : -1;
		const bool ServerChanged = m_ServerContextKnown && NewContext.m_ServerKey != m_ServerContext.m_ServerKey;
		const bool MapChanged = m_ServerContextKnown && NewContext.m_Map != m_ServerContext.m_Map;
		if(ServerChanged || MapChanged)
		{
			// Detach old runtime metadata as soon as the new authoritative identity
			// is observed. Keep the old identity itself until the normal context swap
			// below so run transition detection remains unchanged.
			m_ServerContext.m_MapDifficulty.clear();
			m_ServerContext.m_MapStars = -1;
		}
		const std::string MapInfoSessionKey = std::string(aAddress) + "|" + NewContext.m_CommunityId + "|" + NewContext.m_Map;
		const bool MapSessionChanged = m_MapInfoSessionKey != MapInfoSessionKey;
		if(MapSessionChanged)
		{
			ResetMapInfoRequestState();
			m_MapInfoSessionKey = MapInfoSessionKey;
		}
		const bool ServerNameChanged = HadPreviousContext && PreviousServerName != NewContext.m_ServerName;
		const bool CategoryContextChanged = !HadPreviousContext || ServerChanged || MapChanged || ServerNameChanged;
		if(g_Config.m_Debug && CategoryContextChanged)
		{
			const std::string DiagnosticKey = PreviousServerKey + "|" + NewContext.m_ServerKey + "|" + PreviousMap + "|" + NewContext.m_Map + "|" +
				PreviousServerName + "|" + NewContext.m_ServerName + "|" + NewContext.m_CommunityId;
			if(m_KogCategoryContextLastDiagnosticKey != DiagnosticKey)
			{
				m_KogCategoryContextLastDiagnosticKey = DiagnosticKey;
				ImpactDebugLog("KOG_SERVER_CATEGORY event=CONTEXT previous_server_key=\"%s\" current_server_key=\"%s\" previous_map=\"%s\" current_map=\"%s\" previous_category=\"%s\" previous_rating=%d previous_server_name=\"%s\" current_server_name=\"%s\" current_game_type=\"%s\" current_community=\"%s\" current_address=\"%s\" client_state=%d server_info_type=%d server_changed=%s map_changed=%s server_name_changed=%s map_session_changed=%s",
					PreviousServerKey.c_str(), NewContext.m_ServerKey.c_str(), PreviousMap.c_str(), NewContext.m_Map.c_str(), PreviousCategory.c_str(), PreviousRating,
					PreviousServerName.c_str(), NewContext.m_ServerName.c_str(), ServerInfo.m_aGameType, NewContext.m_CommunityId.c_str(), aAddress, Client()->State(), ServerInfo.m_Type,
					ServerChanged ? "YES" : "NO", MapChanged ? "YES" : "NO", ServerNameChanged ? "YES" : "NO", MapSessionChanged ? "YES" : "NO");
			}
		}

		SMapDifficultyMetadata PassiveMetadata;
		bool HasPassiveMetadata = IsKog && ParseKogMapInfoDifficulty(GameClient()->m_aMapDescription, NewContext.m_Map.c_str(), &PassiveMetadata);
		if(!HasPassiveMetadata)
			HasPassiveMetadata = ParseMapInfoDifficulty(GameClient()->m_aMapDescription, NewContext.m_Map.c_str(), &PassiveMetadata);
		SMapDifficultyMetadata CachedMetadata;
		bool HasCachedMetadata = false;
		if(IsKog)
		{
			if(const SMapDifficultyMetadata *pCached = FindKogMapMetadata(NewContext.m_Map.c_str()))
			{
				CachedMetadata = *pCached;
				HasCachedMetadata = IsMapDifficultyMetadataUsable(CachedMetadata);
				if(MapSessionChanged)
				{
					if(g_Config.m_Debug)
						ImpactDebugLog("KOG_CATEGORY_CACHE_LOOKUP server_key=\"%s\" map=\"%s\" hit=YES cached_category=\"%s\" cached_rating=%d provenance=UNKNOWN_LEGACY_CACHE",
							NewContext.m_ServerKey.c_str(), NewContext.m_Map.c_str(), CachedMetadata.m_Category.c_str(), CachedMetadata.m_Stars);
					LogKogCategoryResolve(NewContext, NewContext.m_Map.c_str(), "", "RUNTIME_RESET", CachedMetadata.m_Category.c_str(), "CACHE", "HIT", "CACHE_LOOKUP");
				}
			}
			else if(MapSessionChanged)
			{
				if(g_Config.m_Debug)
					ImpactDebugLog("KOG_CATEGORY_CACHE_LOOKUP server_key=\"%s\" map=\"%s\" hit=NO cached_category=\"\" cached_rating=-1 provenance=NONE",
						NewContext.m_ServerKey.c_str(), NewContext.m_Map.c_str());
				LogKogCategoryResolve(NewContext, NewContext.m_Map.c_str(), "", "RUNTIME_RESET", "", "CACHE", "MISS", "CACHE_LOOKUP");
			}
		}
		SMapDifficultyMetadata ServerCategoryMetadata;
		const bool HasServerCategory = IsKog && ParseKogServerCategory(NewContext.m_ServerName.c_str(), &ServerCategoryMetadata);
		if((MapSessionChanged || CategoryContextChanged) && IsKog && g_Config.m_Debug)
		{
			ImpactDebugLog("KOG_SERVER_CATEGORY event=RESOLVE server_key=\"%s\" map=\"%s\" server=\"%s\" game_type=\"%s\" raw_category_evidence=\"%s\" normalized_category=\"%s\" status=%s reason=%s",
				NewContext.m_ServerKey.c_str(), NewContext.m_Map.c_str(), NewContext.m_ServerName.c_str(), ServerInfo.m_aGameType, NewContext.m_ServerName.c_str(),
				HasServerCategory ? ServerCategoryMetadata.m_Category.c_str() : "", HasServerCategory ? "ACCEPTED" : "REJECTED",
				HasServerCategory ? "CURRENT_SERVER_CONTEXT" : "NO_CANONICAL_CATEGORY_PHRASE");
		}
		if(MapSessionChanged && HasPassiveMetadata)
			LogKogCategoryResolve(NewContext, NewContext.m_Map.c_str(), "", "RUNTIME_RESET", PassiveMetadata.m_Category.c_str(), "PASSIVE_SV_MAPINFO", "ACCEPTED", "PASSIVE_CURRENT_MAPINFO");
		if(MapSessionChanged && HasServerCategory)
			LogKogCategoryResolve(NewContext, NewContext.m_Map.c_str(), HasCachedMetadata ? CachedMetadata.m_Category.c_str() : "", HasCachedMetadata ? "CACHE" : "NONE",
				ServerCategoryMetadata.m_Category.c_str(), "SERVER_CATEGORY", "CANDIDATE", "CURRENT_SERVER_CONTEXT");

		SMapDifficultyMetadata MapDifficulty;
		const char *pCategorySource = "NONE";
		const char *pRatingSource = "NONE";
		bool HasMapDifficulty = false;
		if(IsKog)
		{
			const SKogMapMetadataResolution Resolution = ResolveKogMapMetadata(
				PassiveMetadata, HasPassiveMetadata, CachedMetadata, HasCachedMetadata, ServerCategoryMetadata, HasServerCategory);
			MapDifficulty = Resolution.m_Metadata;
			pCategorySource = Resolution.m_CategorySource;
			pRatingSource = Resolution.m_RatingSource;
			HasMapDifficulty = IsMapDifficultyMetadataUsable(MapDifficulty);
			if(MapSessionChanged && HasCachedMetadata && !CachedMetadata.m_Category.empty() && !MapDifficulty.m_Category.empty() &&
				CachedMetadata.m_Category != MapDifficulty.m_Category && std::strcmp(pCategorySource, "CACHE") != 0)
				LogKogCategoryResolve(NewContext, NewContext.m_Map.c_str(), CachedMetadata.m_Category.c_str(), "CACHE", MapDifficulty.m_Category.c_str(), pCategorySource,
					"ACCEPTED", "CURRENT_CATEGORY_OVERRIDES_CACHED_CATEGORY");
		}
		else if(HasPassiveMetadata)
		{
			MapDifficulty = PassiveMetadata;
			pCategorySource = !MapDifficulty.m_Category.empty() ? "PASSIVE_SV_MAPINFO" : "NONE";
			pRatingSource = MapDifficulty.m_Stars >= 0 ? "PASSIVE_SV_MAPINFO" : "NONE";
			HasMapDifficulty = IsMapDifficultyMetadataUsable(MapDifficulty);
		}
		const char *pMetadataSource = !MapDifficulty.m_Category.empty() ? pCategorySource : pRatingSource;
		if(IsKog && MapSessionChanged && HasMapDifficulty && MapDifficulty.m_Stars < 0)
		{
			const SKogVoteRatingLookup Lookup = LookupKogVoteListRating(GameClient()->m_Voting, NewContext.m_Map.c_str());
			const char *pLookupStatus = Lookup.m_Accepted ? "ACCEPTED" : (Lookup.m_ExactNameMatches > 0 ? "REJECTED" : "NOT_FOUND");
			LogKogVoteRatingLookup(NewContext.m_Map.c_str(), Lookup.m_OptionsTotal, Lookup.m_ExactNameMatches, pLookupStatus,
				Lookup.m_Reason.c_str(), Lookup.m_RawOption.c_str(), Lookup.m_Accepted ? Lookup.m_Rating : -1);
			if(Lookup.m_Accepted)
			{
				MapDifficulty.m_Stars = Lookup.m_Rating;
				pRatingSource = "KOG_VOTE_LIST";
				if(g_Config.m_Debug)
					ImpactDebugLog("KOG_VOTE_METADATA_MATCH map=\"%s\" raw_option=\"%s\" rating=%d status=ACCEPTED",
						NewContext.m_Map.c_str(), Lookup.m_RawOption.c_str(), Lookup.m_Rating);
			}
		}
		if(HasMapDifficulty)
		{
			NewContext.m_MapDifficulty = MapDifficulty.m_Category;
			NewContext.m_MapStars = MapDifficulty.m_Stars;
			if(IsKog && std::strcmp(pMetadataSource, "NONE") != 0)
				StoreKogMapMetadata(NewContext.m_Map.c_str(), MapDifficulty,
					std::strcmp(pCategorySource, "PASSIVE_SV_MAPINFO") == 0 || std::strcmp(pCategorySource, "SERVER_CATEGORY") == 0);
		}
		if(m_ServerContextKnown)
			RebindMapMetadataForIdentity(m_ServerContext, &NewContext);
		SMapDifficultyMetadata ResolvedMetadata;
		ResolvedMetadata.m_Category = NewContext.m_MapDifficulty;
		ResolvedMetadata.m_Stars = NewContext.m_MapStars;
		const bool HasResolvedMetadata = IsMapDifficultyMetadataUsable(ResolvedMetadata);
		const bool ActiveRunIdentityMatches = m_HasCurrentRun && m_CurrentRun.Context().m_ServerKey == NewContext.m_ServerKey && m_CurrentRun.Context().m_Map == NewContext.m_Map;
		std::string PreviousRunCategory;
		int PreviousRunRating = -1;
		if(ActiveRunIdentityMatches)
		{
			PreviousRunCategory = m_CurrentRun.Context().m_MapDifficulty;
			PreviousRunRating = m_CurrentRun.Context().m_MapStars;
		}
		if(ActiveRunIdentityMatches && HasResolvedMetadata)
		{
			const bool CurrentCategoryIsAuthoritative = std::strcmp(pCategorySource, "PASSIVE_SV_MAPINFO") == 0 || std::strcmp(pCategorySource, "SERVER_CATEGORY") == 0;
			const bool RunMetadataChanged = CurrentCategoryIsAuthoritative ? m_CurrentRun.UpdateAuthoritativeMapDifficultyMetadata(ResolvedMetadata) : m_CurrentRun.UpdateMapDifficultyMetadata(ResolvedMetadata);
			if(RunMetadataChanged && g_Config.m_Debug)
				ImpactDebugLog("KOG_CATEGORY_ACTIVE_RUN map=\"%s\" category_before=\"%s\" category_after=\"%s\" rating_before=%d rating_after=%d category_source=%s rating_source=%s",
					NewContext.m_Map.c_str(), PreviousRunCategory.c_str(), m_CurrentRun.Context().m_MapDifficulty.c_str(), PreviousRunRating, m_CurrentRun.Context().m_MapStars,
					pCategorySource, pRatingSource);
		}
		if(g_Config.m_Debug && (MapSessionChanged || CategoryContextChanged))
		{
			const char *pStatus = !m_HasCurrentRun ? "REJECTED" : !ActiveRunIdentityMatches ? "REJECTED" : !HasResolvedMetadata ? "REJECTED" : "ACCEPTED";
			const char *pReason = !m_HasCurrentRun ? "NO_ACTIVE_RUN" : !ActiveRunIdentityMatches ? "RUN_IDENTITY_MISMATCH" : !HasResolvedMetadata ? "NO_RESOLVED_METADATA" : "RUNTIME_PROPAGATED_OR_UNCHANGED";
			ImpactDebugLog("KOG_DIFFICULTY_ACTIVE_RUN event=REFRESH run_id=%llu server_key=\"%s\" run_server_key=\"%s\" map=\"%s\" run_map=\"%s\" previous_difficulty=\"%s\" candidate_difficulty=\"%s\" new_difficulty=\"%s\" rating=%d source=%s status=%s reason=%s",
				static_cast<unsigned long long>(m_HasCurrentRun ? m_CurrentRun.RunId() : 0), NewContext.m_ServerKey.c_str(), m_HasCurrentRun ? m_CurrentRun.Context().m_ServerKey.c_str() : "",
				NewContext.m_Map.c_str(), m_HasCurrentRun ? m_CurrentRun.Context().m_Map.c_str() : "", PreviousRunCategory.c_str(),
				MapDifficulty.m_Category.c_str(), m_HasCurrentRun && ActiveRunIdentityMatches ? m_CurrentRun.Context().m_MapDifficulty.c_str() : "", NewContext.m_MapStars,
				pCategorySource, pStatus, pReason);
		}
		if(g_Config.m_Debug && (MapSessionChanged || !m_ServerContextKnown || m_ServerContext.m_Map != NewContext.m_Map ||
			m_ServerContext.m_MapDifficulty != NewContext.m_MapDifficulty || m_ServerContext.m_MapStars != NewContext.m_MapStars))
			ImpactDebugLog("KOG_CATEGORY_RUNTIME server_key=\"%s\" previous_server_key=\"%s\" previous_map=\"%s\" map=\"%s\" previous_category=\"%s\" category=\"%s\" previous_rating=%d rating=%d category_source=%s rating_source=%s server_changed=%s map_changed=%s map_session_changed=%s",
				NewContext.m_ServerKey.c_str(), PreviousServerKey.c_str(), PreviousMap.c_str(), NewContext.m_Map.c_str(), PreviousCategory.c_str(), NewContext.m_MapDifficulty.c_str(), PreviousRating, NewContext.m_MapStars,
				NewContext.m_MapDifficulty.empty() && std::strcmp(pCategorySource, "NONE") == 0 ? "NONE" : pCategorySource,
				pRatingSource, ServerChanged ? "YES" : "NO", MapChanged ? "YES" : "NO", MapSessionChanged ? "YES" : "NO");
		if(g_Config.m_Debug && (MapSessionChanged || !m_ServerContextKnown || m_ServerContext.m_Map != NewContext.m_Map ||
			m_ServerContext.m_MapDifficulty != NewContext.m_MapDifficulty || m_ServerContext.m_MapStars != NewContext.m_MapStars))
			ImpactDebugLog("KOG_DIFFICULTY_RUNTIME event=RESOLVE server_key=\"%s\" map=\"%s\" previous_difficulty=\"%s\" candidate_difficulty=\"%s\" new_difficulty=\"%s\" rating=%d source=%s status=%s reason=%s",
				NewContext.m_ServerKey.c_str(), NewContext.m_Map.c_str(), PreviousCategory.c_str(), MapDifficulty.m_Category.c_str(), NewContext.m_MapDifficulty.c_str(), NewContext.m_MapStars,
				pCategorySource, NewContext.m_MapDifficulty.empty() ? "REJECTED" : "ACCEPTED", NewContext.m_MapDifficulty.empty() ? "NO_CATEGORY" : "RESOLVED");
		LogMapMetadataResolution(pMetadataSource, NewContext, 0, HasMapDifficulty ? nullptr : (IsKog ? "WAITING_FOR_MAPINFO" : "PASSIVE_METADATA_UNAVAILABLE"));
		MaybeRequestKogMapInfo(NewContext, Client()->GameTick(0));
		const bool Changed = ServerChanged || MapChanged;
		if(pChanged)
			*pChanged = Changed;
		if(pMapChanged)
			*pMapChanged = MapChanged;
		m_ServerContext = std::move(NewContext);
		m_ServerContextKnown = true;
		RefreshImpactEligibility();
	}

	void CImpactSystem::UpdateSlotGenerations()
	{
		const int64_t Tick = CurrentTick();
		const int TickSpeed = m_HasCurrentRun ? m_CurrentRun.Context().m_TickSpeed : m_ServerContext.m_TickSpeed;
		for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
		{
			const auto &RawCurrent = RawCurrentObservation(GameClient(), ClientId);
			const auto &RawPrevious = RawPreviousObservation(GameClient(), ClientId);
			const bool Observed = HasImpactSlotObservation(GameClient(), ClientId);
			const bool WasActive = m_aSlotActive[ClientId];
			const bool HasUnfinalizedRunParticipant = m_HasCurrentRun && std::any_of(
				m_CurrentRun.Participants().begin(), m_CurrentRun.Participants().end(), [ClientId](const SParticipant &Participant) {
					return Participant.m_Id.m_ClientId == ClientId && !Participant.m_Finalized;
				});
			bool GenerationChanged = false;
			if(m_aPendingResetGenerationAdvance[ClientId] && !RawCurrent.m_FromRawCurrentSnapshot)
				m_aResetSawCharacterAbsence[ClientId] = true;

			if(Observed)
			{
				m_aSlotMissingSnapshots[ClientId] = 0;
				m_aSlotMissingSinceTick[ClientId] = -1;
				m_aSlotMissingLastTick[ClientId] = -1;
				if(!WasActive)
				{
					if(++m_aSlotGeneration[ClientId] == 0)
						m_aSlotGeneration[ClientId] = 1;
					GenerationChanged = true;
					m_aPendingResetGenerationAdvance[ClientId] = false;
					m_aResetSawCharacterAbsence[ClientId] = false;
				}
				else if(m_aPendingResetGenerationAdvance[ClientId] && m_aResetSawCharacterAbsence[ClientId] && RawCurrent.m_FromRawCurrentSnapshot)
				{
					if(++m_aSlotGeneration[ClientId] == 0)
						m_aSlotGeneration[ClientId] = 1;
					GenerationChanged = true;
					m_aPendingResetGenerationAdvance[ClientId] = false;
					m_aResetSawCharacterAbsence[ClientId] = false;
				}
				m_aSlotActive[ClientId] = true;
			}
			else if(WasActive || HasUnfinalizedRunParticipant)
			{
				if(m_aSlotMissingSinceTick[ClientId] < 0)
				{
					m_aSlotMissingSinceTick[ClientId] = Tick;
					m_aSlotMissingSnapshots[ClientId] = 1;
					m_aSlotMissingLastTick[ClientId] = Tick;
				}
				else if(Tick > m_aSlotMissingLastTick[ClientId])
				{
					++m_aSlotMissingSnapshots[ClientId];
					m_aSlotMissingLastTick[ClientId] = Tick;
				}
				if(IsParticipantDisconnectConfirmed(m_aSlotMissingSnapshots[ClientId], Tick - m_aSlotMissingSinceTick[ClientId], TickSpeed))
				{
					m_aSlotActive[ClientId] = false;
					m_aSlotDisconnectConfirmed[ClientId] = true;
					m_aPendingResetGenerationAdvance[ClientId] = false;
					m_aResetSawCharacterAbsence[ClientId] = false;
				}
			}

			const bool SlotChanged = m_aSlotActive[ClientId] != WasActive;
			if(g_Config.m_Debug && (SlotChanged || GenerationChanged))
			{
				const int TeamId = GameClient()->m_Teams.Team(ClientId);
				const bool TeamMember = m_aSlotActive[ClientId] && !IsImpactSpectator(GameClient(), ClientId) && IsValidImpactTeam(TeamId);
				const SParticipantId Participant{ClientId, m_aSlotGeneration[ClientId], ClientId == GameClient()->m_aLocalIds[1] && ClientId != GameClient()->m_aLocalIds[0]};
				const bool RosterMember = m_HasCurrentRun && m_CurrentRun.HasParticipant(Participant);
				const bool Started = m_HasCurrentRun && m_CurrentRun.HasStartedParticipant(Participant);
				const bool Engaged = m_HasCurrentRun && m_CurrentRun.IsParticipantEngaged(Participant);
				ImpactDebugLog("PARTICIPANT_DISCOVERY server_tick=%lld client_id=%d generation=%u team_id=%d player_info_known=%d team_member=%d roster_member=%d started=%d engaged=%d eligible=%d current_character_known=%d previous_character_known=%d present=%d",
					static_cast<long long>(CurrentTick()), ClientId, m_aSlotGeneration[ClientId], TeamId,
					GameClient()->m_Snap.m_apPlayerInfos[ClientId] != nullptr ? 1 : 0, TeamMember ? 1 : 0,
					RosterMember ? 1 : 0, Started ? 1 : 0, Engaged ? 1 : 0, RosterMember ? 1 : 0, RawCurrent.m_FromRawCurrentSnapshot ? 1 : 0,
					RawPrevious.m_FromRawPreviousSnapshot ? 1 : 0, Observed ? 1 : 0);
			}
		}
	}

	void CImpactSystem::ResetStartTracking()
	{
		m_aStartTrackingGeneration.fill(0);
		m_aStartTrackingTeam.fill(0);
		m_aDeferredStartGeneration.fill(0);
		m_aDeferredStartTeam.fill(0);
		m_aDeferredStartObservation.fill(false);
		m_aDeferredStartTick.fill(-1);
		m_aStartOnTile.fill(false);
		m_aStartLastCharacterTick.fill(-1);
		m_aStartDebugGeneration.fill(0);
	}

	void CImpactSystem::ResetDisabledStartObservations(bool BaselinePending)
	{
		m_aDisabledStartSeen.fill(false);
		m_aDisabledStartTeam.fill(0);
		m_aDisabledDeferredStart.fill(false);
		m_aDisabledDeferredStartTeam.fill(0);
		m_DisabledStartBaselinePending = BaselinePending;
	}

	bool CImpactSystem::SynchronizeLocalProcessingToggle()
	{
		const bool RequestedEnabled = g_Config.m_AmfImpactStatisticsAccess != 0;
		if(!m_LocalProcessingToggleInitialized)
		{
			m_LocalProcessingToggleInitialized = true;
			m_LastLocalProcessingEnabled = RequestedEnabled;
			return RequestedEnabled;
		}
		if(RequestedEnabled == m_LastLocalProcessingEnabled)
			return RequestedEnabled;

		if(!RequestedEnabled)
		{
			if(m_HasCurrentRun && m_CurrentRun.StartedParticipantCount() > 0)
				m_LocalRunEnableGate.LockForStartedRun(m_CurrentRun.Context().m_TeamId);
			if(m_HasCurrentRun)
				DiscardActiveAnalysis();
			m_vGameplayMessages.clear();
			m_vRecentNetworkMessages.clear();
			m_RunCompletionToast = {};
			m_LastLocalProcessingEnabled = false;
			ResetDisabledStartObservations();
			ImpactDebugLog("LOCAL_PROCESSING_DISABLED run_locked=%d team_id=%d",
				m_LocalRunEnableGate.m_RunLocked ? 1 : 0, m_LocalRunEnableGate.m_TeamId);
			return false;
		}

		// The config can be toggled between authoritative snapshots. Re-evaluate
		// the last raw current/previous pair before accepting enable, so a START
		// that arrived while disabled cannot slip through this frame boundary.
		if(Client()->State() == IClient::STATE_ONLINE)
			ObserveDisabledStartLifecycle();
		if(!m_LocalRunEnableGate.CanEnable())
		{
			g_Config.m_AmfImpactStatisticsAccess = 0;
			GameClient()->m_Menus.ShowAmfRunStatisticsEnableWarning();
			ImpactDebugLog("LOCAL_PROCESSING_ENABLE_REJECTED team_id=%d reason='active run must be reset first'", m_LocalRunEnableGate.m_TeamId);
			return false;
		}

		m_LastLocalProcessingEnabled = true;
		ResetDisabledStartObservations();
		return true;
	}

	void CImpactSystem::ObserveDisabledStartLifecycle()
	{
		if(m_LocalRunEnableGate.m_RunLocked || Client()->State() != IClient::STATE_ONLINE || !GameClient()->m_GameInfo.m_Race)
		{
			if(!m_LocalRunEnableGate.m_RunLocked)
				ResetDisabledStartObservations();
			return;
		}

		if(m_DisabledStartBaselinePending)
		{
			// A server-confirmed reset creates a fresh pre-run baseline. Do not
			// mistake the reset teleport back onto the start tile for a new START.
			for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
			{
				const auto &RawCurrent = RawCurrentObservation(GameClient(), ClientId);
				if(GameClient()->m_Snap.m_apPlayerInfos[ClientId] == nullptr || !RawCurrent.m_FromRawCurrentSnapshot || IsImpactSpectator(GameClient(), ClientId))
					continue;
				m_aDisabledStartSeen[ClientId] = true;
				m_aDisabledStartTeam[ClientId] = GameClient()->m_Teams.Team(ClientId);
			}
			m_DisabledStartBaselinePending = false;
			return;
		}

		int SelectedTeamId = 0;
		for(const int ClientId : GameClient()->m_aLocalIds)
		{
			if(ClientId < 0 || ClientId >= MAX_CLIENTS || GameClient()->m_Snap.m_apPlayerInfos[ClientId] == nullptr ||
				!RawCurrentObservation(GameClient(), ClientId).m_FromRawCurrentSnapshot || IsImpactSpectator(GameClient(), ClientId))
				continue;
			const int TeamId = GameClient()->m_Teams.Team(ClientId);
			if(IsValidImpactTeam(TeamId))
			{
				SelectedTeamId = TeamId;
				break;
			}
		}
		if(!IsValidImpactTeam(SelectedTeamId))
		{
			ResetDisabledStartObservations();
			return;
		}

		int TeamMemberCount = 0;
		for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
		{
			if(GameClient()->m_Snap.m_apPlayerInfos[ClientId] != nullptr && !IsImpactSpectator(GameClient(), ClientId) &&
				GameClient()->m_Teams.Team(ClientId) == SelectedTeamId)
				++TeamMemberCount;
		}
		const bool TeamContextConfirmed = IsStartTeamContextUsable(SelectedTeamId, TeamMemberCount, false, SelectedTeamId) &&
			IsStartTeamMembershipConfirmed(SelectedTeamId, TeamMemberCount);

		for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
		{
			const CNetObj_PlayerInfo *pInfo = GameClient()->m_Snap.m_apPlayerInfos[ClientId];
			const auto &RawCurrent = RawCurrentObservation(GameClient(), ClientId);
			const auto &RawPrevious = RawPreviousObservation(GameClient(), ClientId);
			if(pInfo == nullptr || !RawCurrent.m_FromRawCurrentSnapshot || IsImpactSpectator(GameClient(), ClientId))
			{
				m_aDisabledStartSeen[ClientId] = false;
				m_aDisabledStartTeam[ClientId] = 0;
				m_aDisabledDeferredStart[ClientId] = false;
				m_aDisabledDeferredStartTeam[ClientId] = 0;
				continue;
			}

			const int TeamId = GameClient()->m_Teams.Team(ClientId);
			if(m_aDisabledDeferredStart[ClientId] && m_aDisabledDeferredStartTeam[ClientId] != 0 &&
				m_aDisabledDeferredStartTeam[ClientId] != TeamId)
			{
				m_aDisabledDeferredStart[ClientId] = false;
				m_aDisabledDeferredStartTeam[ClientId] = 0;
			}
			const bool FirstReliableObservation = !m_aDisabledStartSeen[ClientId] || m_aDisabledStartTeam[ClientId] != TeamId;
			const bool PreviousCharacterKnown = RawPrevious.m_FromRawPreviousSnapshot;
			const vec2 Position(RawCurrent.m_Character.m_X, RawCurrent.m_Character.m_Y);
			const vec2 PreviousPosition(RawPrevious.m_Character.m_X, RawPrevious.m_Character.m_Y);
			const bool OnStartTile = GameClient()->RaceHelper()->IsStart(ClientId, Position, Position);
			const bool CrossedStart = PreviousCharacterKnown && GameClient()->RaceHelper()->IsStart(ClientId, PreviousPosition, Position);
			const bool StartSignal = IsStartObservationConfirmed(FirstReliableObservation, PreviousCharacterKnown, OnStartTile, CrossedStart, false);
			const bool DeferredStart = IsDeferredStartObservationConfirmed(m_aDisabledDeferredStart[ClientId], TeamContextConfirmed, false);
			const protocol7::CNetObj_PlayerInfoRace *pRaceInfo = Client()->IsSixup() ? Client()->m_TranslationContext.m_apPlayerInfosRace[ClientId] : nullptr;
			const bool AuthoritativeStart = IsAuthoritativeStartedParticipant(pInfo != nullptr, pRaceInfo != nullptr,
				pRaceInfo == nullptr ? -1 : pRaceInfo->m_RaceStartTick, CurrentTick(), TeamId, SelectedTeamId,
				TeamId == SelectedTeamId ? TeamMemberCount : 0, false);
			if(AuthoritativeStart || (TeamContextConfirmed && (StartSignal || DeferredStart)))
			{
				m_LocalRunEnableGate.LockForStartedRun(SelectedTeamId);
				ImpactDebugLog("LOCAL_PROCESSING_RUN_LOCKED server_tick=%lld team_id=%d client_id=%d source=%s",
					static_cast<long long>(CurrentTick()), SelectedTeamId, ClientId, AuthoritativeStart ? "sixup_race_start" : (DeferredStart ? "deferred_start" : "raw_start_crossing"));
				return;
			}
			if(StartSignal && !TeamContextConfirmed)
			{
				m_aDisabledDeferredStart[ClientId] = true;
				m_aDisabledDeferredStartTeam[ClientId] = TeamId;
			}
			m_aDisabledStartSeen[ClientId] = true;
			m_aDisabledStartTeam[ClientId] = TeamId;
		}
	}

	void CImpactSystem::DetectStartCrossings()
	{
		if(!GameClient()->m_GameInfo.m_Race)
		{
			ResetStartTracking();
			return;
		}
		STickEventBatch StartBatch;
		StartBatch.m_ServerTick = CurrentTick();
		int SelectedTeamId = m_HasCurrentRun ? m_CurrentRun.Context().m_TeamId : CurrentLocalTeamId();

		for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
		{
			const auto &Character = GameClient()->m_Snap.m_aCharacters[ClientId];
			const auto &RawCurrent = RawCurrentObservation(GameClient(), ClientId);
			const auto &RawPrevious = RawPreviousObservation(GameClient(), ClientId);
			if(!m_aSlotActive[ClientId] || IsImpactSpectator(GameClient(), ClientId))
			{
				m_aStartTrackingGeneration[ClientId] = 0;
				m_aStartTrackingTeam[ClientId] = 0;
				m_aDeferredStartGeneration[ClientId] = 0;
				m_aDeferredStartTeam[ClientId] = 0;
				m_aDeferredStartObservation[ClientId] = false;
				m_aDeferredStartTick[ClientId] = -1;
				m_aStartOnTile[ClientId] = false;
				m_aStartLastCharacterTick[ClientId] = -1;
				m_aStartDebugGeneration[ClientId] = 0;
				continue;
			}
			const uint32_t Generation = m_aSlotGeneration[ClientId];
			const int TeamId = GameClient()->m_Teams.Team(ClientId);
			const SParticipantId Participant{ClientId, Generation, ClientId == GameClient()->m_aLocalIds[1] && ClientId != GameClient()->m_aLocalIds[0]};
			const int StartRosterMemberCount = CountStartRosterMembers(TeamId);
			const bool CurrentRunMatchesTeam = m_HasCurrentRun && m_CurrentRun.Context().m_TeamId == TeamId;
			const bool AlreadyStarted = CurrentRunMatchesTeam && m_CurrentRun.HasStartedParticipant(Participant);
			const protocol7::CNetObj_PlayerInfoRace *pRaceInfo = Client()->IsSixup() ? Client()->m_TranslationContext.m_apPlayerInfosRace[ClientId] : nullptr;
			const int64_t RaceStartTick = pRaceInfo == nullptr ? -1 : pRaceInfo->m_RaceStartTick;
			if(IsAuthoritativeStartedParticipant(GameClient()->m_Snap.m_apPlayerInfos[ClientId] != nullptr, pRaceInfo != nullptr,
				RaceStartTick, StartBatch.m_ServerTick, TeamId, SelectedTeamId, StartRosterMemberCount, AlreadyStarted))
			{
				m_aStartTrackingGeneration[ClientId] = Generation;
				m_aStartTrackingTeam[ClientId] = TeamId;
				m_aDeferredStartGeneration[ClientId] = 0;
				m_aDeferredStartTeam[ClientId] = 0;
				m_aDeferredStartObservation[ClientId] = false;
				m_aDeferredStartTick[ClientId] = -1;
				m_aStartOnTile[ClientId] = false;
				m_aStartLastCharacterTick[ClientId] = RawCurrent.m_FromRawCurrentSnapshot ? Character.m_Cur.m_Tick : -1;
				m_aStartDebugGeneration[ClientId] = Generation;
				ImpactDebugLog("START_AUTHORITATIVE_RECOVERY server_tick=%lld client_id=%d generation=%u team_id=%d race_start_tick=%lld player_info=1 current_character=%d previous_character=%d source='sixup PlayerInfoRace'",
					static_cast<long long>(StartBatch.m_ServerTick), ClientId, Generation, TeamId, static_cast<long long>(RaceStartTick),
					RawCurrent.m_FromRawCurrentSnapshot ? 1 : 0, RawPrevious.m_FromRawPreviousSnapshot ? 1 : 0);
				SImpactEvent Event;
				Event.m_EventId = m_NextEventId++;
				Event.m_Type = EImpactEventType::START_CROSSING;
				Event.m_Actor = Participant;
				Event.m_TeamId = TeamId;
				Event.m_ActorName = GameClient()->m_aClients[ClientId].m_aName;
				Event.m_StartTick = RaceStartTick;
				Event.m_ResolveTick = StartBatch.m_ServerTick;
				Event.m_Confirmed = true;
				StartBatch.m_vCandidates.push_back(std::move(Event));
				continue;
			}
			if(!RawCurrent.m_FromRawCurrentSnapshot)
				continue;

			// Team changes are a new personal-start context even when DDNet keeps the
			// same client slot generation. Do not let a start accepted in Team 5 be
			// reused when that slot later appears in Team 15.
			if(m_aStartTrackingGeneration[ClientId] == Generation && m_aStartTrackingTeam[ClientId] != 0 &&
				m_aStartTrackingTeam[ClientId] != TeamId)
			{
				m_aStartTrackingGeneration[ClientId] = 0;
				m_aStartTrackingTeam[ClientId] = 0;
				m_aDeferredStartGeneration[ClientId] = 0;
				m_aDeferredStartTeam[ClientId] = 0;
				m_aDeferredStartObservation[ClientId] = false;
				m_aDeferredStartTick[ClientId] = -1;
				m_aStartOnTile[ClientId] = false;
				m_aStartLastCharacterTick[ClientId] = -1;
				m_aStartDebugGeneration[ClientId] = 0;
			}
			const vec2 PrevPos(Character.m_Prev.m_X, Character.m_Prev.m_Y);
			const vec2 Pos(RawCurrent.m_Character.m_X, RawCurrent.m_Character.m_Y);
			const bool PreviousCharacterKnown = RawPrevious.m_FromRawPreviousSnapshot;
			const bool OnStartTile = GameClient()->RaceHelper()->IsStart(ClientId, Pos, Pos);
			const bool PreviousOnStartTile = PreviousCharacterKnown && GameClient()->RaceHelper()->IsStart(ClientId, PrevPos, PrevPos);
			const int TeamMemberCount = StartRosterMemberCount;
			const bool TeamMembershipConfirmed = IsStartTeamMembershipConfirmed(TeamId, TeamMemberCount);
			const bool ExistingParticipantContext = m_HasCurrentRun &&
				TeamId == SelectedTeamId && m_CurrentRun.StartedParticipantCount() > 0;
			const bool HasValidTeam = IsStartTeamContextUsable(TeamId, TeamMemberCount, ExistingParticipantContext, SelectedTeamId);
			// HasParticipant is keyed by slot generation, while a participant's
			// personal Start is also scoped to the current DDNet team context.  Do
			// not let a stale participant from Team 5 make the same slot look
			// already-started after it has moved to Team 15 before the old run was
			// retired by the next complete snapshot.
			// ExistingParticipantContext keeps an established run usable across a
			// sparse roster snapshot, but it must not promote a new personal Start.
			// A new Start is accepted only after the current non-zero team is
			// observed with the authoritative two-member roster.
			const bool StartTeamContextConfirmed = HasValidTeam && TeamMembershipConfirmed;
			// Team 0 means that the character/start observation arrived before the
			// team-state snapshot. It is intentionally compatible with the first
			// later non-zero team for this slot generation; a known non-zero team is
			// only compatible with that same team context.
			if(m_aDeferredStartGeneration[ClientId] == Generation && m_aDeferredStartObservation[ClientId] &&
				m_aDeferredStartTeam[ClientId] != 0 && TeamId != 0 && m_aDeferredStartTeam[ClientId] != TeamId)
			{
				m_aDeferredStartGeneration[ClientId] = 0;
				m_aDeferredStartTeam[ClientId] = 0;
				m_aDeferredStartObservation[ClientId] = false;
				m_aDeferredStartTick[ClientId] = -1;
			}
			const bool DeferredStartObservation = m_aDeferredStartGeneration[ClientId] == Generation &&
				(m_aDeferredStartTeam[ClientId] == 0 || m_aDeferredStartTeam[ClientId] == TeamId) &&
				m_aDeferredStartObservation[ClientId];
			const bool FirstReliableObservation = m_aStartTrackingGeneration[ClientId] != Generation;
			const bool CrossedStart = PreviousCharacterKnown && GameClient()->RaceHelper()->IsStart(ClientId, PrevPos, Pos);
			const bool StartSignal = IsStartObservationConfirmed(FirstReliableObservation, PreviousCharacterKnown, OnStartTile, CrossedStart, AlreadyStarted);
			const bool StartCandidate = StartSignal && StartTeamContextConfirmed;
			const bool DeferredStartCandidate = IsDeferredStartObservationConfirmed(DeferredStartObservation, StartTeamContextConfirmed, AlreadyStarted);
			const bool StartTileChanged = OnStartTile != m_aStartOnTile[ClientId];
			const bool ShouldLogStartObservation = m_aStartDebugGeneration[ClientId] != Generation || StartTileChanged || (CrossedStart && !AlreadyStarted) || DeferredStartObservation;
			auto LogStartObservation = [&](const char *pResult) {
				if(!ShouldLogStartObservation)
					return;
				const vec2 LoggedPrevPos = PreviousCharacterKnown ? PrevPos : vec2(-1.0f, -1.0f);
				ImpactDebugLog("START_OBSERVATION server_tick=%lld client_id=%d generation=%u team_id=%d previous_position_known=%d previous_position=%d,%d current_position=%d,%d current_on_start=%d previous_on_start=%d segment_crosses_start=%d deferred_start=%d already_started=%d result=%s",
					static_cast<long long>(StartBatch.m_ServerTick), ClientId, Generation, TeamId, PreviousCharacterKnown ? 1 : 0,
					static_cast<int>(LoggedPrevPos.x), static_cast<int>(LoggedPrevPos.y), static_cast<int>(Pos.x), static_cast<int>(Pos.y),
					OnStartTile ? 1 : 0, PreviousOnStartTile ? 1 : 0, CrossedStart ? 1 : 0, DeferredStartObservation ? 1 : 0,
					AlreadyStarted ? 1 : 0, pResult);
				m_aStartDebugGeneration[ClientId] = Generation;
			};
			if(m_aStartTrackingGeneration[ClientId] != Generation)
			{
				// A deferred remote Start is not accepted merely because one snapshot
				// happens to carry a non-zero team id. Wait for the same team to be
				// confirmed by the normal two-member roster gate; this keeps a sparse
				// team-state update from binding the Start to the wrong run context.
				if(DeferredStartObservation && AlreadyStarted)
				{
					m_aStartTrackingGeneration[ClientId] = Generation;
					m_aStartTrackingTeam[ClientId] = TeamId;
					m_aDeferredStartGeneration[ClientId] = 0;
					m_aDeferredStartTeam[ClientId] = 0;
					m_aDeferredStartObservation[ClientId] = false;
					m_aDeferredStartTick[ClientId] = -1;
					LogStartObservation("already_started");
				}
				if(DeferredStartCandidate)
				{
					const int64_t DeferredStartTick = m_aDeferredStartTick[ClientId] >= 0 ? m_aDeferredStartTick[ClientId] : StartBatch.m_ServerTick;
					LogStartObservation("accepted_deferred");
					if(g_Config.m_Debug)
						log_debug("amf-impact", "deferred start candidate accepted client=%d generation=%u team=%d team_member=1 start_tile=0 started=0 eligible=1 reason='first start observation preceded valid team context'", ClientId, Generation, TeamId);
					ImpactDebugLog("START_CROSSING server_tick=%lld client_id=%d generation=%u team_id=%d started=1 eligible=1 start_tile=%d crossing=deferred first_observation=1 reason='first start observation preceded valid team context'",
						static_cast<long long>(DeferredStartTick), ClientId, Generation, TeamId, OnStartTile ? 1 : 0);
					m_aStartTrackingGeneration[ClientId] = Generation;
					m_aStartTrackingTeam[ClientId] = TeamId;
					m_aStartOnTile[ClientId] = OnStartTile;
					m_aStartLastCharacterTick[ClientId] = Character.m_Cur.m_Tick;
					m_aDeferredStartGeneration[ClientId] = 0;
					m_aDeferredStartTeam[ClientId] = 0;
					m_aDeferredStartObservation[ClientId] = false;
					m_aDeferredStartTick[ClientId] = -1;
					SImpactEvent Event;
					Event.m_EventId = m_NextEventId++;
					Event.m_Type = EImpactEventType::START_CROSSING;
					Event.m_Actor = Participant;
					Event.m_TeamId = TeamId;
					Event.m_ActorName = GameClient()->m_aClients[ClientId].m_aName;
					Event.m_StartTick = DeferredStartTick;
					Event.m_ResolveTick = DeferredStartTick;
					Event.m_Confirmed = true;
					StartBatch.m_vCandidates.push_back(std::move(Event));
					continue;
				}
				// Do not consume the first observation before the authoritative
				// DDNet team state is available. A remote player can be present on
				// the start tile in the character snapshot one tick before
				// SV_TEAMSSTATE assigns the non-zero team. Keeping the generation
				// untracked lets that same current start condition register once
				// the ordinary team metadata becomes valid.
				if(!StartTeamContextConfirmed)
				{
					LogStartObservation(StartSignal ? "deferred" : "not_start");
					if(StartSignal && g_Config.m_Debug)
						log_debug("amf-impact", "start observation deferred client=%d generation=%u team=%d reason='team context not yet valid'", ClientId, Generation, TeamId);
					if(StartSignal)
						ImpactDebugLog("START_DEFERRED server_tick=%lld client_id=%d generation=%u team_id=%d started=0 eligible=0 start_tile=%d reason='team context not yet valid'",
							static_cast<long long>(StartBatch.m_ServerTick), ClientId, Generation, TeamId, OnStartTile ? 1 : 0);
					if(StartSignal)
					{
						m_aDeferredStartGeneration[ClientId] = Generation;
						m_aDeferredStartTeam[ClientId] = TeamId;
						m_aDeferredStartObservation[ClientId] = true;
						m_aDeferredStartTick[ClientId] = StartBatch.m_ServerTick;
					}
					continue;
				}
				// A first reliable snapshot on the authoritative start condition is a
				// valid crossing. There is no previous object to use for a delta yet.
				m_aStartTrackingGeneration[ClientId] = Generation;
				m_aStartTrackingTeam[ClientId] = TeamId;
				m_aDeferredStartGeneration[ClientId] = 0;
				m_aDeferredStartTeam[ClientId] = 0;
				m_aDeferredStartObservation[ClientId] = false;
				m_aDeferredStartTick[ClientId] = -1;
				m_aStartOnTile[ClientId] = OnStartTile;
				m_aStartLastCharacterTick[ClientId] = Character.m_Cur.m_Tick;
				if(IsStartObservationConfirmed(FirstReliableObservation, PreviousCharacterKnown, OnStartTile, CrossedStart, AlreadyStarted) && HasValidTeam)
				{
					LogStartObservation("accepted");
					if(g_Config.m_Debug)
						log_debug("amf-impact", "start candidate accepted client=%d generation=%u team=%d team_member=1 start_tile=%d started=0 eligible=1 first_current=%d crossed=%d", ClientId, Generation, TeamId, OnStartTile ? 1 : 0, RawPrevious.m_FromRawPreviousSnapshot ? 0 : 1, CrossedStart ? 1 : 0);
					ImpactDebugLog("START_CROSSING server_tick=%lld client_id=%d generation=%u team_id=%d started=1 eligible=1 start_tile=%d crossing=%d first_current=%d reason='authoritative start observation'",
						static_cast<long long>(StartBatch.m_ServerTick), ClientId, Generation, TeamId, OnStartTile ? 1 : 0, CrossedStart ? 1 : 0, RawPrevious.m_FromRawPreviousSnapshot ? 0 : 1);
					SImpactEvent Event;
					Event.m_EventId = m_NextEventId++;
					Event.m_Type = EImpactEventType::START_CROSSING;
					Event.m_Actor = Participant;
					Event.m_TeamId = TeamId;
					Event.m_ActorName = GameClient()->m_aClients[ClientId].m_aName;
					Event.m_StartTick = StartBatch.m_ServerTick;
					Event.m_ResolveTick = StartBatch.m_ServerTick;
					Event.m_Confirmed = true;
					StartBatch.m_vCandidates.push_back(std::move(Event));
				}
				else if(StartSignal && g_Config.m_Debug)
				{
					LogStartObservation(AlreadyStarted ? "already_started" : "rejected_team");
					log_debug("amf-impact", "start candidate rejected client=%d generation=%u team=%d start_tile=%d started=%d reason='%s'", ClientId, Generation, TeamId,
						OnStartTile ? 1 : 0, AlreadyStarted ? 1 : 0, AlreadyStarted ? "already started" : "invalid or different team");
				}
				else if(StartSignal)
				{
					LogStartObservation(AlreadyStarted ? "already_started" : "rejected_team");
					ImpactDebugLog("START_REJECTED server_tick=%lld client_id=%d generation=%u team_id=%d started=%d eligible=0 start_tile=%d reason='%s'",
						static_cast<long long>(StartBatch.m_ServerTick), ClientId, Generation, TeamId, AlreadyStarted ? 1 : 0, OnStartTile ? 1 : 0,
						AlreadyStarted ? "already started" : "invalid or different team");
				}
				else
					LogStartObservation("not_start");
				continue;
			}
			// A generation may already have a movement baseline when the current
			// snapshot exposes a real Start crossing, but the roster/team snapshot can
			// still be incomplete. Keep that evidence instead of taking the rejecting
			// branch below. It is consumed only after the same team has a confirmed
			// two-member roster.
			if(StartSignal && !StartTeamContextConfirmed)
			{
				LogStartObservation("deferred");
				if(g_Config.m_Debug)
					log_debug("amf-impact", "start observation deferred client=%d generation=%u team=%d reason='team context not yet valid after movement baseline'", ClientId, Generation, TeamId);
				ImpactDebugLog("START_DEFERRED server_tick=%lld client_id=%d generation=%u team_id=%d started=0 eligible=0 start_tile=%d reason='team context not yet valid after movement baseline'",
					static_cast<long long>(StartBatch.m_ServerTick), ClientId, Generation, TeamId, OnStartTile ? 1 : 0);
				m_aDeferredStartGeneration[ClientId] = Generation;
				m_aDeferredStartTeam[ClientId] = TeamId;
				m_aDeferredStartObservation[ClientId] = true;
				if(m_aDeferredStartTick[ClientId] < 0)
					m_aDeferredStartTick[ClientId] = StartBatch.m_ServerTick;
				m_aStartOnTile[ClientId] = OnStartTile;
				continue;
			}
			if(DeferredStartObservation && AlreadyStarted)
			{
				m_aDeferredStartGeneration[ClientId] = 0;
				m_aDeferredStartTeam[ClientId] = 0;
				m_aDeferredStartObservation[ClientId] = false;
				m_aDeferredStartTick[ClientId] = -1;
				LogStartObservation("already_started");
			}
			if(DeferredStartCandidate)
			{
				const int64_t DeferredStartTick = m_aDeferredStartTick[ClientId] >= 0 ? m_aDeferredStartTick[ClientId] : StartBatch.m_ServerTick;
				LogStartObservation("accepted_deferred");
				if(g_Config.m_Debug)
					log_debug("amf-impact", "deferred start candidate accepted client=%d generation=%u team=%d team_member=1 start_tile=%d started=0 eligible=1 reason='first start observation preceded valid team context'", ClientId, Generation, TeamId, OnStartTile ? 1 : 0);
				ImpactDebugLog("START_CROSSING server_tick=%lld client_id=%d generation=%u team_id=%d started=1 eligible=1 start_tile=%d crossing=deferred first_observation=0 reason='first start observation preceded valid team context'",
					static_cast<long long>(DeferredStartTick), ClientId, Generation, TeamId, OnStartTile ? 1 : 0);
				m_aStartTrackingTeam[ClientId] = TeamId;
				m_aStartOnTile[ClientId] = OnStartTile;
				m_aStartLastCharacterTick[ClientId] = Character.m_Cur.m_Tick;
				m_aDeferredStartGeneration[ClientId] = 0;
				m_aDeferredStartTeam[ClientId] = 0;
				m_aDeferredStartObservation[ClientId] = false;
				m_aDeferredStartTick[ClientId] = -1;
				SImpactEvent Event;
				Event.m_EventId = m_NextEventId++;
				Event.m_Type = EImpactEventType::START_CROSSING;
				Event.m_Actor = Participant;
				Event.m_TeamId = TeamId;
				Event.m_ActorName = GameClient()->m_aClients[ClientId].m_aName;
				Event.m_StartTick = DeferredStartTick;
				Event.m_ResolveTick = DeferredStartTick;
				Event.m_Confirmed = true;
				StartBatch.m_vCandidates.push_back(std::move(Event));
				continue;
			}
			if(m_aStartLastCharacterTick[ClientId] == Character.m_Cur.m_Tick && !StartCandidate)
			{
				LogStartObservation(AlreadyStarted ? "already_started" : "not_start");
				continue;
			}
			m_aStartLastCharacterTick[ClientId] = Character.m_Cur.m_Tick;

			if(StartCandidate)
			{
				LogStartObservation("accepted");
				if(g_Config.m_Debug)
					log_debug("amf-impact", "start candidate accepted client=%d generation=%u team=%d team_member=1 start_tile=%d started=0 eligible=1 crossed=%d current_character=%d previous_character=%d", ClientId, Generation, TeamId, OnStartTile ? 1 : 0, CrossedStart ? 1 : 0, RawCurrent.m_FromRawCurrentSnapshot ? 1 : 0, RawPrevious.m_FromRawPreviousSnapshot ? 1 : 0);
				ImpactDebugLog("START_CROSSING server_tick=%lld client_id=%d generation=%u team_id=%d started=1 eligible=1 start_tile=%d crossing=%d current_character=%d previous_character=%d reason='start transition'",
					static_cast<long long>(StartBatch.m_ServerTick), ClientId, Generation, TeamId, OnStartTile ? 1 : 0, CrossedStart ? 1 : 0,
					RawCurrent.m_FromRawCurrentSnapshot ? 1 : 0, RawPrevious.m_FromRawPreviousSnapshot ? 1 : 0);
				SImpactEvent Event;
				Event.m_EventId = m_NextEventId++;
				Event.m_Type = EImpactEventType::START_CROSSING;
				Event.m_Actor = Participant;
				Event.m_TeamId = TeamId;
				Event.m_ActorName = GameClient()->m_aClients[ClientId].m_aName;
				Event.m_StartTick = StartBatch.m_ServerTick;
				Event.m_ResolveTick = StartBatch.m_ServerTick;
				Event.m_Confirmed = true;
				StartBatch.m_vCandidates.push_back(std::move(Event));
			}
			else if(StartSignal && g_Config.m_Debug)
			{
				LogStartObservation(AlreadyStarted ? "already_started" : "rejected_team");
				log_debug("amf-impact", "start candidate rejected client=%d generation=%u team=%d reason='%s'", ClientId, Generation, TeamId,
					AlreadyStarted ? "already started" : !IsValidImpactTeam(TeamId) ? "invalid team" : "different run team");
			}
			else if(StartSignal)
			{
				LogStartObservation(AlreadyStarted ? "already_started" : "rejected_team");
				ImpactDebugLog("START_REJECTED server_tick=%lld client_id=%d generation=%u team_id=%d started=%d eligible=0 start_tile=%d reason='%s'",
					static_cast<long long>(StartBatch.m_ServerTick), ClientId, Generation, TeamId, AlreadyStarted ? 1 : 0, OnStartTile ? 1 : 0,
					AlreadyStarted ? "already started" : !IsValidImpactTeam(TeamId) ? "invalid team" : "different run team");
			}
			else
				LogStartObservation("not_start");
			m_aStartOnTile[ClientId] = OnStartTile;
		}

		if(!StartBatch.m_vCandidates.empty())
			SubmitTickEventBatch(StartBatch);
	}

	void CImpactSystem::UpdateConfirmedParticipants()
	{
		if(!m_HasCurrentRun)
		{
			m_aSlotDisconnectConfirmed.fill(false);
			return;
		}
		if(m_CurrentRun.StartedParticipantCount() > 0)
			ReconcileCurrentTeamRoster(CurrentTick());
		for(const SParticipant &Participant : m_CurrentRun.Participants())
			if(!Participant.m_Finalized)
				CaptureParticipantCosmetics(Participant.m_Id);
		STickEventBatch PresenceBatch;
		PresenceBatch.m_ServerTick = CurrentTick();
		for(const SParticipant &Participant : m_CurrentRun.Participants())
		{
			if(Participant.m_Finalized)
				continue;
			const int ClientId = Participant.m_Id.m_ClientId;
			if(ClientId >= 0 && ClientId < MAX_CLIENTS &&
				(m_aSlotDisconnectConfirmed[ClientId] ||
					(m_aSlotActive[ClientId] && m_aSlotGeneration[ClientId] != Participant.m_Id.m_SlotGeneration)))
			{
				FinalizeParticipant(Participant.m_Id, PresenceBatch.m_ServerTick, false);
				continue;
			}
			bool Present = false;
			const char *pName = nullptr;
			if(ClientId >= 0 && ClientId < MAX_CLIENTS && m_aSlotActive[ClientId] && m_aSlotGeneration[ClientId] == Participant.m_Id.m_SlotGeneration)
			{
				Present = !IsImpactSpectator(GameClient(), ClientId) &&
					IsValidImpactTeam(m_CurrentRun.Context().m_TeamId) &&
					GameClient()->m_Teams.Team(ClientId) == m_CurrentRun.Context().m_TeamId;
				if(GameClient()->m_Snap.m_apPlayerInfos[ClientId] != nullptr)
					pName = GameClient()->m_aClients[ClientId].m_aName;
			}
			if(pName != nullptr)
				m_CurrentRun.UpdateParticipantName(Participant.m_Id, pName);
			if(Present != Participant.m_Present)
			{
				ImpactDebugLog("PARTICIPANT_PRESENCE server_tick=%lld client_id=%d generation=%u team_id=%d present=%d started=1 eligible=%d reason='confirmed team membership/presence transition'",
					static_cast<long long>(PresenceBatch.m_ServerTick), Participant.m_Id.m_ClientId, Participant.m_Id.m_SlotGeneration,
					m_CurrentRun.Context().m_TeamId, Present ? 1 : 0, Present ? 1 : 0);
				SImpactEvent Event;
				Event.m_EventId = m_NextEventId++;
				Event.m_Type = EImpactEventType::PARTICIPANT_PRESENCE;
				Event.m_Actor = Participant.m_Id;
				Event.m_Confirmed = true;
				Event.m_Present = Present;
				Event.m_StartTick = PresenceBatch.m_ServerTick;
				Event.m_ResolveTick = PresenceBatch.m_ServerTick;
				PresenceBatch.m_vCandidates.push_back(std::move(Event));
			}
		}
		if(!PresenceBatch.m_vCandidates.empty())
			SubmitTickEventBatch(PresenceBatch);
		m_aSlotDisconnectConfirmed.fill(false);
	}

	void CImpactSystem::CaptureParticipantCosmetics(const SParticipantId &Participant)
	{
		if(!m_HasCurrentRun || m_CurrentRun.IsFinalized() || !Participant.IsValid())
			return;
		const int ClientId = Participant.m_ClientId;
		if(ClientId < 0 || ClientId >= MAX_CLIENTS || !m_aSlotActive[ClientId] ||
			m_aSlotGeneration[ClientId] != Participant.m_SlotGeneration ||
			GameClient()->m_Snap.m_apPlayerInfos[ClientId] == nullptr ||
			IsImpactSpectator(GameClient(), ClientId) ||
			GameClient()->m_Teams.Team(ClientId) != m_CurrentRun.Context().m_TeamId)
			return;

		const CGameClient::CClientData &ClientData = GameClient()->m_aClients[ClientId];
		if(!CSkin::IsValidName(ClientData.m_aSkinName))
			return;
		SParticipantCosmeticSnapshot Cosmetics;
		Cosmetics.m_Available = true;
		Cosmetics.m_SkinName = ClientData.m_aSkinName;
		Cosmetics.m_UseCustomColor = ClientData.m_UseCustomColor != 0;
		Cosmetics.m_ColorBody = ClientData.m_ColorBody;
		Cosmetics.m_ColorFeet = ClientData.m_ColorFeet;
		auto It = std::find_if(m_vRunParticipantCosmetics.begin(), m_vRunParticipantCosmetics.end(), [&Participant](const SRunParticipantCosmetic &Entry) {
			return Entry.m_Id == Participant;
		});
		if(It == m_vRunParticipantCosmetics.end())
			m_vRunParticipantCosmetics.push_back({Participant, std::move(Cosmetics)});
		else
			It->m_Cosmetics = std::move(Cosmetics);
	}

	void CImpactSystem::CopyParticipantCosmeticsToSnapshot(SRunSnapshot *pSnapshot) const
	{
		if(pSnapshot == nullptr)
			return;
		for(SParticipant &Participant : pSnapshot->m_vParticipants)
		{
			const auto It = std::find_if(m_vRunParticipantCosmetics.begin(), m_vRunParticipantCosmetics.end(), [&Participant](const SRunParticipantCosmetic &Entry) {
				return Entry.m_Id == Participant.m_Id;
			});
			if(It != m_vRunParticipantCosmetics.end())
				Participant.m_Cosmetics = It->m_Cosmetics;
		}
	}

	void CImpactSystem::RestoreParticipantCosmeticsFromSnapshot(const SRunSnapshot &SnapshotData)
	{
		m_vRunParticipantCosmetics.clear();
		for(const SParticipant &Participant : SnapshotData.m_vParticipants)
			if(Participant.m_Cosmetics.m_Available)
				m_vRunParticipantCosmetics.push_back({Participant.m_Id, Participant.m_Cosmetics});
	}

	void CImpactSystem::FinalizeParticipant(const SParticipantId &Participant, int64_t Tick, bool IsReset, uint64_t ResetMessageId, int ResetTeamId)
	{
		if(!m_HasCurrentRun || !m_CurrentRun.HasParticipant(Participant) || m_CurrentRun.IsParticipantFinalized(Participant))
			return;
		CaptureParticipantCosmetics(Participant);
		SImpactEvent Event;
		Event.m_Type = EImpactEventType::PARTICIPANT_FINALIZED;
		Event.m_Actor = Participant;
		Event.m_TeamId = m_CurrentRun.Context().m_TeamId;
		Event.m_StartTick = Tick;
		Event.m_ResolveTick = Tick;
		Event.m_Confirmed = true;
		Event.m_CausalReason = IsReset ? "confirmed participant self-reset" : "participant absent from authoritative server roster";
		STickEventBatch Batch;
		Batch.m_ServerTick = Tick;
		Batch.m_vCandidates.push_back(std::move(Event));
		if(!SubmitTickEventBatch(Batch) || !m_CurrentRun.IsParticipantFinalized(Participant))
			return;

		// Reuse Phase2's existing participant-boundary path so pending attribution
		// and old observed character context cannot cross this finalized identity.
		SImpactGameplayMessage Disconnect;
		Disconnect.m_MessageId = m_NextGameplayMessageId++;
		Disconnect.m_Type = EImpactGameplayMessageType::DISCONNECT;
		Disconnect.m_ServerTick = Tick;
		Disconnect.m_Confirmed = true;
		Disconnect.m_ClientId = Participant.m_ClientId;
		Disconnect.m_Target = Participant;
		if(m_vGameplayMessages.size() >= 64)
			m_vGameplayMessages.erase(m_vGameplayMessages.begin());
		m_vGameplayMessages.push_back(std::move(Disconnect));

		if(IsReset && Participant.m_ClientId >= 0 && Participant.m_ClientId < MAX_CLIENTS &&
			m_aSlotGeneration[Participant.m_ClientId] == Participant.m_SlotGeneration)
		{
			m_aPendingResetGenerationAdvance[Participant.m_ClientId] = true;
			m_aResetSawCharacterAbsence[Participant.m_ClientId] =
				!RawCurrentObservation(GameClient(), Participant.m_ClientId).m_FromRawCurrentSnapshot;

			// Every client freezes the same participant in the shared run. Only the
			// resetter's own client needs a separate save decision for its final view.
			const bool LocalResetter = Participant.m_ClientId == GameClient()->m_aLocalIds[0] ||
				Participant.m_ClientId == GameClient()->m_aLocalIds[1];
			if(LocalResetter)
			{
				SRunSnapshot ResetSnapshot;
				const uint64_t SnapshotRunId = m_NextRunId;
				if(m_CurrentRun.MakeParticipantResetSnapshot(Participant, SnapshotRunId, Tick, time_timestamp(), &ResetSnapshot))
				{
					CopyParticipantCosmeticsToSnapshot(&ResetSnapshot);
					++m_NextRunId;
					FinalizeRunSnapshot(ResetSnapshot, false);
					m_LocalResetSnapshot.m_RunId = SnapshotRunId;
					m_LocalResetSnapshot.m_SourceRunId = m_CurrentRun.RunId();
					m_LocalResetSnapshot.m_Participant = Participant;
					m_LocalResetSnapshot.m_TeamId = ResetTeamId;
					m_LocalResetSnapshot.m_ServerTick = Tick;
					m_LocalResetSnapshot.m_ResetMessageId = ResetMessageId;
					// The cut snapshot has its own immutable archive identity; persist
					// the advanced allocator while retaining the live team's checkpoint.
					SaveActiveCheckpoint(true);
				}
			}
		}
	}

	void CImpactSystem::EnsureRun(int TeamId)
	{
		if(m_HasCurrentRun || !m_ServerContextKnown || !IsValidImpactTeam(TeamId))
			return;
		m_ServerContext.m_TeamId = TeamId;
		m_CurrentRun.Initialize(m_NextRunId++, m_ServerContext);
		m_Phase2.Reset();
		m_Phase2.SetRunContext(m_CurrentRun.RunId(), TeamId);
		m_HasCurrentRun = true;
		if(g_Config.m_Debug)
			ImpactDebugLog("KOG_DIFFICULTY_ACTIVE_RUN event=CREATE run_id=%llu server_key=\"%s\" map=\"%s\" previous_difficulty=\"\" candidate_difficulty=\"%s\" new_difficulty=\"%s\" rating=%d source=SERVER_CONTEXT status=ACCEPTED reason=RUN_INITIALIZED",
				static_cast<unsigned long long>(m_CurrentRun.RunId()), m_CurrentRun.Context().m_ServerKey.c_str(), m_CurrentRun.Context().m_Map.c_str(),
				m_ServerContext.m_MapDifficulty.c_str(), m_CurrentRun.Context().m_MapDifficulty.c_str(), m_CurrentRun.Context().m_MapStars);
	}

	void CImpactSystem::ReconcileCurrentTeamRoster(int64_t Tick, const STickEventBatch *pStartBatch)
	{
		if(!m_HasCurrentRun || m_CurrentRun.IsFinalized() || !IsValidImpactTeam(m_CurrentRun.Context().m_TeamId))
			return;
		const int TeamId = m_CurrentRun.Context().m_TeamId;
		for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
		{
			if(!IsCurrentTeamRosterEntry(m_aSlotActive[ClientId], GameClient()->m_Snap.m_apPlayerInfos[ClientId] != nullptr,
				IsImpactSpectator(GameClient(), ClientId), GameClient()->m_Teams.Team(ClientId), TeamId, m_aSlotGeneration[ClientId]))
				continue;
			const bool IsDummy = ClientId == GameClient()->m_aLocalIds[1] && ClientId != GameClient()->m_aLocalIds[0];
			const SParticipantId Participant{ClientId, m_aSlotGeneration[ClientId], IsDummy};
			m_CurrentRun.AddRosterMember(Participant, GameClient()->m_aClients[ClientId].m_aName, Tick,
				m_CurrentRun.StartedParticipantCount() == 0 ? "current exact-team roster at first canonical START" : "late exact-team roster discovery");
		}
		if(pStartBatch == nullptr)
			return;
		for(const SImpactEvent &Event : pStartBatch->m_vCandidates)
		{
			if(!Event.m_Confirmed || Event.m_Type != EImpactEventType::START_CROSSING || Event.m_TeamId != TeamId || !Event.m_Actor.IsValid())
				continue;
			m_CurrentRun.AddRosterMember(Event.m_Actor, Event.m_ActorName.c_str(), Tick, "canonical START actor in current-run batch");
		}
	}

	int CImpactSystem::CurrentLocalTeamId() const
	{
		const auto LocalTeamForClient = [this](int ClientId) {
			if(ClientId < 0 || ClientId >= MAX_CLIENTS)
				return 0;
			// PlayerInfo/team state can outlive a disconnected or replaced local
			// character for a sparse snapshot. A current Character is the required
			// local presence signal; remote participant continuity is handled by
			// UpdateSlotGenerations.
			return LocalParticipationTeamForSlot(GameClient()->m_Teams.Team(ClientId), m_aSlotActive[ClientId],
				IsImpactSpectator(GameClient(), ClientId), RawCurrentObservation(GameClient(), ClientId).m_FromRawCurrentSnapshot);
		};
		return SelectLocalParticipationTeam(LocalTeamForClient(GameClient()->m_aLocalIds[0]), LocalTeamForClient(GameClient()->m_aLocalIds[1]));
	}

	int CImpactSystem::CountTeamMembers(int TeamId) const
	{
		if(!IsValidImpactTeam(TeamId))
			return 0;
		int Count = 0;
		for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
		{
			if(m_aSlotActive[ClientId] && RawCurrentObservation(GameClient(), ClientId).m_FromRawCurrentSnapshot &&
				!IsImpactSpectator(GameClient(), ClientId) && GameClient()->m_Teams.Team(ClientId) == TeamId)
				++Count;
		}
		return Count;
	}

	int CImpactSystem::CountStartRosterMembers(int TeamId) const
	{
		if(!IsValidImpactTeam(TeamId))
			return 0;
		int Count = 0;
		for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
		{
			// PlayerInfo and DDNet team state are not camera-clipped with the
			// character snapshot. They prove roster membership, but do not prove a
			// participant Start; that still requires either a real crossing or the
			// server-authored sixup RaceStartTick checked above.
			if(m_aSlotActive[ClientId] && GameClient()->m_Snap.m_apPlayerInfos[ClientId] != nullptr &&
				!IsImpactSpectator(GameClient(), ClientId) && GameClient()->m_Teams.Team(ClientId) == TeamId)
				++Count;
		}
		return Count;
	}

	const CImpactRun *CImpactSystem::LocalViewRun() const
	{
		if(!m_HasCurrentRun)
			return nullptr;
		const int LocalClientId = GameClient()->m_aLocalIds[g_Config.m_ClDummy ? 1 : 0];
		return IsLocalRunViewFinalized(m_CurrentRun, LocalClientId) ? nullptr : &m_CurrentRun;
	}

	const SRunSnapshot *CImpactSystem::LocalResetSnapshot() const
	{
		if(m_LocalResetSnapshot.m_RunId == 0)
			return nullptr;
		if(const SRunSnapshot *pPending = PendingSaveRun(m_LocalResetSnapshot.m_RunId))
			return pPending;
		for(const SRunSnapshot &SnapshotData : m_vHistory)
			if(SnapshotData.m_RunId == m_LocalResetSnapshot.m_RunId)
				return &SnapshotData;
		return nullptr;
	}

	int CImpactSystem::TeamMemberCount() const
	{
		const int TeamId = m_HasCurrentRun ? m_CurrentRun.Context().m_TeamId : CurrentLocalTeamId();
		return CountTeamMembers(TeamId);
	}

	int CImpactSystem::StartedParticipantCount() const
	{
		return m_HasCurrentRun ? m_CurrentRun.StartedParticipantCount() : 0;
	}

	int CImpactSystem::EngagedParticipantCount() const
	{
		return m_HasCurrentRun ? m_CurrentRun.EngagedParticipantCount() : 0;
	}

	int CImpactSystem::EligibleParticipantCount() const
	{
		return m_HasCurrentRun ? m_CurrentRun.EligibleParticipantCount() : 0;
	}

	void CImpactSystem::DebugLogUiFallValue(const SParticipantId &Participant, int Falls)
	{
		if(!g_Config.m_Debug || !Participant.IsValid() || Participant.m_ClientId < 0 || Participant.m_ClientId >= MAX_CLIENTS)
			return;
		const int ClientId = Participant.m_ClientId;
		if(m_aLastDebugUiFallGeneration[ClientId] == Participant.m_SlotGeneration && m_aLastDebugUiFalls[ClientId] == Falls)
			return;
		m_aLastDebugUiFallGeneration[ClientId] = Participant.m_SlotGeneration;
		m_aLastDebugUiFalls[ClientId] = Falls;
		ImpactDebugLog("UI_FALL_VALUE server_tick=%lld client_id=%d generation=%u falls=%d", static_cast<long long>(CurrentTick()), ClientId, Participant.m_SlotGeneration, Falls);
	}

	bool CImpactSystem::CanCollect() const
	{
		return m_EngineActive && m_ServerContextKnown;
	}

	bool CImpactSystem::HasConfirmedStart(const STickEventBatch &Batch) const
	{
		for(const SImpactEvent &Event : Batch.m_vCandidates)
			if(Event.m_Confirmed && Event.m_Type == EImpactEventType::START_CROSSING)
				return true;
		return false;
	}

	bool CImpactSystem::HasStartForCurrentParticipant(const STickEventBatch &Batch) const
	{
		if(!m_HasCurrentRun)
			return false;
		for(const SImpactEvent &Event : Batch.m_vCandidates)
			if(Event.m_Confirmed && Event.m_Type == EImpactEventType::START_CROSSING && m_CurrentRun.HasStartedParticipant(Event.m_Actor))
				return true;
		return false;
	}

	int CImpactSystem::PendingSaveSecondsRemaining() const
	{
		if(m_vPendingSaveRuns.empty())
			return 0;
		return PendingSaveSecondsRemaining(m_vPendingSaveRuns.front().m_Snapshot.m_RunId);
	}

	std::vector<uint64_t> CImpactSystem::PendingSaveRunIds()
	{
		ExpirePendingSaveRuns();
		std::vector<uint64_t> vRunIds;
		vRunIds.reserve(m_vPendingSaveRuns.size());
		for(const SPendingSaveRun &Pending : m_vPendingSaveRuns)
			vRunIds.push_back(Pending.m_Snapshot.m_RunId);
		return vRunIds;
}

	const SRunSnapshot *CImpactSystem::PendingSaveRun(uint64_t RunId) const
	{
		if(RunId == 0)
			return nullptr;
		const auto It = std::find_if(m_vPendingSaveRuns.begin(), m_vPendingSaveRuns.end(), [RunId](const SPendingSaveRun &Pending) {
			return Pending.m_Snapshot.m_RunId == RunId;
		});
		return It == m_vPendingSaveRuns.end() ? nullptr : &It->m_Snapshot;
}

	int CImpactSystem::PendingSaveSecondsRemaining(uint64_t RunId) const
	{
		const auto It = std::find_if(m_vPendingSaveRuns.begin(), m_vPendingSaveRuns.end(), [RunId](const SPendingSaveRun &Pending) {
			return Pending.m_Snapshot.m_RunId == RunId;
		});
		if(It == m_vPendingSaveRuns.end())
			return 0;
		const int64_t Remaining = It->m_Deadline - time_get();
		if(Remaining <= 0)
			return 0;
		const int64_t Frequency = std::max<int64_t>(1, time_freq());
		return static_cast<int>((Remaining + Frequency - 1) / Frequency);
	}

	bool CImpactSystem::HistoryContainsRun(uint64_t RunId) const
	{
		return RunId != 0 && std::any_of(m_vHistory.begin(), m_vHistory.end(), [RunId](const SRunSnapshot &SnapshotData) {
			return SnapshotData.m_RunId == RunId;
		});
	}

	bool CImpactSystem::AppendHistoryRun(const SRunSnapshot &SnapshotData)
	{
		if(!m_HistoryLoadSucceeded || SnapshotData.m_RunId == 0)
			return false;
		if(HistoryContainsRun(SnapshotData.m_RunId))
			return true;
		m_vHistory.push_back(SnapshotData);
		LogDifficultyResult("HISTORY_APPEND", SnapshotData, "FINALIZED_SNAPSHOT", "ACCEPTED", "HISTORY_ENTRY_CREATED");
		if(CPersistence::SaveHistory(Storage(), m_vHistory, m_HistoryLoadSucceeded))
			return true;
		m_vHistory.pop_back();
		log_warn("amf-impact", "failed to persist finalized run %llu", static_cast<unsigned long long>(SnapshotData.m_RunId));
		return false;
	}

	void CImpactSystem::QueuePendingSaveRun(const SRunSnapshot &SnapshotData)
	{
		if(SnapshotData.m_RunId == 0 || HistoryContainsRun(SnapshotData.m_RunId))
			return;
		for(const SPendingSaveRun &Pending : m_vPendingSaveRuns)
			if(Pending.m_Snapshot.m_RunId == SnapshotData.m_RunId)
				return;
		const int64_t Frequency = std::max<int64_t>(1, time_freq());
		SPendingSaveRun Pending;
		Pending.m_Snapshot = SnapshotData;
		Pending.m_Deadline = time_get() + static_cast<int64_t>(SHORT_RUN_DECISION_SECONDS) * Frequency;
		m_vPendingSaveRuns.push_back(std::move(Pending));
		LogDifficultyResult("PENDING_SAVE_COPY", m_vPendingSaveRuns.back().m_Snapshot, "FINALIZED_SNAPSHOT", "ACCEPTED", "PENDING_ENTRY_CREATED");
		ImpactDebugLog("RUN_SAVE_PENDING run_id=%llu result=%s active_ticks=%lld deadline=%lld queue_size=%zu",
			static_cast<unsigned long long>(SnapshotData.m_RunId), RunResultName(SnapshotData.m_Result),
			static_cast<long long>(SnapshotData.m_ActiveAnalysisTicks), static_cast<long long>(m_vPendingSaveRuns.back().m_Deadline),
			m_vPendingSaveRuns.size());
	}

	void CImpactSystem::ExpirePendingSaveRuns()
	{
		const int64_t Now = time_get();
		bool ExpiredAny = false;
		for(auto It = m_vPendingSaveRuns.begin(); It != m_vPendingSaveRuns.end();)
		{
			if(It->m_Deadline > Now)
			{
				++It;
				continue;
			}
			ImpactDebugLog("RUN_SAVE_DISCARDED run_id=%llu reason='decision timeout' queue_size=%zu",
				static_cast<unsigned long long>(It->m_Snapshot.m_RunId), m_vPendingSaveRuns.size() - 1);
			ExpiredAny = true;
			It = m_vPendingSaveRuns.erase(It);
		}
		// New pending decisions are in memory by design. Only an old legacy
		// finalized checkpoint can remain in ACTIVE_FILE, and it is safe to
		// remove it once no current run is using that file.
		if(ShouldRemoveExpiredActiveCheckpoint(ExpiredAny, m_HasCurrentRun, m_vPendingSaveRuns.size()))
			RemoveActiveCheckpoint();
	}

	void CImpactSystem::FinalizeRunSnapshot(const SRunSnapshot &SnapshotData, bool RemoveActiveFile)
	{
		LogDifficultyResult("FINALIZE_INPUT", SnapshotData, "ACTIVE_RUN", "ACCEPTED", "FINALIZED_SNAPSHOT");
		// This only controls the presentation notice; the save decision below remains authoritative.
		const bool ShowCompletionToast = IsStatisticsUiAccessible(g_Config.m_AmfImpactStatisticsAccess) &&
			(SnapshotData.m_Result == ERunResult::FINISHED || IsResetRelatedToastResult(SnapshotData.m_Result));
		const auto PlayResetToastSound = [&]() {
			if(!m_RunCompletionToast.ConsumeResetSound(g_Config.m_AmfImpactStatisticsAccess))
				return;
			GameClient()->m_Sounds.Play(CSounds::CHN_GLOBAL, SOUND_AMF_RUN_RESET, 0.70f);
			log_info("amf-impact", "RESET_TOAST_SOUND run=%llu set=%d channel=%d voice_started=%d",
				static_cast<unsigned long long>(SnapshotData.m_RunId), SOUND_AMF_RUN_RESET, CSounds::CHN_GLOBAL,
				GameClient()->m_Sounds.IsPlaying(SOUND_AMF_RUN_RESET) ? 1 : 0);
		};
		if(ShouldAutoSaveFinalizedRun(SnapshotData.m_Result, SnapshotData.m_ActiveAnalysisTicks, SnapshotData.m_Context.m_TickSpeed))
		{
			// A failed atomic write leaves the finalized checkpoint in place so a
			// later client start can retry it under the same stable RunId.
			if(AppendHistoryRun(SnapshotData))
			{
				if(RemoveActiveFile)
					RemoveActiveCheckpoint();
				if(ShowCompletionToast)
				{
					if(m_RunCompletionToast.m_RunId != SnapshotData.m_RunId)
						m_RunCompletionToast.m_ResetSoundPlayed = false;
					m_RunCompletionToast.m_Active = true;
					m_RunCompletionToast.m_Result = SnapshotData.m_Result;
					m_RunCompletionToast.m_RunId = SnapshotData.m_RunId;
					m_RunCompletionToast.m_PendingSave = false;
					m_RunCompletionToast.m_Dismissing = false;
					m_RunCompletionToast.m_StartTime = time_get();
					PlayResetToastSound();
				}
			}
			return;
		}

		// The immutable snapshot is queued before CurrentRun is cleared. The
		// pending queue is intentionally memory-only until the user chooses Save;
		// this keeps the unspecified client-restart behavior out of persistence.
		const bool WasPending = PendingSaveRun(SnapshotData.m_RunId) != nullptr;
		QueuePendingSaveRun(SnapshotData);
		if(ShowCompletionToast && !WasPending && PendingSaveRun(SnapshotData.m_RunId) != nullptr)
		{
			if(m_RunCompletionToast.m_RunId != SnapshotData.m_RunId)
				m_RunCompletionToast.m_ResetSoundPlayed = false;
			m_RunCompletionToast.m_Active = true;
			m_RunCompletionToast.m_Result = SnapshotData.m_Result;
			m_RunCompletionToast.m_RunId = SnapshotData.m_RunId;
			m_RunCompletionToast.m_PendingSave = true;
			m_RunCompletionToast.m_Dismissing = false;
			m_RunCompletionToast.m_StartTime = time_get();
			PlayResetToastSound();
		}
		if(RemoveActiveFile)
			RemoveActiveCheckpoint();
	}

	void CImpactSystem::HandleFinalizedRun()
	{
		if(!m_HasCurrentRun || !m_CurrentRun.IsFinalized())
			return;
		SRunSnapshot SnapshotData = m_CurrentRun.Snapshot(CurrentTick(), time_timestamp());
		CopyParticipantCosmeticsToSnapshot(&SnapshotData);
		FinalizeRunSnapshot(SnapshotData);
		RemoveCurrentRun();
	}

	void CImpactSystem::HandleInterruptedRun(ERunResult Result)
	{
		if(!m_HasCurrentRun || m_CurrentRun.IsFinalized())
			return;
		if(m_CurrentRun.State() == EAnalysisState::WAITING && m_CurrentRun.Participants().empty())
		{
			// A pre-start display context is not a run result and must not survive a
			// map/server transition as an empty analysis.
			RemoveActiveCheckpoint();
			RemoveCurrentRun();
			return;
		}
		const int64_t Tick = CurrentTick();
		if(!m_CurrentRun.Finalize(Result, Tick))
			return;
		SRunSnapshot SnapshotData = m_CurrentRun.Snapshot(Tick, time_timestamp());
		CopyParticipantCosmeticsToSnapshot(&SnapshotData);
		FinalizeRunSnapshot(SnapshotData);
		RemoveCurrentRun();
	}

	void CImpactSystem::SaveActiveCheckpoint(bool Force)
	{
		const bool WaitingWithoutParticipants = m_HasCurrentRun && m_CurrentRun.State() == EAnalysisState::WAITING && m_CurrentRun.Participants().empty();
		const bool IntervalElapsed = time_get() - m_LastCheckpointTime >= time_freq() * 2;
		if(!ShouldSaveActiveCheckpoint(g_Config.m_AmfImpactStatisticsAccess != 0, m_HasCurrentRun,
			m_HasCurrentRun && m_CurrentRun.IsFinalized(), WaitingWithoutParticipants, Force, IntervalElapsed))
			return;
		SRunSnapshot SnapshotData = m_CurrentRun.Snapshot(CurrentTick(), time_timestamp());
		CopyParticipantCosmeticsToSnapshot(&SnapshotData);
		LogDifficultyResult("ACTIVE_CHECKPOINT", SnapshotData, "ACTIVE_RUN", "ACCEPTED", "CHECKPOINT_COPY");
		if(!m_bImpactPersistenceShuttingDown && m_pActivePersistenceQueue)
		{
			m_pActivePersistenceQueue->Save(std::move(SnapshotData), m_NextRunId, m_NextEventId);
			m_LastCheckpointTime = time_get();
		}
		else if(CPersistence::SaveActive(Storage(), SnapshotData, m_NextRunId, m_NextEventId))
			m_LastCheckpointTime = time_get();
	}

	void CImpactSystem::RemoveActiveCheckpoint()
	{
		if(!m_bImpactPersistenceShuttingDown && m_pActivePersistenceQueue)
			m_pActivePersistenceQueue->Remove();
		else
			CPersistence::RemoveActive(Storage());
	}

	void CImpactSystem::SaveHistory()
	{
		if(!m_HistoryLoadSucceeded)
			return;
		if(!CPersistence::SaveHistory(Storage(), m_vHistory, m_HistoryLoadSucceeded))
			log_warn("amf-impact", "failed to save run history");
	}

	void CImpactSystem::RemoveCurrentRun()
	{
		m_CurrentRun.Clear();
		m_HasCurrentRun = false;
		m_vRunParticipantCosmetics.clear();
		m_Phase2.Reset();
		m_aSlotMissingSnapshots.fill(0);
		m_aSlotMissingSinceTick.fill(-1);
		m_aSlotMissingLastTick.fill(-1);
		m_aSlotDisconnectConfirmed.fill(false);
		m_aPendingResetGenerationAdvance.fill(false);
		m_aResetSawCharacterAbsence.fill(false);
		ResetDiagnosticLogDeduplication();
		// Personal-start observations belong to the run context as well as to the
		// slot generation. A new run may reuse the same client ids and team id, so
		// retaining this table would make the next run report already_started.
		ResetStartTracking();
		m_vGameplayMessages.clear();
		m_LastCheckpointTime = 0;
	}

	void CImpactSystem::DiscardActiveAnalysis()
	{
		if(!m_HasCurrentRun)
			return;
		RemoveCurrentRun();
		RemoveActiveCheckpoint();
	}

	void CImpactSystem::OnUpdate()
	{
		if(!SynchronizeLocalProcessingToggle())
			return;
		ExpirePendingSaveRuns();
		m_RunCompletionToast.SuppressIfDisabled(g_Config.m_AmfImpactStatisticsAccess);
		if(m_RunCompletionToast.m_Active)
		{
			const int64_t Frequency = std::max<int64_t>(1, time_freq());
			const int64_t ToastDuration = Frequency * RUN_COMPLETION_TOAST_DURATION_MS / 1000;
			const bool PendingRunResolved = m_RunCompletionToast.m_PendingSave && PendingSaveRun(m_RunCompletionToast.m_RunId) == nullptr;
			const int64_t Now = time_get();
			if(PendingRunResolved && !m_RunCompletionToast.m_Dismissing)
			{
				m_RunCompletionToast.m_Dismissing = true;
				m_RunCompletionToast.m_StartTime = Now - Frequency * (RUN_COMPLETION_TOAST_DURATION_MS - RUN_COMPLETION_TOAST_EXIT_MS) / 1000;
			}
			if(Now - m_RunCompletionToast.m_StartTime >= ToastDuration)
				m_RunCompletionToast = {};
		}
		PumpMapInfoRequest();
		if(g_Config.m_Debug && !m_ImpactDebugEnabled)
			ImpactDebugLog("IMPACT_DEBUG_STATE timestamp=%lld reason='debug config enabled'", static_cast<long long>(time_timestamp()));
		else if(!g_Config.m_Debug && m_ImpactDebugEnabled)
		{
			char aDisabled[128];
			str_format(aDisabled, sizeof(aDisabled), "IMPACT_DEBUG_DISABLED timestamp=%lld", static_cast<long long>(time_timestamp()));
			WriteImpactDebugLine(aDisabled);
			m_ImpactDebugEnabled = false;
		}
		if(!m_EngineActive)
			return;
		SaveActiveCheckpoint(false);
	}

	void CImpactSystem::UpdatePhase2()
	{
		if(!m_HasCurrentRun)
		{
			if(g_Config.m_Debug)
			{
				for(const SImpactGameplayMessage &Message : m_vGameplayMessages)
				{
					if(Message.m_Type != EImpactGameplayMessageType::KILL)
						continue;
					const int VictimId = Message.m_Victim;
					const bool ValidVictim = VictimId >= 0 && VictimId < MAX_CLIENTS;
					const uint32_t Generation = ValidVictim ? m_aSlotGeneration[VictimId] : 0;
					const int TeamId = ValidVictim ? GameClient()->m_Teams.Team(VictimId) : 0;
					ImpactDebugLog("DEATH_REJECTED server_tick=%lld victim=%d generation=%u team_id=%d started=0 eligible=0 episode_id=0 reason='no current Impact run'",
						static_cast<long long>(Message.m_ServerTick), VictimId, Generation, TeamId);
					ImpactDebugLog("FALL_NOT_EMITTED server_tick=%lld target=%d generation=%u team_id=%d started=0 eligible=0 reason='no current Impact run'",
						static_cast<long long>(Message.m_ServerTick), VictimId, Generation, TeamId);
				}
			}
			m_vGameplayMessages.clear();
			return;
		}

		const int64_t Tick = CurrentTick();
		std::vector<SImpactGameplayMessage> vMessages = std::move(m_vGameplayMessages);
		m_vGameplayMessages.clear();
		const bool TeamBoundary = HasConfirmedTeamBoundary(vMessages, m_CurrentRun.Context().m_TeamId);
		if(m_LocalResetSnapshot.m_RunId != 0)
		{
			const int ResetClientId = m_LocalResetSnapshot.m_Participant.m_ClientId;
			const uint32_t CurrentGeneration = ResetClientId >= 0 && ResetClientId < MAX_CLIENTS ? m_aSlotGeneration[ResetClientId] : 0;
			const uint64_t CurrentRunId = m_HasCurrentRun ? m_CurrentRun.RunId() : 0;
			const auto MatchingBoundary = std::find_if(vMessages.begin(), vMessages.end(), [this, CurrentGeneration, CurrentRunId](const SImpactGameplayMessage &Message) {
				return IsTeamBoundaryForRecentLocalReset(Message, m_LocalResetSnapshot.m_TeamId, m_CurrentRun.Context().m_TeamId, CurrentRunId,
					m_LocalResetSnapshot.m_SourceRunId, m_LocalResetSnapshot.m_Participant, CurrentGeneration,
					m_LocalResetSnapshot.m_ServerTick, m_LocalResetSnapshot.m_ResetMessageId);
			});
			if(MatchingBoundary != vMessages.end())
			{
				const uint64_t SupersededRunId = m_LocalResetSnapshot.m_RunId;
				EraseRunSnapshotsById(m_vPendingSaveRuns, SupersededRunId, [](const SPendingSaveRun &Pending) {
					return Pending.m_Snapshot.m_RunId;
				});
				if(HistoryContainsRun(SupersededRunId))
					DeleteHistoryRun(SupersededRunId);
				if(m_RunCompletionToast.m_RunId == SupersededRunId)
					m_RunCompletionToast = {};
				m_LocalResetSnapshot = {};
			}
		}
		struct SParticipantReset
		{
			SParticipantId m_Participant;
			int64_t m_Tick;
			uint64_t m_MessageId;
			int m_TeamId;
		};
		std::vector<SParticipantReset> vParticipantResets;
		for(const SImpactGameplayMessage &Message : vMessages)
		{
			// SV_KILLMSGTEAM comes from KillTeam: m_First names the initiator, but
			// the server killed the whole team. That boundary takes precedence over
			// any individual death message in the same network batch.
			if(Message.m_Type != EImpactGameplayMessageType::KILL ||
				Message.m_Victim < 0 || Message.m_Victim >= MAX_CLIENTS)
				continue;
			const int CandidateClientId = Message.m_Victim;
			const bool IsDummy = CandidateClientId == GameClient()->m_aLocalIds[1] && CandidateClientId != GameClient()->m_aLocalIds[0];
			const SParticipantId Candidate{CandidateClientId, m_aSlotGeneration[CandidateClientId], IsDummy};
			const bool Started = m_CurrentRun.HasParticipant(Candidate) && !m_CurrentRun.IsParticipantFinalized(Candidate);
			if(!ShouldFinalizeParticipantReset(Message, Started, TeamBoundary))
				continue;
			const auto Existing = std::find_if(vParticipantResets.begin(), vParticipantResets.end(), [&Candidate](const SParticipantReset &Entry) {
				return Entry.m_Participant == Candidate;
			});
			if(Existing == vParticipantResets.end())
				vParticipantResets.push_back({Candidate, Message.m_ServerTick >= 0 ? Message.m_ServerTick : Tick, Message.m_MessageId, Message.m_TeamId});
			else if(Message.m_ServerTick >= 0 && Message.m_ServerTick < Existing->m_Tick)
			{
				Existing->m_Tick = Message.m_ServerTick;
				Existing->m_MessageId = Message.m_MessageId;
				Existing->m_TeamId = Message.m_TeamId;
			}
		}
		std::sort(vParticipantResets.begin(), vParticipantResets.end(), [](const SParticipantReset &Left, const SParticipantReset &Right) {
			if(Left.m_Tick != Right.m_Tick)
				return Left.m_Tick < Right.m_Tick;
			return Left.m_Participant.m_ClientId < Right.m_Participant.m_ClientId;
		});
		bool Phase2Active = IsPhase2ProcessingAllowed(m_CurrentRun.State(), m_CurrentRun.ActiveParticipantCount());
		std::vector<SImpactGameplayFrame> vFrames;
		bool HasUnengagedPotentialInput = false;
		if(!Phase2Active)
		{
			for(const SParticipant &Participant : m_CurrentRun.Participants())
			{
				const int ClientId = Participant.m_Id.m_ClientId;
				if(Participant.m_Finalized || Participant.m_Engaged || !Participant.m_Present || ClientId < 0 || ClientId >= MAX_CLIENTS ||
					!m_aSlotActive[ClientId] || m_aSlotGeneration[ClientId] != Participant.m_Id.m_SlotGeneration ||
					GameClient()->m_Teams.Team(ClientId) != m_CurrentRun.Context().m_TeamId)
					continue;
				const auto &RawCurrent = RawCurrentObservation(GameClient(), ClientId);
				if(!RawCurrent.m_FromRawCurrentSnapshot)
					continue;
				const bool FrozenSignal = RawCurrent.m_HasExtendedData &&
					(RawCurrent.m_ExtendedData.m_FreezeStart >= 0 || GameClient()->m_aClients[ClientId].m_DeepFrozen ||
						GameClient()->m_aClients[ClientId].m_LiveFrozen || GameClient()->m_aClients[ClientId].m_FreezeEnd > Tick);
				const bool HookSignal = RawCurrent.m_Character.m_HookState == HOOK_GRABBED && RawCurrent.m_Character.m_HookedPlayer >= 0;
				if(FrozenSignal || HookSignal)
				{
					HasUnengagedPotentialInput = true;
					break;
				}
			}
			if(!HasUnengagedPotentialInput)
			{
				for(const SImpactGameplayMessage &Message : vMessages)
				{
					if(!Message.m_Confirmed || (Message.m_TeamId != 0 && Message.m_TeamId != m_CurrentRun.Context().m_TeamId))
						continue;
					for(const SParticipant &Participant : m_CurrentRun.Participants())
						if(!Participant.m_Engaged && !Participant.m_Finalized &&
							(Participant.m_Id == Message.m_Actor || Participant.m_Id == Message.m_Target ||
								Participant.m_Id.m_ClientId == Message.m_Victim || Participant.m_Id.m_ClientId == Message.m_ClientId))
							HasUnengagedPotentialInput = true;
					if(HasUnengagedPotentialInput)
						break;
				}
			}
		}
		if(g_Config.m_Debug || Phase2Active || HasUnengagedPotentialInput)
			vFrames = BuildGameplayFrames(GameClient(), m_CurrentRun, m_aSlotGeneration, Tick);
		CaptureAledHammerObservations(vFrames);
		if(Phase2Active || HasUnengagedPotentialInput)
			RunShadowAutonomousDiagnostics(vFrames);
		if(g_Config.m_Debug)
		{
			LogLiveFrameTransitions(vFrames);
		}
		SImpactPhase2Output Output;
		bool Phase2OutputAlreadyProcessed = false;
		if(!Phase2Active && HasUnengagedPotentialInput && CImpactPhase2::HasPotentialEngagementEvidence(vFrames, m_CurrentRun.Context().m_TeamId, vMessages))
		{
			CImpactPhase2 Phase2Probe = m_Phase2;
			SImpactPhase2Output ProbeOutput = Phase2Probe.Update(m_CurrentRun.RunId(), m_CurrentRun.Context().m_TeamId,
				m_CurrentRun.Context().m_TickSpeed, vFrames, vMessages);
			for(const SImpactPhase2Output::SEngagementProof &Proof : ProbeOutput.m_vEngagementProofs)
				m_CurrentRun.ProveParticipantEngaged(Proof.m_Participant, Proof.m_Tick, Proof.m_Reason.c_str());
			if(IsPhase2ProcessingAllowed(m_CurrentRun.State(), m_CurrentRun.ActiveParticipantCount()))
			{
				m_Phase2 = std::move(Phase2Probe);
				Output = std::move(ProbeOutput);
				Phase2Active = true;
				Phase2OutputAlreadyProcessed = true;
				ImpactDebugLog("IMPACT_ENGAGEMENT_PROMOTION server_tick=%lld run_id=%llu active_roster=%d engaged_active=%d source=phase2_probe same_batch=1",
					static_cast<long long>(Tick), static_cast<unsigned long long>(m_CurrentRun.RunId()),
					m_CurrentRun.ActiveParticipantCount(), m_CurrentRun.ActiveEngagedParticipantCount());
			}
		}
		if(!Phase2Active)
		{
			if(g_Config.m_Debug)
			{
				const EAnalysisState RunState = m_CurrentRun.State();
				const int ActiveParticipants = m_CurrentRun.ActiveParticipantCount();
				ImpactDebugLog("IMPACT_ANALYSIS_GATE server_tick=%lld run_id=%llu team_id=%d state=%s roster_participants=%d active_roster=%d engaged_active=%d finalized_participants=%d present_participants=%d required_active_roster=2 phase2_allowed=0 reason='%s'",
					static_cast<long long>(Tick), static_cast<unsigned long long>(m_CurrentRun.RunId()), m_CurrentRun.Context().m_TeamId,
					AnalysisStateName(RunState), m_CurrentRun.RosterMemberCount(), ActiveParticipants, m_CurrentRun.ActiveEngagedParticipantCount(),
					m_CurrentRun.FinalizedParticipantCount(), m_CurrentRun.EligibleParticipantCount(),
					ActiveParticipants < 2 ? "ACTIVE_ROSTER_BELOW_2" : "RUN_STATE_NOT_PROCESSABLE");
				for(const SImpactGameplayFrame &Frame : vFrames)
				{
					if(!Frame.m_Frozen && !Frame.m_HarmfulOutcomeCandidate)
						continue;
					ImpactDebugLog("FALL_NOT_EMITTED server_tick=%lld target=%d generation=%u team_id=%d run_id=%llu episode_state='NOT_UPDATED_BY_PROCESSING_GATE' active_roster=%d frozen=%d autonomous_evaluated=%d autonomous_known=%d autonomous_possible=%d harmful_candidate_known=%d harmful_candidate=%d physical_outcome=%s reason='PHASE2_PROCESSING_GATE'",
						static_cast<long long>(Frame.m_ServerTick), Frame.m_Participant.m_ClientId,
						Frame.m_Participant.m_SlotGeneration, Frame.m_TeamId,
						static_cast<unsigned long long>(m_CurrentRun.RunId()), ActiveParticipants,
						Frame.m_Frozen ? 1 : 0, Frame.m_AutonomousRecoveryEvaluated ? 1 : 0,
						Frame.m_AutonomousRecoveryKnown ? 1 : 0, Frame.m_AutonomousRecoveryPossible ? 1 : 0,
						Frame.m_HarmfulOutcomeCandidateKnown ? 1 : 0, Frame.m_HarmfulOutcomeCandidate ? 1 : 0,
						PhysicalOutcomeName(Frame.m_PhysicalOutcome));
				}
				for(const SImpactGameplayMessage &Message : vMessages)
				{
					if(Message.m_Type != EImpactGameplayMessageType::KILL)
						continue;
					const int VictimId = Message.m_Victim;
					const bool ValidVictim = VictimId >= 0 && VictimId < MAX_CLIENTS;
					const bool IsDummy = ValidVictim && VictimId == GameClient()->m_aLocalIds[1] && VictimId != GameClient()->m_aLocalIds[0];
					const SParticipantId Victim{VictimId, ValidVictim ? m_aSlotGeneration[VictimId] : 0, IsDummy};
					const bool RosterMember = m_HasCurrentRun && Victim.IsValid() && m_CurrentRun.HasParticipant(Victim);
					bool Eligible = false;
					if(RosterMember)
						m_CurrentRun.ParticipantPresent(Victim, &Eligible);
					const int TeamId = ValidVictim ? GameClient()->m_Teams.Team(VictimId) : m_CurrentRun.Context().m_TeamId;
					ImpactDebugLog("DEATH_REJECTED server_tick=%lld victim=%d generation=%u team_id=%d roster_member=%d eligible=%d episode_id=0 reason='Phase2 processing gate closed'",
						static_cast<long long>(Message.m_ServerTick), VictimId, Victim.m_SlotGeneration, TeamId, RosterMember ? 1 : 0, Eligible ? 1 : 0);
					ImpactDebugLog("FALL_NOT_EMITTED server_tick=%lld target=%d generation=%u team_id=%d roster_member=%d eligible=%d reason='PHASE2_PROCESSING_GATE'",
						static_cast<long long>(Message.m_ServerTick), VictimId, Victim.m_SlotGeneration, TeamId, RosterMember ? 1 : 0, Eligible ? 1 : 0);
				}
			}
			for(const auto &Reset : vParticipantResets)
				FinalizeParticipant(Reset.m_Participant, Reset.m_Tick, true, Reset.m_MessageId, Reset.m_TeamId);
			// A quorum pause suppresses gameplay observations, but it must not
			// swallow an authoritative terminal team boundary. Resolve only those
			// lifecycle messages with no gameplay frames so pending episodes close
			// and the existing team result is still finalized.
			if(TeamBoundary && m_HasCurrentRun && !m_CurrentRun.IsFinalized())
			{
				std::vector<SImpactGameplayMessage> vBoundaryMessages;
				for(const SImpactGameplayMessage &Message : vMessages)
					if(IsConfirmedTeamBoundaryMessage(Message, m_CurrentRun.Context().m_TeamId))
						vBoundaryMessages.push_back(Message);
				const std::vector<SImpactGameplayFrame> vNoGameplayFrames;
				SImpactPhase2Output BoundaryOutput = m_Phase2.Update(m_CurrentRun.RunId(), m_CurrentRun.Context().m_TeamId,
					m_CurrentRun.Context().m_TickSpeed, vNoGameplayFrames, vBoundaryMessages);
				m_CurrentRun.SetPendingEpisodes(BoundaryOutput.m_vPendingEpisodes);
				for(SImpactEvent &Event : BoundaryOutput.m_vEvents)
					if(Event.m_EventId == 0)
						Event.m_EventId = m_NextEventId++;
				AttachForensicEventIds(BoundaryOutput);
				m_CurrentRun.AppendForensicEvents(BoundaryOutput.m_vForensic,
					BoundaryOutput.m_ForensicTraceTruncated, BoundaryOutput.m_ForensicDroppedEvents);
				if(!BoundaryOutput.m_vEvents.empty())
				{
					STickEventBatch BoundaryBatch;
					BoundaryBatch.m_ServerTick = Tick;
					BoundaryBatch.m_vCandidates = std::move(BoundaryOutput.m_vEvents);
					SubmitTickEventBatch(BoundaryBatch);
				}
			}
			return;
		}
		if(!Phase2OutputAlreadyProcessed)
			Output = m_Phase2.Update(m_CurrentRun.RunId(), m_CurrentRun.Context().m_TeamId, m_CurrentRun.Context().m_TickSpeed, vFrames, vMessages);
		for(const SImpactPhase2Output::SEngagementProof &Proof : Output.m_vEngagementProofs)
			m_CurrentRun.ProveParticipantEngaged(Proof.m_Participant, Proof.m_Tick, Proof.m_Reason.c_str());
		m_CurrentRun.SetPendingEpisodes(Output.m_vPendingEpisodes);
		STickEventBatch Batch;
		if(!Output.m_vEvents.empty())
		{
			Batch.m_ServerTick = Tick;
			Batch.m_vCandidates = std::move(Output.m_vEvents);
		}
		for(const SImpactPhase2Debug &Debug : Output.m_vDebug)
		{
			CaptureAledCandidate(Debug);
		}
		if(!Batch.m_vCandidates.empty())
		{
			for(const SImpactPhase2Debug &Debug : Output.m_vDebug)
				QueueAledConfirmedEvent(Debug, Batch);
			for(SImpactEvent &Event : Batch.m_vCandidates)
				if(Event.m_EventId == 0)
					Event.m_EventId = m_NextEventId++;
			for(SImpactPhase2Debug &Debug : Output.m_vDebug)
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
		}
		AttachForensicEventIds(Output);
		m_CurrentRun.AppendForensicEvents(Output.m_vForensic, Output.m_ForensicTraceTruncated, Output.m_ForensicDroppedEvents);
		const bool HasEvents = !Batch.m_vCandidates.empty();
		if(g_Config.m_Debug)
		{
			for(const SImpactPhase2Debug &Debug : Output.m_vDebug)
			{
				LogAledResearch(Debug, vFrames);
				LogPhase2Debug(Debug);
				log_debug("amf-impact", "phase2 %s source_tick=%lld event_tick=%lld episode=%llu event_id=%llu type=%s target=%d potential_saver=%d team=%d start=%lld frozen_known=%d frozen_before=%d frozen_now=%d freeze_start=%lld freeze_end=%lld pos_known=%d velocity_known=%d support_known=%d safe_known=%d safe_position=%d autonomous_known=%d autonomous_possible=%d displacement=%d pos=%d,%d vel=%d,%d recovery_need=%s unfreeze_cause=%s hook=%d hook_first=%lld hook_last=%lld hook_continuity=%d hammer_target_known=%d hammer=%d restoration=%lld deadline=%lld state=%s ownership=%s unknown_interruption=%d reason='%s'",
					ImpactPhase2DebugStageName(Debug.m_Stage), static_cast<long long>(Debug.m_ServerTick), static_cast<long long>(Debug.m_EmissionTick), static_cast<unsigned long long>(Debug.m_EpisodeId), static_cast<unsigned long long>(Debug.m_EventId), Phase2DebugEventTypeName(Debug),
					Debug.m_Target.m_ClientId, Debug.m_PotentialSaver.m_ClientId, Debug.m_TeamId, static_cast<long long>(Debug.m_StartTick), Debug.m_FreezeKnown ? 1 : 0,
					Debug.m_FrozenBefore ? 1 : 0, Debug.m_FrozenNow ? 1 : 0, static_cast<long long>(Debug.m_FreezeStart), static_cast<long long>(Debug.m_FreezeEnd),
					Debug.m_PositionKnown ? 1 : 0, Debug.m_VelocityKnown ? 1 : 0, Debug.m_SupportKnown ? 1 : 0,
					Debug.m_SafeCollisionPositionKnown ? 1 : 0, Debug.m_SafeCollisionPosition ? 1 : 0,
					Debug.m_AutonomousRecoveryKnown ? 1 : 0, Debug.m_AutonomousRecoveryPossible ? 1 : 0,
					Debug.m_DisplacementRecoveryEvidence ? 1 : 0, Debug.m_X, Debug.m_Y, Debug.m_VelX, Debug.m_VelY,
					RecoveryNeedName(Debug.m_RecoveryNeed), UnfreezeCauseName(Debug.m_UnfreezeCause), Debug.m_HookEvidence ? 1 : 0,
					static_cast<long long>(Debug.m_HookFirstTick), static_cast<long long>(Debug.m_HookLastTick), Debug.m_HookControlContinuity ? 1 : 0,
					Debug.m_HammerTargetEvidenceKnown ? 1 : 0, Debug.m_HammerEvidence ? 1 : 0,
					static_cast<long long>(Debug.m_RestorationTick), static_cast<long long>(Debug.m_ConfirmationDeadlineTick),
					ImpactEpisodeStateName(Debug.m_EpisodeState), OwnershipStatusName(Debug.m_OwnershipStatus), Debug.m_UnknownInterruption ? 1 : 0, Debug.m_Reason.c_str());
			}
		}

		bool AnyApplied = false;
		if(vParticipantResets.empty())
		{
			if(HasEvents)
			{
				AnyApplied = SubmitTickEventBatch(Batch);
				if(AnyApplied && g_Config.m_Debug)
				{
					for(const SImpactEvent &Event : Batch.m_vCandidates)
					{
						log_debug("amf-impact", "phase2 confirmed event_id=%llu type=%s actor=%d target=%d start=%lld resolve=%lld state=%s ownership=%s confidence=%s cause='%s'",
							static_cast<unsigned long long>(Event.m_EventId), ImpactEventTypeName(Event.m_Type), Event.m_Actor.m_ClientId, Event.m_Target.m_ClientId,
							static_cast<long long>(Event.m_StartTick), static_cast<long long>(Event.m_ResolveTick),
							ImpactEpisodeStateName(Event.m_EpisodeState), OwnershipStatusName(Event.m_OwnershipStatus),
							CausalConfidenceName(Event.m_CausalConfidence), Event.m_CausalReason.c_str());
						ImpactDebugLog("CANONICAL_EVENT server_tick=%lld event_id=%llu type=%s actor=%d actor_generation=%u target=%d target_generation=%u team_id=%d start_tick=%lld resolve_tick=%lld state=%s ownership=%s confidence=%s raw_saves=%d raw_falls=%d cause='%s'",
							static_cast<long long>(Event.m_ResolveTick), static_cast<unsigned long long>(Event.m_EventId), ImpactEventTypeName(Event.m_Type),
							Event.m_Actor.m_ClientId, Event.m_Actor.m_SlotGeneration, Event.m_Target.m_ClientId, Event.m_Target.m_SlotGeneration,
							Event.m_TeamId, static_cast<long long>(Event.m_StartTick), static_cast<long long>(Event.m_ResolveTick),
							ImpactEpisodeStateName(Event.m_EpisodeState), OwnershipStatusName(Event.m_OwnershipStatus), CausalConfidenceName(Event.m_CausalConfidence),
							Event.m_RawDelta.m_Saves, Event.m_RawDelta.m_Falls, Event.m_CausalReason.c_str());
						if(Event.m_RawDelta.m_Clutches > 0)
						{
							int ClutchesAfter = -1;
						for(const SParticipant &Participant : m_CurrentRun.Participants())
							if(Participant.m_Id == Event.m_Actor)
							{
								ClutchesAfter = Participant.m_RawStats.m_Clutches;
								break;
							}
							ImpactDebugLog("CLUTCH_CONFIRMED event_id=%llu actor=%d target=%d confirm_tick=%lld clutch_count_before=%d clutch_count_after=%d reason='confirmed canonical Save for sole-unfrozen episode'",
								static_cast<unsigned long long>(Event.m_EventId), Event.m_Actor.m_ClientId, Event.m_Target.m_ClientId,
								static_cast<long long>(Event.m_ResolveTick), ClutchesAfter >= 0 ? ClutchesAfter - Event.m_RawDelta.m_Clutches : -1, ClutchesAfter);
						}
					}
				}
			}
			else
				SaveActiveCheckpoint(false);
		}
		else
		{
			std::vector<SImpactEvent> vRemaining = Batch.m_vCandidates;
			for(const auto &Reset : vParticipantResets)
			{
				if(!m_HasCurrentRun || m_CurrentRun.IsFinalized())
					break;
				STickEventBatch BeforeReset;
				BeforeReset.m_ServerTick = Reset.m_Tick;
				std::vector<SImpactEvent> vAfterReset;
				vAfterReset.reserve(vRemaining.size());
				for(SImpactEvent &Event : vRemaining)
				{
					const int64_t EventTick = Event.m_ResolveTick >= 0 ? Event.m_ResolveTick :
						(Batch.m_ServerTick >= 0 ? Batch.m_ServerTick : Tick);
					if(EventTick <= Reset.m_Tick)
						BeforeReset.m_vCandidates.push_back(std::move(Event));
					else
						vAfterReset.push_back(std::move(Event));
				}
				vRemaining = std::move(vAfterReset);
				if(!BeforeReset.m_vCandidates.empty())
					AnyApplied = SubmitTickEventBatch(BeforeReset) || AnyApplied;
				if(m_HasCurrentRun && !m_CurrentRun.IsFinalized())
					FinalizeParticipant(Reset.m_Participant, Reset.m_Tick, true, Reset.m_MessageId, Reset.m_TeamId);
			}
			if(m_HasCurrentRun && !m_CurrentRun.IsFinalized() && !vRemaining.empty())
			{
				STickEventBatch AfterResets;
				AfterResets.m_ServerTick = Tick;
				AfterResets.m_vCandidates = std::move(vRemaining);
				AnyApplied = SubmitTickEventBatch(AfterResets) || AnyApplied;
			}
			if(!m_HasCurrentRun)
				AnyApplied = true;
		}
		if(HasEvents)
			CommitAledConfirmedEvents(Batch, AnyApplied);
	}

	void CImpactSystem::SetPhase2RunContext()
	{
		if(m_HasCurrentRun)
		{
			m_Phase2.SetRunContext(m_CurrentRun.RunId(), m_CurrentRun.Context().m_TeamId);
			m_Phase2.SetPendingEpisodes(m_CurrentRun.PendingEpisodes());
		}
	}

	void CImpactSystem::OnNewSnapshot()
	{
		if(!SynchronizeLocalProcessingToggle())
		{
			ObserveDisabledStartLifecycle();
			return;
		}
		RefreshServerContext(nullptr, nullptr);
		if(m_HasCurrentRun && m_ServerContextKnown &&
			(m_CurrentRun.Context().m_ServerKey != m_ServerContext.m_ServerKey || m_CurrentRun.Context().m_Map != m_ServerContext.m_Map))
		{
			const bool RunMapChanged = m_CurrentRun.Context().m_Map != m_ServerContext.m_Map;
			HandleInterruptedRun(RunMapChanged ? ERunResult::ABORTED_MAP_CHANGE : ERunResult::ABORTED_SERVER_CHANGE);
		}
		if(!CanCollect())
			return;
		UpdateSlotGenerations();
		const int LocalTeamId = CurrentLocalTeamId();
		// A confirmed local TeamId change is a new run context. Finalize the old
		// run before creating the new context so its participants/start state cannot
		// leak into the new team. A temporary Team 0 snapshot is intentionally not a
		// barrier; the next confirmed non-zero team decides the transition.
		if(m_HasCurrentRun && IsValidImpactTeam(LocalTeamId) &&
			m_CurrentRun.Context().m_TeamId != LocalTeamId)
			HandleInterruptedRun(ERunResult::TEAM_RESET);
		if(m_HasCurrentRun && m_CurrentRun.State() == EAnalysisState::WAITING && m_CurrentRun.Participants().empty())
		{
			if(LocalTeamId != m_CurrentRun.Context().m_TeamId)
			{
				// A pre-start display context follows the local team. Never carry an
				// empty Team 3 context into Team 4 (or Team 0).
				RemoveCurrentRun();
				EnsureRun(LocalTeamId);
			}
		}
		if(!m_HasCurrentRun)
			EnsureRun(LocalTeamId);
		// A confirmed server team kill closes the old attempt before a respawned
		// Tee can contribute a START to it. The next snapshot may start a new run.
		if(m_HasCurrentRun && m_CurrentRun.StartedParticipantCount() > 0 &&
			HasConfirmedTeamBoundary(m_vGameplayMessages, m_CurrentRun.Context().m_TeamId))
		{
			SetPhase2RunContext();
			UpdatePhase2();
			return;
		}
		DetectStartCrossings();
		UpdateConfirmedParticipants();
		SetPhase2RunContext();
		UpdatePhase2();
	}

	void CImpactSystem::DebugLogRawNetworkMessage(int Msg, int Conn, bool Decoded)
	{
		if(!SynchronizeLocalProcessingToggle())
			return;
		const char *pType = nullptr;
		if(Msg == NETMSGTYPE_SV_KILLMSG)
			pType = "SV_KILLMSG";
		else if(Msg == NETMSGTYPE_SV_KILLMSGTEAM)
			pType = "SV_KILLMSGTEAM";
		else if(Msg == NETMSGTYPE_SV_RACEFINISH)
			pType = "SV_RACEFINISH";
		if(pType == nullptr)
			return;
		const int64_t Tick = Conn >= 0 ? Client()->GameTick(Conn) : -1;
		ImpactDebugLog("NETMSG_RAW server_tick=%lld connection=%d msg_id=%d decoded=%d relevant_type=%s",
			static_cast<long long>(Tick), Conn, Msg, Decoded ? 1 : 0, pType);
	}

	void CImpactSystem::OnMessageFromConnection(int Msg, void *pRawMsg, int Conn)
	{
		if(pRawMsg == nullptr)
			return;
		if(!SynchronizeLocalProcessingToggle())
		{
			if(Msg == NETMSGTYPE_SV_KILLMSGTEAM)
			{
				const CNetMsg_Sv_KillMsgTeam *pTeamKill = static_cast<const CNetMsg_Sv_KillMsgTeam *>(pRawMsg);
				if(m_LocalRunEnableGate.ObserveExplicitTeamReset(pTeamKill->m_Team, pTeamKill->m_First >= 0))
				{
					ResetDisabledStartObservations(true);
					ImpactDebugLog("LOCAL_PROCESSING_RUN_UNLOCKED team_id=%d source='explicit team reset'", pTeamKill->m_Team);
				}
			}
			return;
		}
		SImpactGameplayMessage Message;
		Message.m_MessageId = m_NextGameplayMessageId++;
		Message.m_ServerTick = Conn >= 0 ? Client()->GameTick(Conn) : CurrentTick();
		Message.m_Confirmed = true;
		if(Msg == NETMSGTYPE_SV_KILLMSG)
		{
			const CNetMsg_Sv_KillMsg *pKill = static_cast<const CNetMsg_Sv_KillMsg *>(pRawMsg);
			Message.m_Type = EImpactGameplayMessageType::KILL;
			Message.m_Killer = pKill->m_Killer;
			Message.m_Victim = pKill->m_Victim;
			Message.m_Weapon = pKill->m_Weapon;
		}
		else if(Msg == NETMSGTYPE_SV_KILLMSGTEAM)
		{
			const CNetMsg_Sv_KillMsgTeam *pTeamKill = static_cast<const CNetMsg_Sv_KillMsgTeam *>(pRawMsg);
			Message.m_Type = EImpactGameplayMessageType::TEAM_WIPE;
			Message.m_TeamId = pTeamKill->m_Team;
			Message.m_First = pTeamKill->m_First;
			// DDNet uses m_First >= 0 for the explicit self/team reset path. A
			// team kill with m_First < 0 is the server-confirmed harmful outcome
			// path. It is still only context: Phase2 confirms already-tracked
			// harmful episodes and never turns every team member into a Fall.
			if(Message.m_First < 0)
			{
				Message.m_CausalConfirmed = true;
				Message.m_CausalConfidence = ECausalConfidence::CONFIRMED;
			}
			ImpactDebugLog("DEATH_RAW_INPUT server_tick=%lld connection=%d victim=-1 killer=-1 weapon=-1 special=TEAM_WIPE victim_team_if_known=%d victim_generation_if_known=0",
				static_cast<long long>(Message.m_ServerTick), Conn, Message.m_TeamId);
		}
		else if(Msg == NETMSGTYPE_SV_RACEFINISH)
		{
			const CNetMsg_Sv_RaceFinish *pFinish = static_cast<const CNetMsg_Sv_RaceFinish *>(pRawMsg);
			Message.m_Type = EImpactGameplayMessageType::FINISH;
			Message.m_ClientId = pFinish->m_ClientId;
			Message.m_FinishTimeMillis = pFinish->m_Time >= 0 ? pFinish->m_Time : -1;
		}
		else
			return;

		if(Message.m_Type == EImpactGameplayMessageType::KILL)
		{
			const int VictimId = Message.m_Victim;
			const bool ValidVictim = VictimId >= 0 && VictimId < MAX_CLIENTS;
			if(ValidVictim)
				Message.m_TeamId = GameClient()->m_Teams.Team(VictimId);
			ImpactDebugLog("DEATH_RAW_INPUT server_tick=%lld connection=%d victim=%d killer=%d weapon=%d special=-1 victim_team_if_known=%d victim_generation_if_known=%u",
				static_cast<long long>(Message.m_ServerTick), Conn, VictimId, Message.m_Killer, Message.m_Weapon,
				ValidVictim ? GameClient()->m_Teams.Team(VictimId) : 0, ValidVictim ? m_aSlotGeneration[VictimId] : 0);
			const bool IsDummy = ValidVictim && VictimId == GameClient()->m_aLocalIds[1] && VictimId != GameClient()->m_aLocalIds[0];
			const SParticipantId Victim{VictimId, ValidVictim ? m_aSlotGeneration[VictimId] : 0, IsDummy};
			const bool ParticipantFound = m_HasCurrentRun && Victim.IsValid() && m_CurrentRun.HasParticipant(Victim);
			bool Eligible = false;
			if(ParticipantFound)
				m_CurrentRun.ParticipantPresent(Victim, &Eligible);
			const bool LastCharacterContext = ValidVictim && m_Phase2.DebugHasLastCharacterContext(VictimId);
			const int TeamId = ValidVictim ? GameClient()->m_Teams.Team(VictimId) : (m_HasCurrentRun ? m_CurrentRun.Context().m_TeamId : 0);
			ImpactDebugLog("DEATH_INPUT server_tick=%lld victim=%d victim_generation=%u killer=%d weapon=%d special=-1 team_id=%d started=%d eligible=%d participant_found=%d last_character_context=%d",
				static_cast<long long>(Message.m_ServerTick), VictimId, Victim.m_SlotGeneration, Message.m_Killer, Message.m_Weapon, TeamId,
				ParticipantFound ? 1 : 0, Eligible ? 1 : 0, ParticipantFound ? 1 : 0, LastCharacterContext ? 1 : 0);
		}

		// Main and dummy connections can receive the same server-confirmed
		// outcome. Preserve dummy parity without applying one outcome twice.
		const int64_t Tick = Message.m_ServerTick;
		for(size_t i = 0; i < m_vRecentNetworkMessages.size();)
		{
			if(Tick >= 0 && m_vRecentNetworkMessages[i].m_ServerTick >= 0 && Tick - m_vRecentNetworkMessages[i].m_ServerTick > 2)
				m_vRecentNetworkMessages.erase(m_vRecentNetworkMessages.begin() + i);
			else
				++i;
		}
		auto SameNetworkMessage = [](const SImpactGameplayMessage &Left, const SImpactGameplayMessage &Right) {
			const int64_t TickDelta = Left.m_ServerTick - Right.m_ServerTick;
			const bool SameServerTickWindow = Left.m_ServerTick < 0 || Right.m_ServerTick < 0 ||
				(TickDelta >= -2 && TickDelta <= 2);
			return Left.m_Type == Right.m_Type && SameServerTickWindow &&
				Left.m_Killer == Right.m_Killer && Left.m_Victim == Right.m_Victim &&
				Left.m_Weapon == Right.m_Weapon && Left.m_TeamId == Right.m_TeamId &&
				Left.m_First == Right.m_First && Left.m_ClientId == Right.m_ClientId;
		};
		for(const SImpactGameplayMessage &Recent : m_vRecentNetworkMessages)
			if(SameNetworkMessage(Recent, Message))
				return;
		if(m_vRecentNetworkMessages.size() >= 32)
			m_vRecentNetworkMessages.erase(m_vRecentNetworkMessages.begin());
		m_vRecentNetworkMessages.push_back(Message);

		if(m_vGameplayMessages.size() >= 64)
			m_vGameplayMessages.erase(m_vGameplayMessages.begin());
		m_vGameplayMessages.push_back(std::move(Message));
	}

	void CImpactSystem::OnMessage(int Msg, void *pRawMsg)
	{
		if(!SynchronizeLocalProcessingToggle())
		{
			// Explicit server reset is the only Impact message consumed while local
			// statistics are disabled; it only releases the local re-enable gate.
			if(Msg == NETMSGTYPE_SV_KILLMSGTEAM)
				OnMessageFromConnection(Msg, pRawMsg, -1);
			return;
		}
		if(Msg == NETMSGTYPE_SV_CHAT && pRawMsg != nullptr && m_MapInfoCapturePending)
		{
			PumpMapInfoRequest();
			if(m_MapInfoCapturePending)
			{
				const CNetMsg_Sv_Chat *pMsg = static_cast<const CNetMsg_Sv_Chat *>(pRawMsg);
				if(pMsg->m_ClientId < 0)
				{
					const int64_t ServerTick = Client()->GameTick(0);
					char aRaw[2048];
					str_copy(aRaw, pMsg->m_pMessage != nullptr ? pMsg->m_pMessage : "");
					str_sanitize_cc(aRaw);
					const CServerInfo &ServerInfo = Client()->ServerInfo();
					SMapDifficultyMetadata Metadata;
					const bool IsKog = IsKogCommunity(ServerInfo.m_aCommunityId);
					bool Parsed = IsKog && ParseKogMapInfoDifficulty(pMsg->m_pMessage, ServerInfo.m_aMap, &Metadata);
					if(!Parsed)
						Parsed = ParseMapInfoDifficulty(pMsg->m_pMessage, ServerInfo.m_aMap, &Metadata);
					if(Parsed)
					{
						const bool Automatic = m_MapInfoRequestAutomatic;
						const int Attempt = Automatic ? m_MapInfoRequestAttempts : 0;
						if(IsKogCommunity(ServerInfo.m_aCommunityId))
							StoreKogMapMetadata(ServerInfo.m_aMap, Metadata, true);
						ApplyResolvedMapMetadata(Metadata);
						SRunContext Context = m_ServerContext;
						Context.m_Map = ServerInfo.m_aMap;
						Context.m_ServerName = ServerInfo.m_aName;
						Context.m_CommunityId = ServerInfo.m_aCommunityId;
						Context.m_MapDifficulty = Metadata.m_Category;
						Context.m_MapStars = Metadata.m_Stars;
						LogMapMetadataResolution("MAPINFO_CHAT", Context, Attempt);
						ImpactDebugLog("IMPACT_MAPINFO_RESPONSE server_tick=%lld map=\"%s\" server=\"%s\" community=\"%s\" raw=\"%s\" parsed_category=\"%s\" parsed_rating=%d status=RESOLVED source=MAPINFO_CHAT attempt=%d",
							static_cast<long long>(ServerTick), ServerInfo.m_aMap, ServerInfo.m_aName, ServerInfo.m_aCommunityId, aRaw,
							Metadata.m_Category.c_str(), Metadata.m_Stars, Attempt);
						m_MapInfoCapturePending = false;
						m_MapInfoRequestAutomatic = false;
						m_MapInfoCaptureDeadlineTick = -1;
						m_MapInfoNextRequestTick = -1;
						m_MapInfoRequestExhausted = true;
					}
					else if(!m_MapInfoRequestAutomatic)
					{
						ImpactDebugLog("IMPACT_MAPINFO_RESPONSE server_tick=%lld map=\"%s\" server=\"%s\" community=\"%s\" raw=\"%s\" parsed_category=UNKNOWN parsed_rating=-1 status=RAW_CAPTURED",
							static_cast<long long>(ServerTick), ServerInfo.m_aMap, ServerInfo.m_aName, ServerInfo.m_aCommunityId, aRaw);
						m_MapInfoCapturePending = false;
						m_MapInfoRequestAutomatic = false;
						m_MapInfoCaptureDeadlineTick = -1;
					}
				}
			}
		}
		if(Msg == NETMSGTYPE_SV_VOTECLEAROPTIONS)
		{
			m_KogVoteRatingLastDiagnosticKey.clear();
			if(g_Config.m_Debug)
				ImpactDebugLog("KOG_VOTE_LIST event=CLEAR option_count=%d", GameClient()->m_Voting.NumOptions());
		}
		else if(g_Config.m_Debug && Msg == NETMSGTYPE_SV_VOTEOPTIONGROUPEND)
			ImpactDebugLog("KOG_VOTE_LIST event=GROUP_END option_count=%d", GameClient()->m_Voting.NumOptions());
		else if(g_Config.m_Debug && Msg == NETMSGTYPE_SV_VOTEOPTIONADD)
			ImpactDebugLog("KOG_VOTE_LIST event=ADD option_count=%d", GameClient()->m_Voting.NumOptions());
		else if(g_Config.m_Debug && Msg == NETMSGTYPE_SV_VOTEOPTIONLISTADD && !GameClient()->m_Voting.IsReceivingOptions())
			ImpactDebugLog("KOG_VOTE_LIST event=LIST_ADD option_count=%d", GameClient()->m_Voting.NumOptions());
		if((Msg == NETMSGTYPE_SV_VOTEOPTIONLISTADD || Msg == NETMSGTYPE_SV_VOTEOPTIONADD || Msg == NETMSGTYPE_SV_VOTEOPTIONGROUPEND) &&
			!GameClient()->m_Voting.IsReceivingOptions())
			ApplyKogVoteListRating();
		OnMessageFromConnection(Msg, pRawMsg, -1);
	}

	bool CImpactSystem::SubmitTickEventBatch(const STickEventBatch &Batch)
	{
		if(!CanCollect() || Batch.m_vCandidates.empty())
		{
			return false;
		}

		int ScopedTeamId = m_HasCurrentRun ? m_CurrentRun.Context().m_TeamId : 0;
		if(m_HasCurrentRun && !IsValidImpactTeam(ScopedTeamId))
		{
			return false;
		}
		if(!m_HasCurrentRun)
		{
			// START observation remains cross-client, but only a team containing a
			// currently present local main/dummy Tee may seed this user's Current Run.
			// Remote STARTs alone are not local participation, regardless of count.
			ScopedTeamId = SelectNewRunStartTeam(Batch, CurrentLocalTeamId());
			if(!IsValidImpactTeam(ScopedTeamId))
			{
				return false;
			}
		}
		STickEventBatch NormalizedBatch;
		NormalizedBatch.m_ServerTick = Batch.m_ServerTick;
		NormalizedBatch.m_vCandidates.reserve(std::min(Batch.m_vCandidates.size(), MAX_LEDGER_EVENTS));
		for(const SImpactEvent &Candidate : Batch.m_vCandidates)
		{
			if(!m_HasCurrentRun && Candidate.m_Type != EImpactEventType::START_CROSSING)
				continue;
			if(m_HasCurrentRun && Candidate.m_TeamId != 0 && Candidate.m_TeamId != ScopedTeamId)
				continue;
			if(Candidate.m_Type == EImpactEventType::START_CROSSING &&
				(!Candidate.m_Confirmed || Candidate.m_TeamId != ScopedTeamId))
				continue;
			if(NormalizedBatch.m_vCandidates.size() >= MAX_LEDGER_EVENTS)
				break;
			SImpactEvent Event = Candidate;
			if(Event.m_TeamId == 0)
				Event.m_TeamId = ScopedTeamId;
			if(Event.m_EventId == 0)
				Event.m_EventId = m_NextEventId++;
			else if(Event.m_EventId >= m_NextEventId)
				m_NextEventId = Event.m_EventId + 1;
			NormalizedBatch.m_vCandidates.push_back(std::move(Event));
		}
		if(NormalizedBatch.m_vCandidates.empty())
		{
			return false;
		}
		if(HasStartForCurrentParticipant(NormalizedBatch))
		{
			// A crossing by an existing participant normally starts a new attempt.
			// A participant that temporarily left the scoped team can instead
			// rejoin that same team and resume a paused run through its own start.
			if(m_CurrentRun.State() != EAnalysisState::PAUSED_NO_TEAM)
				DiscardActiveAnalysis();
		}
		if(!m_HasCurrentRun)
			EnsureRun(ScopedTeamId);
		if(!m_HasCurrentRun)
			return false;
		if(m_CurrentRun.StartedParticipantCount() == 0 && HasConfirmedStart(NormalizedBatch))
			ReconcileCurrentTeamRoster(NormalizedBatch.m_ServerTick, &NormalizedBatch);
		std::vector<int> vFallsBefore(NormalizedBatch.m_vCandidates.size(), -1);
		for(size_t EventIndex = 0; EventIndex < NormalizedBatch.m_vCandidates.size(); ++EventIndex)
		{
			const SImpactEvent &Event = NormalizedBatch.m_vCandidates[EventIndex];
			if(Event.m_RawDelta.m_Falls == 0)
				continue;
			const SParticipantId RawTarget = Event.m_Type == EImpactEventType::FALL_CONFIRMED && Event.m_Target.IsValid() ? Event.m_Target : Event.m_Actor;
			for(const SParticipant &Participant : m_CurrentRun.Participants())
				if(Participant.m_Id == RawTarget)
				{
					vFallsBefore[EventIndex] = Participant.m_RawStats.m_Falls;
					break;
				}
		}
		const bool Changed = m_CurrentRun.ApplyTickEventBatch(NormalizedBatch);
		if(Changed && m_LocalResetSnapshot.m_RunId != 0)
		{
			for(const SImpactEvent &Event : NormalizedBatch.m_vCandidates)
			{
				if(Event.m_Type == EImpactEventType::START_CROSSING &&
					(Event.m_Actor.m_ClientId == GameClient()->m_aLocalIds[0] || Event.m_Actor.m_ClientId == GameClient()->m_aLocalIds[1]) &&
					m_CurrentRun.HasParticipant(Event.m_Actor) && !m_CurrentRun.IsParticipantFinalized(Event.m_Actor))
				{
					m_LocalResetSnapshot = {};
					break;
				}
			}
		}
		for(size_t EventIndex = 0; EventIndex < NormalizedBatch.m_vCandidates.size(); ++EventIndex)
		{
			const SImpactEvent &Event = NormalizedBatch.m_vCandidates[EventIndex];
			if(Event.m_RawDelta.m_Falls == 0)
				continue;
			const SParticipantId RawTarget = Event.m_Type == EImpactEventType::FALL_CONFIRMED && Event.m_Target.IsValid() ? Event.m_Target : Event.m_Actor;
			int NewFalls = -1;
			for(const SParticipant &Participant : m_CurrentRun.Participants())
				if(Participant.m_Id == RawTarget)
				{
					NewFalls = Participant.m_RawStats.m_Falls;
					break;
				}
			bool LedgerAccepted = false;
			for(const SImpactEvent &AcceptedEvent : m_CurrentRun.Events())
				if(AcceptedEvent.m_EventId == Event.m_EventId && AcceptedEvent.m_AppliedExactlyOnce)
				{
					LedgerAccepted = true;
					break;
				}
			ImpactDebugLog("FALL_RAW_EVENT server_tick=%lld target=%d generation=%u old_falls=%d new_falls=%d ledger_accepted=%d event_id=%llu",
				static_cast<long long>(Event.m_ResolveTick), RawTarget.m_ClientId, RawTarget.m_SlotGeneration, vFallsBefore[EventIndex], NewFalls,
				LedgerAccepted ? 1 : 0, static_cast<unsigned long long>(Event.m_EventId));
		}
		if(m_CurrentRun.IsFinalized())
			HandleFinalizedRun();
		else if(Changed)
			SaveActiveCheckpoint(true);
		return Changed;
	}

	bool CImpactSystem::SubmitStartCrossing(const SParticipantId &Participant, const char *pName, int64_t Tick, int TeamId)
	{
		if(!CanCollect())
			return false;
		SImpactEvent Event;
		Event.m_Type = EImpactEventType::START_CROSSING;
		Event.m_Actor = Participant;
		Event.m_TeamId = TeamId;
		Event.m_ActorName = pName ? pName : "";
		Event.m_StartTick = Tick;
		Event.m_ResolveTick = Tick;
		Event.m_Confirmed = true;
		STickEventBatch Batch;
		Batch.m_ServerTick = Tick;
		Batch.m_vCandidates.push_back(std::move(Event));
		return SubmitTickEventBatch(Batch);
	}

	bool CImpactSystem::SubmitParticipantPresence(const SParticipantId &Participant, bool Present, int64_t Tick)
	{
		if(!CanCollect() || !m_HasCurrentRun || !m_CurrentRun.HasParticipant(Participant))
			return false;
		SImpactEvent Event;
		Event.m_Type = EImpactEventType::PARTICIPANT_PRESENCE;
		Event.m_Actor = Participant;
		Event.m_StartTick = Tick;
		Event.m_ResolveTick = Tick;
		Event.m_Confirmed = true;
		Event.m_Present = Present;
		STickEventBatch Batch;
		Batch.m_ServerTick = Tick;
		Batch.m_vCandidates.push_back(std::move(Event));
		return SubmitTickEventBatch(Batch);
	}

	bool CImpactSystem::SubmitRunResult(ERunResult Result, int64_t Tick)
	{
		if(!CanCollect() || !m_HasCurrentRun || !IsTerminalResult(Result))
			return false;
		SImpactEvent Event;
		Event.m_Type = Result == ERunResult::FINISHED ? EImpactEventType::FINISH_CONFIRMED :
			Result == ERunResult::WIPED ? EImpactEventType::WIPE_CONFIRMED :
			Result == ERunResult::TEAM_RESET ? EImpactEventType::TEAM_RESET_CONFIRMED :
			Result == ERunResult::ABORTED_MAP_CHANGE ? EImpactEventType::MAP_CHANGE :
			Result == ERunResult::ABORTED_SERVER_CHANGE ? EImpactEventType::SERVER_CHANGE : EImpactEventType::CONNECTION_LOST;
		Event.m_StartTick = Tick;
		Event.m_ResolveTick = Tick;
		Event.m_Confirmed = true;
		STickEventBatch Batch;
		Batch.m_ServerTick = Tick;
		Batch.m_vCandidates.push_back(std::move(Event));
		return SubmitTickEventBatch(Batch);
	}

	bool CImpactSystem::SavePendingRun()
	{
		ExpirePendingSaveRuns();
		if(m_vPendingSaveRuns.empty())
			return false;
		return SavePendingRun(m_vPendingSaveRuns.front().m_Snapshot.m_RunId);
	}

	bool CImpactSystem::SavePendingRun(uint64_t RunId)
	{
		ExpirePendingSaveRuns();
		const auto It = std::find_if(m_vPendingSaveRuns.begin(), m_vPendingSaveRuns.end(), [RunId](const SPendingSaveRun &Pending) {
			return Pending.m_Snapshot.m_RunId == RunId;
		});
		if(It == m_vPendingSaveRuns.end())
			return false;
		const SRunSnapshot SnapshotData = It->m_Snapshot;
		if(!AppendHistoryRun(SnapshotData))
			return false;
		m_vPendingSaveRuns.erase(It);
		if(!m_HasCurrentRun)
			RemoveActiveCheckpoint();
		return true;
	}

	void CImpactSystem::DiscardPendingRun()
	{
		ExpirePendingSaveRuns();
		if(m_vPendingSaveRuns.empty())
			return;
		DiscardPendingRun(m_vPendingSaveRuns.front().m_Snapshot.m_RunId);
	}

	bool CImpactSystem::DiscardPendingRun(uint64_t RunId)
	{
		ExpirePendingSaveRuns();
		const auto It = std::find_if(m_vPendingSaveRuns.begin(), m_vPendingSaveRuns.end(), [RunId](const SPendingSaveRun &Pending) {
			return Pending.m_Snapshot.m_RunId == RunId;
		});
		if(It == m_vPendingSaveRuns.end())
			return false;
		m_vPendingSaveRuns.erase(It);
		if(!m_HasCurrentRun)
			RemoveActiveCheckpoint();
		return true;
	}

	bool CImpactSystem::DeleteHistoryRun(uint64_t RunId)
	{
		if(!m_HistoryLoadSucceeded)
			return false;
		if(EraseRunSnapshotsById(m_vHistory, RunId, [](const SRunSnapshot &SnapshotData) {
			return SnapshotData.m_RunId;
		}) == 0)
			return false;
		SaveHistory();
		return true;
	}
}
