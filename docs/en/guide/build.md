---
title: Build Guide
description: Confirmed build paths for x86 Docker, Sophon artifacts, CPU test builds, and docs.
prev:
  text: Documentation Home
  link: /en/
next:
  text: macOS Docker Preview
  link: /en/guide/macos-docker-preview
---

# Build Guide

This page documents build paths that are confirmed and available in the repository.

> **💡 Docker Compose Version Note**
> This documentation uses the latest Docker Compose V2 command format (`docker compose`). If you are using an older Docker environment, please replace `docker compose` with the hyphenated `docker-compose` in all commands.

## Build Path Overview

| Target | Entry Point | Notes |
| --- | --- | --- |
| x86 Docker runtime | `docker-compose.x86.yml` / `docker-compose.x86.windows.yml` | Starts the containerized development/runtime environment. |
| macOS Docker Preview | `scripts/macos-docker-preview.sh` | Runs the one-video x86 workflow under amd64 emulation on Apple Silicon. |
| Sophon SOURCE package | `docker compose -f docker-compose.sophon.yml run --rm cosmo-sophon-package` | Cross-compiles the installable source-build package. |
| CPU test build | `scripts/build_cpu_test.sh` | Builds `cosmo-tests` for x86 CPU validation. |
| Documentation site | `npm ci` and `npm run docs:build` | Builds this VitePress site. |

## x86 Docker Development Runtime

These entry points are from:

- `docker-compose.x86.yml` (Linux)
- `docker-compose.x86.windows.yml` (Windows)
- `docker-compose.x86.macos.yml` (Apple Silicon macOS Preview)
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

Apple Silicon macOS (Preview):

```bash
./scripts/macos-docker-preview.sh doctor
./scripts/macos-docker-preview.sh up
```

The Mac path explicitly runs `linux/amd64`, uses isolated volumes, and publishes
only on loopback. It does not enable Model Guard and is not native arm64 or NPU
performance evidence. See [macOS Docker Preview](./macos-docker-preview.md) for
the complete setup and acceptance boundary.

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
| Open (`public-runtime`, default) | Public aarch64 compile, link, package, and test validation using the tracked runtime SDK | `build_output/public-runtime/` | Plain models; no device authorization required |
| Protected (`production-release`) | Controlled build with the complete production SDK and provisioning tool | `build_output/production-release/` | Encrypted models; device authorization required |

Both profiles produce `cosmo-V<version>-<32-char-md5>.tar.gz`. The same format
can be uploaded through the management page on a main-branch installation and
on every later version. Application archives are not signed. The profiles differ
only in model protection and availability of `cosmo-model-provision`.

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
build ID, base Edge commit, package version, and service state.

On a configured device, the existing
`/data/cwaiuserdata/model-guard/device-certificate.bin` remains in place. That
single device-bound certificate authorizes all current and future preset models
published under the product model key; no per-model licenses exist. On a blank
device, SOURCE can install the application and service, but protected presets
remain unavailable until the separate authorized Guard workflow installs the
certificate. SOURCE `install` and `status` do not access
`/data/cwaiuserdata/model-guard`.

Maintainers use one command in a controlled environment containing the complete
Guard SDK and provisioning tool:

```bash
COSMO_MODEL_GUARD_BUILD_PROFILE=production-release \
  docker compose -f docker-compose.sophon.yml run --rm cosmo-sophon-package
```

The Protected build fails immediately if the controlled SDK does not contain
`cosmo-model-provision`.
Stage the controlled production SDK under the host path
`build_output/model-guard-sdk-production/`. The existing Compose volume exposes
that ignored directory to the container, and Protected builds select it
automatically. Open builds remain unchanged.

The Protected CPack artifact is itself the upgrade archive accepted by the web
management page. No offline application-signing step is required. Guard device
certificates and model-encryption secrets remain controlled inputs and must never
be placed in the public repository.

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
