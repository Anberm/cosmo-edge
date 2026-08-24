# CosmoEdge 1.1 benchmark repository checklist

Status: **prepared for repository review; not yet published**

- [x] Source commit and tree frozen.
- [x] ScenarioBench source hashes frozen, including detector/classifier FPS override.
- [x] Controlled video metadata and SHA-256 frozen.
- [x] BM1688, CV186X, RK3576, and RV1126B model hashes recorded.
- [x] 49 small-model cases preserved once in four schema-bound canonical files.
- [x] Every canonical case retains the original frozen summary SHA-256.
- [x] Platform summaries, case pages, workload matrices, and indexes generated deterministically at build time.
- [x] 72-hour dual-CV observations preserved once in a sanitized canonical file with private source-summary, report, suite, bundle, and sidecar hashes.
- [x] BM1688, CV186X, and RK3576 completed the configured 8-channel profile; RV1126B completed the configured 4-channel profile at the 24h, 48h, and 72h checkpoints.
- [x] One-minute sampling coverage, boundary/gap integrity, throughput, discard, binding/telemetry completeness, incidents, timed-restart policy, and cleanup limitations retained in canonical evidence and generated bilingual reports.
- [x] Long-run wording is limited to the configured controlled local-loop profile and does not claim a capacity maximum, RTSP resilience, production recommendation, or product-release qualification.
- [x] 2026-08-20 three-platform VLM observations preserved without retroactively enabling an FPS gate or capacity claim.
- [x] Conservative VLM publication evaluation deterministically resolves BM1688 6, CV186X 6, and RK3576 4 from the contiguous 80%-FPS, complete-window prefix.
- [x] Per-run readiness protocol and stop semantics retained in canonical evidence and methodology.
- [x] VLM task-local completion-counter and per-route readiness tool changes covered by tests.
- [x] ScenarioBench suppresses capacity wording whenever a VLM throughput gate is disabled.
- [x] Canonical JSON semantics, generated HTML links/languages, public-data scrub, and source/generated checksums validated.
- [x] The 49-case small-model pre-simplification archive is frozen separately by commit, tree, and SHA-256.
- [x] Refreshed VLM projections retain their projected-artifact and private original-run source hashes.
- [ ] CV186X and RK3576 VLM re-measurement with the unified readiness probe — separate follow-up outside the completed dual-CV observation.

This checklist qualifies the benchmark publication candidate and the configured controlled 72-hour profile only. It does not claim broader soak stability, product-release qualification, RTSP resilience, accuracy acceptance, or customer-journey acceptance.

Do not copy `private-evidence/` or the small-model full evidence archive into the repository. Publishing the separately recorded archive as a release asset requires an explicit release action and final provenance review.
