---
title: macOS Docker Preview
description: Run the CosmoEdge x86 workflow on Apple Silicon through an isolated linux/amd64 Docker environment.
prev:
  text: Build Guide
  link: /en/guide/build
next:
  text: Deployment Guide
  link: /en/guide/deployment
---

# macOS Docker Preview

> **Status: Preview.** This path targets local development and one offline-video
> workflow. It is not a native arm64 build, a Sophon device emulator, or a
> production deployment. Apple Silicon runs a `linux/amd64` image through Docker
> Desktop, so the first build and inference are slower than native x86 Linux.

## What It Covers

The Mac Preview reuses the x86 CPU/ONNX Runtime backend. Its target experience is:

- sign in to the web console;
- upload local video and retain the channel;
- use the bundled x86 ONNX models to create a task, set an ROI, and start or stop analysis;
- inspect live OSD, alarm snapshots, and event history;
- export a non-empty alarm CSV; and
- stop and restart the container without losing configuration or uploaded data.

It does not provide NPU performance parity, USB-camera passthrough, LAN device
discovery, multi-channel capacity evidence, or local VLM support. See
[Acceptance and Boundaries](#acceptance-and-boundaries) for the complete scope.

## Prepare the Environment

The current admission target is an Apple Silicon Mac with Docker Compose V2.
Recommended capacity is:

- at least 20 GiB of free disk space;
- at least 8 GiB assigned to the Docker virtual machine; and
- Rosetta 2 for a better amd64 emulation experience.

Before installing or first starting Docker Desktop, review and accept its separate
[Docker Subscription Service Agreement](https://docs.docker.com/subscription/).
The current operating-system, memory, and Rosetta requirements are in Docker's
[Mac installation guide](https://docs.docker.com/desktop/setup/install/mac-install/).

For this amd64 workload, Docker Desktop is generally faster with Apple
Virtualization Framework and Rosetta enabled. Docker's current
[settings reference](https://docs.docker.com/desktop/settings-and-maintenance/settings/)
states that the Rosetta option is available only with Apple Virtualization
Framework; Docker VMM does not currently accelerate amd64 emulation through
Rosetta. This is a performance recommendation, not a system setting changed by
the CosmoEdge scripts.

## Start the Preview

Run the read-only admission check from the repository root:

```bash
./scripts/macos-docker-preview.sh doctor
```

It checks Apple Silicon, Docker Desktop, Compose configuration, Docker memory,
disk, Rosetta, and local ports. It does not install components or change Docker
settings. When it passes, start the Preview:

```bash
./scripts/macos-docker-preview.sh up
```

The script builds `docker-compose.x86.macos.yml` only when its image is missing,
otherwise reuses the existing image. It then waits until nginx, SRS,
`cosmo-engine`, and the web response are all healthy, then prints:

```text
http://127.0.0.1:8080
```

The first run downloads the amd64 builder and dependencies and compiles under
emulation. It can be substantially slower than later starts.
The Mac Compose file pins the validated amd64 builder and Debian runtime base by
digest so a tag update cannot silently change the base environment for the same candidate.
The Mac Compose file defaults to one build job to avoid nested GNU Make jobserver
descriptor failures under amd64 emulation. `COSMO_X86_BUILD_JOBS` can override
that value, but higher parallelism is experimental and requires repeating both
acceptance runs on this page.

Rebuild explicitly after changing source, the Dockerfile, or build resources:

```bash
./scripts/macos-docker-preview.sh up --build
```

A normal `up` reuses the existing image and does not turn unrelated workspace
changes into another long emulated build.

If host port `8080` is occupied, override only the web port:

```bash
COSMO_X86_WEB_PORT=8280 ./scripts/macos-docker-preview.sh up
```

Then open `http://127.0.0.1:8280`. Media ports `1936`, `1985`, and `18088` remain
fixed and cannot be reused as the web port because the current frontend preview
path relies on the SRS port contract.

The Mac Preview plays SRS HTTP-FLV directly through loopback port `18088` and
does not try WebRTC first. The media port remains `18088` even when
`COSMO_X86_WEB_PORT` moves the web console to another port. This avoids the long
WebRTC media-timeout and fallback window that can occur through Docker Desktop.
It is a deterministic local Preview setting and does not change the WebRTC
default used by other deployment paths.

## Lifecycle and Isolation

```bash
# Show container and health state.
./scripts/macos-docker-preview.sh status

# Print recent logs, or follow until interrupted.
./scripts/macos-docker-preview.sh logs
./scripts/macos-docker-preview.sh logs --follow

# Stop services while preserving settings, uploads, and model resources.
./scripts/macos-docker-preview.sh down
```

The Preview has its own Compose project, container, image, volumes, and build output:

| Object | Name or path |
| --- | --- |
| Compose project | `cosmo-x86-macos-preview` |
| Container | `cosmo-x86-macos-preview` |
| Runtime data volume | `cosmo-x86-macos-preview-data` |
| Model resource volume | `cosmo-x86-macos-preview-app-resource` |
| Package output | `build_output/macos-x86/` |

The default `down` command does not delete named volumes, so an ordinary stop
does not erase Preview data.

## Acceptance and Boundaries

Before community promotion, run the following workflow two consecutive times on
an Apple Silicon Mac, with one `down` / `up` cycle between the runs:

1. The container becomes healthy and the web console accepts a sign-in.
2. Upload `data/test-video/Safety Helmet.mp4`; the channel remains after refresh.
3. Create a task with the bundled No Safety Helmet ONNX model and set an ROI.
4. Start the task and confirm that Live Display shows video and OSD; one enabled
   task should open its OSD directly without first creating a raw stream.
5. Confirm that Event Center receives an alarm with a snapshot.
6. Export a non-empty alarm CSV.
7. After restart, the configuration, video, and task still work, without a crash or abnormal restart.

Conclusions are limited to the layer actually tested:

| Capability | Mac Preview conclusion |
| --- | --- |
| Web console, upload, task, ROI, OSD, alarms, CSV | Preview target; report the result of the consecutive acceptance runs |
| x86 ONNX Runtime CPU inference | Runs under `linux/amd64` emulation; not native performance evidence |
| Sophon / Rockchip NPU, USB cameras, LAN discovery | Not covered |
| Multi-channel performance, soak testing, production deployment | Not covered and cannot be inferred from one local-video run |
| Model Guard, Sophon Protected packages, CEMC models, device provisioning | Disabled and unverified |

`scripts/build_cpu.sh` explicitly disables the Sophon backend and enables the CPU
backend, so CMake disables Model Guard. Building this Preview from the Model Guard
development branch does not run Model Guard on the Mac. CEMC protected models
remain part of the Sophon Protected runtime path.

## Local Security Defaults

`docker-compose.x86.macos.yml` binds the web, RTMP, SRS API, and HTTP stream
ports to `127.0.0.1`. It does not publish UDP discovery, request `NET_ADMIN`, or
expose `/dev/video*` devices. These defaults fit single-machine development and
prevent direct access from other LAN hosts. Remote access requires a separate
review of authentication, TLS, firewall rules, video data, and port exposure. Do
not remove the loopback bindings and treat this Preview as a production configuration.

## Troubleshooting

- `Docker Desktop is not ready`: open Docker Desktop, finish first-run setup and
  agreement acceptance, wait for Running, and rerun `doctor`.
- Slow builds: inspect Docker's VMM and Rosetta settings and assign at least 8 GiB
  of memory. amd64 emulation remains a best-effort Preview.
- The service never becomes healthy: run `./scripts/macos-docker-preview.sh logs`
  and inspect the first failing process among nginx, SRS, and `cosmo-engine`.
- Web-port conflict: set `COSMO_X86_WEB_PORT`; stop the owner of any conflicting fixed media port.
- Rebuild while preserving data: run `./scripts/macos-docker-preview.sh up --build`
  and do not delete the two Preview named volumes. Use normal `up` for a restart.
