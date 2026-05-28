#!/bin/bash
# install.sh — Install the latest better-cf-ip-c binary
#
# Usage:
#   curl -sSL https://github.com/6Kmfi6HP/better-cf-ip-c/releases/latest/download/install.sh | bash
#   curl -sSL https://github.com/6Kmfi6HP/better-cf-ip-c/releases/latest/download/install.sh | bash -s -- --prefix /usr/local/bin
#   curl -sSL https://github.com/6Kmfi6HP/better-cf-ip-c/releases/latest/download/install.sh | bash -s -- --version v1.0.0

set -euo pipefail

REPO="6Kmfi6HP/better-cf-ip-c"
VERSION=""
PREFIX="${PREFIX:-}"
BINARY_NAME="better-cf-ip-c"

# --- Parse arguments ---
while [[ $# -gt 0 ]]; do
  case "$1" in
    --version)
      VERSION="$2"
      shift 2
      ;;
    --prefix)
      PREFIX="$2"
      shift 2
      ;;
    --help)
      echo "Usage: install.sh [options]"
      echo ""
      echo "Options:"
      echo "  --version <tag>   Install a specific version (e.g., v1.0.0)"
      echo "  --prefix <path>   Install to a specific directory (default: ~/.local/bin)"
      echo "  --help            Show this help message"
      exit 0
      ;;
    *)
      echo "Unknown option: $1"
      echo "Usage: install.sh [--version <tag>] [--prefix <path>]"
      exit 1
      ;;
  esac
done

# --- Detect platform ---
OS="$(uname -s | tr '[:upper:]' '[:lower:]')"
ARCH="$(uname -m)"

case "${OS}-${ARCH}" in
  linux-x86_64)
    BINARY="better-cf-ip-c-linux-x86_64"
    ;;
  darwin-arm64|darwin-aarch64)
    BINARY="better-cf-ip-c-darwin-arm64"
    ;;
  darwin-x86_64)
    BINARY="better-cf-ip-c-darwin-x86_64"
    ;;
  *)
    echo "Error: unsupported platform ${OS}-${ARCH}"
    echo "Supported platforms: linux-x86_64, darwin-arm64, darwin-x86_64"
    exit 1
    ;;
esac

# --- Determine install prefix ---
if [ -z "$PREFIX" ]; then
  PREFIX="${HOME}/.local/bin"
fi

# --- Determine version ---
if [ -z "$VERSION" ]; then
  echo "Fetching latest release..."
  VERSION="$(curl -sL "https://api.github.com/repos/${REPO}/releases/latest" | grep '"tag_name"' | head -1 | cut -d'"' -f4)"
  if [ -z "$VERSION" ]; then
    echo "Error: could not determine latest version from GitHub API"
    exit 1
  fi
fi

# --- Download ---
DOWNLOAD_URL="https://github.com/${REPO}/releases/download/${VERSION}/${BINARY}"
mkdir -p "$PREFIX"
echo "Downloading better-cf-ip-c ${VERSION} (${BINARY})..."
curl -fsSL "$DOWNLOAD_URL" -o "${PREFIX}/${BINARY_NAME}"
chmod +x "${PREFIX}/${BINARY_NAME}"

echo ""
echo "Installed to ${PREFIX}/${BINARY_NAME}"
echo ""
echo "Make sure ${PREFIX} is in your PATH:"
echo "  export PATH=\"${PREFIX}:\$PATH\""
echo ""
echo "Run:"
echo "  ${BINARY_NAME} --help"
