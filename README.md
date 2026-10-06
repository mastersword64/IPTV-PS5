# IPTV for PS5

A native IPTV player for the PS5, modelled on IPTVnator. It plays playlists you supply and includes no channels or content of its own.

## Features

**Playlists**
- M3U and M3U8 playlists from a web address or a file, Xtream Codes accounts, and Stalker / Ministra portals
- Several playlists side by side, each with its own favorites and history
- Add playlists from a phone or computer: the console shows a QR code and serves a small web page for typing addresses, sending an M3U file, and saving or restoring a backup

**Live TV**
- Groups and channels, with groups gathered by country (UK, US, AR and so on)
- Programme guide: now and next on every channel, and a full grid of channels against hours
- Catch-up: play back past programmes where the provider keeps them
- Guides for M3U playlists from their XMLTV file, plain or gzip-packed
- Favorites, recently watched, hide a group, a whole country or a single channel, with an optional PIN

**Movies and series**
- Posters, year, rating, length, genres, plot and cast on each title's page
- Resume where you left off, continue watching, watched ticks, next episode plays by itself
- Trailers where the provider gives a playable address
- Search across live channels, movies and series at once

**Playback**
- Software decoding up to 4K HEVC 10-bit, with HDR (PQ and HLG) tone-mapped for an ordinary screen
- Audio track and subtitle selection, text and picture-based subtitles, timing adjustment for both
- Picture fit (fit, zoom, stretch), smoothing for interlaced broadcasts, even film cadence
- DTS, TrueHD and FLAC sound with the optional FFmpeg rebuild in `tools/`
- Sleep timer

**Interface**
- Two layouts: Classic (bar, tabs and columns) and TV style (full-screen artwork, side rail, white focus)
- Home screen with continue watching and favorites, grouped by country
- Colour themes, built in and loadable from files
- Profiles, each with its own favorites, history and hidden groups
- An on-screen pointer driven by the left stick, with adjustable speed, size and shape, and custom pointer files
- Saved copies of category lists, so the app opens at once and refreshes in the background

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

## Controls

| Button | Does |
|---|---|
| D-pad / left stick | Move, or steer the pointer |
| Cross | Choose, play |
| Circle | Back |
| Square | Favorite; clears the screen during playback |
| Triangle | Filter the list, or a new search |
| L1 / R1 | Next country or category; three hours in the guide |
| L2 / R2 | Switch main screen (Classic layout) |
| Right stick | Scroll |
| OPTIONS | Settings |

## Credits

- Modelled on [IPTVnator](https://github.com/4gray/iptvnator).
- Uses FFmpeg, SDL2, FreeType and curl.
- The pointer files in `pointers/` were drawn for this app.

