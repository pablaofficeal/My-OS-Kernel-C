#include "acpi_ec.h"
#include "../../kernel/diagnostics/klog.h"

#define ACPI_EC_DATA_PORT   0x62U
#define ACPI_EC_SC_PORT     0x66U
#define ACPI_EC_CMD_READ    0x80U
#define ACPI_EC_CMD_WRITE   0x81U
#define ACPI_EC_CMD_QUERY   0x84U

static bool ec_ready;

static inline void ec_outb(uint16_t port, uint8_t value){
    __asm__ volatile("outb %0,%1"::"a"(value),"Nd"(port));
}

static inline uint8_t ec_inb(uint16_t port){
    uint8_t value;
    __asm__ volatile("inb %1,%0":"=a"(value):"Nd"(port));
    return value;
}

static bool ec_wait_status(uint8_t mask, uint8_t expected, uint32_t timeout){
    for(uint32_t i=0;i<timeout;i++){
        uint8_t status=ec_inb(ACPI_EC_SC_PORT);
        if((status & mask)==expected)
            return true;
        __asm__ volatile("pause");
    }
    return false;
}

bool acpi_ec_init(void){
    ec_ready=ec_wait_status(ACPI_EC_STATUS_IBF, 0, 100000U);
    if(!ec_ready)
        klog(KLOG_WARN, "acpi-ec: controller busy during init");
    else
        klog(KLOG_OK, "acpi-ec: ports 0x62/0x66 ready");
    return ec_ready;
}

bool acpi_ec_is_ready(void){
    return ec_ready;
}

bool acpi_ec_read(uint8_t offset, uint8_t *value){
    if(!value)
        return false;
    if(!ec_wait_status(ACPI_EC_STATUS_IBF, 0, 100000U))
        return false;
    ec_outb(ACPI_EC_SC_PORT, ACPI_EC_CMD_READ);
    if(!ec_wait_status(ACPI_EC_STATUS_IBF, 0, 100000U))
        return false;
    ec_outb(ACPI_EC_DATA_PORT, offset);
    if(!ec_wait_status(ACPI_EC_STATUS_OBF, ACPI_EC_STATUS_OBF, 100000U))
        return false;
    *value=ec_inb(ACPI_EC_DATA_PORT);
    return true;
}

bool acpi_ec_write(uint8_t offset, uint8_t value){
    if(!ec_wait_status(ACPI_EC_STATUS_IBF, 0, 100000U))
        return false;
    ec_outb(ACPI_EC_SC_PORT, ACPI_EC_CMD_WRITE);
    if(!ec_wait_status(ACPI_EC_STATUS_IBF, 0, 100000U))
        return false;
    ec_outb(ACPI_EC_DATA_PORT, offset);
    if(!ec_wait_status(ACPI_EC_STATUS_IBF, 0, 100000U))
        return false;
    ec_outb(ACPI_EC_DATA_PORT, value);
    return true;
}

bool acpi_ec_query(uint8_t *value){
    if(!value)
        return false;
    if(!ec_wait_status(ACPI_EC_STATUS_IBF, 0, 100000U))
        return false;
    ec_outb(ACPI_EC_SC_PORT, ACPI_EC_CMD_QUERY);
    if(!ec_wait_status(ACPI_EC_STATUS_OBF, ACPI_EC_STATUS_OBF, 100000U))
        return false;
    *value=ec_inb(ACPI_EC_DATA_PORT);
    return true;
}
