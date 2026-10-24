#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_IMPACT_IMPACT_SYSTEM_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_IMPACT_IMPACT_SYSTEM_H

/*
 * Copyright (c) 2026 alya?
 * Original AMF Impact / Run Statistics code, developed for AMF Client.
 * Licensed under licenses/AMF-Impact-LICENSE.txt.
 */

#include "impact_persistence.h"
#include "impact_checkpoint_policy.h"
#include "impact_phase2.h"
#include "impact_run.h"

#include <base/io.h>

#include <engine/console.h>
#include <engine/shared/protocol.h>

#include <array>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <deque>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <game/client/component.h>

class CGameClient;

namespace AmfImpact
{
	class CActivePersistenceQueue;

	// Local-only gate for Impact runtime collection. It intentionally carries no
	// participant/network state: once an observer has seen START while collection
	// is disabled, that same run cannot be enabled halfway through.
	struct SLocalImpactRunEnableGate
	{
		bool m_RunLocked = false;
		int m_TeamId = 0;

		bool CanEnable() const { return !m_RunLocked; }
		void LockForStartedRun(int TeamId)
		{
			m_RunLocked = true;
			m_TeamId = TeamId;
		}
		bool ObserveExplicitTeamReset(int TeamId, bool ExplicitReset)
		{
			if(!m_RunLocked || !ExplicitReset || TeamId != m_TeamId)
				return false;
			Reset();
			return true;
		}
		void Reset()
		{
			m_RunLocked = false;
			m_TeamId = 0;
		}
	};

	class CImpactSystem : public CComponent
	{
	public:
		struct SRunCompletionToast
		{
			bool m_Active = false;
			ERunResult m_Result = ERunResult::NONE;
			uint64_t m_RunId = 0;
			bool m_PendingSave = false;
			bool m_Dismissing = false;
			int64_t m_StartTime = 0;
			bool m_ResetSoundPlayed = false;
			bool IsVisible(int StatisticsAccess) const { return m_Active && StatisticsAccess != 0; }
			bool ConsumeResetSound(int StatisticsAccess)
			{
			if(!IsVisible(StatisticsAccess) || m_ResetSoundPlayed || !CImpactSystem::IsResetRelatedToastResult(m_Result))
				return false;
			m_ResetSoundPlayed = true;
			return true;
			}
			void SuppressIfDisabled(int StatisticsAccess)
			{
				if(StatisticsAccess == 0)
					*this = {};
			}
		};
		static constexpr int RUN_COMPLETION_TOAST_DURATION_MS = 4000;
		static constexpr int RUN_COMPLETION_TOAST_EXIT_MS = 320;

		struct SAledResearchGeometryGridResult
		{
			int m_TilesScanned = 0;
			int m_FreezeTileCount = 0;
			int m_NearestFreezeX = -1;
			int m_NearestFreezeY = -1;
			int m_BarrierAxis = 0;
			int m_ActorSide = 0;
			int m_TargetSide = 0;
			int m_FreezeBetween = -1;
			int m_OppositeSides = -1;
		};

		enum class EAledGeometryV2Topology
		{
			UNKNOWN = 0,
			VERTICAL = 1,
			HORIZONTAL = 2,
			DIAGONAL = 3,
			AMBIGUOUS = 4,
		};

		enum class EAledGeometryV2Status
		{
			SKIPPED = 0,
			VALID = 1,
			INSUFFICIENT_EVIDENCE = 2,
			AMBIGUOUS = 3,
		};

		struct SAledResearchGeometryV2GridResult
		{
			int m_TilesScanned = 0;
			int m_TileReads = 0;
			int m_FreezeTileCount = 0;
			int m_FreezeStrictlyBetween = 0;
			int m_ActorOnFreeze = 0;
			int m_TargetOnFreeze = 0;
			int m_FreezeBetween = -1;
			int m_OppositeSides = -1;
			int m_CornerFreezePair = 0;
			int m_CornerFromX = -1;
			int m_CornerFromY = -1;
			int m_CornerToX = -1;
			int m_CornerToY = -1;
			int m_CornerSideAX = -1;
			int m_CornerSideAY = -1;
			int m_CornerSideBX = -1;
			int m_CornerSideBY = -1;
			int m_DiagonalOrientation = 0;
			EAledGeometryV2Topology m_Topology = EAledGeometryV2Topology::UNKNOWN;
			EAledGeometryV2Status m_Status = EAledGeometryV2Status::SKIPPED;
		};

		// A physical sample is either the native DDNet snapshot state evolved to
		// the snapshot tick, or an exact raw object whose own anchor tick already
		// equals that snapshot tick. The object tick is kept separately because it
		// is a dead-reckoning anchor, not the physical sample time.
		struct SAledResearchPhysicalSample
		{
			bool m_Valid = false;
			bool m_Frozen = false;
			bool m_NativeSnapshotReckoned = false;
			int64_t m_SnapshotTick = -1;
			int64_t m_PhysicalTick = -1;
			int64_t m_ObjectTick = -1;
			int m_X = 0;
			int m_Y = 0;
		};

	private:
		struct SAledResearchGeometryProbeRecord
		{
			bool m_Recorded = false;
			bool m_ProbeLogged = false;
			bool m_ResultLogged = false;
			bool m_AledCandidate = false;
			bool m_AledEventQueued = false;
			bool m_AledConfirmed = false;
			bool m_CandidateLogged = false;
			bool m_ConfirmedLogged = false;
			bool m_V2ProbeLogged = false;
			bool m_V2Computed = false;
			bool m_V2IsAledGeometryValid = false;
			bool m_V2DecisionLogged = false;
			EAledGeometryV2Topology m_V2Topology = EAledGeometryV2Topology::UNKNOWN;
			EAledGeometryV2Topology m_V2Orientation = EAledGeometryV2Topology::UNKNOWN;
			EAledGeometryV2Status m_V2Status = EAledGeometryV2Status::SKIPPED;
			SAledResearchGeometryV2GridResult m_V2Result;
			int m_V2GameFreezeCount = 0;
			int m_V2FrontFreezeCount = 0;
			int m_V2SwitchFreezeCount = 0;
			std::string m_V2Reason = "UNKNOWN";
			uint64_t m_ProblemEpisodeId = 0;
			uint64_t m_RecoveryEpisodeId = 0;
			uint32_t m_ActorGeneration = 0;
			uint32_t m_TargetGeneration = 0;
			int m_ActorId = -1;
			int m_TargetId = -1;
			uint64_t m_SnapshotSequence = 0;
			int64_t m_ServerTick = -1;
			int64_t m_RestoreTick = -1;
			int64_t m_ActorSampleTick = -1;
			int64_t m_ActorPhysicalTick = -1;
			int64_t m_ActorObjectTick = -1;
			int64_t m_TargetSampleTick = -1;
			int64_t m_TargetPhysicalTick = -1;
			int64_t m_TargetObjectTick = -1;
			int64_t m_ActorTargetTickGap = -1;
			int64_t m_ActorTargetObjectTickGap = -1;
			int64_t m_TargetRestoreTickGap = -1;
			int m_ActorX = 0;
			int m_ActorY = 0;
			int m_TargetX = 0;
			int m_TargetY = 0;
			int m_ActorTileX = -1;
			int m_ActorTileY = -1;
			int m_TargetTileX = -1;
			int m_TargetTileY = -1;
			int m_ScanStartX = -1;
			int m_ScanStartY = -1;
			int m_ScanEndX = -1;
			int m_ScanEndY = -1;
			int m_TilesScanned = 0;
			int m_FreezeBetween = -1;
			int m_FreezeTileCount = 0;
			int m_GameFreezeCount = 0;
			int m_FrontFreezeCount = 0;
			int m_SwitchFreezeCount = 0;
			int m_NearestFreezeX = -1;
			int m_NearestFreezeY = -1;
			int m_BarrierAxis = 0;
			int m_ActorSide = 0;
			int m_TargetSide = 0;
			int m_OppositeSides = -1;
			int64_t m_HammerAttackTick = -1;
			int64_t m_HammerObjectTick = -1;
			int64_t m_ConfirmTick = -1;
			std::string m_Status;
			std::string m_Reason;
			std::string m_SampleSelectionReason = "UNKNOWN";
			std::string m_ActorPositionSource = "UNKNOWN";
			std::string m_TargetPositionSource = "UNKNOWN";
		};

		struct SAledHammerObservation
		{
			bool m_Valid = false;
			uint32_t m_ActorGeneration = 0;
			int64_t m_AttackTick = -1;
			int64_t m_ObjectTick = -1;
			uint64_t m_SnapshotSequence = 0;
		};

		struct SKogMapMetadataCacheEntry
		{
			std::string m_MapKey;
			SMapDifficultyMetadata m_Metadata;
		};

		struct SPendingSaveRun
		{
			SRunSnapshot m_Snapshot;
			int64_t m_Deadline = 0;
		};
		struct SLocalResetSnapshot
		{
			uint64_t m_RunId = 0;
			uint64_t m_SourceRunId = 0;
			SParticipantId m_Participant;
			int m_TeamId = 0;
			int64_t m_ServerTick = -1;
			uint64_t m_ResetMessageId = 0;
		};
		struct SRunParticipantCosmetic
		{
			SParticipantId m_Id;
			SParticipantCosmeticSnapshot m_Cosmetics;
		};
		CImpactRun m_CurrentRun;
		std::shared_ptr<CActivePersistenceQueue> m_pActivePersistenceQueue;
		bool m_HasCurrentRun = false;
		bool m_bImpactPersistenceShuttingDown = false;
		std::deque<SPendingSaveRun> m_vPendingSaveRuns;
		std::vector<SRunSnapshot> m_vHistory;
		std::vector<SRunParticipantCosmetic> m_vRunParticipantCosmetics;
		bool m_HistoryLoadSucceeded = false;
		SLocalResetSnapshot m_LocalResetSnapshot;
		SRunCompletionToast m_RunCompletionToast;
		bool m_LocalProcessingToggleInitialized = false;
		bool m_LastLocalProcessingEnabled = false;
		SLocalImpactRunEnableGate m_LocalRunEnableGate;
		std::array<bool, MAX_CLIENTS> m_aDisabledStartSeen{};
		std::array<int, MAX_CLIENTS> m_aDisabledStartTeam{};
		std::array<bool, MAX_CLIENTS> m_aDisabledDeferredStart{};
		std::array<int, MAX_CLIENTS> m_aDisabledDeferredStartTeam{};
		bool m_DisabledStartBaselinePending = false;

		SRunContext m_ServerContext;
		bool m_ServerContextKnown = false;
		bool m_EngineActive = false;
		int64_t m_LastCheckpointTime = 0;
		uint64_t m_NextRunId = 1;
		uint64_t m_NextEventId = 1;
		std::array<uint32_t, MAX_CLIENTS> m_aSlotGeneration{};
		std::array<bool, MAX_CLIENTS> m_aSlotActive{};
		std::array<int, MAX_CLIENTS> m_aSlotMissingSnapshots{};
		std::array<int64_t, MAX_CLIENTS> m_aSlotMissingSinceTick{};
		std::array<int64_t, MAX_CLIENTS> m_aSlotMissingLastTick{};
		std::array<bool, MAX_CLIENTS> m_aSlotDisconnectConfirmed{};
		std::array<bool, MAX_CLIENTS> m_aPendingResetGenerationAdvance{};
		std::array<bool, MAX_CLIENTS> m_aResetSawCharacterAbsence{};
		std::array<uint32_t, MAX_CLIENTS> m_aStartTrackingGeneration{};
		// Start observations are scoped by both slot generation and the current
		// DDNet team. A client can change teams without occupying a new slot.
		std::array<int, MAX_CLIENTS> m_aStartTrackingTeam{};
		std::array<uint32_t, MAX_CLIENTS> m_aDeferredStartGeneration{};
		std::array<int, MAX_CLIENTS> m_aDeferredStartTeam{};
		std::array<bool, MAX_CLIENTS> m_aDeferredStartObservation{};
		std::array<int64_t, MAX_CLIENTS> m_aDeferredStartTick{};
		std::array<bool, MAX_CLIENTS> m_aStartOnTile{};
		std::array<int, MAX_CLIENTS> m_aStartLastCharacterTick{};
		std::array<uint32_t, MAX_CLIENTS> m_aStartDebugGeneration{};
		std::array<int, MAX_CLIENTS> m_aLastFrameDebugState{};
		CImpactPhase2 m_Phase2;
		std::vector<SImpactGameplayMessage> m_vGameplayMessages;
		std::vector<SImpactGameplayMessage> m_vRecentNetworkMessages;
		uint64_t m_NextGameplayMessageId = 1;
		IOHANDLE m_hImpactDebugLog = nullptr;
		bool m_ImpactDebugLogOpenFailed = false;
		bool m_ImpactDebugSessionStarted = false;
		bool m_ImpactDebugEnabled = false;
		std::array<uint32_t, MAX_CLIENTS> m_aLastDebugUiFallGeneration{};
		std::array<int, MAX_CLIENTS> m_aLastDebugUiFalls{};
		std::array<int, MAX_CLIENTS> m_aLastDebugFrameTeam{};
		std::array<int, MAX_CLIENTS> m_aLastDebugFreezeState{};
		std::array<int, MAX_CLIENTS> m_aLastDebugHookKnown{};
		std::array<int, MAX_CLIENTS> m_aLastDebugHookState{};
		std::array<int, MAX_CLIENTS> m_aLastDebugHookedPlayer{};
		int m_LastDebugFrameCount = -1;
		// This pending flag covers both the explicit debug capture command and the
		// bounded automatic KoG metadata request. The automatic/manual distinction
		// is kept separately so a manual capture never consumes retry budget.
		bool m_MapInfoCapturePending = false;
		bool m_MapInfoRequestAutomatic = false;
		int m_MapInfoRequestAttempts = 0;
		int64_t m_MapInfoCaptureDeadlineTick = -1;
		int64_t m_MapInfoNextRequestTick = -1;
		bool m_MapInfoRequestExhausted = false;
		std::string m_MapInfoSessionKey;
		std::string m_MapMetadataLastDiagnosticKey;
		std::string m_KogCategoryLastDiagnosticKey;
		std::string m_KogCategoryContextLastDiagnosticKey;
		std::string m_KogDifficultyResultLastDiagnosticKey;
		std::string m_KogDifficultyUiLastDiagnosticKey;
		std::string m_KogVoteRatingLastDiagnosticKey;
		std::vector<SKogMapMetadataCacheEntry> m_vKogMapMetadataCache;
		uint64_t m_ShadowSimulationCount = 0;
		uint64_t m_ShadowSimulationTotalTicks = 0;
		std::array<std::string, MAX_CLIENTS> m_aLastAledActionLogKey;
		std::array<std::string, MAX_CLIENTS> m_aLastAledStateLogKey;
		std::array<std::string, MAX_CLIENTS> m_aLastAledRestoreLogKey;
		std::array<SAledResearchGeometryProbeRecord, MAX_CLIENTS> m_aAledGeometryProbe{};
		std::array<SAledHammerObservation, MAX_CLIENTS> m_aLastAledHammerObservation{};

		static void ConDeleteRun(IConsole::IResult *pResult, void *pUserData);
#if defined(CONF_DEBUG)
		static void ConCaptureMapInfo(IConsole::IResult *pResult, void *pUserData);
#endif
		int64_t CurrentTick() const;
		int CurrentLocalTeamId() const;
		int CountTeamMembers(int TeamId) const;
		int CountStartRosterMembers(int TeamId) const;
		void RefreshServerContext(bool *pChanged, bool *pMapChanged);
		void RefreshImpactEligibility();
		void UpdateSlotGenerations();
		void DetectStartCrossings();
		void UpdateConfirmedParticipants();
		void UpdatePhase2();
		void SetPhase2RunContext();
		void ResetStartTracking();
		bool SynchronizeLocalProcessingToggle();
		void ObserveDisabledStartLifecycle();
		void ResetDisabledStartObservations(bool BaselinePending = false);
		void EnsureRun(int TeamId);
		void ReconcileCurrentTeamRoster(int64_t Tick, const STickEventBatch *pStartBatch = nullptr);
		void CaptureParticipantCosmetics(const SParticipantId &Participant);
		void CopyParticipantCosmeticsToSnapshot(SRunSnapshot *pSnapshot) const;
		void RestoreParticipantCosmeticsFromSnapshot(const SRunSnapshot &SnapshotData);
		bool CanCollect() const;
		bool HasConfirmedStart(const STickEventBatch &Batch) const;
		bool HasStartForCurrentParticipant(const STickEventBatch &Batch) const;
		void HandleFinalizedRun();
		void HandleInterruptedRun(ERunResult Result);
		void FinalizeParticipant(const SParticipantId &Participant, int64_t Tick, bool IsReset, uint64_t ResetMessageId = 0, int ResetTeamId = 0);
		void FinalizeRunSnapshot(const SRunSnapshot &SnapshotData, bool RemoveActiveFile = true);
		void QueuePendingSaveRun(const SRunSnapshot &SnapshotData);
		void ExpirePendingSaveRuns();
		bool HistoryContainsRun(uint64_t RunId) const;
		bool AppendHistoryRun(const SRunSnapshot &SnapshotData);
		void SaveActiveCheckpoint(bool Force);
		void RemoveActiveCheckpoint();
		void SaveHistory();
		void DiscardActiveAnalysis();
		void RemoveCurrentRun();
		void ImpactDebugLog(const char *pFmt, ...);
		void WriteImpactDebugLine(const char *pLine);
		void CloseImpactDebugLog();
		void LogPhase2Debug(const SImpactPhase2Debug &Debug);
		void LogLiveFrameTransitions(const std::vector<SImpactGameplayFrame> &vFrames);
		void LogRawAuthoritativeDiagnostics(const std::vector<SImpactGameplayFrame> &vFrames);
		void LogRawHookActionObservation(const SImpactGameplayFrame &Actor, const SImpactGameplayFrame &Target);
		void RunShadowAutonomousDiagnostics(std::vector<SImpactGameplayFrame> &vFrames);
		void CaptureAledHammerObservations(const std::vector<SImpactGameplayFrame> &vFrames);
		void CaptureAledCandidate(const SImpactPhase2Debug &Debug);
		void QueueAledConfirmedEvent(const SImpactPhase2Debug &Debug, STickEventBatch &Batch);
		void CommitAledConfirmedEvents(const STickEventBatch &Batch, bool Applied);
		void LogAledCandidate(const SAledResearchGeometryProbeRecord &Record);
		void LogAledConfirmed(const SAledResearchGeometryProbeRecord &Record, int AledCountAfter);
		SAledResearchGeometryProbeRecord BuildAledResearchGeometryProbe(const SImpactPhase2Debug &Debug);
		void LogAledResearch(const SImpactPhase2Debug &Debug, const std::vector<SImpactGameplayFrame> &vFrames);
		void LogAledGeometryProbe(const SAledResearchGeometryProbeRecord &Record);
		void LogAledGeometryResult(const SAledResearchGeometryProbeRecord &Record, const SImpactPhase2Debug &Debug);
		void LogAledGeometryV2(SAledResearchGeometryProbeRecord &Record);
		void ResetDiagnosticLogDeduplication();
		void LoadKogMapMetadataCache();
		bool SaveKogMapMetadataCache() const;
		const SMapDifficultyMetadata *FindKogMapMetadata(const char *pMapName) const;
		void StoreKogMapMetadata(const char *pMapName, const SMapDifficultyMetadata &Metadata, bool Authoritative);
		void ResetMapInfoRequestState();
		void PumpMapInfoRequest();
		void MaybeRequestKogMapInfo(const SRunContext &Context, int64_t CurrentServerTick);
		void ApplyKogVoteListRating();
		void ApplyResolvedMapMetadata(const SMapDifficultyMetadata &Metadata, const char *pSource = "MAPINFO_CHAT");
		bool IsCurrentServerContext() const;
		void LogMapMetadataResolution(const char *pSource, const SRunContext &Context, int Attempt, const char *pReason = nullptr);
		void LogKogCategoryResolve(const SRunContext &Context, const char *pCandidateMap, const char *pExistingCategory, const char *pExistingSource,
			const char *pCandidateCategory, const char *pCandidateSource, const char *pStatus, const char *pReason);
		void LogKogVoteRatingLookup(const char *pMapName, int OptionsTotal, int ExactNameMatches, const char *pStatus, const char *pReason, const char *pRawOption, int Rating);
		void LogDifficultyResult(const char *pEvent, const SRunSnapshot &SnapshotData, const char *pSource, const char *pStatus, const char *pReason);

	public:
		int Sizeof() const override { return sizeof(*this); }
		void OnConsoleInit() override;
		void OnInit() override;
		void OnShutdown() override;
		void OnReset() override;
		void OnStateChange(int NewState, int OldState) override;
		void OnMapLoad() override;
		void OnUpdate() override;
		void OnNewSnapshot() override;
		void OnMessage(int Msg, void *pRawMsg) override;
		void OnMessageFromConnection(int Msg, void *pRawMsg, int Conn);
		void DebugLogRawNetworkMessage(int Msg, int Conn, bool Decoded);

		bool EngineActive() const { return m_EngineActive; }
		// The local feature toggle controls both runtime collection and its UI.
		static bool IsStatisticsUiAccessible(int AccessSetting) { return AccessSetting != 0; }
		static bool IsResetRelatedToastResult(ERunResult Result)
		{
			return Result == ERunResult::WIPED || Result == ERunResult::TEAM_RESET ||
				Result == ERunResult::ABORTED_MAP_CHANGE || Result == ERunResult::ABORTED_SERVER_CHANGE;
		}
		static bool IsSoloMapCategory(const char *pCategory)
		{
			return pCategory != nullptr && MapMetadataAsciiEquals(std::string(pCategory), "Solo");
		}
		static bool IsImpactMapEligibilityAllowed(bool ProductionNetworkEligible, bool MapEligibilityKnown, bool SoloMap)
		{
			return ProductionNetworkEligible && MapEligibilityKnown && !SoloMap;
		}
		// Runtime metadata is bound to the current server/map identity. A refresh
		// on the same identity may temporarily lack one provider, so preserve only
		// the fields that are still missing. A different identity is already built
		// from a fresh context, so it must not inherit either field from Previous.
		static void RebindMapMetadataForIdentity(const SRunContext &Previous, SRunContext *pCurrent)
		{
			if(pCurrent == nullptr)
				return;
			const bool SameIdentity = Previous.m_ServerKey == pCurrent->m_ServerKey && Previous.m_Map == pCurrent->m_Map;
			if(!SameIdentity)
				return;
			if(pCurrent->m_MapDifficulty.empty())
				pCurrent->m_MapDifficulty = Previous.m_MapDifficulty;
			if(pCurrent->m_MapStars < 0 && Previous.m_MapStars >= 0 && Previous.m_MapStars <= MAX_MAP_DIFFICULTY_STARS)
				pCurrent->m_MapStars = Previous.m_MapStars;
		}
		static bool MergeKogMapMetadata(const SMapDifficultyMetadata &Existing, const SMapDifficultyMetadata &Incoming, SMapDifficultyMetadata *pMerged)
		{
			if(pMerged == nullptr || Incoming.m_Category.empty() || !IsMapDifficultyMetadataUsable(Incoming))
				return false;
			if(!Existing.m_Category.empty() && !Incoming.m_Category.empty() && Existing.m_Category != Incoming.m_Category)
				return false;
			if(Existing.m_Stars >= 0 && Incoming.m_Stars >= 0 && Existing.m_Stars != Incoming.m_Stars)
				return false;
			*pMerged = Existing;
			if(pMerged->m_Category.empty())
				pMerged->m_Category = Incoming.m_Category;
			if(pMerged->m_Stars < 0 && Incoming.m_Stars >= 0)
				pMerged->m_Stars = Incoming.m_Stars;
			return true;
		}
		struct SKogMapMetadataResolution
		{
			SMapDifficultyMetadata m_Metadata;
			const char *m_CategorySource = "NONE";
			const char *m_RatingSource = "NONE";
		};
		// Resolve category and rating independently. Current passive metadata is
		// strongest; when it is absent, the current KoG server category describes
		// the active context more reliably than a legacy map-only cache entry. The
		// cache still supplies a known rating without allowing its category to win
		// over current category evidence.
		static SKogMapMetadataResolution ResolveKogMapMetadata(
			const SMapDifficultyMetadata &PassiveMetadata, bool HasPassiveMetadata,
			const SMapDifficultyMetadata &CachedMetadata, bool HasCachedMetadata,
			const SMapDifficultyMetadata &ServerCategoryMetadata, bool HasServerCategory)
		{
			SKogMapMetadataResolution Result;
			if(HasPassiveMetadata && !PassiveMetadata.m_Category.empty())
			{
				Result.m_Metadata.m_Category = PassiveMetadata.m_Category;
				Result.m_CategorySource = "PASSIVE_SV_MAPINFO";
			}
			else if(HasServerCategory && !ServerCategoryMetadata.m_Category.empty())
			{
				Result.m_Metadata.m_Category = ServerCategoryMetadata.m_Category;
				Result.m_CategorySource = "SERVER_CATEGORY";
			}
			else if(HasCachedMetadata && !CachedMetadata.m_Category.empty())
			{
				Result.m_Metadata.m_Category = CachedMetadata.m_Category;
				Result.m_CategorySource = "CACHE";
			}

			if(HasPassiveMetadata && PassiveMetadata.m_Stars >= 0 && PassiveMetadata.m_Stars <= MAX_MAP_DIFFICULTY_STARS)
			{
				Result.m_Metadata.m_Stars = PassiveMetadata.m_Stars;
				Result.m_RatingSource = "PASSIVE_SV_MAPINFO";
			}
			else if(HasCachedMetadata && CachedMetadata.m_Stars >= 0 && CachedMetadata.m_Stars <= MAX_MAP_DIFFICULTY_STARS)
			{
				Result.m_Metadata.m_Stars = CachedMetadata.m_Stars;
				Result.m_RatingSource = "CACHE";
			}
			return Result;
		}
		static constexpr int MAX_AUTOMATIC_MAPINFO_REQUESTS = 3;
		static bool ShouldRequestKogMapInfo(bool IsKog, const SMapDifficultyMetadata &Metadata, bool RequestPending, int Attempts, int64_t CurrentServerTick, int64_t NextRequestTick)
		{
			return IsKog && !IsMapDifficultyMetadataComplete(Metadata) && !RequestPending && Attempts < MAX_AUTOMATIC_MAPINFO_REQUESTS &&
				(NextRequestTick < 0 || CurrentServerTick >= NextRequestTick);
		}
		// ALED research is observation-only. These predicates are deliberately
		// exposed as pure guards so the no-actor/no-map cases are regression-tested
		// without constructing a live CGameClient.
		static bool IsAledResearchClientIdValid(int ClientId)
		{
			return ClientId >= 0 && ClientId < MAX_CLIENTS;
		}
		static bool IsAledResearchPositionFinite(float X, float Y)
		{
			return std::isfinite(X) && std::isfinite(Y);
		}
		static bool IsAledResearchPhysicalSampleUsable(const SAledResearchPhysicalSample &Sample, int64_t ExpectedSnapshotTick, bool RequireFrozen)
		{
			return Sample.m_Valid && Sample.m_SnapshotTick == ExpectedSnapshotTick && Sample.m_PhysicalTick == ExpectedSnapshotTick &&
				Sample.m_ObjectTick >= 0 && (!RequireFrozen || Sample.m_Frozen);
		}
		static bool SelectAledResearchPhysicalSample(
			const SAledResearchPhysicalSample &NativeSnapshotSample, const SAledResearchPhysicalSample &ExactRawSample,
			int64_t ExpectedSnapshotTick, bool RequireFrozen, SAledResearchPhysicalSample &Selected)
		{
			if(IsAledResearchPhysicalSampleUsable(NativeSnapshotSample, ExpectedSnapshotTick, RequireFrozen))
			{
				Selected = NativeSnapshotSample;
				return true;
			}
			if(IsAledResearchPhysicalSampleUsable(ExactRawSample, ExpectedSnapshotTick, RequireFrozen))
			{
				Selected = ExactRawSample;
				return true;
			}
			return false;
		}
		static bool IsAledResearchSwitchAccessValid(int SwitchNumber, int TeamId, int SwitcherCount)
		{
			return SwitchNumber >= 0 && SwitchNumber < SwitcherCount && TeamId > TEAM_FLOCK && TeamId < NUM_DDRACE_TEAMS;
		}
		// Legacy H/V gate retained for shadow/regression comparison only. Production
		// candidate capture uses the V2 gate below.
		static bool IsAledCandidateEvidenceSufficient(
			bool ActorValid, bool TargetValid, bool RecoveryValid, bool GeometryValid,
			int FreezeBetween, int OppositeSides, int BarrierAxis,
			bool HammerAssociated, int64_t HammerAttackTick, int64_t RestoreTick)
		{
			constexpr int64_t MAX_HAMMER_RESTORE_GAP = 2;
			return ActorValid && TargetValid && RecoveryValid && GeometryValid &&
				FreezeBetween == 1 && OppositeSides == 1 &&
				(BarrierAxis == 1 || BarrierAxis == 2) && HammerAssociated &&
				HammerAttackTick >= 0 && RestoreTick >= 0 && HammerAttackTick <= RestoreTick &&
				RestoreTick - HammerAttackTick <= MAX_HAMMER_RESTORE_GAP;
		}
		static EAledGeometryV2Topology AledGeometryV2ProductionOrientation(const SAledResearchGeometryV2GridResult &Result)
		{
			if(Result.m_CornerFreezePair != 0)
				return EAledGeometryV2Topology::DIAGONAL;
			if(Result.m_Topology == EAledGeometryV2Topology::VERTICAL || Result.m_Topology == EAledGeometryV2Topology::HORIZONTAL ||
				Result.m_Topology == EAledGeometryV2Topology::DIAGONAL)
				return Result.m_Topology;
			return EAledGeometryV2Topology::UNKNOWN;
		}
		static bool IsAledGeometryV2ProductionValid(const SAledResearchGeometryV2GridResult &Result)
		{
			const bool SafeSeparatingBarrier = Result.m_FreezeBetween == 1 && Result.m_OppositeSides == 1 &&
				Result.m_ActorOnFreeze == 0 && Result.m_TargetOnFreeze == 0;
			const bool RecognizedTopology = Result.m_Status == EAledGeometryV2Status::VALID &&
				(Result.m_Topology == EAledGeometryV2Topology::VERTICAL ||
					Result.m_Topology == EAledGeometryV2Topology::HORIZONTAL ||
					Result.m_Topology == EAledGeometryV2Topology::DIAGONAL);
			const bool StrongCornerTopology = Result.m_CornerFreezePair != 0;
			return SafeSeparatingBarrier && (RecognizedTopology || StrongCornerTopology);
		}
		static bool IsAledV2CandidateEvidenceSufficient(
			bool ActorValid, bool TargetValid, bool RecoveryValid, bool GeometryValid,
			bool HammerAssociated, int64_t HammerAttackTick, int64_t RestoreTick)
		{
			constexpr int64_t MAX_HAMMER_RESTORE_GAP = 2;
			return ActorValid && TargetValid && RecoveryValid && GeometryValid && HammerAssociated &&
				HammerAttackTick >= 0 && RestoreTick >= 0 && HammerAttackTick <= RestoreTick &&
				RestoreTick - HammerAttackTick <= MAX_HAMMER_RESTORE_GAP;
		}
		static bool IsAledResearchGeometryMapIndexValid(int MapIndex, int Width, int Height)
		{
			const int64_t TileCount = static_cast<int64_t>(Width) * static_cast<int64_t>(Height);
			return Width > 0 && Height > 0 && MapIndex >= 0 && static_cast<int64_t>(MapIndex) < TileCount;
		}
		static bool AnalyzeAledResearchGeometryGrid(
			int Width, int Height, int ActorTileX, int ActorTileY, int TargetTileX, int TargetTileY,
			const std::vector<std::pair<int, int>> &vFreezeTiles, int MaxTiles, SAledResearchGeometryGridResult &Result)
		{
			Result = {};
			const int64_t TileCount = static_cast<int64_t>(Width) * Height;
			if(Width <= 0 || Height <= 0 || MaxTiles <= 0 || TileCount <= 0 || TileCount > std::numeric_limits<int>::max() ||
				!IsAledResearchGeometryMapIndexValid(0, Width, Height) ||
				!IsAledResearchGeometryMapIndexValid(static_cast<int>(TileCount - 1), Width, Height))
				return false;
			const auto InBounds = [Width, Height](int64_t X, int64_t Y) {
				return X >= 0 && Y >= 0 && X < Width && Y < Height;
			};
			if(!InBounds(ActorTileX, ActorTileY) || !InBounds(TargetTileX, TargetTileY))
				return false;
			for(const auto &Tile : vFreezeTiles)
				if(!InBounds(Tile.first, Tile.second))
					return false;
			const int64_t DeltaX = static_cast<int64_t>(TargetTileX) - ActorTileX;
			const int64_t DeltaY = static_cast<int64_t>(TargetTileY) - ActorTileY;
			const int64_t AbsDeltaX = DeltaX < 0 ? -DeltaX : DeltaX;
			const int64_t AbsDeltaY = DeltaY < 0 ? -DeltaY : DeltaY;
			if(std::max(AbsDeltaX, AbsDeltaY) + 1 > MaxTiles)
				return false;
			const auto HasFreezeTile = [&vFreezeTiles](int X, int Y) {
				return std::find(vFreezeTiles.begin(), vFreezeTiles.end(), std::make_pair(X, Y)) != vFreezeTiles.end();
			};
			int64_t X = ActorTileX;
			int64_t Y = ActorTileY;
			const int64_t StepX = DeltaX < 0 ? -1 : 1;
			const int64_t StepY = DeltaY < 0 ? -1 : 1;
			int64_t Error = AbsDeltaX - AbsDeltaY;
			int MinX = 0, MaxX = 0, MinY = 0, MaxY = 0;
			bool HaveFreeze = false;
			for(int Step = 0; Step < MaxTiles; ++Step)
			{
				if(!InBounds(X, Y))
					return false;
				++Result.m_TilesScanned;
				if(HasFreezeTile(static_cast<int>(X), static_cast<int>(Y)))
				{
					if(!HaveFreeze)
					{
						Result.m_NearestFreezeX = static_cast<int>(X);
						Result.m_NearestFreezeY = static_cast<int>(Y);
						MinX = MaxX = static_cast<int>(X);
						MinY = MaxY = static_cast<int>(Y);
						HaveFreeze = true;
					}
					else
					{
						MinX = std::min(MinX, static_cast<int>(X));
						MaxX = std::max(MaxX, static_cast<int>(X));
						MinY = std::min(MinY, static_cast<int>(Y));
						MaxY = std::max(MaxY, static_cast<int>(Y));
					}
					++Result.m_FreezeTileCount;
				}
				if(X == TargetTileX && Y == TargetTileY)
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
				if(Step == MaxTiles - 1)
					return false;
			}
			if(X != TargetTileX || Y != TargetTileY)
				return false;
			if(!HaveFreeze)
			{
				Result.m_FreezeBetween = 0;
				Result.m_OppositeSides = 0;
				return true;
			}
			if(MinX == MaxX && MinY == MaxY)
			{
				if(AbsDeltaX > AbsDeltaY)
					Result.m_BarrierAxis = 2;
				else if(AbsDeltaY > AbsDeltaX)
					Result.m_BarrierAxis = 1;
				else
					Result.m_BarrierAxis = 3;
			}
			else if(MinX == MaxX)
				Result.m_BarrierAxis = 2;
			else if(MinY == MaxY)
				Result.m_BarrierAxis = 1;
			else
				Result.m_BarrierAxis = 3;
			if(Result.m_BarrierAxis == 2)
			{
				Result.m_ActorSide = ActorTileX < MinX ? 1 : ActorTileX > MaxX ? 2 : 3;
				Result.m_TargetSide = TargetTileX < MinX ? 1 : TargetTileX > MaxX ? 2 : 3;
			}
			else if(Result.m_BarrierAxis == 1)
			{
				Result.m_ActorSide = ActorTileY < MinY ? 1 : ActorTileY > MaxY ? 2 : 3;
				Result.m_TargetSide = TargetTileY < MinY ? 1 : TargetTileY > MaxY ? 2 : 3;
			}
			if(Result.m_BarrierAxis == 1 || Result.m_BarrierAxis == 2)
			{
				const bool Opposite = (Result.m_ActorSide == 1 && Result.m_TargetSide == 2) ||
					(Result.m_ActorSide == 2 && Result.m_TargetSide == 1);
				const bool KnownSides = Result.m_ActorSide != 3 && Result.m_TargetSide != 3;
				Result.m_OppositeSides = KnownSides ? (Opposite ? 1 : 0) : -1;
				Result.m_FreezeBetween = Result.m_OppositeSides;
			}
			return true;
		}
		static bool AnalyzeAledResearchGeometryV2Grid(
			int Width, int Height, int ActorTileX, int ActorTileY, int TargetTileX, int TargetTileY,
			const std::vector<std::pair<int, int>> &vFreezeTiles, int MaxTiles, int MaxTileReads,
			SAledResearchGeometryV2GridResult &Result)
		{
			Result = {};
			const int64_t TileCount = static_cast<int64_t>(Width) * Height;
			if(Width <= 0 || Height <= 0 || MaxTiles <= 0 || MaxTileReads <= 0 || TileCount <= 0 ||
				TileCount > std::numeric_limits<int>::max() || vFreezeTiles.size() > static_cast<size_t>(MaxTileReads) ||
				!IsAledResearchGeometryMapIndexValid(0, Width, Height) ||
				!IsAledResearchGeometryMapIndexValid(static_cast<int>(TileCount - 1), Width, Height))
				return false;
			const auto InBounds = [Width, Height](int64_t X, int64_t Y) {
				return X >= 0 && Y >= 0 && X < Width && Y < Height;
			};
			if(!InBounds(ActorTileX, ActorTileY) || !InBounds(TargetTileX, TargetTileY))
				return false;
			for(const auto &Tile : vFreezeTiles)
				if(!InBounds(Tile.first, Tile.second))
					return false;
			Result.m_TileReads = static_cast<int>(vFreezeTiles.size());
			Result.m_FreezeTileCount = Result.m_TileReads;
			const auto HasFreezeTile = [&vFreezeTiles](int X, int Y) {
				return std::find(vFreezeTiles.begin(), vFreezeTiles.end(), std::make_pair(X, Y)) != vFreezeTiles.end();
			};
			Result.m_ActorOnFreeze = HasFreezeTile(ActorTileX, ActorTileY) ? 1 : 0;
			Result.m_TargetOnFreeze = HasFreezeTile(TargetTileX, TargetTileY) ? 1 : 0;

			const int64_t DeltaX = static_cast<int64_t>(TargetTileX) - ActorTileX;
			const int64_t DeltaY = static_cast<int64_t>(TargetTileY) - ActorTileY;
			const int64_t AbsDeltaX = DeltaX < 0 ? -DeltaX : DeltaX;
			const int64_t AbsDeltaY = DeltaY < 0 ? -DeltaY : DeltaY;
			if(std::max(AbsDeltaX, AbsDeltaY) + 1 > MaxTiles)
				return false;
			const int64_t StepX = DeltaX < 0 ? -1 : 1;
			const int64_t StepY = DeltaY < 0 ? -1 : 1;
			int64_t X = ActorTileX;
			int64_t Y = ActorTileY;
			int64_t Error = AbsDeltaX - AbsDeltaY;
			int MinStrictFreezeX = std::numeric_limits<int>::max();
			int MaxStrictFreezeX = std::numeric_limits<int>::min();
			int MinStrictFreezeY = std::numeric_limits<int>::max();
			int MaxStrictFreezeY = std::numeric_limits<int>::min();
			std::vector<std::pair<int, int>> vStrictFreezeTiles;
			bool ReachedTarget = false;
			for(int Step = 0; Step < MaxTiles; ++Step)
			{
				if(!InBounds(X, Y))
					return false;
				++Result.m_TilesScanned;
				const int CurrentX = static_cast<int>(X);
				const int CurrentY = static_cast<int>(Y);
				if(HasFreezeTile(CurrentX, CurrentY))
				{
					const bool Endpoint = (CurrentX == ActorTileX && CurrentY == ActorTileY) ||
						(CurrentX == TargetTileX && CurrentY == TargetTileY);
					if(!Endpoint)
					{
						++Result.m_FreezeStrictlyBetween;
						MinStrictFreezeX = std::min(MinStrictFreezeX, CurrentX);
						MaxStrictFreezeX = std::max(MaxStrictFreezeX, CurrentX);
						MinStrictFreezeY = std::min(MinStrictFreezeY, CurrentY);
						MaxStrictFreezeY = std::max(MaxStrictFreezeY, CurrentY);
						vStrictFreezeTiles.emplace_back(CurrentX, CurrentY);
					}
				}
				if(CurrentX == TargetTileX && CurrentY == TargetTileY)
				{
					ReachedTarget = true;
					break;
				}
				const int64_t PreviousX = X;
				const int64_t PreviousY = Y;
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
				if(X != PreviousX && Y != PreviousY && !Result.m_ActorOnFreeze && !Result.m_TargetOnFreeze)
				{
					const int SideAX = static_cast<int>(PreviousX + StepX);
					const int SideAY = static_cast<int>(PreviousY);
					const int SideBX = static_cast<int>(PreviousX);
					const int SideBY = static_cast<int>(PreviousY + StepY);
					if(InBounds(SideAX, SideAY) && InBounds(SideBX, SideBY) &&
						HasFreezeTile(SideAX, SideAY) && HasFreezeTile(SideBX, SideBY))
					{
						Result.m_CornerFreezePair = 1;
						Result.m_CornerFromX = static_cast<int>(PreviousX);
						Result.m_CornerFromY = static_cast<int>(PreviousY);
						Result.m_CornerToX = static_cast<int>(X);
						Result.m_CornerToY = static_cast<int>(Y);
						Result.m_CornerSideAX = SideAX;
						Result.m_CornerSideAY = SideAY;
						Result.m_CornerSideBX = SideBX;
						Result.m_CornerSideBY = SideBY;
						Result.m_DiagonalOrientation = StepX == StepY ? 1 : 2;
					}
				}
			}
			if(!ReachedTarget)
				return false;
			if(Result.m_CornerFreezePair != 0)
			{
				// A corner pair is only a diagonal proof when the local snapshot does
				// not also contain unrelated freeze cells. Center-line freeze cells are
				// allowed because they can be the same local pinch viewed by the legacy
				// ray; other cells make the local topology ambiguous.
				bool ExtraFreeze = false;
				for(const auto &Tile : vFreezeTiles)
				{
					const bool IsCornerTile =
						(Tile.first == Result.m_CornerSideAX && Tile.second == Result.m_CornerSideAY) ||
						(Tile.first == Result.m_CornerSideBX && Tile.second == Result.m_CornerSideBY);
					const bool IsCenterLineTile =
						Tile.first >= std::min(ActorTileX, TargetTileX) && Tile.first <= std::max(ActorTileX, TargetTileX) &&
						Tile.second >= std::min(ActorTileY, TargetTileY) && Tile.second <= std::max(ActorTileY, TargetTileY);
					if(!IsCornerTile && !IsCenterLineTile)
					{
						ExtraFreeze = true;
						break;
					}
				}
				Result.m_FreezeBetween = 1;
				Result.m_OppositeSides = 1;
				if(ExtraFreeze)
				{
					Result.m_Topology = EAledGeometryV2Topology::AMBIGUOUS;
					Result.m_Status = EAledGeometryV2Status::AMBIGUOUS;
				}
				else
				{
					Result.m_Topology = EAledGeometryV2Topology::DIAGONAL;
					Result.m_Status = EAledGeometryV2Status::VALID;
				}
				return true;
			}

			if(Result.m_FreezeStrictlyBetween == 0)
			{
				Result.m_FreezeBetween = 0;
				Result.m_OppositeSides = 0;
				Result.m_Topology = EAledGeometryV2Topology::UNKNOWN;
				Result.m_Status = EAledGeometryV2Status::INSUFFICIENT_EVIDENCE;
				return true;
			}

			bool VerticalSupport = false;
			bool HorizontalSupport = false;
			bool DiagonalSupport = false;
			int DiagonalOrientation = 0;
			for(const auto &Tile : vStrictFreezeTiles)
			{
				VerticalSupport = VerticalSupport || HasFreezeTile(Tile.first, Tile.second - 1) || HasFreezeTile(Tile.first, Tile.second + 1);
				HorizontalSupport = HorizontalSupport || HasFreezeTile(Tile.first - 1, Tile.second) || HasFreezeTile(Tile.first + 1, Tile.second);
				if(HasFreezeTile(Tile.first - 1, Tile.second - 1) && HasFreezeTile(Tile.first + 1, Tile.second + 1))
				{
					DiagonalSupport = true;
					DiagonalOrientation = 1;
				}
				if(HasFreezeTile(Tile.first - 1, Tile.second + 1) && HasFreezeTile(Tile.first + 1, Tile.second - 1))
				{
					DiagonalSupport = true;
					DiagonalOrientation = DiagonalOrientation == 0 ? 2 : DiagonalOrientation;
				}
			}
			const bool VerticalOpposite = (ActorTileX < MinStrictFreezeX && TargetTileX > MaxStrictFreezeX) ||
				(ActorTileX > MaxStrictFreezeX && TargetTileX < MinStrictFreezeX);
			const bool HorizontalOpposite = (ActorTileY < MinStrictFreezeY && TargetTileY > MaxStrictFreezeY) ||
				(ActorTileY > MaxStrictFreezeY && TargetTileY < MinStrictFreezeY);
			if(DiagonalSupport && !VerticalSupport && !HorizontalSupport)
			{
				Result.m_Topology = EAledGeometryV2Topology::DIAGONAL;
				Result.m_FreezeBetween = 1;
				Result.m_OppositeSides = 1;
				Result.m_DiagonalOrientation = DiagonalOrientation;
				Result.m_Status = EAledGeometryV2Status::VALID;
			}
			else if((DiagonalSupport && (VerticalSupport || HorizontalSupport)) || (VerticalSupport && HorizontalSupport))
			{
				Result.m_Topology = EAledGeometryV2Topology::AMBIGUOUS;
				Result.m_FreezeBetween = 1;
				Result.m_OppositeSides = (VerticalOpposite || HorizontalOpposite) ? 1 : -1;
				Result.m_Status = EAledGeometryV2Status::AMBIGUOUS;
			}
			else if(VerticalSupport && VerticalOpposite && !HorizontalSupport)
			{
				Result.m_Topology = EAledGeometryV2Topology::VERTICAL;
				Result.m_FreezeBetween = 1;
				Result.m_OppositeSides = 1;
				Result.m_Status = EAledGeometryV2Status::VALID;
			}
			else if(HorizontalSupport && HorizontalOpposite && !VerticalSupport)
			{
				Result.m_Topology = EAledGeometryV2Topology::HORIZONTAL;
				Result.m_FreezeBetween = 1;
				Result.m_OppositeSides = 1;
				Result.m_Status = EAledGeometryV2Status::VALID;
			}
			else
			{
				Result.m_Topology = EAledGeometryV2Topology::UNKNOWN;
				Result.m_FreezeBetween = 0;
				Result.m_OppositeSides = 0;
				Result.m_Status = EAledGeometryV2Status::INSUFFICIENT_EVIDENCE;
			}
			return true;
		}
		static bool ShouldAutoSaveFinalizedRun(ERunResult Result, int64_t ActiveAnalysisTicks, int TickSpeed)
		{
			if(Result == ERunResult::FINISHED)
				return true;
			return TickSpeed > 0 && ActiveAnalysisTicks >= static_cast<int64_t>(MIN_AUTO_SAVE_SECONDS) * TickSpeed;
		}
		// The live adapter supplies the collision result from the current DDNet
		// map. Keeping the semantic classification separate makes the exact same
		// authoritative input contract testable before normalization.
		static EImpactRecoveryNeed ClassifyRecoveryNeedFromAuthoritativeState(const SImpactGameplayInput &Input, bool /*SafeCollisionPosition*/)
		{
			if(!Input.m_CurrentCharacterKnown || !Input.m_PositionKnown || !Input.m_VelocityKnown || !Input.m_SupportKnown ||
				!Input.m_FreezeKnown || !Input.m_Frozen)
				return EImpactRecoveryNeed::UNKNOWN;

			if(Input.m_DeepFrozen)
				return EImpactRecoveryNeed::EXTERNAL_REQUIRED;

			// Support and collision safety describe the current geometry only. Neither
			// one proves the counterfactual required by RecoveryNeed: what would happen
			// if all teammate assistance were removed now. The live producer therefore
			// must not turn an unsafe-looking or unsupported snapshot into
			// EXTERNAL_REQUIRED merely because autonomous evidence is missing.
			if(Input.m_AutonomousRecoveryKnown)
				return Input.m_AutonomousRecoveryPossible ? EImpactRecoveryNeed::SELF_SAFE : EImpactRecoveryNeed::EXTERNAL_REQUIRED;

			return EImpactRecoveryNeed::UNKNOWN;
		}
		// This follows the server's CCharacterCore jump rules using only fields
		// carried by the authoritative character/DDNet-character snapshots. It is
		// deliberately conservative when the current character, freeze state,
		// collision grounding, or jump-count metadata is unavailable.
		static EImpactJumpState ClassifyUsableJumpStateFromAuthoritativeState(
			bool CurrentCharacterKnown,
			bool AliveKnown,
			bool Alive,
			bool FreezeKnown,
			bool Frozen,
			bool JumpMetadataKnown,
			int Jumps,
			int Jumped,
			bool GroundedKnown,
			bool Grounded,
			bool EndlessJump)
		{
			if(!CurrentCharacterKnown || !AliveKnown || !FreezeKnown || !JumpMetadataKnown || !GroundedKnown ||
				Jumps < -1 || Jumps > 255 || Jumped < 0 || Jumped > 3)
				return EImpactJumpState::UNKNOWN;
			if(!Alive || Frozen)
				return EImpactJumpState::UNAVAILABLE;
			// The server keeps the feet light for endless-jump players. Its post-core
			// rule lets EndlessJump override both zero-jump and ground-only settings.
			if(EndlessJump)
				return EImpactJumpState::AVAILABLE;
			if(Jumps == 0)
				return EImpactJumpState::UNAVAILABLE;
			if(Grounded)
				return EImpactJumpState::AVAILABLE;
			if(Jumps == -1)
				return EImpactJumpState::UNAVAILABLE;
			return (Jumped & 2) != 0 ? EImpactJumpState::UNAVAILABLE : EImpactJumpState::AVAILABLE;
		}
		// AttackTick is the authoritative snapshot edge for an attack. Aim comes
		// from DDNetCharacter target fields; without the previous character or aim
		// metadata there is no safe way to claim that a self shot occurred.
		static bool ClassifySelfDownwardShotFromAuthoritativeState(
			bool CurrentCharacterKnown,
			bool PreviousCharacterKnown,
			bool AttackKnown,
			int AttackTick,
			int PreviousAttackTick,
			bool AimKnown,
			int TargetY,
			int Weapon,
			bool *pDownward)
		{
			if(pDownward != nullptr)
				*pDownward = false;
			if(pDownward == nullptr || !CurrentCharacterKnown || !PreviousCharacterKnown || !AttackKnown ||
				!AimKnown || AttackTick < 0 || PreviousAttackTick < 0)
				return false;
			if(AttackTick == PreviousAttackTick)
				return true;
			// Hammer and ninja attacks are not shots for Jump Reset evidence. They
			// are known non-matches rather than unknown aim/ownership.
			if(Weapon == WEAPON_HAMMER || Weapon == WEAPON_NINJA)
				return true;
			if(Weapon < WEAPON_GUN || Weapon > WEAPON_LASER)
				return false;
			// DDNet coordinates grow downwards, so a positive target Y is the exact
			// downward aim condition; no angle or magnitude threshold is introduced.
			if(pDownward != nullptr)
				*pDownward = TargetY > 0;
			return true;
		}
		// A death tile is an exact map observation, not a Fall attribution. The
		// result is only an internal candidate that still requires a separate
		// self-cause decision before raw Falls can change.
		static bool IsDeathTileOutcomeCandidate(bool CurrentCharacterKnown, bool PositionKnown, bool DeathTileKnown, bool OnDeathTile)
		{
			return CurrentCharacterKnown && PositionKnown && DeathTileKnown && OnDeathTile;
		}
		// Shared policy for the live start adapter. A current snapshot on the
		// authoritative start condition is sufficient when no previous character
		// object is available. A segment crossing is also sufficient when the
		// sparse snapshot has already advanced beyond the start tile; an
		// already-started participant is the one-shot guard.
		static bool IsStartObservationConfirmed(bool FirstReliableObservation, bool PreviousCharacterKnown, bool OnStartTile, bool CrossedStart, bool AlreadyStarted)
		{
			if(AlreadyStarted)
				return false;
			// A missing previous object is not a missing current Tee. The first
			// reliable current snapshot, or any later snapshot without a previous
			// object, may use the authoritative current start condition directly.
			// With both endpoints available, CRaceHelper's Prev -> Pos query is the
			// authoritative map-segment crossing signal, even when Pos is already
			// beyond the start tile.
			return CrossedStart || ((FirstReliableObservation || !PreviousCharacterKnown) && OnStartTile);
		}
		static bool IsParticipantSlotObserved(bool PlayerInfoKnown, bool CurrentCharacterKnown, bool PreviousCharacterKnown)
		{
			return PlayerInfoKnown || CurrentCharacterKnown || PreviousCharacterKnown;
		}
		static bool IsCurrentTeamRosterEntry(bool SlotActive, bool PlayerInfoKnown, bool Spectator, int MemberTeamId,
			int RunTeamId, uint32_t ParticipantGeneration)
		{
			return SlotActive && PlayerInfoKnown && !Spectator && RunTeamId > TEAM_FLOCK && RunTeamId < NUM_DDRACE_TEAMS &&
				MemberTeamId == RunTeamId && ParticipantGeneration != 0;
		}
		static bool IsImpactSpectatorObservation(bool PlayerInfoKnown, bool PlayerInfoSpectator, bool CurrentCharacterKnown, bool CachedSpectator)
		{
			// A current server Tee is stronger evidence than stale PlayerInfo/client
			// presentation state. Temporary roster metadata gaps must not suppress its
			// ordinary gameplay observations.
			if(CurrentCharacterKnown)
				return false;
			return PlayerInfoKnown ? PlayerInfoSpectator : CachedSpectator;
		}
		static bool IsParticipantDisconnectConfirmed(int MissingSnapshots, int64_t MissingTicks, int TickSpeed)
		{
			const int SafeTickSpeed = TickSpeed > 0 ? TickSpeed : 1;
			return MissingSnapshots >= 3 && MissingTicks >= 2LL * SafeTickSpeed;
		}
		static bool IsConfirmedTeamBoundaryMessage(const SImpactGameplayMessage &Message, int TeamId)
		{
			return Message.m_Confirmed &&
				(Message.m_Type == EImpactGameplayMessageType::TEAM_WIPE || Message.m_Type == EImpactGameplayMessageType::TEAM_RESET) &&
				(Message.m_TeamId == 0 || Message.m_TeamId == TeamId);
		}
		static bool HasConfirmedTeamBoundary(const std::vector<SImpactGameplayMessage> &vMessages, int TeamId)
		{
			return std::any_of(vMessages.begin(), vMessages.end(), [TeamId](const SImpactGameplayMessage &Message) {
				return IsConfirmedTeamBoundaryMessage(Message, TeamId);
			});
		}
		static bool IsTeamBoundaryForRecentLocalReset(const SImpactGameplayMessage &Message, int ResetTeamId, int CurrentRunTeamId, uint64_t CurrentRunId,
			uint64_t ResetSourceRunId, const SParticipantId &ResetParticipant, uint32_t CurrentGeneration,
			int64_t ResetTick, uint64_t ResetMessageId)
		{
			return Message.m_Confirmed &&
				(Message.m_Type == EImpactGameplayMessageType::TEAM_WIPE || Message.m_Type == EImpactGameplayMessageType::TEAM_RESET) &&
				ResetTeamId > 0 && ResetTeamId == CurrentRunTeamId && Message.m_TeamId == ResetTeamId &&
				ResetParticipant.IsValid() && Message.m_First == ResetParticipant.m_ClientId &&
				CurrentRunId != 0 && CurrentRunId == ResetSourceRunId &&
				CurrentGeneration == ResetParticipant.m_SlotGeneration && ResetTick >= 0 && Message.m_ServerTick == ResetTick + 1 &&
				ResetMessageId != 0 && Message.m_MessageId > ResetMessageId;
		}
		template<typename TContainer, typename TRunIdGetter>
		static size_t EraseRunSnapshotsById(TContainer &Runs, uint64_t RunId, TRunIdGetter GetRunId)
		{
			const auto It = std::find_if(Runs.begin(), Runs.end(), [RunId, GetRunId](const auto &Run) {
				return GetRunId(Run) == RunId;
			});
			if(It == Runs.end())
				return 0;
			Runs.erase(It);
			return 1;
		}
		static int ParticipantResetClientId(const SImpactGameplayMessage &Message, bool HasStartedParticipation)
		{
			if(!HasStartedParticipation || !Message.m_Confirmed)
				return -1;
			if(Message.m_Type == EImpactGameplayMessageType::KILL && Message.m_Weapon == WEAPON_SELF &&
				Message.m_Killer >= 0 && Message.m_Killer == Message.m_Victim)
				return Message.m_Victim;
			return -1;
		}
		static bool ShouldFinalizeParticipantReset(const SImpactGameplayMessage &Message, bool HasStartedParticipation, bool HasTeamBoundary)
		{
			return !HasTeamBoundary && ParticipantResetClientId(Message, HasStartedParticipation) >= 0;
		}
		static bool IsLocalRunViewFinalized(const CImpactRun &Run, int LocalClientId)
		{
			bool WasParticipant = false;
			for(const SParticipant &Participant : Run.Participants())
			{
				if(Participant.m_Id.m_ClientId != LocalClientId)
					continue;
				WasParticipant = true;
				if(!Participant.m_Finalized)
					return false;
			}
			return WasParticipant;
		}
		static bool IsValidRunTeamId(int TeamId)
		{
			return TeamId > TEAM_FLOCK && TeamId < NUM_DDRACE_TEAMS;
		}
		// Local identity follows the existing main-first, dummy-fallback rule.
		// Slot eligibility is supplied by the live snapshot adapter: active,
		// non-spectating, and backed by a current Character snapshot.
		static int LocalParticipationTeamForSlot(int TeamId, bool SlotActive, bool Spectator, bool HasCurrentCharacter)
		{
			return SlotActive && !Spectator && HasCurrentCharacter && IsValidRunTeamId(TeamId) ? TeamId : 0;
		}
		static int SelectLocalParticipationTeam(int MainTeamId, int DummyTeamId)
		{
			if(IsValidRunTeamId(MainTeamId))
				return MainTeamId;
			return IsValidRunTeamId(DummyTeamId) ? DummyTeamId : 0;
		}
		// START detection remains cross-client; only a START from the current
		// local participation team may seed this user's new Current Run.
		static int SelectNewRunStartTeam(const STickEventBatch &Batch, int LocalTeamId)
		{
			if(!IsValidRunTeamId(LocalTeamId))
				return 0;
			for(const SImpactEvent &Candidate : Batch.m_vCandidates)
				if(Candidate.m_Confirmed && Candidate.m_Type == EImpactEventType::START_CROSSING && Candidate.m_TeamId == LocalTeamId)
					return LocalTeamId;
			return 0;
		}
		static bool IsDeferredStartObservationConfirmed(bool DeferredStartObservation, bool HasValidTeam, bool AlreadyStarted)
		{
			return DeferredStartObservation && HasValidTeam && !AlreadyStarted;
		}
		// A first personal Start may be accepted only after the non-zero DDNet
		// team is confirmed. Once this run already owns a started participant,
		// a sparse roster snapshot must not invalidate that established context.
		static bool IsStartTeamMembershipConfirmed(int TeamId, int TeamMemberCount)
		{
			return TeamId > TEAM_FLOCK && TeamId < NUM_DDRACE_TEAMS && TeamMemberCount >= 2;
		}
		// A sixup PlayerInfoRace object is server-authored evidence that this
		// specific Tee is already racing. It may recover a Start missed while the
		// character object was clipped, but only for the selected non-zero team,
		// with a complete PlayerInfo/team roster and a new participant generation.
		static bool IsAuthoritativeStartedParticipant(bool PlayerInfoKnown, bool RaceStartInfoKnown, int64_t RaceStartTick, int64_t SnapshotTick,
			int TeamId, int SelectedTeamId, int TeamMemberCount, bool AlreadyStarted)
		{
			return PlayerInfoKnown && RaceStartInfoKnown && RaceStartTick >= 0 && RaceStartTick <= SnapshotTick && !AlreadyStarted &&
				TeamId > TEAM_FLOCK && TeamId < NUM_DDRACE_TEAMS && TeamId == SelectedTeamId &&
				IsStartTeamMembershipConfirmed(TeamId, TeamMemberCount);
		}
		static bool IsStartTeamContextUsable(int TeamId, int TeamMemberCount, bool ExistingParticipantContext, int CurrentRunTeamId)
		{
			if(TeamId <= TEAM_FLOCK || TeamId >= NUM_DDRACE_TEAMS)
				return false;
			if(CurrentRunTeamId > TEAM_FLOCK && TeamId != CurrentRunTeamId)
				return false;
			return IsStartTeamMembershipConfirmed(TeamId, TeamMemberCount) || ExistingParticipantContext;
		}
		// Impact gameplay is team analysis, not historical-run analysis. Use the
		// current non-finalized roster and the shared run gate so paused/singleton
		// runs cannot produce events that would later be applied to RawStats.
		static bool IsPhase2ProcessingAllowed(EAnalysisState State, int ActiveParticipantCount)
		{
			return CImpactRun::IsGameplayProcessingAllowed(State, ActiveParticipantCount);
		}
		static bool IsSupportedCommunity(const char *pCommunityId)
		{
			// The exact KoG practice allowlist is not part of the current client
			// protocol. Keep the Phase 1 engine conservative and require an
			// authoritative community id instead of guessing from server names.
			auto Matches = [](const char *pValue, const char *pExpected) {
				if(!pValue)
					return false;
				while(*pValue && *pExpected)
				{
					if(std::tolower(static_cast<unsigned char>(*pValue)) != std::tolower(static_cast<unsigned char>(*pExpected)))
						return false;
					++pValue;
					++pExpected;
				}
				return *pValue == '\0' && *pExpected == '\0';
			};
			return Matches(pCommunityId, "kog") || Matches(pCommunityId, "ego");
		}
		bool HasActiveRun() const { return m_HasCurrentRun; }
		bool HasPendingSaveRun() const { return !m_vPendingSaveRuns.empty(); }
		size_t PendingSaveRunCount() const { return m_vPendingSaveRuns.size(); }
		int PendingSaveSecondsRemaining() const;
		std::vector<uint64_t> PendingSaveRunIds();
		const SRunSnapshot *PendingSaveRun(uint64_t RunId) const;
		int PendingSaveSecondsRemaining(uint64_t RunId) const;
		const CImpactRun *CurrentRun() const { return m_HasCurrentRun ? &m_CurrentRun : nullptr; }
		const CImpactRun *LocalViewRun() const;
		bool HasLocalViewRun() const { return LocalViewRun() != nullptr; }
		const SRunSnapshot *LocalResetSnapshot() const;
		const SRunSnapshot *PendingSaveRun() const { return m_vPendingSaveRuns.empty() ? nullptr : &m_vPendingSaveRuns.front().m_Snapshot; }
		const std::vector<SRunSnapshot> &History() const { return m_vHistory; }
		const SRunCompletionToast &RunCompletionToast() const { return m_RunCompletionToast; }
		void LogDifficultyUiSource(const char *pSource, uint64_t RunId, const SRunContext &Context, const char *pReason = "UI_READ");
		int TeamMemberCount() const;
		int StartedParticipantCount() const;
		int EngagedParticipantCount() const;
		int EligibleParticipantCount() const;
		void DebugLogUiFallValue(const SParticipantId &Participant, int Falls);
		bool HasPassedDisplayGate(const SParticipantId &Participant) const
		{
			return m_HasCurrentRun && m_CurrentRun.HasPassedDisplayGate(Participant, CurrentTick());
		}

		// These methods are the Phase 1 boundary for future confirmed detectors.
		// They can also be called by deterministic replay/test code through the
		// pure CImpactRun/CReplay APIs.
		bool SubmitTickEventBatch(const STickEventBatch &Batch);
		bool SubmitStartCrossing(const SParticipantId &Participant, const char *pName, int64_t Tick, int TeamId = 0);
		bool SubmitParticipantPresence(const SParticipantId &Participant, bool Present, int64_t Tick);
		bool SubmitRunResult(ERunResult Result, int64_t Tick);

		bool SavePendingRun();
		bool SavePendingRun(uint64_t RunId);
		void DiscardPendingRun();
		bool DiscardPendingRun(uint64_t RunId);
		bool DeleteHistoryRun(uint64_t RunId);
	};
}

#endif
