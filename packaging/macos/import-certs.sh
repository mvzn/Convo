#!/usr/bin/env bash
#
# Imports the Developer ID certificates into a temporary keychain on a CI runner.
# Expects (as env vars, from GitHub Actions secrets):
#   MACOS_CERT_P12        base64-encoded .p12 holding both Developer ID certs + keys
#   MACOS_CERT_PASSWORD   the .p12 export password
#
# Safe to re-run; does nothing if MACOS_CERT_P12 is empty (unsigned build).
set -euo pipefail

if [[ -z "${MACOS_CERT_P12:-}" ]]; then
    echo "MACOS_CERT_P12 not set - skipping certificate import (unsigned build)"
    exit 0
fi

KEYCHAIN_PWD="$(uuidgen)"
security create-keychain -p "$KEYCHAIN_PWD" build.keychain
security set-keychain-settings -lut 21600 build.keychain    # don't auto-lock mid-build
security default-keychain -s build.keychain
security unlock-keychain -p "$KEYCHAIN_PWD" build.keychain

echo "$MACOS_CERT_P12" | base64 -d > /tmp/certs.p12
security import /tmp/certs.p12 -k build.keychain -P "$MACOS_CERT_PASSWORD" \
    -T /usr/bin/codesign -T /usr/bin/productsign -T /usr/bin/pkgbuild -T /usr/bin/productbuild
rm -f /tmp/certs.p12
security set-key-partition-list -S apple-tool:,apple: -s -k "$KEYCHAIN_PWD" build.keychain > /dev/null

echo "Imported identities:"
security find-identity -v build.keychain
