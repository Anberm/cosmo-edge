## Summary

Describe the change and why it is needed.

## Related issue

Replace this line with a closing reference when the PR completes the issue, for example `Closes #<issue-number>`.

## Root cause

Describe the concrete failure path or reason for the change.

## Scope

### In scope

-

### Out of scope

-

## Risk tags

- [ ] Runtime / lifecycle
- [ ] Thread / memory safety
- [ ] API / authentication / network
- [ ] Media / streaming
- [ ] Model / inference / flow
- [ ] Frontend console
- [ ] Build / package / deployment
- [ ] Compatibility / migration
- [ ] None of the above

## Type of change

- [ ] Bug fix
- [ ] Feature
- [ ] Documentation
- [ ] Build / deployment
- [ ] Refactor
- [ ] Test

## Area

- [ ] Backend service
- [ ] Frontend web console
- [ ] Pipeline / scenario configuration
- [ ] Model import / model runtime
- [ ] API / MQTT / WebSocket
- [ ] Media / streaming
- [ ] Build / deployment
- [ ] Documentation

## Candidate identity

- Base commit: `<sha>`
- Candidate commit: `<sha>`
- Candidate tree: `<tree-sha>`
- Package SHA-256: `<sha256 or N/A>`
- Test binary SHA-256: `<sha256 or N/A>`

State whether the candidate was amended, rebased, merged, or otherwise changed after the evidence below was collected.

## Verification

### Parent baseline

Record `PASS`, `FAIL`, or `BLOCKED`, plus the exact command and environment used to establish regression attribution.

```text

```

### Candidate checks

List exact commands, test filters, case/assertion counts, and results.

```text

```

### Risk-based evidence

- API/network:
- Media/streaming:
- Sophon/device:
- Frontend/UI:
- Package/deployment:

## Documentation impact

- [ ] Documentation was updated.
- [ ] Documentation is not needed for this change.
- [ ] Documentation will be handled in a follow-up.

## Compatibility and deployment impact

- [ ] This change is backward compatible.
- [ ] This change may affect public APIs, configuration files, pipelines, deployment scripts, or model artifacts.
- [ ] Not applicable.

## Third-party code and assets

- [ ] This PR does not add third-party code, models, datasets, media, or generated assets.
- [ ] This PR adds third-party materials, and their source and license are documented.
- [ ] This PR does not include GPL, AGPL, or other strong copyleft code.

## Security and release checklist

- [ ] No secrets, tokens, private keys, or certificates are included.
- [ ] No real device SN values, customer names, or private IPs are included.
- [ ] No private model weights or proprietary download links are included.
- [ ] New dependencies have an acceptable license and are documented.
- [ ] Documentation was updated if behavior changed.
- [ ] My commits are signed off with `Signed-off-by:` according to the DCO-style requirement in `CONTRIBUTING.md`.
- [ ] I have read `CONTRIBUTING.md` and `CODE_OF_CONDUCT.md`.

## Acceptance and cleanup

- Candidate-bound acceptance: `<验证通过 candidate-commit package-sha256, or N/A>`
- Device test filter: `<filter or N/A>`
- Device backup state: `<retained/restored/N/A>`
- Temporary-data cleanup: `<complete/N/A>`
- Final Git status: `<clean or explanation>`
- [ ] Evidence was collected from the candidate commit listed above.
- [ ] Any source change after validation invalidated and restarted the required checks.
- [ ] No temporary credentials, media, models, device exports, or generated packages are included.

## Notes for reviewers

Mention any known limitations, follow-up work, or compatibility concerns.
