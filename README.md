# EDK2 UEFI firmware for D-Robotics RDK X5 (Sunrise X5)

An open-source EDK2 (UEFI) port for the D-Robotics / Horizon **Sunrise X5** SoC
(8x ARM Cortex-A55), targeting the **RDK X5** board. It runs as the **BL33**
payload of the vendor boot chain, replacing U-Boot, and aims to provide a
standard UEFI environment (Shell, block I/O, boot manager) for the board.

> Status: early bring-up. The firmware builds for AArch64 and brings up the
> core platform (CPU, MMU, GIC, timer, UART, PSCI, UEFI Shell). Storage and
> OS boot are still in progress — see [Status](#status).

## Boot chain

The X5 boot flow is fixed by the vendor (precompiled) TF-A/OP-TEE miniboot:

```
BootROM -> BL2 (0x1FEA0000) -> BL31 (0x20000000) + OP-TEE (0x80000000)
        -> BL33 = edk2 FD, loaded at 0x88000000 by BL2, entered at offset 0
        -> UEFI (this project)
```

- The EDK2 FD is loaded as the FIP `nt-fw` into the NAND `uboot` partition
  (offset `0x500000`, 2 MB).
- The FD has a `b 0x1000` stub at offset 0 and the firmware volume at 0x1000,
  matching the BL31 entry convention.
- Only BL33 is replaced; BL2/BL31/OP-TEE are untouched.

## Status

| Area | State | Notes |
| --- | --- | --- |
| AArch64 build (DEBUG/RELEASE) | Working | edk2-stable202511, GCC5, 1.5 MB FD |
| PrePi / PEI / DXE / BDS | Working | Based on ArmVirtPkg + ArmPlatformPkg |
| Memory map (no FDT) | Working | Static X5 map in `X5VirtMemInfoLib` |
| Early identity map | Working | Executable-DRAM idmap in `X5ArmPlatformLib` |
| Debug UART (16550) | Working | `0x32120000`, 115200, 8N1 |
| GICv3 | Working | GICD `0x30100000`, GICR `0x30140000` |
| Arch timer / PSCI | Working | 24 MHz, PSCI via SMC |
| UEFI variables | Working (emulated NV) | Not persistent yet |
| SD card block I/O | Bring-up, unvalidated | SD0 `0x35020000`, PIO, 3.3 V default speed |
| eMMC | Not yet | `0x35040000` |
| Display / USB / PCIe / Ethernet | Not yet | — |
| ACPI / DTB handoff to Linux | Not yet | Linux needs `CONFIG_EFI` + payload |
| Persistent var store, A/B, AVB | Not yet | — |

## Repository layout

```
Platform/DRobotics/X5/
  X5Pkg.dec / X5.dsc / X5.fdf / X5FvMain.fdf.inc
  Library/X5VirtMemInfoLib/    static X5 memory map (ArmVirtMemInfoLib)
  Library/X5ArmPlatformLib/    X5 early identity map + platform helpers
  Drivers/X5SdhciDxe/          SDHCI/SD block I/O driver (SD0)
  Scripts/build_x5_uefi.sh     build
  Scripts/pack_bl33_fip.sh     wrap the EDK2 FD as the BL33 FIP
  Scripts/make_flashable.sh    produce flashable uboot/nand images
```

The project is an **out-of-tree EDK2 package** (edk2-platforms style): it is
built against an unmodified `tianocore/edk2` checkout via `PACKAGES_PATH`.

## Requirements

- Linux host (x86_64), Python 3.8+
- AArch64 GCC toolchain, e.g. the vendor
  `gcc-arm-11.2-2022.02-x86_64-aarch64-none-linux-gnu`
- `git`, `make`

## Build

```bash
# Use an existing edk2 checkout (recommended)
X5_EDK2_DIR=/path/to/edk2 ./build.sh DEBUG
X5_EDK2_DIR=/path/to/edk2 ./build.sh RELEASE

# Or let the script clone edk2-stable202511 into ./edk2
./build.sh DEBUG
```

Output:

```
$X5_EDK2_DIR/Build/X5-AArch64/<TARGET>_GCC5/FV/X5_EFI.fd
```

Select a different toolchain with `X5_TOOLCHAIN_PREFIX=/path/to/aarch64-none-linux-gnu-`.

## Packaging and flashing

`Scripts/pack_bl33_fip.sh` wraps the FD into the BL33 FIP that BL2 loads
(`nt-fw` + certificate chain + `hb-bl2-cfg`), reusing the vendor `fiptool`,
`cert_create` and board keys:

```bash
X5_EDK2_DIR=/path/to/edk2 ./Platform/DRobotics/X5/Scripts/pack_bl33_fip.sh
```

`Scripts/make_flashable.sh` produces the flashable artifacts:

- `uboot.img` — drop-in replacement for the vendor `out/product/uboot.img`
- `uboot_partition.img` — FIP padded to the 2 MB partition (0xFF fill)
- `nand_disk.img` — full NAND image (if `MINIBOOT_ALL` / `UBOOTENV_IMG` given)

Only the 2 MB `uboot` partition at NAND offset `0x500000` needs to be written to
persistently replace U-Boot with this firmware. The miniboot partition is
unchanged. For a zero-risk first test, RAM-boot the firmware via the vendor
UART/USB download flow using a `bl3x_all.bin` repacked with the EDK2 FD as
`nt-fw` (volatile; lost on power cycle).

## Hardware notes (RDK X5)

| Item | Value |
| --- | --- |
| SoC | Horizon Sunrise X5, 8x Cortex-A55 |
| Non-secure DRAM | `0x84000000` + 1984 MB (secure 64 MB below) |
| EDK2 FD load address | `0x88000000` (BL33) |
| Debug UART | `0x32120000`, 16550-compatible, stride 4, 202.752 MHz, 115200 |
| GIC | GICv3, GICD `0x30100000`, GICR `0x30140000` |
| Timer | ARM generic timer, 24 MHz, NS-EL1 PPI 30 |
| SD0 | `0x35020000` (DWCMSHC/SDHCI), power switch on GPIO0.26 |
| eMMC | `0x35040000` |
| PSCI / OP-TEE | SMC conduit |

## Known limitations

- The SD driver is first-pass bring-up code (PIO, single block, 25 MHz,
  3.3 V) and has **not** been validated on hardware. GPIO0 clock gating and
  the SD slot power switch may need adjustment.
- BL31 does not hand over a device tree; the platform map is static. Passing a
  DTB/ACPI to Linux and enabling `CONFIG_EFI` in the target kernel is future
  work.
- The vendor `bl2_cfg.json` has secure boot disabled by default; the BL33 image
  is still signed so the existing certificate chain is preserved.
- Firmware size is limited by the 2 MB `uboot` partition.

## Credits

- Built on TianoCore EDK2, `ArmVirtPkg`, `ArmPlatformPkg` and `UefiCpuPkg`.
- Boot-chain integration and board facts derived from the D-Robotics RDK X5 BSP
  (U-Boot `x5_sdhci.c`, Linux `x5.dtsi`, `x5-bootloader`).
- Inspired by [edk2-rk3588](https://github.com/edk2-porting/edk2-rk3588).

## License

BSD-2-Clause-Patent. See [LICENSE](LICENSE).
