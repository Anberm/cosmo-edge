# RK3576 P7.3 72-hour soak

This board-bound soak keeps four 5 FPS inference tasks and four algorithm
preview publishers active for 72 hours. It samples task throughput, CPU,
memory, RKNN/MPP/RGA counters and the host frame memory pool once per minute.

Use `--preview algorithm --preview-streams all --preview-clients 1` and run the
CLI on an always-on host. `--password-stdin` keeps the account password out of
the process arguments and the client discards it after login.
