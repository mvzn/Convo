#!/usr/bin/env bash
#
# Builds a macOS installer (.pkg) for Convo: VST3 -> /Library/Audio/Plug-Ins/VST3
# and AU -> /Library/Audio/Plug-Ins/Components.
#
# Usage:   packaging/macos/build-pkg.sh [version]
# Output:  dist/Convo-<version>-macOS.pkg
#
# Code signing + notarization are OPT-IN via env vars (left empty = unsigned, fine for
# local testing; required before public macOS distribution so Gatekeeper won't block it):
#   CODESIGN_IDENTITY   "Developer ID Application: Name (TEAMID)"   - signs the .vst3/.component
#   PKG_SIGN_IDENTITY   "Developer ID Installer: Name (TEAMID)"     - signs the .pkg
#   NOTARY_PROFILE      a `xcrun notarytool store-credentials` profile name - notarize + staple
set -euo pipefail

VERSION="${1:-1.0.0}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
ART="$BUILD_DIR/Convo_artefacts/Release"
DIST="$ROOT/dist"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$DIST"

PKG_IDENT="com.mvzn.Convo"

codesign_bundle() {
    local bundle="$1"
    if [[ -n "${CODESIGN_IDENTITY:-}" ]]; then
        echo "Signing $bundle"
        codesign --force --deep --options runtime --timestamp \
                 --sign "$CODESIGN_IDENTITY" "$bundle"
    else
        echo "CODESIGN_IDENTITY not set - leaving $(basename "$bundle") unsigned"
    fi
}

# --- component pkg: VST3 (always present) ---
VST3_ROOT="$WORK/vst3/Library/Audio/Plug-Ins/VST3"
mkdir -p "$VST3_ROOT"
cp -R "$ART/VST3/Convo.vst3" "$VST3_ROOT/"
cp "$ROOT/LICENSE" "$VST3_ROOT/Convo.vst3/Contents/Resources/LICENSE.txt" 2>/dev/null || true
codesign_bundle "$VST3_ROOT/Convo.vst3"
pkgbuild --root "$WORK/vst3" --identifier "$PKG_IDENT.vst3" \
         --version "$VERSION" --install-location "/" "$WORK/Convo-VST3.pkg"
PKG_REFS=(Convo-VST3.pkg)

# --- component pkg: AU (only if it was built, i.e. on macOS) ---
if [[ -d "$ART/AU/Convo.component" ]]; then
    AU_ROOT="$WORK/au/Library/Audio/Plug-Ins/Components"
    mkdir -p "$AU_ROOT"
    cp -R "$ART/AU/Convo.component" "$AU_ROOT/"
    cp "$ROOT/LICENSE" "$AU_ROOT/Convo.component/Contents/Resources/LICENSE.txt" 2>/dev/null || true
    codesign_bundle "$AU_ROOT/Convo.component"
    pkgbuild --root "$WORK/au" --identifier "$PKG_IDENT.au" \
             --version "$VERSION" --install-location "/" "$WORK/Convo-AU.pkg"
    PKG_REFS+=(Convo-AU.pkg)
fi

# --- distribution wrapper ---
# The license page shows the AGPLv3; customize="allow" lets the user untick VST3/AU; and
# enable_currentUserHome offers "Install for me only" in Change Install Location…, which
# retargets /Library/Audio/Plug-Ins -> ~/Library/Audio/Plug-Ins (the per-user folders).
RES="$WORK/resources"
mkdir -p "$RES"
cp "$ROOT/LICENSE" "$RES/LICENSE.txt"

choice_title() {
    case "$1" in
        Convo-VST3.pkg) echo "VST3 plug-in" ;;
        Convo-AU.pkg)   echo "Audio Unit (AU) plug-in" ;;
        *)              echo "$1" ;;
    esac
}

DIST_XML="$WORK/distribution.xml"
{
    echo '<?xml version="1.0" encoding="utf-8"?>'
    echo '<installer-gui-script minSpecVersion="1">'
    echo "    <title>Convo $VERSION</title>"
    echo '    <license file="LICENSE.txt"/>'
    echo '    <options customize="allow" require-scripts="false" hostArchitectures="x86_64,arm64"/>'
    echo '    <domains enable_localSystem="true" enable_currentUserHome="true"/>'
    echo '    <choices-outline>'
    for ref in "${PKG_REFS[@]}"; do echo "        <line choice=\"$ref\"/>"; done
    echo '    </choices-outline>'
    for ref in "${PKG_REFS[@]}"; do
        echo "    <choice id=\"$ref\" visible=\"true\" title=\"$(choice_title "$ref")\"><pkg-ref id=\"$ref\"/></choice>"
        echo "    <pkg-ref id=\"$ref\">$ref</pkg-ref>"
    done
    echo '</installer-gui-script>'
} > "$DIST_XML"

OUT="$DIST/Convo-$VERSION-macOS.pkg"
PB_ARGS=(--distribution "$DIST_XML" --package-path "$WORK" --resources "$RES")
[[ -n "${PKG_SIGN_IDENTITY:-}" ]] && PB_ARGS+=(--sign "$PKG_SIGN_IDENTITY")
productbuild "${PB_ARGS[@]}" "$OUT"
echo "Built $OUT"

# --- notarize + staple (opt-in) ---
if [[ -n "${NOTARY_PROFILE:-}" ]]; then
    echo "Notarizing $OUT"
    xcrun notarytool submit "$OUT" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$OUT"
else
    echo "NOTARY_PROFILE not set - skipping notarization (do this before public release)"
fi
