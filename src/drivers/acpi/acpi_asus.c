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

#define ASUS_EC_WIFI_OFFSET 0xD9U
#define ASUS_EC_WIFI_ON     0x01U

static void asus_dump_ec_snapshot(const char *phase){
    static const uint8_t watch_offsets[]={0x03U, 0x31U, 0x57U, 0x5BU, 0x67U, 0xD9U, 0x0AU, 0x0BU, 0x18U, 0x2AU};
    klogf(KLOG_INFO, "acpi-asus: EC snapshot [%s] ec_ready=%u:", phase, acpi_ec_is_ready()?1U:0U);
    for(uint32_t i=0;i<sizeof(watch_offsets);i++){
        uint8_t off=watch_offsets[i];
        uint8_t val=0;
        bool ok=acpi_ec_read(off, &val);
        if(ok)
            klogf(KLOG_INFO, "acpi-asus:   EC[0x%02x] = 0x%02x (%u) %s", off, val, val,
                  (off==0xD9U && val==ASUS_EC_WIFI_ON)?"<- WIFI_ON":"");
        else
            klogf(KLOG_WARN, "acpi-asus:   EC[0x%02x] = <read FAILED> status=0x%02x", off, 0);
    }
}

static bool asus_call_devs_direct(const char *parent, uint32_t dev_id, uint32_t value,
                                   uint32_t *retval){
    struct acpi_aml_method method;
    klogf(KLOG_INFO, "acpi-asus: trying \\%s.DEVS(0x%x, %u)", parent, dev_id, value);
    if(!acpi_aml_find_method(parent, "DEVS", &method)){
        klogf(KLOG_INFO, "acpi-asus: \\%s.DEVS not found (parent=%s)", parent, parent);
        return false;
    }
    klogf(KLOG_INFO, "acpi-asus: found \\%s.DEVS at %p len=%u args=%u", parent, method.bytecode, method.bytecode_length, method.arg_count);

    uint64_t args[7]={dev_id, value, 0, 0, 0, 0, 0};
    uint64_t result=0;
    bool eval=acpi_aml_evaluate_method(&method, args, 2, &result);
    klogf(eval?KLOG_INFO:KLOG_WARN, "acpi-asus: %s.DEVS(0x%x, %u) -> 0x%x (eval %s)",
          parent, dev_id, value, (unsigned)result, eval?"OK":"FAIL");
    if(!eval)
        return false;
    if(retval)
        *retval=(uint32_t)result;
    return true;
}

static bool asus_call_devs_wmi(uint32_t dev_id, uint32_t value, uint32_t *retval){
    struct asus_bios_args args={.arg0=dev_id, .arg1=value};
    klogf(KLOG_INFO, "acpi-asus: trying WMI DEVS dev=0x%x val=%u inst=0/1", dev_id, value);
    uint32_t tmp0=0, tmp1=0;
    bool ok0=acpi_wmi_evaluate_method(asus_wmi_guid, 0, ASUS_WMI_METHODID_DEVS,
                                 &args, sizeof(args), &tmp0);
    klogf(ok0?KLOG_INFO:KLOG_INFO, "acpi-asus: WMI DEVS inst=0 dev=0x%x val=%u -> retval=0x%x %s",
          dev_id, value, tmp0, ok0?"OK":"FAIL/NOT_FOUND");
    if(ok0){
        if(retval) *retval=tmp0;
        klogf(KLOG_INFO, "acpi-asus: WMI DEVS success via instance 0");
        return true;
    }
    bool ok1=acpi_wmi_evaluate_method(asus_wmi_guid, 1, ASUS_WMI_METHODID_DEVS,
                                     &args, sizeof(args), &tmp1);
    klogf(ok1?KLOG_INFO:KLOG_INFO, "acpi-asus: WMI DEVS inst=1 dev=0x%x val=%u -> retval=0x%x %s",
          dev_id, value, tmp1, ok1?"OK":"FAIL/NOT_FOUND");
    if(ok1){
        if(retval) *retval=tmp1;
        klogf(KLOG_INFO, "acpi-asus: WMI DEVS success via instance 1");
        return true;
    }
    klogf(KLOG_INFO, "acpi-asus: WMI DEVS both instances failed for dev=0x%x", dev_id);
    return false;
}

static bool asus_ec_write_verify(uint8_t offset, uint8_t value){
    uint8_t before=0, after=0;
    klogf(KLOG_INFO, "acpi-asus: EC write-verify START [0x%02x] <- 0x%02x (%u)", offset, value, value);
    bool ok_before=acpi_ec_read(offset, &before);
    if(!ok_before){
        klogf(KLOG_WARN, "acpi-asus: EC[0x%02x] write %u: BEFORE read FAILED -> abort", offset, value);
        return false;
    }
    klogf(KLOG_INFO, "acpi-asus: EC[0x%02x] before = 0x%02x (%u)", offset, before, before);
    bool ok_write=acpi_ec_write(offset, value);
    if(!ok_write){
        klogf(KLOG_WARN, "acpi-asus: EC[0x%02x] write %u FAILED at write command", offset, value);
        return false;
    }
    bool ok_after=acpi_ec_read(offset, &after);
    if(!ok_after){
        klogf(KLOG_WARN, "acpi-asus: EC[0x%02x] write %u: AFTER read FAILED", offset, value);
        return false;
    }
    klogf(KLOG_INFO, "acpi-asus: EC[0x%02x] write %u: %u (0x%02x) -> %u (0x%02x) %s",
          offset, value, before, before, after, after,
          (after==value)?"VERIFY_PASSED":"VERIFY_MISMATCH");
    bool is_target_value = (value==ASUS_EC_WIFI_ON);
    bool verify = (after==value) && is_target_value;
    if(!verify){
        if(!is_target_value)
            klogf(KLOG_INFO, "acpi-asus: EC[0x%02x] verify: value 0x%02x != WIFI_ON 0x%02x, not counting as success (probe only)", offset, value, ASUS_EC_WIFI_ON);
        else
            klogf(KLOG_INFO, "acpi-asus: EC[0x%02x] verify FAILED: expected 0x%02x got 0x%02x", offset, value, after);
    } else {
        klogf(KLOG_OK, "acpi-asus: EC[0x%02x] write-verify SUCCESS WIFI_ON", offset);
    }
    return verify;
}

static bool asus_ec_enable_wifi(void){
    static const uint8_t offsets[]={0xD9U, 0x57U, 0x31U, 0x67U, 0x5BU, 0x03U};
    static const uint8_t values[]={0x01U, 0x00U, 0x80U};

    klog(KLOG_INFO, "acpi-asus: === EC enable Wi-Fi START ===");
    klogf(KLOG_INFO, "acpi-asus: will try %u offsets * %u values = %u combos + RAW 0x59",
          (unsigned)sizeof(offsets), (unsigned)sizeof(values), (unsigned)(sizeof(offsets)*sizeof(values)));
    asus_dump_ec_snapshot("before EC enable");

    for(uint32_t oi=0;oi<sizeof(offsets);oi++){
        uint8_t offset=offsets[oi];
        uint8_t cur=0;
        bool probe_ok=acpi_ec_read(offset, &cur);
        if(probe_ok)
            klogf(KLOG_INFO, "acpi-asus: EC[0x%02x] probe = 0x%02x (%u) (offset %u/%u)", offset, cur, cur, oi+1, (unsigned)sizeof(offsets));
        else
            klogf(KLOG_WARN, "acpi-asus: EC[0x%02x] probe READ FAILED (offset %u/%u)", offset, oi+1, (unsigned)sizeof(offsets));
        for(uint32_t vi=0;vi<sizeof(values);vi++){
            uint8_t val=values[vi];
            klogf(KLOG_INFO, "acpi-asus: EC attempt [%u/%u] offset 0x%02x <- 0x%02x (%u)", oi*sizeof(values)+vi+1, (unsigned)(sizeof(offsets)*sizeof(values)), offset, val, val);
            if(asus_ec_write_verify(offset, val)){
                klogf(KLOG_OK, "acpi-asus: EC enable SUCCESS at combo offset=0x%02x val=0x%02x (%u/%u)", offset, val, oi, vi);
                asus_dump_ec_snapshot("after SUCCESS");
                return true;
            } else {
                klogf(KLOG_INFO, "acpi-asus: EC combo offset=0x%02x val=0x%02x FAILED, continue", offset, val);
            }
        }
    }

    klog(KLOG_INFO, "acpi-asus: EC table combos exhausted, trying RAW fallback cmd 0x59");
    /* fallback: raw cmd 0x59 как в заметках, но только если readback изменился */
    uint8_t before_raw=0;
    bool before_ok=acpi_ec_read(0xD9U, &before_raw);
    klogf(KLOG_INFO, "acpi-asus: RAW fallback: EC[0xD9] before=0x%02x (%u) ok=%u", before_raw, before_raw, before_ok?1U:0U);
    bool raw_written=acpi_ec_write_raw(0x59U, 0xD9U, 0x01U);
    klogf(raw_written?KLOG_INFO:KLOG_WARN, "acpi-asus: RAW write 0x59 off=0xD9 val=0x01 -> %s", raw_written?"accepted":"FAILED");
    if(raw_written){
        uint8_t after=0;
        bool after_ok=acpi_ec_read(0xD9U, &after);
        klogf(KLOG_INFO, "acpi-asus: RAW fallback readback EC[0xD9]=0x%02x (%u) ok=%u (expect 0x01)", after, after, after_ok?1U:0U);
        if(after_ok && after==0x01U){
            klog(KLOG_OK, "acpi-asus: RAW fallback SUCCESS");
            asus_dump_ec_snapshot("after RAW SUCCESS");
            return true;
        } else {
            klogf(KLOG_WARN, "acpi-asus: RAW fallback verify FAILED: got 0x%02x expect 0x01", after);
        }
    } else {
        klog(KLOG_WARN, "acpi-asus: RAW write not accepted, skipping readback");
    }
    klog(KLOG_WARN, "acpi-asus: === EC enable Wi-Fi FAILED all methods ===");
    asus_dump_ec_snapshot("after FAIL");
    return false;
}

bool acpi_asus_init(void){
    klogf(KLOG_INFO, "acpi-asus: init start acpi_ready=%u ec_ready_before=%u", acpi_is_ready()?1U:0U, acpi_ec_is_ready()?1U:0U);
    if(!acpi_is_ready()){
        klog(KLOG_WARN, "acpi-asus: init FAIL acpi not ready");
        return false;
    }
    bool ec_ok=acpi_ec_init();
    klogf(ec_ok?KLOG_OK:KLOG_WARN, "acpi-asus: acpi_ec_init -> %s", ec_ok?"OK":"FAIL");
    bool wmi_ok=acpi_wmi_init();
    klogf(wmi_ok?KLOG_OK:KLOG_INFO, "acpi-asus: acpi_wmi_init -> %s", wmi_ok?"OK (blocks found)":"no blocks");
    acpi_asus_log_ec_info();
    asus_dump_ec_snapshot("init");
    klog(KLOG_INFO, "acpi-asus: init done");
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
    static bool attempted;
    static bool cached_result;

    if(attempted){
        klogf(KLOG_INFO, "acpi-asus: rfkill_clear_wifi cached return %u (attempted)", cached_result?1U:0U);
        return cached_result;
    }

    klog(KLOG_INFO, "acpi-asus: ========== RF-KILL clear START ==========");
    if(!acpi_asus_init()){
        klog(KLOG_WARN, "acpi-asus: ACPI not ready, cannot clear RF-kill");
        attempted=true;
        cached_result=false;
        return false;
    }

    uint32_t retval=0;
    klog(KLOG_INFO, "acpi-asus: clearing WLAN RF-kill (DEVID=0x00010011 / 0x00010012)");
    klogf(KLOG_INFO, "acpi-asus: step 1/4: try WMI DEVS WLAN 0x%x", ASUS_WMI_DEVID_WLAN);
    asus_dump_ec_snapshot("before WMI WLAN");

    if(asus_call_devs_wmi(ASUS_WMI_DEVID_WLAN, 1, &retval)){
        klogf(KLOG_OK, "acpi-asus: WLAN enabled via ACPI WMI (retval=0x%x) – SUCCESS", retval);
        asus_dump_ec_snapshot("after WMI WLAN success");
        attempted=true;
        cached_result=true;
        return true;
    }
    klog(KLOG_INFO, "acpi-asus: WMI WLAN 0x00010011 FAILED, next try WMI WLAN_LED 0x00010012");
    if(asus_call_devs_wmi(ASUS_WMI_DEVID_WLAN_LED, 1, &retval)){
        klogf(KLOG_OK, "acpi-asus: WLAN LED/state via ACPI WMI (0x00010012 retval=0x%x) – SUCCESS", retval);
        asus_dump_ec_snapshot("after WMI LED success");
        attempted=true;
        cached_result=true;
        return true;
    }
    klog(KLOG_INFO, "acpi-asus: WMI both devids FAILED, step 2/4: try AML \\ATKD/\\ASUS.DEVS");

    static const char *parents[]={"ATKD", "ASUS", NULL};
    for(int i=0;parents[i];i++){
        klogf(KLOG_INFO, "acpi-asus: trying direct AML \\%s.DEVS 0x%x", parents[i], ASUS_WMI_DEVID_WLAN);
        if(asus_call_devs_direct(parents[i], ASUS_WMI_DEVID_WLAN, 1, &retval)){
            klogf(KLOG_OK, "acpi-asus: WLAN enabled via \\%s.DEVS (retval=0x%x) – SUCCESS", parents[i], retval);
            asus_dump_ec_snapshot("after AML DEVS success");
            attempted=true;
            cached_result=true;
            return true;
        }
        klogf(KLOG_INFO, "acpi-asus: \\%s.DEVS 0x00010011 failed, trying 0x00010012", parents[i]);
        if(asus_call_devs_direct(parents[i], ASUS_WMI_DEVID_WLAN_LED, 1, &retval)){
            klogf(KLOG_OK, "acpi-asus: WLAN LED via \\%s.DEVS (0x00010012 retval=0x%x) – SUCCESS", parents[i], retval);
            asus_dump_ec_snapshot("after AML LED success");
            attempted=true;
            cached_result=true;
            return true;
        }
        klogf(KLOG_INFO, "acpi-asus: \\%s.DEVS both devids failed, next parent", parents[i]);
    }

    klog(KLOG_INFO, "acpi-asus: AML DEVS FAILED all parents, step 3/4: EC RAM brute force");
    asus_dump_ec_snapshot("before EC brute force");
    if(asus_ec_enable_wifi()){
        klog(KLOG_OK, "acpi-asus: WLAN enabled via EC RAM write (verified readback) – SUCCESS");
        asus_dump_ec_snapshot("after EC success");
        attempted=true;
        cached_result=true;
        return true;
    }

    klog(KLOG_WARN, "acpi-asus: ========== RF-KILL clear FAILED ==========");
    klog(KLOG_WARN, "acpi-asus: EC/WMI/namespace did not change RF-kill state – check logs above for EC read values");
    asus_dump_ec_snapshot("final FAIL");
    attempted=true;
    cached_result=false;
    return false;
}
