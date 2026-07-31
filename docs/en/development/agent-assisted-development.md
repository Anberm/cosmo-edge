---
title: Agent-Assisted Development
description: Delegate CosmoEdge extension work to a coding agent in an isolated development environment and receive verifiable deliverables.
prev:
  text: CI and Quality Checks
  link: /en/development/ci
next: false
---

# Agent-Assisted Development

You can hand the CosmoEdge development task you already need to do to the coding agent you already use,
such as Codex, Claude Code, or Copilot. Open this repository on a development machine isolated from
production, then grant access through that agent's existing workspace and terminal controls. You do not
need a CosmoEdge-specific agent, a new authorization service, or prior knowledge of prompts and script
arguments.

Your business task decides whether the deliverable is code, configuration, model artifacts, or deployment
material, and which device it must serve. Chip flags, precision, compiler releases, containers, scripts,
and repository examples are usually implementation choices, not knowledge you need before describing the
task. Tutorials, examples, templates, and scripts are assets the agent may reuse, not one mandatory answer.

## Say the Task Directly

Open the repository, start your usual coding agent, and describe the work as you would to a developer:

- “I have a trained person-detection model and 20 sanitized test images. It needs to run on an isolated
  test device matching the field hardware. Missing fewer people matters more than maximum throughput;
  one real-time stream is enough. The model, training notes, and device model are in the directories I
  identify. Inspect the materials and development environment, choose an implementation path, and return
  an importable model, changes, and verification conclusions. Do not connect to production.”
- “My test service must receive CosmoEdge alarms and write them to our existing ticket system. The API
  description and test account are available through the secure mechanism. Reuse repository assets,
  complete a local integration test, and return code, operating notes, and failure-case evidence.”
- “Operators often enter this setting incorrectly. Add an early warning and reject obviously invalid
  values while keeping the existing interaction style, then verify the build.”
- “I do not know whether this machine is ready. Inspect it read-only first. If it is unsuitable, tell me
  what isolated environment to prepare; do not install or change system configuration.”

Usually you only need four things: the business outcome, available materials, target/test context, and
acceptance preference. You do not need to select ONNX, BM1688, F16, a Docker image, or a script first.
Known technical constraints are useful optional input. Otherwise provide the device model or non-sensitive
device information. The agent recommends a route from the material, repository, actual environment, and
current official instructions. If no reliable mapping exists, it keeps that layer unverified.

The agent should not ask about technical facts it can discover. It should interrupt only for a consolidated
minimum question when business/material input changes the deliverable, no executable isolated environment
exists, or the next step needs new environment-change, remote-execution, model-transfer, or device-deployment
authority.

## Optional Full Work Request

Use this longer version when you also want to make environment and authority boundaries explicit:

```text
Read the repository-root AGENTS.md first.
Task: <describe the business outcome, materials, test context, and expected deliverables>.
I authorize read-only checks needed for this task in the current development environment
and repository. File writes or dependency installation are limited to what I explicitly approve.
Inventory the materials and credible routes read-only first; do not make me preselect a compiler release,
container, or script. After recommending a route, inspect the actual execution environment. Continue if
ready. If repair is possible, list the change, impact, and rollback for approval. Otherwise tell me what
isolated environment to provide. State any repository capability gap.
Once ready, select the closest examples, templates, scripts, and code facts and record the differences.
Return deliverables, measured evidence, and unverified boundaries.
Do not treat development-machine authority as device or production authority, and do not request
production credentials.
```

The agent turns this request into a private, run-local task record. Before execution, it restates the task
and deliverables in one or two sentences; you confirm that language, not JSON. It then runs read-only
`assess` to produce route candidates and `needsInput`. Those structures are internal execution inputs.
Only unresolved entries are translated into an ordinary-language question.

Run records live under the Git-ignored `output/agent-runs/<run-id>/`. That directory must still never
contain passwords, tokens, private keys, or credential-bearing URLs. Use isolated checkouts or workspaces
for different customers on a shared development machine.

## How Route Selection and Environment Admission Work

The agent first runs `scripts/agent/assess.sh` (or `scripts/agent/assess.ps1` on Windows) to inventory
materials, the current host, repository facts, and authority, then compare credible routes. Coverage in
current official upstream instructions makes a route eligible to assess; it does not prove that this
machine is ready. After selecting a route, `doctor` checks only the architecture, resources, runtimes, and
callable tools required by this task. This read-only phase does not install software, pull images, start
services, change groups, or elevate privileges.

Route assessment can return `READY`, `NEEDS_INPUT`, `NEEDS_ENVIRONMENT`, or `UNSUPPORTED`; users do not
need to memorize these states. `needsInput` exists so the agent can generate the minimum question. The
actual execution-environment check may return:

| Script result | Meaning | Next action |
| --- | --- | --- |
| `READY` | This development machine meets the task requirements | Freeze its identity and continue |
| `REPAIRABLE` | The machine can be completed, but that changes the environment or needs new authority | Review the exact change and impact before approval |
| `NEEDS_ENVIRONMENT` | Architecture, resources, hardware, or permissions cannot be safely completed here | Provide a machine matching the report |
| `UNSUPPORTED` | The repository or known toolchain has no credible implementation path | Record the capability gap; do not substitute a nearby example |

These are script outputs, not words users must put in a prompt. Code, frontend, or integration work without
a dedicated profile still uses the repository's native build and test commands. Missing a specialized
profile alone never makes a task unsupported.

A missing item does not automatically mean the customer's machine is unsuitable. The agent must first
cross-check the current repository instruction, its authoritative upstream source, and the actual command
entry points. This separates a genuinely missing dependency from a repository instruction that mistakes
a base environment for a complete toolchain. When a check's `owner` is `repository`, fix the documentation,
generated contract, or script and rerun validation before asking the customer to install anything. For
example, `sophgo/tpuc_dev` provides a TPU-MLIR base environment; the conversion path is not `READY` until a
callable `tpu_mlir` package and compiler commands are present. Conversely, an official-compatible route
must not be blocked merely because its directory, entry-point layout, or release differs from one example.
Admission freezes the actual package version, image digest, command paths, and hashes used by this run.

Windows can remain the host for the agent, repository inspection, material preparation, and task
orchestration. The Linux requirement for Sophon TPU-MLIR comes from that compiler ecosystem; it does not
mean CosmoEdge as a whole is unsupported on Windows. Prefer an isolated Linux x86_64 environment, local,
containerized, or remote as conditions allow. Treat WSL or another compatibility layer as an explicit
experimental route with tool, filesystem, and device-access risks.

## Keep Authority Coarse-Grained

The mechanism distinguishes only four action classes when needed: changing the development environment,
remote execution, model transfer, and test-device deployment. They are risk gates, not a form the user must
fill. The agent consolidates missing grants for one stage and states the target, impact, and recovery plan.
Workspace access does not imply any of these actions and authority does not carry between tasks.

Passwords, tokens, and private keys belong only in the agent product's secure credential mechanism or a
temporary private session. They must not enter task JSON, command arguments, evidence, documentation, or
Git. Records contain only the granted action class and a sanitized target reference.

## Selectable Repository Assets

| Area | Current assets | Boundary |
| --- | --- | --- |
| Model conversion | [Model Porting Guide](/en/tutorials/05-model-porting/model-porting), ONNX preflight, conversion/evidence scripts, model templates, and the example index | BM1688/F16 is the first preferred deep path; every other chip needs independent mapping and evidence |
| HTTP alarm integration | `test/push-test-service`, [HTTP Webhook Reference](/en/reference/webhook), and related API code | Available through the general workflow; no equally deep executor or measured example in the first version |
| Frontend development | `src/web/`, existing components, and build commands | Follow the requested UI task; do not route it through model conversion |
| Core engine | `src/`, build scripts, and Catch2 tests | Upstream core changes still follow issue, review, and hardware-evidence requirements |

Official promotion of a model-conversion example is represented only by an actual `verified` entry in
`test/agent/examples/model-conversion/index.json`. An empty index means the foundation exists but the
release candidate has not yet met the “two real recordings + fixed toolchain identity + tensor comparison”
bar. It must not be described as a verified example. Normal candidates can still return complete, partial,
or unverified results against their own acceptance.

## When the Deliverable Lives in Another Project

Data collectors and integration services often belong to your own project. Clone CosmoEdge beside that
project, expose both directories to the agent, and add:

```text
Read the adjacent CosmoEdge AGENTS.md and API reference first, then implement in my project.
Use CosmoEdge test/push-test-service for integration; do not connect to a real device.
```

CosmoEdge then provides the interface contract, mock service, and acceptance facts. Your business code
stays in your repository, while run records remain under CosmoEdge `output/agent-runs/` so customer context
does not enter public repository rules.

## Reference Path: Make a Model Run on a Test Device

For “make this person-detection model run on the specified isolated test device, prioritize fewer misses,
and return an importable artifact and verification conclusion,” the agent should:

1. Inventory the supplied model, training/export notes, test samples, and device information. Infer format,
   target mapping, I/O, and preprocessing from inspectable facts; ask only about an unresolved fact that
   changes the deliverable.
2. Run `scripts/agent/assess.sh` to compare credible local-Linux and remote-Linux routes, recommend one,
   and identify any coarse-grained authority gap. A Windows host is not mislabeled as product unsupported.
3. Select the closest tutorial, template, and example and record applicability and differences. Example
   parameters do not silently become user requirements.
4. Place the source in the private current run and generate the task record; the user writes neither a
   Schema nor a toolchain release.
5. Run `scripts/agent/doctor.sh` in the Linux environment that will execute the conversion. It discovers
   compatible capabilities and freezes actual releases, paths, and hashes after admission. It never pulls
   an image or installs a dependency.
6. Run `scripts/agent/convert_model.sh`. It calls `tools/check_onnx_model.py`, then invokes
   `model_transform` and `model_deploy` through the admitted TPU-MLIR package, Python, and command identity.
   Docker participates only when the task selects a complete-toolchain image. Generated files do not
   overwrite existing model resources.
7. Run `scripts/agent/verify.sh` to recheck source and artifact hashes and produce machine-readable
   `evidence.json` plus readable `evidence.md`. Reruns preserve earlier failures; only a new measured pass
   or an explicit user waiver explains the later state.

Candidate parameters come from this task, not a YOLOv8n example's fixed shape, class count, or hash. An
explicit request for another chip must not be rewritten as BM1688. Without independent evidence mapping
compiler option to runtime identifier and artifact, only chip-independent preparation can complete.

The first conversion deliverable is `.bmodel + execution-manifest.json + evidence`. A `.bmodel` can continue
through **Add Model**, but it is not the `model.nn` file in a full import package and cannot become CENN by
renaming. Package directory, `config.json`, `model.nn`, and package checks enter scope only when the user
requests a complete package.

## Read the Verification Layers

| Layer | What it proves | What it does not prove |
| --- | --- | --- |
| ONNX preflight | Graph checking, zero-input ONNX Runtime smoke, and recorded I/O | Detection accuracy |
| Conversion artifact | The frozen toolchain generated the artifact and its hash/model info matches this candidate | Device import or sustained runtime |
| Tensor comparison | The toolchain compared outputs using a user override or the admitted tool's default tolerance policy, and recorded that policy | Business-dataset accuracy |
| Full-package check | When requested, package name, configuration, and files are self-consistent | Production compatibility |
| Device/business acceptance | Requires separately authorized test hardware, image/video runs, and accuracy evaluation | Never inherited automatically from a local pass |

Task completion and official-example promotion are separate conclusions. A missing second recording must
not turn a normal task that met its acceptance into failure. Conversely, local completion is not device or
production acceptance.

## Test-Device Risk

This path does not connect to or deploy onto a real device by default. Development-machine authority never
extends automatically to a device.

If you deliberately want the agent to operate a recoverable test device isolated from production, state
the exact target, read-only versus deployment scope, recovery method, and risk in the private session. Use
the agent product's secure connection or credential input mechanism. Giving a test-device IP, username,
and password to an agent is still a risky development experiment: product accounts may have write access,
and the agent host may retain conversation data. Prefer temporary or least-privilege accounts, network and
data isolation, and credential rotation after the task.

Never put credentials in a prompt template, task JSON, `AGENTS.md`, documentation, command evidence, or
Git. Production devices, production credentials, and unrecoverable writes are outside this entry's
default support boundary.
