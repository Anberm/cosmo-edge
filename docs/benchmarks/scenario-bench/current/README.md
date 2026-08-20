# ScenarioBench Current Benchmark Refresh

This directory points to the newest reviewed benchmark refresh generated after the v1.0 release documentation freeze. Canonical measurements live in the versioned v1.1 pack; raw `metrics.json` traces stay out of the repository.

## Summary

| Platform | Target FPS/ch | Conservative 80% display boundary | English | Chinese |
| --- | ---: | --- | --- | --- |
| BM1688 | 0.1 | 6 channels | <a href="../v1.1/results/bm1688/vlm-observation/report.html">open</a> | <a href="../v1.1/results/bm1688/vlm-observation/report.zh-CN.html">zh-CN</a> |
| CV186X | 0.1 | 6 channels | <a href="../v1.1/results/cv186x/vlm-observation/report.html">open</a> | <a href="../v1.1/results/cv186x/vlm-observation/report.zh-CN.html">zh-CN</a> |
| RK3576 | 0.1 | 4 channels | <a href="../v1.1/results/rk3576/vlm-observation/report.html">open</a> | <a href="../v1.1/results/rk3576/vlm-observation/report.zh-CN.html">zh-CN</a> |

## Notes

- The 2026-08-20 refresh uses one controlled 1080p24 local-loop input, 60-second steps, and task-local completion counters.
- VLM FPS was recorded but not gated in the raw runs. The table applies the v1.1 conservative post-evaluation and is not an exact capacity or long-running claim.
- Startup-sensitive windows do not count as performance failures and do not increase the displayed boundary; see the v1.1 methodology and canonical data for the complete contract.
- The [previous single-device report](vlm-77175-npu/report.html) remains available only as a superseded historical observation.
