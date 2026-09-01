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

static bool ec_wait_ibf_clear(uint32_t timeout){
    return ec_wait_status(ACPI_EC_STATUS_IBF, 0, timeout);
}

bool acpi_ec_write_raw(uint8_t cmd, uint8_t offset, uint8_t value){
    uint8_t status_before=ec_inb(ACPI_EC_SC_PORT);
    klogf(KLOG_INFO, "acpi-ec: RAW write cmd=0x%02x off=0x%02x val=0x%02x (%u) status_before=0x%02x",
          cmd, offset, value, value, status_before);
    if(!ec_wait_ibf_clear(100000U)){
        uint8_t st=ec_inb(ACPI_EC_SC_PORT);
        klogf(KLOG_WARN, "acpi-ec: RAW 0x%02x timeout IBF before cmd (status=0x%02x)", cmd, st);
        return false;
    }
    ec_outb(ACPI_EC_SC_PORT, cmd);
    if(!ec_wait_ibf_clear(100000U)){
        uint8_t st=ec_inb(ACPI_EC_SC_PORT);
        klogf(KLOG_WARN, "acpi-ec: RAW 0x%02x timeout IBF after cmd (status=0x%02x)", cmd, st);
        return false;
    }
    ec_outb(ACPI_EC_DATA_PORT, offset);
    if(!ec_wait_ibf_clear(100000U)){
        uint8_t st=ec_inb(ACPI_EC_SC_PORT);
        klogf(KLOG_WARN, "acpi-ec: RAW 0x%02x timeout IBF after offset (status=0x%02x)", cmd, st);
        return false;
    }
    ec_outb(ACPI_EC_DATA_PORT, value);
    if(!ec_wait_ibf_clear(100000U)){
        uint8_t st=ec_inb(ACPI_EC_SC_PORT);
        klogf(KLOG_WARN, "acpi-ec: RAW 0x%02x timeout IBF after value (status=0x%02x)", cmd, st);
        return false;
    }
    uint8_t status_after=ec_inb(ACPI_EC_SC_PORT);
    klogf(KLOG_INFO, "acpi-ec: RAW write cmd=0x%02x off=0x%02x val=0x%02x done status_after=0x%02x",
          cmd, offset, value, status_after);
    return true;
}

bool acpi_ec_init(void){
    uint8_t status=ec_inb(ACPI_EC_SC_PORT);
    ec_ready=ec_wait_status(ACPI_EC_STATUS_IBF, 0, 100000U);
    uint8_t status2=ec_inb(ACPI_EC_SC_PORT);
    if(!ec_ready)
        klogf(KLOG_WARN, "acpi-ec: controller busy during init (status before=0x%02x after=0x%02x IBF still set)", status, status2);
    else
        klogf(KLOG_OK, "acpi-ec: ports 0x62/0x66 ready (status 0x%02x -> 0x%02x)", status, status2);
    return ec_ready;
}

bool acpi_ec_is_ready(void){
    return ec_ready;
}

bool acpi_ec_read(uint8_t offset, uint8_t *value){
    if(!value){
        klog(KLOG_WARN, "acpi-ec: read called with null value ptr");
        return false;
    }
    if(!ec_wait_status(ACPI_EC_STATUS_IBF, 0, 100000U)){
        uint8_t st=ec_inb(ACPI_EC_SC_PORT);
        klogf(KLOG_WARN, "acpi-ec: read [0x%02x] timeout IBF before READ cmd (status=0x%02x)", offset, st);
        return false;
    }
    ec_outb(ACPI_EC_SC_PORT, ACPI_EC_CMD_READ);
    if(!ec_wait_status(ACPI_EC_STATUS_IBF, 0, 100000U)){
        uint8_t st=ec_inb(ACPI_EC_SC_PORT);
        klogf(KLOG_WARN, "acpi-ec: read [0x%02x] timeout IBF after READ cmd (status=0x%02x)", offset, st);
        return false;
    }
    ec_outb(ACPI_EC_DATA_PORT, offset);
    if(!ec_wait_status(ACPI_EC_STATUS_OBF, ACPI_EC_STATUS_OBF, 100000U)){
        uint8_t st=ec_inb(ACPI_EC_SC_PORT);
        klogf(KLOG_WARN, "acpi-ec: read [0x%02x] timeout OBF after offset (status=0x%02x) – EC does not respond", offset, st);
        return false;
    }
    *value=ec_inb(ACPI_EC_DATA_PORT);
    klogf(KLOG_INFO, "acpi-ec: read  [0x%02x] -> 0x%02x (%u) status=0x%02x", offset, *value, *value, ec_inb(ACPI_EC_SC_PORT));
    return true;
}

bool acpi_ec_write(uint8_t offset, uint8_t value){
    klogf(KLOG_INFO, "acpi-ec: write [0x%02x] <- 0x%02x (%u)", offset, value, value);
    if(!ec_wait_status(ACPI_EC_STATUS_IBF, 0, 100000U)){
        uint8_t st=ec_inb(ACPI_EC_SC_PORT);
        klogf(KLOG_WARN, "acpi-ec: write [0x%02x] timeout IBF before WRITE cmd (status=0x%02x)", offset, st);
        return false;
    }
    ec_outb(ACPI_EC_SC_PORT, ACPI_EC_CMD_WRITE);
    if(!ec_wait_status(ACPI_EC_STATUS_IBF, 0, 100000U)){
        uint8_t st=ec_inb(ACPI_EC_SC_PORT);
        klogf(KLOG_WARN, "acpi-ec: write [0x%02x] timeout IBF after WRITE cmd (status=0x%02x)", offset, st);
        return false;
    }
    ec_outb(ACPI_EC_DATA_PORT, offset);
    if(!ec_wait_status(ACPI_EC_STATUS_IBF, 0, 100000U)){
        uint8_t st=ec_inb(ACPI_EC_SC_PORT);
        klogf(KLOG_WARN, "acpi-ec: write [0x%02x] timeout IBF after offset (status=0x%02x)", offset, st);
        return false;
    }
    ec_outb(ACPI_EC_DATA_PORT, value);
    if(!ec_wait_status(ACPI_EC_STATUS_IBF, 0, 100000U)){
        uint8_t st=ec_inb(ACPI_EC_SC_PORT);
        klogf(KLOG_WARN, "acpi-ec: write [0x%02x] <- 0x%02x timeout IBF after value (status=0x%02x)", offset, value, st);
        return false;
    }
    // верификация через IBF clear = EC принял команду
    klogf(KLOG_INFO, "acpi-ec: write [0x%02x] <- 0x%02x accepted (status=0x%02x)", offset, value, ec_inb(ACPI_EC_SC_PORT));
    return true;
}

bool acpi_ec_query(uint8_t *value){
    if(!value)
        return false;
    if(!ec_wait_status(ACPI_EC_STATUS_IBF, 0, 100000U)){
        uint8_t st=ec_inb(ACPI_EC_SC_PORT);
        klogf(KLOG_WARN, "acpi-ec: query timeout IBF before QUERY cmd (status=0x%02x)", st);
        return false;
    }
    ec_outb(ACPI_EC_SC_PORT, ACPI_EC_CMD_QUERY);
    if(!ec_wait_status(ACPI_EC_STATUS_OBF, ACPI_EC_STATUS_OBF, 100000U)){
        uint8_t st=ec_inb(ACPI_EC_SC_PORT);
        klogf(KLOG_WARN, "acpi-ec: query timeout OBF (status=0x%02x)", st);
        return false;
    }
    *value=ec_inb(ACPI_EC_DATA_PORT);
    klogf(KLOG_INFO, "acpi-ec: query -> 0x%02x (%u)", *value, *value);
    return true;
}
