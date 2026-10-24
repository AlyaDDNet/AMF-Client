#!/usr/bin/env bash
# Build guidance for the pinned FFmpeg/x264 sources in the corresponding-source
# artifact. This script is not claimed to reproduce the historical DLL hashes:
# the original MinGW toolchain package versions were not recorded.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PACKAGE_ROOT="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
SOURCE_DIR="$PACKAGE_ROOT/sources"
WORK_DIR="${WORK_DIR:-$PACKAGE_ROOT/rebuild-win64}"
OUT_DIR="${OUT_DIR:-$PACKAGE_ROOT/rebuilt-dlls}"
JOBS="${JOBS:-4}"

FFMPEG_ARCHIVE="$SOURCE_DIR/ffmpeg-7.0.1.tar.gz"
X264_ARCHIVE="$SOURCE_DIR/x264-4613ac3c15fd75cebc4b9f65b7fb95e70a3acce1.tar.bz2"
MINGW_TRIPLET="x86_64-w64-mingw32"

for tool in tar make pkg-config nasm \
	"$MINGW_TRIPLET-gcc" "$MINGW_TRIPLET-g++" "$MINGW_TRIPLET-strip"; do
	command -v "$tool" >/dev/null || { echo "Required build tool not found: $tool" >&2; exit 1; }
done

test -f "$FFMPEG_ARCHIVE" || { echo "Missing source archive: $FFMPEG_ARCHIVE" >&2; exit 1; }
test -f "$X264_ARCHIVE" || { echo "Missing source archive: $X264_ARCHIVE" >&2; exit 1; }
test ! -e "$WORK_DIR" || { echo "Work directory already exists; set WORK_DIR to a new path: $WORK_DIR" >&2; exit 1; }
test ! -e "$OUT_DIR" || { echo "Output directory already exists; set OUT_DIR to a new path: $OUT_DIR" >&2; exit 1; }

mkdir -p "$WORK_DIR/ffmpeg" "$WORK_DIR/x264" "$OUT_DIR"
tar -xzf "$FFMPEG_ARCHIVE" --strip-components=1 -C "$WORK_DIR/ffmpeg"
tar -xjf "$X264_ARCHIVE" --strip-components=1 -C "$WORK_DIR/x264"

X264_SRC="$WORK_DIR/x264"
FFMPEG_SRC="$WORK_DIR/ffmpeg"
MINGW_INCLUDE="/usr/$MINGW_TRIPLET/include"
MINGW_LIB="/usr/$MINGW_TRIPLET/lib"

(
	cd "$X264_SRC"
	AS=nasm \
	CFLAGS="-I$MINGW_INCLUDE" \
	LDFLAGS="-L$MINGW_LIB" \
	./configure \
		--enable-static --disable-cli --disable-gpl --disable-avs \
		--disable-swscale --disable-lavf --disable-ffms --disable-gpac \
		--disable-lsmash --disable-interlaced \
		--host=x86_64-mingw32 \
		--prefix="/usr/$MINGW_TRIPLET" \
		--cross-prefix="$MINGW_TRIPLET-"
	make -j"$JOBS"
)

(
	cd "$FFMPEG_SRC"
	PKG_CONFIG_PATH="$X264_SRC" \
	PKG_CONFIG_LIBDIR="$MINGW_LIB/pkgconfig" \
	./configure \
		--disable-all --disable-alsa --disable-iconv --disable-libxcb \
		--disable-libxcb-shape --disable-libxcb-xfixes --disable-sdl2 \
		--disable-xlib --disable-zlib \
		--enable-avcodec --enable-avformat \
		--enable-encoder='libx264,aac' --enable-muxer='mp4,mov' \
		--enable-protocol=file --enable-libx264 --enable-swresample \
		--enable-swscale --enable-gpl \
		--extra-cflags="-I$X264_SRC" \
		--extra-cxxflags="-I$X264_SRC" \
		--extra-ldflags="-L$X264_SRC" \
		--arch=x86_64 --target_os=mingw32 \
		--cross-prefix="$MINGW_TRIPLET-" \
		--disable-static --enable-shared \
		--extra-libs='-lpthread -lm' --pkg-config-flags='--static'
	make -j"$JOBS"
)

for dll in \
	libavcodec/avcodec-61.dll \
	libavformat/avformat-61.dll \
	libavutil/avutil-59.dll \
	libswresample/swresample-5.dll \
	libswscale/swscale-8.dll; do
	"$MINGW_TRIPLET-strip" -s "$FFMPEG_SRC/$dll" -o "$OUT_DIR/$(basename "$dll")"
done

echo "Built FFmpeg DLLs are in: $OUT_DIR"
echo "These outputs are not claimed to be byte-identical to the distributed DLLs."
