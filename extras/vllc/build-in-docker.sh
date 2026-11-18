#!/bin/bash
set -e
git config --global --add safe.directory '*'
git config --global core.autocrlf false
cd /build
if [ ! -d vlc/.git ]; then
  git clone -q /src vlc
  cd vlc && git apply --whitespace=nowarn /work/vlc-mods.patch && cd ..
fi
cd vlc
git --no-pager diff --stat | tail -1
rm -rf contrib/contrib-win64 contrib/x86_64-w64-mingw32
export VLC_PREBUILT_CONTRIBS_URL="https://artifacts.videolan.org/vlc/win64/vlc-contrib-x86_64-w64-mingw32-07c4d49a310a52ab81be6b1a6688dbaf732aa7f2.tar.zst"
extras/package/win32/build.sh -a x86_64 -p -r
cd win64
make package-win-common
ls -la vlc-*/ | head
du -sh vlc-*/
