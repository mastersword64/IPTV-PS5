# Changes

## 2.0

**Key points**

- **Radio.** A new Radio screen with public stations from the community-run Radio Browser directory.
- **Your phone, tablet or computer as the remote.** The page the console serves is now a full remote control with your channels, movies and series on it.
- **Watch on the device too.** The page can play on the device itself, or hand the stream to any player app.
- **Ready-built download.** Releases now carry a copy of the app that needs no compiling.

**Radio**

- Popular stations, by country, by genre, and search by name; favorites and recently played, kept per profile.
- Shows the song playing when the station announces it; a station keeps playing while you browse other screens.
- Works on consoles set up with a filtering DNS: the app finds the directory without it.
- `tools/ffmpeg-radio-formats.sh` checks that FFmpeg can read plain MP3 and AAC streams, and rebuilds it only if not.

**Remote page**

- Every controller button, with arrows that repeat while held; a computer's arrow keys, Enter and Esc work too.
- Live TV, Movies and Series with channel logos and posters; series list their episodes; films resume or start over.
- Search across channels, films and series, and across radio stations.
- Now-playing bar with previous, pause, stop, next, and a progress bar you can tap to move through a film.
- Stars to add or remove favorites; a box to type into whenever the keyboard is open on the TV.
- A switch between playing on the TV and being asked each time: the TV, the page itself, or a player app on the device.
- A layout for tablets and computers that keeps the remote beside the lists.
- The page runs whenever the app does (it can be switched off in Settings > Remote), and its address carries a key shown only on the console.

**Settings**

- A new About tab: version, credits, and where the data folder and log are.

**Fixes**

- Posters that had scrolled out of view could still be highlighted and opened with the pointer. The pointer now only reaches what is visible, in every scrolling list.
- A small amount of memory was lost each time a background task finished.

**Thanks**

- Radio is possible because of [Radio Browser](https://www.radio-browser.info), a free directory of internet radio stations kept by volunteers.

## 1.0

- First release.
