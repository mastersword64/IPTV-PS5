/* IPTV for PS5: a native player for M3U playlists, modelled on IPTVnator.
 *
 * It is started by the same PS5 entry point the EmulationStation port uses
 * (es_ps5_main.cpp from the kit), which calls es_main() once the console side
 * is ready. It manages several playlists (M3U files, M3U web addresses, Xtream Codes accounts),
 * browses groups and channels in IPTVnator's layout, and plays streams. */
#include "gfx.h"
#include "cursors.h"
#include "images.h"
#include "log.h"
#include "logos.h"
#include "net.h"
#include "player.h"
#include "playlist.h"
#include "sources.h"
#include "threads.h"
#include "qr.h"
#include "webadd.h"
#include "xmltv.h"

#include <SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <functional>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unordered_map>
#include <deque>
#include <unordered_set>
#include <vector>

// The app itself is one program in many parts. They are read in this order and compiled as a
// single unit, exactly as when they were one long file: each part can use what the parts before
// it define. See app/README.md for what lives where.
#include "app/01_log_themes.inc"                 // the log; colour themes, built in and from files
#include "app/02_settings_profiles.inc"          // settings kept between runs; profiles
#include "app/03_libraries.inc"                  // the three libraries (live, movies, series); loading playlists; saved copies
#include "app/04_input_pointer.inc"              // controller and keyboard input; the on-screen pointer
#include "app/05_lists.inc"                      // lists and sections; categories fetched on demand; hidden groups; series and episodes
#include "app/06_playback.inc"                   // opening libraries; starting, following and ending playback; resume and next episode
#include "app/07_guide_classic_screens.inc"      // programme guide lookups; the classic layout: top bar, tabs, columns, account
#include "app/08_player_controls.inc"            // full-screen player controls, track menus, subtitles
#include "app/09_wall_details.inc"               // small shared pieces; the classic poster wall and title pages
#include "app/10_home.inc"                       // Home: what it shows and how it is built (classic drawing)
#include "app/11_tv_home.inc"                    // TV layout: rail, cards, focus frame, Home
#include "app/12_tv_live.inc"                    // TV layout: Live TV
#include "app/13_tv_wall.inc"                    // TV layout: Movies and Series
#include "app/14_tv_details_account.inc"         // TV layout: title pages, Account, welcome
#include "app/15_guide_grid.inc"                 // the guide grid and catch-up
#include "app/16_search.inc"                     // Search
#include "app/17_browse.inc"                     // the More menu; arrivals; the resume question; input and drawing for the main screens
#include "app/18_keyboard_forms.inc"             // opening a playlist; the on-screen keyboard; the add-playlist form
#include "app/19_settings_tabs.inc"              // Settings: playlists, Remote, Profiles, Pointer, Playback
#include "app/20_settings_groups_themes.inc"     // Settings: Groups by country, PIN, Themes and layout
#include "app/21_settings_screen.inc"            // Settings: the screen itself and its input
#include "app/22_main.inc"                       // start-up and the main loop
