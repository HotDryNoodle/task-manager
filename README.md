# task-manager

ZeroMQ-backed task orchestration for satellite CLI plugins.

## Environment

- `SATELLITE_PLUGIN_INDEX` — plugins.index.json (preferred)
- `SATELLITE_PLUGIN_DIR` — colon-separated manifest directories
- `SATELLITE_PLUGIN_BIN` — plugin executable search root
- `SATELLITE_TASK_WORK_ROOT` — task work directories

Legacy: `MISSION_PLANER_ROOT`, `MISSION_PLANER_BIN` (deprecated).

## Build

```bash
meson subprojects download
meson setup build -Dzmq=enabled
meson compile -C build
```
