# RK3576 P7.3 72-hour soak

This board-bound soak keeps four 5 FPS inference tasks and four algorithm
preview publishers active for 72 hours. It samples task throughput, CPU,
memory, RKNN/MPP/RGA counters and the host frame memory pool once per minute.

The validation clip repeatedly contains positive targets. The scenario keeps
the complete CV, OSD, MPP encode and RTMP publish paths active, but extends the
alarm decision window and alarm interval so repeated fixture loops do not fill
the device disk with event images. Business-event correctness is validated in
the separate customer journey; this soak is a bounded-resource stability gate.
The runner also stops the workload if device disk usage reaches 90%.

Use `--preview algorithm --preview-streams all --preview-clients 1` and run the
CLI on an always-on host. `--password-stdin` keeps the account password out of
the process arguments and the client discards it after login.
