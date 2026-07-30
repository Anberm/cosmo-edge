# CosmoEdge Agent Guide

This file gives coding agents stable repository facts. Customer- or task-specific
information belongs only in the ignored `output/agent-runs/` directory. These
rules support agent-assisted work without changing the normal contributor
workflow.

## Repository map

- `src/` — C++ engine, split by subsystem.
- `src/web/` — Vue 3 frontend.
- `docs/` — VitePress site; Chinese pages are at the root and English mirrors
  are under `docs/en/`. Navigation is in `docs/.vitepress/config.mts`.
- `scripts/` — build, run, validation, and packaging scripts.
  `scripts/agent/` contains environment admission, measured evidence, and
  implemented task-specific executors.
- `tools/` — Python and Node validation utilities.
- `test/` — Catch2 tests and the HTTP/MQTT push test service.
- `data/resource/aiboxresource/` and `data/resource/aiboxresource_x86/` —
  Sophon and x86 model resources and templates.
- `3rd/` and `prebuild/` — third-party and prebuilt dependencies; do not edit.

## Working order

1. Read the user's original objective, expected deliverables, and authority.
2. Run `./scripts/agent/doctor.sh --baseline` for a read-only inventory before
   changing the development environment.
3. Select relevant tutorials, examples, templates, tests, and code facts. When
   reusing an example or template, record why it applies and how the task
   differs. A direct code or documentation task may record these sources in its
   evidence instead of creating a separate selection file.
4. Create the task-local contract under `output/agent-runs/<run-id>/`, then run
   the task-specific admission check when one exists. A task without a
   dedicated profile can use the native commands below and must not be rejected
   solely because no profile exists.
5. Execute only inside the granted scope. Environment repair, elevated access,
   devices, production systems, and external writes require separate authority.
6. Validate the deliverables requested by this task. Repository examples are
   reusable evidence, not universal target shapes, hashes, chips, or outputs.

## Minimum checks by change type

| Change | Minimum check |
| --- | --- |
| C++ | `bash scripts/build_cpu_test.sh`, then `./build_cpu/cosmo-tests`; before submission run `scripts/format_check.sh` |
| Documentation | `npm run docs:verify` |
| Frontend | `cd src/web && npm ci && npm run build` |
| Sophon model conversion selected by the task contract | `./scripts/agent/doctor.sh --task model-conversion --contract ...`, then `./scripts/agent/convert_model.sh --contract ...` and `./scripts/agent/verify.sh --contract ...` |
| Other task | Use its task contract and the closest native test commands; list anything not verified |

## Engineering boundaries

- The inference backend is selected at compile time. x86 uses ONNX Runtime CPU
  and loads `model.onnx` directly. Sophon uses BMRT; a `.bmodel` is packaged
  inside `model.nn` (CENN) and cannot be turned into one by renaming.
- BM1688 and CV186X artifacts are not interchangeable. A chip name, compiler
  argument, runtime identifier, and artifact mapping must come from current
  repository facts or independent measured evidence.
- ONNX-to-bmodel conversion depends on an external, locally admitted toolchain
  image; the repository does not contain that compiler. BM1688/F16 is the first
  preferred measured path. An entry appears in the example index only after two
  real recordings pass its promotion rules; an empty index is not a success
  claim and does not make unrelated development unsupported.
- x86 or mock success is not Sophon-device or production acceptance. Report
  conclusions by the layer actually tested.
- Preparing an upstream change to `src/nn/`, `src/infer/`, model templates,
  public APIs, new third-party dependencies, or a broad architecture requires
  the project's normal issue and review process. An authorized customer fork
  may continue locally but must identify the divergence.

## Safety and evidence

- Development-machine authority covers only the workspace, commands, and writes
  explicitly granted by the user. It does not imply software installation,
  administrator access, device access, or production access.
- Do not request, print, persist, or commit credentials, tokens, private
  streams, customer data, device serial numbers, private models, or new model
  binaries.
- Create run directories with private permissions. Redact credential-like
  arguments, environment variables, and URL user information before recording
  commands. Work only in the current run directory; isolate workspaces on
  shared or multi-customer machines.
- Unknown or untested facts remain unverified. Never replace them with guesses
  or promote a local result to a device or production claim.
- Normal task evidence only needs the environment summary, deliverables,
  redacted commands, observed results, and unverified boundaries needed for the
  user's acceptance. Official examples and release claims additionally require
  a frozen commit/tree, input and toolchain identities, artifact hashes,
  applicability, and repeat recordings.
