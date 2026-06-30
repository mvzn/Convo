# Packaging

Installer build scripts for each platform. All read the **Release** build under
`build/Convo_artefacts/Release/` and write to `dist/` (gitignored). CI builds these
automatically (see `.github/workflows/build.yml`); the steps below are for local builds.

Build the plugin first:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Windows — `windows/Convo.iss` (Inno Setup)

Installs `Convo.vst3` into `C:\Program Files\Common Files\VST3`.

```bat
ISCC.exe //DAppVersion=1.0.0 packaging\windows\Convo.iss
```

→ `dist\Convo-1.0.0-Windows.exe`. Sign the resulting `.exe` with a code-signing
certificate to avoid SmartScreen warnings (optional).

## macOS — `macos/build-pkg.sh`

Installs VST3 → `/Library/Audio/Plug-Ins/VST3` and AU → `/Library/Audio/Plug-Ins/Components`.

```sh
packaging/macos/build-pkg.sh 1.0.0
```

→ `dist/Convo-1.0.0-macOS.pkg`. Unsigned by default. For public release, set the
signing/notarization env vars first (Gatekeeper blocks unsigned plugins):

```sh
export CODESIGN_IDENTITY="Developer ID Application: NAME (TEAMID)"
export PKG_SIGN_IDENTITY="Developer ID Installer: NAME (TEAMID)"
export NOTARY_PROFILE="convo-notary"   # from `xcrun notarytool store-credentials`
packaging/macos/build-pkg.sh 1.0.0
```

## Linux — `linux/build-tarball.sh`

```sh
packaging/linux/build-tarball.sh 1.0.0
```

→ `dist/Convo-1.0.0-Linux.tar.gz`, containing `Convo.vst3`, `LICENSE`, and an
`install.sh` that copies the plugin to `~/.vst3` (or `/usr/lib/vst3` with `--system`).
