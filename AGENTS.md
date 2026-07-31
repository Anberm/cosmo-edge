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

1. Start from the user's business outcome, available materials, target context,
   and acceptance. Restate the intended deliverables in ordinary language.
   Do not ask the user to choose a chip flag, precision, compiler version,
   container, script, or repository example when those facts can be inspected
   or recommended from the material, target device, repository, and current
   official documentation.
2. Create the private task contract under `output/agent-runs/<run-id>/`. The
   contract is an agent-owned execution record, not a form for the customer.
   Run `./scripts/agent/assess.sh --contract ...` to inventory materials,
   compare credible routes, and emit only unresolved business, environment, or
   authority questions. Translate `needsInput` into one concise user question;
   do not expose internal JSON for confirmation.
3. Run `./scripts/agent/doctor.sh --baseline` for a read-only inventory before
   changing the development environment. Run task admission on the machine
   that will actually execute the work, not merely on the orchestration client.
4. Select relevant tutorials, examples, templates, tests, and code facts. When
   reusing an example or template, record why it applies and how the task
   differs. A direct code or documentation task may record these sources in its
   evidence instead of creating a separate selection file.
5. Treat an upstream-supported route as eligible to try, not as proof that the
   current machine is ready. Admit by required capabilities and callable tools;
   after admission, freeze the actual Python/package/image/tool identities and
   commands used by this run. Do not require one globally fixed recipe.
6. Execute only inside the granted scope. Use four coarse gates when applicable:
   `environment-change`, `remote-execution`, `model-transfer`, and
   `device-deployment`. Keep the exact target, read/write scope, impact, and
   recovery plan in the task context without turning every implementation
   detail into a blocking standard.
7. Validate the deliverables requested by this task. Repository examples are
   reusable evidence, not universal target shapes, hashes, chips, or outputs.

## Minimum checks by change type

| Change | Minimum check |
| --- | --- |
| C++ | `bash scripts/build_cpu_test.sh`, then `./build_cpu/cosmo-tests`; before submission run `scripts/format_check.sh` |
| Documentation | `npm run docs:verify` |
| Frontend | `cd src/web && npm ci && npm run build` |
| Sophon model conversion selected by assessment | `./scripts/agent/assess.sh --contract ...`, then on admitted Linux `./scripts/agent/doctor.sh --task model-conversion --contract ...`, `./scripts/agent/convert_model.sh --contract ...`, and `./scripts/agent/verify.sh --contract ...` |
| Other task | Use its task contract and the closest native test commands; list anything not verified |

## Engineering boundaries

- The inference backend is selected at compile time. x86 uses ONNX Runtime CPU
  and loads `model.onnx` directly. Sophon uses BMRT; a `.bmodel` is packaged
  inside `model.nn` (CENN) and cannot be turned into one by renaming.
- BM1688 and CV186X artifacts are not interchangeable. A chip name, compiler
  argument, runtime identifier, and artifact mapping must come from current
  repository facts or independent measured evidence.
- ONNX-to-bmodel conversion depends on an external, locally admitted TPU-MLIR
  package. Follow a route supported by current upstream instructions, but do not
  confuse route eligibility with local readiness. An optional `sophgo/tpuc_dev`
  image is only a base environment and is not the compiler. Accept compatible
  official layouts, then freeze the complete resolved package and command
  identity for the run.
  BM1688/F16 is the first preferred measured path. An entry appears in the
  example index only after two real recordings pass its promotion rules; an
  empty index is not a success claim and does not make unrelated development
  unsupported.
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
- Windows can host the agent, repository inspection, material preparation, and
  route assessment. The Sophon TPU-MLIR conversion route executes in an
  isolated Linux x86_64 environment. Guide the user to local/remote Linux or an
  explicitly experimental compatibility layer; do not label CosmoEdge itself
  unsupported merely because this compiler ecosystem is Linux-oriented.
- Preserve prior failed attempts and data-flow status. A later unverified run
  must not erase an earlier failure; a measured pass may supersede it, or the
  evidence must record an explicit user-confirmed waiver.
- A missing named environment is not automatically a customer-machine defect.
  Recheck the repository instruction, its upstream source, and whether it names
  a base environment or a complete tool before requesting an installation. If
  the instruction is wrong, fix it and rerun the same admission and execution
  checks first.
- Normal task evidence only needs the environment summary, deliverables,
  redacted commands, observed results, and unverified boundaries needed for the
  user's acceptance. Official examples and release claims additionally require
  a frozen commit/tree, input and toolchain identities, artifact hashes,
  applicability, and repeat recordings.
