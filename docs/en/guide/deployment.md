---
title: Deployment Guide
description: Runtime directories, service processes, ports, upgrade packages, and systemd behavior.
prev:
  text: Build Guide
  link: /en/guide/build
next:
  text: Runtime Configuration
  link: /en/guide/configuration
---

# Deployment Guide

This page is organized according to the current runtime scripts, mainly covering:

- `scripts/docker-entrypoint.x86.sh`
- `scripts/start.sh`
- `scripts/run_start.sh`
- `scripts/install.sh`

## x86 Docker Runtime

Start:

- **Linux**:
  ```bash
  docker compose -f docker-compose.x86.yml up -d --build
  ```
- **Windows (PowerShell/CMD)**:
  ```powershell
  docker compose -f docker-compose.x86.windows.yml up -d --build
  ```

Stop:

- **Linux**:
  ```bash
  docker compose -f docker-compose.x86.yml down
  ```
- **Windows (PowerShell/CMD)**:
  ```powershell
  docker compose -f docker-compose.x86.windows.yml down
  ```

View logs:

- **Linux**:
  ```bash
  docker compose -f docker-compose.x86.yml logs -f
  ```
- **Windows (PowerShell/CMD)**:
  ```powershell
  docker compose -f docker-compose.x86.windows.yml logs -f
  ```

## Runtime Directories

| Path | Description |
| --- | --- |
| `<INSTALLPATH>` | Main installation directory, set by the Dockerfile or deployment scripts |
| `<INSTALLPATH>/resource` | Runtime resource directory |
| `<DATADIR>` | User persistent data directory, by default located on a persistent volume |
| `<DATADIR>/log/logs` | Log directory |
| `<DATADIR>/upload/sessions` | Resumable chunk-upload sessions; directory mode is fixed to `0700` |
| `<DATADIR>/upgrade` | Upgrade package directory |

## Runtime Processes

The startup scripts launch:

- `nginx` (system, `/usr/sbin/nginx`)
- `srs`
- `cosmo-engine`

Corresponding paths:

`${INSTALLPATH}` is set by the `INSTALLPATH` environment variable in the Dockerfile (default value see Runtime Configuration).
Specific paths:
```text
/usr/sbin/nginx  (system nginx)
${INSTALLPATH}/bin/srs
${INSTALLPATH}/bin/cosmo-engine
```

## Default Ports

| Port | Source | Purpose |
| --- | --- | --- |
| `8080 -> 80` | `docker-compose.x86.yml` / `docker-compose.x86.windows.yml` | x86 Docker web console |
| `1936` | `docker-compose.x86.yml` / `docker-compose.x86.windows.yml` / SRS | RTMP |
| `1985` | `docker-compose.x86.yml` / `docker-compose.x86.windows.yml` / SRS | SRS API |
| `18088` | `docker-compose.x86.yml` / `docker-compose.x86.windows.yml` / SRS | HTTP stream |
| `8000` | `src/app/AppConstants.h` (`kDefaultHttpPort`) | Backend HTTP |
| `9000` | `src/app/AppConstants.h` (`kDefaultWebSocketPort`) | Backend WebSocket |

`8080->80`, `1936`, `1985`, and `18088` are exposed to the host. `8000` and `9000` are in-container process ports. In `docker-compose.x86.yml`, `8000` is mapped as `8000:8000/udp` for device discovery, which is distinct from the backend HTTP (TCP). The host accesses the API through nginx, which reverse-proxies from in-container `80` to host `8080`.

The production UDP discovery protocol accepts only `probe` queries. Network-card changes, hardware-information writes, and authorization-code operations are no longer executed over multicast: use an implemented authenticated management API, while operations without a secure replacement API are rejected.

Stream environment variables set by the runtime scripts:

```bash
COSMO_STREAM_PLAY_MODE=srs
COSMO_STREAM_RTMP_BASE=rtmp://127.0.0.1:1936/live
COSMO_STREAM_RTC_API_PORT=1985
COSMO_STREAM_HTTP_PORT=18088
```

## Release Package Structure

The install/upgrade scripts expect the release package to contain:

- `bin`
- `files`
- `font`
- `scripts`
- `web`

Optional or handled by presence:

- `lib`
- `resource`

The upgrade package filename must match one of these patterns:

```text
cosmo-V<major>.<minor>.<patch>-<32-char-md5>.tar.gz
cosmo-release-<release-id>.tar.gz
```

The web console performs a local upgrade as follows:

1. Query device status and record the current Linux `bootId`.
2. Transfer the package in chunks according to live device capabilities while showing actual upload progress.
3. For a legacy MD5 package, validate its filename, MD5, archive safety, package layout, and live disk budget. For a signed release, preserve and stage the archive byte-for-byte.
4. After a Sophon reboot, the startup script revalidates and installs a legacy MD5 package; the trusted updater verifies a signed release's signature, manifest, payload, and rollback state before installation.
5. Return to login after observing a new `bootId`. If reboot invalidates the login session, first observe the device offline and then require an authentication response from the recovered service before returning to login.

The 15-minute recovery wait is a UI timeout; it does not cancel an upgrade already running on the device. Keep power connected and inspect device networking and systemd logs if it expires. After signing in again, verify the software version against the release package; UI recovery proves reboot and service recovery, not version acceptance.

## systemd Service

During blank-device setup, install this file from the controlled
`FACTORY-BASE`:

```text
share/cosmo-factory/cosmo.service
    -> /etc/systemd/system/cosmo.service
```

Service start command:

```text
ExecStart=/appfs/cosmo_wander/cwai_data/scripts/inte_run_start.sh
```

The current `scripts/install.sh` handles signed release transactions only; it
does not create the systemd unit. Enable the service only after the
device-bound Model Guard certificate and the first signed release archive are
in place. There are no per-model licenses. The service runs as `root` with
`Restart=on-failure`.

Some Sophon images restore the persistent data tree to the appliance administrator at boot. The upload staging service therefore allows `sessions` to inherit the owner of an immediate parent that is not writable by group/other, while still requiring:

- a real, non-symlink `sessions` directory with mode `0700`;
- stable owner, device, and inode identity for the lifetime of the process;
- service-owned private session directories and payloads.

Do not apply broad recursive `chmod` or `chown` operations to the complete `<DATADIR>`.

## Interface Documentation Static Links

Packaged interface files:

- `data/Interface/ai-box-interface_v1.0.html`
- `data/Interface/mqtt_v1.0.html`

Linked at runtime to:

- `web/staticfile/httpInterface.html`
- `web/staticfile/mqttInterface.html`
