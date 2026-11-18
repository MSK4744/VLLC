# VLLC

VLLC is a customized build of VLC 4.0-dev (upstream `videolan/vlc` master) for Windows.

## Changes from upstream

- Dark ("Night") theme by default, with a layered graphite palette and rounder corners.
- A **Theme color** option under Preferences → Interface: Orange (default) or Blue.
- Fast seeking while you drag the timeline, then a precise seek when you release it.
- A YouTube-style thumbnail preview when you hover over the timeline (local files only).
- In fullscreen, the controls span the full width over a transparent background.
- No update check, and no online metadata collection.
- The window no longer auto-resizes to the video size.
- Renamed to VLLC (window title, tray, About, exe resources).
- Fixed "Open with" / double-click when VLLC is already running: the new file now plays, the window comes to the front, and the hand-off times out instead of hanging.

## Building (Windows x64, cross-compiled in Docker)

The image is `registry.videolan.org/vlc-debian-win64-posix:20261109074440`.

- `build-in-docker.sh` does a full build with prebuilt contribs.
  - Mount the source at `/src` and a work volume at `/build`.
  - Mount this folder, together with a `vlc-mods.patch`, at `/work`.
- `rebuild.sh` does an incremental rebuild. It copies the files listed in `changed.txt` from `/src` into the build tree.
- After packaging, relink the srt, x265 and gme plugins with `-lgcc_eh -static-libgcc` instead of `-lgcc_s`. Otherwise they fail to load because they depend on `libgcc_s_seh-1.dll`.

## Installer

`gen_iss.py` generates an Inno Setup 6 script that:

- installs per user, side by side with a regular VLC;
- uses portable config;
- installs `vllc.exe`;
- adds "Open with" and Default Apps registration.

Compile the generated script with `ISCC /DSourceDir=<package dir> vllc.iss`.
