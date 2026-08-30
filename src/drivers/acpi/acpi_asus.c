#include "acpi_asus.h"
#include "acpi.h"
#include "acpi_aml.h"
#include "acpi_ec.h"
#include "acpi_wmi.h"
#include "../../kernel/diagnostics/klog.h"
#include "../../lib/string.h"

static const uint8_t asus_wmi_guid[16]={
    0xD0, 0x5E, 0x84, 0x97, 0x6D, 0x4E, 0xDE, 0x11,
    0x8A, 0x39, 0x08, 0x00, 0x20, 0x0C, 0x9A, 0x66
};

struct asus_bios_args {
    uint32_t arg0;
    uint32_t arg1;
} __attribute__((packed));

static bool asus_call_devs_direct(const char *parent, uint32_t dev_id, uint32_t value,
                                  uint32_t *retval){
    struct acpi_aml_method method;
    if(!acpi_aml_find_method(parent, "DEVS", &method))
        return false;

    uint64_t args[7]={dev_id, value, 0, 0, 0, 0, 0};
    uint64_t result=0;
    if(!acpi_aml_evaluate_method(&method, args, 2, &result))
        return false;
    if(retval)
        *retval=(uint32_t)result;
    klogf(KLOG_INFO, "acpi-asus: %s.DEVS(0x%x, %u) -> 0x%x",
          parent, dev_id, value, (unsigned)result);
    return true;
}

static bool asus_call_devs_wmi(uint32_t dev_id, uint32_t value, uint32_t *retval){
    struct asus_bios_args args={.arg0=dev_id, .arg1=value};
    if(acpi_wmi_evaluate_method(asus_wmi_guid, 0, ASUS_WMI_METHODID_DEVS,
                                &args, sizeof(args), retval))
        return true;
    return acpi_wmi_evaluate_method(asus_wmi_guid, 1, ASUS_WMI_METHODID_DEVS,
                                    &args, sizeof(args), retval);
}

bool acpi_asus_init(void){
    if(!acpi_is_ready())
        return false;
    acpi_ec_init();
    acpi_wmi_init();
    acpi_asus_log_ec_info();
    return true;
}

void acpi_asus_log_ec_info(void){
    const struct acpi_table_header *dsdt=acpi_get_dsdt();
    if(!dsdt)
        return;
    const uint8_t *body=(const uint8_t *)dsdt+sizeof(struct acpi_table_header);
    uint32_t body_len=dsdt->length-(uint32_t)sizeof(struct acpi_table_header);
    for(uint32_t i=0;i+12U<body_len;i++){
        if(memcmp(body+i, "F00A27C2.307", 12)==0
           || memcmp(body+i, "F00A27C2", 8)==0
           || memcmp(body+i, "EC Version", 10)==0){
            char version[32];
            uint32_t n=0;
            for(uint32_t j=i;j<body_len && n+1<sizeof(version);j++){
                char c=(char)body[j];
                if(c=='\0' || c=='\n' || c=='\r')
                    break;
                if(c>=0x20 && c<=0x7E)
                    version[n++]=(char)c;
                if(n>=20U)
                    break;
            }
            version[n]='\0';
            if(n>0)
                klogf(KLOG_INFO, "acpi-asus: EC version string in DSDT: %s", version);
            return;
        }
    }
    klog(KLOG_DEBUG, "acpi-asus: EC version string not found in DSDT");
}

bool acpi_asus_rfkill_clear_wifi(void){
    if(!acpi_asus_init()){
        klog(KLOG_WARN, "acpi-asus: ACPI not ready, cannot clear RF-kill");
        return false;
    }

    uint32_t retval=0;
    klog(KLOG_INFO, "acpi-asus: clearing WLAN RF-kill (DEVID=0x00010011)");

    if(asus_call_devs_wmi(ASUS_WMI_DEVID_WLAN, 1, &retval)){
        klog(KLOG_OK, "acpi-asus: WLAN enabled via ACPI WMI");
        return true;
    }

    static const char *parents[]={"ATKD", "ASUS", NULL};
    for(int i=0;parents[i];i++){
        if(asus_call_devs_direct(parents[i], ASUS_WMI_DEVID_WLAN, 1, &retval)){
            klogf(KLOG_OK, "acpi-asus: WLAN enabled via \\%s.DEVS", parents[i]);
            return true;
        }
    }

  klog(KLOG_WARN, "acpi-asus: failed to clear WLAN RF-kill via WMI/namespace");
    return false;
}
