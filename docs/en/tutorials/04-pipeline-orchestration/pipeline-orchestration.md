---
title: "Pipeline Orchestration: Modify and Create Pipelines"
description: Understand Pipeline inputs, models, rules, and outputs; safely modify an existing task and create a region-intrusion task.
prev:
  text: VLM and DINO
  link: /en/tutorials/03-vlm-guide/vlm-guide
next:
  text: Third-Party Model Integration
  link: /en/tutorials/05-model-porting/model-porting
---

# Pipeline Orchestration: Modify and Create Pipelines

| Item | Details |
| --- | --- |
| Who this is for | Advanced users and developers who need to modify business logic or combine models, rules, and event output |
| What you will accomplish | Understand node data flow, modify an existing Pipeline, and create an accepted region-intrusion Pipeline |
| Prerequisites | Complete scenario task configuration and understand channels, detection areas, parameters, and Event Center |
| Estimated time | About 20 minutes for the modification path; 35–50 minutes for the new-Pipeline path |
| Device required | A running CosmoEdge instance and a playable channel; the example uses an installed pedestrian model |
| Final acceptance result | The filter change can be compared before and after; the new Pipeline detects people in a region and emits queryable events |

This page provides two independent paths:

- **Path A: Modify an existing Pipeline** for a localized configuration change.
- **Path B: Create a new Pipeline** for a new processing chain.

Changing a task used by a running channel affects its output. Capture the current configuration first and
prefer a test channel. For rollback, retain the task package created with **Save and Export** before editing.

## 1. The Four Pipeline Layers

A runnable Pipeline organizes four node types according to their data contracts:

| Layer | Current node examples | Input | Output |
| --- | --- | --- | --- |
| Input | Video Decode | Video file or network stream | Image frames for inference |
| Model | Object Detection, Object Classification, Detection VFM, Vision-Language Model | Image or upstream targets | Boxes, classes, scores, or semantic decisions |
| Rule | Tracking, Category Filter, Region Alarm, Target Eval, Sensitivity (Timer) | Upstream targets and attributes | Associated, filtered, or accepted targets |
| Output | Event Report | Results that satisfy rules | Events, snapshots, and optional video clips |

Data moves downstream in node order:

1. Video Decode produces image frames.
2. Object Detection attaches boxes, classes, and scores.
3. Tracking associates the same target across frames.
4. Class, size, and region rules filter or accept targets.
5. Event Report persists results that satisfy the rules.

A Region Alarm node cannot run before Object Detection, and a rule that requires a tracking identifier
cannot read raw single-frame detections. For every node, ask both “what does it do?” and “what input does it
require and output does it provide?”

![The node order of the No Safety Helmet Pipeline](images/img_04.webp)

## 2. Path A: Modify an Existing Pipeline

### 2.1 Example Goal and Acceptance Design

This example changes **Min Pedestrian Size** in the **Category Filter** node of **No Safety Helmet**.

| Item | Definition |
| --- | --- |
| Input | The same helmet video containing both near and distant people |
| Before | The current template uses `60`, filtering pedestrians with a side below about 60 pixels |
| Test configuration | Temporarily change Min Pedestrian Size to `100` |
| Expected output | Fewer distant small targets, while clear nearby people still reach classification and event rules |
| Acceptance | Compare boxes and events for the same video, ROI, and period; then retain the value or restore `60` |

A larger value filters more aggressively. It does not guarantee fewer false alarms and can increase misses,
so the same input must be compared before and after.

### 2.2 Open and Back Up the Task

1. Open **Scene Tasks** and locate **No Safety Helmet**.
2. Record every channel currently using it.
3. Select **Arrange Algorithm**.
4. Use **Save and Export** and record the package and time.

A backup does not replace test isolation. If production channels are active, use a maintenance window or
bind an imported test copy to a dedicated channel.

### 2.3 Inspect Node Placement

The current **No Safety Helmet** order is:

| Order | Node | Role in this example |
| ---: | --- | --- |
| 1 | Video Decode | Produce image frames |
| 2 | Object Detection | Detect pedestrians |
| 3 | Tracking | Associate people across frames |
| 4 | Category Filter | Keep pedestrians and remove targets below the size threshold |
| 5 | Object Classification | Classify helmet and no-helmet states |
| 6 | Target Eval | Apply classification rules |
| 7 | Sensitivity (Timer) | Suppress transient results |
| 8 | Event Report | Create events |

Modify the existing **Category Filter**; do not add a duplicate node.

![The Category Filter node and its label and size controls](images/img_10.webp)

### 2.4 Change and Save

1. Select **Category Filter**.
2. Confirm that the pedestrian label and minimum-size filtering are enabled.
3. Open **Parameter Settings**.
4. Change **Min Pedestrian Size** from `60` to `100`.
5. Save the Pipeline.

![Changing Min Pedestrian Size under Parameter Settings](images/img_13.webp)

![Saving the modified Pipeline](images/img_15.webp)

If the page reports an unconfigured node, broken flow, or missing atomic model, do not force the save.
Restore the required input and fields first.

### 2.5 Compare, Accept, or Roll Back

1. Bind a test channel to the modified task and start it.
2. Fix the channel, ROI, video segment, and playback start.
3. Record near and distant boxes in Live Display.
4. Record events for the same period in Event Center.
5. Compare the evidence with the baseline.

Pass when distant small targets are filtered as intended and important nearby people still receive
detection, classification, and valid events. If important people are missed, reduce the value stepwise to
`80` or restore `60`, replaying the same segment each time.

## 3. Path B: Create a New Pipeline

This example creates **Region Intrusion Detection**. The current resource template already uses this node
relationship, so the guide starts from a known minimum business chain.

### 3.1 Define Input, Configuration, Output, and Acceptance

| Item | Definition |
| --- | --- |
| Input | A playable channel containing a person moving into and outside a defined region |
| Model | Installed `PedestrianDetection` atomic model |
| Rules | Track people, keep the pedestrian class, decide whether a target enters the main area |
| Output | Region-intrusion event with the person target and snapshot |
| Positive sample | A clearly visible person enters the area and satisfies the duration |
| Negative sample | A person passes only outside the area |
| Acceptance | Positive has a box and event; negative has no intrusion event; the task still works after restart |

### 3.2 Create the Task

1. Open **Scene Tasks** and select **New Task**.
2. Name it “Region Intrusion Validation”.
3. Select **Video Analysis** as the data-source type.
4. Select **Detection / Analysis** as the task type.
5. Confirm, then select **Arrange Algorithm**.

![Creating a video-analysis Region Intrusion task](images/img_23.webp)

An empty flow contains only Start and End. Select the plus sign between them and choose **Add Component**.

![An empty Pipeline containing only Start and End](images/img_27.webp)

![The algorithm-action and business-processing component palette](images/img_30.webp)

### 3.3 Add and Configure Nodes in Order

Complete these six numbered steps:

1. **Video Decode**
   Place it first. It receives the channel video and emits image frames; it normally has no business
   parameters.

2. **Object Detection**
   Select the `PedestrianDetection` atomic model and pedestrian label. Tune frame sampling only after
   measuring the target device.

   ![Selecting the pedestrian atomic model and label](images/img_36.webp)

3. **Tracking**
   Use the pedestrian detection output. Enable tracking-state features only when a downstream rule requires
   them; keep the current template defaults for the first check.

4. **Category Filter**
   Keep the pedestrian label and start with the current Min Pedestrian Size of `60`.

5. **Region Alarm**
   Select the main area as input. This node evaluates whether pedestrians enter the area drawn during
   scenario task assignment.

   ![Using the main area in Region Alarm](images/img_42.webp)

6. **Event Report**
   Select a triggered event with target tracking. Video clips increase storage and transfer load, so enable
   them only when required; retain at least a snapshot for local acceptance.

   ![Configuring event reporting for region intrusion](images/img_48.webp)

The completed order must be:

1. Video Decode
2. Object Detection
3. Tracking
4. Category Filter
5. Region Alarm
6. Event Report

Save, reopen the orchestration page, and confirm that every node and field persists.

### 3.4 Configure Task Parameters

Under **Parameter Settings**, start from an explainable baseline:

| Parameter | Baseline | Verification intent |
| --- | ---: | --- |
| Pedestrian Detection Method | Bottom | Use the person's foot position for a ground-plane area |
| Min Pedestrian Size | 60 | Remove very small pedestrian targets |
| Detection Time | 0 seconds | Prove the chain first; increase later to suppress brief entry |
| Alarm Interval (sec) | 3 | Convenient for a short test; increase for the production response process |
| Alarm Count | 1 | Verify a single trigger first |
| Stationary Target Deduplication | Off | Observe raw behavior before deciding whether to enable it |

Model confidence comes from the selected atomic model configuration. Do not copy a threshold from another
model; tune within the values accepted by the current page using fixed samples.

### 3.5 Assign a Channel and Area

1. Select the test channel in **Video Access**, then select **Allocate Task**.
2. Select “Region Intrusion Validation”.
3. Create the main area so the positive person enters it while the negative person stays outside.
4. Include the current time in **Running Strategy**.
5. Save and enable the service.

![Enabling and saving the Region Intrusion task on a channel](images/img_64.webp)

### 3.6 Accept the Pipeline

1. Enable the “Region Intrusion Validation” overlay in Live Display.
2. Play the negative sample and confirm that an outside person creates no intrusion event.
3. Play the positive sample and confirm a person box and event after entry.
4. Inspect event type, channel, snapshot, and time in Event Center.
5. Stop and restart the task, then replay a positive segment.

![A live event indication caused by a person inside the region](images/img_68.webp)

![The persisted Region Intrusion record in Event Center](images/img_69.webp)

Final pass criteria:

- nodes persist without a broken connection;
- the positive sample produces a pedestrian box and one explainable event;
- the negative sample produces no region-intrusion event;
- the restarted task still processes video and emits an event.

## 4. Troubleshooting

### A Configuration Change Has No Effect

1. Reopen the Pipeline and verify persistence.
2. Confirm that the test channel uses the modified task, not an older task with the same name.
3. Check the channel switch and running strategy.
4. Confirm that the edited field belongs to the node and label actually in use.
5. Compare the current configuration with the export or change record.

### A Node Does Not Run

1. Confirm output layer by layer, starting at Video Decode.
2. Verify that the atomic model is installed and matches the active device backend.
3. Check node order and upstream data type.
4. Locate the first initialization or processing failure in logs; silent downstream nodes are usually a
   consequence of an upstream failure.
5. Reduce the chain to Video Decode plus Object Detection, then restore rule nodes one at a time.

### Detection Works but No Result Is Emitted

1. Confirm that Category Filter does not remove every target.
2. Confirm that the target enters the area using the selected box position.
3. Restore detection duration and alarm suppression to a testable baseline.
4. Confirm that Event Report exists after the rules and is configured for a triggered event.
5. Check Event Center filters and storage space.

### Saving Fails or Node Connections Are Invalid

Record the page error instead of retrying repeatedly. Check for:

- a model node without a selected atomic model;
- a node whose required upstream data is absent;
- duplicate rule nodes that perform the same responsibility;
- an imported task referencing a model unavailable on this device.

If the cause is not clear, restore the exported working version and add one change at a time.

## 5. Pipeline Design Checklist

- [ ] The input node can independently produce frames.
- [ ] Every model node has an atomic model for the active device.
- [ ] Every rule receives the target, class, or tracking identifier it requires.
- [ ] Filtering occurs before expensive downstream processing.
- [ ] Event Report follows the final decision.
- [ ] Every example has fixed input, configuration, expected output, and positive/negative acceptance samples.
- [ ] A recoverable export or configuration record exists before modification.

## Next Step

Read [Third-Party Model Integration](../05-model-porting/model-porting.md) to convert, upload, and connect
a new model capability to an accepted Pipeline.
