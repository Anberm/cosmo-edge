---
title: "VLM and DINO: Prompt-Driven Vision Tasks"
description: Use a VLM for visual-state decisions, use DINO to locate open-vocabulary targets, and validate prompts with fixed samples.
prev:
  text: Scenario Task Configuration
  link: /en/tutorials/02-scenario-config/scenario-config
next:
  text: Pipeline Orchestration
  link: /en/tutorials/04-pipeline-orchestration/pipeline-orchestration
---

# VLM and DINO: Prompt-Driven Vision Tasks

| Item | Details |
| --- | --- |
| Who this is for | Solution engineers, ML engineers, and advanced users validating long-tail visual requirements |
| What you will accomplish | Create a VLM state-decision task, create a DINO open-vocabulary detector, and evaluate both with fixed samples |
| Prerequisites | Know how to configure channels, regions, parameters, and schedules; the required model and tokenizer are installed |
| Estimated time | 30–45 minutes |
| Device required | Local Qwen VLM requires the Sophon backend and matching models; DINO needs a model for the active backend; a compatible remote VLM can also be configured |
| Final acceptance result | VLM decisions are stable on positive and negative samples, DINO draws boxes at the expected targets, and events match the prompt definition |

This page starts with a task question: **How do you turn a prompt into a testable visual rule?**

![Examples of visual-state questions that can be explored with a VLM](images/img_01.webp)

## 1. Choose the Right Capability First

| Capability | Best question | Output | Typical use | Main limitation |
| --- | --- | --- | --- | --- |
| Conventional detector/classifier | Does a known fixed class appear? | Boxes, classes, scores | People, helmets, vehicles, and other frequent real-time tasks | New classes usually need model training and deployment |
| VLM | Does the image satisfy a semantic description? | Yes/no decision and event | Door open/closed, area cleanliness, suspected damage | Prompt, image quality, and semantics affect output; local Qwen currently requires Sophon |
| DINO | Where is an open-vocabulary object? | Boxes, classes, scores | Fire extinguishers, garbage, packages | Targets must be visually discernible; small, dense, or vague targets need testing |

Selection rules:

- Prefer a conventional model when a suitable one exists and the task requires stable, low-latency,
  fixed-class detection.
- Validate a VLM when the task is a state question rather than precise localization.
- Validate DINO when the task asks where a visible object class is.
- Do not deploy a one-line prompt alone for exact counting, fine-grained identity, tiny targets, or a
  safety-critical control loop. Evaluate labeled samples and quantitative acceptance criteria, and train a
  dedicated model when necessary.

A template visible in the console does not prove that its model files are installed. Before creating a task,
verify the model in **Model Repository** and confirm that the device has sufficient memory.

## 2. VLM: Create a Visual-State Task

A VLM receives an image or ROI together with a prompt, then passes the decision to event reporting. It
usually does not produce object boxes, so acceptance focuses on decisions and events rather than OSD.

### 2.1 Define a Decidable Question

An actionable prompt includes:

1. **Observation target**: river surface, cabinet, work zone.
2. **Visible condition**: floating object, open door, fallen person.
3. **Exclusions**: reflections, shadows, normal construction.
4. **One decision**: a question that can be answered yes or no.

Example:

```text
Determine whether the river-surface area contains plastic bags, bottles,
clusters of leaves, or other obvious floating debris.
Ignore reflections and small ripples. Answer only yes or no.
```

| Input | Expected result |
| --- | --- |
| A clearly visible plastic bottle or bag on the water | Yes, then an event when the reporting rule is satisfied |
| Only ripples, reflections, or tiny unidentifiable spots | No event of this type |

“Is anything wrong?” is too vague. “Does the wall show obvious human-made carving, impact damage, or
fresh loss, ignoring natural weathering and shadows?” is easier to evaluate consistently.

### 2.2 Establish a Sample Baseline with Image Analysis

Validate a new requirement with images before waiting for a rare video event.

1. Open **Scene Tasks** and create an **Image Analysis** task.
2. Select **Arrange Algorithm** and add **Vision-Language Model**.
3. Select an installed atomic model, set the prompt and generation style, then save.
4. Open **Image Analysis** and select the new task.
5. Upload positive and negative samples, run analysis, and record each result.

![Creating an image-analysis VLM task](images/img_16.webp)

![Entering the prompt in the Vision-Language Model node](images/img_20.webp)

![Uploading an image and running VLM analysis](images/img_23.webp)

For an initial feasibility check, prepare at least:

- five clear positive samples;
- five clear negative samples;
- common difficult samples from the deployment, such as reflections, occlusion, night scenes, or similar
  objects.

This is not a sufficient production accuracy dataset, but it exposes definition, model-availability, and
obvious classification failures quickly. Keep the expected and actual label for every image and rerun the
same set after changing the prompt.

### 2.3 Bind the Task to Video and Verify Events

After the image baseline passes:

1. Create or open a video task containing **Vision-Language Model** and **Event Report**.
2. Set the frame-sampling rate. Higher rates increase local compute or remote request pressure; start with
   a low-frequency inspection workload.
3. Assign the task to a video channel.
4. Draw an ROI around only the area to be judged.
5. Set the alarm interval and running strategy, then save and start.

![Model, frame rate, and prompt fields in the VLM node](images/img_03.webp)

![Drawing the area to be judged by the VLM](images/img_09.webp)

![An event indication produced by the VLM decision](images/img_12.webp)

Acceptance requires:

- a positive segment produces the expected decision;
- a negative segment does not create this event;
- the Event Center snapshot comes from the intended ROI and time;
- sustained operation has no repeating model-load, remote-timeout, or resource-exhaustion error.

::: warning Local and remote VLM boundaries
The current local Qwen VLM implementation requires the Sophon backend and matching model resources. A
compatible remote VLM also introduces network reliability, latency, cost, credential storage, and image-data
compliance requirements. A successful API call does not prove the on-site task has passed acceptance.
:::

## 3. DINO: Create an Open-Vocabulary Detection Task

A DINO prompt names the objects to locate, and its output is a set of boxes. Start with concrete visible
nouns. Do not use an action, risk, or complex state as though it were an object class.

### 3.1 Define the Targets

Example input:

```text
person.garbage
```

The current template separates multiple targets with an English period. Expected output:

- people are labeled `person`;
- discernible garbage is labeled `garbage`;
- the background does not produce large numbers of matching boxes;
- a target inside the configured region creates an event after the event rule is satisfied.

If the combined prompt is unstable, test `person` and `garbage` separately to isolate the class causing the
problem.

### 3.2 Configure and Run

1. Open or create a video task that uses **Detection VFM**.
2. In the pipeline, select the DINO atomic model, enter the target names, and save.
3. Under **Video Access**, select **Allocate Task** and choose the task.
4. Draw the area, set parameters and the running strategy, then save.
5. Open **Live Display** and enable the algorithm overlay.

![Entering DINO target names in the Detection VFM node](images/img_26.webp)

![Drawing the detection area for the DINO task](images/img_29.webp)

![DINO open-vocabulary boxes in Live Display](images/img_31.webp)

Detection overlays may update later than the source frame. Use timestamped snapshots and Event Center
records for acceptance instead of relying on one visual impression of synchronization.

### 3.3 DINO Prompt Guidance

| Requirement | Start with | Avoid |
| --- | --- | --- |
| People | `person` | `someone behaving suspiciously` |
| Fire extinguishers | `fire extinguisher` | `fire safety is abnormal` |
| Garbage | Test `garbage` and `trash` separately | `the area is dirty` |
| Packages | Test `package` and `box` separately | `dangerous parcel` |
| Vehicles | Choose `car` or the broader `vehicle` | `illegal parking` |

“Illegal parking”, “dangerous”, and “clean” contain rules or state. DINO can propose candidate boxes, while
region, duration, and other rules are applied later in a Pipeline.

## 4. Prompt Validation Record

Record at least:

| Item | Required evidence |
| --- | --- |
| Task definition | One sentence describing the triggering condition |
| Prompt version | Complete text and change time |
| Input samples | A fixed set of positive, negative, and difficult samples |
| Runtime conditions | Model, device backend, ROI, frame rate, and thresholds |
| Actual result | Decision, boxes, and events for each input |
| Acceptance criteria | Allowed miss rate, false positives, latency, and resource range |

A passing positive sample is not enough. Negative and difficult on-site samples determine whether the task
can be deployed reliably.

## 5. Troubleshooting

### The VLM Produces No Result

1. Confirm that the atomic model and tokenizer are installed, not merely that a task template exists.
2. For local Qwen, verify the Sophon backend. For a remote service, verify network access, endpoint, model
   name, and credentials.
3. Test one clear image first to remove video sampling and ROI from the problem.
4. Inspect logs for model initialization, insufficient memory, request timeout, or response parsing errors.
5. Restore the video task only after image analysis passes.

### VLM Results Are Unstable

1. Fix the model, generation style, and sample set.
2. Split a vague problem into one visible, singular decision.
3. Add essential visual features and explicit exclusions to the prompt.
4. Tighten the ROI to remove irrelevant background.
5. If identical inputs still fail the acceptance criteria, use a dedicated model or human review instead of
   endlessly expanding the prompt.

### DINO Produces No Boxes or Too Many Boxes

1. Confirm that the target is large enough and visible to a person.
2. Test one concrete English noun at a time.
3. Test synonyms separately and record them instead of combining several near-synonyms.
4. Check ROI, confidence, and model resources.
5. For a fixed frequent class, compare the stability and resource cost of a conventional detector.

### The Target Definition Is Ambiguous

Ask domain owners to label a small set of images as “should trigger” and “should not trigger”. If reviewers
cannot agree, neither the prompt nor the model has a stable acceptance standard. Clarify the business
boundary before choosing the technology.

## Acceptance Checklist

- [ ] The choice of VLM, DINO, or conventional model is justified.
- [ ] The VLM has been tested on a fixed positive and negative image set.
- [ ] DINO outputs the expected classes and boxes at the target positions.
- [ ] Video ROI, frame rate, parameters, and schedule are recorded.
- [ ] Event Center results match the task definition.
- [ ] A sustained run shows no model, network, or resource errors.

## Next Step

Read [Pipeline Orchestration](../04-pipeline-orchestration/pipeline-orchestration.md) to organize inputs,
models, rules, and outputs into a maintainable Pipeline.
