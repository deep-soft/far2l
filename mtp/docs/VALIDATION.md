# MTP/PTP Validation Plan

This document defines the minimum acceptance run for the libusb MTP/PTP plugin backend.

## Build

- `cmake --build /Users/C5370280/SAPDevelop/far2l-adb --target mtp -j4`

## Runtime logging

- Release/NDEBUG logs are written to `/tmp/mtp_plugin.log`.
- Clear log before each run:
  - `rm -f /tmp/mtp_plugin.log`

## Device matrix

- Android MTP device (Samsung)
- Android MTP device (Sony Xperia or Lenovo)
- Android MTP device with SD card storage
- PTP-only camera or iPhone USB/PTP

## Functional checks

1. Device routing
- Confirm MTP-capable Android routes as `MTP`.
- Confirm PTP-only device routes as `PTP`.
- Confirm no dual-claim conflict (`Busy` loop must not appear on idle).

2. Browse
- Open plugin panel.
- Enter device.
- Enter each storage.
- Enter nested folders and go back (`..`).
- Verify repeated folder open hits cache (see timing lines in log).

3. Transfers
- Download single file.
- Download directory recursively.
- Upload file (MTP path).
- Create directory (MTP path).
- Delete uploaded object(s) recursively (MTP path).

4. Fault behavior
- Unplug device during listing.
- Unplug device during download.
- Reconnect and re-enter device.
- Verify no freeze and clear error mapping (`Disconnected`, `Timeout`, `Busy`).

## Large directory checks (>=10k entries)

- Open large folder twice.
- First open should complete with tiered listing.
- Second open should be cache-fast.
- Confirm no repeated full per-item metadata storm in logs.

## Metrics extraction

- Use:
  - `/Users/C5370280/SAPDevelop/far2l-adb/mtp/tools/mtp_log_summary.sh /tmp/mtp_plugin.log`
- Check:
  - warm folder open target: <150ms typical
  - cold medium folder open target: <1200ms typical
  - tier usage includes tier 1 or tier 2 on modern Android

## Pass criteria

- Plugin opens devices/storages reliably.
- PTP-only devices are browsable/downloadable through PTP backend.
- No backend dual-claim collisions.
- Disconnects do not leave stale frozen panel state.
- Log summary shows stable list timings and tier ladder activity.
