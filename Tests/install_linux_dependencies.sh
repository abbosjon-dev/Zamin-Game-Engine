#!/usr/bin/env bash
# GitHub Ubuntu runners may point at an unavailable Azure mirror. Keep dependency failures
# visible, retry downloads, and use the signed official Ubuntu archive as a fallback.
set -euo pipefail
if [[ $# -eq 0 ]]; then
    echo "Usage: Tests/install_linux_dependencies.sh PACKAGE..." >&2
    exit 2
fi
# Existing runner dependencies do not need a mirror lookup or an index refresh.
all_installed=true
for package in "$@"; do
    if [[ "$(dpkg-query -W -f='${db:Status-Status}' "$package" 2>/dev/null)" != installed ]]; then
        all_installed=false
        break
    fi
done
if "$all_installed"; then
    echo "::notice::Dependencies already installed; apt network access is not required"
    exit 0
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
if ! timeout 10 getent hosts azure.archive.ubuntu.com >/dev/null; then
    if ! timeout 10 getent hosts archive.ubuntu.com >/dev/null; then
        echo "::error::Ubuntu dependency DNS unavailable for Azure and official mirrors; C++ build/tests have not run." >&2
        exit 69
    fi
    use_official_mirror
fi
# apt's HTTP timeout does not bound resolver waits for every individual package.
# Bound the entire command too, including children on timeout.
update() { sudo timeout --kill-after=10s 120s apt-get "${apt_options[@]}" update; }
install() { sudo timeout --kill-after=10s 240s apt-get "${apt_options[@]}" install -y "$@"; }
if update && install "$@"; then
    exit 0
fi
use_official_mirror
update
install "$@"
