---
title: "Volume 1: Quick Start"
description: Use either a pre-installed edge box or an x86 Docker deployment to complete your first real-time AI detection experience in 10-15 minutes.
prev:
  text: Tutorials
  link: /en/tutorials/
next:
  text: "Volume 2: Scenario Configuration"
  link: /en/tutorials/02-scenario-config/scenario-config
---
# Volume 1: Quick Start

> **Reading time**: 10-15 minutes
> **Objective**: Cover the workflow from environment preparation to viewing real-time AI detection results in a web browser.
> **Environment Types**:
>
> - Pre-installed CosmoEdge edge box: no deployment required; power on and use it directly.
> - No hardware box: deploy and run the platform with Docker containers on an x86 server.
>
> **General note**: Both environments support demo videos, so no camera is required to complete the tutorial.

## Table of Contents

1. Option 1: Docker Deployment on an x86 Server
2. Option 2: Quick Start with a CosmoEdge Edge Box
3. Quick Start Completion and Next Steps

## Option 1: Docker Deployment on an x86 Server

### 1. Operating Environment

The following environment has been verified:

- OS: ubuntu-22.04.2
- Docker version: 29.1.3
- Docker Compose version: v5.1.4
- CPU: 13th Gen Intel(R) Core(TM) i9-13900F
- Memory: 64 GB
- Linux compose file: `docker-compose.x86.yml`
- Windows compose file: `docker-compose.x86.windows.yml`

> **Docker Compose version note**: This documentation uses the Docker Compose V2 command format, `docker compose`. If you use an older Docker environment, replace it with `docker-compose`.

### 2. Get the Code and Start the Containers

For a first run, clone the repository and enter the repository root first:

```bash
git clone https://github.com/cosmo-wander-ai/cosmo-edge.git
# Or use the Gitee mirror in China:
# git clone https://gitee.com/cosmo-wander-ai/cosmo-edge.git
cd cosmo-edge
```

Linux:

```bash
sudo docker compose -f docker-compose.x86.yml up -d --build
```

Windows (PowerShell/CMD):

```powershell
docker compose -f docker-compose.x86.windows.yml up -d --build
```

### 3. Running Results

Image build and startup process:

![](images/build.webp)

Docker container running status:

![](images/container.webp)

### 4. Browser Access

Access URL: `http://<server-ip>:8080`. If you run it on the same machine, you can also use `http://127.0.0.1:8080`.

Default Login Credentials:

![](images/x86-login.webp)

- Username: `admin`
- Password: `admin`

> Important reminder: Change the default password immediately after logging in.

### 5. Post-Deployment Operation Guide

After the containers start, follow the [Scenario Configuration tutorial](/en/tutorials/02-scenario-config/scenario-config) to set up your first AI detection scenario.

### 6. Algorithm Inference Result Preview

![](images/res.webp)

## Option 2: Quick Start with a CosmoEdge Edge Box

### Hardware Overview

CosmoEdge runs on a Sophon-based AI edge computing device powered by the BM1688 processor, delivering 16.0 TOPS of AI compute for intelligent analysis of IP camera video streams.

<!-- This is a product photo showing the device front panel -->

![](images/img_01.webp)

<!-- This is a product photo showing the device rear panel with ports -->

![](images/img_02.webp)

Key specifications:

| Component  | Specification                                           |
| ---------- | ------------------------------------------------------- |
| Processor  | BM1688                                                  |
| CPU        | Octa-core ARM A53 @ 1.6 GHz                             |
| Memory     | LPDDR4 8 GB                                             |
| Storage    | 64 GB                                                   |
| AI Compute | 16.0 TOPS                                               |
| Network    | 2 × 10/100/1000 Mbps auto-negotiation Ethernet         |
| Interfaces | USB 3.0 × 2, Type-C × 1, HDMI × 1, TF × 1, SIM × 1 |

> CosmoEdge-ready certified devices are available in mainland China through the [Taobao store](https://item.taobao.com/item.htm?id=1066672051450). The open-source software does not require a device purchase. For availability outside mainland China or project deployment support, contact [hello@cosmowander.ai](mailto:hello@cosmowander.ai).

### Step 1: Hardware Connection

Connect the device to your local network and power it on.

1. Use an Ethernet cable to connect the device's **ETH port** to your router or switch.

<!-- Image showing the Ethernet port -->

![](images/img_03.webp)

2. **Plug in the power adapter.**

<!-- Image showing the power port -->

![](images/img_04.webp)

3. Wait approximately **60 seconds** for the device to finish booting.

**Expected status indicators:**

- Power LED (PWR): Solid on (red)

<!-- Image showing the power LED -->

![](images/img_05.webp)

- Network LED (WAN): Blinking (indicates a healthy network connection)

<!-- Image showing the network LED -->

![](images/img_06.webp)

4. **Configure a static IP on your computer**

   On your computer, go to **Start** → **Control Panel** → **Network and Internet** → **Network and Sharing Center** → **Change adapter settings** → **Ethernet**. (A direct connection between the device and your computer is recommended for initial setup.)

<!-- Screenshot of network adapter settings -->

![](images/img_07.webp)

5. Double-click the Ethernet adapter and open **Internet Protocol Version 4 (TCP/IPv4)**.

<!-- Screenshot of TCP/IPv4 properties -->

![](images/img_08.webp)

6. Set the IP address and subnet mask to be on the same subnet as the CosmoEdge device, then click **OK**.

<!-- Screenshot of IP address configuration -->

![](images/img_09.webp)

**💡 The default CosmoEdge address is: WAN: 192.168.100.1**

### Step 2: Access the Management Console and Configure System Settings

Open a browser and navigate to the device's IP address.

1. We recommend **Chrome** or **Edge**.
2. In the address bar, enter: `http://<device IP>` (e.g., `http://192.168.100.1`)

<!-- Screenshot of browser navigation -->

![](images/x86-login.webp)

3. Log in with the default credentials:
   - Username: `admin`
   - Password: `admin` (we strongly recommend changing this after first login)

<!-- Screenshot of login page -->

![](images/img_11.webp)

After a successful login, you'll see the **System Dashboard**, which displays the following key metrics:

|   Metric   |                 Description                 |
| :---------: | :------------------------------------------: |
|  CPU Usage  |                Processor load                |
| VRAM Usage |    Video memory consumed by loaded tasks    |
|  NPU Usage  |         Neural processing unit load         |
| eMMC Usage |          System storage utilization          |
| Packet Loss | If above 10%, system performance may degrade |

Go to **System Management** → **System Settings** → **Time Settings** → **Manual Sync** → **Sync with Computer**. (Since the device is directly connected, it can't automatically obtain the correct time.)

<!-- Screenshot of time sync settings -->

![](images/img_12.webp)

Go to **System Management** → **Network Configuration** to change the device's IP address so it's on the same subnet as your LAN. (This lets your computer access the console while still having internet connectivity.)

<!-- Screenshot of network configuration -->

![](images/img_13.webp)

> **⚠️ Can't access the management console?**
>
> - Make sure the device and your computer are on the **same subnet**.
> - Try connecting the device directly to your computer with an Ethernet cable (bypassing the router).
> - Check whether your browser is using a proxy — disable it and try again.

### Step 3: View Live AI Detection Results

The device comes pre-loaded with several demo scenarios using **built-in demo videos** as data sources — no cameras needed to experience the full AI detection workflow. We'll use **Pedestrian Flow Counting** as our example.

1. **Download the pedestrian flow demo video**

   Demo video download link: [github.com/cosmo-wander-ai/cosmo-edge/releases/tag/v1.0-videos](https://github.com/cosmo-wander-ai/cosmo-edge/releases/tag/v1.0-videos)

---

2. **Upload the pedestrian flow demo video**

   In the Task Configuration menu, click **Video Sources**, then click **Add**.

<!-- Screenshot of video source page -->

![](images/img_14.webp)

In the dialog that appears:

- Source type: Offline Video
- Channel name: Give it a descriptive name, e.g., **Building 1 East Corridor**
- Upload video: Upload the demo MP4 file, then click **Save**.

<!-- Screenshot of add channel dialog -->

![](images/img_15.webp)

Click **Scenario Task Assignment**.

<!-- Screenshot of task assignment button -->

![](images/img_16.webp)

3. **Assign an algorithm to the channel**

   Configure the video analytics algorithm for the channel.

<!-- Screenshot of algorithm assignment -->

![](images/img_17.webp)

Page layout overview:

<!-- Screenshot showing the three functional areas -->

![](images/img_18.webp)

The Service Assignment page has three main areas:

- **Area 1**: All available algorithm types — Detection/Analysis, Face/Body, Counting/Statistics.
- **Area 2**: Controls for starting, stopping, and deleting services on the channel.
- **Area 3**: Service configuration panel — detection region setup, parameter tuning, runtime strategy, etc.

Under **Counting/Statistics**, find **Pedestrian Flow Counting** and select it.

<!-- Screenshot of selecting pedestrian flow counting -->

![](images/img_19.webp)

The configuration parameters for the selected algorithm will appear. The pedestrian flow counting algorithm works by detecting pedestrians crossing a detection line. Key controls include:

- **Draw**: Draw a detection line.
- **Direction Toggle**: Switch the crossing direction for one-sided counting.
- **Delete**: Remove the detection line.

<!-- Screenshot of detection line parameters -->

![](images/img_20.webp)

> **💡 Configuration Parameters**
>
> Parameters are determined by the algorithm's pipeline orchestration.

**Draw the detection line**

Click **Draw**, then click once in the video to set the starting point, and click again to set the endpoint. (The line counts pedestrians crossing in the direction of the arrow.)

<!-- Screenshot of drawing process -->

![](images/img_21.webp)

<!-- Screenshot of completed line -->

![](images/img_22.webp)

Click **Finish Drawing** to save. The detection line is now configured.

<!-- Screenshot of saved detection line -->

![](images/img_23.webp)

**Set the runtime strategy**

Set the play count to 0 for looping playback.

<!-- Screenshot of runtime strategy -->

![](images/img_24.webp)

Click **Save**. This assigns the pedestrian flow counting algorithm to the channel and simultaneously starts the analysis service.

<!-- Screenshot of save confirmation -->

![](images/img_25.webp)

Return to the **Video Sources** menu. The "Running" toggle being on indicates the service is active.

<!-- Screenshot of running status -->

![](images/img_26.webp)

4. **Algorithm Visualization**

Click **Live Preview** to enter the visualization page.

<!-- Screenshot of live preview entry -->

![](images/img_27.webp)

The visualization page has three areas:

- **Left panel**: All service channels in the system.
- **Center area**: Visual algorithm display — select a channel to view.
- **Right panel**: Real-time scrolling alarm feed from all active algorithm tasks.

<!-- Screenshot of visualization page layout -->

![](images/img_28.webp)

> **💡 Layout Options**
> Use the toggle in the upper-right corner to switch between **single view** and **quad view** layouts.

Select the **Building 1 East Corridor** channel to stream its algorithm-enhanced video.

<!-- Screenshot of channel selection -->

![](images/img_29.webp)

Under algorithm overlay, select **Pedestrian Flow Counting**. Different algorithms have different overlay behaviors.

<!-- Screenshot of overlay selection -->

![](images/img_30.webp)

> **💡 Overlay Logic**
>
> Each algorithm's pipeline strategy determines its own overlay behavior.

**Overlay results:**

<!-- Screenshot of overlay results -->

![](images/img_31.webp)

The visualization includes:

1. **Pedestrian detection**: Object class and confidence score, e.g., `Pedestrian: 0.91`
2. **Pedestrian tracking**: Unique track ID for each person, e.g., `#1318`
3. **Pipeline timing**: Upper-left area shows per-component latency breakdown.
4. **Counting results**: Upper-right area shows IN/OUT pedestrian flow totals.

**Demo walkthrough**

> Video demo: Coming soon.

### Step 4: View Statistics

All AI-detected alarms and statistical events are automatically recorded and can be reviewed later.

1. In the left navigation, click **Event Center** → **Counting/Statistics**.

<!-- Screenshot of event center navigation -->

![](images/img_32.webp)

2. Select **Query Events** → **Channel Name** → **Algorithm Service** → click **Query**. (Click Query again to refresh with the latest real-time statistics.)

<!-- Screenshot of query results -->

![](images/img_33.webp)

Page note: "Departing visitors" = the OUT count from the visualization; "Net inflow" = the IN count.

## 🎉 Quick Start Complete

You've now successfully:

### x86 Server Docker Environment Completion Checklist

- [X] Cloned the CosmoEdge repository and started containers from the repository root
- [X] Accessed the web management console in a browser
- [X] Logged in with the default account and noted that the initial password should be changed
- [X] Found the next tutorial entry for scenario configuration

### Hardware Box Environment Completion Checklist

- [X] Powered on the device and connected it to the network
- [X] Accessed the web management console
- [X] Previewed pre-deployed real-time AI detection demos
- [X] Reviewed counting/statistics records

**What's next:**

| Goal                                                         | Read                                          |
| ------------------------------------------------------------ | --------------------------------------------- |
| Configure your own AI detection scenarios                    | → Scenario Configuration Guide (Volume 2)    |
| Try the VLM — switch detection rules without model training | → VLM Visual State Judgment Guide (Volume 3) |
| Look up a specific parameter or troubleshoot an issue        | → Reference Manual                           |

---
