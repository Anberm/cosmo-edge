#!/bin/bash
# Label the ordinary CPack output according to its user-facing package variant.
#
# public-runtime is intentionally retained as the internal build profile for
# compatibility with existing automation. Its user-facing artifact is SOURCE:
# an installable source-build package that is not a signed production release.
# The controlled production CPack output may seed a blank device, but it is not
# an updater archive. A filename checksum is not authentication and must never
# be accepted by the updater.
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

if [ ! -f "$original" ] || [ -L "$original" ]; then
    echo "CPack artifact not found or has an unsafe type: $original" >&2
    exit 1
fi

case "$build_epoch" in
    "" | *[!0-9]*)
        echo "Build epoch must be a non-negative integer" >&2
        exit 2
        ;;
    *) ;;
esac

# CPack preserves build-time metadata in TGZ output. Repack its locally
# generated tree with stable metadata before naming and hashing the artifact.
if ! tar -tzf "$original" |
    awk -v root="$package_name" '
        BEGIN { count = 0 }
        {
            count++
            if (substr($0, 1, 1) == "/" ||
                $0 ~ /(^|\/)\.\.(\/|$)/ ||
                ($0 != root && index($0, root "/") != 1)) {
                exit 1
            }
        }
        END { if (count == 0) exit 1 }
    '; then
    echo "CPack artifact has an unexpected archive layout" >&2
    exit 1
fi

normalization_root="$(
    mktemp -d "${packages_dir}/.cosmo-package-normalize.XXXXXX"
)"
cleanup() {
    rm -rf -- "$normalization_root"
}
trap cleanup EXIT

extract_root="${normalization_root}/extract"
normalized="${normalization_root}/${package_name}.tar.gz"
mkdir -p -- "$extract_root"
tar -xzf "$original" -C "$extract_root"
if [ ! -d "${extract_root}/${package_name}" ] ||
   [ -L "${extract_root}/${package_name}" ]; then
    echo "CPack artifact does not contain the expected package root" >&2
    exit 1
fi

tar --sort=name \
    --format=gnu \
    --mtime="@${build_epoch}" \
    --owner=0 \
    --group=0 \
    --numeric-owner \
    -C "$extract_root" \
    -cf - \
    -- "$package_name" |
    gzip -n >"$normalized"
mv -- "$normalized" "$original"

digest="$(sha256sum -- "$original")"
digest="${digest%% *}"
if [ "${#digest}" -ne 64 ]; then
    echo "Cannot calculate CPack artifact SHA-256" >&2
    exit 1
fi
case "$digest" in
    *[!0-9a-f]*)
        echo "Cannot calculate CPack artifact SHA-256" >&2
        exit 1
        ;;
    *) ;;
esac

case "$build_profile" in
    public-runtime)
        label="SOURCE"
        description="SOURCE package"
        guidance="Install with the SOURCE workflow; protected preset models require one separately provisioned device-bound certificate."
        identity_members="$(
            tar -tzf "$original" |
                awk '/\/share\/cosmo-source\/build-identity\.env$/ { print }'
        )"
        identity_member_count="$(
            printf '%s\n' "$identity_members" | sed '/^$/d' | wc -l
        )"
        if [ "$identity_member_count" -ne 1 ]; then
            echo "SOURCE package must contain exactly one build identity" >&2
            exit 1
        fi
        identity_record="$(tar -xOzf "$original" -- "$identity_members")"
        edge_commit="$(
            printf '%s\n' "$identity_record" |
                sed -n 's/^edge_commit=//p'
        )"
        build_identity="$(
            printf '%s\n' "$identity_record" |
                sed -n 's/^build_identity=//p'
        )"
        if [ "${#edge_commit}" -ne 40 ] ||
           [ "${#build_identity}" -ne 64 ]; then
            echo "SOURCE package build identity is malformed" >&2
            exit 1
        fi
        case "${edge_commit}${build_identity}" in
            *[!0-9a-f]*)
                echo "SOURCE package build identity is malformed" >&2
                exit 1
                ;;
            *) ;;
        esac
        identity_label="-${edge_commit}-${build_identity}"
        ;;
    production-release)
        label="FACTORY-BASE"
        description="Controlled factory base"
        guidance="Use only for a hash-verified blank-device install; the updater still requires a signed release."
        identity_label=""
        ;;
    *)
        echo "Unsupported build profile: $build_profile" >&2
        exit 2
        ;;
esac

labeled="${packages_dir}/${package_name}-${label}${identity_label}-${digest}.tar.gz"
if [ -e "$labeled" ] || [ -L "$labeled" ]; then
    if [ -f "$labeled" ] && [ ! -L "$labeled" ] && cmp -s -- "$original" "$labeled"; then
        rm -f -- "$original"
        echo "${description} unchanged: $(basename "$labeled")"
        if [ "$build_profile" = "public-runtime" ]; then
            echo "Edge commit: ${edge_commit}"
            echo "Build identity: ${build_identity}"
        fi
        echo "Archive SHA-256: ${digest}"
        echo "$guidance"
        exit 0
    fi
    echo "Conflicting package output already exists: $labeled" >&2
    exit 1
fi
mv -- "$original" "$labeled"

echo "${description}: $(basename "$labeled")"
if [ "$build_profile" = "public-runtime" ]; then
    echo "Edge commit: ${edge_commit}"
    echo "Build identity: ${build_identity}"
fi
echo "Archive SHA-256: ${digest}"
echo "$guidance"
