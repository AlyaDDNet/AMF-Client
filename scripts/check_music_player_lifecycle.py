#!/usr/bin/env python3
"""Bounded ownership/lifecycle checks for the AMF Music Player visualizer."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
MUSIC_CPP = (ROOT / "src/game/client/components/tclient/music_player/music_player.cpp").read_text(encoding="utf-8")
SERVICE_H = (ROOT / "src/game/client/components/tclient/music_player/visualizer/service.h").read_text(encoding="utf-8")
SERVICE_CPP = (ROOT / "src/game/client/components/tclient/music_player/visualizer/service.cpp").read_text(encoding="utf-8")
WASAPI = (ROOT / "src/game/client/components/tclient/music_player/visualizer/source_wasapi.cpp").read_text(encoding="utf-8")
TEST = (ROOT / "src/test/music_player_lifecycle_test.cpp").read_text(encoding="utf-8")


# Async activation owns its callback reference. The callback object itself must
# therefore be heap-owned and COM-refcounted, not a stack object that can expire
# after WaitForCompletion's timeout while the OS still has a callback pending.
assert "winrt::com_ptr<CProcessLoopbackActivationHandler> CompletionHandler;" in WASAPI
assert "CompletionHandler.attach(new CProcessLoopbackActivationHandler(m_ShutdownEvent));" in WASAPI
assert "CProcessLoopbackActivationHandler CompletionHandler;" not in WASAPI
release = WASAPI[WASAPI.index("ULONG STDMETHODCALLTYPE Release() override"):]
release = release[: release.index("HRESULT STDMETHODCALLTYPE ActivateCompleted")]
assert "if(RefCount == 0)" in release and "delete this;" in release
assert "WaitForMultipleObjects(NumWaitHandles, aWaitHandles, FALSE, 1000)" in WASAPI
assert "m_ShutdownEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);" in WASAPI
assert "WaitForShutdown(std::chrono::milliseconds(8));" in WASAPI
assert "sleep_for(std::chrono::milliseconds(1))" not in WASAPI, "WASAPI capture must not retain the 1-ms polling loop"
assert "SetEvent(m_ShutdownEvent);" in WASAPI and "m_Cv.notify_all();" in WASAPI
assert "if(!Started && IsShutdown())" in WASAPI, "disable during async process-loopback activation must not fall through into device capture"
assert "CompletionHandler.get()->WaitForCompletion()" in WASAPI

# Capture interface/buffers remain worker-local. Published output and config
# cross the source boundary only as locked value copies; destruction joins.
worker = WASAPI[WASAPI.index("void WorkerMain()"):WASAPI.index("public:", WASAPI.index("void WorkerMain()"))]
assert "std::vector<float> vMonoBuffer;" in worker
assert "std::vector<float> vIdleSilenceBuffer;" in worker
assert "SVisualizerFrame m_LatestFrame;" in WASAPI
assert "m_LatestFrame = Frame;" in WASAPI
assert "m_WorkerThread.join();" in WASAPI
assert re.search(r"std::lock_guard<std::mutex> Lock\(m_Mutex\);\s*m_Shutdown = true;", WASAPI)

# The source factory is lazy and the same owner is used by the runtime service.
assert "class CVisualizerSourceLifecycle" in SERVICE_H
assert "if(!Enabled)" in SERVICE_H and "m_pSource.reset();" in SERVICE_H
assert "if(!m_pSource)" in SERVICE_H and "m_pSource = Factory();" in SERVICE_H
assert "bool CRealtimeMusicVisualizer::SetEnabled(bool Enabled)" in SERVICE_CPP
assert "m_Source.SetEnabled(Enabled," in SERVICE_CPP
assert "m_pSource = CreateWasapiVisualizerSource();" not in SERVICE_CPP

# Music Player OFF or Visualizer OFF synchronously tears down both actual
# visualizer workers; a later ON creates one fresh source/level worker.
gate_start = MUSIC_CPP.index("void SetVisualizerPipelineEnabled(bool Enabled)")
gate_end = MUSIC_CPP.index("void ResetHudState()", gate_start)
gate = MUSIC_CPP[gate_start:gate_end]
impl_ctor_start = MUSIC_CPP.index("CImpl()")
impl_ctor_end = MUSIC_CPP.index("void SetVisualizerPipelineEnabled", impl_ctor_start)
impl_ctor = MUSIC_CPP[impl_ctor_start:impl_ctor_end]
shutdown_start = MUSIC_CPP.index("void Shutdown(IGraphics *pGraphics)")
shutdown_end = MUSIC_CPP.index("void StartArtDecode", shutdown_start)
shutdown = MUSIC_CPP[shutdown_start:shutdown_end]
assert "m_pVisualizer->SetEnabled(true);" in gate
assert "std::make_unique<CVisualizerLevelThread>()" in gate
assert "m_pLevelThread->Stop();" in gate and "m_pLevelThread.reset();" in gate
assert "m_pVisualizer->SetEnabled(false);" in gate
assert "std::make_unique<CVisualizerLevelThread>()" not in impl_ctor
assert "SetVisualizerPipelineEnabled(false);" in shutdown
assert "m_pImpl->SetVisualizerPipelineEnabled(" in MUSIC_CPP
assert "g_Config.m_AmfMusicPlayer != 0" in MUSIC_CPP
assert "g_Config.m_AmfMusicPlayerVisualizer != 0" in MUSIC_CPP
assert "std::unique_ptr<CVisualizerLevelThread> m_pLevelThread;" in MUSIC_CPP
assert "CVisualizerLevelThread m_LevelThread;" not in MUSIC_CPP
assert "m_pLevelThread->join();" not in MUSIC_CPP  # Stop owns the join.
assert "std::shared_ptr<const std::vector<unsigned char>> m_pBytes;" in MUSIC_CPP, "GSMTC image bytes must be immutable shared snapshots, not copied under the provider mutex"
assert "m_pProvider->SetEnabled(MusicPlayerEnabled);" in MUSIC_CPP and "if(MusicPlayerEnabled && (m_pImpl->m_LastPollTick" in MUSIC_CPP, "Music Player OFF must stop metadata polling and provider work"
assert "m_Cv.wait(Lock" in MUSIC_CPP and "m_Cv.wait_for(Lock, std::chrono::milliseconds(100)" not in MUSIC_CPP, "idle GSMTC worker must sleep until enabled work is queued"
assert "ManagerRetryDelay = std::min(ManagerRetryDelay * 2, std::chrono::milliseconds(5000));" in MUSIC_CPP, "missing GSMTC manager must use bounded retry backoff"
query_session = MUSIC_CPP[MUSIC_CPP.index("bool QuerySession("):MUSIC_CPP.index("static std::string ArtworkTrackKey", MUSIC_CPP.index("bool QuerySession("))]
assert "ReadThumbnailBytes" not in query_session, "enumerating candidate sessions must not read every session's artwork"
assert "ReadSelectedArtwork(BestMediaProperties, Out" in MUSIC_CPP, "artwork bytes should only be read for the selected session"
assert "ShouldRefreshThumbnail(TrackKey, CachedTrackKey" in MUSIC_CPP, "unchanged track artwork reads must be throttled while late artwork remains retryable"
assert "m_DebugNextProviderFailureTick = Now + time_freq();" in MUSIC_CPP, "explicit Music Player diagnostics must not log an unavailable provider 8 times per second"
service_ctor = SERVICE_CPP[SERVICE_CPP.index("CRealtimeMusicVisualizer::CRealtimeMusicVisualizer()"):SERVICE_CPP.index("CRealtimeMusicVisualizer::~CRealtimeMusicVisualizer()")]
assert "CreateWasapiVisualizerSource()" not in service_ctor

# Regression uses the production lifecycle owner with an active threaded fake,
# including startup-off, repeated toggles, concurrent frame copies, and destroy.
for case in (
    "DisabledFromStartupDoesNotCreateCaptureWorker",
    "RepeatedEnableDisableJoinsEachWorkerAndNeverDuplicates",
    "DestroyingActivePipelineJoinsCaptureWorker",
):
    assert f"TEST(MusicPlayerLifecycle, {case})" in TEST
assert "Iteration < 64" in TEST
assert "m_MaxActive.load(), 1" in TEST

print("AMF Music Player ownership, disabled-state gating, capture wake cadence, and lifecycle regressions PASS")
