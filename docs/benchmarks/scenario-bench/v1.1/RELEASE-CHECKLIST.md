# CosmoEdge 1.1 benchmark release checklist

Status: **PUBLICATION-READY PERFORMANCE REPORT — PREPARED, NOT YET PUBLISHED**

## Completed for the public benchmark

- [x] CosmoEdge 1.1 source baseline commit and tree frozen in `release-manifest.json`.
- [x] BM1688 and CV186X evidence bound to the exact Open package SHA-256 and running engine SHA-256.
- [x] Known environment facts captured; unknown runtime fields remain explicit.
- [x] Public environment material applies the commercial disclosure policy; non-public environment fields remain only in the private archive.
- [x] Available model and dataset identities and SHA-256 values recorded.
- [x] Sanitized single-detector, dual-detector, and VLM attachments generated.
- [x] VLM target fixed at 0.1 FPS per channel and RK3576 shared-counter correction documented.
- [x] Bilingual HTML includes scope, method, reproduction, comparability, and limitations.
- [x] Capacity heatmap, throughput/latency curves, and resource-peak chart regenerated.
- [x] Public package and generator checked for device addresses, credentials, local paths, serial numbers, and internal IDs.
- [x] The two approved Open Sophon models are present in both platform-scoped resource sets; the benchmark directory contains no model bytes or video sample.
- [x] `SHA256SUMS` regenerated after all output files.

## Product-release evidence outside this benchmark

- [ ] Record the final Protected package SHA-256 and private build provenance.
- [ ] Recover the final RK3576 package SHA-256 and source provenance.
- [ ] Record the final RK3576 VLM artifact SHA-256.
- [ ] Complete repeat, soak, customer-journey, and accuracy qualification for any recommended-profile claim.

The unchecked items do not block publication of this scoped performance report because it does not distribute package, non-approved model, or video binaries and does not claim an official recommended profile. They remain required for complete product-package qualification and any stronger capacity commitment.

Do not copy `private-evidence/` into a release artifact.

## Website publication handoff

- [x] Local documentation build contains the bilingual main reports, three platform indexes, and nine standalone workload reports.
- [x] Local smoke test resolves every main-report attachment link to a generated file.
- [ ] Deploy the documentation build containing `benchmarks/scenario-bench/v1.1/`.
- [ ] Confirm the Chinese and English primary report URLs return HTTP 200.
- [ ] Confirm all nine standalone workload report URLs return HTTP 200 and render as HTML rather than GitHub source.

The last three checks remain intentionally open while this PR is prepared but not published.
