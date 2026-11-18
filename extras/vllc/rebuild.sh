#!/bin/bash
set -e
cd /build/vlc
while read -r f; do f=${f%$'\r'}; [ -z "$f" ] && continue; mkdir -p "$(dirname "$f")"; sed 's/\r$//' "/src/$f" > "$f"; done < /work/changed.txt
cd win64
make -j20 2>&1 | grep -E "error|Error|warning: unused|seek_preview" | grep -v "^make.*Leaving\|Entering" | head -40
make package-win-common >/dev/null 2>&1 && echo PACKAGED
cd vlc-4.0.0-dev && find . -name "*.dll" -o -name "*.exe" | xargs x86_64-w64-mingw32-strip --strip-debug && du -sh .
