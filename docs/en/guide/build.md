---
title: Build Guide
description: Confirmed build paths for x86 Docker, Sophon artifacts, CPU test builds, and docs.
prev:
  text: Documentation Home
  link: /en/
next:
  text: Deployment Guide
  link: /en/guide/deployment
---

# Build Guide

This page documents build paths that are confirmed and available in the repository.

> **💡 Docker Compose Version Note**
> This documentation uses the latest Docker Compose V2 command format (`docker compose`). If you are using an older Docker environment, please replace `docker compose` with the hyphenated `docker-compose` in all commands.

## Build Path Overview

| Target | Entry Point | Notes |
| --- | --- | --- |
| x86 Docker runtime | `docker-compose.x86.yml` / `docker-compose.x86.windows.yml` | Starts the containerized development/runtime environment. |
| Sophon SOURCE package | `docker compose -f docker-compose.sophon.yml run --rm cosmo-sophon-package` | Cross-compiles the installable source-build package. |
| CPU test build | `scripts/build_cpu_test.sh` | Builds `cosmo-tests` for x86 CPU validation. |
| Documentation site | `npm ci` and `npm run docs:build` | Builds this VitePress site. |

## x86 Docker Development Runtime

These entry points are from:

- `docker-compose.x86.yml` (Linux)
- `docker-compose.x86.windows.yml` (Windows)
- `Dockerfile.x86`
- `scripts/build_cpu.sh`

Confirmed CMake parameters:

| Parameter | Value |
| --- | --- |
| `COSMO_TARGET_ARCH` | `x86_64` |
| `COSMO_NN_USE_SOPHON_BACKEND` | `OFF` |
| `COSMO_NN_USE_CPU_BACKEND` | `ON` |
| `COSMO_ENABLE_OPENH264` | `ON` |
| `COSMO_DEV_MODE` | `ON` |
| `RESOURCE_DIR` | `data/resource/aiboxresource_x86` |

Linux:

```bash
docker compose -f docker-compose.x86.yml up -d --build
docker compose -f docker-compose.x86.yml ps
```

Windows (PowerShell/CMD):

```powershell
docker compose -f docker-compose.x86.windows.yml up -d --build
docker compose -f docker-compose.x86.windows.yml ps
```

After build:

- Web console available at `http://127.0.0.1:8080`.
- Release packages and build artifacts exported to `build_output/`.
- Runtime data stored in Docker volume `cosmo-x86-data`.
- Resource directory mounted to Docker volume `cosmo-x86-app-resource`.

## Sophon Artifacts

The public entry point defaults to
`COSMO_MODEL_GUARD_BUILD_PROFILE=public-runtime`:

```bash
docker compose -f docker-compose.sophon.yml run --rm cosmo-sophon-package
```

Windows PowerShell:

```powershell
.\scripts\build_sophon_package.ps1
```

The two supported profiles are deliberately isolated:

| Profile | Intended use | Output directory | Deployment status |
| --- | --- | --- | --- |
| SOURCE (`public-runtime`, default) | Public aarch64 compile, link, package, and test validation using the tracked runtime SDK | `build_output/public-runtime/` | Installable source build; not a signed production release |
| `production-release` | Controlled release build with the complete production SDK, provisioning tool, release public key, and release bootstrap inputs | `build_output/production-release/` | Emits a `FACTORY-BASE` only for blank-device setup; OTA still requires a signed release |

The SOURCE archive name ends with
`-SOURCE-<edge-commit>-<build-identity>-<archive-sha256>.tar.gz`. It includes
the runtime Guard SDK and SOURCE installation assets, but no provisioner,
release bootstrap, private signing material, or signing transaction entry
point. It can update application code on a device prepared separately; it
cannot commission a blank device or be renamed into a signed release.

### Install a SOURCE Build on a Device

Verify the outer archive SHA-256 against the digest in its filename before
extracting it. Then change to the single extracted package directory and run:

```bash
sudo ./install-device.sh install
sudo ./install-device.sh status
```

The installer creates `/appfs/cosmo_wander` when needed, validates the
extracted payload, stops `cosmo.service`, deletes the existing `cwai_data`,
installs the new tree, and starts the SOURCE service. It does not create an
application backup and provides no rollback command. If the final health check
fails, the command reports failure and leaves the newly installed tree in
place for direct diagnosis or reinstall. `status` reports the active mode,
build ID, base Edge commit, Guard SDK release, and service state.

On a configured device, the existing
`/data/cwaiuserdata/model-guard/device-certificate.bin` remains in place. That
single device-bound certificate authorizes all current and future preset models
published under the product model key; no per-model licenses exist. On a blank
device, SOURCE can install the application and service, but protected presets
remain unavailable until the separate authorized Guard workflow installs the
certificate. SOURCE `install` and `status` do not access
`/data/cwaiuserdata/model-guard`.

Maintainers select the production profile only inside the controlled release
environment. The base Compose file alone intentionally cannot do this: an
approved override must mount the complete SDK and each public trust input
read-only and set all required production variables:

```bash
COSMO_MODEL_GUARD_BUILD_PROFILE=production-release \
  docker compose -f docker-compose.sophon.yml \
  -f /path/to/approved-production.override.yml \
  run --rm cosmo-sophon-package
```

The PowerShell entry point validates the same profile value for output
selection, but setting that value alone does not provide the controlled inputs;
it fails closed unless the organization's approved release automation supplies
them.

The `production-release` CPack artifact is not an OTA archive and is rejected
by the updater. It may be used only as a SHA-256-pinned `FACTORY-BASE` in the
controlled blank-device procedure. Normal installation and upgrades still use
the signed release archive emitted by the offline release process. Release
signing keys must never be placed in the repository or passed to the ordinary
Compose build.

This path is from:

- `docker-compose.sophon.yml`
- `scripts/build_sophon_package.ps1` (Windows: restores `.so` symlinks before building)
- `scripts/build.sh`

Confirmed behavior:

- Base image uses the pre-built GHCR image: `ghcr.io/cosmo-wander-ai/cosmo_edge-build-env_sophon:v1` (unified build environment, speeding up local start time).
- Builds the package and `cosmo-tests` with `scripts/build.sh -T -m data/resource/aiboxresource`.
- Exports build artifacts only (does not start services).
- Keeps profile outputs separate under `build_output/<profile>/`.

## CPU Test Build

```bash
bash scripts/build_cpu_test.sh
```

This script configures CMake with the CPU backend and `BUILD_TESTS=ON`, producing:

```sh
build_cpu/cosmo-tests
```

Useful for smoke testing C++ compilation and packaging logic without a target edge device.

## Documentation Build

```bash
npm ci
npm run docs:build
```

The build output is generated under `docs/.vitepress/dist` and should not be committed.
