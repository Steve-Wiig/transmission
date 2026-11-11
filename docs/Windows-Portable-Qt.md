# Windows portable Qt build

## Build prerequisites

Windows 10/11 x64, Visual Studio 2022 with the C++ workload, CMake 3.20 or newer, Git, Qt 6 for MSVC x64, and the vcpkg dependencies used by this repository. Set VCPKG_ROOT and Qt6_DIR in a Visual Studio Developer PowerShell.

Run scripts/build-portable.ps1. It configures the repository's existing CMake Qt target, builds Release, installs into a staging directory, and runs windeployqt. The staged directory is self-contained for Qt runtime/plugin dependencies; review any non-Qt DLLs required by the selected vcpkg triplet before distributing it.

## Portable mode and copy-based import

Create an empty portable.dat beside transmission-qt.exe or launch with --portable. The executable directory becomes the portable root. On Windows the application looks for %APPDATA%\\Transmission and copies settings.json, resume/, and torrents/ into the portable root.

Import is one-way and non-destructive: a destination file that already exists is never overwritten. Existing files in the portable profile remain authoritative on later launches. Stop the official client before the first import so that settings and resume data are a consistent snapshot. The portable client does not write to the official profile.

The marker can contain a line `config-dir=relative-or-absolute-path`. Relative paths are resolved against the executable directory. `-g` / `--config-dir` takes precedence over the marker path. Portable mode remains isolated even when this override is supplied. Without either marker or flag, normal Transmission configuration discovery is unchanged.

Portable mode skips single-instance delegation and COM/DBus interop registration, preventing the test build from forwarding torrent additions to or impersonating the official client.

## Live testing

1. Stop official Transmission before the initial import.
2. Launch the portable executable and confirm its data is in the staged directory (or the marker's config-dir).
3. For clean test runs, create another portable folder with its own executable and portable.dat.
4. Do not sync resume files back to the official profile while either client is running. Automatic two-way synchronization is intentionally not implemented.

## Known limitations / validation

- Import copies files missing at the destination; it does not merge JSON settings or replace pre-existing resume entries.
- Download paths in settings and resume data are preserved exactly. UNC paths are accepted as normal Windows paths, but mapped-drive letters are not automatically rewritten to UNC because that requires a format-aware migration of settings and resume records. Validate NAS access under the same user/session context.
- A first import should be made with the official client stopped. This implementation does not attempt to lock the official profile or take a filesystem snapshot.
- Build/runtime validation requires Windows and the selected Qt/vcpkg toolchain. No performance gains are claimed until startup, torrent-load, UI-event-loop-latency, and working-set baselines are measured.
