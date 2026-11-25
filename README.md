# EDK2 UEFI firmware for the D-Robotics RDK X5

An open-source [EDK2](https://github.com/tianocore/edk2) (UEFI) port for the
**D-Robotics / Horizon Sunrise X5** SoC (8× ARM Cortex-A55), targeting the
**RDK X5** board.

The firmware runs as the **BL33** payload of the vendor boot chain — the same
slot normally occupied by U-Boot — and provides a standard UEFI environment
(Shell, block I/O, boot manager) instead of a U-Boot console.

```
BootROM ─► BL2 (0x1FEA0000) ─► BL31 (0x20000000) + OP-TEE (0x80000000)
        ─► BL33 = EDK2 FD @ 0x88000000 ─► UEFI ─► (payload / OS)
```

> **Project status: early bring-up.** The firmware builds for AArch64 and brings
> up the core platform (CPU, MMU, GIC, timer, UART, PSCI, UEFI Shell). Storage
> and OS boot are still work in progress — see [Status](#status).

---

## Why a BL33 replacement

The X5 boot chain is fixed by the vendor's precompiled TF-A/OP-TEE *miniboot*:
BL2 loads a FIP `nt-fw` image from the NAND `uboot` partition, and BL31 jumps to
it. By making EDK2 that `nt-fw` image, we get a full UEFI firmware without
touching BL2/BL31/OP-TEE, and keep the existing signing/certificate chain.

- The EDK2 FD is loaded at **`0x88000000`** by BL2.
- Offset 0 of the FD is a `b 0x1000` stub; the firmware volume lives at `0x1000`,
  matching the BL31 entry convention.
- Only BL33 is replaced. BL2/BL31/OP-TEE and the miniboot partition are
  unchanged.

## Status

| Area | State | Notes |
| --- | --- | --- |
| AArch64 build (DEBUG/RELEASE) | ✅ Working | edk2-stable202511, GCC5, 1.5 MB FD |
| PrePi / PEI / DXE / BDS | ✅ Working | Based on `ArmVirtPkg` + `ArmPlatformPkg` |
| Static memory map (no FDT) | ✅ Working | `X5VirtMemInfoLib` |
| Early identity map | ✅ Working | Executable-DRAM idmap in `X5ArmPlatformLib` |
| Debug UART (16550) | ✅ Working | `0x32120000`, 115200 8N1 |
| GICv3 | ✅ Working | GICD `0x30100000`, GICR `0x30140000` |
| Arch timer / PSCI | ✅ Working | 24 MHz, PSCI via SMC |
| UEFI variables | 🟡 Emulated NV | Works in RAM, not persistent |
| SD card block I/O | 🟡 Bring-up | SD0 `0x35020000`, PIO, 3.3 V; not HW-validated |
| eMMC | 🔴 Not yet | `0x35040000` |
| Display / USB / PCIe / Ethernet | 🔴 Not yet | — |
| ACPI / DTB hand-off to Linux | 🔴 Not yet | Linux needs `CONFIG_EFI` + payload |
| Persistent var store, A/B, AVB | 🔴 Not yet | — |

✅ working · 🟡 partial / bring-up · 🔴 not yet

## Quick start

```bash
# Clone with the edk2 submodule
git clone --recursive https://github.com/wentywenty/edk-rdkx5.git
cd edk-rdkx5

# Build (DEBUG by default)
./build.sh
./build.sh RELEASE
```

`build.sh` will initialize the `edk2` submodule and build BaseTools on first use.
The firmware image is produced at:

```
edk2/Build/X5-AArch64/<TARGET>_GCC5/FV/X5_EFI.fd
```

### Using an existing edk2 checkout

```bash
X5_EDK2_DIR=/path/to/edk2 ./build.sh DEBUG
```

### Toolchain

By default the build uses the vendor toolchain:

```
/opt/gcc-arm-11.2-2022.02-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-
```

Override it with `X5_TOOLCHAIN_PREFIX`:

```bash
X5_TOOLCHAIN_PREFIX=/opt/arm-gnu-toolchain-11.3.rel1-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu- \
  ./build.sh DEBUG
```

## Repository layout

This is an *umbrella* repository, modeled after
[edk2-rk3588](https://github.com/edk2-porting/edk2-rk3588).

```
edk-rdkx5/
├── edk2/                      # git submodule, pinned to edk2-stable202511
├── Platform/DRobotics/X5/     # the X5 platform package (this project)
│   ├── X5Pkg.dec  X5.dsc  X5.fdf  X5FvMain.fdf.inc
│   ├── Library/X5VirtMemInfoLib/   static X5 memory map
│   ├── Library/X5ArmPlatformLib/   early executable-DRAM identity map
│   ├── Drivers/X5SdhciDxe/         SDHCI/SD block I/O (SD0)
│   └── Scripts/
│       ├── build_x5_uefi.sh        build the platform against edk2
│       ├── pack_bl33_fip.sh        wrap the FD as the BL33 FIP
│       └── make_flashable.sh       produce flashable uboot/nand images
├── build.sh
├── LICENSE
└── README.md
```

The platform is built out-of-tree via `PACKAGES_PATH`; no changes to the edk2
submodule are required.

## Requirements

- Linux host (x86_64), `git`, `make`, `python3` (3.8+)
- An AArch64 GCC cross toolchain (see [Toolchain](#toolchain))

## Packaging and flashing

The EDK2 FD must be wrapped as a FIP so BL2 can load it. `pack_bl33_fip.sh`
reuses the vendor `fiptool`, `cert_create`, board keys and `bl2_cfg` from an
RDK X5 BSP tree (set `RDK_BOOTLOADER` / `X5_BOARD_CONF` to point at it):

```bash
./Platform/DRobotics/X5/Scripts/pack_bl33_fip.sh
```

Then create flashable artifacts:

```bash
./Platform/DRobotics/X5/Scripts/make_flashable.sh
```

| Artifact | Size | Purpose |
| --- | --- | --- |
| `uboot.img` | ~1.5 MB | drop-in replacement for the vendor `out/product/uboot.img` |
| `uboot_partition.img` | 2 MB | FIP padded to the NAND `uboot` partition (0xFF) |
| `nand_disk.img` | 8.5 MB | full NAND image (when `MINIBOOT_ALL`/`UBOOTENV_IMG` are supplied) |

**Flashing**

- **Full image**: burn `nand_disk.img` to the SPI NAND with the vendor flashing
  tool (MaskROM + UART/USB download), or
- **Firmware only**: write just the `uboot` partition (NAND offset `0x500000`,
  2 MB) with `uboot.img`. The miniboot partition does not change.
- **Zero-risk test**: RAM-boot via the vendor UART/USB download flow using a
  `bl3x_all.bin` repacked with the EDK2 FD as `nt-fw` (volatile, lost on power
  cycle).

> ⚠️ Keep a copy of the original `uboot.img` / full image for rollback. The
> firmware is early bring-up code and may not boot on the first attempt.

## Hardware notes (RDK X5)

| Item | Value |
| --- | --- |
| SoC | Horizon Sunrise X5, 8× Cortex-A55 |
| Non-secure DRAM | `0x84000000` + 1984 MB (64 MB secure region below) |
| EDK2 FD load address | `0x88000000` (BL33) |
| Debug UART | `0x32120000`, 16550-compatible, register stride 4, 202.752 MHz, 115200 8N1 |
| GIC | GICv3, GICD `0x30100000`, GICR `0x30140000` |
| Timer | ARM generic timer, 24 MHz, NS-EL1 PPI 30 |
| SD0 | `0x35020000` (DWCMSHC/SDHCI), 3.3 V, slot power on GPIO0.26 |
| eMMC | `0x35040000` |
| PSCI / OP-TEE | SMC conduit |

## Known limitations

- The SD driver is first-pass bring-up code (PIO, single block, 25 MHz, 3.3 V)
  and has **not** been validated on hardware. GPIO0 clock gating and the SD slot
  power switch may need adjustment.
- BL31 does not hand over a device tree, so the memory map is static. Passing a
  DTB or ACPI to Linux, and enabling `CONFIG_EFI` in the target kernel, is
  future work.
- Secure boot is disabled by default in the vendor `bl2_cfg.json`; the BL33
  image is nevertheless signed so the certificate chain stays intact.
- Firmware size is constrained by the 2 MB `uboot` partition.

## Contributing

Issues and pull requests are welcome. Please keep the platform out-of-tree
(no edk2 submodule modifications) and add `SPDX-License-Identifier:
BSD-2-Clause-Patent` headers to new files.

## Credits

- Built on TianoCore EDK2, `ArmVirtPkg`, `ArmPlatformPkg` and `UefiCpuPkg`.
- Boot-chain integration and board facts derived from the D-Robotics RDK X5 BSP
  (U-Boot `drivers/mmc/x5_sdhci.c`, Linux `arch/arm64/boot/dts/hobot/x5*.dtsi`,
  and the `x5-bootloader` packaging scripts).
- Project layout inspired by [edk2-rk3588](https://github.com/edk2-porting/edk2-rk3588).

## License

BSD-2-Clause-Patent. See [LICENSE](LICENSE).
