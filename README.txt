# 🖥️ Micro VMM – A Minimal Virtual Machine Monitor using KVM

This project implements a minimal hypervisor (Virtual Machine Monitor) using the Linux Kernel-based Virtual Machine (KVM) API. It demonstrates low-level virtualization concepts including guest memory management, I/O port emulation, and device polling without interrupts.

---

## 📋 Overview

The Micro VMM runs on Linux and uses KVM to virtualize x86-64 guest binaries. It loads a custom binary into guest memory and emulates basic hardware components (console, keyboard, and interval timer) using polled I/O mechanisms.

---

## 🧰 Requirements

•⁠  ⁠Linux OS with KVM support
•⁠  ⁠Nested virtualization enabled (for VM environments like VMware or VirtualBox)
•⁠  ⁠GCC or Clang (for C/C++)
•⁠  ⁠libkvm (KVM headers)
•⁠  ⁠⁠ make ⁠ (for build automation)
•⁠  ⁠(Optional) ⁠ libelf ⁠ if extending to support ELF binaries

---

## 📦 Installation

Clone the repository:

⁠ bash
git clone https://github.com/zealp25/Micro-VMM
cd micro-vmm
 ⁠

Build the VMM:

⁠ bash
make
 ⁠

---

## 🚀 Usage

To run the Micro VMM:

⁠ bash
./vmm <guest_binary>
 ⁠

The guest binary must be a raw binary file containing simple x86-64 code. ELF support is optional and can be added with ⁠ libelf ⁠.

---

## ✅ Features Summary

•⁠  ⁠🧠 *Loads Guest Binary*: Maps a raw binary into guest memory and executes it
•⁠  ⁠🖨️ *Console Emulation*: Writes to I/O port ⁠ 0x42 ⁠ are buffered and printed to the host console
•⁠  ⁠⌨️ *Virtual Keyboard Device*:

  * Accepts key input from host STDIN
  * Maps it to I/O port ⁠ 0x44 ⁠ with a status register at ⁠ 0x45 ⁠
•⁠  ⁠⏱️ *Virtual Interval Timer*:

  * Guest sets timer via ⁠ 0x46 ⁠, enables/disables via bit 0 of ⁠ 0x47 ⁠
  * Timer events notified by bit 1 of ⁠ 0x47 ⁠
•⁠  ⁠🔁 *Polled I/O Architecture*: Guest polls device status registers instead of relying on interrupts
•⁠  ⁠📜 *Simple Device Drivers in Guest*: Basic keyboard and console drivers implemented in guest code

---

## 🛠️ Devices Implemented

| Device         | I/O Port(s)    | Description                                                               |
| -------------- | -------------- | ------------------------------------------------------------------------- |
| Console        | ⁠ 0x42 ⁠         | Writes characters to a buffer. Newline prints the buffer to host console. |
| Keyboard       | ⁠ 0x44 ⁠, ⁠ 0x45 ⁠ | Accepts input from STDIN and sets status flags.                           |
| Interval Timer | ⁠ 0x46 ⁠, ⁠ 0x47 ⁠ | Fires events at intervals. Guest must poll and ack each event.            |

---

## 🧪 Testing

•⁠  ⁠Guest binaries should implement a loop to poll:

  * Keyboard status at ⁠ 0x45 ⁠
  * Timer status at ⁠ 0x47 ⁠
•⁠  ⁠Pressing keys on host STDIN sends input to guest
•⁠  ⁠Console prints echoed lines on timer ticks after enter is pressed

---

## 📁 Structure


├── vmm.c              # Main hypervisor logic
├── guest.asm          # Sample guest binary (or guest.c for a compiled version)
├── devices/           # Device emulation logic (keyboard, timer, console)
├── Makefile           # Build instructions
└── README.md          # This file


---

## 📌 Notes

•⁠  ⁠This implementation uses *polled I/O* instead of interrupts to simplify development.
•⁠  ⁠Device emulation is minimal and intentionally limited to key concepts.
•⁠  ⁠Designed for educational use to understand low-level virtualization with KVM.

---

Let me know if you'd like a version of the ⁠ README.md ⁠ that includes code snippets, architecture diagrams, or setup instructions for specific Linux distributions (e.g., enabling KVM on Ubuntu).
