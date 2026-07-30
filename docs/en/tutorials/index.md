---
title: Using CosmoEdge
description: Task-oriented guides from the first detection through AI configuration, Pipeline orchestration, and third-party model integration.
prev: false
next:
  text: "Quick Start: Deployment, Sign-In, and First Detection"
  link: /en/tutorials/01-quickstart/quickstart
---

# Using CosmoEdge

These guides are organized around real tasks. Each starts from a defined goal and ends with an observable,
repeatable result. First-time users should follow the sequence; experienced users can enter through one of
the three groups below.

## Basic Use

| Guide | Use it when | Final result |
| --- | --- | --- |
| [Quick Start: Deployment, Sign-In, and First Detection](01-quickstart/quickstart.md) | Deploying or connecting for the first time | A running video channel and a first result verified in Live View and Event Center |
| [Scenario Task Configuration: Channels, Regions, Parameters, and Alarms](02-scenario-config/scenario-config.md) | Turning a built-in algorithm into a business task | Persisted region, parameters, and schedule with positive and negative alarm evidence |

## AI Capability Configuration

| Guide | Use it when | Final result |
| --- | --- | --- |
| [VLM and DINO: Prompt-Driven Vision Tasks](03-vlm-guide/vlm-guide.md) | Exploring a state decision or open-vocabulary target | Fixed samples show that VLM decisions and DINO boxes match the prompt definition |

## Advanced Extensions

| Guide | Use it when | Final result |
| --- | --- | --- |
| [Pipeline Orchestration: Modify and Create Pipelines](04-pipeline-orchestration/pipeline-orchestration.md) | Changing a rule chain or creating new business logic | Nodes persist and the Pipeline passes positive, negative, and restart checks |
| [Third-Party Model Integration: Convert, Upload, and Validate](05-model-porting/model-porting.md) | Bringing a custom model into CosmoEdge | Evidence covers loading, inference, parsing, events, and sustained operation |

## Recommended Paths

- **First experience**: Quick Start → Scenario Task Configuration.
- **Long-tail requirement exploration**: Scenario Task Configuration → VLM and DINO.
- **Custom business logic**: Scenario Task Configuration → Pipeline Orchestration.
- **Custom model deployment**: Pipeline Orchestration → Third-Party Model Integration.

Do not skip the final acceptance of the preceding guide. An uploaded model, a saved task, or a visible box
is only an intermediate state; every path includes result and failure-path validation.

## Additional Deployment Example

[Deploy Ultralytics YOLO with CosmoEdge](06-ultralytics-yolo-edge/ultralytics-yolo-edge.md) remains available
as a separate community-style deployment record. It is not one of the five core guides above, and its own
status and evidence determine whether it is ready for a specific release.

## Related Documentation

- [Deployment Guide](../guide/deployment.md)
- [Runtime Configuration](../guide/configuration.md)
- [Troubleshooting](../guide/troubleshooting.md)
- [Architecture Overview](../guide/architecture.md)
- [Models and Resources](../reference/models.md)
- [HTTP Webhook Reference](../reference/webhook.md)
- [MQTT Reference](../reference/mqtt.md)
- [CI and Quality Checks](../development/ci.md)
