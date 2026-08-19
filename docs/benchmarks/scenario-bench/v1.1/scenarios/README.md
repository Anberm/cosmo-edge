# Public scenario descriptors

- `single-detector/person-detector.public.yml`: person-detection FPS gradient.
- `single-detector/safety-helmet-detector.public.yml`: two-stage no-safety-helmet FPS gradient.
- `dual-detector/scenario.public.yml`: both CV tasks at 5 FPS per channel.
- `vlm-observation/scenario.public.yml`: existing three-platform VLM observation.

Resolve `<platform>` and `<platform-maximum>` locally before execution. Device addresses, credentials, and device-local identifiers stay outside the public descriptor.
