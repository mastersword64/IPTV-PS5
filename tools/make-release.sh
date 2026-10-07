#!/usr/bin/env bash
#
# Packs what build.sh built as a release zip that others can install without compiling:
#   iptv-ps5-<version>-<title id>.zip, holding the title folder (with its Makefile, whose
#   "deploy" sends it to a console), NOTICES.md and the changes.
#
# It refuses to run if a playlist of yours is in the project folder, because build.sh would
# have built it into the app.
#
#   usage: bash build.sh && bash tools/make-release.sh
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$here"
for f in playlist.m3u playlist-url.txt; do
    if [ -e "$f" ]; then
        echo "STOP: $f is in the project folder, so the build has your playlist inside it."
        echo "Move it away (mv $f ~/), run 'bash build.sh' again, then run this again."
        exit 1
    fi
done
[ -d build/title ] || { echo "Nothing built yet: run 'bash build.sh' first."; exit 1; }
if find build/title -name "playlist*" | grep -q .; then
    echo "STOP: a playlist file is inside build/title:"
    find build/title -name "playlist*"
    echo "Delete the build folder (rm -rf build), run 'bash build.sh' again, then run this again."
    exit 1
fi
version=$(sed -n 's/.*kAppVersion = "\([^"]*\)".*/\1/p' src/app/02_settings_profiles.inc)
tid=${IPTV_TITLE_ID:-PPSA99779}
out="iptv-ps5-$version-$tid.zip"
work=$(mktemp -d)
cp -a build/title "$work/title"
cp NOTICES.md CHANGELOG.md "$work/"
{
    echo "IPTV for PS5 $version"
    echo
    echo "Needs a PlayStation 5 able to run homebrew, with its FTP server running, and a computer"
    echo "on the same network with \"make\" and \"curl\"."
    echo
    echo "    make -C title deploy PS5_HOST=<the console's address>"
    echo
    echo "Then start \"IPTV for PS5\" from the console's home screen and add a playlist in Settings."
    echo "This download contains no playlist and no channels."
} > "$work/INSTALL.txt"
rm -f "$out"
( cd "$work" && zip -qr "$here/$out" . )
rm -rf "$work"
echo "Made $out ($(du -h "$out" | cut -f1))"
