# UniXNU-haydn

UniXNU device repository for the **Xiaomi Redmi K40 Pro / K40 Pro+ / Mi 11i**
(codename `haydn`, Qualcomm SM8350 / Snapdragon 888).

Status: **bring-up**. The firmware, boot manager and DarwinBoot hand-off are the
current targets; storage, display driver, touch, GPU, Wi-Fi, audio and power are
not adapted yet. See `porting.toml` for the per-area record and evidence.

## Inputs

| Input | Value |
| --- | --- |
| SoC | SM8350 (lahaina), shared support in `UniXNU-sm8350-common` |
| OS profile | `ipados-17.7.11-21H461` (iPad7,1 17.7.11 restore image) |
| UEFI | `uefi-haydn`, Mu-Silicium fork with `Platforms/Xiaomi/haydnPkg` |
| Bootloader | unlocked ABL, `fastboot boot` only - nothing is flashed |
| Logs | ttyMSM0 @ 115200 8N1 (USB serial adapter, host COM3) |

Hardware facts were read from the device itself:

- `/sys/firmware/fdt` (live firmware device tree, 793454 bytes) -> `docs/haydn-fdt.dts`
- on-device GPT (`userdata` = LBA 2785280..61844474, 4096-byte blocks)
- `ro.boot.*` properties, `/proc/cmdline`, `/proc/iomem`, `/proc/cpuinfo`

## Build

```sh
git clone --recurse-submodules https://github.com/UniXNU/UniXNU
git clone --recurse-submodules <this repo> UniXNU-haydn
git clone --recurse-submodules <uefi-haydn> uefi-haydn

cd UniXNU
./unixnu defconfig haydn_defconfig -D ../UniXNU-haydn
./unixnu fetch
./unixnu status --json

# Linux host: install the Mach-O capable toolchain once
sudo scripts/linux/setup-toolchain.py

./unixnu build firmware --uefi ../uefi-haydn
./unixnu build kernel
./unixnu build payload
./unixnu bootimg
```

Then boot without writing anything to the device:

```sh
fastboot devices
fastboot boot out/haydn/ipados-17.7.11-21H461/haydn/boot/boot.img
```

## Board notes

- The GIC/UFS/PHY addresses match the SM8550 layout, but the modem reservation
  `0x8f000000-0x9f000000` removes the Mi 11 DXE heap location, so the haydn heap
  sits at `0xa0000000`.
- The ABL leaves the K40 Pro splash framebuffer at `0xdfc00000` (1008x2400 class
  panel, 1080x2400 visible), which is what SimpleFbDxe hands to UniXNU.
- `Binaries/haydn` currently contains the venus (Mi 11) SM8350 vendor DXE set as
  a first baseline; replace it with haydn's own extracted volumes.

## Links

- UniXNU: https://unixnu.com/zh/
- Porting guide: https://unixnu.com/zh/porting/steps
- Record: `porting.toml` (format 2, schema_version 1)
