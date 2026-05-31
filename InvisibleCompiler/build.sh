#!/usr/bin/env sh
set -eu

mkdir -p dist
export CGO_ENABLED=0

GOOS=windows GOARCH=amd64 go build -ldflags "-s -w -H=windowsgui" -o dist/invisiblecompiler-windows-amd64.exe .
GOOS=linux GOARCH=amd64 go build -ldflags "-s -w" -o dist/invisiblecompiler-linux-amd64 .
GOOS=linux GOARCH=arm64 go build -ldflags "-s -w" -o dist/invisiblecompiler-linux-arm64 .

echo "Build finished."
