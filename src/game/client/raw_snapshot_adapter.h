#ifndef GAME_CLIENT_RAW_SNAPSHOT_ADAPTER_H
#define GAME_CLIENT_RAW_SNAPSHOT_ADAPTER_H

#include <engine/shared/protocol.h>
#include <generated/protocol.h>
#include <game/gamecore.h>

#include <array>
#include <cstdint>

// Raw observation contract carried over from AMF 19.9. In production these
// objects are copied from the validated/translated SNAP_CURRENT and SNAP_PREV
// alt-snapshot items, before CGameClient evolves character state. They must not
// be populated from evolved, predicted, interpolated, or rendered data.
// m_SnapshotServerTick is the owning snapshot's GameTick/PrevGameTick;
// m_ObjectTick is the tick stored inside the raw character object itself.
struct SRawAuthoritativeCharacterObservation
{
	bool m_Valid = false;
	bool m_FromRawCurrentSnapshot = false;
	bool m_FromRawPreviousSnapshot = false;
	bool m_SequenceKnown = false;
	bool m_Present = false;
	bool m_Alive = false;
	int m_ClientId = -1;
	uint64_t m_SnapshotSequence = 0;
	int64_t m_SnapshotServerTick = -1;
	int m_ObjectTick = -1;
	int m_TeamId = 0;
	CNetObj_Character m_Character{};
	bool m_HasExtendedData = false;
	CNetObj_DDNetCharacter m_ExtendedData{};

	// Provenance is additive metadata. The legacy adapter used one global
	// ClientId-indexed previous/current series while the selected connection
	// changed; retain that series, but record which selected connection supplied
	// each copied object. A known sequence may originate from a prior connection
	// when the legacy object-tick continuity check matched across a dummy switch.
	int m_SourceConnection = -1;
	int m_SequenceConnection = -1;
};

struct SRawAuthoritativeSnapshotContext
{
	bool m_Valid = false;
	bool m_DummySwapped = false;
	bool m_DemoPlayback = false;
	int m_Connection = -1;
	int m_LocalClientId = -1;
	uint64_t m_Sequence = 0;
	int64_t m_ServerTick = -1;
	int64_t m_PreviousServerTick = -1;
};

// Observation-only adapter. It deliberately performs no gameplay
// classification and no duplicate suppression. Each OnNewSnapshot call
// advances the sequence, including a DummySwapped re-read of the same tick.
class CRawAuthoritativeSnapshotAdapter
{
public:
	void Reset()
	{
		m_aPrevious.fill(SRawAuthoritativeCharacterObservation{});
		m_aCurrent.fill(SRawAuthoritativeCharacterObservation{});
		m_PreviousContext = {};
		m_CurrentContext = {};
		m_Sequence = 0;
	}

	// Mirrors the start of the legacy OnNewSnapshot path: shift the global
	// ClientId-indexed current array to previous, clear current, and advance once
	// per callback (including dummy-switch re-reads of an already-seen tick).
	void BeginSnapshot(int Connection, bool DummySwapped, int64_t ServerTick, int64_t PreviousServerTick, bool DemoPlayback = false)
	{
		m_aPrevious = m_aCurrent;
		m_aCurrent.fill(SRawAuthoritativeCharacterObservation{});
		m_PreviousContext = m_CurrentContext;
		m_CurrentContext = {};
		m_CurrentContext.m_Valid = true;
		m_CurrentContext.m_DummySwapped = DummySwapped;
		m_CurrentContext.m_DemoPlayback = DemoPlayback;
		m_CurrentContext.m_Connection = DemoPlayback ? -1 : Connection;
		m_CurrentContext.m_Sequence = ++m_Sequence;
		m_CurrentContext.m_ServerTick = ServerTick;
		m_CurrentContext.m_PreviousServerTick = PreviousServerTick;
	}

	void ObservePlayerInfo(int ClientId, int TeamId, bool IsLocal)
	{
		if(!IsValidClientId(ClientId))
			return;
		SRawAuthoritativeCharacterObservation &Observation = m_aCurrent[ClientId];
		Observation.m_Present = true;
		Observation.m_TeamId = TeamId;
		Observation.m_SourceConnection = m_CurrentContext.m_Connection;
		if(IsLocal)
			m_CurrentContext.m_LocalClientId = ClientId;
	}

	void ObserveCharacter(int ClientId, const CNetObj_Character &Current, const CNetObj_Character *pPrevious, int TeamId)
	{
		if(!IsValidClientId(ClientId))
			return;

		SRawAuthoritativeCharacterObservation &RawCurrent = m_aCurrent[ClientId];
		RawCurrent.m_Valid = true;
		RawCurrent.m_FromRawCurrentSnapshot = true;
		RawCurrent.m_Present = true;
		RawCurrent.m_Alive = TeamId != TEAM_SPECTATORS;
		RawCurrent.m_ClientId = ClientId;
		RawCurrent.m_SnapshotSequence = m_CurrentContext.m_Sequence;
		RawCurrent.m_SequenceConnection = m_CurrentContext.m_Connection;
		RawCurrent.m_SnapshotServerTick = m_CurrentContext.m_ServerTick;
		RawCurrent.m_ObjectTick = Current.m_Tick;
		RawCurrent.m_TeamId = TeamId;
		RawCurrent.m_Character = Current;
		RawCurrent.m_SourceConnection = m_CurrentContext.m_Connection;

		SRawAuthoritativeCharacterObservation &RawPrevious = m_aPrevious[ClientId];
		const SRawAuthoritativeCharacterObservation PreviousObservation = RawPrevious;
		RawPrevious = SRawAuthoritativeCharacterObservation{};
		if(pPrevious)
		{
			RawPrevious.m_Valid = true;
			RawPrevious.m_FromRawPreviousSnapshot = true;
			RawPrevious.m_Present = true;
			RawPrevious.m_Alive = TeamId != TEAM_SPECTATORS;
			RawPrevious.m_ClientId = ClientId;
			RawPrevious.m_ObjectTick = pPrevious->m_Tick;
			RawPrevious.m_SnapshotServerTick = m_CurrentContext.m_PreviousServerTick;
			RawPrevious.m_TeamId = TeamId;
			RawPrevious.m_Character = *pPrevious;
			RawPrevious.m_SourceConnection = m_CurrentContext.m_Connection;
			if(PreviousObservation.m_Valid && PreviousObservation.m_ObjectTick == RawPrevious.m_ObjectTick)
			{
				// Preserve the 19.9 continuity marker exactly: equality is based on
				// object tick, even across a selected-connection switch.
				RawPrevious.m_SequenceKnown = true;
				RawPrevious.m_SnapshotSequence = PreviousObservation.m_SnapshotSequence;
				RawPrevious.m_SequenceConnection = PreviousObservation.m_SequenceConnection;
			}
		}
	}

	void ObserveExtendedCharacter(int ClientId, const CNetObj_DDNetCharacter &Current, const CNetObj_DDNetCharacter *pPrevious)
	{
		if(!IsValidClientId(ClientId))
			return;

		SRawAuthoritativeCharacterObservation &RawCurrent = m_aCurrent[ClientId];
		RawCurrent.m_HasExtendedData = true;
		RawCurrent.m_ExtendedData = Current;
		RawCurrent.m_SourceConnection = m_CurrentContext.m_Connection;
		if(pPrevious)
		{
			SRawAuthoritativeCharacterObservation &RawPrevious = m_aPrevious[ClientId];
			RawPrevious.m_HasExtendedData = true;
			RawPrevious.m_ExtendedData = *pPrevious;
			RawPrevious.m_SourceConnection = m_CurrentContext.m_Connection;
		}
	}

	const std::array<SRawAuthoritativeCharacterObservation, MAX_CLIENTS> &Previous() const { return m_aPrevious; }
	const std::array<SRawAuthoritativeCharacterObservation, MAX_CLIENTS> &Current() const { return m_aCurrent; }
	const SRawAuthoritativeSnapshotContext &PreviousContext() const { return m_PreviousContext; }
	const SRawAuthoritativeSnapshotContext &CurrentContext() const { return m_CurrentContext; }
	uint64_t Sequence() const { return m_Sequence; }

private:
	static bool IsValidClientId(int ClientId) { return ClientId >= 0 && ClientId < MAX_CLIENTS; }

	std::array<SRawAuthoritativeCharacterObservation, MAX_CLIENTS> m_aPrevious{};
	std::array<SRawAuthoritativeCharacterObservation, MAX_CLIENTS> m_aCurrent{};
	SRawAuthoritativeSnapshotContext m_PreviousContext{};
	SRawAuthoritativeSnapshotContext m_CurrentContext{};
	uint64_t m_Sequence = 0;
};

#endif // GAME_CLIENT_RAW_SNAPSHOT_ADAPTER_H
