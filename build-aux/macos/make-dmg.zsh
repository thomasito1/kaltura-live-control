#!/usr/bin/env zsh
# Wrap the built macOS package into a distributable .dmg.
#
# Must run on macOS - hdiutil, pkgbuild and codesign have no Windows equivalent.
#
# The DMG contains the .pkg rather than the raw .plugin bundle, deliberately. OBS
# loads macOS plugins from ~/Library/Application Support/obs-studio/plugins/, which is
# per-user, and a symlink baked into a DMG cannot resolve to whoever mounts it. The
# installer package expands that path at install time; a drag-and-drop DMG cannot.
#
# Usage:
#   ./build-aux/macos/make-dmg.zsh [version]

set -euo pipefail

local project_root="${0:A:h:h:h}"
local version="${1:-$(/usr/bin/plutil -extract version raw -o - "${project_root}/buildspec.json" 2>/dev/null || echo 0.1.0)}"
local name="kaltura-live-control"
local vol_name="Kaltura Live Control"
local release_dir="${project_root}/release"
local dist_dir="${project_root}/dist"
local staging="$(mktemp -d)"

trap 'rm -rf "${staging}"' EXIT

# The template's `package-macos` script drops the .pkg in release/.
local pkg
pkg=$(/usr/bin/find "${release_dir}" -maxdepth 1 -name '*.pkg' -print -quit 2>/dev/null || true)

if [[ -z "${pkg}" ]]; then
  print -u2 "error: no .pkg found in ${release_dir}"
  print -u2 "       build and package first, e.g."
  print -u2 "         cmake --preset macos"
  print -u2 "         cmake --build --preset macos --config RelWithDebInfo"
  print -u2 "         ./.github/scripts/package-macos --config RelWithDebInfo"
  exit 1
fi

mkdir -p "${dist_dir}"
cp "${pkg}" "${staging}/"

cat > "${staging}/READ ME FIRST.txt" <<'TXT'
Kaltura Live Control - OBS Studio plugin

1. Quit OBS Studio if it is running.
2. Double-click the .pkg and follow the installer.
3. Start OBS and open  Docks -> Kaltura Live Control.

Installs to:
  ~/Library/Application Support/obs-studio/plugins/

Requires OBS Studio 31.x.

If macOS reports the package is damaged or from an unidentified developer, this
build has not been notarized. Notarization requires an Apple Developer ID; until
then you can right-click the .pkg and choose Open, or run:
  xattr -dr com.apple.quarantine "<path to .pkg>"
TXT

local dmg="${dist_dir}/${name}-${version}-macos-universal.dmg"
rm -f "${dmg}"

/usr/bin/hdiutil create \
  -volname "${vol_name}" \
  -srcfolder "${staging}" \
  -ov -format UDZO \
  "${dmg}"

# Sign the DMG too when a Developer ID is configured. Gatekeeper checks the DMG
# itself, not only the package inside it.
if [[ -n "${CODESIGN_IDENT:-}" && "${CODESIGN_IDENT}" != '-' ]]; then
  /usr/bin/codesign --force --sign "${CODESIGN_IDENT}" "${dmg}"
  print "signed with ${CODESIGN_IDENT}"

  if [[ -n "${CODESIGN_IDENT_USER:-}" && -n "${CODESIGN_IDENT_PASS:-}" ]]; then
    local team
    team=$(print "${CODESIGN_IDENT}" | /usr/bin/sed -En 's/.+\((.+)\)/\1/p')
    /usr/bin/xcrun notarytool submit "${dmg}" \
      --apple-id "${CODESIGN_IDENT_USER}" \
      --team-id "${team}" \
      --password "${CODESIGN_IDENT_PASS}" \
      --wait
    /usr/bin/xcrun stapler staple "${dmg}"
    print "notarized and stapled"
  else
    print "note: CODESIGN_IDENT_USER / _PASS not set - DMG signed but NOT notarized"
  fi
else
  print "note: CODESIGN_IDENT not set - DMG is unsigned; Gatekeeper will block it"
fi

print "built ${dmg}"
