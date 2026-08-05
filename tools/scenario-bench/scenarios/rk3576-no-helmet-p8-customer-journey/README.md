# RK3576 P8 customer journey

This one-channel, 5 FPS workload provides a bounded window for the P8 customer
journey: web login, video-channel visibility, task binding, real raw and
algorithm HTTP-FLV playback, event visibility, task stop/start recovery and
final cleanup.

Run it with `--profile configured` and execute the dedicated `preview` command
against the prepared channel while the 180-second hold is active. The preview
gate must use a real media client and validate H.264 decode, timestamps, OSD
pixel delta, concurrent consumers, reconnect, invalid requests and lifecycle
cleanup. Event correctness is customer-journey evidence, not a labeled model
accuracy result.
