// src/drivers/wifi/intel_ax210.h
#ifndef INTEL_AX210_H
#define INTEL_AX210_H

#include "../pci/pci.h"
#include "../../lib/stddef.h"

// Intel Vendor ID
#define INTEL_VENDOR_ID 0x8086

// AX210 Device IDs
#define AX210_DEVICE_ID 0x2725

// Регистры AX210
#define AX210_CSR_BASE 0x0000
#define AX210_CSR_CTRL 0x0000
#define AX210_CSR_STATUS 0x0008
#define AX210_CSR_INTR 0x0010
#define AX210_CSR_TX_DESC 0x0020
#define AX210_CSR_RX_DESC 0x0028

// Команды AX210
#define AX210_CMD_RESET 0x0001
#define AX210_CMD_SCAN 0x0002
#define AX210_CMD_CONNECT 0x0003
#define AX210_CMD_DISCONNECT 0x0004
#define AX210_CMD_GET_STATUS 0x0005

// Состояния адаптера
#define AX210_STATE_READY 0x00
#define AX210_STATE_BUSY 0x01
#define AX210_STATE_ERROR 0x02

// Структура дескриптора DMA
typedef struct {
    uint64_t address;
    uint32_t length;
    uint32_t flags;
} ax210_dma_descriptor_t;

// Структура команды
typedef struct {
    uint16_t command;
    uint16_t length;
    uint8_t data[256];
} ax210_command_t;

// Основные функции AX210
int ax210_probe(pci_device_t* dev);
int ax210_initialize(void);
int ax210_send_command(uint16_t cmd, void* data, size_t len);
int ax210_receive_data(void* buffer, size_t len);

#endif