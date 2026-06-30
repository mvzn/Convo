#!/usr/bin/env bash
#
# Packages the Linux VST3 into a distributable tarball with an installer script.
#
# Usage:   packaging/linux/build-tarball.sh [version]
# Output:  dist/Convo-<version>-Linux.tar.gz
set -euo pipefail

VERSION="${1:-1.0.0}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
ART="$BUILD_DIR/Convo_artefacts/Release/VST3"
DIST="$ROOT/dist"
NAME="Convo-$VERSION-Linux"
STAGE="$(mktemp -d)/$NAME"
trap 'rm -rf "$(dirname "$STAGE")"' EXIT
mkdir -p "$STAGE" "$DIST"

cp -R "$ART/Convo.vst3" "$STAGE/"
cp "$ROOT/packaging/linux/install.sh" "$STAGE/"
cp "$ROOT/LICENSE" "$STAGE/"
chmod +x "$STAGE/install.sh"

tar -czf "$DIST/$NAME.tar.gz" -C "$(dirname "$STAGE")" "$NAME"
echo "Built $DIST/$NAME.tar.gz"
