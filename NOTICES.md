# Notices

IPTV for PS5 itself is released under the MIT License (see `LICENSE` in the source repository).

A ready-built copy of the app has these open-source libraries built into it. Each remains under its own licence; the links lead to the source code and the full licence texts.

| Library | Used for | Licence | Source |
| --- | --- | --- | --- |
| FFmpeg | Reading and decoding video and sound | LGPL 2.1 or later (GPL if built with GPL parts) | https://ffmpeg.org |
| SDL2 | Sound output and the controller | zlib licence | https://www.libsdl.org |
| FreeType | Lettering | FreeType License (FTL) | https://freetype.org |
| curl | Downloads | curl licence (MIT-style) | https://curl.se |
| The TLS library that curl and FFmpeg were built with | Secure connections | its own licence | see the build scripts |
| DejaVu Sans (or the font chosen at build time) | The app's font | Bitstream Vera / DejaVu licence | https://dejavu-fonts.github.io |

The app is linked against FFmpeg, whose licence asks that you be able to rebuild the app with your own copy of it: the complete source of this app and its build script are in the repository this release came from.

Radio stations are listed by Radio Browser (https://www.radio-browser.info), a community directory. The stations and their streams belong to their broadcasters.

This is an independent homebrew project. It is not affiliated with, endorsed by, or supported by Sony Interactive Entertainment, the IPTVnator project, Radio Browser, or any of the projects above.
