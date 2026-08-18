#!/bin/bash
# Normalize CPack output and publish the permanent upgrade filename accepted by
# main and all later Open/Protected releases.
set -euo pipefail

if [ "$#" -ne 4 ]; then
    echo "Usage: $0 <packages-dir> <package-name> <build-profile> <build-epoch>" >&2
    exit 2
fi

packages_dir="$1"
package_name="$2"
build_profile="$3"
build_epoch="$4"
original="${packages_dir}/${package_name}.tar.gz"

case "$build_profile" in
    public-runtime | production-release) ;;
    *) echo "Unsupported build profile: $build_profile" >&2; exit 2 ;;
esac
case "$build_epoch" in
    "" | *[!0-9]*) echo "Build epoch must be a non-negative integer" >&2; exit 2 ;;
esac
if [ ! -f "$original" ] || [ -L "$original" ]; then
    echo "CPack artifact not found or has an unsafe type: $original" >&2
    exit 1
fi
if ! tar -tzf "$original" | awk -v root="$package_name" '
    BEGIN { count = 0 }
    {
        count++
        if (substr($0, 1, 1) == "/" || $0 ~ /(^|\/)\.\.(\/|$)/ ||
            ($0 != root && index($0, root "/") != 1)) exit 1
    }
    END { if (count == 0) exit 1 }
'; then
    echo "CPack artifact has an unexpected archive layout" >&2
    exit 1
fi

normalization_root="$(mktemp -d "${packages_dir}/.cosmo-package-normalize.XXXXXX")"
trap 'rm -rf -- "$normalization_root"' EXIT
extract_root="${normalization_root}/extract"
normalized="${normalization_root}/${package_name}.tar.gz"
mkdir -p -- "$extract_root"
tar -xzf "$original" -C "$extract_root"
test -d "${extract_root}/${package_name}" && test ! -L "${extract_root}/${package_name}"
tar --sort=name --format=gnu --mtime="@${build_epoch}" --owner=0 --group=0 \
    --numeric-owner -C "$extract_root" -cf - -- "$package_name" | gzip -n >"$normalized"
mv -- "$normalized" "$original"

digest="$(md5sum -- "$original")"
digest="${digest%% *}"
labeled="${packages_dir}/${package_name}-${digest}.tar.gz"
mv -- "$original" "$labeled"
echo "Upgrade package: $(basename "$labeled")"
