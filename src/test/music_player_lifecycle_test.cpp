#include <game/client/components/tclient/music_player/visualizer/service.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>

namespace
{
	struct SWorkerCounts
	{
		std::atomic<int> m_Active{0};
		std::atomic<int> m_MaxActive{0};
		std::atomic<int> m_Started{0};
		std::atomic<int> m_Stopped{0};
	};

	class CThreadedFakeVisualizerSource final : public BestClientVisualizer::IVisualizerSource
	{
		std::shared_ptr<SWorkerCounts> m_pCounts;
		std::atomic<bool> m_Stop{false};
		std::thread m_Worker;
		std::mutex m_Mutex;
		BestClientVisualizer::SVisualizerFrame m_Frame;
		bool m_HasFrame = false;
		int m_FrameNumber = 0;

	public:
		explicit CThreadedFakeVisualizerSource(std::shared_ptr<SWorkerCounts> pCounts) :
			m_pCounts(std::move(pCounts))
		{
			const int Active = ++m_pCounts->m_Active;
			++m_pCounts->m_Started;
			int Peak = m_pCounts->m_MaxActive.load();
			while(Active > Peak && !m_pCounts->m_MaxActive.compare_exchange_weak(Peak, Active))
			{
			}

			m_Worker = std::thread([this]() {
				while(!m_Stop.load())
				{
					{
						std::lock_guard<std::mutex> Lock(m_Mutex);
						m_Frame.m_HasRealtimeSignal = true;
						m_Frame.m_Rms = (m_FrameNumber++ % 100) / 100.0f;
						m_HasFrame = true;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
			});
		}

		~CThreadedFakeVisualizerSource() override
		{
			m_Stop = true;
			if(m_Worker.joinable())
				m_Worker.join();
			--m_pCounts->m_Active;
			++m_pCounts->m_Stopped;
		}

		void SetPlaybackHint(const BestClientVisualizer::SVisualizerPlaybackHint &) override {}
		void SetConfig(const BestClientVisualizer::SVisualizerConfig &) override {}

		bool PollFrame(BestClientVisualizer::SVisualizerFrame &OutFrame) override
		{
			std::lock_guard<std::mutex> Lock(m_Mutex);
			OutFrame = m_Frame;
			return m_HasFrame;
		}
	};

	auto MakeFakeFactory(const std::shared_ptr<SWorkerCounts> &pCounts, int &FactoryCalls)
	{
		return [&pCounts, &FactoryCalls]() -> std::unique_ptr<BestClientVisualizer::IVisualizerSource> {
			++FactoryCalls;
			return std::make_unique<CThreadedFakeVisualizerSource>(pCounts);
		};
	}
}

TEST(MusicPlayerLifecycle, DisabledFromStartupDoesNotCreateCaptureWorker)
{
	BestClientVisualizer::CVisualizerSourceLifecycle Lifecycle;
	auto pCounts = std::make_shared<SWorkerCounts>();
	int FactoryCalls = 0;
	auto Factory = MakeFakeFactory(pCounts, FactoryCalls);

	EXPECT_FALSE(Lifecycle.SetEnabled(false, Factory));
	EXPECT_FALSE(Lifecycle.IsEnabled());
	EXPECT_EQ(FactoryCalls, 0);
	EXPECT_EQ(pCounts->m_Active.load(), 0);
}

TEST(MusicPlayerLifecycle, RepeatedEnableDisableJoinsEachWorkerAndNeverDuplicates)
{
	BestClientVisualizer::CVisualizerSourceLifecycle Lifecycle;
	auto pCounts = std::make_shared<SWorkerCounts>();
	int FactoryCalls = 0;
	auto Factory = MakeFakeFactory(pCounts, FactoryCalls);

	for(int Iteration = 0; Iteration < 64; ++Iteration)
	{
		ASSERT_TRUE(Lifecycle.SetEnabled(true, Factory));
		ASSERT_TRUE(Lifecycle.SetEnabled(true, Factory));
		EXPECT_EQ(FactoryCalls, Iteration + 1);
		EXPECT_EQ(pCounts->m_Active.load(), 1);

		BestClientVisualizer::SVisualizerFrame Frame;
		bool HasFrame = false;
		for(int Retry = 0; Retry < 100 && !HasFrame; ++Retry)
		{
			HasFrame = Lifecycle.Get()->PollFrame(Frame);
			if(!HasFrame)
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		ASSERT_TRUE(HasFrame);
		const float CopiedRms = Frame.m_Rms;

		EXPECT_FALSE(Lifecycle.SetEnabled(false, Factory));
		EXPECT_FALSE(Lifecycle.IsEnabled());
		EXPECT_EQ(pCounts->m_Active.load(), 0);
		EXPECT_FLOAT_EQ(Frame.m_Rms, CopiedRms);
	}

	EXPECT_EQ(pCounts->m_Started.load(), 64);
	EXPECT_EQ(pCounts->m_Stopped.load(), 64);
	EXPECT_EQ(pCounts->m_MaxActive.load(), 1);
}

TEST(MusicPlayerLifecycle, DestroyingActivePipelineJoinsCaptureWorker)
{
	auto pCounts = std::make_shared<SWorkerCounts>();
	int FactoryCalls = 0;
	auto Factory = MakeFakeFactory(pCounts, FactoryCalls);
	{
		BestClientVisualizer::CVisualizerSourceLifecycle Lifecycle;
		ASSERT_TRUE(Lifecycle.SetEnabled(true, Factory));
		EXPECT_EQ(pCounts->m_Active.load(), 1);
	}

	EXPECT_EQ(pCounts->m_Active.load(), 0);
	EXPECT_EQ(pCounts->m_Started.load(), 1);
	EXPECT_EQ(pCounts->m_Stopped.load(), 1);
}
