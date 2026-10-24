#!/usr/bin/env python3
"""Bounded production-path checks for AMF Music Player artwork ownership."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MUSIC = (ROOT / "src/game/client/components/tclient/music_player/music_player.cpp").read_text(encoding="utf-8")
DECODER = (ROOT / "src/game/client/components/tclient/music_player/media_decoder.cpp").read_text(encoding="utf-8")
ARTWORK = (ROOT / "src/game/client/components/tclient/music_player/music_player_artwork.h").read_text(encoding="utf-8")
TESTS = (ROOT / "src/test/music_player_artwork_test.cpp").read_text(encoding="utf-8")


# The Windows Media Session path must consume SMTC's own media properties and
# thumbnail stream, regardless of whether the session belongs to a browser.
assert "Session.TryGetMediaPropertiesAsync().get()" in MUSIC
assert "ReadSelectedArtwork(BestMediaProperties, Out, PreviousSnapshot, CachedTrackKey, LastArtworkReadMs)" in MUSIC
assert "MediaProperties.Thumbnail()" in MUSIC
assert "Thumbnail.OpenReadAsync().get()" in MUSIC
assert "Reader.LoadAsync((uint32_t)Size).get() != Size" in MUSIC
assert "Size > 8ull * 1024ull * 1024ull" in MUSIC
assert "ReadState = SMusicArt::EThumbnailReadState::READY" in MUSIC
assert "m_Art.m_pBytes = std::make_shared<const std::vector<unsigned char>>(std::move(vArtBytes))" in MUSIC
assert '"|thumbnail-state="' in MUSIC and '"|art-bytes="' in MUSIC
assert "Stream.ContentType()" in MUSIC and "m_Art.m_MimeType" in MUSIC
assert "art_mime='%s'" in MUSIC
assert '"heart fallback: service=' in MUSIC
assert '"decode succeeded: service=' in MUSIC and '"decode failed: service=' in MUSIC
assert '"texture upload complete: service=' in MUSIC and '"texture upload failed: service=' in MUSIC

# Only the selected best-scoring active session reads its thumbnail, and
# immutable shared bytes avoid copying a full image while crossing snapshots.
poll_start = MUSIC.index("bool PollSessions(")
poll_end = MUSIC.index("void Run()", poll_start)
poll_sessions = MUSIC[poll_start:poll_end]
assert poll_sessions.index("BestMediaProperties = std::move(MediaProperties)") < poll_sessions.index("ReadSelectedArtwork(BestMediaProperties")
assert "m_pBytes = std::make_shared<const std::vector<unsigned char>>(std::move(vArtBytes))" in MUSIC
assert "std::shared_ptr<const std::vector<unsigned char>> m_pBytes" in MUSIC

decode_fail_start = MUSIC.index('"decode failed: service=')
decode_fail_end = MUSIC.index("m_pArtDecodeJob = nullptr;", decode_fail_start)
decode_fail_path = MUSIC[decode_fail_start:decode_fail_end]
assert "m_LoadedArtTrackKey.clear()" in decode_fail_path
assert "MediaDecoder::UnloadFrames(pOwner->Graphics(), m_vArtFrames)" in decode_fail_path
assert "ScheduleArtRetry();" in decode_fail_path

# Artwork identity is session/track plus content, not just byte count, so a
# late/revised same-size thumbnail gets a distinct cache entry.
assert "CacheKey(Out.m_ServiceId, Out.m_Title, Out.m_Artist, vArtBytes.data(), vArtBytes.size())" in MUSIC
assert "Hash ^= pBytes[i]" in ARTWORK and "Hash *= 1099511628211ull" in ARTWORK
assert "CurrentKey != LastKey || (RetryAtTicks > 0 && NowTicks >= RetryAtTicks)" in ARTWORK

# Decode jobs own their input bytes. They are abortable and shutdown/reset
# cancels them before the provider/component state is released. CPU decode does
# not dereference the graphics pointer from the worker thread.
job_start = MUSIC.index("class CMusicPlayerArtDecodeJob")
job_end = MUSIC.index("class CMusicPlayer::CImpl", job_start)
job = MUSIC[job_start:job_end]
assert "std::vector<unsigned char> m_vData;" in job
assert "m_vData.assign(pData, pData + DataSize)" in job
assert "Abortable(true)" in job
reset_start = MUSIC.index("void ResetArtwork(IGraphics *pGraphics)")
reset_end = MUSIC.index("void RefreshPaletteForConfig()", reset_start)
reset = MUSIC[reset_start:reset_end]
assert "m_pArtRequest->Abort()" in reset
assert "m_pArtDecodeJob->Abort()" in reset
shutdown_start = MUSIC.index("void Shutdown(IGraphics *pGraphics)")
shutdown_end = MUSIC.index("void ScheduleArtRetry()", shutdown_start)
shutdown = MUSIC[shutdown_start:shutdown_end]
assert "Reset(pGraphics);" in shutdown
assert shutdown.index("Reset(pGraphics);") < shutdown.index("m_pProvider.reset();")
on_shutdown_start = MUSIC.index("void CMusicPlayer::OnShutdown()")
on_shutdown_end = MUSIC.index("void CMusicPlayer::OnWindowResize()", on_shutdown_start)
on_shutdown = MUSIC[on_shutdown_start:on_shutdown_end]
assert on_shutdown.index("m_pImpl->Shutdown(Graphics());") < on_shutdown.index("m_pImpl.reset();")
decode_start = DECODER.index("bool DecodeStaticImageCpu(")
decode_end = DECODER.index("bool DecodeAnimatedImageCpu(", decode_start)
decode = DECODER[decode_start:decode_end]
assert "if(!pData || DataSize == 0)" in decode
assert "IsPngSignature(pData, DataSize)" in decode
assert "IsJpegSignature(pData, DataSize)" in decode
assert "pGraphics->" not in decode
assert "CImageLoader::LoadPng" in decode
assert "DecodeImageWithFfmpegCpu" in decode

# Keep the heart only as a genuine fallback, and preserve tests for immediate,
# late, revised, switched-session and failed artwork data.
assert "DrawRoundedFallbackArt(Graphics(), g_pData->m_aImages[IMAGE_AMF_HEART].m_Id" in MUSIC
for case in (
    "ImmediateThumbnailCreatesLoadableArtworkKey",
    "MissingThumbnailFallsBackAndLateThumbnailIsProcessed",
    "SameLengthArtworkRevisionInvalidatesCache",
    "TrackAndSessionSwitchInvalidateArtworkCache",
    "FailedArtworkCanRetryWithBoundedBackoff",
    "UnavailableArtworkLeavesFallbackAvailable",
):
    assert f"TEST(MusicPlayerArtwork, {case})" in TESTS
assert "ScheduleArtRetry();" in MUSIC

print("AMF Music Player GSMTC artwork path, cache/retry, fallback, and async ownership checks PASS")
