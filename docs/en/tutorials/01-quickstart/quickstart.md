---
title: "Quick Start: Deployment, Sign-In, and First Detection"
description: Deploy or connect to CosmoEdge, add a video, configure a scenario task, and verify the first AI detection.
prev:
  text: Using CosmoEdge
  link: /en/tutorials/
next:
  text: Scenario Task Configuration
  link: /en/tutorials/02-scenario-config/scenario-config
---

# Quick Start: Deployment, Sign-In, and First Detection

| Item | Details |
| --- | --- |
| Who this is for | First-time CosmoEdge users, deployment engineers, and developers |
| What you will accomplish | Sign in, add one offline video, assign an algorithm, and observe a detection result |
| Prerequisites | An x86 host with Docker, or an edge device with CosmoEdge preinstalled |
| Estimated time | About 15–30 minutes for a first x86 build; 10–15 minutes for a preinstalled device |
| Device required | Choose an x86 Docker host or a preinstalled edge device; a camera is not required |
| Final acceptance result | The channel is running, the live view shows algorithm output, and matching events can be queried |

The goal is not merely to open the console. You will complete one
**verifiable first detection**:

1. Deploy or connect to CosmoEdge.
2. Sign in to the console.
3. Add a test video.
4. Assign and save an algorithm.
5. Verify the live result and event record.

Changing the default password, connecting a real camera, and tuning business parameters are important
follow-up tasks, but they are not required to prove the first detection.

## 1. Deploy or Connect to CosmoEdge

### Path A: Docker on an x86 Host

Use this path on a Linux x86_64 host. Windows users should use
`docker-compose.x86.windows.yml`; the remaining steps are the same.

```bash
git clone https://github.com/cosmo-wander-ai/cosmo-edge.git
cd cosmo-edge
docker compose -f docker-compose.x86.yml up -d --build
docker compose -f docker-compose.x86.yml ps
```

Windows PowerShell:

```powershell
git clone https://github.com/cosmo-wander-ai/cosmo-edge.git
cd cosmo-edge
docker compose -f docker-compose.x86.windows.yml up -d --build
docker compose -f docker-compose.x86.windows.yml ps
```

The initial build downloads dependencies and compiles the application, so its duration depends on the
network and host.

![Docker building the CosmoEdge service images](images/build.webp)

Expected state:

- `docker compose ... ps` reports the services as `Up` or `running`;
- `http://127.0.0.1:8080` opens in a browser;
- for remote access, replace `127.0.0.1` with the host IP and allow TCP port 8080 through the host firewall.

![CosmoEdge containers in the running state](images/container.webp)

### Path B: A Preinstalled Edge Device

1. Connect the device to power and Ethernet.
2. Connect the operator computer to the same LAN.
3. Open the management address supplied with the device.

Expected state: the sign-in page opens. If the device address is unknown, check the delivery label or
the router's DHCP leases before considering any reset.

## 2. Sign In

The default x86 address is `http://<host-ip>:8080`. Initial credentials are:

- Username: `admin`
- Password: `admin`

![CosmoEdge sign-in page with username and password fields](images/x86-login.webp)

After signing in, **Operation Overview** should open. Change the default password after the first sign-in.
This is a security requirement, although it does not block this tutorial's first-detection acceptance.

## 3. Add the Test Video

The repository contains a reproducible test asset:

```text
data/test-video/Safety Helmet.mp4
```

1. Open **Video Access**.
2. Select **Add**.
3. Choose **Offline Video** as the source type.
4. Enter a channel name such as “First helmet detection”.
5. Upload `Safety Helmet.mp4`, then save.

![Adding an offline test video on the Video Access page](images/img_14.webp)

Expected state: the new channel appears in the list without a persistent connection or decoding error.

::: tip A camera is optional
After the first detection works, replace the offline video with an RTSP/RTSPS network source. Starting
with the local asset isolates CosmoEdge from network, credentials, and camera codec differences.
:::

## 4. Create the First Detection Task

1. Select **Allocate Task** for the new channel.
2. Select **No Safety Helmet** from the available services.
3. Under **Detection Area**, cover the relevant part of the frame. For this first check, use the main
   work area.
4. Keep the built-in parameter defaults.
5. Select **Save**.

![Selecting a scenario task for the test video](images/img_16.webp)

Saving associates the channel with the algorithm and starts analysis. Return to Video Access and verify
that the running switch is enabled.

![The channel in the running state after the task is saved](images/img_25.webp)

Expected state:

- the channel is assigned to **No Safety Helmet**;
- its running switch is enabled;
- no persistent start failure is displayed.

## 5. Verify the Result

### 5.1 Live Display

Open **Live Display** and select the channel. Allow time for video playback and model initialization.

![Safety-helmet inference overlays in the live view](images/res.webp)

Pass criteria:

- the video plays continuously;
- bounding boxes, labels, or algorithm status overlays appear;
- the channel can be reopened after refreshing the page.

### 5.2 Event Center

Open **Event Center** and query by channel and time. An event is created only when the image matches the
detection condition and also satisfies the alarm interval, count, duration, and deduplication rules.
“Live overlays are present” and “an event was generated” are therefore separate checks.

![Detection records listed in Event Center](images/img_32.webp)

## 6. First-Detection Acceptance Checklist

- [ ] The sign-in page and Operation Overview are reachable.
- [ ] `Safety Helmet.mp4` exists as an offline video channel.
- [ ] **No Safety Helmet** is assigned and the channel is running.
- [ ] Live Display plays continuously and displays algorithm output.
- [ ] A matching segment produces a queryable Event Center record.
- [ ] The deployment method, access address, and verification time are recorded.

The first detection is complete only when every item passes.

## 7. Troubleshooting

### Containers Do Not Start

Check in this order:

```bash
docker compose -f docker-compose.x86.yml ps
docker compose -f docker-compose.x86.yml logs --tail=200
docker system df
```

Verify that Docker is running, disk space is available, and port 8080 is free. See
[Troubleshooting](../../guide/troubleshooting.md) for the broader diagnostic path.

### The Sign-In Page Does Not Open or Authentication Fails

1. Use host port `8080`, not a container-internal port.
2. For remote access, verify the host IP, firewall, and network segment.
3. Use `admin` / `admin` only for the initial sign-in. If the password has been changed, use the new
   value instead of rebuilding containers.

### The Saved Video Has No Picture

Check **file readability → channel state → decoder logs → algorithm state**:

1. Start with the repository MP4 so that RTSP networking is not another variable.
2. Confirm that upload completed and the channel is enabled.
3. Inspect logs for file-access, codec, or decoding errors.
4. If video plays but no overlay appears, confirm that the scenario task was saved and is running.

### Live Detections Appear but No Event Is Created

This is usually not a connection failure. Confirm that the clip matches the rule, then inspect the alarm
interval, alarm count, detection duration, and stationary-target deduplication settings. The next guide
explains these controls.

## Next Step

Continue to [Scenario Task Configuration](../02-scenario-config/scenario-config.md) to set detection
areas, parameters, and alarm-related rules intentionally.
