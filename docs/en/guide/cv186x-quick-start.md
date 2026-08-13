---
title: CV186X Quick Start
description: Install CosmoEdge 1.1 on a prepared CV186X Linux device and produce the first detection event.
prev:
  text: Build Guide
  link: /en/guide/build
next:
  text: RK3576 / RKNN Integration
  link: /en/guide/rk3576-rknn-development
---

# CV186X Quick Start

This guide targets a CV186X Linux device with Sophon runtime dependencies and networking already
prepared. CosmoEdge 1.1 uses BMRT and CV186X-specific `.nn` models on this platform; BM1688 model
artifacts are not interchangeable.

## 1. Obtain and verify the package

Download the CosmoEdge 1.1 Sophon Open package and its published SHA-256 from the
[GitHub Release](https://github.com/cosmo-wander-ai/cosmo-edge/releases), then verify it on the build host:

```bash
sha256sum cosmo-V1.1.0-*.tar.gz
scp cosmo-V1.1.0-*.tar.gz root@<device_ip>:/tmp/
```

## 2. Install and start

```bash
ssh root@<device_ip>
install_dir=$(mktemp -d /tmp/cosmo-install.XXXXXX)
tar -xzf /tmp/cosmo-V1.1.0-*.tar.gz -C "$install_dir"
cd "$install_dir"/cosmo-V*/
./scripts/install.sh
reboot
```

After the device returns, confirm that `cosmo.service` is active and verify version 1.1 in the
device console. An existing CosmoEdge device can upload the same archive through **System
Management → System Maintenance → Software Upgrade**.

## 3. Import a model and create the first event

1. Sign in to the Web console and import a `.nn` model directory with `chip_type=CV186X` under
   **Model Repository**.
2. Add one test stream under **Video Input**, then create a detection task and bind the stream.
3. Open algorithm preview, confirm OSD and an event, then verify that task state and service logs
   show no continuing error.

The model directory must contain a matching `config.json` and model file. Input dimensions,
quantization, preprocessing, postprocessing, and output tensors must match the configuration. See
the [Model Porting Guide](/en/tutorials/05-model-porting/model-porting) for the full contract.

## Upgrade, recovery, and evidence boundary

- Installation and Web upgrade use the same `cosmo-V<version>-<md5>.tar.gz` lifecycle. Keep power
  connected and verify both service state and software version after recovery.
- If the service does not recover, inspect `systemctl status cosmo.service` and service logs before
  attempting another upload.
- See the [Deployment Guide](/en/guide/deployment) for directories, ports, persistence, failure
  recovery, and rollback boundaries.
- Public workload results are in [ScenarioBench v1.1](/benchmarks/scenario-bench/v1.1/README).
  A highest short-run point is not an official recommended profile; size production deployments
  with the actual models, streams, and accuracy requirements.

