# IPTV for PS5

A native IPTV player for the PS5: M3U playlists, Xtream Codes accounts and Stalker / Ministra portals; live TV with a programme guide and catch-up; movies and series with resume; two layouts (Classic and TV style).

## Build and deploy

```bash
bash build.sh
make -C build/title deploy PS5_HOST=<console address>
```

`build.sh` expects the toolchain and kit described at its top (`~/ps5-workspace/es-port`, `~/Downloads/es-ps5-kit`). `tools/ffmpeg-more-formats.sh` rebuilds FFmpeg with DTS, TrueHD and picture subtitles.

## Where things are

| Path | What it is |
|---|---|
| `src/main.cpp` | The list of the app's parts, in order |
| `src/app/` | The app itself: screens, input, state. See `src/app/README.md` |
| `src/gfx.*` | Drawing: shapes, text, pictures, video |
| `src/player.*` | Playback: decoding, sound, subtitles, timing |
| `src/sources.*` | Playlists and providers: M3U, Xtream, Stalker; guide, search, catch-up |
| `src/net.*`, `src/json.*`, `src/playlist.*` | Downloads, JSON reading, M3U reading |
| `src/logos.*`, `src/images.*`, `src/cursors.*` | Logos and posters, picture decoding, pointer files |
| `src/webadd.*`, `src/qr.*` | The Remote page served to phones, and its QR code |
| `src/xmltv.*` | Guides for M3U playlists, with a gzip unpacker |
| `sce_sys/` | The console's icon and background artwork |

## On the console

Everything the app keeps is in `/data/iptv/`: playlists, settings, favorites and history (per playlist and per profile), `themes/` for extra colour themes, `pointers/` for custom pointers, `cache/` for saved copies, and `iptv-log.txt`.
