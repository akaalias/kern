#!/usr/bin/env bash
#
# release.sh — build a Developer ID-signed Kern.app to the Desktop for local use.
#
# What it does (no notarization — fast, runs on this Mac):
#   1. Bumps the version: minor +1, rolling .9 -> next major .0 (e.g. 5.0 -> 5.1,
#      5.9 -> 6.0), and resets the build number to 1.
#   2. Archives the Release configuration.
#   3. Exports a Developer ID-signed Kern.app.
#   4. Copies it to ~/Desktop/Kern.app (you drag it into /Applications yourself).
#   5. Persists the new version into the project ONLY if the build succeeded.
#
# Usage:
#   scripts/release.sh              # bump + build + copy to Desktop
#   scripts/release.sh --no-bump    # rebuild the current version (no bump)
#
# Notarization is intentionally skipped (local use). To add it later: zip the
# exported app, `xcrun notarytool submit --keychain-profile <profile> --wait`,
# then `xcrun stapler staple` the .app before copying.

set -euo pipefail

cd "$(dirname "$0")/.."   # repo root
PROJ="application/Kern/Kern.xcodeproj"
PBX="$PROJ/project.pbxproj"
EXPORT_OPTS="scripts/ExportOptions.plist"

BUMP=1
[ "${1:-}" = "--no-bump" ] && BUMP=0

# ---- compute version ----
cur=$(grep -m1 'MARKETING_VERSION = ' "$PBX" | sed -E 's/.*MARKETING_VERSION = ([0-9]+\.[0-9]+);.*/\1/')
curbuild=$(grep -m1 'CURRENT_PROJECT_VERSION = ' "$PBX" | sed -E 's/.*CURRENT_PROJECT_VERSION = ([0-9.]+);.*/\1/')
major="${cur%%.*}"
minor="${cur#*.}"

if [ "$BUMP" = 1 ]; then
  minor=$((minor + 1))
  if [ "$minor" -gt 9 ]; then minor=0; major=$((major + 1)); fi
  NEWVER="${major}.${minor}"
  NEWBUILD=1
else
  NEWVER="$cur"
  NEWBUILD="$curbuild"
fi
echo "==> Version ${cur} (build ${curbuild})  ->  ${NEWVER} (build ${NEWBUILD})"

# ---- archive (Release), version passed as overrides so a failed build never
#      leaves the project bumped ----
rm -rf build
ARCHIVE="build/Kern.xcarchive"
echo "==> Archiving Release…"
xcodebuild archive \
  -project "$PROJ" \
  -scheme Kern \
  -configuration Release \
  -archivePath "$ARCHIVE" \
  -allowProvisioningUpdates \
  MARKETING_VERSION="$NEWVER" \
  CURRENT_PROJECT_VERSION="$NEWBUILD" \
  -quiet

# ---- export Developer ID-signed app ----
echo "==> Exporting Developer ID app…"
xcodebuild -exportArchive \
  -archivePath "$ARCHIVE" \
  -exportOptionsPlist "$EXPORT_OPTS" \
  -exportPath build/export \
  -allowProvisioningUpdates \
  -quiet

APP="build/export/Kern.app"
[ -d "$APP" ] || { echo "!! export produced no Kern.app" >&2; exit 1; }

# ---- copy to Desktop ----
DEST="$HOME/Desktop/Kern.app"
rm -rf "$DEST"
cp -R "$APP" "$DEST"

# ---- persist the bump now that the build succeeded ----
if [ "$BUMP" = 1 ]; then
  sed -i '' -E "s/MARKETING_VERSION = [0-9.]+;/MARKETING_VERSION = ${NEWVER};/g" "$PBX"
  sed -i '' -E "s/CURRENT_PROJECT_VERSION = [0-9.]+;/CURRENT_PROJECT_VERSION = ${NEWBUILD};/g" "$PBX"
fi

echo "==> Done: ${DEST}  (v${NEWVER} build ${NEWBUILD})"
echo "    Drag it into /Applications when you're ready to switch."
