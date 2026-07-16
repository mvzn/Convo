#!/usr/bin/env bash
#
# Builds the macOS distributables for Convo:
#   dist/Convo-<version>-macOS.pkg        installer (VST3 -> /Library/Audio/Plug-Ins/VST3,
#                                         AU -> /Library/Audio/Plug-Ins/Components)
#   dist/Convo-<version>-macOS-VST3.zip   bare VST3 bundle
#   dist/Convo-<version>-macOS-AU.zip     bare AU bundle
#
# Usage:   packaging/macos/build-pkg.sh [version]
#
# Code signing + notarization are OPT-IN via env vars (left empty = unsigned, fine for
# local testing; required before public macOS distribution so Gatekeeper won't block it):
#   CODESIGN_IDENTITY   "Developer ID Application: Name (TEAMID)"  - signs .vst3/.component
#   PKG_SIGN_IDENTITY   "Developer ID Installer: Name (TEAMID)"    - signs the .pkg
# Notarization (runs only when CODESIGN_IDENTITY is set), either credential style:
#   NOTARY_PROFILE      a `xcrun notarytool store-credentials` profile name (local dev)
#   ASC_API_KEY_P8 + ASC_KEY_ID + ASC_ISSUER_ID   App Store Connect API key (CI secrets)
#
# Pipeline order (per Apple's rules): sign bundles -> notarize -> staple bundles ->
# zip bundles for distribution -> build pkg from the stapled bundles -> sign pkg ->
# notarize pkg -> staple pkg. Never modify an artifact after signing.
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

# --- signing / notarization helpers -----------------------------------------

codesign_bundle() {
    local bundle="$1"
    if [[ -n "${CODESIGN_IDENTITY:-}" ]]; then
        echo "Signing $bundle"
        # No --deep: it is deprecated for distribution signing and applies the wrong
        # entitlements to nested code. The bundles hold a single Mach-O each, so signing
        # the bundle root is the complete, notarization-clean signature.
        codesign --force --options runtime --timestamp \
                 --sign "$CODESIGN_IDENTITY" "$bundle"
    else
        echo "CODESIGN_IDENTITY not set - leaving $(basename "$bundle") unsigned"
    fi
}

have_notary_creds() {
    [[ -n "${NOTARY_PROFILE:-}" ]] && return 0
    [[ -n "${ASC_API_KEY_P8:-}" && -n "${ASC_KEY_ID:-}" && -n "${ASC_ISSUER_ID:-}" ]]
}

notarize() {
    local path="$1"
    echo "Notarizing $(basename "$path")"
    if [[ -n "${NOTARY_PROFILE:-}" ]]; then
        xcrun notarytool submit "$path" --keychain-profile "$NOTARY_PROFILE" --wait
    else
        if [[ ! -f "$WORK/AuthKey.p8" ]]; then
            printf '%s' "$ASC_API_KEY_P8" > "$WORK/AuthKey.p8"
            chmod 600 "$WORK/AuthKey.p8"
        fi
        xcrun notarytool submit "$path" --key "$WORK/AuthKey.p8" \
              --key-id "$ASC_KEY_ID" --issuer "$ASC_ISSUER_ID" --wait
    fi
}

# --- stage + sign bundles -----------------------------------------------------

VST3_ROOT="$WORK/vst3/Library/Audio/Plug-Ins/VST3"
mkdir -p "$VST3_ROOT"
cp -R "$ART/VST3/Convo.vst3" "$VST3_ROOT/"
cp "$ROOT/LICENSE" "$VST3_ROOT/Convo.vst3/Contents/Resources/LICENSE.txt" 2>/dev/null || true
codesign_bundle "$VST3_ROOT/Convo.vst3"
BUNDLES=("$VST3_ROOT/Convo.vst3")
PKG_REFS=(Convo-VST3.pkg)

# AU only exists when built on macOS
if [[ -d "$ART/AU/Convo.component" ]]; then
    AU_ROOT="$WORK/au/Library/Audio/Plug-Ins/Components"
    mkdir -p "$AU_ROOT"
    cp -R "$ART/AU/Convo.component" "$AU_ROOT/"
    cp "$ROOT/LICENSE" "$AU_ROOT/Convo.component/Contents/Resources/LICENSE.txt" 2>/dev/null || true
    codesign_bundle "$AU_ROOT/Convo.component"
    BUNDLES+=("$AU_ROOT/Convo.component")
    PKG_REFS+=(Convo-AU.pkg)
fi

# --- notarize + staple the bundles (one submission covers every binary) -------

if [[ -n "${CODESIGN_IDENTITY:-}" ]] && have_notary_creds; then
    NZ="$WORK/notarize-bundles"
    mkdir -p "$NZ"
    for b in "${BUNDLES[@]}"; do cp -R "$b" "$NZ/"; done
    ditto -c -k "$NZ" "$WORK/bundles.zip"
    notarize "$WORK/bundles.zip"
    for b in "${BUNDLES[@]}"; do xcrun stapler staple "$b"; done
elif [[ -n "${CODESIGN_IDENTITY:-}" ]]; then
    echo "No notarization credentials - bundles signed but NOT notarized (Gatekeeper will block)"
fi

# --- bare-bundle zips (the stapled bundles, sellable as-is) -------------------

ditto -c -k --keepParent "$VST3_ROOT/Convo.vst3" "$DIST/Convo-$VERSION-macOS-VST3.zip"
echo "Built $DIST/Convo-$VERSION-macOS-VST3.zip"
if [[ -d "${AU_ROOT:-}/Convo.component" ]]; then
    ditto -c -k --keepParent "$AU_ROOT/Convo.component" "$DIST/Convo-$VERSION-macOS-AU.zip"
    echo "Built $DIST/Convo-$VERSION-macOS-AU.zip"
fi

# --- component pkgs from the stapled bundles ----------------------------------

pkgbuild --root "$WORK/vst3" --identifier "$PKG_IDENT.vst3" \
         --version "$VERSION" --install-location "/" "$WORK/Convo-VST3.pkg"
if [[ -d "${AU_ROOT:-}/Convo.component" ]]; then
    pkgbuild --root "$WORK/au" --identifier "$PKG_IDENT.au" \
             --version "$VERSION" --install-location "/" "$WORK/Convo-AU.pkg"
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

# --- notarize + staple the pkg -------------------------------------------------

if [[ -n "${PKG_SIGN_IDENTITY:-}" ]] && have_notary_creds; then
    notarize "$OUT"
    xcrun stapler staple "$OUT"
    echo "Verifying"
    spctl -a -vv -t install "$OUT"
    xcrun stapler validate "$VST3_ROOT/Convo.vst3"
else
    echo "PKG_SIGN_IDENTITY/notary credentials not set - skipping pkg notarization (do this before public release)"
fi
