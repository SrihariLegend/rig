#!/bin/sh
set -e

REPO="SrihariLegend/rig"
INSTALL_DIR="${RIG_INSTALL_DIR:-/usr/local/bin}"

# Detect platform
OS="$(uname -s)"
ARCH="$(uname -m)"

case "$OS" in
  Linux)  os="linux" ;;
  Darwin) os="macos" ;;
  *) echo "Unsupported OS: $OS" >&2; exit 1 ;;
esac

case "$ARCH" in
  x86_64|amd64)  arch="amd64" ;;
  aarch64|arm64)  arch="arm64" ;;
  *) echo "Unsupported arch: $ARCH" >&2; exit 1 ;;
esac

TARGET="${os}-${arch}"

# Get latest release tag
if [ -z "$RIG_VERSION" ]; then
  RIG_VERSION="$(curl -fsSL "https://api.github.com/repos/${REPO}/releases/latest" | grep '"tag_name"' | cut -d'"' -f4)"
fi

if [ -z "$RIG_VERSION" ]; then
  echo "Failed to get latest version" >&2
  exit 1
fi

URL="https://github.com/${REPO}/releases/download/${RIG_VERSION}/rig-${TARGET}"
SHA_URL="${URL}.sha256"

echo "Installing rig ${RIG_VERSION} (${TARGET})..."

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

curl -fsSL "$URL" -o "$TMP/rig"
curl -fsSL "$SHA_URL" -o "$TMP/rig.sha256"

# Verify checksum
cd "$TMP"
if command -v sha256sum >/dev/null 2>&1; then
  echo "$(cat rig.sha256 | awk '{print $1}')  rig" | sha256sum -c - >/dev/null
elif command -v shasum >/dev/null 2>&1; then
  echo "$(cat rig.sha256 | awk '{print $1}')  rig" | shasum -a 256 -c - >/dev/null
fi

chmod +x rig

if [ -w "$INSTALL_DIR" ]; then
  mv rig "$INSTALL_DIR/rig"
else
  echo "Need sudo to install to $INSTALL_DIR"
  sudo mv rig "$INSTALL_DIR/rig"
fi

echo "rig ${RIG_VERSION} installed to ${INSTALL_DIR}/rig"
echo ""
echo "Run 'rig auth' to configure your API key, then 'rig' to start."
