# ScenarioBench Current Benchmark Refresh

This directory points to the newest reviewed benchmark refresh generated after the v1.0 release documentation freeze. Canonical measurements live in the versioned v1.1 pack; raw `metrics.json` traces stay out of the repository.

## Summary

| Platform | Target FPS/ch | Gate result in this run | English | Chinese |
| --- | ---: | --- | --- | --- |
| BM1688 | 0.1 | 8 channels passed | <a href="../v1.1/results/bm1688/vlm-observation/report.html">open</a> | <a href="../v1.1/results/bm1688/vlm-observation/report.zh-CN.html">zh-CN</a> |
| CV186X | 0.1 | 6 channels passed | <a href="../v1.1/results/cv186x/vlm-observation/report.html">open</a> | <a href="../v1.1/results/cv186x/vlm-observation/report.zh-CN.html">zh-CN</a> |
| RK3576 | 0.1 | 6 channels passed | <a href="../v1.1/results/rk3576/vlm-observation/report.html">open</a> | <a href="../v1.1/results/rk3576/vlm-observation/report.zh-CN.html">zh-CN</a> |

## Notes

- The 2026-08-20 refresh uses one controlled 1080p24 local-loop input, 60-second steps, and task-local completion counters.
- VLM FPS is recorded but not gated. These are short-run runtime observations, not supported-channel or long-running qualification claims.
- Results are recorded against the gates enabled for this run; see the v1.1 methodology and canonical data for the complete evaluation contract.
- The [previous single-device report](vlm-77175-npu/report.html) remains available for old links and is superseded by the versioned three-platform refresh above.
