---
title: "Scenario Task Configuration: Channels, Regions, Parameters, and Alarms"
description: Configure a scenario task by channel, algorithm, detection area, parameters, and schedule, then verify detections and alarms.
prev:
  text: Quick Start
  link: /en/tutorials/01-quickstart/quickstart
next:
  text: VLM and DINO
  link: /en/tutorials/03-vlm-guide/vlm-guide
---

# Scenario Task Configuration: Channels, Regions, Parameters, and Alarms

| Item | Details |
| --- | --- |
| Who this is for | Deployment engineers, integrators, and advanced users configuring built-in algorithms for a real task |
| What you will accomplish | Select an algorithm for a channel, set its region, parameters, and schedule, and prove the alarm rules work |
| Prerequisites | Complete the [first detection](../01-quickstart/quickstart.md) and have a playable video channel |
| Estimated time | 25–40 minutes |
| Device required | A running CosmoEdge instance; the repository video can be used instead of a camera |
| Final acceptance result | Live Display analyzes only the intended region, settings persist, and Event Center contains an explainable alarm |

Configure a scenario task in this order:

1. Select a channel.
2. Select an algorithm.
3. Draw a detection area.
4. Set detection and alarm-related parameters.
5. Set the running schedule.
6. Verify live output and event records.

The current page tabs are **Detection Area**, **Parameter Settings**, and **Running Strategy**. There is
no separate Alarm Configuration tab. Alarm intervals, counts, durations, and deduplication are controlled
by the parameter settings and running strategy together.

![The complete channel, algorithm, area, parameter, live-view, and event workflow](images/img_01.webp)

## 1. What Each Setting Controls

| Setting | Controls | Typical effect | Does not control |
| --- | --- | --- | --- |
| Detection area | The spatial region in the frame | Whether a target enters the area to be analyzed | Model confidence |
| Detection parameters | Model thresholds, duration, counts, and deduplication | Which result is accepted and when an event is formed | Which days and hours the task runs |
| Running strategy | Task schedule | Whether the task runs at the current time | Recognition output |

These layers also define the diagnostic order. A correctly playing video can still produce no alarm
because the area misses the target, the schedule excludes the current time, or an alarm condition has not
yet been satisfied.

## 2. Complete Example: No Safety Helmet

### 2.1 Select a Channel

Prepare a playable channel. This example uses the repository asset:

```text
data/test-video/Safety Helmet.mp4
```

If necessary, add it as an **Offline Video** under **Video Access**. Verify playback before configuring an
algorithm so that video and inference failures are not mixed together.

### 2.2 Select the Algorithm

1. Select **Allocate Task** for the channel.
2. Select **No Safety Helmet** from the available services.
3. Confirm that it appears among the selected services.

![Opening scenario task assignment from a video channel](images/img_03.webp)

![Selecting the No Safety Helmet task from the available services](images/img_04.webp)

Expected state: the selected algorithm is visible and the Detection Area, Parameter Settings, and Running
Strategy tabs are available.

### 2.3 Draw the Detection Area

1. Open **Detection Area**.
2. Select **Add Area** and enter a business-specific name such as “North entrance work zone”.
3. Drag the polygon vertices over the area where helmets are required.
4. Save the area.

![Adding and naming a detection area](images/img_07.webp)

![Adjusting the polygon to cover the actual work zone](images/img_10.webp)

Cover the full worker movement area, while excluding posters, reflective displays, and public roads where
possible. During initial debugging, use a slightly larger area; tighten it after the task works.

### 2.4 Set Parameters

Open **Parameter Settings**. The following names come from the current **No Safety Helmet** resource
template. Other algorithms or versions may expose different fields, so use the labels shown by the current
console.

| Current parameter name | Default | What it controls | Tuning guidance |
| --- | ---: | --- | --- |
| Safety Helmet Confidence | Template configuration | The classification score accepted as a safety-helmet result | Raise gradually for false positives; lower cautiously for missed targets |
| Alarm Interval (sec) | 60 | Minimum time between consecutive events | Shorten for a demo; avoid alert flooding in production |
| Alarm Count | 1 | Number of events allowed for the same alarm type; `0` means unlimited | Match the operational response process |
| Stationary Target Deduplication | Off | Whether repeated events from a stationary target are suppressed | Enable for repeated events caused by fixed images or posters |
| Stationary Target Overlap Rate | 0.2 | Overlap used to decide whether a stationary target is the same one | Tune only when stationary deduplication is enabled |
| Stationary Target Dedup Time (hr) | 6 | Period during which the same stationary target is suppressed | Match the on-site response cycle |
| Pedestrian Detection Method | Bottom | Uses the bottom, center, or top of a box to test region membership | Ground-plane regions usually use Bottom |
| Min Pedestrian Size | 60 | Targets smaller than this do not enter later stages | Lowering can recover distant people but increases noise and load |
| Sensitivity | 2 | Required hit strength across consecutive decisions | Keep the default first, then tune with positive and negative samples |
| Detection Time (sec) | 3 | How long the condition must persist before an event | Increase to suppress transient false positives |

![Parameter Settings for the No Safety Helmet task](images/img_11.webp)

Change one parameter per test round and record the input video, old value, new value, and result. Changing
several values together makes the outcome impossible to attribute.

### 2.5 Set the Running Strategy

Open **Running Strategy** and select the days and time periods when the task should run. During the first
verification, include the current date and time.

![Setting the active days and time periods for a scenario task](images/img_12.webp)

Save the task. Return to Video Access and confirm that the channel's running switch is enabled.

![Saving the scenario task and starting analysis](images/img_14.webp)

### 2.6 Verify the Result

Check detection first, then alarms:

1. Open **Live Display**, select the channel, and enable the **No Safety Helmet** overlay.
2. Confirm continuous playback, the correct area position, and classification output after a person enters.
3. Play a segment containing a person without a helmet and wait longer than **Detection Time (seconds)**.
4. Open **Event Center → Detection / Analysis**.
5. Filter by channel, task, and time; inspect the snapshot against the target, area, and timestamp.

![No Safety Helmet output and an event indication in Live Display](images/img_21.webp)

![Filtering alarm records by channel and time in Event Center](images/img_25.webp)

The acceptance record should include:

- channel and algorithm names;
- a detection-area screenshot;
- every non-default parameter used;
- the running strategy;
- one positive-sample event and one negative segment that should not alarm.

## 3. Parameter Example: Post Absence Detection

Post Absence Detection is not configured by inventing a generic “people threshold” and “absence time”.
The current template exposes these exact fields:

| Current parameter name | Default | Meaning |
| --- | ---: | --- |
| Target Count in Region | 1 | Target count used by the comparison rule |
| Region Target Count Limit Type | Alarm when below target count | Whether below, equal to, or above the count satisfies the rule |
| Detection Time | 10 | Duration for which the count condition must persist |
| Detection Time Unit | Seconds | Unit combined with Detection Time |
| Alarm Interval (sec) | 60 | Minimum interval between consecutive events |
| Stationary Target Deduplication | Off | Whether repeated stationary-target events are suppressed |

To detect “fewer than one person in the post area for 10 seconds”, use
**Target Count in Region = 1**, **Region Target Count Limit Type = Alarm when below target count**,
**Detection Time = 10**, and **Detection Time Unit = Seconds**. Then:

1. Use a staffed segment as a negative sample; it must not create an absence event.
2. Use a segment where the area is empty for more than 10 seconds as a positive sample.
3. Verify that the snapshot area and timestamp match the expected interval.

Positive and negative samples prove that the configuration is active more reliably than a “Saved”
notification.

## 4. How to Prove the Configuration Is Active

Verify four layers in order:

1. **Persistence**: leave and reopen the page; the area, parameters, and strategy retain the saved values.
2. **Runtime**: the channel switch is enabled and the current time is included by the strategy.
3. **Detection**: Live Display shows the expected target, area, and class output.
4. **Event**: a positive sample creates an event, a negative sample does not, and the snapshot is explainable.

A successful save does not prove that inference is running. A bounding box alone does not prove that the
alarm rule is correct.

## 5. Troubleshooting Order

### The Configuration Cannot Be Saved or Does Not Persist

1. Check for required-field, unfinished-area, or parameter-range messages.
2. Retry with one minimal area and default parameters.
3. Reopen the page to confirm persistence.
4. Inspect the browser network request and backend logs for the save API error.

### There Is No Detection or Alarm

Do not lower every threshold first. Check in this order:

1. The video plays.
2. The channel is associated with the algorithm and its running switch is enabled.
3. The current time is included by the running strategy.
4. The target enters the area, using the selected pedestrian box position.
5. Live Display already has a detection result.
6. The positive sample lasts long enough to satisfy detection time and sensitivity.
7. Alarm interval, alarm count, or stationary-target deduplication is not suppressing the event.
8. Only then tune confidence and minimum target size using known samples.

### There Are Too Many False Positives

1. Use the event snapshot to classify the cause: out-of-area target, wrong class, or transient result.
2. Tighten the area or change the pedestrian box position first.
3. Enable stationary-target deduplication for fixed interference.
4. Then raise the relevant confidence, sensitivity, or detection duration incrementally.
5. Change one item at a time and replay the same positive and negative sample set.

If area, rule, and threshold tuning still cannot meet the requirement, a better-matched algorithm or model
may be needed.

## Next Step

Read [VLM and DINO](../03-vlm-guide/vlm-guide.md) to decide which long-tail tasks can be defined in
natural language and which still need a conventional detector.
