#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_IMPACT_IMPACT_CHECKPOINT_POLICY_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_IMPACT_IMPACT_CHECKPOINT_POLICY_H

#include <cstddef>
#include <mutex>
#include <optional>
#include <utility>

namespace AmfImpact
{
template<typename TOperation>
class CLatestAsyncOperationQueue
{
	std::mutex m_Mutex;
	std::optional<TOperation> m_Pending;
	bool m_JobScheduled = false;

public:
	// Replacing the pending operation is safe when it represents the latest
	// desired contents of one checkpoint file. Returns true only when a drain
	// job needs to be scheduled.
	bool Enqueue(TOperation Operation)
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		m_Pending = std::move(Operation);
		if(m_JobScheduled)
			return false;
		m_JobScheduled = true;
		return true;
	}

	bool TakeNext(TOperation *pOperation)
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		if(!m_Pending)
		{
			m_JobScheduled = false;
			return false;
		}
		*pOperation = std::move(*m_Pending);
		m_Pending.reset();
		return true;
	}
};

inline bool ShouldSaveActiveCheckpoint(bool Enabled, bool HasCurrentRun, bool Finalized, bool WaitingWithoutParticipants, bool Force, bool IntervalElapsed)
{
	return Enabled && HasCurrentRun && !Finalized && !WaitingWithoutParticipants && (Force || IntervalElapsed);
}

inline bool ShouldRemoveExpiredActiveCheckpoint(bool ExpiredAny, bool HasCurrentRun, size_t PendingRunCount)
{
	return ExpiredAny && !HasCurrentRun && PendingRunCount == 0;
}
}

#endif
