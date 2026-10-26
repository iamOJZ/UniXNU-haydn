# AGENTS.md - UniXNU-haydn

Board-level repository for the Xiaomi Redmi K40 Pro (haydn, SM8350).

## Responsibilities

- `board.toml` - device identity, component selection, firmware/OS wiring, payload and userdata layout.
- `dts/haydn.dts` - board memory, reservations and peripheral enablement; includes `soc:dts/sm8350.dtsi`.
- `configs/haydn_defconfig` - kernel switches for this board.
- `package/` - board kits (empty during bring-up).
- `scripts/check-haydn.py` - firmware preflight, cross-checks the memory map against the compiled DTB.
- `porting.toml` - authoritative porting record; update the affected area in the same commit as the code.

## Read first

- https://unixnu.com/zh/porting/steps
- https://unixnu.com/zh/porting/device-repository
- https://unixnu.com/zh/porting/device-tree
- https://unixnu.com/zh/porting/uefi-firmware
- https://unixnu.com/zh/porting/bring-up

## Rules

- Hardware values must come from this device (firmware FDT, GPT, vendor sources), never from another board.
- Do not flash; the supported path is `fastboot boot`.
- Shared controller code belongs in `UniXNU-sm8350-common`, board wiring belongs here.
- Every code path change needs an updated `porting.toml` area with evidence.
