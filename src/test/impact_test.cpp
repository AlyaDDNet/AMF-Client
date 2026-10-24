/*
 * Copyright (c) 2026 alya?
 * Original AMF Impact / Run Statistics tests, developed for AMF Client.
 * Licensed under licenses/AMF-Impact-LICENSE.txt.
 */
#include "test.h"

#include <base/io.h>
#include <engine/storage.h>
#include <engine/shared/uuid_manager.h>

#include <game/client/gameclient.h>
#include <game/client/components/tclient/impact/impact_impact.h>
#include <game/client/components/tclient/impact/impact_persistence.h>
#include <game/client/components/tclient/impact/impact_presentation.h>
#include <game/client/components/tclient/impact/impact_rating.h>
#include <game/client/components/tclient/impact/impact_rating_presentation.h>
#include <game/client/components/tclient/impact/impact_scoring_features.h>
#include <game/client/components/tclient/impact/impact_stability.h>
#include <game/client/components/tclient/impact/impact_replay.h>
#include <game/client/components/tclient/impact/impact_view.h>
#include <game/gamecore.h>
#include <game/client/components/tclient/impact/impact_system.h>
#include <game/client/components/tclient/impact/impact_checkpoint_policy.h>
#include <game/client/components/tclient/impact/impact_context_policy.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

using namespace AmfImpact;

TEST(ImpactPersistencePolicy, FreshDisabledStatisticsNeverScheduleActiveCheckpoints)
{
	EXPECT_FALSE(ShouldSaveActiveCheckpoint(false, true, false, false, false, true));
	EXPECT_FALSE(ShouldSaveActiveCheckpoint(false, true, false, false, true, true));
	EXPECT_FALSE(ShouldSaveActiveCheckpoint(true, false, false, false, true, true));
	EXPECT_FALSE(ShouldSaveActiveCheckpoint(true, true, true, false, true, true));
	EXPECT_TRUE(ShouldSaveActiveCheckpoint(true, true, false, false, false, true));
	EXPECT_TRUE(ShouldSaveActiveCheckpoint(true, true, false, false, true, false));
}

TEST(ImpactPersistencePolicy, EmptyPendingQueueDoesNotDeleteActiveFileEveryUpdate)
{
	EXPECT_FALSE(ShouldRemoveExpiredActiveCheckpoint(false, false, 0));
	EXPECT_FALSE(ShouldRemoveExpiredActiveCheckpoint(true, true, 0));
	EXPECT_FALSE(ShouldRemoveExpiredActiveCheckpoint(true, false, 1));
	EXPECT_TRUE(ShouldRemoveExpiredActiveCheckpoint(true, false, 0));
}

TEST(ImpactPersistencePolicy, AsyncCheckpointOperationsStayOrderedAndBounded)
{
	CLatestAsyncOperationQueue<int> Queue;
	EXPECT_TRUE(Queue.Enqueue(1)); // schedules the single drain job
	EXPECT_FALSE(Queue.Enqueue(2)); // replaces stale pending checkpoint
	int Operation = 0;
	ASSERT_TRUE(Queue.TakeNext(&Operation));
	EXPECT_EQ(Operation, 2);

	EXPECT_FALSE(Queue.Enqueue(3)); // still owned by the active drain job
	EXPECT_FALSE(Queue.Enqueue(4)); // latest desired state replaces pending state
	ASSERT_TRUE(Queue.TakeNext(&Operation));
	EXPECT_EQ(Operation, 4);
	EXPECT_FALSE(Queue.TakeNext(&Operation)); // drain relinquishes ownership
	EXPECT_TRUE(Queue.Enqueue(5)); // a later operation schedules a new job
}

TEST(ImpactNetworkLifecycle, DemoPlaybackHasNoLiveServerAddress)
{
	EXPECT_TRUE(HasLiveServerAddress(IClient::STATE_ONLINE));
	EXPECT_FALSE(HasLiveServerAddress(IClient::STATE_DEMOPLAYBACK));
	EXPECT_FALSE(HasLiveServerAddress(IClient::STATE_OFFLINE));
	EXPECT_FALSE(HasLiveServerAddress(IClient::STATE_CONNECTING));
	EXPECT_FALSE(HasLiveServerAddress(IClient::STATE_LOADING));
}

TEST(ImpactInstrumentation, RawAuthoritativeRecordsKeepCurrentAndPreviousSeparate)
{
	SRawAuthoritativeCharacterObservation Previous;
	Previous.m_Valid = true;
	Previous.m_FromRawPreviousSnapshot = true;
	Previous.m_SnapshotServerTick = 100;
	Previous.m_ObjectTick = 100;
	Previous.m_Character.m_X = 10;
	Previous.m_Character.m_Y = 20;

	SRawAuthoritativeCharacterObservation Current;
	Current.m_Valid = true;
	Current.m_FromRawCurrentSnapshot = true;
	Current.m_SnapshotServerTick = 150;
	Current.m_ObjectTick = 150;
	Current.m_Character.m_X = 30;
	Current.m_Character.m_Y = 40;

	EXPECT_TRUE(Previous.m_Valid);
	EXPECT_TRUE(Current.m_Valid);
	EXPECT_TRUE(Previous.m_FromRawPreviousSnapshot);
	EXPECT_TRUE(Current.m_FromRawCurrentSnapshot);
	EXPECT_EQ(Current.m_SnapshotServerTick - Previous.m_SnapshotServerTick, 50);
	EXPECT_EQ(Current.m_ObjectTick - Previous.m_ObjectTick, 50);
	EXPECT_EQ(Previous.m_Character.m_X, 10);
	EXPECT_EQ(Current.m_Character.m_X, 30);
	EXPECT_NE(Previous.m_Character.m_X, Current.m_Character.m_X);
}

TEST(ImpactInstrumentation, RawObservationDoesNotClaimIntraTickOrdering)
{
	// The production diagnostic record deliberately contains only snapshot
	// observations. A same-tick movement/collision/hook order is not inferred.
	const bool IntraTickOrderKnown = false;
	const bool PlatformPreexistedHook = false;
	const bool HookCreatedPlatform = false;
	EXPECT_FALSE(IntraTickOrderKnown);
	EXPECT_FALSE(PlatformPreexistedHook);
	EXPECT_FALSE(HookCreatedPlatform);
}

TEST(ImpactInstrumentation, AledResearchSelectsFreshLocalSaverTargetSample)
{
	CImpactSystem::SAledResearchPhysicalSample NativeTarget;
	NativeTarget.m_Valid = true;
	NativeTarget.m_Frozen = true;
	NativeTarget.m_NativeSnapshotReckoned = true;
	NativeTarget.m_SnapshotTick = 39806;
	NativeTarget.m_PhysicalTick = 39806;
	NativeTarget.m_ObjectTick = 39771;
	NativeTarget.m_X = 2068;
	NativeTarget.m_Y = 742;

	CImpactSystem::SAledResearchPhysicalSample StaleRawTarget;
	StaleRawTarget.m_Valid = true;
	StaleRawTarget.m_Frozen = true;
	StaleRawTarget.m_SnapshotTick = 39806;
	StaleRawTarget.m_PhysicalTick = 39771;
	StaleRawTarget.m_ObjectTick = 39771;
	StaleRawTarget.m_X = 2102;
	StaleRawTarget.m_Y = 882;

	CImpactSystem::SAledResearchPhysicalSample SelectedTarget;
	EXPECT_TRUE(CImpactSystem::SelectAledResearchPhysicalSample(NativeTarget, StaleRawTarget, 39806, true, SelectedTarget));
	EXPECT_TRUE(SelectedTarget.m_NativeSnapshotReckoned);
	EXPECT_EQ(SelectedTarget.m_PhysicalTick, 39806);
	EXPECT_EQ(SelectedTarget.m_X, 2068);
	EXPECT_EQ(SelectedTarget.m_Y, 742);
}

TEST(ImpactInstrumentation, AledResearchSelectsFreshRemoteSaverTargetSample)
{
	CImpactSystem::SAledResearchPhysicalSample NativeTarget;
	NativeTarget.m_Valid = true;
	NativeTarget.m_Frozen = true;
	NativeTarget.m_NativeSnapshotReckoned = true;
	NativeTarget.m_SnapshotTick = 54374;
	NativeTarget.m_PhysicalTick = 54374;
	NativeTarget.m_ObjectTick = 54351;
	NativeTarget.m_X = 1474;
	NativeTarget.m_Y = 741;

	CImpactSystem::SAledResearchPhysicalSample StaleRawTarget;
	StaleRawTarget.m_Valid = true;
	StaleRawTarget.m_Frozen = true;
	StaleRawTarget.m_SnapshotTick = 54374;
	StaleRawTarget.m_PhysicalTick = 54351;
	StaleRawTarget.m_ObjectTick = 54351;
	StaleRawTarget.m_X = 1490;
	StaleRawTarget.m_Y = 779;

	CImpactSystem::SAledResearchPhysicalSample SelectedTarget;
	EXPECT_TRUE(CImpactSystem::SelectAledResearchPhysicalSample(NativeTarget, StaleRawTarget, 54374, true, SelectedTarget));
	EXPECT_TRUE(SelectedTarget.m_NativeSnapshotReckoned);
	EXPECT_EQ(SelectedTarget.m_PhysicalTick, 54374);
	EXPECT_EQ(SelectedTarget.m_X, 1474);
	EXPECT_EQ(SelectedTarget.m_Y, 741);
}

TEST(ImpactInstrumentation, AledResearchRejectsStaleTargetWhenNoFreshPairExists)
{
	CImpactSystem::SAledResearchPhysicalSample NoNativeTarget;
	CImpactSystem::SAledResearchPhysicalSample StaleRawTarget;
	StaleRawTarget.m_Valid = true;
	StaleRawTarget.m_Frozen = true;
	StaleRawTarget.m_SnapshotTick = 54374;
	StaleRawTarget.m_PhysicalTick = 54351;
	StaleRawTarget.m_ObjectTick = 54351;

	CImpactSystem::SAledResearchPhysicalSample SelectedTarget;
	EXPECT_FALSE(CImpactSystem::SelectAledResearchPhysicalSample(NoNativeTarget, StaleRawTarget, 54374, true, SelectedTarget));
}

TEST(ImpactInstrumentation, AledResearchAllowsOnlyExactRawFallback)
{
	CImpactSystem::SAledResearchPhysicalSample NoNativeTarget;
	CImpactSystem::SAledResearchPhysicalSample ExactRawTarget;
	ExactRawTarget.m_Valid = true;
	ExactRawTarget.m_Frozen = true;
	ExactRawTarget.m_SnapshotTick = 700;
	ExactRawTarget.m_PhysicalTick = 700;
	ExactRawTarget.m_ObjectTick = 700;

	CImpactSystem::SAledResearchPhysicalSample SelectedTarget;
	EXPECT_TRUE(CImpactSystem::SelectAledResearchPhysicalSample(NoNativeTarget, ExactRawTarget, 700, true, SelectedTarget));
	EXPECT_FALSE(SelectedTarget.m_NativeSnapshotReckoned);
	EXPECT_EQ(SelectedTarget.m_PhysicalTick, 700);
}

static SParticipantId TestId(int ClientId, uint32_t Generation = 1, bool Dummy = false)
{
	return {ClientId, Generation, Dummy};
}

static SImpactEvent StartEvent(uint64_t EventId, const SParticipantId &Participant, int64_t Tick, const char *pName = "tee", int TeamId = 1)
{
	SImpactEvent Event;
	Event.m_EventId = EventId;
	Event.m_Type = EImpactEventType::START_CROSSING;
	Event.m_Actor = Participant;
	Event.m_TeamId = TeamId;
	Event.m_ActorName = pName;
	Event.m_StartTick = Tick;
	Event.m_ResolveTick = Tick;
	Event.m_Confirmed = true;
	return Event;
}

static SImpactEvent PresenceEvent(uint64_t EventId, const SParticipantId &Participant, bool Present, int64_t Tick)
{
	SImpactEvent Event;
	Event.m_EventId = EventId;
	Event.m_Type = EImpactEventType::PARTICIPANT_PRESENCE;
	Event.m_Actor = Participant;
	Event.m_StartTick = Tick;
	Event.m_ResolveTick = Tick;
	Event.m_Confirmed = true;
	Event.m_Present = Present;
	return Event;
}

static int CountEvents(const CImpactRun &Run, EImpactEventType Type)
{
	return static_cast<int>(std::count_if(Run.Events().begin(), Run.Events().end(), [Type](const SImpactEvent &Event) {
		return Event.m_Type == Type;
	}));
}

static int CountDebugLabels(const SImpactPhase2Output &Output, const char *pLabel)
{
	return static_cast<int>(std::count_if(Output.m_vDebug.begin(), Output.m_vDebug.end(), [pLabel](const SImpactPhase2Debug &Debug) {
		return Debug.m_DebugLabel == pLabel;
	}));
}

static int CountOutputEvents(const SImpactPhase2Output &Output, EImpactEventType Type)
{
	return static_cast<int>(std::count_if(Output.m_vEvents.begin(), Output.m_vEvents.end(), [Type](const SImpactEvent &Event) {
		return Event.m_Type == Type;
	}));
}

static const SImpactPhase2Debug *FindDebugLabel(const SImpactPhase2Output &Output, const char *pLabel)
{
	const auto It = std::find_if(Output.m_vDebug.begin(), Output.m_vDebug.end(), [pLabel](const SImpactPhase2Debug &Debug) {
		return Debug.m_DebugLabel == pLabel;
	});
	return It == Output.m_vDebug.end() ? nullptr : &*It;
}

static void StartPair(CReplay &Replay, int64_t FirstTick = 0, int64_t SecondTick = 10)
{
	const int TeamId = Replay.Run().Context().m_TeamId;
	STickEventBatch Batch;
	Batch.m_ServerTick = FirstTick;
	Batch.m_vCandidates.push_back(StartEvent(1, TestId(1), FirstTick, "A", TeamId));
	EXPECT_TRUE(Replay.Submit(Batch));
	Batch = {};
	Batch.m_ServerTick = SecondTick;
	Batch.m_vCandidates.push_back(StartEvent(2, TestId(2), SecondTick, "B", TeamId));
	EXPECT_TRUE(Replay.Submit(Batch));
}

static void StartPairWithIds(CReplay &Replay, const SParticipantId &First, const SParticipantId &Second, int64_t FirstTick = 0, int64_t SecondTick = 10)
{
	const int TeamId = Replay.Run().Context().m_TeamId;
	STickEventBatch Batch;
	Batch.m_ServerTick = FirstTick;
	Batch.m_vCandidates.push_back(StartEvent(1, First, FirstTick, "A", TeamId));
	EXPECT_TRUE(Replay.Submit(Batch));
	Batch = {};
	Batch.m_ServerTick = SecondTick;
	Batch.m_vCandidates.push_back(StartEvent(2, Second, SecondTick, "B", TeamId));
	EXPECT_TRUE(Replay.Submit(Batch));
}

static void StartThirdParticipant(CReplay &Replay, int64_t Tick = 20)
{
	STickEventBatch Batch;
	Batch.m_ServerTick = Tick;
	Batch.m_vCandidates.push_back(StartEvent(3, TestId(3), Tick, "C", Replay.Run().Context().m_TeamId));
	EXPECT_TRUE(Replay.Submit(Batch));
}

static SImpactEvent ParticipantStatsEvent(uint64_t EventId, EImpactEventType Type, const SParticipantId &Actor, int64_t Tick, const SRawStats &Delta, const SParticipantId &Target = {})
{
	SImpactEvent Event;
	Event.m_EventId = EventId;
	Event.m_Type = Type;
	Event.m_Actor = Actor;
	Event.m_Target = Target;
	Event.m_TeamId = 1;
	Event.m_StartTick = Tick;
	Event.m_ResolveTick = Tick;
	Event.m_Confirmed = true;
	Event.m_RawDelta = Delta;
	return Event;
}

static bool SubmitParticipantStats(CReplay &Replay, uint64_t EventId, EImpactEventType Type, const SParticipantId &Actor, int64_t Tick, const SRawStats &Delta, const SParticipantId &Target = {})
{
	STickEventBatch Batch;
	Batch.m_ServerTick = Tick;
	Batch.m_vCandidates.push_back(ParticipantStatsEvent(EventId, Type, Actor, Tick, Delta, Target));
	return Replay.Submit(Batch);
}

static bool FinalizeReplayParticipant(CReplay &Replay, uint64_t EventId, const SParticipantId &Participant, int64_t Tick)
{
	SImpactEvent Event;
	Event.m_EventId = EventId;
	Event.m_Type = EImpactEventType::PARTICIPANT_FINALIZED;
	Event.m_Actor = Participant;
	Event.m_TeamId = Replay.Run().Context().m_TeamId;
	Event.m_StartTick = Tick;
	Event.m_ResolveTick = Tick;
	Event.m_Confirmed = true;
	STickEventBatch Batch;
	Batch.m_ServerTick = Tick;
	Batch.m_vCandidates.push_back(Event);
	return Replay.Submit(Batch);
}

static const SParticipant *FindRunParticipant(const CImpactRun &Run, const SParticipantId &Id)
{
	const auto It = std::find_if(Run.Participants().begin(), Run.Participants().end(), [&Id](const SParticipant &Participant) {
		return Participant.m_Id == Id;
	});
	return It == Run.Participants().end() ? nullptr : &*It;
}

static const SParticipant *FindSnapshotParticipant(const SRunSnapshot &Snapshot, const SParticipantId &Id)
{
	const auto It = std::find_if(Snapshot.m_vParticipants.begin(), Snapshot.m_vParticipants.end(), [&Id](const SParticipant &Participant) {
		return Participant.m_Id == Id;
	});
	return It == Snapshot.m_vParticipants.end() ? nullptr : &*It;
}

static void StartThreeParticipantsWithStats(CReplay &Replay)
{
	StartPair(Replay, 0, 10);
	StartThirdParticipant(Replay, 20);
	SRawStats Delta;
	Delta.m_Saves = 2;
	EXPECT_TRUE(SubmitParticipantStats(Replay, 4, EImpactEventType::SAVE_CONFIRMED, TestId(1), 30, Delta));
	Delta = {};
	Delta.m_Saves = 4;
	EXPECT_TRUE(SubmitParticipantStats(Replay, 5, EImpactEventType::SAVE_CONFIRMED, TestId(2), 31, Delta));
	Delta = {};
	Delta.m_Saves = 1;
	EXPECT_TRUE(SubmitParticipantStats(Replay, 6, EImpactEventType::SAVE_CONFIRMED, TestId(3), 32, Delta));
}

static SImpactGameplayMessage ReplayTeamBoundary(int64_t Tick, int First, uint64_t MessageId = 1)
{
	SImpactGameplayMessage Message;
	Message.m_MessageId = MessageId;
	Message.m_Type = EImpactGameplayMessageType::TEAM_WIPE;
	Message.m_Confirmed = true;
	Message.m_TeamId = 1;
	Message.m_ServerTick = Tick;
	Message.m_First = First;
	return Message;
}

static bool SubmitReplayTeamBoundary(CReplay &Replay, int64_t Tick, int First, uint64_t MessageId = 1)
{
	const SImpactGameplayMessage Message = ReplayTeamBoundary(Tick, First, MessageId);
	return Replay.SubmitGameplay({}, {Message});
}

static SImpactGameplayMessage ReplaySelfKill(int ClientId, int64_t Tick)
{
	SImpactGameplayMessage Message;
	Message.m_MessageId = static_cast<uint64_t>(Tick + 1);
	Message.m_Type = EImpactGameplayMessageType::KILL;
	Message.m_Confirmed = true;
	Message.m_Killer = ClientId;
	Message.m_Victim = ClientId;
	Message.m_Weapon = WEAPON_SELF;
	Message.m_TeamId = 1;
	Message.m_ServerTick = Tick;
	return Message;
}

static SRunContext TestContext(int TeamId = 1)
{
	SRunContext Context;
	Context.m_ServerKey = "test-server";
	Context.m_ServerName = "Test server";
	Context.m_CommunityId = "kog";
	Context.m_Map = "test.map";
	Context.m_TickSpeed = 50;
	Context.m_TeamId = TeamId;
	return Context;
}

static SImpactGameplayInput ImpactInput(int ClientId, int64_t Tick, bool Frozen, bool Alive, bool Eligible, int TeamId, bool Dummy, EImpactRecoveryNeed RecoveryNeed);
static void SetPhysicalHarmfulOutcome(SImpactGameplayInput &Input);
static void SetSelfCausedHarm(SImpactGameplayInput &Input);
static SImpactGameplayMessage ImpactSignal(uint64_t MessageId, EImpactGameplayMessageType Type, int64_t Tick, const SParticipantId &Actor, const SParticipantId &Target);
static void SetHooked(SImpactGameplayInput &Input, int TargetClientId, bool MeaningfulSupport = false);
static void ReleaseHook(SImpactGameplayInput &Input);
static void ClearPhysicalHarmfulOutcome(SImpactGameplayInput &Input);
static bool SubmitLiveEquivalentInputs(CReplay &Replay, std::vector<SImpactGameplayInput> vInputs, const std::vector<SImpactGameplayMessage> &vMessages = {});

TEST(ImpactLifecycle, IndividualResetFinalizesOnlyThatParticipant)
{
	CReplay Replay(3001, TestContext());
	StartThreeParticipantsWithStats(Replay);
	ASSERT_TRUE(FinalizeReplayParticipant(Replay, 7, TestId(1), 40));
	EXPECT_EQ(Replay.Run().StartedParticipantCount(), 3);
	EXPECT_EQ(Replay.Run().ActiveParticipantCount(), 2);
	EXPECT_EQ(Replay.Run().FinalizedParticipantCount(), 1);
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::ACTIVE);
	EXPECT_EQ(Replay.Run().EligibleParticipantCount(), 2);
}

TEST(ImpactLifecycle, IndividualResetKeepsRemainingTeamRunActive)
{
	CReplay Replay(3020, TestContext());
	StartThreeParticipantsWithStats(Replay);
	ASSERT_TRUE(FinalizeReplayParticipant(Replay, 7, TestId(1), 40));
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::ACTIVE);
	EXPECT_EQ(Replay.Run().ActiveParticipantCount(), 2);
	EXPECT_GT(Replay.Run().TeamRaceTicks(140), Replay.Run().TeamRaceTicks(40));
}

TEST(ImpactLifecycle, LocalIndividualResetCreatesFrozenTeamSnapshot)
{
	CReplay Replay(3002, TestContext());
	StartThreeParticipantsWithStats(Replay);
	SRawStats Delta;
	Delta.m_Aleds = 1;
	ASSERT_TRUE(SubmitParticipantStats(Replay, 11, EImpactEventType::ALED_CONFIRMED, TestId(1), 33, Delta));
	Delta = {};
	Delta.m_Clutches = 1;
	ASSERT_TRUE(SubmitParticipantStats(Replay, 12, EImpactEventType::SAVE_CONFIRMED, TestId(1), 34, Delta));
	Delta = {};
	Delta.m_Falls = 2;
	ASSERT_TRUE(SubmitParticipantStats(Replay, 13, EImpactEventType::FALL_CONFIRMED, TestId(2), 35, Delta, TestId(1)));
	Delta = {};
	Delta.m_Aleds = 3;
	ASSERT_TRUE(SubmitParticipantStats(Replay, 14, EImpactEventType::ALED_CONFIRMED, TestId(3), 36, Delta));
	ASSERT_TRUE(FinalizeReplayParticipant(Replay, 15, TestId(1), 40));
	SRunSnapshot ResetSnapshot;
	ASSERT_TRUE(Replay.Run().MakeParticipantResetSnapshot(TestId(1), 4002, 40, 123, &ResetSnapshot));
	EXPECT_EQ(ResetSnapshot.m_RunId, 4002U);
	EXPECT_EQ(ResetSnapshot.m_Result, ERunResult::TEAM_RESET);
	EXPECT_EQ(ResetSnapshot.m_vParticipants.size(), 3U);
	ASSERT_NE(FindSnapshotParticipant(ResetSnapshot, TestId(1)), nullptr);
	ASSERT_NE(FindSnapshotParticipant(ResetSnapshot, TestId(2)), nullptr);
	ASSERT_NE(FindSnapshotParticipant(ResetSnapshot, TestId(3)), nullptr);
	EXPECT_EQ(FindSnapshotParticipant(ResetSnapshot, TestId(1))->m_RawStats.m_Saves, 2);
	EXPECT_EQ(FindSnapshotParticipant(ResetSnapshot, TestId(1))->m_RawStats.m_Aleds, 1);
	EXPECT_EQ(FindSnapshotParticipant(ResetSnapshot, TestId(1))->m_RawStats.m_Clutches, 1);
	EXPECT_EQ(FindSnapshotParticipant(ResetSnapshot, TestId(1))->m_RawStats.m_Falls, 2);
	EXPECT_EQ(FindSnapshotParticipant(ResetSnapshot, TestId(2))->m_RawStats.m_Saves, 4);
	EXPECT_EQ(FindSnapshotParticipant(ResetSnapshot, TestId(3))->m_RawStats.m_Saves, 1);
	EXPECT_EQ(FindSnapshotParticipant(ResetSnapshot, TestId(3))->m_RawStats.m_Aleds, 3);
	EXPECT_TRUE(FindSnapshotParticipant(ResetSnapshot, TestId(1))->m_Finalized);
	EXPECT_FALSE(FindSnapshotParticipant(ResetSnapshot, TestId(2))->m_Finalized);
	EXPECT_TRUE(CImpactSystem::IsLocalRunViewFinalized(Replay.Run(), 1));
	EXPECT_FALSE(CImpactSystem::IsLocalRunViewFinalized(Replay.Run(), 2));
	EXPECT_EQ(ResetSnapshot.m_ActiveAnalysisTicks, Replay.Run().Snapshot(40, 124).m_ActiveAnalysisTicks);
}

TEST(ImpactLifecycle, ResettingPlayerSnapshotDoesNotReceiveLaterTeammateStats)
{
	CReplay Replay(3003, TestContext());
	StartThreeParticipantsWithStats(Replay);
	ASSERT_TRUE(FinalizeReplayParticipant(Replay, 7, TestId(1), 40));
	SRunSnapshot ResetSnapshot;
	ASSERT_TRUE(Replay.Run().MakeParticipantResetSnapshot(TestId(1), 4003, 40, 123, &ResetSnapshot));
	SRawStats Delta;
	Delta.m_Saves = 10;
	ASSERT_TRUE(SubmitParticipantStats(Replay, 8, EImpactEventType::SAVE_CONFIRMED, TestId(2), 50, Delta));
	Delta = {};
	Delta.m_Falls = 5;
	ASSERT_TRUE(SubmitParticipantStats(Replay, 9, EImpactEventType::FALL_CONFIRMED, TestId(3), 51, Delta));
	EXPECT_EQ(FindSnapshotParticipant(ResetSnapshot, TestId(2))->m_RawStats.m_Saves, 4);
	EXPECT_EQ(FindSnapshotParticipant(ResetSnapshot, TestId(3))->m_RawStats.m_Falls, 0);
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(2))->m_RawStats.m_Saves, 14);
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(3))->m_RawStats.m_Falls, 5);
}

TEST(ImpactLifecycle, FrozenParticipantDoesNotReceiveLaterStats)
{
	CReplay Replay(3004, TestContext());
	StartThreeParticipantsWithStats(Replay);
	ASSERT_TRUE(FinalizeReplayParticipant(Replay, 7, TestId(1), 40));
	SRawStats Delta;
	Delta.m_Saves = 1;
	EXPECT_FALSE(SubmitParticipantStats(Replay, 8, EImpactEventType::SAVE_CONFIRMED, TestId(1), 50, Delta));
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(1))->m_RawStats.m_Saves, 2);
	EXPECT_TRUE(FindRunParticipant(Replay.Run(), TestId(1))->m_Finalized);
}

TEST(ImpactLifecycle, RemainingParticipantsContinueAccumulatingAfterIndividualReset)
{
	CReplay Replay(3005, TestContext());
	StartThreeParticipantsWithStats(Replay);
	ASSERT_TRUE(FinalizeReplayParticipant(Replay, 7, TestId(1), 40));
	SRawStats Delta;
	Delta.m_Saves = 4;
	ASSERT_TRUE(SubmitParticipantStats(Replay, 8, EImpactEventType::SAVE_CONFIRMED, TestId(2), 50, Delta));
	Delta = {};
	Delta.m_Falls = 3;
	ASSERT_TRUE(SubmitParticipantStats(Replay, 9, EImpactEventType::FALL_CONFIRMED, TestId(3), 51, Delta));
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(1))->m_RawStats.m_Saves, 2);
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(2))->m_RawStats.m_Saves, 8);
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(3))->m_RawStats.m_Falls, 3);
}

TEST(ImpactLifecycle, RemoteIndividualResetKeepsObserverRunActive)
{
	CReplay Replay(3021, TestContext());
	StartThreeParticipantsWithStats(Replay);
	ASSERT_TRUE(FinalizeReplayParticipant(Replay, 7, TestId(1), 40));
	EXPECT_FALSE(CImpactSystem::IsLocalRunViewFinalized(Replay.Run(), 2));
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::ACTIVE);
	SRawStats Delta;
	Delta.m_Saves = 1;
	ASSERT_TRUE(SubmitParticipantStats(Replay, 8, EImpactEventType::SAVE_CONFIRMED, TestId(2), 50, Delta));
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(2))->m_RawStats.m_Saves, 5);
}

TEST(ImpactLifecycle, RealTeamResetFinalizesWholeRun)
{
	CReplay Replay(3022, TestContext());
	StartThreeParticipantsWithStats(Replay);
	ASSERT_TRUE(SubmitReplayTeamBoundary(Replay, 300, 1));
	EXPECT_EQ(Replay.Run().Result(), ERunResult::TEAM_RESET);
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::FINALIZED);
	EXPECT_EQ(Replay.Run().StartedParticipantCount(), 3);
	EXPECT_EQ(Replay.Run().ActiveParticipantCount(), 0);
	EXPECT_EQ(Replay.Run().FinalizedParticipantCount(), 3);
	for(const SParticipant &Participant : Replay.Run().Participants())
		EXPECT_TRUE(Participant.m_Finalized);
}

TEST(ImpactLifecycle, RealTeamWipeFinalizesWholeRun)
{
	CReplay Replay(3023, TestContext());
	StartThreeParticipantsWithStats(Replay);
	ASSERT_TRUE(SubmitReplayTeamBoundary(Replay, 300, -1));
	EXPECT_EQ(Replay.Run().Result(), ERunResult::WIPED);
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::FINALIZED);
	EXPECT_EQ(Replay.Run().FinalizedParticipantCount(), 3);
}

TEST(ImpactLifecycle, RealTeamResetStopsTimerForAll)
{
	CReplay Replay(3024, TestContext());
	StartThreeParticipantsWithStats(Replay);
	ASSERT_TRUE(SubmitReplayTeamBoundary(Replay, 300, 1));
	const int64_t FrozenRaceTicks = Replay.Run().TeamRaceTicks(300);
	const int64_t FrozenAnalysisTicks = Replay.Run().ActiveAnalysisTicks(300);
	EXPECT_GT(FrozenRaceTicks, 0);
	EXPECT_EQ(Replay.Run().TeamRaceTicks(3000), FrozenRaceTicks);
	EXPECT_EQ(Replay.Run().ActiveAnalysisTicks(3000), FrozenAnalysisTicks);
}

TEST(ImpactLifecycle, RealTeamResetDoesNotEnterWaitingForStartInsideSameRun)
{
	CReplay Replay(3025, TestContext());
	StartThreeParticipantsWithStats(Replay);
	ASSERT_TRUE(SubmitReplayTeamBoundary(Replay, 300, 1));
	STickEventBatch Batch;
	Batch.m_ServerTick = 310;
	Batch.m_vCandidates.push_back(StartEvent(8, TestId(1, 2), 310, "restarted", 1));
	EXPECT_FALSE(Replay.Submit(Batch));
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::FINALIZED);
	EXPECT_EQ(Replay.Run().RunId(), 3025U);
}

TEST(ImpactLifecycle, NextStartAfterTeamResetCreatesNewRun)
{
	CReplay Finished(3026, TestContext());
	StartThreeParticipantsWithStats(Finished);
	ASSERT_TRUE(SubmitReplayTeamBoundary(Finished, 300, 1));
	const SRunSnapshot FinalSnapshot = Finished.Run().Snapshot(300, 123);
	CReplay Next(3027, TestContext());
	StartPair(Next, 310, 320);
	EXPECT_EQ(FinalSnapshot.m_State, EAnalysisState::FINALIZED);
	EXPECT_EQ(Next.Run().State(), EAnalysisState::ACTIVE);
	EXPECT_EQ(Next.Run().RunId(), 3027U);
	EXPECT_EQ(Next.Run().StartedParticipantCount(), 2);
	EXPECT_EQ(Next.Run().RawStats().m_Saves, 0);
}

TEST(ImpactLifecycle, FinalRunContainsFrozenDepartedAndFinalActivePlayers)
{
	CReplay Replay(3006, TestContext());
	StartThreeParticipantsWithStats(Replay);
	ASSERT_TRUE(FinalizeReplayParticipant(Replay, 7, TestId(1), 40));
	SRawStats Delta;
	Delta.m_Saves = 4;
	ASSERT_TRUE(SubmitParticipantStats(Replay, 8, EImpactEventType::SAVE_CONFIRMED, TestId(2), 50, Delta));
	Delta.m_Falls = 3;
	ASSERT_TRUE(SubmitParticipantStats(Replay, 9, EImpactEventType::FALL_CONFIRMED, TestId(3), 51, Delta));
	SImpactEvent Finish;
	Finish.m_EventId = 10;
	Finish.m_Type = EImpactEventType::FINISH_CONFIRMED;
	Finish.m_TeamId = 1;
	Finish.m_StartTick = 60;
	Finish.m_ResolveTick = 60;
	Finish.m_Confirmed = true;
	STickEventBatch Batch;
	Batch.m_ServerTick = 60;
	Batch.m_vCandidates.push_back(Finish);
	ASSERT_TRUE(Replay.Submit(Batch));
	const SRunSnapshot FinalSnapshot = Replay.Run().Snapshot(60, 124);
	ASSERT_EQ(FinalSnapshot.m_vParticipants.size(), 3U);
	EXPECT_TRUE(FindSnapshotParticipant(FinalSnapshot, TestId(1))->m_Finalized);
	EXPECT_TRUE(FindSnapshotParticipant(FinalSnapshot, TestId(2))->m_Finalized);
	EXPECT_TRUE(FindSnapshotParticipant(FinalSnapshot, TestId(3))->m_Finalized);
	EXPECT_EQ(FindSnapshotParticipant(FinalSnapshot, TestId(1))->m_RawStats.m_Saves, 2);
	EXPECT_EQ(FindSnapshotParticipant(FinalSnapshot, TestId(2))->m_RawStats.m_Saves, 8);
	EXPECT_EQ(FindSnapshotParticipant(FinalSnapshot, TestId(3))->m_RawStats.m_Falls, 3);
}

TEST(ImpactLifecycle, UnlockDoesNotReactivateResetParticipant)
{
	CReplay Replay(3007, TestContext());
	StartThreeParticipantsWithStats(Replay);
	ASSERT_TRUE(FinalizeReplayParticipant(Replay, 7, TestId(1), 40));
	STickEventBatch Batch;
	Batch.m_ServerTick = 45;
	Batch.m_vCandidates.push_back(StartEvent(8, TestId(1), 45, "A", 1));
	EXPECT_FALSE(Replay.Submit(Batch));
	EXPECT_EQ(Replay.Run().StartedParticipantCount(), 3);
	EXPECT_EQ(Replay.Run().ActiveParticipantCount(), 2);
	EXPECT_EQ(Replay.Run().FinalizedParticipantCount(), 1);
}

TEST(ImpactLifecycle, UnlockDoesNotWaitForFinalizedParticipant)
{
	CReplay Replay(3008, TestContext());
	StartThreeParticipantsWithStats(Replay);
	ASSERT_TRUE(FinalizeReplayParticipant(Replay, 7, TestId(1), 40));
	EXPECT_EQ(Replay.Run().StartedParticipantCount(), 3);
	EXPECT_EQ(Replay.Run().ActiveEngagedParticipantCount(), 2);
	EXPECT_EQ(Replay.Run().FinalizedParticipantCount(), 1);
}

TEST(ImpactLifecycle, UnlockDoesNotPauseValidTwoPlayerContinuation)
{
	CReplay Replay(3009, TestContext());
	StartThreeParticipantsWithStats(Replay);
	ASSERT_TRUE(FinalizeReplayParticipant(Replay, 7, TestId(1), 40));
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::ACTIVE);
EXPECT_TRUE(CImpactSystem::IsPhase2ProcessingAllowed(Replay.Run().State(), Replay.Run().ActiveParticipantCount()));
	SRawStats Delta;
	Delta.m_Saves = 1;
	EXPECT_TRUE(SubmitParticipantStats(Replay, 8, EImpactEventType::SAVE_CONFIRMED, TestId(2), 50, Delta));
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(2))->m_RawStats.m_Saves, 5);
}

TEST(ImpactLifecycle, TwoActiveParticipantsAllowImpactProcessing)
{
	CReplay Replay(3030, TestContext());
	StartPair(Replay);
	ASSERT_EQ(Replay.Run().ActiveParticipantCount(), 2);
		EXPECT_TRUE(CImpactSystem::IsPhase2ProcessingAllowed(Replay.Run().State(), Replay.Run().ActiveParticipantCount()));
	SRawStats Delta;
	Delta.m_Saves = 1;
	EXPECT_TRUE(SubmitParticipantStats(Replay, 3, EImpactEventType::SAVE_CONFIRMED, TestId(2), 30, Delta));
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(2))->m_RawStats.m_Saves, 1);
}

TEST(ImpactLifecycle, TwoToOneTransitionPausesImpactProcessing)
{
	CReplay Replay(3031, TestContext());
	StartPair(Replay);
	ASSERT_TRUE(FinalizeReplayParticipant(Replay, 3, TestId(1), 20));
	EXPECT_EQ(Replay.Run().StartedParticipantCount(), 2);
	EXPECT_EQ(Replay.Run().FinalizedParticipantCount(), 1);
	EXPECT_EQ(Replay.Run().ActiveParticipantCount(), 1);
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::PAUSED_NO_TEAM);
		EXPECT_FALSE(CImpactSystem::IsPhase2ProcessingAllowed(Replay.Run().State(), Replay.Run().ActiveParticipantCount()));
}

TEST(ImpactLifecycle, PausedRunStillFinalizesConfirmedTeamBoundary)
{
	CReplay Replay(3041, TestContext());
	StartPair(Replay);
	ASSERT_TRUE(FinalizeReplayParticipant(Replay, 3, TestId(1), 20));
	ASSERT_EQ(Replay.Run().State(), EAnalysisState::PAUSED_NO_TEAM);
	ASSERT_EQ(Replay.Run().ActiveParticipantCount(), 1);
	EXPECT_TRUE(SubmitReplayTeamBoundary(Replay, 30, 1, 4));
	EXPECT_EQ(Replay.Run().Result(), ERunResult::TEAM_RESET);
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::FINALIZED);
	EXPECT_EQ(Replay.Run().FinalizedParticipantCount(), 2);

	CReplay WipedReplay(3042, TestContext());
	StartPair(WipedReplay);
	ASSERT_TRUE(FinalizeReplayParticipant(WipedReplay, 3, TestId(1), 20));
	ASSERT_EQ(WipedReplay.Run().State(), EAnalysisState::PAUSED_NO_TEAM);
	EXPECT_TRUE(SubmitReplayTeamBoundary(WipedReplay, 30, -1, 4));
	EXPECT_EQ(WipedReplay.Run().Result(), ERunResult::WIPED);
	EXPECT_EQ(WipedReplay.Run().State(), EAnalysisState::FINALIZED);
	EXPECT_EQ(WipedReplay.Run().RawStats().m_Wipes, 0);
}

TEST(ImpactLifecycle, SoloRemainingPlayerFallIsNotCounted)
{
	CReplay Replay(3032, TestContext());
	StartPair(Replay);
	SRawStats Initial;
	Initial.m_Falls = 1;
	ASSERT_TRUE(SubmitParticipantStats(Replay, 3, EImpactEventType::FALL_CONFIRMED, TestId(2), 20, Initial, TestId(2)));
	ASSERT_TRUE(FinalizeReplayParticipant(Replay, 4, TestId(1), 30));
	ASSERT_EQ(Replay.Run().ActiveParticipantCount(), 1);
	ASSERT_EQ(Replay.Run().State(), EAnalysisState::PAUSED_NO_TEAM);

	// Exercise the real Phase 2 Fall producer while only P2 remains active.
	SImpactGameplayInput Target = ImpactInput(2, 100, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	SetPhysicalHarmfulOutcome(Target);
	SetSelfCausedHarm(Target);
	const SImpactGameplayMessage Harm = ImpactSignal(10, EImpactGameplayMessageType::CONFIRMED_HARM, 100, TestId(2), TestId(2));
	Replay.SubmitGameplayInputs({Target}, {Harm});
	Target.m_ServerTick = 101;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	Replay.SubmitGameplayInputs({Target});
	EXPECT_TRUE(Replay.LastOutput().m_vEvents.empty());
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(2))->m_RawStats.m_Falls, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 1);
}

TEST(ImpactLifecycle, SoloRemainingPlayerSaveIsNotCounted)
{
	CReplay Replay(3033, TestContext());
	StartPair(Replay);
	ASSERT_TRUE(FinalizeReplayParticipant(Replay, 3, TestId(1), 20));
	SRawStats Save;
	Save.m_Saves = 1;
	EXPECT_FALSE(SubmitParticipantStats(Replay, 4, EImpactEventType::SAVE_CONFIRMED, TestId(2), 30, Save));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(2))->m_RawStats.m_Saves, 0);
}

TEST(ImpactLifecycle, SoloRemainingPlayerAledIsNotCounted)
{
	CReplay Replay(3034, TestContext());
	StartPair(Replay);
	ASSERT_TRUE(FinalizeReplayParticipant(Replay, 3, TestId(1), 20));
	SRawStats Aled;
	Aled.m_Aleds = 1;
	EXPECT_FALSE(SubmitParticipantStats(Replay, 4, EImpactEventType::ALED_CONFIRMED, TestId(2), 30, Aled));
	EXPECT_EQ(Replay.Run().RawStats().m_Aleds, 0);
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(2))->m_RawStats.m_Aleds, 0);
}

TEST(ImpactLifecycle, SoloRemainingPlayerClutchIsNotCounted)
{
	CReplay Replay(3035, TestContext());
	StartPair(Replay);
	ASSERT_TRUE(FinalizeReplayParticipant(Replay, 3, TestId(1), 20));
	SRawStats Clutch;
	Clutch.m_Clutches = 1;
	EXPECT_FALSE(SubmitParticipantStats(Replay, 4, EImpactEventType::SAVE_CONFIRMED, TestId(2), 30, Clutch));
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 0);
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(2))->m_RawStats.m_Clutches, 0);
}

TEST(ImpactLifecycle, PausedRunRejectsOtherGameplayImpactCounters)
{
	CReplay Replay(3040, TestContext());
	StartPair(Replay);
	ASSERT_TRUE(FinalizeReplayParticipant(Replay, 3, TestId(1), 20));
	const int ConfirmedEventsBeforePauseAttempt = Replay.Run().RawStats().m_ConfirmedEvents;
	SRawStats OtherCounters;
	OtherCounters.m_Wipes = 1;
	OtherCounters.m_UsefulSaves = 1;
	OtherCounters.m_MeaningfulSkips = 1;
	OtherCounters.m_BackRecoveries = 1;
	OtherCounters.m_HarmfulEvents = 1;
	OtherCounters.m_PositiveEvents = 1;
	OtherCounters.m_UpDowns = 1;
	OtherCounters.m_MissedCatches = 1;
	OtherCounters.m_TeamMistakes = 1;
	OtherCounters.m_PreventedProgressLoss = 1;
	OtherCounters.m_RecoveryDifficultyIncreases = 1;
	EXPECT_FALSE(SubmitParticipantStats(Replay, 4, EImpactEventType::TEAM_MISTAKE_CONFIRMED, TestId(2), 30, OtherCounters));
	EXPECT_EQ(Replay.Run().RawStats().m_ConfirmedEvents, ConfirmedEventsBeforePauseAttempt);
	EXPECT_EQ(Replay.Run().RawStats().m_Wipes, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_UsefulSaves, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_MeaningfulSkips, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_BackRecoveries, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_HarmfulEvents, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_PositiveEvents, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_UpDowns, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_MissedCatches, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_TeamMistakes, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_PreventedProgressLoss, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_RecoveryDifficultyIncreases, 0);
}

TEST(ImpactLifecycle, PausedRunPreservesExistingStatsAndFrozenDepartedParticipant)
{
	CReplay Replay(3036, TestContext());
	StartPair(Replay);
	SRawStats P1Stats;
	P1Stats.m_Saves = 2;
	P1Stats.m_Falls = 1;
	ASSERT_TRUE(SubmitParticipantStats(Replay, 3, EImpactEventType::SAVE_CONFIRMED, TestId(1), 20, P1Stats));
	SRawStats P2Stats;
	P2Stats.m_Saves = 4;
	P2Stats.m_Falls = 2;
	P2Stats.m_Aleds = 1;
	P2Stats.m_Clutches = 1;
	ASSERT_TRUE(SubmitParticipantStats(Replay, 4, EImpactEventType::SAVE_CONFIRMED, TestId(2), 21, P2Stats));
	ASSERT_TRUE(FinalizeReplayParticipant(Replay, 5, TestId(1), 30));

	SRawStats Later;
	Later.m_Saves = 1;
	Later.m_Falls = 1;
	Later.m_Aleds = 1;
	Later.m_Clutches = 1;
	EXPECT_FALSE(SubmitParticipantStats(Replay, 6, EImpactEventType::SAVE_CONFIRMED, TestId(2), 40, Later, TestId(2)));
	const SParticipant *pP1 = FindRunParticipant(Replay.Run(), TestId(1));
	const SParticipant *pP2 = FindRunParticipant(Replay.Run(), TestId(2));
	ASSERT_NE(pP1, nullptr);
	ASSERT_NE(pP2, nullptr);
	EXPECT_TRUE(pP1->m_Finalized);
	EXPECT_EQ(pP1->m_RawStats.m_Saves, 2);
	EXPECT_EQ(pP1->m_RawStats.m_Falls, 1);
	EXPECT_EQ(pP2->m_RawStats.m_Saves, 4);
	EXPECT_EQ(pP2->m_RawStats.m_Falls, 2);
	EXPECT_EQ(pP2->m_RawStats.m_Aleds, 1);
	EXPECT_EQ(pP2->m_RawStats.m_Clutches, 1);
}

TEST(ImpactLifecycle, HistoricalCountTwoDoesNotPermitProcessingWhenActiveCountOne)
{
	CReplay Replay(3037, TestContext());
	StartPair(Replay);
	ASSERT_TRUE(FinalizeReplayParticipant(Replay, 3, TestId(1), 20));
	EXPECT_EQ(Replay.Run().StartedParticipantCount(), 2);
	EXPECT_EQ(Replay.Run().FinalizedParticipantCount(), 1);
	EXPECT_EQ(Replay.Run().ActiveParticipantCount(), 1);
	EXPECT_FALSE(CImpactSystem::IsPhase2ProcessingAllowed(Replay.Run().State(), Replay.Run().ActiveParticipantCount()));
}

TEST(ImpactLifecycle, SecondValidActiveParticipantResumesProcessing)
{
	CReplay Replay(3038, TestContext());
	StartPair(Replay);
	ASSERT_TRUE(FinalizeReplayParticipant(Replay, 3, TestId(1), 20));
	STickEventBatch Start;
	Start.m_ServerTick = 30;
	Start.m_vCandidates.push_back(StartEvent(4, TestId(3), 30, "C", 1));
	ASSERT_TRUE(Replay.Submit(Start));
	EXPECT_EQ(Replay.Run().ActiveParticipantCount(), 2);
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::ACTIVE);
	EXPECT_TRUE(CImpactSystem::IsPhase2ProcessingAllowed(Replay.Run().State(), Replay.Run().ActiveParticipantCount()));
	SRawStats Save;
	Save.m_Saves = 1;
	EXPECT_TRUE(SubmitParticipantStats(Replay, 5, EImpactEventType::SAVE_CONFIRMED, TestId(2), 40, Save));
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(2))->m_RawStats.m_Saves, 1);
}

TEST(ImpactLifecycle, EventsDuringPauseAreNotAppliedRetroactively)
{
	CReplay Replay(3039, TestContext());
	StartPair(Replay);
	ASSERT_TRUE(FinalizeReplayParticipant(Replay, 3, TestId(1), 20));
	SRawStats DuringPause;
	DuringPause.m_Falls = 1;
	EXPECT_FALSE(SubmitParticipantStats(Replay, 4, EImpactEventType::FALL_CONFIRMED, TestId(2), 30, DuringPause, TestId(2)));
	ASSERT_EQ(FindRunParticipant(Replay.Run(), TestId(2))->m_RawStats.m_Falls, 0);

	STickEventBatch Start;
	Start.m_ServerTick = 40;
	Start.m_vCandidates.push_back(StartEvent(5, TestId(3), 40, "C", 1));
	ASSERT_TRUE(Replay.Submit(Start));
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(2))->m_RawStats.m_Falls, 0);
	SRawStats AfterResume;
	AfterResume.m_Falls = 1;
	EXPECT_TRUE(SubmitParticipantStats(Replay, 6, EImpactEventType::FALL_CONFIRMED, TestId(2), 50, AfterResume, TestId(2)));
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(2))->m_RawStats.m_Falls, 1);
}

TEST(ImpactLifecycle, FullServerDisconnectFreezesLastKnownStats)
{
	CReplay Replay(3010, TestContext());
	StartThreeParticipantsWithStats(Replay);
	ASSERT_TRUE(FinalizeReplayParticipant(Replay, 7, TestId(1), 40));
	SRawStats Delta;
	Delta.m_Saves = 1;
	EXPECT_FALSE(SubmitParticipantStats(Replay, 8, EImpactEventType::SAVE_CONFIRMED, TestId(1), 50, Delta));
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(1))->m_RawStats.m_Saves, 2);
}

TEST(ImpactLifecycle, FullyDisconnectedPlayerRemainsInHistoricalRoster)
{
	CReplay Replay(3011, TestContext());
	StartThreeParticipantsWithStats(Replay);
	ASSERT_TRUE(FinalizeReplayParticipant(Replay, 7, TestId(1), 40));
	const SRunSnapshot Snapshot = Replay.Run().Snapshot(40, 124);
	EXPECT_EQ(Snapshot.m_vParticipants.size(), 3U);
	EXPECT_EQ(Replay.Run().StartedParticipantCount(), 3);
	EXPECT_EQ(Replay.Run().ActiveParticipantCount(), 2);
	EXPECT_EQ(Replay.Run().FinalizedParticipantCount(), 1);
}

TEST(ImpactLifecycle, FullyDisconnectedPlayerExcludedFromFutureEvents)
{
	CReplay Replay(3012, TestContext());
	StartThreeParticipantsWithStats(Replay);
	ASSERT_TRUE(FinalizeReplayParticipant(Replay, 7, TestId(1), 40));
	SRawStats Delta;
	Delta.m_Saves = 1;
	EXPECT_FALSE(SubmitParticipantStats(Replay, 8, EImpactEventType::SAVE_CONFIRMED, TestId(2), 50, Delta, TestId(1)));
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(2))->m_RawStats.m_Saves, 4);
}

TEST(ImpactLifecycle, FinalizedParticipantCannotReceiveFutureActionAttribution)
{
	CReplay Replay(3017, TestContext());
	StartThreeParticipantsWithStats(Replay);
	ASSERT_TRUE(FinalizeReplayParticipant(Replay, 7, TestId(1), 40));
	SRawStats Delta;
	Delta.m_Saves = 1;
	Delta.m_Clutches = 1;
	EXPECT_FALSE(SubmitParticipantStats(Replay, 8, EImpactEventType::SAVE_CONFIRMED, TestId(1), 50, Delta));
	Delta = {};
	Delta.m_Aleds = 1;
	EXPECT_FALSE(SubmitParticipantStats(Replay, 9, EImpactEventType::ALED_CONFIRMED, TestId(1), 51, Delta));
	Delta = {};
	Delta.m_Falls = 1;
	EXPECT_FALSE(SubmitParticipantStats(Replay, 10, EImpactEventType::FALL_CONFIRMED, TestId(2), 52, Delta, TestId(1)));
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(1))->m_RawStats.m_Saves, 2);
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(1))->m_RawStats.m_Clutches, 0);
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(1))->m_RawStats.m_Aleds, 0);
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(1))->m_RawStats.m_Falls, 0);
}

TEST(ImpactLifecycle, TemporaryInactiveButTeeStillExistsDoesNotFreeze)
{
	CReplay Replay(3013, TestContext());
	StartThreeParticipantsWithStats(Replay);
	EXPECT_TRUE(CImpactSystem::IsParticipantSlotObserved(false, true, false));
	EXPECT_FALSE(CImpactSystem::IsImpactSpectatorObservation(false, false, true, true));
	EXPECT_FALSE(CImpactSystem::IsParticipantDisconnectConfirmed(0, 0, 50));
	EXPECT_FALSE(Replay.Run().IsParticipantFinalized(TestId(1)));
}

TEST(ImpactLifecycle, TemporaryInactiveTeeStillReceivesNormalStats)
{
	CReplay Replay(3014, TestContext());
	StartThreeParticipantsWithStats(Replay);
	STickEventBatch PresenceBatch;
	PresenceBatch.m_ServerTick = 40;
	PresenceBatch.m_vCandidates.push_back(PresenceEvent(7, TestId(1), false, 40));
	ASSERT_TRUE(Replay.Submit(PresenceBatch));
	SRawStats Delta;
	Delta.m_Aleds = 1;
	EXPECT_TRUE(SubmitParticipantStats(Replay, 8, EImpactEventType::ALED_CONFIRMED, TestId(1), 41, Delta));
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(1))->m_RawStats.m_Aleds, 1);
	EXPECT_FALSE(FindRunParticipant(Replay.Run(), TestId(1))->m_Finalized);
}

TEST(ImpactLifecycle, TemporaryInactivePlayerCanReturnToSameRun)
{
	CReplay Replay(3015, TestContext());
	StartThreeParticipantsWithStats(Replay);
	STickEventBatch Batch;
	Batch.m_ServerTick = 40;
	Batch.m_vCandidates.push_back(PresenceEvent(7, TestId(1), false, 40));
	ASSERT_TRUE(Replay.Submit(Batch));
	Batch = {};
	Batch.m_ServerTick = 50;
	Batch.m_vCandidates.push_back(PresenceEvent(8, TestId(1), true, 50));
	ASSERT_TRUE(Replay.Submit(Batch));
	SRawStats Delta;
	Delta.m_Aleds = 1;
	ASSERT_TRUE(SubmitParticipantStats(Replay, 9, EImpactEventType::ALED_CONFIRMED, TestId(1), 51, Delta));
	EXPECT_EQ(Replay.Run().StartedParticipantCount(), 3);
	EXPECT_EQ(Replay.Run().ActiveParticipantCount(), 3);
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(1))->m_RawStats.m_Aleds, 1);
}

TEST(ImpactLifecycle, ShortTemporaryPlayerAbsenceDoesNotCountAsDisconnect)
{
	EXPECT_FALSE(CImpactSystem::IsParticipantDisconnectConfirmed(2, 100, 50));
	EXPECT_FALSE(CImpactSystem::IsParticipantDisconnectConfirmed(3, 99, 50));
	EXPECT_TRUE(CImpactSystem::IsParticipantDisconnectConfirmed(3, 100, 50));
}

TEST(ImpactLifecycle, Under5MinResetUsesExisting60SecondSaveFlow)
{
	EXPECT_FALSE(CImpactSystem::ShouldAutoSaveFinalizedRun(ERunResult::TEAM_RESET, 5LL * 60 * 50 - 1, 50));
	EXPECT_EQ(SHORT_RUN_DECISION_SECONDS, 60);
}

TEST(ImpactLifecycle, Over5MinResetUsesExistingAutoSaveFlow)
{
	EXPECT_TRUE(CImpactSystem::ShouldAutoSaveFinalizedRun(ERunResult::TEAM_RESET, 5LL * 60 * 50, 50));
}

TEST(ImpactLifecycle, NewParticipationDoesNotReuseOldFrozenState)
{
	CReplay Replay(3016, TestContext());
	StartThreeParticipantsWithStats(Replay);
	ASSERT_TRUE(FinalizeReplayParticipant(Replay, 7, TestId(1), 40));
	const SParticipantId Rejoined = TestId(1, 2);
	STickEventBatch Batch;
	Batch.m_ServerTick = 50;
	Batch.m_vCandidates.push_back(StartEvent(8, Rejoined, 50, "A-returned", 1));
	ASSERT_TRUE(Replay.Submit(Batch));
	SRawStats Delta;
	Delta.m_Saves = 1;
	ASSERT_TRUE(SubmitParticipantStats(Replay, 9, EImpactEventType::SAVE_CONFIRMED, Rejoined, 51, Delta));
	EXPECT_EQ(Replay.Run().ActiveParticipantCount(), 3);
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(1))->m_RawStats.m_Saves, 2);
	ASSERT_NE(FindRunParticipant(Replay.Run(), Rejoined), nullptr);
	EXPECT_EQ(FindRunParticipant(Replay.Run(), Rejoined)->m_RawStats.m_Saves, 1);
}

TEST(ImpactLifecycle, ResetDetectionDistinguishesIndividualFromTeamBoundary)
{
	SImpactGameplayMessage Message;
	Message.m_Confirmed = true;
	Message.m_Type = EImpactGameplayMessageType::KILL;
	Message.m_Killer = 1;
	Message.m_Victim = 1;
	Message.m_Weapon = WEAPON_SELF;
	EXPECT_EQ(CImpactSystem::ParticipantResetClientId(Message, true), 1);
	EXPECT_FALSE(CImpactSystem::IsConfirmedTeamBoundaryMessage(Message, 1));
	const SImpactGameplayMessage IndividualReset = Message;
	Message = {};
	Message.m_Confirmed = true;
	Message.m_Type = EImpactGameplayMessageType::TEAM_WIPE;
	Message.m_TeamId = 1;
	Message.m_First = 2;
	EXPECT_TRUE(CImpactSystem::IsConfirmedTeamBoundaryMessage(Message, 1));
	EXPECT_TRUE(CImpactSystem::HasConfirmedTeamBoundary({IndividualReset, Message}, 1));
	EXPECT_EQ(CImpactSystem::ParticipantResetClientId(Message, true), -1);
	EXPECT_EQ(CImpactSystem::ParticipantResetClientId(Message, false), -1);
	Message.m_First = -1;
	EXPECT_TRUE(CImpactSystem::IsConfirmedTeamBoundaryMessage(Message, 1));
	EXPECT_EQ(CImpactSystem::ParticipantResetClientId(Message, true), -1);
	EXPECT_FALSE(CImpactSystem::IsConfirmedTeamBoundaryMessage(Message, 2));
	Message.m_Confirmed = false;
	EXPECT_FALSE(CImpactSystem::IsConfirmedTeamBoundaryMessage(Message, 1));
}

TEST(ImpactLifecycle, SeparatedSelfKillThenTeamBoundaryDoesNotDoubleFinalize)
{
	auto VerifySeparateBatches = [](uint64_t RunId, int64_t ResetTick, bool ExpectAutoSave) {
		CReplay Replay(RunId, TestContext());
		StartThreeParticipantsWithStats(Replay);

		// Batch N: the production adapter recognizes a confirmed individual self-KILL
		// and finalizes that participant before making its local reset snapshot.
		const SImpactGameplayMessage SelfKill = ReplaySelfKill(1, ResetTick);
		ASSERT_EQ(CImpactSystem::ParticipantResetClientId(SelfKill, true), 1);
		ASSERT_FALSE(CImpactSystem::IsConfirmedTeamBoundaryMessage(SelfKill, 1));
		ASSERT_FALSE(Replay.SubmitGameplay({}, {SelfKill}));
		ASSERT_TRUE(FinalizeReplayParticipant(Replay, 100, TestId(1), ResetTick));
		ASSERT_EQ(Replay.Run().State(), EAnalysisState::ACTIVE);
		ASSERT_EQ(Replay.Run().FinalizedParticipantCount(), 1);

		SRunSnapshot IndividualSnapshot;
		ASSERT_TRUE(Replay.Run().MakeParticipantResetSnapshot(TestId(1), RunId + 10000, ResetTick, 123, &IndividualSnapshot));
		ASSERT_EQ(IndividualSnapshot.m_vParticipants.size(), 3U);
		ASSERT_EQ(IndividualSnapshot.m_Result, ERunResult::TEAM_RESET);
		const bool IndividualAutoSave = CImpactSystem::ShouldAutoSaveFinalizedRun(
			IndividualSnapshot.m_Result, IndividualSnapshot.m_ActiveAnalysisTicks, IndividualSnapshot.m_Context.m_TickSpeed);
		ASSERT_EQ(IndividualAutoSave, ExpectAutoSave);
		struct STestPendingRun
		{
			SRunSnapshot m_Snapshot;
		};
		std::vector<STestPendingRun> PendingSnapshots;
		std::vector<SRunSnapshot> AutoSavedSnapshots;
		SRunSnapshot UnrelatedPendingSnapshot = IndividualSnapshot;
		UnrelatedPendingSnapshot.m_RunId = RunId + 20000;
		PendingSnapshots.push_back({UnrelatedPendingSnapshot});
		SRunSnapshot UnrelatedSavedSnapshot = IndividualSnapshot;
		UnrelatedSavedSnapshot.m_RunId = RunId + 30000;
		AutoSavedSnapshots.push_back(UnrelatedSavedSnapshot);
		if(IndividualAutoSave)
			AutoSavedSnapshots.push_back(IndividualSnapshot);
		else
			PendingSnapshots.push_back({IndividualSnapshot});
		const auto ReconcileProvisionalSnapshot = [&](const SImpactGameplayMessage &Boundary, int BoundaryTeamId,
			uint64_t CurrentRunId, uint32_t CurrentGeneration) {
			if(!CImpactSystem::IsTeamBoundaryForRecentLocalReset(Boundary, SelfKill.m_TeamId, BoundaryTeamId, CurrentRunId, RunId,
				TestId(1), CurrentGeneration, ResetTick, SelfKill.m_MessageId))
				return size_t{0};
			const size_t RemovedPending = CImpactSystem::EraseRunSnapshotsById(PendingSnapshots, IndividualSnapshot.m_RunId,
				[](const STestPendingRun &Pending) { return Pending.m_Snapshot.m_RunId; });
			const size_t RemovedSaved = CImpactSystem::EraseRunSnapshotsById(AutoSavedSnapshots, IndividualSnapshot.m_RunId,
				[](const SRunSnapshot &Snapshot) { return Snapshot.m_RunId; });
			return RemovedPending + RemovedSaved;
		};

		// Batch N+1: the server confirms that the same reset action was team-wide.
		const SImpactGameplayMessage TeamBoundary = ReplayTeamBoundary(
			ResetTick + 1, 1, static_cast<uint64_t>(ResetTick + 2));
		const SParticipantId ResetParticipant = TestId(1);
		EXPECT_FALSE(CImpactSystem::IsTeamBoundaryForRecentLocalReset(TeamBoundary, 2, 1, RunId, RunId,
			ResetParticipant, ResetParticipant.m_SlotGeneration, ResetTick, SelfKill.m_MessageId));
		EXPECT_EQ(ReconcileProvisionalSnapshot(TeamBoundary, 1, RunId, ResetParticipant.m_SlotGeneration + 1), 0U);
		EXPECT_EQ(ReconcileProvisionalSnapshot(TeamBoundary, 1, RunId + 1, ResetParticipant.m_SlotGeneration), 0U);
		EXPECT_EQ(ReconcileProvisionalSnapshot(TeamBoundary, 2, RunId, ResetParticipant.m_SlotGeneration), 0U);
		SImpactGameplayMessage OtherInitiatorBoundary = TeamBoundary;
		OtherInitiatorBoundary.m_First = 2;
		EXPECT_EQ(ReconcileProvisionalSnapshot(OtherInitiatorBoundary, 1, RunId, ResetParticipant.m_SlotGeneration), 0U);
		SImpactGameplayMessage OtherTeamBoundary = TeamBoundary;
		OtherTeamBoundary.m_TeamId = 2;
		EXPECT_EQ(ReconcileProvisionalSnapshot(OtherTeamBoundary, 1, RunId, ResetParticipant.m_SlotGeneration), 0U);
		EXPECT_EQ(ReconcileProvisionalSnapshot(ReplayTeamBoundary(ResetTick + 3, 1, static_cast<uint64_t>(ResetTick + 4)),
			1, RunId, ResetParticipant.m_SlotGeneration), 0U);
		const uint64_t SupersededRunId = IndividualSnapshot.m_RunId;
		const uint64_t UnrelatedPendingRunId = RunId + 20000;
		const uint64_t UnrelatedSavedRunId = RunId + 30000;
		const auto CountRunId = [](const auto &Runs, uint64_t CountedRunId, auto GetRunId) {
			return std::count_if(Runs.begin(), Runs.end(), [CountedRunId, GetRunId](const auto &Run) {
				return GetRunId(Run) == CountedRunId;
			});
		};
		EXPECT_EQ(CountRunId(PendingSnapshots, SupersededRunId, [](const STestPendingRun &Pending) { return Pending.m_Snapshot.m_RunId; }) +
			CountRunId(AutoSavedSnapshots, SupersededRunId, [](const SRunSnapshot &Snapshot) { return Snapshot.m_RunId; }), 1);
		EXPECT_EQ(CountRunId(PendingSnapshots, SupersededRunId, [](const STestPendingRun &Pending) { return Pending.m_Snapshot.m_RunId; }), IndividualAutoSave ? 0 : 1);
		EXPECT_EQ(CountRunId(AutoSavedSnapshots, SupersededRunId, [](const SRunSnapshot &Snapshot) { return Snapshot.m_RunId; }), IndividualAutoSave ? 1 : 0);
		EXPECT_EQ(CountRunId(PendingSnapshots, UnrelatedPendingRunId, [](const STestPendingRun &Pending) { return Pending.m_Snapshot.m_RunId; }), 1);
		EXPECT_EQ(CountRunId(AutoSavedSnapshots, UnrelatedSavedRunId, [](const SRunSnapshot &Snapshot) { return Snapshot.m_RunId; }), 1);
		EXPECT_EQ(ReconcileProvisionalSnapshot(TeamBoundary, 1, RunId, ResetParticipant.m_SlotGeneration), 1U);
		ASSERT_TRUE(SubmitReplayTeamBoundary(Replay, ResetTick + 1, 1, static_cast<uint64_t>(ResetTick + 2)));
		ASSERT_EQ(Replay.Run().State(), EAnalysisState::FINALIZED);
		ASSERT_EQ(Replay.Run().Result(), ERunResult::TEAM_RESET);
		ASSERT_EQ(Replay.Run().FinalizedParticipantCount(), 3);
		const int64_t FrozenTimer = Replay.Run().TeamRaceTicks(ResetTick + 1);
		EXPECT_EQ(Replay.Run().TeamRaceTicks(ResetTick + 1000), FrozenTimer);

		const SRunSnapshot FinalTeamSnapshot = Replay.Run().Snapshot(ResetTick + 1000, 456);
		EXPECT_EQ(FinalTeamSnapshot.m_Result, ERunResult::TEAM_RESET);
		EXPECT_EQ(FinalTeamSnapshot.m_vParticipants.size(), 3U);
		const bool TeamAutoSave = CImpactSystem::ShouldAutoSaveFinalizedRun(
			FinalTeamSnapshot.m_Result, FinalTeamSnapshot.m_ActiveAnalysisTicks, FinalTeamSnapshot.m_Context.m_TickSpeed);
		EXPECT_EQ(TeamAutoSave, ExpectAutoSave);
		// Route the final team snapshot after the correlated provisional result was removed.
		if(TeamAutoSave)
			AutoSavedSnapshots.push_back(FinalTeamSnapshot);
		else
			PendingSnapshots.push_back({FinalTeamSnapshot});

		const auto PendingFinalCount = std::count_if(PendingSnapshots.begin(), PendingSnapshots.end(), [RunId](const STestPendingRun &Pending) {
			return Pending.m_Snapshot.m_RunId == RunId;
		});
		const auto SavedFinalCount = std::count_if(AutoSavedSnapshots.begin(), AutoSavedSnapshots.end(), [RunId](const SRunSnapshot &Snapshot) {
			return Snapshot.m_RunId == RunId;
		});
		EXPECT_EQ(PendingFinalCount + SavedFinalCount, 1);
		EXPECT_EQ(PendingFinalCount, ExpectAutoSave ? 0 : 1);
		EXPECT_EQ(SavedFinalCount, ExpectAutoSave ? 1 : 0);
		EXPECT_EQ(PendingSnapshots.size(), ExpectAutoSave ? 1U : 2U);
		EXPECT_EQ(AutoSavedSnapshots.size(), ExpectAutoSave ? 2U : 1U);
		const SRunSnapshot &FinalizedSnapshot = TeamAutoSave ? AutoSavedSnapshots.back() : PendingSnapshots.back().m_Snapshot;
		EXPECT_EQ(FinalizedSnapshot.m_RunId, RunId);
		EXPECT_EQ(std::count_if(FinalizedSnapshot.m_vParticipants.begin(), FinalizedSnapshot.m_vParticipants.end(), [](const SParticipant &Participant) {
			return Participant.m_Finalized;
		}), 3);
		ASSERT_NE(FindSnapshotParticipant(FinalizedSnapshot, TestId(1)), nullptr);
		EXPECT_EQ(FindSnapshotParticipant(FinalizedSnapshot, TestId(1))->m_RawStats.m_Saves, 2);
		ASSERT_NE(FindSnapshotParticipant(FinalizedSnapshot, TestId(2)), nullptr);
		EXPECT_EQ(FindSnapshotParticipant(FinalizedSnapshot, TestId(2))->m_RawStats.m_Saves, 4);
		ASSERT_NE(FindSnapshotParticipant(FinalizedSnapshot, TestId(3)), nullptr);
		EXPECT_EQ(FindSnapshotParticipant(FinalizedSnapshot, TestId(3))->m_RawStats.m_Saves, 1);
	};

	VerifySeparateBatches(3050, 40, false);
	VerifySeparateBatches(3051, 15260, true);

	CReplay SameBatch(3052, TestContext());
	StartThreeParticipantsWithStats(SameBatch);
	const SImpactGameplayMessage SameTickSelfKill = ReplaySelfKill(1, 40);
	const SImpactGameplayMessage SameTickTeamBoundary = ReplayTeamBoundary(40, 1, SameTickSelfKill.m_MessageId + 1);
	EXPECT_FALSE(CImpactSystem::ShouldFinalizeParticipantReset(SameTickSelfKill, true, true));
	ASSERT_TRUE(SameBatch.SubmitGameplay({}, {SameTickSelfKill, SameTickTeamBoundary}));
	EXPECT_EQ(SameBatch.Run().State(), EAnalysisState::FINALIZED);
	EXPECT_EQ(SameBatch.Run().FinalizedParticipantCount(), 3);
}

TEST(ImpactFoundation, UninitializedRunRejectsEvents)
{
	CImpactRun Run;
	STickEventBatch Batch;
	Batch.m_ServerTick = 100;
	Batch.m_vCandidates.push_back(StartEvent(1, TestId(1), 100));
	EXPECT_FALSE(Run.IsInitialized());
	EXPECT_FALSE(Run.ApplyTickEventBatch(Batch));
}

TEST(ImpactFoundation, UnsupportedCommunityIsInactive)
{
	EXPECT_TRUE(CImpactSystem::IsSupportedCommunity("kog"));
	EXPECT_TRUE(CImpactSystem::IsSupportedCommunity("EGO"));
	EXPECT_FALSE(CImpactSystem::IsSupportedCommunity("ddnet"));
	EXPECT_FALSE(CImpactSystem::IsSupportedCommunity("unknown"));
}

TEST(ImpactFoundation, SoloMapCategoryIsNeverImpactEligible)
{
	EXPECT_TRUE(CImpactSystem::IsSoloMapCategory("Solo"));
	EXPECT_TRUE(CImpactSystem::IsSoloMapCategory("solo"));
	EXPECT_FALSE(CImpactSystem::IsSoloMapCategory("Insane"));
	EXPECT_FALSE(CImpactSystem::IsImpactMapEligibilityAllowed(true, true, true));
}

TEST(ImpactFoundation, ProductionEligibilityCannotBeBypassed)
{
	EXPECT_FALSE(CImpactSystem::IsImpactMapEligibilityAllowed(false, false, false)); // Unsupported community and unknown KoG category remain inactive.
	EXPECT_FALSE(CImpactSystem::IsImpactMapEligibilityAllowed(false, true, false)); // No unsupported-server bypass.
	EXPECT_FALSE(CImpactSystem::IsImpactMapEligibilityAllowed(true, false, false)); // KoG waits until map category is known.
	EXPECT_TRUE(CImpactSystem::IsImpactMapEligibilityAllowed(true, true, false)); // Valid community and known non-Solo category.
	EXPECT_FALSE(CImpactSystem::IsImpactMapEligibilityAllowed(true, true, true)); // Solo remains excluded.
}

TEST(ImpactFoundation, StatisticsAccessPredicate)
{
	EXPECT_FALSE(CImpactSystem::IsStatisticsUiAccessible(0));
	EXPECT_TRUE(CImpactSystem::IsStatisticsUiAccessible(1));
}

TEST(ImpactLifecycle, LocalDisableLocksAnObservedRunUntilExplicitTeamReset)
{
	SLocalImpactRunEnableGate Gate;
	EXPECT_TRUE(Gate.CanEnable()); // Enabled before START remains the normal path.

	Gate.LockForStartedRun(7); // START observed while this client's local feature was disabled.
	EXPECT_FALSE(Gate.CanEnable());
	EXPECT_FALSE(Gate.ObserveExplicitTeamReset(8, true));
	EXPECT_FALSE(Gate.ObserveExplicitTeamReset(7, false));
	EXPECT_TRUE(Gate.ObserveExplicitTeamReset(7, true));
	EXPECT_TRUE(Gate.CanEnable());

	Gate.LockForStartedRun(7); // Disabling during a run cannot resume that same run.
	EXPECT_FALSE(Gate.CanEnable());
	Gate.Reset(); // A connection/map lifecycle boundary discards the old gate.
	EXPECT_TRUE(Gate.CanEnable());
}

TEST(ImpactPresentation, RunCompletionToastIsSuppressedAcrossFeatureToggle)
{
	CImpactSystem::SRunCompletionToast Toast;
	Toast.m_Active = true;
	Toast.m_RunId = 101;
	Toast.m_PendingSave = true;
	EXPECT_TRUE(Toast.IsVisible(1));
	EXPECT_FALSE(Toast.IsVisible(0));
	Toast.SuppressIfDisabled(0);
	EXPECT_FALSE(Toast.m_Active);
	EXPECT_FALSE(Toast.IsVisible(1));
	Toast.m_Active = true;
	Toast.m_RunId = 102;
	EXPECT_TRUE(Toast.IsVisible(1));
}

TEST(ImpactPresentation, ResetSoundAppliesOnlyToResetRelatedToastResults)
{
	EXPECT_TRUE(CImpactSystem::IsResetRelatedToastResult(ERunResult::WIPED));
	EXPECT_TRUE(CImpactSystem::IsResetRelatedToastResult(ERunResult::TEAM_RESET));
	EXPECT_TRUE(CImpactSystem::IsResetRelatedToastResult(ERunResult::ABORTED_MAP_CHANGE));
	EXPECT_TRUE(CImpactSystem::IsResetRelatedToastResult(ERunResult::ABORTED_SERVER_CHANGE));
	EXPECT_FALSE(CImpactSystem::IsResetRelatedToastResult(ERunResult::FINISHED));
	EXPECT_FALSE(CImpactSystem::IsResetRelatedToastResult(ERunResult::NONE));
}

TEST(ImpactPresentation, ResetToastSoundTriggerUsesValidSample)
{
	EXPECT_GE(SOUND_AMF_RUN_RESET, 0);
	EXPECT_LT(SOUND_AMF_RUN_RESET, NUM_SOUNDS);
	const std::filesystem::path SamplePath = std::filesystem::path(__FILE__).parent_path() / "../../data/audio/amf_run_reset.wv";
	std::ifstream Sample(SamplePath, std::ios::binary | std::ios::ate);
	ASSERT_TRUE(Sample.is_open()) << SamplePath.string();
	ASSERT_GT(Sample.tellg(), 32);
	Sample.seekg(0);
	std::array<char, 4> aHeader{};
	Sample.read(aHeader.data(), aHeader.size());
	EXPECT_EQ(aHeader, (std::array<char, 4>{'w', 'v', 'p', 'k'}));

	CImpactSystem::SRunCompletionToast Toast;
	Toast.m_Active = true;
	Toast.m_RunId = 101;
	Toast.m_Result = ERunResult::TEAM_RESET;
	int SoundTriggers = 0;
	for(int Frame = 0; Frame < 20; ++Frame)
		SoundTriggers += Toast.ConsumeResetSound(1) ? 1 : 0;
	EXPECT_EQ(SoundTriggers, 1);
	Toast.m_RunId = 102;
	Toast.m_ResetSoundPlayed = false;
	Toast.SuppressIfDisabled(0);
	SoundTriggers += Toast.ConsumeResetSound(0) ? 1 : 0;
	EXPECT_EQ(SoundTriggers, 1);
	Toast.m_Active = true;
	Toast.m_RunId = 103;
	Toast.m_Result = ERunResult::TEAM_RESET;
	SoundTriggers += Toast.ConsumeResetSound(1) ? 1 : 0;
	EXPECT_EQ(SoundTriggers, 2);
	Toast.m_Result = ERunResult::FINISHED;
	Toast.m_ResetSoundPlayed = false;
	EXPECT_FALSE(Toast.ConsumeResetSound(1));
}

TEST(ImpactPresentation, ImpactDisplayUsesTwoDecimals)
{
	EXPECT_EQ(FormatTruncatedImpact(7.229), "+7.22%");
	EXPECT_EQ(FormatTruncatedImpact(-3.478), "-3.47%");
}

TEST(ImpactPresentation, ImpactDisplayTruncatesPositiveTowardZero)
{
	EXPECT_EQ(FormatTruncatedImpact(7.229), "+7.22%");
	EXPECT_EQ(FormatTruncatedImpact(7.999), "+7.99%");
}

TEST(ImpactPresentation, ImpactDisplayTruncatesNegativeTowardZero)
{
	EXPECT_EQ(FormatTruncatedImpact(-3.478), "-3.47%");
	EXPECT_EQ(FormatTruncatedImpact(-9.999), "-9.99%");
}

TEST(ImpactPresentation, ImpactDisplayDoesNotRound)
{
	EXPECT_NE(FormatTruncatedImpact(7.229), "+7.23%");
	EXPECT_NE(FormatTruncatedImpact(7.999), "+8.00%");
	EXPECT_NE(FormatTruncatedImpact(-3.478), "-3.48%");
	EXPECT_NE(FormatTruncatedImpact(-9.999), "-10.00%");
}

TEST(ImpactPresentation, ImpactDisplayZeroIsZeroPointZeroZero)
{
	EXPECT_EQ(FormatTruncatedImpact(0.0), "0.00%");
	EXPECT_EQ(FormatTruncatedImpact(-0.0), "0.00%");
	EXPECT_EQ(FormatTruncatedImpact(0.009), "0.00%");
	EXPECT_EQ(FormatTruncatedImpact(-0.009), "0.00%");
}

TEST(ImpactPresentation, ImpactDisplayDoesNotAffectInternalPrecision)
{
	SImpactResult Result;
	Result.m_Available = true;
	Result.m_FinalImpact = 7.229;
	const double Before = Result.m_FinalImpact;
	EXPECT_EQ(FormatTruncatedImpact(Result.m_FinalImpact), "+7.22%");
	EXPECT_DOUBLE_EQ(Result.m_FinalImpact, Before);
}

TEST(ImpactInstrumentation, AledResearchUnknownActorDoesNotBecomeAnArrayIndex)
	{
		const SParticipantId NoActor = TestId(-1, 0);
		EXPECT_FALSE(CImpactSystem::IsAledResearchClientIdValid(NoActor.m_ClientId));

		const SParticipantId Actor = TestId(1, 7);
		EXPECT_TRUE(CImpactSystem::IsAledResearchClientIdValid(Actor.m_ClientId));
}

TEST(ImpactInstrumentation, AledResearchInvalidSwitchContextIsUnknown)
{
	EXPECT_FALSE(CImpactSystem::IsAledResearchSwitchAccessValid(-1, 1, 8));
	EXPECT_FALSE(CImpactSystem::IsAledResearchSwitchAccessValid(8, 1, 8));
	EXPECT_FALSE(CImpactSystem::IsAledResearchSwitchAccessValid(1, -1, 8));
	EXPECT_TRUE(CImpactSystem::IsAledResearchSwitchAccessValid(1, 1, 8));
}

TEST(ImpactInstrumentation, AledResearchGeometryMapIndexGuard)
{
	EXPECT_TRUE(CImpactSystem::IsAledResearchGeometryMapIndexValid(0, 1, 1));
	EXPECT_TRUE(CImpactSystem::IsAledResearchGeometryMapIndexValid(99, 10, 10));
	EXPECT_FALSE(CImpactSystem::IsAledResearchGeometryMapIndexValid(-1, 10, 10));
	EXPECT_FALSE(CImpactSystem::IsAledResearchGeometryMapIndexValid(100, 10, 10));
	EXPECT_FALSE(CImpactSystem::IsAledResearchGeometryMapIndexValid(0, 0, 10));
	EXPECT_FALSE(CImpactSystem::IsAledResearchGeometryMapIndexValid(0, 10, 0));
}

TEST(ImpactInstrumentation, AledResearchGeometryProbeFindsVerticalBarrier)
{
	CImpactSystem::SAledResearchGeometryGridResult Result;
	EXPECT_TRUE(CImpactSystem::AnalyzeAledResearchGeometryGrid(8, 8, 1, 3, 5, 3, {{3, 3}}, 16, Result));
	EXPECT_EQ(Result.m_BarrierAxis, 2);
	EXPECT_EQ(Result.m_FreezeBetween, 1);
	EXPECT_EQ(Result.m_OppositeSides, 1);
}

TEST(ImpactInstrumentation, AledResearchGeometryProbeFindsHorizontalBarrier)
{
	CImpactSystem::SAledResearchGeometryGridResult Result;
	EXPECT_TRUE(CImpactSystem::AnalyzeAledResearchGeometryGrid(8, 8, 3, 1, 3, 5, {{3, 3}}, 16, Result));
	EXPECT_EQ(Result.m_BarrierAxis, 1);
	EXPECT_EQ(Result.m_FreezeBetween, 1);
	EXPECT_EQ(Result.m_OppositeSides, 1);
}

TEST(ImpactInstrumentation, AledResearchGeometryProbeDoesNotInventBarrier)
{
	CImpactSystem::SAledResearchGeometryGridResult Result;
	EXPECT_TRUE(CImpactSystem::AnalyzeAledResearchGeometryGrid(8, 8, 1, 3, 5, 3, {{0, 0}}, 16, Result));
	EXPECT_EQ(Result.m_FreezeTileCount, 0);
	EXPECT_EQ(Result.m_FreezeBetween, 0);
	EXPECT_EQ(Result.m_OppositeSides, 0);
}

TEST(ImpactInstrumentation, AledResearchGeometryProbeRejectsUnsafeGridInputs)
{
	CImpactSystem::SAledResearchGeometryGridResult Result;
	EXPECT_FALSE(CImpactSystem::AnalyzeAledResearchGeometryGrid(0, 8, 1, 1, 2, 1, {}, 16, Result));
	EXPECT_FALSE(CImpactSystem::AnalyzeAledResearchGeometryGrid(8, 8, -1, 1, 2, 1, {}, 16, Result));
	EXPECT_FALSE(CImpactSystem::AnalyzeAledResearchGeometryGrid(8, 8, 1, 1, 2, 1, {{8, 1}}, 16, Result));
	EXPECT_FALSE(CImpactSystem::AnalyzeAledResearchGeometryGrid(8, 8, 1, 1, 7, 7, {}, 4, Result));
	EXPECT_FALSE(CImpactSystem::IsAledResearchPositionFinite(std::numeric_limits<float>::infinity(), 0.0f));
	EXPECT_FALSE(CImpactSystem::IsAledResearchPositionFinite(0.0f, -std::numeric_limits<float>::infinity()));
	EXPECT_TRUE(CImpactSystem::IsAledResearchPositionFinite(32.0f, 64.0f));
}

TEST(ImpactInstrumentation, AledResearchGeometryProbeMarksIrregularBarrierAmbiguous)
{
	CImpactSystem::SAledResearchGeometryGridResult Result;
	EXPECT_TRUE(CImpactSystem::AnalyzeAledResearchGeometryGrid(8, 8, 1, 1, 5, 5, {{2, 2}, {3, 3}}, 16, Result));
	EXPECT_EQ(Result.m_BarrierAxis, 3);
	EXPECT_EQ(Result.m_FreezeBetween, -1);
	EXPECT_EQ(Result.m_OppositeSides, -1);
}

TEST(ImpactInstrumentation, AledResearchGeometryV2FindsVerticalTopology)
{
	CImpactSystem::SAledResearchGeometryV2GridResult Result;
	EXPECT_TRUE(CImpactSystem::AnalyzeAledResearchGeometryV2Grid(8, 8, 1, 3, 5, 3,
		{{3, 2}, {3, 3}, {3, 4}}, 16, 64, Result));
	EXPECT_EQ(Result.m_Topology, CImpactSystem::EAledGeometryV2Topology::VERTICAL);
	EXPECT_EQ(Result.m_Status, CImpactSystem::EAledGeometryV2Status::VALID);
	EXPECT_EQ(Result.m_FreezeBetween, 1);
	EXPECT_EQ(Result.m_OppositeSides, 1);
	EXPECT_TRUE(CImpactSystem::IsAledGeometryV2ProductionValid(Result));
	EXPECT_EQ(CImpactSystem::AledGeometryV2ProductionOrientation(Result), CImpactSystem::EAledGeometryV2Topology::VERTICAL);
}

TEST(ImpactInstrumentation, AledResearchGeometryV2FindsHorizontalTopology)
{
	CImpactSystem::SAledResearchGeometryV2GridResult Result;
	EXPECT_TRUE(CImpactSystem::AnalyzeAledResearchGeometryV2Grid(8, 8, 3, 1, 3, 5,
		{{2, 3}, {3, 3}, {4, 3}}, 16, 64, Result));
	EXPECT_EQ(Result.m_Topology, CImpactSystem::EAledGeometryV2Topology::HORIZONTAL);
	EXPECT_EQ(Result.m_Status, CImpactSystem::EAledGeometryV2Status::VALID);
	EXPECT_EQ(Result.m_FreezeBetween, 1);
	EXPECT_EQ(Result.m_OppositeSides, 1);
	EXPECT_TRUE(CImpactSystem::IsAledGeometryV2ProductionValid(Result));
	EXPECT_EQ(CImpactSystem::AledGeometryV2ProductionOrientation(Result), CImpactSystem::EAledGeometryV2Topology::HORIZONTAL);
}

TEST(ImpactInstrumentation, AledResearchGeometryV2FindsDiagonalCorner)
{
	CImpactSystem::SAledResearchGeometryV2GridResult Result;
	EXPECT_TRUE(CImpactSystem::AnalyzeAledResearchGeometryV2Grid(8, 8, 2, 2, 3, 3,
		{{3, 2}, {2, 3}}, 16, 64, Result));
	EXPECT_EQ(Result.m_Topology, CImpactSystem::EAledGeometryV2Topology::DIAGONAL);
	EXPECT_EQ(Result.m_Status, CImpactSystem::EAledGeometryV2Status::VALID);
	EXPECT_EQ(Result.m_CornerFreezePair, 1);
	EXPECT_EQ(Result.m_FreezeBetween, 1);
	EXPECT_EQ(Result.m_OppositeSides, 1);
}

TEST(ImpactInstrumentation, AledResearchGeometryV2FindsMirroredDiagonalCorner)
{
	CImpactSystem::SAledResearchGeometryV2GridResult Result;
	EXPECT_TRUE(CImpactSystem::AnalyzeAledResearchGeometryV2Grid(8, 8, 3, 3, 2, 2,
		{{2, 3}, {3, 2}}, 16, 64, Result));
	EXPECT_EQ(Result.m_Topology, CImpactSystem::EAledGeometryV2Topology::DIAGONAL);
	EXPECT_EQ(Result.m_CornerFreezePair, 1);
}

TEST(ImpactInstrumentation, AledResearchGeometryV2FindsBothDirectionsOfNeSwSlope)
{
	CImpactSystem::SAledResearchGeometryV2GridResult Forward;
	CImpactSystem::SAledResearchGeometryV2GridResult Reverse;
	EXPECT_TRUE(CImpactSystem::AnalyzeAledResearchGeometryV2Grid(8, 8, 3, 2, 2, 3,
		{{2, 2}, {3, 3}}, 16, 64, Forward));
	EXPECT_TRUE(CImpactSystem::AnalyzeAledResearchGeometryV2Grid(8, 8, 2, 3, 3, 2,
		{{3, 3}, {2, 2}}, 16, 64, Reverse));
	EXPECT_EQ(Forward.m_Topology, CImpactSystem::EAledGeometryV2Topology::DIAGONAL);
	EXPECT_EQ(Reverse.m_Topology, CImpactSystem::EAledGeometryV2Topology::DIAGONAL);
	EXPECT_EQ(Forward.m_DiagonalOrientation, 2);
	EXPECT_EQ(Reverse.m_DiagonalOrientation, 2);
	EXPECT_EQ(Forward.m_CornerFreezePair, 1);
	EXPECT_EQ(Reverse.m_CornerFreezePair, 1);
}

TEST(ImpactInstrumentation, AledResearchGeometryV2RejectsSingleCornerSide)
{
	CImpactSystem::SAledResearchGeometryV2GridResult Result;
	EXPECT_TRUE(CImpactSystem::AnalyzeAledResearchGeometryV2Grid(8, 8, 2, 2, 3, 3,
		{{3, 2}}, 16, 64, Result));
	EXPECT_NE(Result.m_Topology, CImpactSystem::EAledGeometryV2Topology::DIAGONAL);
	EXPECT_EQ(Result.m_CornerFreezePair, 0);
}

TEST(ImpactInstrumentation, AledResearchGeometryV2DoesNotUseEndpointAsBarrier)
{
	CImpactSystem::SAledResearchGeometryV2GridResult Result;
	EXPECT_TRUE(CImpactSystem::AnalyzeAledResearchGeometryV2Grid(8, 8, 1, 3, 5, 3,
		{{1, 3}}, 16, 64, Result));
	EXPECT_EQ(Result.m_ActorOnFreeze, 1);
	EXPECT_EQ(Result.m_FreezeBetween, 0);
	EXPECT_NE(Result.m_Topology, CImpactSystem::EAledGeometryV2Topology::VERTICAL);
	EXPECT_FALSE(CImpactSystem::IsAledGeometryV2ProductionValid(Result));
}

TEST(ImpactInstrumentation, AledResearchGeometryV2KeepsIsolatedFreezeUnknown)
{
	CImpactSystem::SAledResearchGeometryV2GridResult Result;
	EXPECT_TRUE(CImpactSystem::AnalyzeAledResearchGeometryV2Grid(8, 8, 1, 3, 5, 3,
		{{3, 3}}, 16, 64, Result));
	EXPECT_EQ(Result.m_Topology, CImpactSystem::EAledGeometryV2Topology::UNKNOWN);
	EXPECT_EQ(Result.m_Status, CImpactSystem::EAledGeometryV2Status::INSUFFICIENT_EVIDENCE);
	EXPECT_FALSE(CImpactSystem::IsAledGeometryV2ProductionValid(Result));
}

TEST(ImpactInstrumentation, AledResearchGeometryV2DoesNotCopyLegacyVerticalForDiagonalStaircase)
{
	CImpactSystem::SAledResearchGeometryGridResult Legacy;
	CImpactSystem::SAledResearchGeometryV2GridResult V2;
	const std::vector<std::pair<int, int>> LegacyFreeze = {{4, 4}};
	const std::vector<std::pair<int, int>> LocalTopology = {{3, 3}, {4, 4}, {5, 5}};
	EXPECT_TRUE(CImpactSystem::AnalyzeAledResearchGeometryGrid(8, 8, 5, 4, 3, 4, LegacyFreeze, 16, Legacy));
	EXPECT_EQ(Legacy.m_BarrierAxis, 2);
	EXPECT_EQ(Legacy.m_FreezeBetween, 1);
	EXPECT_EQ(Legacy.m_OppositeSides, 1);
	EXPECT_TRUE(CImpactSystem::AnalyzeAledResearchGeometryV2Grid(8, 8, 5, 4, 3, 4, LocalTopology, 16, 64, V2));
	EXPECT_EQ(V2.m_Topology, CImpactSystem::EAledGeometryV2Topology::DIAGONAL);
	EXPECT_EQ(V2.m_Status, CImpactSystem::EAledGeometryV2Status::VALID);
	EXPECT_NE(V2.m_Topology, CImpactSystem::EAledGeometryV2Topology::VERTICAL);
}

TEST(ImpactInstrumentation, AledResearchGeometryV2RejectsUnrelatedCorner)
{
	CImpactSystem::SAledResearchGeometryV2GridResult Result;
	EXPECT_TRUE(CImpactSystem::AnalyzeAledResearchGeometryV2Grid(8, 8, 1, 1, 5, 1,
		{{2, 2}, {2, 0}}, 16, 64, Result));
	EXPECT_NE(Result.m_Topology, CImpactSystem::EAledGeometryV2Topology::DIAGONAL);
	EXPECT_EQ(Result.m_CornerFreezePair, 0);
}

TEST(ImpactInstrumentation, AledResearchGeometryV2MarksExtraLocalTopologyAmbiguous)
{
	CImpactSystem::SAledResearchGeometryV2GridResult Result;
	EXPECT_TRUE(CImpactSystem::AnalyzeAledResearchGeometryV2Grid(8, 8, 2, 2, 3, 3,
		{{3, 2}, {2, 3}, {4, 2}}, 16, 64, Result));
	EXPECT_EQ(Result.m_Topology, CImpactSystem::EAledGeometryV2Topology::AMBIGUOUS);
	EXPECT_EQ(Result.m_Status, CImpactSystem::EAledGeometryV2Status::AMBIGUOUS);
}

TEST(ImpactInstrumentation, AledResearchGeometryV2StrongCornerRemainsProductionAled)
{
	CImpactSystem::SAledResearchGeometryV2GridResult Result;
	EXPECT_TRUE(CImpactSystem::AnalyzeAledResearchGeometryV2Grid(256, 256, 171, 137, 170, 135,
		{{170, 136}, {171, 135}, {170, 134}}, 16, 64, Result));
	EXPECT_EQ(Result.m_Topology, CImpactSystem::EAledGeometryV2Topology::AMBIGUOUS);
	EXPECT_EQ(Result.m_Status, CImpactSystem::EAledGeometryV2Status::AMBIGUOUS);
	EXPECT_EQ(Result.m_FreezeBetween, 1);
	EXPECT_EQ(Result.m_OppositeSides, 1);
	EXPECT_EQ(Result.m_CornerFreezePair, 1);
	EXPECT_EQ(CImpactSystem::AledGeometryV2ProductionOrientation(Result), CImpactSystem::EAledGeometryV2Topology::DIAGONAL);
	EXPECT_TRUE(CImpactSystem::IsAledGeometryV2ProductionValid(Result));
	EXPECT_TRUE(CImpactSystem::IsAledV2CandidateEvidenceSufficient(true, true, true, true, true, 8350, 8352));

	// The legacy ray is deliberately not part of this production decision.
	EXPECT_TRUE(CImpactSystem::IsAledCandidateEvidenceSufficient(true, true, true, true, 1, 1, 2, true, 8350, 8352));
	EXPECT_FALSE(CImpactSystem::IsAledV2CandidateEvidenceSufficient(true, true, true, false, true, 8350, 8352));
}

TEST(ImpactInstrumentation, AledResearchGeometryV2RejectsUnsafeInputs)
{
	CImpactSystem::SAledResearchGeometryV2GridResult Result;
	EXPECT_FALSE(CImpactSystem::AnalyzeAledResearchGeometryV2Grid(0, 8, 1, 1, 2, 2, {}, 16, 64, Result));
	EXPECT_FALSE(CImpactSystem::AnalyzeAledResearchGeometryV2Grid(8, 8, -1, 1, 2, 2, {}, 16, 64, Result));
	EXPECT_FALSE(CImpactSystem::AnalyzeAledResearchGeometryV2Grid(8, 8, 1, 1, 2, 2, {{8, 1}}, 16, 64, Result));
	EXPECT_FALSE(CImpactSystem::AnalyzeAledResearchGeometryV2Grid(8, 8, 1, 1, 7, 7, {}, 4, 64, Result));
	EXPECT_FALSE(CImpactSystem::AnalyzeAledResearchGeometryV2Grid(8, 8, 1, 1, 2, 2, {{1, 1}, {2, 2}}, 16, 1, Result));
}

TEST(ImpactLiveAdapter, ForeignTeamStartAndFinishCannotCreateLocalRunWithoutParticipation)
{
	const int LocalMainTeamId = CImpactSystem::LocalParticipationTeamForSlot(0, true, false, true);
	const int LocalTeamId = CImpactSystem::SelectLocalParticipationTeam(LocalMainTeamId, 0);
	EXPECT_EQ(LocalTeamId, 0);

	STickEventBatch ForeignRun;
	ForeignRun.m_ServerTick = 100;
	ForeignRun.m_vCandidates.push_back(StartEvent(1, TestId(7), 100, "foreign team", 9));
	SImpactEvent Finish;
	Finish.m_EventId = 2;
	Finish.m_Type = EImpactEventType::FINISH_CONFIRMED;
	Finish.m_TeamId = 9;
	Finish.m_StartTick = 200;
	Finish.m_ResolveTick = 200;
	Finish.m_Confirmed = true;
	ForeignRun.m_vCandidates.push_back(Finish);

	// Production uses this decision before EnsureRun. A rejected foreign START
	// leaves no Current Run for the later FINISH to finalize or offer for saving.
	const int ScopedTeamId = CImpactSystem::SelectNewRunStartTeam(ForeignRun, LocalTeamId);
	EXPECT_EQ(ScopedTeamId, 0);
	CImpactRun CurrentRun;
	if(ScopedTeamId != 0)
		CurrentRun.Initialize(8101, TestContext(ScopedTeamId));
	EXPECT_FALSE(CurrentRun.IsInitialized());
	EXPECT_FALSE(CurrentRun.Finalize(ERunResult::FINISHED, 200));
	EXPECT_FALSE(CurrentRun.IsFinalized());

	STickEventBatch LateFinish;
	LateFinish.m_ServerTick = 200;
	LateFinish.m_vCandidates.push_back(Finish);
	EXPECT_EQ(CImpactSystem::SelectNewRunStartTeam(LateFinish, LocalTeamId), 0);
}

TEST(ImpactLiveAdapter, SpectatingForeignTeamDoesNotEstablishLocalParticipation)
{
	// The production local-slot predicate excludes spectators and missing current
	// Character snapshots, even when stale team metadata is present.
	const int SpectatorTeam = CImpactSystem::LocalParticipationTeamForSlot(9, true, true, true);
	const int NoCharacterTeam = CImpactSystem::LocalParticipationTeamForSlot(9, true, false, false);
	EXPECT_EQ(SpectatorTeam, 0);
	EXPECT_EQ(NoCharacterTeam, 0);
	EXPECT_EQ(CImpactSystem::SelectLocalParticipationTeam(SpectatorTeam, 0), 0);

	STickEventBatch ForeignStart;
	ForeignStart.m_ServerTick = 100;
	ForeignStart.m_vCandidates.push_back(StartEvent(1, TestId(7), 100, "spectated team", 9));
	EXPECT_EQ(CImpactSystem::SelectNewRunStartTeam(ForeignStart, SpectatorTeam), 0);
}

TEST(ImpactLiveAdapter, LocalTeamCanStartWhenTeammateCrossesStartFirst)
{
	const int MainTeamId = CImpactSystem::LocalParticipationTeamForSlot(5, true, false, true);
	const int LocalTeamId = CImpactSystem::SelectLocalParticipationTeam(MainTeamId, 0);
	ASSERT_EQ(LocalTeamId, 5);

	STickEventBatch FirstStart;
	FirstStart.m_ServerTick = 100;
	FirstStart.m_vCandidates.push_back(StartEvent(1, TestId(2, 3), 100, "teammate starts first", 5));
	const int ScopedTeamId = CImpactSystem::SelectNewRunStartTeam(FirstStart, LocalTeamId);
	ASSERT_EQ(ScopedTeamId, 5);

	CReplay Replay(8102, TestContext(ScopedTeamId));
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(1, 4), "local main", 90, "current local team membership"));
	ASSERT_TRUE(Replay.Submit(FirstStart));
	EXPECT_TRUE(Replay.Run().HasParticipant(TestId(1, 4)));
	EXPECT_FALSE(Replay.Run().HasStartedParticipant(TestId(1, 4)));
	EXPECT_TRUE(Replay.Run().HasStartedParticipant(TestId(2, 3)));

	SImpactEvent Save;
	Save.m_EventId = 2;
	Save.m_Type = EImpactEventType::SAVE_CONFIRMED;
	Save.m_Actor = TestId(2, 3);
	Save.m_TeamId = 5;
	Save.m_StartTick = 150;
	Save.m_ResolveTick = 150;
	Save.m_Confirmed = true;
	Save.m_RawDelta.m_Saves = 1;
	STickEventBatch NormalEvents;
	NormalEvents.m_ServerTick = 150;
	NormalEvents.m_vCandidates.push_back(Save);
	ASSERT_TRUE(Replay.Submit(NormalEvents));

	SImpactEvent Finish;
	Finish.m_EventId = 3;
	Finish.m_Type = EImpactEventType::FINISH_CONFIRMED;
	Finish.m_TeamId = 5;
	Finish.m_StartTick = 200;
	Finish.m_ResolveTick = 200;
	Finish.m_Confirmed = true;
	STickEventBatch FinishBatch;
	FinishBatch.m_ServerTick = 200;
	FinishBatch.m_vCandidates.push_back(Finish);
	ASSERT_TRUE(Replay.Submit(FinishBatch));
	EXPECT_EQ(Replay.Run().Result(), ERunResult::FINISHED);
	EXPECT_TRUE(Replay.Run().IsFinalized());
	const SRunSnapshot Final = Replay.Run().Snapshot(200, 123);
	EXPECT_EQ(Final.m_RunId, 8102U);
	EXPECT_EQ(Final.m_Result, ERunResult::FINISHED);
	EXPECT_NE(FindSnapshotParticipant(Final, TestId(1, 4)), nullptr);
	const SParticipant *pTeammate = FindSnapshotParticipant(Final, TestId(2, 3));
	ASSERT_NE(pTeammate, nullptr);
	EXPECT_EQ(pTeammate->m_RawStats.m_Saves, 1);
}

TEST(ImpactLiveAdapter, OtherPlayersRemainTrackedInsideLocallyOwnedRun)
{
	const int LocalTeamId = CImpactSystem::SelectLocalParticipationTeam(
		CImpactSystem::LocalParticipationTeamForSlot(5, true, false, true), 0);
	ASSERT_EQ(LocalTeamId, 5);
	CReplay Replay(8103, TestContext(LocalTeamId));
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(1, 2), "local", 90, "current local team membership"));

	STickEventBatch Starts;
	Starts.m_ServerTick = 100;
	Starts.m_vCandidates.push_back(StartEvent(1, TestId(2, 4), 100, "teammate A", 5));
	ASSERT_EQ(CImpactSystem::SelectNewRunStartTeam(Starts, LocalTeamId), 5);
	ASSERT_TRUE(Replay.Submit(Starts));
	Starts = {};
	Starts.m_ServerTick = 110;
	Starts.m_vCandidates.push_back(StartEvent(2, TestId(3, 7), 110, "teammate B", 5));
	ASSERT_TRUE(Replay.Submit(Starts));

	SImpactEvent Save;
	Save.m_EventId = 3;
	Save.m_Type = EImpactEventType::SAVE_CONFIRMED;
	Save.m_Actor = TestId(3, 7);
	Save.m_TeamId = 5;
	Save.m_StartTick = 120;
	Save.m_ResolveTick = 120;
	Save.m_Confirmed = true;
	Save.m_RawDelta.m_Saves = 1;
	STickEventBatch Stats;
	Stats.m_ServerTick = 120;
	Stats.m_vCandidates.push_back(Save);
	ASSERT_TRUE(Replay.Submit(Stats));
	EXPECT_EQ(Replay.Run().StartedParticipantCount(), 2);
	EXPECT_TRUE(Replay.Run().HasParticipant(TestId(1, 2)));
	EXPECT_TRUE(Replay.Run().HasParticipant(TestId(2, 4)));
	EXPECT_TRUE(Replay.Run().HasParticipant(TestId(3, 7)));
	const SParticipant *pTeammate = FindRunParticipant(Replay.Run(), TestId(3, 7));
	ASSERT_NE(pTeammate, nullptr);
	EXPECT_EQ(pTeammate->m_RawStats.m_Saves, 1);
}

TEST(ImpactLiveAdapter, OtherTeamsCannotStealCurrentRunScope)
{
	const int LocalTeamId = CImpactSystem::SelectLocalParticipationTeam(
		CImpactSystem::LocalParticipationTeamForSlot(5, true, false, true), 0);
	STickEventBatch ForeignOnly;
	ForeignOnly.m_ServerTick = 100;
	ForeignOnly.m_vCandidates.push_back(StartEvent(1, TestId(7), 100, "other A", 9));
	ForeignOnly.m_vCandidates.push_back(StartEvent(2, TestId(8), 100, "other B", 9));
	EXPECT_EQ(CImpactSystem::SelectNewRunStartTeam(ForeignOnly, LocalTeamId), 0);

	STickEventBatch MixedStarts = ForeignOnly;
	MixedStarts.m_vCandidates.push_back(StartEvent(3, TestId(9), 100, "other C", 9));
	MixedStarts.m_vCandidates.push_back(StartEvent(4, TestId(2), 100, "local teammate", 5));
	EXPECT_EQ(CImpactSystem::SelectNewRunStartTeam(MixedStarts, LocalTeamId), 5);

	CReplay Replay(8104, TestContext(LocalTeamId));
	ASSERT_TRUE(Replay.Submit(MixedStarts));
	EXPECT_EQ(Replay.Run().StartedParticipantCount(), 1);
	EXPECT_TRUE(Replay.Run().HasParticipant(TestId(2)));
	EXPECT_FALSE(Replay.Run().HasParticipant(TestId(7)));
	EXPECT_FALSE(Replay.Run().HasParticipant(TestId(8)));
	EXPECT_FALSE(Replay.Run().HasParticipant(TestId(9)));
}

TEST(ImpactLiveAdapter, DummyUsesExistingLocalParticipationFallbackSemantics)
{
	EXPECT_EQ(CImpactSystem::LocalParticipationTeamForSlot(6, true, false, true), 6);
	EXPECT_EQ(CImpactSystem::SelectLocalParticipationTeam(5, 6), 5);
	const int DummyOnlyTeam = CImpactSystem::SelectLocalParticipationTeam(0, 6);
	ASSERT_EQ(DummyOnlyTeam, 6);

	STickEventBatch DummyStart;
	DummyStart.m_ServerTick = 100;
	DummyStart.m_vCandidates.push_back(StartEvent(1, TestId(2, 8, true), 100, "local dummy", 6));
	EXPECT_EQ(CImpactSystem::SelectNewRunStartTeam(DummyStart, DummyOnlyTeam), 6);

	CReplay Replay(8105, TestContext(DummyOnlyTeam));
	ASSERT_TRUE(Replay.Submit(DummyStart));
	ASSERT_EQ(Replay.Run().Participants().size(), 1U);
	EXPECT_TRUE(Replay.Run().Participants()[0].m_Id.m_Dummy);
}

TEST(ImpactFoundation, OneEngagedParticipantKeepsSingleMemberRunInAnalyzing)
{
	CReplay Replay(1, TestContext());
	STickEventBatch Batch;
	Batch.m_ServerTick = 100;
	Batch.m_vCandidates.push_back(StartEvent(1, TestId(1), 100));
	EXPECT_TRUE(Replay.Submit(Batch));
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::ANALYZING);
	EXPECT_EQ(Replay.Run().Participants().size(), 1U);
	EXPECT_EQ(Replay.Run().ActiveEngagedParticipantCount(), 1);
	EXPECT_FALSE(CImpactSystem::IsPhase2ProcessingAllowed(Replay.Run().State(), Replay.Run().ActiveParticipantCount()));
}

TEST(ImpactFoundation, OneStartOpensWholeTeamRun)
{
	CReplay Replay(2001, TestContext(7));
	const std::array<SParticipantId, 3> aMembers = {TestId(1, 4), TestId(2, 9), TestId(3, 2)};
	const std::array<const char *, 3> apNames = {"AFK", "Starter", "Teammate"};
	for(size_t Index = 0; Index < aMembers.size(); ++Index)
	{
		// Production reconciliation uses PlayerInfo + exact DDNet TeamId and slot
		// generation, not a visible Character snapshot or a personal START flag.
		ASSERT_TRUE(CImpactSystem::IsCurrentTeamRosterEntry(true, true, false, 7, 7, aMembers[Index].m_SlotGeneration));
		ASSERT_TRUE(Replay.Run().AddRosterMember(aMembers[Index], apNames[Index], 100, "authoritative exact-team roster"));
	}

	STickEventBatch Batch;
	Batch.m_ServerTick = 120;
	Batch.m_vCandidates.push_back(StartEvent(1, aMembers[1], 120, "Starter", 7));
	ASSERT_TRUE(Replay.Submit(Batch));

	ASSERT_EQ(Replay.Run().Participants().size(), 3U);
	EXPECT_EQ(Replay.Run().RosterMemberCount(), 3);
	EXPECT_EQ(Replay.Run().StartedParticipantCount(), 1);
	EXPECT_EQ(Replay.Run().EngagedParticipantCount(), 1);
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::ACTIVE);
	EXPECT_TRUE(CImpactSystem::IsPhase2ProcessingAllowed(Replay.Run().State(), Replay.Run().ActiveParticipantCount()));
	EXPECT_EQ(ImpactStatisticsParticipantCount(Replay.Run().Participants()), 3U);
	EXPECT_FALSE(Replay.Run().IsParticipantEngaged(aMembers[0]));
	EXPECT_TRUE(Replay.Run().IsParticipantEngaged(aMembers[1]));
	EXPECT_FALSE(Replay.Run().IsParticipantEngaged(aMembers[2]));
}

TEST(ImpactFoundation, SecondPersonalStartNotRequiredForRunRoster)
{
	CReplay Replay(2012, TestContext(5));
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(1, 4), "Starter", 90, "current team roster"));
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(2, 7), "Afk", 90, "current team roster"));
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(3, 9), "Teammate", 90, "current team roster"));
	STickEventBatch Batch;
	Batch.m_ServerTick = 100;
	Batch.m_vCandidates.push_back(StartEvent(1, TestId(1, 4), 100, "Starter", 5));
	ASSERT_TRUE(Replay.Submit(Batch));

	EXPECT_EQ(Replay.Run().State(), EAnalysisState::ACTIVE);
	EXPECT_EQ(Replay.Run().RosterMemberCount(), 3);
	EXPECT_EQ(Replay.Run().StartedParticipantCount(), 1);
	EXPECT_TRUE(CImpactSystem::IsPhase2ProcessingAllowed(Replay.Run().State(), Replay.Run().ActiveParticipantCount()));
	EXPECT_FALSE(Replay.Run().HasStartedParticipant(TestId(2, 7)));
	EXPECT_FALSE(Replay.Run().HasStartedParticipant(TestId(3, 9)));
}

TEST(ImpactFoundation, OneStartDoesNotShowWaitingForSecondStart)
{
	CReplay Replay(2013, TestContext(5));
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(1, 4), "Starter", 90, "current team roster"));
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(2, 7), "Afk", 90, "current team roster"));
	STickEventBatch Batch;
	Batch.m_ServerTick = 100;
	Batch.m_vCandidates.push_back(StartEvent(1, TestId(1, 4), 100, "Starter", 5));
	ASSERT_TRUE(Replay.Submit(Batch));

	ASSERT_EQ(Replay.Run().State(), EAnalysisState::ACTIVE);
	ASSERT_EQ(Replay.Run().ActiveEngagedParticipantCount(), 1);
	const char *pStatus = RunStatusLocalizationKey(Replay.Run().State(), Replay.Run().RosterMemberCount(), Replay.Run().EligibleParticipantCount());
	EXPECT_STREQ(pStatus, "Analyzing...");
	EXPECT_STRNE(pStatus, "Waiting for the second participant to start");
	EXPECT_TRUE(CImpactSystem::IsPhase2ProcessingAllowed(Replay.Run().State(), Replay.Run().ActiveParticipantCount()));
}

TEST(ImpactFoundation, ThreeRosterMembers_FirstStartImmediatelyStartsPhase2)
{
	CReplay Replay(2014, TestContext(5));
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(1, 4), "A", 90, "current team roster"));
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(2, 7), "B", 90, "current team roster"));
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(3, 9), "C", 90, "current team roster"));
	STickEventBatch Batch;
	Batch.m_ServerTick = 100;
	Batch.m_vCandidates.push_back(StartEvent(1, TestId(1, 4), 100, "A", 5));
	ASSERT_TRUE(Replay.Submit(Batch));

	EXPECT_EQ(Replay.Run().RosterMemberCount(), 3);
	EXPECT_EQ(Replay.Run().ActiveParticipantCount(), 3);
	EXPECT_EQ(Replay.Run().ActiveEngagedParticipantCount(), 1);
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::ACTIVE);
	EXPECT_TRUE(CImpactSystem::IsPhase2ProcessingAllowed(Replay.Run().State(), Replay.Run().ActiveParticipantCount()));
	EXPECT_EQ(Replay.Run().Snapshot(100).m_AnalysisStartedTick, 100);
	EXPECT_EQ(Replay.Run().ActiveAnalysisTicks(101), 1);
	EXPECT_STREQ(RunStatusLocalizationKey(Replay.Run().State(), Replay.Run().RosterMemberCount(), Replay.Run().EligibleParticipantCount()), "Analyzing...");
	EXPECT_FALSE(Replay.Run().IsParticipantEngaged(TestId(2, 7)));
	EXPECT_FALSE(Replay.Run().IsParticipantEngaged(TestId(3, 9)));
}

TEST(ImpactFoundation, NonStartedAfkMemberAppearsInRunStatistics)
{
	CReplay Replay(2002, TestContext(3));
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(1, 3), "AFK", 100, "authoritative exact-team roster"));
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(2, 5), "Starter", 100, "authoritative exact-team roster"));
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(3, 8), "Teammate", 100, "authoritative exact-team roster"));
	STickEventBatch Batch;
	Batch.m_ServerTick = 120;
	Batch.m_vCandidates.push_back(StartEvent(1, TestId(2, 5), 120, "Starter", 3));
	ASSERT_TRUE(Replay.Submit(Batch));

	EXPECT_EQ(ImpactStatisticsParticipantCount(Replay.Run().Participants()), 3U);
	EXPECT_NE(FindRunParticipant(Replay.Run(), TestId(1, 3)), nullptr);
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(1, 3))->m_FirstStartTick, -1);
	EXPECT_FALSE(Replay.Run().IsParticipantEngaged(TestId(1, 3)));
}

TEST(ImpactFoundation, OneEngagedTwoAfkStartsPhase2FromRoster)
{
	CReplay Replay(2003, TestContext());
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(1), "Started", 0, "team roster"));
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(2), "AFK B", 0, "team roster"));
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(3), "AFK C", 0, "team roster"));
	STickEventBatch Batch;
	Batch.m_ServerTick = 10;
	Batch.m_vCandidates.push_back(StartEvent(1, TestId(1), 10));
	ASSERT_TRUE(Replay.Submit(Batch));

	EXPECT_EQ(Replay.Run().RosterMemberCount(), 3);
	EXPECT_EQ(Replay.Run().ActiveParticipantCount(), 3);
	EXPECT_EQ(Replay.Run().ActiveEngagedParticipantCount(), 1);
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::ACTIVE);
	EXPECT_TRUE(CImpactSystem::IsPhase2ProcessingAllowed(Replay.Run().State(), Replay.Run().ActiveParticipantCount()));
}

TEST(ImpactFoundation, TwoEngagedOneAfkKeepsPhase2Active)
{
	CReplay Replay(2004, TestContext());
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(3), "AFK", 0, "team roster"));
	StartPair(Replay);
	EXPECT_EQ(Replay.Run().RosterMemberCount(), 3);
	EXPECT_EQ(Replay.Run().ActiveEngagedParticipantCount(), 2);
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::ACTIVE);
	EXPECT_TRUE(CImpactSystem::IsPhase2ProcessingAllowed(Replay.Run().State(), Replay.Run().ActiveParticipantCount()));
	EXPECT_FALSE(Replay.Run().IsParticipantEngaged(TestId(3)));
}

TEST(ImpactFoundation, SecondParticipantCanEngageWithoutObservedStart)
{
	CReplay Replay(2005, TestContext());
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(1), "Starter", 0, "team roster"));
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(2), "Late Tee", 0, "team roster"));
	STickEventBatch Batch;
	Batch.m_ServerTick = 10;
	Batch.m_vCandidates.push_back(StartEvent(1, TestId(1), 10));
	ASSERT_TRUE(Replay.Submit(Batch));
	ASSERT_EQ(Replay.Run().State(), EAnalysisState::ACTIVE);
	ASSERT_FALSE(Replay.Run().IsParticipantEngaged(TestId(2)));
	ASSERT_TRUE(CImpactSystem::IsPhase2ProcessingAllowed(Replay.Run().State(), Replay.Run().ActiveParticipantCount()));

	SRawStats Fall;
	Fall.m_Falls = 1;
	Batch = {};
	Batch.m_ServerTick = 20;
	Batch.m_vCandidates.push_back(ParticipantStatsEvent(2, EImpactEventType::FALL_CONFIRMED, TestId(2), 20, Fall, TestId(2)));
	ASSERT_TRUE(Replay.Submit(Batch));
	EXPECT_TRUE(Replay.Run().IsParticipantEngaged(TestId(2)));
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::ACTIVE);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(2))->m_RawStats.m_Falls, 1);
}

TEST(ImpactFoundation, UnstartedDummyDoesNotActivateAnalysis)
{
	CReplay Replay(1, TestContext());
	STickEventBatch Batch;
	Batch.m_ServerTick = 100;
	Batch.m_vCandidates.push_back(StartEvent(1, TestId(1, 1, false), 100, "main"));
	EXPECT_TRUE(Replay.Submit(Batch));
	// A dummy only becomes a participant through its own START_CROSSING event.
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::ANALYZING);
	EXPECT_EQ(Replay.Run().Participants().size(), 1U);
}

TEST(ImpactFoundation, SecondParticipantStartAlsoBeginsActiveAnalysis)
{
	CReplay Replay(1, TestContext());
	StartPair(Replay);
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::ACTIVE);
	ASSERT_EQ(Replay.Run().Participants().size(), 2U);
	EXPECT_EQ(Replay.Run().Participants()[0].m_PersonalAnalysisStartTick, 10);
	EXPECT_EQ(Replay.Run().Participants()[1].m_PersonalAnalysisStartTick, 10);
}

TEST(ImpactFoundation, ImpactProcessingRequiresTwoActiveRosterMembers)
{
	// Engagement is not the Phase2 quorum; the current non-finalized roster is.
	// A single-member run and a paused run still cannot process gameplay.
	EXPECT_FALSE(CImpactSystem::IsPhase2ProcessingAllowed(EAnalysisState::WAITING, 0));
	EXPECT_FALSE(CImpactSystem::IsPhase2ProcessingAllowed(EAnalysisState::ANALYZING, 0));
	EXPECT_FALSE(CImpactSystem::IsPhase2ProcessingAllowed(EAnalysisState::ANALYZING, 1));
	EXPECT_TRUE(CImpactSystem::IsPhase2ProcessingAllowed(EAnalysisState::ANALYZING, 2));
	EXPECT_FALSE(CImpactSystem::IsPhase2ProcessingAllowed(EAnalysisState::ACTIVE, 1));
	EXPECT_TRUE(CImpactSystem::IsPhase2ProcessingAllowed(EAnalysisState::ACTIVE, 2));
	EXPECT_FALSE(CImpactSystem::IsPhase2ProcessingAllowed(EAnalysisState::PAUSED_NO_TEAM, 2));
}

TEST(ImpactFoundation, MainAndDummyStartsCreateTwoEligibleParticipants)
{
	CReplay Replay(1, TestContext());
	STickEventBatch Batch;
	Batch.m_ServerTick = 100;
	Batch.m_vCandidates.push_back(StartEvent(1, TestId(1, 1, false), 100, "main"));
	EXPECT_TRUE(Replay.Submit(Batch));
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::ANALYZING);
	EXPECT_EQ(Replay.Run().Participants().size(), 1U);

	Batch = {};
	Batch.m_ServerTick = 120;
	Batch.m_vCandidates.push_back(StartEvent(2, TestId(2, 1, true), 120, "dummy"));
	EXPECT_TRUE(Replay.Submit(Batch));
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::ACTIVE);
	ASSERT_EQ(Replay.Run().Participants().size(), 2U);
	EXPECT_NE(Replay.Run().Participants()[0].m_Id, Replay.Run().Participants()[1].m_Id);
	EXPECT_EQ(Replay.Run().Participants()[0].m_PersonalAnalysisStartTick, 120);
	EXPECT_EQ(Replay.Run().Participants()[1].m_PersonalAnalysisStartTick, 120);
}

TEST(ImpactFoundation, DummyThenMainStartsCreateTwoEligibleParticipants)
{
	CReplay Replay(1, TestContext());
	STickEventBatch Batch;
	Batch.m_ServerTick = 100;
	Batch.m_vCandidates.push_back(StartEvent(1, TestId(2, 1, true), 100, "dummy"));
	EXPECT_TRUE(Replay.Submit(Batch));
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::ANALYZING);

	Batch = {};
	Batch.m_ServerTick = 120;
	Batch.m_vCandidates.push_back(StartEvent(2, TestId(1, 1, false), 120, "main"));
	EXPECT_TRUE(Replay.Submit(Batch));
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::ACTIVE);
	EXPECT_EQ(Replay.Run().Participants().size(), 2U);
}

TEST(ImpactFoundation, RemoteSecondStartCreatesActiveAnalysis)
{
	CReplay Replay(1, TestContext());
	STickEventBatch Batch;
	Batch.m_ServerTick = 100;
	Batch.m_vCandidates.push_back(StartEvent(1, TestId(1), 100, "main"));
	EXPECT_TRUE(Replay.Submit(Batch));

	Batch = {};
	Batch.m_ServerTick = 120;
	Batch.m_vCandidates.push_back(StartEvent(2, TestId(3), 120, "remote"));
	EXPECT_TRUE(Replay.Submit(Batch));
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::ACTIVE);
	EXPECT_EQ(Replay.Run().Participants().size(), 2U);
}

TEST(ImpactFoundation, OrdinaryRemoteDdnetStartIsEligibleWithoutAmfMetadata)
{
	CReplay Replay(2, TestContext(3));
	STickEventBatch Batch;
	Batch.m_ServerTick = 100;
	Batch.m_vCandidates.push_back(StartEvent(1, TestId(1), 100, "local AMF", 3));
	ASSERT_TRUE(Replay.Submit(Batch));

	// The ordinary remote client contributes only the same authoritative
	// start/team data as any other Tee; it has no AMF presence or version data.
	Batch = {};
	Batch.m_ServerTick = 120;
	Batch.m_vCandidates.push_back(StartEvent(2, TestId(2), 120, "ordinary DDNet", 3));
	ASSERT_TRUE(Replay.Submit(Batch));

	EXPECT_EQ(Replay.Run().StartedParticipantCount(), 2);
	EXPECT_EQ(Replay.Run().EligibleParticipantCount(), 2);
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::ACTIVE);
	ASSERT_EQ(Replay.Run().Participants().size(), 2U);
	EXPECT_EQ(Replay.Run().Participants()[1].m_Name, "ordinary DDNet");
}

TEST(ImpactLiveAdapter, OrdinaryRemoteStartSurvivesLateTeamMetadata)
{
	CReplay Replay(92, TestContext(3));
	STickEventBatch Batch;
	Batch.m_ServerTick = 100;
	Batch.m_vCandidates.push_back(StartEvent(1, TestId(1), 100, "local AMF", 3));
	ASSERT_TRUE(Replay.Submit(Batch));

	// This models the live ordering: the remote current character was first
	// observed on Start while DDNet team metadata was not valid yet. The
	// generation remains deferred, so no new physical crossing is required
	// once the same non-zero team context arrives.
	EXPECT_TRUE(CImpactSystem::IsStartObservationConfirmed(true, false, true, false, false));
	EXPECT_FALSE(CImpactSystem::IsDeferredStartObservationConfirmed(true, false, false));
	EXPECT_TRUE(CImpactSystem::IsDeferredStartObservationConfirmed(true, true, false));
	Batch = {};
	Batch.m_ServerTick = 120;
	Batch.m_vCandidates.push_back(StartEvent(2, TestId(2), 120, "ordinary DDNet", 3));
	ASSERT_TRUE(Replay.Submit(Batch));

	EXPECT_EQ(Replay.Run().StartedParticipantCount(), 2);
	EXPECT_EQ(Replay.Run().EligibleParticipantCount(), 2);
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::ACTIVE);
}

TEST(ImpactLiveAdapter, OrdinaryRemoteSegmentCrossingStartIsEligibleWithoutAmfMetadata)
{
	CReplay Replay(93, TestContext(3));
	STickEventBatch Batch;
	Batch.m_ServerTick = 100;
	Batch.m_vCandidates.push_back(StartEvent(1, TestId(1), 100, "local AMF", 3));
	ASSERT_TRUE(Replay.Submit(Batch));

	// Snapshot N: the ordinary remote Tee is reliably observed before Start.
	// There is no current Start tile observation and no segment crossing yet.
	EXPECT_FALSE(CImpactSystem::IsStartObservationConfirmed(true, true, false, false, false));

	// Snapshot N+1: the current endpoint is already beyond Start and neither
	// endpoint is on the tile. CRaceHelper::IsStart(ClientId, Prev, Pos)
	// supplies the authoritative collision-map segment-crossing result here.
	const bool SegmentCrossesStart = true;
	ASSERT_TRUE(CImpactSystem::IsStartObservationConfirmed(false, true, false, SegmentCrossesStart, false));

	// The event is submitted only after the same start gate that the live
	// detector uses accepts the ordinary DDNet Tee. No AMF metadata is needed.
	Batch = {};
	Batch.m_ServerTick = 120;
	Batch.m_vCandidates.push_back(StartEvent(2, TestId(2), 120, "ordinary DDNet", 3));
	ASSERT_TRUE(Replay.Submit(Batch));

	EXPECT_EQ(Replay.Run().StartedParticipantCount(), 2);
	EXPECT_EQ(Replay.Run().EligibleParticipantCount(), 2);
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::ACTIVE);
}

TEST(ImpactLiveAdapter, RemotePlayerFirstSnapshotOnStartCounts)
{
	// This is the exact first-current-snapshot acceptance predicate used by
	// DetectStartCrossings for a remote character; it does not depend on the
	// local client id or on AMF metadata.
	EXPECT_TRUE(CImpactSystem::IsStartObservationConfirmed(true, false, true, false, false));
	EXPECT_FALSE(CImpactSystem::IsStartObservationConfirmed(true, false, false, false, false));

	CReplay Replay(108, TestContext(5));
	STickEventBatch Batch;
	Batch.m_ServerTick = 100;
	Batch.m_vCandidates.push_back(StartEvent(1, TestId(7, 3), 100, "remote", 5));
	ASSERT_TRUE(Replay.Submit(Batch));
	EXPECT_TRUE(Replay.Run().HasParticipant(TestId(7, 3)));
	EXPECT_EQ(Replay.Run().EligibleParticipantCount(), 1);
}

TEST(ImpactLiveAdapter, RemoteParticipantStartOutsideLocalObservationIsRecovered)
{
	CReplay Replay(114, TestContext(5));
	StartPair(Replay, 100, 110);

	// Character snapshots are legitimately clipped by the server when a remote
	// Tee is outside the observer's view. PlayerInfo + sixup PlayerInfoRace are
	// separate, server-authored roster/start evidence and recover that participant.
	ASSERT_TRUE(CImpactSystem::IsAuthoritativeStartedParticipant(true, true, 80, 200, 5, 5, 2, false));
	SImpactEvent RemoteStart = StartEvent(3, TestId(3, 7), 80, "remote-started", 5);
	RemoteStart.m_ResolveTick = 200;
	STickEventBatch Batch;
	Batch.m_ServerTick = 200;
	Batch.m_vCandidates.push_back(RemoteStart);
	ASSERT_TRUE(Replay.Submit(Batch));

	EXPECT_TRUE(Replay.Run().HasParticipant(TestId(3, 7)));
	EXPECT_EQ(Replay.Run().Participants().size(), 3U);
	EXPECT_EQ(Replay.Run().Participants()[2].m_FirstStartTick, 80);
}

TEST(ImpactLiveAdapter, LateAuthoritativeStartedParticipantJoinsCurrentRun)
{
	CReplay Replay(115, TestContext(5));
	StartPair(Replay, 100, 110);
	ASSERT_TRUE(CImpactSystem::IsAuthoritativeStartedParticipant(true, true, 120, 200, 5, 5, 3, false));

	SImpactEvent RemoteStart = StartEvent(3, TestId(3, 4), 120, "late-started", 5);
	RemoteStart.m_ResolveTick = 200;
	STickEventBatch Batch;
	Batch.m_ServerTick = 200;
	Batch.m_vCandidates.push_back(RemoteStart);
	ASSERT_TRUE(Replay.Submit(Batch));

	ASSERT_EQ(Replay.Run().Participants().size(), 3U);
	EXPECT_TRUE(Replay.Run().Participants()[2].m_Late);
	EXPECT_EQ(Replay.Run().Participants()[2].m_PersonalAnalysisStartActiveTicks, Replay.Run().ActiveAnalysisTicks(200));
}

TEST(ImpactLiveAdapter, SameTeamNotStartedIsRosterOnly)
{
	// This 0.7-only signal is still not proof that a non-started Tee is engaged;
	// exact-team authoritative roster membership is a separate admission rule.
	EXPECT_FALSE(CImpactSystem::IsAuthoritativeStartedParticipant(true, false, -1, 200, 5, 5, 3, false));
	EXPECT_FALSE(CImpactSystem::IsAuthoritativeStartedParticipant(true, true, -1, 200, 5, 5, 3, false));
	EXPECT_FALSE(CImpactSystem::IsAuthoritativeStartedParticipant(true, true, 220, 200, 5, 5, 3, false));
	EXPECT_FALSE(CImpactSystem::IsAuthoritativeStartedParticipant(true, true, 120, 200, 6, 5, 3, false));
	EXPECT_FALSE(CImpactSystem::IsAuthoritativeStartedParticipant(true, true, 120, 200, 5, 5, 1, false));

	CReplay Replay(2006, TestContext(5));
	EXPECT_TRUE(CImpactSystem::IsCurrentTeamRosterEntry(true, true, false, 5, 5, 7));
	EXPECT_FALSE(CImpactSystem::IsCurrentTeamRosterEntry(false, true, false, 5, 5, 7));
	EXPECT_FALSE(CImpactSystem::IsCurrentTeamRosterEntry(true, false, false, 5, 5, 7));
	EXPECT_FALSE(CImpactSystem::IsCurrentTeamRosterEntry(true, true, true, 5, 5, 7));
	EXPECT_FALSE(CImpactSystem::IsCurrentTeamRosterEntry(true, true, false, 6, 5, 7));
	EXPECT_FALSE(CImpactSystem::IsCurrentTeamRosterEntry(true, true, false, 5, 5, 0));
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(3, 7), "AFK", 200, "exact current TeamId roster"));
	EXPECT_TRUE(Replay.Run().HasParticipant(TestId(3, 7)));
	EXPECT_FALSE(Replay.Run().HasStartedParticipant(TestId(3, 7)));
	EXPECT_FALSE(Replay.Run().IsParticipantEngaged(TestId(3, 7)));
}

TEST(ImpactLiveAdapter, LateDiscoveredParticipantUsesCorrectGeneration)
{
	CReplay Replay(116, TestContext(5));
	StartPair(Replay, 100, 110);
	ASSERT_TRUE(CImpactSystem::IsAuthoritativeStartedParticipant(true, true, 120, 200, 5, 5, 3, false));

	SImpactEvent RemoteStart = StartEvent(3, TestId(3, 7), 120, "generation-seven", 5);
	RemoteStart.m_ResolveTick = 200;
	STickEventBatch Batch;
	Batch.m_ServerTick = 200;
	Batch.m_vCandidates.push_back(RemoteStart);
	ASSERT_TRUE(Replay.Submit(Batch));

	EXPECT_TRUE(Replay.Run().HasParticipant(TestId(3, 7)));
	EXPECT_FALSE(Replay.Run().HasParticipant(TestId(3, 6)));
}

TEST(ImpactLiveAdapter, LateDiscoveryDoesNotDuplicateExistingParticipant)
{
	CReplay Replay(117, TestContext(5));
	StartPair(Replay, 100, 110);
	SImpactEvent FirstRemoteStart = StartEvent(3, TestId(3, 2), 120, "remote", 5);
	FirstRemoteStart.m_ResolveTick = 200;
	STickEventBatch Batch;
	Batch.m_ServerTick = 200;
	Batch.m_vCandidates.push_back(FirstRemoteStart);
	ASSERT_TRUE(Replay.Submit(Batch));
	ASSERT_EQ(Replay.Run().Participants().size(), 3U);

	EXPECT_FALSE(CImpactSystem::IsAuthoritativeStartedParticipant(true, true, 120, 210, 5, 5, 3, true));
	EXPECT_EQ(Replay.Run().Participants().size(), 3U);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::START_CROSSING), 3);
}

TEST(ImpactLiveAdapter, LateDiscoveryDoesNotResetExistingStats)
{
	CReplay Replay(118, TestContext(1));
	StartPair(Replay, 100, 110);
	ASSERT_TRUE(CImpactSystem::IsAuthoritativeStartedParticipant(true, true, 120, 200, 1, 1, 3, false));
	SImpactEvent RemoteStart = StartEvent(3, TestId(3, 9), 120, "remote", 1);
	RemoteStart.m_ResolveTick = 200;
	STickEventBatch Batch;
	Batch.m_ServerTick = 200;
	Batch.m_vCandidates.push_back(RemoteStart);
	ASSERT_TRUE(Replay.Submit(Batch));

	SRawStats SaveDelta;
	SaveDelta.m_Saves = 1;
	ASSERT_TRUE(SubmitParticipantStats(Replay, 4, EImpactEventType::SAVE_CONFIRMED, TestId(3, 9), 220, SaveDelta));
	EXPECT_FALSE(CImpactSystem::IsAuthoritativeStartedParticipant(true, true, 120, 230, 1, 1, 3, true));
	EXPECT_EQ(Replay.Run().Participants()[2].m_RawStats.m_Saves, 1);
	EXPECT_EQ(Replay.Run().Participants().size(), 3U);
}

TEST(ImpactLiveAdapter, TemporarySnapshotGapDoesNotRemoveParticipant)
{
	CReplay Replay(119, TestContext());
	StartPair(Replay);
	EXPECT_FALSE(CImpactSystem::IsParticipantDisconnectConfirmed(1, 0, 50));
	EXPECT_FALSE(CImpactSystem::IsParticipantDisconnectConfirmed(2, 99, 50));
	EXPECT_FALSE(Replay.Run().IsParticipantFinalized(TestId(1)));
}

TEST(ImpactLiveAdapter, ParticipantDefinitivelyGoneStillFinalizesCorrectly)
{
	CReplay Replay(120, TestContext());
	StartPair(Replay);
	ASSERT_TRUE(CImpactSystem::IsParticipantDisconnectConfirmed(3, 100, 50));
	ASSERT_TRUE(FinalizeReplayParticipant(Replay, 3, TestId(1), 100));
	EXPECT_TRUE(Replay.Run().IsParticipantFinalized(TestId(1)));
}

TEST(ImpactLiveAdapter, AllStartedRemoteTeesAppearInRunStatistics)
{
	CReplay Replay(121, TestContext(5));
	StartPair(Replay, 100, 110);
	STickEventBatch Batch;
	Batch.m_ServerTick = 200;
	for(int ClientId = 3; ClientId <= 4; ++ClientId)
	{
		ASSERT_TRUE(CImpactSystem::IsAuthoritativeStartedParticipant(true, true, 90 + ClientId, 200, 5, 5, 4, false));
		SImpactEvent RemoteStart = StartEvent(ClientId, TestId(ClientId, 3), 90 + ClientId, "remote", 5);
		RemoteStart.m_ResolveTick = 200;
		Batch.m_vCandidates.push_back(RemoteStart);
	}
	ASSERT_TRUE(Replay.Submit(Batch));
	EXPECT_EQ(Replay.Run().StartedParticipantCount(), 4);
	EXPECT_EQ(Replay.Run().Participants().size(), 4U);
}

TEST(ImpactLiveAdapter, RemotePlayerSegmentCrossingStartCounts)
{
	// With a reliable previous/current pair, the map-segment crossing result is
	// sufficient even when neither endpoint is on the Start tile.
	EXPECT_TRUE(CImpactSystem::IsStartObservationConfirmed(false, true, false, true, false));

	CReplay Replay(109, TestContext(5));
	STickEventBatch Batch;
	Batch.m_ServerTick = 100;
	Batch.m_vCandidates.push_back(StartEvent(1, TestId(7, 3), 100, "remote", 5));
	ASSERT_TRUE(Replay.Submit(Batch));
	EXPECT_TRUE(Replay.Run().HasParticipant(TestId(7, 3)));
}

TEST(ImpactLiveAdapter, RemotePlayerDeferredStartBeforeTeamResolutionCounts)
{
	// The remote crossing is retained before team validation and is consumed
	// once the same non-zero team becomes confirmed. The accepted event keeps
	// the original crossing tick.
	EXPECT_TRUE(CImpactSystem::IsStartObservationConfirmed(true, false, true, false, false));
	EXPECT_FALSE(CImpactSystem::IsDeferredStartObservationConfirmed(true, false, false));
	EXPECT_TRUE(CImpactSystem::IsDeferredStartObservationConfirmed(true, true, false));

	CReplay Replay(110, TestContext(5));
	STickEventBatch Batch;
	Batch.m_ServerTick = 130;
	Batch.m_vCandidates.push_back(StartEvent(1, TestId(7, 3), 100, "remote", 5));
	ASSERT_TRUE(Replay.Submit(Batch));
	ASSERT_TRUE(Replay.Run().HasParticipant(TestId(7, 3)));
	EXPECT_EQ(Replay.Run().Participants()[0].m_FirstStartTick, 100);
}

TEST(ImpactLiveAdapter, RemotePlayerStartPromotesEligibility)
{
	CReplay Replay(111, TestContext(5));
	STickEventBatch Batch;
	Batch.m_ServerTick = 100;
	Batch.m_vCandidates.push_back(StartEvent(1, TestId(7, 3), 100, "remote", 5));
	ASSERT_TRUE(Replay.Submit(Batch));

	// This is the same source used by BuildGameplayFrames: personal eligibility
	// comes from the run-scoped participant, not from a frame-injected bit.
	EXPECT_TRUE(Replay.Run().HasParticipant(TestId(7, 3)));
	EXPECT_EQ(Replay.Run().EligibleParticipantCount(), 1);
}

TEST(ImpactLiveAdapter, RemotePlayerStartDoesNotLeakAcrossGenerationOrTeam)
{
	CReplay Replay(112, TestContext(5));
	STickEventBatch Batch;
	Batch.m_ServerTick = 100;
	Batch.m_vCandidates.push_back(StartEvent(1, TestId(7, 3), 100, "remote", 5));
	ASSERT_TRUE(Replay.Submit(Batch));

	// A slot generation/team change is not the old personal context. The
	// production Start gate rejects the old duplicate and the old team.
	EXPECT_FALSE(CImpactSystem::IsStartObservationConfirmed(false, true, true, true, true));
	EXPECT_FALSE(CImpactSystem::IsStartTeamContextUsable(15, 2, false, 5));

	CReplay NewTeamReplay(113, TestContext(15));
	Batch = {};
	Batch.m_ServerTick = 120;
	Batch.m_vCandidates.push_back(StartEvent(2, TestId(7, 4), 120, "remote-new-team", 15));
	ASSERT_TRUE(NewTeamReplay.Submit(Batch));
	EXPECT_TRUE(NewTeamReplay.Run().HasParticipant(TestId(7, 4)));
}

TEST(ImpactFoundation, SoloTimeDoesNotCount)
{
	CReplay Replay(1, TestContext());
	STickEventBatch Batch;
	Batch.m_ServerTick = 100;
	Batch.m_vCandidates.push_back(StartEvent(1, TestId(1), 100));
	EXPECT_TRUE(Replay.Submit(Batch));
	EXPECT_EQ(Replay.Run().Snapshot(1000).m_ActiveAnalysisTicks, 0);
	Batch = {};
	Batch.m_ServerTick = 1000;
	Batch.m_vCandidates.push_back(StartEvent(2, TestId(2), 1000));
	EXPECT_TRUE(Replay.Submit(Batch));
	EXPECT_EQ(Replay.Run().Snapshot(1100).m_ActiveAnalysisTicks, 100);
}

TEST(ImpactFoundation, LateParticipantHasPersonalStart)
{
	CReplay Replay(1, TestContext());
	StartPair(Replay);
	STickEventBatch Batch;
	Batch.m_ServerTick = 200;
	Batch.m_vCandidates.push_back(StartEvent(3, TestId(3), 200, "C"));
	EXPECT_TRUE(Replay.Submit(Batch));
	ASSERT_EQ(Replay.Run().Participants().size(), 3U);
	EXPECT_TRUE(Replay.Run().Participants()[2].m_Late);
	EXPECT_EQ(Replay.Run().Participants()[2].m_PersonalAnalysisStartTick, 200);
}

TEST(ImpactFoundation, TwoToOnePauses)
{
	CReplay Replay(1, TestContext());
	StartPair(Replay);
	EXPECT_TRUE(FinalizeReplayParticipant(Replay, 3, TestId(1), 100));
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::PAUSED_NO_TEAM);
}

TEST(ImpactFoundation, ThreeToTwoRemainsActive)
{
	CReplay Replay(1, TestContext());
	StartPair(Replay);
	STickEventBatch Batch;
	Batch.m_ServerTick = 100;
	Batch.m_vCandidates.push_back(StartEvent(3, TestId(3), 100));
	EXPECT_TRUE(Replay.Submit(Batch));
	Batch = {};
	Batch.m_ServerTick = 110;
	Batch.m_vCandidates.push_back(PresenceEvent(4, TestId(3), false, 110));
	EXPECT_TRUE(Replay.Submit(Batch));
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::ACTIVE);
}

TEST(ImpactFoundation, PausedRunResumesWhenRosterGrows)
{
	CReplay Replay(1, TestContext());
	StartPair(Replay);
	ASSERT_TRUE(FinalizeReplayParticipant(Replay, 3, TestId(1), 100));
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::PAUSED_NO_TEAM);
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(3), "Late roster member", 120, "authoritative current team roster"));
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::ACTIVE);
	EXPECT_EQ(Replay.Run().ActiveParticipantCount(), 2);
	EXPECT_FALSE(Replay.Run().IsParticipantEngaged(TestId(3)));
}

TEST(ImpactFoundation, NicknameChangeDoesNotResetIdentity)
{
	CReplay Replay(1, TestContext());
	StartPair(Replay);
	STickEventBatch Batch;
	Batch.m_ServerTick = 100;
	Batch.m_vCandidates.push_back(StartEvent(3, TestId(1), 100, "renamed"));
	EXPECT_TRUE(Replay.Submit(Batch));
	ASSERT_EQ(Replay.Run().Participants().size(), 2U);
	EXPECT_EQ(Replay.Run().Participants()[0].m_Id, TestId(1));
	EXPECT_EQ(Replay.Run().Participants()[0].m_Name, "renamed");
}

TEST(ImpactFoundation, SlotReuseGetsNewParticipant)
{
	CReplay Replay(1, TestContext());
	StartPair(Replay);
	STickEventBatch Batch;
	SImpactEvent A = StartEvent(3, TestId(1), 100);
	A.m_RawDelta.m_Saves = 1;
	Batch.m_ServerTick = 100;
	Batch.m_vCandidates.push_back(A);
	EXPECT_TRUE(Replay.Submit(Batch));
	Batch = {};
	Batch.m_ServerTick = 110;
	Batch.m_vCandidates.push_back(StartEvent(4, TestId(1, 2), 110, "new"));
	EXPECT_TRUE(Replay.Submit(Batch));
	ASSERT_EQ(Replay.Run().Participants().size(), 3U);
	EXPECT_EQ(Replay.Run().Participants()[0].m_RawStats.m_Saves, 1);
	EXPECT_EQ(Replay.Run().Participants()[2].m_RawStats.m_Saves, 0);
}

TEST(ImpactFoundation, MainAndDummyAreDistinct)
{
	CReplay Replay(1, TestContext());
	STickEventBatch Batch;
	Batch.m_ServerTick = 0;
	Batch.m_vCandidates = {StartEvent(1, TestId(1, 1, false), 0), StartEvent(2, TestId(2, 1, true), 0)};
	EXPECT_TRUE(Replay.Submit(Batch));
	EXPECT_EQ(Replay.Run().Participants().size(), 2U);
	EXPECT_NE(Replay.Run().Participants()[0].m_Id, Replay.Run().Participants()[1].m_Id);
	EXPECT_FALSE(Replay.Run().Participants()[0].m_Id.m_Dummy);
	EXPECT_TRUE(Replay.Run().Participants()[1].m_Id.m_Dummy);
}

TEST(ImpactFoundation, TeamZeroNeverActivatesImpact)
{
	CReplay Replay(1, TestContext(0));
	STickEventBatch Batch;
	Batch.m_ServerTick = 100;
	Batch.m_vCandidates = {
		StartEvent(1, TestId(1, 1, false), 100, "main", 0),
		StartEvent(2, TestId(2, 1, true), 100, "dummy", 0),
	};
	EXPECT_FALSE(Replay.Submit(Batch));
	EXPECT_NE(Replay.Run().State(), EAnalysisState::ACTIVE);
	EXPECT_TRUE(Replay.Run().Participants().empty());
}

TEST(ImpactFoundation, DifferentTeamsDoNotActivateImpact)
{
	CReplay Replay(1, TestContext(3));
	STickEventBatch Batch;
	Batch.m_ServerTick = 100;
	Batch.m_vCandidates.push_back(StartEvent(1, TestId(1), 100, "A", 3));
	EXPECT_TRUE(Replay.Submit(Batch));
	Batch = {};
	Batch.m_ServerTick = 110;
	Batch.m_vCandidates.push_back(StartEvent(2, TestId(2), 110, "B", 4));
	EXPECT_FALSE(Replay.Submit(Batch));
	EXPECT_NE(Replay.Run().State(), EAnalysisState::ACTIVE);
	EXPECT_EQ(Replay.Run().Participants().size(), 1U);
}

TEST(ImpactFoundation, SameTeamStartsActivateImpact)
{
	CReplay Replay(1, TestContext(3));
	StartPair(Replay);
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::ACTIVE);
	EXPECT_EQ(Replay.Run().Context().m_TeamId, 3);
	EXPECT_EQ(Replay.Run().Participants().size(), 2U);
}

TEST(ImpactFoundation, MainAndDummySameTeamActivateImpact)
{
	CReplay Replay(1, TestContext(5));
	STickEventBatch Batch;
	Batch.m_ServerTick = 100;
	Batch.m_vCandidates = {
		StartEvent(1, TestId(1, 1, false), 100, "main", 5),
		StartEvent(2, TestId(2, 1, true), 100, "dummy", 5),
	};
	EXPECT_TRUE(Replay.Submit(Batch));
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::ACTIVE);
	EXPECT_EQ(Replay.Run().Participants().size(), 2U);
}

TEST(ImpactFoundation, MainAndDummyDifferentTeamsDoNotActivateImpact)
{
	CReplay Replay(1, TestContext(5));
	STickEventBatch Batch;
	Batch.m_ServerTick = 100;
	Batch.m_vCandidates.push_back(StartEvent(1, TestId(1, 1, false), 100, "main", 5));
	EXPECT_TRUE(Replay.Submit(Batch));
	Batch = {};
	Batch.m_ServerTick = 110;
	Batch.m_vCandidates.push_back(StartEvent(2, TestId(2, 1, true), 110, "dummy", 6));
	EXPECT_FALSE(Replay.Submit(Batch));
	EXPECT_NE(Replay.Run().State(), EAnalysisState::ACTIVE);
	EXPECT_EQ(Replay.Run().Participants().size(), 1U);
}

TEST(ImpactFoundation, UnrelatedTeamIsExcludedFromRun)
{
	CReplay Replay(1, TestContext(2));
	STickEventBatch Batch;
	Batch.m_ServerTick = 100;
	Batch.m_vCandidates = {
		StartEvent(1, TestId(1), 100, "A", 2),
		StartEvent(2, TestId(2), 100, "B", 2),
	};
	EXPECT_TRUE(Replay.Submit(Batch));
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::ACTIVE);
	Batch = {};
	Batch.m_ServerTick = 110;
	Batch.m_vCandidates.push_back(StartEvent(3, TestId(3), 110, "C", 7));
	EXPECT_FALSE(Replay.Submit(Batch));
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::ACTIVE);
	EXPECT_EQ(Replay.Run().Participants().size(), 2U);
	EXPECT_FALSE(Replay.Run().HasParticipant(TestId(3)));
}

TEST(ImpactFoundation, TeamSwitchCannotMixIntoExistingRun)
{
	CReplay Replay(1, TestContext(3));
	StartPair(Replay);
	EXPECT_TRUE(FinalizeReplayParticipant(Replay, 3, TestId(2), 100));
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::PAUSED_NO_TEAM);

	STickEventBatch Batch;
	Batch.m_ServerTick = 110;
	Batch.m_vCandidates.push_back(StartEvent(4, TestId(2), 110, "B-new-team", 4));
	EXPECT_FALSE(Replay.Submit(Batch));
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::PAUSED_NO_TEAM);
	EXPECT_EQ(Replay.Run().Context().m_TeamId, 3);
	EXPECT_EQ(Replay.Run().Participants().size(), 2U);
	EXPECT_FALSE(Replay.Run().HasParticipant(TestId(3)));
}

TEST(ImpactFoundation, DummyFlagIsMetadataNotParticipantIdentity)
{
	EXPECT_EQ(TestId(1, 1, false), TestId(1, 1, true));
	EXPECT_NE(TestId(1, 1, false), TestId(2, 1, true));
}

TEST(ImpactFoundation, DuplicateEventIdIsIgnored)
{
	CReplay Replay(1, TestContext());
	STickEventBatch Batch;
	Batch.m_ServerTick = 0;
	Batch.m_vCandidates.push_back(StartEvent(1, TestId(1), 0));
	Batch.m_vCandidates.push_back(StartEvent(1, TestId(2), 0));
	EXPECT_TRUE(Replay.Submit(Batch));
	EXPECT_EQ(Replay.Run().Events().size(), 1U);
	EXPECT_EQ(Replay.Run().Participants().size(), 1U);
}

TEST(ImpactFoundation, BatchOrderIsCanonical)
{
	CReplay First(1, TestContext());
	CReplay Second(1, TestContext());
	STickEventBatch Batch;
	Batch.m_ServerTick = 10;
	Batch.m_vCandidates = {StartEvent(2, TestId(2), 10), StartEvent(1, TestId(1), 10)};
	EXPECT_TRUE(First.Submit(Batch));
	std::reverse(Batch.m_vCandidates.begin(), Batch.m_vCandidates.end());
	EXPECT_TRUE(Second.Submit(Batch));
	const SRunSnapshot FirstSnapshot = First.Run().Snapshot(20);
	const SRunSnapshot SecondSnapshot = Second.Run().Snapshot(20);
	EXPECT_EQ(FirstSnapshot.m_State, SecondSnapshot.m_State);
	EXPECT_EQ(FirstSnapshot.m_vParticipants.size(), SecondSnapshot.m_vParticipants.size());
	EXPECT_EQ(FirstSnapshot.m_vEvents.size(), SecondSnapshot.m_vEvents.size());
	for(size_t i = 0; i < FirstSnapshot.m_vEvents.size(); ++i)
		EXPECT_EQ(FirstSnapshot.m_vEvents[i].m_EventId, SecondSnapshot.m_vEvents[i].m_EventId);
}

TEST(ImpactFoundation, CheckpointRestoreKeepsState)
{
	CReplay Replay(1, TestContext());
	StartPair(Replay);
	const SRunSnapshot Before = Replay.Run().Snapshot(200);
	CImpactRun Restored;
	EXPECT_TRUE(Restored.RestoreSnapshot(Before));
	EXPECT_EQ(Restored.State(), Before.m_State);
	EXPECT_EQ(Restored.RunId(), Before.m_RunId);
	EXPECT_EQ(Restored.Participants().size(), Before.m_vParticipants.size());
}

TEST(ImpactFoundation, ActiveRunKeepsPhase4ScoreUnavailable)
{
	CReplay Replay(41, TestContext());
	StartPair(Replay);
	const SScoreSnapshot &Score = Replay.Run().Score();
	EXPECT_FALSE(Score.m_Available);
	EXPECT_EQ(Score.m_ScoringVersion, IMPACT_SCORING_VERSION_UNSET);
	EXPECT_DOUBLE_EQ(Score.m_Stability, 0.0);
	const SRunSnapshot SnapshotData = Replay.Run().Snapshot(20);
	EXPECT_FALSE(SnapshotData.m_Score.m_Available);
	EXPECT_EQ(SnapshotData.m_Score.m_ScoringVersion, IMPACT_SCORING_VERSION_UNSET);
	for(const SParticipant &Participant : SnapshotData.m_vParticipants)
		EXPECT_FALSE(Participant.m_Score.m_Available);
	EXPECT_TRUE(BuildFinalRanking(SnapshotData, true).empty());
}

static SParticipant MakeRankingParticipant(int ClientId, EParticipantTerminalStatus Status, bool Available = true,
	double Rating = 0.0, double Impact = 0.0, double Stability = 50.0, uint32_t Generation = 1, bool Engaged = true)
{
	SParticipant Participant;
	Participant.m_Id = TestId(ClientId, Generation);
	Participant.m_Engaged = Engaged;
	Participant.m_Finalized = true;
	Participant.m_TerminalStatus = Status;
	Participant.m_Score.m_Frozen = true;
	Participant.m_Score.m_Available = Available;
	Participant.m_Score.m_Rating = Rating;
	Participant.m_Score.m_Impact = Impact;
	Participant.m_Score.m_Stability = Stability;
	Participant.m_Score.m_RatingVersion = IMPACT_RATING_VERSION;
	Participant.m_Score.m_ScoringVersion = IMPACT_SCORING_VERSION;
	return Participant;
}

static SRunSnapshot MakeFinalRankingSnapshot()
{
	SRunSnapshot Snapshot;
	Snapshot.m_State = EAnalysisState::FINALIZED;
	Snapshot.m_Result = ERunResult::FINISHED;
	return Snapshot;
}

static void AddRankingParticipant(SRunSnapshot &Snapshot, int ClientId, EParticipantTerminalStatus Status,
	bool Available = true, double Rating = 0.0, double Impact = 0.0, double Stability = 50.0,
	uint32_t Generation = 1, bool Engaged = true)
{
	Snapshot.m_vParticipants.push_back(MakeRankingParticipant(ClientId, Status, Available, Rating, Impact, Stability, Generation, Engaged));
}

static std::vector<int> RankedClientIds(const SRunSnapshot &Snapshot, const std::vector<SFinalRankingEntry> &Ranking)
{
	std::vector<int> vClientIds;
	for(const SFinalRankingEntry &Entry : Ranking)
		vClientIds.push_back(Snapshot.m_vParticipants[Entry.m_ParticipantIndex].m_Id.m_ClientId);
	return vClientIds;
}

static int MvpClientId(const SRunSnapshot &Snapshot, const std::vector<SFinalRankingEntry> &Ranking)
{
	for(const SFinalRankingEntry &Entry : Ranking)
		if(Entry.m_IsMvp)
			return Snapshot.m_vParticipants[Entry.m_ParticipantIndex].m_Id.m_ClientId;
	return -1;
}

TEST(ImpactFoundation, FinalRankingRequiresFinalizedTerminalRunNotSavedFlag)
{
	SRunSnapshot SnapshotData;
	SnapshotData.m_State = EAnalysisState::ACTIVE;
	SnapshotData.m_Result = ERunResult::NONE;
	SParticipant Participant = MakeRankingParticipant(1, EParticipantTerminalStatus::NOT_FINALIZED);
	Participant.m_Finalized = false;
	SnapshotData.m_vParticipants.push_back(Participant);
	EXPECT_TRUE(BuildFinalRanking(SnapshotData, true).empty());

	SnapshotData.m_State = EAnalysisState::FINALIZED;
	SnapshotData.m_Result = ERunResult::FINISHED;
	SnapshotData.m_vParticipants[0].m_Finalized = true;
	SnapshotData.m_vParticipants[0].m_TerminalStatus = EParticipantTerminalStatus::FINISHED;
	const std::vector<SFinalRankingEntry> UnsavedFinalRanking = BuildFinalRanking(SnapshotData, false);
	ASSERT_EQ(UnsavedFinalRanking.size(), 1U);
	EXPECT_TRUE(UnsavedFinalRanking[0].m_IsMvp);
	const std::vector<SFinalRankingEntry> Ranking = BuildFinalRanking(SnapshotData, true);
	ASSERT_EQ(Ranking.size(), 1U);
	EXPECT_TRUE(Ranking[0].m_IsMvp);
}

TEST(ImpactFoundation, LeftReceivesPlaceByScoreButMvpRemainsFinished)
{
	SRunSnapshot SnapshotData;
	SnapshotData.m_State = EAnalysisState::FINALIZED;
	SnapshotData.m_Result = ERunResult::FINISHED;
	auto AddScoredParticipant = [&SnapshotData](const SParticipantId &Id, EParticipantTerminalStatus Status, double Rating) {
		SParticipant Participant = MakeRankingParticipant(Id.m_ClientId, Status, true, Rating, 0.0, 50.0, Id.m_SlotGeneration);
		SnapshotData.m_vParticipants.push_back(Participant);
	};
	AddScoredParticipant(TestId(1), EParticipantTerminalStatus::LEFT, 4.0);
	AddScoredParticipant(TestId(2), EParticipantTerminalStatus::FINISHED, 1.5);
	AddScoredParticipant(TestId(3), EParticipantTerminalStatus::FINISHED, 1.4);
	const std::vector<SFinalRankingEntry> Ranking = BuildFinalRanking(SnapshotData, true);
	ASSERT_EQ(Ranking.size(), 3U);
	EXPECT_EQ(Ranking[0].m_ParticipantIndex, 0U);
	EXPECT_EQ(Ranking[0].m_Rank, 1);
	EXPECT_FALSE(Ranking[0].m_IsMvp);
	EXPECT_EQ(Ranking[1].m_ParticipantIndex, 1U);
	EXPECT_EQ(Ranking[1].m_Rank, 2);
	EXPECT_TRUE(Ranking[1].m_IsMvp);
	EXPECT_EQ(Ranking[2].m_ParticipantIndex, 2U);
	EXPECT_EQ(Ranking[2].m_Rank, 3);
	EXPECT_FALSE(Ranking[2].m_IsMvp);
}

TEST(ImpactFoundation, FinalRankingUsesExactHiddenPrecision)
{
	SRunSnapshot SnapshotData;
	SnapshotData.m_State = EAnalysisState::FINALIZED;
	SnapshotData.m_Result = ERunResult::FINISHED;
	for(const auto &[ClientId, Rating] : {std::pair{2, 1.429734}, std::pair{1, 1.429731}})
	{
		SParticipant Participant = MakeRankingParticipant(ClientId, EParticipantTerminalStatus::FINISHED, true, Rating, 5.0, 90.0);
		SnapshotData.m_vParticipants.push_back(Participant);
	}
	const std::vector<SFinalRankingEntry> Ranking = BuildFinalRanking(SnapshotData, true);
	ASSERT_EQ(Ranking.size(), 2U);
	EXPECT_EQ(SnapshotData.m_vParticipants[Ranking[0].m_ParticipantIndex].m_Id.m_ClientId, 2);
	EXPECT_EQ(SnapshotData.m_vParticipants[Ranking[1].m_ParticipantIndex].m_Id.m_ClientId, 1);
	EXPECT_EQ(FormatTruncatedRating(1.429734), "1.42");
	EXPECT_EQ(FormatTruncatedRating(1.2), "1.20");
	EXPECT_EQ(FormatTruncatedImpact(5.279412), "+5.27%");
	EXPECT_EQ(FormatTruncatedImpact(-5.279412), "-5.27%");
	EXPECT_EQ(FormatTruncatedStability(87.963), "87%");
	EXPECT_EQ(FormatTruncatedRating(std::numeric_limits<double>::infinity()), "—");
}

TEST(ImpactRanking, FinishedPlayersRankByExactRatingDescending)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 3, EParticipantTerminalStatus::FINISHED, true, 1.0);
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::FINISHED, true, 2.0);
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::FINISHED, true, 1.5);
	EXPECT_EQ(RankedClientIds(Snapshot, BuildFinalRanking(Snapshot, true)), (std::vector<int>{2, 1, 3}));
}

TEST(ImpactRanking, EqualRatingRanksByExactImpactDescending)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::FINISHED, true, 1.0, 3.0);
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::FINISHED, true, 1.0, 4.0);
	EXPECT_EQ(RankedClientIds(Snapshot, BuildFinalRanking(Snapshot, true)), (std::vector<int>{1, 2}));
}

TEST(ImpactRanking, EqualRatingImpactRanksByExactStabilityDescending)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::FINISHED, true, 1.0, 4.0, 60.0);
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::FINISHED, true, 1.0, 4.0, 70.0);
	EXPECT_EQ(RankedClientIds(Snapshot, BuildFinalRanking(Snapshot, true)), (std::vector<int>{1, 2}));
}

TEST(ImpactRanking, ExactMetricTieUsesDeterministicFallback)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 8, EParticipantTerminalStatus::FINISHED, true, 1.0, 2.0, 50.0, 2);
	AddRankingParticipant(Snapshot, 3, EParticipantTerminalStatus::FINISHED, true, 1.0, 2.0, 50.0, 4);
	AddRankingParticipant(Snapshot, 3, EParticipantTerminalStatus::FINISHED, true, 1.0, 2.0, 50.0, 1);
	EXPECT_EQ(RankedClientIds(Snapshot, BuildFinalRanking(Snapshot, true)), (std::vector<int>{3, 3, 8}));
	const auto Ranking = BuildFinalRanking(Snapshot, true);
	ASSERT_EQ(Ranking.size(), 3U);
	EXPECT_EQ(Snapshot.m_vParticipants[Ranking[0].m_ParticipantIndex].m_Id.m_SlotGeneration, 1U);
	EXPECT_EQ(Snapshot.m_vParticipants[Ranking[1].m_ParticipantIndex].m_Id.m_SlotGeneration, 4U);
}

TEST(ImpactRanking, PlacesAreAlwaysUnique)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	for(int ClientId = 4; ClientId >= 1; --ClientId)
		AddRankingParticipant(Snapshot, ClientId, EParticipantTerminalStatus::FINISHED, true, 1.0, 2.0, 50.0);
	const auto Ranking = BuildFinalRanking(Snapshot, true);
	ASSERT_EQ(Ranking.size(), 4U);
	for(size_t i = 0; i < Ranking.size(); ++i)
		EXPECT_EQ(Ranking[i].m_Rank, static_cast<int>(i + 1));
}

TEST(ImpactRanking, ExactScoresDetermineOrderRegardlessOfTerminalStatus)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::LEFT, true, 5.0);
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::FINISHED, true, -2.0);
	EXPECT_EQ(RankedClientIds(Snapshot, BuildFinalRanking(Snapshot, true)), (std::vector<int>{1, 2}));
}

TEST(ImpactRanking, LeftWithHigherRatingRanksFirstButCannotBeMvp)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::LEFT, true, 9.0);
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::FINISHED, true, 0.1);
	const auto Ranking = BuildFinalRanking(Snapshot, true);
	EXPECT_EQ(RankedClientIds(Snapshot, Ranking), (std::vector<int>{1, 2}));
	EXPECT_EQ(MvpClientId(Snapshot, Ranking), 2);
}

TEST(ImpactRanking, AvailableRanksBeforeUnavailableWithinSameTerminalGroup)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::FINISHED, false, 100.0);
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::FINISHED, true, 0.0);
	EXPECT_EQ(RankedClientIds(Snapshot, BuildFinalRanking(Snapshot, true)), (std::vector<int>{2}));
}

TEST(ImpactRanking, FinishedUnavailableStillRanksBeforeLeftAvailable)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::LEFT, true, 99.0);
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::FINISHED, false, 0.0);
	EXPECT_EQ(RankedClientIds(Snapshot, BuildFinalRanking(Snapshot, true)), (std::vector<int>{1}));
}

TEST(ImpactRanking, UnavailableScoreDoesNotBecomeNumericZero)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::FINISHED, false, 1000.0);
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::FINISHED, true, 0.0);
	const auto Ranking = BuildFinalRanking(Snapshot, true);
	ASSERT_EQ(Ranking.size(), 1U);
	EXPECT_EQ(Snapshot.m_vParticipants[Ranking[0].m_ParticipantIndex].m_Id.m_ClientId, 2);
}

TEST(ImpactRanking, SuccessfulFinishAt120SecondsHasRankingAndMvp)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	Snapshot.m_ActiveAnalysisTicks = 120 * Snapshot.m_Context.m_TickSpeed;
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::FINISHED, true, 1.0);
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::FINISHED, true, 0.5);
	const auto Ranking = BuildFinalRanking(Snapshot, true);
	ASSERT_EQ(Ranking.size(), 2U);
	EXPECT_EQ(Ranking[0].m_Rank, 1);
	EXPECT_EQ(Ranking[1].m_Rank, 2);
	EXPECT_EQ(MvpClientId(Snapshot, Ranking), 1);
}

TEST(ImpactRanking, SuccessfulFinishAt299SecondsHasMvp)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	Snapshot.m_ActiveAnalysisTicks = 299 * Snapshot.m_Context.m_TickSpeed;
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::FINISHED, true, 1.0);
	EXPECT_EQ(MvpClientId(Snapshot, BuildFinalRanking(Snapshot, true)), 1);
}

TEST(ImpactRanking, TeamResetAt30SecondsHasPlacesButNoMvp)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	Snapshot.m_Result = ERunResult::TEAM_RESET;
	Snapshot.m_ActiveAnalysisTicks = 30 * Snapshot.m_Context.m_TickSpeed;
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::OTHER, true, 1.2);
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::OTHER, true, 0.8);
	const auto Ranking = BuildFinalRanking(Snapshot, true);
	EXPECT_EQ(RankedClientIds(Snapshot, Ranking), (std::vector<int>{1, 2}));
	ASSERT_EQ(Ranking.size(), 2U);
	EXPECT_EQ(Ranking[0].m_Rank, 1);
	EXPECT_EQ(Ranking[1].m_Rank, 2);
	EXPECT_EQ(MvpClientId(Snapshot, Ranking), -1);
}

TEST(ImpactRanking, SquareOfGoresTeamResetAtThreeThirtyKeepsPlacesWithoutMvp)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	Snapshot.m_Context.m_Map = "Square_of_Gores_2";
	Snapshot.m_Result = ERunResult::TEAM_RESET;
	Snapshot.m_ActiveAnalysisTicks = 210 * Snapshot.m_Context.m_TickSpeed;
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::OTHER, true, 1.6006270875959445, 7.0261801729998581, 60.7843137254902);
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::OTHER, true, 0.75393746391193861, -0.063044809168170485, 40.74074074074074);
	const auto Ranking = BuildFinalRanking(Snapshot, true);
	EXPECT_EQ(RankedClientIds(Snapshot, Ranking), (std::vector<int>{1, 2}));
	ASSERT_EQ(Ranking.size(), 2U);
	EXPECT_EQ(Ranking[0].m_Rank, 1);
	EXPECT_EQ(Ranking[1].m_Rank, 2);
	EXPECT_EQ(MvpClientId(Snapshot, Ranking), -1);
}

TEST(ImpactRanking, NonFinishMvpRequiresAtLeast300ExactSeconds)
{
	for(const auto &[DurationTicks, ExpectedMvp] : {std::pair<int64_t, int>{299999, -1}, {300000, 1}, {301000, 1}})
	{
		SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
		Snapshot.m_Result = ERunResult::TEAM_RESET;
		Snapshot.m_Context.m_TickSpeed = 1000;
		Snapshot.m_ActiveAnalysisTicks = DurationTicks;
		AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::OTHER, true, 1.0);
		const auto Ranking = BuildFinalRanking(Snapshot, true);
		ASSERT_EQ(Ranking.size(), 1U);
		EXPECT_EQ(Ranking[0].m_Rank, 1);
		EXPECT_EQ(MvpClientId(Snapshot, Ranking), ExpectedMvp);
	}
}

TEST(ImpactRanking, NonFinishMvpAllowsOtherAndExcludesHighestScoringLeftParticipant)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	Snapshot.m_Result = ERunResult::TEAM_RESET;
	Snapshot.m_ActiveAnalysisTicks = 300 * Snapshot.m_Context.m_TickSpeed;
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::LEFT, true, 9.0);
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::OTHER, true, 1.5);
	AddRankingParticipant(Snapshot, 3, EParticipantTerminalStatus::OTHER, true, 1.4);
	const auto Ranking = BuildFinalRanking(Snapshot, true);
	EXPECT_EQ(RankedClientIds(Snapshot, Ranking), (std::vector<int>{1, 2, 3}));
	ASSERT_EQ(Ranking.size(), 3U);
	EXPECT_EQ(Ranking[0].m_Rank, 1);
	EXPECT_FALSE(Ranking[0].m_IsMvp);
	EXPECT_EQ(Ranking[1].m_Rank, 2);
	EXPECT_TRUE(Ranking[1].m_IsMvp);
	EXPECT_EQ(MvpClientId(Snapshot, Ranking), 2);
}

TEST(ImpactRanking, NonFinishUnknownOrMalformedDurationLeavesMvpUnavailable)
{
	for(const auto &[TickSpeed, DurationTicks] : {std::pair<int, int64_t>{50, -1}, {0, 15000}, {1001, 301000}})
	{
		SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
		Snapshot.m_Result = ERunResult::TEAM_RESET;
		Snapshot.m_Context.m_TickSpeed = TickSpeed;
		Snapshot.m_ActiveAnalysisTicks = DurationTicks;
		AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::OTHER, true, 1.0);
		const auto Ranking = BuildFinalRanking(Snapshot, true);
		ASSERT_EQ(Ranking.size(), 1U);
		EXPECT_EQ(Ranking[0].m_Rank, 1);
		EXPECT_EQ(MvpClientId(Snapshot, Ranking), -1);
	}
}

TEST(ImpactRanking, NonFinishHistoryRoundTripDerivesPlacesAndMvpFromFrozenSnapshot)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	Snapshot.m_RunId = 7050;
	Snapshot.m_Context.m_Map = "Square_of_Gores_2";
	Snapshot.m_Result = ERunResult::TEAM_RESET;
	Snapshot.m_ActiveAnalysisTicks = 210 * Snapshot.m_Context.m_TickSpeed;
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::OTHER, true, 1.6006270875959445, 7.0261801729998581, 60.7843137254902);
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::OTHER, true, 0.75393746391193861, -0.063044809168170485, 40.74074074074074);
	const std::vector<SParticipant> FrozenParticipants = Snapshot.m_vParticipants;
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {Snapshot}));
	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), 1U);
	const SImpactRunPresentation Presentation = BuildImpactRunPresentation(Loaded[0]);
	EXPECT_EQ(RankedClientIds(Loaded[0], Presentation.m_vRanking), (std::vector<int>{1, 2}));
	EXPECT_EQ(Presentation.m_vPlaces, (std::vector<int>{1, 2}));
	EXPECT_FALSE(Presentation.m_HasMvp);
	for(size_t i = 0; i < FrozenParticipants.size(); ++i)
	{
		EXPECT_DOUBLE_EQ(Loaded[0].m_vParticipants[i].m_Score.m_Rating, FrozenParticipants[i].m_Score.m_Rating);
		EXPECT_DOUBLE_EQ(Loaded[0].m_vParticipants[i].m_Score.m_Impact, FrozenParticipants[i].m_Score.m_Impact);
		EXPECT_DOUBLE_EQ(Loaded[0].m_vParticipants[i].m_Score.m_Stability, FrozenParticipants[i].m_Score.m_Stability);
	}
}

TEST(ImpactRanking, RatingFloorTieUsesImpact)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::FINISHED, true, 0.0, -25.0);
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::FINISHED, true, 0.0, -20.0);
	EXPECT_EQ(RankedClientIds(Snapshot, BuildFinalRanking(Snapshot, true)), (std::vector<int>{1, 2}));
}

TEST(ImpactRanking, DisplayTruncationDoesNotAffectRatingOrdering)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::FINISHED, true, 1.6001, 5.0);
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::FINISHED, true, 1.6099, 5.0);
	EXPECT_EQ(FormatTruncatedRating(1.6001), "1.60");
	EXPECT_EQ(FormatTruncatedRating(1.6099), "1.60");
	EXPECT_EQ(RankedClientIds(Snapshot, BuildFinalRanking(Snapshot, true)), (std::vector<int>{1, 2}));
}

TEST(ImpactRanking, ImpactDisplayTruncationDoesNotAffectTieBreak)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::FINISHED, true, 1.0, 6.001);
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::FINISHED, true, 1.0, 6.009);
	EXPECT_EQ(FormatTruncatedImpact(6.001), "+6.00%");
	EXPECT_EQ(FormatTruncatedImpact(6.009), "+6.00%");
	EXPECT_EQ(RankedClientIds(Snapshot, BuildFinalRanking(Snapshot, true)), (std::vector<int>{1, 2}));
}

TEST(ImpactRanking, StabilityDisplayTruncationDoesNotAffectTieBreak)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::FINISHED, true, 1.0, 4.0, 87.01);
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::FINISHED, true, 1.0, 4.0, 87.99);
	EXPECT_EQ(FormatTruncatedStability(87.01), "87%");
	EXPECT_EQ(FormatTruncatedStability(87.99), "87%");
	EXPECT_EQ(RankedClientIds(Snapshot, BuildFinalRanking(Snapshot, true)), (std::vector<int>{1, 2}));
}

TEST(ImpactRanking, MvpIsTopEligibleFinishedPlayer)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::FINISHED, true, 1.0);
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::FINISHED, true, 2.0);
	AddRankingParticipant(Snapshot, 3, EParticipantTerminalStatus::LEFT, true, 9.0);
	EXPECT_EQ(MvpClientId(Snapshot, BuildFinalRanking(Snapshot, true)), 2);
}

TEST(ImpactRanking, MvpUsesExactFullPrecisionRating)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::FINISHED, true, 1.6001);
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::FINISHED, true, 1.6099);
	EXPECT_EQ(MvpClientId(Snapshot, BuildFinalRanking(Snapshot, true)), 1);
}

TEST(ImpactRanking, MvpUsesImpactTieBreakWhenRatingEqual)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::FINISHED, true, 1.0, 4.0);
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::FINISHED, true, 1.0, 5.0);
	EXPECT_EQ(MvpClientId(Snapshot, BuildFinalRanking(Snapshot, true)), 1);
}

TEST(ImpactRanking, MvpUsesStabilityTieBreakWhenNeeded)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::FINISHED, true, 1.0, 4.0, 60.0);
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::FINISHED, true, 1.0, 4.0, 70.0);
	EXPECT_EQ(MvpClientId(Snapshot, BuildFinalRanking(Snapshot, true)), 1);
}

TEST(ImpactRanking, MvpUsesDeterministicFallbackForExactTie)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 8, EParticipantTerminalStatus::FINISHED, true, 1.0, 4.0, 70.0);
	AddRankingParticipant(Snapshot, 3, EParticipantTerminalStatus::FINISHED, true, 1.0, 4.0, 70.0);
	EXPECT_EQ(MvpClientId(Snapshot, BuildFinalRanking(Snapshot, true)), 3);
}

TEST(ImpactRanking, ExactlyOneMvpOnExactTie)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	for(int ClientId = 1; ClientId <= 5; ++ClientId)
		AddRankingParticipant(Snapshot, ClientId, EParticipantTerminalStatus::FINISHED, true, 1.0, 4.0, 70.0);
	const auto Ranking = BuildFinalRanking(Snapshot, true);
	EXPECT_EQ(std::count_if(Ranking.begin(), Ranking.end(), [](const SFinalRankingEntry &Entry) { return Entry.m_IsMvp; }), 1);
}

TEST(ImpactRanking, LeftCanNeverBeMvp)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::LEFT, true, 10.0);
	const auto Ranking = BuildFinalRanking(Snapshot, true);
	ASSERT_EQ(Ranking.size(), 1U);
	EXPECT_FALSE(Ranking[0].m_IsMvp);
}

TEST(ImpactRanking, UnavailableScoreCanNeverBeMvp)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::FINISHED, false, 1.0);
	const auto Ranking = BuildFinalRanking(Snapshot, true);
	EXPECT_TRUE(Ranking.empty());
	EXPECT_EQ(MvpClientId(Snapshot, Ranking), -1);
}

TEST(ImpactRanking, NotEngagedCanNeverBeMvp)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::FINISHED, true, 5.0, 8.0, 90.0, 1, false);
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::FINISHED, true, 1.0, 2.0, 50.0);
	EXPECT_EQ(MvpClientId(Snapshot, BuildFinalRanking(Snapshot, true)), 2);
}

TEST(ImpactRanking, NoFinishedPlayersMeansNoMvp)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::LEFT, true, 5.0);
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::LEFT, false);
	EXPECT_EQ(MvpClientId(Snapshot, BuildFinalRanking(Snapshot, true)), -1);
}

TEST(ImpactRanking, ActiveRunHasNoRankingButFinalizedRunDoes)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	Snapshot.m_State = EAnalysisState::ACTIVE;
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::FINISHED, true, 5.0);
	EXPECT_TRUE(BuildFinalRanking(Snapshot, true).empty());
	Snapshot.m_State = EAnalysisState::FINALIZED;
	const auto Ranking = BuildFinalRanking(Snapshot, false);
	ASSERT_EQ(Ranking.size(), 1U);
	EXPECT_TRUE(Ranking[0].m_IsMvp);
}

TEST(ImpactRanking, FrozenResultProducesDeterministicMvp)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::FINISHED, true, 1.1);
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::FINISHED, true, 1.2);
	const auto First = BuildFinalRanking(Snapshot, true);
	const auto Second = BuildFinalRanking(Snapshot, true);
	EXPECT_EQ(RankedClientIds(Snapshot, First), RankedClientIds(Snapshot, Second));
	EXPECT_EQ(MvpClientId(Snapshot, First), MvpClientId(Snapshot, Second));
}

TEST(ImpactRanking, ContinuingRunCannotExposePrematureMvp)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	Snapshot.m_State = EAnalysisState::ACTIVE;
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::LEFT, true, 9.0);
	EXPECT_TRUE(BuildFinalRanking(Snapshot, false).empty());
}

TEST(ImpactRanking, FrozenLeftScoreIsNotMutatedByRanking)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::LEFT, true, 2.0, 3.0, 4.0);
	const SScoreSnapshot Before = Snapshot.m_vParticipants[0].m_Score;
	(void)BuildFinalRanking(Snapshot, true);
	const SScoreSnapshot &After = Snapshot.m_vParticipants[0].m_Score;
	EXPECT_DOUBLE_EQ(After.m_Rating, Before.m_Rating);
	EXPECT_DOUBLE_EQ(After.m_Impact, Before.m_Impact);
	EXPECT_DOUBLE_EQ(After.m_Stability, Before.m_Stability);
	EXPECT_EQ(After.m_Frozen, Before.m_Frozen);
}

TEST(ImpactRanking, RankingDoesNotMutateScoreSnapshots)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::FINISHED, true, 1.5, 4.0, 60.0);
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::FINISHED, false, 0.0, 0.0, 0.0);
	const auto Before = Snapshot.m_vParticipants;
	(void)BuildFinalRanking(Snapshot, true);
	ASSERT_EQ(Snapshot.m_vParticipants.size(), Before.size());
	for(size_t i = 0; i < Before.size(); ++i)
	{
		EXPECT_DOUBLE_EQ(Snapshot.m_vParticipants[i].m_Score.m_Rating, Before[i].m_Score.m_Rating);
		EXPECT_DOUBLE_EQ(Snapshot.m_vParticipants[i].m_Score.m_Impact, Before[i].m_Score.m_Impact);
		EXPECT_DOUBLE_EQ(Snapshot.m_vParticipants[i].m_Score.m_Stability, Before[i].m_Score.m_Stability);
		EXPECT_EQ(Snapshot.m_vParticipants[i].m_Score.m_Available, Before[i].m_Score.m_Available);
	}
}

TEST(ImpactRanking, RankingDoesNotRecalculateScores)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::FINISHED, true, 1.5, 4.0, 60.0);
	Snapshot.m_vParticipants[0].m_RawStats.m_Saves = 500;
	Snapshot.m_vParticipants[0].m_RawStats.m_Falls = 300;
	const SScoreSnapshot Frozen = Snapshot.m_vParticipants[0].m_Score;
	(void)BuildFinalRanking(Snapshot, true);
	EXPECT_DOUBLE_EQ(Snapshot.m_vParticipants[0].m_Score.m_Rating, Frozen.m_Rating);
	EXPECT_DOUBLE_EQ(Snapshot.m_vParticipants[0].m_Score.m_Impact, Frozen.m_Impact);
	EXPECT_DOUBLE_EQ(Snapshot.m_vParticipants[0].m_Score.m_Stability, Frozen.m_Stability);
}

TEST(ImpactRanking, MvpDoesNotReadRawCounters)
{
	SRunSnapshot A = MakeFinalRankingSnapshot();
	SRunSnapshot B = MakeFinalRankingSnapshot();
	AddRankingParticipant(A, 1, EParticipantTerminalStatus::FINISHED, true, 1.4, 5.0, 70.0);
	AddRankingParticipant(B, 1, EParticipantTerminalStatus::FINISHED, true, 1.4, 5.0, 70.0);
	B.m_vParticipants[0].m_RawStats.m_Saves = 9999;
	B.m_vParticipants[0].m_RawStats.m_Falls = 7777;
	B.m_vParticipants[0].m_RawStats.m_Aleds = 123;
	B.m_vParticipants[0].m_RawStats.m_Clutches = 456;
	EXPECT_EQ(MvpClientId(A, BuildFinalRanking(A, true)), MvpClientId(B, BuildFinalRanking(B, true)));
}

TEST(ImpactRanking, RankingDoesNotReadRawCounters)
{
	SRunSnapshot A = MakeFinalRankingSnapshot();
	SRunSnapshot B = MakeFinalRankingSnapshot();
	for(int ClientId : {1, 2})
	{
		AddRankingParticipant(A, ClientId, EParticipantTerminalStatus::FINISHED, true, ClientId, ClientId, 50.0);
		AddRankingParticipant(B, ClientId, EParticipantTerminalStatus::FINISHED, true, ClientId, ClientId, 50.0);
	}
	B.m_vParticipants[0].m_RawStats.m_Saves = 1000;
	B.m_vParticipants[0].m_RawStats.m_Falls = 1000;
	B.m_vParticipants[1].m_RawStats.m_Clutches = 1000;
	B.m_vParticipants[1].m_RawStats.m_Aleds = 1000;
	EXPECT_EQ(RankedClientIds(A, BuildFinalRanking(A, true)), RankedClientIds(B, BuildFinalRanking(B, true)));
}

TEST(ImpactRanking, RankingBeforeAfterHistoryRoundTripIsIdentical)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	CReplay Replay(7020, TestContext());
	StartPair(Replay);
	ASSERT_TRUE(Replay.Run().Finalize(ERunResult::FINISHED, 100));
	SRunSnapshot Snapshot = Replay.Run().Snapshot(100, 1234);
	for(size_t i = 0; i < Snapshot.m_vParticipants.size(); ++i)
	{
		SParticipant &Participant = Snapshot.m_vParticipants[i];
		Participant.m_Engaged = true;
		Participant.m_Finalized = true;
		Participant.m_TerminalStatus = EParticipantTerminalStatus::FINISHED;
		Participant.m_Score = MakeRankingParticipant(Participant.m_Id.m_ClientId, EParticipantTerminalStatus::FINISHED,
			true, i == 0 ? 1.6099 : 1.6001, i == 0 ? 6.009 : 6.001, i == 0 ? 87.99 : 87.01,
			Participant.m_Id.m_SlotGeneration).m_Score;
	}
	const auto Before = BuildFinalRanking(Snapshot, true);
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {Snapshot}));
	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), 1U);
	const auto After = BuildFinalRanking(Loaded[0], true);
	EXPECT_EQ(RankedClientIds(Snapshot, Before), RankedClientIds(Loaded[0], After));
	for(size_t i = 0; i < Before.size(); ++i)
		EXPECT_EQ(Before[i].m_Rank, After[i].m_Rank);
}

TEST(ImpactRanking, MvpBeforeAfterHistoryRoundTripIsIdentical)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	CReplay Replay(7021, TestContext());
	StartPair(Replay);
	ASSERT_TRUE(Replay.Run().Finalize(ERunResult::FINISHED, 100));
	SRunSnapshot Snapshot = Replay.Run().Snapshot(100, 1234);
	for(size_t i = 0; i < Snapshot.m_vParticipants.size(); ++i)
	{
		SParticipant &Participant = Snapshot.m_vParticipants[i];
		Participant.m_Score = MakeRankingParticipant(Participant.m_Id.m_ClientId, EParticipantTerminalStatus::FINISHED,
			true, i == 0 ? 1.5 : 1.6, i == 0 ? 5.0 : 6.0, 60.0, Participant.m_Id.m_SlotGeneration).m_Score;
	}
	const int MvpBefore = MvpClientId(Snapshot, BuildFinalRanking(Snapshot, true));
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {Snapshot}));
	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), 1U);
	EXPECT_EQ(MvpBefore, MvpClientId(Loaded[0], BuildFinalRanking(Loaded[0], true)));
}

TEST(ImpactRanking, NewGenerationDoesNotAffectOldRanking)
{
	SRunSnapshot OldRun = MakeFinalRankingSnapshot();
	AddRankingParticipant(OldRun, 4, EParticipantTerminalStatus::FINISHED, true, 1.0, 2.0, 50.0, 7);
	const auto OldBefore = BuildFinalRanking(OldRun, true);
	SRunSnapshot NewRun = MakeFinalRankingSnapshot();
	AddRankingParticipant(NewRun, 4, EParticipantTerminalStatus::FINISHED, true, 99.0, 99.0, 99.0, 8);
	EXPECT_EQ(RankedClientIds(OldRun, OldBefore), RankedClientIds(OldRun, BuildFinalRanking(OldRun, true)));
	EXPECT_NE(OldRun.m_vParticipants[0].m_Id.m_SlotGeneration, NewRun.m_vParticipants[0].m_Id.m_SlotGeneration);
}

TEST(ImpactRanking, NewRunDoesNotAffectOldRanking)
{
	SRunSnapshot OldRun = MakeFinalRankingSnapshot();
	AddRankingParticipant(OldRun, 1, EParticipantTerminalStatus::FINISHED, true, 1.0);
	const auto OldBefore = BuildFinalRanking(OldRun, true);
	SRunSnapshot NewRun = MakeFinalRankingSnapshot();
	AddRankingParticipant(NewRun, 1, EParticipantTerminalStatus::FINISHED, true, 10.0);
	EXPECT_EQ(RankedClientIds(OldRun, OldBefore), RankedClientIds(OldRun, BuildFinalRanking(OldRun, true)));
}

TEST(ImpactRanking, ToxigonRanksAlyaAshuraJoland)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::FINISHED, true, 1.6006270875959445, 7.0261801729998581, 60.7843137254902);
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::FINISHED, true, 0.75393746391193861, -0.063044809168170485, 40.74074074074074);
	AddRankingParticipant(Snapshot, 3, EParticipantTerminalStatus::FINISHED, true, 0.50090080248142099, -3.3790851245995128, 31.668856767411302);
	EXPECT_EQ(RankedClientIds(Snapshot, BuildFinalRanking(Snapshot, true)), (std::vector<int>{1, 2, 3}));
}

TEST(ImpactRanking, ToxigonMvpIsAlya)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::FINISHED, true, 1.6006270875959445, 7.0261801729998581, 60.7843137254902);
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::FINISHED, true, 0.75393746391193861, -0.063044809168170485, 40.74074074074074);
	AddRankingParticipant(Snapshot, 3, EParticipantTerminalStatus::FINISHED, true, 0.50090080248142099, -3.3790851245995128, 31.668856767411302);
	EXPECT_EQ(MvpClientId(Snapshot, BuildFinalRanking(Snapshot, true)), 1);
}

TEST(ImpactRanking, ToxigonLeftVariantRanksByExactScore)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::LEFT, true, 1.6006270875959445, 7.0261801729998581, 60.7843137254902);
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::FINISHED, true, 0.75393746391193861, -0.063044809168170485, 40.74074074074074);
	AddRankingParticipant(Snapshot, 3, EParticipantTerminalStatus::FINISHED, true, 0.50090080248142099, -3.3790851245995128, 31.668856767411302);
	EXPECT_EQ(RankedClientIds(Snapshot, BuildFinalRanking(Snapshot, true)), (std::vector<int>{1, 2, 3}));
}

TEST(ImpactRanking, ToxigonLeftVariantMvpIsAshura)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::LEFT, true, 1.6006270875959445, 7.0261801729998581, 60.7843137254902);
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::FINISHED, true, 0.75393746391193861, -0.063044809168170485, 40.74074074074074);
	AddRankingParticipant(Snapshot, 3, EParticipantTerminalStatus::FINISHED, true, 0.50090080248142099, -3.3790851245995128, 31.668856767411302);
	EXPECT_EQ(MvpClientId(Snapshot, BuildFinalRanking(Snapshot, true)), 2);
}

TEST(ImpactRanking, AnyFinalizedStatusWithValidFrozenScoreCanReceivePlace)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::SELF_RESET, true, 5.0);
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::RUN_ENDED, true, 5.0);
	AddRankingParticipant(Snapshot, 3, EParticipantTerminalStatus::UNKNOWN, true, 5.0);
	AddRankingParticipant(Snapshot, 4, EParticipantTerminalStatus::OTHER, true, 5.0);
	AddRankingParticipant(Snapshot, 5, EParticipantTerminalStatus::NOT_FINALIZED, true, 5.0);
	const auto Ranking = BuildFinalRanking(Snapshot, true);
	EXPECT_EQ(RankedClientIds(Snapshot, Ranking), (std::vector<int>{1, 2, 3, 4, 5}));
	EXPECT_EQ(MvpClientId(Snapshot, Ranking), -1);
}

TEST(ImpactRanking, UnengagedAvailableFlagIsTreatedAsUnavailable)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::FINISHED, true, 99.0, 99.0, 99.0, 1, false);
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::FINISHED, true, 0.0, 0.0, 0.0);
	EXPECT_EQ(RankedClientIds(Snapshot, BuildFinalRanking(Snapshot, true)), (std::vector<int>{2}));
}

TEST(ImpactRanking, InvalidGenerationCannotEnterRanking)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::FINISHED, true, 9.0);
	SParticipant Invalid = MakeRankingParticipant(2, EParticipantTerminalStatus::FINISHED, true, 10.0);
	Invalid.m_Id.m_SlotGeneration = 0;
	Snapshot.m_vParticipants.push_back(Invalid);
	EXPECT_EQ(RankedClientIds(Snapshot, BuildFinalRanking(Snapshot, true)), (std::vector<int>{1}));
}

TEST(ImpactRanking, HigherFinalRatingImpliesHigherImpactExceptRatingFloorTieHandling)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::FINISHED, true, 1.6, 7.0);
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::FINISHED, true, 1.4, 5.0);
	AddRankingParticipant(Snapshot, 3, EParticipantTerminalStatus::FINISHED, true, 0.0, -20.0);
	const auto Ranking = BuildFinalRanking(Snapshot, true);
	EXPECT_GT(Snapshot.m_vParticipants[Ranking[0].m_ParticipantIndex].m_Score.m_Impact,
		Snapshot.m_vParticipants[Ranking[1].m_ParticipantIndex].m_Score.m_Impact);
	EXPECT_GT(Snapshot.m_vParticipants[Ranking[1].m_ParticipantIndex].m_Score.m_Impact,
		Snapshot.m_vParticipants[Ranking[2].m_ParticipantIndex].m_Score.m_Impact);
}

TEST(ImpactRanking, ExactOrderingUsesFullPrecisionOnly)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::FINISHED, true, 1.6001, 6.001, 87.01);
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::FINISHED, true, 1.6002, 6.009, 87.99);
	EXPECT_EQ(FormatTruncatedRating(1.6001), FormatTruncatedRating(1.6002));
	EXPECT_EQ(FormatTruncatedImpact(6.001), FormatTruncatedImpact(6.009));
	EXPECT_EQ(FormatTruncatedStability(87.01), FormatTruncatedStability(87.99));
	EXPECT_EQ(RankedClientIds(Snapshot, BuildFinalRanking(Snapshot, true)), (std::vector<int>{2, 1}));
}

TEST(ImpactFoundation, FinishedRunDistinguishesLeftMemberFromFinishers)
{
	CReplay Replay(44, TestContext());
	StartPair(Replay);
	StartThirdParticipant(Replay);
	SImpactEvent Left;
	Left.m_EventId = 4;
	Left.m_Type = EImpactEventType::PARTICIPANT_FINALIZED;
	Left.m_Actor = TestId(1);
	Left.m_TeamId = 1;
	Left.m_StartTick = 30;
	Left.m_ResolveTick = 30;
	Left.m_Confirmed = true;
	Left.m_CausalReason = "participant absent from authoritative server roster";
	STickEventBatch Batch;
	Batch.m_ServerTick = 30;
	Batch.m_vCandidates.push_back(Left);
	ASSERT_TRUE(Replay.Submit(Batch));
	SImpactEvent Finish;
	Finish.m_EventId = 5;
	Finish.m_Type = EImpactEventType::FINISH_CONFIRMED;
	Finish.m_TeamId = 1;
	Finish.m_StartTick = 40;
	Finish.m_ResolveTick = 40;
	Finish.m_Confirmed = true;
	Batch = {};
	Batch.m_ServerTick = 40;
	Batch.m_vCandidates.push_back(Finish);
	ASSERT_TRUE(Replay.Submit(Batch));
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(1))->m_TerminalStatus, EParticipantTerminalStatus::LEFT);
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(2))->m_TerminalStatus, EParticipantTerminalStatus::FINISHED);
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(3))->m_TerminalStatus, EParticipantTerminalStatus::FINISHED);
}

TEST(ImpactFoundation, LeftTerminalStatusPersistsOnlyWithEngagementAndAuthoritativeAbsence)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	CReplay Replay(441, TestContext());
	StartPair(Replay, 0, 10);
	SImpactEvent Left;
	Left.m_EventId = 3;
	Left.m_Type = EImpactEventType::PARTICIPANT_FINALIZED;
	Left.m_Actor = TestId(1);
	Left.m_TeamId = 1;
	Left.m_StartTick = 30;
	Left.m_ResolveTick = 30;
	Left.m_Confirmed = true;
	Left.m_CausalReason = "participant absent from authoritative server roster";
	STickEventBatch Batch;
	Batch.m_ServerTick = 30;
	Batch.m_vCandidates.push_back(Left);
	ASSERT_TRUE(Replay.Submit(Batch));
	const SParticipant *pLeft = FindRunParticipant(Replay.Run(), TestId(1));
	ASSERT_NE(pLeft, nullptr);
	EXPECT_EQ(pLeft->m_TerminalStatus, EParticipantTerminalStatus::LEFT);
	EXPECT_EQ(pLeft->m_ParticipationTerminalTick, 30);
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {Replay.Run().Snapshot(30)}));
	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), 1U);
	const SParticipant *pLoadedLeft = FindSnapshotParticipant(Loaded[0], TestId(1));
	ASSERT_NE(pLoadedLeft, nullptr);
	EXPECT_EQ(pLoadedLeft->m_TerminalStatus, EParticipantTerminalStatus::LEFT);
	EXPECT_EQ(pLoadedLeft->m_ParticipationTicks, 30);
	EXPECT_EQ(pLoadedLeft->m_ParticipationTerminalTick, 30);
}

TEST(ImpactFoundation, FinishedTerminalStatusPersists)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	CReplay Replay(442, TestContext());
	StartPair(Replay, 100, 110);
	SImpactEvent Finish;
	Finish.m_EventId = 3;
	Finish.m_Type = EImpactEventType::FINISH_CONFIRMED;
	Finish.m_TeamId = 1;
	Finish.m_StartTick = 150;
	Finish.m_ResolveTick = 150;
	Finish.m_Confirmed = true;
	STickEventBatch Batch;
	Batch.m_ServerTick = 150;
	Batch.m_vCandidates.push_back(Finish);
	ASSERT_TRUE(Replay.Submit(Batch));
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {Replay.Run().Snapshot(150)}));
	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), 1U);
	ASSERT_EQ(Loaded[0].m_vParticipants.size(), 2U);
	for(const SParticipant &Participant : Loaded[0].m_vParticipants)
	{
		EXPECT_TRUE(Participant.m_Finalized);
		EXPECT_EQ(Participant.m_TerminalStatus, EParticipantTerminalStatus::FINISHED);
		EXPECT_EQ(Participant.m_ParticipationTerminalTick, 150);
	}
}

TEST(ImpactFoundation, RunUidIsStableAcrossCheckpointRestore)
{
	CReplay Replay(42, TestContext());
	StartPair(Replay);
	const std::string RunUid = Replay.Run().RunUid();
	ASSERT_EQ(RunUid.size(), static_cast<size_t>(UUID_MAXSTRSIZE - 1));
	CUuid ParsedUid;
	EXPECT_EQ(ParseUuid(&ParsedUid, RunUid.c_str()), 0);
	const SRunSnapshot SnapshotData = Replay.Run().Snapshot(20);
	EXPECT_EQ(SnapshotData.m_RunUid, RunUid);
	CImpactRun Restored;
	ASSERT_TRUE(Restored.RestoreSnapshot(SnapshotData));
	EXPECT_EQ(Restored.RunUid(), RunUid);
	EXPECT_FALSE(Restored.HasGlobalRunNumber());
	EXPECT_FALSE(Restored.AssignGlobalRunNumber(0));
	EXPECT_TRUE(Restored.AssignGlobalRunNumber(12));
	EXPECT_TRUE(Restored.AssignGlobalRunNumber(12));
	EXPECT_FALSE(Restored.AssignGlobalRunNumber(13));
	const SRunSnapshot NumberedSnapshot = Restored.Snapshot(20);
	EXPECT_TRUE(NumberedSnapshot.m_HasGlobalRunNumber);
	EXPECT_EQ(NumberedSnapshot.m_GlobalRunNumber, 12U);
	SRunSnapshot InvalidNumberSnapshot = NumberedSnapshot;
	InvalidNumberSnapshot.m_GlobalRunNumber = 0;
	CImpactRun InvalidNumberRestore;
	EXPECT_FALSE(InvalidNumberRestore.RestoreSnapshot(InvalidNumberSnapshot));
}

TEST(ImpactFoundation, RescuesReceivedCountsAcceptedSaveTargetsExactlyOnce)
{
	CReplay Replay(43, TestContext());
	StartPair(Replay);
	SRawStats SaveDelta;
	SaveDelta.m_Saves = 1;
	const SImpactEvent Save = ParticipantStatsEvent(3, EImpactEventType::SAVE_CONFIRMED, TestId(1), 20, SaveDelta, TestId(2));
	STickEventBatch Batch;
	Batch.m_ServerTick = 20;
	Batch.m_vCandidates.push_back(Save);
	ASSERT_TRUE(Replay.Submit(Batch));
	const SParticipant *pSaver = FindRunParticipant(Replay.Run(), TestId(1));
	const SParticipant *pTarget = FindRunParticipant(Replay.Run(), TestId(2));
	ASSERT_NE(pSaver, nullptr);
	ASSERT_NE(pTarget, nullptr);
	EXPECT_EQ(pSaver->m_RawStats.m_Saves, 1);
	EXPECT_EQ(pTarget->m_RawStats.m_Saves, 0);
	EXPECT_TRUE(pTarget->m_RescuesReceivedKnown);
	EXPECT_EQ(pTarget->m_RescuesReceived, 1);
	ASSERT_EQ(Replay.Run().ScoringEvidence().size(), 1U);
	EXPECT_EQ(Replay.Run().ScoringEvidence()[0].m_LinkedSaveEventId, Save.m_EventId);
	EXPECT_FALSE(Replay.Submit(Batch));
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(2))->m_RescuesReceived, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().ScoringEvidence().size(), 1U);
}

TEST(ImpactFoundation, RescuesReceivedBecomesUnknownWithoutReliableTarget)
{
	CReplay Replay(431, TestContext());
	StartPair(Replay);
	SRawStats SaveDelta;
	SaveDelta.m_Saves = 1;
	ASSERT_TRUE(SubmitParticipantStats(Replay, 3, EImpactEventType::SAVE_CONFIRMED, TestId(1), 20, SaveDelta));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(1))->m_RawStats.m_Saves, 1);
	for(const SParticipant &Participant : Replay.Run().Participants())
	{
		EXPECT_FALSE(Participant.m_RescuesReceivedKnown);
		EXPECT_EQ(Participant.m_RescuesReceived, 0);
	}
}

TEST(ImpactFoundation, EngagedParticipantAccumulatesOwnParticipationTime)
{
	CReplay Replay(432, TestContext());
	StartPair(Replay, 100, 120);
	int64_t FirstTicks = -1;
	int64_t SecondTicks = -1;
	const SParticipant *pFirst = FindRunParticipant(Replay.Run(), TestId(1));
	const SParticipant *pSecond = FindRunParticipant(Replay.Run(), TestId(2));
	ASSERT_NE(pFirst, nullptr);
	ASSERT_NE(pSecond, nullptr);
	EXPECT_TRUE(ParticipantParticipationTicksAt(*pFirst, 180, &FirstTicks));
	EXPECT_TRUE(ParticipantParticipationTicksAt(*pSecond, 180, &SecondTicks));
	EXPECT_EQ(FirstTicks, 80);
	EXPECT_EQ(SecondTicks, 60);
	EXPECT_EQ(pFirst->m_ParticipationStartTick, 100);
	EXPECT_EQ(pSecond->m_ParticipationStartTick, 120);
}

TEST(ImpactFoundation, RosterOnlyNotEngagedDoesNotAccumulateParticipationTime)
{
	CReplay Replay(433, TestContext());
	StartPair(Replay, 100, 110);
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(3), "Roster only", 120, "authoritative exact-team roster"));
	const SParticipant *pRosterOnly = FindRunParticipant(Replay.Run(), TestId(3));
	ASSERT_NE(pRosterOnly, nullptr);
	EXPECT_FALSE(pRosterOnly->m_Engaged);
	int64_t Ticks = -1;
	EXPECT_TRUE(ParticipantParticipationTicksAt(*pRosterOnly, 1000, &Ticks));
	EXPECT_EQ(Ticks, 0);
	EXPECT_EQ(pRosterOnly->m_ParticipationStartTick, -1);
}

TEST(ImpactFoundation, ParticipationStopsAtTerminalState)
{
	CReplay Replay(434, TestContext());
	StartPair(Replay, 100, 110);
	ASSERT_TRUE(FinalizeReplayParticipant(Replay, 3, TestId(1), 140));
	const SParticipant *pParticipant = FindRunParticipant(Replay.Run(), TestId(1));
	ASSERT_NE(pParticipant, nullptr);
	EXPECT_TRUE(pParticipant->m_ParticipationTicksKnown);
	EXPECT_EQ(pParticipant->m_ParticipationTicks, 40);
	EXPECT_EQ(pParticipant->m_ParticipationTerminalTick, 140);
	EXPECT_EQ(pParticipant->m_ParticipationStartTick, -1);
	int64_t TicksLater = -1;
	EXPECT_TRUE(ParticipantParticipationTicksAt(*pParticipant, 900, &TicksLater));
	EXPECT_EQ(TicksLater, 40);
}

TEST(ImpactFoundation, ParticipationTimeSurvivesCheckpointRestoreWithoutDoubleCount)
{
	CReplay Replay(437, TestContext());
	StartPair(Replay, 100, 120);
	const SRunSnapshot Checkpoint = Replay.Run().Snapshot(180);
	CImpactRun Restored;
	ASSERT_TRUE(Restored.RestoreSnapshot(Checkpoint));
	int64_t Before = -1;
	int64_t After = -1;
	ASSERT_TRUE(ParticipantParticipationTicksAt(*FindSnapshotParticipant(Checkpoint, TestId(1)), 180, &Before));
	ASSERT_TRUE(ParticipantParticipationTicksAt(*FindRunParticipant(Restored, TestId(1)), 180, &After));
	EXPECT_EQ(Before, 80);
	EXPECT_EQ(After, Before);
	EXPECT_TRUE(ParticipantParticipationTicksAt(*FindRunParticipant(Restored, TestId(1)), 220, &After));
	EXPECT_EQ(After, 120);
}

TEST(ImpactFoundation, ScoringEvidencePreservesActorTargetGenerationAndCanonicalLinks)
{
	CReplay Replay(435, TestContext());
	const SParticipantId Saver = TestId(1, 4);
	const SParticipantId Saved = TestId(2, 9);
	StartPairWithIds(Replay, Saver, Saved, 0, 10);

	SImpactForensicEvent SaveLink;
	SaveLink.m_Type = EImpactForensicType::SAVE_CONFIRMED;
	SaveLink.m_EventId = 3;
	SaveLink.m_ProblemEpisodeId = 31;
	SaveLink.m_FallEpisodeId = 32;
	SaveLink.m_SaveCandidateId = 33;
	SaveLink.m_ClutchEpisodeId = 34;
	SaveLink.m_ActorInteractionId = 35;
	Replay.Run().AppendForensicEvents({SaveLink});
	SRawStats SaveDelta;
	SaveDelta.m_Saves = 1;
	SaveDelta.m_Clutches = 1;

	SRawStats AledDelta;
	AledDelta.m_Aleds = 1;
	SImpactEvent AledEvent = ParticipantStatsEvent(4, EImpactEventType::ALED_CONFIRMED, Saver, 20, AledDelta, Saved);
	AledEvent.m_StartTick = 15;
	SImpactEvent SaveEvent = ParticipantStatsEvent(3, EImpactEventType::SAVE_CONFIRMED, Saver, 20, SaveDelta, Saved);
	SaveEvent.m_StartTick = 15;
	STickEventBatch SaveAndAledBatch;
	SaveAndAledBatch.m_ServerTick = 20;
	SaveAndAledBatch.m_vCandidates = {AledEvent, SaveEvent};
	ASSERT_TRUE(Replay.Submit(SaveAndAledBatch));

	ASSERT_EQ(Replay.Run().ScoringEvidence().size(), 2U);
	const auto FindEvidence = [&Replay](uint64_t EventId) -> const SImpactScoringEvidence * {
		const auto It = std::find_if(Replay.Run().ScoringEvidence().begin(), Replay.Run().ScoringEvidence().end(), [EventId](const SImpactScoringEvidence &Evidence) {
			return Evidence.m_EventId == EventId;
		});
		return It == Replay.Run().ScoringEvidence().end() ? nullptr : &*It;
	};
	const SImpactScoringEvidence *pSaveEvidence = FindEvidence(3);
	ASSERT_NE(pSaveEvidence, nullptr);
	const SImpactScoringEvidence &SaveEvidence = *pSaveEvidence;
	EXPECT_EQ(SaveEvidence.m_RunId, Replay.Run().RunId());
	EXPECT_EQ(SaveEvidence.m_EventId, 3U);
	EXPECT_EQ(SaveEvidence.m_Type, EImpactEventType::SAVE_CONFIRMED);
	EXPECT_EQ(SaveEvidence.m_Actor, Saver);
	EXPECT_EQ(SaveEvidence.m_Actor.m_SlotGeneration, 4U);
	EXPECT_EQ(SaveEvidence.m_Target, Saved);
	EXPECT_EQ(SaveEvidence.m_Target.m_SlotGeneration, 9U);
	EXPECT_EQ(SaveEvidence.m_ProblemEpisodeId, 31U);
	EXPECT_EQ(SaveEvidence.m_FallEpisodeId, 32U);
	EXPECT_EQ(SaveEvidence.m_RescueEpisodeId, 33U);
	EXPECT_EQ(SaveEvidence.m_ClutchEpisodeId, 34U);
	EXPECT_EQ(SaveEvidence.m_ActorInteractionId, 35U);
	EXPECT_EQ(SaveEvidence.m_LinkedSaveEventId, 3U);
	EXPECT_TRUE(SaveEvidence.m_ClutchConfirmed);
	EXPECT_FALSE(SaveEvidence.m_AledConfirmed);

	const SImpactScoringEvidence *pAledEvidence = FindEvidence(4);
	ASSERT_NE(pAledEvidence, nullptr);
	const SImpactScoringEvidence &AledEvidence = *pAledEvidence;
	EXPECT_EQ(AledEvidence.m_Type, EImpactEventType::ALED_CONFIRMED);
	EXPECT_EQ(AledEvidence.m_LinkedSaveEventId, 3U);
	EXPECT_FALSE(AledEvidence.m_IsCanonicalSave);
	EXPECT_TRUE(AledEvidence.m_AledConfirmed);
	EXPECT_FALSE(AledEvidence.m_ClutchConfirmed);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Aleds, 1);
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {Replay.Run().Snapshot(40)}));
	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), 1U);
	ASSERT_EQ(Loaded[0].m_vScoringEvidence.size(), 2U);
	EXPECT_EQ(Loaded[0].m_ScoringEvidenceVersion, IMPACT_SCORING_EVIDENCE_VERSION);
	EXPECT_TRUE(Loaded[0].m_ScoringEvidenceComplete);
	EXPECT_EQ(Loaded[0].m_vScoringEvidence[0].m_Actor.m_SlotGeneration, 4U);
	EXPECT_EQ(Loaded[0].m_vScoringEvidence[0].m_Target.m_SlotGeneration, 9U);
	const auto FindLoadedEvidence = [&Loaded](uint64_t EventId) -> const SImpactScoringEvidence * {
		const auto It = std::find_if(Loaded[0].m_vScoringEvidence.begin(), Loaded[0].m_vScoringEvidence.end(), [EventId](const SImpactScoringEvidence &Evidence) {
			return Evidence.m_EventId == EventId;
		});
		return It == Loaded[0].m_vScoringEvidence.end() ? nullptr : &*It;
	};
	ASSERT_NE(FindLoadedEvidence(3), nullptr);
	EXPECT_EQ(FindLoadedEvidence(3)->m_LinkedSaveEventId, 3U);
	EXPECT_TRUE(FindLoadedEvidence(3)->m_ClutchConfirmed);
	ASSERT_NE(FindLoadedEvidence(4), nullptr);
	EXPECT_EQ(FindLoadedEvidence(4)->m_LinkedSaveEventId, 3U);
	EXPECT_TRUE(FindLoadedEvidence(4)->m_AledConfirmed);
}

TEST(ImpactFoundation, UnknownEpisodeDoesNotBecomeFarm)
{
	SImpactScoringEvidence Previous;
	Previous.m_RunId = 7;
	Previous.m_EventId = 1;
	Previous.m_Tick = 100;
	Previous.m_Actor = TestId(1, 4);
	Previous.m_Target = TestId(2, 9);
	Previous.m_IsCanonicalSave = true;
	Previous.m_LinkedSaveEventId = 1;
	SImpactScoringEvidence Current = Previous;
	Current.m_EventId = 2;
	Current.m_Tick = 101;
	Current.m_LinkedSaveEventId = 2;
	EXPECT_EQ(ClassifyRepeatedSaveContext(Previous, Current, 20), ERepeatedContextClass::UNKNOWN);
}

static SImpactScoringEvidence AntiFarmEvidence(uint64_t EventId, int64_t Tick, uint64_t ProblemEpisodeId = 0,
	uint64_t FallEpisodeId = 0, uint64_t RescueEpisodeId = 0, uint64_t InteractionId = 0,
	SParticipantId Actor = TestId(1, 4), SParticipantId Target = TestId(2, 9), uint64_t RunId = 7)
{
	SImpactScoringEvidence Evidence;
	Evidence.m_RunId = RunId;
	Evidence.m_EventId = EventId;
	Evidence.m_Tick = Tick;
	Evidence.m_Actor = Actor;
	Evidence.m_Target = Target;
	Evidence.m_ProblemEpisodeId = ProblemEpisodeId;
	Evidence.m_FallEpisodeId = FallEpisodeId;
	Evidence.m_RescueEpisodeId = RescueEpisodeId;
	Evidence.m_ActorInteractionId = InteractionId;
	Evidence.m_IsCanonicalSave = true;
	Evidence.m_LinkedSaveEventId = EventId;
	return Evidence;
}

TEST(ImpactFoundation, TimeProximityAloneIsNotRepeated)
{
	const SImpactScoringEvidence Previous = AntiFarmEvidence(1, 100);
	const SImpactScoringEvidence Close = AntiFarmEvidence(2, 101);
	const SImpactScoringEvidence Distant = AntiFarmEvidence(3, 100000);
	EXPECT_EQ(ClassifyRepeatedSaveContext(Previous, Close, 1), ERepeatedContextClass::UNKNOWN);
	EXPECT_EQ(ClassifyRepeatedSaveContext(Previous, Close, 100000), ERepeatedContextClass::UNKNOWN);
	EXPECT_EQ(ClassifyRepeatedSaveContext(Previous, Distant, 1), ERepeatedContextClass::UNKNOWN);
}

TEST(ImpactFoundation, SameSaverTargetAloneIsNotRepeated)
{
	const SImpactScoringEvidence Previous = AntiFarmEvidence(1, 100);
	const SImpactScoringEvidence Current = AntiFarmEvidence(2, 101);
	EXPECT_EQ(ClassifyRepeatedSaveContext(Previous, Current, 1000), ERepeatedContextClass::UNKNOWN);
}

TEST(ImpactFoundation, NewFallAloneDoesNotProveIndependentContext)
{
	const SImpactScoringEvidence Previous = AntiFarmEvidence(1, 100, 0, 41);
	const SImpactScoringEvidence Current = AntiFarmEvidence(2, 101, 0, 42);
	EXPECT_EQ(ClassifyRepeatedSaveContext(Previous, Current, 1000), ERepeatedContextClass::UNKNOWN);
}

TEST(ImpactFoundation, ProvenSeparateRescueContextIsIndependent)
{
	const SImpactScoringEvidence Previous = AntiFarmEvidence(1, 100, 41, 41, 51, 61);
	const SImpactScoringEvidence Current = AntiFarmEvidence(2, 101, 42, 42, 52, 62);
	EXPECT_EQ(ClassifyRepeatedSaveContext(Previous, Current, 1000), ERepeatedContextClass::INDEPENDENT);
}

TEST(ImpactFoundation, StructuralRepeatDoesNotDependOnElapsedTime)
{
	const SImpactScoringEvidence Previous = AntiFarmEvidence(1, 100, 41, 41, 51, 61);
	const SImpactScoringEvidence Current = AntiFarmEvidence(2, 100000, 41, 41, 52, 62);
	EXPECT_EQ(ClassifyRepeatedSaveContext(Previous, Current, 1), ERepeatedContextClass::POSSIBLE_REPEATED_CONTEXT);
}

TEST(ImpactFoundation, DifferentTargetDoesNotLeakLoopState)
{
	const SImpactScoringEvidence Previous = AntiFarmEvidence(1, 100, 41, 41, 51, 61);
	const SImpactScoringEvidence Current = AntiFarmEvidence(2, 101, 41, 41, 52, 62, TestId(1, 4), TestId(3, 9));
	EXPECT_EQ(ClassifyRepeatedSaveContext(Previous, Current, 1), ERepeatedContextClass::UNKNOWN);
}

TEST(ImpactFoundation, DifferentSaverDoesNotLeakLoopState)
{
	const SImpactScoringEvidence Previous = AntiFarmEvidence(1, 100, 41, 41, 51, 61);
	const SImpactScoringEvidence Current = AntiFarmEvidence(2, 101, 41, 41, 52, 62, TestId(3, 4), TestId(2, 9));
	EXPECT_EQ(ClassifyRepeatedSaveContext(Previous, Current, 1000), ERepeatedContextClass::UNKNOWN);
}

TEST(ImpactFoundation, GenerationBoundaryDoesNotLeakLoopState)
{
	const SImpactScoringEvidence Previous = AntiFarmEvidence(1, 100, 41, 41, 51, 61);
	const SImpactScoringEvidence Current = AntiFarmEvidence(2, 101, 41, 41, 52, 62, TestId(1, 5), TestId(2, 9));
	EXPECT_EQ(ClassifyRepeatedSaveContext(Previous, Current, 1000), ERepeatedContextClass::UNKNOWN);
}

TEST(ImpactFoundation, RunBoundaryDoesNotLeakLoopState)
{
	const SImpactScoringEvidence Previous = AntiFarmEvidence(1, 100, 41, 41, 51, 61);
	const SImpactScoringEvidence Current = AntiFarmEvidence(2, 101, 41, 41, 52, 62, TestId(1, 4), TestId(2, 9), 8);
	EXPECT_EQ(ClassifyRepeatedSaveContext(Previous, Current, 1000), ERepeatedContextClass::UNKNOWN);
}

TEST(ImpactFoundation, TeamResetProblemIdentityDoesNotReuseOldLoopContext)
{
	// Phase2 preserves its episode sequence across an in-run reset boundary, so
	// the next distinct authoritative problem identity is a separate context.
	const SImpactScoringEvidence BeforeReset = AntiFarmEvidence(1, 100, 41, 41, 51, 61);
	const SImpactScoringEvidence AfterReset = AntiFarmEvidence(2, 101, 42, 42, 52, 62);
	EXPECT_EQ(ClassifyRepeatedSaveContext(BeforeReset, AfterReset, 1000), ERepeatedContextClass::INDEPENDENT);
}

TEST(ImpactFoundation, SpecCharSeparatedProblemChainsDoNotLink)
{
	// A confirmed SpecChar boundary is not itself part of scoring evidence. The
	// post-boundary physical chain is represented by a distinct problem identity.
	const SImpactScoringEvidence BeforeBoundary = AntiFarmEvidence(1, 100, 41, 41, 51, 61);
	const SImpactScoringEvidence AfterBoundary = AntiFarmEvidence(2, 101, 42, 42, 52, 62);
	EXPECT_EQ(ClassifyRepeatedSaveContext(BeforeBoundary, AfterBoundary, 1000), ERepeatedContextClass::INDEPENDENT);
}

TEST(ImpactFoundation, LongTimeGapAloneDoesNotProveIndependentContext)
{
	const SImpactScoringEvidence Previous = AntiFarmEvidence(1, 100);
	const SImpactScoringEvidence Current = AntiFarmEvidence(2, 100000);
	EXPECT_EQ(ClassifyRepeatedSaveContext(Previous, Current, 1), ERepeatedContextClass::UNKNOWN);
}

TEST(ImpactFoundation, RepeatedContextEvidenceDoesNotChangeCanonicalCounters)
{
	CReplay Replay(436, TestContext());
	StartPair(Replay);
	const SParticipantId Saver = TestId(1);
	const SParticipantId Target = TestId(2);
	for(uint64_t EventId : {3ULL, 4ULL})
	{
		SImpactForensicEvent Link;
		Link.m_Type = EImpactForensicType::SAVE_CONFIRMED;
		Link.m_EventId = EventId;
		Link.m_ProblemEpisodeId = 51;
		Link.m_SaveCandidateId = EventId + 10;
		Replay.Run().AppendForensicEvents({Link});
		SRawStats Delta;
		Delta.m_Saves = 1;
		ASSERT_TRUE(SubmitParticipantStats(Replay, EventId, EImpactEventType::SAVE_CONFIRMED, Saver,
			static_cast<int64_t>(EventId * 10), Delta, Target));
	}
	const SRawStats BeforeClassification = Replay.Run().RawStats();
	ASSERT_EQ(Replay.Run().ScoringEvidence().size(), 2U);
	EXPECT_EQ(ClassifyRepeatedSaveContext(Replay.Run().ScoringEvidence()[0], Replay.Run().ScoringEvidence()[1], 5), ERepeatedContextClass::POSSIBLE_REPEATED_CONTEXT);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, BeforeClassification.m_Saves);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, BeforeClassification.m_Falls);
	EXPECT_EQ(Replay.Run().RawStats().m_Aleds, BeforeClassification.m_Aleds);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, BeforeClassification.m_Clutches);
	EXPECT_EQ(FindRunParticipant(Replay.Run(), Target)->m_RescuesReceived, 2);
}

static void ExpectRawStatsEqual(const SRawStats &Left, const SRawStats &Right);

TEST(ImpactFoundation, NoPhase2CountersChangeFromFoundationTelemetry)
{
	CReplay Traced(438, TestContext());
	CReplay NoDebugTrace(439, TestContext());
	StartPair(Traced);
	StartPair(NoDebugTrace);
	NoDebugTrace.Run().SetForensicTraceEnabled(false);
	SRawStats Delta;
	Delta.m_Saves = 1;
	Delta.m_Clutches = 1;
	ASSERT_TRUE(SubmitParticipantStats(Traced, 3, EImpactEventType::SAVE_CONFIRMED, TestId(1), 20, Delta, TestId(2)));
	ASSERT_TRUE(SubmitParticipantStats(NoDebugTrace, 3, EImpactEventType::SAVE_CONFIRMED, TestId(1), 20, Delta, TestId(2)));
	ExpectRawStatsEqual(Traced.Run().RawStats(), NoDebugTrace.Run().RawStats());
	ASSERT_EQ(Traced.Run().Participants().size(), NoDebugTrace.Run().Participants().size());
	for(size_t i = 0; i < Traced.Run().Participants().size(); ++i)
	{
		ExpectRawStatsEqual(Traced.Run().Participants()[i].m_RawStats, NoDebugTrace.Run().Participants()[i].m_RawStats);
		EXPECT_EQ(Traced.Run().Participants()[i].m_RescuesReceived, NoDebugTrace.Run().Participants()[i].m_RescuesReceived);
	}
	EXPECT_EQ(Traced.Run().ScoringEvidence().size(), NoDebugTrace.Run().ScoringEvidence().size());
	EXPECT_EQ(Traced.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Traced.Run().RawStats().m_Clutches, 1);
}

TEST(ImpactFoundation, CheckpointRestoreDoesNotDoubleCountActiveTime)
{
	CReplay Replay(1, TestContext());
	StartPair(Replay);
	const int64_t BeforeCheckpoint = Replay.Run().ActiveAnalysisTicks(100);
	const SRunSnapshot SnapshotData = Replay.Run().Snapshot(100);
	CImpactRun Restored;
	ASSERT_TRUE(Restored.RestoreSnapshot(SnapshotData));
	EXPECT_EQ(Restored.ActiveAnalysisTicks(200), Replay.Run().ActiveAnalysisTicks(200));
	EXPECT_EQ(SnapshotData.m_ActiveAnalysisTicks, BeforeCheckpoint);
}

static SRunSnapshot PersistentSnapshot(uint64_t RunId)
{
	CReplay Replay(RunId, TestContext());
	StartPair(Replay);
	SRunSnapshot SnapshotData = Replay.Run().Snapshot(200, 1700000000);
	SnapshotData.m_Score.m_Available = true;
	SnapshotData.m_Score.m_Rating = 1.84;
	SnapshotData.m_Score.m_Impact = 18.0;
	SnapshotData.m_Score.m_Consistency = 87.0;
	return SnapshotData;
}

static SImpactForensicEvent TestForensicEvent(EImpactForensicType Type, uint64_t RunId, uint64_t Sequence, int64_t Tick)
{
	SImpactForensicEvent Event;
	Event.m_Type = Type;
	Event.m_RunId = RunId;
	Event.m_Sequence = Sequence;
	Event.m_Tick = Tick;
	Event.m_TeamId = 1;
	return Event;
}

static bool RemoveJsonObjectProperty(std::string *pJson, const std::string &Name)
{
	if(pJson == nullptr)
		return false;
	const std::string Key = "\"" + Name + "\"";
	const size_t KeyStart = pJson->find(Key);
	if(KeyStart == std::string::npos)
		return false;
	size_t ValueStart = pJson->find(':', KeyStart + Key.size());
	if(ValueStart == std::string::npos)
		return false;
	++ValueStart;
	while(ValueStart < pJson->size() && std::isspace(static_cast<unsigned char>((*pJson)[ValueStart])))
		++ValueStart;
	size_t ValueEnd = ValueStart;
	if(ValueStart < pJson->size() && ((*pJson)[ValueStart] == '{' || (*pJson)[ValueStart] == '['))
	{
		std::vector<char> Closers;
		bool InString = false;
		bool Escaped = false;
		for(; ValueEnd < pJson->size(); ++ValueEnd)
		{
			const char Character = (*pJson)[ValueEnd];
			if(InString)
			{
				if(Escaped)
					Escaped = false;
				else if(Character == '\\')
					Escaped = true;
				else if(Character == '"')
					InString = false;
				continue;
			}
			if(Character == '"')
				InString = true;
			else if(Character == '{')
				Closers.push_back('}');
			else if(Character == '[')
				Closers.push_back(']');
			else if(!Closers.empty() && Character == Closers.back())
			{
				Closers.pop_back();
				if(Closers.empty())
				{
					++ValueEnd;
					break;
				}
			}
		}
		if(!Closers.empty())
			return false;
	}
	else if(ValueStart < pJson->size() && (*pJson)[ValueStart] == '"')
	{
		bool Escaped = false;
		for(++ValueEnd; ValueEnd < pJson->size(); ++ValueEnd)
		{
			const char Character = (*pJson)[ValueEnd];
			if(Escaped)
				Escaped = false;
			else if(Character == '\\')
				Escaped = true;
			else if(Character == '"')
			{
				++ValueEnd;
				break;
			}
		}
	}
	else
	{
		while(ValueEnd < pJson->size() && (*pJson)[ValueEnd] != ',' && (*pJson)[ValueEnd] != '}')
			++ValueEnd;
	}
	size_t EraseStart = KeyStart;
	size_t EraseEnd = ValueEnd;
	while(EraseStart > 0 && std::isspace(static_cast<unsigned char>((*pJson)[EraseStart - 1])))
		--EraseStart;
	if(EraseStart > 0 && (*pJson)[EraseStart - 1] == ',')
		--EraseStart;
	else
	{
		while(EraseEnd < pJson->size() && std::isspace(static_cast<unsigned char>((*pJson)[EraseEnd])))
			++EraseEnd;
		if(EraseEnd < pJson->size() && (*pJson)[EraseEnd] == ',')
			++EraseEnd;
	}
	pJson->erase(EraseStart, EraseEnd - EraseStart);
	return true;
}

static bool WriteTestStorageText(IStorage *pStorage, const char *pPath, const std::string &Text)
{
	if(pStorage == nullptr || pPath == nullptr)
		return false;
	pStorage->RemoveFile(pPath, IStorage::TYPE_SAVE);
	IOHANDLE File = pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return false;
	const bool Written = io_write(File, Text.data(), static_cast<unsigned>(Text.size())) == Text.size();
	const bool Synced = Written && io_sync(File) == 0;
	const bool Closed = io_close(File) == 0;
	return Written && Synced && Closed;
}

static void ExpectRawStatsEqual(const SRawStats &Left, const SRawStats &Right)
{
	EXPECT_EQ(Left.m_ConfirmedEvents, Right.m_ConfirmedEvents);
	EXPECT_EQ(Left.m_Falls, Right.m_Falls);
	EXPECT_EQ(Left.m_Wipes, Right.m_Wipes);
	EXPECT_EQ(Left.m_Saves, Right.m_Saves);
	EXPECT_EQ(Left.m_UsefulSaves, Right.m_UsefulSaves);
	EXPECT_EQ(Left.m_MeaningfulSkips, Right.m_MeaningfulSkips);
	EXPECT_EQ(Left.m_BackRecoveries, Right.m_BackRecoveries);
	EXPECT_EQ(Left.m_HarmfulEvents, Right.m_HarmfulEvents);
	EXPECT_EQ(Left.m_PositiveEvents, Right.m_PositiveEvents);
	EXPECT_EQ(Left.m_Aleds, Right.m_Aleds);
	EXPECT_EQ(Left.m_Clutches, Right.m_Clutches);
	EXPECT_EQ(Left.m_UpDowns, Right.m_UpDowns);
	EXPECT_EQ(Left.m_MissedCatches, Right.m_MissedCatches);
	EXPECT_EQ(Left.m_TeamMistakes, Right.m_TeamMistakes);
	EXPECT_EQ(Left.m_PreventedProgressLoss, Right.m_PreventedProgressLoss);
	EXPECT_EQ(Left.m_RecoveryDifficultyIncreases, Right.m_RecoveryDifficultyIncreases);
}

static void ExpectCanonicalEventsEqual(const std::vector<SImpactEvent> &Left, const std::vector<SImpactEvent> &Right)
{
	ASSERT_EQ(Left.size(), Right.size());
	for(size_t i = 0; i < Left.size(); ++i)
	{
		EXPECT_EQ(Left[i].m_EventId, Right[i].m_EventId);
		EXPECT_EQ(Left[i].m_Type, Right[i].m_Type);
		EXPECT_EQ(Left[i].m_Actor, Right[i].m_Actor);
		EXPECT_EQ(Left[i].m_Target, Right[i].m_Target);
		EXPECT_EQ(Left[i].m_TeamId, Right[i].m_TeamId);
		EXPECT_EQ(Left[i].m_StartTick, Right[i].m_StartTick);
		EXPECT_EQ(Left[i].m_ResolveTick, Right[i].m_ResolveTick);
		ExpectRawStatsEqual(Left[i].m_RawDelta, Right[i].m_RawDelta);
	}
}

static SRunSnapshot Phase4FeatureSnapshot(uint64_t RunId = 9800)
{
	SRunSnapshot Snapshot;
	Snapshot.m_RunId = RunId;
	Snapshot.m_State = EAnalysisState::FINALIZED;
	Snapshot.m_Result = ERunResult::WIPED;
	Snapshot.m_EndTick = 100;
	Snapshot.m_Context = TestContext();
	Snapshot.m_ScoringEvidenceVersion = IMPACT_SCORING_EVIDENCE_VERSION;
	Snapshot.m_ScoringEvidenceComplete = true;
	for(int ClientId : {1, 2})
	{
		SParticipant Participant;
		Participant.m_Id = TestId(ClientId);
		Participant.m_Name = ClientId == 1 ? "Saver" : "Target";
		Participant.m_Engaged = true;
		Participant.m_EngagedTick = 0;
		Participant.m_Finalized = true;
		Participant.m_TerminalStatus = EParticipantTerminalStatus::FINISHED;
		Participant.m_ParticipationTicks = 100 + ClientId;
		Participant.m_ParticipationTicksKnown = true;
		Participant.m_ParticipationTerminalTick = 100;
		Participant.m_RescuesReceivedKnown = true;
		Snapshot.m_vParticipants.push_back(Participant);
	}
	return Snapshot;
}

static SParticipant *FindPhase4FeatureParticipant(SRunSnapshot *pSnapshot, const SParticipantId &Id)
{
	if(pSnapshot == nullptr)
		return nullptr;
	auto It = std::find_if(pSnapshot->m_vParticipants.begin(), pSnapshot->m_vParticipants.end(), [&Id](const SParticipant &Participant) {
		return Participant.m_Id == Id;
	});
	return It == pSnapshot->m_vParticipants.end() ? nullptr : &*It;
}

static uint64_t AddPhase4CanonicalSave(SRunSnapshot *pSnapshot, uint64_t EventId, const SParticipantId &Actor,
	const SParticipantId &Target, int64_t Tick, uint64_t ProblemEpisodeId, uint64_t RescueEpisodeId, bool Clutch = false)
{
	if(pSnapshot == nullptr)
		return 0;
	SImpactEvent Event;
	Event.m_EventId = EventId;
	Event.m_Type = EImpactEventType::SAVE_CONFIRMED;
	Event.m_Actor = Actor;
	Event.m_Target = Target;
	Event.m_TeamId = pSnapshot->m_Context.m_TeamId;
	Event.m_StartTick = Tick;
	Event.m_ResolveTick = Tick;
	Event.m_Confirmed = true;
	Event.m_AppliedExactlyOnce = true;
	Event.m_RawDelta.m_Saves = 1;
	Event.m_RawDelta.m_Clutches = Clutch ? 1 : 0;
	pSnapshot->m_vEvents.push_back(Event);

	SImpactScoringEvidence Evidence;
	Evidence.m_RunId = pSnapshot->m_RunId;
	Evidence.m_EventId = EventId;
	Evidence.m_Type = EImpactEventType::SAVE_CONFIRMED;
	Evidence.m_Tick = Tick;
	Evidence.m_Actor = Actor;
	Evidence.m_Target = Target;
	Evidence.m_ProblemEpisodeId = ProblemEpisodeId;
	Evidence.m_RescueEpisodeId = RescueEpisodeId;
	Evidence.m_ActorInteractionId = EventId + 1000;
	Evidence.m_LinkedSaveEventId = EventId;
	Evidence.m_IsCanonicalSave = true;
	Evidence.m_ClutchConfirmed = Clutch;
	pSnapshot->m_vScoringEvidence.push_back(Evidence);

	if(SParticipant *pActor = FindPhase4FeatureParticipant(pSnapshot, Actor))
	{
		++pActor->m_RawStats.m_Saves;
		pActor->m_RawStats.m_Clutches += Clutch ? 1 : 0;
	}
	if(Target.IsValid() && Target != Actor)
		if(SParticipant *pTarget = FindPhase4FeatureParticipant(pSnapshot, Target))
			++pTarget->m_RescuesReceived;
	return EventId;
}

static void AddPhase4AledSidecar(SRunSnapshot *pSnapshot, uint64_t EventId, uint64_t SaveEventId,
	const SParticipantId &Actor, const SParticipantId &Target, int64_t Tick)
{
	SImpactEvent Event;
	Event.m_EventId = EventId;
	Event.m_Type = EImpactEventType::ALED_CONFIRMED;
	Event.m_Actor = Actor;
	Event.m_Target = Target;
	Event.m_TeamId = pSnapshot->m_Context.m_TeamId;
	Event.m_StartTick = Tick;
	Event.m_ResolveTick = Tick;
	Event.m_Confirmed = true;
	Event.m_AppliedExactlyOnce = true;
	Event.m_RawDelta.m_Aleds = 1;
	pSnapshot->m_vEvents.push_back(Event);

	SImpactScoringEvidence Evidence;
	Evidence.m_RunId = pSnapshot->m_RunId;
	Evidence.m_EventId = EventId;
	Evidence.m_Type = EImpactEventType::ALED_CONFIRMED;
	Evidence.m_Tick = Tick;
	Evidence.m_Actor = Actor;
	Evidence.m_Target = Target;
	Evidence.m_LinkedSaveEventId = SaveEventId;
	Evidence.m_AledConfirmed = true;
	pSnapshot->m_vScoringEvidence.push_back(Evidence);
	if(SParticipant *pActor = FindPhase4FeatureParticipant(pSnapshot, Actor))
		++pActor->m_RawStats.m_Aleds;
}

static const SPlayerScoringFeatures *FindScoringFeatures(const CRunScoringContext &Features, const SParticipantId &Id)
{
	const auto It = std::find_if(Features.m_vPlayers.begin(), Features.m_vPlayers.end(), [&Id](const SPlayerScoringFeatures &Player) {
		return Player.m_Id == Id;
	});
	return It == Features.m_vPlayers.end() ? nullptr : &*It;
}

TEST(ImpactPhase4Aggregation, CanonicalCountersAggregateExactly)
{
	SRunSnapshot Snapshot = Phase4FeatureSnapshot();
	Snapshot.m_ScoringEvidenceVersion = 0;
	Snapshot.m_ScoringEvidenceComplete = false;
	Snapshot.m_vParticipants[0].m_RawStats.m_Saves = 3;
	Snapshot.m_vParticipants[0].m_RawStats.m_Falls = 2;
	Snapshot.m_vParticipants[0].m_RawStats.m_Aleds = 1;
	Snapshot.m_vParticipants[0].m_RawStats.m_Clutches = 2;
	Snapshot.m_vParticipants[1].m_RawStats.m_Saves = 2;
	Snapshot.m_vParticipants[1].m_RawStats.m_Falls = 4;
	SPhase4ScoringAggregationOptions Options;
	CRunScoringContext Features;
	ASSERT_TRUE(BuildPhase4ScoringFeatures(Snapshot, Options, &Features));
	EXPECT_EQ(Features.m_TotalSaves, 5);
	EXPECT_EQ(Features.m_TotalFalls, 6);
	EXPECT_EQ(Features.m_TotalAleds, 1);
	EXPECT_EQ(Features.m_TotalClutches, 2);
	EXPECT_EQ(Features.m_vPlayers[0].m_Saves, 3);
	EXPECT_EQ(Features.m_vPlayers[0].m_Aleds, 1);
	EXPECT_EQ(Features.m_vPlayers[0].m_Clutches, 2);
}

TEST(ImpactPhase4Aggregation, AledDoesNotDoubleCountSave)
{
	SRunSnapshot Snapshot = Phase4FeatureSnapshot();
	AddPhase4CanonicalSave(&Snapshot, 1, TestId(1), TestId(2), 20, 4, 5);
	AddPhase4AledSidecar(&Snapshot, 2, 1, TestId(1), TestId(2), 20);
	SPhase4ScoringAggregationOptions Options;
	CRunScoringContext Features;
	ASSERT_TRUE(BuildPhase4ScoringFeatures(Snapshot, Options, &Features));
	EXPECT_EQ(FindScoringFeatures(Features, TestId(1))->m_Saves, 1);
	EXPECT_EQ(FindScoringFeatures(Features, TestId(1))->m_Aleds, 1);
	EXPECT_EQ(Features.m_TotalSaves, 1);
	EXPECT_EQ(Features.m_TotalAleds, 1);
	EXPECT_EQ(Features.m_ScoringEvidenceStatus, EPhase4EvidenceStatus::COMPLETE);
}

TEST(ImpactPhase4Aggregation, ClutchDoesNotDoubleCountSave)
{
	SRunSnapshot Snapshot = Phase4FeatureSnapshot();
	AddPhase4CanonicalSave(&Snapshot, 1, TestId(1), TestId(2), 20, 4, 5, true);
	SPhase4ScoringAggregationOptions Options;
	CRunScoringContext Features;
	ASSERT_TRUE(BuildPhase4ScoringFeatures(Snapshot, Options, &Features));
	EXPECT_EQ(FindScoringFeatures(Features, TestId(1))->m_Saves, 1);
	EXPECT_EQ(FindScoringFeatures(Features, TestId(1))->m_Clutches, 1);
	EXPECT_EQ(Features.m_TotalSaves, 1);
	EXPECT_EQ(Features.m_TotalClutches, 1);
}

TEST(ImpactPhase4Aggregation, RescuesReceivedTargetsCorrectPlayer)
{
	SRunSnapshot Snapshot = Phase4FeatureSnapshot();
	AddPhase4CanonicalSave(&Snapshot, 1, TestId(1), TestId(2), 20, 4, 5);
	SPhase4ScoringAggregationOptions Options;
	CRunScoringContext Features;
	ASSERT_TRUE(BuildPhase4ScoringFeatures(Snapshot, Options, &Features));
	EXPECT_EQ(FindScoringFeatures(Features, TestId(1))->m_RescuesReceived, 0);
	EXPECT_TRUE(FindScoringFeatures(Features, TestId(1))->m_RescuesReceivedKnown);
	EXPECT_EQ(FindScoringFeatures(Features, TestId(2))->m_RescuesReceived, 1);
	EXPECT_TRUE(Features.m_TotalRescuesReceivedKnown);
	EXPECT_EQ(Features.m_TotalKnownRescuesReceived, 1);
}

TEST(ImpactPhase4Aggregation, UnknownRescuesReceivedStaysUnknown)
{
	SRunSnapshot Snapshot = Phase4FeatureSnapshot();
	Snapshot.m_vParticipants[1].m_RescuesReceivedKnown = false;
	SPhase4ScoringAggregationOptions Options;
	CRunScoringContext Features;
	ASSERT_TRUE(BuildPhase4ScoringFeatures(Snapshot, Options, &Features));
	const SPlayerScoringFeatures *pTarget = FindScoringFeatures(Features, TestId(2));
	ASSERT_NE(pTarget, nullptr);
	EXPECT_FALSE(pTarget->m_RescuesReceivedKnown);
	EXPECT_FALSE(Features.m_TotalRescuesReceivedKnown);
	EXPECT_EQ(Features.m_RescuesReceivedUnknownParticipantCount, 1U);
}

TEST(ImpactPhase4Aggregation, ParticipationStartsOnlyAtEngagedProof)
{
	SRunSnapshot Snapshot = Phase4FeatureSnapshot();
	Snapshot.m_vParticipants[1].m_Engaged = false;
	Snapshot.m_vParticipants[1].m_RawStats.m_Saves = 30;
	SPhase4ScoringAggregationOptions Options;
	CRunScoringContext Features;
	ASSERT_TRUE(BuildPhase4ScoringFeatures(Snapshot, Options, &Features));
	EXPECT_EQ(Features.m_vPlayers.size(), 1U);
	EXPECT_EQ(Features.m_NotEngagedRosterParticipantCount, 1U);
	EXPECT_EQ(Features.m_FinalizedEngagedParticipantCount, 1U);
	EXPECT_EQ(Features.m_TotalKnownParticipationTicks, 101);
}

TEST(ImpactPhase4Aggregation, UnknownParticipationDoesNotBecomeZero)
{
	SRunSnapshot Snapshot = Phase4FeatureSnapshot();
	Snapshot.m_vParticipants[0].m_ParticipationTicksKnown = false;
	Snapshot.m_vParticipants[0].m_ParticipationTicks = 0;
	SPhase4ScoringAggregationOptions Options;
	CRunScoringContext Features;
	ASSERT_TRUE(BuildPhase4ScoringFeatures(Snapshot, Options, &Features));
	const SPlayerScoringFeatures *pPlayer = FindScoringFeatures(Features, TestId(1));
	ASSERT_NE(pPlayer, nullptr);
	EXPECT_FALSE(pPlayer->m_ParticipationKnown);
	EXPECT_FALSE(Features.m_ParticipationSampleComplete);
	EXPECT_EQ(Features.m_ParticipationUnknownParticipantCount, 1U);
}

TEST(ImpactPhase4Aggregation, LeftParticipantKeepsFinalFeatures)
{
	SRunSnapshot Snapshot = Phase4FeatureSnapshot();
	Snapshot.m_vParticipants[0].m_TerminalStatus = EParticipantTerminalStatus::LEFT;
	Snapshot.m_vParticipants[0].m_RawStats.m_Saves = 2;
	SPhase4ScoringAggregationOptions Options;
	CRunScoringContext Features;
	ASSERT_TRUE(BuildPhase4ScoringFeatures(Snapshot, Options, &Features));
	const SPlayerScoringFeatures *pLeft = FindScoringFeatures(Features, TestId(1));
	ASSERT_NE(pLeft, nullptr);
	EXPECT_EQ(pLeft->m_TerminalStatus, EParticipantTerminalStatus::LEFT);
	EXPECT_TRUE(pLeft->m_InFinalizedScoringCohort);
	EXPECT_EQ(pLeft->m_Saves, 2);
	EXPECT_EQ(Features.m_TotalSaves, 2);
}

TEST(ImpactPhase4Aggregation, NotEngagedRosterMemberExcludedFromScoringCohort)
{
	SRunSnapshot Snapshot = Phase4FeatureSnapshot();
	Snapshot.m_vParticipants[0].m_RawStats.m_Saves = 1;
	Snapshot.m_vParticipants[1].m_Engaged = false;
	Snapshot.m_vParticipants[1].m_RawStats.m_Saves = 100;
	SPhase4ScoringAggregationOptions Options;
	CRunScoringContext Features;
	ASSERT_TRUE(BuildPhase4ScoringFeatures(Snapshot, Options, &Features));
	EXPECT_EQ(Features.m_vPlayers.size(), 1U);
	EXPECT_EQ(Features.m_FinalizedEngagedParticipantCount, 1U);
	EXPECT_EQ(Features.m_TotalSaves, 1);
	EXPECT_EQ(FindScoringFeatures(Features, TestId(2)), nullptr);
}

TEST(ImpactPhase4Aggregation, AntiFarmClassificationDoesNotChangeCanonicalSaveCount)
{
	SRunSnapshot Snapshot = Phase4FeatureSnapshot();
	AddPhase4CanonicalSave(&Snapshot, 1, TestId(1), TestId(2), 20, 40, 50);
	AddPhase4CanonicalSave(&Snapshot, 2, TestId(1), TestId(2), 22, 40, 51);
	const int SavesBefore = Snapshot.m_vParticipants[0].m_RawStats.m_Saves;
	SPhase4ScoringAggregationOptions Options;
	Options.m_MaxRepeatedSaveSeparationTicks = 5;
	CRunScoringContext Features;
	ASSERT_TRUE(BuildPhase4ScoringFeatures(Snapshot, Options, &Features));
	EXPECT_EQ(FindScoringFeatures(Features, TestId(1))->m_Saves, SavesBefore);
	EXPECT_EQ(Features.m_TotalSaves, SavesBefore);
	EXPECT_EQ(Snapshot.m_vParticipants[0].m_RawStats.m_Saves, SavesBefore);
}

TEST(ImpactPhase4Aggregation, IndependentSaveRemainsIndependent)
{
	SRunSnapshot Snapshot = Phase4FeatureSnapshot();
	SParticipant Third = Snapshot.m_vParticipants[1];
	Third.m_Id = TestId(3);
	Third.m_Name = "Third";
	Snapshot.m_vParticipants.push_back(Third);
	AddPhase4CanonicalSave(&Snapshot, 1, TestId(1), TestId(2), 20, 40, 50);
	AddPhase4CanonicalSave(&Snapshot, 2, TestId(1), TestId(3), 25, 41, 51);
	SPhase4ScoringAggregationOptions Options;
	Options.m_MaxRepeatedSaveSeparationTicks = 5;
	CRunScoringContext Features;
	ASSERT_TRUE(BuildPhase4ScoringFeatures(Snapshot, Options, &Features));
	EXPECT_EQ(FindScoringFeatures(Features, TestId(1))->m_IndependentSaveCount, 2);
	EXPECT_EQ(FindScoringFeatures(Features, TestId(1))->m_RepeatedContextSaveCount, 0);
	EXPECT_EQ(FindScoringFeatures(Features, TestId(1))->m_UnknownSavePatternCount, 0);
}

TEST(ImpactPhase4Aggregation, RepeatedLoopClassifiesButDoesNotSuppressCounter)
{
	SRunSnapshot Snapshot = Phase4FeatureSnapshot();
	AddPhase4CanonicalSave(&Snapshot, 1, TestId(1), TestId(2), 20, 40, 50);
	AddPhase4CanonicalSave(&Snapshot, 2, TestId(1), TestId(2), 22, 40, 51);
	SPhase4ScoringAggregationOptions Options;
	Options.m_MaxRepeatedSaveSeparationTicks = 5;
	CRunScoringContext Features;
	ASSERT_TRUE(BuildPhase4ScoringFeatures(Snapshot, Options, &Features));
	const SPlayerScoringFeatures *pSaver = FindScoringFeatures(Features, TestId(1));
	ASSERT_NE(pSaver, nullptr);
	EXPECT_EQ(pSaver->m_Saves, 2);
	EXPECT_EQ(pSaver->m_IndependentSaveCount, 1);
	EXPECT_EQ(pSaver->m_RepeatedContextSaveCount, 1);
	EXPECT_EQ(Features.m_TotalSaves, 2);
}

TEST(ImpactPhase4Aggregation, HardPartRapidIndependentSavesRemainFullWeight)
{
	SRunSnapshot Snapshot = Phase4FeatureSnapshot();
	AddPhase4CanonicalSave(&Snapshot, 1, TestId(1), TestId(2), 20, 40, 50);
	AddPhase4CanonicalSave(&Snapshot, 2, TestId(1), TestId(2), 21, 41, 51);
	SPhase4ScoringAggregationOptions Options;
	Options.m_MaxRepeatedSaveSeparationTicks = 100000;
	CRunScoringContext Features;
	ASSERT_TRUE(BuildPhase4ScoringFeatures(Snapshot, Options, &Features));
	const SPlayerScoringFeatures *pSaver = FindScoringFeatures(Features, TestId(1));
	ASSERT_NE(pSaver, nullptr);
	EXPECT_EQ(pSaver->m_Saves, 2);
	EXPECT_EQ(pSaver->m_IndependentSaveCount, 2);
	EXPECT_EQ(pSaver->m_RepeatedContextSaveCount, 0);
	EXPECT_EQ(pSaver->m_UnknownSavePatternCount, 0);
}

TEST(ImpactPhase4Aggregation, TimeWindowDoesNotChangeSavePatternClassification)
{
	SRunSnapshot Snapshot = Phase4FeatureSnapshot();
	AddPhase4CanonicalSave(&Snapshot, 1, TestId(1), TestId(2), 20, 40, 50);
	AddPhase4CanonicalSave(&Snapshot, 2, TestId(1), TestId(2), 21, 41, 51);
	SPhase4ScoringAggregationOptions NarrowOptions;
	SPhase4ScoringAggregationOptions WideOptions;
	WideOptions.m_MaxRepeatedSaveSeparationTicks = 100000;
	CRunScoringContext NarrowFeatures;
	CRunScoringContext WideFeatures;
	ASSERT_TRUE(BuildPhase4ScoringFeatures(Snapshot, NarrowOptions, &NarrowFeatures));
	ASSERT_TRUE(BuildPhase4ScoringFeatures(Snapshot, WideOptions, &WideFeatures));
	const SPlayerScoringFeatures *pNarrow = FindScoringFeatures(NarrowFeatures, TestId(1));
	const SPlayerScoringFeatures *pWide = FindScoringFeatures(WideFeatures, TestId(1));
	ASSERT_NE(pNarrow, nullptr);
	ASSERT_NE(pWide, nullptr);
	EXPECT_EQ(pNarrow->m_IndependentSaveCount, pWide->m_IndependentSaveCount);
	EXPECT_EQ(pNarrow->m_RepeatedContextSaveCount, pWide->m_RepeatedContextSaveCount);
	EXPECT_EQ(pNarrow->m_UnknownSavePatternCount, pWide->m_UnknownSavePatternCount);
}

TEST(ImpactPhase4Aggregation, IncompleteEvidenceCannotProveRepeatedLoop)
{
	SRunSnapshot Snapshot = Phase4FeatureSnapshot();
	AddPhase4CanonicalSave(&Snapshot, 1, TestId(1), TestId(2), 20, 40, 50);
	AddPhase4CanonicalSave(&Snapshot, 2, TestId(1), TestId(2), 21, 40, 51);
	Snapshot.m_ScoringEvidenceComplete = false;
	Snapshot.m_ScoringEvidenceDroppedEvents = 1;
	SPhase4ScoringAggregationOptions Options;
	CRunScoringContext Features;
	ASSERT_TRUE(BuildPhase4ScoringFeatures(Snapshot, Options, &Features));
	const SPlayerScoringFeatures *pSaver = FindScoringFeatures(Features, TestId(1));
	ASSERT_NE(pSaver, nullptr);
	EXPECT_EQ(pSaver->m_Saves, 2);
	EXPECT_EQ(pSaver->m_IndependentSaveCount, 0);
	EXPECT_EQ(pSaver->m_RepeatedContextSaveCount, 0);
	EXPECT_EQ(pSaver->m_UnknownSavePatternCount, 2);
}

TEST(ImpactPhase4Aggregation, IncompleteEvidencePropagatesCompleteness)
{
	SRunSnapshot Snapshot = Phase4FeatureSnapshot();
	AddPhase4CanonicalSave(&Snapshot, 1, TestId(1), TestId(2), 20, 40, 50);
	Snapshot.m_ScoringEvidenceComplete = false;
	Snapshot.m_ScoringEvidenceDroppedEvents = 1;
	SPhase4ScoringAggregationOptions Options;
	CRunScoringContext Features;
	ASSERT_TRUE(BuildPhase4ScoringFeatures(Snapshot, Options, &Features));
	EXPECT_EQ(Features.m_ScoringEvidenceStatus, EPhase4EvidenceStatus::INCOMPLETE);
	EXPECT_EQ(Features.m_ScoringEvidenceDroppedEvents, 1U);
	EXPECT_EQ(Features.m_TotalSaves, 1);
	EXPECT_EQ(Features.m_UnknownSavePatternCount, 1);
	EXPECT_FALSE(Features.m_SavePatternEvidenceCoverageComplete);
}

TEST(ImpactPhase4Aggregation, GenerationIdentityDoesNotLeakBetweenPlayers)
{
	SRunSnapshot Snapshot = Phase4FeatureSnapshot();
	Snapshot.m_vParticipants[0].m_Id = TestId(1, 4);
	SParticipant NewGeneration = Snapshot.m_vParticipants[0];
	NewGeneration.m_Id = TestId(1, 5);
	NewGeneration.m_Name = "New generation";
	NewGeneration.m_RawStats = {};
	NewGeneration.m_RescuesReceived = 0;
	Snapshot.m_vParticipants.push_back(NewGeneration);
	AddPhase4CanonicalSave(&Snapshot, 1, TestId(1, 4), TestId(2), 20, 40, 50);
	AddPhase4CanonicalSave(&Snapshot, 2, TestId(1, 5), TestId(2), 30, 41, 51);
	SPhase4ScoringAggregationOptions Options;
	CRunScoringContext Features;
	ASSERT_TRUE(BuildPhase4ScoringFeatures(Snapshot, Options, &Features));
	ASSERT_NE(FindScoringFeatures(Features, TestId(1, 4)), nullptr);
	ASSERT_NE(FindScoringFeatures(Features, TestId(1, 5)), nullptr);
	EXPECT_EQ(FindScoringFeatures(Features, TestId(1, 4))->m_Saves, 1);
	EXPECT_EQ(FindScoringFeatures(Features, TestId(1, 5))->m_Saves, 1);
}

TEST(ImpactPhase4Aggregation, FinalizedAggregationIsDeterministic)
{
	SRunSnapshot Snapshot = Phase4FeatureSnapshot();
	AddPhase4CanonicalSave(&Snapshot, 1, TestId(1), TestId(2), 20, 40, 50);
	SPhase4ScoringAggregationOptions Options;
	Options.m_MaxRepeatedSaveSeparationTicks = 5;
	CRunScoringContext First;
	CRunScoringContext Second;
	ASSERT_TRUE(BuildPhase4ScoringFeatures(Snapshot, Options, &First));
	ASSERT_TRUE(BuildPhase4ScoringFeatures(Snapshot, Options, &Second));
	EXPECT_EQ(FormatPhase4ScoringFeatures(First), FormatPhase4ScoringFeatures(Second));
	EXPECT_EQ(First.m_TotalSaves, Second.m_TotalSaves);
	EXPECT_EQ(First.m_vPlayers.size(), Second.m_vPlayers.size());
}

TEST(ImpactPhase4Aggregation, OldHistoryMissingPhase4FieldsLoadsAsUnknown)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	SRunSnapshot Legacy = Phase4FeatureSnapshot(9901);
	// The old aggregate reports one Save, but the retained legacy event ledger
	// cannot account for it. RescuesReceived therefore must remain UNKNOWN.
	Legacy.m_vParticipants[0].m_RawStats.m_Saves = 1;
	Legacy.m_RawStats.m_Saves = 1;
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {Legacy}));
	void *pData = nullptr;
	unsigned Length = 0;
	ASSERT_TRUE(Storage->ReadFile("amf_impact/history.json", IStorage::TYPE_SAVE, &pData, &Length));
	ASSERT_NE(pData, nullptr);
	std::string Json(static_cast<const char *>(pData), Length);
	std::free(pData);
	for(const char *pProperty : {"run_uid", "global_run_number", "terminal_status", "phase4_score", "rescues_received", "rescues_received_known", "participation_ticks", "participation_ticks_known", "participation_start_tick", "participation_terminal_tick", "scoring_evidence_version", "scoring_evidence_complete", "scoring_evidence_dropped", "scoring_evidence"})
		while(RemoveJsonObjectProperty(&Json, pProperty))
	ASSERT_TRUE(WriteTestStorageText(Storage.get(), "amf_impact/history.json", Json));
	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), 1U);
	EXPECT_EQ(Loaded[0].m_ScoringEvidenceVersion, 0);
	SPhase4ScoringAggregationOptions Options;
	CRunScoringContext Features;
	ASSERT_TRUE(BuildPhase4ScoringFeatures(Loaded[0], Options, &Features));
	EXPECT_EQ(Features.m_ScoringEvidenceStatus, EPhase4EvidenceStatus::UNAVAILABLE);
	for(const SPlayerScoringFeatures &Player : Features.m_vPlayers)
	{
		EXPECT_EQ(Player.m_TerminalStatus, EParticipantTerminalStatus::UNKNOWN);
		EXPECT_FALSE(Player.m_ParticipationKnown);
		EXPECT_FALSE(Player.m_RescuesReceivedKnown);
	}
}

TEST(ImpactPhase4Aggregation, ProvenRescueAssistRemainsUnsupportedWithoutStrictLinkage)
{
	SRunSnapshot Snapshot = Phase4FeatureSnapshot();
	SParticipant Helper = Snapshot.m_vParticipants[1];
	Helper.m_Id = TestId(3);
	Helper.m_Name = "Potential helper";
	Helper.m_RawStats = {};
	Snapshot.m_vParticipants.push_back(Helper);
	AddPhase4CanonicalSave(&Snapshot, 1, TestId(1), TestId(2), 20, 40, 50);
	Snapshot.m_vEvents[0].m_vHelpers.push_back(TestId(3));
	SPhase4ScoringAggregationOptions Options;
	CRunScoringContext Features;
	ASSERT_TRUE(BuildPhase4ScoringFeatures(Snapshot, Options, &Features));
	const SPlayerScoringFeatures *pHelper = FindScoringFeatures(Features, TestId(3));
	ASSERT_NE(pHelper, nullptr);
	EXPECT_EQ(pHelper->m_ProvenRescueAssistSupport, EPhase4FeatureSupport::UNSUPPORTED);
	EXPECT_FALSE(pHelper->m_ProvenRescueAssistsKnown);
	EXPECT_EQ(pHelper->m_ProvenRescueAssists, 0);
	EXPECT_NE(FormatPhase4ScoringFeatures(Features).find("proven_rescue_assists=UNSUPPORTED:UNKNOWN"), std::string::npos);
}

TEST(ImpactPhase4Aggregation, ActiveSnapshotIsNotFinalScoringInput)
{
	SRunSnapshot Snapshot = Phase4FeatureSnapshot();
	Snapshot.m_State = EAnalysisState::ACTIVE;
	Snapshot.m_Result = ERunResult::NONE;
	SPhase4ScoringAggregationOptions Options;
	CRunScoringContext Features;
	EXPECT_FALSE(BuildPhase4ScoringFeatures(Snapshot, Options, &Features));
	EXPECT_FALSE(Features.m_Available);
}

static SPlayerScoringFeatures MakeStabilityFeature(int ClientId, int64_t Saves, int64_t Falls, int64_t RescuesReceived,
	int64_t ParticipationTicks, int64_t IndependentSaves, int64_t RepeatedSaves, int64_t UnknownPatternSaves,
	bool RescuesReceivedKnown = true, bool ParticipationKnown = true)
{
	SPlayerScoringFeatures Player;
	Player.m_Id = TestId(ClientId);
	Player.m_Name = "Stability test player";
	Player.m_TerminalStatus = EParticipantTerminalStatus::FINISHED;
	Player.m_Engaged = true;
	Player.m_Finalized = true;
	Player.m_InFinalizedScoringCohort = true;
	Player.m_ParticipationKnown = ParticipationKnown;
	Player.m_ParticipationTicks = ParticipationTicks;
	Player.m_Saves = Saves;
	Player.m_Falls = Falls;
	Player.m_Aleds = 0;
	Player.m_Clutches = 0;
	Player.m_RescuesReceived = RescuesReceived;
	Player.m_RescuesReceivedKnown = RescuesReceivedKnown;
	Player.m_IndependentSaveCount = IndependentSaves;
	Player.m_RepeatedContextSaveCount = RepeatedSaves;
	Player.m_UnknownSavePatternCount = UnknownPatternSaves;
	Player.m_SavePatternEvidenceCoverageComplete = true;
	Player.m_SavePatternClassificationComplete = true;
	return Player;
}

static CRunScoringContext MakeStabilityContext(std::vector<SPlayerScoringFeatures> Players,
	EPhase4EvidenceStatus EvidenceStatus = EPhase4EvidenceStatus::COMPLETE, uint64_t DroppedEvents = 0)
{
	CRunScoringContext Context;
	Context.m_Available = true;
	Context.m_RunId = 50001;
	Context.m_TeamId = 7;
	Context.m_TickSpeed = 50;
	Context.m_RosterParticipantCount = Players.size();
	Context.m_ScoringEvidenceVersion = EvidenceStatus == EPhase4EvidenceStatus::UNAVAILABLE ? 0 : IMPACT_SCORING_EVIDENCE_VERSION;
	Context.m_ScoringEvidenceStatus = EvidenceStatus;
	Context.m_ScoringEvidenceDroppedEvents = DroppedEvents;
	Context.m_SavePatternEvidenceCoverageComplete = EvidenceStatus == EPhase4EvidenceStatus::COMPLETE && DroppedEvents == 0;
	Context.m_SavePatternClassificationComplete = Context.m_SavePatternEvidenceCoverageComplete;
	for(const SPlayerScoringFeatures &Player : Players)
	{
		if(!Player.m_InFinalizedScoringCohort)
			continue;
		++Context.m_FinalizedEngagedParticipantCount;
		Context.m_TotalSaves += Player.m_Saves;
		Context.m_TotalFalls += Player.m_Falls;
		Context.m_TotalAleds += Player.m_Aleds;
		Context.m_TotalClutches += Player.m_Clutches;
	}
	Context.m_vPlayers = std::move(Players);
	return Context;
}

static bool ScoreStability(const CRunScoringContext &Context, const SParticipantId &Id, SStabilityResult *pResult)
{
	const auto It = std::find_if(Context.m_vPlayers.begin(), Context.m_vPlayers.end(), [&Id](const SPlayerScoringFeatures &Player) {
		return Player.m_Id == Id;
	});
	return It != Context.m_vPlayers.end() && CalculateStabilityV1(*It, Context, pResult);
}

TEST(ImpactStabilityV1, ZeroEvidenceIsNeutral)
{
	const SPlayerScoringFeatures Player = MakeStabilityFeature(1, 0, 0, 0, 30000, 0, 0, 0);
	const CRunScoringContext Context = MakeStabilityContext({Player});
	SStabilityResult Result;
	ASSERT_TRUE(ScoreStability(Context, Player.m_Id, &Result));
	EXPECT_DOUBLE_EQ(Result.m_RawStability, 50.0);
	EXPECT_DOUBLE_EQ(Result.m_Value, 50.0);
}

TEST(ImpactStabilityV1, FallStrictlyReducesStability)
{
	const auto Score = [](int Falls) {
		const SPlayerScoringFeatures Player = MakeStabilityFeature(1, 3, Falls, 0, 30000, 3, 0, 0);
		const CRunScoringContext Context = MakeStabilityContext({Player});
		SStabilityResult Result;
		return ScoreStability(Context, Player.m_Id, &Result) ? Result.m_Value : -1.0;
	};
	EXPECT_GT(Score(1), Score(2));
}

TEST(ImpactStabilityV1, IndependentSaveRaisesStability)
{
	const SPlayerScoringFeatures NoSaves = MakeStabilityFeature(1, 0, 1, 0, 30000, 0, 0, 0);
	const SPlayerScoringFeatures OneSave = MakeStabilityFeature(1, 1, 1, 0, 30000, 1, 0, 0);
	const CRunScoringContext NoSaveContext = MakeStabilityContext({NoSaves});
	const CRunScoringContext OneSaveContext = MakeStabilityContext({OneSave});
	SStabilityResult NoSaveResult;
	SStabilityResult OneSaveResult;
	ASSERT_TRUE(ScoreStability(NoSaveContext, NoSaves.m_Id, &NoSaveResult));
	ASSERT_TRUE(ScoreStability(OneSaveContext, OneSave.m_Id, &OneSaveResult));
	EXPECT_GT(OneSaveResult.m_Value, NoSaveResult.m_Value);
}

TEST(ImpactStabilityV1, RepeatedSaveRaisesLessThanIndependent)
{
	const SPlayerScoringFeatures Independent = MakeStabilityFeature(1, 1, 2, 0, 30000, 1, 0, 0);
	const SPlayerScoringFeatures Repeated = MakeStabilityFeature(1, 1, 2, 0, 30000, 0, 1, 0);
	const SPlayerScoringFeatures NoSave = MakeStabilityFeature(1, 0, 2, 0, 30000, 0, 0, 0);
	const CRunScoringContext IndependentContext = MakeStabilityContext({Independent});
	const CRunScoringContext RepeatedContext = MakeStabilityContext({Repeated});
	const CRunScoringContext NoSaveContext = MakeStabilityContext({NoSave});
	SStabilityResult IndependentResult;
	SStabilityResult RepeatedResult;
	SStabilityResult NoSaveResult;
	ASSERT_TRUE(ScoreStability(IndependentContext, Independent.m_Id, &IndependentResult));
	ASSERT_TRUE(ScoreStability(RepeatedContext, Repeated.m_Id, &RepeatedResult));
	ASSERT_TRUE(ScoreStability(NoSaveContext, NoSave.m_Id, &NoSaveResult));
	EXPECT_GT(IndependentResult.m_Value, RepeatedResult.m_Value);
	EXPECT_GT(RepeatedResult.m_Value, NoSaveResult.m_Value);
}

TEST(ImpactStabilityV1, CanonicalSaveCountIsNotSuppressed)
{
	const SPlayerScoringFeatures Player = MakeStabilityFeature(1, 10, 4, 0, 30000, 3, 7, 0);
	const CRunScoringContext Context = MakeStabilityContext({Player});
	SStabilityResult Result;
	ASSERT_TRUE(ScoreStability(Context, Player.m_Id, &Result));
	EXPECT_EQ(Result.m_CanonicalSaves, 10);
	EXPECT_EQ(Result.m_RepeatedLoopSaves, 7);
}

TEST(ImpactStabilityV1, RescueReceivedDoesNotAffectStability)
{
	const SPlayerScoringFeatures NoneReceived = MakeStabilityFeature(1, 4, 1, 0, 30000, 4, 0, 0);
	const SPlayerScoringFeatures ManyReceived = MakeStabilityFeature(1, 4, 1, 99, 30000, 4, 0, 0);
	const CRunScoringContext NoneContext = MakeStabilityContext({NoneReceived});
	const CRunScoringContext ManyContext = MakeStabilityContext({ManyReceived});
	SStabilityResult NoneResult;
	SStabilityResult ManyResult;
	ASSERT_TRUE(ScoreStability(NoneContext, NoneReceived.m_Id, &NoneResult));
	ASSERT_TRUE(ScoreStability(ManyContext, ManyReceived.m_Id, &ManyResult));
	EXPECT_DOUBLE_EQ(SStabilityV1Constants::m_RescueReceivedWeight, 0.0);
	EXPECT_EQ(ManyResult.m_RescuesReceived, 99);
	EXPECT_TRUE(ManyResult.m_RescuesReceivedKnown);
	EXPECT_DOUBLE_EQ(ManyResult.m_NegativeEvidence, 1.0);
	EXPECT_DOUBLE_EQ(ManyResult.m_NegativeEvidence, NoneResult.m_NegativeEvidence);
	EXPECT_DOUBLE_EQ(ManyResult.m_RawStability, NoneResult.m_RawStability);
	EXPECT_DOUBLE_EQ(ManyResult.m_Value, NoneResult.m_Value);
}

TEST(ImpactStabilityV1, UnknownRescueReceivedIsNeutral)
{
	const SPlayerScoringFeatures KnownZero = MakeStabilityFeature(1, 4, 1, 0, 30000, 4, 0, 0, true);
	const SPlayerScoringFeatures Unknown = MakeStabilityFeature(1, 4, 1, 0, 30000, 4, 0, 0, false);
	const CRunScoringContext KnownContext = MakeStabilityContext({KnownZero});
	const CRunScoringContext UnknownContext = MakeStabilityContext({Unknown});
	SStabilityResult KnownResult;
	SStabilityResult UnknownResult;
	ASSERT_TRUE(ScoreStability(KnownContext, KnownZero.m_Id, &KnownResult));
	ASSERT_TRUE(ScoreStability(UnknownContext, Unknown.m_Id, &UnknownResult));
	EXPECT_DOUBLE_EQ(UnknownResult.m_NegativeEvidence, KnownResult.m_NegativeEvidence);
	EXPECT_DOUBLE_EQ(UnknownResult.m_RawStability, KnownResult.m_RawStability);
	EXPECT_DOUBLE_EQ(UnknownResult.m_Value, KnownResult.m_Value);
	EXPECT_FALSE(UnknownResult.m_RescuesReceivedKnown);
}

TEST(ImpactStabilityV1, ShortParticipationShrinksTowardNeutral)
{
	const SPlayerScoringFeatures Long = MakeStabilityFeature(1, 5, 0, 0, 30000, 5, 0, 0);
	const SPlayerScoringFeatures Short = MakeStabilityFeature(1, 5, 0, 0, 2250, 5, 0, 0);
	const CRunScoringContext LongContext = MakeStabilityContext({Long});
	const CRunScoringContext ShortContext = MakeStabilityContext({Short});
	SStabilityResult LongResult;
	SStabilityResult ShortResult;
	ASSERT_TRUE(ScoreStability(LongContext, Long.m_Id, &LongResult));
	ASSERT_TRUE(ScoreStability(ShortContext, Short.m_Id, &ShortResult));
	EXPECT_DOUBLE_EQ(LongResult.m_RawStability, ShortResult.m_RawStability);
	EXPECT_GT(LongResult.m_Value, ShortResult.m_Value);
	EXPECT_GT(ShortResult.m_Value, 50.0);
}

TEST(ImpactStabilityV1, LongZeroEventParticipationDoesNotCreateEliteStability)
{
	const SPlayerScoringFeatures Player = MakeStabilityFeature(1, 0, 0, 0, 30000, 0, 0, 0);
	const CRunScoringContext Context = MakeStabilityContext({Player});
	SStabilityResult Result;
	ASSERT_TRUE(ScoreStability(Context, Player.m_Id, &Result));
	EXPECT_DOUBLE_EQ(Result.m_Value, 50.0);
	EXPECT_LT(Result.m_Value, 70.0);
}

TEST(ImpactStabilityV1, IncompleteEvidenceShrinksTowardNeutral)
{
	const SPlayerScoringFeatures Player = MakeStabilityFeature(1, 8, 0, 0, 30000, 8, 0, 0);
	const CRunScoringContext CompleteContext = MakeStabilityContext({Player});
	const CRunScoringContext IncompleteContext = MakeStabilityContext({Player}, EPhase4EvidenceStatus::INCOMPLETE, 2);
	SStabilityResult CompleteResult;
	SStabilityResult IncompleteResult;
	ASSERT_TRUE(ScoreStability(CompleteContext, Player.m_Id, &CompleteResult));
	ASSERT_TRUE(ScoreStability(IncompleteContext, Player.m_Id, &IncompleteResult));
	EXPECT_DOUBLE_EQ(IncompleteResult.m_EvidenceConfidence, SStabilityV1Constants::m_IncompleteEvidenceConfidence);
	EXPECT_LT(std::abs(IncompleteResult.m_Value - 50.0), std::abs(CompleteResult.m_Value - 50.0));
}

TEST(ImpactStabilityV1, UnknownEvidenceShrinksTowardNeutral)
{
	const SPlayerScoringFeatures Player = MakeStabilityFeature(1, 8, 0, 0, 30000, 0, 0, 8);
	const CRunScoringContext UnknownContext = MakeStabilityContext({Player}, EPhase4EvidenceStatus::UNAVAILABLE);
	const CRunScoringContext CompleteContext = MakeStabilityContext({Player});
	SStabilityResult UnknownResult;
	SStabilityResult CompleteResult;
	ASSERT_TRUE(ScoreStability(UnknownContext, Player.m_Id, &UnknownResult));
	ASSERT_TRUE(ScoreStability(CompleteContext, Player.m_Id, &CompleteResult));
	EXPECT_DOUBLE_EQ(UnknownResult.m_EvidenceConfidence, SStabilityV1Constants::m_UnknownEvidenceConfidence);
	EXPECT_LT(std::abs(UnknownResult.m_Value - 50.0), std::abs(CompleteResult.m_Value - 50.0));
}

static CRunScoringContext MakePeerFallRateContext()
{
	constexpr int64_t TenMinutes = 10 * 60 * 50;
	return MakeStabilityContext({
		MakeStabilityFeature(1, 0, 0, 0, TenMinutes, 0, 0, 0),
		MakeStabilityFeature(2, 0, 10, 0, TenMinutes, 0, 0, 0),
		MakeStabilityFeature(3, 0, 20, 0, TenMinutes, 0, 0, 0),
	});
}

TEST(ImpactStabilityV1, PeerAdjustmentIsBounded)
{
	constexpr int64_t OneMinute = 60 * 50;
	const CRunScoringContext PositiveContext = MakeStabilityContext({
		MakeStabilityFeature(1, 0, 0, 0, OneMinute, 0, 0, 0),
		MakeStabilityFeature(2, 0, 1000, 0, OneMinute, 0, 0, 0),
	});
	const CRunScoringContext NegativeContext = MakeStabilityContext({
		MakeStabilityFeature(1, 0, 1000, 0, OneMinute, 0, 0, 0),
		MakeStabilityFeature(2, 0, 0, 0, OneMinute, 0, 0, 0),
	});
	SStabilityResult Positive;
	SStabilityResult Negative;
	ASSERT_TRUE(ScoreStability(PositiveContext, TestId(1), &Positive));
	ASSERT_TRUE(ScoreStability(NegativeContext, TestId(1), &Negative));
	EXPECT_LE(std::abs(Positive.m_PeerAdjustment), SStabilityV1Constants::m_PeerAdjustmentMax);
	EXPECT_LE(std::abs(Negative.m_PeerAdjustment), SStabilityV1Constants::m_PeerAdjustmentMax);
	EXPECT_DOUBLE_EQ(Positive.m_PeerAdjustment, SStabilityV1Constants::m_PeerAdjustmentMax);
	EXPECT_DOUBLE_EQ(Negative.m_PeerAdjustment, -SStabilityV1Constants::m_PeerAdjustmentMax);
}

TEST(ImpactStabilityV1, PeerBelowMedianCanReceiveSmallPositiveAdjustment)
{
	const CRunScoringContext Context = MakePeerFallRateContext();
	SStabilityResult Result;
	ASSERT_TRUE(ScoreStability(Context, TestId(1), &Result));
	EXPECT_DOUBLE_EQ(Result.m_PlayerFallRatePerMinute, 0.0);
	EXPECT_DOUBLE_EQ(Result.m_CohortMedianFallRatePerMinute, 1.0);
	EXPECT_DOUBLE_EQ(Result.m_PeerAdjustment, 4.0);
	EXPECT_DOUBLE_EQ(Result.m_PeerAdjustmentApplied, 4.0);
}

TEST(ImpactStabilityV1, PeerAboveMedianCanReceiveSmallNegativeAdjustment)
{
	const CRunScoringContext Context = MakePeerFallRateContext();
	SStabilityResult Result;
	ASSERT_TRUE(ScoreStability(Context, TestId(3), &Result));
	EXPECT_DOUBLE_EQ(Result.m_PlayerFallRatePerMinute, 2.0);
	EXPECT_DOUBLE_EQ(Result.m_CohortMedianFallRatePerMinute, 1.0);
	EXPECT_DOUBLE_EQ(Result.m_PeerAdjustment, -4.0);
	EXPECT_DOUBLE_EQ(Result.m_PeerAdjustmentApplied, -4.0);
}

TEST(ImpactStabilityV1, PeerEqualMedianHasNoAdjustment)
{
	const CRunScoringContext Context = MakePeerFallRateContext();
	SStabilityResult Result;
	ASSERT_TRUE(ScoreStability(Context, TestId(2), &Result));
	EXPECT_DOUBLE_EQ(Result.m_PlayerFallRatePerMinute, Result.m_CohortMedianFallRatePerMinute);
	EXPECT_DOUBLE_EQ(Result.m_PeerAdjustment, 0.0);
}

TEST(ImpactStabilityV1, TeamSizeAloneDoesNotMultiplyScore)
{
	const SPlayerScoringFeatures Player = MakeStabilityFeature(1, 4, 2, 0, 30000, 4, 0, 0);
	const CRunScoringContext SoloContext = MakeStabilityContext({Player});
	SPlayerScoringFeatures MatchingPeer = Player;
	MatchingPeer.m_Id = TestId(2);
	const CRunScoringContext LargerContext = MakeStabilityContext({Player, MatchingPeer});
	SStabilityResult Solo;
	SStabilityResult Larger;
	ASSERT_TRUE(ScoreStability(SoloContext, Player.m_Id, &Solo));
	ASSERT_TRUE(ScoreStability(LargerContext, Player.m_Id, &Larger));
	EXPECT_DOUBLE_EQ(Solo.m_RawStability, Larger.m_RawStability);
	EXPECT_DOUBLE_EQ(Solo.m_Value, Larger.m_Value);
}

TEST(ImpactStabilityV1, AledDoesNotDoubleRewardStability)
{
	const SPlayerScoringFeatures WithoutAled = MakeStabilityFeature(1, 4, 1, 0, 30000, 4, 0, 0);
	SPlayerScoringFeatures WithAled = WithoutAled;
	WithAled.m_Aleds = 1;
	const CRunScoringContext WithoutContext = MakeStabilityContext({WithoutAled});
	const CRunScoringContext WithContext = MakeStabilityContext({WithAled});
	SStabilityResult Without;
	SStabilityResult With;
	ASSERT_TRUE(ScoreStability(WithoutContext, WithoutAled.m_Id, &Without));
	ASSERT_TRUE(ScoreStability(WithContext, WithAled.m_Id, &With));
	EXPECT_DOUBLE_EQ(Without.m_Value, With.m_Value);
}

TEST(ImpactStabilityV1, ClutchDoesNotDoubleRewardStability)
{
	const SPlayerScoringFeatures WithoutClutch = MakeStabilityFeature(1, 4, 1, 0, 30000, 4, 0, 0);
	SPlayerScoringFeatures WithClutch = WithoutClutch;
	WithClutch.m_Clutches = 1;
	const CRunScoringContext WithoutContext = MakeStabilityContext({WithoutClutch});
	const CRunScoringContext WithContext = MakeStabilityContext({WithClutch});
	SStabilityResult Without;
	SStabilityResult With;
	ASSERT_TRUE(ScoreStability(WithoutContext, WithoutClutch.m_Id, &Without));
	ASSERT_TRUE(ScoreStability(WithContext, WithClutch.m_Id, &With));
	EXPECT_DOUBLE_EQ(Without.m_Value, With.m_Value);
}

TEST(ImpactStabilityV1, NotEngagedPlayerNotScored)
{
	SPlayerScoringFeatures Player = MakeStabilityFeature(1, 0, 0, 0, 30000, 0, 0, 0);
	Player.m_Engaged = false;
	Player.m_InFinalizedScoringCohort = false;
	const CRunScoringContext Context = MakeStabilityContext({Player});
	SStabilityResult Result;
	EXPECT_FALSE(ScoreStability(Context, Player.m_Id, &Result));
	EXPECT_FALSE(Result.m_Available);
}

TEST(ImpactStabilityV1, DeterministicSameInputSameOutput)
{
	const CRunScoringContext Context = MakePeerFallRateContext();
	SStabilityResult First;
	SStabilityResult Second;
	ASSERT_TRUE(ScoreStability(Context, TestId(3), &First));
	ASSERT_TRUE(ScoreStability(Context, TestId(3), &Second));
	EXPECT_EQ(FormatStabilityV1Result(First), FormatStabilityV1Result(Second));
	EXPECT_DOUBLE_EQ(First.m_Value, Second.m_Value);
}

TEST(ImpactStabilityV1, ResultAlwaysWithinZeroToHundred)
{
	for(int Saves = 0; Saves <= 100; Saves += 5)
	{
		for(int Falls = 0; Falls <= 100; Falls += 5)
		{
			const SPlayerScoringFeatures Player = MakeStabilityFeature(1, Saves, Falls, Falls, 1, Saves, 0, 0);
			const CRunScoringContext Context = MakeStabilityContext({Player}, EPhase4EvidenceStatus::INCOMPLETE, 1);
			SStabilityResult Result;
			ASSERT_TRUE(ScoreStability(Context, Player.m_Id, &Result));
			EXPECT_GE(Result.m_Value, 0.0);
			EXPECT_LE(Result.m_Value, 100.0);
			EXPECT_GE(Result.m_RawStability, 0.0);
			EXPECT_LE(Result.m_RawStability, 100.0);
		}
	}
}

TEST(ImpactStabilityV1, Phase2CountersRemainUntouched)
{
	const SPlayerScoringFeatures Player = MakeStabilityFeature(1, 8, 4, 3, 30000, 6, 2, 0);
	const CRunScoringContext Context = MakeStabilityContext({Player});
	const int64_t SavesBefore = Context.m_vPlayers[0].m_Saves;
	const int64_t FallsBefore = Context.m_vPlayers[0].m_Falls;
	const int64_t AledsBefore = Context.m_vPlayers[0].m_Aleds;
	const int64_t ClutchesBefore = Context.m_vPlayers[0].m_Clutches;
	SStabilityResult Result;
	ASSERT_TRUE(ScoreStability(Context, Player.m_Id, &Result));
	EXPECT_EQ(Context.m_vPlayers[0].m_Saves, SavesBefore);
	EXPECT_EQ(Context.m_vPlayers[0].m_Falls, FallsBefore);
	EXPECT_EQ(Context.m_vPlayers[0].m_Aleds, AledsBefore);
	EXPECT_EQ(Context.m_vPlayers[0].m_Clutches, ClutchesBefore);
}

TEST(ImpactStabilityV1, CalibrationScenariosProduceFullPrecisionDiagnostics)
{
	constexpr int64_t TenMinutes = 10 * 60 * 50;
	constexpr int64_t FortyFiveSeconds = 45 * 50;
	struct SCalibrationCase
	{
		const char *m_pName;
		SPlayerScoringFeatures m_Player;
	};
	const std::array<SCalibrationCase, 10> vCases{{
		{"A", MakeStabilityFeature(1, 23, 5, 5, TenMinutes, 23, 0, 0)},
		{"B", MakeStabilityFeature(2, 13, 11, 11, TenMinutes, 13, 0, 0)},
		{"C", MakeStabilityFeature(3, 1, 8, 8, TenMinutes, 1, 0, 0)},
		{"D", MakeStabilityFeature(4, 0, 0, 0, TenMinutes, 0, 0, 0)},
		{"E", MakeStabilityFeature(5, 10, 4, 4, TenMinutes, 10, 0, 0)},
		{"F", MakeStabilityFeature(6, 10, 4, 4, FortyFiveSeconds, 10, 0, 0)},
		{"G", MakeStabilityFeature(7, 10, 4, 0, TenMinutes, 10, 0, 0)},
		{"H", MakeStabilityFeature(8, 10, 4, 0, TenMinutes, 3, 7, 0)},
		{"I", MakeStabilityFeature(9, 20, 20, 0, TenMinutes, 20, 0, 0)},
		{"J", MakeStabilityFeature(10, 5, 1, 0, TenMinutes, 5, 0, 0)},
	}};
	double ScoreA = 0.0;
	double ScoreB = 0.0;
	double ScoreE = 0.0;
	double ScoreF = 0.0;
	for(size_t i = 0; i < vCases.size(); ++i)
	{
		const SCalibrationCase &Case = vCases[i];
		const CRunScoringContext Context = MakeStabilityContext({Case.m_Player});
		SStabilityResult Result;
		ASSERT_TRUE(ScoreStability(Context, Case.m_Player.m_Id, &Result));
		std::cout << "STABILITY_CALIBRATION " << Case.m_pName << ' ' << FormatStabilityV1Result(Result) << '\n';
		if(Case.m_pName[0] == 'A')
			ScoreA = Result.m_Value;
		else if(Case.m_pName[0] == 'B')
			ScoreB = Result.m_Value;
		else if(Case.m_pName[0] == 'E')
			ScoreE = Result.m_Value;
		else if(Case.m_pName[0] == 'F')
			ScoreF = Result.m_Value;
	}
	EXPECT_GT(ScoreA, ScoreB);
	EXPECT_GT(ScoreA - ScoreB, 10.0);
	EXPECT_GT(ScoreE, ScoreF);

	const CRunScoringContext PeerContext = MakePeerFallRateContext();
	for(const int ClientId : {1, 2, 3})
	{
		SStabilityResult Result;
		ASSERT_TRUE(CalculateStabilityV1(PeerContext.m_vPlayers[static_cast<size_t>(ClientId - 1)], PeerContext, &Result));
		std::cout << "STABILITY_PEER_SAMPLE " << ClientId << ' ' << FormatStabilityV1Result(Result) << '\n';
	}
}

TEST(ImpactForensic, ForensicDoesNotChangeCounters)
{
	CReplay WithTrace(901, TestContext());
	CReplay WithoutTrace(901, TestContext());
	WithoutTrace.Phase2().SetForensicTraceEnabled(false);
	WithoutTrace.Run().SetForensicTraceEnabled(false);
	StartPair(WithTrace);
	StartPair(WithoutTrace);
	auto SubmitParity = [&](const std::vector<SImpactGameplayInput> &vInputs) {
		const bool WithTraceChanged = SubmitLiveEquivalentInputs(WithTrace, vInputs);
		const bool WithoutTraceChanged = SubmitLiveEquivalentInputs(WithoutTrace, vInputs);
		EXPECT_EQ(WithTraceChanged, WithoutTraceChanged);
		ExpectCanonicalEventsEqual(WithTrace.LastOutput().m_vEvents, WithoutTrace.LastOutput().m_vEvents);
	};

	SImpactGameplayInput Saver = ImpactInput(1, 90, false, true, false, 1, false, EImpactRecoveryNeed::UNKNOWN);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, false, 1, false, EImpactRecoveryNeed::UNKNOWN);
	SubmitParity({Saver, Target});

	Saver = ImpactInput(1, 100, false, true, false, 1, false, EImpactRecoveryNeed::UNKNOWN);
	SetHooked(Saver, 2, true);
	Target = ImpactInput(2, 100, true, true, false, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	SetPhysicalHarmfulOutcome(Target);
	SubmitParity({Saver, Target});

	Saver.m_ServerTick = Target.m_ServerTick = 101;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	SubmitParity({Saver, Target});
	EXPECT_EQ(WithTrace.LastOutput().m_vEvents.size(), WithoutTrace.LastOutput().m_vEvents.size());
	ExpectCanonicalEventsEqual(WithTrace.LastOutput().m_vEvents, WithoutTrace.LastOutput().m_vEvents);

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_X = 16;
	Target.m_VelX = 2;
	SubmitParity({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 120;
	ReleaseHook(Saver);
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	SubmitParity({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 155;
	SubmitParity({Saver, Target});

	const SRawStats &TracedStats = WithTrace.Run().RawStats();
	const SRawStats &UntracedStats = WithoutTrace.Run().RawStats();
	ExpectRawStatsEqual(TracedStats, UntracedStats);
	ExpectCanonicalEventsEqual(WithTrace.Run().Events(), WithoutTrace.Run().Events());
	EXPECT_EQ(TracedStats.m_Falls, 1);
	EXPECT_EQ(TracedStats.m_Saves, 1);
	const SRunSnapshot TracedSnapshot = WithTrace.Run().Snapshot(155);
	const SRunSnapshot UntracedSnapshot = WithoutTrace.Run().Snapshot(155);
	EXPECT_TRUE(TracedSnapshot.m_ForensicTraceEnabled);
	EXPECT_TRUE(TracedSnapshot.m_ForensicTraceComplete);
	EXPECT_FALSE(UntracedSnapshot.m_ForensicTraceEnabled);
	EXPECT_FALSE(UntracedSnapshot.m_ForensicTraceComplete);
	EXPECT_FALSE(WithTrace.Run().ForensicEvents().empty());
	EXPECT_TRUE(WithoutTrace.Run().ForensicEvents().empty());
	const auto &Forensic = WithTrace.Run().ForensicEvents();
	const auto ConfirmedFall = std::find_if(Forensic.begin(), Forensic.end(), [](const SImpactForensicEvent &Event) {
		return Event.m_Type == EImpactForensicType::FALL_CONFIRMED;
	});
	ASSERT_NE(ConfirmedFall, Forensic.end());
	EXPECT_NE(ConfirmedFall->m_FallEpisodeId, 0U);
	const auto ConfirmedSave = std::find_if(Forensic.begin(), Forensic.end(), [](const SImpactForensicEvent &Event) {
		return Event.m_Type == EImpactForensicType::SAVE_CONFIRMED;
	});
	ASSERT_NE(ConfirmedSave, Forensic.end());
	EXPECT_NE(ConfirmedSave->m_SaveCandidateId, 0U);
	EXPECT_NE(ConfirmedSave->m_EventId, 0U);
	EXPECT_EQ(ConfirmedSave->m_RawDelta.m_Saves, 1);
}

TEST(ImpactForensic, ConfirmedFallHasDurableEpisodeId)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	CImpactRun Run;
	Run.Initialize(902, TestContext());
	auto Event = TestForensicEvent(EImpactForensicType::FALL_CONFIRMED, 902, 1, 120);
	Event.m_ProblemEpisodeId = 88;
	Event.m_FallEpisodeId = 88;
	Event.m_Target = TestId(2, 5);
	Event.m_RawDelta.m_Falls = 1;
	Run.AppendForensicEvents({Event});
	const SRunSnapshot Snapshot = Run.Snapshot(120);
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {Snapshot}));
	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), 1U);
	ASSERT_EQ(Loaded[0].m_vForensicEvents.size(), 1U);
	EXPECT_EQ(Loaded[0].m_vForensicEvents[0].m_Type, EImpactForensicType::FALL_CONFIRMED);
	EXPECT_EQ(Loaded[0].m_vForensicEvents[0].m_FallEpisodeId, 88U);
	EXPECT_EQ(Loaded[0].m_vForensicEvents[0].m_RawDelta.m_Falls, 1);
	const SRunSnapshot &LoadedSnapshot = Loaded[0];
	EXPECT_EQ(LoadedSnapshot.m_vForensicEvents[0].m_Target.m_SlotGeneration, 5U);
}

TEST(ImpactForensic, ClutchTraceObservesProductionRosterWithoutChangingAward)
{
	CReplay WithTrace(913, TestContext());
	CReplay WithoutTrace(913, TestContext());
	WithoutTrace.Phase2().SetForensicTraceEnabled(false);
	WithoutTrace.Run().SetForensicTraceEnabled(false);
	StartPair(WithTrace);
	StartPair(WithoutTrace);
	auto SubmitParity = [&](const std::vector<SImpactGameplayInput> &vInputs) {
		const bool WithTraceChanged = SubmitLiveEquivalentInputs(WithTrace, vInputs);
		const bool WithoutTraceChanged = SubmitLiveEquivalentInputs(WithoutTrace, vInputs);
		EXPECT_EQ(WithTraceChanged, WithoutTraceChanged);
		ExpectCanonicalEventsEqual(WithTrace.LastOutput().m_vEvents, WithoutTrace.LastOutput().m_vEvents);
	};

	SImpactGameplayInput Saver = ImpactInput(1, 90, false, true, true, 1, false, EImpactRecoveryNeed::EXTERNAL_REQUIRED);
	SImpactGameplayInput Target = ImpactInput(2, 90, true, true, true, 1, false, EImpactRecoveryNeed::EXTERNAL_REQUIRED);
	Target.m_FreezeEnd = 200;
	SubmitParity({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 100;
	SetHooked(Saver, 2, true);
	SetPhysicalHarmfulOutcome(Target);
	SubmitParity({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	SubmitParity({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 145;
	SubmitParity({Saver, Target});

	ExpectRawStatsEqual(WithTrace.Run().RawStats(), WithoutTrace.Run().RawStats());
	ExpectCanonicalEventsEqual(WithTrace.Run().Events(), WithoutTrace.Run().Events());
	EXPECT_EQ(WithTrace.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(WithTrace.Run().RawStats().m_Clutches, 1);
	const auto &Forensic = WithTrace.Run().ForensicEvents();
	const auto Candidate = std::find_if(Forensic.begin(), Forensic.end(), [](const SImpactForensicEvent &Event) {
		return Event.m_Type == EImpactForensicType::CLUTCH_CANDIDATE_CREATED;
	});
	ASSERT_NE(Candidate, Forensic.end());
	EXPECT_NE(Candidate->m_ClutchEpisodeId, 0U);
	ASSERT_FALSE(Candidate->m_vFunctionalRoster.empty());
	const auto Confirmed = std::find_if(Forensic.begin(), Forensic.end(), [](const SImpactForensicEvent &Event) {
		return Event.m_Type == EImpactForensicType::CLUTCH_CONFIRMED;
	});
	ASSERT_NE(Confirmed, Forensic.end());
	EXPECT_NE(Confirmed->m_EventId, 0U);
	EXPECT_EQ(Confirmed->m_RawDelta.m_Clutches, 1);
}

TEST(ImpactForensic, AledTraceDoesNotChangeProductionCounters)
{
	CReplay WithTrace(914, TestContext());
	CReplay WithoutTrace(914, TestContext());
	WithoutTrace.Phase2().SetForensicTraceEnabled(false);
	WithoutTrace.Run().SetForensicTraceEnabled(false);
	StartPair(WithTrace);
	StartPair(WithoutTrace);
	auto SubmitParity = [&](const std::vector<SImpactGameplayInput> &vInputs, const std::vector<SImpactGameplayMessage> &vMessages = {}) {
		const bool WithTraceChanged = SubmitLiveEquivalentInputs(WithTrace, vInputs, vMessages);
		const bool WithoutTraceChanged = SubmitLiveEquivalentInputs(WithoutTrace, vInputs, vMessages);
		EXPECT_EQ(WithTraceChanged, WithoutTraceChanged);
		ExpectCanonicalEventsEqual(WithTrace.LastOutput().m_vEvents, WithoutTrace.LastOutput().m_vEvents);
	};
	const SImpactGameplayMessage Aled = ImpactSignal(1, EImpactGameplayMessageType::CONFIRMED_ALED, 200, TestId(1), TestId(2));
	SubmitParity({
		ImpactInput(1, 200, false, true, true, 1, false, EImpactRecoveryNeed::EXTERNAL_REQUIRED),
		ImpactInput(2, 200, false, true, true, 1, false, EImpactRecoveryNeed::EXTERNAL_REQUIRED)}, {Aled});
	SubmitParity({
		ImpactInput(1, 235, false, true, true, 1, false, EImpactRecoveryNeed::EXTERNAL_REQUIRED),
		ImpactInput(2, 235, false, true, true, 1, false, EImpactRecoveryNeed::EXTERNAL_REQUIRED)});
	ExpectRawStatsEqual(WithTrace.Run().RawStats(), WithoutTrace.Run().RawStats());
	ExpectCanonicalEventsEqual(WithTrace.Run().Events(), WithoutTrace.Run().Events());
	EXPECT_EQ(WithTrace.Run().RawStats().m_Aleds, 1);
	EXPECT_EQ(WithTrace.Run().RawStats().m_Saves, 1);
}

TEST(ImpactForensic, SaveCandidateLifecyclePersisted)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	SRunSnapshot Snapshot = PersistentSnapshot(903);
	auto Candidate = TestForensicEvent(EImpactForensicType::SAVE_CANDIDATE_CREATED, 903, 1, 100);
	Candidate.m_ProblemEpisodeId = 50;
	Candidate.m_FallEpisodeId = 50;
	Candidate.m_SaveCandidateId = 51;
	Candidate.m_Actor = TestId(1);
	Candidate.m_Target = TestId(2);
	auto Started = TestForensicEvent(EImpactForensicType::SAVE_CONFIRMATION_STARTED, 903, 2, 120);
	Started.m_SaveCandidateId = 51;
	Started.m_ConfirmationStartTick = 120;
	Started.m_ConfirmationDeadlineTick = 155;
	auto Confirmed = TestForensicEvent(EImpactForensicType::SAVE_CONFIRMED, 903, 3, 155);
	Confirmed.m_ProblemEpisodeId = 50;
	Confirmed.m_SaveCandidateId = 51;
	Confirmed.m_EventId = 99;
	Confirmed.m_Actor = TestId(1);
	Confirmed.m_Target = TestId(2);
	Confirmed.m_RawDelta.m_Saves = 1;
	Snapshot.m_vForensicEvents = {Candidate, Started, Confirmed};
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {Snapshot}));
	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), 1U);
	ASSERT_EQ(Loaded[0].m_vForensicEvents.size(), 3U);
	EXPECT_EQ(Loaded[0].m_vForensicEvents[0].m_SaveCandidateId, 51U);
	EXPECT_EQ(Loaded[0].m_vForensicEvents[1].m_Type, EImpactForensicType::SAVE_CONFIRMATION_STARTED);
	EXPECT_EQ(Loaded[0].m_vForensicEvents[2].m_EventId, 99U);
	EXPECT_EQ(Loaded[0].m_vForensicEvents[2].m_RawDelta.m_Saves, 1);
}

TEST(ImpactForensic, SaveRejectionReasonPersisted)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	SRunSnapshot Snapshot = PersistentSnapshot(904);
	Snapshot.m_vForensicEvents.clear();
	Snapshot.m_ForensicTraceTruncated = false;
	Snapshot.m_ForensicDroppedEvents = 0;
	auto Rejected = TestForensicEvent(EImpactForensicType::SAVE_CANDIDATE_REJECTED, 904, 1, 100);
	Rejected.m_SaveCandidateId = 65;
	Rejected.m_Reason = EImpactForensicReason::NON_CAUSAL_INTERACTION;
	Rejected.m_ReasonDetail = "existing actor-specific causality decision: non-causal interaction";
	Snapshot.m_vForensicEvents.push_back(Rejected);
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {Snapshot}));
	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), 1U);
	ASSERT_EQ(Loaded[0].m_vForensicEvents.size(), 1U);
	EXPECT_EQ(Loaded[0].m_vForensicEvents[0].m_Reason, EImpactForensicReason::NON_CAUSAL_INTERACTION);
	EXPECT_EQ(Loaded[0].m_vForensicEvents[0].m_ReasonDetail, Rejected.m_ReasonDetail);
}

TEST(ImpactForensic, RefreezeFailurePersisted)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	SRunSnapshot Snapshot = PersistentSnapshot(905);
	Snapshot.m_vForensicEvents.clear();
	Snapshot.m_ForensicTraceTruncated = false;
	Snapshot.m_ForensicDroppedEvents = 0;
	auto Failed = TestForensicEvent(EImpactForensicType::REFREEZE_BEFORE_CONFIRMATION, 905, 1, 130);
	Failed.m_SaveCandidateId = 66;
	Failed.m_Reason = EImpactForensicReason::REFROZEN_BEFORE_CONFIRMATION;
	Failed.m_ConfirmationStartTick = 120;
	Failed.m_ConfirmationDeadlineTick = 155;
	Snapshot.m_vForensicEvents.push_back(Failed);
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {Snapshot}));
	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded[0].m_vForensicEvents.size(), 1U);
	EXPECT_EQ(Loaded[0].m_vForensicEvents[0].m_Type, EImpactForensicType::REFREEZE_BEFORE_CONFIRMATION);
	EXPECT_EQ(Loaded[0].m_vForensicEvents[0].m_SaveCandidateId, 66U);
}

TEST(ImpactForensic, SelfSafeProofAndInvalidationPersisted)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	SRunSnapshot Snapshot = PersistentSnapshot(906);
	auto Proven = TestForensicEvent(EImpactForensicType::SELF_SAFE_PROVEN, 906, 1, 100);
	Proven.m_ProblemEpisodeId = 75;
	Proven.m_Target = TestId(2, 3);
	Proven.m_SelfSafeProven = true;
	Proven.m_SelfSafeEvidenceTick = 99;
	Proven.m_AutonomousEvidenceEvaluated = true;
	Proven.m_AutonomousEvidenceKnown = true;
	Proven.m_AutonomousRecoveryPossible = true;
	auto Invalidated = TestForensicEvent(EImpactForensicType::SELF_SAFE_INVALIDATED, 906, 2, 120);
	Invalidated.m_ProblemEpisodeId = 75;
	Invalidated.m_Target = Proven.m_Target;
	Invalidated.m_SelfSafeProven = false;
	Invalidated.m_SelfSafeEvidenceTick = 99;
	Invalidated.m_EpisodeStateBefore = EImpactEpisodeState::RECOVERED_SELF;
	Invalidated.m_EpisodeStateAfter = EImpactEpisodeState::NONE;
	Snapshot.m_vForensicEvents = {Proven, Invalidated};
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {Snapshot}));
	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded[0].m_vForensicEvents.size(), 2U);
	EXPECT_TRUE(Loaded[0].m_vForensicEvents[0].m_SelfSafeProven);
	EXPECT_FALSE(Loaded[0].m_vForensicEvents[1].m_SelfSafeProven);
	EXPECT_EQ(Loaded[0].m_vForensicEvents[1].m_EpisodeStateBefore, EImpactEpisodeState::RECOVERED_SELF);
	EXPECT_EQ(Loaded[0].m_vForensicEvents[1].m_EpisodeStateAfter, EImpactEpisodeState::NONE);
}

TEST(ImpactForensic, ProcessingPauseResumePersisted)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	CImpactRun Run;
	Run.Initialize(907, TestContext());
	STickEventBatch Batch;
	Batch.m_ServerTick = 1;
	Batch.m_vCandidates = {StartEvent(1, TestId(1), 1), StartEvent(2, TestId(2), 1)};
	ASSERT_TRUE(Run.ApplyTickEventBatch(Batch));
	Batch = {};
	Batch.m_ServerTick = 2;
	SImpactEvent Finalized;
	Finalized.m_EventId = 3;
	Finalized.m_Type = EImpactEventType::PARTICIPANT_FINALIZED;
	Finalized.m_Actor = TestId(2);
	Finalized.m_TeamId = Run.Context().m_TeamId;
	Finalized.m_StartTick = 2;
	Finalized.m_ResolveTick = 2;
	Finalized.m_Confirmed = true;
	Batch.m_vCandidates.push_back(Finalized);
	ASSERT_TRUE(Run.ApplyTickEventBatch(Batch));
	EXPECT_EQ(Run.State(), EAnalysisState::PAUSED_NO_TEAM);
	Batch = {};
	Batch.m_ServerTick = 3;
	ASSERT_TRUE(Run.AddRosterMember(TestId(3), "Late roster member", 3, "authoritative current team roster"));
	EXPECT_EQ(Run.State(), EAnalysisState::ACTIVE);
	const SRunSnapshot Snapshot = Run.Snapshot(3);
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {Snapshot}));
	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded[0].m_vForensicEvents.size(), Snapshot.m_vForensicEvents.size());
	EXPECT_EQ(std::count_if(Loaded[0].m_vForensicEvents.begin(), Loaded[0].m_vForensicEvents.end(), [](const SImpactForensicEvent &Event) {
		return Event.m_Type == EImpactForensicType::PROCESSING_PAUSED &&
			Event.m_RunStateBefore == EAnalysisState::ACTIVE && Event.m_RunStateAfter == EAnalysisState::PAUSED_NO_TEAM &&
			Event.m_ActiveParticipantCount == 1 && Event.m_PresentParticipantCount == 1 && Event.m_HistoricalParticipantCount == 2;
	}), 1);
	EXPECT_EQ(std::count_if(Loaded[0].m_vForensicEvents.begin(), Loaded[0].m_vForensicEvents.end(), [](const SImpactForensicEvent &Event) {
		return Event.m_Type == EImpactForensicType::PROCESSING_RESUMED &&
			Event.m_RunStateBefore == EAnalysisState::PAUSED_NO_TEAM && Event.m_RunStateAfter == EAnalysisState::ACTIVE &&
			Event.m_ActiveParticipantCount == 2 && Event.m_HistoricalParticipantCount == 3;
	}), 1);
}

TEST(ImpactForensic, ClutchFunctionalRosterPersisted)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	SRunSnapshot Snapshot = PersistentSnapshot(908);
	Snapshot.m_vForensicEvents.clear();
	Snapshot.m_ForensicTraceTruncated = false;
	Snapshot.m_ForensicDroppedEvents = 0;
	auto Evaluation = TestForensicEvent(EImpactForensicType::CLUTCH_ROSTER_EVALUATED, 908, 1, 240);
	Evaluation.m_ClutchEpisodeId = 33;
	Evaluation.m_DecisionEligible = true;
	Evaluation.m_DecisionAccepted = false;
	SImpactFunctionalRosterEntry Functional;
	Functional.m_Participant = TestId(1, 4);
	Functional.m_Classification = EImpactFunctionalClass::FUNCTIONAL;
	Functional.m_Reason = "UNFROZEN_FUNCTIONAL";
	Functional.m_AliveKnown = true;
	Functional.m_Alive = true;
	Functional.m_RosterMember = true;
	Functional.m_Engaged = true;
	SImpactFunctionalRosterEntry NonEngagedRosterMember;
	NonEngagedRosterMember.m_Participant = TestId(2, 7);
	NonEngagedRosterMember.m_Classification = EImpactFunctionalClass::IGNORED;
	NonEngagedRosterMember.m_Reason = "NOT_ENGAGED";
	NonEngagedRosterMember.m_AliveKnown = true;
	NonEngagedRosterMember.m_Alive = true;
	NonEngagedRosterMember.m_Frozen = true;
	NonEngagedRosterMember.m_RosterMember = true;
	NonEngagedRosterMember.m_Engaged = false;
	Evaluation.m_vFunctionalRoster = {Functional, NonEngagedRosterMember};
	Snapshot.m_vForensicEvents.push_back(Evaluation);
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {Snapshot}));
	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded[0].m_vForensicEvents.size(), 1U);
	const SImpactForensicEvent &Restored = Loaded[0].m_vForensicEvents[0];
	EXPECT_EQ(Restored.m_ClutchEpisodeId, 33U);
	EXPECT_TRUE(Restored.m_DecisionEligible);
	EXPECT_FALSE(Restored.m_DecisionAccepted);
	ASSERT_EQ(Restored.m_vFunctionalRoster.size(), 2U);
	EXPECT_EQ(Restored.m_vFunctionalRoster[0].m_Classification, EImpactFunctionalClass::FUNCTIONAL);
	EXPECT_EQ(Restored.m_vFunctionalRoster[0].m_Reason, "UNFROZEN_FUNCTIONAL");
	EXPECT_EQ(Restored.m_vFunctionalRoster[1].m_Participant.m_SlotGeneration, 7U);
	EXPECT_TRUE(Restored.m_vFunctionalRoster[0].m_RosterMember);
	EXPECT_TRUE(Restored.m_vFunctionalRoster[0].m_Engaged);
	EXPECT_TRUE(Restored.m_vFunctionalRoster[1].m_RosterMember);
	EXPECT_FALSE(Restored.m_vFunctionalRoster[1].m_Engaged);
	EXPECT_EQ(Restored.m_vFunctionalRoster[1].m_Classification, EImpactFunctionalClass::IGNORED);
	EXPECT_EQ(Restored.m_vFunctionalRoster[1].m_Reason, "NOT_ENGAGED");
}

TEST(ImpactForensic, RosterAndEngagementTransitionsPersisted)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	CImpactRun Run;
	Run.Initialize(2018, TestContext(4));
	ASSERT_TRUE(Run.AddRosterMember(TestId(1, 4), "A", 10, "first START roster snapshot"));
	ASSERT_TRUE(Run.AddRosterMember(TestId(2, 7), "B", 10, "first START roster snapshot"));
	ASSERT_TRUE(Run.AddRosterMember(TestId(3, 8), "AFK", 10, "first START roster snapshot"));
	STickEventBatch StartBatch;
	StartBatch.m_ServerTick = 12;
	StartBatch.m_vCandidates = {StartEvent(1, TestId(1, 4), 12, "A", 4), StartEvent(2, TestId(2, 7), 12, "B", 4)};
	ASSERT_TRUE(Run.ApplyTickEventBatch(StartBatch));
	ASSERT_TRUE(Run.Finalize(ERunResult::TEAM_RESET, 50));

	SRunSnapshot Snapshot = Run.Snapshot(50);
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {Snapshot}));
	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), 1U);
	const auto FindTransition = [&Loaded](EImpactForensicType Type, int ClientId, uint32_t Generation) {
		return std::find_if(Loaded[0].m_vForensicEvents.begin(), Loaded[0].m_vForensicEvents.end(),
			[Type, ClientId, Generation](const SImpactForensicEvent &Event) {
				const SParticipantId &Identity = Event.m_Type == EImpactForensicType::ROSTER_MEMBER_ADDED ||
					Event.m_Type == EImpactForensicType::ENGAGED_PROVEN ? Event.m_Target : Event.m_Actor;
				return Event.m_Type == Type && Identity.m_ClientId == ClientId && Identity.m_SlotGeneration == Generation;
			});
	};
	const auto Roster = FindTransition(EImpactForensicType::ROSTER_MEMBER_ADDED, 2, 7);
	ASSERT_NE(Roster, Loaded[0].m_vForensicEvents.end());
	EXPECT_EQ(Roster->m_RunId, 2018U);
	EXPECT_EQ(Roster->m_TeamId, 4);
	EXPECT_EQ(Roster->m_Tick, 10);
	EXPECT_EQ(Roster->m_ReasonDetail, "first START roster snapshot");
	const auto Engaged = FindTransition(EImpactForensicType::ENGAGED_PROVEN, 1, 4);
	ASSERT_NE(Engaged, Loaded[0].m_vForensicEvents.end());
	EXPECT_EQ(Engaged->m_RunId, 2018U);
	EXPECT_EQ(Engaged->m_Tick, 12);
	EXPECT_EQ(Engaged->m_ReasonDetail, "canonical observed START crossing");
	const auto Finalized = FindTransition(EImpactForensicType::ENGAGED_FINALIZED, 3, 8);
	ASSERT_NE(Finalized, Loaded[0].m_vForensicEvents.end());
	EXPECT_EQ(Finalized->m_RunId, 2018U);
	EXPECT_EQ(Finalized->m_Tick, 50);
	EXPECT_EQ(Finalized->m_ReasonDetail, "unengaged roster participant finalized at team boundary");
}

TEST(ImpactForensic, NewClutchEpisodeHasDistinctId)
{
	CImpactRun Run;
	Run.Initialize(909, TestContext());
	auto First = TestForensicEvent(EImpactForensicType::CLUTCH_EPISODE_START, 909, 1, 10);
	First.m_ClutchEpisodeId = 1;
	auto Second = TestForensicEvent(EImpactForensicType::CLUTCH_EPISODE_START, 909, 2, 20);
	Second.m_ClutchEpisodeId = 2;
	Run.AppendForensicEvents({First, Second});
	ASSERT_EQ(Run.ForensicEvents().size(), 2U);
	EXPECT_NE(Run.ForensicEvents()[0].m_ClutchEpisodeId, Run.ForensicEvents()[1].m_ClutchEpisodeId);
}

TEST(ImpactForensic, OldHistoryWithoutForensicStillLoads)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {PersistentSnapshot(910)}));
	void *pData = nullptr;
	unsigned Length = 0;
	ASSERT_TRUE(Storage->ReadFile("amf_impact/history.json", IStorage::TYPE_SAVE, &pData, &Length));
	ASSERT_NE(pData, nullptr);
	std::string Json(static_cast<const char *>(pData), Length);
	free(pData);
	ASSERT_TRUE(RemoveJsonObjectProperty(&Json, "forensic"));
	ASSERT_TRUE(RemoveJsonObjectProperty(&Json, "end_tick"));
	ASSERT_TRUE(WriteTestStorageText(Storage.get(), "amf_impact/history.json", Json));
	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), 1U);
	EXPECT_EQ(Loaded[0].m_ForensicVersion, 0);
	EXPECT_FALSE(Loaded[0].m_ForensicTraceEnabled);
	EXPECT_FALSE(Loaded[0].m_ForensicTraceComplete);
	EXPECT_TRUE(Loaded[0].m_vForensicEvents.empty());
	EXPECT_EQ(Loaded[0].m_EndTick, -1);
	CImpactRun Restored;
	ASSERT_TRUE(Restored.RestoreSnapshot(Loaded[0]));
	EXPECT_TRUE(Restored.Snapshot(200).m_ForensicTraceEnabled);
	EXPECT_FALSE(Restored.Snapshot(200).m_ForensicTraceComplete);
}

TEST(ImpactForensic, LegacyHistoryWithSerializedVersionZeroLoadsAndAppears)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	SRunSnapshot Legacy = PersistentSnapshot(915);
	Legacy.m_ForensicVersion = 0;
	Legacy.m_ForensicTraceEnabled = false;
	Legacy.m_ForensicTraceComplete = false;
	Legacy.m_ForensicTraceTruncated = false;
	Legacy.m_ForensicDroppedEvents = 0;
	Legacy.m_vForensicEvents.clear();
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {Legacy}));

	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), 1U);
	EXPECT_EQ(Loaded[0].m_RunId, 915U);
	EXPECT_EQ(Loaded[0].m_Context.m_Map, Legacy.m_Context.m_Map);
	EXPECT_EQ(Loaded[0].m_ForensicVersion, 0);
	EXPECT_FALSE(Loaded[0].m_ForensicTraceEnabled);
	EXPECT_TRUE(Loaded[0].m_vForensicEvents.empty());
}

TEST(ImpactForensic, OneMalformedRunDoesNotSilentlyEraseExistingHistory)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	SRunSnapshot Legacy = PersistentSnapshot(916);
	Legacy.m_ForensicVersion = 0;
	Legacy.m_ForensicTraceEnabled = false;
	Legacy.m_ForensicTraceComplete = false;
	Legacy.m_vForensicEvents.clear();
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {Legacy}));
	void *pData = nullptr;
	unsigned Length = 0;
	ASSERT_TRUE(Storage->ReadFile("amf_impact/history.json", IStorage::TYPE_SAVE, &pData, &Length));
	ASSERT_NE(pData, nullptr);
	std::string OriginalJson(static_cast<const char *>(pData), Length);
	std::free(pData);
	const size_t RunsEnd = OriginalJson.rfind(']');
	ASSERT_NE(RunsEnd, std::string::npos);
	OriginalJson.insert(RunsEnd, ",{}");
	ASSERT_TRUE(WriteTestStorageText(Storage.get(), "amf_impact/history.json", OriginalJson));
	Storage->RemoveFile("amf_impact/history.json.bak", IStorage::TYPE_SAVE);

	std::vector<SRunSnapshot> Loaded{PersistentSnapshot(999)};
	EXPECT_FALSE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), 1U);
	EXPECT_EQ(Loaded[0].m_RunId, 999U);
	EXPECT_FALSE(CPersistence::SaveHistory(Storage.get(), {}, false));

	pData = nullptr;
	Length = 0;
	ASSERT_TRUE(Storage->ReadFile("amf_impact/history.json", IStorage::TYPE_SAVE, &pData, &Length));
	ASSERT_NE(pData, nullptr);
	const std::string AfterFailedSave(static_cast<const char *>(pData), Length);
	std::free(pData);
	EXPECT_EQ(AfterFailedSave, OriginalJson);
}

TEST(ImpactForensic, MissingHistoryLoadsAsLegitimateEmptyHistory)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	std::vector<SRunSnapshot> Loaded{PersistentSnapshot(917)};
	EXPECT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	EXPECT_TRUE(Loaded.empty());
}

TEST(ImpactForensic, NewHistoryRoundTripsForensicData)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	SRunSnapshot Snapshot = PersistentSnapshot(911);
	Snapshot.m_vForensicEvents.clear();
	Snapshot.m_ForensicTraceTruncated = false;
	Snapshot.m_ForensicDroppedEvents = 0;
	Snapshot.m_EndTick = 777;
	auto Event = TestForensicEvent(EImpactForensicType::SAVE_CONFIRMED, 911, 1, 700);
	Event.m_EventId = 99;
	Event.m_ProblemEpisodeId = 41;
	Event.m_FallEpisodeId = 41;
	Event.m_SaveCandidateId = 42;
	Event.m_ClutchEpisodeId = 43;
	Event.m_ActorInteractionId = 44;
	Event.m_Actor = TestId(1, 2);
	Event.m_Target = TestId(2, 3);
	Event.m_Reason = EImpactForensicReason::NONE;
	Event.m_ReasonDetail = "save confirmed by current production path";
	Event.m_RunStateBefore = EAnalysisState::ACTIVE;
	Event.m_RunStateAfter = EAnalysisState::ACTIVE;
	Event.m_EpisodeStateBefore = EImpactEpisodeState::RECOVERED_BY_TEAM;
	Event.m_EpisodeStateAfter = EImpactEpisodeState::CONFIRMED_RECOVERY;
	Event.m_PhysicalOutcome = EImpactPhysicalOutcome::SAFE;
	Event.m_TargetSnapshotAvailable = true;
	Event.m_ActorSnapshotAvailable = true;
	Event.m_TargetPredictedStateAvailable = true;
	Event.m_ActorTargetInteractionObservable = true;
	Event.m_PhysicalChangeEvidence = true;
	Event.m_DecisivePhysicalEvidence = true;
	Event.m_DecisionEligible = true;
	Event.m_DecisionAccepted = true;
	Event.m_SelfSafeProven = false;
	Event.m_RawDelta.m_Saves = 1;
	Event.m_vFunctionalRoster.push_back({TestId(1, 2), EImpactFunctionalClass::FUNCTIONAL,
		"UNFROZEN_FUNCTIONAL", true, true, false, true, false});
	Snapshot.m_vForensicEvents.push_back(Event);
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {Snapshot}));
	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), 1U);
	EXPECT_EQ(Loaded[0].m_EndTick, 777);
	EXPECT_EQ(Loaded[0].m_ForensicVersion, IMPACT_FORENSIC_VERSION);
	EXPECT_TRUE(Loaded[0].m_ForensicTraceEnabled);
	EXPECT_TRUE(Loaded[0].m_ForensicTraceComplete);
	ASSERT_EQ(Loaded[0].m_vForensicEvents.size(), 1U);
	const SImpactForensicEvent &Restored = Loaded[0].m_vForensicEvents[0];
	EXPECT_EQ(Restored.m_Sequence, 1U);
	EXPECT_EQ(Restored.m_RunId, 911U);
	EXPECT_EQ(Restored.m_EventId, 99U);
	EXPECT_EQ(Restored.m_ProblemEpisodeId, 41U);
	EXPECT_EQ(Restored.m_FallEpisodeId, 41U);
	EXPECT_EQ(Restored.m_SaveCandidateId, 42U);
	EXPECT_EQ(Restored.m_ClutchEpisodeId, 43U);
	EXPECT_EQ(Restored.m_ActorInteractionId, 44U);
	EXPECT_EQ(Restored.m_Actor.m_SlotGeneration, 2U);
	EXPECT_TRUE(Restored.m_DecisionAccepted);
	EXPECT_TRUE(Restored.m_DecisivePhysicalEvidence);
	ASSERT_EQ(Restored.m_vFunctionalRoster.size(), 1U);
	EXPECT_EQ(Restored.m_vFunctionalRoster[0].m_Reason, "UNFROZEN_FUNCTIONAL");
}

TEST(ImpactForensic, HarmfulPhysicalOutcomeLoadsFromForensicHistory)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	SRunSnapshot Snapshot = PersistentSnapshot(918);
	Snapshot.m_vForensicEvents.clear();
	auto Event = TestForensicEvent(EImpactForensicType::FALL_CANDIDATE_CREATED, 918, 1, 100);
	Event.m_PhysicalOutcome = EImpactPhysicalOutcome::HARMFUL;
	Snapshot.m_vForensicEvents.push_back(Event);
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {Snapshot}));

	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), 1U);
	ASSERT_EQ(Loaded[0].m_vForensicEvents.size(), 1U);
	EXPECT_EQ(Loaded[0].m_vForensicEvents[0].m_PhysicalOutcome, EImpactPhysicalOutcome::HARMFUL);
}

TEST(ImpactForensic, TraceTruncationIsExplicit)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	CImpactRun Run;
	Run.Initialize(912, TestContext());
	std::vector<SImpactForensicEvent> Events(MAX_FORENSIC_EVENTS + 2);
	for(size_t i = 0; i < Events.size(); ++i)
	{
		Events[i].m_Type = EImpactForensicType::INTERACTION_OBSERVED;
		Events[i].m_Tick = static_cast<int64_t>(i);
		Events[i].m_Actor = TestId(1, 2);
		Events[i].m_Target = TestId(2, 3);
		Events[i].m_ReasonDetail = "target-specific interaction observed; physical change evidence pending";
		Events[i].m_ActiveParticipantCount = 8;
		Events[i].m_HistoricalParticipantCount = 8;
	}
	Run.AppendForensicEvents(Events);
	const SRunSnapshot Snapshot = Run.Snapshot(MAX_FORENSIC_EVENTS + 2);
	EXPECT_EQ(Snapshot.m_vForensicEvents.size(), MAX_FORENSIC_EVENTS);
	EXPECT_TRUE(Snapshot.m_ForensicTraceEnabled);
	EXPECT_TRUE(Snapshot.m_ForensicTraceComplete);
	EXPECT_TRUE(Snapshot.m_ForensicTraceTruncated);
	EXPECT_EQ(Snapshot.m_ForensicDroppedEvents, 2U);
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {Snapshot}));
	void *pData = nullptr;
	unsigned Length = 0;
	ASSERT_TRUE(Storage->ReadFile("amf_impact/history.json", IStorage::TYPE_SAVE, &pData, &Length));
	ASSERT_NE(pData, nullptr);
	EXPECT_LT(Length, 8U * 1024U * 1024U);
	std::free(pData);
	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), 1U);
	EXPECT_TRUE(Loaded[0].m_ForensicTraceTruncated);
	EXPECT_EQ(Loaded[0].m_ForensicDroppedEvents, 2U);
	EXPECT_EQ(Loaded[0].m_vForensicEvents.size(), MAX_FORENSIC_EVENTS);
}

TEST(ImpactFoundation, HistorySerializeLoadPreservesValues)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	std::vector<SRunSnapshot> History{PersistentSnapshot(42)};
	History[0].m_HasGlobalRunNumber = true;
	History[0].m_GlobalRunNumber = 9876543210123ULL;
	History[0].m_Score.m_Stability = 87.12345678901234;
	History[0].m_Score.m_ScoringVersion = 3;
	ASSERT_FALSE(History[0].m_vParticipants.empty());
	History[0].m_vParticipants[0].m_Finalized = true;
	History[0].m_vParticipants[0].m_TerminalStatus = EParticipantTerminalStatus::LEFT;
	History[0].m_vParticipants[0].m_ParticipationTicks = 515;
	History[0].m_vParticipants[0].m_ParticipationTicksKnown = true;
	History[0].m_vParticipants[0].m_ParticipationStartTick = -1;
	History[0].m_vParticipants[0].m_ParticipationTerminalTick = 555;
	History[0].m_vParticipants[0].m_Score.m_Available = true;
	History[0].m_vParticipants[0].m_Score.m_Rating = 2.579321;
	History[0].m_vParticipants[0].m_Score.m_Impact = -3.678912;
	History[0].m_vParticipants[0].m_Score.m_Stability = 91.987654321;
	History[0].m_vParticipants[0].m_Score.m_ScoringVersion = 4;
	History[0].m_vParticipants[0].m_RescuesReceived = 6;
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), History));
	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), 1U);
	EXPECT_EQ(Loaded[0].m_RunId, 42U);
	EXPECT_EQ(Loaded[0].m_RunUid, History[0].m_RunUid);
	EXPECT_TRUE(Loaded[0].m_HasGlobalRunNumber);
	EXPECT_EQ(Loaded[0].m_GlobalRunNumber, 9876543210123ULL);
	EXPECT_EQ(Loaded[0].m_Context.m_TeamId, 1);
	EXPECT_DOUBLE_EQ(Loaded[0].m_Score.m_Rating, 1.84);
	EXPECT_DOUBLE_EQ(Loaded[0].m_Score.m_Stability, 87.12345678901234);
	EXPECT_EQ(Loaded[0].m_Score.m_ScoringVersion, 3);
	ASSERT_FALSE(Loaded[0].m_vParticipants.empty());
	EXPECT_TRUE(Loaded[0].m_vParticipants[0].m_Finalized);
	EXPECT_EQ(Loaded[0].m_vParticipants[0].m_TerminalStatus, EParticipantTerminalStatus::LEFT);
	EXPECT_TRUE(Loaded[0].m_vParticipants[0].m_ParticipationTicksKnown);
	EXPECT_EQ(Loaded[0].m_vParticipants[0].m_ParticipationTicks, 515);
	EXPECT_EQ(Loaded[0].m_vParticipants[0].m_ParticipationTerminalTick, 555);
	EXPECT_TRUE(Loaded[0].m_vParticipants[0].m_Score.m_Available);
	EXPECT_DOUBLE_EQ(Loaded[0].m_vParticipants[0].m_Score.m_Rating, 2.579321);
	EXPECT_DOUBLE_EQ(Loaded[0].m_vParticipants[0].m_Score.m_Impact, -3.678912);
	EXPECT_DOUBLE_EQ(Loaded[0].m_vParticipants[0].m_Score.m_Stability, 91.987654321);
	EXPECT_EQ(Loaded[0].m_vParticipants[0].m_Score.m_ScoringVersion, 4);
	EXPECT_TRUE(Loaded[0].m_vParticipants[0].m_RescuesReceivedKnown);
	EXPECT_EQ(Loaded[0].m_vParticipants[0].m_RescuesReceived, 6);
	EXPECT_EQ(Loaded[0].m_vEvents.size(), History[0].m_vEvents.size());
	ASSERT_FALSE(Loaded[0].m_vEvents.empty());
	EXPECT_EQ(Loaded[0].m_vEvents[0].m_TeamId, 1);
}

TEST(ImpactFoundation, Phase4MetadataWithoutGlobalNumberRoundTripsAsNull)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	const SRunSnapshot Snapshot = PersistentSnapshot(421);
	ASSERT_FALSE(Snapshot.m_RunUid.empty());
	ASSERT_FALSE(Snapshot.m_HasGlobalRunNumber);
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {Snapshot}));
	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), 1U);
	EXPECT_EQ(Loaded[0].m_RunUid, Snapshot.m_RunUid);
	EXPECT_FALSE(Loaded[0].m_HasGlobalRunNumber);
	EXPECT_EQ(Loaded[0].m_Score.m_ScoringVersion, IMPACT_SCORING_VERSION_UNSET);
}

TEST(ImpactFoundation, LegacyHistoryWithoutPhase4FieldsLoadsUnknownNotZero)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	SRunSnapshot Legacy = PersistentSnapshot(422);
	ASSERT_FALSE(Legacy.m_vParticipants.empty());
	Legacy.m_vParticipants[0].m_Finalized = true;
	// A legacy counter without the matching canonical Save event is not enough
	// to prove which participant received the rescue.
	Legacy.m_RawStats.m_Saves = 1;
	Legacy.m_vParticipants[0].m_RawStats.m_Saves = 1;
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {Legacy}));
	void *pData = nullptr;
	unsigned Length = 0;
	ASSERT_TRUE(Storage->ReadFile("amf_impact/history.json", IStorage::TYPE_SAVE, &pData, &Length));
	ASSERT_NE(pData, nullptr);
	std::string Json(static_cast<const char *>(pData), Length);
	std::free(pData);
	for(const char *pProperty : {"run_uid", "global_run_number", "stability", "scoring_version", "terminal_status", "phase4_score", "rescues_received", "rescues_received_known", "participation_ticks", "participation_ticks_known", "participation_start_tick", "participation_terminal_tick", "scoring_evidence_version", "scoring_evidence_complete", "scoring_evidence_dropped", "scoring_evidence"})
		while(RemoveJsonObjectProperty(&Json, pProperty))
	ASSERT_TRUE(WriteTestStorageText(Storage.get(), "amf_impact/history.json", Json));

	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), 1U);
	EXPECT_TRUE(Loaded[0].m_RunUid.empty());
	EXPECT_FALSE(Loaded[0].m_HasGlobalRunNumber);
	EXPECT_DOUBLE_EQ(Loaded[0].m_Score.m_Stability, 0.0);
	EXPECT_EQ(Loaded[0].m_Score.m_ScoringVersion, IMPACT_SCORING_VERSION_UNSET);
	ASSERT_FALSE(Loaded[0].m_vParticipants.empty());
	EXPECT_EQ(Loaded[0].m_vParticipants[0].m_TerminalStatus, EParticipantTerminalStatus::UNKNOWN);
	EXPECT_EQ(Loaded[0].m_ScoringEvidenceVersion, 0);
	EXPECT_FALSE(Loaded[0].m_ScoringEvidenceComplete);
	for(const SParticipant &Participant : Loaded[0].m_vParticipants)
	{
		EXPECT_FALSE(Participant.m_Score.m_Available);
		EXPECT_EQ(Participant.m_Score.m_ScoringVersion, IMPACT_SCORING_VERSION_UNSET);
		EXPECT_FALSE(Participant.m_RescuesReceivedKnown);
		EXPECT_EQ(Participant.m_RescuesReceived, 0);
		EXPECT_FALSE(Participant.m_ParticipationTicksKnown);
		EXPECT_EQ(Participant.m_ParticipationTicks, 0);
		EXPECT_EQ(Participant.m_ParticipationStartTick, -1);
		EXPECT_EQ(Participant.m_ParticipationTerminalTick, -1);
	}
}

TEST(ImpactFoundation, OldHistoryWithoutParticipationTimeLoadsAsUnknown)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	SRunSnapshot Legacy = PersistentSnapshot(424);
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {Legacy}));
	void *pData = nullptr;
	unsigned Length = 0;
	ASSERT_TRUE(Storage->ReadFile("amf_impact/history.json", IStorage::TYPE_SAVE, &pData, &Length));
	ASSERT_NE(pData, nullptr);
	std::string Json(static_cast<const char *>(pData), Length);
	std::free(pData);
	for(const char *pProperty : {"participation_ticks", "participation_ticks_known", "participation_start_tick", "participation_terminal_tick"})
		while(RemoveJsonObjectProperty(&Json, pProperty))
	ASSERT_TRUE(WriteTestStorageText(Storage.get(), "amf_impact/history.json", Json));
	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), 1U);
	for(const SParticipant &Participant : Loaded[0].m_vParticipants)
	{
		EXPECT_FALSE(Participant.m_ParticipationTicksKnown);
		EXPECT_EQ(Participant.m_ParticipationTicks, 0);
		EXPECT_EQ(Participant.m_ParticipationStartTick, -1);
		EXPECT_EQ(Participant.m_ParticipationTerminalTick, -1);
	}
}

TEST(ImpactFoundation, LegacyRescuesReceivedDerivesOnlyFromCompleteSaveLedger)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	CReplay Replay(423, TestContext());
	StartPair(Replay);
	SRawStats SaveDelta;
	SaveDelta.m_Saves = 1;
	ASSERT_TRUE(SubmitParticipantStats(Replay, 3, EImpactEventType::SAVE_CONFIRMED, TestId(1), 20, SaveDelta, TestId(2)));
	SRunSnapshot Legacy = Replay.Run().Snapshot(200, 1700000000);
	ASSERT_EQ(Legacy.m_RawStats.m_Saves, 1);
	ASSERT_EQ(Legacy.m_vEvents.size(), 3U);
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {Legacy}));
	void *pData = nullptr;
	unsigned Length = 0;
	ASSERT_TRUE(Storage->ReadFile("amf_impact/history.json", IStorage::TYPE_SAVE, &pData, &Length));
	ASSERT_NE(pData, nullptr);
	std::string Json(static_cast<const char *>(pData), Length);
	std::free(pData);
	for(const char *pProperty : {"rescues_received", "rescues_received_known"})
		while(RemoveJsonObjectProperty(&Json, pProperty))
	ASSERT_TRUE(WriteTestStorageText(Storage.get(), "amf_impact/history.json", Json));

	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), 1U);
	const SParticipant *pSaver = nullptr;
	const SParticipant *pTarget = nullptr;
	for(const SParticipant &Participant : Loaded[0].m_vParticipants)
	{
		if(Participant.m_Id == TestId(1))
			pSaver = &Participant;
		if(Participant.m_Id == TestId(2))
			pTarget = &Participant;
	}
	ASSERT_NE(pSaver, nullptr);
	ASSERT_NE(pTarget, nullptr);
	EXPECT_TRUE(pSaver->m_RescuesReceivedKnown);
	EXPECT_EQ(pSaver->m_RescuesReceived, 0);
	EXPECT_TRUE(pTarget->m_RescuesReceivedKnown);
	EXPECT_EQ(pTarget->m_RescuesReceived, 1);
}

TEST(ImpactFoundation, MapDifficultyMetadataRoundTripsAndFormats)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	SRunSnapshot SnapshotData = PersistentSnapshot(44);
	SnapshotData.m_Context.m_MapDifficulty = "Insane";
	SnapshotData.m_Context.m_MapStars = 3;
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {SnapshotData}));
	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), 1U);
	EXPECT_EQ(Loaded[0].m_Context.m_MapDifficulty, "Insane");
	EXPECT_EQ(Loaded[0].m_Context.m_MapStars, 3);
	EXPECT_EQ(MapDifficultyDisplayName(Loaded[0].m_Context), "Insane · ★★★");
}

TEST(ImpactFoundation, ActiveRunAcceptsLateMapDifficultyMetadata)
{
	SRunContext Context;
	Context.m_Map = "SakuraWorld";
	Context.m_TeamId = 1;
	CImpactRun Run;
	Run.Initialize(45, Context);
	EXPECT_TRUE(Run.Context().m_MapDifficulty.empty());
	EXPECT_EQ(Run.Context().m_MapStars, -1);

	SMapDifficultyMetadata CategoryOnly;
	CategoryOnly.m_Category = "Insane";
	EXPECT_TRUE(Run.UpdateMapDifficultyMetadata(CategoryOnly));
	EXPECT_EQ(Run.Context().m_MapDifficulty, "Insane");
	EXPECT_EQ(Run.Context().m_MapStars, -1);

	SMapDifficultyMetadata Metadata;
	Metadata.m_Category = "Insane";
	Metadata.m_Stars = 3;
	EXPECT_TRUE(Run.UpdateMapDifficultyMetadata(Metadata));
	EXPECT_EQ(Run.Context().m_MapDifficulty, "Insane");
	EXPECT_EQ(Run.Context().m_MapStars, 3);
	EXPECT_EQ(MapDifficultyDisplayName(Run.Context()), "Insane · ★★★");

	EXPECT_FALSE(Run.UpdateMapDifficultyMetadata(Metadata));
}

TEST(ImpactFoundation, MapChangeDetachesPreviousRuntimeMetadata)
{
	SRunContext Previous;
	Previous.m_ServerKey = "kog-server|KOG";
	Previous.m_Map = "OldMap";
	Previous.m_MapDifficulty = "Main";
	Previous.m_MapStars = 3;

	SRunContext Current = Previous;
	Current.m_Map = "NewMap";
	Current.m_MapDifficulty.clear();
	Current.m_MapStars = -1;
	CImpactSystem::RebindMapMetadataForIdentity(Previous, &Current);

	EXPECT_EQ(Current.m_Map, "NewMap");
	EXPECT_TRUE(Current.m_MapDifficulty.empty());
	EXPECT_EQ(Current.m_MapStars, -1);
}

TEST(ImpactFoundation, MapChangeUsesOnlyNewMapMetadata)
{
	SRunContext Previous;
	Previous.m_ServerKey = "kog-server|KOG";
	Previous.m_Map = "OldMap";
	Previous.m_MapDifficulty = "Main";
	Previous.m_MapStars = 3;

	SRunContext Current = Previous;
	Current.m_Map = "NewMap";
	Current.m_MapDifficulty = "Hard";
	Current.m_MapStars = 2;
	CImpactSystem::RebindMapMetadataForIdentity(Previous, &Current);

	EXPECT_EQ(Current.m_MapDifficulty, "Hard");
	EXPECT_EQ(Current.m_MapStars, 2);
	EXPECT_EQ(MapDifficultyDisplayName(Current), "Hard · ★★");
}

TEST(ImpactFoundation, MapChangeKeepsCategoryAndRatingIndependent)
{
	SRunContext Previous;
	Previous.m_ServerKey = "kog-server|KOG";
	Previous.m_Map = "OldMap";
	Previous.m_MapDifficulty = "Main";
	Previous.m_MapStars = 3;

	SRunContext CategoryOnly = Previous;
	CategoryOnly.m_Map = "NewMap";
	CategoryOnly.m_MapDifficulty = "Insane";
	CategoryOnly.m_MapStars = -1;
	CImpactSystem::RebindMapMetadataForIdentity(Previous, &CategoryOnly);
	EXPECT_EQ(MapDifficultyDisplayName(CategoryOnly), "Insane");

	SRunContext RatingOnly = Previous;
	RatingOnly.m_Map = "AnotherMap";
	RatingOnly.m_MapDifficulty.clear();
	RatingOnly.m_MapStars = 2;
	CImpactSystem::RebindMapMetadataForIdentity(Previous, &RatingOnly);
	EXPECT_TRUE(RatingOnly.m_MapDifficulty.empty());
	EXPECT_EQ(RatingOnly.m_MapStars, 2);
}

TEST(ImpactFoundation, SameMapRefreshPreservesKnownMetadata)
{
	SRunContext Previous;
	Previous.m_ServerKey = "kog-server|KOG";
	Previous.m_Map = "SameMap";
	Previous.m_MapDifficulty = "Main";
	Previous.m_MapStars = 3;

	SRunContext Refreshed = Previous;
	Refreshed.m_MapDifficulty.clear();
	Refreshed.m_MapStars = -1;
	CImpactSystem::RebindMapMetadataForIdentity(Previous, &Refreshed);

	EXPECT_EQ(Refreshed.m_MapDifficulty, "Main");
	EXPECT_EQ(Refreshed.m_MapStars, 3);
	EXPECT_EQ(MapDifficultyDisplayName(Refreshed), "Main · ★★★");
}

TEST(ImpactFoundation, MapDifficultyMetadataIsStrictAndBackwardCompatible)
{
	SMapDifficultyMetadata Metadata;
	EXPECT_TRUE(ParseMapInfoDifficulty(
		"\"IceBox\" by Mapper on Insane, ★★★✰✰, 3 points, 42 finishes by 7 tees",
		"IceBox", &Metadata));
	EXPECT_EQ(Metadata.m_Category, "Insane");
	EXPECT_EQ(Metadata.m_Stars, 3);
	EXPECT_TRUE(ParseMapInfoDifficulty(
		"\"IceBox\" by Mapper on Insane, ★★★✰✰, 3 points, released 2 days and 3 hours ago, 42 finishes by 7 tees in 01:02 median, your time: 00:10.50",
		"IceBox", &Metadata));
	EXPECT_EQ(Metadata.m_Category, "Insane");
	EXPECT_EQ(Metadata.m_Stars, 3);

	EXPECT_FALSE(ParseMapInfoDifficulty(
		"\"IceBox\" by Mapper on Insane, unrelated ★★★, 3 points, 42 finishes by 7 tees",
		"IceBox", &Metadata));
	EXPECT_EQ(Metadata.m_Category, "");
	EXPECT_EQ(Metadata.m_Stars, -1);
	EXPECT_FALSE(ParseMapInfoDifficulty(
		"\"IceBox\" by Mapper on Insane, ★★★✰✰, custom text",
		"IceBox", &Metadata));
	EXPECT_FALSE(ParseMapInfoDifficulty(
		"\"IceBox\" by Mapper on Insane, ★★★✰✰, 2 points, 1 finishes by 1 tees",
		"IceBox", &Metadata));
	EXPECT_FALSE(ParseMapInfoDifficulty(
		"\"IceBox\" by Mapper on Insane, ★★★✰✰, 3 points, 42 finishes by 7 tees in arbitrary median",
		"IceBox", &Metadata));
	EXPECT_FALSE(ParseMapInfoDifficulty(
		"\"IceBox\" by Mapper on Insane, 3 points, 42 finishes by 7 tees",
		"IceBox", &Metadata));

	SRunContext LegacyContext;
	EXPECT_EQ(MapDifficultyDisplayName(LegacyContext), "—");
	SRunContext CategoryOnlyContext;
	CategoryOnlyContext.m_MapDifficulty = "Insane";
	EXPECT_EQ(MapDifficultyDisplayName(CategoryOnlyContext), "Insane");
}

TEST(ImpactFoundation, KogMapInfoMetadataSupportsCanonicalRatingRepresentations)
{
	SMapDifficultyMetadata Metadata;
	EXPECT_TRUE(ParseKogMapInfoDifficulty(
		"\"Square_of_Gores_2\" by Mixi on INS, ★★★✰✰, 73 points, 4 finishes by 2 tees",
		"Square_of_Gores_2", &Metadata));
	EXPECT_EQ(Metadata.m_Category, "Insane");
	EXPECT_EQ(Metadata.m_Stars, 3);

	EXPECT_TRUE(ParseKogMapInfoDifficulty(
		"\"bzolow\" by bzw on SOL, 5, 44 points, 1 finish by 1 tee",
		"bzolow", &Metadata));
	EXPECT_EQ(Metadata.m_Category, "Solo");
	EXPECT_EQ(Metadata.m_Stars, 5);

	EXPECT_TRUE(ParseKogMapInfoDifficulty(
		"\"Greeny\" by KOF on ESY, 3, 4 points, 0 finishes by 0 tees",
		"Greeny", &Metadata));
	EXPECT_EQ(Metadata.m_Category, "Easy");
	EXPECT_EQ(Metadata.m_Stars, 3);
}

TEST(ImpactFoundation, KogMetadataCacheMergeFillsLateRatingWithoutGuessing)
{
	SMapDifficultyMetadata Cached;
	Cached.m_Category = "Insane";
	Cached.m_Stars = -1;
	SMapDifficultyMetadata Incoming;
	Incoming.m_Category = "Insane";
	Incoming.m_Stars = 3;
	SMapDifficultyMetadata Merged;
	EXPECT_TRUE(CImpactSystem::MergeKogMapMetadata(Cached, Incoming, &Merged));
	EXPECT_EQ(Merged.m_Category, "Insane");
	EXPECT_EQ(Merged.m_Stars, 3);

	SMapDifficultyMetadata CategoryOnly;
	CategoryOnly.m_Category = "Insane";
	CategoryOnly.m_Stars = -1;
	EXPECT_TRUE(CImpactSystem::MergeKogMapMetadata(Merged, CategoryOnly, &Merged));
	EXPECT_EQ(Merged.m_Stars, 3);

	SMapDifficultyMetadata Conflicting;
	Conflicting.m_Category = "Insane";
	Conflicting.m_Stars = 4;
	EXPECT_FALSE(CImpactSystem::MergeKogMapMetadata(Merged, Conflicting, &Merged));
	EXPECT_EQ(Merged.m_Stars, 3);
}

TEST(ImpactFoundation, CurrentKogCategoryOverridesLegacyCacheButKeepsRating)
{
	SMapDifficultyMetadata Passive;
	SMapDifficultyMetadata Cached;
	Cached.m_Category = "Main";
	Cached.m_Stars = 3;
	SMapDifficultyMetadata ServerCategory;
	ServerCategory.m_Category = "Insane";

	const CImpactSystem::SKogMapMetadataResolution Resolved = CImpactSystem::ResolveKogMapMetadata(
		Passive, false, Cached, true, ServerCategory, true);
	EXPECT_EQ(Resolved.m_Metadata.m_Category, "Insane");
	EXPECT_EQ(Resolved.m_Metadata.m_Stars, 3);
	EXPECT_STREQ(Resolved.m_CategorySource, "SERVER_CATEGORY");
	EXPECT_STREQ(Resolved.m_RatingSource, "CACHE");
}

TEST(ImpactFoundation, StrongerKogCategoryCannotBeDowngradedByWeakerSource)
{
	SMapDifficultyMetadata Passive;
	Passive.m_Category = "Insane";
	Passive.m_Stars = 3;
	SMapDifficultyMetadata Cached;
	Cached.m_Category = "Main";
	Cached.m_Stars = 3;
	SMapDifficultyMetadata ServerCategory;
	ServerCategory.m_Category = "Main";

	const CImpactSystem::SKogMapMetadataResolution Resolved = CImpactSystem::ResolveKogMapMetadata(
		Passive, true, Cached, true, ServerCategory, true);
	EXPECT_EQ(Resolved.m_Metadata.m_Category, "Insane");
	EXPECT_EQ(Resolved.m_Metadata.m_Stars, 3);
	EXPECT_STREQ(Resolved.m_CategorySource, "PASSIVE_SV_MAPINFO");
	EXPECT_STREQ(Resolved.m_RatingSource, "PASSIVE_SV_MAPINFO");
}

TEST(ImpactFoundation, KogVoteRatingNeverInventsCategory)
{
	SMapDifficultyMetadata Passive;
	SMapDifficultyMetadata Cached;
	Cached.m_Stars = 3;
	SMapDifficultyMetadata ServerCategory;

	const CImpactSystem::SKogMapMetadataResolution Resolved = CImpactSystem::ResolveKogMapMetadata(
		Passive, false, Cached, true, ServerCategory, false);
	EXPECT_TRUE(Resolved.m_Metadata.m_Category.empty());
	EXPECT_EQ(Resolved.m_Metadata.m_Stars, 3);
	EXPECT_STREQ(Resolved.m_CategorySource, "NONE");
	EXPECT_STREQ(Resolved.m_RatingSource, "CACHE");
}

TEST(ImpactFoundation, ActiveRunAcceptsStrongerLateKogCategoryForSameMap)
{
	SRunContext Context;
	Context.m_ServerKey = "kog|ger5";
	Context.m_CommunityId = "kog";
	Context.m_Map = "AngelDust3";
	Context.m_MapDifficulty = "Main";
	Context.m_MapStars = 3;
	CImpactRun Run;
	Run.Initialize(1, Context);

	SMapDifficultyMetadata StrongerMetadata;
	StrongerMetadata.m_Category = "Insane";
	StrongerMetadata.m_Stars = 3;
	EXPECT_TRUE(Run.UpdateAuthoritativeMapDifficultyMetadata(StrongerMetadata));
	EXPECT_EQ(Run.Context().m_Map, "AngelDust3");
	EXPECT_EQ(Run.Context().m_MapDifficulty, "Insane");
	EXPECT_EQ(Run.Context().m_MapStars, 3);
}

TEST(ImpactFoundation, KogMapInfoMetadataRejectsUntrustedText)
{
	SMapDifficultyMetadata Metadata;
	EXPECT_FALSE(ParseKogMapInfoDifficulty(
		"this map is insane 3 stars lol",
		"Greeny", &Metadata));
	EXPECT_FALSE(ParseKogMapInfoDifficulty(
		"\"Greeny\" by KOF on ESY, 3, unrelated text",
		"Greeny", &Metadata));
	EXPECT_FALSE(ParseKogMapInfoDifficulty(
		"\"Greeny\" by KOF on UNKNOWN, ★★★✰✰, 4 points, 0 finishes by 0 tees",
		"Greeny", &Metadata));
}

TEST(ImpactFoundation, KogServerCategoryIsCategoryOnlyAndStrict)
{
	SMapDifficultyMetadata Metadata;
	EXPECT_TRUE(ParseKogServerCategory("GER5 - Insane Gores [kog.tw]", &Metadata));
	EXPECT_EQ(Metadata.m_Category, "Insane");
	EXPECT_EQ(Metadata.m_Stars, -1);
	EXPECT_TRUE(ParseKogServerCategory("Main Gores", &Metadata));
	EXPECT_EQ(Metadata.m_Category, "Main");
	EXPECT_FALSE(ParseKogServerCategory("NotInsane Gores", &Metadata));
	EXPECT_FALSE(ParseKogServerCategory("GER5 - Gores", &Metadata));
	EXPECT_FALSE(ParseKogServerCategory("Insane Gores / Easy Gores", &Metadata));
}

TEST(ImpactFoundation, KogVoteListRatingParserUsesExactMapEntryGrammar)
{
	int Rating = -1;
	EXPECT_TRUE(ParseKogVoteMapRating("Aip-Hardcores | ★★★☆☆ | 1760 | 04:07", "Aip-Hardcores", &Rating));
	EXPECT_EQ(Rating, 3);
	EXPECT_TRUE(ParseKogVoteMapRating("Q-tele | ★☆☆☆☆ | 1320 | 02:15", "Q-tele", &Rating));
	EXPECT_EQ(Rating, 1);
	EXPECT_TRUE(ParseKogVoteMapRating("Miku4Lama | ★★★★★ | 1912 | 01:41", "Miku4Lama", &Rating));
	EXPECT_EQ(Rating, 5);
	EXPECT_TRUE(ParseKogVoteMapRating("Themed4RegKecske | ★★✰✰✰ | 1760 | 04:07", "Themed4RegKecske", &Rating));
	EXPECT_EQ(Rating, 2);
	EXPECT_TRUE(ParseKogVoteMapRating("fikbzw | ★★★✰✰ | 8553 ⚐ | 01:26 ◷", "fikbzw", &Rating));
	EXPECT_EQ(Rating, 3);
	EXPECT_TRUE(ParseKogVoteMapRating("fik4imp | ★★✰✰✰ | 7974 ⚐ | 01:30 ◷", "fik4imp", &Rating));
	EXPECT_EQ(Rating, 2);
	EXPECT_TRUE(ParseKogVoteMapRating("Corrupted_Easy | ★✰✰✰✰ | 27215 ⚐ | 01:26 ◷", "Corrupted_Easy", &Rating));
	EXPECT_EQ(Rating, 1);
}

TEST(ImpactFoundation, KogVoteListRatingParserRejectsNonEntriesAndNearMatches)
{
	int Rating = -1;
	const char *pReason = nullptr;
	EXPECT_FALSE(ParseKogVoteMapRating("Aip-Hardcores Extra | ★★★☆☆ | 1760 | 04:07", "Aip-Hardcores", &Rating, &pReason));
	EXPECT_STREQ(pReason, "MAP_NAME_MISMATCH");
	EXPECT_FALSE(ParseKogVoteMapRating("Aip-Hardcores | ★★★ | 1760 | 04:07", "Aip-Hardcores", &Rating, &pReason));
	EXPECT_STREQ(pReason, "MALFORMED_RATING_FIELD");
	EXPECT_FALSE(ParseKogVoteMapRating("NEW MAPS", "Aip-Hardcores", &Rating, &pReason));
	EXPECT_STREQ(pReason, "MALFORMED_OPTION_FIELDS");
	EXPECT_FALSE(ParseKogVoteMapRating("Aip-Hardcores | ★★★☆☆ | 1760 | 4:07", "Aip-Hardcores", &Rating, &pReason));
	EXPECT_STREQ(pReason, "MALFORMED_TIME_FIELD");
	EXPECT_FALSE(ParseKogVoteMapRating("Aip-Hardcores | 3 | 1760 | 04:07", "Aip-Hardcores", &Rating, &pReason));
	EXPECT_STREQ(pReason, "MALFORMED_RATING_FIELD");
	EXPECT_FALSE(ParseKogVoteMapRating("fikbzw | ★★★✰✰ | 8553 ⚑ | 01:26 ◷", "fikbzw", &Rating, &pReason));
	EXPECT_STREQ(pReason, "MALFORMED_POINTS_FIELD");
	EXPECT_FALSE(ParseKogVoteMapRating("fikbzw | ★★★✰✰ | 8553 ⚐ | 01:26 ◶", "fikbzw", &Rating, &pReason));
	EXPECT_STREQ(pReason, "MALFORMED_TIME_FIELD");
	EXPECT_FALSE(ParseKogVoteMapRating("fikbzw | ★★★✰✰ | eight ⚐ | 01:26 ◷", "fikbzw", &Rating, &pReason));
	EXPECT_STREQ(pReason, "MALFORMED_POINTS_FIELD");
	EXPECT_FALSE(ParseKogVoteMapRating("fikbzw | ★★★✰✰ | 8553 ⚐ | 01:60 ◷", "fikbzw", &Rating, &pReason));
	EXPECT_STREQ(pReason, "MALFORMED_TIME_FIELD");
	EXPECT_FALSE(ParseKogVoteMapRating("other_map | ★★★✰✰ | 8553 ⚐ | 01:26 ◷", "fikbzw", &Rating, &pReason));
	EXPECT_STREQ(pReason, "MAP_NAME_MISMATCH");
}

TEST(ImpactFoundation, AuthoritativeMapInfoOverridesVoteListRatingInActiveRun)
{
	SRunContext Context;
	Context.m_ServerKey = "kog|ger5";
	Context.m_CommunityId = "kog";
	Context.m_Map = "Aip-Hardcores";
	Context.m_MapDifficulty = "Insane";
	Context.m_MapStars = -1;

	CImpactRun Run;
	Run.Initialize(1, Context);
	SMapDifficultyMetadata VoteMetadata;
	VoteMetadata.m_Category = "Insane";
	VoteMetadata.m_Stars = 2;
	EXPECT_TRUE(Run.UpdateMapDifficultyMetadata(VoteMetadata));
	EXPECT_EQ(Run.Context().m_MapStars, 2);

	SMapDifficultyMetadata MapInfoMetadata;
	MapInfoMetadata.m_Category = "Insane";
	MapInfoMetadata.m_Stars = 3;
	EXPECT_TRUE(Run.UpdateAuthoritativeMapDifficultyMetadata(MapInfoMetadata));
	EXPECT_EQ(Run.Context().m_MapStars, 3);
}

TEST(ImpactFoundation, KogMapInfoRequestPolicyIsBounded)
{
	SMapDifficultyMetadata Unknown;
	SMapDifficultyMetadata CategoryOnly;
	CategoryOnly.m_Category = "Insane";
	SMapDifficultyMetadata Complete = CategoryOnly;
	Complete.m_Stars = 3;

	EXPECT_FALSE(CImpactSystem::ShouldRequestKogMapInfo(false, CategoryOnly, false, 0, 100, -1));
	EXPECT_FALSE(CImpactSystem::ShouldRequestKogMapInfo(true, Complete, false, 0, 100, -1));
	EXPECT_TRUE(CImpactSystem::ShouldRequestKogMapInfo(true, Unknown, false, 0, 100, -1));
	EXPECT_FALSE(CImpactSystem::ShouldRequestKogMapInfo(true, Unknown, true, 0, 100, -1));
	EXPECT_FALSE(CImpactSystem::ShouldRequestKogMapInfo(true, Unknown, false, CImpactSystem::MAX_AUTOMATIC_MAPINFO_REQUESTS, 100, -1));
	EXPECT_FALSE(CImpactSystem::ShouldRequestKogMapInfo(true, Unknown, false, 1, 99, 100));
	EXPECT_TRUE(CImpactSystem::ShouldRequestKogMapInfo(true, Unknown, false, 1, 100, 100));
}

TEST(ImpactFoundation, MapDifficultyStarDisplayUsesOnlyKnownSupportedRange)
{
	SRunContext Context;
	Context.m_MapDifficulty = "Moderate";
	for(int Stars = 0; Stars <= MAX_MAP_DIFFICULTY_STARS; ++Stars)
	{
		Context.m_MapStars = Stars;
		const std::string Display = MapDifficultyDisplayName(Context);
		if(Stars == 0)
			EXPECT_EQ(Display, "Moderate");
		else
		{
			std::string Expected = "Moderate · ";
			for(int Index = 0; Index < Stars; ++Index)
				Expected += "★";
			EXPECT_EQ(Display, Expected);
		}
	}
	Context.m_MapStars = -1;
	EXPECT_EQ(MapDifficultyDisplayName(Context), "Moderate");
	Context.m_MapStars = MAX_MAP_DIFFICULTY_STARS + 1;
	EXPECT_EQ(MapDifficultyDisplayName(Context), "Moderate");
}

TEST(ImpactFoundation, AlgorithmVersionsPersist)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	SRunSnapshot SnapshotData = PersistentSnapshot(7);
	SnapshotData.m_AlgorithmVersion = 9;
	SnapshotData.m_Score.m_RatingVersion = 11;
	std::vector<SRunSnapshot> History{SnapshotData};
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), History));
	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), 1U);
	EXPECT_EQ(Loaded[0].m_AlgorithmVersion, 9);
	EXPECT_EQ(Loaded[0].m_Score.m_RatingVersion, 11);
}

TEST(ImpactFoundation, FullHistoryRetainsRunsBeyondRecentLimit)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	std::vector<SRunSnapshot> History;
	constexpr uint64_t PersistedRunCount = 100;
	for(uint64_t RunId = 1; RunId <= PersistedRunCount; ++RunId)
		History.push_back(PersistentSnapshot(RunId));
	EXPECT_TRUE(CPersistence::SaveHistory(Storage.get(), History));
	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), PersistedRunCount);
	EXPECT_EQ(Loaded.front().m_RunId, 1U);
	EXPECT_EQ(Loaded[4].m_RunId, 5U);
	const size_t RecentStart = RecentHistoryStartIndex(Loaded.size());
	EXPECT_EQ(Loaded.size() - RecentStart, MAX_RECENT_HISTORY_RUNS);
	EXPECT_EQ(RecentStart, PersistedRunCount - MAX_RECENT_HISTORY_RUNS);
	EXPECT_EQ(Loaded[RecentStart].m_RunId, 71U);
	EXPECT_EQ(HistoryViewStartIndex(Loaded.size(), false), RecentStart);
	EXPECT_EQ(HistoryViewStartIndex(Loaded.size(), true), 0U);
}

TEST(ImpactFoundation, HistoryViewLastThirtyThenAllBoundaries)
{
	EXPECT_EQ(HistoryViewStartIndex(0, false), 0U);
	EXPECT_EQ(HistoryViewStartIndex(30, false), 0U);
	EXPECT_EQ(HistoryViewStartIndex(31, false), 1U);
	bool Expanded = false;
	EXPECT_EQ(HistoryViewStartIndex(100, Expanded), 70U);
	Expanded = true;
	EXPECT_EQ(HistoryViewStartIndex(100, Expanded), 0U);
	// The menu resets its UI-local expanded state on the next opening.
	Expanded = false;
	EXPECT_EQ(HistoryViewStartIndex(100, Expanded), 70U);
}

TEST(ImpactFoundation, StatisticsRowsIncludeRosterMembersWithoutPersonalStart)
{
	std::vector<SParticipant> TeamRoster(3);
	TeamRoster[0].m_Id = TestId(1);
	TeamRoster[0].m_FirstStartTick = 100;
	TeamRoster[1].m_Id = TestId(2);
	TeamRoster[1].m_FirstStartTick = 110;
	TeamRoster[2].m_Id = TestId(3);
	// Roster rows exist before individual engagement or personal START.
	EXPECT_EQ(ImpactStatisticsParticipantCount(TeamRoster), 3U);
	EXPECT_EQ(TeamRoster[2].m_FirstStartTick, -1);
	EXPECT_FALSE(TeamRoster[2].m_Engaged);

	TeamRoster[2].m_FirstStartTick = 130;
	EXPECT_EQ(ImpactStatisticsParticipantCount(TeamRoster), 3U);
}

TEST(ImpactFoundation, FinalizedRunAutoSavePolicyUsesFinishOrFiveActiveMinutes)
{
	const int TickSpeed = TestContext().m_TickSpeed;
	const int64_t OneMinute = 60LL * TickSpeed;
	const int64_t FiveMinutes = 5LL * OneMinute;
	EXPECT_EQ(FiveMinutes, static_cast<int64_t>(MIN_AUTO_SAVE_SECONDS) * TickSpeed);
	EXPECT_TRUE(CImpactSystem::ShouldAutoSaveFinalizedRun(ERunResult::FINISHED, 0, TickSpeed));
	EXPECT_FALSE(CImpactSystem::ShouldAutoSaveFinalizedRun(ERunResult::TEAM_RESET, 5LL * TickSpeed, TickSpeed));
	EXPECT_FALSE(CImpactSystem::ShouldAutoSaveFinalizedRun(ERunResult::TEAM_RESET, OneMinute, TickSpeed));
	EXPECT_FALSE(CImpactSystem::ShouldAutoSaveFinalizedRun(ERunResult::TEAM_RESET, FiveMinutes - TickSpeed, TickSpeed));
	EXPECT_TRUE(CImpactSystem::ShouldAutoSaveFinalizedRun(ERunResult::TEAM_RESET, FiveMinutes, TickSpeed));
	EXPECT_TRUE(CImpactSystem::ShouldAutoSaveFinalizedRun(ERunResult::WIPED, FiveMinutes + 1, TickSpeed));
	EXPECT_FALSE(CImpactSystem::ShouldAutoSaveFinalizedRun(ERunResult::ABORTED_CONNECTION, FiveMinutes - TickSpeed, TickSpeed));
}

TEST(ImpactFoundation, DeprecatedUpDownRawCounterIsNotWrittenForNewHistory)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	SRunSnapshot SnapshotData = PersistentSnapshot(43);
	SnapshotData.m_RawStats.m_UpDowns = 7;
	std::vector<SRunSnapshot> History{SnapshotData};
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), History));
	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), 1U);
	EXPECT_EQ(Loaded[0].m_RawStats.m_UpDowns, 0);
}

TEST(ImpactFoundation, ClutchRawCounterPersistsInHistory)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	SRunSnapshot SnapshotData = PersistentSnapshot(45);
	SnapshotData.m_RawStats.m_Clutches = 2;
	SnapshotData.m_vParticipants[0].m_RawStats.m_Clutches = 2;
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {SnapshotData}));
	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), 1U);
	EXPECT_EQ(Loaded[0].m_RawStats.m_Clutches, 2);
	EXPECT_EQ(Loaded[0].m_vParticipants[0].m_RawStats.m_Clutches, 2);
}

TEST(ImpactFoundation, ParticipantContextIsScopedByRunId)
{
	CReplay FirstRun(100, TestContext());
	CReplay SecondRun(101, TestContext());
	StartPair(FirstRun);
	StartPair(SecondRun);
	ASSERT_EQ(FirstRun.Run().Participants().size(), 2U);
	ASSERT_EQ(SecondRun.Run().Participants().size(), 2U);
	EXPECT_NE(FirstRun.Run().RunId(), SecondRun.Run().RunId());
	EXPECT_EQ(FirstRun.Run().Participants()[0].m_Id, SecondRun.Run().Participants()[0].m_Id);

	STickEventBatch Batch;
	Batch.m_ServerTick = 100;
	SImpactEvent Save = StartEvent(3, TestId(1), 100);
	Save.m_RawDelta.m_Saves = 1;
	Batch.m_vCandidates.push_back(std::move(Save));
	EXPECT_TRUE(FirstRun.Submit(Batch));
	EXPECT_EQ(FirstRun.Run().Participants()[0].m_RawStats.m_Saves, 1);
	EXPECT_EQ(SecondRun.Run().Participants()[0].m_RawStats.m_Saves, 0);
}

TEST(ImpactFoundation, DeleteRequiresExplicitOperation)
{
	std::vector<SRunSnapshot> History{PersistentSnapshot(1), PersistentSnapshot(2)};
	EXPECT_EQ(History.size(), 2U);
	History.erase(std::remove_if(History.begin(), History.end(), [](const SRunSnapshot &SnapshotData) {
		return SnapshotData.m_RunId == 1;
	}), History.end());
	ASSERT_EQ(History.size(), 1U);
	EXPECT_EQ(History[0].m_RunId, 2U);
}

TEST(ImpactFoundation, VisibilityToggleDoesNotCreateResult)
{
	CReplay Replay(1, TestContext());
	StartPair(Replay);
	EXPECT_FALSE(Replay.Run().IsFinalized());
	// Visibility changes are not gameplay lifecycle events. The pure core
	// remains active and has no implicit result transition of its own.
	EXPECT_EQ(Replay.Run().Result(), ERunResult::NONE);
}

TEST(ImpactFoundation, CurrentStateNamesAreStable)
{
	EXPECT_STREQ(AnalysisStateName(EAnalysisState::WAITING), "WAITING");
	EXPECT_STREQ(AnalysisStateName(EAnalysisState::ANALYZING), "ANALYZING");
	EXPECT_STREQ(AnalysisStateName(EAnalysisState::ACTIVE), "ACTIVE");
	EXPECT_STREQ(AnalysisStateName(EAnalysisState::PAUSED_NO_TEAM), "PAUSED_NO_TEAM");
}

TEST(ImpactFoundation, RunResultDisplayNamesAreHumanReadable)
{
	EXPECT_STREQ(RunResultDisplayName(ERunResult::NONE), "In progress");
	EXPECT_STREQ(RunResultDisplayName(ERunResult::FINISHED), "Finished");
	EXPECT_STREQ(RunResultDisplayName(ERunResult::WIPED), "Wiped");
	EXPECT_STREQ(RunResultDisplayName(ERunResult::TEAM_RESET), "Reset");
	EXPECT_STREQ(RunResultDisplayName(ERunResult::ABORTED_MAP_CHANGE), "Map changed");
	EXPECT_STREQ(RunResultDisplayName(ERunResult::ABORTED_SERVER_CHANGE), "Server changed");
	EXPECT_STREQ(RunResultDisplayName(ERunResult::ABORTED_CONNECTION), "Disconnected");
	const std::array<ERunResult, 7> aResults = {
		ERunResult::NONE,
		ERunResult::FINISHED,
		ERunResult::WIPED,
		ERunResult::TEAM_RESET,
		ERunResult::ABORTED_MAP_CHANGE,
		ERunResult::ABORTED_SERVER_CHANGE,
		ERunResult::ABORTED_CONNECTION};
	for(const ERunResult Result : aResults)
	{
		const std::string Display = RunResultDisplayName(Result);
		EXPECT_EQ(Display.find('_'), std::string::npos);
		EXPECT_STRNE(Display.c_str(), RunResultName(Result));
	}
}

TEST(ImpactFoundation, FinishedResultIsTerminal)
{
	CReplay Replay(1, TestContext());
	StartPair(Replay);
	STickEventBatch Batch;
	Batch.m_ServerTick = 300;
	SImpactEvent Finish;
	Finish.m_EventId = 3;
	Finish.m_Type = EImpactEventType::FINISH_CONFIRMED;
	Finish.m_Confirmed = true;
	Finish.m_StartTick = 300;
	Finish.m_ResolveTick = 300;
	Batch.m_vCandidates.push_back(Finish);
	EXPECT_TRUE(Replay.Submit(Batch));
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::FINALIZED);
	EXPECT_EQ(Replay.Run().Result(), ERunResult::FINISHED);
}

TEST(ImpactFoundation, FinishedResultCannotBeOverwrittenOrMutatedByLaterLifecycle)
{
	CReplay Replay(2, TestContext());
	StartPair(Replay);
	STickEventBatch SaveBatch;
	SaveBatch.m_ServerTick = 250;
	SImpactEvent Save = StartEvent(3, TestId(1), 250);
	Save.m_Type = EImpactEventType::SAVE_CONFIRMED;
	Save.m_Target = TestId(2);
	Save.m_RawDelta.m_Saves = 1;
	SaveBatch.m_vCandidates.push_back(Save);
	ASSERT_TRUE(Replay.Submit(SaveBatch));

	STickEventBatch FinishBatch;
	FinishBatch.m_ServerTick = 300;
	SImpactEvent Finish = StartEvent(4, TestId(1), 300);
	Finish.m_Type = EImpactEventType::FINISH_CONFIRMED;
	Finish.m_Actor = {};
	Finish.m_Target = {};
	Finish.m_AuthoritativeFinishTimeMillis = 6000;
	FinishBatch.m_vCandidates.push_back(Finish);
	ASSERT_TRUE(Replay.Submit(FinishBatch));
	const SRunSnapshot Finished = Replay.Run().Snapshot(300);
	ASSERT_EQ(Finished.m_Result, ERunResult::FINISHED);
	ASSERT_EQ(Finished.m_RawStats.m_Saves, 1);

	EXPECT_FALSE(Replay.Run().Finalize(ERunResult::TEAM_RESET, 301));
	EXPECT_FALSE(Replay.Run().Finalize(ERunResult::ABORTED_CONNECTION, 302));
	EXPECT_EQ(Replay.Run().Result(), ERunResult::FINISHED);
	const SRunSnapshot AfterLifecycle = Replay.Run().Snapshot(1000);
	EXPECT_EQ(AfterLifecycle.m_Result, ERunResult::FINISHED);
	EXPECT_EQ(AfterLifecycle.m_RawStats.m_Saves, Finished.m_RawStats.m_Saves);
	EXPECT_EQ(AfterLifecycle.m_ActiveAnalysisTicks, Finished.m_ActiveAnalysisTicks);
}

TEST(ImpactFoundation, AbortedResultIsNotWipe)
{
	CReplay Replay(1, TestContext());
	StartPair(Replay);
	EXPECT_TRUE(Replay.Run().Finalize(ERunResult::ABORTED_MAP_CHANGE, 300));
	EXPECT_EQ(Replay.Run().Result(), ERunResult::ABORTED_MAP_CHANGE);
	EXPECT_NE(Replay.Run().Result(), ERunResult::WIPED);
}

TEST(ImpactFoundation, ScoreIsUnavailableUntilScorerExists)
{
	CReplay Replay(1, TestContext());
	StartPair(Replay);
	EXPECT_FALSE(Replay.Run().Score().m_Available);
}

TEST(ImpactFoundation, UIStateDoesNotChangeCoreSemantics)
{
	CReplay Replay(1, TestContext());
	StartPair(Replay);
	const int64_t NormalScaleTick = Replay.Run().ActiveAnalysisTicks(200);
	const int64_t LargeScaleTick = Replay.Run().ActiveAnalysisTicks(2000);
	EXPECT_LT(NormalScaleTick, LargeScaleTick);
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::ACTIVE);
}

static SImpactGameplayInput ImpactInput(int ClientId, int64_t Tick, bool Frozen = false, bool Alive = true, bool Eligible = true, int TeamId = 1, bool Dummy = false, EImpactRecoveryNeed RecoveryNeed = EImpactRecoveryNeed::EXTERNAL_REQUIRED)
{
	SImpactGameplayInput Input;
	Input.m_Participant = TestId(ClientId, 1, Dummy);
	Input.m_TeamId = TeamId;
	Input.m_ServerTick = Tick;
	Input.m_Present = true;
	Input.m_Confirmed = true;
	Input.m_CurrentCharacterKnown = true;
	Input.m_PreviousCharacterKnown = true;
	Input.m_PositionKnown = true;
	Input.m_VelocityKnown = true;
	Input.m_SupportKnown = true;
	Input.m_Supported = RecoveryNeed == EImpactRecoveryNeed::SELF_SAFE;
	if(RecoveryNeed == EImpactRecoveryNeed::SELF_SAFE)
	{
		// SELF_SAFE is an explicit fixture proof of autonomous recovery, not an
		// inference from platform support alone.
		Input.m_SafeCollisionPositionKnown = true;
		Input.m_SafeCollisionPosition = true;
		Input.m_AutonomousRecoveryKnown = true;
		Input.m_AutonomousRecoveryPossible = true;
	}
	Input.m_AliveKnown = true;
	Input.m_Alive = Alive;
	Input.m_FreezeKnown = true;
	Input.m_Frozen = Frozen;
	// The default fixture models a short freeze that naturally expires before
	// the next recovery observation. Tests that need a longer timer set the
	// authoritative freeze end explicitly.
	Input.m_FreezeEnd = Frozen ? Tick + 10 : 0;
	Input.m_FreezeStart = Frozen ? Tick : -1;
	Input.m_SoloKnown = true;
	Input.m_Eligible = Eligible;
	Input.m_RecoveryNeed = RecoveryNeed;
	Input.m_HookKnown = true;
	Input.m_HookState = HOOK_IDLE;
	Input.m_AttackKnown = true;
	Input.m_Weapon = WEAPON_GUN;
	// The fixture supplies a complete target-specific hammer-evidence channel;
	// tests that model the live DDNet limitation use a zero/unknown input
	// explicitly instead of silently treating missing evidence as no hammer.
	Input.m_HammerTargetEvidenceKnown = true;
	return Input;
}

static SImpactGameplayFrame ImpactFrame(int ClientId, int64_t Tick, bool Frozen = false, bool Alive = true, bool Eligible = true, int TeamId = 1, bool Dummy = false, EImpactRecoveryNeed RecoveryNeed = EImpactRecoveryNeed::EXTERNAL_REQUIRED)
{
	return NormalizeGameplayFrame(ImpactInput(ClientId, Tick, Frozen, Alive, Eligible, TeamId, Dummy, RecoveryNeed));
}

static SImpactGameplayFrame AmbiguousFrozenFrame(int ClientId, int64_t Tick, int64_t FreezeStart)
{
	SImpactGameplayFrame Frame = ImpactFrame(ClientId, Tick, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Frame.m_Supported = false;
	Frame.m_SafeCollisionPositionKnown = true;
	Frame.m_SafeCollisionPosition = false;
	Frame.m_OrdinaryFreezeTileKnown = true;
	Frame.m_OrdinaryFreezeTile = false;
	Frame.m_AutonomousRecoveryKnown = false;
	Frame.m_AutonomousRecoveryPossible = false;
	Frame.m_AutonomousRecoveryEvaluated = false;
	Frame.m_FreezeStart = FreezeStart;
	Frame.m_FreezeEnd = FreezeStart + 100;
	return Frame;
}

static void SetSelfCausedHarm(SImpactGameplayInput &Input)
{
	Input.m_HarmCause = EImpactHarmCause::SELF_CAUSED;
	Input.m_HarmCausalConfidence = ECausalConfidence::CONFIRMED;
}

static void SetSelfCausedHarm(SImpactGameplayFrame &Frame)
{
	Frame.m_HarmCause = EImpactHarmCause::SELF_CAUSED;
	Frame.m_HarmCausalConfidence = ECausalConfidence::CONFIRMED;
}

// This is the same positive physical evidence that the live adapter derives
// from a death-impact tile. It is deliberately separate from RecoveryNeed and
// from the frozen bit so Save tests cannot accidentally rely on either one as
// proof that the pre-action state was unsafe.
static void SetPhysicalHarmfulOutcome(SImpactGameplayInput &Input)
{
	Input.m_HarmfulOutcomeCandidateKnown = true;
	Input.m_HarmfulOutcomeCandidate = true;
}

static void SetPhysicalHarmfulOutcome(SImpactGameplayFrame &Frame)
{
	Frame.m_HarmfulOutcomeCandidateKnown = true;
	Frame.m_HarmfulOutcomeCandidate = true;
}

static void ClearPhysicalHarmfulOutcome(SImpactGameplayInput &Input)
{
	Input.m_HarmfulOutcomeCandidateKnown = false;
	Input.m_HarmfulOutcomeCandidate = false;
}

static void ClearPhysicalHarmfulOutcome(SImpactGameplayFrame &Frame)
{
	Frame.m_HarmfulOutcomeCandidateKnown = false;
	Frame.m_HarmfulOutcomeCandidate = false;
}

static void SetHooked(SImpactGameplayFrame &Frame, int TargetClientId, bool MeaningfulSupport);

static void SetConfirmedExternalRecoveryEvidence(SImpactGameplayFrame &Frame)
{
	SetPhysicalHarmfulOutcome(Frame);
	Frame.m_AutonomousRecoveryKnown = true;
	Frame.m_AutonomousRecoveryPossible = false;
}

static bool BeginTwoConfirmedFallClutchEpisode(CReplay &Replay, SImpactGameplayFrame &Saver,
	SImpactGameplayFrame &Target, SImpactGameplayFrame &Other)
{
	Saver = ImpactFrame(1, 90, false);
	Target = ImpactFrame(2, 90, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_FreezeEnd = 200;
	Other = ImpactFrame(3, 90, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Other.m_FreezeEnd = 200;
	if(Replay.SubmitGameplay({Saver, Target, Other}))
		return false;

	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 100;
	SetHooked(Saver, 2, true);
	SetConfirmedExternalRecoveryEvidence(Target);
	SetConfirmedExternalRecoveryEvidence(Other);
	return Replay.SubmitGameplay({Saver, Target, Other});
}

static void SetDynamicSupport(SImpactGameplayInput &Input, bool Supported = true)
{
	Input.m_DynamicSupportKnown = true;
	Input.m_DynamicSupport = Supported;
}

// Mirrors the production adapter's eligibility source: the live frame is
// eligible only when the participant was registered by the run's start path.
// Tests in this block must not make an otherwise ordinary Tee eligible by
// setting the input bit themselves.
static bool SubmitLiveEquivalentInputs(CReplay &Replay, std::vector<SImpactGameplayInput> vInputs, const std::vector<SImpactGameplayMessage> &vMessages)
{
	for(SImpactGameplayInput &Input : vInputs)
		Input.m_Eligible = Replay.Run().HasParticipant(Input.m_Participant);
	return Replay.SubmitGameplayInputs(vInputs, vMessages);
}

// Build jump/reset evidence from the same authoritative fields used by the
// live adapter. Tests intentionally do not set a reset flag or harm cause.
static void SetAuthoritativeJumpEvidence(SImpactGameplayInput &Input, int Jumps, int Jumped, bool Grounded,
	bool JumpMetadataKnown, int AttackTick, int PreviousAttackTick, bool AimKnown, int TargetY, int Weapon)
{
	Input.m_CurrentCharacterKnown = true;
	Input.m_PreviousCharacterKnown = true;
	Input.m_AliveKnown = true;
	Input.m_FreezeKnown = true;
	Input.m_SupportKnown = true;
	Input.m_Supported = Grounded;
	Input.m_AttackKnown = true;
	Input.m_AttackTick = AttackTick;
	Input.m_Weapon = Weapon;
	Input.m_JumpState = CImpactSystem::ClassifyUsableJumpStateFromAuthoritativeState(
		Input.m_CurrentCharacterKnown, Input.m_AliveKnown, Input.m_Alive,
		Input.m_FreezeKnown, Input.m_Frozen, JumpMetadataKnown, Jumps, Jumped,
		Input.m_SupportKnown, Grounded, false);
	bool Downward = false;
	Input.m_DownwardShotKnown = CImpactSystem::ClassifySelfDownwardShotFromAuthoritativeState(
		Input.m_CurrentCharacterKnown, Input.m_PreviousCharacterKnown, Input.m_AttackKnown,
		Input.m_AttackTick, PreviousAttackTick, AimKnown, TargetY, Weapon, &Downward);
	Input.m_DownwardShot = Input.m_DownwardShotKnown && Downward;
}

static SImpactGameplayInput MissingCharacterInput(int ClientId, int64_t Tick, int TeamId = 1, bool Dummy = false)
{
	SImpactGameplayInput Input;
	Input.m_Participant = TestId(ClientId, 1, Dummy);
	Input.m_TeamId = TeamId;
	Input.m_ServerTick = Tick;
	Input.m_Present = true;
	Input.m_Confirmed = true;
	Input.m_Eligible = true;
	return Input;
}

static EImpactRecoveryNeed ClassifyLiveRecoveryInput(SImpactGameplayInput &Input, bool SafeCollisionPosition)
{
	Input.m_RecoveryNeed = CImpactSystem::ClassifyRecoveryNeedFromAuthoritativeState(Input, SafeCollisionPosition);
	return Input.m_RecoveryNeed;
}

static SImpactGameplayInput LiveExternalRecoveryTarget(int64_t Tick)
{
	SImpactGameplayInput Target = ImpactInput(2, Tick, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_Supported = false;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	// Unsupported/unsafe geometry alone is not a counterfactual proof. This
	// fixture is explicitly deterministic so it remains an external-recovery
	// fixture after the classifier stops guessing from support alone.
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	Target.m_FreezeStart = Tick;
	Target.m_FreezeEnd = Tick + 90;
	Target.m_RecoveryNeed = ClassifyLiveRecoveryInput(Target, false);
	return Target;
}

static SImpactGameplayInput LiveOrdinaryFrozenTarget(int64_t Tick)
{
	SImpactGameplayInput Target = ImpactInput(2, Tick, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_Supported = false;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	Target.m_FreezeStart = Tick;
	Target.m_FreezeEnd = Tick + 90;
	// This is the ordinary production shape: the adapter knows the target is
	// frozen and not on a safe collision position, but cannot produce a
	// counterfactual autonomous-recovery result. The simplified baseline must
	// not require a synthetic EXTERNAL_REQUIRED flag here.
	Target.m_RecoveryNeed = ClassifyLiveRecoveryInput(Target, false);
	return Target;
}

static SImpactGameplayInput LiveExplicitSelfOriginTarget(int64_t Tick)
{
	SImpactGameplayInput Target = LiveExternalRecoveryTarget(Tick);
	SetSelfCausedHarm(Target);
	return Target;
}

static void SetHooked(SImpactGameplayFrame &Frame, int TargetClientId, bool MeaningfulSupport = false)
{
	Frame.m_HookedPlayer = TargetClientId;
	Frame.m_HookState = HOOK_GRABBED;
	Frame.m_HookKnown = true;
	Frame.m_HookStateConfirmed = true;
	Frame.m_MeaningfulHookSupportKnown = MeaningfulSupport;
	Frame.m_MeaningfulHookSupport = MeaningfulSupport;
}

static void SetHooked(SImpactGameplayInput &Input, int TargetClientId, bool MeaningfulSupport)
{
	Input.m_HookedPlayer = TargetClientId;
	Input.m_HookState = HOOK_GRABBED;
	Input.m_HookKnown = true;
	Input.m_MeaningfulHookSupportKnown = MeaningfulSupport;
	Input.m_MeaningfulHookSupport = MeaningfulSupport;
}

// This models a separate authoritative, target-specific support conclusion.
// HOOK_GRABBED alone is intentionally not enough to classify UpDown.
static void SetMeaningfulHookSupport(SImpactGameplayFrame &Frame)
{
	Frame.m_MeaningfulHookSupportKnown = true;
	Frame.m_MeaningfulHookSupport = true;
}

static void SetMeaningfulHookSupport(SImpactGameplayInput &Input)
{
	Input.m_MeaningfulHookSupportKnown = true;
	Input.m_MeaningfulHookSupport = true;
}

static void ReleaseHook(SImpactGameplayFrame &Frame)
{
	Frame.m_HookedPlayer = -1;
	Frame.m_HookState = HOOK_RETRACTED;
}

static void ReleaseHook(SImpactGameplayInput &Input)
{
	Input.m_HookedPlayer = -1;
	Input.m_HookState = HOOK_RETRACTED;
	Input.m_HookKnown = true;
}

static SImpactGameplayInput LiveSupportedUnknownFreeze(int64_t Tick)
{
	SImpactGameplayInput Target = ImpactInput(2, Tick, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	Target.m_AutonomousRecoveryKnown = false;
	Target.m_AutonomousRecoveryPossible = false;
	Target.m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
	Target.m_FreezeStart = Tick;
	Target.m_FreezeEnd = Tick + 90;
	return Target;
}

static SImpactGameplayMessage ImpactKill(uint64_t MessageId, int64_t Tick, int Killer, int Victim, int Weapon)
{
	SImpactGameplayMessage Message;
	Message.m_MessageId = MessageId;
	Message.m_Type = EImpactGameplayMessageType::KILL;
	Message.m_ServerTick = Tick;
	Message.m_Killer = Killer;
	Message.m_Victim = Victim;
	Message.m_Weapon = Weapon;
	return Message;
}

static SImpactGameplayMessage ImpactFinish(uint64_t MessageId, int64_t Tick, int ClientId)
{
	SImpactGameplayMessage Message;
	Message.m_MessageId = MessageId;
	Message.m_Type = EImpactGameplayMessageType::FINISH;
	Message.m_ServerTick = Tick;
	Message.m_ClientId = ClientId;
	// The live path receives the current team-race time in SV_RACEFINISH. The
	// deterministic fixture uses the same tick clock for its authoritative time.
	Message.m_FinishTimeMillis = Tick >= 0 ? static_cast<int>(Tick * 1000 / 50) : -1;
	return Message;
}

static SImpactGameplayMessage ImpactSignal(uint64_t MessageId, EImpactGameplayMessageType Type, int64_t Tick, const SParticipantId &Actor, const SParticipantId &Target)
{
	SImpactGameplayMessage Message;
	Message.m_MessageId = MessageId;
	Message.m_Type = Type;
	Message.m_ServerTick = Tick;
	Message.m_Actor = Actor;
	Message.m_Target = Target;
	Message.m_CausalConfirmed = true;
	Message.m_CausalConfidence = ECausalConfidence::CONFIRMED;
	return Message;
}

static SImpactPendingEpisode PendingEpisode(uint64_t EpisodeId, EImpactEventType Type, const SParticipantId &Target, int64_t RestoreTick = 0)
{
	SImpactPendingEpisode Episode;
	Episode.m_EpisodeId = EpisodeId;
	Episode.m_Type = Type;
	Episode.m_Target = Target;
	Episode.m_TeamId = 1;
	Episode.m_StartTick = RestoreTick;
	Episode.m_RestoreTick = RestoreTick;
	Episode.m_RequiredTicks = CImpactPhase2::ConfirmationTicks(50);
	Episode.m_State = EImpactEpisodeState::NONE;
	Episode.m_CausalConfidence = ECausalConfidence::CONFIRMED;
	Episode.m_TargetWasEligible = true;
	if(Type == EImpactEventType::FALL_CONFIRMED)
	{
		Episode.m_RecoveryNeed = EImpactRecoveryNeed::EXTERNAL_REQUIRED;
		Episode.m_HarmCause = EImpactHarmCause::SELF_CAUSED;
		Episode.m_HarmCausalConfidence = ECausalConfidence::CONFIRMED;
	}
	return Episode;
}

TEST(ImpactFoundation, TeamZeroWithoutFinishIsNotFinishProof)
{
	CReplay Replay(3, TestContext());
	StartPair(Replay);
	const int64_t Tick = 300;
	const std::vector<SImpactGameplayFrame> Frames{
		ImpactFrame(1, Tick, false),
		ImpactFrame(2, Tick, false),
	};
	EXPECT_FALSE(Replay.SubmitGameplay(Frames));
	EXPECT_FALSE(Replay.SubmitGameplay({}, {}));
	EXPECT_FALSE(Replay.Run().IsFinalized());
	EXPECT_EQ(Replay.Run().Result(), ERunResult::NONE);
}

TEST(ImpactLiveAdapter, TeamFinishWaitsForAllParticipantsAndUsesAuthoritativeTime)
{
	CReplay Replay(104, TestContext());
	StartPair(Replay);

	// One SV_RACEFINISH is only one participant's finish. The team run remains
	// open until the other eligible participant receives the server finish too.
	const int64_t FinishTick = 15550;
	const auto Frames = std::vector<SImpactGameplayFrame>{ImpactFrame(1, FinishTick, false), ImpactFrame(2, FinishTick, false)};
	EXPECT_FALSE(Replay.SubmitGameplay(Frames, {ImpactFinish(1, FinishTick, 1)}));
	EXPECT_FALSE(Replay.Run().IsFinalized());

	EXPECT_TRUE(Replay.SubmitGameplay(Frames, {ImpactFinish(2, FinishTick, 2)}));
	EXPECT_TRUE(Replay.Run().IsFinalized());
	EXPECT_EQ(Replay.Run().Result(), ERunResult::FINISHED);
	EXPECT_EQ(Replay.Run().AuthoritativeFinishTimeMillis(), 311000);
	EXPECT_EQ(Replay.Run().Snapshot(FinishTick).m_AuthoritativeFinishTimeMillis, 311000);
	EXPECT_EQ(Replay.Run().Snapshot(FinishTick).m_ActiveAnalysisTicks, FinishTick);
}

TEST(ImpactLiveAdapter, TeamFinishSurvivesTeamZeroRosterTransition)
{
	CReplay Replay(105, TestContext());
	StartPair(Replay);

	// This is the production ordering after DDNet sends the authoritative finish
	// messages: the current team roster can already be empty or partial because
	// the finished Tees have moved to Team 0. The phase2 observer retains the last
	// non-zero-team authoritative frames, but still requires both finish messages.
	const int64_t FinishTick = 16000;
	const auto Frames = std::vector<SImpactGameplayFrame>{ImpactFrame(1, FinishTick, false), ImpactFrame(2, FinishTick, false)};
	EXPECT_FALSE(Replay.SubmitGameplay(Frames));
	EXPECT_TRUE(Replay.SubmitGameplay({}, {ImpactFinish(1, FinishTick, 1), ImpactFinish(2, FinishTick, 2)}));
	EXPECT_TRUE(Replay.Run().IsFinalized());
	EXPECT_EQ(Replay.Run().Result(), ERunResult::FINISHED);
	EXPECT_EQ(Replay.Run().AuthoritativeFinishTimeMillis(), 320000);
	EXPECT_EQ(Replay.Run().Snapshot(20000).m_ActiveAnalysisTicks, 16000);

	// Post-finish lifecycle notifications cannot replace the terminal outcome.
	EXPECT_FALSE(Replay.Run().Finalize(ERunResult::TEAM_RESET, 20001));
	EXPECT_FALSE(Replay.Run().Finalize(ERunResult::ABORTED_CONNECTION, 20002));
	EXPECT_EQ(Replay.Run().Result(), ERunResult::FINISHED);
}

TEST(ImpactLiveAdapter, TeamRaceTimerStartsOnFirstPersonalStart)
{
	CReplay Replay(107, TestContext());
	const int TeamId = Replay.Run().Context().m_TeamId;
	STickEventBatch Batch;
	Batch.m_ServerTick = 0;
	Batch.m_vCandidates.push_back(StartEvent(1, TestId(1), 0, "A", TeamId));
	ASSERT_TRUE(Replay.Submit(Batch));

	// The team clock is already running while the second Tee has not taken
	// personal Start. Active analysis remains a separate, later lifecycle state.
	EXPECT_EQ(Replay.Run().TeamRaceTicks(2250), 2250);
	EXPECT_EQ(Replay.Run().TeamRaceSeconds(2250), 45);
	EXPECT_EQ(Replay.Run().ActiveAnalysisTicks(2250), 0);

	Batch = {};
	Batch.m_ServerTick = 2250;
	Batch.m_vCandidates.push_back(StartEvent(2, TestId(2), 2250, "B", TeamId));
	ASSERT_TRUE(Replay.Submit(Batch));
	EXPECT_EQ(Replay.Run().TeamRaceSeconds(2250), 45);

	const int64_t FinishTick = 15550;
	const auto Frames = std::vector<SImpactGameplayFrame>{ImpactFrame(1, FinishTick, false), ImpactFrame(2, FinishTick, false)};
	EXPECT_FALSE(Replay.SubmitGameplay(Frames, {ImpactFinish(1, FinishTick, 1)}));
	EXPECT_TRUE(Replay.SubmitGameplay(Frames, {ImpactFinish(2, FinishTick, 2)}));
	EXPECT_EQ(Replay.Run().AuthoritativeFinishTimeMillis(), 311000);
	EXPECT_EQ(Replay.Run().TeamRaceSeconds(FinishTick), 311);
	EXPECT_EQ(Replay.Run().Snapshot(FinishTick).m_AuthoritativeFinishTimeMillis, 311000);
}

TEST(ImpactLiveAdapter, FirstCurrentSnapshotIsPresentWithoutPrevious)
{
	SImpactGameplayInput Input;
	Input.m_Participant = TestId(1);
	Input.m_TeamId = 3;
	Input.m_ServerTick = 100;
	Input.m_Present = true;
	Input.m_CurrentCharacterKnown = true;
	Input.m_PreviousCharacterKnown = false;
	Input.m_PositionKnown = true;
	Input.m_AliveKnown = true;
	Input.m_Alive = true;
	Input.m_FreezeKnown = true;
	Input.m_Frozen = true;
	Input.m_SoloKnown = true;
	const SImpactGameplayFrame Frame = NormalizeGameplayFrame(Input);

	EXPECT_TRUE(Frame.m_Present);
	EXPECT_TRUE(Frame.m_CurrentCharacterKnown);
	EXPECT_FALSE(Frame.m_PreviousCharacterKnown);
	EXPECT_TRUE(Frame.m_PositionKnown);
	EXPECT_TRUE(Frame.m_AliveKnown);
	EXPECT_TRUE(Frame.m_Alive);
	EXPECT_TRUE(Frame.m_FreezeKnown);
	EXPECT_TRUE(Frame.m_Frozen);
	EXPECT_TRUE(Frame.m_Active);
}

TEST(ImpactLiveAdapter, FirstCurrentStartConditionIsOneShot)
{
	EXPECT_TRUE(CImpactSystem::IsStartObservationConfirmed(true, false, true, false, false));
	// A started participant is the run-scoped duplicate guard, even if the
	// current snapshot remains on the start tile or revisits it later.
	EXPECT_FALSE(CImpactSystem::IsStartObservationConfirmed(false, true, true, true, true));
	// A new slot generation has a new run participant context and can start.
	EXPECT_TRUE(CImpactSystem::IsStartObservationConfirmed(true, false, true, false, false));
	// A snapshot without a previous character may still use its current
	// authoritative start condition after team metadata becomes available.
	EXPECT_TRUE(CImpactSystem::IsStartObservationConfirmed(false, false, true, false, false));
}

TEST(ImpactLiveAdapter, ParticipantSlotObservationSurvivesSparseMetadata)
{
	// PlayerInfo, current Character and previous Character are independent
	// snapshot observations. Any one of them keeps the same slot observable;
	// only the absence of all three means that the slot is not observed.
	EXPECT_TRUE(CImpactSystem::IsParticipantSlotObserved(true, false, false));
	EXPECT_TRUE(CImpactSystem::IsParticipantSlotObserved(false, true, false));
	EXPECT_TRUE(CImpactSystem::IsParticipantSlotObserved(false, false, true));
	EXPECT_FALSE(CImpactSystem::IsParticipantSlotObserved(false, false, false));
}

TEST(ImpactLiveAdapter, OrdinaryRemoteStartOrderingKeepsDeferredPersonalStart)
{
	// Snapshot N: the remote character is already reliably visible on Start,
	// but DDNet team metadata has not arrived yet. Snapshot N+1 supplies the
	// same non-zero team while no new physical crossing is observed.
	EXPECT_TRUE(CImpactSystem::IsStartObservationConfirmed(true, false, true, false, false));
	EXPECT_TRUE(CImpactSystem::IsDeferredStartObservationConfirmed(true, true, false));
	EXPECT_FALSE(CImpactSystem::IsDeferredStartObservationConfirmed(true, false, false));
	// A single generation-scoped deferred observation is consumed once.
	EXPECT_FALSE(CImpactSystem::IsDeferredStartObservationConfirmed(true, true, true));
}

TEST(ImpactLiveAdapter, FiniteSafeFreezeIsSelfSafe)
{
	SImpactGameplayInput Input = ImpactInput(1, 100, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Input.m_Supported = true;
	Input.m_FreezeEnd = 200;
	Input.m_AutonomousRecoveryKnown = true;
	Input.m_AutonomousRecoveryPossible = true;
	EXPECT_EQ(ClassifyLiveRecoveryInput(Input, true), EImpactRecoveryNeed::SELF_SAFE);
	const SImpactGameplayFrame Frame = NormalizeGameplayFrame(Input);
	EXPECT_EQ(Frame.m_RecoveryNeed, EImpactRecoveryNeed::SELF_SAFE);
}

TEST(ImpactLiveAdapter, FiniteSafeFreezeWithoutAutonomousProofStaysUnknown)
{
	SImpactGameplayInput Input = ImpactInput(1, 100, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Input.m_Supported = true;
	Input.m_FreezeEnd = 200;
	EXPECT_EQ(ClassifyLiveRecoveryInput(Input, true), EImpactRecoveryNeed::UNKNOWN);
	EXPECT_EQ(NormalizeGameplayFrame(Input).m_RecoveryNeed, EImpactRecoveryNeed::UNKNOWN);
}

TEST(ImpactLiveAdapter, SafeDeepFreezeRequiresExternalRecovery)
{
	SImpactGameplayInput Input = ImpactInput(1, 100, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Input.m_Supported = true;
	Input.m_DeepFrozen = true;
	Input.m_FreezeEnd = 0;
	EXPECT_EQ(ClassifyLiveRecoveryInput(Input, true), EImpactRecoveryNeed::EXTERNAL_REQUIRED);
	const SImpactGameplayFrame Frame = NormalizeGameplayFrame(Input);
	EXPECT_EQ(Frame.m_RecoveryNeed, EImpactRecoveryNeed::EXTERNAL_REQUIRED);
}

TEST(ImpactLiveAdapter, UnknownFreezeMetadataStaysUnknown)
{
	SImpactGameplayInput Input = ImpactInput(1, 100, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Input.m_Supported = true;
	Input.m_DeepFrozen = true;
	Input.m_FreezeKnown = false;
	EXPECT_EQ(ClassifyLiveRecoveryInput(Input, true), EImpactRecoveryNeed::UNKNOWN);
	const SImpactGameplayFrame Frame = NormalizeGameplayFrame(Input);
	EXPECT_EQ(Frame.m_RecoveryNeed, EImpactRecoveryNeed::UNKNOWN);
}

TEST(ImpactLiveAdapter, DeepFrozenAloneDoesNotCreateFall)
{
	CReplay Replay(145, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_DeepFrozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	Target.m_SafeCollisionPositionKnown = false;
	Target.m_SafeCollisionPosition = false;
	EXPECT_EQ(CImpactPhase2::EvaluatePhysicalOutcome(NormalizeGameplayFrame(Target)), EImpactPhysicalOutcome::UNKNOWN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_EQ(Replay.Run().PendingEpisodes().front().m_PhysicalOutcome, EImpactPhysicalOutcome::UNKNOWN);
}

TEST(ImpactLiveAdapter, DeepFrozenOrdinaryPlatformIsNotAutomaticallySafe)
{
	CReplay Replay(146, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_DeepFrozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	EXPECT_EQ(CImpactPhase2::EvaluatePhysicalOutcome(NormalizeGameplayFrame(Target)), EImpactPhysicalOutcome::UNKNOWN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_EQ(Replay.Run().PendingEpisodes().front().m_PhysicalOutcome, EImpactPhysicalOutcome::UNKNOWN);

	Target.m_ServerTick = 120;
	Target.m_Frozen = true;
	Target.m_DeepFrozen = true;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_EQ(Replay.Run().PendingEpisodes().front().m_PhysicalOutcome, EImpactPhysicalOutcome::UNKNOWN);
}

TEST(ImpactLiveAdapter, DeepFrozenTrajectoryToRealUnfreezeDoesNotCreateFall)
{
	CReplay Replay(147, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_DeepFrozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	Target.m_SafeCollisionPositionKnown = false;
	Target.m_SafeCollisionPosition = false;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 120;
	Target.m_Frozen = false;
	Target.m_DeepFrozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	EXPECT_EQ(CImpactPhase2::EvaluatePhysicalOutcome(NormalizeGameplayFrame(Target)), EImpactPhysicalOutcome::SAFE);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	Target.m_ServerTick = 155;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_TRUE(Replay.Run().PendingEpisodes().empty());
}

TEST(ImpactLiveAdapter, NativeUnresolvedAirborneOutcomeDoesNotCreateFall)
{
	CReplay Replay(1471, TestContext());
	StartPair(Replay);

	SImpactGameplayFrame Saver = ImpactFrame(1, 90);
	SImpactGameplayFrame Target = ImpactFrame(2, 90);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	// This is the production shape after the read-only native shadow evaluator
	// has reached its natural-unfreeze boundary without a decisive safe terminal
	// fact. The evaluation is authoritative telemetry, but UNRESOLVED is neutral:
	// it must not be promoted to a harmful Fall by the generic unsafe-collision
	// fallback on the next frozen frame.
	Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	Target.m_Supported = false;
	Target.m_SupportKnown = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	Target.m_AutonomousRecoveryEvaluated = true;
	Target.m_AutonomousRecoveryKnown = false;
	Target.m_AutonomousRecoveryPossible = false;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	Target.m_ServerTick = 102;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_FALSE(Replay.Run().PendingEpisodes().front().m_FallAlreadyConfirmed);
}

TEST(ImpactLiveAdapter, AuthoritativeFreezeRefreshInvalidatesSafePlatformProof)
{
	CReplay Replay(1472, TestContext());
	StartPair(Replay);

	SImpactGameplayFrame Saver = ImpactFrame(1, 90);
	SImpactGameplayFrame Target = ImpactFrame(2, 90);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	// A supported safe-looking frame opens the problem episode only. It is not
	// enough to emit a Fall on entry and does not yet establish a refresh.
	Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	Target.m_Supported = true;
	Target.m_SupportKnown = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	Target.m_AutonomousRecoveryKnown = false;
	Target.m_AutonomousRecoveryPossible = false;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);

	// The authoritative freeze timer advances while the target remains on the
	// same supported position. This is positive active-freeze-source evidence,
	// not a sticky self-safe platform fact, and confirms exactly one Fall.
	Target.m_ServerTick = 110;
	Target.m_FreezeStart = 110;
	Target.m_FreezeEnd = 210;
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	Target.m_ServerTick = 120;
	Target.m_FreezeStart = 120;
	Target.m_FreezeEnd = 220;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
}

TEST(ImpactLiveAdapter, AuthoritativeFreezeRefreshSurvivesCharacterGap)
{
	CReplay Replay(1474, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 90);
	SImpactGameplayInput Target = ImpactInput(2, 90);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// The target enters an ordinary frozen problem. The native evaluator has
	// inspected the state but has no positive autonomous result.
	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	Target.m_Supported = false;
	Target.m_SupportKnown = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	Target.m_AutonomousRecoveryKnown = false;
	Target.m_AutonomousRecoveryPossible = false;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// The character object then becomes temporarily unavailable. This is not a
	// new episode and must not erase the last raw freeze lifecycle observation.
	Saver.m_ServerTick = 110;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, MissingCharacterInput(2, 110)}));

	// The target returns with the same identity and a newer authoritative
	// freeze start. Position/trajectory was not synthesized across the gap;
	// only the existing canonical freeze-refresh proof is carried forward.
	Saver.m_ServerTick = Target.m_ServerTick = 120;
	Target.m_FreezeStart = 110;
	Target.m_FreezeEnd = 210;
	Target.m_Supported = false;
	Target.m_SupportKnown = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	Target.m_AutonomousRecoveryKnown = false;
	Target.m_AutonomousRecoveryPossible = false;
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	// A later helper observation cannot be the thing that creates this Fall or
	// create a duplicate. Save attribution remains on its existing path.
	Saver.m_ServerTick = Target.m_ServerTick = 130;
	SetHooked(Saver, 2);
	Target.m_X = 32;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
}

TEST(ImpactLiveAdapter, AuthoritativeFreezeRefreshUsesRepeatedRawObjectTick)
{
	CReplay Replay(1475, TestContext());
	StartPair(Replay);

	SImpactGameplayFrame Saver = ImpactFrame(1, 90);
	SImpactGameplayFrame Target = ImpactFrame(2, 90);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	Target.m_Supported = false;
	Target.m_SupportKnown = true;
	Target.m_SafeCollisionPositionKnown = false;
	Target.m_AutonomousRecoveryEvaluated = true;
	Target.m_AutonomousRecoveryKnown = false;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	// The current character object remains the same raw object while the
	// authoritative snapshot advances and refreshes the freeze lifecycle. This
	// fact is enough to preserve the existing refresh proof, but no old position
	// or velocity is used as a new trajectory observation.
	Saver.m_ServerTick = Target.m_ServerTick = 120;
	Target.m_FreezeStart = 120;
	Target.m_FreezeEnd = 220;
	Target.m_AuthoritativePhysicalStateStale = true;
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	Target.m_ServerTick = 130;
	Target.m_FreezeStart = 130;
	Target.m_FreezeEnd = 230;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
}

TEST(ImpactLiveAdapter, PostHelperSelfSafeCannotErasePreHelperRefreshFall)
{
	CReplay Replay(1476, TestContext());
	StartPair(Replay);

	SImpactGameplayFrame Saver = ImpactFrame(1, 90);
	SImpactGameplayFrame Target = ImpactFrame(2, 90);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	// Before the helper acts, the native evaluator is explicitly unresolved. The
	// target is not supported and no positive autonomous-safe fact exists.
	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	Target.m_Supported = false;
	Target.m_SupportKnown = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	Target.m_AutonomousRecoveryEvaluated = true;
	Target.m_AutonomousRecoveryKnown = false;
	Target.m_AutonomousRecoveryPossible = false;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	// This authoritative refresh is pre-helper harmful evidence. The repeated
	// raw object tick models the stale physical object without treating its old
	// position or velocity as a new physical observation.
	Saver.m_ServerTick = Target.m_ServerTick = 120;
	Target.m_FreezeStart = 120;
	Target.m_FreezeEnd = 220;
	Target.m_AuthoritativePhysicalStateStale = true;
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 1);

	// The helper changes the target trajectory after the Fall is already
	// confirmed. Its later native SELF_SAFE/NATURAL_UNFREEZE result is therefore
	// post-action evidence and must not rewrite the original episode.
	Saver.m_ServerTick = Target.m_ServerTick = 130;
	SetHooked(Saver, 2);
	Target.m_X = 16;
	Target.m_AuthoritativePhysicalStateStale = false;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 140;
	Target.m_X = 24;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	Target.m_AutonomousRecoveryEvaluated = true;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = true;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_EQ(Replay.Run().PendingEpisodes().front().m_State, EImpactEpisodeState::CONFIRMED_FALL);
	EXPECT_TRUE(Replay.Run().PendingEpisodes().front().m_FallAlreadyConfirmed);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	// Keep the working release-before-unfreeze Save path in the scenario. The
	// valid Save, if confirmed, is independent of the already-confirmed Fall.
	Saver.m_ServerTick = Target.m_ServerTick = 145;
	ReleaseHook(Saver);
	Target.m_X = 28;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 150;
	Target.m_X = 32;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	Target.m_Supported = true;
	Target.m_SafeCollisionPosition = true;
	Target.m_AutonomousRecoveryEvaluated = false;
	Target.m_AutonomousRecoveryKnown = false;
	Target.m_AutonomousRecoveryPossible = false;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 185;
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::SAVE_CONFIRMED), 1);
}

TEST(ImpactLiveAdapter, UnresolvedSupportedPlatformDoesNotLatchSelfSafeAcrossRescue)
{
	CReplay Replay(1473, TestContext());
	StartPair(Replay);

	SImpactGameplayFrame Saver = ImpactFrame(1, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	SImpactGameplayFrame Target = ImpactFrame(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	// Open the same ordinary frozen problem as the live episode. The native
	// evaluator has inspected the state but has no positive autonomous result.
	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeKnown = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	Target.m_Supported = false;
	Target.m_SupportKnown = true;
	Target.m_SafeCollisionPosition = false;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_AutonomousRecoveryEvaluated = true;
	Target.m_AutonomousRecoveryKnown = false;
	Target.m_AutonomousRecoveryPossible = false;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	// Two supported frames with the same finite freeze deadline are still not a
	// positive SELF_SAFE result when the native evaluation is UNRESOLVED. This
	// mirrors the live supported=1 / MAX_SAFE_SIMULATION_BOUNDARY sequence.
	for(const int64_t Tick : {110LL, 120LL})
	{
		Saver.m_ServerTick = Target.m_ServerTick = Tick;
		Target.m_Frozen = true;
		Target.m_Supported = true;
		Target.m_SupportKnown = true;
		Target.m_SafeCollisionPositionKnown = true;
		Target.m_SafeCollisionPosition = true;
		Target.m_OrdinaryFreezeTileKnown = true;
		Target.m_OrdinaryFreezeTile = false;
		Target.m_DynamicSupportKnown = true;
		Target.m_DynamicSupport = false;
		Target.m_AutonomousRecoveryEvaluated = true;
		Target.m_AutonomousRecoveryKnown = false;
		Target.m_AutonomousRecoveryPossible = false;
		ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	}
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);

	// The first helper frame is observed while the weak supported baseline is still
	// visible. The interaction must not turn the unresolved native result into a
	// positive self-safe latch, but there is not yet harmful evidence on this frame.
	Saver.m_ServerTick = Target.m_ServerTick = 130;
	SetHooked(Saver, 2);
	Target.m_Frozen = true;
	Target.m_FreezeKnown = true;
	Target.m_Supported = true;
	Target.m_SupportKnown = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	Target.m_AutonomousRecoveryEvaluated = true;
	Target.m_AutonomousRecoveryKnown = false;
	Target.m_AutonomousRecoveryPossible = false;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);

	// Harmful evidence arrives only after the interaction is already recorded. The
	// authoritative refresh must clear the weak platform-only latch and confirm one
	// Fall; it must not be blocked by the existing Save interaction history.
	Saver.m_ServerTick = Target.m_ServerTick = 140;
	Target.m_Supported = false;
	Target.m_SupportKnown = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	Target.m_AutonomousRecoveryEvaluated = false;
	Target.m_FreezeStart = 140;
	Target.m_FreezeEnd = 240;
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	// A later refresh remains the same episode and cannot create a duplicate.
	Saver.m_ServerTick = Target.m_ServerTick = 150;
	Target.m_FreezeStart = 150;
	Target.m_FreezeEnd = 250;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
}

TEST(ImpactLiveAdapter, LiveTemporaryMapSupportDoesNotSuppressLaterHookRescue)
{
	CReplay Replay(164, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 64780, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	SImpactGameplayInput Target = ImpactInput(2, 64780, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// Live-order equivalent of the first observed freeze. There is no physical
	// harmful marker yet, so this is a pending episode rather than an immediate
	// Fall.
	Saver.m_ServerTick = Target.m_ServerTick = 64788;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 64788;
	Target.m_FreezeEnd = 65000;
	Target.m_Supported = false;
	Target.m_SafeCollisionPosition = false;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);

	// A brief map-ground contact while the target is still frozen is only an
	// observation. It must not close the episode or clear future interaction
	// evidence.
	Saver.m_ServerTick = Target.m_ServerTick = 64790;
	Target.m_Supported = true;
	Target.m_SafeCollisionPosition = true;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);

	// The contact is gone when the real rescue begins. The current authoritative
	// unsafe position establishes the independent Fall before the rescue; keep the
	// complete target-specific hook history across the sparse live snapshots.
	Saver.m_ServerTick = Target.m_ServerTick = 64860;
	Target.m_Supported = false;
	Target.m_SafeCollisionPosition = false;
	SetHooked(Saver, 2);
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_NE(FindDebugLabel(Replay.LastOutput(), "SAVE_INTERACTION"), nullptr);

	Saver.m_ServerTick = Target.m_ServerTick = 64884;
	Target.m_X = 16;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 64902;
	Target.m_X = 24;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// Unfreeze occurs while the hook is still active, before the authoritative
	// freeze expiry. The active span and the observed movement are captured at
	// the restoration boundary before any later release/cleanup.
	Saver.m_ServerTick = Target.m_ServerTick = 64920;
	Target.m_X = 32;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	Target.m_Supported = true;
	Target.m_SafeCollisionPosition = true;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_NE(FindDebugLabel(Replay.LastOutput(), "SAVE_RESTORE"), nullptr);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 1);
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 2U);
	const auto TemporarySupportSave = std::find_if(Replay.Run().PendingEpisodes().begin(), Replay.Run().PendingEpisodes().end(), [](const SImpactPendingEpisode &Episode) {
		return Episode.m_Type == EImpactEventType::SAVE_CONFIRMED;
	});
	ASSERT_NE(TemporarySupportSave, Replay.Run().PendingEpisodes().end());
	EXPECT_EQ(TemporarySupportSave->m_Actor, TestId(1));
	EXPECT_EQ(TemporarySupportSave->m_OwnershipStatus, EOwnershipStatus::ACTOR);
	EXPECT_TRUE(TemporarySupportSave->m_DecisivePhysicalEvidence);

	// Release after restoration must not erase the already captured causal
	// recovery evidence.
	Saver.m_ServerTick = Target.m_ServerTick = 64928;
	ReleaseHook(Saver);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 64955;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
}

TEST(ImpactLiveAdapter, LiveTemporarySupportKeepsConfirmedFallIndependentFromSave)
{
	CReplay Replay(165, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 64780, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	SImpactGameplayInput Target = ImpactInput(2, 64780, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 64788;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 64788;
	Target.m_FreezeEnd = 65000;
	Target.m_Supported = false;
	Target.m_SafeCollisionPosition = false;
	Target.m_OrdinaryFreezeTileKnown = true;
	Target.m_OrdinaryFreezeTile = true;
	SetPhysicalHarmfulOutcome(Target);
	// The entry snapshot contains an independent authoritative harmful marker,
	// but it only opens the active problem episode. The next authoritative frame
	// is the earliest Fall decision; later support cannot rewrite the origin.
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	Saver.m_ServerTick = Target.m_ServerTick = 64789;
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);

	// The later support snapshot is not allowed to rewrite the confirmed origin.
	Saver.m_ServerTick = Target.m_ServerTick = 64790;
	Target.m_Supported = true;
	Target.m_SafeCollisionPosition = true;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	ASSERT_EQ(Replay.Run().RawStats().m_Falls, 1);

	Saver.m_ServerTick = Target.m_ServerTick = 64860;
	Target.m_Supported = false;
	Target.m_SafeCollisionPosition = false;
	SetHooked(Saver, 2);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 64884;
	Target.m_X = 16;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 64902;
	Target.m_X = 24;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 64920;
	Target.m_X = 32;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	Target.m_Supported = true;
	Target.m_SafeCollisionPosition = true;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_NE(FindDebugLabel(Replay.LastOutput(), "SAVE_RESTORE"), nullptr);

	Saver.m_ServerTick = Target.m_ServerTick = 64928;
	ReleaseHook(Saver);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 64955;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::SAVE_CONFIRMED), 1);
}

TEST(ImpactLiveAdapter, OrdinaryFreezeStillFrozenAfterAuthoritativeExpiryConfirmsFall)
{
	CReplay Replay(166, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// A finite ordinary freeze is still an active problem while its authoritative
	// timer is running. Expiry alone is evidence, not a Fall producer: the target
	// can still recover through later autonomous physics.
	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_LiveFrozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 130;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(CImpactPhase2::EvaluatePhysicalOutcome(NormalizeGameplayFrame(Target)), EImpactPhysicalOutcome::UNKNOWN);

	Saver.m_ServerTick = Target.m_ServerTick = 120;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);

	Saver.m_ServerTick = Target.m_ServerTick = 131;
	EXPECT_EQ(CImpactPhase2::EvaluatePhysicalOutcome(NormalizeGameplayFrame(Target)), EImpactPhysicalOutcome::HARMFUL);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 0);
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_FALSE(Replay.Run().PendingEpisodes().front().m_FallAlreadyConfirmed);
}

TEST(ImpactLiveAdapter, SafePhysicalOutcomeWinsOverOrdinaryFreezeRefresh)
{
	CReplay Replay(1661, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// The target is on an ordinary freeze tile with a refreshed stationary timer,
	// but the authoritative input also proves an autonomous-safe outcome. The
	// positive SAFE producer must not be overwritten by the freeze-refresh probe.
	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	Target.m_Supported = true;
	Target.m_OrdinaryFreezeTileKnown = true;
	Target.m_OrdinaryFreezeTile = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = true;
	EXPECT_EQ(CImpactPhase2::EvaluatePhysicalOutcome(NormalizeGameplayFrame(Target)), EImpactPhysicalOutcome::SAFE);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 120;
	Target.m_FreezeStart = 120;
	Target.m_FreezeEnd = 220;
	EXPECT_EQ(CImpactPhase2::EvaluatePhysicalOutcome(NormalizeGameplayFrame(Target)), EImpactPhysicalOutcome::SAFE);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	// PhysicalOutcome is compatibility metadata only. Raw Fall uses the
	// autonomous-safe predicate directly and does not require a SAFE state slot.
	EXPECT_FALSE(Replay.Run().PendingEpisodes().front().m_FallAlreadyConfirmed);
}

TEST(ImpactLiveAdapter, Live103940ExpiryThenRecoveryProducesIndependentFallAndSave)
{
	CReplay Replay(167, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 103900, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	SImpactGameplayInput Target = ImpactInput(2, 103900, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 103940;
	Target.m_Frozen = true;
	Target.m_LiveFrozen = true;
	Target.m_FreezeStart = 103940;
	Target.m_FreezeEnd = 104090;
	Target.m_Supported = false;
	Target.m_OrdinaryFreezeTileKnown = true;
	Target.m_OrdinaryFreezeTile = true;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);

	// First span: the target is still frozen and the timer is still running.
	// This is interaction history, not yet a Fall or an autonomous-safe result.
	Saver.m_ServerTick = Target.m_ServerTick = 104072;
	Target.m_Supported = true;
	Target.m_SafeCollisionPosition = true;
	SetHooked(Saver, 2);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 104076;
	Target.m_X = 4;
	ReleaseHook(Saver);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// The live rehook at 104094 is after the authoritative 104090 expiry while
	// the target remains frozen. This is the independent physical HARMFUL proof;
	// it is not inferred from the later Save/restoration.
	Saver.m_ServerTick = Target.m_ServerTick = 104094;
	Target.m_Supported = false;
	Target.m_SafeCollisionPosition = false;
	Target.m_X = 8;
	SetHooked(Saver, 2);
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 1);

	// Preserve the real repeated hook/release ordering from the live episode.
	for(const auto &Span : {std::pair<int64_t, int64_t>{104104, 104116},
		std::pair<int64_t, int64_t>{104124, 104140},
		std::pair<int64_t, int64_t>{104150, 104166},
		std::pair<int64_t, int64_t>{104172, 104188}})
	{
		Saver.m_ServerTick = Target.m_ServerTick = Span.first;
		ReleaseHook(Saver);
		ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
		Saver.m_ServerTick = Target.m_ServerTick = Span.second;
		Target.m_X += 4;
		SetHooked(Saver, 2);
		ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	}

	// Last hook is active at the real FROZEN -> UNFROZEN boundary. The confirmed
	// Fall remains immutable while the same episode independently creates Save.
	Saver.m_ServerTick = Target.m_ServerTick = 104192;
	Target.m_X = 40;
	Target.m_Frozen = false;
	Target.m_LiveFrozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_NE(FindDebugLabel(Replay.LastOutput(), "SAVE_RESTORE"), nullptr);
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 2U);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	Saver.m_ServerTick = Target.m_ServerTick = 104196;
	ReleaseHook(Saver);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 104227;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::SAVE_CONFIRMED), 1);
}

TEST(ImpactLiveAdapter, SparseRecoveryConfirmationPrecedesNewFreeze)
{
	CReplay Replay(168, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 108000, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	SImpactGameplayInput Target = ImpactInput(2, 108000, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 108020;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 108020;
	Target.m_FreezeEnd = 108200;
	SetHooked(Saver, 2);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// The target-specific movement creates a causal early-unfreeze recovery, but
	// no intermediate confirmation frame is supplied after restoration. The
	// subsequent frozen unsafe frame has already established the independent Fall.
	Saver.m_ServerTick = Target.m_ServerTick = 108040;
	Target.m_X = 32;
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	Saver.m_ServerTick = Target.m_ServerTick = 108072;
	Target.m_Frozen = false;
	Target.m_LiveFrozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	ASSERT_EQ(Replay.Run().RawStats().m_Saves, 0);
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 2U);

	ReleaseHook(Saver);
	Saver.m_ServerTick = Target.m_ServerTick = 108074;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// This is the next authoritative frame in the sparse stream. 36 ticks have
	// elapsed since restore, so the already completed 35-tick confirmation must
	// win before the current independent freeze is rejected.
	Saver.m_ServerTick = Target.m_ServerTick = 108108;
	Target.m_Frozen = true;
	Target.m_LiveFrozen = true;
	Target.m_FreezeStart = 108108;
	Target.m_FreezeEnd = 108258;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::SAVE_CONFIRMED), 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
}

TEST(ImpactLiveAdapter, LiveDebug8RepeatedHookReleaseRehookRestoresAndConfirmsSave)
{
	CReplay Replay(169, TestContext());
	const SParticipantId SaverId = TestId(13);
	const SParticipantId TargetId = TestId(3);
	StartPairWithIds(Replay, SaverId, TargetId);

	SImpactGameplayInput Saver = ImpactInput(13, 5860, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	SImpactGameplayInput Target = ImpactInput(3, 5860, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// Production-order equivalent of the debug8 episode: the target remains
	// frozen while the same teammate creates several hook spans and the final
	// hook is still active when the authoritative unfreeze arrives at 6010.
	Saver.m_ServerTick = Target.m_ServerTick = 5868;
	Target.m_Frozen = true;
	Target.m_LiveFrozen = true;
	Target.m_FreezeStart = 5868;
	Target.m_FreezeEnd = 6100;
	SetHooked(Saver, 3);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 5880;
	Target.m_X = 4;
	Target.m_SafeCollisionPosition = true;
	ReleaseHook(Saver);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 5900;
	Target.m_X = 8;
	Target.m_SafeCollisionPosition = false;
	SetHooked(Saver, 3);
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	Saver.m_ServerTick = Target.m_ServerTick = 5920;
	Target.m_X = 12;
	ReleaseHook(Saver);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 5940;
	Target.m_X = 16;
	SetHooked(Saver, 3);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 5960;
	Target.m_X = 20;
	ReleaseHook(Saver);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 5980;
	Target.m_X = 24;
	SetHooked(Saver, 3);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 5992;
	Target.m_X = 28;
	ReleaseHook(Saver);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 6008;
	Target.m_X = 32;
	SetHooked(Saver, 3);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 6010;
	Target.m_X = 36;
	Target.m_Frozen = false;
	Target.m_LiveFrozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_NE(FindDebugLabel(Replay.LastOutput(), "SAVE_RESTORE"), nullptr);
	// The external rescue is independent of Save confirmation, but it does not
	// erase the victim's already established problem/Fall result.
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 1);
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 2U);
	const auto Debug8Save = std::find_if(Replay.Run().PendingEpisodes().begin(), Replay.Run().PendingEpisodes().end(), [](const SImpactPendingEpisode &Episode) {
		return Episode.m_Type == EImpactEventType::SAVE_CONFIRMED;
	});
	ASSERT_NE(Debug8Save, Replay.Run().PendingEpisodes().end());
	EXPECT_EQ(Debug8Save->m_Target, TargetId);
	EXPECT_EQ(Debug8Save->m_Actor, SaverId);
	EXPECT_EQ(Debug8Save->m_OwnershipStatus, EOwnershipStatus::ACTOR);
	EXPECT_TRUE(Debug8Save->m_DecisivePhysicalEvidence);

	// A release after the restoration boundary must not erase the captured
	// episode history. The 35-tick confirmation is intentionally sparse.
	Saver.m_ServerTick = Target.m_ServerTick = 6014;
	ReleaseHook(Saver);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 6045;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::SAVE_CONFIRMED), 1);
}

TEST(ImpactLiveAdapter, Debug9ForensicReplayKeepsLifecycleBoundaries)
{
	CReplay Replay(170, TestContext(5));
	StartPair(Replay, 224000, 224010);

	SImpactGameplayInput Actor = ImpactInput(1, 224100, false, true, true, 5, false, EImpactRecoveryNeed::UNKNOWN);
	SImpactGameplayInput Target = ImpactInput(2, 224100, false, true, true, 5, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Actor, Target}));

	// Episode 1 is the autonomous-safe negative from the forensic stream. The
	// first supported frozen frame creates only a pending episode; the repeated
	// authoritative support observation establishes the safe continuation, and
	// natural unfreeze closes it without a Fall or a recovery interaction.
	Actor.m_ServerTick = Target.m_ServerTick = 224120;
	Target = LiveSupportedUnknownFreeze(224120);
	Target.m_TeamId = 5;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Actor, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);

	Actor.m_ServerTick = Target.m_ServerTick = 224140;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Actor, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);

	Actor.m_ServerTick = Target.m_ServerTick = 224150;
	Target.m_Frozen = false;
	Target.m_LiveFrozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Actor, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	Actor.m_ServerTick = Target.m_ServerTick = 224185;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Actor, Target}));
	EXPECT_TRUE(Replay.Run().PendingEpisodes().empty());

	// Episode 2 is a causal Save discovered before the authoritative freeze
	// expiry. The target's own physical outcome remains UNKNOWN, so the rescue
	// creates a Save candidate without manufacturing a Fall.
	Actor = ImpactInput(1, 224200, false, true, true, 5, false, EImpactRecoveryNeed::UNKNOWN);
	Target = ImpactInput(2, 224200, false, true, true, 5, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Actor, Target}));

	Actor = ImpactInput(1, 224220, false, true, true, 5, false, EImpactRecoveryNeed::UNKNOWN);
	Target = LiveOrdinaryFrozenTarget(224220);
	Target.m_TeamId = 5;
	Target.m_X = 0;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Actor, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	const uint64_t FallEpisodeId = Replay.Run().PendingEpisodes().front().m_EpisodeId;

	Actor.m_ServerTick = Target.m_ServerTick = 224240;
	SetHooked(Actor, 2);
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Actor, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	Actor.m_ServerTick = Target.m_ServerTick = 224260;
	Target.m_X = 32;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	SetDynamicSupport(Target);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Actor, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 1);
	// Interaction history is collected while the target is still frozen. A
	// recovery record is not materialized until the later authoritative
	// restoration boundary.
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);

	// Release-before-unfreeze is part of the same proof. It must not erase the
	// bound recovery candidate or replace its actor with the target.
	Actor.m_ServerTick = Target.m_ServerTick = 224270;
	ReleaseHook(Actor);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Actor, Target}));

	Actor.m_ServerTick = Target.m_ServerTick = 224280;
	Target.m_Frozen = false;
	Target.m_LiveFrozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Actor, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 2U);
	const auto CandidateIt = std::find_if(Replay.Run().PendingEpisodes().begin(), Replay.Run().PendingEpisodes().end(), [](const SImpactPendingEpisode &Episode) {
		return Episode.m_Type == EImpactEventType::SAVE_CONFIRMED;
	});
	ASSERT_NE(CandidateIt, Replay.Run().PendingEpisodes().end());
	EXPECT_EQ(CandidateIt->m_RelatedFallEpisodeId, FallEpisodeId);
	EXPECT_EQ(CandidateIt->m_Actor, Actor.m_Participant);
	EXPECT_TRUE(CandidateIt->m_CausalRecoveryProof.m_Valid);
	EXPECT_EQ(CandidateIt->m_CausalRecoveryProof.m_FallEpisodeId, FallEpisodeId);
	EXPECT_EQ(CandidateIt->m_CausalRecoveryProof.m_RecoveryEpisodeId, CandidateIt->m_EpisodeId);
	EXPECT_LE(CandidateIt->m_StartTick, 224280);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
	EXPECT_NE(FindDebugLabel(Replay.LastOutput(), "SAVE_RESTORE"), nullptr);

	Actor.m_ServerTick = Target.m_ServerTick = 224315;
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Actor, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::SAVE_CONFIRMED), 1);
	const SImpactPhase2Debug *pFallCleanup = FindDebugLabel(Replay.LastOutput(), "FALL_EPISODE_CLOSED");
	ASSERT_NE(pFallCleanup, nullptr);
	EXPECT_EQ(pFallCleanup->m_EpisodeId, FallEpisodeId);
	EXPECT_TRUE(pFallCleanup->m_FallAlreadyConfirmed);
	EXPECT_TRUE(Replay.Run().PendingEpisodes().empty());

	// Episode 3 verifies the post-cleanup boundary. A new unresolved freeze has
	// a new episode identity and no stale proof/actor; a team reset then closes it
	// without manufacturing another Fall or Save.
	Actor = ImpactInput(1, 224400, false, true, true, 5, false, EImpactRecoveryNeed::UNKNOWN);
	Target = ImpactInput(2, 224400, false, true, true, 5, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Actor, Target}));

	Target = LiveOrdinaryFrozenTarget(224420);
	Target.m_TeamId = 5;
	Actor.m_ServerTick = Target.m_ServerTick = 224420;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Actor, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	const SImpactPendingEpisode &NewEpisode = Replay.Run().PendingEpisodes().front();
	EXPECT_NE(NewEpisode.m_EpisodeId, FallEpisodeId);
	EXPECT_FALSE(NewEpisode.m_CausalRecoveryProof.m_Valid);
	EXPECT_EQ(NewEpisode.m_RelatedRecoveryEpisodeId, 0U);
	EXPECT_EQ(NewEpisode.m_Actor, Target.m_Participant);

	SImpactGameplayMessage Reset;
	Reset.m_MessageId = 1;
	Reset.m_Type = EImpactGameplayMessageType::TEAM_RESET;
	Reset.m_ServerTick = 224430;
	Reset.m_TeamId = 5;
	Reset.m_First = 0;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Actor, Target}, {Reset}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_TRUE(Replay.Run().PendingEpisodes().empty());
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::SAVE_CONFIRMED), 1);
}

TEST(ImpactLiveAdapter, Debug10Episode5LateRehookAfterObservedExpiryCreatesSave)
{
	CReplay Replay(171, TestContext());
	StartPair(Replay, 315600, 315610);

	SImpactGameplayInput Actor = ImpactInput(1, 315620, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	SImpactGameplayInput Target = ImpactInput(2, 315620, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Actor, Target}));

	// Production-order equivalent of debug(10) episode 5. The target enters an
	// ordinary finite freeze and is still not on a safe collision position. The
	// first hook starts before the authoritative expiry, so its pre-action outcome
	// is genuinely UNKNOWN rather than a synthetic UNSAFE marker.
	Actor.m_ServerTick = Target.m_ServerTick = 315638;
	Target.m_Frozen = true;
	Target.m_LiveFrozen = true;
	Target.m_FreezeStart = 315638;
	Target.m_FreezeEnd = 315920;
	Target.m_Supported = false;
	Target.m_SafeCollisionPosition = false;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Actor, Target}));
	ASSERT_EQ(Replay.Run().RawStats().m_Falls, 0);

	Actor.m_ServerTick = Target.m_ServerTick = 315888;
	SetHooked(Actor, 2);
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Actor, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	Actor.m_ServerTick = Target.m_ServerTick = 315892;
	Target.m_X = 4;
	ReleaseHook(Actor);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Actor, Target}));

	// Rehook before expiry keeps the same episode-level interaction while
	// retaining the UNKNOWN pre-action baseline for the decisive span.
	Actor.m_ServerTick = Target.m_ServerTick = 315912;
	Target.m_X = 8;
	SetHooked(Actor, 2);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Actor, Target}));

	// Freeze expiry alone is not a Fall producer. It remains episode context while
	// the target is still observed; a later concrete self-recovery result may still
	// resolve this ordinary freeze without a raw Fall. The target then moves while
	// the second hook is active.
	Actor.m_ServerTick = Target.m_ServerTick = 315926;
	Target.m_X = 16;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Actor, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	Actor.m_ServerTick = Target.m_ServerTick = 315930;
	Target.m_X = 20;
	ReleaseHook(Actor);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Actor, Target}));

	// The hook is released before restoration. The saved post-expiry frozen
	// evidence must still allow the UNKNOWN pre-action interaction to materialize
	// a recovery candidate at the actual unfreeze boundary.
	Actor.m_ServerTick = Target.m_ServerTick = 315940;
	Target.m_X = 24;
	Target.m_Frozen = false;
	Target.m_LiveFrozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	Target.m_Supported = true;
	Target.m_SafeCollisionPosition = true;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Actor, Target}));
	EXPECT_NE(FindDebugLabel(Replay.LastOutput(), "SAVE_RESTORE"), nullptr);

	const auto CandidateIt = std::find_if(Replay.Run().PendingEpisodes().begin(), Replay.Run().PendingEpisodes().end(), [](const SImpactPendingEpisode &Episode) {
		return Episode.m_Type == EImpactEventType::SAVE_CONFIRMED;
	});
	ASSERT_NE(CandidateIt, Replay.Run().PendingEpisodes().end());
	EXPECT_EQ(CandidateIt->m_Actor, Actor.m_Participant);
	EXPECT_EQ(CandidateIt->m_CausalRecoveryProof.m_PreActionOutcome, EImpactPhysicalOutcome::UNKNOWN);
	EXPECT_TRUE(CandidateIt->m_CausalRecoveryProof.m_PhysicalChangeEvidence);
	EXPECT_TRUE(CandidateIt->m_CausalRecoveryProof.m_SafeOutcomeEvidence);
	EXPECT_EQ(CandidateIt->m_CausalRecoveryProof.m_FirstProvenTick, 315940);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);

	// Confirmation remains the existing authoritative 35-tick recovery rule;
	// the new evidence only fixes candidate creation before that boundary.
	Actor.m_ServerTick = Target.m_ServerTick = 315944;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Actor, Target}));
	Actor.m_ServerTick = Target.m_ServerTick = 315976;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Actor, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::SAVE_CONFIRMED), 1);
}

TEST(ImpactLiveAdapter, FrozenTargetFallsOntoOtherTeeAndNaturalUnfreezes_DynamicSupportDoesNotSuppressFall)
{
	CReplay Replay(152, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Support = ImpactInput(1, 90, false, true, true, 1);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Support, Target}));

	Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	SetDynamicSupport(Target, false);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Support, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);

	// Dynamic support from another Tee is external assistance, not autonomous
	// recovery. It therefore cannot suppress the already observable unsafe
	// problem state for the independent Fall resolver.
	Target.m_ServerTick = 110;
	SetDynamicSupport(Target);
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Support, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	// Dynamic support is an observed continuation while the target is still
	// frozen; it does not create a second Fall or change the existing result.
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);

	Target.m_ServerTick = 200;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	SetDynamicSupport(Target, false);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Support, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
}

TEST(ImpactLiveAdapter, FrozenTargetFallsOntoTower_NoFall)
{
	CReplay Replay(153, TestContext());
	StartPair(Replay);

	SImpactGameplayInput LowerTee = ImpactInput(1, 90);
	SImpactGameplayInput Target = ImpactInput(2, 90);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {LowerTee, Target}));

	Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	SetDynamicSupport(Target, false);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {LowerTee, Target}));

	// A tower is the same physical outcome as any other stable character
	// support; no tower-specific or map-ground detector is used.
	Target.m_ServerTick = 120;
	SetDynamicSupport(Target);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {LowerTee, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);

	Target.m_ServerTick = 200;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	SetDynamicSupport(Target, false);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {LowerTee, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
}

TEST(ImpactLiveAdapter, FrozenTargetFallsOntoBoat_NoFall)
{
	CReplay Replay(154, TestContext());
	StartPair(Replay);

	SImpactGameplayInput BoatTee = ImpactInput(1, 90);
	SImpactGameplayInput Target = ImpactInput(2, 90);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {BoatTee, Target}));

	Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	SetDynamicSupport(Target, false);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {BoatTee, Target}));

	// Boat support is deliberately not a second mechanic. The same observed
	// dynamic character support resolves the ordinary freeze safely.
	Target.m_ServerTick = 130;
	SetDynamicSupport(Target);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {BoatTee, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);

	Target.m_ServerTick = 200;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	SetDynamicSupport(Target, false);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {BoatTee, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
}

TEST(ImpactLiveAdapter, NearbyTeeWithoutPhysicalSupportDoesNotBecomeSafeOutcome)
{
	SImpactGameplayInput Target = ImpactInput(2, 100, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	Target.m_DynamicSupportKnown = true;
	Target.m_DynamicSupport = false;
	EXPECT_EQ(CImpactPhase2::EvaluatePhysicalOutcome(NormalizeGameplayFrame(Target)), EImpactPhysicalOutcome::UNKNOWN);
}

TEST(ImpactLiveAdapter, DynamicTeeSupportIsNotAutonomousSelfRecovery)
{
	CReplay Replay(158, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// The target enters an ordinary freeze with no autonomous-safe fact. A later
	// dynamic-support observation is physical contact supplied by another Tee,
	// not proof that this target can recover without that Tee.
	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	SetDynamicSupport(Target, false);
	// Freeze entry only opens the problem episode. The later authoritative
	// impossible-autonomous result is the physical Fall decision; dynamic Tee
	// support observed after that must not rewrite it as self recovery.
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 101;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	SetDynamicSupport(Target);
	Target.m_AutonomousRecoveryKnown = false;
	Target.m_AutonomousRecoveryPossible = false;
	EXPECT_EQ(CImpactPhase2::EvaluatePhysicalOutcome(NormalizeGameplayFrame(Target)), EImpactPhysicalOutcome::SAFE);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	// The later physical SAFE observation must not rewrite the already-decided
	// Fall-side origin. No Save interaction exists in this scenario.
	Saver.m_ServerTick = Target.m_ServerTick = 200;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	SetDynamicSupport(Target, false);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
}

TEST(ImpactLiveAdapter, DeepFrozenHeldByOtherTeeIsNotAutomaticallySafe)
{
	SImpactGameplayInput Target = ImpactInput(2, 100, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_DeepFrozen = true;
	Target.m_FreezeEnd = 0;
	SetDynamicSupport(Target);
	EXPECT_EQ(CImpactPhase2::EvaluatePhysicalOutcome(NormalizeGameplayFrame(Target)), EImpactPhysicalOutcome::UNKNOWN);
}

TEST(ImpactLiveAdapter, DeepFrozenHarmfulPhysicalOutcomeCreatesFall)
{
	CReplay Replay(148, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_DeepFrozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	SetPhysicalHarmfulOutcome(Target);
	SetSelfCausedHarm(Target);
	EXPECT_EQ(CImpactPhase2::EvaluatePhysicalOutcome(NormalizeGameplayFrame(Target)), EImpactPhysicalOutcome::HARMFUL);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target.m_ServerTick = 101;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_TRUE(Replay.Run().PendingEpisodes().front().m_FallAlreadyConfirmed);
}

TEST(ImpactLiveAdapter, PositionalTeleAllSafeOutputsResolveSafeOutcome)
{
	SImpactGameplayInput Input = ImpactInput(1, 100, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Input.m_TeleportType = EImpactPositionalTeleportType::NORMAL;
	Input.m_TeleportNumber = 3;
	Input.m_TeleportOutputSetKnown = true;
	Input.m_TeleportOutputSetAllSafe = true;
	EXPECT_EQ(CImpactPhase2::EvaluatePhysicalOutcome(NormalizeGameplayFrame(Input)), EImpactPhysicalOutcome::SAFE);
}

TEST(ImpactLiveAdapter, PositionalTeleMixedOutputsRemainUnknownBeforeTeleport)
{
	SImpactGameplayInput Input = ImpactInput(1, 100, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Input.m_TeleportType = EImpactPositionalTeleportType::EVIL;
	Input.m_TeleportNumber = 3;
	Input.m_TeleportOutputSetKnown = true;
	EXPECT_EQ(CImpactPhase2::EvaluatePhysicalOutcome(NormalizeGameplayFrame(Input)), EImpactPhysicalOutcome::UNKNOWN);
}

TEST(ImpactLiveAdapter, PositionalTeleAllHarmfulOutputsResolveHarmfulOutcome)
{
	SImpactGameplayInput Input = ImpactInput(1, 100, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Input.m_TeleportType = EImpactPositionalTeleportType::NORMAL;
	Input.m_TeleportNumber = 3;
	Input.m_TeleportOutputSetKnown = true;
	Input.m_TeleportOutputSetAllHarmful = true;
	EXPECT_EQ(CImpactPhase2::EvaluatePhysicalOutcome(NormalizeGameplayFrame(Input)), EImpactPhysicalOutcome::HARMFUL);
}

TEST(ImpactLiveAdapter, DeepFrozenTeleRequiresDeepUnfreezeOutput)
{
	SImpactGameplayInput Input = ImpactInput(1, 100, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Input.m_DeepFrozen = true;
	Input.m_TeleportType = EImpactPositionalTeleportType::NORMAL;
	Input.m_TeleportNumber = 3;
	Input.m_TeleportOutputSetKnown = true;
	Input.m_TeleportOutputSetAllSafe = true;
	EXPECT_EQ(CImpactPhase2::EvaluatePhysicalOutcome(NormalizeGameplayFrame(Input)), EImpactPhysicalOutcome::UNKNOWN);

	Input.m_TeleportOutputSetClearsDeepFreeze = true;
	EXPECT_EQ(CImpactPhase2::EvaluatePhysicalOutcome(NormalizeGameplayFrame(Input)), EImpactPhysicalOutcome::SAFE);
}

TEST(ImpactLiveAdapter, DeepFrozenResolvedOrdinaryTelePlatformIsNotSafe)
{
	SImpactGameplayInput Input = ImpactInput(1, 100, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Input.m_DeepFrozen = true;
	Input.m_TeleportType = EImpactPositionalTeleportType::EVIL;
	Input.m_TeleportNumber = 3;
	Input.m_TeleportDestinationKnown = true;
	Input.m_TeleportDestinationSafe = true;
	EXPECT_EQ(CImpactPhase2::EvaluatePhysicalOutcome(NormalizeGameplayFrame(Input)), EImpactPhysicalOutcome::UNKNOWN);

	Input.m_TeleportDestinationClearsDeepFreeze = true;
	EXPECT_EQ(CImpactPhase2::EvaluatePhysicalOutcome(NormalizeGameplayFrame(Input)), EImpactPhysicalOutcome::SAFE);
}

TEST(ImpactLiveAdapter, DeepFrozenUnsafeToRealUnfreezeCreatesSave)
{
	CReplay Replay(149, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 100;
	SetHooked(Saver, 2);
	Target.m_Frozen = true;
	Target.m_DeepFrozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	SetPhysicalHarmfulOutcome(Target);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_X = 32;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 120;
	Target.m_Frozen = false;
	Target.m_DeepFrozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	ClearPhysicalHarmfulOutcome(Target);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 155;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
}

TEST(ImpactLiveAdapter, UnsafeFrozenStateWithoutAutonomousProofStaysUnknown)
{
	SImpactGameplayInput Input = ImpactInput(1, 100, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Input.m_Supported = false;
	Input.m_SafeCollisionPositionKnown = true;
	Input.m_SafeCollisionPosition = false;
	EXPECT_EQ(ClassifyLiveRecoveryInput(Input, false), EImpactRecoveryNeed::UNKNOWN);
	const SImpactGameplayFrame Frame = NormalizeGameplayFrame(Input);
	EXPECT_EQ(Frame.m_RecoveryNeed, EImpactRecoveryNeed::UNKNOWN);
}

TEST(ImpactLiveAdapter, FrozenTeeLandsOnSafePlatform)
{
	CReplay Replay(140, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 110;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 110;
	Target.m_FreezeEnd = 200;
	// This fixture is the helpless external-recovery case. A supported safe
	// platform is autonomous evidence and would correctly resolve as NO_FALL.
	Target.m_Supported = false;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	Target.m_AutonomousRecoveryKnown = false;
	Target.m_AutonomousRecoveryPossible = false;
	EXPECT_EQ(ClassifyLiveRecoveryInput(Target, true), EImpactRecoveryNeed::UNKNOWN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);

	Target.m_ServerTick = 200;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	Target.m_RecoveryNeed = ClassifyLiveRecoveryInput(Target, true);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	Target.m_ServerTick = 235;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_TRUE(Replay.Run().PendingEpisodes().empty());
}

TEST(ImpactLiveAdapter, SafeFreezeDoesNotBecomeHarmfulFromLaterStateNoise)
{
	CReplay Replay(141, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target = LiveSupportedUnknownFreeze(110);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	// A single supported frozen snapshot is only an unresolved physical
	// observation. Keep the pending episode open until the actual outcome.
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);

	Target.m_ServerTick = 120;
	// RecoveryNeed is context only. Do not turn this fixture into an explicit
	// physical-harm producer: the target is still merely unresolved/frozen.
	Target.m_RecoveryNeed = EImpactRecoveryNeed::EXTERNAL_REQUIRED;
	EXPECT_EQ(Target.m_RecoveryNeed, EImpactRecoveryNeed::EXTERNAL_REQUIRED);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
}

TEST(ImpactLiveAdapter, RealExternalRequiredSelfFallConfirmsAtOrigin)
{
	CReplay Replay(142, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 110;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 110;
	Target.m_FreezeEnd = 200;
	// This fixture is the helpless external-recovery case. A supported safe
	// platform is autonomous evidence and would correctly resolve as NO_FALL.
	Target.m_Supported = false;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	EXPECT_EQ(ClassifyLiveRecoveryInput(Target, true), EImpactRecoveryNeed::EXTERNAL_REQUIRED);
	SetSelfCausedHarm(Target);
	// Explicit self-origin plus deterministic external-recovery context is a
	// positive Fall producer. Recovery, death, and reset are independent.
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target.m_ServerTick = 111;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_TRUE(Replay.Run().PendingEpisodes().front().m_FallAlreadyConfirmed);
}

TEST(ImpactLiveAdapter, SupportedDoesNotMeanSelfSafe)
{
	SImpactGameplayInput Input = ImpactInput(1, 100, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Input.m_Supported = true;
	Input.m_SafeCollisionPositionKnown = true;
	Input.m_SafeCollisionPosition = true;
	Input.m_AutonomousRecoveryKnown = true;
	Input.m_AutonomousRecoveryPossible = false;
	EXPECT_EQ(ClassifyLiveRecoveryInput(Input, true), EImpactRecoveryNeed::EXTERNAL_REQUIRED);
}

TEST(ImpactLiveAdapter, SupportedDoesNotMeanExternalRequired)
{
	SImpactGameplayInput Input = ImpactInput(1, 100, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Input.m_Supported = true;
	Input.m_SafeCollisionPositionKnown = true;
	Input.m_SafeCollisionPosition = true;
	Input.m_AutonomousRecoveryKnown = true;
	Input.m_AutonomousRecoveryPossible = true;
	EXPECT_EQ(ClassifyLiveRecoveryInput(Input, true), EImpactRecoveryNeed::SELF_SAFE);
}

TEST(ImpactLiveAdapter, AutonomousFreezeUnfreezeTraversalNoFall)
{
	CReplay Replay(143, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 110;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 110;
	Target.m_FreezeEnd = 200;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = true;
	Target.m_RecoveryNeed = ClassifyLiveRecoveryInput(Target, true);
	EXPECT_EQ(Target.m_RecoveryNeed, EImpactRecoveryNeed::SELF_SAFE);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 200;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	Target.m_RecoveryNeed = ClassifyLiveRecoveryInput(Target, true);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
}

TEST(ImpactLiveAdapter, UnfreezeTileAutonomousRecoveryNoFall)
{
	CReplay Replay(144, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 110;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 110;
	Target.m_FreezeEnd = 200;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = true;
	Target.m_RecoveryNeed = ClassifyLiveRecoveryInput(Target, true);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 200;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	Target.m_RecoveryNeed = ClassifyLiveRecoveryInput(Target, true);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
}

TEST(ImpactLiveAdapter, LiveSelfSafeHookDoesNotCreateSave)
{
	CReplay Replay(82, TestContext());
	StartPair(Replay);
	SImpactGameplayInput Saver = ImpactInput(1, 100);
	Saver.m_HookedPlayer = 2;
	Saver.m_HookState = HOOK_GRABBED;
	SetMeaningfulHookSupport(Saver);
	SImpactGameplayInput Target = ImpactInput(2, 100, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_Supported = true;
	Target.m_FreezeEnd = 110;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = true;
	EXPECT_EQ(ClassifyLiveRecoveryInput(Target, true), EImpactRecoveryNeed::SELF_SAFE);
	EXPECT_FALSE(Replay.SubmitGameplayInputs({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	EXPECT_FALSE(Replay.SubmitGameplayInputs({Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 145;
	EXPECT_FALSE(Replay.SubmitGameplayInputs({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_UpDowns, 0);
}

TEST(ImpactLiveAdapter, FrozenOnSafePlatformUnfreezeIsNotSave)
{
	CReplay Replay(91, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	Saver.m_Weapon = WEAPON_HAMMER;
	Saver.m_AttackTick = 100;
	Saver.m_HammerTargetEvidence = true;
	SImpactGameplayInput Target = ImpactInput(2, 100, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = true;
	Target.m_FreezeEnd = 200;
	Target.m_RecoveryNeed = ClassifyLiveRecoveryInput(Target, true);
	EXPECT_EQ(Target.m_RecoveryNeed, EImpactRecoveryNeed::SELF_SAFE);
	SubmitLiveEquivalentInputs(Replay, {Saver, Target});

	// An early hammer unfreeze does not become a Save when the target would
	// have recovered safely without teammate assistance.
	Saver.m_ServerTick = Target.m_ServerTick = 120;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 155;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_UpDowns, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
}

TEST(ImpactLiveAdapter, CurrentEpisodeSelfSafeVetoesSave)
{
	CReplay Replay(92, TestContext());
	StartPair(Replay);
	SImpactGameplayInput Saver = ImpactInput(1, 90);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// The hook is observed before Phase2 has the positive autonomous result. This
	// is the ordering that can leave an interaction's immutable pre-action fields
	// UNKNOWN even though the same freeze episode later proves SELF_SAFE.
	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	SetHooked(Saver, 2, true);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	ASSERT_EQ(Replay.Phase2().PendingEpisodes().size(), 1U);

	Saver.m_ServerTick = Target.m_ServerTick = 105;
	ReleaseHook(Saver);
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = true;
	Target.m_RecoveryNeed = EImpactRecoveryNeed::SELF_SAFE;
	std::vector<SImpactGameplayFrame> PositiveSafeFrames = {NormalizeGameplayFrame(Saver), NormalizeGameplayFrame(Target)};
	PositiveSafeFrames[1].m_AutonomousRecoveryEvaluated = true;
	ASSERT_FALSE(Replay.SubmitGameplay(PositiveSafeFrames));

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_X = 32;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_AutonomousRecoveryKnown = false;
	Target.m_AutonomousRecoveryPossible = false;
	Target.m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(std::count_if(Replay.Phase2().PendingEpisodes().begin(), Replay.Phase2().PendingEpisodes().end(), [](const SImpactPendingEpisode &Episode) {
		return Episode.m_Type == EImpactEventType::SAVE_CONFIRMED;
	}), 0);

	Saver.m_ServerTick = Target.m_ServerTick = 145;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::SAVE_CONFIRMED), 0);
}

TEST(ImpactLiveAdapter, OldInteractionFromAnotherEpisodeDoesNotAffectAttribution)
{
	CReplay Replay(94, TestContext());
	StartPair(Replay);
	SImpactGameplayInput Saver = ImpactInput(1, 90);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = true;
	Target.m_RecoveryNeed = EImpactRecoveryNeed::SELF_SAFE;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_AutonomousRecoveryKnown = false;
	Target.m_AutonomousRecoveryPossible = false;
	Target.m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 145;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// A new dangerous freeze is a different problem episode. The earlier
	// SELF_SAFE result must not suppress a genuine hook rescue now.
	Saver.m_ServerTick = Target.m_ServerTick = 150;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 150;
	Target.m_FreezeEnd = 250;
	Target.m_Supported = false;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	Target.m_RecoveryNeed = EImpactRecoveryNeed::EXTERNAL_REQUIRED;
	SetPhysicalHarmfulOutcome(Target);
	SetHooked(Saver, 2, true);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 170;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	ReleaseHook(Saver);
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);

	Saver.m_ServerTick = Target.m_ServerTick = 250;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_Supported = true;
	Target.m_SafeCollisionPosition = true;
	Target.m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 285;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::SAVE_CONFIRMED), 1);
}

TEST(ImpactLiveAdapter, RealHookThatChangesUnsafeOutcomeStillCountsSave)
{
	CReplay Replay(83, TestContext());
	StartPair(Replay);
	SImpactGameplayInput Saver = ImpactInput(1, 100);
	Saver.m_HookedPlayer = 2;
	Saver.m_HookState = HOOK_GRABBED;
	SetMeaningfulHookSupport(Saver);
	SImpactGameplayInput Target = ImpactInput(2, 100, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_Supported = false;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	Target.m_FreezeEnd = 110;
	EXPECT_EQ(ClassifyLiveRecoveryInput(Target, false), EImpactRecoveryNeed::EXTERNAL_REQUIRED);
	// The first frozen observation opens the episode; it is not the Fall decision.
	EXPECT_FALSE(Replay.SubmitGameplayInputs({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);

	Saver.m_ServerTick = Target.m_ServerTick = 105;
	// The target-specific hook is followed by an observable target movement;
	// this is the production physical evidence that makes the interaction a
	// decisive rescue candidate.
	Target.m_X = 32;
	// The explicit impossible autonomous outcome is an independent harmful
	// producer on this subsequent authoritative frozen frame. The recovery
	// candidate is still created by the Save path only at its safe boundary.
	EXPECT_TRUE(Replay.SubmitGameplayInputs({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	EXPECT_FALSE(Replay.SubmitGameplayInputs({Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 145;
	EXPECT_TRUE(Replay.SubmitGameplayInputs({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::SAVE_CONFIRMED), 1);
	EXPECT_EQ(Replay.Run().RawStats().m_UpDowns, 0);
}

TEST(ImpactLiveAdapter, DangerousFrozenRescueToSafePlatformStillCountsSave)
{
	CReplay Replay(165, TestContext());
	StartPair(Replay);
	SImpactGameplayInput Saver = ImpactInput(1, 90);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 100;
	SetHooked(Saver, 2, true);
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 250;
	Target.m_Supported = false;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	Target.m_OrdinaryFreezeTileKnown = true;
	Target.m_OrdinaryFreezeTile = true;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	SetPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 105;
	Target.m_X = 16;
	Target.m_VelX = 4;
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_X = 32;
	Target.m_Supported = true;
	Target.m_SafeCollisionPosition = true;
	Target.m_OrdinaryFreezeTile = false;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 115;
	Target.m_X = 48;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_AutonomousRecoveryKnown = false;
	Target.m_AutonomousRecoveryPossible = false;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 150;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::SAVE_CONFIRMED), 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 1);
}

TEST(ImpactLiveAdapter, EndedSelfSafeOutcomeDoesNotSuppressLaterFrozenFall)
{
	CReplay Replay(166, TestContext());
	StartPair(Replay);
	SImpactGameplayInput Saver = ImpactInput(1, 90);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 250;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = true;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);

	// Same freeze episode, but the autonomous-safe outcome is now gone and a
	// positive harmful physical fact is present. This must re-open Fall analysis.
	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Supported = false;
	Target.m_SafeCollisionPosition = false;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	SetPhysicalHarmfulOutcome(Target);
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 1);
	EXPECT_NE(FindDebugLabel(Replay.LastOutput(), "SELF_SAFE_INVALIDATED"), nullptr);

	Saver.m_ServerTick = Target.m_ServerTick = 120;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 1);
}

TEST(ImpactLiveAdapter, SelfSafeInvalidationAloneDoesNotCreateFall)
{
	CReplay Replay(1662, TestContext());
	StartPair(Replay);
	SImpactGameplayInput Saver = ImpactInput(1, 90);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 250;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = true;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// The same episode has left its earlier SELF_SAFE state, but this frame has
	// no positive autonomous-harmful result. Generic unsafe/frozen collision
	// context must not turn proof invalidation into a Fall.
	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Supported = false;
	Target.m_SafeCollisionPosition = false;
	Target.m_AutonomousRecoveryKnown = false;
	Target.m_AutonomousRecoveryPossible = false;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_NE(FindDebugLabel(Replay.LastOutput(), "SELF_SAFE_INVALIDATED"), nullptr);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 0);
}

TEST(ImpactLiveAdapter, SelfSafeInvalidatedThenUnknownOutcomeRemainsNeutral)
{
	CReplay Replay(1663, TestContext());
	StartPair(Replay);
	SImpactGameplayInput Saver = ImpactInput(1, 90);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 250;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = true;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Supported = false;
	Target.m_SafeCollisionPosition = false;
	Target.m_AutonomousRecoveryKnown = false;
	Target.m_AutonomousRecoveryPossible = false;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 120;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 0);
}

TEST(ImpactLiveAdapter, SelfSafeInvalidatedThenNaturalUnfreezeDoesNotFall)
{
	CReplay Replay(1664, TestContext());
	StartPair(Replay);
	SImpactGameplayInput Saver = ImpactInput(1, 90);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 250;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = true;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Supported = false;
	Target.m_SafeCollisionPosition = false;
	Target.m_AutonomousRecoveryKnown = false;
	Target.m_AutonomousRecoveryPossible = false;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 120;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 160;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
}

TEST(ImpactLiveAdapter, SelfSafeInvalidatedThenRealHarmfulOutcomeCanStillFall)
{
	CReplay Replay(1665, TestContext());
	StartPair(Replay);
	SImpactGameplayInput Saver = ImpactInput(1, 90);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 300;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = true;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Supported = false;
	Target.m_SafeCollisionPosition = false;
	Target.m_AutonomousRecoveryKnown = false;
	Target.m_AutonomousRecoveryPossible = false;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);

	// A fresh authoritative autonomous-harmful result in a later frame is valid
	// evidence for this still-current problem episode and must remain countable.
	Saver.m_ServerTick = Target.m_ServerTick = 120;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	SetPhysicalHarmfulOutcome(Target);
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 1);
}

TEST(ImpactLiveAdapter, OtherPlayerCreatesSafeTrajectory_LateUnfreezerGetsNoSave)
{
	CReplay Replay(164, TestContext());
	StartPair(Replay);
	STickEventBatch StartThird;
	StartThird.m_ServerTick = 20;
	StartThird.m_vCandidates.push_back(StartEvent(3, TestId(3), 20, "Target"));
	ASSERT_TRUE(Replay.Submit(StartThird));

	SImpactGameplayInput PlayerA = ImpactInput(1, 90, false);
	SImpactGameplayInput PlayerB = ImpactInput(2, 90, false);
	SImpactGameplayInput Target = ImpactInput(3, 90, false);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {PlayerA, PlayerB, Target}));

	PlayerA.m_ServerTick = PlayerB.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 250;
	Target.m_Supported = false;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	Target.m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
	SetHooked(PlayerA, 3);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {PlayerA, PlayerB, Target}));

	// A changes the unsafe target's trajectory, then releases. This is the
	// causally relevant action; the resulting momentum continues independently.
	PlayerA.m_ServerTick = PlayerB.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_X = 16;
	Target.m_VelX = 8;
	ReleaseHook(PlayerA);
	SubmitLiveEquivalentInputs(Replay, {PlayerA, PlayerB, Target});

	// The existing native autonomous evaluator proves the A-created trajectory
	// will reach safety before B makes any target-specific contact.
	PlayerA.m_ServerTick = PlayerB.m_ServerTick = Target.m_ServerTick = 119;
	Target.m_X = 32;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = true;
	Target.m_RecoveryNeed = EImpactRecoveryNeed::SELF_SAFE;
	PlayerA.m_Eligible = Replay.Run().HasParticipant(PlayerA.m_Participant);
	PlayerB.m_Eligible = Replay.Run().HasParticipant(PlayerB.m_Participant);
	Target.m_Eligible = Replay.Run().HasParticipant(Target.m_Participant);
	std::vector<SImpactGameplayFrame> PreBFrames = {
		NormalizeGameplayFrame(PlayerA), NormalizeGameplayFrame(PlayerB), NormalizeGameplayFrame(Target)};
	PreBFrames[2].m_AutonomousRecoveryEvaluated = true;
	ASSERT_FALSE(Replay.SubmitGameplay(PreBFrames));

	// B's brief hook merely observes the already-safe trajectory. Position keeps
	// changing because of A's earlier impulse, but B does not alter that trajectory.
	PlayerA.m_ServerTick = PlayerB.m_ServerTick = Target.m_ServerTick = 120;
	Target.m_X = 40;
	Target.m_PreActionAutonomousSelfSafeTick = 119;
	SetHooked(PlayerB, 3);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {PlayerA, PlayerB, Target}));
	PlayerA.m_ServerTick = PlayerB.m_ServerTick = Target.m_ServerTick = 130;
	Target.m_X = 80;
	ReleaseHook(PlayerB);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {PlayerA, PlayerB, Target}));

	// B's hammer/unfreeze is only a late action. A has already produced the
	// successful trajectory; the target lands safely and then becomes unfrozen.
	PlayerA.m_ServerTick = PlayerB.m_ServerTick = Target.m_ServerTick = 140;
	Target.m_Supported = true;
	Target.m_SafeCollisionPosition = true;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {PlayerA, PlayerB, Target}));
	PlayerA.m_ServerTick = PlayerB.m_ServerTick = Target.m_ServerTick = 150;
	PlayerB.m_Weapon = WEAPON_HAMMER;
	PlayerB.m_AttackTick = 150;
	PlayerB.m_HammerTargetEvidence = true;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {PlayerA, PlayerB, Target}));

	PlayerA.m_ServerTick = PlayerB.m_ServerTick = Target.m_ServerTick = 185;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {PlayerA, PlayerB, Target}));
	EXPECT_EQ(Replay.Run().Participants()[0].m_RawStats.m_Saves, 1);
	EXPECT_EQ(Replay.Run().Participants()[1].m_RawStats.m_Saves, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
}

TEST(ImpactLiveAdapter, OrdinaryHookSaveWorksAgain)
{
	CReplay Replay(150, TestContext());
	StartPair(Replay);

	// This uses only fields that BuildGameplayFrames derives from the current
	// character snapshot and collision map. In particular, no synthetic
	// meaningful-support flag and no preclassified EXTERNAL_REQUIRED need are
	// supplied by the fixture.
	SImpactGameplayInput Saver = ImpactInput(1, 90, false, true, true, 1);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	Target.m_Supported = false;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	SetPhysicalHarmfulOutcome(Target);
	SetHooked(Saver, 2);
	// Entry only creates the active problem episode; a raw Fall needs the
	// subsequent physical decision and is independent of the later Save.
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// The target remains frozen while the episode-level hook span is closed.
	// Release timing is not a Save gate.
	Saver.m_ServerTick = Target.m_ServerTick = 120;
	ClearPhysicalHarmfulOutcome(Target);
	ReleaseHook(Saver);
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	Saver.m_ServerTick = Target.m_ServerTick = 200;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	ClearPhysicalHarmfulOutcome(Target);
	// The recovery record is created at restoration, but the raw event is emitted
	// only after the existing 35-tick confirmation window.
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	Saver.m_ServerTick = Target.m_ServerTick = 235;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_UpDowns, 0);
}

TEST(ImpactLiveAdapter, UnnecessaryHookOnAutonomousSafeTeeIsNotSave)
{
	CReplay Replay(89, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	Target.m_FreezeEnd = 200;
	// The live adapter supplies this positive native proof before the helper
	// action. Platform contact alone is intentionally not the Save veto.
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = true;
	EXPECT_EQ(ClassifyLiveRecoveryInput(Target, true), EImpactRecoveryNeed::SELF_SAFE);
	// Establish the same-generation unfrozen baseline so the following frozen
	// frame is processed as a real production-order freeze transition.
	Saver.m_ServerTick = Target.m_ServerTick = 90;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	EXPECT_FALSE(Replay.SubmitGameplayInputs({Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	Saver.m_HookedPlayer = 2;
	Saver.m_HookState = HOOK_GRABBED;
	Replay.SubmitGameplayInputs({Saver, Target});

	// The target is already on an observable safe platform before the hook. A
	// target-specific hook plus a position change is therefore unnecessary help.
	Saver.m_ServerTick = Target.m_ServerTick = 105;
	Target.m_X = 32;
	EXPECT_FALSE(Replay.SubmitGameplayInputs({Saver, Target}));
	// Keep the physical episode open until the actual restoration boundary.
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);

	Saver.m_ServerTick = Target.m_ServerTick = 200;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	EXPECT_FALSE(Replay.SubmitGameplayInputs({Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 235;
	EXPECT_FALSE(Replay.SubmitGameplayInputs({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_UpDowns, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
}

TEST(ImpactLiveAdapter, SafePlatformQuantizedMotionDoesNotCreateSave)
{
	CReplay Replay(93, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 90);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// Open the problem on a platform that was already safe immediately before the
	// freeze. The small coordinate change below models authoritative quantization
	// or an autonomous slide while the Tee remains on that same safe platform.
	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	// Model the positive autonomous proof that the production shadow evaluator
	// supplies for this already-safe platform case.
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = true;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_X = 1;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);

	// With the target still positively self-safe, this hook is unnecessary. The
	// movement after it is deliberately large enough to exercise the normal hook
	// physical-change path; it must not turn the old coordinate drift into a Save.
	Saver.m_ServerTick = Target.m_ServerTick = 120;
	SetHooked(Saver, 2);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 125;
	Target.m_X = 32;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 130;
	ReleaseHook(Saver);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 200;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 235;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
}

TEST(ImpactLiveAdapter, PriorFrozenSafePlatformFrameVetoesFirstHookSave)
{
	CReplay Replay(94, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 90);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// Entry is an unsafe frozen problem. It must not be turned into a Fall on the
	// same frame, and it must not inherit a platform that existed before this
	// problem episode.
	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	Target.m_Supported = false;
	Target.m_SafeCollisionPosition = false;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);

	// This is the live episode-6 ordering: a single authoritative frozen frame
	// already shows an independent safe platform immediately before the first
	// helper action. The frame is deliberately marked with known non-freeze-tile
	// facts so the baseline is positive rather than an UNKNOWN fallback.
	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	Target.m_OrdinaryFreezeTileKnown = true;
	Target.m_OrdinaryFreezeTile = false;
	Target.m_DynamicSupportKnown = true;
	Target.m_DynamicSupport = false;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);

	Saver.m_ServerTick = Target.m_ServerTick = 120;
	SetHooked(Saver, 2);
	Target.m_X = 2;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);

	// The target naturally unfreezes; the hook is intentionally useless and must
	// not become a Save merely because it preceded restoration.
	Saver.m_ServerTick = Target.m_ServerTick = 200;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	ReleaseHook(Saver);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 235;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
}

TEST(ImpactLiveAdapter, RefreshedFreezePlatformDoesNotVetoRealHookSave)
{
	CReplay Replay(97, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 90);
	SImpactGameplayInput Target = LiveOrdinaryFrozenTarget(90);
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// The problem is confirmed before any helper action. The later supported
	// frames are a platform contact whose freeze timer keeps being refreshed;
	// they are not positive autonomous natural-unfreeze proof.
	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target = LiveOrdinaryFrozenTarget(100);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 101;
	Target = LiveOrdinaryFrozenTarget(101);
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Supported = true;
	Target.m_SafeCollisionPosition = true;
	Target.m_FreezeStart = 110;
	Target.m_FreezeEnd = 210;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 120;
	Target.m_FreezeStart = 120;
	Target.m_FreezeEnd = 220;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// The first hook follows the refreshed platform frames. The old direct
	// platform baseline would veto this interaction; the Save-side path must
	// leave it eligible because no positive autonomous proof exists.
	Saver.m_ServerTick = Target.m_ServerTick = 130;
	SetHooked(Saver, 2);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 140;
	ReleaseHook(Saver);
	Target.m_Supported = false;
	Target.m_SafeCollisionPosition = false;
	Target.m_X = 32;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 200;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	Target.m_Supported = true;
	Target.m_SafeCollisionPosition = true;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 235;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
}

TEST(ImpactLiveAdapter, SubsequentFrozenAuthoritativeFrameConfirmsOrdinaryFall)
{
	CReplay Replay(90, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Actor = ImpactInput(1, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Actor, Target}));

	// The entry frame only opens the active problem episode.
	Actor.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Actor, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);

	// The next authoritative frame still shows the same target at a known unsafe
	// collision position. It establishes the independent Fall before any rescue;
	// the entry frame itself remains observation-only.
	Actor.m_ServerTick = Target.m_ServerTick = 101;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Actor, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_TRUE(Replay.Run().PendingEpisodes().front().m_FallAlreadyConfirmed);
	// The active episode itself is the lifecycle record. Debug output is
	// observational and is not required to emit a per-frame trace.

	// An explicit autonomous=false marker is corroborating context only; it cannot
	// create a duplicate Fall after the current unsafe frame already did so.
	Actor.m_ServerTick = Target.m_ServerTick = 102;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Actor, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_TRUE(Replay.Run().PendingEpisodes().front().m_FallAlreadyConfirmed);
}

TEST(ImpactLiveAdapter, ExternalInteractionAfterUnresolvedDoesNotCreateFall)
{
	CReplay Replay(190, TestContext());
	StartPair(Replay);

	SImpactGameplayFrame Helper = ImpactFrame(1, 90);
	SImpactGameplayFrame Target = AmbiguousFrozenFrame(2, 90, 100);
	ASSERT_FALSE(Replay.SubmitGameplay({Helper, ImpactFrame(2, 90)}));

	Helper.m_ServerTick = Target.m_ServerTick = 100;
	Target = AmbiguousFrozenFrame(2, 100, 100);
	ASSERT_FALSE(Replay.SubmitGameplay({Helper, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);

	// The native unresolved result was evaluated before the helper appeared.
	Helper.m_ServerTick = Target.m_ServerTick = 101;
	Target = AmbiguousFrozenFrame(2, 101, 100);
	Target.m_AutonomousRecoveryEvaluated = true;
	ASSERT_FALSE(Replay.SubmitGameplay({Helper, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);

	// The helper makes the current shadow path unavailable. That post-action
	// state is not a pre-action harmful proof.
	Helper.m_ServerTick = Target.m_ServerTick = 102;
	SetHooked(Helper, 2);
	Target = AmbiguousFrozenFrame(2, 102, 100);
	EXPECT_FALSE(Replay.SubmitGameplay({Helper, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
}

TEST(ImpactLiveAdapter, SameTickSafePlatformAmbiguityDoesNotCreateFall)
{
	CReplay Replay(191, TestContext());
	StartPair(Replay);

	SImpactGameplayFrame Helper = ImpactFrame(1, 90);
	SImpactGameplayFrame Target = ImpactFrame(2, 90);
	ASSERT_FALSE(Replay.SubmitGameplay({Helper, Target}));

	Helper.m_ServerTick = Target.m_ServerTick = 100;
	Target = AmbiguousFrozenFrame(2, 100, 100);
	ASSERT_FALSE(Replay.SubmitGameplay({Helper, Target}));

	// Support first appears in the same observed frame as the helper. No
	// intra-tick ordering or platform causality is available to this fixture.
	Helper.m_ServerTick = Target.m_ServerTick = 101;
	SetHooked(Helper, 2);
	Target = ImpactFrame(2, 101, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	Target.m_OrdinaryFreezeTileKnown = true;
	Target.m_OrdinaryFreezeTile = false;
	Target.m_DynamicSupportKnown = true;
	Target.m_DynamicSupport = false;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	EXPECT_FALSE(Replay.SubmitGameplay({Helper, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);

	// The later unsupported frame is post-action trajectory, not proof that the
	// pre-action same-tick platform was harmful.
	Helper.m_ServerTick = Target.m_ServerTick = 102;
	ReleaseHook(Helper);
	Target = AmbiguousFrozenFrame(2, 102, 100);
	EXPECT_FALSE(Replay.SubmitGameplay({Helper, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
}

TEST(ImpactLiveAdapter, AuthoritativeFreezeRefreshStillConfirmsFallAfterUnresolved)
{
	CReplay Replay(192, TestContext());
	StartPair(Replay);

	SImpactGameplayFrame Helper = ImpactFrame(1, 90);
	SImpactGameplayFrame Target = ImpactFrame(2, 90);
	ASSERT_FALSE(Replay.SubmitGameplay({Helper, Target}));

	Helper.m_ServerTick = Target.m_ServerTick = 100;
	Target = AmbiguousFrozenFrame(2, 100, 100);
	ASSERT_FALSE(Replay.SubmitGameplay({Helper, Target}));

	// The freeze lifecycle advances before any helper action. This is the
	// canonical independent Fall proof, even though the native result remains
	// unresolved.
	Helper.m_ServerTick = Target.m_ServerTick = 101;
	Target = ImpactFrame(2, 101, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	Target.m_OrdinaryFreezeTileKnown = true;
	Target.m_OrdinaryFreezeTile = false;
	Target.m_FreezeStart = 101;
	Target.m_FreezeEnd = 201;
	EXPECT_TRUE(Replay.SubmitGameplay({Helper, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	Helper.m_ServerTick = Target.m_ServerTick = 102;
	SetHooked(Helper, 2);
	Target.m_ServerTick = 102;
	EXPECT_FALSE(Replay.SubmitGameplay({Helper, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 1);
}

TEST(ImpactLiveAdapter, PositiveHarmfulAutonomousFactSurvivesLaterHelper)
{
	CReplay Replay(193, TestContext());
	StartPair(Replay);

	SImpactGameplayFrame Helper = ImpactFrame(1, 90);
	SImpactGameplayFrame Target = ImpactFrame(2, 90);
	ASSERT_FALSE(Replay.SubmitGameplay({Helper, Target}));

	Helper.m_ServerTick = Target.m_ServerTick = 100;
	Target = AmbiguousFrozenFrame(2, 100, 100);
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	ASSERT_FALSE(Replay.SubmitGameplay({Helper, Target}));

	Helper.m_ServerTick = Target.m_ServerTick = 101;
	Target = AmbiguousFrozenFrame(2, 101, 100);
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	EXPECT_TRUE(Replay.SubmitGameplay({Helper, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	Helper.m_ServerTick = Target.m_ServerTick = 102;
	SetHooked(Helper, 2);
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	EXPECT_FALSE(Replay.SubmitGameplay({Helper, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 1);
}

TEST(ImpactLiveAdapter, GenuinePreHelperSelfSafeRemainsFallFree)
{
	CReplay Replay(194, TestContext());
	StartPair(Replay);

	SImpactGameplayFrame Helper = ImpactFrame(1, 90);
	SImpactGameplayFrame Target = ImpactFrame(2, 90);
	ASSERT_FALSE(Replay.SubmitGameplay({Helper, Target}));

	Helper.m_ServerTick = Target.m_ServerTick = 100;
	Target = ImpactFrame(2, 100, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	Target.m_OrdinaryFreezeTileKnown = true;
	Target.m_OrdinaryFreezeTile = false;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	ASSERT_FALSE(Replay.SubmitGameplay({Helper, Target}));

	Helper.m_ServerTick = Target.m_ServerTick = 101;
	Target.m_ServerTick = 101;
	EXPECT_FALSE(Replay.SubmitGameplay({Helper, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);

	// The helper is unnecessary because the positive autonomous proof existed
	// before it. The episode must remain Fall-free through the interaction.
	Helper.m_ServerTick = Target.m_ServerTick = 102;
	SetHooked(Helper, 2);
	EXPECT_FALSE(Replay.SubmitGameplay({Helper, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);

	Helper.m_ServerTick = Target.m_ServerTick = 103;
	ReleaseHook(Helper);
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	EXPECT_FALSE(Replay.SubmitGameplay({Helper, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
}

TEST(ImpactLiveAdapter, SaveCanConfirmWithoutFallProof)
{
	CReplay Replay(195, TestContext());
	StartPair(Replay);

	SImpactGameplayFrame Helper = ImpactFrame(1, 90);
	SImpactGameplayFrame Target = ImpactFrame(2, 90);
	ASSERT_FALSE(Replay.SubmitGameplay({Helper, Target}));

	Helper.m_ServerTick = Target.m_ServerTick = 100;
	SetHooked(Helper, 2);
	Target = AmbiguousFrozenFrame(2, 100, 100);
	ASSERT_FALSE(Replay.SubmitGameplay({Helper, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);

	Helper.m_ServerTick = Target.m_ServerTick = 110;
	ReleaseHook(Helper);
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	Target.m_Supported = true;
	Target.m_SafeCollisionPosition = true;
	EXPECT_FALSE(Replay.SubmitGameplay({Helper, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);

	Helper.m_ServerTick = Target.m_ServerTick = 145;
	EXPECT_TRUE(Replay.SubmitGameplay({Helper, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
}

// These two fixtures intentionally enter the current Save path before any
// independent Fall proof is available. They are the fast-rescue regression:
// the target is frozen in an unresolved/unsafe observation, the target-specific
// action changes it while frozen, and the existing 35-tick Save confirmation
// succeeds. The action is not allowed to manufacture a Fall, but a genuinely
// rescued dangerous episode must not be silently closed as Fall=0 either.
static std::pair<int, int> RunFastExternalRescueWithoutIndependentFallProof(uint64_t RunId, bool Hammer)
{
	CReplay Replay(RunId, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Helper = ImpactInput(1, 90);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Helper, Target}));

	// The first frozen frame is the real problem entry. Native evaluation is
	// explicitly present but unresolved; no harmful result and no SELF_SAFE
	// result are supplied.
	Helper.m_ServerTick = Target.m_ServerTick = 100;
	Target = LiveOrdinaryFrozenTarget(100);
	SImpactGameplayFrame HelperFrame = NormalizeGameplayFrame(Helper);
	SImpactGameplayFrame TargetFrame = NormalizeGameplayFrame(Target);
	TargetFrame.m_AutonomousRecoveryEvaluated = true;
	EXPECT_FALSE(Replay.SubmitGameplay({HelperFrame, TargetFrame}));

	// The helper arrives one authoritative observation after the unresolved
	// native result. This preserves a strict pre-action evaluation baseline.
	Helper.m_ServerTick = Target.m_ServerTick = 101;
	if(Hammer)
	{
		Helper.m_HammerTargetEvidenceKnown = true;
		Helper.m_HammerTargetEvidence = true;
		Helper.m_Weapon = WEAPON_HAMMER;
	}
	else
		SetHooked(Helper, 2);
	HelperFrame = NormalizeGameplayFrame(Helper);
	TargetFrame = NormalizeGameplayFrame(Target);
	EXPECT_FALSE(Replay.SubmitGameplay({HelperFrame, TargetFrame}));

	// The helper is intentionally immediate. This is physical target movement
	// while the target is still frozen, not a post-unfreeze movement heuristic.
	Helper.m_ServerTick = Target.m_ServerTick = 102;
	Target.m_X = 32;
	HelperFrame = NormalizeGameplayFrame(Helper);
	TargetFrame = NormalizeGameplayFrame(Target);
	EXPECT_FALSE(Replay.SubmitGameplay({HelperFrame, TargetFrame}));

	// Existing restoration and 35-tick confirmation remain the only Save path.
	Helper.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	if(!Hammer)
		ReleaseHook(Helper);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Helper, Target}));

	Helper.m_ServerTick = Target.m_ServerTick = 145;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Helper, Target}));
	return {Replay.Run().RawStats().m_Falls, Replay.Run().RawStats().m_Saves};
}

TEST(ImpactLiveAdapter, FastRealFallImmediateHookSuccessfulSave)
{
	const auto Result = RunFastExternalRescueWithoutIndependentFallProof(196, false);
	// This is expected to fail on the pre-fix production path: Save confirms,
	// while CloseStableProblemEpisodes discards the still-unconfirmed Fall.
	EXPECT_EQ(Result.first, 1);
	EXPECT_EQ(Result.second, 1);
}

TEST(ImpactLiveAdapter, FastRealRescueStillCountsSave)
{
	const auto Result = RunFastExternalRescueWithoutIndependentFallProof(197, true);
	// The harness supplies exact target-specific hammer evidence; no proximity
	// or inferred actor attribution is involved in this regression.
	EXPECT_EQ(Result.first, 1);
	EXPECT_EQ(Result.second, 1);
}

TEST(ImpactLiveAdapter, PositiveSelfSafeProofSurvivesUnnecessaryHookMovement)
{
	CReplay Replay(96, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 90);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// The frozen entry opens the problem episode without deciding Fall.
	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	Target.m_Supported = false;
	Target.m_SafeCollisionPosition = false;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// This is the positive autonomous proof that the native shadow evaluator
	// supplies in the live path. The later helper must not erase it merely by
	// moving the target away from the platform.
	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Supported = true;
	Target.m_SafeCollisionPosition = true;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = true;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 120;
	SetHooked(Saver, 2);
	Target.m_Supported = false;
	Target.m_SafeCollisionPosition = false;
	Target.m_X = 32;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);

	Saver.m_ServerTick = Target.m_ServerTick = 130;
	ReleaseHook(Saver);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 200;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 235;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
}

TEST(ImpactLiveAdapter, OldSafeFrameDoesNotPoisonLaterProblemSaveBaseline)
{
	CReplay Replay(91, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// The target leaves the earlier safe map position before entering this new
	// freeze episode. That old frame must not become the first-action baseline.
	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	Target.m_SafeCollisionPosition = false;
	Target.m_X = 0;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 101;
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	// A later target-specific action changes the target onto a safe position.
	Saver.m_ServerTick = Target.m_ServerTick = 120;
	SetHooked(Saver, 2);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 130;
	Target.m_X = 32;
	Target.m_SafeCollisionPosition = true;
	ReleaseHook(Saver);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
	Saver.m_ServerTick = Target.m_ServerTick = 140;
	// Candidate creation is retained in the pending recovery ledger; it does not
	// emit a raw gameplay event until the 35-tick survival confirmation.
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);

	Saver.m_ServerTick = Target.m_ServerTick = 150;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 185;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
}

TEST(ImpactLiveAdapter, TransientSafePlatformDoesNotPoisonProblemRescue)
{
	CReplay Replay(95, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	SImpactGameplayInput Target = LiveOrdinaryFrozenTarget(90);
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// The first frozen frame is only the problem boundary.
	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target = LiveOrdinaryFrozenTarget(100);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);

	// A single supported/safe snapshot is transient evidence, not self-safe.
	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);

	// The stale-looking contact is still present when the first hook is seen,
	// but the frame before it has no continuous autonomous-safe history.
	Saver.m_ServerTick = Target.m_ServerTick = 120;
	SetHooked(Saver, 2);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// The target leaves the transient contact and the hook produces a real frozen
	// movement change. This is one continuous problem episode, not a new timeout.
	Saver.m_ServerTick = Target.m_ServerTick = 125;
	Target.m_Supported = false;
	Target.m_SafeCollisionPosition = false;
	Target.m_X = 32;
	// Once the target leaves the transient contact, this subsequent frozen frame
	// is the independent Fall decision point; no rescue/restoration is involved.
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	Saver.m_ServerTick = Target.m_ServerTick = 130;
	ReleaseHook(Saver);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// Restoration after the released hook creates only the independent Save
	// confirmation record; the Fall was already emitted from the frozen frame.
	Saver.m_ServerTick = Target.m_ServerTick = 140;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	Target.m_Supported = true;
	Target.m_SafeCollisionPosition = true;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);

	Saver.m_ServerTick = Target.m_ServerTick = 175;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
}

TEST(ImpactLiveAdapter, HookDisplacementFromProblemPositionToSafePlatformCreatesFallAndSave)
{
	CReplay Replay(92, TestContext());
	StartPair(Replay);

	// The saver is on a safe platform; the target starts outside it in a
	// position from which autonomous recovery is not proven.
	SImpactGameplayInput Saver = ImpactInput(1, 100, false, true, true, 1);
	Saver.m_Supported = true;
	Saver.m_SafeCollisionPositionKnown = true;
	Saver.m_SafeCollisionPosition = true;
	Saver.m_HookedPlayer = 2;
	Saver.m_HookState = HOOK_GRABBED;
	Saver.m_HookKnown = true;

	SImpactGameplayInput Target = ImpactInput(2, 100, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_X = 0;
	Target.m_Y = 0;
	Target.m_Supported = false;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	Target.m_FreezeEnd = 200;
	Target.m_RecoveryNeed = ClassifyLiveRecoveryInput(Target, false);
	EXPECT_EQ(Target.m_RecoveryNeed, EImpactRecoveryNeed::EXTERNAL_REQUIRED);

	// Establish the authoritative unfrozen baseline before the problem entry.
	// The first frozen frame opens the episode; even an explicit later physical
	// marker is evaluated only on a subsequent frozen observation.
	Saver.m_HookedPlayer = -1;
	Saver.m_HookState = HOOK_RETRACTED;
	Saver.m_ServerTick = Target.m_ServerTick = 99;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	SetHooked(Saver, 2);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);

	// The next frozen frame carries the concrete impossible-autonomous marker,
	// which is the independent Fall producer for this regression.
	Saver.m_ServerTick = Target.m_ServerTick = 101;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	// The same frozen episode is moved onto the platform by the target-specific
	// hook. The later supported state must not erase the pre-recovery need.
	Saver.m_ServerTick = Target.m_ServerTick = 105;
	Target.m_X = 32;
	Target.m_Supported = true;
	Target.m_SafeCollisionPosition = true;
	// The post-pull frame may independently prove that the target is now
	// self-recoverable. That must not erase the pre-recovery external-need
	// context that already established the ordinary hook Save.
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = true;
	Target.m_RecoveryNeed = ClassifyLiveRecoveryInput(Target, true);
	EXPECT_EQ(Target.m_RecoveryNeed, EImpactRecoveryNeed::SELF_SAFE);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Saver.m_HookedPlayer = -1;
	Saver.m_HookState = HOOK_RETRACTED;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 200;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_RecoveryNeed = ClassifyLiveRecoveryInput(Target, true);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 235;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_UpDowns, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
}

TEST(ImpactLiveAdapter, DirectFallResultIsIndependentOfLaterRescue)
{
	CReplay NoRescue(220, TestContext());
	CReplay Rescued(221, TestContext());
	StartPair(NoRescue);
	StartPair(Rescued);

	SImpactGameplayInput NoRescueActor = ImpactInput(1, 90);
	SImpactGameplayInput NoRescueTarget = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	SImpactGameplayInput Rescuer = ImpactInput(1, 90);
	SImpactGameplayInput RescuedTarget = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(NoRescue, {NoRescueActor, NoRescueTarget}));
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Rescued, {Rescuer, RescuedTarget}));

	NoRescueActor.m_ServerTick = NoRescueTarget.m_ServerTick = 100;
	Rescuer.m_ServerTick = RescuedTarget.m_ServerTick = 100;
	NoRescueTarget = LiveOrdinaryFrozenTarget(100);
	RescuedTarget = LiveOrdinaryFrozenTarget(100);
	NoRescueTarget.m_FreezeEnd = RescuedTarget.m_FreezeEnd = 130;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(NoRescue, {NoRescueActor, NoRescueTarget}));
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Rescued, {Rescuer, RescuedTarget}));

	// Same physical sequence in both runs: an ordinary finite freeze is still
	// unresolved before its authoritative expiry. A concrete impossible-autonomous
	// marker is then observed in both runs; rescue presence must not affect the
	// independent Fall result.
	NoRescueActor.m_ServerTick = NoRescueTarget.m_ServerTick = 131;
	Rescuer.m_ServerTick = RescuedTarget.m_ServerTick = 131;
	NoRescueTarget.m_AutonomousRecoveryKnown = true;
	NoRescueTarget.m_AutonomousRecoveryPossible = false;
	RescuedTarget.m_AutonomousRecoveryKnown = true;
	RescuedTarget.m_AutonomousRecoveryPossible = false;
	ASSERT_TRUE(SubmitLiveEquivalentInputs(NoRescue, {NoRescueActor, NoRescueTarget}));
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Rescued, {Rescuer, RescuedTarget}));
	EXPECT_EQ(NoRescue.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(Rescued.Run().RawStats().m_Falls, 1);

	NoRescueActor.m_ServerTick = NoRescueTarget.m_ServerTick = 140;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(NoRescue, {NoRescueActor, NoRescueTarget}));
	EXPECT_EQ(NoRescue.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(NoRescue.Run().RawStats().m_Saves, 0);

	// The later hook creates a separate Save proof. It cannot create, remove, or
	// change the already-confirmed Fall; only its own restoration/survival path
	// contributes to Saves.
	Rescuer.m_ServerTick = RescuedTarget.m_ServerTick = 132;
	SetHooked(Rescuer, 2);
	RescuedTarget.m_X = 16;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Rescued, {Rescuer, RescuedTarget}));
	Rescuer.m_ServerTick = RescuedTarget.m_ServerTick = 133;
	RescuedTarget.m_X = 32;
	RescuedTarget.m_AutonomousRecoveryKnown = true;
	RescuedTarget.m_AutonomousRecoveryPossible = true;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Rescued, {Rescuer, RescuedTarget}));
	EXPECT_EQ(Rescued.Run().RawStats().m_Falls, 1);

	Rescuer.m_ServerTick = RescuedTarget.m_ServerTick = 140;
	ReleaseHook(Rescuer);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Rescued, {Rescuer, RescuedTarget}));
	Rescuer.m_ServerTick = RescuedTarget.m_ServerTick = 150;
	RescuedTarget.m_Frozen = false;
	RescuedTarget.m_LiveFrozen = false;
	RescuedTarget.m_FreezeStart = -1;
	RescuedTarget.m_FreezeEnd = 0;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Rescued, {Rescuer, RescuedTarget}));
	ASSERT_EQ(Rescued.Run().PendingEpisodes().size(), 2U);
	Rescuer.m_ServerTick = RescuedTarget.m_ServerTick = 185;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Rescued, {Rescuer, RescuedTarget}));
	EXPECT_EQ(Rescued.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(Rescued.Run().RawStats().m_Saves, 1);
}

TEST(ImpactLiveAdapter, TeammateCausedOriginDoesNotGateIndependentLaterSave)
{
	CReplay Replay(222, TestContext());
	StartPair(Replay);
	STickEventBatch ThirdStart;
	ThirdStart.m_ServerTick = 20;
	ThirdStart.m_vCandidates.push_back(StartEvent(3, TestId(3), 20, "third", Replay.Run().Context().m_TeamId));
	ASSERT_TRUE(Replay.Submit(ThirdStart));

	SImpactGameplayInput CausingActor = ImpactInput(1, 90);
	SImpactGameplayInput Rescuer = ImpactInput(3, 90);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_X = 0;
	SetHooked(CausingActor, 2);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {CausingActor, Rescuer, Target}));

	// The pre-freeze target-specific hook changes target position on the same
	// continuous transition into freeze. The cause remains neutral context while
	// the episode stays unresolved because no independent harmful proof exists.
	CausingActor.m_ServerTick = Rescuer.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	Target.m_X = 8;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {CausingActor, Rescuer, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_TRUE(Replay.Run().PendingEpisodes().front().m_OriginExternalCauseEvidence);

	// A different teammate performs a later target-specific rescue. The old
	// origin hook is released before this new action, so its movement cannot be
	// attributed to the rescuer or gate the independent Save decision.
	CausingActor.m_ServerTick = Rescuer.m_ServerTick = Target.m_ServerTick = 110;
	ReleaseHook(CausingActor);
	SetHooked(Rescuer, 2);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {CausingActor, Rescuer, Target}));
	CausingActor.m_ServerTick = Rescuer.m_ServerTick = Target.m_ServerTick = 120;
	Target.m_X = 32;
	SetDynamicSupport(Target);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {CausingActor, Rescuer, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 0);

	CausingActor.m_ServerTick = Rescuer.m_ServerTick = Target.m_ServerTick = 125;
	ReleaseHook(Rescuer);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {CausingActor, Rescuer, Target}));
	CausingActor.m_ServerTick = Rescuer.m_ServerTick = Target.m_ServerTick = 130;
	Target.m_Frozen = false;
	Target.m_LiveFrozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {CausingActor, Rescuer, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 2U);
	const auto SaveCandidate = std::find_if(Replay.Run().PendingEpisodes().begin(), Replay.Run().PendingEpisodes().end(), [](const SImpactPendingEpisode &Episode) {
		return Episode.m_Type == EImpactEventType::SAVE_CONFIRMED;
	});
	ASSERT_NE(SaveCandidate, Replay.Run().PendingEpisodes().end());
	EXPECT_EQ(SaveCandidate->m_OwnershipStatus, EOwnershipStatus::ACTOR);
	EXPECT_EQ(SaveCandidate->m_Actor, Rescuer.m_Participant);
	// The previously suppressed victim Fall episode remains available to the
	// existing association logic. Save confirmation itself remains independent;
	// the retained relation does not create a Fall without canonical evidence.
	EXPECT_NE(SaveCandidate->m_RelatedFallEpisodeId, 0U);
	EXPECT_NE(FindDebugLabel(Replay.LastOutput(), "SAVE_RESTORE"), nullptr);
	CausingActor.m_ServerTick = Rescuer.m_ServerTick = Target.m_ServerTick = 165;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {CausingActor, Rescuer, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
}

TEST(ImpactLiveAdapter, ConfirmedTeammateHarmDoesNotCancelIndependentSaveCandidate)
{
	CReplay Replay(224, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 90);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	SetHooked(Saver, 2);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// The target-specific action proves an independent Save while the current
	// unsafe frozen frame has already established the separate Fall.
	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_X = 32;
	SetDynamicSupport(Target);
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(Replay.Run().PendingEpisodes().size(), 1U);

	// A separate confirmed teammate-harm signal closes only the Fall origin
	// context. Recovery is still not materialized while the target is frozen,
	// and the interaction history must survive until restoration.
	Saver.m_ServerTick = Target.m_ServerTick = 115;
	const SImpactGameplayMessage Harm = ImpactSignal(1, EImpactGameplayMessageType::CONFIRMED_HARM, 115, TestId(1), TestId(2));
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}, {Harm}));

	Saver.m_ServerTick = Target.m_ServerTick = 120;
	ReleaseHook(Saver);
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	SetDynamicSupport(Target, false);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	ASSERT_EQ(std::count_if(Replay.Run().PendingEpisodes().begin(), Replay.Run().PendingEpisodes().end(), [](const SImpactPendingEpisode &Episode) {
		return Episode.m_Type == EImpactEventType::SAVE_CONFIRMED;
	}), 1);

	Saver.m_ServerTick = Target.m_ServerTick = 155;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::SAVE_CONFIRMED), 1);
}

TEST(ImpactLiveAdapter, ShortRefreezeKeepsProblemEpisodeAndStableRecoveryClosesIt)
{
	CReplay Replay(223, TestContext());
	StartPair(Replay);
	SImpactGameplayInput Saver = ImpactInput(1, 90);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target = LiveOrdinaryFrozenTarget(100);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	const uint64_t FirstProblemId = Replay.Run().PendingEpisodes().front().m_EpisodeId;

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_LiveFrozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 140;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// Refreeze arrives 30 ticks after recovery began, before the approved 35-tick
	// stable boundary. It stays in the same problem episode and cannot count a
	// second Fall merely because it crossed frozen again.
	Saver.m_ServerTick = Target.m_ServerTick = 142;
	Target = LiveOrdinaryFrozenTarget(142);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	ASSERT_EQ(Replay.Run().RawStats().m_Falls, 0);
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_EQ(Replay.Run().PendingEpisodes().front().m_EpisodeId, FirstProblemId);

	Saver.m_ServerTick = Target.m_ServerTick = 150;
	Target.m_Frozen = false;
	Target.m_LiveFrozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 185;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_TRUE(Replay.Run().PendingEpisodes().empty());

	Saver.m_ServerTick = Target.m_ServerTick = 200;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 210;
	Target = LiveOrdinaryFrozenTarget(210);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_NE(Replay.Run().PendingEpisodes().front().m_EpisodeId, FirstProblemId);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
}

TEST(ImpactLiveAdapter, StartedMultiAnchorHookSupportProducesSave)
{
	CReplay Replay(93, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100, false, true, true, 1);
	SImpactGameplayInput Target = ImpactInput(2, 100, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_Supported = false;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	Target.m_FreezeEnd = 200;
	Target.m_RecoveryNeed = ClassifyLiveRecoveryInput(Target, false);
	// The entry snapshot only opens the active problem episode. A subsequent
	// authoritative frozen problem observation with the explicit physical
	// helplessness marker is the earliest Fall decision.
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	Saver.m_ServerTick = Target.m_ServerTick = 101;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	// Three target-specific spans represent left-wall, right-wall and ceiling
	// rehooks. Release/re-hook changes are part of one freeze episode.
	Saver.m_HookedPlayer = 2;
	Saver.m_HookState = HOOK_GRABBED;
	SetMeaningfulHookSupport(Saver);
	Saver.m_ServerTick = Target.m_ServerTick = 105;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	Saver.m_HookedPlayer = -1;
	Saver.m_HookState = HOOK_RETRACTED;
	Saver.m_ServerTick = Target.m_ServerTick = 110;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_HookedPlayer = 2;
	Saver.m_HookState = HOOK_GRABBED;
	Saver.m_ServerTick = Target.m_ServerTick = 115;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_HookedPlayer = -1;
	Saver.m_HookState = HOOK_RETRACTED;
	Saver.m_ServerTick = Target.m_ServerTick = 120;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_HookedPlayer = 2;
	Saver.m_HookState = HOOK_GRABBED;
	Saver.m_ServerTick = Target.m_ServerTick = 125;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_HookedPlayer = -1;
	Saver.m_HookState = HOOK_RETRACTED;
	Saver.m_ServerTick = Target.m_ServerTick = 130;
	Target.m_X = 32;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 200;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_RecoveryNeed = ClassifyLiveRecoveryInput(Target, false);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 235;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::SAVE_CONFIRMED), 1);
}

TEST(ImpactLiveAdapter, UnknownPreActionOutcomeWithHookMovementCreatesSave)
{
	CReplay Replay(94, TestContext());
	StartPair(Replay);

	// UNKNOWN before the hook is still a valid pending state. The target-specific
	// hook and the later observed movement provide the physical Save evidence.
	SImpactGameplayInput Saver = ImpactInput(1, 100);
	Saver.m_HookedPlayer = 2;
	Saver.m_HookState = HOOK_GRABBED;
	SImpactGameplayInput Target = ImpactInput(2, 100, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_Supported = false;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	Target.m_FreezeEnd = 110;
	Target.m_RecoveryNeed = ClassifyLiveRecoveryInput(Target, false);
	EXPECT_EQ(Target.m_RecoveryNeed, EImpactRecoveryNeed::UNKNOWN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 105;
	Target.m_X = 32;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 145;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_UpDowns, 0);
}

TEST(ImpactLiveAdapter, UnknownPreActionButProvenCausalDynamicSupportCreatesSave)
{
	CReplay Replay(155, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 90);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// The target is known frozen, but its pre-action physical outcome is still
	// UNKNOWN. A target-specific hook is retained across the episode; no Save is
	// assigned merely for that interaction.
	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	SetHooked(Saver, 2);
	EXPECT_EQ(CImpactPhase2::EvaluatePhysicalOutcome(NormalizeGameplayFrame(Target)), EImpactPhysicalOutcome::UNKNOWN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// Later authoritative support/stabilization is rescue evidence, not autonomous
	// self-recovery: the target's current unsafe position already established Fall.
	// The hook has changed the target's physical position before that boundary.
	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_X = 32;
	SetDynamicSupport(Target);
	EXPECT_EQ(CImpactPhase2::EvaluatePhysicalOutcome(NormalizeGameplayFrame(Target)), EImpactPhysicalOutcome::SAFE);
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 1);

	Saver.m_ServerTick = Target.m_ServerTick = 200;
	ReleaseHook(Saver);
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	SetDynamicSupport(Target, false);
	// Restoration creates the pending Save record; no new raw event is emitted
	// until the existing confirmation window completes.
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 235;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
}

TEST(ImpactLiveAdapter, UnknownPreActionUnnecessaryHookOnSafeDynamicSupportDoesNotCreateSave)
{
	CReplay Replay(156, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 90);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// An explicit autonomous-safe result is observed before the hook. The later
	// movement change is therefore an unnecessary touch. Dynamic Tee support is
	// intentionally not used as autonomous self-recovery evidence.
	Saver.m_ServerTick = Target.m_ServerTick = 99;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 99;
	Target.m_FreezeEnd = 200;
	SetDynamicSupport(Target);
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = true;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 100;
	SetHooked(Saver, 2);
	EXPECT_EQ(CImpactPhase2::EvaluatePhysicalOutcome(NormalizeGameplayFrame(Target)), EImpactPhysicalOutcome::SAFE);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_X = 32;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 200;
	ReleaseHook(Saver);
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	SetDynamicSupport(Target, false);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 235;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
}

TEST(ImpactLiveAdapter, UnknownPreActionAlreadySafeTrajectoryDoesNotCreateSave)
{
	CReplay Replay(157, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	Target.m_Supported = true;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = true;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// The pre-action physical state already carries an authoritative autonomous
	// safe result. A later small movement change must not turn that continuation
	// into Save credit.
	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = true;
	SetHooked(Saver, 2);
	EXPECT_EQ(CImpactPhase2::EvaluatePhysicalOutcome(NormalizeGameplayFrame(Target)), EImpactPhysicalOutcome::SAFE);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_X = 32;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	EXPECT_EQ(CImpactPhase2::EvaluatePhysicalOutcome(NormalizeGameplayFrame(Target)), EImpactPhysicalOutcome::SAFE);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 200;
	ReleaseHook(Saver);
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 235;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
}

TEST(ImpactLiveAdapter, UnknownPreActionInteractionWithoutNecessaryOutcomeChangeDoesNotCreateSave)
{
	CReplay Replay(158, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	SetHooked(Saver, 2);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// No target movement is observed after the hook. The later natural unfreeze is
	// therefore not attributable to this interaction.
	Saver.m_ServerTick = Target.m_ServerTick = 110;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	Saver.m_ServerTick = Target.m_ServerTick = 200;
	ReleaseHook(Saver);
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 235;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
}

TEST(ImpactLiveAdapter, UnknownPreActionDecisiveTrajectoryChangeCanCreateSave)
{
	CReplay Replay(159, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	SetHooked(Saver, 2);
	EXPECT_EQ(CImpactPhase2::EvaluatePhysicalOutcome(NormalizeGameplayFrame(Target)), EImpactPhysicalOutcome::UNKNOWN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// The pre-action state is UNKNOWN, but the target-specific action is followed
	// by a physical movement change and an early, authoritative unfreeze. This is
	// a new recovery boundary, not merely a later generic SAFE snapshot.
	Saver.m_ServerTick = Target.m_ServerTick = 130;
	Target.m_X = 32;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	EXPECT_EQ(CImpactPhase2::EvaluatePhysicalOutcome(NormalizeGameplayFrame(Target)), EImpactPhysicalOutcome::SAFE);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 0);

	Saver.m_ServerTick = Target.m_ServerTick = 165;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
}

TEST(ImpactLiveAdapter, SingleActorIsNotAutomaticSaver)
{
	CReplay Replay(160, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	SetHooked(Saver, 2);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 200;
	ReleaseHook(Saver);
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 235;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
}

TEST(ImpactLiveAdapter, MovementChangeAloneIsNotEnoughForSave)
{
	CReplay Replay(161, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	SetHooked(Saver, 2);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 120;
	Target.m_X = 32;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	Saver.m_ServerTick = Target.m_ServerTick = 200;
	ReleaseHook(Saver);
	Target.m_Frozen = true;
	Target.m_FreezeEnd = 300;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 235;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
}

TEST(ImpactLiveAdapter, LaterSafeOutcomeAloneIsNotEnoughForSave)
{
	CReplay Replay(162, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	SetHooked(Saver, 2);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 150;
	Target.m_SafeCollisionPosition = true;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 200;
	ReleaseHook(Saver);
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 235;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
}

TEST(ImpactLiveAdapter, EarlierSafeTrajectoryLaterUnnecessaryTouchDoesNotTransferSave)
{
	CReplay Replay(163, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	SetHooked(Saver, 2);
	EXPECT_EQ(CImpactPhase2::EvaluatePhysicalOutcome(NormalizeGameplayFrame(Target)), EImpactPhysicalOutcome::UNKNOWN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 200;
	ReleaseHook(Saver);
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 235;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
}

TEST(ImpactLiveAdapter, OrdinaryTargetSpecificHookRecoveryCreatesSave)
{
	CReplay Replay(148, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target = LiveOrdinaryFrozenTarget(110);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// The practical baseline accepts a target-specific recovery interaction and
	// leaves any finer usefulness/quality interpretation to later phases.
	Saver.m_ServerTick = Target.m_ServerTick = 120;
	SetPhysicalHarmfulOutcome(Target);
	SetHooked(Saver, 2);
	// The subsequent authoritative problem frame establishes the independent
	// victim Fall. Save remains pending until the restoration confirmation.
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	Saver.m_ServerTick = Target.m_ServerTick = 130;
	ReleaseHook(Saver);
	Target.m_X = 32;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	ClearPhysicalHarmfulOutcome(Target);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	Saver.m_ServerTick = Target.m_ServerTick = 165;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
}

TEST(ImpactLiveAdapter, OrdinaryHookRecoveryStillCreatesSave)
{
	CReplay Replay(149, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target = LiveOrdinaryFrozenTarget(110);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// The target-specific hook and subsequent restoration are the practical
	// ordinary Save path. No strict material-usefulness flag is required.
	Saver.m_ServerTick = Target.m_ServerTick = 120;
	SetPhysicalHarmfulOutcome(Target);
	SetHooked(Saver, 2);
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	Saver.m_ServerTick = Target.m_ServerTick = 130;
	ReleaseHook(Saver);
	Target.m_X = 32;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 200;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	ClearPhysicalHarmfulOutcome(Target);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	Saver.m_ServerTick = Target.m_ServerTick = 235;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
}

TEST(ImpactLiveAdapter, DeathAloneDoesNotCreateFallFromPendingFreeze)
{
	CReplay Replay(150, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// No positive origin evidence is present. The freeze remains an unresolved
	// candidate; the later death closes it without manufacturing a Fall.
	Target = LiveOrdinaryFrozenTarget(110);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_EQ(Replay.Run().PendingEpisodes().front().m_HarmCause, EImpactHarmCause::UNKNOWN);
	EXPECT_FALSE(Replay.Run().PendingEpisodes().front().m_OriginSelfOriginEvidence);
	EXPECT_FALSE(Replay.Run().PendingEpisodes().front().m_FallAlreadyConfirmed);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);

	// Death is lifecycle termination only. It must close the pending candidate,
	// not manufacture a Fall from the death outcome.
	const SImpactGameplayInput MissingVictim = MissingCharacterInput(2, 120);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {ImpactInput(1, 120), MissingVictim},
		{ImpactKill(1, 120, 2, 2, WEAPON_WORLD)}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_TRUE(Replay.Run().PendingEpisodes().empty());
}

TEST(ImpactLiveAdapter, ExplicitSelfOriginConfirmsFallAtOrigin)
{
	CReplay Replay(151, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// This normalized frame represents positive physical helplessness. Freeze
	// entry opens the episode; the following authoritative frame confirms it.
	Target = LiveExplicitSelfOriginTarget(110);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target.m_ServerTick = 111;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_EQ(Replay.Run().PendingEpisodes().front().m_HarmCause, EImpactHarmCause::SELF_CAUSED);
	EXPECT_TRUE(Replay.Run().PendingEpisodes().front().m_OriginSelfOriginEvidence);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_TRUE(Replay.Run().PendingEpisodes().front().m_FallAlreadyConfirmed);
}

TEST(ImpactLiveAdapter, UnknownFreezeIsNotUnfrozen)
{
	SImpactGameplayInput Input;
	Input.m_Participant = TestId(1);
	Input.m_TeamId = 1;
	Input.m_ServerTick = 100;
	Input.m_Present = true;
	Input.m_CurrentCharacterKnown = true;
	Input.m_PreviousCharacterKnown = true;
	Input.m_PositionKnown = true;
	Input.m_VelocityKnown = true;
	Input.m_AliveKnown = true;
	Input.m_Alive = true;
	Input.m_FreezeKnown = false;
	Input.m_Frozen = false;
	Input.m_SoloKnown = true;
	const SImpactGameplayFrame Frame = NormalizeGameplayFrame(Input);

	EXPECT_FALSE(Frame.m_FreezeKnown);
	EXPECT_FALSE(Frame.m_Frozen);
}

TEST(ImpactLiveAdapter, TargetHookEvidenceRequiresConfirmedGrab)
{
	CReplay Replay(58, TestContext());
	StartPair(Replay);
	SImpactGameplayInput Saver = ImpactInput(1, 100);
	Saver.m_HookedPlayer = 2;
	Saver.m_HookState = HOOK_FLYING;
	SImpactGameplayInput Target = ImpactInput(2, 100, true);
	Replay.SubmitGameplayInputs({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 120;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Replay.SubmitGameplayInputs({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 185;
	EXPECT_FALSE(Replay.SubmitGameplayInputs({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
}

TEST(ImpactLiveAdapter, LiveEquivalentHookFrameFeedsPhase2)
{
	SRunContext Context = TestContext();
	Context.m_TickSpeed = 50;
	CReplay Replay(76, Context);
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	Saver.m_HookedPlayer = 2;
	Saver.m_HookState = HOOK_GRABBED;
	SetMeaningfulHookSupport(Saver);
	SImpactGameplayInput Target = ImpactInput(2, 100, true);
	Target.m_FreezeEnd = 110;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	SetPhysicalHarmfulOutcome(Target);
	EXPECT_FALSE(Replay.SubmitGameplayInputs({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);

	Saver.m_ServerTick = Target.m_ServerTick = 105;
	Target.m_X = 32;
	EXPECT_TRUE(Replay.SubmitGameplayInputs({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	Saver.m_ServerTick = Target.m_ServerTick = 105;
	EXPECT_FALSE(Replay.SubmitGameplayInputs({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	ClearPhysicalHarmfulOutcome(Target);
	EXPECT_FALSE(Replay.SubmitGameplayInputs({Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 145;
	EXPECT_TRUE(Replay.SubmitGameplayInputs({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::SAVE_CONFIRMED), 1);
}

TEST(ImpactLiveAdapter, UnattributedHammerEvidenceStaysUnknown)
{
	SImpactGameplayInput Input;
	Input.m_Participant = TestId(1);
	Input.m_TeamId = 1;
	Input.m_ServerTick = 100;
	Input.m_Present = true;
	Input.m_CurrentCharacterKnown = true;
	Input.m_PreviousCharacterKnown = true;
	Input.m_PositionKnown = true;
	Input.m_VelocityKnown = true;
	Input.m_AliveKnown = true;
	Input.m_Alive = true;
	Input.m_FreezeKnown = true;
	Input.m_SoloKnown = true;
	Input.m_AttackKnown = true;
	Input.m_AttackTick = 7;
	Input.m_Weapon = WEAPON_HAMMER;
	// The DDNet 19.9 live event has no actor/target pair.
	Input.m_HammerTargetEvidence = false;
	const SImpactGameplayFrame Frame = NormalizeGameplayFrame(Input);

	EXPECT_TRUE(Frame.m_AttackKnown);
	EXPECT_EQ(Frame.m_Weapon, WEAPON_HAMMER);
	EXPECT_FALSE(Frame.m_HammerTargetEvidence);
}

TEST(ImpactLiveAdapter, UnknownHammerAttributionNeverBecomesSeparateRecoveryResult)
{
	CReplay Replay(95, TestContext());
	StartPair(Replay);

	// This follows the live adapter contract: hook state is available, but the
	// DDNet 19.9 client-side hammer event cannot prove an actor/target pair.
	SImpactGameplayInput Saver = ImpactInput(1, 100);
	Saver.m_HookedPlayer = 2;
	Saver.m_HookState = HOOK_GRABBED;
	SetMeaningfulHookSupport(Saver);
	Saver.m_HammerTargetEvidenceKnown = false;
	SImpactGameplayInput Target = ImpactInput(2, 100, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_Supported = false;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	Target.m_FreezeEnd = 110;
	Target.m_RecoveryNeed = ClassifyLiveRecoveryInput(Target, false);
	EXPECT_EQ(Target.m_RecoveryNeed, EImpactRecoveryNeed::EXTERNAL_REQUIRED);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target.m_ServerTick = 105;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	Saver.m_ServerTick = Target.m_ServerTick = 105;
	Target.m_X = 32;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 2U);
	EXPECT_EQ(Replay.Run().PendingEpisodes().front().m_UnfreezeCause, EImpactUnfreezeCause::UNKNOWN);
	EXPECT_FALSE(Replay.Run().PendingEpisodes().front().m_UpDownEvidence);

	Saver.m_ServerTick = Target.m_ServerTick = 145;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_UpDowns, 0);
}

TEST(ImpactLiveAdapter, UnprovableCausalEvidenceRemainsUnknown)
{
	// This is the client-only live contract: the normal DDNet protocol exposes
	// the hook relation, but not a semantic support conclusion, hammer
	// actor/target attribution, autonomous counterfactual, or harm cause.
	SImpactGameplayInput Input = ImpactInput(1, 100, true);
	Input.m_HookedPlayer = 2;
	Input.m_HookState = HOOK_GRABBED;
	Input.m_AutonomousRecoveryKnown = false;
	Input.m_HammerTargetEvidenceKnown = false;
	Input.m_HarmCause = EImpactHarmCause::UNKNOWN;
	Input.m_HarmCausalConfidence = ECausalConfidence::INSUFFICIENT;
	const SImpactGameplayFrame Frame = NormalizeGameplayFrame(Input);

	EXPECT_TRUE(Frame.m_HookKnown);
	EXPECT_EQ(Frame.m_HookedPlayer, 2);
	EXPECT_EQ(Frame.m_HookState, HOOK_GRABBED);
	EXPECT_FALSE(Frame.m_MeaningfulHookSupportKnown);
	EXPECT_FALSE(Frame.m_AutonomousRecoveryKnown);
	EXPECT_FALSE(Frame.m_HammerTargetEvidenceKnown);
	EXPECT_EQ(Frame.m_HarmCause, EImpactHarmCause::UNKNOWN);
	EXPECT_EQ(Frame.m_HarmCausalConfidence, ECausalConfidence::INSUFFICIENT);
}

TEST(ImpactLiveAdapter, UnknownLiveHarmCauseDoesNotCreateFall)
{
	CReplay Replay(96, TestContext());
	StartPair(Replay);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {ImpactInput(1, 100), ImpactInput(2, 100)}));

	// No client-only evidence proves that the death was an own harmful mistake.
	// A server death outcome alone must therefore remain Fall 0.
	const SImpactGameplayInput MissingVictim = MissingCharacterInput(2, 120);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {ImpactInput(1, 120), MissingVictim},
		{ImpactKill(1, 120, 2, 2, WEAPON_WORLD)}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
}

TEST(ImpactLiveAdapter, LiveDeathTileOutcomeDoesNotCreateFallAlone)
{
	CReplay Replay(101, TestContext());
	StartPair(Replay);

	// Establish the same-generation unfrozen baseline before the authoritative
	// freeze/death-tile transition. A death-tile/death outcome is lifecycle
	// evidence only; it is not a standalone Fall decision.
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {ImpactInput(1, 90), ImpactInput(2, 90)}));
	SImpactGameplayInput Target = ImpactInput(2, 100, true);
	Target.m_HarmfulOutcomeCandidateKnown = CImpactSystem::IsDeathTileOutcomeCandidate(true, true, true, true);
	Target.m_HarmfulOutcomeCandidate = Target.m_HarmfulOutcomeCandidateKnown;
	ASSERT_TRUE(Target.m_HarmfulOutcomeCandidate);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {ImpactInput(1, 100), Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_FALSE(Replay.Run().PendingEpisodes().front().m_FallAlreadyConfirmed);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);

	const SImpactGameplayInput MissingVictim = MissingCharacterInput(2, 120);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {ImpactInput(1, 120), MissingVictim},
		{ImpactKill(1, 120, 2, 2, WEAPON_WORLD)}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 0);
}

TEST(ImpactLiveAdapter, LiveUnknownExternalRecoveryDeathDoesNotCreateFall)
{
	CReplay Replay(106, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// This is the normalized ordinary live transition: the target was previously
	// alive and unfrozen, then enters a freeze while the client cannot prove a
	// safe or harmful physical outcome. No HarmCause is injected.
	Target = LiveOrdinaryFrozenTarget(110);
	EXPECT_EQ(ClassifyLiveRecoveryInput(Target, false), EImpactRecoveryNeed::UNKNOWN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_EQ(Replay.Run().PendingEpisodes().front().m_HarmCause, EImpactHarmCause::UNKNOWN);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);

	// The server removes the character in the death snapshot. The ordinary
	// world/self death message closes the pending live episode, but death alone
	// is not an individual Fall confirmation.
	const SImpactGameplayInput MissingVictim = MissingCharacterInput(2, 120);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {ImpactInput(1, 120), MissingVictim},
		{ImpactKill(1, 120, 2, 2, WEAPON_WORLD)}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_TRUE(Replay.Run().PendingEpisodes().empty());
}

TEST(ImpactLiveAdapter, SelfOriginExternalRequiredFallConfirmsAtOrigin)
{
	CReplay Replay(115, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target = LiveExplicitSelfOriginTarget(110);
	// The positive self-origin producer confirms the Fall independently of any
	// later Save, unfreeze, or death outcome.
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target.m_ServerTick = 111;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 1);
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_TRUE(Replay.Run().PendingEpisodes().front().m_FallAlreadyConfirmed);
	EXPECT_EQ(Replay.Run().PendingEpisodes().front().m_InitialRecoveryNeed, EImpactRecoveryNeed::EXTERNAL_REQUIRED);
}

TEST(ImpactLiveAdapter, ConfirmedFallThenSaveRemainsExactlyOnce)
{
	CReplay Replay(116, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target = LiveExplicitSelfOriginTarget(110);
	// Freeze entry opens the problem episode. The following authoritative frame
	// with the same explicit self-origin evidence confirms the independent Fall.
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target.m_ServerTick = 111;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	Saver.m_ServerTick = Target.m_ServerTick = 120;
	Saver.m_HookedPlayer = 2;
	Saver.m_HookState = HOOK_GRABBED;
	SetMeaningfulHookSupport(Saver);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 130;
	Target.m_X = 32;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	Saver.m_HookedPlayer = -1;
	Saver.m_HookState = HOOK_RETRACTED;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 165;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 1);
}

TEST(ImpactLiveAdapter, ConfirmedFallSurvivesTeamReset)
{
	CReplay Replay(117, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target = LiveExplicitSelfOriginTarget(110);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target.m_ServerTick = 111;
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 120;
	SImpactGameplayMessage Reset;
	Reset.m_MessageId = 1;
	Reset.m_Type = EImpactGameplayMessageType::TEAM_RESET;
	Reset.m_ServerTick = 120;
	Reset.m_TeamId = 1;
	Reset.m_First = 0;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {ImpactInput(1, 120), Target}, {Reset}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::TEAM_RESET_CONFIRMED), 1);
}

TEST(ImpactLiveAdapter, ConfirmedFallSurvivesTeamWipe)
{
	CReplay Replay(152, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target = LiveExplicitSelfOriginTarget(110);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target.m_ServerTick = 111;
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	SImpactGameplayMessage Wipe;
	Wipe.m_MessageId = 1;
	Wipe.m_Type = EImpactGameplayMessageType::TEAM_WIPE;
	Wipe.m_ServerTick = 120;
	Wipe.m_TeamId = 1;
	Wipe.m_First = -1;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {ImpactInput(1, 120), Target}, {Wipe}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Wipes, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::WIPE_CONFIRMED), 1);
	EXPECT_TRUE(Replay.Run().PendingEpisodes().empty());
}

TEST(ImpactLiveAdapter, PendingFallPreservesInitialRecoveryNeedContext)
{
	CReplay Replay(118, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target = LiveExplicitSelfOriginTarget(110);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target.m_ServerTick = 111;
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// The next frozen snapshot has support but no authoritative recovery
	// conclusion. It must not rewrite the harmful-entry context.
	Target.m_ServerTick = 120;
	Target.m_Supported = true;
	Target.m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_TRUE(Replay.Run().PendingEpisodes().front().m_FallAlreadyConfirmed);
	EXPECT_EQ(Replay.Run().PendingEpisodes().front().m_InitialRecoveryNeed, EImpactRecoveryNeed::EXTERNAL_REQUIRED);
}

TEST(ImpactLiveAdapter, SelfSafeStillZero)
{
	CReplay Replay(119, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, true, true, true, 1, false, EImpactRecoveryNeed::SELF_SAFE);
	Target.m_FreezeEnd = 200;
	ASSERT_EQ(ClassifyLiveRecoveryInput(Target, true), EImpactRecoveryNeed::SELF_SAFE);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 200;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
}

TEST(ImpactLiveAdapter, AutonomousSafeNaturalUnfreezeStillZero)
{
	CReplay Replay(120, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = LiveSupportedUnknownFreeze(110);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN)}));
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 120;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	Target.m_ServerTick = 155;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_TRUE(Replay.Run().PendingEpisodes().empty());
}

TEST(ImpactLiveAdapter, ActiveHookAtFreezeWithoutMovementChangeDoesNotProveTeammateCause)
{
	CReplay Replay(121, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	Saver.m_HookedPlayer = 2;
	Saver.m_HookState = HOOK_GRABBED;
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 110;
	Target.m_FreezeEnd = 200;
	Target.m_Supported = false;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	Target.m_RecoveryNeed = ClassifyLiveRecoveryInput(Target, false);
	EXPECT_EQ(Target.m_RecoveryNeed, EImpactRecoveryNeed::UNKNOWN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_EQ(Replay.Run().PendingEpisodes().front().m_HarmCause, EImpactHarmCause::UNKNOWN);
	EXPECT_FALSE(Replay.Run().PendingEpisodes().front().m_OriginSelfOriginEvidence);
	EXPECT_FALSE(Replay.Run().PendingEpisodes().front().m_FallAlreadyConfirmed);
}

TEST(ImpactLiveAdapter, SupportedSafeFreezeClosesWithoutPendingFall)
{
	CReplay Replay(122, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target = LiveSupportedUnknownFreeze(110);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	// Map-ground support while the target is still frozen does not close the
	// physical episode. The later restoration boundary resolves it safely.
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_NE(FindDebugLabel(Replay.LastOutput(), "FALL_TRACE"), nullptr);
}

TEST(ImpactLiveAdapter, SupportedFreezeRemainsPendingAfterRecoveryNeedNoise)
{
	CReplay Replay(123, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target = LiveSupportedUnknownFreeze(110);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	// The first supported frozen snapshot is not a resolved safe outcome.
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);

	Target.m_ServerTick = 120;
	// A later RecoveryNeed label is not physical outcome evidence and must not
	// resolve or recreate the episode by itself.
	Target.m_RecoveryNeed = EImpactRecoveryNeed::EXTERNAL_REQUIRED;
	EXPECT_EQ(Target.m_RecoveryNeed, EImpactRecoveryNeed::EXTERNAL_REQUIRED);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_EQ(CountDebugLabels(Replay.LastOutput(), "FALL_CONFIRM"), 0);
}

TEST(ImpactLiveAdapter, SupportedUnknownThenSelfSafe)
{
	CReplay Replay(124, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target = LiveSupportedUnknownFreeze(110);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target = ImpactInput(2, 120, true, true, true, 1, false, EImpactRecoveryNeed::SELF_SAFE);
	Target.m_FreezeStart = 110;
	Target.m_FreezeEnd = 200;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	// SELF_SAFE metadata does not erase a still-frozen physical episode before
	// the actual recovery boundary.
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
}

TEST(ImpactLiveAdapter, SupportedUnknownThenSelfFix)
{
	CReplay Replay(125, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target = LiveSupportedUnknownFreeze(110);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target = ImpactInput(2, 120, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	Target.m_ServerTick = 155;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_TRUE(Replay.Run().PendingEpisodes().empty());
	EXPECT_EQ(CountDebugLabels(Replay.LastOutput(), "SAVE_RESULT"), 0);
}

TEST(ImpactLiveAdapter, PendingThenTeammateCausedOrigin)
{
	CReplay Replay(126, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target = LiveSupportedUnknownFreeze(110);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target = LiveSupportedUnknownFreeze(120);
	Target.m_HarmCause = EImpactHarmCause::TEAMMATE_CAUSED;
	Target.m_HarmCausalConfidence = ECausalConfidence::CONFIRMED;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 1);
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	const SImpactPendingEpisode &Episode = Replay.Run().PendingEpisodes().front();
	EXPECT_TRUE(Episode.m_FallAlreadyConfirmed);
	EXPECT_TRUE(Episode.m_OriginExternalCauseEvidence);
	EXPECT_EQ(Episode.m_HarmCause, EImpactHarmCause::TEAMMATE_CAUSED);

	// Teammate causation remains metadata only; repeated physical evidence
	// cannot create a second victim Fall for the same episode.
	Target.m_ServerTick = 130;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 1);
}

TEST(ImpactLiveAdapter, LateSaverHookDoesNotRewriteSelfOrigin)
{
	CReplay Replay(127, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target = LiveExplicitSelfOriginTarget(110);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target.m_ServerTick = 111;
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	Saver.m_ServerTick = Target.m_ServerTick = 120;
	SetHooked(Saver, 2);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	const SImpactPendingEpisode &Episode = Replay.Run().PendingEpisodes().front();
	EXPECT_EQ(Episode.m_HarmCause, EImpactHarmCause::SELF_CAUSED);
	EXPECT_TRUE(Episode.m_OriginSelfOriginEvidence);
	EXPECT_FALSE(Episode.m_OriginExternalCauseEvidence);
	EXPECT_TRUE(Episode.m_FallAlreadyConfirmed);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
}

TEST(ImpactLiveAdapter, RecoveryInteractionBindsValidEpisode)
{
	CReplay Replay(128, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target = LiveExplicitSelfOriginTarget(110);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target.m_ServerTick = 111;
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 120;
	SetHooked(Saver, 2);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	const SImpactPhase2Debug *pDebug = FindDebugLabel(Replay.LastOutput(), "SAVE_INTERACTION");
	ASSERT_NE(pDebug, nullptr);
	EXPECT_NE(pDebug->m_EpisodeId, 0U);
	EXPECT_EQ(pDebug->m_Target, TestId(2));
	EXPECT_EQ(pDebug->m_PotentialSaver, TestId(1));
	EXPECT_EQ(pDebug->m_HookState, HOOK_GRABBED);
	EXPECT_EQ(pDebug->m_HookedPlayer, 2);
}

TEST(ImpactLiveAdapter, ResolvedRecoveryDoesNotTickForever)
{
	CReplay Replay(129, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target = LiveExplicitSelfOriginTarget(110);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target.m_ServerTick = 111;
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 120;
	SetHooked(Saver, 2);
	SetMeaningfulHookSupport(Saver);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 130;
	ReleaseHook(Saver);
	Target.m_X = 32;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	Target.m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 165;
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	Saver.m_ServerTick = Target.m_ServerTick = 166;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().PendingEpisodes().size(), 0U);
	EXPECT_EQ(CountDebugLabels(Replay.LastOutput(), "SAVE_RESULT"), 0);
	EXPECT_EQ(CountDebugLabels(Replay.LastOutput(), "SAVE_INTERACTION"), 0);
}

TEST(ImpactLiveAdapter, ParticipantEligibilityPersistedFromEntry)
{
	CReplay Replay(130, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target = LiveExplicitSelfOriginTarget(110);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target.m_ServerTick = 111;
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_TRUE(Replay.Run().PendingEpisodes().front().m_TargetWasEligible);

	Saver.m_ServerTick = 120;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_TRUE(Replay.Run().PendingEpisodes().front().m_TargetWasEligible);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
}

TEST(ImpactLiveAdapter, FallDedupeAcrossFailedSaves)
{
	CReplay Replay(131, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target = LiveExplicitSelfOriginTarget(110);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target.m_ServerTick = 111;
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 120;
	SetHooked(Saver, 2);
	SetMeaningfulHookSupport(Saver);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 130;
	ReleaseHook(Saver);
	Target.m_X = 32;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	Target.m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 2U);
	const auto FirstRecovery = std::find_if(Replay.Run().PendingEpisodes().begin(), Replay.Run().PendingEpisodes().end(), [](const SImpactPendingEpisode &Episode) {
		return Episode.m_Type == EImpactEventType::SAVE_CONFIRMED;
	});
	ASSERT_NE(FirstRecovery, Replay.Run().PendingEpisodes().end());
	EXPECT_EQ(FirstRecovery->m_RestoreTick, 130);

	// Refreeze before the 0.7 second confirmation cancels only that Save
	// attempt. The already-confirmed origin Fall must survive unchanged, and the
	// continuous problem episode keeps its history without retaining stale proof.
	Saver.m_ServerTick = Target.m_ServerTick = 140;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 140;
	Target.m_FreezeEnd = 240;
	Target.m_Supported = false;
	Target.m_SafeCollisionPosition = false;
	Target.m_RecoveryNeed = ClassifyLiveRecoveryInput(Target, false);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
	EXPECT_EQ(std::count_if(Replay.Run().PendingEpisodes().begin(), Replay.Run().PendingEpisodes().end(), [](const SImpactPendingEpisode &Episode) {
		return Episode.m_Type == EImpactEventType::SAVE_CONFIRMED;
	}), 0);

	Saver.m_ServerTick = Target.m_ServerTick = 150;
	SetHooked(Saver, 2);
	SetMeaningfulHookSupport(Saver);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 160;
	ReleaseHook(Saver);
	Target.m_X = 64;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	Target.m_Supported = true;
	Target.m_SafeCollisionPosition = true;
	Target.m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
	const auto SecondRecovery = std::find_if(Replay.Run().PendingEpisodes().begin(), Replay.Run().PendingEpisodes().end(), [](const SImpactPendingEpisode &Episode) {
		return Episode.m_Type == EImpactEventType::SAVE_CONFIRMED;
	});
	ASSERT_NE(SecondRecovery, Replay.Run().PendingEpisodes().end());
	EXPECT_EQ(SecondRecovery->m_StartTick, 150);
	EXPECT_EQ(SecondRecovery->m_CausalRecoveryProof.m_FirstProvenTick, 160);
	EXPECT_EQ(SecondRecovery->m_RestoreTick, 160);
	Saver.m_ServerTick = Target.m_ServerTick = 195;
	const bool SaveConfirmedAtBoundary = SubmitLiveEquivalentInputs(Replay, {Saver, Target});
	ASSERT_TRUE(SaveConfirmedAtBoundary) << "phase2_events=" << Replay.LastOutput().m_vEvents.size()
		<< " run_events=" << Replay.Run().Events().size() << " saves=" << Replay.Run().RawStats().m_Saves;
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 1);
}

TEST(ImpactLiveAdapter, SuccessfulSaveDoesNotCreateFakeDeath)
{
	CReplay Replay(132, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target = LiveExplicitSelfOriginTarget(110);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target.m_ServerTick = 111;
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 120;
	SetHooked(Saver, 2);
	SetMeaningfulHookSupport(Saver);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 130;
	ReleaseHook(Saver);
	Target.m_X = 32;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	Target.m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 165;
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::SAVE_CONFIRMED), 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 1);
	EXPECT_EQ(CountDebugLabels(Replay.LastOutput(), "HARM_DEATH_RESOLVE"), 0);
}

TEST(ImpactLiveAdapter, HarmCauseStateIsInternallyConsistent)
{
	CReplay Replay(133, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target = LiveExplicitSelfOriginTarget(110);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target.m_ServerTick = 111;
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	const SImpactPendingEpisode &Episode = Replay.Run().PendingEpisodes().front();
	EXPECT_EQ(Episode.m_HarmCause, EImpactHarmCause::SELF_CAUSED);
	EXPECT_EQ(Episode.m_HarmCausalConfidence, ECausalConfidence::CONFIRMED);
	EXPECT_TRUE(Episode.m_OriginSelfOriginEvidence);
	EXPECT_FALSE(Episode.m_OriginExternalCauseEvidence);
	const SImpactPhase2Debug *pStart = FindDebugLabel(Replay.LastOutput(), "FALL_CONFIRM");
	ASSERT_NE(pStart, nullptr);
	EXPECT_TRUE(pStart->m_SelfOriginEvidence);
	EXPECT_EQ(pStart->m_HarmCause, EImpactHarmCause::SELF_CAUSED);
	EXPECT_EQ(pStart->m_HarmCausalConfidence, ECausalConfidence::CONFIRMED);
}

TEST(ImpactLiveAdapter, NoRecoveryDoesNotReportSaveConfirmedMechanic)
{
	CReplay Replay(134, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target = LiveSupportedUnknownFreeze(110);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target = ImpactInput(2, 120, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::SAVE_CONFIRMED), 0);
	EXPECT_EQ(CountDebugLabels(Replay.LastOutput(), "SAVE_RESULT"), 0);
	// With no target-specific interaction there is no recovery record to reject;
	// the important contract is that this autonomous unfreeze never emits Save.
	EXPECT_EQ(CountDebugLabels(Replay.LastOutput(), "SAVE_REJECTED"), 0);
}

TEST(ImpactLiveAdapter, ResetAfterSafeFreezeDoesNotCreateFall)
{
	CReplay Replay(135, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target = LiveSupportedUnknownFreeze(110);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 120;
	SImpactGameplayMessage Reset;
	Reset.m_MessageId = 1;
	Reset.m_Type = EImpactGameplayMessageType::TEAM_RESET;
	Reset.m_ServerTick = 120;
	Reset.m_TeamId = 1;
	Reset.m_First = 0;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}, {Reset}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 0);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::TEAM_RESET_CONFIRMED), 1);
	EXPECT_EQ(CountDebugLabels(Replay.LastOutput(), "SAVE_RESULT"), 0);
	EXPECT_EQ(CountDebugLabels(Replay.LastOutput(), "FALL_CONFIRM"), 0);
	// Reset closes the unresolved lifecycle; it does not manufacture a Fall.
	EXPECT_NE(FindDebugLabel(Replay.LastOutput(), "FALL_NOT_EMITTED"), nullptr);
}

TEST(ImpactLiveAdapter, PhysicalFreezeHasStableHarmRecoveryBinding)
{
	CReplay Replay(136, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target = LiveExplicitSelfOriginTarget(110);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target.m_ServerTick = 111;
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	const uint64_t FallEpisodeId = Replay.Run().PendingEpisodes().front().m_EpisodeId;

	Saver.m_ServerTick = Target.m_ServerTick = 120;
	SetHooked(Saver, 2);
	SetMeaningfulHookSupport(Saver);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 130;
	ReleaseHook(Saver);
	Target.m_X = 32;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	Target.m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	const auto SaveIt = std::find_if(Replay.Run().PendingEpisodes().begin(), Replay.Run().PendingEpisodes().end(), [](const SImpactPendingEpisode &Episode) {
		return Episode.m_Type == EImpactEventType::SAVE_CONFIRMED;
	});
	ASSERT_NE(SaveIt, Replay.Run().PendingEpisodes().end());
	EXPECT_NE(SaveIt->m_EpisodeId, 0U);
	EXPECT_EQ(SaveIt->m_RelatedFallEpisodeId, FallEpisodeId);
	const SImpactPhase2Debug *pRestore = FindDebugLabel(Replay.LastOutput(), "SAVE_RESTORE");
	ASSERT_NE(pRestore, nullptr);
	EXPECT_EQ(pRestore->m_EpisodeId, SaveIt->m_EpisodeId);
	EXPECT_EQ(pRestore->m_RelatedFallEpisodeId, FallEpisodeId);
	EXPECT_EQ(pRestore->m_RelatedRecoveryEpisodeId, SaveIt->m_EpisodeId);
	// This fixture has already supplied independent harmful physical evidence
	// before the rescue interaction. The recovery record therefore carries the
	// already-confirmed immutable Fall binding; this is not a recovery rewrite.
	EXPECT_TRUE(pRestore->m_FallAlreadyConfirmed);
	const auto FallIt = std::find_if(Replay.Run().PendingEpisodes().begin(), Replay.Run().PendingEpisodes().end(), [FallEpisodeId](const SImpactPendingEpisode &Episode) {
		return Episode.m_EpisodeId == FallEpisodeId;
	});
	ASSERT_NE(FallIt, Replay.Run().PendingEpisodes().end());
	EXPECT_EQ(FallIt->m_RelatedRecoveryEpisodeId, SaveIt->m_EpisodeId);
}

TEST(ImpactLiveAdapter, BoundHarmAndRecoveryCannotDisagreeOnConfirmedSave)
{
	CReplay Replay(144, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = LiveExplicitSelfOriginTarget(110);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {ImpactInput(1, 100), ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN)}));
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target.m_ServerTick = 111;
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	const uint64_t FallEpisodeId = Replay.Run().PendingEpisodes().front().m_EpisodeId;

	Saver.m_ServerTick = Target.m_ServerTick = 120;
	SetHooked(Saver, 2);
	SetMeaningfulHookSupport(Saver);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 130;
	ReleaseHook(Saver);
	Target.m_X = 32;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	Target.m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 2U);
	const auto SaveIt = std::find_if(Replay.Run().PendingEpisodes().begin(), Replay.Run().PendingEpisodes().end(), [](const SImpactPendingEpisode &Episode) {
		return Episode.m_Type == EImpactEventType::SAVE_CONFIRMED;
	});
	ASSERT_NE(SaveIt, Replay.Run().PendingEpisodes().end());
	EXPECT_EQ(SaveIt->m_RelatedFallEpisodeId, FallEpisodeId);
	const uint64_t SaveEpisodeId = SaveIt->m_EpisodeId;

	Saver.m_ServerTick = Target.m_ServerTick = 165;
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::SAVE_CONFIRMED), 1);
	EXPECT_EQ(CountDebugLabels(Replay.LastOutput(), "FALL_NOT_EMITTED"), 0);
	const SImpactPhase2Debug *pRecovery = FindDebugLabel(Replay.LastOutput(), "SAVE_CONFIRMED");
	ASSERT_NE(pRecovery, nullptr);
	EXPECT_EQ(pRecovery->m_RelatedFallEpisodeId, FallEpisodeId);
	EXPECT_EQ(pRecovery->m_RelatedRecoveryEpisodeId, SaveEpisodeId);
	EXPECT_EQ(CountDebugLabels(Replay.LastOutput(), "FALL_EPISODE_CLOSED"), 1);
}

TEST(ImpactLiveAdapter, RecoveryNeedContextDoesNotBlockOrdinaryFall)
{
	CReplay Replay(145, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 110;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 110;
	Target.m_FreezeEnd = 200;
	Target.m_Supported = false;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	Target.m_AutonomousRecoveryKnown = false;
	Target.m_AutonomousRecoveryPossible = false;
	Target.m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
	Target.m_X = 100;
	Target.m_Y = 100;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_EQ(Replay.Run().PendingEpisodes().front().m_InitialRecoveryNeed, EImpactRecoveryNeed::UNKNOWN);
	EXPECT_FALSE(Replay.Run().PendingEpisodes().front().m_FallAlreadyConfirmed);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);

	Target.m_ServerTick = 120;
	Target.m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_EQ(Replay.Run().PendingEpisodes().front().m_RecoveryNeed, EImpactRecoveryNeed::UNKNOWN);
	EXPECT_EQ(Replay.Run().PendingEpisodes().front().m_InitialRecoveryNeed, EImpactRecoveryNeed::UNKNOWN);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
}

TEST(ImpactLiveAdapter, UnknownRecoveryCannotSilentlyBecomeExternalRequiredAtRestore)
{
	CReplay Replay(146, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target = LiveSupportedUnknownFreeze(110);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 120;
	SetHooked(Saver, 2);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 130;
	ReleaseHook(Saver);
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	Target.m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
	EXPECT_EQ(CountDebugLabels(Replay.LastOutput(), "SAVE_RESTORE"), 0);
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	Saver.m_ServerTick = Target.m_ServerTick = 165;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_TRUE(Replay.Run().PendingEpisodes().empty());
}

TEST(ImpactLiveAdapter, ActiveHookAtFreezeDoesNotAloneProveTeammateCausedOrigin)
{
	CReplay Replay(147, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	SetHooked(Saver, 2);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = 110;
	Target = LiveExplicitSelfOriginTarget(110);
	SetHooked(Saver, 2);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target.m_ServerTick = 111;
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	const SImpactPendingEpisode &FallEpisode = Replay.Run().PendingEpisodes().front();
	EXPECT_EQ(FallEpisode.m_HarmCause, EImpactHarmCause::SELF_CAUSED);
	EXPECT_TRUE(FallEpisode.m_OriginSelfOriginEvidence);
	EXPECT_FALSE(FallEpisode.m_OriginExternalCauseEvidence);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
}

TEST(ImpactLiveAdapter, HookGrabInteractionUsesSameAuthoritativeHookState)
{
	CReplay Replay(137, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target = LiveExplicitSelfOriginTarget(110);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target.m_ServerTick = 111;
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 120;
	SetHooked(Saver, 2);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	const SImpactPhase2Debug *pInteraction = FindDebugLabel(Replay.LastOutput(), "SAVE_INTERACTION");
	ASSERT_NE(pInteraction, nullptr);
	EXPECT_EQ(pInteraction->m_HookState, HOOK_GRABBED);
	EXPECT_EQ(pInteraction->m_HookedPlayer, pInteraction->m_Target.m_ClientId);
	EXPECT_EQ(pInteraction->m_PotentialSaver, Saver.m_Participant);
}

TEST(ImpactLiveAdapter, TeamResetPrecedesRespawnRecoveryProcessing)
{
	CReplay Replay(138, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target = LiveExplicitSelfOriginTarget(110);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target.m_ServerTick = 111;
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 120;
	SImpactGameplayMessage Reset;
	Reset.m_MessageId = 1;
	Reset.m_Type = EImpactGameplayMessageType::TEAM_RESET;
	Reset.m_ServerTick = 120;
	Reset.m_TeamId = 1;
	Reset.m_First = 0;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}, {Reset}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	Target = ImpactInput(2, 130, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
	EXPECT_EQ(CountDebugLabels(Replay.LastOutput(), "SAVE_RESTORE"), 0);
}

TEST(ImpactLiveAdapter, FallDedupeAcrossDeathOrReset)
{
	CReplay Replay(139, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target = LiveExplicitSelfOriginTarget(110);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target.m_ServerTick = 111;
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	Target.m_ServerTick = 120;
	SImpactGameplayMessage Reset;
	Reset.m_MessageId = 1;
	Reset.m_Type = EImpactGameplayMessageType::TEAM_RESET;
	Reset.m_ServerTick = 120;
	Reset.m_TeamId = 1;
	Reset.m_First = 0;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}, {Reset}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	Target = MissingCharacterInput(2, 130);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}, {ImpactKill(2, 130, 2, 2, WEAPON_WORLD)}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 1);
}

TEST(ImpactPhase2, OldRunHarmEpisodeCannotResolveInNewRun)
{
	CImpactPhase2 Phase;
	const std::vector<SImpactGameplayFrame> Normal = {ImpactFrame(1, 100, false), ImpactFrame(2, 100, false)};
	SImpactGameplayInput HarmfulInput = ImpactInput(2, 110, true);
	HarmfulInput.m_HarmfulOutcomeCandidateKnown = true;
	HarmfulInput.m_HarmfulOutcomeCandidate = true;
	SetSelfCausedHarm(HarmfulInput);
	const SImpactGameplayFrame HarmfulTarget = NormalizeGameplayFrame(HarmfulInput);
	EXPECT_TRUE(Phase.Update(140, 1, 50, Normal, {}).m_vEvents.empty());
	const SImpactPhase2Output EntryOutput = Phase.Update(140, 1, 50,
		{ImpactFrame(1, 110, false), HarmfulTarget}, {});
	EXPECT_TRUE(EntryOutput.m_vEvents.empty());
	SImpactGameplayInput HarmfulNextInput = HarmfulInput;
	HarmfulNextInput.m_ServerTick = 111;
	HarmfulNextInput.m_AutonomousRecoveryKnown = true;
	HarmfulNextInput.m_AutonomousRecoveryPossible = false;
	const SImpactPhase2Output OldRunOutput = Phase.Update(140, 1, 50,
		{ImpactFrame(1, 111, false), NormalizeGameplayFrame(HarmfulNextInput)}, {});
	ASSERT_EQ(OldRunOutput.m_vPendingEpisodes.size(), 1U);
	// The explicit positive self-origin evidence resolves the old run's Fall at
	// its own harmful transition. The new run must not inherit that episode.
	ASSERT_EQ(OldRunOutput.m_vEvents.size(), 1U);
	EXPECT_EQ(OldRunOutput.m_vEvents.front().m_Type, EImpactEventType::FALL_CONFIRMED);

	const SImpactPhase2Output NewRunOutput = Phase.Update(141, 1, 50, Normal, {});
	EXPECT_TRUE(NewRunOutput.m_vEvents.empty());
	EXPECT_TRUE(NewRunOutput.m_vPendingEpisodes.empty());
}

TEST(ImpactPhase2, OldRunRecoveryInteractionCannotAttachToNewRun)
{
	CImpactPhase2 Phase;
	const std::vector<SImpactGameplayFrame> Normal = {ImpactFrame(1, 100, false), ImpactFrame(2, 100, false)};
	EXPECT_TRUE(Phase.Update(142, 1, 50, Normal, {}).m_vEvents.empty());
	Phase.Update(142, 1, 50, {ImpactFrame(1, 110, false), ImpactFrame(2, 110, true)}, {});

	SImpactGameplayFrame Saver = ImpactFrame(1, 120, false);
	SetHooked(Saver, 2);
	const SImpactPhase2Output OldInteraction = Phase.Update(142, 1, 50, {Saver, ImpactFrame(2, 120, true)}, {});
	ASSERT_FALSE(OldInteraction.m_vPendingEpisodes.empty());

	const SImpactPhase2Output NewRunOutput = Phase.Update(143, 1, 50, Normal, {});
	EXPECT_TRUE(NewRunOutput.m_vEvents.empty());
	EXPECT_TRUE(NewRunOutput.m_vPendingEpisodes.empty());
}

TEST(ImpactPhase2, ResetClearsRunScopedMotionHistory)
{
	CImpactPhase2 Phase;
	const std::vector<SImpactGameplayFrame> Normal = {ImpactFrame(1, 100, false), ImpactFrame(2, 100, false)};
	EXPECT_TRUE(Phase.Update(144, 1, 50, Normal, {}).m_vEvents.empty());

	SImpactGameplayMessage Reset;
	Reset.m_MessageId = 1;
	Reset.m_Type = EImpactGameplayMessageType::TEAM_RESET;
	Reset.m_ServerTick = 110;
	Reset.m_TeamId = 1;
	Reset.m_First = 0;
	const SImpactPhase2Output ResetOutput = Phase.Update(144, 1, 50,
		{ImpactFrame(1, 110, false), ImpactFrame(2, 110, true)}, {Reset});
	EXPECT_EQ(CountOutputEvents(ResetOutput, EImpactEventType::TEAM_RESET_CONFIRMED), 1);

	const SImpactPhase2Output RespawnOutput = Phase.Update(144, 1, 50,
		{ImpactFrame(1, 120, false), ImpactFrame(2, 120, false)}, {});
	EXPECT_TRUE(RespawnOutput.m_vEvents.empty());
	EXPECT_TRUE(RespawnOutput.m_vPendingEpisodes.empty());
}

TEST(ImpactLiveAdapter, AuthoritativeJumpStateMatchesDdnetSnapshotRules)
{
	const auto State = [](bool Alive, bool Frozen, bool MetadataKnown, int Jumps, int Jumped, bool Grounded, bool Endless = false) {
		return CImpactSystem::ClassifyUsableJumpStateFromAuthoritativeState(
			true, true, Alive, true, Frozen, MetadataKnown, Jumps, Jumped, true, Grounded, Endless);
	};

	EXPECT_EQ(State(true, false, true, 2, 0, true), EImpactJumpState::AVAILABLE);
	EXPECT_EQ(State(true, false, true, 2, 0, false), EImpactJumpState::AVAILABLE);
	EXPECT_EQ(State(true, false, true, 2, 2, false), EImpactJumpState::UNAVAILABLE);
	EXPECT_EQ(State(true, false, true, 0, 0, true), EImpactJumpState::UNAVAILABLE);
	EXPECT_EQ(State(true, false, true, -1, 0, true), EImpactJumpState::AVAILABLE);
	EXPECT_EQ(State(true, false, true, -1, 0, false), EImpactJumpState::UNAVAILABLE);
	EXPECT_EQ(State(true, false, true, 2, 2, false, true), EImpactJumpState::AVAILABLE);
	EXPECT_EQ(State(true, false, true, 0, 2, false, true), EImpactJumpState::AVAILABLE);
	EXPECT_EQ(State(true, false, true, -1, 2, false, true), EImpactJumpState::AVAILABLE);
	EXPECT_EQ(State(false, false, true, 2, 0, true), EImpactJumpState::UNAVAILABLE);
	EXPECT_EQ(State(true, true, true, 2, 0, true), EImpactJumpState::UNAVAILABLE);
	EXPECT_EQ(State(true, false, false, 2, 0, true), EImpactJumpState::UNKNOWN);
	EXPECT_EQ(CImpactSystem::ClassifyUsableJumpStateFromAuthoritativeState(
		false, true, true, true, false, true, 2, 0, true, true, false), EImpactJumpState::UNKNOWN);
}

TEST(ImpactLiveAdapter, AuthoritativeDownwardShotRequiresSelfAttackEdge)
{
	bool Downward = false;
	EXPECT_TRUE(CImpactSystem::ClassifySelfDownwardShotFromAuthoritativeState(
		true, true, true, 100, 100, true, 100, WEAPON_GUN, &Downward));
	EXPECT_FALSE(Downward);
	EXPECT_TRUE(CImpactSystem::ClassifySelfDownwardShotFromAuthoritativeState(
		true, true, true, 101, 100, true, 100, WEAPON_GUN, &Downward));
	EXPECT_TRUE(Downward);
	EXPECT_TRUE(CImpactSystem::ClassifySelfDownwardShotFromAuthoritativeState(
		true, true, true, 102, 101, true, -100, WEAPON_GUN, &Downward));
	EXPECT_FALSE(Downward);
	EXPECT_TRUE(CImpactSystem::ClassifySelfDownwardShotFromAuthoritativeState(
		true, true, true, 103, 102, true, 100, WEAPON_HAMMER, &Downward));
	EXPECT_FALSE(Downward);
	EXPECT_FALSE(CImpactSystem::ClassifySelfDownwardShotFromAuthoritativeState(
		true, false, true, 104, 103, true, 100, WEAPON_GUN, &Downward));
	EXPECT_FALSE(CImpactSystem::ClassifySelfDownwardShotFromAuthoritativeState(
		true, true, true, 105, 104, false, 100, WEAPON_GUN, &Downward));
}

TEST(ImpactLiveAdapter, LiveUnconfirmedResetPatternDoesNotCreateResetEvent)
{
	CReplay Replay(97, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	SetAuthoritativeJumpEvidence(Target, 2, 2, false, true, 100, 99, true, 100, WEAPON_GUN);
	EXPECT_EQ(Target.m_JumpState, EImpactJumpState::UNAVAILABLE);
	EXPECT_TRUE(Target.m_DownwardShotKnown);
	EXPECT_TRUE(Target.m_DownwardShot);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 110;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 110;
	Target.m_FreezeEnd = 200;
	SetAuthoritativeJumpEvidence(Target, 2, 2, false, true, 100, 100, true, 0, WEAPON_GUN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 120;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 200;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	SetAuthoritativeJumpEvidence(Target, 2, 0, true, true, 100, 100, true, 0, WEAPON_GUN);
	EXPECT_EQ(Target.m_JumpState, EImpactJumpState::AVAILABLE);
	EXPECT_FALSE(Target.m_DownwardShot);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	// DDNet 19.9 does not expose whether this freeze entry was self-initiated.
	// The observable sequence therefore remains UNKNOWN rather than becoming a
	// false-positive Jump Reset.
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::JUMP_RESET_CONFIRMED), 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
}

TEST(ImpactPhase2, JumpResetAcceptsExplicitSelfInitiatedFreezeEvidence)
{
	CReplay Replay(105, TestContext());
	StartPair(Replay);

	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	SImpactGameplayFrame Target = ImpactFrame(2, 100, false);
	Target.m_JumpState = EImpactJumpState::UNAVAILABLE;
	Target.m_DownwardShotKnown = true;
	Target.m_DownwardShot = true;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	Target.m_ServerTick = 110;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 110;
	Target.m_FreezeEnd = 200;
	Target.m_SelfInitiatedFreezeKnown = true;
	Target.m_SelfInitiatedFreeze = true;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	Target.m_ServerTick = 200;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_JumpState = EImpactJumpState::AVAILABLE;
	Target.m_DownwardShotKnown = true;
	Target.m_DownwardShot = false;
		EXPECT_TRUE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::JUMP_RESET_CONFIRMED), 1);
}

TEST(ImpactLiveAdapter, LiveFailedResetPatternDoesNotCreateResetEvent)
{
	CReplay Replay(98, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	SetAuthoritativeJumpEvidence(Target, 2, 2, false, true, 100, 99, true, 100, WEAPON_GUN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 110;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 110;
	Target.m_FreezeEnd = 200;
	SetAuthoritativeJumpEvidence(Target, 2, 2, false, true, 100, 100, true, 0, WEAPON_GUN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 125;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	SetAuthoritativeJumpEvidence(Target, 2, 2, false, true, 100, 100, true, 0, WEAPON_GUN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 130;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 130;
	Target.m_FreezeEnd = 230;
	SetAuthoritativeJumpEvidence(Target, 2, 2, false, true, 100, 100, true, 0, WEAPON_GUN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::JUMP_RESET_CONFIRMED), 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
}

TEST(ImpactLiveAdapter, LiveAvailableJumpDownwardShotStillCountsRawFall)
{
	CReplay Replay(99, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false);
	SetAuthoritativeJumpEvidence(Target, 2, 0, true, true, 100, 99, true, 100, WEAPON_GUN);
	EXPECT_EQ(Target.m_JumpState, EImpactJumpState::AVAILABLE);
	EXPECT_TRUE(Target.m_DownwardShot);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 110;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 110;
	Target.m_FreezeEnd = 200;
	SetAuthoritativeJumpEvidence(Target, 2, 2, false, true, 100, 100, true, 0, WEAPON_GUN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target.m_ServerTick = 200;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	SetAuthoritativeJumpEvidence(Target, 2, 0, true, true, 100, 100, true, 0, WEAPON_GUN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::JUMP_RESET_CONFIRMED), 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
}

TEST(ImpactLiveAdapter, AvailableJumpDownwardShotDoesNotSuppressFall)
{
	CReplay Replay(108, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false);
	SetAuthoritativeJumpEvidence(Target, 2, 0, true, true, 100, 99, true, 100, WEAPON_GUN);
	ASSERT_EQ(Target.m_JumpState, EImpactJumpState::AVAILABLE);
	ASSERT_TRUE(Target.m_DownwardShot);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// Harmful entry happens before any teammate interaction. The target had an
	// AVAILABLE jump at the shot edge, so this cannot be reset evidence.
	Target.m_ServerTick = 110;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 110;
	Target.m_FreezeEnd = 200;
	SetAuthoritativeJumpEvidence(Target, 2, 2, false, true, 100, 100, true, 0, WEAPON_GUN);
	SetSelfCausedHarm(Target);
	// Keep the positive Fall expectation tied to the observable harmful tile
	// outcome that the live adapter can produce.
	Target.m_HarmfulOutcomeCandidateKnown = true;
	Target.m_HarmfulOutcomeCandidate = true;
	// The compatibility marker is not itself a raw Fall producer. Confirm on
	// the next authoritative frame carrying positive helplessness evidence.
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target.m_ServerTick = 111;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_EQ(Replay.Run().PendingEpisodes().front().m_HarmCause, EImpactHarmCause::SELF_CAUSED);

	// A later hook is the causal recovery interaction.
	Target.m_ServerTick = 120;
	Saver.m_ServerTick = 120;
	Saver.m_HookedPlayer = 2;
	Saver.m_HookState = HOOK_GRABBED;
	SetMeaningfulHookSupport(Saver);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 130;
	Saver.m_ServerTick = 130;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_HarmfulOutcomeCandidateKnown = false;
	Target.m_HarmfulOutcomeCandidate = false;
	Target.m_X = 100;
	Target.m_Y = 100;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	Saver.m_HookedPlayer = -1;
	Saver.m_HookState = HOOK_RETRACTED;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 165;
	Saver.m_ServerTick = 165;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_UpDowns, 0);
}

TEST(ImpactLiveAdapter, AvailableJumpDownwardShotDeathDoesNotCreateFall)
{
	CReplay Replay(113, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false);
	SetAuthoritativeJumpEvidence(Target, 2, 0, true, true, 100, 99, true, 100, WEAPON_GUN);
	ASSERT_EQ(Target.m_JumpState, EImpactJumpState::AVAILABLE);
	ASSERT_TRUE(Target.m_DownwardShot);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// A downward shot with an AVAILABLE jump is ordinary action context. The
	// target then enters an unresolved ordinary freeze without a target-specific
	// teammate cause; death alone must not manufacture a Fall.
	Target.m_ServerTick = 110;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 110;
	Target.m_FreezeEnd = 200;
	Target.m_Supported = false;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	Target.m_AutonomousRecoveryKnown = false;
	Target.m_AutonomousRecoveryPossible = false;
	SetAuthoritativeJumpEvidence(Target, 2, 2, false, true, 100, 100, true, 0, WEAPON_GUN);
	EXPECT_EQ(ClassifyLiveRecoveryInput(Target, false), EImpactRecoveryNeed::UNKNOWN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_EQ(Replay.Run().PendingEpisodes().front().m_HarmCause, EImpactHarmCause::UNKNOWN);

	const SImpactGameplayInput MissingVictim = MissingCharacterInput(2, 120);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {ImpactInput(1, 120), MissingVictim},
		{ImpactKill(1, 120, 2, 2, WEAPON_WORLD)}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_TRUE(Replay.Run().PendingEpisodes().empty());
}

TEST(ImpactLiveAdapter, AvailableJumpDownwardShotWithUnattributedTeammateHammerStillCreatesFall)
{
	CReplay Replay(109, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false);
	SetAuthoritativeJumpEvidence(Target, 2, 0, true, true, 100, 99, true, 100, WEAPON_GUN);
	ASSERT_EQ(Target.m_JumpState, EImpactJumpState::AVAILABLE);
	ASSERT_TRUE(Target.m_DownwardShot);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// A generic teammate hammer attack has no target attribution in the live
	// adapter. It must not turn an otherwise self-origin harmful transition into
	// an UNKNOWN Fall candidate.
	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Saver.m_Weapon = WEAPON_HAMMER;
	Saver.m_AttackTick = 110;
	Saver.m_HammerTargetEvidenceKnown = false;
	Saver.m_HammerTargetEvidence = false;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 110;
	Target.m_FreezeEnd = 200;
	Target.m_Supported = false;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	Target.m_AutonomousRecoveryKnown = false;
	Target.m_AutonomousRecoveryPossible = false;
	SetAuthoritativeJumpEvidence(Target, 2, 2, false, true, 100, 100, true, 0, WEAPON_GUN);
	SetSelfCausedHarm(Target);
	Target.m_HarmfulOutcomeCandidateKnown = true;
	Target.m_HarmfulOutcomeCandidate = true;
	EXPECT_EQ(ClassifyLiveRecoveryInput(Target, false), EImpactRecoveryNeed::UNKNOWN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target.m_ServerTick = 111;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_EQ(Replay.Run().PendingEpisodes().front().m_HarmCause, EImpactHarmCause::SELF_CAUSED);

	Saver.m_ServerTick = Target.m_ServerTick = 120;
	Saver.m_HookedPlayer = 2;
	Saver.m_HookState = HOOK_GRABBED;
	SetMeaningfulHookSupport(Saver);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 200;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_HarmfulOutcomeCandidateKnown = false;
	Target.m_HarmfulOutcomeCandidate = false;
	Target.m_X = 100;
	Target.m_Y = 100;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	Saver.m_HookedPlayer = -1;
	Saver.m_HookState = HOOK_RETRACTED;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 235;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_UpDowns, 0);
}

TEST(ImpactLiveAdapter, UnavailableJumpDownwardShotStillResolvesToRawFall)
{
	CReplay Replay(114, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false);
	SetAuthoritativeJumpEvidence(Target, 2, 2, false, true, 100, 99, true, 100, WEAPON_GUN);
	ASSERT_EQ(Target.m_JumpState, EImpactJumpState::UNAVAILABLE);
	ASSERT_TRUE(Target.m_DownwardShot);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// Raw Fall semantics intentionally do not classify or suppress this as a
	// Jump Reset. The target enters the same harmful external-recovery state as
	// any other self-origin fall, regardless of its jump state or shot context.
	Target.m_ServerTick = 110;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 110;
	Target.m_FreezeEnd = 200;
	Target.m_Supported = false;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	Target.m_AutonomousRecoveryKnown = false;
	Target.m_AutonomousRecoveryPossible = false;
	SetAuthoritativeJumpEvidence(Target, 2, 2, false, true, 100, 100, true, 100, WEAPON_GUN);
	SetSelfCausedHarm(Target);
	Target.m_HarmfulOutcomeCandidateKnown = true;
	Target.m_HarmfulOutcomeCandidate = true;
	EXPECT_EQ(ClassifyLiveRecoveryInput(Target, false), EImpactRecoveryNeed::UNKNOWN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target.m_ServerTick = 111;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_EQ(Replay.Run().PendingEpisodes().front().m_HarmCause, EImpactHarmCause::SELF_CAUSED);

	// A later target-specific hook provides the ordinary Save recovery. The
	// pending Fall is resolved at the recovery boundary and remains
	// independent of the later Save confirmation interval.
	Target.m_ServerTick = 120;
	Saver.m_ServerTick = 120;
	Saver.m_HookedPlayer = 2;
	Saver.m_HookState = HOOK_GRABBED;
	SetMeaningfulHookSupport(Saver);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 130;
	Saver.m_ServerTick = 130;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_HarmfulOutcomeCandidateKnown = false;
	Target.m_HarmfulOutcomeCandidate = false;
	Target.m_X = 100;
	Target.m_Y = 100;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	Saver.m_HookedPlayer = -1;
	Saver.m_HookState = HOOK_RETRACTED;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 165;
	Saver.m_ServerTick = 165;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_UpDowns, 0);
}

TEST(ImpactLiveAdapter, SparseRemoteHarmfulTransitionStaysPendingWithoutOutcome)
{
	CReplay Replay(110, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false);
	SetAuthoritativeJumpEvidence(Target, 2, 0, true, true, 100, 100, true, 0, WEAPON_GUN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// The same generation is present, but its current character metadata is
	// temporarily absent. The next frozen frame must still compare against the
	// last confirmed normal character frame.
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {ImpactInput(1, 105), MissingCharacterInput(2, 105)}));

	Target.m_ServerTick = 110;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 110;
	Target.m_FreezeEnd = 200;
	Target.m_Supported = false;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	Target.m_AutonomousRecoveryKnown = false;
	Target.m_AutonomousRecoveryPossible = false;
	SetAuthoritativeJumpEvidence(Target, 2, 2, false, true, 100, 100, true, 0, WEAPON_GUN);
	EXPECT_EQ(ClassifyLiveRecoveryInput(Target, false), EImpactRecoveryNeed::UNKNOWN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {ImpactInput(1, 110), Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_EQ(Replay.Run().PendingEpisodes().front().m_HarmCause, EImpactHarmCause::UNKNOWN);

	const SImpactGameplayInput MissingVictim = MissingCharacterInput(2, 120);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {ImpactInput(1, 120), MissingVictim},
		{ImpactKill(1, 120, 2, 2, WEAPON_WORLD)}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_TRUE(Replay.Run().PendingEpisodes().empty());
}

TEST(ImpactLiveAdapter, PanicDownwardShotDoesNotCreateFallOnDeath)
{
	CReplay Replay(111, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false);
	SetAuthoritativeJumpEvidence(Target, 2, 0, true, true, 100, 100, true, 0, WEAPON_GUN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 110;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 110;
	Target.m_FreezeEnd = 200;
	Target.m_Supported = false;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	SetAuthoritativeJumpEvidence(Target, 2, 2, false, true, 100, 100, true, 0, WEAPON_GUN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_EQ(Replay.Run().PendingEpisodes().front().m_HarmCause, EImpactHarmCause::UNKNOWN);

	// The target is already in the tracked harmful episode. A later downward
	// attack is recovery/panic context and cannot retroactively become reset
	// evidence or suppress the pending Fall.
	Target.m_ServerTick = 115;
	SetAuthoritativeJumpEvidence(Target, 2, 2, false, true, 115, 100, true, 100, WEAPON_GUN);
	EXPECT_TRUE(Target.m_DownwardShot);
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	const SImpactGameplayInput MissingVictim = MissingCharacterInput(2, 120);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {ImpactInput(1, 120), MissingVictim},
		{ImpactKill(1, 120, 2, 2, WEAPON_WORLD)}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_TRUE(Replay.Run().PendingEpisodes().front().m_FallAlreadyConfirmed);
}

TEST(ImpactLiveAdapter, ClearedJumpResetShotDoesNotCreateFallOnDeath)
{
	CReplay Replay(112, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false);
	SetAuthoritativeJumpEvidence(Target, 2, 2, false, true, 100, 99, true, 100, WEAPON_GUN);
	ASSERT_EQ(Target.m_JumpState, EImpactJumpState::UNAVAILABLE);
	ASSERT_TRUE(Target.m_DownwardShot);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// A confirmed non-frozen observation without the same shot ends the
	// generation-scoped reset context before the later harmful episode.
	Target.m_ServerTick = 105;
	SetAuthoritativeJumpEvidence(Target, 2, 2, false, true, 100, 100, true, 0, WEAPON_GUN);
	EXPECT_FALSE(Target.m_DownwardShot);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 110;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 110;
	Target.m_FreezeEnd = 200;
	Target.m_Supported = false;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	SetAuthoritativeJumpEvidence(Target, 2, 2, false, true, 100, 100, true, 0, WEAPON_GUN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_EQ(Replay.Run().PendingEpisodes().front().m_HarmCause, EImpactHarmCause::UNKNOWN);

	const SImpactGameplayInput MissingVictim = MissingCharacterInput(2, 120);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {ImpactInput(1, 120), MissingVictim},
		{ImpactKill(1, 120, 2, 2, WEAPON_WORLD)}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_TRUE(Replay.Run().PendingEpisodes().empty());
}

TEST(ImpactLiveAdapter, LiveUnknownJumpStateStillCountsRawFall)
{
	CReplay Replay(100, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false);
	SetAuthoritativeJumpEvidence(Target, 2, 2, false, false, 100, 99, true, 100, WEAPON_GUN);
	EXPECT_EQ(Target.m_JumpState, EImpactJumpState::UNKNOWN);
	EXPECT_TRUE(Target.m_DownwardShot);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 110;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 110;
	Target.m_FreezeEnd = 200;
	SetAuthoritativeJumpEvidence(Target, 2, 2, false, false, 100, 100, true, 0, WEAPON_GUN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target.m_ServerTick = 200;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	SetAuthoritativeJumpEvidence(Target, 2, 0, true, true, 100, 100, true, 0, WEAPON_GUN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::JUMP_RESET_CONFIRMED), 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
}

TEST(ImpactLiveAdapter, LivePanicShotAfterTrackedHarmDoesNotCreateResetEvent)
{
	CReplay Replay(102, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	Saver.m_HookedPlayer = 2;
	Saver.m_HookState = HOOK_GRABBED;
	SImpactGameplayInput Target = ImpactInput(2, 100, true);
	Target.m_HarmfulOutcomeCandidateKnown = CImpactSystem::IsDeathTileOutcomeCandidate(true, true, true, true);
	Target.m_HarmfulOutcomeCandidate = Target.m_HarmfulOutcomeCandidateKnown;
	ASSERT_TRUE(Target.m_HarmfulOutcomeCandidate);
	// A first frozen snapshot is not itself a transition. Establish the
	// authoritative same-generation unfrozen baseline before tracking this
	// harmful freeze episode.
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {ImpactInput(1, 90), ImpactInput(2, 90)}));
	Saver.m_ServerTick = Target.m_ServerTick = 100;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target.m_ServerTick = 101;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	EXPECT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);

	// The tracked harmful episode exists before the self downward shot. The
	// recovery snapshot is followed by a refreeze, so this is a panic/recovery
	// action rather than a reset episode.
	Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	SetAuthoritativeJumpEvidence(Target, 2, 2, false, true, 101, 100, true, 100, WEAPON_GUN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 120;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 120;
	Target.m_FreezeEnd = 220;
	Target.m_HarmfulOutcomeCandidateKnown = false;
	Target.m_HarmfulOutcomeCandidate = false;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	// This refreeze has no independently proven external-recovery need. The
	// regression is about rejecting a panic shot as Jump Reset, not about using
	// the still-active hook as a teammate-origin Fall suppression.
	Target.m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
	SetAuthoritativeJumpEvidence(Target, 2, 2, false, true, 101, 101, true, 0, WEAPON_GUN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 220;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	SetAuthoritativeJumpEvidence(Target, 2, 0, true, true, 101, 101, true, 0, WEAPON_GUN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::JUMP_RESET_CONFIRMED), 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
}

TEST(ImpactLiveAdapter, LiveFreezeWithoutDownwardShotWithoutHarmfulEvidenceStaysActive)
{
	CReplay Replay(103, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false);
	SetAuthoritativeJumpEvidence(Target, 2, 2, false, true, 100, 100, true, 0, WEAPON_GUN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 110;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 110;
	Target.m_FreezeEnd = 210;
	Target.m_SupportKnown = true;
	Target.m_Supported = false;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	Target.m_OrdinaryFreezeTileKnown = true;
	Target.m_OrdinaryFreezeTile = false;
	SetAuthoritativeJumpEvidence(Target, 2, 2, false, true, 100, 100, true, 0, WEAPON_GUN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 210;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	SetAuthoritativeJumpEvidence(Target, 2, 0, true, true, 100, 100, true, 0, WEAPON_GUN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::JUMP_RESET_CONFIRMED), 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);

	// Natural unfreeze is the target's own recovery. It closes the active
	// problem episode only after the normal stable-recovery boundary; it does
	// not retroactively manufacture a harmful Fall from missing physical harm
	// evidence.
	Target.m_ServerTick = 245;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_TRUE(Replay.Run().PendingEpisodes().empty());
}

TEST(ImpactLiveAdapter, StationaryOrdinaryFreezeTileRefreshConfirmsFallBeforeFreezeEnd)
{
	CReplay Replay(114, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 90);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_X = -32;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	Target.m_OrdinaryFreezeTileKnown = true;
	Target.m_OrdinaryFreezeTile = false;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// The Tee enters an ordinary freeze tile and is physically stationary on
	// static map support. Freeze entry only creates a pending episode.
	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_X = 0;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 250;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	Target.m_OrdinaryFreezeTileKnown = true;
	Target.m_OrdinaryFreezeTile = true;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_EQ(Replay.Run().PendingEpisodes().front().m_PhysicalOutcome, EImpactPhysicalOutcome::UNKNOWN);

	// The ordinary freeze tile plus an authoritative unsafe map position is the
	// positive helplessness fact. Freeze entry itself remains observation-only.
	Saver.m_ServerTick = Target.m_ServerTick = 101;
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_TRUE(Replay.Run().PendingEpisodes().front().m_FallAlreadyConfirmed);

	// DDNet Freeze() now refreshes the ordinary freeze and publishes a later
	// FreezeStart plus an extended absolute FreezeEnd. Refresh evidence alone is
	// not a Fall producer; the target may still later recover autonomously.
	Saver.m_ServerTick = Target.m_ServerTick = 151;
	Target.m_FreezeStart = 151;
	Target.m_FreezeEnd = 301;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 1);
	EXPECT_EQ(Replay.Run().PendingEpisodes().size(), 1U);

	// Refresh/expiry does not create another Fall after the independent physical
	// decision has already been emitted.
	Saver.m_ServerTick = Target.m_ServerTick = 302;
	Target.m_FreezeEnd = 301;
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), 1);
}

TEST(ImpactLiveAdapter, MovingFreezeTileRefreshDoesNotConfirmFallFromTileAlone)
{
	CReplay Replay(115, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 90);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_X = -32;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 100;
	Target.m_Frozen = true;
	Target.m_X = 0;
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 250;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	Target.m_OrdinaryFreezeTileKnown = true;
	Target.m_OrdinaryFreezeTile = true;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// The later refresh is real, but the target is moving. That snapshot alone
	// does not prove a trapped autonomous outcome, so Fall stays pending.
	Saver.m_ServerTick = Target.m_ServerTick = 151;
	Target.m_X = 4;
	Target.m_VelX = 1;
	Target.m_SafeCollisionPositionKnown = false;
	Target.m_FreezeStart = 151;
	Target.m_FreezeEnd = 301;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_FALSE(Replay.Run().PendingEpisodes().front().m_FallAlreadyConfirmed);
	EXPECT_EQ(Replay.Run().PendingEpisodes().front().m_PhysicalOutcome, EImpactPhysicalOutcome::UNKNOWN);
}

TEST(ImpactLiveAdapter, LiveExpiredDownwardShotDoesNotSuppressLaterFall)
{
	CReplay Replay(104, TestContext());
	StartPair(Replay);

	SImpactGameplayInput Saver = ImpactInput(1, 100);
	SImpactGameplayInput Target = ImpactInput(2, 100, false);
	SetAuthoritativeJumpEvidence(Target, 2, 2, false, true, 100, 99, true, 100, WEAPON_GUN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	// A reliable non-frozen observation without a new shot closes the pending
	// reset context. A later freeze must not inherit the old shot.
	Target.m_ServerTick = 110;
	SetAuthoritativeJumpEvidence(Target, 2, 2, false, true, 101, 101, true, 0, WEAPON_GUN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Target.m_ServerTick = 120;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 120;
	Target.m_FreezeEnd = 220;
	SetAuthoritativeJumpEvidence(Target, 2, 2, false, true, 101, 101, true, 0, WEAPON_GUN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Target.m_ServerTick = 220;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	SetAuthoritativeJumpEvidence(Target, 2, 0, true, true, 101, 101, true, 0, WEAPON_GUN);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::JUMP_RESET_CONFIRMED), 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
}

TEST(ImpactPhase2, SaveConfirmsAtSevenTenthsTickWindow)
{
	CReplay Replay(1, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	SetHooked(Saver, 2);
	SetMeaningfulHookSupport(Saver);
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	SetPhysicalHarmfulOutcome(Target);
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	Target.m_ServerTick = 101;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	EXPECT_TRUE(Replay.SubmitGameplay({Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	// The external-recovery frame also owns a pending harmful episode. It is
	// resolved together with the Save and must not be confused with the Save
	// confirmation episode.
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 2U);
	Saver.m_ServerTick = Target.m_ServerTick = 145;
	EXPECT_TRUE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().Participants()[0].m_RawStats.m_Saves, 1);
	EXPECT_EQ(Replay.Run().PendingEpisodes().size(), 0U);
}

TEST(ImpactPhase2, ConfirmationWindowUsesCeilSevenTenthsOfTickSpeed)
{
	EXPECT_EQ(CImpactPhase2::ConfirmationTicks(50), 35);
	EXPECT_EQ(CImpactPhase2::ConfirmationTicks(100), 70);
	EXPECT_EQ(CImpactPhase2::ConfirmationTicks(1), 1);
}

TEST(ImpactPhase2, SaveDoesNotConfirmAtZeroPointSixNineSeconds)
{
	SRunContext Context = TestContext();
	Context.m_TickSpeed = 100;
	CReplay Replay(2, Context);
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	SetHooked(Saver, 2);
	SetMeaningfulHookSupport(Saver);
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	SetPhysicalHarmfulOutcome(Target);
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 179;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
	EXPECT_EQ(Replay.Run().PendingEpisodes().size(), 2U);
}

TEST(ImpactPhase2, SaveConfirmsAtZeroPointSevenSeconds)
{
	SRunContext Context = TestContext();
	Context.m_TickSpeed = 100;
	CReplay Replay(63, Context);
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	SetHooked(Saver, 2);
	SetMeaningfulHookSupport(Saver);
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	SetPhysicalHarmfulOutcome(Target);
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 180;
	EXPECT_TRUE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
}

TEST(ImpactPhase2, SaveConfirmsAtZeroPointSevenOneSeconds)
{
	SRunContext Context = TestContext();
	Context.m_TickSpeed = 100;
	CReplay Replay(64, Context);
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	SetHooked(Saver, 2);
	SetMeaningfulHookSupport(Saver);
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	SetPhysicalHarmfulOutcome(Target);
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 181;
	EXPECT_TRUE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
}

TEST(ImpactPhase2, RefreezeBeforeSevenTenthsCancelsSave)
{
	CReplay Replay(65, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	SetHooked(Saver, 2);
	ASSERT_FALSE(Replay.SubmitGameplay({ImpactFrame(1, 90), ImpactFrame(2, 90)}));
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 144;
	Target.m_Frozen = true;
	Target.m_FreezeEnd = 244;
	// This retry is the same origin episode. It must not manufacture a second
	// raw Fall, even though the first harmful entry was already counted.
	Target.m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
}

TEST(ImpactPhase2, ConfirmedSaveStaysSeparateFromLaterPendingDeath)
{
	CReplay Replay(3, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	SetHooked(Saver, 2);
	SetMeaningfulHookSupport(Saver);
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	SetPhysicalHarmfulOutcome(Target);
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	Target.m_ServerTick = 101;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	EXPECT_TRUE(Replay.SubmitGameplay({Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 145;
	Replay.SubmitGameplay({Saver, Target});
	// A later death is only attributable after a new harmful episode has been
	// observed; a previously confirmed Save is not itself a Fall candidate.
	Saver.m_ServerTick = Target.m_ServerTick = 180;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 180;
	Target.m_FreezeEnd = 280;
	Target.m_Alive = true;
	Target.m_Active = true;
	Replay.SubmitGameplay({Saver, Target});
	Target.m_ServerTick = 200;
	Target.m_Alive = false;
	Target.m_Active = false;
	// The later freeze is still pending. Death must close it without creating a
	// second raw Fall; the earlier necessary-rescue Fall remains independent.
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}, {ImpactKill(1, 200, 2, 2, WEAPON_WORLD)}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_TRUE(Replay.Run().PendingEpisodes().empty());
}

TEST(ImpactPhase2, FailedSaveIsNeitherSaveNorFall)
{
	CReplay Replay(4, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	SetHooked(Saver, 2);
	ASSERT_FALSE(Replay.SubmitGameplay({ImpactFrame(1, 90), ImpactFrame(2, 90)}));
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Replay.SubmitGameplay({Saver, Target});
	Target.m_ServerTick = 150;
	Target.m_Frozen = true;
	Target.m_FreezeEnd = 250;
	Target.m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
}

TEST(ImpactPhase2, FinishBeforeConfirmationCancelsSave)
{
	CReplay Replay(5, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	SetHooked(Saver, 2);
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_Finished = Target.m_Finished = true;
	Saver.m_ServerTick = Target.m_ServerTick = 130;
	EXPECT_TRUE(Replay.SubmitGameplay({Saver, Target}, {ImpactFinish(1, 130, 1), ImpactFinish(2, 130, 2)}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::FINALIZED);
}

TEST(ImpactPhase2, FinishAfterConfirmationKeepsSave)
{
	CReplay Replay(6, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	SetHooked(Saver, 2);
	SetMeaningfulHookSupport(Saver);
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	SetPhysicalHarmfulOutcome(Target);
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 175;
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_Finished = Target.m_Finished = true;
	Saver.m_ServerTick = Target.m_ServerTick = 180;
	EXPECT_TRUE(Replay.SubmitGameplay({Saver, Target}, {ImpactFinish(1, 180, 1), ImpactFinish(2, 180, 2)}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
}

TEST(ImpactPhase2, AmbiguousCooperativeSaveHasNoOwner)
{
	CReplay Replay(7, TestContext());
	StartPair(Replay);
	STickEventBatch StartThird;
	StartThird.m_ServerTick = 20;
	StartThird.m_vCandidates.push_back(StartEvent(3, TestId(3), 20, "C"));
	ASSERT_TRUE(Replay.Submit(StartThird));
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	SetHooked(Saver, 2, true);
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	SetPhysicalHarmfulOutcome(Target);
	SImpactGameplayFrame Helper = ImpactFrame(3, 100, false, true, true);
	SetHooked(Helper, 2, true);
	Replay.SubmitGameplay({Saver, Target, Helper});
	Saver.m_ServerTick = Target.m_ServerTick = Helper.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	Replay.SubmitGameplay({Saver, Target, Helper});
	Saver.m_ServerTick = Target.m_ServerTick = Helper.m_ServerTick = 175;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target, Helper}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
	EXPECT_EQ(Replay.Run().Participants()[0].m_RawStats.m_Saves, 0);
	const auto SaveEvent = std::find_if(Replay.Run().Events().rbegin(), Replay.Run().Events().rend(), [](const SImpactEvent &Event) {
		return Event.m_Type == EImpactEventType::SAVE_CONFIRMED;
	});
	EXPECT_EQ(SaveEvent, Replay.Run().Events().rend());
}

TEST(ImpactPhase2, DecisiveSaverGetsOnlyPersonalSave)
{
	CReplay Replay(8, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	SetHooked(Saver, 2);
	SetMeaningfulHookSupport(Saver);
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	SetPhysicalHarmfulOutcome(Target);
	SImpactGameplayFrame Helper = ImpactFrame(3, 100, false, true, false);
	Replay.SubmitGameplay({Saver, Target, Helper});
	Saver.m_ServerTick = Target.m_ServerTick = Helper.m_ServerTick = 110;
	Target.m_X = 32;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	ClearPhysicalHarmfulOutcome(Target);
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Replay.SubmitGameplay({Saver, Target, Helper});
	Saver.m_ServerTick = Target.m_ServerTick = Helper.m_ServerTick = 175;
	Replay.SubmitGameplay({Saver, Target, Helper});
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().Participants()[0].m_RawStats.m_Saves, 1);
	EXPECT_EQ(Replay.Run().Participants()[1].m_RawStats.m_Saves, 0);
}

TEST(ImpactPhase2, UnfreezeWithoutCausalActionIsNotSave)
{
	CReplay Replay(9, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame A = ImpactFrame(1, 100);
	SImpactGameplayFrame B = ImpactFrame(2, 100, true);
	Replay.SubmitGameplay({A, B});
	B.m_ServerTick = 110;
	B.m_Frozen = false;
	B.m_FreezeEnd = 0;
	EXPECT_FALSE(Replay.SubmitGameplay({A, B}));
	B.m_ServerTick = 200;
	EXPECT_FALSE(Replay.SubmitGameplay({A, B}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
}

TEST(ImpactPhase2, PreStartHelperCannotCreatePersonalSave)
{
	CReplay Replay(10, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	SetPhysicalHarmfulOutcome(Target);
	SImpactGameplayFrame Helper = ImpactFrame(3, 100, false, true, false);
	SetHooked(Helper, 2, true);
	Replay.SubmitGameplay({ImpactFrame(1, 100), Target, Helper});
	Target.m_ServerTick = Helper.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	EXPECT_FALSE(Replay.SubmitGameplay({ImpactFrame(1, 110), Target, Helper}));
	Target.m_ServerTick = Helper.m_ServerTick = 175;
	EXPECT_FALSE(Replay.SubmitGameplay({ImpactFrame(1, 175), Target, Helper}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
	EXPECT_EQ(Replay.Run().Participants()[0].m_RawStats.m_Saves, 0);
	EXPECT_EQ(Replay.Run().Participants()[1].m_RawStats.m_Saves, 0);
}

TEST(ImpactPhase2, FrozenContactAloneDoesNotCreateFall)
{
	CReplay Replay(11, TestContext());
	StartPair(Replay);
	EXPECT_FALSE(Replay.SubmitGameplay({ImpactFrame(1, 100), ImpactFrame(2, 100, true)}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
}

TEST(ImpactPhase2, SafeOneTileRecoveryDoesNotCreateFall)
{
	CReplay Replay(12, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	SetHooked(Saver, 2);
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true, true, true, 1, false, EImpactRecoveryNeed::SELF_SAFE);
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_UpDowns, 0);
}

TEST(ImpactPhase2, SafeFrozenHookIsNotSaveOrFall)
{
	CReplay Replay(77, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	SetHooked(Saver, 2);
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true, true, true, 1, false, EImpactRecoveryNeed::SELF_SAFE);
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_RecoveryNeed = EImpactRecoveryNeed::SELF_SAFE;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 180;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_UpDowns, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
}

TEST(ImpactPhase2, IntentionalRouteFreezeDoesNotCreateFall)
{
	CReplay Replay(13, TestContext());
	StartPair(Replay);
	Replay.SubmitGameplay({ImpactFrame(1, 100), ImpactFrame(2, 100, true, true, true, 1, false, EImpactRecoveryNeed::SELF_SAFE)});
	EXPECT_FALSE(Replay.SubmitGameplay({ImpactFrame(1, 160), ImpactFrame(2, 160)}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
}

TEST(ImpactPhase2, SelfCorrectionDoesNotCreateFall)
{
	CReplay Replay(14, TestContext());
	StartPair(Replay);
	Replay.SubmitGameplay({ImpactFrame(1, 100), ImpactFrame(2, 100, false, true, true, 1, false, EImpactRecoveryNeed::SELF_SAFE)});
	EXPECT_FALSE(Replay.SubmitGameplay({ImpactFrame(1, 120), ImpactFrame(2, 120, false, true, true, 1, false, EImpactRecoveryNeed::SELF_SAFE)}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
}

TEST(ImpactPhase2, ServerConfirmedWorldDeathDoesNotCreatePendingFall)
{
	CReplay Replay(15, TestContext());
	StartPair(Replay);
	Replay.SubmitGameplay({ImpactFrame(1, 100), ImpactFrame(2, 100)});
	SImpactGameplayFrame HarmfulFreeze = ImpactFrame(2, 110, true);
	Replay.SubmitGameplay({ImpactFrame(1, 110), HarmfulFreeze});
	SImpactGameplayFrame Victim = ImpactFrame(2, 120, false, false);
	EXPECT_FALSE(Replay.SubmitGameplay({ImpactFrame(1, 120), Victim}, {ImpactKill(1, 120, 2, 2, WEAPON_WORLD)}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_TRUE(Replay.Run().PendingEpisodes().empty());
}

TEST(ImpactLiveAdapter, LiveDeathUsesLastKnownCharacterContextWithoutCreatingFall)
{
	CReplay Replay(87, TestContext());
	StartPair(Replay);
	SubmitLiveEquivalentInputs(Replay, {ImpactInput(1, 100), ImpactInput(2, 100)});
	SImpactGameplayInput HarmfulFreeze = ImpactInput(2, 110, true);
	SubmitLiveEquivalentInputs(Replay, {ImpactInput(1, 110), HarmfulFreeze});

	// The server can remove the victim's character object in the death snapshot
	// while retaining PlayerInfo. The live adapter therefore supplies a present
	// participant with unknown current character state.
	const SImpactGameplayInput MissingVictim = MissingCharacterInput(2, 120);
	EXPECT_FALSE(SubmitLiveEquivalentInputs(Replay, {ImpactInput(1, 120), MissingVictim}, {ImpactKill(1, 120, 2, 2, WEAPON_WORLD)}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_TRUE(Replay.Run().PendingEpisodes().empty());
}

TEST(ImpactLiveAdapter, UntrackedLiveDeathDoesNotCreateFall)
{
	CReplay Replay(88, TestContext());
	StartPair(Replay);
	Replay.SubmitGameplayInputs({ImpactInput(1, 100), ImpactInput(2, 100)});
	const SImpactGameplayInput MissingVictim = MissingCharacterInput(2, 120);
	EXPECT_FALSE(Replay.SubmitGameplayInputs({ImpactInput(1, 120), MissingVictim}, {ImpactKill(1, 120, 2, 2, WEAPON_WORLD)}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
}

TEST(ImpactPhase2, ConfirmedHarmAndRecoveryProduceOneFallAndOneSave)
{
	CReplay Replay(78, TestContext());
	StartPair(Replay);
	ASSERT_FALSE(Replay.SubmitGameplay({ImpactFrame(1, 90), ImpactFrame(2, 90)}));
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	SetHooked(Saver, 2, true);
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	Target.m_HarmfulOutcomeCandidateKnown = true;
	Target.m_HarmfulOutcomeCandidate = true;
	SetSelfCausedHarm(Target);
	const SImpactGameplayMessage Harm = ImpactSignal(1, EImpactGameplayMessageType::CONFIRMED_HARM, 100, TestId(2), TestId(2));
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}, {Harm}));
	Target.m_ServerTick = 101;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	EXPECT_TRUE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_HarmfulOutcomeCandidateKnown = false;
	Target.m_HarmfulOutcomeCandidate = false;
	Target.m_X = 32;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 145;
	EXPECT_TRUE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
}

TEST(ImpactLiveAdapter, TwoStartedParticipantsReachNonZeroFallAndSaveStats)
{
	// This is the live ordering that was missing in the debug log: personal
	// Starts register both participants, gameplay frames are then normalized from
	// those run participants, Phase 2 observes the harmful freeze and the
	// target-specific recovery, and only the canonical events update counters.
	CReplay Replay(114, TestContext(5));
	StartPair(Replay, 10, 20);
	ASSERT_EQ(Replay.Run().StartedParticipantCount(), 2);
	ASSERT_EQ(Replay.Run().EligibleParticipantCount(), 2);
	EXPECT_TRUE(CImpactSystem::IsPhase2ProcessingAllowed(EAnalysisState::ACTIVE, Replay.Run().ActiveParticipantCount()));

	SImpactGameplayInput Saver = ImpactInput(1, 90, false, true, false, 5);
	SImpactGameplayInput Target = ImpactInput(2, 90, false, true, false, 5);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver = ImpactInput(1, 100, false, true, false, 5);
	SetHooked(Saver, 2, true);
	Target = ImpactInput(2, 100, true, true, false, 5, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_FreezeStart = 100;
	Target.m_FreezeEnd = 200;
	SetPhysicalHarmfulOutcome(Target);
	// The compatibility marker is not itself a raw Fall producer. The next
	// authoritative target frame carries the positive helplessness fact.
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 101;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 102;
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);

	// The hook changes the observed target movement while the target is still
	// frozen. This is the physical recovery evidence, not a direct Save event.
	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_X = 16;
	Target.m_VelX = 2;
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 120;
	ReleaseHook(Saver);
	Target.m_X = 32;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 155;
	ASSERT_TRUE(SubmitLiveEquivalentInputs(Replay, {Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	ASSERT_EQ(Replay.Run().Participants().size(), 2U);
	EXPECT_EQ(Replay.Run().Participants()[0].m_RawStats.m_Falls, 0);
	EXPECT_EQ(Replay.Run().Participants()[0].m_RawStats.m_Saves, 1);
	EXPECT_EQ(Replay.Run().Participants()[1].m_RawStats.m_Falls, 1);
	EXPECT_EQ(Replay.Run().Participants()[1].m_RawStats.m_Saves, 0);
	const SRunSnapshot Snapshot = Replay.Run().Snapshot(155);
	EXPECT_EQ(Snapshot.m_RawStats.m_Falls, 1);
	EXPECT_EQ(Snapshot.m_RawStats.m_Saves, 1);
}

TEST(ImpactPhase2, SoleUnfrozenConfirmedSaveAddsOneClutch)
{
	CReplay Replay(115, TestContext());
	StartPair(Replay);

	// Establish the last-unfrozen state before the target enters the rescue
	// episode. The clutch latch must exist before the target is restored.
	SImpactGameplayFrame Saver = ImpactFrame(1, 90, false);
	SImpactGameplayFrame Target = ImpactFrame(2, 90, true);
	Target.m_FreezeEnd = 200;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 100;
	SetHooked(Saver, 2, true);
	SetPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 145;
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 1);
	ASSERT_EQ(Replay.Run().Participants().size(), 2U);
	EXPECT_EQ(Replay.Run().Participants()[0].m_RawStats.m_Saves, 1);
	EXPECT_EQ(Replay.Run().Participants()[0].m_RawStats.m_Clutches, 1);
	EXPECT_EQ(Replay.Run().Participants()[1].m_RawStats.m_Clutches, 0);
}

TEST(ImpactPhase2, TrueClutchWithAllOthersExternalRequired)
{
	CReplay Replay(121, TestContext());
	StartPair(Replay);
	StartThirdParticipant(Replay);

	SImpactGameplayFrame Saver = ImpactFrame(1, 90, false);
	SImpactGameplayFrame Target = ImpactFrame(2, 90, true);
	Target.m_FreezeEnd = 200;
	SImpactGameplayFrame Other = ImpactFrame(3, 90, true, true, true, 1, false, EImpactRecoveryNeed::EXTERNAL_REQUIRED);
	Other.m_FreezeEnd = 200;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 100;
	SetHooked(Saver, 2, true);
	SetPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 145;
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target, Other}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 1);
}

TEST(ImpactPhase2, TwoConfirmedFallsAllowTrueClutch)
{
	CReplay Replay(149, TestContext());
	StartPair(Replay);
	StartThirdParticipant(Replay);

	SImpactGameplayFrame Saver = ImpactFrame(1, 90, false);
	SImpactGameplayFrame Target = ImpactFrame(2, 90, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_FreezeEnd = 200;
	SImpactGameplayFrame Other = ImpactFrame(3, 90, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Other.m_FreezeEnd = 200;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	// Both frozen teammates receive the same authoritative negative physical
	// evidence that the Phase2 Fall producer uses. They remain frozen after the
	// evidence, so the current confirmed Fall episode must make them
	// NON_FUNCTIONAL for Clutch without changing Save detection.
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 100;
	SetHooked(Saver, 2, true);
	SetPhysicalHarmfulOutcome(Target);
	SetPhysicalHarmfulOutcome(Other);
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	Other.m_AutonomousRecoveryKnown = true;
	Other.m_AutonomousRecoveryPossible = false;
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target, Other}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 2);

	// Restore only the saved target. The other confirmed current Fall remains
	// frozen and must keep the sole-functional crisis proven through Save's
	// confirmation window.
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 145;
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target, Other}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 1);
}

TEST(ImpactPhase2, ConfirmedCurrentFallMakesFrozenTeeNonFunctionalForClutch)
{
	CReplay Replay(150, TestContext());
	StartPair(Replay);
	StartThirdParticipant(Replay);
	SImpactGameplayFrame Saver;
	SImpactGameplayFrame Target;
	SImpactGameplayFrame Other;
	ASSERT_TRUE(BeginTwoConfirmedFallClutchEpisode(Replay, Saver, Target, Other));

	// The current confirmed Fall is consumed by Clutch. Restore the saved target;
	// the other confirmed Fall keeps the crisis proven through Save confirmation.
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 145;
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target, Other}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 2);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 1);
}

TEST(ImpactPhase2, ConfirmedFallClutchCandidateSurvivesTargetRestoration)
{
	CReplay Replay(151, TestContext());
	StartPair(Replay);
	StartThirdParticipant(Replay);
	SImpactGameplayFrame Saver;
	SImpactGameplayFrame Target;
	SImpactGameplayFrame Other;
	ASSERT_TRUE(BeginTwoConfirmedFallClutchEpisode(Replay, Saver, Target, Other));

	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	// The target is now restored, but it is excluded from the final Clutch proof.
	// The other Tee's current confirmed Fall must keep the already-proven crisis
	// alive until the canonical Save confirmation boundary.
	const auto SaveCandidate = std::find_if(Replay.Run().PendingEpisodes().begin(), Replay.Run().PendingEpisodes().end(),
		[](const SImpactPendingEpisode &Episode) {
			return Episode.m_Type == EImpactEventType::SAVE_CONFIRMED;
		});
	ASSERT_NE(SaveCandidate, Replay.Run().PendingEpisodes().end());
	EXPECT_TRUE(SaveCandidate->m_ClutchCandidate);
	EXPECT_NE(SaveCandidate->m_ClutchEpisodeId, 0U);

	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 145;
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target, Other}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 1);
}

TEST(ImpactPhase2, ConfirmedFallTrueClutchAwardsOnCanonicalSaveConfirmation)
{
	CReplay Replay(152, TestContext());
	StartPair(Replay);
	StartThirdParticipant(Replay);
	SImpactGameplayFrame Saver;
	SImpactGameplayFrame Target;
	SImpactGameplayFrame Other;
	ASSERT_TRUE(BeginTwoConfirmedFallClutchEpisode(Replay, Saver, Target, Other));

	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 0);

	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 145;
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target, Other}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 1);
}

TEST(ImpactPhase2, RecoveredTeeAfterConfirmedFallBecomesFunctional)
{
	CReplay Replay(153, TestContext());
	StartPair(Replay);
	StartThirdParticipant(Replay);

	SImpactGameplayFrame Saver = ImpactFrame(1, 90, false);
	SImpactGameplayFrame Target = ImpactFrame(2, 90, false);
	SImpactGameplayFrame Other = ImpactFrame(3, 90, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Other.m_FreezeEnd = 200;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	// Establish and confirm a real Fall for Other.
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 100;
	SetConfirmedExternalRecoveryEvidence(Other);
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target, Other}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	// A stable unfrozen boundary closes the current problem episode. The old Fall
	// remains a historical result, not a current Clutch classification.
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 110;
	Other.m_Frozen = false;
	Other.m_FreezeStart = -1;
	Other.m_FreezeEnd = 0;
	ClearPhysicalHarmfulOutcome(Other);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 145;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	// Other is plainly functional when Saver later rescues Target.
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 200;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 200;
	Target.m_FreezeEnd = 300;
	Target.m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 210;
	SetHooked(Saver, 2, true);
	SetPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 220;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 255;
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target, Other}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 0);
}

TEST(ImpactPhase2, HistoricalFallFromOldEpisodeDoesNotMakeCurrentTeeNonFunctional)
{
	CReplay Replay(154, TestContext());
	StartPair(Replay);
	StartThirdParticipant(Replay);

	SImpactGameplayFrame Saver = ImpactFrame(1, 90, false);
	SImpactGameplayFrame Target = ImpactFrame(2, 90, false);
	SImpactGameplayFrame Other = ImpactFrame(3, 90, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Other.m_FreezeEnd = 200;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	// Confirm an old Fall and close it at a stable recovery boundary.
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 100;
	SetConfirmedExternalRecoveryEvidence(Other);
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target, Other}));
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 110;
	Other.m_Frozen = false;
	Other.m_FreezeStart = -1;
	Other.m_FreezeEnd = 0;
	ClearPhysicalHarmfulOutcome(Other);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 145;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	// A later independent freeze has a new problem identity and is unresolved.
	// The old confirmed Fall must not satisfy the new episode's Clutch predicate.
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 200;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 200;
	Target.m_FreezeEnd = 300;
	Target.m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
	Other.m_Frozen = true;
	Other.m_FreezeStart = 200;
	Other.m_FreezeEnd = 300;
	Other.m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
	Other.m_AutonomousRecoveryKnown = false;
	Other.m_AutonomousRecoveryPossible = false;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 210;
	SetHooked(Saver, 2, true);
	SetPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 220;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 255;
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target, Other}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 0);
}

TEST(ImpactPhase2, NewFreezeDoesNotInheritOldConfirmedFall)
{
	CReplay Replay(155, TestContext());
	StartPair(Replay);
	StartThirdParticipant(Replay);

	SImpactGameplayFrame Saver = ImpactFrame(1, 90, false);
	SImpactGameplayFrame Target = ImpactFrame(2, 90, false);
	SImpactGameplayFrame Other = ImpactFrame(3, 90, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Other.m_FreezeEnd = 200;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 100;
	SetConfirmedExternalRecoveryEvidence(Other);
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target, Other}));
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 110;
	Other.m_Frozen = false;
	Other.m_FreezeStart = -1;
	Other.m_FreezeEnd = 0;
	ClearPhysicalHarmfulOutcome(Other);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 145;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 200;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 200;
	Target.m_FreezeEnd = 300;
	Target.m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
	Other.m_Frozen = true;
	Other.m_FreezeStart = 200;
	Other.m_FreezeEnd = 300;
	Other.m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
	Other.m_AutonomousRecoveryKnown = false;
	Other.m_AutonomousRecoveryPossible = false;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	SetHooked(Saver, 2, true);
	SetPhysicalHarmfulOutcome(Target);
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 210;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 220;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 255;
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target, Other}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 0);
}

TEST(ImpactPhase2, UnknownRecoveryDoesNotProveClutch)
{
	CReplay Replay(133, TestContext());
	StartPair(Replay);
	StartThirdParticipant(Replay);

	SImpactGameplayFrame Saver = ImpactFrame(1, 90, false);
	SImpactGameplayFrame Target = ImpactFrame(2, 90, true);
	Target.m_FreezeEnd = 200;
	SImpactGameplayFrame Other = ImpactFrame(3, 90, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Other.m_FreezeEnd = 200;
	// An evaluated native simulation with no positive self-safe result is still
	// unresolved. It must not be promoted to external recovery merely because the
	// self-safe veto is absent.
	Other.m_AutonomousRecoveryEvaluated = true;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 100;
	SetHooked(Saver, 2, true);
	SetPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 145;
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target, Other}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 0);
}

TEST(ImpactPhase2, UnknownCanResolveWithinSameEpisodeBeforeDecision)
{
	CReplay Replay(131, TestContext());
	StartPair(Replay);
	StartThirdParticipant(Replay);

	SImpactGameplayFrame Saver = ImpactFrame(1, 90, false);
	SImpactGameplayFrame Target = ImpactFrame(2, 90, true);
	Target.m_FreezeEnd = 200;
	SImpactGameplayFrame Other = ImpactFrame(3, 90, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Other.m_FreezeEnd = 200;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 100;
	SetHooked(Saver, 2, true);
	SetPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	// The other Tee is unresolved while the crisis is being latched, then the
	// same authoritative restoration frame proves EXTERNAL_REQUIRED. Clutch
	// must use that bounded rescue frame, not the initial UNKNOWN latch.
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	Other.m_RecoveryNeed = EImpactRecoveryNeed::EXTERNAL_REQUIRED;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 145;
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target, Other}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 1);
}

TEST(ImpactPhase2, AledTrueClutch)
{
	CReplay Replay(132, TestContext());
	StartPair(Replay);
	StartThirdParticipant(Replay);

	SImpactGameplayFrame Saver = ImpactFrame(1, 90, false);
	SImpactGameplayFrame Target = ImpactFrame(2, 90, true);
	Target.m_FreezeEnd = 200;
	SImpactGameplayFrame Other = ImpactFrame(3, 90, true, true, true, 1, false, EImpactRecoveryNeed::EXTERNAL_REQUIRED);
	Other.m_FreezeEnd = 200;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	const SImpactGameplayMessage Message = ImpactSignal(1, EImpactGameplayMessageType::CONFIRMED_ALED, 200, TestId(1), TestId(2));
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 200;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}, {Message}));

	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 235;
	EXPECT_TRUE(Replay.SubmitGameplay({Saver, Target, Other}));
	EXPECT_EQ(Replay.Run().RawStats().m_Aleds, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 1);
}

TEST(ImpactPhase2, NoClutchWithSelfSafeFrozenTeammate)
{
	CReplay Replay(122, TestContext());
	StartPair(Replay);
	StartThirdParticipant(Replay);

	SImpactGameplayFrame Saver = ImpactFrame(1, 90, false);
	SImpactGameplayFrame Target = ImpactFrame(2, 90, true);
	Target.m_FreezeEnd = 200;
	SImpactGameplayFrame SelfSafe = ImpactFrame(3, 90, true, true, true, 1, false, EImpactRecoveryNeed::SELF_SAFE);
	SelfSafe.m_FreezeEnd = 200;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, SelfSafe}));

	Saver.m_ServerTick = Target.m_ServerTick = SelfSafe.m_ServerTick = 100;
	SetHooked(Saver, 2, true);
	SetPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, SelfSafe}));

	Saver.m_ServerTick = Target.m_ServerTick = SelfSafe.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, SelfSafe}));

	Saver.m_ServerTick = Target.m_ServerTick = SelfSafe.m_ServerTick = 145;
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target, SelfSafe}));
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 0);
}

TEST(ImpactPhase2, NoClutchWhenOtherParticipantNaturallyRecovers)
{
	CReplay Replay(123, TestContext());
	StartPair(Replay);
	StartThirdParticipant(Replay);

	SImpactGameplayFrame Saver = ImpactFrame(1, 90, false);
	SImpactGameplayFrame Target = ImpactFrame(2, 90, true);
	Target.m_FreezeEnd = 200;
	SImpactGameplayFrame Other = ImpactFrame(3, 90, true, true, true, 1, false, EImpactRecoveryNeed::SELF_SAFE);
	Other.m_FreezeEnd = 200;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 100;
	Other.m_Frozen = false;
	Other.m_FreezeEnd = 0;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 110;
	SetHooked(Saver, 2, true);
	SetPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 120;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 155;
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target, Other}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 0);
}

TEST(ImpactPhase2, UnknownNeverCreatesFalseClutch)
{
	CReplay Replay(124, TestContext());
	StartPair(Replay);
	StartThirdParticipant(Replay);

	SImpactGameplayFrame Saver = ImpactFrame(1, 90, false);
	SImpactGameplayFrame Target = ImpactFrame(2, 90, true);
	Target.m_FreezeEnd = 200;
	SImpactGameplayFrame Unknown = ImpactFrame(3, 90, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Unknown.m_FreezeEnd = 200;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Unknown}));

	Saver.m_ServerTick = Target.m_ServerTick = Unknown.m_ServerTick = 100;
	SetHooked(Saver, 2, true);
	SetPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Unknown}));

	Saver.m_ServerTick = Target.m_ServerTick = Unknown.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Unknown}));

	Saver.m_ServerTick = Target.m_ServerTick = Unknown.m_ServerTick = 145;
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target, Unknown}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 0);
}

TEST(ImpactPhase2, DelayedSelfSafeProofCancelsProvisionalClutch)
{
	CReplay Replay(134, TestContext());
	StartPair(Replay);
	StartThirdParticipant(Replay);

	SImpactGameplayFrame Saver = ImpactFrame(1, 90, false);
	SImpactGameplayFrame Target = ImpactFrame(2, 90, true);
	Target.m_FreezeEnd = 200;
	SImpactGameplayFrame Other = ImpactFrame(3, 90, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Other.m_FreezeEnd = 200;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 100;
	SetHooked(Saver, 2, true);
	SetPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	// The unresolved participant becomes positively SELF_SAFE before the target
	// reaches the restoration boundary. The provisional observation must not turn
	// into an awardable Clutch.
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 105;
	Other.m_RecoveryNeed = EImpactRecoveryNeed::SELF_SAFE;
	Other.m_AutonomousRecoveryKnown = true;
	Other.m_AutonomousRecoveryPossible = true;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 145;
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target, Other}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 0);
}

TEST(ImpactPhase2, PostActionSelfSafeSavedTargetPreservesProvisionalClutch)
{
	CReplay Replay(154, TestContext());
	StartPair(Replay);

	SImpactGameplayFrame Saver = ImpactFrame(1, 90, false);
	SImpactGameplayFrame Target = ImpactFrame(2, 90, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	Target.m_FreezeEnd = 200;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	// At first the frozen target's functional state is unknown. The saver then
	// produces target-specific physical change in this same episode.
	Saver.m_ServerTick = Target.m_ServerTick = 100;
	SetHooked(Saver, 2, true);
	SetPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	// This positive autonomous result arrives after the physical interaction.
	// It describes the helped target's post-action state, not whether the target
	// was functional before the saver began the rescue.
	Saver.m_ServerTick = Target.m_ServerTick = 105;
	Target.m_X = 32;
	Target.m_RecoveryNeed = EImpactRecoveryNeed::SELF_SAFE;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = true;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 145;
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 1);
}

TEST(ImpactPhase2, NotEngagedRosterMemberStillDoesNotBlockClutch)
{
	CReplay Replay(135, TestContext());
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(1), "AFK", 0, "test team roster"));
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(2), "Target", 0, "test team roster"));
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(3), "Saver", 0, "test team roster"));
	StartPairWithIds(Replay, TestId(2), TestId(3));

	SImpactGameplayFrame Afk = ImpactFrame(1, 90, false);
	SImpactGameplayFrame Saver = ImpactFrame(3, 90, false);
	SImpactGameplayFrame Target = ImpactFrame(2, 90, true);
	Target.m_FreezeEnd = 200;
	ASSERT_FALSE(Replay.SubmitGameplay({Afk, Saver, Target}));
	EXPECT_FALSE(Replay.Run().IsParticipantEngaged(TestId(1)));

	Saver.m_ServerTick = Target.m_ServerTick = Afk.m_ServerTick = 100;
	SetHooked(Saver, 2, true);
	SetPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Afk, Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = Afk.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Afk, Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = Afk.m_ServerTick = 145;
	ASSERT_TRUE(Replay.SubmitGameplay({Afk, Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 1);
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(3))->m_RawStats.m_Saves, 1);
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(3))->m_RawStats.m_Clutches, 1);
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(1))->m_RawStats.m_Clutches, 0);
}

static bool CompleteThreeMemberRescue(CReplay &Replay, EImpactRecoveryNeed BlockerNeed, bool BlockerFrozen, bool FinalizeBlocker = false)
{
	StartPair(Replay);
	StartThirdParticipant(Replay);
	if(FinalizeBlocker && !FinalizeReplayParticipant(Replay, 10, TestId(1), 80))
		return false;

	SImpactGameplayFrame Saver = ImpactFrame(3, 90, false);
	SImpactGameplayFrame Target = ImpactFrame(2, 90, true);
	Target.m_FreezeEnd = 200;
	SImpactGameplayFrame Blocker = ImpactFrame(1, 90, BlockerFrozen, true, true, 1, false, BlockerNeed);
	if(BlockerFrozen)
		Blocker.m_FreezeEnd = 200;
	std::vector<SImpactGameplayFrame> vFrames = {Saver, Target};
	if(!FinalizeBlocker)
		vFrames.push_back(Blocker);
	if(Replay.SubmitGameplay(vFrames))
		return false;

	Saver.m_ServerTick = Target.m_ServerTick = Blocker.m_ServerTick = 100;
	SetHooked(Saver, 2, true);
	SetPhysicalHarmfulOutcome(Target);
	vFrames = {Saver, Target};
	if(!FinalizeBlocker)
		vFrames.push_back(Blocker);
	if(Replay.SubmitGameplay(vFrames))
		return false;

	Saver.m_ServerTick = Target.m_ServerTick = Blocker.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	vFrames = {Saver, Target};
	if(!FinalizeBlocker)
		vFrames.push_back(Blocker);
	if(Replay.SubmitGameplay(vFrames))
		return false;

	Saver.m_ServerTick = Target.m_ServerTick = Blocker.m_ServerTick = 145;
	vFrames = {Saver, Target};
	if(!FinalizeBlocker)
		vFrames.push_back(Blocker);
	return Replay.SubmitGameplay(vFrames);
}

TEST(ImpactPhase2, EngagedFunctionalMemberStillBlocksClutch)
{
	CReplay Replay(2007, TestContext());
	ASSERT_TRUE(CompleteThreeMemberRescue(Replay, EImpactRecoveryNeed::UNKNOWN, false));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 0);
	EXPECT_TRUE(Replay.Run().IsParticipantEngaged(TestId(1)));
}

TEST(ImpactPhase2, EngagedNonFunctionalThirdPlayerDoesNotBlockClutch)
{
	CReplay Replay(2008, TestContext());
	ASSERT_TRUE(CompleteThreeMemberRescue(Replay, EImpactRecoveryNeed::EXTERNAL_REQUIRED, true));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 1);
	EXPECT_TRUE(Replay.Run().IsParticipantEngaged(TestId(1)));
}

TEST(ImpactPhase2, EngagedSelfSafeThirdPlayerBlocksClutch)
{
	CReplay Replay(2009, TestContext());
	ASSERT_TRUE(CompleteThreeMemberRescue(Replay, EImpactRecoveryNeed::SELF_SAFE, true));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 0);
	EXPECT_TRUE(Replay.Run().IsParticipantEngaged(TestId(1)));
}

TEST(ImpactPhase2, FinalizedMemberDoesNotBlockClutch)
{
	CReplay Replay(2010, TestContext());
	ASSERT_TRUE(CompleteThreeMemberRescue(Replay, EImpactRecoveryNeed::UNKNOWN, false, true));
	EXPECT_TRUE(Replay.Run().IsParticipantFinalized(TestId(1)));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 1);
}

TEST(ImpactLiveAdapter, LateAfkPlayerCanBecomeEngaged)
{
	CReplay Replay(2011, TestContext());
	StartPair(Replay);
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(3, 6), "Late Tee", 20, "late current team member"));
	ASSERT_EQ(Replay.Run().RosterMemberCount(), 3);

	SImpactGameplayFrame Saver = ImpactFrame(1, 90, false);
	SImpactGameplayFrame Target = ImpactFrame(2, 90, true);
	Target.m_FreezeEnd = 200;
	SImpactGameplayFrame LateTee = ImpactFrame(3, 90, false, true, true, 1);
	LateTee.m_Participant = TestId(3, 6);
	SetHooked(LateTee, 2, true);
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target, LateTee}));
	EXPECT_TRUE(Replay.Run().IsParticipantEngaged(TestId(3, 6)));
	EXPECT_EQ(FindRunParticipant(Replay.Run(), TestId(3, 6))->m_EngagedTick, 90);
	EXPECT_EQ(Replay.Run().RosterMemberCount(), 3);
}

TEST(ImpactLiveAdapter, LateEngagementDoesNotDuplicateParticipant)
{
	CReplay Replay(2012, TestContext());
	StartPair(Replay);
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(3, 6), "Late Tee", 20, "late current team member"));
	STickEventBatch Batch;
	Batch.m_ServerTick = 30;
	Batch.m_vCandidates.push_back(StartEvent(3, TestId(3, 6), 30, "Late Tee"));
	ASSERT_TRUE(Replay.Submit(Batch));
	EXPECT_EQ(Replay.Run().RosterMemberCount(), 3);
	EXPECT_EQ(Replay.Run().StartedParticipantCount(), 3);
	EXPECT_EQ(ImpactStatisticsParticipantCount(Replay.Run().Participants()), 3U);
	EXPECT_TRUE(Replay.Run().IsParticipantEngaged(TestId(3, 6)));
}

TEST(ImpactLiveAdapter, LaterVisibleStartDoesNotDuplicateParticipant)
{
	CReplay Replay(2013, TestContext());
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(1), "A", 0, "team roster"));
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(2), "B", 0, "team roster"));
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(3), "C", 0, "team roster"));
	STickEventBatch Batch;
	Batch.m_ServerTick = 10;
	Batch.m_vCandidates.push_back(StartEvent(1, TestId(1), 10, "A"));
	ASSERT_TRUE(Replay.Submit(Batch));
	Batch = {};
	Batch.m_ServerTick = 20;
	Batch.m_vCandidates.push_back(StartEvent(2, TestId(2), 20, "B"));
	ASSERT_TRUE(Replay.Submit(Batch));
	Batch = {};
	Batch.m_ServerTick = 30;
	Batch.m_vCandidates.push_back(StartEvent(3, TestId(3), 30, "C"));
	ASSERT_TRUE(Replay.Submit(Batch));
	EXPECT_EQ(Replay.Run().RosterMemberCount(), 3);
	EXPECT_EQ(Replay.Run().StartedParticipantCount(), 3);
	EXPECT_EQ(ImpactStatisticsParticipantCount(Replay.Run().Participants()), 3U);
}

TEST(ImpactLiveAdapter, LateEngagementPreservesStats)
{
	CImpactRun Run;
	Run.Initialize(2014, TestContext());
	ASSERT_TRUE(Run.AddRosterMember(TestId(1), "A", 0, "team roster"));
	ASSERT_TRUE(Run.AddRosterMember(TestId(2), "B", 0, "team roster"));
	ASSERT_TRUE(Run.AddRosterMember(TestId(3, 6), "Late Tee", 0, "team roster"));
	STickEventBatch Batch;
	Batch.m_ServerTick = 10;
	Batch.m_vCandidates.push_back(StartEvent(1, TestId(1), 10));
	ASSERT_TRUE(Run.ApplyTickEventBatch(Batch));
	Batch = {};
	Batch.m_ServerTick = 20;
	Batch.m_vCandidates.push_back(StartEvent(2, TestId(2), 20));
	ASSERT_TRUE(Run.ApplyTickEventBatch(Batch));

	SRunSnapshot Snapshot = Run.Snapshot(20);
	for(SParticipant &Participant : Snapshot.m_vParticipants)
		if(Participant.m_Id == TestId(3, 6))
			Participant.m_RawStats.m_Saves = 4;
	Snapshot.m_RawStats.m_Saves = 4;
	CImpactRun Restored;
	ASSERT_TRUE(Restored.RestoreSnapshot(Snapshot));
	SRawStats Save;
	Save.m_Saves = 1;
	Batch = {};
	Batch.m_ServerTick = 30;
	Batch.m_vCandidates.push_back(ParticipantStatsEvent(3, EImpactEventType::SAVE_CONFIRMED, TestId(3, 6), 30, Save));
	ASSERT_TRUE(Restored.ApplyTickEventBatch(Batch));
	EXPECT_TRUE(Restored.IsParticipantEngaged(TestId(3, 6)));
	EXPECT_EQ(FindRunParticipant(Restored, TestId(3, 6))->m_RawStats.m_Saves, 5);
	EXPECT_EQ(Restored.RawStats().m_Saves, 5);
	EXPECT_EQ(Restored.RosterMemberCount(), 3);
}

TEST(ImpactLifecycle, OldGenerationEngagedDoesNotLeakIntoNewGeneration)
{
	CReplay Replay(2015, TestContext());
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(1, 4), "Old generation", 0, "team roster"));
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(2, 1), "B", 0, "team roster"));
	STickEventBatch Batch;
	Batch.m_ServerTick = 10;
	Batch.m_vCandidates.push_back(StartEvent(1, TestId(1, 4), 10));
	ASSERT_TRUE(Replay.Submit(Batch));
	Batch = {};
	Batch.m_ServerTick = 20;
	Batch.m_vCandidates.push_back(StartEvent(2, TestId(2, 1), 20));
	ASSERT_TRUE(Replay.Submit(Batch));
	ASSERT_TRUE(FinalizeReplayParticipant(Replay, 3, TestId(1, 4), 30));
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(1, 5), "New generation", 40, "slot generation advanced"));
	EXPECT_TRUE(Replay.Run().IsParticipantFinalized(TestId(1, 4)));
	EXPECT_FALSE(Replay.Run().IsParticipantEngaged(TestId(1, 5)));
	EXPECT_EQ(Replay.Run().ActiveEngagedParticipantCount(), 1);
}

TEST(ImpactLifecycle, TeamResetFinalizesWholeRoster)
{
	CReplay Replay(2016, TestContext());
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(1), "A", 0, "team roster"));
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(2), "B", 0, "team roster"));
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(3), "AFK", 0, "team roster"));
	StartPair(Replay);
	ASSERT_TRUE(Replay.Run().Finalize(ERunResult::TEAM_RESET, 30));
	EXPECT_EQ(Replay.Run().FinalizedParticipantCount(), 3);
	for(const SParticipant &Participant : Replay.Run().Participants())
		EXPECT_TRUE(Participant.m_Finalized);
}

TEST(ImpactLifecycle, IndividualResetDoesNotFinalizeOtherRosterMembers)
{
	CReplay Replay(2017, TestContext());
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(3), "AFK", 0, "team roster"));
	StartPair(Replay);
	ASSERT_TRUE(FinalizeReplayParticipant(Replay, 3, TestId(2), 20));
	EXPECT_FALSE(Replay.Run().IsParticipantFinalized(TestId(1)));
	EXPECT_TRUE(Replay.Run().IsParticipantFinalized(TestId(2)));
	EXPECT_FALSE(Replay.Run().IsParticipantFinalized(TestId(3)));
	EXPECT_FALSE(Replay.Run().IsParticipantEngaged(TestId(3)));
	EXPECT_EQ(Replay.Run().FinalizedParticipantCount(), 1);
}

TEST(ImpactPhase2, SelfSafeProofPersistsOnlyWithinSameCanonicalProblemEpisode)
{
	CReplay Replay(136, TestContext());
	StartPair(Replay);
	StartThirdParticipant(Replay);

	SImpactGameplayFrame Saver = ImpactFrame(1, 90, false);
	SImpactGameplayFrame Target = ImpactFrame(2, 90, true);
	Target.m_FreezeEnd = 200;
	SImpactGameplayFrame SelfSafe = ImpactFrame(3, 90, true, true, true, 1, false, EImpactRecoveryNeed::SELF_SAFE);
	SelfSafe.m_FreezeEnd = 200;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, SelfSafe}));

	// A brief unfreeze/refreeze remains the same canonical problem episode, so the
	// positive proof is intentionally still valid and blocks Clutch.
	Saver.m_ServerTick = Target.m_ServerTick = SelfSafe.m_ServerTick = 100;
	SelfSafe.m_Frozen = false;
	SelfSafe.m_FreezeEnd = 0;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, SelfSafe}));
	Saver.m_ServerTick = Target.m_ServerTick = SelfSafe.m_ServerTick = 110;
	SelfSafe.m_Frozen = true;
	SelfSafe.m_FreezeStart = 110;
	SelfSafe.m_FreezeEnd = 210;
	SelfSafe.m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
	SelfSafe.m_AutonomousRecoveryKnown = false;
	SelfSafe.m_AutonomousRecoveryPossible = false;
	SetHooked(Saver, 2, true);
	SetPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, SelfSafe}));
	Saver.m_ServerTick = Target.m_ServerTick = SelfSafe.m_ServerTick = 120;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, SelfSafe}));
	Saver.m_ServerTick = Target.m_ServerTick = SelfSafe.m_ServerTick = 155;
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target, SelfSafe}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 0);
}

TEST(ImpactPhase2, SelfSafeProofDoesNotLeakIntoNewIndependentFreezeEpisode)
{
	CReplay Replay(137, TestContext());
	StartPair(Replay);
	StartThirdParticipant(Replay);

	SImpactGameplayFrame Saver = ImpactFrame(1, 90, false);
	SImpactGameplayFrame Target = ImpactFrame(2, 90, false);
	SImpactGameplayFrame Other = ImpactFrame(3, 90, true, true, true, 1, false, EImpactRecoveryNeed::SELF_SAFE);
	Other.m_FreezeEnd = 200;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	// Close the first autonomous-safe episode with the canonical stable boundary.
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 100;
	Other.m_Frozen = false;
	Other.m_FreezeEnd = 0;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 140;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	// A genuinely new freeze has explicit external-recovery evidence. The old
	// SELF_SAFE proof must not classify this new problem as functional.
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 200;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 200;
	Target.m_FreezeEnd = 300;
	Other.m_Frozen = true;
	Other.m_FreezeStart = 200;
	Other.m_FreezeEnd = 300;
	Other.m_RecoveryNeed = EImpactRecoveryNeed::EXTERNAL_REQUIRED;
	Other.m_AutonomousRecoveryKnown = true;
	Other.m_AutonomousRecoveryPossible = false;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 210;
	SetHooked(Saver, 2, true);
	SetPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 220;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 255;
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target, Other}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 1);
}

TEST(ImpactPhase2, SaveStillCountsWhenClutchRejected)
{
	CReplay Replay(125, TestContext());
	StartPair(Replay);
	StartThirdParticipant(Replay);

	SImpactGameplayFrame Saver = ImpactFrame(1, 90, false);
	SImpactGameplayFrame Target = ImpactFrame(2, 90, true);
	Target.m_FreezeEnd = 200;
	SImpactGameplayFrame SelfSafe = ImpactFrame(3, 90, true, true, true, 1, false, EImpactRecoveryNeed::SELF_SAFE);
	SelfSafe.m_FreezeEnd = 200;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, SelfSafe}));

	Saver.m_ServerTick = Target.m_ServerTick = SelfSafe.m_ServerTick = 100;
	SetHooked(Saver, 2, true);
	SetPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, SelfSafe}));

	Saver.m_ServerTick = Target.m_ServerTick = SelfSafe.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, SelfSafe}));

	Saver.m_ServerTick = Target.m_ServerTick = SelfSafe.m_ServerTick = 145;
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target, SelfSafe}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 0);
}

TEST(ImpactPhase2, AledNoClutchWithSelfSafeTeammate)
{
	CReplay Replay(126, TestContext());
	StartPair(Replay);
	StartThirdParticipant(Replay);

	SImpactGameplayFrame Saver = ImpactFrame(1, 90, false);
	SImpactGameplayFrame Target = ImpactFrame(2, 90, true);
	Target.m_FreezeEnd = 200;
	SImpactGameplayFrame SelfSafe = ImpactFrame(3, 90, true, true, true, 1, false, EImpactRecoveryNeed::SELF_SAFE);
	SelfSafe.m_FreezeEnd = 300;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, SelfSafe}));

	const SImpactGameplayMessage Message = ImpactSignal(1, EImpactGameplayMessageType::CONFIRMED_ALED, 200, TestId(1), TestId(2));
	Saver.m_ServerTick = Target.m_ServerTick = SelfSafe.m_ServerTick = 200;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target, SelfSafe}, {Message}));

	Saver.m_ServerTick = Target.m_ServerTick = SelfSafe.m_ServerTick = 235;
	EXPECT_TRUE(Replay.SubmitGameplay({Saver, Target, SelfSafe}));
	EXPECT_EQ(Replay.Run().RawStats().m_Aleds, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 0);
}

TEST(ImpactPhase2, TrueClutchCountsOncePerEpisode)
{
	CReplay Replay(127, TestContext());
	StartPair(Replay);

	SImpactGameplayFrame Saver = ImpactFrame(1, 90, false);
	SImpactGameplayFrame Target = ImpactFrame(2, 90, true);
	Target.m_FreezeEnd = 200;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 100;
	SetHooked(Saver, 2, true);
	SetPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 145;
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 1);
}

TEST(ImpactPhase2, SecondSaveSameCrisisNoSecondClutch)
{
	CReplay Replay(128, TestContext());
	StartPair(Replay);

	SImpactGameplayFrame Saver = ImpactFrame(1, 90, false);
	SImpactGameplayFrame Target = ImpactFrame(2, 90, true);
	Target.m_FreezeEnd = 200;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 100;
	SetHooked(Saver, 2, true);
	SetPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 145;
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target}));

	const SImpactGameplayMessage SecondSave = ImpactSignal(77, EImpactGameplayMessageType::CONFIRMED_ALED, 200, TestId(1), TestId(2));
	Saver.m_ServerTick = Target.m_ServerTick = 200;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}, {SecondSave}));
	Saver.m_ServerTick = Target.m_ServerTick = 235;
	EXPECT_TRUE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 2);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 1);
}

TEST(ImpactPhase2, NewCrisisCanCreateNewClutch)
{
	CReplay Replay(129, TestContext());
	StartPair(Replay);

	SImpactGameplayFrame Saver = ImpactFrame(1, 90, false);
	SImpactGameplayFrame Target = ImpactFrame(2, 90, true);
	Target.m_FreezeEnd = 200;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 100;
	SetHooked(Saver, 2, true);
	SetPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 145;
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 1);

	Saver.m_ServerTick = Target.m_ServerTick = 200;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 200;
	Target.m_FreezeEnd = 300;
	SetHooked(Saver, 2, true);
	SetPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 210;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 64;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 245;
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 2);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 2);
}

TEST(ImpactPhase2, NewFallAfterConfirmedClutchCreatesNewClutchEpisode)
{
	CReplay Replay(154, TestContext());
	StartPair(Replay);
	StartThirdParticipant(Replay);

	SImpactGameplayFrame Saver;
	SImpactGameplayFrame Target;
	SImpactGameplayFrame Other;
	ASSERT_TRUE(BeginTwoConfirmedFallClutchEpisode(Replay, Saver, Target, Other));
	const SImpactPhase2Debug *pFirstArm = FindDebugLabel(Replay.LastOutput(), "CLUTCH_EPISODE_ARM");
	ASSERT_NE(pFirstArm, nullptr);
	const uint64_t FirstClutchEpisodeId = pFirstArm->m_ClutchEpisodeId;
	ASSERT_NE(FirstClutchEpisodeId, 0U);

	// Confirm the first canonical Save while the other fallen Tee remains in its
	// current confirmed Fall. The successful Clutch must close the episode and
	// must not re-arm it merely because that old Fall is still present.
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	Target.m_AutonomousRecoveryKnown = false;
	Target.m_AutonomousRecoveryPossible = false;
	ClearPhysicalHarmfulOutcome(Target);
	ReleaseHook(Saver);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 145;
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target, Other}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 1);
	const SImpactPhase2Debug *pFirstClear = FindDebugLabel(Replay.LastOutput(), "CLUTCH_EPISODE_CLEAR");
	ASSERT_NE(pFirstClear, nullptr);
	EXPECT_EQ(pFirstClear->m_ClutchEpisodeId, FirstClutchEpisodeId);
	EXPECT_NE(pFirstClear->m_Reason.find("CLUTCH_CONFIRMED"), std::string::npos);
	EXPECT_EQ(CountDebugLabels(Replay.LastOutput(), "CLUTCH_EPISODE_ARM"), 0);

	// The saved Tee enters a genuinely new physical freeze/problem episode. The
	// old confirmed Fall for Other is intentionally still present, so only a new
	// problem-start boundary may release the post-confirmation lifecycle barrier.
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 200;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 200;
	Target.m_FreezeEnd = 300;
	Target.m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
	Target.m_AutonomousRecoveryKnown = false;
	Target.m_AutonomousRecoveryPossible = false;
	Target.m_AutonomousRecoveryEvaluated = false;
	Target.m_HarmfulOutcomeCandidateKnown = false;
	Target.m_HarmfulOutcomeCandidate = false;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 210;
	SetHooked(Saver, 2, true);
	SetConfirmedExternalRecoveryEvidence(Target);
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target, Other}));
	const SImpactPhase2Debug *pSecondArm = FindDebugLabel(Replay.LastOutput(), "CLUTCH_EPISODE_ARM");
	ASSERT_NE(pSecondArm, nullptr);
	EXPECT_NE(pSecondArm->m_ClutchEpisodeId, FirstClutchEpisodeId);
	const uint64_t SecondClutchEpisodeId = pSecondArm->m_ClutchEpisodeId;
	ASSERT_NE(SecondClutchEpisodeId, 0U);

	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 220;
	Target.m_Frozen = false;
	Target.m_FreezeStart = -1;
	Target.m_FreezeEnd = 0;
	Target.m_X = 64;
	Target.m_AutonomousRecoveryKnown = false;
	Target.m_AutonomousRecoveryPossible = false;
	ClearPhysicalHarmfulOutcome(Target);
	ReleaseHook(Saver);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 255;
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target, Other}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 2);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 2);
	const SImpactPhase2Debug *pSecondClear = FindDebugLabel(Replay.LastOutput(), "CLUTCH_EPISODE_CLEAR");
	ASSERT_NE(pSecondClear, nullptr);
	EXPECT_EQ(pSecondClear->m_ClutchEpisodeId, SecondClutchEpisodeId);
	EXPECT_NE(pSecondClear->m_Reason.find("CLUTCH_CONFIRMED"), std::string::npos);
}

TEST(ImpactPhase2, NewProblemDuringPendingClutchConfirmationCanEarnNewClutch)
{
	CReplay Replay(156, TestContext());
	StartPair(Replay);

	SImpactGameplayFrame FirstSaver = ImpactFrame(1, 90, false);
	SImpactGameplayFrame FirstTarget = ImpactFrame(2, 90, true);
	FirstTarget.m_FreezeEnd = 200;
	ASSERT_FALSE(Replay.SubmitGameplay({FirstSaver, FirstTarget}));

	// The first rescue becomes safe at tick 110, but its existing 35-tick Save
	// confirmation does not complete until tick 145.
	FirstSaver.m_ServerTick = FirstTarget.m_ServerTick = 100;
	SetHooked(FirstSaver, 2, true);
	SetPhysicalHarmfulOutcome(FirstTarget);
	ASSERT_FALSE(Replay.SubmitGameplay({FirstSaver, FirstTarget}));

	FirstSaver.m_ServerTick = FirstTarget.m_ServerTick = 110;
	FirstTarget.m_Frozen = false;
	FirstTarget.m_FreezeStart = -1;
	FirstTarget.m_FreezeEnd = 0;
	FirstTarget.m_X = 32;
	ClearPhysicalHarmfulOutcome(FirstTarget);
	ReleaseHook(FirstSaver);
	ASSERT_FALSE(Replay.SubmitGameplay({FirstSaver, FirstTarget}));

	// A genuinely new problem starts for the former saver after the first
	// physical rescue, but before that Save/Clutch has passed confirmation.
	FirstSaver.m_ServerTick = FirstTarget.m_ServerTick = 120;
	FirstSaver.m_Frozen = true;
	FirstSaver.m_FreezeStart = 120;
	FirstSaver.m_FreezeEnd = 300;
	SetPhysicalHarmfulOutcome(FirstSaver);
	ASSERT_FALSE(Replay.SubmitGameplay({FirstSaver, FirstTarget}));
	const SImpactPhase2Debug *pOverlapArm = FindDebugLabel(Replay.LastOutput(), "CLUTCH_EPISODE_ARM");
	ASSERT_NE(pOverlapArm, nullptr);
	EXPECT_EQ(pOverlapArm->m_PotentialSaver, TestId(2));

	FirstSaver.m_ServerTick = FirstTarget.m_ServerTick = 145;
	ASSERT_TRUE(Replay.SubmitGameplay({FirstSaver, FirstTarget}));
	ASSERT_EQ(Replay.Run().RawStats().m_Saves, 1);
	ASSERT_EQ(Replay.Run().RawStats().m_Clutches, 1);

	// The newly-frozen former saver is now rescued by the former target, after
	// the first confirmation but within the same already-started new problem.
	FirstSaver.m_ServerTick = FirstTarget.m_ServerTick = 150;
	SetHooked(FirstTarget, 1, true);
	SetPhysicalHarmfulOutcome(FirstSaver);
	ASSERT_FALSE(Replay.SubmitGameplay({FirstSaver, FirstTarget}));

	FirstSaver.m_ServerTick = FirstTarget.m_ServerTick = 160;
	FirstSaver.m_Frozen = false;
	FirstSaver.m_FreezeStart = -1;
	FirstSaver.m_FreezeEnd = 0;
	FirstSaver.m_X = 64;
	ClearPhysicalHarmfulOutcome(FirstSaver);
	ReleaseHook(FirstTarget);
	ASSERT_FALSE(Replay.SubmitGameplay({FirstSaver, FirstTarget}));
	const SImpactPhase2Debug *pSecondClutchProof = FindDebugLabel(Replay.LastOutput(), "CLUTCH_FINAL_PROOF");
	ASSERT_NE(pSecondClutchProof, nullptr);
	EXPECT_NE(pSecondClutchProof->m_Reason.find("final=ACCEPTED"), std::string::npos);

	FirstSaver.m_ServerTick = FirstTarget.m_ServerTick = 195;
	ASSERT_TRUE(Replay.SubmitGameplay({FirstSaver, FirstTarget}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 2);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 2);
}

TEST(ImpactPhase2, NewProblemProvenAfterPriorClutchConfirmationCanEarnNewClutch)
{
	CReplay Replay(157, TestContext());
	StartPair(Replay);

	SImpactGameplayFrame FirstSaver = ImpactFrame(1, 90, false);
	SImpactGameplayFrame FirstTarget = ImpactFrame(2, 90, true);
	FirstTarget.m_FreezeEnd = 200;
	ASSERT_FALSE(Replay.SubmitGameplay({FirstSaver, FirstTarget}));

	FirstSaver.m_ServerTick = FirstTarget.m_ServerTick = 100;
	SetHooked(FirstSaver, 2, true);
	SetConfirmedExternalRecoveryEvidence(FirstTarget);
	ASSERT_TRUE(Replay.SubmitGameplay({FirstSaver, FirstTarget}));

	FirstSaver.m_ServerTick = FirstTarget.m_ServerTick = 110;
	FirstTarget.m_Frozen = false;
	FirstTarget.m_FreezeStart = -1;
	FirstTarget.m_FreezeEnd = 0;
	FirstTarget.m_X = 32;
	ClearPhysicalHarmfulOutcome(FirstTarget);
	ReleaseHook(FirstSaver);
	ASSERT_FALSE(Replay.SubmitGameplay({FirstSaver, FirstTarget}));

	// A distinct new problem begins while the previous Clutch Save is still
	// inside its 35-tick confirmation. Its own external-recovery proof is not
	// available yet, so the current problem is initially UNKNOWN, not awardable.
	FirstSaver.m_ServerTick = FirstTarget.m_ServerTick = 120;
	FirstSaver.m_Frozen = true;
	FirstSaver.m_FreezeStart = 120;
	FirstSaver.m_FreezeEnd = 300;
	FirstSaver.m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
	FirstSaver.m_AutonomousRecoveryKnown = false;
	FirstSaver.m_AutonomousRecoveryPossible = false;
	FirstSaver.m_AutonomousRecoveryEvaluated = false;
	FirstSaver.m_HarmfulOutcomeCandidateKnown = false;
	FirstSaver.m_HarmfulOutcomeCandidate = false;
	ASSERT_FALSE(Replay.SubmitGameplay({FirstSaver, FirstTarget}));
	EXPECT_EQ(CountDebugLabels(Replay.LastOutput(), "CLUTCH_EPISODE_ARM"), 0);

	// The first Save/Clutch confirms after the distinct problem has already
	// started. The new problem then becomes provably externally-caused; that
	// evidence transition must arm a fresh episode even though its start edge is
	// no longer on the current frame.
	FirstSaver.m_ServerTick = FirstTarget.m_ServerTick = 145;
	ASSERT_TRUE(Replay.SubmitGameplay({FirstSaver, FirstTarget}));
	ASSERT_EQ(Replay.Run().RawStats().m_Saves, 1);
	ASSERT_EQ(Replay.Run().RawStats().m_Clutches, 1);

	FirstSaver.m_ServerTick = FirstTarget.m_ServerTick = 150;
	SetHooked(FirstTarget, 1, true);
	SetConfirmedExternalRecoveryEvidence(FirstSaver);
	Replay.SubmitGameplay({FirstSaver, FirstTarget});
	const SImpactPhase2Debug *pSecondArm = FindDebugLabel(Replay.LastOutput(), "CLUTCH_EPISODE_ARM");
	ASSERT_NE(pSecondArm, nullptr);

	FirstSaver.m_ServerTick = FirstTarget.m_ServerTick = 160;
	FirstSaver.m_Frozen = false;
	FirstSaver.m_FreezeStart = -1;
	FirstSaver.m_FreezeEnd = 0;
	FirstSaver.m_X = 64;
	ClearPhysicalHarmfulOutcome(FirstSaver);
	ReleaseHook(FirstTarget);
	ASSERT_FALSE(Replay.SubmitGameplay({FirstSaver, FirstTarget}));
	FirstSaver.m_ServerTick = FirstTarget.m_ServerTick = 195;
	ASSERT_TRUE(Replay.SubmitGameplay({FirstSaver, FirstTarget}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 2);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 2);
}

TEST(ImpactPhase2, DifferentActorCanEarnClutchAfterPreviousEpisodeEnded)
{
	CReplay Replay(155, TestContext());
	StartPair(Replay);

	// First rescue: client 1 is the sole functional actor.
	SImpactGameplayFrame FirstSaver = ImpactFrame(1, 90, false);
	SImpactGameplayFrame FirstTarget = ImpactFrame(2, 90, true);
	FirstTarget.m_FreezeEnd = 200;
	ASSERT_FALSE(Replay.SubmitGameplay({FirstSaver, FirstTarget}));
	FirstSaver.m_ServerTick = FirstTarget.m_ServerTick = 100;
	SetHooked(FirstSaver, 2, true);
	SetConfirmedExternalRecoveryEvidence(FirstTarget);
	ASSERT_TRUE(Replay.SubmitGameplay({FirstSaver, FirstTarget}));

	FirstSaver.m_ServerTick = FirstTarget.m_ServerTick = 110;
	FirstTarget.m_Frozen = false;
	FirstTarget.m_FreezeStart = -1;
	FirstTarget.m_FreezeEnd = 0;
	FirstTarget.m_X = 32;
	ClearPhysicalHarmfulOutcome(FirstTarget);
	ReleaseHook(FirstSaver);
	ASSERT_FALSE(Replay.SubmitGameplay({FirstSaver, FirstTarget}));
	FirstSaver.m_ServerTick = FirstTarget.m_ServerTick = 145;
	ASSERT_TRUE(Replay.SubmitGameplay({FirstSaver, FirstTarget}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 1);
	const SImpactPhase2Debug *pFirstRaw = FindDebugLabel(Replay.LastOutput(), "CLUTCH_RAW_DELTA");
	ASSERT_NE(pFirstRaw, nullptr);
	const uint64_t FirstClutchEpisodeId = pFirstRaw->m_ClutchEpisodeId;
	ASSERT_NE(FirstClutchEpisodeId, 0U);

	// Second rescue: the former target is now the sole functional actor and the
	// former saver is the new current Fall target. This proves actor/generation
	// state from the first confirmed episode cannot leak into the next one.
	FirstSaver.m_ServerTick = FirstTarget.m_ServerTick = 200;
	FirstSaver.m_Frozen = true;
	FirstSaver.m_FreezeStart = 200;
	FirstSaver.m_FreezeEnd = 300;
	FirstSaver.m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
	FirstSaver.m_AutonomousRecoveryKnown = false;
	FirstSaver.m_AutonomousRecoveryPossible = false;
	FirstSaver.m_AutonomousRecoveryEvaluated = false;
	ClearPhysicalHarmfulOutcome(FirstSaver);
	FirstTarget.m_Frozen = false;
	FirstTarget.m_FreezeStart = -1;
	FirstTarget.m_FreezeEnd = 0;
	ReleaseHook(FirstTarget);
	ASSERT_FALSE(Replay.SubmitGameplay({FirstSaver, FirstTarget}));

	FirstSaver.m_ServerTick = FirstTarget.m_ServerTick = 210;
	SetHooked(FirstTarget, 1, true);
	SetConfirmedExternalRecoveryEvidence(FirstSaver);
	ASSERT_TRUE(Replay.SubmitGameplay({FirstSaver, FirstTarget}));

	FirstSaver.m_ServerTick = FirstTarget.m_ServerTick = 220;
	FirstSaver.m_Frozen = false;
	FirstSaver.m_FreezeStart = -1;
	FirstSaver.m_FreezeEnd = 0;
	FirstSaver.m_X = 64;
	ClearPhysicalHarmfulOutcome(FirstSaver);
	ReleaseHook(FirstTarget);
	ASSERT_FALSE(Replay.SubmitGameplay({FirstSaver, FirstTarget}));
	FirstSaver.m_ServerTick = FirstTarget.m_ServerTick = 255;
	ASSERT_TRUE(Replay.SubmitGameplay({FirstSaver, FirstTarget}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 2);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 2);
	const SImpactPhase2Debug *pSecondRaw = FindDebugLabel(Replay.LastOutput(), "CLUTCH_RAW_DELTA");
	ASSERT_NE(pSecondRaw, nullptr);
	EXPECT_NE(pSecondRaw->m_ClutchEpisodeId, FirstClutchEpisodeId);
	const auto SecondSave = std::find_if(Replay.Run().Events().rbegin(), Replay.Run().Events().rend(), [](const SImpactEvent &Event) {
		return Event.m_Type == EImpactEventType::SAVE_CONFIRMED;
	});
	ASSERT_NE(SecondSave, Replay.Run().Events().rend());
	EXPECT_EQ(SecondSave->m_Actor, TestId(2));
}

TEST(ImpactPhase2, LocalAndRemoteParticipantsUseSameFunctionalClutchPredicate)
{
	CReplay Replay(130, TestContext());
	const SParticipantId TargetId = TestId(1, 1);
	const SParticipantId RemoteSaverId = TestId(2, 7);
	StartPairWithIds(Replay, TargetId, RemoteSaverId);
	StartThirdParticipant(Replay);

	SImpactGameplayFrame Target = ImpactFrame(TargetId.m_ClientId, 90, true);
	Target.m_Participant = TargetId;
	Target.m_FreezeEnd = 200;
	SImpactGameplayFrame RemoteSaver = ImpactFrame(RemoteSaverId.m_ClientId, 90, false);
	RemoteSaver.m_Participant = RemoteSaverId;
	SImpactGameplayFrame SelfSafe = ImpactFrame(3, 90, true, true, true, 1, false, EImpactRecoveryNeed::SELF_SAFE);
	SelfSafe.m_FreezeEnd = 200;
	ASSERT_FALSE(Replay.SubmitGameplay({Target, RemoteSaver, SelfSafe}));

	Target.m_ServerTick = RemoteSaver.m_ServerTick = SelfSafe.m_ServerTick = 100;
	SetHooked(RemoteSaver, TargetId.m_ClientId, true);
	SetPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Target, RemoteSaver, SelfSafe}));

	Target.m_ServerTick = RemoteSaver.m_ServerTick = SelfSafe.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Target, RemoteSaver, SelfSafe}));

	Target.m_ServerTick = RemoteSaver.m_ServerTick = SelfSafe.m_ServerTick = 145;
	ASSERT_TRUE(Replay.SubmitGameplay({Target, RemoteSaver, SelfSafe}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 0);
	const auto It = std::find_if(Replay.Run().Participants().begin(), Replay.Run().Participants().end(), [RemoteSaverId](const SParticipant &Participant) {
		return Participant.m_Id == RemoteSaverId;
	});
	ASSERT_NE(It, Replay.Run().Participants().end());
	EXPECT_EQ(It->m_RawStats.m_Saves, 1);
	EXPECT_EQ(It->m_RawStats.m_Clutches, 0);
}

TEST(ImpactPhase2, NoClutchWithOtherUnfrozenTeammate)
{
	CReplay Replay(116, TestContext());
	StartPair(Replay);
	STickEventBatch StartThird;
	StartThird.m_ServerTick = 20;
	StartThird.m_vCandidates.push_back(StartEvent(3, TestId(3), 20, "C"));
	ASSERT_TRUE(Replay.Submit(StartThird));

	SImpactGameplayFrame Saver = ImpactFrame(1, 90, false);
	SImpactGameplayFrame OtherUnfrozen = ImpactFrame(2, 90, false);
	SImpactGameplayFrame Target = ImpactFrame(3, 90, true);
	Target.m_FreezeEnd = 200;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, OtherUnfrozen, Target}));

	Saver.m_ServerTick = OtherUnfrozen.m_ServerTick = Target.m_ServerTick = 100;
	SetHooked(Saver, 3, true);
	SetPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, OtherUnfrozen, Target}));

	Saver.m_ServerTick = OtherUnfrozen.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, OtherUnfrozen, Target}));

	Saver.m_ServerTick = OtherUnfrozen.m_ServerTick = Target.m_ServerTick = 145;
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, OtherUnfrozen, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 0);
	EXPECT_EQ(Replay.Run().Participants()[0].m_RawStats.m_Clutches, 0);
}

TEST(ImpactPhase2, RefrozenRescueDoesNotConfirmClutch)
{
	CReplay Replay(117, TestContext());
	StartPair(Replay);

	SImpactGameplayFrame Saver = ImpactFrame(1, 90, false);
	SImpactGameplayFrame Target = ImpactFrame(2, 90, true);
	Target.m_FreezeEnd = 200;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 100;
	SetHooked(Saver, 2, true);
	SetPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 120;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 120;
	Target.m_FreezeEnd = 220;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 160;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 0);
}

TEST(ImpactPhase2, RemoteSoleUnfrozenSaverReceivesClutch)
{
	CReplay Replay(118, TestContext());
	const SParticipantId TargetId = TestId(1, 1);
	const SParticipantId SaverId = TestId(2, 7);
	StartPairWithIds(Replay, TargetId, SaverId);

	SImpactGameplayFrame Target = ImpactFrame(TargetId.m_ClientId, 90, true);
	Target.m_Participant = TargetId;
	Target.m_FreezeEnd = 200;
	SImpactGameplayFrame Saver = ImpactFrame(SaverId.m_ClientId, 90, false);
	Saver.m_Participant = SaverId;
	ASSERT_FALSE(Replay.SubmitGameplay({Target, Saver}));

	Target.m_ServerTick = Saver.m_ServerTick = 100;
	SetHooked(Saver, TargetId.m_ClientId, true);
	SetPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Target, Saver}));

	Target.m_ServerTick = Saver.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Target, Saver}));

	Target.m_ServerTick = Saver.m_ServerTick = 145;
	ASSERT_TRUE(Replay.SubmitGameplay({Target, Saver}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 1);
	const auto It = std::find_if(Replay.Run().Participants().begin(), Replay.Run().Participants().end(), [SaverId](const SParticipant &Participant) {
		return Participant.m_Id == SaverId;
	});
	ASSERT_NE(It, Replay.Run().Participants().end());
	EXPECT_EQ(It->m_RawStats.m_Clutches, 1);
}

TEST(ImpactPhase2, AdditionalSaveAfterFirstRescueDoesNotAddClutchWithoutNewSoleEpisode)
{
	CReplay Replay(119, TestContext());
	StartPair(Replay);
	STickEventBatch StartThird;
	StartThird.m_ServerTick = 20;
	StartThird.m_vCandidates.push_back(StartEvent(3, TestId(3), 20, "C"));
	ASSERT_TRUE(Replay.Submit(StartThird));

	SImpactGameplayFrame Saver = ImpactFrame(1, 90, false);
	SImpactGameplayFrame OtherUnfrozen = ImpactFrame(2, 90, false);
	SImpactGameplayFrame Target = ImpactFrame(3, 90, true);
	Target.m_FreezeEnd = 200;
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, OtherUnfrozen, Target}));
	Saver.m_ServerTick = OtherUnfrozen.m_ServerTick = Target.m_ServerTick = 100;
	SetHooked(Saver, 3, true);
	SetPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, OtherUnfrozen, Target}));
	Saver.m_ServerTick = OtherUnfrozen.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({Saver, OtherUnfrozen, Target}));
	Saver.m_ServerTick = OtherUnfrozen.m_ServerTick = Target.m_ServerTick = 145;
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, OtherUnfrozen, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);

	// A later confirmed rescue while two teammates remain unfrozen is another
	// Save, but it is not a new last-unfrozen episode.
	const SImpactGameplayMessage SecondSave = ImpactSignal(77, EImpactGameplayMessageType::CONFIRMED_ALED, 200, TestId(1), TestId(3));
	Saver.m_ServerTick = OtherUnfrozen.m_ServerTick = Target.m_ServerTick = 200;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, OtherUnfrozen, Target}, {SecondSave}));
	Saver.m_ServerTick = OtherUnfrozen.m_ServerTick = Target.m_ServerTick = 235;
	EXPECT_TRUE(Replay.SubmitGameplay({Saver, OtherUnfrozen, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 2);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 0);
}

TEST(ImpactFoundation, DirectHarmFallCounterBelongsToVictim)
{
	CReplay Replay(81, TestContext());
	StartPair(Replay);
	ASSERT_FALSE(Replay.SubmitGameplay({ImpactFrame(1, 90), ImpactFrame(2, 90)}));
	SImpactGameplayFrame HarmfulTarget = ImpactFrame(2, 100, true);
	HarmfulTarget.m_HarmfulOutcomeCandidateKnown = true;
	HarmfulTarget.m_HarmfulOutcomeCandidate = true;
	SetSelfCausedHarm(HarmfulTarget);
	const SImpactGameplayMessage Harm = ImpactSignal(1, EImpactGameplayMessageType::CONFIRMED_HARM, 100, TestId(2), TestId(2));
	// The explicit harm packet is metadata only. The entry frame creates the
	// active problem; the next authoritative helpless frame is the independent
	// raw Fall decision point.
	EXPECT_FALSE(Replay.SubmitGameplay({ImpactFrame(1, 100), HarmfulTarget}, {Harm}));
	HarmfulTarget.m_ServerTick = 101;
	HarmfulTarget.m_AutonomousRecoveryKnown = true;
	HarmfulTarget.m_AutonomousRecoveryPossible = false;
	EXPECT_TRUE(Replay.SubmitGameplay({ImpactFrame(1, 101), HarmfulTarget}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	ASSERT_EQ(Replay.Run().Participants().size(), 2U);
	EXPECT_EQ(Replay.Run().Participants()[0].m_RawStats.m_Falls, 0);
	EXPECT_EQ(Replay.Run().Participants()[1].m_RawStats.m_Falls, 1);
}

TEST(ImpactPhase2, ExplicitHarmSignalRequiresPhysicalOutcome)
{
	CReplay AmbiguousReplay(85, TestContext());
	StartPair(AmbiguousReplay);
	SImpactGameplayMessage Ambiguous = ImpactSignal(1, EImpactGameplayMessageType::CONFIRMED_HARM, 100, TestId(1), TestId(2));
	Ambiguous.m_CausalConfidence = ECausalConfidence::AMBIGUOUS;
	EXPECT_FALSE(AmbiguousReplay.SubmitGameplay({ImpactFrame(1, 100), ImpactFrame(2, 100)}, {Ambiguous}));
	EXPECT_EQ(AmbiguousReplay.Run().RawStats().m_Falls, 0);

	CReplay ConfirmedReplay(86, TestContext());
	StartPair(ConfirmedReplay);
	const SImpactGameplayMessage Confirmed = ImpactSignal(1, EImpactGameplayMessageType::CONFIRMED_HARM, 100, TestId(2), TestId(2));
	EXPECT_FALSE(ConfirmedReplay.SubmitGameplay({ImpactFrame(1, 100), ImpactFrame(2, 100)}, {Confirmed}));
	EXPECT_EQ(ConfirmedReplay.Run().RawStats().m_Falls, 0);
}

TEST(ImpactPhase2, AmbiguousPendingFallIsClosedByWorldDeath)
{
	CReplay Replay(89, TestContext());
	StartPair(Replay);
	SImpactPendingEpisode Pending = PendingEpisode(1, EImpactEventType::FALL_CONFIRMED, TestId(2), 100);
	Pending.m_CausalConfidence = ECausalConfidence::AMBIGUOUS;
	Pending.m_HarmCausalConfidence = ECausalConfidence::AMBIGUOUS;
	Pending.m_HarmCausalConfidence = ECausalConfidence::AMBIGUOUS;
	Replay.SetPendingEpisodes({Pending});
	SImpactGameplayFrame Victim = ImpactFrame(2, 120, false, false);
	EXPECT_FALSE(Replay.SubmitGameplay({ImpactFrame(1, 120), Victim}, {ImpactKill(1, 120, 2, 2, WEAPON_WORLD)}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_TRUE(Replay.Run().PendingEpisodes().empty());
}

TEST(ImpactPhase2, ManualKillDoesNotCreateFall)
{
	CReplay Replay(16, TestContext());
	StartPair(Replay);
	Replay.SubmitGameplay({ImpactFrame(1, 100), ImpactFrame(2, 100)});
	EXPECT_FALSE(Replay.SubmitGameplay({ImpactFrame(1, 120), ImpactFrame(2, 120, false, false)}, {ImpactKill(1, 120, -1, 2, WEAPON_SELF)}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_TeamMistakes, 0);
}

TEST(ImpactPhase2, DisconnectCancelsPendingFall)
{
	CReplay Replay(17, TestContext());
	StartPair(Replay);
	Replay.SetPendingEpisodes({PendingEpisode(1, EImpactEventType::FALL_CONFIRMED, TestId(2), 100)});
	EXPECT_FALSE(Replay.SubmitGameplay({ImpactFrame(1, 120)}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
}

TEST(ImpactPhase2, PendingFallDoesNotAppearAfterDeathOrDisconnect)
{
	CReplay Replay(18, TestContext());
	StartPair(Replay);
	Replay.SubmitGameplay({ImpactFrame(1, 100), ImpactFrame(2, 100)});
	SImpactGameplayFrame HarmfulFreeze = ImpactFrame(2, 110, true);
	Replay.SubmitGameplay({ImpactFrame(1, 110), HarmfulFreeze});
	SImpactGameplayFrame Victim = ImpactFrame(2, 120, false, false);
	EXPECT_FALSE(Replay.SubmitGameplay({ImpactFrame(1, 120), Victim}, {ImpactKill(1, 120, 2, 2, WEAPON_WORLD)}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_FALSE(Replay.SubmitGameplay({ImpactFrame(1, 130)}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
}

TEST(ImpactPhase2, HarmlessTeammateHookHasNoPenalty)
{
	CReplay Replay(19, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame A = ImpactFrame(1, 100);
	SetHooked(A, 2);
	EXPECT_FALSE(Replay.SubmitGameplay({A, ImpactFrame(2, 100)}));
	EXPECT_EQ(Replay.Run().RawStats().m_TeamMistakes, 0);
}

TEST(ImpactPhase2, HarmlessTeammateHammerHasNoPenalty)
{
	CReplay Replay(20, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame A = ImpactFrame(1, 100);
	A.m_Weapon = WEAPON_HAMMER;
	A.m_AttackTick = 2;
	EXPECT_FALSE(Replay.SubmitGameplay({A, ImpactFrame(2, 100)}));
	EXPECT_EQ(Replay.Run().RawStats().m_TeamMistakes, 0);
}

TEST(ImpactPhase2, ProtectedTeammateHarmBelongsToCulprit)
{
	CReplay Replay(21, TestContext());
	StartPair(Replay);
	Replay.SubmitGameplay({ImpactFrame(1, 0), ImpactFrame(2, 0)});
	SImpactGameplayFrame Victim = ImpactFrame(1, 250, false, false);
	SImpactGameplayFrame Killer = ImpactFrame(2, 250);
	EXPECT_FALSE(Replay.SubmitGameplay({Victim, Killer}, {ImpactKill(1, 250, 2, 1, WEAPON_HAMMER)}));
	EXPECT_EQ(Replay.Run().RawStats().m_TeamMistakes, 0);
	EXPECT_EQ(Replay.Run().Participants()[1].m_RawStats.m_TeamMistakes, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_FALSE(std::any_of(Replay.Run().Events().begin(), Replay.Run().Events().end(), [](const SImpactEvent &Event) {
		return Event.m_Type == EImpactEventType::TEAM_HARM_CONFIRMED;
	}));
}

TEST(ImpactPhase2, BrokenProtectionWindowDoesNotBlameTeammate)
{
	CReplay Replay(22, TestContext());
	StartPair(Replay);
	Replay.SubmitGameplay({ImpactFrame(1, 0), ImpactFrame(2, 0)});
	Replay.SubmitGameplay({ImpactFrame(1, 100, true), ImpactFrame(2, 100)});
	SImpactGameplayFrame Victim = ImpactFrame(1, 200, false, false);
	SImpactGameplayFrame Killer = ImpactFrame(2, 200);
	EXPECT_FALSE(Replay.SubmitGameplay({Victim, Killer}, {ImpactKill(1, 200, 2, 1, WEAPON_HAMMER)}));
	EXPECT_EQ(Replay.Run().RawStats().m_TeamMistakes, 0);
	EXPECT_FALSE(std::any_of(Replay.Run().Events().begin(), Replay.Run().Events().end(), [](const SImpactEvent &Event) {
		return Event.m_Type == EImpactEventType::TEAM_HARM_CONFIRMED;
	}));
}

TEST(ImpactPhase2, AmbiguousTeamHarmHasNoOwner)
{
	CReplay Replay(23, TestContext());
	StartPair(Replay);
	SImpactEvent Event;
	Event.m_EventId = 100;
	Event.m_Type = EImpactEventType::TEAM_HARM_CONFIRMED;
	Event.m_TeamId = 1;
	Event.m_Target = TestId(1);
	Event.m_Confirmed = true;
	Event.m_StartTick = Event.m_ResolveTick = 100;
	Event.m_OwnershipStatus = EOwnershipStatus::AMBIGUOUS;
	Event.m_CausalConfidence = ECausalConfidence::AMBIGUOUS;
	EXPECT_TRUE(Replay.Submit({100, {Event}}));
	EXPECT_EQ(Replay.Run().RawStats().m_TeamMistakes, 0);
	EXPECT_FALSE(Replay.Run().Events().back().m_Actor.IsValid());
}

TEST(ImpactPhase2, ThirdPartyInterferenceCancelsMissedCatch)
{
	CReplay Replay(24, TestContext());
	StartPair(Replay);
	SImpactGameplayMessage Message = ImpactSignal(1, EImpactGameplayMessageType::CONFIRMED_MISSED_CATCH, 100, TestId(1), TestId(2));
	Message.m_Feasible = true;
	Message.m_OutcomeWorsened = true;
	Message.m_ThirdPartyInterference = true;
	EXPECT_FALSE(Replay.SubmitGameplay({ImpactFrame(1, 100), ImpactFrame(2, 100)}, {Message}));
	EXPECT_EQ(Replay.Run().RawStats().m_MissedCatches, 0);
}

TEST(ImpactPhase2, FeasibleMissedCatchCreatesRawEvent)
{
	CReplay Replay(25, TestContext());
	StartPair(Replay);
	SImpactGameplayMessage Message = ImpactSignal(1, EImpactGameplayMessageType::CONFIRMED_MISSED_CATCH, 100, TestId(1), TestId(2));
	Message.m_Feasible = true;
	Message.m_OutcomeWorsened = true;
	EXPECT_TRUE(Replay.SubmitGameplay({ImpactFrame(1, 100), ImpactFrame(2, 100)}, {Message}));
	EXPECT_EQ(Replay.Run().RawStats().m_MissedCatches, 1);
}

TEST(ImpactPhase2, OutOfRangeCatchIsNotMissedCatch)
{
	CReplay Replay(26, TestContext());
	StartPair(Replay);
	SImpactGameplayMessage Message = ImpactSignal(1, EImpactGameplayMessageType::CONFIRMED_MISSED_CATCH, 100, TestId(1), TestId(2));
	Message.m_Feasible = false;
	Message.m_OutcomeWorsened = true;
	EXPECT_FALSE(Replay.SubmitGameplay({ImpactFrame(1, 100), ImpactFrame(2, 100)}, {Message}));
	EXPECT_EQ(Replay.Run().RawStats().m_MissedCatches, 0);
}

TEST(ImpactPhase2, BlockedLineOfSightIsNotMissedCatch)
{
	CReplay Replay(27, TestContext());
	StartPair(Replay);
	SImpactGameplayMessage Message = ImpactSignal(1, EImpactGameplayMessageType::CONFIRMED_MISSED_CATCH, 100, TestId(1), TestId(2));
	Message.m_Feasible = false;
	Message.m_OutcomeWorsened = true;
	Message.m_Reason = "server-confirmed line of sight was blocked";
	EXPECT_FALSE(Replay.SubmitGameplay({ImpactFrame(1, 100), ImpactFrame(2, 100)}, {Message}));
	EXPECT_EQ(Replay.Run().RawStats().m_MissedCatches, 0);
}

TEST(ImpactPhase2, BadBeneficiaryTrajectoryIsNotCatcherFault)
{
	CReplay Replay(28, TestContext());
	StartPair(Replay);
	SImpactGameplayMessage Message = ImpactSignal(1, EImpactGameplayMessageType::CONFIRMED_MISSED_CATCH, 100, TestId(1), TestId(2));
	Message.m_Feasible = false;
	Message.m_OutcomeWorsened = true;
	Message.m_Reason = "beneficiary trajectory was not catchable";
	EXPECT_FALSE(Replay.SubmitGameplay({ImpactFrame(1, 100), ImpactFrame(2, 100)}, {Message}));
	EXPECT_EQ(Replay.Run().RawStats().m_MissedCatches, 0);
}

TEST(ImpactPhase2, UnneededCatchIsNotMissedCatch)
{
	CReplay Replay(29, TestContext());
	StartPair(Replay);
	SImpactGameplayMessage Message = ImpactSignal(1, EImpactGameplayMessageType::CONFIRMED_MISSED_CATCH, 100, TestId(1), TestId(2));
	Message.m_Feasible = true;
	Message.m_OutcomeWorsened = false;
	EXPECT_FALSE(Replay.SubmitGameplay({ImpactFrame(1, 100), ImpactFrame(2, 100)}, {Message}));
	EXPECT_EQ(Replay.Run().RawStats().m_MissedCatches, 0);
}

TEST(ImpactPhase2, ValidRecoveryConfirmsSave)
{
	CReplay Replay(30, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	SetHooked(Saver, 2, true);
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	SetPhysicalHarmfulOutcome(Target);
	Target.m_OrdinaryFreezeTileKnown = true;
	Target.m_OrdinaryFreezeTile = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 105;
	Target.m_X = 16;
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 175;
	EXPECT_TRUE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::SAVE_CONFIRMED), 1);
}

TEST(ImpactPhase2, OrdinaryHookSaveFromSingleTargetSpecificSnapshot)
{
	CReplay Replay(84, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	SetHooked(Saver, 2);
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	SetPhysicalHarmfulOutcome(Target);
	Target.m_OrdinaryFreezeTileKnown = true;
	Target.m_OrdinaryFreezeTile = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	Target.m_FreezeEnd = 200;
	Replay.SubmitGameplay({Saver, Target});

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	ReleaseHook(Saver);
	// The entry frame only creates the active problem. This subsequent
	// authoritative frozen frame carries the fixture's explicit helpless
	// physical evidence, so the independent Fall writer must emit now.
	EXPECT_TRUE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	Saver.m_ServerTick = Target.m_ServerTick = 200;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	Saver.m_ServerTick = Target.m_ServerTick = 235;
	EXPECT_TRUE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_UpDowns, 0);
}

TEST(ImpactPhase2, FailedRecoveryDoesNotConfirmCounters)
{
	CReplay Replay(31, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	SetHooked(Saver, 2);
	ASSERT_FALSE(Replay.SubmitGameplay({ImpactFrame(1, 90), ImpactFrame(2, 90)}));
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Replay.SubmitGameplay({Saver, Target});
	Target.m_ServerTick = 150;
	Target.m_Frozen = true;
	Target.m_FreezeEnd = 250;
	Target.m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_UpDowns, 0);
}

TEST(ImpactPhase2, AledRealRescueStillCountsSave)
{
	CReplay Replay(32, TestContext());
	StartPair(Replay);
	const SImpactGameplayMessage Message = ImpactSignal(1, EImpactGameplayMessageType::CONFIRMED_ALED, 200, TestId(1), TestId(2));
	EXPECT_FALSE(Replay.SubmitGameplay({ImpactFrame(1, 200), ImpactFrame(2, 200)}, {Message}));
	EXPECT_EQ(Replay.Run().RawStats().m_Aleds, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
	EXPECT_TRUE(Replay.SubmitGameplay({ImpactFrame(1, 235), ImpactFrame(2, 235)}));
	EXPECT_EQ(Replay.Run().RawStats().m_Aleds, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
}

TEST(ImpactPhase2, ConfirmedAledFromSoleUnfrozenTeeAddsOneClutch)
{
	CReplay Replay(120, TestContext());
	StartPair(Replay);
	ASSERT_FALSE(Replay.SubmitGameplay({ImpactFrame(1, 90, false), ImpactFrame(2, 90, true)}));

	const SImpactGameplayMessage Message = ImpactSignal(1, EImpactGameplayMessageType::CONFIRMED_ALED, 200, TestId(1), TestId(2));
	EXPECT_FALSE(Replay.SubmitGameplay({ImpactFrame(1, 200), ImpactFrame(2, 200)}, {Message}));
	EXPECT_EQ(Replay.Run().RawStats().m_Aleds, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 0);

	EXPECT_TRUE(Replay.SubmitGameplay({ImpactFrame(1, 235), ImpactFrame(2, 235)}));
	EXPECT_EQ(Replay.Run().RawStats().m_Aleds, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Clutches, 1);
}

TEST(ImpactPhase2, ClutchEpisodeIsClearedAcrossReset)
{
	CImpactPhase2 Phase;
	SImpactGameplayFrame Saver = ImpactFrame(1, 90, false);
	SImpactGameplayFrame Target = ImpactFrame(2, 90, true);
	Target.m_FreezeEnd = 200;
	Phase.Update(900, 1, 50, {Saver, Target}, {});

	Phase.Reset();
	Saver.m_ServerTick = Target.m_ServerTick = 200;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	const SImpactGameplayMessage Message = ImpactSignal(1, EImpactGameplayMessageType::CONFIRMED_ALED, 200, TestId(1), TestId(2));
	Phase.Update(901, 1, 50, {Saver, Target}, {Message});
	Saver.m_ServerTick = Target.m_ServerTick = 235;
	const SImpactPhase2Output Output = Phase.Update(901, 1, 50, {Saver, Target}, {});
	ASSERT_EQ(CountOutputEvents(Output, EImpactEventType::ALED_CONFIRMED), 1);
	EXPECT_EQ(Output.m_vEvents.back().m_RawDelta.m_Aleds, 1);
	EXPECT_EQ(Output.m_vEvents.back().m_RawDelta.m_Saves, 1);
	EXPECT_EQ(Output.m_vEvents.back().m_RawDelta.m_Clutches, 0);
}

TEST(ImpactAledDetector, CandidateGateRequiresConfirmedSideCarEvidence)
{
	EXPECT_TRUE(CImpactSystem::IsAledCandidateEvidenceSufficient(true, true, true, true, 1, 1, 2, true, 3625, 3626));
	EXPECT_TRUE(CImpactSystem::IsAledCandidateEvidenceSufficient(true, true, true, true, 1, 1, 1, true, 4460, 4462));
	EXPECT_FALSE(CImpactSystem::IsAledCandidateEvidenceSufficient(true, true, true, true, 0, 0, 2, true, 4259, 4260));
	EXPECT_FALSE(CImpactSystem::IsAledCandidateEvidenceSufficient(true, true, true, true, 1, 0, 2, true, 3625, 3626));
	EXPECT_FALSE(CImpactSystem::IsAledCandidateEvidenceSufficient(true, true, true, true, 1, 1, 3, true, 3625, 3626));
	EXPECT_FALSE(CImpactSystem::IsAledCandidateEvidenceSufficient(true, true, true, true, 1, 1, 2, false, 3625, 3626));
	EXPECT_FALSE(CImpactSystem::IsAledCandidateEvidenceSufficient(true, true, true, true, 1, 1, 2, true, 3627, 3626));
	EXPECT_FALSE(CImpactSystem::IsAledCandidateEvidenceSufficient(true, true, true, true, 1, 1, 2, true, 3622, 3626));
	EXPECT_FALSE(CImpactSystem::IsAledCandidateEvidenceSufficient(false, true, true, true, 1, 1, 2, true, 3625, 3626));
	EXPECT_FALSE(CImpactSystem::IsAledCandidateEvidenceSufficient(true, false, true, true, 1, 1, 2, true, 3625, 3626));
	EXPECT_FALSE(CImpactSystem::IsAledCandidateEvidenceSufficient(true, true, false, true, 1, 1, 2, true, 3625, 3626));
	EXPECT_FALSE(CImpactSystem::IsAledCandidateEvidenceSufficient(true, true, true, false, 1, 1, 2, true, 3625, 3626));
}

TEST(ImpactPhase2, AledAndSaveStayAfterLaterPendingDeath)
{
	CReplay Replay(33, TestContext());
	StartPair(Replay);
	const SImpactGameplayMessage Aled = ImpactSignal(1, EImpactGameplayMessageType::CONFIRMED_ALED, 200, TestId(1), TestId(2));
	EXPECT_FALSE(Replay.SubmitGameplay({ImpactFrame(1, 200), ImpactFrame(2, 200)}, {Aled}));
	EXPECT_TRUE(Replay.SubmitGameplay({ImpactFrame(1, 235), ImpactFrame(2, 235)}));
	SImpactGameplayFrame HarmfulFreeze = ImpactFrame(2, 240, true);
	Replay.SubmitGameplay({ImpactFrame(1, 240), HarmfulFreeze});
	SImpactGameplayFrame Victim = ImpactFrame(2, 250, false, false);
	EXPECT_FALSE(Replay.SubmitGameplay({ImpactFrame(1, 250), Victim}, {ImpactKill(2, 250, 2, 2, WEAPON_WORLD)}));
	EXPECT_EQ(Replay.Run().RawStats().m_Aleds, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
}

TEST(ImpactPhase2, RepeatedRecoveryEpisodesRemainSeparate)
{
	CReplay Replay(34, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	SetHooked(Saver, 2, true);
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	SetPhysicalHarmfulOutcome(Target);
	Target.m_OrdinaryFreezeTileKnown = true;
	Target.m_OrdinaryFreezeTile = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 105;
	Target.m_X = 16;
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 175;
	Replay.SubmitGameplay({Saver, Target});
	Target.m_ServerTick = 200;
	Target.m_Frozen = true;
	Target.m_FreezeEnd = 210;
	SetPhysicalHarmfulOutcome(Target);
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 205;
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 210;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 64;
	ClearPhysicalHarmfulOutcome(Target);
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 275;
	Replay.SubmitGameplay({Saver, Target});
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::SAVE_CONFIRMED), 2);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 2);
}

TEST(ImpactPhase2, RepeatedAledSignalsRemainSeparateEvents)
{
	CReplay Replay(35, TestContext());
	StartPair(Replay);
	const SImpactGameplayMessage First = ImpactSignal(1, EImpactGameplayMessageType::CONFIRMED_ALED, 100, TestId(1), TestId(2));
	const SImpactGameplayMessage Second = ImpactSignal(2, EImpactGameplayMessageType::CONFIRMED_ALED, 200, TestId(1), TestId(2));
	EXPECT_FALSE(Replay.SubmitGameplay({ImpactFrame(1, 100), ImpactFrame(2, 100)}, {First}));
	EXPECT_TRUE(Replay.SubmitGameplay({ImpactFrame(1, 135), ImpactFrame(2, 135)}));
	EXPECT_FALSE(Replay.SubmitGameplay({ImpactFrame(1, 200), ImpactFrame(2, 200)}, {Second}));
	EXPECT_TRUE(Replay.SubmitGameplay({ImpactFrame(1, 235), ImpactFrame(2, 235)}));
	EXPECT_EQ(Replay.Run().RawStats().m_Aleds, 2);
}

TEST(ImpactPhase2, ConfirmedJumpResetIsNonHarmful)
{
	CReplay Replay(36, TestContext());
	StartPair(Replay);
	SImpactGameplayMessage Message = ImpactSignal(1, EImpactGameplayMessageType::CONFIRMED_JUMP_RESET, 100, TestId(1), TestId(1));
	EXPECT_TRUE(Replay.SubmitGameplay({ImpactFrame(1, 100), ImpactFrame(2, 100)}, {Message}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_EQ(Replay.Run().Events().back().m_Type, EImpactEventType::JUMP_RESET_CONFIRMED);
}

TEST(ImpactPhase2, JumpResetDoesNotImmunizeLaterPendingHarm)
{
	CReplay Replay(37, TestContext());
	StartPair(Replay);
	SImpactGameplayMessage ResetMessage = ImpactSignal(1, EImpactGameplayMessageType::CONFIRMED_JUMP_RESET, 100, TestId(1), TestId(1));
	Replay.SubmitGameplay({ImpactFrame(1, 100), ImpactFrame(2, 100)}, {ResetMessage});
	SImpactGameplayFrame HarmfulFreeze = ImpactFrame(1, 190, true);
	SetSelfCausedHarm(HarmfulFreeze);
	HarmfulFreeze.m_HarmfulOutcomeCandidateKnown = true;
	HarmfulFreeze.m_HarmfulOutcomeCandidate = true;
	EXPECT_FALSE(Replay.SubmitGameplay({HarmfulFreeze, ImpactFrame(2, 190)}));
	HarmfulFreeze.m_ServerTick = 191;
	HarmfulFreeze.m_AutonomousRecoveryKnown = true;
	HarmfulFreeze.m_AutonomousRecoveryPossible = false;
	EXPECT_TRUE(Replay.SubmitGameplay({HarmfulFreeze, ImpactFrame(2, 191)}));
	SImpactGameplayFrame Victim = ImpactFrame(1, 200, false, false);
	EXPECT_FALSE(Replay.SubmitGameplay({Victim, ImpactFrame(2, 200)}, {ImpactKill(2, 200, 1, 1, WEAPON_WORLD)}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
}

TEST(ImpactPhase2, ConfirmedDownwardResetSignalDoesNotNeedNewMagicAngle)
{
	CReplay Replay(38, TestContext());
	StartPair(Replay);
	SImpactGameplayMessage Message = ImpactSignal(1, EImpactGameplayMessageType::CONFIRMED_JUMP_RESET, 100, TestId(1), TestId(1));
	Message.m_Reason = "server-confirmed reset signal with non-perfect vertical aim";
	EXPECT_TRUE(Replay.SubmitGameplay({ImpactFrame(1, 100), ImpactFrame(2, 100)}, {Message}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
}

TEST(ImpactPhase2, OrdinaryHarmfulShotHasNoResetImmunity)
{
	CReplay Replay(39, TestContext());
	StartPair(Replay);
	ASSERT_FALSE(Replay.SubmitGameplay({ImpactFrame(1, 80), ImpactFrame(2, 80)}));
	SImpactGameplayFrame HarmfulFreeze = ImpactFrame(1, 90, true);
	SetSelfCausedHarm(HarmfulFreeze);
	HarmfulFreeze.m_HarmfulOutcomeCandidateKnown = true;
	HarmfulFreeze.m_HarmfulOutcomeCandidate = true;
	EXPECT_FALSE(Replay.SubmitGameplay({HarmfulFreeze, ImpactFrame(2, 90)}));
	HarmfulFreeze.m_ServerTick = 91;
	HarmfulFreeze.m_AutonomousRecoveryKnown = true;
	HarmfulFreeze.m_AutonomousRecoveryPossible = false;
	EXPECT_TRUE(Replay.SubmitGameplay({HarmfulFreeze, ImpactFrame(2, 91)}));
	SImpactGameplayFrame Victim = ImpactFrame(1, 100, false, false);
	// The harmful entry is already confirmed. A later death only closes the
	// retained context and therefore produces no duplicate raw Fall event.
	EXPECT_FALSE(Replay.SubmitGameplay({Victim, ImpactFrame(2, 100)}, {ImpactKill(1, 100, 1, 1, WEAPON_WORLD)}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	EXPECT_FALSE(std::any_of(Replay.Run().Events().begin(), Replay.Run().Events().end(), [](const SImpactEvent &Event) {
		return Event.m_Type == EImpactEventType::JUMP_RESET_CONFIRMED;
	}));
}

TEST(ImpactPhase2, TargetDisconnectCancelsPendingSave)
{
	CReplay Replay(40, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	SetHooked(Saver, 2);
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Replay.SubmitGameplay({Saver, Target});
	EXPECT_FALSE(Replay.SubmitGameplay({Saver}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
}

TEST(ImpactPhase2, SaverDisconnectDoesNotEraseConfirmedCausality)
{
	CReplay Replay(41, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	SetHooked(Saver, 2, true);
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	SetPhysicalHarmfulOutcome(Target);
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	Replay.SubmitGameplay({Saver, Target});
	Target.m_ServerTick = 175;
	EXPECT_TRUE(Replay.SubmitGameplay({Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().Participants()[0].m_RawStats.m_Saves, 1);
}

TEST(ImpactPhase2, PendingAledIsCancelledByEarlyFinish)
{
	CReplay Replay(42, TestContext());
	StartPair(Replay);
	Replay.SetPendingEpisodes({PendingEpisode(1, EImpactEventType::ALED_CONFIRMED, TestId(2), 0)});
	SImpactGameplayFrame A = ImpactFrame(1, 10);
	A.m_Finished = true;
	SImpactGameplayFrame B = ImpactFrame(2, 10);
	B.m_Finished = true;
	EXPECT_TRUE(Replay.SubmitGameplay({A, B}, {ImpactFinish(1, 10, 1), ImpactFinish(2, 10, 2)}));
	EXPECT_EQ(Replay.Run().RawStats().m_Aleds, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
}

TEST(ImpactPhase2, PendingRecoveryIsCancelledByEarlyFinish)
{
	CReplay Replay(43, TestContext());
	StartPair(Replay);
	Replay.SetPendingEpisodes({PendingEpisode(1, EImpactEventType::SAVE_CONFIRMED, TestId(2), 0)});
	SImpactGameplayFrame A = ImpactFrame(1, 10);
	A.m_Finished = true;
	SImpactGameplayFrame B = ImpactFrame(2, 10);
	B.m_Finished = true;
	EXPECT_TRUE(Replay.SubmitGameplay({A, B}, {ImpactFinish(1, 10, 1), ImpactFinish(2, 10, 2)}));
	EXPECT_EQ(Replay.Run().RawStats().m_UpDowns, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
}

TEST(ImpactPhase2, PendingFallIsNotConfirmedByWipeWithoutIndividualOutcome)
{
	CReplay Replay(44, TestContext());
	StartPair(Replay);
	Replay.SetPendingEpisodes({PendingEpisode(1, EImpactEventType::FALL_CONFIRMED, TestId(2), 0)});
	SImpactGameplayMessage Wipe;
	Wipe.m_MessageId = 1;
	Wipe.m_Type = EImpactGameplayMessageType::TEAM_WIPE;
	Wipe.m_ServerTick = 10;
	Wipe.m_TeamId = 1;
	Wipe.m_CausalConfirmed = true;
	Wipe.m_CausalConfidence = ECausalConfidence::CONFIRMED;
	EXPECT_TRUE(Replay.SubmitGameplay({ImpactFrame(1, 10), ImpactFrame(2, 10)}, {Wipe}));
	// Team wipe is only server outcome context. It never proves an individual
	// self-caused Fall for every member of the team.
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_EQ(Replay.Run().Result(), ERunResult::WIPED);
}

TEST(ImpactPhase2, AmbiguousPendingFallIsNotAttributedByWipe)
{
	CReplay Replay(45, TestContext());
	StartPair(Replay);
	Replay.SetPendingEpisodes({PendingEpisode(1, EImpactEventType::FALL_CONFIRMED, TestId(2), 0)});
	SImpactGameplayMessage Wipe;
	Wipe.m_MessageId = 1;
	Wipe.m_Type = EImpactGameplayMessageType::TEAM_WIPE;
	Wipe.m_ServerTick = 10;
	Wipe.m_TeamId = 1;
	Wipe.m_CausalConfirmed = true;
	Wipe.m_CausalConfidence = ECausalConfidence::AMBIGUOUS;
	EXPECT_TRUE(Replay.SubmitGameplay({ImpactFrame(1, 10), ImpactFrame(2, 10)}, {Wipe}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_EQ(Replay.Run().Result(), ERunResult::WIPED);
}

TEST(ImpactPhase2, UnprovenPendingFallIsNotPromotedByWipe)
{
	CReplay Replay(79, TestContext());
	StartPair(Replay);
	SImpactPendingEpisode Pending = PendingEpisode(1, EImpactEventType::FALL_CONFIRMED, TestId(2), 0);
	// HIGH is sufficient evidence for the production team-wipe path. This
	// fixture is intentionally unproven and must remain below that gate.
	Pending.m_CausalConfidence = ECausalConfidence::INSUFFICIENT;
	Pending.m_HarmCausalConfidence = ECausalConfidence::INSUFFICIENT;
	Replay.SetPendingEpisodes({Pending});
	SImpactGameplayMessage Wipe;
	Wipe.m_MessageId = 1;
	Wipe.m_Type = EImpactGameplayMessageType::TEAM_WIPE;
	Wipe.m_ServerTick = 10;
	Wipe.m_TeamId = 1;
	Wipe.m_CausalConfirmed = true;
	Wipe.m_CausalConfidence = ECausalConfidence::CONFIRMED;
	EXPECT_TRUE(Replay.SubmitGameplay({ImpactFrame(1, 10), ImpactFrame(2, 10)}, {Wipe}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_EQ(Replay.Run().Result(), ERunResult::WIPED);
}

TEST(ImpactPhase2, ManualTeamResetHasNoGameplayPenalty)
{
	CReplay Replay(46, TestContext());
	StartPair(Replay);
	SImpactGameplayMessage Reset;
	Reset.m_MessageId = 1;
	Reset.m_Type = EImpactGameplayMessageType::TEAM_RESET;
	Reset.m_ServerTick = 100;
	Reset.m_TeamId = 1;
	EXPECT_TRUE(Replay.SubmitGameplay({ImpactFrame(1, 100), ImpactFrame(2, 100)}, {Reset}));
	EXPECT_EQ(Replay.Run().Result(), ERunResult::TEAM_RESET);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_TeamMistakes, 0);
}

TEST(ImpactPhase2, SoloStateSuppressesTeamInteraction)
{
	CReplay Replay(47, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	Saver.m_Solo = true;
	SetHooked(Saver, 2);
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	Target.m_Solo = true;
	Replay.SubmitGameplay({Saver, Target});
	Target.m_ServerTick = Saver.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::ACTIVE);
}

TEST(ImpactPhase2, PendingEpisodesSurviveCheckpointRestore)
{
	CReplay Replay(48, TestContext());
	StartPair(Replay);
	Replay.SetPendingEpisodes({PendingEpisode(9, EImpactEventType::SAVE_CONFIRMED, TestId(2), 100)});
	const SRunSnapshot SnapshotData = Replay.Run().Snapshot(120);
	CImpactRun Restored;
	ASSERT_TRUE(Restored.RestoreSnapshot(SnapshotData));
	ASSERT_EQ(Restored.PendingEpisodes().size(), 1U);
	EXPECT_EQ(Restored.PendingEpisodes()[0].m_EpisodeId, 9U);
}

TEST(ImpactPhase2, RawCountersAreAppliedOnlyThroughCanonicalEvents)
{
	CReplay Replay(49, TestContext());
	StartPair(Replay);
	SImpactEvent Event;
	Event.m_EventId = 100;
	Event.m_Type = EImpactEventType::RECOVERY_DIFFICULTY_INCREASE_CONFIRMED;
	Event.m_TeamId = 1;
	Event.m_Actor = TestId(1);
	Event.m_Confirmed = true;
	Event.m_StartTick = Event.m_ResolveTick = 100;
	Event.m_RawDelta.m_RecoveryDifficultyIncreases = 1;
	EXPECT_TRUE(Replay.Submit({100, {Event}}));
	EXPECT_EQ(Replay.Run().RawStats().m_RecoveryDifficultyIncreases, 1);
	EXPECT_EQ(Replay.Run().Participants()[0].m_RawStats.m_RecoveryDifficultyIncreases, 1);
}

TEST(ImpactPhase2, SameConfirmedReplayIsDeterministic)
{
	CReplay First(50, TestContext());
	CReplay Second(50, TestContext());
	StartPair(First);
	StartPair(Second);
	SImpactGameplayFrame FirstSaver = ImpactFrame(1, 100);
	SetHooked(FirstSaver, 2);
	SImpactGameplayFrame FirstTarget = ImpactFrame(2, 100, true);
	SImpactGameplayFrame SecondSaver = FirstSaver;
	SImpactGameplayFrame SecondTarget = FirstTarget;
	First.SubmitGameplay({FirstSaver, FirstTarget});
	Second.SubmitGameplay({SecondSaver, SecondTarget});
	FirstSaver.m_ServerTick = FirstTarget.m_ServerTick = 110;
	SecondSaver.m_ServerTick = SecondTarget.m_ServerTick = 110;
	FirstTarget.m_Frozen = SecondTarget.m_Frozen = false;
	FirstTarget.m_FreezeEnd = SecondTarget.m_FreezeEnd = 0;
	First.SubmitGameplay({FirstSaver, FirstTarget});
	Second.SubmitGameplay({SecondSaver, SecondTarget});
	FirstSaver.m_ServerTick = FirstTarget.m_ServerTick = 175;
	SecondSaver.m_ServerTick = SecondTarget.m_ServerTick = 175;
	First.SubmitGameplay({FirstSaver, FirstTarget});
	Second.SubmitGameplay({SecondSaver, SecondTarget});
	ASSERT_EQ(First.Run().Events().size(), Second.Run().Events().size());
	ASSERT_EQ(First.Run().RawStats().m_Saves, Second.Run().RawStats().m_Saves);
	ASSERT_EQ(First.Run().PendingEpisodes().size(), Second.Run().PendingEpisodes().size());
	ASSERT_FALSE(First.Run().Events().empty());
	EXPECT_EQ(First.Run().Events().back().m_Type, Second.Run().Events().back().m_Type);
	EXPECT_EQ(First.Run().Events().back().m_Actor, Second.Run().Events().back().m_Actor);
	EXPECT_EQ(First.Run().Events().back().m_Target, Second.Run().Events().back().m_Target);
}

TEST(ImpactPhase2, HookEvidenceSurvivesReleaseBeforeRestoration)
{
	CReplay Replay(51, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	SetHooked(Saver, 2, true);
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	SetPhysicalHarmfulOutcome(Target);
	Target.m_OrdinaryFreezeTileKnown = true;
	Target.m_OrdinaryFreezeTile = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 105;
	Target.m_X = 16;
	Replay.SubmitGameplay({Saver, Target});

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	ReleaseHook(Saver);
	Replay.SubmitGameplay({Saver, Target});

	Saver.m_ServerTick = Target.m_ServerTick = 120;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	Saver.m_ServerTick = Target.m_ServerTick = 185;
	EXPECT_TRUE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	// The hook evidence belongs to the frozen episode, not only to the exact
	// restoration snapshot. Releasing it before restoration must not erase the
	// valid episode-level recovery classification.
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::SAVE_CONFIRMED), 1);
}

TEST(ImpactPhase2, RescueAfterUnconfirmedRestorationRearmsForNextInteraction)
{
	auto RunScenario = [](uint64_t RunId, bool ReleaseBeforeRestoration) {
		CReplay Replay(RunId, TestContext());
		StartPair(Replay);
		SImpactGameplayFrame Saver = ImpactFrame(1, 100);
		SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
		Target.m_FreezeEnd = 220;
		EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));

		// This first unfrozen boundary has no target-specific interaction and does
		// not survive the canonical confirmation window. The next freeze therefore
		// remains in the same physical context, but must be eligible for a new
		// rescue interaction.
		Saver.m_ServerTick = Target.m_ServerTick = 110;
		Target.m_Frozen = false;
		Target.m_FreezeStart = -1;
		Target.m_FreezeEnd = 0;
		Target.m_X = 8;
		EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
		EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);

		// Re-enter freeze before 35 ticks have elapsed, matching the live 5210
		// and 5816 false-negative shape after their earlier 4970/5616 restore.
		Saver.m_ServerTick = Target.m_ServerTick = 120;
		Target.m_Frozen = true;
		Target.m_FreezeStart = 120;
		Target.m_FreezeEnd = 300;
		Target.m_X = 16;
		EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));

		// Establish target-specific physical evidence while the target is frozen.
		Saver.m_ServerTick = Target.m_ServerTick = 130;
		SetHooked(Saver, 2);
		EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
		Saver.m_ServerTick = Target.m_ServerTick = 132;
		Target.m_X = 32;
		EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));

		int64_t RestoreTick = 134;
		if(ReleaseBeforeRestoration)
		{
			Saver.m_ServerTick = Target.m_ServerTick = 134;
			ReleaseHook(Saver);
			EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
			RestoreTick = 180;
		}

		Saver.m_ServerTick = Target.m_ServerTick = RestoreTick;
		Target.m_Frozen = false;
		Target.m_FreezeStart = -1;
		Target.m_FreezeEnd = 0;
		Target.m_X = 48;
		EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
		EXPECT_NE(FindDebugLabel(Replay.LastOutput(), "SAVE_RESTORE"), nullptr);
		EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);

		Saver.m_ServerTick = Target.m_ServerTick = RestoreTick + 36;
		EXPECT_TRUE(Replay.SubmitGameplay({Saver, Target}));
		EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
		EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::SAVE_CONFIRMED), 1);

		// Replaying later confirmation frames must not create a second Save.
		Saver.m_ServerTick = Target.m_ServerTick = RestoreTick + 46;
		EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
		EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
		EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::SAVE_CONFIRMED), 1);
	};

	RunScenario(61, false);
	RunScenario(62, true);
}

TEST(ImpactPhase2, UnattributedHammerSwingDoesNotCreateSave)
{
	CReplay Replay(52, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	Saver.m_Weapon = WEAPON_HAMMER;
	Saver.m_AttackTick = 1;
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	Replay.SubmitGameplay({Saver, Target});

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Saver.m_AttackTick = 2;
	Replay.SubmitGameplay({Saver, Target});

	Saver.m_ServerTick = Target.m_ServerTick = 120;
	Saver.m_Weapon = WEAPON_GUN;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 185;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_UpDowns, 0);
	EXPECT_EQ(Replay.Run().Participants()[0].m_RawStats.m_Saves, 0);
}

TEST(ImpactPhase2, UnknownFreezeDoesNotCreateRestoration)
{
	CReplay Replay(59, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	SetHooked(Saver, 2);
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	Replay.SubmitGameplay({Saver, Target});

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_FreezeKnown = false;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().PendingEpisodes().size(), 1U);

	Saver.m_ServerTick = Target.m_ServerTick = 120;
	Target.m_FreezeKnown = true;
	Target.m_Frozen = true;
	Target.m_FreezeEnd = 220;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().PendingEpisodes().size(), 1U);
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
}

TEST(ImpactPhase2, UnknownPreActionWithoutPhysicalChangeCannotRestoreAsSave)
{
	CReplay Replay(60, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	SetHooked(Saver, 2, true);
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 105;
	Replay.SubmitGameplay({Saver, Target});

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_FreezeKnown = false;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Replay.SubmitGameplay({Saver, Target});

	Saver.m_ServerTick = Target.m_ServerTick = 120;
	Target.m_FreezeKnown = true;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().PendingEpisodes().size(), 1U);

	Saver.m_ServerTick = Target.m_ServerTick = 185;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::SAVE_CONFIRMED), 0);
}

TEST(ImpactPhase2, UnknownConfirmationTimeIsNotCounted)
{
	CReplay Replay(73, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	SetHooked(Saver, 2, true);
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	SetPhysicalHarmfulOutcome(Target);
	Replay.SubmitGameplay({Saver, Target});

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	Replay.SubmitGameplay({Saver, Target});

	// The target remains present, but freeze metadata is unavailable. This
	// interrupts (rather than satisfies) the continuous confirmation window.
	Saver.m_ServerTick = Target.m_ServerTick = 120;
	Target.m_FreezeKnown = false;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 180;
	Target.m_FreezeKnown = true;
	Target.m_Frozen = false;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);

	Saver.m_ServerTick = Target.m_ServerTick = 215;
	EXPECT_TRUE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
}

TEST(ImpactPhase2, ExplicitTargetSpecificHammerEvidenceCanSaveWithoutHook)
{
	CReplay Replay(61, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	Saver.m_Weapon = WEAPON_HAMMER;
	Saver.m_AttackTick = 1;
	Saver.m_HammerTargetEvidence = true;
	Saver.m_HookKnown = false;
	Saver.m_HookStateConfirmed = false;
	Saver.m_HookedPlayer = -1;
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	Target.m_FreezeEnd = 200;
	Target.m_OrdinaryFreezeTileKnown = true;
	Target.m_OrdinaryFreezeTile = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = false;
	Replay.SubmitGameplay({Saver, Target});

	// Reliable hammer attribution is valid only when the target's physical
	// change is observed while the problem is still active. This distinguishes
	// a real hammer rescue from an attack followed by ordinary natural expiry.
	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_X = 16;
	Replay.SubmitGameplay({Saver, Target});

	Saver.m_ServerTick = Target.m_ServerTick = 120;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
	Saver.m_ServerTick = Target.m_ServerTick = 185;
	EXPECT_TRUE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_UpDowns, 0);
	const auto HammerSave = std::find_if(Replay.Run().Events().rbegin(), Replay.Run().Events().rend(), [](const SImpactEvent &Event) {
		return Event.m_Type == EImpactEventType::SAVE_CONFIRMED;
	});
	ASSERT_NE(HammerSave, Replay.Run().Events().rend());
	EXPECT_EQ(HammerSave->m_Actor, TestId(1));
}

TEST(ImpactPhase2, TargetSpecificHammerAfterNaturalExpiryWithoutRescueIsNotSave)
{
	CReplay Replay(80, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	Saver.m_Weapon = WEAPON_HAMMER;
	Saver.m_AttackTick = 100;
	Saver.m_HammerTargetEvidence = true;
	Saver.m_HookKnown = false;
	Saver.m_HookStateConfirmed = false;
	Saver.m_HookedPlayer = -1;
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	Target.m_FreezeEnd = 110;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 120;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 190;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_UpDowns, 0);
}

TEST(ImpactPhase2, HookHeldHammerCausedUnfreezeKeepsHookSave)
{
	CReplay Replay(66, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	SetHooked(Saver, 2);
	Saver.m_Weapon = WEAPON_HAMMER;
	Saver.m_AttackTick = 1;
	Saver.m_HammerTargetEvidence = true;
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	Target.m_FreezeEnd = 200;
	SetPhysicalHarmfulOutcome(Target);
	Replay.SubmitGameplay({Saver, Target});

	// The target becomes unfrozen before its known timer expiry because of the
	// target-specific hammer evidence, while the hook remains held.
	Saver.m_ServerTick = Target.m_ServerTick = 120;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	// Restoration now materializes only the independent Save confirmation record;
	// rescue must never synthesize a Fall event at this boundary.
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	// The unconfirmed problem episode remains alongside the independent Save
	// confirmation record; it has not emitted a Fall at the restoration boundary.
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 2U);
	const auto HammerEpisode = std::find_if(Replay.Run().PendingEpisodes().begin(), Replay.Run().PendingEpisodes().end(), [](const SImpactPendingEpisode &Episode) {
		return Episode.m_Type == EImpactEventType::SAVE_CONFIRMED;
	});
	ASSERT_NE(HammerEpisode, Replay.Run().PendingEpisodes().end());
	// DDNet's live hammer path does not provide a reliable actor->target pair.
	// The hook evidence remains causal, but the unfreeze cause must stay unknown.
	EXPECT_EQ(HammerEpisode->m_UnfreezeCause, EImpactUnfreezeCause::UNKNOWN);
	EXPECT_FALSE(HammerEpisode->m_UpDownEvidence);

	Saver.m_ServerTick = Target.m_ServerTick = 155;
	EXPECT_TRUE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_UpDowns, 0);
}

TEST(ImpactPhase2, UnknownUnfreezeCauseKeepsSave)
{
	CReplay Replay(67, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	SetHooked(Saver, 2, true);
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	SetPhysicalHarmfulOutcome(Target);
	// No authoritative timer expiry is available, so the unfreeze cause is
	// intentionally unknown even though the hook evidence is valid.
	Target.m_FreezeEnd = 0;
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Replay.SubmitGameplay({Saver, Target});

	Saver.m_ServerTick = Target.m_ServerTick = 120;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	// The hook remains valid Save evidence, but an unknown unfreeze cause is not a
	// Fall writer trigger and therefore produces no event at restoration.
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	ASSERT_EQ(Replay.Run().PendingEpisodes().size(), 2U);
	const auto UnknownCauseEpisode = std::find_if(Replay.Run().PendingEpisodes().begin(), Replay.Run().PendingEpisodes().end(), [](const SImpactPendingEpisode &Episode) {
		return Episode.m_Type == EImpactEventType::SAVE_CONFIRMED;
	});
	ASSERT_NE(UnknownCauseEpisode, Replay.Run().PendingEpisodes().end());
	EXPECT_EQ(UnknownCauseEpisode->m_UnfreezeCause, EImpactUnfreezeCause::UNKNOWN);
	EXPECT_FALSE(UnknownCauseEpisode->m_UpDownEvidence);

	Saver.m_ServerTick = Target.m_ServerTick = 155;
	EXPECT_TRUE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_UpDowns, 0);
}

TEST(ImpactPhase2, UnknownRecoveryNeedDoesNotBlockOrdinaryFallOrHookSave)
{
	CReplay Replay(68, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	SetHooked(Saver, 2);
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true, true, true, 1, false, EImpactRecoveryNeed::UNKNOWN);
	SetPhysicalHarmfulOutcome(Target);
	ASSERT_FALSE(Replay.SubmitGameplay({ImpactFrame(1, 90), ImpactFrame(2, 90)}));
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	Target.m_ServerTick = 101;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	EXPECT_TRUE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);

	Saver.m_ServerTick = Target.m_ServerTick = 120;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	Target.m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
	ClearPhysicalHarmfulOutcome(Target);
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 200;
	EXPECT_TRUE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_UpDowns, 0);
	EXPECT_TRUE(Replay.Run().PendingEpisodes().empty());
}

TEST(ImpactPhase2, SelfSafeRecoveryWithInteractionHasNoImpactPenalty)
{
	CReplay Replay(69, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	SetHooked(Saver, 2);
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true, true, true, 1, false, EImpactRecoveryNeed::SELF_SAFE);
	Replay.SubmitGameplay({Saver, Target});

	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_RecoveryNeed = EImpactRecoveryNeed::SELF_SAFE;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 180;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_UpDowns, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
}

TEST(ImpactPhase2, SelfSafeHammerUnfreezeHasNoImpactPenalty)
{
	CReplay Replay(74, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	Saver.m_Weapon = WEAPON_HAMMER;
	Saver.m_AttackTick = 1;
	Saver.m_HammerTargetEvidence = true;
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true, true, true, 1, false, EImpactRecoveryNeed::SELF_SAFE);
	Target.m_FreezeEnd = 200;
	Replay.SubmitGameplay({Saver, Target});

	Saver.m_ServerTick = Target.m_ServerTick = 120;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_RecoveryNeed = EImpactRecoveryNeed::SELF_SAFE;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 180;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_UpDowns, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 0);
}

TEST(ImpactPhase2, DirectHarmFallAndLaterSaveRemainIndependent)
{
	CReplay Replay(70, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	SetHooked(Saver, 2, true);
	ASSERT_FALSE(Replay.SubmitGameplay({ImpactFrame(1, 90), ImpactFrame(2, 90)}));
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	Target.m_FreezeEnd = 200;
	SetSelfCausedHarm(Target);
	Target.m_HarmfulOutcomeCandidateKnown = true;
	Target.m_HarmfulOutcomeCandidate = true;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	Target.m_ServerTick = 101;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	EXPECT_TRUE(Replay.SubmitGameplay({Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 200;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_HarmfulOutcomeCandidateKnown = false;
	Target.m_HarmfulOutcomeCandidate = false;
	Target.m_X = 32;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 235;
	EXPECT_TRUE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
}

TEST(ImpactPhase2, FailedSaveBeforeConfirmationCanLaterConfirmOnlyFall)
{
	CReplay Replay(71, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	SetHooked(Saver, 2, true);
	ASSERT_FALSE(Replay.SubmitGameplay({ImpactFrame(1, 90), ImpactFrame(2, 90)}));
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	Target.m_FreezeEnd = 200;
	SetSelfCausedHarm(Target);
	Target.m_HarmfulOutcomeCandidateKnown = true;
	Target.m_HarmfulOutcomeCandidate = true;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	Target.m_ServerTick = 101;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	EXPECT_TRUE(Replay.SubmitGameplay({Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 120;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 140;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 140;
	Target.m_FreezeEnd = 240;
	Target.m_HarmfulOutcomeCandidateKnown = false;
	Target.m_HarmfulOutcomeCandidate = false;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));

	Saver.m_ServerTick = Target.m_ServerTick = 250;
	Target.m_Alive = false;
	Target.m_Active = false;
	// The harmful entry was already counted. Death closes the retained episode
	// without emitting a second Fall event.
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}, {ImpactKill(1, 250, 2, 2, WEAPON_WORLD)}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 0);
	EXPECT_EQ(Replay.Run().RawStats().m_Falls, 1);
}

TEST(ImpactPhase2, RehookWithinSameFreezeEpisodeRetainsRecoveryEvidence)
{
	CReplay Replay(72, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	SetHooked(Saver, 2, true);
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	SetPhysicalHarmfulOutcome(Target);
	Target.m_FreezeEnd = 200;
	Replay.SubmitGameplay({Saver, Target});

	Saver.m_ServerTick = Target.m_ServerTick = 105;
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 110;
	ReleaseHook(Saver);
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 120;
	SetHooked(Saver, 2, true);
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 130;
	Replay.SubmitGameplay({Saver, Target});

	Saver.m_ServerTick = Target.m_ServerTick = 200;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 235;
	EXPECT_TRUE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::SAVE_CONFIRMED), 1);
}

TEST(ImpactPhase2, MultiAnchorRehooksAggregateOneRecoveryEpisode)
{
	CReplay Replay(90, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	SetPhysicalHarmfulOutcome(Target);
	Target.m_FreezeEnd = 200;

	SetHooked(Saver, 2, true);
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 105;
	ReleaseHook(Saver);
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 110;
	SetHooked(Saver, 2, true);
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 115;
	ReleaseHook(Saver);
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 120;
	SetHooked(Saver, 2, true);
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 125;
	ReleaseHook(Saver);
	Replay.SubmitGameplay({Saver, Target});

	// Each span has only one grabbed snapshot, but each carries separate
	// target-specific support evidence. Together they remain one episode; no
	// particular anchor is required and observation count is not used.
	Saver.m_ServerTick = Target.m_ServerTick = 200;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	// Restoration only creates the independent Save confirmation record; this
	// rescue cannot synthesize a Fall event at the restoration boundary.
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target}));
	Saver.m_ServerTick = Target.m_ServerTick = 235;
	EXPECT_TRUE(Replay.SubmitGameplay({Saver, Target}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::SAVE_CONFIRMED), 1);
}

TEST(ImpactPhase2, LaterDecisiveSaverGetsOwnershipAndEarlyHelperStaysHelper)
{
	CReplay Replay(62, TestContext());
	StartPair(Replay);
	STickEventBatch StartThird;
	StartThird.m_ServerTick = 20;
	StartThird.m_vCandidates.push_back(StartEvent(3, TestId(3), 20, "C"));
	ASSERT_TRUE(Replay.Submit(StartThird));

	SImpactGameplayFrame Helper = ImpactFrame(1, 100);
	SImpactGameplayFrame Saver = ImpactFrame(2, 100);
	SImpactGameplayFrame Target = ImpactFrame(3, 100, true);
	SetPhysicalHarmfulOutcome(Target);
	SetHooked(Helper, Target.m_Participant.m_ClientId);
	EXPECT_FALSE(Replay.SubmitGameplay({Helper, Saver, Target}));
	Target.m_ServerTick = 101;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	EXPECT_TRUE(Replay.SubmitGameplay({Helper, Saver, Target}));

	Helper.m_ServerTick = Saver.m_ServerTick = Target.m_ServerTick = 110;
	ReleaseHook(Helper);
	SetHooked(Saver, Target.m_Participant.m_ClientId, true);
	Target.m_X = 16;
	Replay.SubmitGameplay({Helper, Saver, Target});

	Helper.m_ServerTick = Saver.m_ServerTick = Target.m_ServerTick = 120;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	EXPECT_FALSE(Replay.SubmitGameplay({Helper, Saver, Target}));

	Helper.m_ServerTick = Saver.m_ServerTick = Target.m_ServerTick = 185;
	ASSERT_TRUE(Replay.SubmitGameplay({Helper, Saver, Target}));
	ASSERT_FALSE(Replay.Run().Events().empty());
	const auto SaveEvent = std::find_if(Replay.Run().Events().rbegin(), Replay.Run().Events().rend(), [](const SImpactEvent &Event) {
		return Event.m_Type == EImpactEventType::SAVE_CONFIRMED;
	});
	ASSERT_NE(SaveEvent, Replay.Run().Events().rend());
	EXPECT_EQ(SaveEvent->m_Actor, TestId(2));
	EXPECT_EQ(SaveEvent->m_Target, TestId(3));
	EXPECT_EQ(SaveEvent->m_vHelpers.size(), 1U);
	EXPECT_EQ(SaveEvent->m_vHelpers[0], TestId(1));
	EXPECT_EQ(Replay.Run().Participants()[1].m_RawStats.m_Saves, 1);
	EXPECT_EQ(Replay.Run().Participants()[0].m_RawStats.m_Saves, 0);
}

TEST(ImpactPhase2, TwoPreSpecHookAttemptsAreInvalidatedAtSpecUnspecBoundary)
{
	CReplay Replay(6201, TestContext());
	StartPair(Replay);
	StartThirdParticipant(Replay);

	SImpactGameplayFrame EarlyActor = ImpactFrame(1, 100);
	SImpactGameplayFrame LaterActor = ImpactFrame(2, 100);
	SImpactGameplayFrame Target = ImpactFrame(3, 100, true);
	SetPhysicalHarmfulOutcome(Target);
	SetHooked(EarlyActor, Target.m_Participant.m_ClientId, true);
	EXPECT_FALSE(Replay.SubmitGameplay({EarlyActor, LaterActor, Target}));

	EarlyActor.m_ServerTick = LaterActor.m_ServerTick = Target.m_ServerTick = 101;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	EXPECT_TRUE(Replay.SubmitGameplay({EarlyActor, LaterActor, Target}));

	EarlyActor.m_ServerTick = LaterActor.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_X = 16;
	EXPECT_FALSE(Replay.SubmitGameplay({EarlyActor, LaterActor, Target}));
	// A second Ashura hook span still belongs to the pre-SpecChar lifecycle.
	EarlyActor.m_ServerTick = LaterActor.m_ServerTick = Target.m_ServerTick = 112;
	ReleaseHook(EarlyActor);
	EXPECT_FALSE(Replay.SubmitGameplay({EarlyActor, LaterActor, Target}));
	EarlyActor.m_ServerTick = LaterActor.m_ServerTick = Target.m_ServerTick = 113;
	SetHooked(EarlyActor, Target.m_Participant.m_ClientId, true);
	Target.m_X = 24;
	EXPECT_FALSE(Replay.SubmitGameplay({EarlyActor, LaterActor, Target}));
	EarlyActor.m_ServerTick = LaterActor.m_ServerTick = Target.m_ServerTick = 115;
	Target.m_SupportKnown = true;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	EXPECT_FALSE(Replay.SubmitGameplay({EarlyActor, LaterActor, Target}));
	const SImpactPhase2Debug *pEarlyInteraction = FindDebugLabel(Replay.LastOutput(), "SAVE_INTERACTION");
	ASSERT_NE(pEarlyInteraction, nullptr);
	EXPECT_EQ(pEarlyInteraction->m_PotentialSaver, TestId(1));
	EXPECT_TRUE(pEarlyInteraction->m_DecisivePhysicalEvidence);
	EXPECT_EQ(pEarlyInteraction->m_HookFirstTick, 100);
	EXPECT_EQ(pEarlyInteraction->m_HookLastTick, 115);
	const int FallsBeforeSpec = CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED);

	// A server SpecChar proves actual CCharacter::Pause(true), unlike a sparse
	// snapshot gap. It ends old trajectory attribution, but is not restoration,
	// death, reset, or a new problem episode.
	EarlyActor.m_ServerTick = LaterActor.m_ServerTick = Target.m_ServerTick = 120;
	Target.m_CurrentCharacterKnown = false;
	Target.m_PreviousCharacterKnown = false;
	Target.m_PositionKnown = false;
	Target.m_VelocityKnown = false;
	Target.m_AliveKnown = false;
	Target.m_Alive = false;
	Target.m_FreezeKnown = false;
	Target.m_Frozen = false;
	Target.m_SoloKnown = false;
	Target.m_PhysicallyPausedCharacter = true;
	// DDNet drops the paused character from the world; hookers lose that target
	// on their next core tick and the demo shows no active hook after the pause.
	ReleaseHook(EarlyActor);
	EXPECT_FALSE(Replay.SubmitGameplay({EarlyActor, LaterActor, Target}));
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::SAVE_CONFIRMED), 0);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), FallsBeforeSpec);

	// /unspec resumes the same frozen participant with zero velocity. A fresh,
	// independently evidenced post-boundary rescue now belongs to LaterActor.
	Target.m_ServerTick = EarlyActor.m_ServerTick = LaterActor.m_ServerTick = 125;
	Target.m_CurrentCharacterKnown = true;
	Target.m_PreviousCharacterKnown = false;
	Target.m_PositionKnown = true;
	Target.m_VelocityKnown = true;
	Target.m_AliveKnown = true;
	Target.m_Alive = true;
	Target.m_FreezeKnown = true;
	Target.m_Frozen = true;
	Target.m_SoloKnown = true;
	Target.m_PhysicallyPausedCharacter = false;
	Target.m_X = 16;
	SetHooked(LaterActor, Target.m_Participant.m_ClientId, true);
	EXPECT_FALSE(Replay.SubmitGameplay({EarlyActor, LaterActor, Target}));

	Target.m_ServerTick = EarlyActor.m_ServerTick = LaterActor.m_ServerTick = 130;
	Target.m_X = 32;
	EXPECT_FALSE(Replay.SubmitGameplay({EarlyActor, LaterActor, Target}));
	Target.m_ServerTick = EarlyActor.m_ServerTick = LaterActor.m_ServerTick = 135;
	ReleaseHook(LaterActor);
	Target.m_X = 40;
	EXPECT_FALSE(Replay.SubmitGameplay({EarlyActor, LaterActor, Target}));
	Target.m_ServerTick = EarlyActor.m_ServerTick = LaterActor.m_ServerTick = 140;
	Target.m_X = 48;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	ClearPhysicalHarmfulOutcome(Target);
	EXPECT_FALSE(Replay.SubmitGameplay({EarlyActor, LaterActor, Target}));
	Target.m_ServerTick = EarlyActor.m_ServerTick = LaterActor.m_ServerTick = 175;
	ASSERT_TRUE(Replay.SubmitGameplay({EarlyActor, LaterActor, Target}));

	const auto SaveEvent = std::find_if(Replay.Run().Events().rbegin(), Replay.Run().Events().rend(), [](const SImpactEvent &Event) {
		return Event.m_Type == EImpactEventType::SAVE_CONFIRMED;
	});
	ASSERT_NE(SaveEvent, Replay.Run().Events().rend());
	EXPECT_EQ(SaveEvent->m_Actor, TestId(2));
	EXPECT_EQ(SaveEvent->m_Target, TestId(3));
	EXPECT_EQ(Replay.Run().Participants()[0].m_RawStats.m_Saves, 0);
	EXPECT_EQ(Replay.Run().Participants()[1].m_RawStats.m_Saves, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::FALL_CONFIRMED), FallsBeforeSpec);
}

TEST(ImpactPhase2, SnapshotGapWithoutSpecCharPreservesActorEvidence)
{
	CReplay Replay(6202, TestContext());
	StartPair(Replay);
	StartThirdParticipant(Replay);

	SImpactGameplayFrame Saver = ImpactFrame(1, 100);
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	SImpactGameplayFrame Other = ImpactFrame(3, 100);
	SetPhysicalHarmfulOutcome(Target);
	SetHooked(Saver, Target.m_Participant.m_ClientId, true);
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 101;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	EXPECT_TRUE(Replay.SubmitGameplay({Saver, Target, Other}));
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 110;
	Target.m_X = 16;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 115;
	Target.m_SupportKnown = true;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	// A missing Character object with no server SpecChar is UNKNOWN, not proof
	// of a lifecycle boundary. The old causal interaction remains eligible.
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 120;
	Target.m_CurrentCharacterKnown = false;
	Target.m_PositionKnown = false;
	Target.m_VelocityKnown = false;
	Target.m_AliveKnown = false;
	Target.m_Alive = false;
	Target.m_FreezeKnown = false;
	Target.m_Frozen = false;
	Target.m_SoloKnown = false;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));

	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 125;
	Target.m_CurrentCharacterKnown = true;
	Target.m_PositionKnown = true;
	Target.m_VelocityKnown = true;
	Target.m_AliveKnown = true;
	Target.m_Alive = true;
	Target.m_FreezeKnown = true;
	Target.m_Frozen = true;
	Target.m_SoloKnown = true;
	Target.m_X = 16;
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 130;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	EXPECT_FALSE(Replay.SubmitGameplay({Saver, Target, Other}));
	Saver.m_ServerTick = Target.m_ServerTick = Other.m_ServerTick = 165;
	ASSERT_TRUE(Replay.SubmitGameplay({Saver, Target, Other}));
	const auto SaveEvent = std::find_if(Replay.Run().Events().rbegin(), Replay.Run().Events().rend(), [](const SImpactEvent &Event) {
		return Event.m_Type == EImpactEventType::SAVE_CONFIRMED;
	});
	ASSERT_NE(SaveEvent, Replay.Run().Events().rend());
	EXPECT_EQ(SaveEvent->m_Actor, TestId(1));
}

TEST(ImpactPhase2, RefreezeRearmPreservesLaterDecisiveSaverAfterReleasedSetupActor)
{
	CReplay Replay(92, TestContext());
	StartPair(Replay);
	STickEventBatch StartThird;
	StartThird.m_ServerTick = 20;
	StartThird.m_vCandidates.push_back(StartEvent(3, TestId(3), 20, "Target"));
	ASSERT_TRUE(Replay.Submit(StartThird));

	SImpactGameplayFrame SetupActor = ImpactFrame(1, 100);
	SImpactGameplayFrame Saver = ImpactFrame(2, 100);
	SImpactGameplayFrame Target = ImpactFrame(3, 100, true);
	SetPhysicalHarmfulOutcome(Target);
	SetHooked(SetupActor, Target.m_Participant.m_ClientId, true);
	ASSERT_FALSE(Replay.SubmitGameplay({SetupActor, Saver, Target}));

	// Establish the dangerous episode and let the setup actor move the frozen
	// target. This creates the first restoration/Save attempt for that actor.
	SetupActor.m_ServerTick = Saver.m_ServerTick = Target.m_ServerTick = 101;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	EXPECT_TRUE(Replay.SubmitGameplay({SetupActor, Saver, Target}));
	SetupActor.m_ServerTick = Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_X = 16;
	EXPECT_FALSE(Replay.SubmitGameplay({SetupActor, Saver, Target}));
	SetupActor.m_ServerTick = Saver.m_ServerTick = Target.m_ServerTick = 120;
	Target.m_X = 32;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	ClearPhysicalHarmfulOutcome(Target);
	EXPECT_FALSE(Replay.SubmitGameplay({SetupActor, Saver, Target}));
	const auto FirstAttempt = std::find_if(Replay.Run().PendingEpisodes().begin(), Replay.Run().PendingEpisodes().end(), [](const SImpactPendingEpisode &Episode) {
		return Episode.m_Type == EImpactEventType::SAVE_CONFIRMED && Episode.m_Target == TestId(3);
	});
	ASSERT_NE(FirstAttempt, Replay.Run().PendingEpisodes().end());
	EXPECT_EQ(FirstAttempt->m_Actor, TestId(1));

	// The first confirmation fails on refreeze while the setup actor is still
	// active. The actor then releases before the later saver creates a separate
	// positive safe-transition proof; the released setup history must not shadow it.
	SetupActor.m_ServerTick = Saver.m_ServerTick = Target.m_ServerTick = 125;
	Target.m_Frozen = true;
	Target.m_FreezeStart = 125;
	Target.m_FreezeEnd = 225;
	EXPECT_FALSE(Replay.SubmitGameplay({SetupActor, Saver, Target}));
	EXPECT_EQ(std::count_if(Replay.Run().PendingEpisodes().begin(), Replay.Run().PendingEpisodes().end(), [](const SImpactPendingEpisode &Episode) {
		return Episode.m_Type == EImpactEventType::SAVE_CONFIRMED && Episode.m_Target == TestId(3);
	}), 0);
	SetupActor.m_ServerTick = Saver.m_ServerTick = Target.m_ServerTick = 130;
	Target.m_X = 48;
	EXPECT_FALSE(Replay.SubmitGameplay({SetupActor, Saver, Target}));
	SetupActor.m_ServerTick = Saver.m_ServerTick = Target.m_ServerTick = 135;
	ReleaseHook(SetupActor);
	Target.m_X = 56;
	EXPECT_FALSE(Replay.SubmitGameplay({SetupActor, Saver, Target}));

	SetupActor.m_ServerTick = Saver.m_ServerTick = Target.m_ServerTick = 140;
	SetHooked(Saver, Target.m_Participant.m_ClientId, true);
	EXPECT_FALSE(Replay.SubmitGameplay({SetupActor, Saver, Target}));
	SetupActor.m_ServerTick = Saver.m_ServerTick = Target.m_ServerTick = 145;
	Target.m_X = 64;
	Target.m_SupportKnown = true;
	Target.m_Supported = true;
	Target.m_SafeCollisionPositionKnown = true;
	Target.m_SafeCollisionPosition = true;
	EXPECT_FALSE(Replay.SubmitGameplay({SetupActor, Saver, Target}));
	SetupActor.m_ServerTick = Saver.m_ServerTick = Target.m_ServerTick = 150;
	ReleaseHook(Saver);
	Target.m_X = 72;
	EXPECT_FALSE(Replay.SubmitGameplay({SetupActor, Saver, Target}));

	SetupActor.m_ServerTick = Saver.m_ServerTick = Target.m_ServerTick = 155;
	Target.m_X = 80;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	EXPECT_FALSE(Replay.SubmitGameplay({SetupActor, Saver, Target}));
	const auto PendingSave = std::find_if(Replay.Run().PendingEpisodes().begin(), Replay.Run().PendingEpisodes().end(), [](const SImpactPendingEpisode &Episode) {
		return Episode.m_Type == EImpactEventType::SAVE_CONFIRMED && Episode.m_Target == TestId(3);
	});
	ASSERT_NE(PendingSave, Replay.Run().PendingEpisodes().end());
	EXPECT_EQ(PendingSave->m_Actor, TestId(2));
	SetupActor.m_ServerTick = Saver.m_ServerTick = Target.m_ServerTick = 190;
	ASSERT_TRUE(Replay.SubmitGameplay({SetupActor, Saver, Target}));

	const auto SaveEvent = std::find_if(Replay.Run().Events().rbegin(), Replay.Run().Events().rend(), [](const SImpactEvent &Event) {
		return Event.m_Type == EImpactEventType::SAVE_CONFIRMED;
	});
	ASSERT_NE(SaveEvent, Replay.Run().Events().rend());
	EXPECT_EQ(SaveEvent->m_Actor, TestId(2));
	EXPECT_EQ(SaveEvent->m_Target, TestId(3));
	EXPECT_EQ(Replay.Run().Participants()[1].m_RawStats.m_Saves, 1);
	EXPECT_EQ(Replay.Run().Participants()[0].m_RawStats.m_Saves, 0);
}

TEST(ImpactPhase2, EarlierCausalRescueIsNotStolenByLaterHarmlessHook)
{
	CReplay Replay(93, TestContext());
	StartPair(Replay);
	STickEventBatch StartThird;
	StartThird.m_ServerTick = 20;
	StartThird.m_vCandidates.push_back(StartEvent(3, TestId(3), 20, "Target"));
	ASSERT_TRUE(Replay.Submit(StartThird));

	SImpactGameplayFrame SaverA = ImpactFrame(1, 100);
	SImpactGameplayFrame HarmlessB = ImpactFrame(3, 100);
	SImpactGameplayFrame Target = ImpactFrame(2, 100, true);
	SetPhysicalHarmfulOutcome(Target);
	SetHooked(SaverA, Target.m_Participant.m_ClientId, true);
	ASSERT_FALSE(Replay.SubmitGameplay({SaverA, HarmlessB, Target}));

	Target.m_ServerTick = SaverA.m_ServerTick = HarmlessB.m_ServerTick = 101;
	Target.m_AutonomousRecoveryKnown = true;
	Target.m_AutonomousRecoveryPossible = false;
	EXPECT_TRUE(Replay.SubmitGameplay({SaverA, HarmlessB, Target}));

	// A changes the frozen target's trajectory. Native no-interaction replay
	// then proves that trajectory safe before B starts a later harmless hook.
	Target.m_ServerTick = SaverA.m_ServerTick = HarmlessB.m_ServerTick = 110;
	Target.m_X = 16;
	EXPECT_FALSE(Replay.SubmitGameplay({SaverA, HarmlessB, Target}));
	Target.m_ServerTick = SaverA.m_ServerTick = HarmlessB.m_ServerTick = 115;
	Target.m_X = 32;
	EXPECT_FALSE(Replay.SubmitGameplay({SaverA, HarmlessB, Target}));
	Target.m_ServerTick = SaverA.m_ServerTick = HarmlessB.m_ServerTick = 120;
	ReleaseHook(SaverA);
	Target.m_PreActionAutonomousSelfSafeTick = 115;
	SetHooked(HarmlessB, Target.m_Participant.m_ClientId, true);
	EXPECT_FALSE(Replay.SubmitGameplay({SaverA, HarmlessB, Target}));
	Target.m_ServerTick = SaverA.m_ServerTick = HarmlessB.m_ServerTick = 125;
	Target.m_X = 40;
	EXPECT_FALSE(Replay.SubmitGameplay({SaverA, HarmlessB, Target}));
	Target.m_ServerTick = SaverA.m_ServerTick = HarmlessB.m_ServerTick = 130;
	ReleaseHook(HarmlessB);
	Target.m_X = 48;
	EXPECT_FALSE(Replay.SubmitGameplay({SaverA, HarmlessB, Target}));
	Target.m_ServerTick = SaverA.m_ServerTick = HarmlessB.m_ServerTick = 135;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 56;
	ClearPhysicalHarmfulOutcome(Target);
	EXPECT_FALSE(Replay.SubmitGameplay({SaverA, HarmlessB, Target}));
	Target.m_ServerTick = SaverA.m_ServerTick = HarmlessB.m_ServerTick = 170;
	ASSERT_TRUE(Replay.SubmitGameplay({SaverA, HarmlessB, Target}));

	const auto SaveEvent = std::find_if(Replay.Run().Events().rbegin(), Replay.Run().Events().rend(), [](const SImpactEvent &Event) {
		return Event.m_Type == EImpactEventType::SAVE_CONFIRMED;
	});
	ASSERT_NE(SaveEvent, Replay.Run().Events().rend());
	EXPECT_EQ(SaveEvent->m_Actor, TestId(1));
	EXPECT_EQ(SaveEvent->m_Target, TestId(2));
	EXPECT_EQ(Replay.Run().Participants()[0].m_RawStats.m_Saves, 1);
	EXPECT_EQ(Replay.Run().Participants()[2].m_RawStats.m_Saves, 0);
}

TEST(ImpactFoundation, StartedAndEligibleCountsAreDistinct)
{
	CReplay Replay(53, TestContext());
	StartPair(Replay);
	EXPECT_EQ(Replay.Run().StartedParticipantCount(), 2);
	EXPECT_EQ(Replay.Run().EligibleParticipantCount(), 2);
	STickEventBatch Batch;
	Batch.m_ServerTick = 100;
	Batch.m_vCandidates.push_back(PresenceEvent(3, TestId(2), false, 100));
	EXPECT_TRUE(Replay.Submit(Batch));
	EXPECT_EQ(Replay.Run().StartedParticipantCount(), 2);
	EXPECT_EQ(Replay.Run().EligibleParticipantCount(), 1);
}

TEST(ImpactPhase2, NonAmfParticipantCanBeSaved)
{
	CReplay Replay(54, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Local = ImpactFrame(1, 100);
	SetHooked(Local, 2, true);
	SImpactGameplayFrame OrdinaryDdnet = ImpactFrame(2, 100, true);
	SetPhysicalHarmfulOutcome(OrdinaryDdnet);
	Replay.SubmitGameplay({Local, OrdinaryDdnet});
	Local.m_ServerTick = OrdinaryDdnet.m_ServerTick = 105;
	Replay.SubmitGameplay({Local, OrdinaryDdnet});
	Local.m_ServerTick = OrdinaryDdnet.m_ServerTick = 110;
	OrdinaryDdnet.m_Frozen = false;
	OrdinaryDdnet.m_FreezeEnd = 0;
	OrdinaryDdnet.m_X = 32;
	ClearPhysicalHarmfulOutcome(OrdinaryDdnet);
	Replay.SubmitGameplay({Local, OrdinaryDdnet});
	Local.m_ServerTick = OrdinaryDdnet.m_ServerTick = 175;
	EXPECT_TRUE(Replay.SubmitGameplay({Local, OrdinaryDdnet}));
	EXPECT_EQ(Replay.Run().RawStats().m_Saves, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::SAVE_CONFIRMED), 1);
}

TEST(ImpactFoundation, PersonalDisplayGateUsesActiveTime)
{
	CReplay Replay(63, TestContext());
	StartPair(Replay);
	const int64_t GateTicks = static_cast<int64_t>(SHORT_RUN_SECONDS) * Replay.Run().Context().m_TickSpeed;
	EXPECT_FALSE(Replay.Run().HasPassedDisplayGate(TestId(1), 10 + GateTicks - 1));
	EXPECT_TRUE(Replay.Run().HasPassedDisplayGate(TestId(1), 10 + GateTicks));
}

TEST(ImpactFoundation, PersonalDisplayGateStopsDuringPauseAndLateParticipantStartsAtEligibility)
{
	CReplay Replay(64, TestContext());
	StartPair(Replay);
	const int64_t GateTicks = static_cast<int64_t>(SHORT_RUN_SECONDS) * Replay.Run().Context().m_TickSpeed;

	ASSERT_TRUE(FinalizeReplayParticipant(Replay, 3, TestId(1), 100));
	const int64_t ActiveBeforePause = Replay.Run().ActiveAnalysisTicks(100);
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::PAUSED_NO_TEAM);
	EXPECT_FALSE(Replay.Run().HasPassedDisplayGate(TestId(2), 100 + GateTicks));

	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(3), "Late participant", 200, "authoritative current team roster"));
	EXPECT_EQ(Replay.Run().State(), EAnalysisState::ACTIVE);
	EXPECT_FALSE(Replay.Run().HasPassedDisplayGate(TestId(2), 200 + GateTicks - ActiveBeforePause - 1));
	EXPECT_TRUE(Replay.Run().HasPassedDisplayGate(TestId(2), 200 + GateTicks - ActiveBeforePause));
	ASSERT_EQ(Replay.Run().Participants().size(), 3U);
	const int64_t LateBaseline = Replay.Run().Participants()[2].m_PersonalAnalysisStartActiveTicks;
	EXPECT_EQ(LateBaseline, Replay.Run().ActiveAnalysisTicks(200));
	EXPECT_FALSE(Replay.Run().HasPassedDisplayGate(TestId(3), 200 + GateTicks - 1));
	EXPECT_TRUE(Replay.Run().HasPassedDisplayGate(TestId(3), 200 + GateTicks));
}

TEST(ImpactPhase2, MainSavesDummy)
{
	CReplay Replay(55, TestContext());
	const SParticipantId Main = TestId(1, 1, false);
	const SParticipantId Dummy = TestId(2, 1, true);
	StartPairWithIds(Replay, Main, Dummy);
	SImpactGameplayFrame Saver = ImpactFrame(Main.m_ClientId, 100, false, true, true, 1, Main.m_Dummy);
	SetHooked(Saver, Dummy.m_ClientId, true);
	SImpactGameplayFrame Target = ImpactFrame(Dummy.m_ClientId, 100, true, true, true, 1, Dummy.m_Dummy);
	SetPhysicalHarmfulOutcome(Target);
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 105;
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	Replay.SubmitGameplay({Saver, Target});
	Saver.m_ServerTick = Target.m_ServerTick = 175;
	EXPECT_TRUE(Replay.SubmitGameplay({Saver, Target}));
	ASSERT_EQ(Replay.Run().Events().back().m_Actor, Main);
	EXPECT_EQ(Replay.Run().Events().back().m_Target, Dummy);
	EXPECT_EQ(Replay.Run().Participants()[0].m_RawStats.m_Saves, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::SAVE_CONFIRMED), 1);
}

TEST(ImpactPhase2, DummySavesMain)
{
	CReplay Replay(56, TestContext());
	const SParticipantId Main = TestId(1, 1, false);
	const SParticipantId Dummy = TestId(2, 1, true);
	StartPairWithIds(Replay, Main, Dummy);
	SImpactGameplayFrame Target = ImpactFrame(Main.m_ClientId, 100, true, true, true, 1, Main.m_Dummy);
	SetPhysicalHarmfulOutcome(Target);
	SImpactGameplayFrame Saver = ImpactFrame(Dummy.m_ClientId, 100, false, true, true, 1, Dummy.m_Dummy);
	SetHooked(Saver, Main.m_ClientId, true);
	Replay.SubmitGameplay({Target, Saver});
	Saver.m_ServerTick = Target.m_ServerTick = 105;
	Replay.SubmitGameplay({Target, Saver});
	Saver.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	Replay.SubmitGameplay({Target, Saver});
	Saver.m_ServerTick = Target.m_ServerTick = 175;
	EXPECT_TRUE(Replay.SubmitGameplay({Target, Saver}));
	ASSERT_EQ(Replay.Run().Events().back().m_Actor, Dummy);
	EXPECT_EQ(Replay.Run().Events().back().m_Target, Main);
	EXPECT_EQ(Replay.Run().Participants()[1].m_RawStats.m_Saves, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::SAVE_CONFIRMED), 1);
}

TEST(ImpactPhase2, RemoteNonAmfParticipantSavesLocal)
{
	CReplay Replay(57, TestContext());
	StartPair(Replay);
	SImpactGameplayFrame Target = ImpactFrame(1, 100, true);
	SetPhysicalHarmfulOutcome(Target);
	SImpactGameplayFrame Remote = ImpactFrame(2, 100);
	SetHooked(Remote, Target.m_Participant.m_ClientId, true);
	Replay.SubmitGameplay({Target, Remote});
	Remote.m_ServerTick = Target.m_ServerTick = 105;
	Replay.SubmitGameplay({Target, Remote});
	Remote.m_ServerTick = Target.m_ServerTick = 110;
	Target.m_Frozen = false;
	Target.m_FreezeEnd = 0;
	Target.m_X = 32;
	ClearPhysicalHarmfulOutcome(Target);
	Replay.SubmitGameplay({Target, Remote});
	Remote.m_ServerTick = Target.m_ServerTick = 175;
	EXPECT_TRUE(Replay.SubmitGameplay({Target, Remote}));
	ASSERT_EQ(Replay.Run().Events().back().m_Actor, TestId(2));
	EXPECT_EQ(Replay.Run().Events().back().m_Target, TestId(1));
	EXPECT_EQ(Replay.Run().Participants()[1].m_RawStats.m_Saves, 1);
	EXPECT_EQ(CountEvents(Replay.Run(), EImpactEventType::SAVE_CONFIRMED), 1);
}

struct SImpactV1TestInput
{
	int64_t m_Saves = 0;
	int64_t m_IndependentSaves = 0;
	int64_t m_RepeatedSaves = 0;
	int64_t m_UnknownPatternSaves = 0;
	int64_t m_Falls = 0;
	int64_t m_RescuesReceived = 0;
	bool m_RescuesReceivedKnown = true;
	int64_t m_Aleds = 0;
	int64_t m_Clutches = 0;
	int64_t m_ParticipationTicks = 30000;
	bool m_ParticipationKnown = true;
	size_t m_EngagedCohortSize = 4;
};

static SPlayerScoringFeatures MakeImpactV1Feature(int ClientId, const SImpactV1TestInput &Input)
{
	SPlayerScoringFeatures Player;
	Player.m_Id = TestId(ClientId);
	Player.m_Name = "Impact V1 calibration player";
	Player.m_TerminalStatus = EParticipantTerminalStatus::FINISHED;
	Player.m_Engaged = true;
	Player.m_Finalized = true;
	Player.m_InFinalizedScoringCohort = true;
	Player.m_ParticipationTicks = Input.m_ParticipationTicks;
	Player.m_ParticipationKnown = Input.m_ParticipationKnown;
	Player.m_Saves = Input.m_Saves;
	Player.m_Falls = Input.m_Falls;
	Player.m_Aleds = Input.m_Aleds;
	Player.m_Clutches = Input.m_Clutches;
	Player.m_RescuesReceived = Input.m_RescuesReceived;
	Player.m_RescuesReceivedKnown = Input.m_RescuesReceivedKnown;
	Player.m_IndependentSaveCount = Input.m_IndependentSaves;
	Player.m_RepeatedContextSaveCount = Input.m_RepeatedSaves;
	Player.m_UnknownSavePatternCount = Input.m_UnknownPatternSaves;
	Player.m_SavePatternEvidenceCoverageComplete = true;
	Player.m_SavePatternClassificationComplete = Input.m_UnknownPatternSaves == 0;
	return Player;
}

static CRunScoringContext MakeImpactV1Context(std::vector<SPlayerScoringFeatures> Players,
	EPhase4EvidenceStatus EvidenceStatus = EPhase4EvidenceStatus::COMPLETE, uint64_t DroppedEvents = 0)
{
	CRunScoringContext Context;
	Context.m_Available = true;
	Context.m_RunId = 60001;
	Context.m_TeamId = 7;
	Context.m_TickSpeed = 50;
	Context.m_RosterParticipantCount = Players.size();
	Context.m_ScoringEvidenceVersion = EvidenceStatus == EPhase4EvidenceStatus::UNAVAILABLE ? 0 : IMPACT_SCORING_EVIDENCE_VERSION;
	Context.m_ScoringEvidenceStatus = EvidenceStatus;
	Context.m_ScoringEvidenceDroppedEvents = DroppedEvents;
	for(const SPlayerScoringFeatures &Player : Players)
	{
		if(!Player.m_Engaged)
		{
			++Context.m_NotEngagedRosterParticipantCount;
			continue;
		}
		if(!Player.m_Finalized || !Player.m_InFinalizedScoringCohort)
			continue;
		++Context.m_FinalizedEngagedParticipantCount;
		Context.m_TotalSaves += Player.m_Saves;
		Context.m_TotalFalls += Player.m_Falls;
		Context.m_TotalAleds += Player.m_Aleds;
		Context.m_TotalClutches += Player.m_Clutches;
	}
	Context.m_vPlayers = std::move(Players);
	return Context;
}

static CRunScoringContext MakeImpactV1PlayerContext(const SPlayerScoringFeatures &Player, size_t EngagedCohortSize,
	size_t NotEngagedRosterCount = 0, EPhase4EvidenceStatus EvidenceStatus = EPhase4EvidenceStatus::COMPLETE)
{
	std::vector<SPlayerScoringFeatures> vPlayers{Player};
	size_t CurrentCohortSize = Player.m_Engaged && Player.m_Finalized && Player.m_InFinalizedScoringCohort ? 1 : 0;
	int DummyClientId = 10000;
	while(CurrentCohortSize < EngagedCohortSize)
	{
		SImpactV1TestInput NoEvents;
		vPlayers.push_back(MakeImpactV1Feature(DummyClientId++, NoEvents));
		++CurrentCohortSize;
	}
	while(NotEngagedRosterCount > 0)
	{
		SImpactV1TestInput NoEvents;
		SPlayerScoringFeatures NotEngaged = MakeImpactV1Feature(DummyClientId++, NoEvents);
		NotEngaged.m_Engaged = false;
		NotEngaged.m_Finalized = false;
		NotEngaged.m_InFinalizedScoringCohort = false;
		vPlayers.push_back(std::move(NotEngaged));
		--NotEngagedRosterCount;
	}
	return MakeImpactV1Context(std::move(vPlayers), EvidenceStatus);
}

static bool ScoreImpactV1(const CRunScoringContext &Context, const SParticipantId &Id, SImpactResult *pResult,
	const SStabilityResult *pOptionalStabilityDiagnostic = nullptr)
{
	const auto It = std::find_if(Context.m_vPlayers.begin(), Context.m_vPlayers.end(), [&Id](const SPlayerScoringFeatures &Player) {
		return Player.m_Id == Id;
	});
	return It != Context.m_vPlayers.end() && CalculateImpactV1(*It, Context, pResult, pOptionalStabilityDiagnostic);
}

static SImpactResult GetImpactV1Result(const SImpactV1TestInput &Input, int ClientId = 1,
	EPhase4EvidenceStatus EvidenceStatus = EPhase4EvidenceStatus::COMPLETE)
{
	const SPlayerScoringFeatures Player = MakeImpactV1Feature(ClientId, Input);
	const CRunScoringContext Context = MakeImpactV1PlayerContext(Player, Input.m_EngagedCohortSize, 0, EvidenceStatus);
	SImpactResult Result;
	EXPECT_TRUE(ScoreImpactV1(Context, Player.m_Id, &Result));
	return Result;
}

static SImpactResult GetImpactV1ResultWithCohort(const SImpactV1TestInput &Input, size_t EngagedCohortSize,
	size_t NotEngagedRosterCount = 0, int ClientId = 1)
{
	const SPlayerScoringFeatures Player = MakeImpactV1Feature(ClientId, Input);
	const CRunScoringContext Context = MakeImpactV1PlayerContext(Player, EngagedCohortSize, NotEngagedRosterCount);
	SImpactResult Result;
	EXPECT_TRUE(ScoreImpactV1(Context, Player.m_Id, &Result));
	return Result;
}

static SRatingResult GetRatingV1Result(double Impact, double Stability, int ClientId = 1, size_t CohortSize = 1)
{
	SFinalizedScoringDimensions Dimensions;
	Dimensions.m_ImpactAvailable = true;
	Dimensions.m_StabilityAvailable = true;
	Dimensions.m_ImpactId = TestId(ClientId);
	Dimensions.m_StabilityId = TestId(ClientId);
	Dimensions.m_FinalizedEngagedScoringCohortSize = CohortSize;
	Dimensions.m_FinalImpact = Impact;
	Dimensions.m_FinalStability = Stability;
	SRatingResult Result;
	EXPECT_TRUE(CalculateRatingV1(Dimensions, &Result));
	return Result;
}

static SRatingResult GetRatingV1ResultForGameplay(const SImpactV1TestInput &Input, int ClientId = 1)
{
	const SPlayerScoringFeatures Player = MakeImpactV1Feature(ClientId, Input);
	const CRunScoringContext Context = MakeImpactV1PlayerContext(Player, Input.m_EngagedCohortSize);
	SImpactResult Impact;
	SStabilityResult Stability;
	EXPECT_TRUE(CalculateImpactV1(Player, Context, &Impact));
	EXPECT_TRUE(CalculateStabilityV1(Player, Context, &Stability));
	const SFinalizedScoringDimensions Dimensions = MakeFinalizedScoringDimensions(Impact, Stability);
	SRatingResult Result;
	EXPECT_TRUE(CalculateRatingV1(Dimensions, &Result));
	return Result;
}

static void MakeSyntheticUpstreamResults(double FinalImpact, double FinalStability, SImpactResult *pImpact, SStabilityResult *pStability)
{
	*pImpact = {};
	pImpact->m_Available = true;
	pImpact->m_Id = TestId(1);
	pImpact->m_FinalizedEngagedScoringCohortSize = 4;
	pImpact->m_FinalImpact = FinalImpact;
	*pStability = {};
	pStability->m_Available = true;
	pStability->m_Id = TestId(1);
	pStability->m_Value = FinalStability;
}

static double ScoreRatingFromUpstreamResults(const SImpactResult &Impact, const SStabilityResult &Stability)
{
	const SFinalizedScoringDimensions Dimensions = MakeFinalizedScoringDimensions(Impact, Stability);
	SRatingResult Result;
	EXPECT_TRUE(CalculateRatingV1(Dimensions, &Result));
	return Result.m_FinalRating;
}

TEST(ImpactV1, ZeroEvidenceProducesZeroImpact)
{
	const SImpactResult Result = GetImpactV1Result({});
	ASSERT_TRUE(Result.m_Available);
	EXPECT_DOUBLE_EQ(Result.m_NetEvidence, 0.0);
	EXPECT_DOUBLE_EQ(Result.m_RawImpact, 0.0);
	EXPECT_DOUBLE_EQ(Result.m_FinalImpact, 0.0);
}

TEST(ImpactV1, IndependentSaveRaisesImpact)
{
	const SImpactResult None = GetImpactV1Result({});
	SImpactV1TestInput Input;
	Input.m_Saves = Input.m_IndependentSaves = 1;
	const SImpactResult OneSave = GetImpactV1Result(Input);
	EXPECT_GT(OneSave.m_FinalImpact, None.m_FinalImpact);
}

TEST(ImpactV1, RepeatedSaveRaisesLessThanIndependent)
{
	SImpactV1TestInput Independent;
	Independent.m_Saves = Independent.m_IndependentSaves = 1;
	SImpactV1TestInput Repeated;
	Repeated.m_Saves = Repeated.m_RepeatedSaves = 1;
	const SImpactResult IndependentResult = GetImpactV1Result(Independent);
	const SImpactResult RepeatedResult = GetImpactV1Result(Repeated);
	const SImpactResult Neutral = GetImpactV1Result({});
	EXPECT_GT(RepeatedResult.m_FinalImpact, Neutral.m_FinalImpact);
	EXPECT_LT(RepeatedResult.m_FinalImpact - Neutral.m_FinalImpact,
		IndependentResult.m_FinalImpact - Neutral.m_FinalImpact);
}

TEST(ImpactV1, FallReducesImpact)
{
	SImpactV1TestInput OneFall;
	OneFall.m_Falls = 1;
	SImpactV1TestInput TwoFalls = OneFall;
	TwoFalls.m_Falls = 2;
	EXPECT_GT(GetImpactV1Result(OneFall).m_FinalImpact, GetImpactV1Result(TwoFalls).m_FinalImpact);
}

TEST(ImpactV1, RescuesReceivedDoesNotAffectImpact)
{
	SImpactV1TestInput Baseline;
	Baseline.m_Saves = Baseline.m_IndependentSaves = 6;
	Baseline.m_Falls = 3;
	Baseline.m_Clutches = 2;
	Baseline.m_Aleds = 1;
	Baseline.m_RescuesReceivedKnown = true;
	const SImpactResult Zero = GetImpactV1Result(Baseline);
	SImpactV1TestInput One = Baseline;
	One.m_RescuesReceived = 1;
	SImpactV1TestInput Five = Baseline;
	Five.m_RescuesReceived = 5;
	SImpactV1TestInput Twenty = Baseline;
	Twenty.m_RescuesReceived = 20;
	SImpactV1TestInput Hundred = Baseline;
	Hundred.m_RescuesReceived = 100;
	SImpactV1TestInput Unknown = Baseline;
	Unknown.m_RescuesReceived = 999;
	Unknown.m_RescuesReceivedKnown = false;
	const SImpactResult OneResult = GetImpactV1Result(One);
	const SImpactResult FiveResult = GetImpactV1Result(Five);
	const SImpactResult TwentyResult = GetImpactV1Result(Twenty);
	const SImpactResult HundredResult = GetImpactV1Result(Hundred);
	const SImpactResult UnknownResult = GetImpactV1Result(Unknown);
	EXPECT_DOUBLE_EQ(Zero.m_NegativeEvidence, 2.7);
	EXPECT_DOUBLE_EQ(Zero.m_NetEvidence, OneResult.m_NetEvidence);
	EXPECT_DOUBLE_EQ(Zero.m_NetEvidence, FiveResult.m_NetEvidence);
	EXPECT_DOUBLE_EQ(Zero.m_NetEvidence, TwentyResult.m_NetEvidence);
	EXPECT_DOUBLE_EQ(Zero.m_NetEvidence, HundredResult.m_NetEvidence);
	EXPECT_DOUBLE_EQ(Zero.m_NetEvidence, UnknownResult.m_NetEvidence);
	EXPECT_DOUBLE_EQ(Zero.m_RawImpact, OneResult.m_RawImpact);
	EXPECT_DOUBLE_EQ(Zero.m_RawImpact, FiveResult.m_RawImpact);
	EXPECT_DOUBLE_EQ(Zero.m_RawImpact, TwentyResult.m_RawImpact);
	EXPECT_DOUBLE_EQ(Zero.m_RawImpact, HundredResult.m_RawImpact);
	EXPECT_DOUBLE_EQ(Zero.m_RawImpact, UnknownResult.m_RawImpact);
	EXPECT_DOUBLE_EQ(Zero.m_FinalImpact, OneResult.m_FinalImpact);
	EXPECT_DOUBLE_EQ(Zero.m_FinalImpact, FiveResult.m_FinalImpact);
	EXPECT_DOUBLE_EQ(Zero.m_FinalImpact, TwentyResult.m_FinalImpact);
	EXPECT_DOUBLE_EQ(Zero.m_FinalImpact, HundredResult.m_FinalImpact);
	EXPECT_DOUBLE_EQ(Zero.m_FinalImpact, UnknownResult.m_FinalImpact);
}

TEST(ImpactV1, ClutchRaisesImpact)
{
	SImpactV1TestInput WithoutClutch;
	WithoutClutch.m_Saves = WithoutClutch.m_IndependentSaves = 1;
	SImpactV1TestInput WithClutch = WithoutClutch;
	WithClutch.m_Clutches = 1;
	EXPECT_GT(GetImpactV1Result(WithClutch).m_FinalImpact, GetImpactV1Result(WithoutClutch).m_FinalImpact);
}

TEST(ImpactV1, AledRaisesImpact)
{
	SImpactV1TestInput WithoutAled;
	WithoutAled.m_Saves = WithoutAled.m_IndependentSaves = 1;
	SImpactV1TestInput WithAled = WithoutAled;
	WithAled.m_Aleds = 1;
	EXPECT_GT(GetImpactV1Result(WithAled).m_FinalImpact, GetImpactV1Result(WithoutAled).m_FinalImpact);
}

TEST(ImpactV1, ClutchContributionExceedsAledContribution)
{
	SImpactV1TestInput Baseline;
	Baseline.m_Saves = Baseline.m_IndependentSaves = 1;
	SImpactV1TestInput WithClutch = Baseline;
	WithClutch.m_Clutches = 1;
	SImpactV1TestInput WithAled = Baseline;
	WithAled.m_Aleds = 1;
	const double ClutchDelta = GetImpactV1Result(WithClutch).m_FinalImpact - GetImpactV1Result(Baseline).m_FinalImpact;
	const double AledDelta = GetImpactV1Result(WithAled).m_FinalImpact - GetImpactV1Result(Baseline).m_FinalImpact;
	EXPECT_GT(ClutchDelta, AledDelta);
}

TEST(ImpactV1, UnknownRescueReceivedNeutral)
{
	SImpactV1TestInput Unknown;
	Unknown.m_Saves = Unknown.m_IndependentSaves = 3;
	Unknown.m_Falls = 1;
	Unknown.m_RescuesReceived = 99;
	Unknown.m_RescuesReceivedKnown = false;
	SImpactV1TestInput KnownZero = Unknown;
	KnownZero.m_RescuesReceived = 0;
	KnownZero.m_RescuesReceivedKnown = true;
	const SImpactResult UnknownResult = GetImpactV1Result(Unknown);
	const SImpactResult KnownZeroResult = GetImpactV1Result(KnownZero);
	EXPECT_FALSE(UnknownResult.m_RescuesReceivedKnown);
	EXPECT_DOUBLE_EQ(UnknownResult.m_NegativeEvidence, KnownZeroResult.m_NegativeEvidence);
	EXPECT_DOUBLE_EQ(UnknownResult.m_FinalImpact, KnownZeroResult.m_FinalImpact);
}

TEST(ImpactV1, AntiFarmDoesNotChangeCanonicalSaveCount)
{
	SImpactV1TestInput Input;
	Input.m_Saves = 5;
	Input.m_IndependentSaves = 1;
	Input.m_RepeatedSaves = 2;
	Input.m_UnknownPatternSaves = 2;
	const SImpactResult Result = GetImpactV1Result(Input);
	EXPECT_EQ(Result.m_RawCanonicalSaves, 5);
	EXPECT_EQ(Result.m_IndependentSaves + Result.m_RepeatedSaves + Result.m_UnknownPatternSaves, 5);
	EXPECT_DOUBLE_EQ(Result.m_EffectiveSaveEvidence, 3.8);
}

TEST(ImpactV1, UnknownSavePatternGetsFullScoringWeight)
{
	SImpactV1TestInput Independent;
	Independent.m_Saves = Independent.m_IndependentSaves = 1;
	SImpactV1TestInput Unknown = Independent;
	Unknown.m_IndependentSaves = 0;
	Unknown.m_UnknownPatternSaves = 1;
	const SImpactResult IndependentResult = GetImpactV1Result(Independent);
	const SImpactResult UnknownResult = GetImpactV1Result(Unknown);
	EXPECT_EQ(UnknownResult.m_RawCanonicalSaves, 1);
	EXPECT_EQ(UnknownResult.m_UnknownPatternSaves, 1);
	EXPECT_DOUBLE_EQ(UnknownResult.m_EffectiveSaveEvidence, IndependentResult.m_EffectiveSaveEvidence);
	EXPECT_DOUBLE_EQ(UnknownResult.m_FinalImpact, IndependentResult.m_FinalImpact);
}

TEST(ImpactV1, RepeatedLoopSuppressesOnlyScoringEvidence)
{
	SImpactV1TestInput Independent;
	Independent.m_Saves = Independent.m_IndependentSaves = 10;
	SImpactV1TestInput Repeated = Independent;
	Repeated.m_IndependentSaves = 3;
	Repeated.m_RepeatedSaves = 7;
	const SImpactResult IndependentResult = GetImpactV1Result(Independent);
	const SImpactResult RepeatedResult = GetImpactV1Result(Repeated);
	EXPECT_EQ(IndependentResult.m_RawCanonicalSaves, RepeatedResult.m_RawCanonicalSaves);
	EXPECT_EQ(RepeatedResult.m_RawCanonicalSaves, 10);
	EXPECT_LT(RepeatedResult.m_EffectiveSaveEvidence, IndependentResult.m_EffectiveSaveEvidence);
	EXPECT_LT(RepeatedResult.m_FinalImpact, IndependentResult.m_FinalImpact);
}

TEST(ImpactV1, ShortParticipationShrinksMagnitude)
{
	SImpactV1TestInput Long;
	Long.m_Saves = Long.m_IndependentSaves = 10;
	Long.m_Falls = 2;
	SImpactV1TestInput Short = Long;
	Short.m_ParticipationTicks = 2250;
	const SImpactResult LongResult = GetImpactV1Result(Long);
	const SImpactResult ShortResult = GetImpactV1Result(Short);
	EXPECT_DOUBLE_EQ(LongResult.m_RawImpact, ShortResult.m_RawImpact);
	EXPECT_LT(std::abs(ShortResult.m_FinalImpact), std::abs(LongResult.m_FinalImpact));
	EXPECT_EQ(std::signbit(ShortResult.m_FinalImpact), std::signbit(LongResult.m_FinalImpact));
}

TEST(ImpactV1, IncompleteEvidenceShrinksMagnitude)
{
	SImpactV1TestInput Input;
	Input.m_Saves = Input.m_IndependentSaves = 8;
	Input.m_Falls = 2;
	const SImpactResult Complete = GetImpactV1Result(Input, 1, EPhase4EvidenceStatus::COMPLETE);
	const SImpactResult Incomplete = GetImpactV1Result(Input, 1, EPhase4EvidenceStatus::INCOMPLETE);
	EXPECT_DOUBLE_EQ(Incomplete.m_EvidenceConfidence, SImpactV1Constants::m_IncompleteEvidenceConfidence);
	EXPECT_LT(std::abs(Incomplete.m_FinalImpact), std::abs(Complete.m_FinalImpact));
	EXPECT_EQ(std::signbit(Incomplete.m_FinalImpact), std::signbit(Complete.m_FinalImpact));
}

TEST(ImpactV1, ConfidenceNeverFlipsSign)
{
	SImpactV1TestInput Positive;
	Positive.m_Saves = Positive.m_IndependentSaves = 3;
	Positive.m_ParticipationTicks = 1;
	SImpactV1TestInput Negative;
	Negative.m_Falls = 3;
	Negative.m_ParticipationTicks = 1;
	const SImpactResult PositiveResult = GetImpactV1Result(Positive, 1, EPhase4EvidenceStatus::INCOMPLETE);
	const SImpactResult NegativeResult = GetImpactV1Result(Negative, 2, EPhase4EvidenceStatus::INCOMPLETE);
	EXPECT_GT(PositiveResult.m_FinalImpact, 0.0);
	EXPECT_LT(NegativeResult.m_FinalImpact, 0.0);
}

TEST(ImpactV1, TeamSizeAloneDoesNotChangeImpact)
{
	SImpactV1TestInput Input;
	Input.m_Saves = Input.m_IndependentSaves = 5;
	Input.m_Falls = 1;
	Input.m_Clutches = 2;
	const SImpactResult Team3 = GetImpactV1ResultWithCohort(Input, 3);
	const SImpactResult Team5 = GetImpactV1ResultWithCohort(Input, 5);
	const SImpactResult Team8 = GetImpactV1ResultWithCohort(Input, 8);
	EXPECT_DOUBLE_EQ(Team3.m_FinalImpact, Team5.m_FinalImpact);
	EXPECT_DOUBLE_EQ(Team5.m_FinalImpact, Team8.m_FinalImpact);
	EXPECT_DOUBLE_EQ(Team3.m_ClutchEvidence, Team5.m_ClutchEvidence);
	EXPECT_DOUBLE_EQ(Team5.m_ClutchEvidence, Team8.m_ClutchEvidence);
}

TEST(ImpactV1, AledUnderlyingSaveNotDoubleCounted)
{
	SImpactV1TestInput WithoutAled;
	WithoutAled.m_Saves = WithoutAled.m_IndependentSaves = 5;
	SImpactV1TestInput WithAled = WithoutAled;
	WithAled.m_Aleds = 1;
	const SImpactResult Base = GetImpactV1Result(WithoutAled);
	const SImpactResult Aled = GetImpactV1Result(WithAled);
	EXPECT_DOUBLE_EQ(Base.m_EffectiveSaveEvidence, Aled.m_EffectiveSaveEvidence);
	EXPECT_DOUBLE_EQ(Aled.m_PositiveEvidence - Base.m_PositiveEvidence, Aled.m_AledEvidence);
	EXPECT_NEAR(Aled.m_AledEvidence, SImpactV1Constants::m_FirstAledEvidence, 1e-15);
}

TEST(ImpactV1, ClutchUnderlyingSaveNotDoubleCounted)
{
	SImpactV1TestInput WithoutClutch;
	WithoutClutch.m_Saves = WithoutClutch.m_IndependentSaves = 5;
	SImpactV1TestInput WithClutch = WithoutClutch;
	WithClutch.m_Clutches = 1;
	const SImpactResult Base = GetImpactV1Result(WithoutClutch);
	const SImpactResult Clutch = GetImpactV1Result(WithClutch);
	EXPECT_DOUBLE_EQ(Base.m_EffectiveSaveEvidence, Clutch.m_EffectiveSaveEvidence);
	EXPECT_DOUBLE_EQ(Clutch.m_PositiveEvidence - Base.m_PositiveEvidence, Clutch.m_ClutchEvidence);
	EXPECT_NEAR(Clutch.m_ClutchEvidence, SImpactV1Constants::m_FirstClutchEvidence, 1e-15);
}

TEST(ImpactV1, ClutchContributionIsNonlinearAndDiminishing)
{
	SImpactV1TestInput Input;
	const SImpactResult Empty = GetImpactV1ResultWithCohort(Input, 3);
	SImpactResult Previous = Empty;
	std::array<double, 22> Marginals{};
	for(int64_t Count = 1; Count <= 21; ++Count)
	{
		Input.m_Clutches = Count;
		const SImpactResult Current = GetImpactV1ResultWithCohort(Input, 3);
		Marginals[static_cast<size_t>(Count)] = Current.m_ClutchEvidence - Previous.m_ClutchEvidence;
		EXPECT_GT(Current.m_ClutchEvidence, Previous.m_ClutchEvidence);
		if(Count > 1)
			EXPECT_LT(Marginals[static_cast<size_t>(Count)], Marginals[static_cast<size_t>(Count - 1)]);
		Previous = Current;
	}
	EXPECT_LT(Marginals[10], Marginals[5]);
	EXPECT_GT(Previous.m_ClutchEvidence, 0.0);
	EXPECT_GT(Previous.m_ClutchEvidence, GetImpactV1ResultWithCohort(SImpactV1TestInput{}, 3).m_ClutchEvidence);
}

TEST(ImpactV1, AledContributionIsNonlinearAndDiminishing)
{
	SImpactV1TestInput Input;
	SImpactResult Previous = GetImpactV1ResultWithCohort(Input, 3);
	std::array<double, 22> Marginals{};
	for(int64_t Count = 1; Count <= 21; ++Count)
	{
		Input.m_Aleds = Count;
		const SImpactResult Current = GetImpactV1ResultWithCohort(Input, 3);
		Marginals[static_cast<size_t>(Count)] = Current.m_AledEvidence - Previous.m_AledEvidence;
		EXPECT_GT(Current.m_AledEvidence, Previous.m_AledEvidence);
		if(Count > 1)
			EXPECT_LT(Marginals[static_cast<size_t>(Count)], Marginals[static_cast<size_t>(Count - 1)]);
		Previous = Current;
	}
	EXPECT_LT(Marginals[10], Marginals[5]);
	EXPECT_GT(Previous.m_AledEvidence, 0.0);
}

TEST(ImpactV1, ComparableClutchContributionExceedsAled)
{
	for(const int64_t Count : {1, 3, 10})
	{
		SImpactV1TestInput ClutchInput;
		ClutchInput.m_Clutches = Count;
		SImpactV1TestInput AledInput;
		AledInput.m_Aleds = Count;
		const SImpactResult Clutch = GetImpactV1ResultWithCohort(ClutchInput, 3);
		const SImpactResult Aled = GetImpactV1ResultWithCohort(AledInput, 3);
		EXPECT_GT(Clutch.m_ClutchEvidence, Aled.m_AledEvidence) << "count=" << Count;
	}
}

TEST(ImpactV1, AledCurveUnchanged)
{
	const std::array<std::pair<int64_t, double>, 3> vExpected = {{{1, 0.30}, {3, 0.60}, {10, 1.0378294855911894}}};
	for(const auto &[Count, Expected] : vExpected)
	{
		SImpactV1TestInput Input;
		Input.m_Aleds = Count;
		EXPECT_NEAR(GetImpactV1ResultWithCohort(Input, 3).m_AledEvidence, Expected, 1e-15) << "count=" << Count;
	}
}

TEST(ImpactV1, SidecarContributionCurvesAreReportedAtFullPrecision)
{
	const std::array<int64_t, 8> vCounts = {0, 1, 2, 3, 5, 10, 20, 50};
	std::cout << std::setprecision(std::numeric_limits<double>::max_digits10);
	for(const int64_t Count : vCounts)
	{
		SImpactV1TestInput Input;
		Input.m_Clutches = Count;
		const SImpactResult Clutch = GetImpactV1ResultWithCohort(Input, 3);
		const SImpactResult DuoClutch = GetImpactV1ResultWithCohort(Input, 2);
		Input.m_Aleds = Count;
		const SImpactResult Aled = GetImpactV1ResultWithCohort(Input, 3);
		SImpactV1TestInput PriorClutchInput;
		PriorClutchInput.m_Clutches = std::max<int64_t>(0, Count - 1);
		SImpactV1TestInput PriorAledInput;
		PriorAledInput.m_Aleds = std::max<int64_t>(0, Count - 1);
		const double PriorClutch = GetImpactV1ResultWithCohort(PriorClutchInput, 3).m_ClutchEvidence;
		const double PriorDuoClutch = GetImpactV1ResultWithCohort(PriorClutchInput, 2).m_ClutchEvidence;
		const double PriorAled = GetImpactV1ResultWithCohort(PriorAledInput, 3).m_AledEvidence;
		std::cout << "IMPACT_SIDECAR_CURVE clutch_count=" << Count << " clutch_total=" << Clutch.m_ClutchEvidence
			  << " clutch_marginal=" << Clutch.m_ClutchEvidence - PriorClutch
			  << " duo_clutch_total=" << DuoClutch.m_ClutchEvidence
			  << " duo_clutch_marginal=" << DuoClutch.m_ClutchEvidence - PriorDuoClutch
			  << " aled_total=" << Aled.m_AledEvidence << " aled_marginal=" << Aled.m_AledEvidence - PriorAled << '\n';
	}
}

TEST(ImpactV1, FirstClutchHasSubstantialContributionForThreePlusTeam)
{
	SImpactV1TestInput OneClutch;
	OneClutch.m_Clutches = 1;
	const SImpactResult Result = GetImpactV1ResultWithCohort(OneClutch, 3);
	EXPECT_NEAR(Result.m_ClutchEvidence, 1.25, 1e-15);
}

TEST(ImpactV1, SecondClutchAddsLessThanFirst)
{
	SImpactV1TestInput One;
	One.m_Clutches = 1;
	SImpactV1TestInput Two = One;
	Two.m_Clutches = 2;
	const double FirstMarginal = GetImpactV1ResultWithCohort(One, 3).m_ClutchEvidence;
	const double SecondMarginal = GetImpactV1ResultWithCohort(Two, 3).m_ClutchEvidence - FirstMarginal;
	EXPECT_GT(SecondMarginal, 0.0);
	EXPECT_LT(SecondMarginal, FirstMarginal);
}

TEST(ImpactV1, ThirdClutchAddsLessThanSecond)
{
	const auto Evidence = [](int64_t Count) {
		SImpactV1TestInput Input;
		Input.m_Clutches = Count;
		return GetImpactV1ResultWithCohort(Input, 3).m_ClutchEvidence;
	};
	EXPECT_LT(Evidence(3) - Evidence(2), Evidence(2) - Evidence(1));
}

TEST(ImpactV1, TenthClutchAddsLessThanSecond)
{
	const auto Evidence = [](int64_t Count) {
		SImpactV1TestInput Input;
		Input.m_Clutches = Count;
		return GetImpactV1ResultWithCohort(Input, 3).m_ClutchEvidence;
	};
	EXPECT_LT(Evidence(10) - Evidence(9), Evidence(2) - Evidence(1));
}

TEST(ImpactV1, ClutchContributionHasNoHardCap)
{
	const auto Evidence = [](int64_t Count) {
		SImpactV1TestInput Input;
		Input.m_Clutches = Count;
		return GetImpactV1ResultWithCohort(Input, 3).m_ClutchEvidence;
	};
	EXPECT_GT(Evidence(20), Evidence(10));
	EXPECT_LT(Evidence(20), Evidence(10) * 2.0);
	EXPECT_GT(Evidence(1000), Evidence(100));
}

TEST(ImpactV1, FirstAledAddsSmallContribution)
{
	SImpactV1TestInput OneAled;
	OneAled.m_Aleds = 1;
	const SImpactResult Result = GetImpactV1ResultWithCohort(OneAled, 3);
	EXPECT_NEAR(Result.m_AledEvidence, 0.30, 1e-15);
}

TEST(ImpactV1, SecondAledAddsLessThanFirst)
{
	const auto Evidence = [](int64_t Count) {
		SImpactV1TestInput Input;
		Input.m_Aleds = Count;
		return GetImpactV1ResultWithCohort(Input, 3).m_AledEvidence;
	};
	EXPECT_LT(Evidence(2) - Evidence(1), Evidence(1));
}

TEST(ImpactV1, AledContributionHasNoHardCap)
{
	const auto Evidence = [](int64_t Count) {
		SImpactV1TestInput Input;
		Input.m_Aleds = Count;
		return GetImpactV1ResultWithCohort(Input, 3).m_AledEvidence;
	};
	EXPECT_GT(Evidence(20), Evidence(10));
	EXPECT_LT(Evidence(20), Evidence(10) * 2.0);
	EXPECT_GT(Evidence(1000), Evidence(100));
}

TEST(ImpactV1, DuoClutchCounterStillRecorded)
{
	SImpactV1TestInput Input;
	Input.m_Saves = Input.m_IndependentSaves = 1;
	Input.m_Clutches = 3;
	const SImpactResult Result = GetImpactV1ResultWithCohort(Input, 2);
	EXPECT_EQ(Result.m_FinalizedEngagedScoringCohortSize, 2);
	EXPECT_EQ(Result.m_Clutches, 3);
	EXPECT_DOUBLE_EQ(Result.m_ClutchEvidence, 0.0);
}

TEST(ImpactV1, DuoSingleAndMultipleClutchesAddZeroExtraImpact)
{
	SImpactV1TestInput NoClutch;
	NoClutch.m_Saves = NoClutch.m_IndependentSaves = 1;
	SImpactV1TestInput OneClutch = NoClutch;
	OneClutch.m_Clutches = 1;
	SImpactV1TestInput ManyClutches = NoClutch;
	ManyClutches.m_Clutches = 100;
	const SImpactResult Base = GetImpactV1ResultWithCohort(NoClutch, 2);
	const SImpactResult One = GetImpactV1ResultWithCohort(OneClutch, 2);
	const SImpactResult Many = GetImpactV1ResultWithCohort(ManyClutches, 2);
	EXPECT_DOUBLE_EQ(One.m_ClutchEvidence, 0.0);
	EXPECT_DOUBLE_EQ(Many.m_ClutchEvidence, 0.0);
	EXPECT_DOUBLE_EQ(Base.m_NetEvidence, One.m_NetEvidence);
	EXPECT_DOUBLE_EQ(Base.m_NetEvidence, Many.m_NetEvidence);
	EXPECT_DOUBLE_EQ(Base.m_FinalImpact, One.m_FinalImpact);
	EXPECT_DOUBLE_EQ(Base.m_FinalImpact, Many.m_FinalImpact);
}

TEST(ImpactV1, DuoClutchUnderlyingSaveStillCounts)
{
	SImpactV1TestInput Input;
	Input.m_Saves = Input.m_IndependentSaves = 1;
	Input.m_Clutches = 1;
	const SImpactResult Result = GetImpactV1ResultWithCohort(Input, 2);
	EXPECT_DOUBLE_EQ(Result.m_EffectiveSaveEvidence, 1.0);
	EXPECT_DOUBLE_EQ(Result.m_PositiveEvidence, 1.0);
	EXPECT_GT(Result.m_FinalImpact, 0.0);
}

TEST(ImpactV1, DuoAledStillContributes)
{
	SImpactV1TestInput Input;
	Input.m_Saves = Input.m_IndependentSaves = 1;
	Input.m_Clutches = 1;
	Input.m_Aleds = 1;
	const SImpactResult Result = GetImpactV1ResultWithCohort(Input, 2);
	EXPECT_DOUBLE_EQ(Result.m_ClutchEvidence, 0.0);
	EXPECT_NEAR(Result.m_AledEvidence, 0.30, 1e-15);
	EXPECT_NEAR(Result.m_PositiveEvidence, 1.30, 1e-15);
}

TEST(ImpactV1, NotEngagedRosterMemberDoesNotTurnDuoIntoThreePlayerRun)
{
	SImpactV1TestInput Input;
	Input.m_Clutches = 1;
	const SPlayerScoringFeatures Player = MakeImpactV1Feature(1, Input);
	const CRunScoringContext Context = MakeImpactV1PlayerContext(Player, 2, 1);
	SImpactResult Result;
	ASSERT_TRUE(ScoreImpactV1(Context, Player.m_Id, &Result));
	EXPECT_EQ(Context.m_RosterParticipantCount, 3);
	EXPECT_EQ(Context.m_FinalizedEngagedParticipantCount, 2);
	EXPECT_EQ(Result.m_FinalizedEngagedScoringCohortSize, 2);
	EXPECT_DOUBLE_EQ(Result.m_ClutchEvidence, 0.0);
}

TEST(ImpactV1, DuoAAndDuoBHaveIdenticalImpactWhenOnlyClutchCountDiffers)
{
	SImpactV1TestInput DuoA;
	DuoA.m_Saves = DuoA.m_IndependentSaves = 10;
	DuoA.m_Clutches = 3;
	DuoA.m_Aleds = 1;
	DuoA.m_Falls = 4;
	DuoA.m_RescuesReceived = 4;
	SImpactV1TestInput DuoB = DuoA;
	DuoB.m_Clutches = 0;
	const SImpactResult A = GetImpactV1ResultWithCohort(DuoA, 2);
	const SImpactResult B = GetImpactV1ResultWithCohort(DuoB, 2);
	EXPECT_DOUBLE_EQ(A.m_Clutches, 3);
	EXPECT_DOUBLE_EQ(A.m_ClutchEvidence, 0.0);
	EXPECT_DOUBLE_EQ(B.m_ClutchEvidence, 0.0);
	EXPECT_DOUBLE_EQ(A.m_FinalImpact, B.m_FinalImpact);
}

TEST(ImpactV1, TrioAHasMoreImpactThanTrioBOnlyFromClutchEvidence)
{
	SImpactV1TestInput TrioA;
	TrioA.m_Saves = TrioA.m_IndependentSaves = 10;
	TrioA.m_Clutches = 3;
	TrioA.m_Aleds = 1;
	TrioA.m_Falls = 4;
	TrioA.m_RescuesReceived = 4;
	SImpactV1TestInput TrioB = TrioA;
	TrioB.m_Clutches = 0;
	const SImpactResult A = GetImpactV1ResultWithCohort(TrioA, 3);
	const SImpactResult B = GetImpactV1ResultWithCohort(TrioB, 3);
	EXPECT_DOUBLE_EQ(A.m_EffectiveSaveEvidence, B.m_EffectiveSaveEvidence);
	EXPECT_DOUBLE_EQ(A.m_AledEvidence, B.m_AledEvidence);
	EXPECT_GT(A.m_ClutchEvidence, 0.0);
	EXPECT_GT(A.m_FinalImpact, B.m_FinalImpact);
}

TEST(ImpactV1, DuoClutchDoesNotAffectNetEvidence)
{
	SImpactV1TestInput NoClutch;
	NoClutch.m_Saves = NoClutch.m_IndependentSaves = 2;
	SImpactV1TestInput WithClutch = NoClutch;
	WithClutch.m_Clutches = 10;
	EXPECT_DOUBLE_EQ(GetImpactV1ResultWithCohort(NoClutch, 2).m_NetEvidence,
		GetImpactV1ResultWithCohort(WithClutch, 2).m_NetEvidence);
}

TEST(ImpactV1, DuoClutchDoesNotAffectFinalImpact)
{
	SImpactV1TestInput NoClutch;
	NoClutch.m_Saves = NoClutch.m_IndependentSaves = 2;
	SImpactV1TestInput WithClutch = NoClutch;
	WithClutch.m_Clutches = 10;
	EXPECT_DOUBLE_EQ(GetImpactV1ResultWithCohort(NoClutch, 2).m_FinalImpact,
		GetImpactV1ResultWithCohort(WithClutch, 2).m_FinalImpact);
}

TEST(ImpactV1, ThreeEngagedPlayersEnableClutchEvidence)
{
	SImpactV1TestInput Input;
	Input.m_Clutches = 1;
	const SImpactResult Result = GetImpactV1ResultWithCohort(Input, 3);
	EXPECT_EQ(Result.m_FinalizedEngagedScoringCohortSize, 3);
	EXPECT_NEAR(Result.m_ClutchEvidence, 1.25, 1e-15);
}

TEST(ImpactV1, LargerTeamWithTwoCurrentlyFunctionalStillGetsNonDuoClutchEvidence)
{
	// Functional count at the event tick is intentionally absent from the score
	// context; only the finalized ENGAGED cohort defines duo vs non-duo.
	SImpactV1TestInput Input;
	Input.m_Clutches = 1;
	const SImpactResult Result = GetImpactV1ResultWithCohort(Input, 5);
	EXPECT_EQ(Result.m_FinalizedEngagedScoringCohortSize, 5);
	EXPECT_NEAR(Result.m_ClutchEvidence, 1.25, 1e-15);
}

TEST(ImpactV1, InconsistentCohortSummaryDisablesClutchBonus)
{
	SImpactV1TestInput Input;
	Input.m_Clutches = 1;
	const SPlayerScoringFeatures Player = MakeImpactV1Feature(1, Input);
	CRunScoringContext Context = MakeImpactV1Context({Player});
	Context.m_FinalizedEngagedParticipantCount = 3;
	SImpactResult Result;
	ASSERT_TRUE(ScoreImpactV1(Context, Player.m_Id, &Result));
	EXPECT_EQ(Result.m_FinalizedEngagedScoringCohortSize, 0);
	EXPECT_DOUBLE_EQ(Result.m_ClutchEvidence, 0.0);
}

TEST(ImpactV1, NotEngagedPlayerNotScored)
{
	SImpactV1TestInput Input;
	Input.m_Saves = Input.m_IndependentSaves = 2;
	SPlayerScoringFeatures Player = MakeImpactV1Feature(1, Input);
	Player.m_Engaged = false;
	const CRunScoringContext Context = MakeImpactV1Context({Player});
	SImpactResult Result;
	EXPECT_FALSE(ScoreImpactV1(Context, Player.m_Id, &Result));
	EXPECT_FALSE(Result.m_Available);
}

TEST(ImpactV1, PositiveMonotonicity)
{
	SImpactV1TestInput One;
	One.m_Saves = One.m_IndependentSaves = 1;
	SImpactV1TestInput Two = One;
	Two.m_Saves = Two.m_IndependentSaves = 2;
	EXPECT_GT(GetImpactV1Result(Two).m_FinalImpact, GetImpactV1Result(One).m_FinalImpact);
}

TEST(ImpactV1, NegativeMonotonicity)
{
	SImpactV1TestInput One;
	One.m_Falls = 1;
	SImpactV1TestInput Two = One;
	Two.m_Falls = 2;
	EXPECT_LT(GetImpactV1Result(Two).m_FinalImpact, GetImpactV1Result(One).m_FinalImpact);
}

TEST(ImpactV1, PositiveTransformIsMonotonic)
{
	EXPECT_DOUBLE_EQ(TransformImpactEvidenceV1(0.0, 0.0), 0.0);
	EXPECT_LT(TransformImpactEvidenceV1(1.0, 0.0), TransformImpactEvidenceV1(2.0, 0.0));
	EXPECT_LT(TransformImpactEvidenceV1(2.0, 0.0), TransformImpactEvidenceV1(10.0, 0.0));
}

TEST(ImpactV1, NegativeTransformIsMonotonic)
{
	EXPECT_GT(TransformImpactEvidenceV1(0.0, 1.0), TransformImpactEvidenceV1(0.0, 2.0));
	EXPECT_GT(TransformImpactEvidenceV1(0.0, 2.0), TransformImpactEvidenceV1(0.0, 10.0));
}

TEST(ImpactV1, PositiveTransformHasDiminishingReturns)
{
	const double First = TransformImpactEvidenceV1(2.0, 0.0) - TransformImpactEvidenceV1(1.0, 0.0);
	const double Later = TransformImpactEvidenceV1(10.0, 0.0) - TransformImpactEvidenceV1(9.0, 0.0);
	EXPECT_GT(First, Later);
}

TEST(ImpactV1, NegativeTransformHasDiminishingReturns)
{
	const double FirstMagnitudeIncrease = std::abs(TransformImpactEvidenceV1(0.0, 2.0)) - std::abs(TransformImpactEvidenceV1(0.0, 1.0));
	const double LaterMagnitudeIncrease = std::abs(TransformImpactEvidenceV1(0.0, 10.0)) - std::abs(TransformImpactEvidenceV1(0.0, 9.0));
	EXPECT_GT(FirstMagnitudeIncrease, LaterMagnitudeIncrease);
}

TEST(ImpactV1, PositiveTransformIsNotHardCapped)
{
	EXPECT_GT(TransformImpactEvidenceV1(1e12, 0.0), 10.0);
	EXPECT_GT(TransformImpactEvidenceV1(1e15, 0.0), TransformImpactEvidenceV1(1e12, 0.0));
}

TEST(ImpactV1, NegativeTransformIsNotHardCapped)
{
	EXPECT_LT(TransformImpactEvidenceV1(0.0, 1e12), -10.0);
	EXPECT_LT(TransformImpactEvidenceV1(0.0, 1e15), TransformImpactEvidenceV1(0.0, 1e12));
}

TEST(ImpactV1, ExtremeFiniteEvidenceRemainsFinite)
{
	double HarmSeverity = 0.0;
	const double Result = TransformImpactEvidenceV1(0.0, std::numeric_limits<double>::max(), &HarmSeverity);
	EXPECT_TRUE(std::isfinite(HarmSeverity));
	EXPECT_DOUBLE_EQ(HarmSeverity, std::numeric_limits<double>::max());
	EXPECT_TRUE(std::isfinite(Result));
	EXPECT_LT(Result, 0.0);
}

TEST(ImpactV1, PositiveTransformIsSlightlyMoreGenerousThanPreviousV1)
{
	const double Evidence = 8.0;
	const double PreviousV1 = 4.0 * std::log1p(std::abs(Evidence) / 4.0);
	EXPECT_GT(TransformImpactEvidenceV1(Evidence, 0.0), PreviousV1);
}

TEST(ImpactV1, ZeroSaveTwentyFallsIsExtremeNegative)
{
	SImpactV1TestInput Input;
	Input.m_Falls = 20;
	const SImpactResult Result = GetImpactV1Result(Input);
	EXPECT_DOUBLE_EQ(Result.m_PositiveEvidence, 0.0);
	EXPECT_DOUBLE_EQ(Result.m_NegativeEvidence, 18.0);
	EXPECT_DOUBLE_EQ(Result.m_NetEvidence, -18.0);
	EXPECT_DOUBLE_EQ(Result.m_HarmSeverity, 18.0);
	EXPECT_NEAR(Result.m_FinalImpact, -10.228488553430552, 1e-12);
}

TEST(ImpactV1, OneSaveEightFallsIsStrongNegative)
{
	SImpactV1TestInput Input;
	Input.m_Saves = Input.m_IndependentSaves = 1;
	Input.m_Falls = 8;
	const SImpactResult Result = GetImpactV1Result(Input);
	EXPECT_DOUBLE_EQ(Result.m_PositiveEvidence, 1.0);
	EXPECT_DOUBLE_EQ(Result.m_NegativeEvidence, 7.2);
	EXPECT_DOUBLE_EQ(Result.m_NetEvidence, -6.2);
	EXPECT_NEAR(Result.m_HarmSeverity, 5.338888888888889, 1e-14);
	EXPECT_NEAR(Result.m_FinalImpact, -5.087357528391088, 1e-12);
}

TEST(ImpactV1, JolandLikeIsLessNegativeThanOneSaveEightFalls)
{
	SImpactV1TestInput Joland;
	Joland.m_Saves = Joland.m_IndependentSaves = 13;
	Joland.m_Falls = 25;
	Joland.m_Clutches = 1;
	const SImpactResult Result = GetImpactV1Result(Joland);
	const SImpactResult OneSaveEightFalls = [] {
		SImpactV1TestInput Input;
		Input.m_Saves = Input.m_IndependentSaves = 1;
		Input.m_Falls = 8;
		return GetImpactV1Result(Input);
	}();
	EXPECT_DOUBLE_EQ(Result.m_PositiveEvidence, 14.25);
	EXPECT_DOUBLE_EQ(Result.m_NegativeEvidence, 22.5);
	EXPECT_NEAR(Result.m_HarmSeverity, 3.025, 1e-14);
	EXPECT_NEAR(Result.m_FinalImpact, -3.379085124599513, 1e-12);
	EXPECT_GT(Result.m_FinalImpact, OneSaveEightFalls.m_FinalImpact);
}

TEST(ImpactV1, FallStrictlyWorsensNegativeImpact)
{
	SImpactV1TestInput OneFall;
	OneFall.m_Saves = OneFall.m_IndependentSaves = 1;
	OneFall.m_Falls = 8;
	SImpactV1TestInput TwoFalls = OneFall;
	TwoFalls.m_Falls = 9;
	EXPECT_LT(GetImpactV1Result(TwoFalls).m_FinalImpact, GetImpactV1Result(OneFall).m_FinalImpact);
}

TEST(ImpactV1, SaveStrictlyImprovesNegativeImpact)
{
	SImpactV1TestInput Input;
	Input.m_Falls = 20;
	const double NoSaves = GetImpactV1Result(Input).m_FinalImpact;
	Input.m_Saves = Input.m_IndependentSaves = 5;
	const double FiveSaves = GetImpactV1Result(Input).m_FinalImpact;
	Input.m_Saves = Input.m_IndependentSaves = 10;
	const double TenSaves = GetImpactV1Result(Input).m_FinalImpact;
	Input.m_Saves = Input.m_IndependentSaves = 15;
	const double FifteenSaves = GetImpactV1Result(Input).m_FinalImpact;
	EXPECT_LT(NoSaves, FiveSaves);
	EXPECT_LT(FiveSaves, TenSaves);
	EXPECT_LT(TenSaves, FifteenSaves);
}

TEST(ImpactV1, NegativeBranchConvergesToZeroAtBalance)
{
	EXPECT_DOUBLE_EQ(TransformImpactEvidenceV1(9.0, 9.0), 0.0);
}

TEST(ImpactV1, CrossingBalanceUsesPositiveBranch)
{
	const double Expected = SImpactV1Constants::m_PositiveImpactScale *
		std::log1p(1.0 / SImpactV1Constants::m_PositiveImpactSoftness);
	double HarmSeverity = -1.0;
	EXPECT_DOUBLE_EQ(TransformImpactEvidenceV1(10.0, 9.0, &HarmSeverity), Expected);
	EXPECT_DOUBLE_EQ(HarmSeverity, 0.0);
}

TEST(ImpactV1, SameBadRatioMoreAbsoluteHarmCanBeMoreNegative)
{
	SImpactV1TestInput Smaller;
	Smaller.m_Saves = Smaller.m_IndependentSaves = 1;
	Smaller.m_Falls = 8;
	SImpactV1TestInput Larger;
	Larger.m_Saves = Larger.m_IndependentSaves = 2;
	Larger.m_Falls = 16;
	const SImpactResult SmallerResult = GetImpactV1Result(Smaller);
	const SImpactResult LargerResult = GetImpactV1Result(Larger);
	EXPECT_NEAR(SmallerResult.m_PositiveEvidence / SmallerResult.m_NegativeEvidence,
		LargerResult.m_PositiveEvidence / LargerResult.m_NegativeEvidence, 1e-15);
	EXPECT_GT(LargerResult.m_HarmSeverity, SmallerResult.m_HarmSeverity);
	EXPECT_LT(LargerResult.m_FinalImpact, SmallerResult.m_FinalImpact);
}

TEST(ImpactV1, UnopposedFallsPreserveFullHarmSeverity)
{
	double HarmSeverity = 0.0;
	EXPECT_DOUBLE_EQ(TransformImpactEvidenceV1(0.0, 18.0, &HarmSeverity),
		-SImpactV1Constants::m_NegativeHarmScale * std::log1p(18.0 / SImpactV1Constants::m_NegativeHarmSoftness));
	EXPECT_DOUBLE_EQ(HarmSeverity, 18.0);
}

TEST(ImpactV1, PositiveCompensationReducesHarmSeverity)
{
	double FiveSaveHarm = 0.0;
	double TenSaveHarm = 0.0;
	double FifteenSaveHarm = 0.0;
	TransformImpactEvidenceV1(5.0, 18.0, &FiveSaveHarm);
	TransformImpactEvidenceV1(10.0, 18.0, &TenSaveHarm);
	TransformImpactEvidenceV1(15.0, 18.0, &FifteenSaveHarm);
	EXPECT_GT(FiveSaveHarm, TenSaveHarm);
	EXPECT_GT(TenSaveHarm, FifteenSaveHarm);
	EXPECT_GE(FifteenSaveHarm, 0.0);
}

TEST(ImpactV1, HarmSeverityNeverNegative)
{
	for(const auto &[Positive, Negative] : std::array<std::pair<double, double>, 5>{{
		{0.0, 18.0}, {1.0, 7.2}, {14.25, 22.5}, {16.25, 17.1}, {29.25, 13.5}}})
	{
		double HarmSeverity = -1.0;
		TransformImpactEvidenceV1(Positive, Negative, &HarmSeverity);
		EXPECT_GE(HarmSeverity, 0.0);
	}
}

TEST(ImpactV1, NegativeHarmBranchBalancesCompensationComparedWithPreviousV1)
{
	const double PreviousUnopposed = -3.70 * std::log1p(18.0 / 4.0);
	const double NewUnopposed = TransformImpactEvidenceV1(0.0, 18.0);
	const double PreviousJolandLike = -3.70 * std::log1p(8.25 / 4.0);
	const double NewJolandLike = TransformImpactEvidenceV1(14.25, 22.5);
	std::cout << std::setprecision(std::numeric_limits<double>::max_digits10)
		  << "IMPACT_NEGATIVE_BRANCH_COMPARISON unopposed_18 previous=" << PreviousUnopposed << " new=" << NewUnopposed << '\n'
		  << "IMPACT_NEGATIVE_BRANCH_COMPARISON joland_like previous=" << PreviousJolandLike << " new=" << NewJolandLike << '\n';
	EXPECT_LT(NewUnopposed, PreviousUnopposed);
	EXPECT_GT(NewJolandLike, PreviousJolandLike);
}

TEST(ImpactV1, ZeroNetEvidenceIsExactlyZeroImpact)
{
	SImpactV1TestInput Balanced;
	Balanced.m_Saves = Balanced.m_IndependentSaves = 9;
	Balanced.m_Falls = 10;
	EXPECT_DOUBLE_EQ(GetImpactV1Result(Balanced).m_NetEvidence, 0.0);
	EXPECT_DOUBLE_EQ(GetImpactV1Result(Balanced).m_RawImpact, 0.0);
	EXPECT_DOUBLE_EQ(GetImpactV1Result(Balanced).m_FinalImpact, 0.0);
}

TEST(ImpactV1, DeterministicSameInputSameOutput)
{
	SImpactV1TestInput Input;
	Input.m_Saves = 10;
	Input.m_IndependentSaves = 4;
	Input.m_RepeatedSaves = 3;
	Input.m_UnknownPatternSaves = 3;
	Input.m_Clutches = 2;
	Input.m_Aleds = 1;
	Input.m_Falls = 4;
	Input.m_RescuesReceived = 2;
	const SImpactResult First = GetImpactV1Result(Input);
	const SImpactResult Second = GetImpactV1Result(Input);
	EXPECT_EQ(FormatImpactV1Result(First), FormatImpactV1Result(Second));
}

TEST(ImpactV1, Phase2CountersRemainUntouched)
{
	SImpactV1TestInput Input;
	Input.m_Saves = Input.m_IndependentSaves = 8;
	Input.m_Falls = 4;
	Input.m_Aleds = 2;
	Input.m_Clutches = 3;
	SPlayerScoringFeatures Player = MakeImpactV1Feature(1, Input);
	const SPlayerScoringFeatures Before = Player;
	const CRunScoringContext Context = MakeImpactV1Context({Player});
	SImpactResult Result;
	ASSERT_TRUE(ScoreImpactV1(Context, Player.m_Id, &Result));
	EXPECT_EQ(Player.m_Saves, Before.m_Saves);
	EXPECT_EQ(Player.m_Falls, Before.m_Falls);
	EXPECT_EQ(Player.m_Aleds, Before.m_Aleds);
	EXPECT_EQ(Player.m_Clutches, Before.m_Clutches);
}

TEST(ImpactV1, OptionalStabilityIsDiagnosticOnly)
{
	SImpactV1TestInput Input;
	Input.m_Saves = Input.m_IndependentSaves = 4;
	const SPlayerScoringFeatures Player = MakeImpactV1Feature(1, Input);
	const CRunScoringContext Context = MakeImpactV1Context({Player});
	SStabilityResult LowStability;
	LowStability.m_Available = true;
	LowStability.m_Id = Player.m_Id;
	LowStability.m_Value = 10.0;
	SStabilityResult HighStability = LowStability;
	HighStability.m_Value = 90.0;
	SImpactResult WithLowStability;
	SImpactResult WithHighStability;
	ASSERT_TRUE(ScoreImpactV1(Context, Player.m_Id, &WithLowStability, &LowStability));
	ASSERT_TRUE(ScoreImpactV1(Context, Player.m_Id, &WithHighStability, &HighStability));
	EXPECT_DOUBLE_EQ(WithLowStability.m_FinalImpact, WithHighStability.m_FinalImpact);
	EXPECT_DOUBLE_EQ(WithLowStability.m_StabilityDiagnosticValue, 10.0);
	EXPECT_DOUBLE_EQ(WithHighStability.m_StabilityDiagnosticValue, 90.0);
}

TEST(ImpactV1, CalibrationScenariosProduceFullPrecisionDiagnostics)
{
	const std::vector<std::pair<const char *, SImpactV1TestInput>> vCases = {
		{"A", {10, 10, 0, 0, 4, 4, true, 1, 3, 30000, true}},
		{"B", {4, 4, 0, 0, 1, 1, true, 0, 0, 30000, true}},
		{"C", {1, 1, 0, 0, 8, 8, true, 0, 0, 30000, true}},
		{"D", {0, 0, 0, 0, 0, 0, true, 0, 0, 30000, true}},
		{"E", {45, 45, 0, 0, 5, 5, true, 7, 10, 30000, true}},
		{"F", {20, 20, 0, 0, 2, 2, true, 0, 0, 30000, true}},
		{"G", {20, 20, 0, 0, 20, 20, true, 0, 0, 30000, true}},
		{"H", {10, 10, 0, 0, 10, 10, true, 0, 3, 30000, true}},
		{"I", {10, 10, 0, 0, 4, 4, true, 1, 3, 2250, true}},
		{"J", {10, 3, 7, 0, 4, 4, true, 1, 3, 30000, true}},
		{"K", {0, 0, 0, 0, 5, 5, true, 0, 0, 30000, true}},
		{"L", {0, 0, 0, 0, 20, 20, true, 0, 0, 30000, true}},
		{"M", {5, 5, 0, 0, 1, 0, false, 0, 0, 30000, true}},
		{"N", {23, 23, 0, 0, 5, 0, false, 0, 0, 30000, true}},
		{"O", {13, 13, 0, 0, 11, 0, false, 0, 0, 30000, true}},
	};
	std::vector<SImpactResult> vResults;
	vResults.reserve(vCases.size());
	for(size_t i = 0; i < vCases.size(); ++i)
	{
		SImpactResult Result = GetImpactV1Result(vCases[i].second, static_cast<int>(i + 1));
		ASSERT_TRUE(Result.m_Available) << vCases[i].first;
		ASSERT_EQ(Result.m_FinalizedEngagedScoringCohortSize, vCases[i].second.m_EngagedCohortSize) << vCases[i].first;
		std::cout << "IMPACT_CALIBRATION " << vCases[i].first << ' ' << FormatImpactV1Result(Result)
			  << " display=" << FormatTruncatedImpact(Result.m_FinalImpact) << '\n';
		vResults.push_back(Result);
	}
	const auto Value = [&vResults](size_t Index) { return vResults[Index].m_FinalImpact; };
	EXPECT_DOUBLE_EQ(Value(3), 0.0);
	EXPECT_GT(Value(0), Value(1));
	EXPECT_GT(Value(1), 0.0);
	EXPECT_LT(Value(2), 0.0);
	EXPECT_GT(Value(4), Value(0));
	EXPECT_GT(Value(5), 0.0);
	EXPECT_LT(std::abs(Value(6)), 3.0);
	EXPECT_GT(Value(0), Value(9));
	EXPECT_LT(Value(10), 0.0);
	EXPECT_LT(Value(11), Value(10));
	EXPECT_GT(Value(13), Value(14));
	EXPECT_GT(Value(8), 0.0);
	EXPECT_LT(Value(8), Value(0));
}

TEST(ImpactV1, ToxigonLikeCalibrationCasesAreReported)
{
	SImpactV1TestInput Alya;
	Alya.m_Saves = Alya.m_IndependentSaves = 28;
	Alya.m_Falls = 15;
	Alya.m_Clutches = 1;
	Alya.m_RescuesReceivedKnown = false;

	SImpactV1TestInput Ashura;
	Ashura.m_Saves = Ashura.m_IndependentSaves = 15;
	Ashura.m_Falls = 19;
	Ashura.m_Clutches = 1;
	Ashura.m_RescuesReceivedKnown = false;

	SImpactV1TestInput Joland;
	Joland.m_Saves = Joland.m_IndependentSaves = 13;
	Joland.m_Falls = 25;
	Joland.m_Clutches = 1;
	Joland.m_RescuesReceivedKnown = false;

	const SImpactResult AlyaResult = GetImpactV1Result(Alya, 1);
	const SImpactResult AshuraResult = GetImpactV1Result(Ashura, 2);
	const SImpactResult JolandResult = GetImpactV1Result(Joland, 3);
	std::cout << std::setprecision(std::numeric_limits<double>::max_digits10)
		  << "IMPACT_TOXIGON_ALYA " << FormatImpactV1Result(AlyaResult) << " display=" << FormatTruncatedImpact(AlyaResult.m_FinalImpact) << '\n'
		  << "IMPACT_TOXIGON_ASHURA " << FormatImpactV1Result(AshuraResult) << " display=" << FormatTruncatedImpact(AshuraResult.m_FinalImpact) << '\n'
		  << "IMPACT_TOXIGON_JOLAND " << FormatImpactV1Result(JolandResult) << " display=" << FormatTruncatedImpact(JolandResult.m_FinalImpact) << '\n';
	EXPECT_EQ(AlyaResult.m_FinalizedEngagedScoringCohortSize, 4);
	EXPECT_NEAR(AlyaResult.m_FinalImpact, 7.026180172999858, 1e-12);
	EXPECT_NEAR(AshuraResult.m_FinalImpact, -0.06304480916817047, 1e-12);
	EXPECT_NEAR(JolandResult.m_FinalImpact, -3.379085124599513, 1e-12);
	EXPECT_GT(AlyaResult.m_FinalImpact, AshuraResult.m_FinalImpact);
	EXPECT_GT(AshuraResult.m_FinalImpact, JolandResult.m_FinalImpact);
	EXPECT_EQ(FormatTruncatedImpact(AlyaResult.m_FinalImpact), "+7.02%");
	EXPECT_EQ(FormatTruncatedImpact(AshuraResult.m_FinalImpact), "-0.06%");
	EXPECT_EQ(FormatTruncatedImpact(JolandResult.m_FinalImpact), "-3.37%");
}

TEST(ImpactV1, AsymmetricTransformCalibrationComparedWithPreviousV1)
{
	const double PositiveEvidence = 8.0;
	const double PreviousPositive = 4.0 * std::log1p(PositiveEvidence / 4.0);
	const double NewPositive = TransformImpactEvidenceV1(PositiveEvidence, 0.0);
	const double NegativeEvidence = 8.0;
	const double PreviousNegative = -3.70 * std::log1p(NegativeEvidence / 4.0);
	const double NewNegative = TransformImpactEvidenceV1(0.0, NegativeEvidence);
	std::cout << std::setprecision(std::numeric_limits<double>::max_digits10)
		  << "IMPACT_TRANSFORM_COMPARISON positive_evidence=8 previous=" << PreviousPositive << " new=" << NewPositive << '\n'
		  << "IMPACT_TRANSFORM_COMPARISON unopposed_negative_evidence=8 previous=" << PreviousNegative << " new=" << NewNegative << '\n';
	EXPECT_GT(NewPositive, PreviousPositive);
	EXPECT_LT(NewNegative, PreviousNegative);
}

TEST(ImpactV1, NegativeBalanceCalibrationRowsAreReported)
{
	struct SCase
	{
		const char *m_pName;
		int64_t m_Saves;
		int64_t m_Falls;
		int64_t m_Clutches;
	};
	const std::array<SCase, 16> vCases = {{
		{"0s_1f", 0, 1, 0}, {"0s_2f", 0, 2, 0}, {"0s_5f", 0, 5, 0},
		{"0s_10f", 0, 10, 0}, {"0s_20f", 0, 20, 0}, {"0s_40f", 0, 40, 0},
		{"1s_8f", 1, 8, 0}, {"5s_20f", 5, 20, 0}, {"10s_20f", 10, 20, 0},
		{"15s_20f", 15, 20, 0}, {"20s_20f", 20, 20, 0},
		{"joland_like", 13, 25, 1}, {"ashura_like", 15, 19, 1}, {"alya_like", 28, 15, 1},
		{"balance_10s_9f", 9, 10, 0}, {"crossed_10s_9f", 10, 10, 0},
	}};
	std::cout << std::setprecision(std::numeric_limits<double>::max_digits10);
	for(size_t i = 0; i < vCases.size(); ++i)
	{
		SImpactV1TestInput Input;
		Input.m_Saves = Input.m_IndependentSaves = vCases[i].m_Saves;
		Input.m_Falls = vCases[i].m_Falls;
		Input.m_Clutches = vCases[i].m_Clutches;
		const SImpactResult Result = GetImpactV1Result(Input, static_cast<int>(i + 1));
		ASSERT_TRUE(Result.m_Available) << vCases[i].m_pName;
		std::cout << "IMPACT_NEGATIVE_CALIBRATION " << vCases[i].m_pName << ' '
			  << FormatImpactV1Result(Result) << " display=" << FormatTruncatedImpact(Result.m_FinalImpact) << '\n';
	}
}

TEST(ImpactV1, DuoTrioAndTeamSizeCalibrationAreReported)
{
	SImpactV1TestInput DuoA;
	DuoA.m_Saves = DuoA.m_IndependentSaves = 10;
	DuoA.m_Clutches = 3;
	DuoA.m_Aleds = 1;
	DuoA.m_Falls = 4;
	DuoA.m_RescuesReceived = 4;
	SImpactV1TestInput DuoB = DuoA;
	DuoB.m_Clutches = 0;
	SImpactV1TestInput TrioA = DuoA;
	SImpactV1TestInput TrioB = DuoB;
	const SImpactResult DuoAResult = GetImpactV1ResultWithCohort(DuoA, 2);
	const SImpactResult DuoBResult = GetImpactV1ResultWithCohort(DuoB, 2);
	const SImpactResult TrioAResult = GetImpactV1ResultWithCohort(TrioA, 3);
	const SImpactResult TrioBResult = GetImpactV1ResultWithCohort(TrioB, 3);
	std::cout << std::setprecision(std::numeric_limits<double>::max_digits10)
		  << "IMPACT_COHORT_CALIBRATION DUO-A " << FormatImpactV1Result(DuoAResult) << '\n'
		  << "IMPACT_COHORT_CALIBRATION DUO-B " << FormatImpactV1Result(DuoBResult) << '\n'
		  << "IMPACT_COHORT_CALIBRATION TRIO-A " << FormatImpactV1Result(TrioAResult) << '\n'
		  << "IMPACT_COHORT_CALIBRATION TRIO-B " << FormatImpactV1Result(TrioBResult) << '\n';
	EXPECT_DOUBLE_EQ(DuoAResult.m_FinalImpact, DuoBResult.m_FinalImpact);
	EXPECT_GT(TrioAResult.m_FinalImpact, TrioBResult.m_FinalImpact);

	SImpactV1TestInput TeamInput = DuoA;
	const SImpactResult Team3 = GetImpactV1ResultWithCohort(TeamInput, 3);
	const SImpactResult Team5 = GetImpactV1ResultWithCohort(TeamInput, 5);
	const SImpactResult Team8 = GetImpactV1ResultWithCohort(TeamInput, 8);
	std::cout << "IMPACT_COHORT_INVARIANCE TEAM3=" << Team3.m_FinalImpact << " TEAM5=" << Team5.m_FinalImpact
		  << " TEAM8=" << Team8.m_FinalImpact << '\n';
	EXPECT_DOUBLE_EQ(Team3.m_FinalImpact, Team5.m_FinalImpact);
	EXPECT_DOUBLE_EQ(Team5.m_FinalImpact, Team8.m_FinalImpact);
}

TEST(ImpactRatingV1, NeutralImpactNeutralStabilityIsOrdinary)
{
	const SRatingResult Result = GetRatingV1Result(0.0, 50.0);
	EXPECT_DOUBLE_EQ(Result.m_FinalRating, 0.76);
	EXPECT_EQ(Result.m_Class, ERatingClass::MEDIUM);
}

TEST(ImpactRatingV1, NeutralImpactMapsTo076)
{
	EXPECT_DOUBLE_EQ(GetRatingV1Result(0.0, 50.0).m_FinalRating, SRatingV1Constants::m_NeutralBase);
}

TEST(ImpactRatingV1, NeutralImpactAtZeroStabilityIsNotCatastrophic)
{
	const SRatingResult Result = GetRatingV1Result(0.0, 0.0);
	EXPECT_DOUBLE_EQ(Result.m_FinalRating, 0.76);
}

TEST(ImpactRatingV1, ZeroEvidenceProducesBaselineRating)
{
	EXPECT_DOUBLE_EQ(GetRatingV1Result(0.0, 50.0).m_FinalRating, 0.76);
}

TEST(ImpactRatingV1, PositiveImpactRaisesRating)
{
	EXPECT_GT(GetRatingV1Result(2.0, 50.0).m_FinalRating, GetRatingV1Result(0.0, 50.0).m_FinalRating);
}

TEST(ImpactRatingV1, NegativeImpactLowersRating)
{
	EXPECT_LT(GetRatingV1Result(-2.0, 50.0).m_FinalRating, GetRatingV1Result(0.0, 50.0).m_FinalRating);
}

TEST(ImpactRatingV1, SameImpactDifferentStabilitySameRating)
{
	const std::array<double, 5> aStability = {0.0, 20.0, 50.0, 75.0, 100.0};
	const double Expected = GetRatingV1Result(5.0, aStability[0]).m_FinalRating;
	for(double Stability : aStability)
		EXPECT_DOUBLE_EQ(GetRatingV1Result(5.0, Stability).m_FinalRating, Expected);
	EXPECT_DOUBLE_EQ(GetRatingV1Result(-3.0, 0.0).m_FinalRating,
		GetRatingV1Result(-3.0, 100.0).m_FinalRating);
}

TEST(ImpactRatingV1, StabilityZeroVsHundredCannotChangeRating)
{
	for(double Impact : {-5.0, -3.0, -0.1, 0.0, 1.0, 5.0, 7.0, 10.0})
		EXPECT_DOUBLE_EQ(GetRatingV1Result(Impact, 0.0).m_FinalRating,
			GetRatingV1Result(Impact, 100.0).m_FinalRating);
}

TEST(ImpactRatingV1, HighStabilityWithoutImpactIsNotElite)
{
	const SRatingResult Result = GetRatingV1Result(0.0, 100.0);
	EXPECT_LT(Result.m_FinalRating, 1.40);
	EXPECT_LT(Result.m_FinalRating, 1.0);
}

TEST(ImpactRatingV1, HighStabilityAndTinyImpactIsNotElite)
{
	EXPECT_LT(GetRatingV1Result(1.0, 100.0).m_FinalRating, 1.40);
}

TEST(ImpactRatingV1, MinorNegativeImpactDoesNotCollapseRating)
{
	const SRatingResult Result = GetRatingV1Result(-0.1, 40.0);
	EXPECT_GT(Result.m_FinalRating, 0.6);
	EXPECT_NEAR(Result.m_FinalRating, 0.75, 0.02);
}

TEST(ImpactRatingV1, AshuraLikeNearNeutralImpactIsNotLowCollapse)
{
	const SRatingResult Result = GetRatingV1Result(-0.063044809168170485, 40.74074074074074);
	EXPECT_GE(Result.m_FinalRating, 0.7);
	EXPECT_LE(Result.m_FinalRating, 0.85);
	EXPECT_GT(Result.m_FinalRating, 0.6);
}

TEST(ImpactRatingV1, JolandLikeRemainsClearlyBelowAshura)
{
	const SRatingResult Ashura = GetRatingV1Result(-0.063044809168170485, 40.74074074074074);
	const SRatingResult Joland = GetRatingV1Result(-3.3790851245995128, 31.668856767411302);
	EXPECT_GT(Ashura.m_FinalRating, Joland.m_FinalRating);
}

TEST(ImpactRatingV1, RatingIsMonotonicInImpact)
{
	const std::array<double, 12> aImpact = {-15.0, -10.0, -5.0, -3.0, -1.0, -0.1,
		0.0, 0.1, 1.0, 5.0, 15.0, 40.0};
	double Previous = GetRatingV1Result(aImpact[0], 50.0).m_FinalRating;
	for(double Impact : aImpact)
	{
		const double Rating = GetRatingV1Result(Impact, 50.0).m_FinalRating;
		EXPECT_GE(Rating, Previous);
		Previous = Rating;
	}
	for(size_t i = 1; i < aImpact.size(); i++)
		EXPECT_GT(GetRatingV1Result(aImpact[i], 50.0).m_FinalRating,
			GetRatingV1Result(aImpact[i - 1], 50.0).m_FinalRating);
}

TEST(ImpactRatingV1, HigherRatingImpliesHigherImpact)
{
	const std::array<std::pair<double, double>, 8> vPoints = {{{-10.0, 100.0}, {-3.0, 0.0}, {-0.1, 100.0},
		{0.0, 0.0}, {0.1, 100.0}, {1.0, 0.0}, {5.0, 100.0}, {15.0, 0.0}}};
	for(const auto &[ImpactA, StabilityA] : vPoints)
	{
		for(const auto &[ImpactB, StabilityB] : vPoints)
		{
			const double RatingA = GetRatingV1Result(ImpactA, StabilityA).m_FinalRating;
			const double RatingB = GetRatingV1Result(ImpactB, StabilityB).m_FinalRating;
			if(RatingA > RatingB)
				EXPECT_GT(ImpactA, ImpactB);
		}
	}
}

TEST(ImpactRatingV1, ImpactOrderingCannotBeInvertedByStability)
{
	const double Rating5HighStability = GetRatingV1Result(5.0, 100.0).m_FinalRating;
	const double Rating7LowStability = GetRatingV1Result(7.0, 0.0).m_FinalRating;
	EXPECT_LT(Rating5HighStability, Rating7LowStability);
	EXPECT_LT(GetRatingV1Result(5.0, 0.0).m_FinalRating,
		GetRatingV1Result(7.0, 100.0).m_FinalRating);
}

TEST(ImpactRatingV1, HigherImpactNeverProducesLowerRating)
{
	const std::array<std::pair<double, double>, 8> vPairs = {{{-10.0, -5.0}, {-5.0, -3.0}, {-3.0, -0.1},
		{-0.1, 0.0}, {0.0, 0.1}, {0.1, 1.0}, {1.0, 5.0}, {5.0, 15.0}}};
	for(size_t i = 0; i < vPairs.size(); i++)
	{
		const double RatingA = GetRatingV1Result(vPairs[i].first, (i % 2) ? 100.0 : 0.0).m_FinalRating;
		const double RatingB = GetRatingV1Result(vPairs[i].second, (i % 2) ? 0.0 : 100.0).m_FinalRating;
		EXPECT_GT(RatingB, RatingA);
	}
}

TEST(ImpactRatingV1, TeamPairwiseRatingOrderImpliesImpactOrder)
{
	const std::array<std::pair<double, double>, 11> vPlayers = {{{-15.0, 100.0}, {-10.0, 0.0}, {-5.0, 75.0},
		{-0.1, 10.0}, {0.0, 100.0}, {0.000000001, 0.0}, {1.0, 90.0}, {5.0, 10.0},
		{5.000000001, 100.0}, {10.0, 0.0}, {40.0, 100.0}}};
	std::array<SRatingResult, vPlayers.size()> vRatings;
	for(size_t i = 0; i < vPlayers.size(); i++)
		vRatings[i] = GetRatingV1Result(vPlayers[i].first, vPlayers[i].second, static_cast<int>(i + 1), i % 4 + 2);
	for(size_t i = 0; i < vRatings.size(); i++)
	{
		for(size_t j = i + 1; j < vRatings.size(); j++)
		{
			if(vRatings[i].m_FinalRating > vRatings[j].m_FinalRating)
				EXPECT_GT(vRatings[i].m_Impact, vRatings[j].m_Impact);
			else if(vRatings[j].m_FinalRating > vRatings[i].m_FinalRating)
				EXPECT_GT(vRatings[j].m_Impact, vRatings[i].m_Impact);
			else if(vRatings[i].m_Impact == vRatings[j].m_Impact)
				EXPECT_DOUBLE_EQ(vRatings[i].m_FinalRating, vRatings[j].m_FinalRating);
		}
	}
}

TEST(ImpactRatingV1, NearEqualImpactCannotBeInverted)
{
	const double LowerImpact = 5.0;
	const double HigherImpact = 5.000000001;
	EXPECT_GT(GetRatingV1Result(HigherImpact, 0.0).m_FinalRating,
		GetRatingV1Result(LowerImpact, 100.0).m_FinalRating);
	EXPECT_GT(GetRatingV1Result(HigherImpact, 100.0).m_FinalRating,
		GetRatingV1Result(LowerImpact, 0.0).m_FinalRating);
}

TEST(ImpactRatingV1, RatingNeverNegative)
{
	const SRatingResult Result = GetRatingV1Result(-1000000.0, 0.0);
	EXPECT_GE(Result.m_FinalRating, 0.0);
}

TEST(ImpactRatingV1, RatingHasNoUpperHardCap)
{
	const SRatingResult TwoThousand = GetRatingV1Result(2000.0, 100.0);
	const SRatingResult TenThousand = GetRatingV1Result(10000.0, 100.0);
	EXPECT_GT(TwoThousand.m_FinalRating, 2.0);
	EXPECT_GT(TenThousand.m_FinalRating, TwoThousand.m_FinalRating);
}

TEST(ImpactRatingV1, SameInputIsDeterministic)
{
	const SFinalizedScoringDimensions Dimensions = {true, true, TestId(1), TestId(1), 4, 5.25, 65.5};
	SRatingResult A;
	SRatingResult B;
	ASSERT_TRUE(CalculateRatingV1(Dimensions, &A));
	ASSERT_TRUE(CalculateRatingV1(Dimensions, &B));
	EXPECT_DOUBLE_EQ(A.m_FinalRating, B.m_FinalRating);
	EXPECT_DOUBLE_EQ(A.m_RawRating, B.m_RawRating);
	EXPECT_EQ(A.m_Class, B.m_Class);
}

TEST(ImpactRatingV1, SameDimensionsSameRating)
{
	const SRatingResult First = GetRatingV1Result(5.0, 65.0, 1, 2);
	const SRatingResult SameDimensionsDifferentParticipantContext = GetRatingV1Result(5.0, 65.0, 17, 8);
	EXPECT_DOUBLE_EQ(First.m_FinalRating, SameDimensionsDifferentParticipantContext.m_FinalRating);
}

TEST(ImpactRatingV1, TeamSizeAloneDoesNotAffectRating)
{
	const SRatingResult Duo = GetRatingV1Result(5.0, 72.0, 1, 2);
	const SRatingResult Trio = GetRatingV1Result(5.0, 72.0, 1, 3);
	const SRatingResult FullTeam = GetRatingV1Result(5.0, 72.0, 1, 8);
	EXPECT_DOUBLE_EQ(Duo.m_FinalRating, Trio.m_FinalRating);
	EXPECT_DOUBLE_EQ(Trio.m_FinalRating, FullTeam.m_FinalRating);
}

TEST(ImpactRatingV1, RatingRequiresImpactAvailable)
{
	SFinalizedScoringDimensions Dimensions = {false, true, TestId(1), TestId(1), 4, 5.0, 70.0};
	SRatingResult Result;
	EXPECT_FALSE(CalculateRatingV1(Dimensions, &Result));
	EXPECT_FALSE(Result.m_Available);
}

TEST(ImpactRatingV1, RatingRequiresStabilityAvailable)
{
	SFinalizedScoringDimensions Dimensions = {true, false, TestId(1), TestId(1), 4, 5.0, 70.0};
	SRatingResult Result;
	EXPECT_FALSE(CalculateRatingV1(Dimensions, &Result));
	EXPECT_FALSE(Result.m_Available);
}

TEST(ImpactRatingV1, UnknownDimensionDoesNotBecomeNeutralDefault)
{
	SFinalizedScoringDimensions Dimensions = {true, true, TestId(1), TestId(2), 4, 0.0, 50.0};
	SRatingResult Result;
	EXPECT_FALSE(CalculateRatingV1(Dimensions, &Result));
	Dimensions.m_ImpactId = Dimensions.m_StabilityId;
	Dimensions.m_FinalizedEngagedScoringCohortSize = 0;
	EXPECT_FALSE(CalculateRatingV1(Dimensions, &Result));
	Dimensions.m_FinalizedEngagedScoringCohortSize = 4;
	Dimensions.m_FinalStability = std::numeric_limits<double>::quiet_NaN();
	EXPECT_FALSE(CalculateRatingV1(Dimensions, &Result));
}

TEST(ImpactRatingV1, NotEngagedParticipantCannotBeScored)
{
	SImpactV1TestInput Input;
	SPlayerScoringFeatures Player = MakeImpactV1Feature(1, Input);
	Player.m_Engaged = false;
	Player.m_Finalized = false;
	Player.m_InFinalizedScoringCohort = false;
	const CRunScoringContext Context = MakeImpactV1PlayerContext(Player, 1);
	SImpactResult Impact;
	SStabilityResult Stability;
	EXPECT_FALSE(CalculateImpactV1(Player, Context, &Impact));
	EXPECT_FALSE(CalculateStabilityV1(Player, Context, &Stability));
	const SFinalizedScoringDimensions Dimensions = MakeFinalizedScoringDimensions(Impact, Stability);
	SRatingResult Rating;
	EXPECT_FALSE(CalculateRatingV1(Dimensions, &Rating));
}

TEST(ImpactRatingV1, RatingDoesNotReadRawSaves)
{
	SImpactResult Impact;
	SStabilityResult Stability;
	MakeSyntheticUpstreamResults(2.0, 70.0, &Impact, &Stability);
	const SFinalizedScoringDimensions Base = MakeFinalizedScoringDimensions(Impact, Stability);
	Impact.m_RawCanonicalSaves = Impact.m_IndependentSaves = 500;
	Stability.m_CanonicalSaves = Stability.m_IndependentSaves = 500;
	const SFinalizedScoringDimensions Changed = MakeFinalizedScoringDimensions(Impact, Stability);
	SRatingResult A;
	SRatingResult B;
	ASSERT_TRUE(CalculateRatingV1(Base, &A));
	ASSERT_TRUE(CalculateRatingV1(Changed, &B));
	EXPECT_DOUBLE_EQ(A.m_FinalRating, B.m_FinalRating);
}

TEST(ImpactRatingV1, RepeatedSaveVolumeCannotDirectlyFarmRating)
{
	SImpactResult ImpactA;
	SStabilityResult StabilityA;
	MakeSyntheticUpstreamResults(2.0, 70.0, &ImpactA, &StabilityA);
	SImpactResult ImpactB = ImpactA;
	SStabilityResult StabilityB = StabilityA;
	ImpactB.m_RawCanonicalSaves = ImpactB.m_RepeatedSaves = 1000000;
	StabilityB.m_CanonicalSaves = StabilityB.m_RepeatedLoopSaves = 1000000;
	EXPECT_DOUBLE_EQ(ScoreRatingFromUpstreamResults(ImpactA, StabilityA), ScoreRatingFromUpstreamResults(ImpactB, StabilityB));
}

TEST(ImpactRatingV1, RatingDoesNotReadRawFalls)
{
	SImpactResult Impact;
	SStabilityResult Stability;
	MakeSyntheticUpstreamResults(2.0, 70.0, &Impact, &Stability);
	const SFinalizedScoringDimensions Base = MakeFinalizedScoringDimensions(Impact, Stability);
	Impact.m_Falls = 10000;
	Stability.m_Falls = 10000;
	const SFinalizedScoringDimensions Changed = MakeFinalizedScoringDimensions(Impact, Stability);
	SRatingResult A;
	SRatingResult B;
	ASSERT_TRUE(CalculateRatingV1(Base, &A));
	ASSERT_TRUE(CalculateRatingV1(Changed, &B));
	EXPECT_DOUBLE_EQ(A.m_FinalRating, B.m_FinalRating);
}

TEST(ImpactRatingV1, RatingDoesNotReadRawClutches)
{
	SImpactResult Impact;
	SStabilityResult Stability;
	MakeSyntheticUpstreamResults(2.0, 70.0, &Impact, &Stability);
	const SFinalizedScoringDimensions Base = MakeFinalizedScoringDimensions(Impact, Stability);
	Impact.m_Clutches = Stability.m_Clutches = 1000;
	const double ChangedRating = ScoreRatingFromUpstreamResults(Impact, Stability);
	const auto BaseRating = GetRatingV1Result(Base.m_FinalImpact, Base.m_FinalStability);
	EXPECT_DOUBLE_EQ(BaseRating.m_FinalRating, ChangedRating);
}

TEST(ImpactRatingV1, RatingDoesNotReadRawAleds)
{
	SImpactResult Impact;
	SStabilityResult Stability;
	MakeSyntheticUpstreamResults(2.0, 70.0, &Impact, &Stability);
	const SFinalizedScoringDimensions Base = MakeFinalizedScoringDimensions(Impact, Stability);
	Impact.m_Aleds = Stability.m_Aleds = 1000;
	const double ChangedRating = ScoreRatingFromUpstreamResults(Impact, Stability);
	const auto BaseRating = GetRatingV1Result(Base.m_FinalImpact, Base.m_FinalStability);
	EXPECT_DOUBLE_EQ(BaseRating.m_FinalRating, ChangedRating);
}

TEST(ImpactRatingV1, RatingDoesNotReadRescuesReceived)
{
	SImpactResult Impact;
	SStabilityResult Stability;
	MakeSyntheticUpstreamResults(2.0, 70.0, &Impact, &Stability);
	const SFinalizedScoringDimensions Base = MakeFinalizedScoringDimensions(Impact, Stability);
	Impact.m_RescuesReceived = Stability.m_RescuesReceived = 1000000;
	Impact.m_RescuesReceivedKnown = Stability.m_RescuesReceivedKnown = true;
	const double ChangedRating = ScoreRatingFromUpstreamResults(Impact, Stability);
	const auto BaseRating = GetRatingV1Result(Base.m_FinalImpact, Base.m_FinalStability);
	EXPECT_DOUBLE_EQ(BaseRating.m_FinalRating, ChangedRating);
}

TEST(ImpactRatingV1, SameDimensionsDifferentRawCountersSameRating)
{
	SImpactResult ImpactA;
	SStabilityResult StabilityA;
	MakeSyntheticUpstreamResults(4.75, 83.0, &ImpactA, &StabilityA);
	SImpactResult ImpactB = ImpactA;
	SStabilityResult StabilityB = StabilityA;
	ImpactB.m_RawCanonicalSaves = ImpactB.m_IndependentSaves = ImpactB.m_RepeatedSaves = 500;
	ImpactB.m_UnknownPatternSaves = ImpactB.m_Clutches = ImpactB.m_Aleds = ImpactB.m_Falls = 500;
	ImpactB.m_RescuesReceived = 500;
	StabilityB.m_CanonicalSaves = StabilityB.m_IndependentSaves = StabilityB.m_RepeatedLoopSaves = 900;
	StabilityB.m_UnknownPatternSaves = StabilityB.m_Clutches = StabilityB.m_Aleds = StabilityB.m_Falls = 900;
	StabilityB.m_RescuesReceived = 900;
	const SFinalizedScoringDimensions DimensionsA = MakeFinalizedScoringDimensions(ImpactA, StabilityA);
	const SFinalizedScoringDimensions DimensionsB = MakeFinalizedScoringDimensions(ImpactB, StabilityB);
	SRatingResult A;
	SRatingResult B;
	ASSERT_TRUE(CalculateRatingV1(DimensionsA, &A));
	ASSERT_TRUE(CalculateRatingV1(DimensionsB, &B));
	EXPECT_DOUBLE_EQ(A.m_FinalRating, B.m_FinalRating);
}

TEST(ImpactRatingV1, DuoClutchCannotReenterRating)
{
	SImpactResult ImpactA;
	SStabilityResult StabilityA;
	MakeSyntheticUpstreamResults(3.0, 75.0, &ImpactA, &StabilityA);
	SImpactResult ImpactB = ImpactA;
	ImpactB.m_Clutches = 100;
	EXPECT_DOUBLE_EQ(ScoreRatingFromUpstreamResults(ImpactA, StabilityA), ScoreRatingFromUpstreamResults(ImpactB, StabilityA));
}

TEST(ImpactRatingV1, AledCannotBeDoubleCountedByRating)
{
	SImpactResult ImpactA;
	SStabilityResult StabilityA;
	MakeSyntheticUpstreamResults(3.0, 75.0, &ImpactA, &StabilityA);
	SImpactResult ImpactB = ImpactA;
	ImpactB.m_Aleds = 100;
	EXPECT_DOUBLE_EQ(ScoreRatingFromUpstreamResults(ImpactA, StabilityA), ScoreRatingFromUpstreamResults(ImpactB, StabilityA));
}

TEST(ImpactRatingV1, NoDoubleConfidenceShrink)
{
	SImpactResult ImpactA;
	SStabilityResult StabilityA;
	MakeSyntheticUpstreamResults(3.0, 75.0, &ImpactA, &StabilityA);
	SImpactResult ImpactB = ImpactA;
	SStabilityResult StabilityB = StabilityA;
	ImpactB.m_CombinedConfidence = 0.1;
	ImpactB.m_ParticipationConfidence = 0.1;
	ImpactB.m_EvidenceConfidence = 0.1;
	StabilityB.m_CombinedConfidence = 0.1;
	StabilityB.m_ParticipationConfidence = 0.1;
	StabilityB.m_EvidenceConfidence = 0.1;
	const auto A = MakeFinalizedScoringDimensions(ImpactA, StabilityA);
	const auto B = MakeFinalizedScoringDimensions(ImpactB, StabilityB);
	EXPECT_DOUBLE_EQ(GetRatingV1Result(A.m_FinalImpact, A.m_FinalStability).m_FinalRating,
		GetRatingV1Result(B.m_FinalImpact, B.m_FinalStability).m_FinalRating);
}

TEST(ImpactRatingV1, ReferenceAIsNearExpectedBand)
{
	SImpactV1TestInput Input;
	Input.m_Saves = Input.m_IndependentSaves = 10;
	Input.m_Clutches = 3;
	Input.m_Aleds = 1;
	Input.m_Falls = 4;
	const SRatingResult Result = GetRatingV1ResultForGameplay(Input);
	EXPECT_NEAR(Result.m_FinalRating, 1.40, 0.05);
	EXPECT_EQ(Result.m_Class, ERatingClass::EXCELLENT);
	std::cout << std::setprecision(std::numeric_limits<double>::max_digits10)
		  << "RATING_REFERENCE_A " << FormatRatingV1Result(Result) << " display=" << FormatTruncatedRating(Result.m_FinalRating) << '\n';
}

TEST(ImpactRatingV1, ReferenceARemainsExcellent)
{
	SImpactV1TestInput Input;
	Input.m_Saves = Input.m_IndependentSaves = 10;
	Input.m_Clutches = 3;
	Input.m_Aleds = 1;
	Input.m_Falls = 4;
	const SRatingResult Result = GetRatingV1ResultForGameplay(Input);
	EXPECT_NEAR(Result.m_FinalRating, 1.42, 0.05);
	EXPECT_EQ(Result.m_Class, ERatingClass::EXCELLENT);
}

TEST(ImpactRatingV1, ReferenceBIsNearExpectedBand)
{
	SImpactV1TestInput Input;
	Input.m_Saves = Input.m_IndependentSaves = 4;
	Input.m_Falls = 1;
	const SRatingResult Result = GetRatingV1ResultForGameplay(Input);
	EXPECT_NEAR(Result.m_FinalRating, 1.1132051203228752, 1e-14);
	std::cout << std::setprecision(std::numeric_limits<double>::max_digits10)
		  << "RATING_REFERENCE_B " << FormatRatingV1Result(Result) << " display=" << FormatTruncatedRating(Result.m_FinalRating) << '\n';
}

TEST(ImpactRatingV1, ReferenceBIsGood)
{
	SImpactV1TestInput Input;
	Input.m_Saves = Input.m_IndependentSaves = 4;
	Input.m_Falls = 1;
	const SRatingResult Result = GetRatingV1ResultForGameplay(Input);
	EXPECT_NEAR(Result.m_FinalRating, 1.1132051203228752, 1e-14);
	EXPECT_EQ(Result.m_Class, ERatingClass::GOOD);
}

TEST(ImpactRatingV1, ReferenceCIsLow)
{
	SImpactV1TestInput Input;
	Input.m_Saves = Input.m_IndependentSaves = 1;
	Input.m_Falls = 8;
	const SRatingResult Result = GetRatingV1ResultForGameplay(Input);
	EXPECT_GE(Result.m_FinalRating, 0.0);
	EXPECT_LE(Result.m_FinalRating, 0.6);
	std::cout << std::setprecision(std::numeric_limits<double>::max_digits10)
		  << "RATING_REFERENCE_C " << FormatRatingV1Result(Result) << " display=" << FormatTruncatedRating(Result.m_FinalRating) << '\n';
}

TEST(ImpactRatingV1, OrdinaryNegativeRunDoesNotAutomaticallyReachPointTwo)
{
	const SRatingResult Result = GetRatingV1Result(-3.0, 30.0);
	EXPECT_GT(Result.m_FinalRating, 0.2);
	EXPECT_GT(Result.m_FinalRating, 0.4);
}

TEST(ImpactRatingV1, ExtremeHarmCanApproachVeryLowRating)
{
	SImpactV1TestInput Input;
	Input.m_Falls = 20;
	Input.m_EngagedCohortSize = 4;
	const SRatingResult Result = GetRatingV1ResultForGameplay(Input);
	EXPECT_LT(Result.m_FinalRating, 0.25);
	EXPECT_GT(Result.m_FinalRating, 0.0);
	std::cout << std::setprecision(std::numeric_limits<double>::max_digits10)
		  << "RATING_EXTREME_HARM " << FormatRatingV1Result(Result)
		  << " display=" << FormatTruncatedRating(Result.m_FinalRating) << '\n';
}

TEST(ImpactRatingV1, StrongCaseCanExceedTwo)
{
	SImpactV1TestInput Input;
	Input.m_Saves = Input.m_IndependentSaves = 45;
	Input.m_Clutches = 10;
	Input.m_Aleds = 7;
	Input.m_Falls = 5;
	const SRatingResult Result = GetRatingV1ResultForGameplay(Input);
	EXPECT_GT(Result.m_FinalRating, 2.0);
	EXPECT_DOUBLE_EQ(Result.m_FinalRating, Result.m_RawRating);
	std::cout << std::setprecision(std::numeric_limits<double>::max_digits10)
		  << "RATING_STRONG_E " << FormatRatingV1Result(Result) << " display=" << FormatTruncatedRating(Result.m_FinalRating) << '\n';
}

TEST(ImpactRatingV1, ToxigonOrderingAlyaAshuraJoland)
{
	SImpactV1TestInput Alya;
	Alya.m_Saves = Alya.m_IndependentSaves = 28;
	Alya.m_Falls = 15;
	Alya.m_Clutches = 1;
	SImpactV1TestInput Ashura;
	Ashura.m_Saves = Ashura.m_IndependentSaves = 15;
	Ashura.m_Falls = 19;
	Ashura.m_Clutches = 1;
	SImpactV1TestInput Joland;
	Joland.m_Saves = Joland.m_IndependentSaves = 13;
	Joland.m_Falls = 25;
	Joland.m_Clutches = 1;
	const SRatingResult AlyaResult = GetRatingV1ResultForGameplay(Alya, 1);
	const SRatingResult AshuraResult = GetRatingV1ResultForGameplay(Ashura, 2);
	const SRatingResult JolandResult = GetRatingV1ResultForGameplay(Joland, 3);
	EXPECT_GT(AlyaResult.m_FinalRating, AshuraResult.m_FinalRating);
	EXPECT_GT(AshuraResult.m_FinalRating, JolandResult.m_FinalRating);
	EXPECT_GT(AlyaResult.m_Impact, AshuraResult.m_Impact);
	EXPECT_GT(AshuraResult.m_Impact, JolandResult.m_Impact);
	std::cout << std::setprecision(std::numeric_limits<double>::max_digits10)
		  << "RATING_TOXIGON_ALYA " << FormatRatingV1Result(AlyaResult) << " display=" << FormatTruncatedRating(AlyaResult.m_FinalRating) << '\n'
		  << "RATING_TOXIGON_ASHURA " << FormatRatingV1Result(AshuraResult) << " display=" << FormatTruncatedRating(AshuraResult.m_FinalRating) << '\n'
		  << "RATING_TOXIGON_JOLAND " << FormatRatingV1Result(JolandResult) << " display=" << FormatTruncatedRating(JolandResult.m_FinalRating) << '\n';
}

TEST(ImpactRatingV1, AlyaLikeRemainsAroundOnePointSix)
{
	SImpactV1TestInput Alya;
	Alya.m_Saves = Alya.m_IndependentSaves = 28;
	Alya.m_Falls = 15;
	Alya.m_Clutches = 1;
	const SRatingResult Result = GetRatingV1ResultForGameplay(Alya);
	EXPECT_NEAR(Result.m_FinalRating, 1.60, 0.05);
	EXPECT_EQ(Result.m_Class, ERatingClass::EXCELLENT);
}

TEST(ImpactRatingV1, ClassificationUsesFullPrecision)
{
	EXPECT_EQ(ClassifyRatingV1(0.599), ERatingClass::LOW);
	EXPECT_EQ(ClassifyRatingV1(0.60), ERatingClass::MEDIUM);
	EXPECT_EQ(ClassifyRatingV1(1.009), ERatingClass::MEDIUM);
	EXPECT_EQ(ClassifyRatingV1(1.01), ERatingClass::GOOD);
	EXPECT_EQ(ClassifyRatingV1(1.399), ERatingClass::GOOD);
	EXPECT_EQ(ClassifyRatingV1(1.40), ERatingClass::EXCELLENT);
	EXPECT_EQ(ClassifyRatingV1(1.405), ERatingClass::EXCELLENT);
	EXPECT_EQ(ClassifyRatingV1(1.609), ERatingClass::EXCELLENT);
	EXPECT_EQ(ClassifyRatingV1(1.61), ERatingClass::OUTSTANDING);
	EXPECT_EQ(ClassifyRatingV1(1.611), ERatingClass::OUTSTANDING);
	EXPECT_EQ(ClassifyRatingV1(1.999), ERatingClass::OUTSTANDING);
	EXPECT_EQ(ClassifyRatingV1(2.00), ERatingClass::STRONG_IMPACT);
	EXPECT_EQ(ClassifyRatingV1(-0.01), ERatingClass::UNAVAILABLE);
	EXPECT_EQ(ClassifyRatingV1(std::numeric_limits<double>::infinity()), ERatingClass::UNAVAILABLE);
}

TEST(ImpactRatingV1, RatingDisplayUsesTwoDecimals)
{
	EXPECT_EQ(FormatTruncatedRating(1.019), "1.01");
	EXPECT_EQ(FormatTruncatedRating(1.4297), "1.42");
	EXPECT_EQ(FormatTruncatedRating(1.4999), "1.49");
	EXPECT_EQ(FormatTruncatedRating(1.9999), "1.99");
	EXPECT_EQ(FormatTruncatedRating(2.059), "2.05");
	EXPECT_EQ(FormatTruncatedRating(0.599), "0.59");
	EXPECT_EQ(FormatTruncatedRating(1.606), "1.60");
	EXPECT_EQ(FormatTruncatedRating(1.599), "1.59");
	EXPECT_EQ(FormatTruncatedRating(0.999), "0.99");
	EXPECT_EQ(FormatTruncatedRating(0.753), "0.75");
	EXPECT_EQ(FormatTruncatedRating(0.501), "0.50");
	EXPECT_EQ(FormatTruncatedRating(0.399), "0.39");
	EXPECT_EQ(FormatTruncatedRating(0.0), "0.00");
	EXPECT_EQ(FormatTruncatedRating(2.0), "2.00");
}

TEST(ImpactRatingV1, RatingDisplayTruncatesTowardZero)
{
	EXPECT_EQ(FormatTruncatedRating(1.4999), "1.49");
	EXPECT_EQ(FormatTruncatedRating(-1.4999), "-1.49");
	EXPECT_EQ(FormatTruncatedRating(-0.069), "-0.06");
	EXPECT_EQ(FormatTruncatedRating(-0.009), "0.00");
	EXPECT_EQ(FormatTruncatedRating(1.999), "1.99");
}

TEST(ImpactRatingV1, RatingDisplayDoesNotRound)
{
	EXPECT_NE(FormatTruncatedRating(1.4999), "1.50");
	EXPECT_NE(FormatTruncatedRating(1.9999), "2.00");
}

TEST(ImpactRatingV1, DisplayDoesNotAffectClassification)
{
	EXPECT_EQ(FormatTruncatedRating(1.405), "1.40");
	EXPECT_EQ(ClassifyRatingV1(1.405), ERatingClass::EXCELLENT);
	EXPECT_EQ(FormatTruncatedRating(1.609), "1.60");
	EXPECT_EQ(ClassifyRatingV1(1.609), ERatingClass::EXCELLENT);
	EXPECT_EQ(FormatTruncatedRating(1.611), "1.61");
	EXPECT_EQ(ClassifyRatingV1(1.611), ERatingClass::OUTSTANDING);
}

TEST(ImpactRatingV1, DisplayDoesNotAffectInternalPrecision)
{
	const SRatingResult Result = GetRatingV1Result(5.0, 65.0);
	const double FullPrecision = Result.m_FinalRating;
	EXPECT_EQ(FormatTruncatedRating(FullPrecision), FormatTruncatedRating(FullPrecision));
	EXPECT_DOUBLE_EQ(Result.m_FinalRating, FullPrecision);
	EXPECT_NE(Result.m_FinalRating, std::stod(FormatTruncatedRating(FullPrecision)));
}

TEST(ImpactRatingV1, DisplayCannotChangeOrdering)
{
	const SRatingResult Lower = GetRatingV1Result(5.0, 100.0);
	const SRatingResult Higher = GetRatingV1Result(5.000000001, 0.0);
	EXPECT_EQ(FormatTruncatedRating(Lower.m_FinalRating), FormatTruncatedRating(Higher.m_FinalRating));
	EXPECT_LT(Lower.m_FinalRating, Higher.m_FinalRating);
}

TEST(ImpactRatingV1, CalibrationMatrixIsReported)
{
	const std::array<double, 25> aImpact = {-20.0, -15.0, -10.228488553430552, -10.0,
		-5.0873575283910881, -5.0, -3.3790851245995128, -3.0, -1.0, -0.1,
		-0.063044809168170485, 0.0, 0.1, 1.0, 2.5247218608804687, 5.0,
		5.2532588612787121, 6.0, 7.0261801729998581, 7.5, 10.0,
		11.088876371757134, 15.0, 20.0, 40.0};
	std::cout << std::setprecision(std::numeric_limits<double>::max_digits10);
	for(double Impact : aImpact)
	{
		const SRatingResult Result = GetRatingV1Result(Impact, 50.0);
		ASSERT_TRUE(Result.m_Available);
		std::cout << "RATING_IMPACT_CALIBRATION impact=" << Impact << " rating=" << Result.m_FinalRating
			  << " display=" << FormatTruncatedRating(Result.m_FinalRating)
			  << " class=" << RatingClassName(Result.m_Class) << '\n';
	}
}

TEST(ImpactRatingV1, StabilityInvarianceCalibrationIsReported)
{
	const std::array<double, 8> aImpact = {-5.0, -3.0, -0.1, 0.0, 1.0, 5.0, 7.0, 10.0};
	const std::array<double, 5> aStability = {0.0, 25.0, 50.0, 75.0, 100.0};
	std::cout << std::setprecision(std::numeric_limits<double>::max_digits10);
	for(double Impact : aImpact)
	{
		const double Expected = GetRatingV1Result(Impact, aStability[0]).m_FinalRating;
		std::cout << "RATING_STABILITY_INVARIANCE impact=" << Impact;
		for(double Stability : aStability)
		{
			const double Rating = GetRatingV1Result(Impact, Stability).m_FinalRating;
			EXPECT_DOUBLE_EQ(Rating, Expected);
			std::cout << " stability=" << Stability << ':' << Rating;
		}
		std::cout << '\n';
	}
}

TEST(ImpactRatingV1, CalibrationSelectedRowsReportDisplayAndClass)
{
	struct SCase
	{
		const char *m_pName;
		double m_Impact;
		double m_Stability;
	};
	const std::array<SCase, 13> vCases = {{{"-15/0", -15.0, 0.0}, {"-10/10-nearest-row", -10.0, 10.0},
		{"-5/20", -5.0, 20.0}, {"-3/30", -3.0, 30.0}, {"-0.1/40", -0.1, 40.0},
		{"0/50", 0.0, 50.0}, {"+1/100", 1.0, 100.0}, {"+5/50", 5.0, 50.0},
		{"+5/10", 5.0, 10.0}, {"+10/10", 10.0, 10.0}, {"+10/90", 10.0, 90.0},
		{"+15/0", 15.0, 0.0}, {"+15/100", 15.0, 100.0}}};
	std::cout << std::setprecision(std::numeric_limits<double>::max_digits10);
	for(const SCase &Case : vCases)
	{
		const SRatingResult Result = GetRatingV1Result(Case.m_Impact, Case.m_Stability);
		std::cout << "RATING_SELECTED " << Case.m_pName << " exact=" << Result.m_FinalRating
			  << " display=" << FormatTruncatedRating(Result.m_FinalRating)
			  << " class=" << RatingClassName(Result.m_Class) << '\n';
	}
}

TEST(ImpactPresentationColor, ImpactNegativeIsRed)
{
	EXPECT_EQ(ClassifyImpactDisplayColor(-5.0), EImpactDisplayColor::RED);
	EXPECT_EQ(ClassifyImpactDisplayColor(-0.01), EImpactDisplayColor::RED);
}

TEST(ImpactPresentationColor, ImpactZeroIsWhite)
{
	EXPECT_EQ(ClassifyImpactDisplayColor(0.0), EImpactDisplayColor::WHITE);
	EXPECT_EQ(FormatTruncatedImpact(0.0), "0.00%");
}

TEST(ImpactPresentationColor, ImpactNegativeZeroIsWhite)
{
	EXPECT_EQ(ClassifyImpactDisplayColor(-0.0), EImpactDisplayColor::WHITE);
	EXPECT_EQ(FormatTruncatedImpact(-0.0), "0.00%");
}

TEST(ImpactPresentationColor, ImpactNormalRangeIsWhite)
{
	EXPECT_EQ(ClassifyImpactDisplayColor(0.0), EImpactDisplayColor::WHITE);
	EXPECT_EQ(ClassifyImpactDisplayColor(3.999), EImpactDisplayColor::WHITE);
	EXPECT_EQ(ClassifyImpactDisplayColor(2.52), EImpactDisplayColor::WHITE);
}

TEST(ImpactPresentationColor, ImpactGoodRangeIsSaturatedGreen)
{
	EXPECT_EQ(ClassifyImpactDisplayColor(4.0), EImpactDisplayColor::GREEN);
	EXPECT_EQ(ClassifyImpactDisplayColor(5.999), EImpactDisplayColor::GREEN);
}

TEST(ImpactPresentationColor, ImpactExcellentRangeIsPurpleIncludingTen)
{
	EXPECT_EQ(ClassifyImpactDisplayColor(6.0), EImpactDisplayColor::PURPLE);
	EXPECT_EQ(ClassifyImpactDisplayColor(7.02), EImpactDisplayColor::PURPLE);
	EXPECT_EQ(ClassifyImpactDisplayColor(10.0), EImpactDisplayColor::PURPLE);
}

TEST(ImpactPresentationColor, ImpactAboveTenIsGold)
{
	EXPECT_EQ(ClassifyImpactDisplayColor(10.000001), EImpactDisplayColor::GOLD);
	EXPECT_EQ(ClassifyImpactDisplayColor(11.08), EImpactDisplayColor::GOLD);
}

TEST(ImpactPresentationColor, ImpactColorUsesFullPrecisionNotTruncatedDisplay)
{
	EXPECT_EQ(FormatTruncatedImpact(6.009), "+6.00%");
	EXPECT_EQ(ClassifyImpactDisplayColor(6.009), EImpactDisplayColor::PURPLE);
	EXPECT_EQ(FormatTruncatedImpact(10.009), "+10.00%");
	EXPECT_EQ(ClassifyImpactDisplayColor(10.009), EImpactDisplayColor::GOLD);
}

TEST(ImpactPresentationColor, PaletteUsesCentralizedReadableColors)
{
	EXPECT_EQ(GetImpactDisplayColor(EImpactDisplayColor::RED), SImpactPresentationConstants::m_RedColor);
	EXPECT_EQ(GetImpactDisplayColor(EImpactDisplayColor::WHITE), SImpactPresentationConstants::m_WhiteColor);
	EXPECT_EQ(GetImpactDisplayColor(EImpactDisplayColor::GREEN), SImpactPresentationConstants::m_GreenColor);
	EXPECT_EQ(GetImpactDisplayColor(EImpactDisplayColor::PURPLE), SImpactPresentationConstants::m_PurpleColor);
	EXPECT_EQ(GetImpactDisplayColor(EImpactDisplayColor::GOLD), SImpactPresentationConstants::m_GoldColor);
	EXPECT_EQ(GetImpactDisplayColor(-1.0), SImpactPresentationConstants::m_RedColor);
	EXPECT_EQ(GetImpactDisplayColor(0.0), SImpactPresentationConstants::m_WhiteColor);
	EXPECT_EQ(GetImpactDisplayColor(4.0), SImpactPresentationConstants::m_GreenColor);
	EXPECT_EQ(GetImpactDisplayColor(7.0), SImpactPresentationConstants::m_PurpleColor);
	EXPECT_EQ(GetImpactDisplayColor(11.0), SImpactPresentationConstants::m_GoldColor);
	EXPECT_EQ(SImpactPresentationConstants::m_GreenColor, GetRatingFrameStyle(true, 1.20).m_Text);
	EXPECT_EQ(SImpactPresentationConstants::m_PurpleColor, GetRatingFrameStyle(true, 1.50).m_Text);
}

TEST(ImpactPresentationColor, ImpactColorDoesNotAffectScore)
{
	SImpactV1TestInput Input;
	Input.m_Saves = Input.m_IndependentSaves = 10;
	Input.m_Falls = 4;
	const SImpactResult Before = GetImpactV1Result(Input);
	(void)GetImpactDisplayColor(Before.m_FinalImpact);
	const SImpactResult After = GetImpactV1Result(Input);
	EXPECT_DOUBLE_EQ(Before.m_FinalImpact, After.m_FinalImpact);
}

TEST(ImpactPresentationColor, ImpactColorDoesNotAffectRating)
{
	const SRatingResult Before = GetRatingV1Result(7.0261801729998581, 65.0);
	(void)GetImpactDisplayColor(Before.m_Impact);
	const SRatingResult After = GetRatingV1Result(7.0261801729998581, 65.0);
	EXPECT_DOUBLE_EQ(Before.m_FinalRating, After.m_FinalRating);
	EXPECT_EQ(Before.m_Class, After.m_Class);
}

TEST(ImpactPresentationColor, ImpactColorDoesNotAffectSortingInputs)
{
	const std::array<double, 5> aValues = {-3.0, 0.0, 6.009, 10.009, 4.0};
	std::array<double, 5> aSortedBefore = aValues;
	std::sort(aSortedBefore.begin(), aSortedBefore.end());
	for(double Value : aValues)
		(void)GetImpactDisplayColor(Value);
	std::array<double, 5> aSortedAfter = aValues;
	std::sort(aSortedAfter.begin(), aSortedAfter.end());
	EXPECT_EQ(aSortedBefore, aSortedAfter);
}

static SRunSnapshot FinalizedScoreSnapshot(uint64_t RunId, ERunResult Result = ERunResult::FINISHED)
{
	CReplay Replay(RunId, TestContext());
	StartPair(Replay, 0, 10);
	EXPECT_TRUE(Replay.Run().Finalize(Result, 300));
	return Replay.Run().Snapshot(300, 1700000000);
}

static bool FinishScoreTestReplay(CReplay &Replay, uint64_t EventId, int64_t Tick, EImpactEventType Type = EImpactEventType::FINISH_CONFIRMED)
{
	SImpactEvent Finish;
	Finish.m_EventId = EventId;
	Finish.m_Type = Type;
	Finish.m_TeamId = Replay.Run().Context().m_TeamId;
	Finish.m_StartTick = Tick;
	Finish.m_ResolveTick = Tick;
	Finish.m_Confirmed = true;
	STickEventBatch Batch;
	Batch.m_ServerTick = Tick;
	Batch.m_vCandidates.push_back(Finish);
	return Replay.Submit(Batch);
}

TEST(ImpactFinalScore, FinalizedEngagedParticipantsFreezeAcceptedScorerOutputs)
{
	CReplay Replay(7001, TestContext());
	StartPair(Replay, 0, 10);
	SRawStats Delta;
	Delta.m_Saves = 2;
	ASSERT_TRUE(SubmitParticipantStats(Replay, 3, EImpactEventType::SAVE_CONFIRMED, TestId(1), 40, Delta, TestId(2)));
	Delta = {};
	Delta.m_Falls = 1;
	ASSERT_TRUE(SubmitParticipantStats(Replay, 4, EImpactEventType::FALL_CONFIRMED, TestId(2), 50, Delta, TestId(2)));
	ASSERT_TRUE(FinishScoreTestReplay(Replay, 5, 300));
	const SRunSnapshot Final = Replay.Run().Snapshot(300);
	CRunScoringContext Context;
	ASSERT_TRUE(BuildPhase4ScoringFeatures(Final, {}, &Context));
	ASSERT_EQ(Context.m_FinalizedEngagedParticipantCount, 2U);
	for(const SPlayerScoringFeatures &Player : Context.m_vPlayers)
	{
		if(!Player.m_InFinalizedScoringCohort)
			continue;
		const SParticipant *pParticipant = FindSnapshotParticipant(Final, Player.m_Id);
		ASSERT_NE(pParticipant, nullptr);
		const SScoreSnapshot &Score = pParticipant->m_Score;
		ASSERT_TRUE(Score.m_Frozen);
		ASSERT_TRUE(Score.m_Available);
		EXPECT_EQ(Score.m_ScoringVersion, IMPACT_SCORING_VERSION);
		EXPECT_EQ(Score.m_RatingVersion, IMPACT_RATING_VERSION);

		SStabilityResult Stability;
		SImpactResult Impact;
		SRatingResult Rating;
		ASSERT_TRUE(CalculateStabilityV1(Player, Context, &Stability));
		ASSERT_TRUE(CalculateImpactV1(Player, Context, &Impact, &Stability));
		ASSERT_TRUE(CalculateRatingV1(MakeFinalizedScoringDimensions(Impact, Stability), &Rating));
		EXPECT_EQ(Score.m_Impact, Impact.m_FinalImpact);
		EXPECT_EQ(Score.m_Stability, Stability.m_Value);
		EXPECT_EQ(Score.m_Rating, Rating.m_FinalRating);
	}
}

TEST(ImpactFinalScore, NotEngagedRosterMemberFreezesUnavailableNotNeutral)
{
	CReplay Replay(7002, TestContext());
	StartPair(Replay, 0, 10);
	ASSERT_TRUE(Replay.Run().AddRosterMember(TestId(3), "roster-only", 20, "test roster member"));
	ASSERT_TRUE(Replay.Run().Finalize(ERunResult::ABORTED_SERVER_CHANGE, 300));
	const SRunSnapshot Final = Replay.Run().Snapshot(300);
	const SParticipant *pNotEngaged = FindSnapshotParticipant(Final, TestId(3));
	ASSERT_NE(pNotEngaged, nullptr);
	EXPECT_FALSE(pNotEngaged->m_Engaged);
	EXPECT_TRUE(pNotEngaged->m_Score.m_Frozen);
	EXPECT_FALSE(pNotEngaged->m_Score.m_Available);
	EXPECT_EQ(pNotEngaged->m_Score.m_ScoringVersion, IMPACT_SCORING_VERSION);
}

TEST(ImpactFinalScore, DuplicateTerminalSignalDoesNotRecomputeAndSnapshotIsImmutable)
{
	CReplay Replay(7003, TestContext());
	StartPair(Replay, 0, 10);
	ASSERT_TRUE(Replay.Run().Finalize(ERunResult::FINISHED, 300));
	const SRunSnapshot Before = Replay.Run().Snapshot(300);
	const SParticipant *pBefore = FindSnapshotParticipant(Before, TestId(1));
	ASSERT_NE(pBefore, nullptr);
	const SScoreSnapshot Frozen = pBefore->m_Score;
	EXPECT_FALSE(Replay.Run().Finalize(ERunResult::TEAM_RESET, 400));
	EXPECT_FALSE(SubmitParticipantStats(Replay, 90, EImpactEventType::SAVE_CONFIRMED, TestId(1), 350, SRawStats{}));
	const SRunSnapshot After = Replay.Run().Snapshot(400);
	const SParticipant *pAfter = FindSnapshotParticipant(After, TestId(1));
	ASSERT_NE(pAfter, nullptr);
	EXPECT_TRUE(pAfter->m_Score.m_Frozen);
	EXPECT_EQ(pAfter->m_Score.m_Available, Frozen.m_Available);
	EXPECT_EQ(pAfter->m_Score.m_ScoringVersion, Frozen.m_ScoringVersion);
	EXPECT_EQ(pAfter->m_Score.m_Impact, Frozen.m_Impact);
	EXPECT_EQ(pAfter->m_Score.m_Stability, Frozen.m_Stability);
	EXPECT_EQ(pAfter->m_Score.m_Rating, Frozen.m_Rating);
}

TEST(ImpactFinalScore, IndividualFinalizationFreezesBeforeTeammatesContinue)
{
	CReplay Replay(7004, TestContext());
	StartPair(Replay, 0, 10);
	StartThirdParticipant(Replay, 20);
	ASSERT_TRUE(FinalizeReplayParticipant(Replay, 4, TestId(1), 30));
	const SRunSnapshot AtIndividualBoundary = Replay.Run().Snapshot(30);
	const SParticipant *pFrozenAtBoundary = FindSnapshotParticipant(AtIndividualBoundary, TestId(1));
	ASSERT_NE(pFrozenAtBoundary, nullptr);
	ASSERT_TRUE(pFrozenAtBoundary->m_Score.m_Frozen);
	ASSERT_TRUE(pFrozenAtBoundary->m_Score.m_Available);
	const SScoreSnapshot Frozen = pFrozenAtBoundary->m_Score;

	SRunSnapshot ResetArchive;
	ASSERT_TRUE(Replay.Run().MakeParticipantResetSnapshot(TestId(1), 17004, 30, 1700000000, &ResetArchive));
	const SParticipant *pResetArchive = FindSnapshotParticipant(ResetArchive, TestId(1));
	ASSERT_NE(pResetArchive, nullptr);
	EXPECT_EQ(pResetArchive->m_Score.m_Rating, Frozen.m_Rating);
	EXPECT_EQ(pResetArchive->m_Score.m_Impact, Frozen.m_Impact);
	EXPECT_EQ(pResetArchive->m_Score.m_Stability, Frozen.m_Stability);

	SRawStats TeammateSave;
	TeammateSave.m_Saves = 5;
	ASSERT_TRUE(SubmitParticipantStats(Replay, 5, EImpactEventType::SAVE_CONFIRMED, TestId(2), 100, TeammateSave, TestId(3)));
	ASSERT_TRUE(Replay.Run().Finalize(ERunResult::FINISHED, 200));
	const SRunSnapshot TeamFinal = Replay.Run().Snapshot(200);
	const SParticipant *pAfterTeamFinal = FindSnapshotParticipant(TeamFinal, TestId(1));
	ASSERT_NE(pAfterTeamFinal, nullptr);
	EXPECT_EQ(pAfterTeamFinal->m_Score.m_Rating, Frozen.m_Rating);
	EXPECT_EQ(pAfterTeamFinal->m_Score.m_Impact, Frozen.m_Impact);
	EXPECT_EQ(pAfterTeamFinal->m_Score.m_Stability, Frozen.m_Stability);
}

TEST(ImpactFinalScore, LeftAndPartialRunKeepFrozenParticipantMetrics)
{
	CReplay Replay(7005, TestContext());
	StartPair(Replay, 0, 10);
	SImpactEvent Left;
	Left.m_EventId = 3;
	Left.m_Type = EImpactEventType::PARTICIPANT_FINALIZED;
	Left.m_Actor = TestId(1);
	Left.m_TeamId = 1;
	Left.m_StartTick = 30;
	Left.m_ResolveTick = 30;
	Left.m_Confirmed = true;
	Left.m_CausalReason = "participant absent from authoritative server roster";
	STickEventBatch Batch;
	Batch.m_ServerTick = 30;
	Batch.m_vCandidates.push_back(Left);
	ASSERT_TRUE(Replay.Submit(Batch));
	const SScoreSnapshot LeftScore = FindRunParticipant(Replay.Run(), TestId(1))->m_Score;
	ASSERT_TRUE(LeftScore.m_Available);
	ASSERT_TRUE(Replay.Run().Finalize(ERunResult::ABORTED_SERVER_CHANGE, 100));
	const SRunSnapshot Partial = Replay.Run().Snapshot(100);
	const SParticipant *pLeft = FindSnapshotParticipant(Partial, TestId(1));
	ASSERT_NE(pLeft, nullptr);
	EXPECT_EQ(pLeft->m_TerminalStatus, EParticipantTerminalStatus::LEFT);
	EXPECT_TRUE(pLeft->m_Score.m_Available);
	EXPECT_EQ(pLeft->m_Score.m_Rating, LeftScore.m_Rating);
	EXPECT_EQ(pLeft->m_Score.m_Impact, LeftScore.m_Impact);
	EXPECT_EQ(pLeft->m_Score.m_Stability, LeftScore.m_Stability);
	EXPECT_TRUE(FindSnapshotParticipant(Partial, TestId(2))->m_Score.m_Available);
}

TEST(ImpactFinalScore, NumericZeroIsAvailableAndNewRunGenerationDoesNotInheritScore)
{
	const SRunSnapshot FirstRun = FinalizedScoreSnapshot(7006);
	const SParticipant *pFirst = FindSnapshotParticipant(FirstRun, TestId(1));
	ASSERT_NE(pFirst, nullptr);
	ASSERT_TRUE(pFirst->m_Score.m_Available);
	EXPECT_DOUBLE_EQ(pFirst->m_Score.m_Impact, 0.0);
	EXPECT_DOUBLE_EQ(pFirst->m_Score.m_Rating, 0.76);

	CReplay NewGeneration(7007, TestContext());
	StartPairWithIds(NewGeneration, TestId(1, 2), TestId(2, 2));
	const SRunSnapshot Active = NewGeneration.Run().Snapshot(20);
	const SParticipant *pNewGeneration = FindSnapshotParticipant(Active, TestId(1, 2));
	ASSERT_NE(pNewGeneration, nullptr);
	EXPECT_FALSE(pNewGeneration->m_Score.m_Available);
	EXPECT_FALSE(pNewGeneration->m_Score.m_Frozen);
}

TEST(ImpactFinalScore, HistoryRoundTripPreservesFullPrecisionScoresAndVersion)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	SRunSnapshot Snapshot = FinalizedScoreSnapshot(7008);
	ASSERT_EQ(Snapshot.m_vParticipants.size(), 2U);
	const std::array<SScoreSnapshot, 3> aExpected = {{
		{true, true, 1.6006270875959445, 7.0261801729998581, 60.7843137254902, 50.0, 0, IMPACT_RATING_VERSION, IMPACT_SCORING_VERSION},
		{true, true, 0.75393746391193861, -0.063044809168170485, 40.74074074074074, 50.0, 0, IMPACT_RATING_VERSION, IMPACT_SCORING_VERSION},
		{true, true, 0.50090080248142099, -3.3790851245995128, 31.668856767411302, 50.0, 0, IMPACT_RATING_VERSION, IMPACT_SCORING_VERSION},
	}};
	// The three persistence-only fixtures use distinct finalized run identities.
	std::vector<SRunSnapshot> History;
	for(size_t i = 0; i < aExpected.size(); ++i)
	{
		SRunSnapshot Entry = i == 0 ? Snapshot : FinalizedScoreSnapshot(7009 + i);
		ASSERT_FALSE(Entry.m_vParticipants.empty());
		Entry.m_vParticipants[0].m_Score = aExpected[i];
		History.push_back(std::move(Entry));
	}
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), History));
	void *pData = nullptr;
	unsigned Length = 0;
	ASSERT_TRUE(Storage->ReadFile("amf_impact/history.json", IStorage::TYPE_SAVE, &pData, &Length));
	ASSERT_NE(pData, nullptr);
	const std::string Json(static_cast<const char *>(pData), Length);
	std::free(pData);
	EXPECT_NE(Json.find("7.0261801729998581"), std::string::npos);
	EXPECT_EQ(Json.find("rating_class"), std::string::npos);
	EXPECT_EQ(Json.find("impact_color"), std::string::npos);
	EXPECT_EQ(Json.find("formatted_impact"), std::string::npos);
	EXPECT_EQ(Json.find("7.02%"), std::string::npos);

	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), aExpected.size());
	for(size_t i = 0; i < aExpected.size(); ++i)
	{
		const SParticipant *pParticipant = FindSnapshotParticipant(Loaded[i], TestId(1));
		ASSERT_NE(pParticipant, nullptr);
		EXPECT_TRUE(pParticipant->m_Score.m_Frozen);
		EXPECT_TRUE(pParticipant->m_Score.m_Available);
		EXPECT_EQ(pParticipant->m_Score.m_ScoringVersion, IMPACT_SCORING_VERSION);
		EXPECT_DOUBLE_EQ(pParticipant->m_Score.m_Impact, aExpected[i].m_Impact);
		EXPECT_DOUBLE_EQ(pParticipant->m_Score.m_Stability, aExpected[i].m_Stability);
		EXPECT_DOUBLE_EQ(pParticipant->m_Score.m_Rating, aExpected[i].m_Rating);
	}
}

TEST(ImpactFinalScore, OldHistoryWithoutScoresLoadsUnavailableWithoutRecalculation)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	SRunSnapshot Legacy = FinalizedScoreSnapshot(7012);
	Legacy.m_vParticipants[0].m_RawStats.m_Saves = 9;
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {Legacy}));
	void *pData = nullptr;
	unsigned Length = 0;
	ASSERT_TRUE(Storage->ReadFile("amf_impact/history.json", IStorage::TYPE_SAVE, &pData, &Length));
	std::string Json(static_cast<const char *>(pData), Length);
	std::free(pData);
	for(const char *pProperty : {"phase4_score", "score"})
		while(RemoveJsonObjectProperty(&Json, pProperty))
	ASSERT_TRUE(WriteTestStorageText(Storage.get(), "amf_impact/history.json", Json));
	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), 1U);
	EXPECT_EQ(Loaded[0].m_vParticipants[0].m_RawStats.m_Saves, 9);
	for(const SParticipant &Participant : Loaded[0].m_vParticipants)
	{
		EXPECT_FALSE(Participant.m_Score.m_Available);
		EXPECT_FALSE(Participant.m_Score.m_Frozen);
		EXPECT_EQ(Participant.m_Score.m_ScoringVersion, IMPACT_SCORING_VERSION_UNSET);
	}
}

TEST(ImpactFinalScore, MalformedAndNonFiniteScoreFieldsDoNotDiscardHistory)
{
	for(const char *pInvalidValue : {"\"corrupt\"", "\"nan\""})
	{
		CTestInfo Info;
		std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
		ASSERT_TRUE(Storage);
		SRunSnapshot Final = FinalizedScoreSnapshot(7013);
		Final.m_vParticipants[0].m_Score.m_Available = true;
		Final.m_vParticipants[0].m_Score.m_Frozen = true;
		Final.m_vParticipants[0].m_Score.m_ScoringVersion = IMPACT_SCORING_VERSION;
		Final.m_vParticipants[0].m_Score.m_Impact = 1.2345678901234567;
		Final.m_vParticipants[0].m_Score.m_Rating = 1.0;
		Final.m_vParticipants[0].m_Score.m_Stability = 50.0;
		ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {Final}));
		void *pData = nullptr;
		unsigned Length = 0;
		ASSERT_TRUE(Storage->ReadFile("amf_impact/history.json", IStorage::TYPE_SAVE, &pData, &Length));
		std::string Json(static_cast<const char *>(pData), Length);
		std::free(pData);
		const size_t ScorePosition = Json.find("\"phase4_score\"");
		ASSERT_NE(ScorePosition, std::string::npos);
		const size_t ImpactPosition = Json.find("\"impact\"", ScorePosition);
		ASSERT_NE(ImpactPosition, std::string::npos);
		const size_t ValueStart = Json.find(':', ImpactPosition) + 1;
		const size_t ValueEnd = Json.find(',', ValueStart);
		ASSERT_NE(ValueEnd, std::string::npos);
		Json.replace(ValueStart, ValueEnd - ValueStart, pInvalidValue);
		ASSERT_TRUE(WriteTestStorageText(Storage.get(), "amf_impact/history.json", Json));
		std::vector<SRunSnapshot> Loaded;
		ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
		ASSERT_EQ(Loaded.size(), 1U);
		EXPECT_EQ(Loaded[0].m_RunId, Final.m_RunId);
		EXPECT_EQ(Loaded[0].m_State, EAnalysisState::FINALIZED);
		EXPECT_FALSE(Loaded[0].m_vParticipants[0].m_Score.m_Available);
		EXPECT_TRUE(Loaded[0].m_vParticipants[0].m_Score.m_Frozen);
	}
}

TEST(ImpactFinalScore, NonFiniteInMemoryScoreIsPersistedOnlyAsUnavailable)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	SRunSnapshot Final = FinalizedScoreSnapshot(7016);
	SScoreSnapshot &Score = Final.m_vParticipants[0].m_Score;
	Score.m_Frozen = true;
	Score.m_Available = true;
	Score.m_ScoringVersion = IMPACT_SCORING_VERSION;
	Score.m_Rating = std::numeric_limits<double>::infinity();
	Score.m_Impact = std::numeric_limits<double>::quiet_NaN();
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {Final}));
	void *pData = nullptr;
	unsigned Length = 0;
	ASSERT_TRUE(Storage->ReadFile("amf_impact/history.json", IStorage::TYPE_SAVE, &pData, &Length));
	const std::string Json(static_cast<const char *>(pData), Length);
	std::free(pData);
	EXPECT_EQ(Json.find("nan"), std::string::npos);
	EXPECT_EQ(Json.find("inf"), std::string::npos);
	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), 1U);
	EXPECT_TRUE(Loaded[0].m_vParticipants[0].m_Score.m_Frozen);
	EXPECT_FALSE(Loaded[0].m_vParticipants[0].m_Score.m_Available);
}

TEST(ImpactFinalScore, RatingImpactOrderingAndParticipantGenerationSurviveHistoryRoundTrip)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	SRunSnapshot First = FinalizedScoreSnapshot(7014);
	SRunSnapshot Second = FinalizedScoreSnapshot(7015);
	First.m_vParticipants[0].m_Id = TestId(4, 7);
	Second.m_vParticipants[0].m_Id = TestId(4, 8);
	First.m_vParticipants[0].m_Score.m_Frozen = true;
	First.m_vParticipants[0].m_Score.m_Available = true;
	First.m_vParticipants[0].m_Score.m_ScoringVersion = IMPACT_SCORING_VERSION;
	First.m_vParticipants[0].m_Score.m_Impact = 5.000000001;
	First.m_vParticipants[0].m_Score.m_Rating = 1.4;
	First.m_vParticipants[0].m_Score.m_Stability = 60.0;
	Second.m_vParticipants[0].m_Score.m_Frozen = true;
	Second.m_vParticipants[0].m_Score.m_Available = true;
	Second.m_vParticipants[0].m_Score.m_ScoringVersion = IMPACT_SCORING_VERSION;
	Second.m_vParticipants[0].m_Score.m_Impact = 5.000000002;
	Second.m_vParticipants[0].m_Score.m_Rating = 1.4000000001;
	Second.m_vParticipants[0].m_Score.m_Stability = 60.0;
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {First, Second}));
	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), 2U);
	const SParticipant *pGeneration7 = FindSnapshotParticipant(Loaded[0], TestId(4, 7));
	const SParticipant *pGeneration8 = FindSnapshotParticipant(Loaded[1], TestId(4, 8));
	ASSERT_NE(pGeneration7, nullptr);
	ASSERT_NE(pGeneration8, nullptr);
	EXPECT_EQ(pGeneration7->m_Score.m_Impact, First.m_vParticipants[0].m_Score.m_Impact);
	EXPECT_EQ(pGeneration8->m_Score.m_Impact, Second.m_vParticipants[0].m_Score.m_Impact);
	EXPECT_LT(pGeneration7->m_Score.m_Rating, pGeneration8->m_Score.m_Rating);
	EXPECT_LT(pGeneration7->m_Score.m_Impact, pGeneration8->m_Score.m_Impact);
}

TEST(ImpactUiIntegration, ActiveRunKeepsAllFinalMetricsAndMvpUnavailable)
{
	SParticipant Participant = MakeRankingParticipant(1, EParticipantTerminalStatus::NOT_FINALIZED, true, 1.6, 7.0, 60.0);
	SImpactScoreDisplay Display = BuildImpactScoreDisplay(Participant, false);
	EXPECT_FALSE(Display.m_Available);
	EXPECT_FALSE(Display.m_HasImpactColor);
	EXPECT_EQ(Display.m_Rating, "—");
	EXPECT_EQ(Display.m_Impact, "—");
	EXPECT_EQ(Display.m_Stability, "—");

	SRunSnapshot Active;
	Active.m_State = EAnalysisState::ACTIVE;
	Active.m_Result = ERunResult::NONE;
	Active.m_vParticipants.push_back(Participant);
	const SImpactRunPresentation RunView = BuildImpactRunPresentation(Active);
	EXPECT_FALSE(RunView.m_Finalized);
	EXPECT_TRUE(RunView.m_vRanking.empty());
	EXPECT_FALSE(RunView.m_HasMvp);
}

TEST(ImpactUiIntegration, FinalizedRunDisplaysExactFrozenScoreThroughCanonicalFormatters)
{
	SParticipant Participant = MakeRankingParticipant(1, EParticipantTerminalStatus::FINISHED, true,
		1.6006270875959445, 7.0261801729998581, 60.7843137254902);
	const SImpactScoreDisplay Display = BuildImpactScoreDisplay(Participant, true);
	ASSERT_TRUE(Display.m_Available);
	EXPECT_EQ(Display.m_Rating, "1.60");
	EXPECT_EQ(Display.m_Impact, "+7.02%");
	EXPECT_EQ(Display.m_Stability, "60%");
	EXPECT_TRUE(Display.m_HasImpactColor);
	EXPECT_EQ(Display.m_ImpactColor, EImpactDisplayColor::PURPLE);
	EXPECT_DOUBLE_EQ(Participant.m_Score.m_Impact, 7.0261801729998581);
}

TEST(ImpactUiIntegration, RatingFrameUsesCanonicalFullPrecisionClasses)
{
	EXPECT_EQ(GetRatingFrameStyle(true, 0.50).m_Class, ERatingClass::LOW);
	EXPECT_EQ(GetRatingFrameStyle(true, 0.75).m_Class, ERatingClass::MEDIUM);
	EXPECT_EQ(GetRatingFrameStyle(true, 1.20).m_Class, ERatingClass::GOOD);
	EXPECT_EQ(GetRatingFrameStyle(true, 1.50).m_Class, ERatingClass::EXCELLENT);
	EXPECT_EQ(GetRatingFrameStyle(true, 1.80).m_Class, ERatingClass::OUTSTANDING);
	EXPECT_EQ(GetRatingFrameStyle(true, 2.10).m_Class, ERatingClass::STRONG_IMPACT);
}

TEST(ImpactUiIntegration, RatingFramePaletteUsesDistinctTierAccents)
{
	const auto Low = GetRatingFrameStyle(true, 0.50);
	const auto Medium = GetRatingFrameStyle(true, 0.75);
	const auto Good = GetRatingFrameStyle(true, 1.20);
	const auto Excellent = GetRatingFrameStyle(true, 1.50);
	const auto Outstanding = GetRatingFrameStyle(true, 1.80);
	const auto Strong = GetRatingFrameStyle(true, 2.10);
	EXPECT_GT(Low.m_Border.z, Low.m_Border.x); // cold blue steel
	EXPECT_GT(Medium.m_Border.x, 0.6f); // silver/gray
	EXPECT_GT(Good.m_Border.y, Good.m_Border.x); // green
	EXPECT_GT(Excellent.m_Border.z, Excellent.m_Border.y); // purple
	EXPECT_GT(Outstanding.m_Border.x, Outstanding.m_Border.y); // gold
	EXPECT_GT(Strong.m_Border.x, Strong.m_Border.z); // gold at the reference's 2.00 tier
	EXPECT_GT(Strong.m_Text.x, 0.95f); // light value on dark glass
}

TEST(ImpactUiIntegration, RatingFrameUsesFullPrecisionNotDisplayedValue)
{
	const double ExcellentRating = 1.6006270875959445;
	const double OutstandingRating = 1.611;
	EXPECT_EQ(FormatTruncatedRating(ExcellentRating), "1.60");
	EXPECT_EQ(FormatTruncatedRating(OutstandingRating), "1.61");
	EXPECT_EQ(GetRatingFrameStyle(true, ExcellentRating).m_Class, ERatingClass::EXCELLENT);
	EXPECT_EQ(GetRatingFrameStyle(true, OutstandingRating).m_Class, ERatingClass::OUTSTANDING);
}

TEST(ImpactUiIntegration, UnavailableRatingUsesNeutralFrame)
{
	const auto Style = GetRatingFrameStyle(false, 2.10);
	EXPECT_EQ(Style.m_Class, ERatingClass::UNAVAILABLE);
	EXPECT_GT(Style.m_Border.z, Style.m_Border.x);
	EXPECT_LT(Style.m_Border.x, 0.5f);
}

TEST(ImpactUiIntegration, RatingStarsUseExactCanonicalClass)
{
	EXPECT_EQ(RatingStarCount(true, 0.50), 0);
	EXPECT_EQ(RatingStarCount(true, 0.67), 1);
	EXPECT_EQ(RatingStarCount(true, 0.76), 1);
	EXPECT_EQ(RatingStarCount(true, 0.75), 1);
	EXPECT_EQ(RatingStarCount(true, 1.20), 2);
	EXPECT_EQ(RatingStarCount(true, 1.6006270875959445), 3);
	EXPECT_EQ(RatingStarCount(true, 1.61), 4);
	EXPECT_EQ(RatingStarCount(true, 2.00), 5);
	EXPECT_EQ(RatingStarCount(false, 2.00), 0);
	EXPECT_EQ(RatingStarCount(true, std::numeric_limits<double>::quiet_NaN()), 0);
}

TEST(ImpactUiIntegration, RatingStarPaletteKeepsFilledAndEmptyStarsDistinct)
{
	EXPECT_EQ(RatingStarTextColor(true), ColorRGBA(0.780392f, 0.823529f, 0.886275f, 1.0f));
	EXPECT_EQ(RatingStarTextColor(false), ColorRGBA(0.203922f, 0.254902f, 0.313725f, 1.0f));
	EXPECT_NE(RatingStarTextColor(true), RatingStarTextColor(false));
	EXPECT_EQ(RatingStarCount(true, 0.67), 1);
	EXPECT_EQ(RatingStarCount(true, 0.76), 1);
}

TEST(ImpactUiIntegration, GoodRatingTextUsesSaturatedGreen)
{
	const ColorRGBA Color = GetRatingFrameStyle(true, 1.20).m_Text;
	EXPECT_EQ(Color, ColorRGBA(0.30f, 0.90f, 0.30f, 1.0f));
	EXPECT_GT(Color.g, Color.b);
	EXPECT_GT(Color.g, Color.r);
}

TEST(ImpactUiIntegration, RatingTextUsesCanonicalRedWhiteGreenPlatinumAndGold)
{
	EXPECT_EQ(GetRatingFrameStyle(true, 0.50).m_Text, GetImpactDisplayColor(EImpactDisplayColor::RED));
	EXPECT_EQ(GetRatingFrameStyle(true, 0.75).m_Text, GetImpactDisplayColor(EImpactDisplayColor::WHITE));
	EXPECT_EQ(GetRatingFrameStyle(true, 1.20).m_Text, ColorRGBA(0.30f, 0.90f, 0.30f, 1.0f));
	const ColorRGBA Platinum = GetRatingFrameStyle(true, 1.80).m_Text;
	EXPECT_LT(Platinum.r, 1.0f);
	EXPECT_GT(Platinum.b, Platinum.r);
	EXPECT_EQ(GetRatingFrameStyle(true, 2.10).m_Text, GetImpactDisplayColor(EImpactDisplayColor::GOLD));
}

TEST(ImpactUiIntegration, RatingTooltipCacheKeyDoesNotAliasStabilityKey)
{
	SParticipant Participant;
	const void *pPreviousRatingTooltipId = &Participant;
	const void *pRatingTooltipId = &Participant.m_Name;
	const void *pStabilityTooltipId = &Participant.m_Id;
	EXPECT_EQ(pPreviousRatingTooltipId, pStabilityTooltipId);
	EXPECT_NE(pRatingTooltipId, pStabilityTooltipId);
}

TEST(ImpactUiIntegration, RatingTooltipHitRectUsesMeasuredTextAndStaysClearOfStars)
{
	const CUIRect RatingLine{10.0f, 20.0f, 100.0f, 20.0f};
	const CUIRect HitRect = MakeRatingTooltipHitRect(RatingLine, 24.0f, 8.0f);
	EXPECT_GT(HitRect.w, 0.0f);
	EXPECT_GT(HitRect.h, 0.0f);
	EXPECT_FLOAT_EQ(HitRect.Center().x, RatingLine.Center().x);
	EXPECT_FLOAT_EQ(HitRect.Center().y, RatingLine.Center().y);
	EXPECT_FLOAT_EQ(HitRect.w, 26.0f);
	EXPECT_FLOAT_EQ(HitRect.h, 10.0f);
	const float FirstStarGlyphTop = RatingLine.y + 15.5f;
	EXPECT_LE(HitRect.y + HitRect.h, FirstStarGlyphTop);
}

TEST(ImpactUiIntegration, ImpactTooltipHitRectsAreTextSizedForTableAndMvpValues)
{
	const CUIRect TableCell{20.0f, 30.0f, 100.0f, 24.0f};
	const CUIRect TableHitRect = MakeStatisticTooltipHitRect(TableCell, 34.0f, 10.0f);
	EXPECT_GT(TableHitRect.w, 0.0f);
	EXPECT_GT(TableHitRect.h, 0.0f);
	EXPECT_LT(TableHitRect.w, TableCell.w);
	EXPECT_LT(TableHitRect.h, TableCell.h);
	EXPECT_FLOAT_EQ(TableHitRect.Center().x, TableCell.Center().x);
	EXPECT_FLOAT_EQ(TableHitRect.Center().y, TableCell.Center().y);

	const CUIRect MvpValueWell{150.0f, 70.0f, 120.0f, 28.0f};
	const CUIRect MvpHitRect = MakeStatisticTooltipHitRect(MvpValueWell, 48.0f, 15.0f);
	EXPECT_GT(MvpHitRect.w, 0.0f);
	EXPECT_GT(MvpHitRect.h, 0.0f);
	EXPECT_LT(MvpHitRect.w, MvpValueWell.w);
	EXPECT_LT(MvpHitRect.h, MvpValueWell.h);
	EXPECT_FLOAT_EQ(MvpHitRect.Center().x, MvpValueWell.Center().x);
	EXPECT_FLOAT_EQ(MvpHitRect.Center().y, MvpValueWell.Center().y);
}

TEST(ImpactUiIntegration, ImpactRatingAndStabilityTooltipTextUsesAsciiHyphens)
{
	EXPECT_STREQ(gs_aImpactTooltipText[0], "Воздействие");
	EXPECT_STREQ(gs_aImpactTooltipText[1], "< 0.00% - Ужасное");
	EXPECT_STREQ(gs_aImpactTooltipText[2], "0.00-3.99% - Нормальное");
	EXPECT_STREQ(gs_aImpactTooltipText[3], "4.00-5.99% - Хорошее");
	EXPECT_STREQ(gs_aImpactTooltipText[4], "6.00-10.00% - Отличное");
	EXPECT_STREQ(gs_aImpactTooltipText[5], "> 10.00% - Выдающееся");

	EXPECT_STREQ(gs_aRatingTooltipText[0], "Рейтинг");
	EXPECT_STREQ(gs_aRatingTooltipText[1], "0.00-0.59 - Низкий");
	EXPECT_STREQ(gs_aRatingTooltipText[2], "0.60-1.00 - Средний");
	EXPECT_STREQ(gs_aRatingTooltipText[3], "1.01-1.39 - Хороший");
	EXPECT_STREQ(gs_aRatingTooltipText[4], "1.40-1.60 - Отличный");
	EXPECT_STREQ(gs_aRatingTooltipText[5], "1.61-1.99 - Превосходный");
	EXPECT_STREQ(gs_aRatingTooltipText[6], "2.00+ - Сильное воздействие");

	EXPECT_STREQ(gs_aStabilityTooltipText[0], "Стабильность");
	EXPECT_STREQ(gs_aStabilityTooltipText[1], "0-40% - Плохая");
	EXPECT_STREQ(gs_aStabilityTooltipText[2], "41-59% - Хорошая");
	EXPECT_STREQ(gs_aStabilityTooltipText[3], "60-75% - Отличная");
	EXPECT_STREQ(gs_aStabilityTooltipText[4], "76-100% - Выдающаяся");
}

TEST(ImpactUiIntegration, StabilityTextColorUsesExactScoreNotDisplay)
{
	const auto Red = GetStabilityTextColor(true, 40.99);
	const auto White = GetStabilityTextColor(true, 41.0);
	const auto WhiteUpper = GetStabilityTextColor(true, 59.99);
	const auto Green = GetStabilityTextColor(true, 60.0);
	const auto GreenUpper = GetStabilityTextColor(true, 75.99);
	const auto Gold = GetStabilityTextColor(true, 76.0);
	EXPECT_GT(Red.r, Red.g);
	EXPECT_EQ(White, GetImpactDisplayColor(EImpactDisplayColor::WHITE));
	EXPECT_EQ(White, WhiteUpper);
	EXPECT_GT(Green.g, Green.r);
	EXPECT_FLOAT_EQ(Green.g, GreenUpper.g);
	EXPECT_GT(Gold.r, Gold.b);
	EXPECT_FLOAT_EQ(GetStabilityTextColor(false, 76.0).r, GetStabilityTextColor(false, 0.0).r);
	EXPECT_FLOAT_EQ(GetStabilityTextColor(true, std::numeric_limits<double>::infinity()).r, GetStabilityTextColor(false, 0.0).r);
}

TEST(ImpactUiIntegration, TableStabilityUsesWhiteOnlyForExactFortyOneToFiftyNine)
{
	const auto White = GetTableStabilityTextColor(true, 41.0);
	EXPECT_FLOAT_EQ(White.r, 1.0f);
	EXPECT_FLOAT_EQ(White.g, 1.0f);
	EXPECT_FLOAT_EQ(White.b, 1.0f);
	EXPECT_FLOAT_EQ(GetTableStabilityTextColor(true, 59.99).r, 1.0f);
	EXPECT_FLOAT_EQ(GetTableStabilityTextColor(true, 40.99).r, GetStabilityTextColor(true, 40.99).r);
	EXPECT_FLOAT_EQ(GetTableStabilityTextColor(true, 60.0).r, GetStabilityTextColor(true, 60.0).r);
	EXPECT_FLOAT_EQ(GetTableStabilityTextColor(false, 52.0).r, GetStabilityTextColor(false, 52.0).r);
	EXPECT_EQ(GetStabilityTextColor(true, 52.0), GetImpactDisplayColor(EImpactDisplayColor::WHITE));
}

TEST(ImpactUiIntegration, CosmeticSnapshotDoesNotChangeScoreOrCanonicalMvp)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::FINISHED, true, 1.6006270875959445,
		7.0261801729998581, 60.7843137254902);
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::FINISHED, true, 0.75393746391193861,
		-0.063044809168170485, 40.74074074074074);
	Snapshot.m_vParticipants[0].m_Name = "Alya";
	Snapshot.m_vParticipants[0].m_Cosmetics = {true, "mvp_skin", true, 0x123456, 0x654321};
	const SImpactRunPresentation Before = BuildImpactRunPresentation(Snapshot);
	const SScoreSnapshot FrozenScore = Snapshot.m_vParticipants[0].m_Score;
	Snapshot.m_vParticipants[0].m_Cosmetics = {true, "different_skin", false, 0, 0};
	const SImpactRunPresentation After = BuildImpactRunPresentation(Snapshot);
	ASSERT_TRUE(Before.m_HasMvp);
	ASSERT_TRUE(After.m_HasMvp);
	EXPECT_EQ(Before.m_MvpParticipantIndex, After.m_MvpParticipantIndex);
	EXPECT_EQ(Before.m_vDisplayOrder, After.m_vDisplayOrder);
	EXPECT_DOUBLE_EQ(Snapshot.m_vParticipants[0].m_Score.m_Rating, FrozenScore.m_Rating);
	EXPECT_DOUBLE_EQ(Snapshot.m_vParticipants[0].m_Score.m_Impact, FrozenScore.m_Impact);
	EXPECT_DOUBLE_EQ(Snapshot.m_vParticipants[0].m_Score.m_Stability, FrozenScore.m_Stability);
}

TEST(ImpactUiIntegration, HistoryMvpUsesPersistedParticipantCosmeticsAndNickname)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	SRunSnapshot Snapshot = FinalizedScoreSnapshot(8003);
	const SImpactRunPresentation Before = BuildImpactRunPresentation(Snapshot);
	ASSERT_TRUE(Before.m_HasMvp);
	ASSERT_LT(Before.m_MvpParticipantIndex, Snapshot.m_vParticipants.size());
	SParticipant &MvpSnapshot = Snapshot.m_vParticipants[Before.m_MvpParticipantIndex];
	MvpSnapshot.m_Name = "Persisted MVP name";
	MvpSnapshot.m_Cosmetics = {true, "persisted_skin", true, 0x123456, 0x654321};
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {Snapshot}));
	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), 1U);
	const SImpactRunPresentation View = BuildImpactRunPresentation(Loaded[0]);
	ASSERT_TRUE(View.m_HasMvp);
	const SParticipant &Mvp = Loaded[0].m_vParticipants[View.m_MvpParticipantIndex];
	EXPECT_EQ(Mvp.m_Name, "Persisted MVP name");
	ASSERT_TRUE(Mvp.m_Cosmetics.m_Available);
	EXPECT_EQ(Mvp.m_Cosmetics.m_SkinName, "persisted_skin");
	EXPECT_TRUE(Mvp.m_Cosmetics.m_UseCustomColor);
	EXPECT_EQ(Mvp.m_Cosmetics.m_ColorBody, 0x123456);
	EXPECT_EQ(Mvp.m_Cosmetics.m_ColorFeet, 0x654321);
}

#if defined(CONF_DEBUG)
TEST(ImpactUiIntegration, DebugHistoryPreviewUsesFrozenValuesAndCanonicalRanking)
{
	const SRunSnapshot Snapshot = BuildDebugHistoryPreviewSnapshot();
	ASSERT_TRUE(IsDebugHistoryPreview(Snapshot));
	EXPECT_EQ(Snapshot.m_Context.m_Map, "Square_of_Gores_3");
	EXPECT_EQ(Snapshot.m_Context.m_MapDifficulty, "Extreme");
	EXPECT_EQ(Snapshot.m_Context.m_MapStars, 3);
	EXPECT_EQ(MapDifficultyDisplayName(Snapshot.m_Context), "Extreme · ★★★");
	EXPECT_EQ(Snapshot.m_SavedAt, DEBUG_HISTORY_PREVIEW_SAVED_AT);
	EXPECT_EQ(Snapshot.m_State, EAnalysisState::FINALIZED);
	EXPECT_EQ(Snapshot.m_Result, ERunResult::FINISHED);
	EXPECT_EQ(Snapshot.m_AuthoritativeFinishTimeMillis, 4773000);
	EXPECT_EQ(Snapshot.m_AuthoritativeFinishTimeMillis / 1000, 4773);
	ASSERT_EQ(Snapshot.m_vParticipants.size(), 3U);

	EXPECT_EQ(Snapshot.m_vParticipants[0].m_Name, "alya?");
	EXPECT_EQ(Snapshot.m_vParticipants[1].m_Name, "AshuraHX");
	EXPECT_EQ(Snapshot.m_vParticipants[2].m_Name, "mordıser");
	for(const SParticipant &Participant : Snapshot.m_vParticipants)
	{
		EXPECT_TRUE(Participant.m_Engaged);
		EXPECT_TRUE(Participant.m_Finalized);
		EXPECT_EQ(Participant.m_TerminalStatus, EParticipantTerminalStatus::FINISHED);
		EXPECT_TRUE(HasUsableFrozenFinalScore(Participant));
		EXPECT_TRUE(Participant.m_Cosmetics.m_Available);
		EXPECT_EQ(Participant.m_Cosmetics.m_SkinName, "default");
	}

	const SImpactRunPresentation Presentation = BuildImpactRunPresentation(Snapshot);
	ASSERT_TRUE(Presentation.m_Finalized);
	ASSERT_EQ(Presentation.m_vRanking.size(), 3U);
	EXPECT_EQ(Presentation.m_vDisplayOrder, (std::vector<size_t>{0, 2, 1}));
	EXPECT_EQ(Presentation.m_vPlaces, (std::vector<int>{1, 3, 2}));
	EXPECT_EQ(Presentation.m_vRanking[0].m_ParticipantIndex, 0U);
	EXPECT_EQ(Presentation.m_vRanking[1].m_ParticipantIndex, 2U);
	EXPECT_EQ(Presentation.m_vRanking[2].m_ParticipantIndex, 1U);
	ASSERT_TRUE(Presentation.m_HasMvp);
	EXPECT_EQ(Presentation.m_MvpParticipantIndex, 0U);

	const SImpactScoreDisplay MvpDisplay = BuildImpactScoreDisplay(Snapshot.m_vParticipants[Presentation.m_MvpParticipantIndex], true);
	EXPECT_EQ(MvpDisplay.m_Rating, "2.11");
	EXPECT_EQ(MvpDisplay.m_Impact, "+11.24%");
	EXPECT_EQ(MvpDisplay.m_Stability, "85%");
	EXPECT_EQ(Snapshot.m_vParticipants[0].m_RawStats.m_Saves, 143);
	EXPECT_EQ(Snapshot.m_vParticipants[0].m_RawStats.m_Aleds, 13);
	EXPECT_EQ(Snapshot.m_vParticipants[0].m_RawStats.m_Clutches, 51);
	EXPECT_EQ(Snapshot.m_vParticipants[0].m_RawStats.m_Falls, 45);
}
#endif

TEST(ImpactUiIntegration, OldHistoryWithoutCosmeticsStillLoadsWithUnavailableSnapshot)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	SRunSnapshot Snapshot = FinalizedScoreSnapshot(8004);
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {Snapshot}));
	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), 1U);
	EXPECT_FALSE(Loaded[0].m_vParticipants[0].m_Cosmetics.m_Available);
	const SImpactRunPresentation View = BuildImpactRunPresentation(Loaded[0]);
	EXPECT_TRUE(View.m_HasMvp);
}

TEST(ImpactUiIntegration, CosmeticSnapshotRemainsBoundToParticipantGeneration)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	CReplay Replay(8005, TestContext());
	const SParticipantId OlderId = TestId(4, 7);
	const SParticipantId NewerId = TestId(4, 8);
	StartPairWithIds(Replay, OlderId, NewerId);
	ASSERT_TRUE(Replay.Run().Finalize(ERunResult::FINISHED, 300));
	SRunSnapshot Snapshot = Replay.Run().Snapshot(300, 1700000000);
	auto OlderIt = std::find_if(Snapshot.m_vParticipants.begin(), Snapshot.m_vParticipants.end(), [&OlderId](const SParticipant &Participant) {
		return Participant.m_Id == OlderId;
	});
	auto NewerIt = std::find_if(Snapshot.m_vParticipants.begin(), Snapshot.m_vParticipants.end(), [&NewerId](const SParticipant &Participant) {
		return Participant.m_Id == NewerId;
	});
	ASSERT_NE(OlderIt, Snapshot.m_vParticipants.end());
	ASSERT_NE(NewerIt, Snapshot.m_vParticipants.end());
	OlderIt->m_Cosmetics = {true, "older_skin", false, 0, 0};
	NewerIt->m_Cosmetics = {true, "newer_skin", true, 101, 202};
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {Snapshot}));
	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), 1U);
	const SParticipant *pOlder = FindSnapshotParticipant(Loaded[0], TestId(4, 7));
	const SParticipant *pNewer = FindSnapshotParticipant(Loaded[0], TestId(4, 8));
	ASSERT_NE(pOlder, nullptr);
	ASSERT_NE(pNewer, nullptr);
	EXPECT_EQ(pOlder->m_Cosmetics.m_SkinName, "older_skin");
	EXPECT_EQ(pNewer->m_Cosmetics.m_SkinName, "newer_skin");
}

TEST(ImpactUiIntegration, FinalizedRunPresentationUsesCanonicalRankingAndMvp)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 3, EParticipantTerminalStatus::FINISHED, true, 0.50090080248142099,
		-3.3790851245995128, 31.668856767411302);
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::FINISHED, true, 0.75393746391193861,
		-0.063044809168170485, 40.74074074074074);
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::FINISHED, true, 1.6006270875959445,
		7.0261801729998581, 60.7843137254902);
	Snapshot.m_vParticipants[0].m_Name = "Joland";
	Snapshot.m_vParticipants[1].m_Name = "AshuraHX";
	Snapshot.m_vParticipants[2].m_Name = "Alya";

	const SImpactRunPresentation View = BuildImpactRunPresentation(Snapshot);
	ASSERT_TRUE(View.m_Finalized);
	ASSERT_EQ(View.m_vRanking.size(), 3U);
	ASSERT_EQ(View.m_vDisplayOrder, (std::vector<size_t>{2, 1, 0}));
	ASSERT_EQ(View.m_vPlaces, (std::vector<int>{3, 2, 1}));
	ASSERT_TRUE(View.m_HasMvp);
	EXPECT_EQ(View.m_MvpParticipantIndex, 2U);
	EXPECT_EQ(Snapshot.m_vParticipants[View.m_MvpParticipantIndex].m_Name, "Alya");
	const SImpactScoreDisplay AlyaDisplay = BuildImpactScoreDisplay(Snapshot.m_vParticipants[2], View.m_Finalized);
	const SImpactScoreDisplay AshuraDisplay = BuildImpactScoreDisplay(Snapshot.m_vParticipants[1], View.m_Finalized);
	const SImpactScoreDisplay JolandDisplay = BuildImpactScoreDisplay(Snapshot.m_vParticipants[0], View.m_Finalized);
	EXPECT_EQ(AlyaDisplay.m_Rating, "1.60");
	EXPECT_EQ(AlyaDisplay.m_Impact, "+7.02%");
	EXPECT_EQ(AlyaDisplay.m_Stability, "60%");
	EXPECT_EQ(AlyaDisplay.m_ImpactColor, EImpactDisplayColor::PURPLE);
	EXPECT_EQ(AshuraDisplay.m_Rating, "0.75");
	EXPECT_EQ(AshuraDisplay.m_Impact, "-0.06%");
	EXPECT_EQ(AshuraDisplay.m_Stability, "40%");
	EXPECT_EQ(AshuraDisplay.m_ImpactColor, EImpactDisplayColor::RED);
	EXPECT_EQ(JolandDisplay.m_Rating, "0.50");
	EXPECT_EQ(JolandDisplay.m_Impact, "-3.37%");
	EXPECT_EQ(JolandDisplay.m_Stability, "31%");
	EXPECT_EQ(JolandDisplay.m_ImpactColor, EImpactDisplayColor::RED);
}

TEST(ImpactUiIntegration, LeftRetainsPlaceByScoreButCannotBeMvp)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::LEFT, true, 1.6006270875959445,
		7.0261801729998581, 60.7843137254902);
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::FINISHED, true, 0.75393746391193861,
		-0.063044809168170485, 40.74074074074074);
	AddRankingParticipant(Snapshot, 3, EParticipantTerminalStatus::FINISHED, true, 0.50090080248142099,
		-3.3790851245995128, 31.668856767411302);
	Snapshot.m_vParticipants[0].m_Name = "Alya";

	const SImpactRunPresentation View = BuildImpactRunPresentation(Snapshot);
	ASSERT_EQ(View.m_vDisplayOrder, (std::vector<size_t>{0, 1, 2}));
	ASSERT_EQ(View.m_vPlaces, (std::vector<int>{1, 2, 3}));
	ASSERT_TRUE(View.m_HasMvp);
	EXPECT_EQ(View.m_MvpParticipantIndex, 1U);
	const SImpactScoreDisplay LeftDisplay = BuildImpactScoreDisplay(Snapshot.m_vParticipants[0], View.m_Finalized);
	EXPECT_TRUE(LeftDisplay.m_Available);
	EXPECT_EQ(LeftDisplay.m_Rating, "1.60");
	EXPECT_EQ(LeftDisplay.m_Impact, "+7.02%");
	EXPECT_EQ(LeftDisplay.m_Stability, "60%");
	EXPECT_FALSE(View.m_vRanking[2].m_IsMvp);
}

TEST(ImpactUiIntegration, UnavailableHistoryScoreStaysUnavailableAndHasNoImpactColor)
{
	SParticipant OldHistory = MakeRankingParticipant(1, EParticipantTerminalStatus::FINISHED, false, 0.0, 0.0, 50.0);
	OldHistory.m_Score = {};
	OldHistory.m_RawStats.m_Saves = 23;
	OldHistory.m_RawStats.m_Falls = 5;
	const SImpactScoreDisplay Display = BuildImpactScoreDisplay(OldHistory, true);
	EXPECT_FALSE(Display.m_Available);
	EXPECT_FALSE(Display.m_HasImpactColor);
	EXPECT_EQ(Display.m_Rating, "—");
	EXPECT_EQ(Display.m_Impact, "—");
	EXPECT_EQ(Display.m_Stability, "—");
}

TEST(ImpactUiIntegration, ImpactColorUsesRawFrozenValueAtDisplayBoundaries)
{
	const auto DisplayFor = [](double Impact) {
		SParticipant Participant = MakeRankingParticipant(1, EParticipantTerminalStatus::FINISHED, true, 1.0, Impact, 50.0);
		return BuildImpactScoreDisplay(Participant, true);
	};
	EXPECT_EQ(DisplayFor(-0.01).m_ImpactColor, EImpactDisplayColor::RED);
	EXPECT_EQ(DisplayFor(0.0).m_ImpactColor, EImpactDisplayColor::WHITE);
	EXPECT_EQ(DisplayFor(3.999).m_ImpactColor, EImpactDisplayColor::WHITE);
	EXPECT_EQ(DisplayFor(4.0).m_ImpactColor, EImpactDisplayColor::GREEN);
	EXPECT_EQ(DisplayFor(5.999).m_ImpactColor, EImpactDisplayColor::GREEN);
	EXPECT_EQ(DisplayFor(6.0).m_ImpactColor, EImpactDisplayColor::PURPLE);
	EXPECT_EQ(DisplayFor(10.0).m_ImpactColor, EImpactDisplayColor::PURPLE);
	EXPECT_EQ(DisplayFor(10.009).m_ImpactColor, EImpactDisplayColor::GOLD);
	EXPECT_EQ(DisplayFor(6.009).m_Impact, "+6.00%");
	EXPECT_EQ(DisplayFor(10.009).m_Impact, "+10.00%");
}

TEST(ImpactUiIntegration, FinalizedRowsWithValidScoresRankRegardlessOfParticipantStatus)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::SELF_RESET, true, 1.0, 2.0, 60.0);
	Snapshot.m_vParticipants[0].m_Name = "Reset player";
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::FINISHED, true, 1.2, 3.0, 70.0);
	const SImpactRunPresentation View = BuildImpactRunPresentation(Snapshot);
	ASSERT_EQ(View.m_vDisplayOrder, (std::vector<size_t>{1, 0}));
	EXPECT_EQ(View.m_vPlaces[0], 2);
	EXPECT_EQ(View.m_vPlaces[1], 1);
}

TEST(ImpactUiIntegration, UnavailableFrozenScoreKeepsVisibleRowWithoutPlaceOrFakeMvp)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	Snapshot.m_Result = ERunResult::TEAM_RESET;
	Snapshot.m_ActiveAnalysisTicks = 300 * Snapshot.m_Context.m_TickSpeed;
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::OTHER, false, 99.0);
	AddRankingParticipant(Snapshot, 2, EParticipantTerminalStatus::OTHER, true, 1.0, 2.0, 50.0, 1, false);
	AddRankingParticipant(Snapshot, 3, EParticipantTerminalStatus::OTHER, true, 0.5);
	const SImpactRunPresentation View = BuildImpactRunPresentation(Snapshot);
	ASSERT_EQ(View.m_vRanking.size(), 1U);
	EXPECT_EQ(View.m_vPlaces, (std::vector<int>{0, 0, 1}));
	EXPECT_EQ(View.m_vDisplayOrder, (std::vector<size_t>{2, 0, 1}));
	EXPECT_EQ(MvpClientId(Snapshot, View.m_vRanking), 3);
	EXPECT_FALSE(BuildImpactScoreDisplay(Snapshot.m_vParticipants[0], View.m_Finalized).m_Available);
	EXPECT_FALSE(BuildImpactScoreDisplay(Snapshot.m_vParticipants[1], View.m_Finalized).m_Available);
}

TEST(ImpactUiIntegration, FrozenCurrentSnapshotCanBecomeVisibleWithoutScoringAgain)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::FINISHED, true, 1.6006270875959445,
		7.0261801729998581, 60.7843137254902);
	Snapshot.m_vParticipants[0].m_RawStats.m_Saves = 28;
	Snapshot.m_vParticipants[0].m_RawStats.m_Falls = 15;
	const SScoreSnapshot FrozenScore = Snapshot.m_vParticipants[0].m_Score;

	SRunSnapshot Active = Snapshot;
	Active.m_State = EAnalysisState::ACTIVE;
	Active.m_Result = ERunResult::NONE;
	const SImpactRunPresentation ActiveView = BuildImpactRunPresentation(Active);
	EXPECT_FALSE(BuildImpactScoreDisplay(Active.m_vParticipants[0], ActiveView.m_Finalized).m_Available);

	const SImpactRunPresentation FinalView = BuildImpactRunPresentation(Snapshot);
	const SImpactScoreDisplay FinalDisplay = BuildImpactScoreDisplay(Snapshot.m_vParticipants[0], FinalView.m_Finalized);
	ASSERT_TRUE(FinalDisplay.m_Available);
	EXPECT_EQ(FinalDisplay.m_Rating, "1.60");
	EXPECT_EQ(FinalDisplay.m_Impact, "+7.02%");
	EXPECT_EQ(FinalDisplay.m_Stability, "60%");
	EXPECT_DOUBLE_EQ(Snapshot.m_vParticipants[0].m_Score.m_Rating, FrozenScore.m_Rating);
	EXPECT_DOUBLE_EQ(Snapshot.m_vParticipants[0].m_Score.m_Impact, FrozenScore.m_Impact);
	EXPECT_DOUBLE_EQ(Snapshot.m_vParticipants[0].m_Score.m_Stability, FrozenScore.m_Stability);
}

TEST(ImpactUiIntegration, RenderingViewDoesNotMutateHistoryOrReadRawCountersForScores)
{
	SRunSnapshot Snapshot = MakeFinalRankingSnapshot();
	AddRankingParticipant(Snapshot, 1, EParticipantTerminalStatus::FINISHED, true, 1.4, 5.0, 65.0);
	Snapshot.m_vParticipants[0].m_RawStats.m_Saves = 500;
	Snapshot.m_vParticipants[0].m_RawStats.m_Falls = 400;
	const SRunSnapshot Before = Snapshot;
	const SImpactRunPresentation View = BuildImpactRunPresentation(Snapshot);
	const SImpactScoreDisplay Display = BuildImpactScoreDisplay(Snapshot.m_vParticipants[0], View.m_Finalized);
	EXPECT_EQ(Display.m_Rating, FormatTruncatedRating(Before.m_vParticipants[0].m_Score.m_Rating));
	EXPECT_EQ(Display.m_Impact, FormatTruncatedImpact(Before.m_vParticipants[0].m_Score.m_Impact));
	EXPECT_EQ(Display.m_Stability, FormatTruncatedStability(Before.m_vParticipants[0].m_Score.m_Stability));
	EXPECT_EQ(Snapshot.m_vParticipants[0].m_RawStats.m_Saves, Before.m_vParticipants[0].m_RawStats.m_Saves);
	EXPECT_EQ(Snapshot.m_vParticipants[0].m_RawStats.m_Falls, Before.m_vParticipants[0].m_RawStats.m_Falls);
	EXPECT_DOUBLE_EQ(Snapshot.m_vParticipants[0].m_Score.m_Rating, Before.m_vParticipants[0].m_Score.m_Rating);
	EXPECT_DOUBLE_EQ(Snapshot.m_vParticipants[0].m_Score.m_Impact, Before.m_vParticipants[0].m_Score.m_Impact);
	EXPECT_DOUBLE_EQ(Snapshot.m_vParticipants[0].m_Score.m_Stability, Before.m_vParticipants[0].m_Score.m_Stability);
}

TEST(ImpactUiIntegration, HistoryRoundTripKeepsVisibleRankingAndMvp)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> Storage = Info.CreateTestStorage();
	ASSERT_TRUE(Storage);
	SRunSnapshot Snapshot = FinalizedScoreSnapshot(7040);
	ASSERT_EQ(Snapshot.m_vParticipants.size(), 2U);
	const std::array<SScoreSnapshot, 2> aScores = {{
		{true, true, 1.6006270875959445, 7.0261801729998581, 60.7843137254902, 50.0, 0, IMPACT_RATING_VERSION, IMPACT_SCORING_VERSION},
		{true, true, 0.75393746391193861, -0.063044809168170485, 40.74074074074074, 50.0, 0, IMPACT_RATING_VERSION, IMPACT_SCORING_VERSION},
	}};
	for(size_t i = 0; i < Snapshot.m_vParticipants.size(); ++i)
	{
		Snapshot.m_vParticipants[i].m_Engaged = true;
		Snapshot.m_vParticipants[i].m_Finalized = true;
		Snapshot.m_vParticipants[i].m_TerminalStatus = EParticipantTerminalStatus::FINISHED;
		Snapshot.m_vParticipants[i].m_Score = aScores[i];
	}
	const SImpactRunPresentation Before = BuildImpactRunPresentation(Snapshot);
	ASSERT_TRUE(CPersistence::SaveHistory(Storage.get(), {Snapshot}));
	std::vector<SRunSnapshot> Loaded;
	ASSERT_TRUE(CPersistence::LoadHistory(Storage.get(), &Loaded));
	ASSERT_EQ(Loaded.size(), 1U);
	const SImpactRunPresentation After = BuildImpactRunPresentation(Loaded[0]);
	EXPECT_EQ(After.m_vDisplayOrder, Before.m_vDisplayOrder);
	EXPECT_EQ(After.m_vPlaces, Before.m_vPlaces);
	EXPECT_EQ(After.m_HasMvp, Before.m_HasMvp);
	EXPECT_EQ(After.m_MvpParticipantIndex, Before.m_MvpParticipantIndex);
}
