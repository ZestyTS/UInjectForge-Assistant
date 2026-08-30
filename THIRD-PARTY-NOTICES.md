# Third-party notices

The Assistant's original source is MIT licensed. Dependencies retain their own
terms. The Wii U system font is obtained on the console; no Nintendo font file
is included in this repository.

The retained Docker toolchain includes:
- SDL2 2.0.9-3 and SDL2_ttf 2.0.15-2: zlib-style terms; see licenses/.
- FreeType 2.9.1-2: FreeType License (FTL option), see licenses/FreeType.txt.
  Portions of this software are copyright The FreeType Project (www.freetype.org).
  All rights reserved.
- libpng 1.6.37-1, bzip2 1.0.6-2, zlib 1.2.11-2: notices in licenses/.
- mbed TLS 2.28.1-1: Apache-2.0, see licenses/mbedTLS.txt.
- devkitPro wut: zlib license, see licenses/wut.txt.

Notice texts were collected from wiiuenv/devkitppc:20220907 and wut commit
0edd2e9c5c24d128714a6ef892c4b77bb5e20fce. This records the existing build
environment; it is not a claim that its library versions are current.
Recheck versions and notices when changing the toolchain.
