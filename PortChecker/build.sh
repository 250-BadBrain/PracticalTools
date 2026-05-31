#!/usr/bin/env sh
set -eu

mkdir -p dist
base_ldflags="-s -w"

tags_for_mode() {
  case "$1" in
    cli) printf '%s' '-tags cli' ;;
    browser) printf '%s' '' ;;
    native) printf '%s' '-tags native' ;;
    *) echo "Unsupported build mode: $1" >&2; exit 1 ;;
  esac
}

build_one() {
  mode="$1"
  target_os="$2"
  target_arch="$3"
  tags="$(tags_for_mode "$mode")"
  ldflags="$base_ldflags"
  cgo_enabled=0
  if [ "$mode" = "browser" ] && [ "$target_os" = "windows" ]; then
    ldflags="$base_ldflags -H=windowsgui"
  fi
  if [ "$mode" = "native" ]; then
    cgo_enabled=1
    if [ "$target_os" = "windows" ]; then
      ldflags="$base_ldflags -H=windowsgui"
    fi
    host_os="$(uname -s | tr '[:upper:]' '[:lower:]')"
    host_arch="$(uname -m)"
    case "$host_os" in
      linux*) host_os="linux" ;;
      mingw*|msys*|cygwin*) host_os="windows" ;;
    esac
    case "$host_arch" in
      x86_64|amd64) host_arch="amd64" ;;
      i386|i686) host_arch="386" ;;
      aarch64|arm64) host_arch="arm64" ;;
      armv7*|armv7l) host_arch="armv7" ;;
    esac
    if [ "$target_os" != "$host_os" ] || [ "$target_arch" != "$host_arch" ]; then
      echo "Native GUI uses platform graphics libraries and is not cross-compiled by this script." >&2
      echo "Build native GUI on the target OS/architecture, or use browser/cli mode." >&2
      exit 1
    fi
  fi

  case "$target_os:$target_arch" in
    windows:amd64|windows:386|windows:arm64)
      CGO_ENABLED="$cgo_enabled" GOOS=windows GOARCH="$target_arch" GOARM= go build $tags -ldflags "$ldflags" -o "dist/portchecker-$mode-windows-$target_arch.exe" .
      echo "Built dist/portchecker-$mode-windows-$target_arch.exe"
      ;;
    linux:amd64|linux:386|linux:arm64)
      CGO_ENABLED="$cgo_enabled" GOOS=linux GOARCH="$target_arch" GOARM= go build $tags -ldflags "$ldflags" -o "dist/portchecker-$mode-linux-$target_arch" .
      echo "Built dist/portchecker-$mode-linux-$target_arch"
      ;;
    linux:armv7)
      CGO_ENABLED="$cgo_enabled" GOOS=linux GOARCH=arm GOARM=7 go build $tags -ldflags "$ldflags" -o "dist/portchecker-$mode-linux-armv7" .
      echo "Built dist/portchecker-$mode-linux-armv7"
      ;;
    *)
      echo "Unsupported target: $target_os $target_arch" >&2
      exit 1
      ;;
  esac
}

build_mode_all() {
  mode="$1"
  build_one "$mode" windows amd64
  build_one "$mode" windows 386
  build_one "$mode" windows arm64
  build_one "$mode" linux amd64
  build_one "$mode" linux 386
  build_one "$mode" linux arm64
  build_one "$mode" linux armv7
  echo "Build finished."
}

if [ "${1:-}" = "all" ]; then
  build_one cli windows amd64
  build_one browser windows amd64
  build_one native windows amd64
  build_one browser linux amd64
  build_one browser linux arm64
  echo "Build finished."
  exit 0
fi

if [ -n "${1:-}" ]; then
  case "$1" in
    cli|browser|native)
      if [ "${2:-}" = "all" ]; then
        build_mode_all "$1"
        exit 0
      fi
      if [ -z "${2:-}" ] || [ -z "${3:-}" ]; then
        echo "Usage: sh build.sh MODE OS ARCH" >&2
        exit 1
      fi
      build_one "$1" "$2" "$3"
      exit 0
      ;;
    *)
      if [ -z "${2:-}" ]; then
        echo "Usage: sh build.sh OS ARCH" >&2
        exit 1
      fi
      build_one browser "$1" "$2"
      exit 0
      ;;
  esac
fi

echo
echo "Select build mode:"
echo "1. CLI"
echo "2. Browser GUI"
echo "3. Native GUI"
printf "Choose [1-3]: "
read mode_choice

case "$mode_choice" in
  1) mode="cli" ;;
  2) mode="browser" ;;
  3) mode="native" ;;
  *) echo "Invalid choice" >&2; exit 1 ;;
esac

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
  3) build_mode_all "$mode"; exit 0 ;;
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

build_one "$mode" "$target_os" "$target_arch"
