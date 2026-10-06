#!/usr/bin/env bash
#
# Rebuilds the app's FFmpeg with more formats:
#   sound:      DTS, TrueHD / MLP, FLAC, plain PCM   (common on 4K films; silent without these)
#   subtitles:  the picture kind used by film discs and some broadcasts (PGS, DVD, DVB)
#
# It reuses the script that built the present FFmpeg, changing only the lists of formats.
# The present build is copied aside first and put back if anything goes wrong, so the app can
# always still be built. Takes several minutes.
#
#   usage: bash tools/ffmpeg-more-formats.sh
set -uo pipefail

workspace=${ES_WORKSPACE:-$HOME/ps5-workspace}
script=$(find "$workspace" -maxdepth 4 -name "build-ffmpeg-ps5-tls.sh" 2>/dev/null | head -1)
if [ -z "$script" ]; then
    echo "The script that built FFmpeg (build-ffmpeg-ps5-tls.sh) was not found under $workspace."
    echo "Tell Claude; nothing was changed."
    exit 1
fi
dir=$(dirname "$script")
built="$workspace/es-port/ffmpeg-ps5-net"
[ -d "$built/lib" ] || { echo "The present FFmpeg was not found at $built; nothing was changed."; exit 1; }

if grep -q "truehd" "$script"; then
    echo "This FFmpeg script already includes the extra formats."
else
    grep -q -- "--enable-decoder=" "$script" || { echo "The decoder list was not found in $script; nothing was changed."; exit 1; }
fi

new="$dir/build-ffmpeg-ps5-more.sh"
sed -E \
    -e 's|(--enable-decoder=[A-Za-z0-9_,]+)|\1,dca,truehd,mlp,flac,pcm_s16le,pcm_s24le,pgssub,dvdsub,dvbsub|' \
    -e 's|(--enable-parser=[A-Za-z0-9_,]+)|\1,dca,mlp,flac,dvdsub,dvbsub|' \
    "$script" > "$new"
if ! grep -q "truehd" "$new"; then
    echo "The format lists could not be changed; nothing was built."
    exit 1
fi

echo "==> keeping a copy of the present build"
rm -rf "$built.before-more-formats"
cp -a "$built" "$built.before-more-formats"

echo "==> building (several minutes, no output until done)"
( cd "$dir" && bash "$new" ) > /tmp/ffmpeg-more.log 2>&1
status=$?
if [ $status -ne 0 ] || [ ! -f "$built/lib/libavcodec.a" ]; then
    echo "The build failed. Putting the previous one back."
    rm -rf "$built"
    mv "$built.before-more-formats" "$built"
    echo "Send Claude the end of the log:   tail -30 /tmp/ffmpeg-more.log"
    exit 1
fi
tail -3 /tmp/ffmpeg-more.log
echo
echo "FFmpeg rebuilt with the extra formats. Now build and deploy the app as usual:"
echo "    cd $workspace/iptv-ps5 && bash build.sh && make -C build/title deploy PS5_HOST=10.230.88.228"
echo "(The previous FFmpeg is kept at $built.before-more-formats in case it is wanted back.)"
