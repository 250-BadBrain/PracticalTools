#!/usr/bin/env sh
set -eu

mkdir -p dist
export CGO_ENABLED=0
ldflags="-s -w"

build_one() {
  target_os="$1"
  target_arch="$2"

  case "$target_os:$target_arch" in
    windows:amd64|windows:386|windows:arm64)
      GOOS=windows GOARCH="$target_arch" GOARM= go build -ldflags "$ldflags -H=windowsgui" -o "dist/invisiblecompiler-windows-$target_arch.exe" .
      echo "Built dist/invisiblecompiler-windows-$target_arch.exe"
      ;;
    linux:amd64|linux:386|linux:arm64)
      GOOS=linux GOARCH="$target_arch" GOARM= go build -ldflags "$ldflags" -o "dist/invisiblecompiler-linux-$target_arch" .
      echo "Built dist/invisiblecompiler-linux-$target_arch"
      ;;
    linux:armv7)
      GOOS=linux GOARCH=arm GOARM=7 go build -ldflags "$ldflags" -o dist/invisiblecompiler-linux-armv7 .
      echo "Built dist/invisiblecompiler-linux-armv7"
      ;;
    *)
      echo "Unsupported target: $target_os $target_arch" >&2
      exit 1
      ;;
  esac
}

build_all() {
  build_one windows amd64
  build_one windows 386
  build_one windows arm64
  build_one linux amd64
  build_one linux 386
  build_one linux arm64
  build_one linux armv7
  echo "Build finished."
}

if [ "${1:-}" = "all" ]; then
  build_all
  exit 0
fi

if [ "${1:-}" = "release" ]; then
  build_one windows amd64
  build_one linux amd64
  build_one linux arm64
  echo "Release build finished."
  exit 0
fi

if [ -n "${1:-}" ]; then
  if [ -z "${2:-}" ]; then
    echo "Usage: sh build.sh OS ARCH" >&2
    exit 1
  fi
  build_one "$1" "$2"
  exit 0
fi

echo
echo "Select operating system:"
echo "1. Windows"
echo "2. Linux"
echo "3. All"
printf "Choose [1-3]: "
read os_choice

case "$os_choice" in
  1) target_os="windows" ;;
  2) target_os="linux" ;;
  3) build_all; exit 0 ;;
  *) echo "Invalid choice" >&2; exit 1 ;;
esac

echo
echo "Select architecture:"
echo "1. amd64"
echo "2. 386"
echo "3. arm64"
if [ "$target_os" = "linux" ]; then
  echo "4. armv7"
  printf "Choose [1-4]: "
else
  printf "Choose [1-3]: "
fi
read arch_choice

case "$arch_choice" in
  1) target_arch="amd64" ;;
  2) target_arch="386" ;;
  3) target_arch="arm64" ;;
  4)
    if [ "$target_os" = "linux" ]; then
      target_arch="armv7"
    else
      echo "Invalid choice" >&2
      exit 1
    fi
    ;;
  *) echo "Invalid choice" >&2; exit 1 ;;
esac

build_one "$target_os" "$target_arch"
