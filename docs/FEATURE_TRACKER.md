# USBusKit — Feature Tracker

> **Status:** Living backlog · **Target:** STM32F103C8T6 Bluepill + Linux host  
> **Last reviewed:** 2026-10-18

## Legend

- `[x]` Implemented and tested (unless qualified)
- `[ ]` Not yet completed
- **Decided** — Design direction agreed; implementation may still be pending
- **Planned** — Intended work
- **Proposed** — Under consideration, not committed

## 1. Firmware and USB foundation

- [x] Build bare-metal STM32F103 firmware with CMSIS and CMake.
- [x] Integrate TinyUSB device stack.
- [x] Enumerate a vendor-specific USB interface (`0xCAFE:0x4001`, development IDs).
- [x] Configure bulk OUT `0x01` and bulk IN `0x81` (64-byte max packets).
- [x] Verify bidirectional bulk echo using PyUSB.
- [ ] Implement firmware-side peripheral capabilities.
- [ ] Implement peripheral configuration commands.
- [ ] Add MCU logging ability.

## 2. USBusKit bulk protocol

**Decided:** Use one bulk IN/OUT endpoint pair to multiplex peripheral transactions. Opcodes are interpreted within each protocol ID.

Proposed frame:

```text
+-------------+-------------+--------+----------------+-------------+
| Protocol ID | Instance ID | Opcode | Payload Length | Payload     |
| 1 byte      | 1 byte      | 1 byte | 2 bytes        | variable    |
+-------------+-------------+--------+----------------+-------------+
```

- [ ] Specify protocol and instance identifiers.
- [ ] Define opcodes for requests, responses, and unsolicited events.
- [ ] Specify byte order, maximum payload, and protocol versioning.
- [ ] Implement framing/reassembly across USB transfers and buffering of multiple messages.
- [ ] Define peripheral-specific status and error codes.
- [ ] Decide whether request correlation/transaction IDs are necessary.
- [ ] Define behavior for disconnects, timeouts, and late responses.

## 3. Peripheral discovery and resource management

- [ ] **Proposed:** `GET_CAPABILITIES` reports *firmware-exposed* peripheral instances, not just MCU hardware counts.
- [ ] **Proposed:** Report supported pin mappings and per-instance capabilities.
- [ ] **Proposed:** Query active peripheral and pin allocations.
- [ ] **Proposed:** Select predefined STM32F103 pin remaps where available.
- [ ] **Proposed:** Detect and reject conflicting pin allocations.
- [ ] **Proposed:** Enable/disable peripheral instances.
- [ ] **Proposed:** Report firmware and protocol versions.

## 4. Peripheral implementations

### I²C — first target

- [ ] Initialize an I²C controller instance.
- [ ] Configure speed and supported pin mapping.
- [ ] Support Linux I²C transfers, including combined messages/repeated START where hardware permits.
- [ ] Detect and report NACK, arbitration loss, bus errors, and stuck-bus conditions.
- [ ] Implement bounded transfer waits and appropriate bus recovery.

### SPI

- [ ] Initialize SPI instances and configure clock, mode, and word size.
- [ ] Support transfers and chip-select management.

### UART

- [ ] Configure baud rate, parity, stop bits, and pin mapping.
- [ ] Support asynchronous RX and TX with buffering.

### CAN / LIN

- [ ] Add CAN controller support, bitrate, and filters (hardware permitting).
- [ ] Evaluate LIN support and implementation strategy.

## 5. Linux kernel driver

- [x] Create minimal USB kernel module with registration, probe, and disconnect callbacks.
- [ ] Update VID/PID matching to the USBusKit development device and verify binding.
- [ ] Implement bulk command/response transport.
- [ ] Implement vendor-specific control transfers if EP0 is selected.
- [ ] Discover exposed peripheral instances during probe.
- [ ] Register Linux `i2c_adapter` instances and implement `master_xfer()`.
- [ ] Handle disconnects, transfer failures, and teardown safely.
- [ ] Later: SPI controller, UART/TTY, and CAN network interfaces.

## 6. Diagnostics and tooling

- [x] Verify vendor bulk echo from Linux with PyUSB.
- [ ] Add MCU log capture and retrieval.
- [ ] Decide log transport: dedicated USB IN endpoint, multiplexed bulk, CDC-ACM, or debug UART.
- [ ] Add USB and peripheral error counters.
- [ ] Add useful Linux kernel diagnostics.

### CLI — proposed commands

```bash
usbuskit list
usbuskit info
usbuskit capabilities
usbuskit status
usbuskit configure i2c 1 --speed 400000
usbuskit logs
```

- [ ] Implement device discovery and information commands.
- [ ] Implement peripheral configuration commands.
- [ ] Implement live log viewing.

## 7. Open design decisions

| Decision | Current position | Status |
|---|---|---|
| Peripheral configuration transport | Compare vendor-specific EP0 control requests with bulk messages | **Open** |
| Request/response transaction IDs | May be unnecessary for one outstanding I²C request per adapter; consider late responses | **Open** |
| Debug logging transport | USB endpoint, shared bulk, CDC, or UART | **Open** |
| Pin assignment policy | MCU should enforce hardware limits; host selection vs automatic allocation TBD | **Open** |
| Capability format | Prefer per-instance records over counts alone | **Proposed** |

## 8. Suggested next milestones

1. **Discovery:** Specify and implement `GET_CAPABILITIES`; read it from Linux.
2. **Configuration:** Choose EP0 or bulk; configure one Bluepill I²C instance.
3. **I²C firmware:** Execute a real transfer and return bus-level results.
4. **Linux integration:** Register one `i2c_adapter` and connect `master_xfer()` to USB.
5. **Robustness:** Test NACK, timeout, disconnect, and bus recovery.
6. **Expansion:** Add more peripherals and optional diagnostics.

---

### USB VID/PID Licensing and Allocation

**Priority:** Medium
**Status:** Pending 
**Target:** Before public firmware/hardware distribution

#### Current Development Setup
- [x] Use temporary VID/PID (`0xCAFE:0x4001`) for local development.
- [x] Make VID/PID configurable through CMake.
- [ ] Document that development identifiers are not officially allocated to USBusKit.

#### Investigate VID/PID Allocation Options
- [ ] Investigate the OpenMoko USB PID allocation program (`VID 0x1D50`).
- [ ] Investigate the pid.codes open-source PID registry.
- [ ] Investigate STMicroelectronics' USB PID sublicensing program (`VID 0x0483`).
- [ ] Contact ST support or an authorized distributor to confirm eligibility.
- [ ] Verify whether ST sublicensing is free for open-source projects.
- [ ] Confirm whether ST's PID can be used on third-party Bluepill boards.
- [ ] Review USB-IF requirements for VID/PID allocation.

#### Multi-MCU Compatibility
- [ ] Determine whether one VID/PID can be used across different MCU families.
- [ ] Verify licensing restrictions for STM32, ESP32-S3, and other supported MCUs.
- [ ] Evaluate using a single USBusKit identity with runtime capability reporting.
- [ ] Decide whether different hardware implementations require separate PIDs.

#### Implementation
- [ ] Select a long-term VID/PID allocation strategy.
- [ ] Apply for an authorized PID?
- [ ] Update USB descriptors with the allocated VID/PID.
- [ ] Update Linux USB driver matching.
- [ ] Document VID/PID licensing and usage restrictions.

#### References
- [OpenMoko USB PID Registry](https://github.com/openmoko/openmoko-usb-oui)
- [pid.codes — Open-source USB PID Registry](https://pid.codes/)
- [Raspberry Pi USB PID Allocation](https://github.com/raspberrypi/usb-pid)
- [STMicroelectronics — STM32F103 USB PID Discussion](https://community.st.com/stm32-mcus-embedded-software-32/dear-sir-do-i-need-a-sub-license-for-the-usb-hid-product-based-on-stm32f103-or-is-there-any-generic-pid-that-i-can-use-thanks-26218)
- [USB-IF — Obtaining a Vendor ID](https://www.usb.org/getting-vendor-id)

---

### Maintenance notes

- Check a box only after implementation and a basic test.
- Record architectural decisions in the **Open design decisions** table before treating them as requirements.
- Keep MCU-specific limitations separate from the common USBusKit protocol.
