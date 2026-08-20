# Public scenario descriptors

- `single-workload/person-detector.public.yml`: person-detection FPS gradient.
- `single-workload/no-safety-helmet-analysis.public.yml`: two-stage detector-plus-classifier FPS gradient.
- `concurrent-mixed/scenario.public.yml`: person detection and two-stage no-safety-helmet analysis running concurrently at 5 FPS per business task.
- `vlm-observation/scenario.public.yml`: existing three-platform VLM observation.

Resolve `<platform>` and `<platform-maximum>` locally before execution. Device addresses, credentials, and device-local identifiers stay outside the public descriptor.
