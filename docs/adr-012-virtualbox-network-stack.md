# ADR-012: VirtualBox network stack target

## Status

Accepted. The first hardware and device-interface layer is implemented.

## Context

VirtualBox does not expose an IEEE 802.11 wireless adapter to the guest. A
bridged connection may use the host's Wi-Fi interface, but the guest still sees
an emulated Ethernet NIC. Supported virtual hardware includes AMD PCnet,
Intel PRO/1000 variants and virtio-net.

## Decision

The initial PureC OS network target is the VirtualBox Intel PRO/1000 MT Desktop
(82540EM, e1000). Networking will be split into bounded layers:

1. PCI discovery, BAR mapping and reset for vendor `8086`, device `100e`.
2. Fixed-size DMA RX/TX descriptor rings with no allocation in interrupts.
3. Ethernet frame interface and MAC address ownership.
4. ARP cache with expiry and bounded entries.
5. IPv4 validation, checksum and routing for one interface.
6. UDP sockets and a DHCP client for VirtualBox NAT/bridged networking.
7. ICMP echo for diagnostics, followed by DNS and TCP in later iterations.

The interrupt handler will acknowledge hardware and publish completed ring
indices only. Packet parsing and protocol work will run in a scheduler thread.
Ring sizes, packet queues and protocol tables will be compile-time bounded.

## Implemented first slice

- `drivers/net/e1000_82540em` owns PCI `8086:100e`, controller reset, MAC,
  MMIO registers and legacy RX/TX DMA descriptors.
- `net/net_device` owns the driver-neutral device API, counters and bounded
  raw-frame receive queues. It contains no IPv4, UDP, DHCP, DNS or TCP logic.
- `net/net_service` owns deferred polling and is the future hand-off point to
  the Ethernet protocol module.
- The initial mode is polling with a budget of 32 RX descriptors per scheduler
  pass. Hardware interrupts remain masked until the kernel has a general PCI
  IRQ registration and routing interface.

For one 82540EM adapter the driver reserves 34 physical pages: one RX ring,
one TX ring, 16 RX buffers and 16 TX buffers (136 KiB with 4 KiB pages). The
device layer has four statically bounded queues of 16 Ethernet frames. No heap
allocation occurs while transmitting or receiving.

The next slice should add a separate Ethernet-II/ARP module that drains the raw
frame queue. IPv4, UDP and DHCP follow it as independent modules; DNS and TCP
remain later iterations.

## Wi-Fi consequence

An IEEE 802.11 MAC/authentication stack is deliberately postponed until a
specific physical chipset is selected. That work additionally requires scan,
association, authentication, encryption/key management and radio-specific
firmware handling; none of those operations can be tested against VirtualBox's
Ethernet devices.
