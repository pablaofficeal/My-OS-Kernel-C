// usb_driver.h
#ifndef USB_DRIVER_H
#define USB_DRIVER_H

#include <stdint.h>
#include <stddef.h>

// Базовые структуры USB
typedef struct {
    uint8_t request_type;
    uint8_t request;
    uint16_t value;
    uint16_t index;
    uint16_t length;
} usb_setup_packet_t;

typedef struct {
    uint8_t length;
    uint8_t descriptor_type;
    uint16_t bcd_usb;
    uint8_t device_class;
    uint8_t device_subclass;
    uint8_t device_protocol;
    uint8_t max_packet_size;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t release_number;
    uint8_t manufacturer_string_index;
    uint8_t product_string_index;
    uint8_t serial_number_string_index;
    uint8_t num_configurations;
} usb_device_descriptor_t;

// Функции драйвера
int usb_controller_init(void);
int usb_device_detect(void);
int usb_send_control_transfer(uint8_t device_addr, usb_setup_packet_t* setup, void* data);
void usb_controller_reset(void);

#endif