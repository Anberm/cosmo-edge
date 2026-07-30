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

Your task decides the model, chip, mapping, change scope, and whether the deliverable is code,
configuration, model artifacts, or deployment material. Repository tutorials, examples, templates, and
scripts are reusable assets, not one mandatory answer for every task.

## Say the Task Directly

Open the repository, start your usual coding agent, and describe the original development task:

- “Convert this ONNX model into an F16 `.bmodel` for BM1688 and return conversion and verification
  evidence.”
- “Use the repository's HTTP alarm example to connect alarm pushes to my test service and complete a
  local integration test.”
- “Add a frontend setting that follows the existing interaction style, and verify the build.”
- “I do not know whether this machine is ready. Check it first; if it is not, tell me what environment I
  need, and do not install anything.”

State the outcome, inputs you already have, and expected deliverables. You do not need to copy these
examples literally. If you do not know a chip name or quantization term, provide the device model or
non-sensitive device information. The agent should resolve the mapping from current repository facts and
measured records. If no reliable mapping exists, it must keep that layer unverified instead of guessing.

## Optional Full Work Request

Use this longer version when you also want to make environment and authority boundaries explicit:

```text
Read the repository-root AGENTS.md first.
Task: <describe the development outcome and expected deliverables in ordinary language>.
I authorize read-only checks needed for this task in the current development environment
and repository. File writes or dependency installation are limited to what I explicitly approve.
Check whether this machine meets the task requirements before execution. Continue if it does.
If it can be repaired, list the exact changes for approval. If it cannot, tell me what development
environment to provide. If the repository has no credible path, state the gap.
Once ready, select the closest examples, templates, scripts, and code facts; explain the differences,
then execute. Return the deliverables, measured verification evidence, and unverified boundaries.
Do not treat development-machine authority as device or production authority, and do not request
production credentials.
```

The agent turns this request into a private, run-local task record. Before execution, it should restate
the task and expected deliverables in one or two sentences. You confirm that natural-language restatement,
not a JSON file. It should ask only when missing information changes the deliverable, authority, or an
irreversible action and cannot be determined from the environment or repository.

Run records live under the Git-ignored `output/agent-runs/<run-id>/`. That directory must still never
contain passwords, tokens, private keys, or credential-bearing URLs. Use isolated checkouts or workspaces
for different customers on a shared development machine.

## What the Environment Check Does

The agent first inventories the host, repository, authority, and installed tools without installing
software, starting system services, changing groups, or elevating privileges. After choosing an
implementation path, it checks only the architecture, resources, runtimes, and toolchain required by this
task:

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
frozen `tpu_mlir` package and callable compiler commands are also present.

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

## Reference Path: Convert an ONNX Model

For “convert this ONNX detector to an F16 `.bmodel` for BM1688 and return evidence,” the agent should:

1. Resolve the source, backend/chip, inputs and outputs, preprocessing, precision, and expected deliverable
   from the request and model.
2. Inventory the machine, then select the model tutorial, template, and closest example, recording
   differences.
3. Place the source in the private current run and generate the task record; the user does not write a
   Schema.
4. Run `scripts/agent/doctor.sh`. Continue only when the environment passes. The script never pulls an
   image or installs a dependency.
5. Run `scripts/agent/convert_model.sh`. It calls `tools/check_onnx_model.py`, then invokes
   `model_transform` and `model_deploy` through the admitted TPU-MLIR package, Python, and command identity.
   Docker participates only when the task selects a complete-toolchain image. Generated files do not
   overwrite existing model resources.
6. Run `scripts/agent/verify.sh` to recheck source and artifact hashes and produce machine-readable
   `evidence.json` plus the readable `evidence.md`.

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
| Tensor comparison | The toolchain compared pre- and post-conversion outputs at the task tolerance | Business-dataset accuracy |
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
