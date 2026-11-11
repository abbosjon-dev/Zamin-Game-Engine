#!/usr/bin/env bash
# GitHub Ubuntu runners may point at an unavailable Azure mirror. Keep dependency failures
# visible, retry downloads, and use the signed official Ubuntu archive as a fallback.
set -euo pipefail
if [[ $# -eq 0 ]]; then
    echo "Usage: Tests/install_linux_dependencies.sh PACKAGE..." >&2
    exit 2
fi
apt_options=(-o Acquire::Retries=2 -o Acquire::http::Timeout=20 -o Acquire::https::Timeout=20)
use_official_mirror() {
    echo "::notice::Using archive.ubuntu.com instead of the Azure Ubuntu mirror"
    for source in /etc/apt/apt-mirrors.txt /etc/apt/sources.list /etc/apt/sources.list.d/ubuntu.sources; do
        if [[ -f "$source" ]]; then
            sudo sed -i 's/azure\.archive\.ubuntu\.com/archive.ubuntu.com/g' "$source"
        fi
    done
}
# Avoid waiting once for every package when the default mirror's DNS is down.
if ! timeout 15 getent hosts azure.archive.ubuntu.com >/dev/null; then
    use_official_mirror
fi
if sudo apt-get "${apt_options[@]}" update && sudo apt-get "${apt_options[@]}" install -y "$@"; then
    exit 0
fi
use_official_mirror
sudo apt-get "${apt_options[@]}" update
sudo apt-get "${apt_options[@]}" install -y "$@"
