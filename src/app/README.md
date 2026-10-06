# How the app's source is laid out

Everything outside this folder is a self-contained module with its own header (`gfx`, `player`, `sources`, `net`, `playlist`, `json`, `logos`, `images`, `cursors`, `qr`, `webadd`, `xmltv`, `threads`).

This folder is the app itself: its screens, its input and its state. The parts are included by `../main.cpp` in the order below and compiled as one unit, so a part may use anything defined in an earlier part. To find something, start from this table.

| File | What it holds |
|---|---|
| `01_log_themes.inc` | The log; colour themes, built in and from files |
| `02_settings_profiles.inc` | Settings kept between runs; profiles |
| `03_libraries.inc` | The three libraries (live, movies, series); loading playlists; saved copies |
| `04_input_pointer.inc` | Controller and keyboard input; the on-screen pointer |
| `05_lists.inc` | Lists and sections; categories fetched on demand; hidden groups; series and episodes |
| `06_playback.inc` | Opening libraries; starting, following and ending playback; resume and next episode |
| `07_guide_classic_screens.inc` | Programme guide lookups; the classic layout: top bar, tabs, columns, account |
| `08_player_controls.inc` | Full-screen player controls, track menus, subtitles |
| `09_wall_details.inc` | Small shared pieces; the classic poster wall and title pages |
| `10_home.inc` | Home: what it shows and how it is built (classic drawing) |
| `11_tv_home.inc` | TV layout: rail, cards, focus frame, Home |
| `12_tv_live.inc` | TV layout: Live TV |
| `13_tv_wall.inc` | TV layout: Movies and Series |
| `14_tv_details_account.inc` | TV layout: title pages, Account, welcome |
| `15_guide_grid.inc` | The guide grid and catch-up |
| `16_search.inc` | Search |
| `17_browse.inc` | The More menu; arrivals; the resume question; input and drawing for the main screens |
| `18_keyboard_forms.inc` | Opening a playlist; the on-screen keyboard; the add-playlist form |
| `19_settings_tabs.inc` | Settings: playlists, Remote, Profiles, Pointer, Playback |
| `20_settings_groups_themes.inc` | Settings: Groups by country, PIN, Themes and layout |
| `21_settings_screen.inc` | Settings: the screen itself and its input |
| `22_main.inc` | Start-up and the main loop |

## Working on it

- A new screen usually means a new part, added to the list in `main.cpp` after the parts it depends on.
- The build compiles every `.cpp` in `src/`; these parts end in `.inc` so that they are compiled only through `main.cpp`.
- Both layouts share all state. Classic drawing is in parts 07 to 10; the TV layout is in parts 11 to 14; the guide grid and Search are drawn one way for both.
