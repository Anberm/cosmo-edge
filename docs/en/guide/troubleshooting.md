---
title: Troubleshooting
description: Common build, runtime, port, Sophon image, log, and documentation-site issues.
prev:
  text: Runtime Configuration
  link: /en/guide/configuration
next:
  text: Architecture Overview
  link: /en/guide/architecture
---

# Troubleshooting

This page collects the most common build and runtime issues for the current project.

## Web Console Cannot Open

Confirm that you are using host port `8080`:

```text
http://127.0.0.1:8080
```

Check container status:

- **Linux**:

  ```bash
  docker compose -f docker-compose.x86.yml ps
  ```

- **Windows (PowerShell/CMD)**:

  ```powershell
  docker compose -f docker-compose.x86.windows.yml ps
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

## Port Conflicts

The x86 Compose file publishes:

- `8080`
- `1936`
- `1985`
- `18088`
- `8000/udp`

If a port is occupied, you can modify the host port in `docker-compose.x86.yml`, or stop the service that occupies the port.

On Windows, Hyper-V / WSL can reserve a TCP port range. Docker may therefore report a bind failure even when `netstat` shows no listening process. Check the reserved ranges first:

```powershell
netsh interface ipv4 show excludedportrange protocol=tcp
```

`docker-compose.x86.windows.yml` accepts `COSMO_X86_WEB_PORT`, so you can change the web host port without editing a tracked file. For example, use `8280`:

```powershell
$env:COSMO_X86_WEB_PORT = "8280"
docker compose -f docker-compose.x86.windows.yml up -d --build
```

Then open `http://127.0.0.1:8280`. The default remains `8080` when the variable is unset.

## Windows Build Scripts Report `No such file or directory`

If a Docker build reports that an existing `configure`, `config`, or `Configure` file cannot be executed, Git for Windows may have checked out the extensionless script with CRLF endings. The container then cannot parse its shebang.

The root `.gitattributes` pins automatically detected text files, including these extensionless scripts, to LF. After pulling the latest rules, retry from a fresh clone or clean worktree with no uncommitted changes. Confirm the rules with:

```powershell
git check-attr text eol -- 3rd/mp4v2-2.0.0/configure 3rd/openssl-3.5.3/config 3rd/srs-6.0-r0/trunk/configure
```

All three files should report `text: auto` and `eol: lf`.

## No Release Package in `build_output/`

Use the full run command:

- **Linux**:

  ```bash
  docker compose -f docker-compose.x86.yml up -d --build
  ```

- **Windows (PowerShell/CMD)**:

  ```powershell
  docker compose -f docker-compose.x86.windows.yml up -d --build
  ```

For the Sophon path, use:

```bash
docker compose -f docker-compose.sophon.yml run --rm cosmo-sophon-package
```

Note: `docker compose build` only builds the image and does not necessarily execute the container command that exports the release package.

## Sophon Build Failure

The Sophon build uses a self-contained `Dockerfile.sophon` (based on `ubuntu:22.04`) and does not require an external base image.

If the build fails, check the Docker build logs:

```bash
docker compose -f docker-compose.sophon.yml run --rm cosmo-sophon-package 2>&1 | tail -50
```

Common causes:

- Network issues preventing apt/npm/cargo mirror downloads — check `SOPHON_APT_MIRROR` and related environment variables.
- Insufficient disk space — the build requires approximately 3GB.

## nginx / SRS / cosmo-engine Not Started

Run the script:

```text
${INSTALLPATH}/scripts/run_start.sh
```

The startup sequence includes:

1. Stop existing processes.
2. Start nginx.
3. Start SRS.
4. Start `cosmo-engine`.

Check the logs:

```text
/data/cwaiuserdata/log/logs
```

## Upgrade Page Keeps Waiting

The device goes offline during an upgrade. The page waits for a new Linux `bootId` and stops the UI wait after 15 minutes. If reboot clears the login session, the page returns to login only after it observed an offline interval and the recovered service answers at the authentication boundary. This UI timeout does not cancel the device-side upgrade; verify the software version after signing in again.

On a Sophon device, inspect:

```bash
systemctl status cosmo --no-pager -l
journalctl -u cosmo -b --no-pager -n 200
stat -c '%F %a %U:%G %n' /data/cwaiuserdata/upload/sessions
```

Normally `cosmo.service` is `active (running)` and the staging root is a real directory with mode `0700`. A fatal initialization exception exits non-zero so `Restart=on-failure` can retry. Do not bypass the checks by recursively widening permissions on all of `/data/cwaiuserdata`.

## Documentation Site Build Fails

First install dependencies:

```bash
npm ci
```

Then build:

```bash
npm run docs:build
```

In Windows PowerShell, if you encounter an `npm.ps1` execution-policy issue, you can use:

```powershell
npm.cmd run docs:build
```

## `vitepress` Not Found

This means the documentation-site dependencies have not been installed:

```bash
npm ci
```

## npm Audit Reports Vulnerabilities

The current documentation-site dependencies may trigger npm audit warnings. Do not blindly upgrade dependencies; before upgrading, confirm that VitePress, the theme configuration, and the GitHub Pages workflow still build successfully.

## Windows Native CPU Build

There is currently no confirmed-working Windows native CPU build script in this repository. Do not present old scripts or old commands as a publicly supported path.
