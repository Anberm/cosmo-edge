# CosmoEdge 1.1 benchmark repository checklist

Status: **prepared for repository review; not yet published**

- [x] Source commit and tree frozen.
- [x] ScenarioBench source hashes frozen, including detector/classifier FPS override.
- [x] Controlled video metadata and SHA-256 frozen.
- [x] BM1688, CV186X, RK3576, and RV1126B model hashes recorded.
- [x] 49 small-model cases preserved once in four schema-bound canonical files.
- [x] Every canonical case retains the original frozen summary SHA-256.
- [x] Platform summaries, case pages, workload matrices, and indexes generated deterministically at build time.
- [x] Existing three-platform VLM observations consolidated without enabling an FPS gate or capacity claim.
- [x] Canonical JSON semantics, generated HTML links/languages, public-data scrub, and source/generated checksums validated.
- [x] Full pre-simplification evidence archive frozen separately by commit, tree, and SHA-256.

This checklist qualifies the benchmark publication candidate only. It does not claim product-release qualification, soak stability, RTSP resilience, accuracy acceptance, or customer-journey acceptance.

Do not copy `private-evidence/` or the full evidence archive into the repository. Publishing the separately recorded archive as a release asset requires an explicit release action and final provenance review.
