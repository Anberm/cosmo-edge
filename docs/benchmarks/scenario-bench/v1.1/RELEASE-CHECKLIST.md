# CosmoEdge 1.1 benchmark release checklist

Status: **LOCAL UNPUBLISHED CANDIDATE — DO NOT PUBLISH**

## Completed in this candidate

- [x] Source commit and tree frozen in `release-manifest.json`.
- [x] Known environment facts captured; unknown runtime fields remain explicit.
- [x] Public environment material applies the commercial disclosure policy; non-public environment fields remain only in the private archive.
- [x] Available model and dataset identities and SHA-256 values recorded.
- [x] Sanitized single-detector, dual-detector, and VLM attachments generated.
- [x] VLM target fixed at 0.1 FPS per channel and RK3576 shared-counter correction documented.
- [x] Bilingual HTML includes scope, method, reproduction, comparability, and limitations.
- [x] Capacity heatmap, throughput/latency curves, and resource-peak chart regenerated.
- [x] Public package and generator checked for device addresses, credentials, local paths, serial numbers, and internal IDs.
- [x] `SHA256SUMS` regenerated after all output files.

## Required before publication

- [ ] Supply the final Open package and record its SHA-256.
- [ ] Supply the final Protected package and record its SHA-256.
- [ ] Prove both packages were produced from the frozen source commit/tree.
- [ ] Rebind the evidence to the final `feat/model-guard-v2.3` release candidate after its RK3576 VLM changes.
- [ ] Record the final RK3576 VLM artifact SHA-256.
- [ ] Approve redistribution/provenance for every model and the sample video.
- [ ] Complete repeat, soak, customer-journey, and accuracy qualification for any recommended-profile claim.
- [ ] Change `qualification.readyToPublish` only after all items above are complete.

Do not copy `private-evidence/` into a release artifact.
