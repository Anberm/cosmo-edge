# Cosmo Model Guard v2 SDK

This directory exposes the public, consumer-facing portion of the formally
built Model Guard v2 SDK used by CosmoEdge:

- `include/cosmo_model_guard_v2.h`
- `lib/libcosmo_model_guard.so*`
- `share/cosmo-model-guard/cmg_v2_abi.json`
- `share/cosmo-model-guard/cmg_v2_dependencies.json`
- `share/cosmo-model-guard/sdk-release.env`

The checked-in AArch64 shared library has the `v2-only` runtime compatibility
profile. It does not expose the legacy Model Guard ABI.

The default CosmoEdge build profile remains `public-runtime` for automation
compatibility; its user-facing artifact is the SOURCE package. The public SDK
and SOURCE package contain the runtime library and public compatibility
metadata, but no `bin/cosmo-model-provision` or private signing material. A
configured device needs only
`/data/cwaiuserdata/model-guard/device-certificate.bin` to authorize all
current and future preset models published under the product model key. There
are no per-model licenses. SOURCE cannot commission a blank device or construct
or sign a formal production release.

`sdk-release.env` contains only six fields: its format, SDK release ID, and
SHA-256 values for the shared library, header, ABI manifest, and dependency
manifest. Edge recomputes those hashes to prevent accidental component mixing.
This unsigned inventory is not a model-authorization gate or proof of official
origin.

`bin/cosmo-model-provision` is an offline device-initialization tool and is not
part of the public runtime SDK. It remains ignored by Git and must not be
force-added. Selecting `production-release` does not create or recover any
signing key.

This public repository does not contain the private Model Guard source,
production signing keys, device secrets, or the complete controlled inputs
required to reconstruct, sign, or deploy a production package. This README
does not grant or alter artifact licensing or redistribution rights; those
require separately approved terms from the artifact owner.

## Verification

The canonical public Sophon build invokes
`scripts/verify_model_guard_v2_sdk.py` with the build dependencies and the
checked-in runtime SDK:

```bash
/usr/bin/python3 -I -B scripts/verify_model_guard_v2_sdk.py \
  --admission-profile public-runtime \
  --sdk-root "$MODEL_GUARD_SDK_ROOT" \
  --snapshot-base "$MODEL_GUARD_SNAPSHOT_BASE" \
  --readelf "$AARCH64_READELF" \
  --nm "$AARCH64_NM" \
  --openssl-include-dir "$OPENSSL_INCLUDE_DIR" \
  --libcrypto "$LIBCRYPTO_PATH" \
  --libssl "$LIBSSL_PATH" \
  --cryptopp-include-dir "$CRYPTOPP_INCLUDE_DIR" \
  --cryptopp-library "$CRYPTOPP_LIBRARY_PATH" \
  --sophon-include-dir "$SOPHON_INCLUDE_DIR" \
  --sophon-library-dir "$SOPHON_LIBRARY_DIR" \
  --expected-profile v2-only
```
