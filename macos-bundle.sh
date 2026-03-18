#!/bin/bash
# Build a macOS .app bundle for Elektroid.
# Requires: rsvg-convert (librsvg), Homebrew GTK dependencies.
# The binary resolves its data directory at runtime from the bundle
# location, so no special configure prefix is needed.
set -euo pipefail

APP_NAME="Elektroid"
APP_BUNDLE="${APP_NAME}.app"
CONTENTS="${APP_BUNDLE}/Contents"
MACOS="${CONTENTS}/MacOS"
RESOURCES="${CONTENTS}/Resources"
SRC_DIR="$(cd "$(dirname "$0")" && pwd)"
NPROC=$(sysctl -n hw.ncpu)

cd "${SRC_DIR}"

# Build if needed
if [ ! -f src/elektroid ]; then
    echo "==> Configuring..."
    test -f configure || autoreconf -fi
    ./configure 2>&1 | tail -5
    echo "==> Building..."
    make -j${NPROC} 2>&1 | tail -3
fi

echo "==> Creating app bundle structure..."
rm -rf "${APP_BUNDLE}"
mkdir -p "${MACOS}" "${RESOURCES}/share/elektroid" \
    "${RESOURCES}/share/icons/hicolor/scalable/apps"

# Copy binaries
cp src/elektroid "${MACOS}/elektroid-bin"
cp src/elektroid-cli "${MACOS}/"

# Copy data files
cp res/elektroid.ui res/elektroid.css res/libraries.html res/THANKS \
    "${RESOURCES}/share/elektroid/"
cp -R res/elektron "${RESOURCES}/share/elektroid/"
test -d res/microbrute && cp -R res/microbrute "${RESOURCES}/share/elektroid/"
test -d res/volca_sample_2 && cp -R res/volca_sample_2 "${RESOURCES}/share/elektroid/"

# Copy icons
cp res/*.svg "${RESOURCES}/share/icons/hicolor/scalable/apps/"

# Generate .icns from SVG
echo "==> Generating app icon..."
ICONSET_DIR=$(mktemp -d)/Elektroid.iconset
mkdir -p "${ICONSET_DIR}"
SVG="res/io.github.dagargo.Elektroid.svg"

for size in 16 32 64 128 256 512; do
    rsvg-convert -w ${size} -h ${size} "${SVG}" -o "${ICONSET_DIR}/icon_${size}x${size}.png"
done
for size in 32 64 128 256 512 1024; do
    half=$((size / 2))
    rsvg-convert -w ${size} -h ${size} "${SVG}" -o "${ICONSET_DIR}/icon_${half}x${half}@2x.png"
done
iconutil -c icns "${ICONSET_DIR}" -o "${RESOURCES}/Elektroid.icns"
rm -rf "$(dirname "${ICONSET_DIR}")"

# Create launcher script
cat > "${MACOS}/Elektroid" << 'LAUNCHER'
#!/bin/bash
DIR="$(cd "$(dirname "$0")" && pwd)"
RESOURCES="$(cd "${DIR}/../Resources" && pwd)"
export XDG_DATA_DIRS="${RESOURCES}/share:${XDG_DATA_DIRS:-/opt/homebrew/share:/usr/local/share:/usr/share}"
exec "${DIR}/elektroid-bin" "$@"
LAUNCHER
chmod +x "${MACOS}/Elektroid"

# Create Info.plist
VERSION=$(grep 'AC_INIT' configure.ac | sed 's/.*\[\([0-9.]*\)\].*/\1/')
cat > "${CONTENTS}/Info.plist" << PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>
    <string>${APP_NAME}</string>
    <key>CFBundleDisplayName</key>
    <string>${APP_NAME}</string>
    <key>CFBundleIdentifier</key>
    <string>io.github.dagargo.Elektroid</string>
    <key>CFBundleVersion</key>
    <string>${VERSION}</string>
    <key>CFBundleShortVersionString</key>
    <string>${VERSION}</string>
    <key>CFBundleExecutable</key>
    <string>Elektroid</string>
    <key>CFBundleIconFile</key>
    <string>Elektroid</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>LSMinimumSystemVersion</key>
    <string>11.0</string>
</dict>
</plist>
PLIST

echo "==> Done! Created ${APP_BUNDLE}"
echo "    cp -r ${APP_BUNDLE} /Applications/"
echo "    open /Applications/${APP_BUNDLE}"
