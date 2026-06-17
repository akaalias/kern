#!/bin/bash
# Bundle textview as a macOS .app

APP_NAME="MicroEdit"
APP_DIR="${APP_NAME}.app"
CONTENTS="${APP_DIR}/Contents"
MACOS="${CONTENTS}/MacOS"
RESOURCES="${CONTENTS}/Resources"

# clean
rm -rf "${APP_DIR}"

# create structure
mkdir -p "${MACOS}" "${RESOURCES}"

# build the binary
bash build_textview.sh

# copy binary
cp textview "${MACOS}/${APP_NAME}"

# copy fonts
cp iAWriterQuattroS-Regular.ttf "${RESOURCES}/"
cp iAWriterQuattroS-Bold.ttf "${RESOURCES}/"
cp iAWriterQuattroS-Italic.ttf "${RESOURCES}/"
cp iAWriterMonoS-Regular.ttf "${RESOURCES}/"

# create Info.plist
cat > "${CONTENTS}/Info.plist" << 'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>
    <string>MicroEdit</string>
    <key>CFBundleDisplayName</key>
    <string>MicroEdit</string>
    <key>CFBundleIdentifier</key>
    <string>com.microui.microedit</string>
    <key>CFBundleVersion</key>
    <string>1.0</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
    <key>CFBundleExecutable</key>
    <string>MicroEdit</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleSignature</key>
    <string>????</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>CFBundleDocumentTypes</key>
    <array>
        <dict>
            <key>CFBundleTypeRole</key>
            <string>Editor</string>
            <key>CFBundleTypeExtensions</key>
            <array>
                <string>txt</string>
                <string>md</string>
                <string>markdown</string>
                <string>text</string>
            </array>
            <key>CFBundleTypeName</key>
            <string>Text Document</string>
        </dict>
    </array>
</dict>
</plist>
PLIST

# create a launcher script that sets working dir to Resources for font loading
mv "${MACOS}/${APP_NAME}" "${MACOS}/${APP_NAME}-bin"
cat > "${MACOS}/${APP_NAME}" << 'LAUNCHER'
#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# resolve file arg to absolute path before cd'ing to Resources
ARGS=()
for arg in "$@"; do
  if [ -f "$arg" ]; then
    ARGS+=("$(cd "$(dirname "$arg")" && pwd)/$(basename "$arg")")
  else
    ARGS+=("$arg")
  fi
done
cd "${SCRIPT_DIR}/../Resources"
exec "${SCRIPT_DIR}/MicroEdit-bin" "${ARGS[@]}"
LAUNCHER
chmod +x "${MACOS}/${APP_NAME}"

echo "Built ${APP_DIR}"
echo "Run with: open ${APP_DIR} --args <file>"
echo "Or: ./${APP_DIR}/Contents/MacOS/${APP_NAME} <file>"
