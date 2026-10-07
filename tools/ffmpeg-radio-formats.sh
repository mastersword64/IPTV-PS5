#!/usr/bin/env bash
#
# Makes sure the app's FFmpeg can read radio streams. Radio stations send plain MP3 or AAC sound,
# not wrapped in a video file, and reading those needs two readers ("demuxers": mp3 and aac) that
# a build made only for TV channels and films may have been made without.
#
# The script looks at how the present FFmpeg was built. If both readers are already there it says
# so and changes nothing. Otherwise it rebuilds FFmpeg with them added, keeping everything else as
# it was. The present build is copied aside first and put back if anything goes wrong.
#
#   usage: bash tools/ffmpeg-radio-formats.sh
set -uo pipefail

workspace=${ES_WORKSPACE:-$HOME/ps5-workspace}
# the script used last: the one with the extra film formats if that was run, else the original
script=$(find "$workspace" -maxdepth 4 -name "build-ffmpeg-ps5-more.sh" 2>/dev/null | head -1)
[ -n "$script" ] || script=$(find "$workspace" -maxdepth 4 -name "build-ffmpeg-ps5-tls.sh" 2>/dev/null | head -1)
if [ -z "$script" ]; then
    echo "The script that built FFmpeg (build-ffmpeg-ps5-tls.sh) was not found under $workspace."
    echo "Tell Claude; nothing was changed."
    exit 1
fi
dir=$(dirname "$script")
built="$workspace/es-port/ffmpeg-ps5-net"
[ -d "$built/lib" ] || { echo "The present FFmpeg was not found at $built; nothing was changed."; exit 1; }

demuxers=$(grep -oE -- "--enable-demuxer=[A-Za-z0-9_,]+" "$script" | head -1)
if [ -z "$demuxers" ]; then
    if grep -q -- "--disable-everything" "$script"; then
        echo "The list of readers was not found in $script; nothing was changed. Tell Claude."
        exit 1
    fi
    echo "This FFmpeg was built with every reader it has, so radio streams can already be read. Nothing to do."
    exit 0
fi
echo "Readers in the present build: ${demuxers#--enable-demuxer=}"
have() { echo ",${demuxers#--enable-demuxer=}," | grep -q ",$1,"; }
if have mp3 && have aac; then
    echo "MP3 and AAC radio streams can already be read. Nothing to do."
    exit 0
fi

new="$dir/build-ffmpeg-ps5-radio.sh"
sed -E \
    -e 's|(--enable-demuxer=[A-Za-z0-9_,]+)|\1,mp3,aac|' \
    -e 's|(--enable-decoder=[A-Za-z0-9_,]+)|\1,mp3,mp3float,aac,aac_latm|' \
    -e 's|(--enable-parser=[A-Za-z0-9_,]+)|\1,mpegaudio,aac,aac_latm|' \
    "$script" > "$new"
if ! grep -qE -- "--enable-demuxer=[A-Za-z0-9_,]+,mp3,aac" "$new"; then
    echo "The format lists could not be changed; nothing was built."
    exit 1
fi

echo "==> keeping a copy of the present build"
rm -rf "$built.before-radio-formats"
cp -a "$built" "$built.before-radio-formats"

echo "==> building (several minutes, no output until done)"
( cd "$dir" && bash "$new" ) > /tmp/ffmpeg-radio.log 2>&1
status=$?
if [ $status -ne 0 ] || [ ! -f "$built/lib/libavformat.a" ]; then
    echo "The build failed. Putting the previous one back."
    rm -rf "$built"
    mv "$built.before-radio-formats" "$built"
    echo "Send Claude the end of the log:   tail -30 /tmp/ffmpeg-radio.log"
    exit 1
fi
tail -3 /tmp/ffmpeg-radio.log
echo
echo "FFmpeg rebuilt with the radio readers. Now build and deploy the app as usual:"
echo "    cd $workspace/iptv-ps5 && bash build.sh && make -C build/title deploy PS5_HOST=<console address>"
echo "(The previous FFmpeg is kept at $built.before-radio-formats in case it is wanted back.)"
