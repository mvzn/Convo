#!/usr/bin/env bash
#
# Installs Convo.vst3 for the current user.
#
# Usage:   ./install.sh            install to ~/.vst3 (per-user, no root)
#          ./install.sh --system   install to /usr/lib/vst3 (all users, needs sudo)
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/Convo.vst3"

if [[ "${1:-}" == "--system" ]]; then
    DEST="/usr/lib/vst3"
else
    DEST="${1:-$HOME/.vst3}"
fi

if [[ ! -d "$SRC" ]]; then
    echo "error: Convo.vst3 not found next to this script" >&2
    exit 1
fi

mkdir -p "$DEST"
rm -rf "$DEST/Convo.vst3"
cp -R "$SRC" "$DEST/"
echo "Installed Convo.vst3 -> $DEST"
echo "Rescan plugins in your DAW to pick it up."
