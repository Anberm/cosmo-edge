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
prepared. CosmoEdge 1.1 uses BMRT and `.nn` artifacts validated on CV186X. The two open Sophon
models used by this release benchmark are byte-identical to the BM1688 workload artifacts; other
models still require contract-by-contract compatibility validation.

## 1. Obtain and verify the package

You can build the CV186X package from source:

```bash
docker compose -f docker-compose.sophon.yml run --rm cosmo-sophon-package --chip cv186x
ls -lh build_output/public-runtime/
```

The chip-model argument only selects the CV186X resource directory. It does not
change the output location or package naming: the artifact remains under
`build_output/public-runtime/` and uses the
`cosmo-V<version>-<md5>.tar.gz` format.

Alternatively, download a CosmoEdge 1.1 Sophon Open package explicitly marked
for CV186X and its published SHA-256 from the
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

## 3. Create the first event with the bundled open models

For a `cv186x` build, the build script automatically selects the matching resource
directory. Users do not provide that path. The package includes the two models
used by the public CV186X benchmark:

- `YOLOV8n V1.0.0`: person detector, `1x3x640x640`, 7,023,600-byte model file;
- `helmet V1.0.0`: safety-helmet classifier, `1x3x224x224`, 6,001,416-byte model file.

Their repository directories, input/output contracts, and SHA-256 identities are recorded in the
[ScenarioBench v1.1 model identity record](/benchmarks/scenario-bench/v1.1/models/cv186x.json).
The model subdirectories retain the legacy `prod_BM1688_` compatibility prefix because the files
were copied from the CV186X benchmark device without modification. CV186X qualification is based
on an exact SHA-256 match between the device-loaded files and the copies under the CV186X resource
set. It does not imply that other BM1688 artifacts are interchangeable.

1. Sign in to the Web console, open **Model Repository**, and confirm that both models are present.
2. Add one test stream under **Video Input**, then create and bind a task with the person detector.
   Add the helmet classifier to the orchestration when validating the complete helmet workflow.
3. Open algorithm preview, confirm OSD and an event, then verify that task state and service logs
   show no continuing error.

When importing a custom model, its directory must contain a matching `config.json` and model file.
Input dimensions, quantization, preprocessing, postprocessing, and output tensors must match the
configuration. See the [Model Porting Guide](/en/tutorials/05-model-porting/model-porting) for the
full contract.

## Upgrade, recovery, and evidence boundary

- Installation and Web upgrade use the same `cosmo-V<version>-<md5>.tar.gz` lifecycle. Keep power
  connected and verify both service state and software version after recovery.
- If the service does not recover, inspect `systemctl status cosmo.service` and service logs before
  attempting another upload.
- See the [Deployment Guide](/en/guide/deployment) for directories, ports, persistence, failure
  recovery, and rollback boundaries.
- Public workload results are in [ScenarioBench v1.1](/benchmarks/scenario-bench/v1.1/report.html).
  A highest short-run point is not an official recommended profile; size production deployments
  with the actual models, streams, and accuracy requirements.
