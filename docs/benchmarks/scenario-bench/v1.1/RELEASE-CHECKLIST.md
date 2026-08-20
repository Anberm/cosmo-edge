# CosmoEdge 1.1 benchmark repository checklist

Status: **prepared for repository review; not yet published**

- [x] Source commit and tree frozen.
- [x] ScenarioBench source hashes frozen, including detector/classifier FPS override.
- [x] Controlled video metadata and SHA-256 frozen.
- [x] BM1688, CV186X, RK3576, and RV1126B model hashes recorded.
- [x] 49 small-model cases preserved once in four schema-bound canonical files.
- [x] Every canonical case retains the original frozen summary SHA-256.
- [x] Platform summaries, case pages, workload matrices, and indexes generated deterministically at build time.
- [x] 2026-08-20 three-platform VLM observations preserved without retroactively enabling an FPS gate or capacity claim.
- [x] Conservative VLM publication evaluation deterministically resolves BM1688 6, CV186X 6, and RK3576 4 from the contiguous 80%-FPS, complete-window prefix.
- [x] Per-run readiness protocol and stop semantics retained in canonical evidence and methodology.
- [x] VLM task-local completion-counter and per-route readiness tool changes covered by tests.
- [x] ScenarioBench suppresses capacity wording whenever a VLM throughput gate is disabled.
- [x] Canonical JSON semantics, generated HTML links/languages, public-data scrub, and source/generated checksums validated.
- [x] The 49-case small-model pre-simplification archive is frozen separately by commit, tree, and SHA-256.
- [x] Refreshed VLM projections retain their projected-artifact and private original-run source hashes.

This checklist qualifies the benchmark publication candidate only. It does not claim product-release qualification, soak stability, RTSP resilience, accuracy acceptance, or customer-journey acceptance.

Do not copy `private-evidence/` or the small-model full evidence archive into the repository. Publishing the separately recorded archive as a release asset requires an explicit release action and final provenance review.
