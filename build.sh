#!/usr/bin/env bash
# IPTV for PS5: compile, link, and build the title folder.
# Uses the same toolchain, OpenGL SDK, SDL and PS5 start-up files as the EmulationStation port,
# and builds everything under this folder, so the EmulationStation port is not touched.
#
#   bash build.sh                                   build
#   IPTV_PLAYLIST="http://.../list.m3u" bash build.sh   build with this playlist address built in
#   IPTV_PLAYLIST=/path/to/list.m3u bash build.sh       build with this playlist file built in
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
W=${ES_WORKSPACE:-$HOME/ps5-workspace}
KIT=${ES_KIT:-$HOME/Downloads/es-ps5-kit}
R=$W/ps5-emulationstation
SDK=$R/.deps/native/ps5-payload-sdk
GLSDK=$W/ps5-opengl/ps5-opengl-sdk-1.0.1/sdk
GLSRC=$W/ps5-opengl/src
SDLSDK=$GLSRC/build/sdl-native/sdk
N=$R/tooling/native
FFP=$W/es-port/ffmpeg-ps5-net
B=$HERE/build
L=$B/link
CLANG=$(command -v "${PS5_CLANG:-clang-18}")
TID=${IPTV_TITLE_ID:-PPSA99779}
FF=("$FFP/lib/libavformat.a" "$FFP/lib/libavcodec.a" "$FFP/lib/libswscale.a" "$FFP/lib/libswresample.a" "$FFP/lib/libavutil.a")

for p in "$R/tooling/prospero-clang18" "$SDK/bin/prospero-lld" "$GLSDK/include/GL/glcorearb.h" "$SDLSDK/lib/libSDL2.a" \
         "$SDLSDK/include/SDL2/SDL.h" "$GLSRC/native-app/app_heap.c" "$GLSRC/native-app/cts_runtime_shims.c" \
         "$KIT/shim/es_ps5_main.cpp" "$KIT/shim/es_ps5_compat.c" "${FF[@]}" "$FFP/include/libavformat/avformat.h"; do
    [ -e "$p" ] || { echo "MISSING: $p"; exit 1; }
done
[ -n "$CLANG" ] || { echo "MISSING: clang-18"; exit 1; }
mkdir -p "$B/obj" "$L"; cd "$R" || exit 1
cc() { env PS5_PAYLOAD_SDK="$SDK" PS5_CLANG="$CLANG" USE_CCACHE=0 sh "$R/tooling/prospero-clang18" "$@"; }

echo "=== 1. PLAYLIST AND FONT ==="
if [ -n "${IPTV_PLAYLIST:-}" ]; then
    rm -f "$HERE/playlist.m3u" "$HERE/playlist-url.txt"
    if [ -f "$IPTV_PLAYLIST" ]; then cp "$IPTV_PLAYLIST" "$HERE/playlist.m3u"
    else printf '%s\n' "$IPTV_PLAYLIST" > "$HERE/playlist-url.txt"; fi
fi
if [ -f "$HERE/playlist.m3u" ]; then echo "playlist: built-in file ($(grep -c '^#EXTINF' "$HERE/playlist.m3u") channels)"
elif [ -f "$HERE/playlist-url.txt" ]; then echo "playlist: built-in address $(cat "$HERE/playlist-url.txt")"
else echo "playlist: none built in (the app will ask for one to be uploaded)"; fi
FONT=""
for f in "$HERE/font.ttf" /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf /usr/share/fonts/truetype/noto/NotoSans-Regular.ttf \
         /usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf /usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf \
         "$(find "$W/es-port/batocera-emulationstation/resources" -name '*.ttf' 2>/dev/null | sort | head -1)" \
         "$(find /usr/share/fonts -name '*.ttf' 2>/dev/null | sort | head -1)"; do
    [ -n "$f" ] && [ -f "$f" ] && { FONT=$f; break; }
done
[ -n "$FONT" ] || { echo "No .ttf font found. Copy any .ttf font to $HERE/font.ttf and run this again."; exit 1; }
echo "font: $FONT"

echo "=== 2. COMPILE ==="
resolve() { # sets PCF, PLB, PROOT for the given PacBrew modules
    PCF=(); PLB=()
    local res; res=$(bash "$R/tools/setup-pacbrew-dependencies.sh" --resolve "$@" 2>/dev/null) || return 1
    mapfile -d '' -t PCF < <(python3 -c 'import json,sys; [print(v, end="\0") for v in json.loads(sys.argv[1])["cflags"]]' "$res")
    mapfile -d '' -t PLB < <(python3 -c 'import json,sys; [print(v, end="\0") for v in json.loads(sys.argv[1])["libs"]]' "$res")
    PROOT=$(python3 -c 'import json,sys; print(json.loads(sys.argv[1])["root"])' "$res")
}
resolve freetype2 libcurl || { echo "pacbrew resolve failed"; exit 1; }
COMMON=(-O2 -w -ferror-limit=6 -fPIC -ffunction-sections -fdata-sections
        -DES_PS5 -DGL_GLEXT_PROTOTYPES=1 -DSDL_MAIN_HANDLED=1
        -Dstat64=stat -Dlstat64=lstat -Dfstat64=fstat
        -include sys/types.h -include sys/socket.h -include netinet/in.h -include arpa/inet.h -include sys/wait.h
        "-I$HERE/src" "-I$KIT/shim" "-I$GLSDK/include" "-I$SDLSDK/include/SDL2" "-I$SDLSDK/include" "-I$FFP/include" "${PCF[@]}")
CFLAGS_CXX=(-std=c++17 -fexceptions -frtti "${COMMON[@]}")
# the console's address, shown on the "add from a phone" screen if the app cannot find it out itself
CFLAGS_CXX+=("-DIPTV_HOST_HINT=\"${PS5_HOST:-10.230.88.228}\"")
# the picture decoder (custom pointers) comes from the kit, when it has one
if [ -f "$KIT/shim/stb_image.h" ]; then CFLAGS_CXX+=(-DIPTV_HAVE_STB); echo "pictures: decoder found"; else echo "pictures: no stb_image.h in the kit, custom pointer pictures will be unavailable"; fi
CFLAGS_KITC=(-std=gnu11 -O2 -w -fPIC -ffunction-sections -fdata-sections "-I$KIT/shim" "-I$FFP/include")

OBJS=(); FAILED=0
for src in "$HERE"/src/*.cpp "$KIT/shim/es_ps5_main.cpp" "$KIT/shim/es_ps5_compat.c"; do
    name=$(basename "$src"); obj="$B/obj/$name.o"; log="$B/obj/$name.err"
    case "$src" in *.c) f=("${CFLAGS_KITC[@]}");; *) f=("${CFLAGS_CXX[@]}");; esac
    if cc "${f[@]}" -c "$src" -o "$obj" 2>"$log"; then echo "OK   $name"; OBJS+=("$obj")
    else echo "FAIL $name"; grep -m3 -A4 'error:' "$log" | cut -c1-220; FAILED=1; fi
done
[ "$FAILED" = 0 ] || { echo "=== STOPPED: compile errors above ==="; exit 1; }
rm -f "$B/libiptv.a"
"$SDK/bin/llvm-ar" rcs "$B/libiptv.a" "${OBJS[@]}" || { echo "archive failed"; exit 1; }

echo "=== 3. TRIAL LINK ==="
{ cat "$N/ps5-pie.ld"
  printf '%s\n' 'PROVIDE(__eh_frame_start = ADDR(.eh_frame));' 'PROVIDE(__eh_frame_end = ADDR(.eh_frame) + SIZEOF(.eh_frame));' \
                'PROVIDE(__eh_frame_hdr_start = ADDR(.eh_frame_hdr));' 'PROVIDE(__eh_frame_hdr_end = ADDR(.eh_frame_hdr) + SIZEOF(.eh_frame_hdr));'
} > "$L/ps5-pie-iptv.ld"
for n in app_crt app_cpp_runtime; do
    cc -std=c++20 -O2 -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections -c "$N/$n.cpp" -o "$L/$n.o" 2>"$L/$n.err" || { echo "FAIL $n"; head -3 "$L/$n.err"; }
done
for n in app_heap runtime_shims cts_runtime_shims; do
    cc -std=gnu11 -O2 -w -fPIC -c "$GLSRC/native-app/$n.c" -o "$L/$n.o" 2>"$L/$n.err" || { echo "FAIL $n"; head -3 "$L/$n.err"; }
done
# Functions that the start-up files reroute (sound, screen updates, memory, sockets):
# whichever of them this build actually contains.
mapfile -t WRAPPED < <("$SDK/bin/llvm-nm" -g --defined-only "$B/libiptv.a" "$L"/*.o "$GLSDK/lib/libPS5OpenGL.a" "$SDLSDK/lib/libSDL2.a" 2>/dev/null \
    | awk 'NF==3 && $3 ~ /^__wrap_/ {print substr($3, 8)}' | sort -u)
WR=(); for s in "${WRAPPED[@]}"; do WR+=("--wrap=$s"); done
echo "rerouted functions: ${WRAPPED[*]}"
PLBF=(); for a in "${PLB[@]}"; do case "$a" in -lSDL2|-lSDL2main|-lOSMesa) ;; *) PLBF+=("$a");; esac; done
[ -f "$PROOT/user/homebrew/lib/libssl.a" ] && PLBF+=(-lssl -lcrypto)
RT=$("$CLANG" --print-resource-dir)/lib/linux/libclang_rt.builtins-x86_64.a
SYS=(); for a in "$SDK/target/lib/libunwind.a" "$SDK/target/lib/libc++abi.a" "$SDK/target/lib/libc++.a" "$RT"; do
    [ -f "$a" ] && SYS+=("$a") || echo "(not found, skipped: $a)"
done
if "$SDK/bin/prospero-lld" --error-limit=0 -T "$L/ps5-pie-iptv.ld" --eh-frame-hdr "${WR[@]}" \
    --version-script "$N/app-symbols.map" -e _start -u ps5_agc_gate2_run -o "$L/iptv-trial.elf" \
    "$L/app_crt.o" "$L/app_cpp_runtime.o" "$L/app_heap.o" "$L/runtime_shims.o" "$L/cts_runtime_shims.o" \
    -L "$SDK/target/lib" -L "$GLSDK/lib" \
    --start-group "$B/libiptv.a" "${FF[@]}" "$SDLSDK/lib/libSDL2.a" -lPS5OpenGL "${PLBF[@]}" "${SYS[@]}" --end-group \
    --as-needed "$SDK"/target/lib/*.so "$GLSDK/lib/libSceAgc.so" "$GLSDK/lib/libSceAgcDriver.so" 2>"$L/trial.err"; then
    echo "LINK OK: $(du -h "$L/iptv-trial.elf" | cut -f1)"
else
    echo "undefined: $(grep -c 'undefined symbol' "$L/trial.err")   duplicate: $(grep -c 'duplicate symbol' "$L/trial.err")"
    echo "--- undefined symbols (and one place each is used) ---"
    awk '/undefined symbol: /{sub(/^.*undefined symbol: /,""); s=$0; next}
         s!="" && />>> referenced by/{r=$0; sub(/^>>> referenced by /,"",r); sub(/:\(.*$/,"",r); n=split(r,p,"/"); print s "   <- " p[n]; s=""}' "$L/trial.err" \
        | sort -u | cut -c1-170 | head -130
    echo "--- duplicate symbols ---"
    grep 'duplicate symbol' "$L/trial.err" | sed 's/^.*duplicate symbol: //' | sort -u | head -25
    echo "--- other linker errors ---"
    grep 'error:' "$L/trial.err" | grep -vE 'undefined symbol|duplicate symbol' | sort -u | cut -c1-200 | head -12
    echo "=== STOPPED: link errors above ==="; exit 1
fi

# Optional functions that exist on no PS5 system module are pinned to "absent".
"$SDK/bin/llvm-nm" -D --defined-only "$SDK"/target/lib/*.so "$GLSDK/lib/libSceAgc.so" "$GLSDK/lib/libSceAgcDriver.so" 2>/dev/null \
    | awk 'NF==3 {sub(/@.*/,"",$3); print $3}' | sort -u > "$L/stub-exports.txt"
mapfile -t WEAK < <("$SDK/bin/llvm-nm" --undefined-only "$L/iptv-trial.elf" 2>/dev/null \
    | awk 'NF==2 && $1 ~ /^[wv]$/ {print $2}' | sort -u | grep -Fxvf "$L/stub-exports.txt")
echo "optional functions absent on the console: ${WEAK[*]:-none}"

echo "=== 4. BUILD THE TITLE ==="
APP=$B/title
rm -rf "$APP/src" "$APP/vendor" "$APP/assets" "$APP/dist" "$APP/sce_sys" "$APP/tooling" "$APP/tools" "$APP/runtime"
mkdir -p "$APP/src" "$APP/vendor" "$APP/assets"
cp "$R/Makefile" "$APP/Makefile"
for d in runtime sce_sys tooling tools; do mkdir -p "$APP/$d"; cp -a "$R/$d/." "$APP/$d/"; done
rm -f "$APP/sce_sys/snd0.at9"
# The app's own home-screen icon, used when it is the size the console expects (the same as the one it replaces).
if [ -f "$HERE/sce_sys/icon0.png" ] && [ -z "${IPTV_NO_ICON:-}" ]; then
    want=$(file -b "$APP/sce_sys/icon0.png" 2>/dev/null | grep -oE '[0-9]+ x [0-9]+' | head -1)
    have=$(file -b "$HERE/sce_sys/icon0.png" | grep -oE '[0-9]+ x [0-9]+' | head -1)
    if [ -z "$want" ] || [ "$want" = "$have" ]; then cp -f "$HERE/sce_sys/icon0.png" "$APP/sce_sys/icon0.png"; echo "icon: the app's own ($have)"
    else echo "icon: kept the default (it is $want, the app's is $have)"; fi
fi
# The pictures the console shows behind the app on its home screen and while it starts. The console
# wants them as 3840x2160 BC7 textures (the build checks this). The app's own was made that way
# in advance, by the same scheme the EmulationStation port's artwork script uses.
if [ -f "$HERE/sce_sys/pic0.dds" ] && [ -z "${IPTV_NO_ICON:-}" ]; then
    cp -f "$HERE/sce_sys/pic0.dds" "$APP/sce_sys/pic0.dds"
    cp -f "$HERE/sce_sys/pic0.dds" "$APP/sce_sys/pic1.dds"
    echo "background: the app's own (pic0.dds and pic1.dds, $(wc -c < "$HERE/sce_sys/pic0.dds" | tr -d ' ') bytes each)"
fi
if [ ! -x "$APP/.deps/native/ps5-payload-sdk/bin/prospero-lld" ]; then
    mkdir -p "$APP/.deps"; cp -a "$R/.deps/native" "$APP/.deps/native"
fi
ASDK=$APP/.deps/native/ps5-payload-sdk
cp "$GLSDK/lib/libSceAgc.so" "$GLSDK/lib/libSceAgcDriver.so" "$ASDK/target/lib/"

HS=$APP/tooling/native/sce_module_writer.cpp
HD='write_u64(result.data, result.heap_size, std::numeric_limits<std::uint64_t>::max());'
if [ "$(grep -Fc "$HD" "$HS")" = 1 ]; then sed -i "s/$HD/write_u64(result.data, result.heap_size, 0x10000000ULL);/" "$HS"
else echo "WARNING: heap-size line not found in sce_module_writer.cpp (boilerplate differs from the expected one)"; fi
LS=$APP/tools/build.sh
if [ "$(grep -Fc -- '--eh-frame-hdr \' "$LS")" = 1 ]; then
    sed -i "s/--eh-frame-hdr \\\\/--eh-frame-hdr ${WR[*]} \\\\/" "$LS"
else echo "WARNING: link line not found in tools/build.sh (boilerplate differs from the expected one)"; fi
cp "$R/tooling/native/ps5-pie.ld" "$APP/tooling/native/ps5-pie-base.ld"
cp "$GLSRC/native-app/ps5-pie.ld" "$GLSRC/native-app/app-symbols.map" "$APP/tooling/native/"
cp "$GLSRC/native-app/runtime_shims.c" "$GLSRC/native-app/cts_runtime_shims.c" "$GLSRC/native-app/app_heap.c" "$APP/src/"
EX=$R/examples/sandbox-elevation
if [ -f "$EX/src/elevation.cpp" ] && [ -f "$EX/elevation.hpp" ] && [ -f "$EX/protocol.hpp" ]; then
    cp "$EX/src/elevation.cpp" "$APP/src/elevation.cpp"
    cp "$EX/elevation.hpp" "$EX/protocol.hpp" "$APP/"
    if [ -d "$R/.deps/lapy" ] && [ ! -d "$APP/.deps/lapy" ]; then cp -a "$R/.deps/lapy" "$APP/.deps/lapy"; fi
    export APP_LAPY_HELPER=1
fi

python3 - "$GLSRC/native-app/param.json" "$APP/sce_sys/param.json" "$TID" <<'PY'
import json, sys
meta = json.load(open(sys.argv[1]))
tid = sys.argv[3]
meta["titleId"] = tid
meta["conceptId"] = tid[4:]
meta["contentId"] = f"UP9000-{tid}_00-IPTVPS5000000000"
meta["localizedParameters"]["en-US"]["titleName"] = "IPTV"
json.dump(meta, open(sys.argv[2], "w"), indent=2)
PY
python3 "$GLSRC/tools/native-display-metadata.py" "$APP/sce_sys/param.json" --sdk-prefix "$GLSDK" || echo "WARNING: display metadata step failed"

cp "$FONT" "$APP/assets/font.ttf"
# the heavy weight of the same font, for titles in the TV layout: looked for beside the ordinary one
BOLD=""
for f in "$HERE/font-bold.ttf" "${FONT%.ttf}-Bold.ttf" "$(dirname "$FONT")/DejaVuSans-Bold.ttf" /usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf \
         /usr/share/fonts/truetype/noto/NotoSans-Bold.ttf /usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf; do
    [ -f "$f" ] && { BOLD=$f; break; }
done
if [ -n "$BOLD" ]; then cp "$BOLD" "$APP/assets/font-bold.ttf"; echo "font: bold weight from $BOLD"; else echo "font: no bold weight found (titles will use the ordinary weight)"; fi
# the pointers that come with the app (arrows, and any cursor files placed in the project's pointers folder)
if [ -d "$HERE/pointers" ]; then mkdir -p "$APP/assets/pointers"; cp "$HERE/pointers/"* "$APP/assets/pointers/"; echo "pointers: $(ls "$HERE/pointers" | grep -ciE '\.(cur|ani|ico|png)$') bundled"; fi
[ -f "$HERE/playlist.m3u" ] && cp "$HERE/playlist.m3u" "$APP/assets/playlist.m3u"
[ -f "$HERE/playlist-url.txt" ] && cp "$HERE/playlist-url.txt" "$APP/assets/playlist-url.txt"

{
    printf 'SEARCH_DIR("%s")\n' "$ASDK/target/lib" "$GLSDK/lib" "$PROOT/user/homebrew/lib"
    for w in "${WEAK[@]}"; do printf '%s = 0;\n' "$w"; done
    printf 'EXTERN(ps5_agc_gate2_run)\nGROUP (\n'
    printf '  "%s"\n' "$B/libiptv.a" "${FF[@]}" "$SDLSDK/lib/libSDL2.a" "$GLSDK/lib/libPS5OpenGL.a"
    printf '%s\n' "${PLBF[@]}" | grep -E '^-l' | grep -vE '^-l(Sce|kernel)' | awk '!seen[$0]++ {print "  " $0}'
    printf '  "%s"\n' "${SYS[@]}"
    printf ')\n'
} > "$APP/vendor/libiptv_group.a"
printf 'APP_STATIC_ARCHIVES = vendor/libiptv_group.a\n' > "$APP/.env"

if make -C "$APP" --no-print-directory app > "$B/title-build.log" 2>&1 && [ -s "$APP/dist/$TID/eboot.bin" ]; then
    echo "TITLE BUILT: $APP/dist/$TID"
    echo "  eboot.bin $(du -h "$APP/dist/$TID/eboot.bin" | cut -f1), folder $(du -sh "$APP/dist/$TID" | cut -f1)"
    echo "  to send it to the console:"
    echo "  make -C $APP deploy PS5_HOST=${PS5_HOST:-10.230.88.228}"
else
    echo "TITLE BUILD FAILED. Last lines of $B/title-build.log:"
    tail -40 "$B/title-build.log" | cut -c1-220
fi
echo "=== DONE ==="
