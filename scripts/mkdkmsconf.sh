#!/usr/bin/env bash

set -e

pkgver() {
  if git describe --tags >/dev/null 2>&1; then
    git describe --tags | sed 's/^v//;s/\([^-]*-g\)/r\1/;s/-/./g'
  else
    printf "r%s.%s" "$(git rev-list --count HEAD)" "$(git rev-parse --short HEAD)"
  fi
}

PACKAGE_VERSION=${PACKAGE_VERSION:-$(pkgver)}

cat << EOF
PACKAGE_NAME="tcp-brutal"
PACKAGE_VERSION="$PACKAGE_VERSION"

MAKE[0]="make KERNEL_DIR=\${kernel_source_dir} all"
CLEAN="make KERNEL_DIR=\${kernel_source_dir} clean"

BUILT_MODULE_NAME[0]="brutal"
DEST_MODULE_LOCATION[0]="/extra"

AUTOINSTALL="yes"
EOF
