#!/usr/bin/env bash
# Pinned CLI releases, verified before executing; no GitHub secrets required.
set -euo pipefail
destination=${1:?Pass an installation directory}
mkdir -p "$destination"
destination=$(cd "$destination" && pwd)
download_dir=$(mktemp -d)
trap 'rm -rf "$download_dir"' EXIT
install_release() {
  local url=$1 checksum=$2 executable=$3
  curl --fail --location --silent --show-error --retry 3 "$url" -o "$download_dir/archive.tar.gz"
  printf '%s  %s\n' "$checksum" "$download_dir/archive.tar.gz" | sha256sum --check
  tar -xzf "$download_dir/archive.tar.gz" -C "$destination" "$executable"
}
install_release \
  https://github.com/rhysd/actionlint/releases/download/v1.7.7/actionlint_1.7.7_linux_amd64.tar.gz \
  023070a287cd8cccd71515fedc843f1985bf96c436b7effaecce67290e7e0757 actionlint
install_release \
  https://github.com/gitleaks/gitleaks/releases/download/v8.24.2/gitleaks_8.24.2_linux_x64.tar.gz \
  fa0500f6b7e41d28791ebc680f5dd9899cd42b58629218a5f041efa899151a8e gitleaks
