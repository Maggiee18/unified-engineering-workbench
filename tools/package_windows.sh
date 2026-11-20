#!/usr/bin/env bash
# Packaging script (no backend logic): cross-compiles the Windows x64 release
# with MinGW-w64, runs the Windows test binary under Wine if available, and
# assembles CandidateID_Workbench/ + CandidateID_Workbench.zip in dist/.
#   tools/package_windows.sh [CandidateID]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ID="${1:-CandidateID}"
NAME="${ID}_Workbench"
DIST="$ROOT/dist"
STAGE="$DIST/$NAME"

cmake -S "$ROOT" -B "$ROOT/build-win" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE="$ROOT/tools/mingw-w64-x86_64.cmake" >/dev/null
cmake --build "$ROOT/build-win"
x86_64-w64-mingw32-strip "$ROOT"/build-win/*.exe

echo "== imported DLLs (must be Windows system DLLs only) =="
x86_64-w64-mingw32-objdump -p "$ROOT/build-win/Launch.exe" | grep "DLL Name"

rm -rf "$STAGE" "$DIST/$NAME.zip"
mkdir -p "$STAGE/bin"
cp "$ROOT/build-win/Launch.exe" "$STAGE/"
cp "$ROOT/build-win/workbench_tests.exe" "$ROOT/build-win/workbench_cli.exe" "$STAGE/bin/"
cp "$ROOT"/tools/windows/*.cmd "$STAGE/bin/"
# Source tree, evidence and history (tracked files only, plus .git).
git -C "$ROOT" archive --format=tar HEAD | tar -x -C "$STAGE"
cp -r "$ROOT/.git" "$STAGE/.git"
mkdir -p "$STAGE/docs"
git -C "$ROOT" bundle create "$STAGE/docs/workbench.bundle" --all >/dev/null 2>&1
git -C "$ROOT" log --stat --date=iso > "$STAGE/docs/COMMIT_LOG.txt"
# Windows line endings for the batch files.
sed -i 's/\r*$/\r/' "$STAGE"/bin/*.cmd
(cd "$DIST" && zip -qr "$NAME.zip" "$NAME")
ls -la "$DIST/$NAME.zip"
