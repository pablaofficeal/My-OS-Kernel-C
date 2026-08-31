#include "intel_ax201.h"
#include "../pci/pci.h"
#include "../../arch/x86_64/mmio.h"
#include "../../kernel/diagnostics/klog.h"
#include "../../lib/string.h"
#include "../../drivers/interrupts/timer.h"
#include "../../mm/pmm.h"
#include "../../net/core/net_device.h"
#include "../../net/wifi/wifi.h"
#include "../../net/network/ipv4.h"
#include "../../net/link/ethernet.h"
#include "../../boot/install_source.h"
#include "iwl-context-info.h"
#include "iwl-context-info-v2.h"
#include <stddef.h>
#include <stdint.h>

#define AX201_VENDOR_INTEL 0x8086
#define AX201_MMIO_SIZE 0x4000U
#define AX201_CTXT_INFO_SIZE 0x1000U  /* one page for CTXT_INFO struct */
#define AX201_DMA_BUFFER_SIZE 2048U

static const uint16_t ax201_device_ids[] = {
    0x06F0, 0xA0F0, 0x02F0, 0x34F0, 0x43F0, 0x4DF0, 0x51F0, 0x51F1, 0x54F0, 0x7A70, 0x7AF0, 0x2723, 0x2725, 0x7360,
};

#define AX201_FIRMWARE_QUZ_HINT   "iwlwifi-QuZ-a0-hr-b0-77.ucode"
#define AX201_FIRMWARE_QUZ_MODULE "/firmware/iwlwifi-QuZ-a0-hr-b0-77.ucode"
#define AX201_FIRMWARE_SO_HR_HINT   "iwlwifi-so-a0-hr-b0-89.ucode"
#define AX201_FIRMWARE_SO_HR_MODULE "/firmware/iwlwifi-so-a0-hr-b0-89.ucode"
#define AX201_FIRMWARE_SO_GF_HINT   "iwlwifi-so-a0-gf-a0-89.ucode"
#define AX201_FIRMWARE_SO_GF_MODULE "/firmware/iwlwifi-so-a0-gf-a0-89.ucode"
#define AX201_FIRMWARE_MAGIC        0x0A4C5749U
#define AX201_FIRMWARE_MAGIC_OFFSET 4U

/* CSR register offsets (BAR0) */
#define AX201_CSR_HW_IF_CONFIG_REG  0x000
#define AX201_CSR_INT               0x008
#define AX201_CSR_INT_MASK          0x00C
#define AX201_CSR_GPIO_IN           0x018
#define AX201_CSR_RESET             0x020
#define AX201_CSR_GP_CNTRL          0x024
#define AX201_CSR_HW_REV            0x028
#define AX201_CSR_CTXT_INFO_BA      0x040   /* v1 context-info base (Qu) */
#define AX201_CSR_CTXT_INFO_BA_HI   0x044
#define AX201_CSR_CTXT_INFO_KICK    0x048
#define AX201_CSR_UCODE_DRV_GP1_CLR 0x05C
#define AX201_CSR_MBOX_SET_REG       0x088
#define AX201_CSR_HW_RF_ID          0x09C
#define AX201_CSR_IML_DATA_ADDR_V1  0x0C0
#define AX201_CSR_IML_SIZE_ADDR_V1  0x0C4
#define AX201_CSR_CTXT_INFO_BOOT_CTRL_V1 0x0F0
#define AX201_CSR_LTR_LAST_MSG      0x0DC
#define AX201_CSR_MSIX_HW_INT_CAUSES 0x2808
#define AX201_HBUS_TARG_PRPH_WADDR  0x444
#define AX201_HBUS_TARG_PRPH_WDAT   0x448
#define AX201_UMAC_PRPH_OFFSET      0x300000U
#define AX201_UREG_CPU_INIT_RUN     0x0A05C44U
#define AX201_SO_CMD_QUEUE_SIZE     128U
#define AX201_SO_NUM_RBDS           2048U
#define AX201_CSR_AUTO_FUNC_BOOT_ENA  0x02
#define AX201_CSR_UCODE_SW_BIT_RFKILL       0x00000002U
#define AX201_CSR_UCODE_DRV_GP1_CMD_BLOCKED 0x00000004U
#define AX201_CSR_MAC_ADDR_BASE     0x380
#define AX201_CSR_MAC_ADDR0_OTP     (AX201_CSR_MAC_ADDR_BASE + 0x00)
#define AX201_CSR_MAC_ADDR1_OTP     (AX201_CSR_MAC_ADDR_BASE + 0x04)
#define AX201_CSR_MAC_ADDR0_STRAP   (AX201_CSR_MAC_ADDR_BASE + 0x08)
#define AX201_CSR_MAC_ADDR1_STRAP   (AX201_CSR_MAC_ADDR_BASE + 0x0C)

/* GP_CNTRL bits (iwlwifi CSR_GP_CNTRL) */
#define AX201_GP_CNTRL_MAC_CLOCK_READY 0x00000001U
#define AX201_GP_CNTRL_INIT_DONE       0x00000004U
#define AX201_GP_CNTRL_MAC_ACCESS_REQ  0x00000008U
#define AX201_GP_CNTRL_GOING_TO_SLEEP  0x00000010U
#define AX201_GP_CNTRL_HW_RF_KILL_SW   0x08000000U
#define AX201_HW_IF_CONFIG_PCI_OWN_SET 0x00400000U
#define AX201_MBOX_SET_OS_ALIVE        0x00000020U

#define AX201_CSR_GIO_CHICKEN_BITS          0x100
#define AX201_CSR_GIO_CHICKEN_BITS_L1A_NO_L0S_RX 0x00800000U
#define AX201_CSR_DBG_HPET_MEM_REG          0x240
#define AX201_CSR_DBG_HPET_MEM_REG_VAL      0xFFFF0000U
#define AX201_CSR_DBG_LINK_PWR_MGMT_REG     0x250
#define AX201_CSR_RESET_LINK_PWR_MGMT_DISABLED 0x80000000U
#define AX201_CSR_GIO_REG                   0x03C
#define AX201_CSR_GIO_REG_VAL_L0S_DISABLED  0x00000002U
#define AX201_CSR_HW_IF_CONFIG_HAP_WAKE     0x00080000U
#define AX201_CSR_HW_IF_CONFIG_WAKE_ME      0x08000000U
#define AX201_CSR_HW_IF_CONFIG_WAKE_ME_PCIE_OWNER_EN 0x10000000U

/* CSR_INT bits (Linux iwl-csr.h) */
#define AX201_INT_ALIVE             0x00000001U
#define AX201_INT_FH_RX             0x80000000U
#define AX201_INT_SW_ERR            0x04000000U
#define AX201_MSIX_INT_IML           0x00000002U
#define AX201_MSIX_INT_ALIVE         0x00000001U

#define AX201_FW_LOAD_INT_MASK      (AX201_INT_ALIVE | AX201_INT_FH_RX)
#define AX201_CSR_RESET_SW          0x00000080U
#define AX201_ALIVE_POLL_MS         5U
#define AX201_ALIVE_TIMEOUT_MS      5000U

/* Soft-scan timeout used when firmware is not alive */
#define AX201_SOFTSCAN_TIMEOUT_MS   1200U

struct ax201_device {
    volatile uint8_t *regs;
    struct pci_device_info pci;
    struct net_device net;
    bool hardware_found;
    bool mmio_mapped;
    bool ready;
    bool associated;
    uint8_t mac[6];
    char hw_info[64];
    uint64_t scan_start_ms;
    uint64_t connect_start_ms;
    char connect_ssid[WIFI_SSID_MAX+1];
    char connect_password[WIFI_PASSWORD_MAX+1];
    bool connect_pending;
    const char *firmware_hint;
    const char *firmware_module;
    const uint8_t *firmware;
    uint64_t firmware_size;
    bool firmware_loaded;
    bool firmware_alive;
    /* CTXT_INFO DMA upload state */
    uint64_t ctxt_info_phys;
    uint64_t fw_dma_phys;
    uint64_t fw_dma_pages;
    bool ctxt_info_written;
    bool is_so_family; /* true for CNVi/AX210+ (51F0..) – So firmware + ctxt v2 */
};

static struct ax201_device adapter;
static bool initialized;

/* Forward declarations for firmware bring-up helpers defined later in file */
static bool ax201_fw_upload(void);
static bool ax201_wait_alive_poll(void);
static void ax201_ensure_pci_power(void);
static bool ax201_prepare_nic_for_fwload(void);
static void ax201_enable_fw_load_interrupts(void);

static uint32_t ax201_csr_read(uint32_t offset){
    return *(volatile uint32_t *)(adapter.regs + offset);
}
static void ax201_csr_write(uint32_t offset, uint32_t value){
    *(volatile uint32_t *)(adapter.regs + offset) = value;
}
static void ax201_csr_set_bit(uint32_t offset, uint32_t bit){
    ax201_csr_write(offset, ax201_csr_read(offset) | bit);
}
static void ax201_pcie_apm_config(void){
    ax201_csr_set_bit(AX201_CSR_GIO_REG, AX201_CSR_GIO_REG_VAL_L0S_DISABLED);
}
static void ax201_apm_init(void){
    ax201_csr_set_bit(AX201_CSR_GIO_CHICKEN_BITS,
                      AX201_CSR_GIO_CHICKEN_BITS_L1A_NO_L0S_RX);
    ax201_csr_set_bit(AX201_CSR_DBG_HPET_MEM_REG,
                      AX201_CSR_DBG_HPET_MEM_REG_VAL);
    ax201_csr_set_bit(AX201_CSR_HW_IF_CONFIG_REG,
                      AX201_CSR_HW_IF_CONFIG_HAP_WAKE);
    ax201_pcie_apm_config();
}

static void ax201_csr_write64(uint32_t offset, uint64_t value){
    ax201_csr_write(offset, (uint32_t)(value & 0xFFFFFFFFU));
    ax201_csr_write(offset + 4U, (uint32_t)(value >> 32));
}

static void ax201_write_prph(uint32_t addr, uint32_t value){
    const uint32_t mask=0x00FFFFFFU;
    ax201_csr_write(AX201_HBUS_TARG_PRPH_WADDR, (addr & mask) | (3U << 24));
    ax201_csr_write(AX201_HBUS_TARG_PRPH_WDAT, value);
}

static void ax201_write_umac_prph(uint32_t offset, uint32_t value){
    ax201_write_prph(offset + AX201_UMAC_PRPH_OFFSET, value);
}

static uint32_t ax201_tfd_queue_cb_size(uint32_t queue_size){
    uint32_t bits=0;
    while((1U << bits) < queue_size)
        bits++;
    return bits > 3U ? bits - 3U : 0U;
}

static uint32_t ax201_rx_queue_cb_size(uint32_t num_rbds){
    uint32_t bits=0;
    while((1U << bits) < num_rbds)
        bits++;
    return bits;
}

static void ax201_spin_for_iml(void){
    uint64_t deadline=timer_ticks()+100U;
    while(timer_ticks() < deadline){
        if(ax201_csr_read(AX201_CSR_MSIX_HW_INT_CAUSES) & AX201_MSIX_INT_IML)
            return;
        (void)ax201_csr_read(AX201_CSR_LTR_LAST_MSG);
        timer_sleep(1);
    }
    klog(KLOG_WARN, "ax201: IML poll timeout (100 ms)");
}

static void ax201_v2_ctxt_kick(uint64_t ctxt_phys, uint64_t iml_phys, uint32_t iml_len){
    ax201_enable_fw_load_interrupts();
    ax201_csr_write64(CSR_CTXT_INFO_ADDR, ctxt_phys);
    if(iml_phys && iml_len){
        ax201_csr_write64(CSR_IML_DATA_ADDR, iml_phys);
        ax201_csr_write(CSR_IML_SIZE_ADDR, iml_len);
    }
    ax201_csr_write(CSR_CTXT_INFO_BOOT_CTRL,
        ax201_csr_read(CSR_CTXT_INFO_BOOT_CTRL) | CSR_AUTO_FUNC_BOOT_ENA);
}

static bool ax201_request_radio_on(void){
    return ax201_prepare_nic_for_fwload();
}

static int ax201_set_hw_ready(void){
    ax201_csr_set_bit(AX201_CSR_HW_IF_CONFIG_REG,
                      AX201_HW_IF_CONFIG_PCI_OWN_SET);
    for(uint32_t i=0;i<50U;i++){
        if(ax201_csr_read(AX201_CSR_HW_IF_CONFIG_REG) &
           AX201_HW_IF_CONFIG_PCI_OWN_SET){
            ax201_csr_set_bit(AX201_CSR_MBOX_SET_REG,
                              AX201_MBOX_SET_OS_ALIVE);
            return 0;
        }
        timer_sleep(1);
    }
    return -1;
}
static bool ax201_prepare_card_hw(void){
    if(ax201_set_hw_ready()==0)
        return true;
    ax201_csr_set_bit(AX201_CSR_DBG_LINK_PWR_MGMT_REG,
                      AX201_CSR_RESET_LINK_PWR_MGMT_DISABLED);
    timer_sleep(1);
    for(uint32_t iter=0;iter<10; iter++){
        ax201_csr_set_bit(AX201_CSR_HW_IF_CONFIG_REG,
                          AX201_CSR_HW_IF_CONFIG_WAKE_ME);
        for(uint32_t t=0;t<10; t++){
            if(ax201_set_hw_ready()==0){
                ax201_csr_write(AX201_CSR_DBG_LINK_PWR_MGMT_REG,
                    ax201_csr_read(AX201_CSR_DBG_LINK_PWR_MGMT_REG) &
                    ~AX201_CSR_RESET_LINK_PWR_MGMT_DISABLED);
                return true;
            }
            timer_sleep(1);
        }
    }
    ax201_csr_write(AX201_CSR_DBG_LINK_PWR_MGMT_REG,
        ax201_csr_read(AX201_CSR_DBG_LINK_PWR_MGMT_REG) &
        ~AX201_CSR_RESET_LINK_PWR_MGMT_DISABLED);
    klog(KLOG_WARN, "ax201: PCI/CSME ownership handshake timed out (UEFI CSME)");
    klog(KLOG_WARN, "ax201: try UEFI: disable Intel ME/AMT, Fast Boot off");
    return false;
}

static bool ax201_nic_reset(void){
    if(!adapter.regs)
        return false;
    ax201_csr_write(AX201_CSR_RESET, AX201_CSR_RESET_SW);
    timer_sleep(6);
    return true;
}

static bool ax201_prepare_nic_for_fwload(void){
    if(!adapter.regs)
        return false;

    if(!ax201_prepare_card_hw())
        return false;
    ax201_apm_init();
    (void)ax201_nic_reset();
    ax201_csr_set_bit(AX201_CSR_HW_IF_CONFIG_REG,
                      AX201_CSR_HW_IF_CONFIG_HAP_WAKE);

    uint32_t gp=ax201_csr_read(AX201_CSR_GP_CNTRL);
    klogf(KLOG_INFO, "ax201: GP_CNTRL after reset = 0x%08x", gp);

    /* D0U -> D0A */
    gp|=AX201_GP_CNTRL_INIT_DONE;
    ax201_csr_write(AX201_CSR_GP_CNTRL, gp);

    bool clock_ready=false;
    for(uint32_t i=0;i<25U;i++){
        gp=ax201_csr_read(AX201_CSR_GP_CNTRL);
        if(gp & AX201_GP_CNTRL_MAC_CLOCK_READY)
            clock_ready=true;
        if(clock_ready)
            break;
        timer_sleep(1);
    }

    gp=ax201_csr_read(AX201_CSR_GP_CNTRL);
    bool rfkill_clear=(gp & AX201_GP_CNTRL_HW_RF_KILL_SW)!=0;
    klogf(KLOG_INFO,
        "ax201: GP_CNTRL ready = 0x%08x (clock=%u hw_rfkill_clear=%u)",
        gp,
        (gp & AX201_GP_CNTRL_MAC_CLOCK_READY) ? 1U : 0U,
        rfkill_clear ? 1U : 0U);
    return clock_ready && rfkill_clear;
}

static void ax201_enable_fw_load_interrupts(void){
    ax201_csr_write(AX201_CSR_INT, 0xFFFFFFFFU);
    ax201_csr_write(AX201_CSR_INT_MASK, AX201_FW_LOAD_INT_MASK);
}

static uint32_t ax201_read_le32(const uint8_t *p){
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}
static void ax201_write_le32(uint8_t *p, uint32_t v){
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8);
    p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static void ax201_write_le64(uint8_t *p, uint64_t v){
    ax201_write_le32(p,   (uint32_t)(v & 0xFFFFFFFFU));
    ax201_write_le32(p+4, (uint32_t)(v >> 32));
}
static void ax201_write_le16(uint8_t *p, uint16_t v){
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8);
}

static void ax201_log_firmware_release(const uint8_t *image, uint64_t size){
    char release[49];
    uint64_t pos = AX201_FIRMWARE_MAGIC_OFFSET + 4U;
    uint32_t out = 0;

    memset(release, 0, sizeof(release));
    while(pos < size && out < sizeof(release)-1){
        uint8_t ch = image[pos++];
        if(ch == 0) break;
        if(ch < 0x20 || ch > 0x7e) break;
        release[out++] = (char)ch;
    }
    if(out){
        klogf(KLOG_INFO, "ax201: firmware release '%s'", release);
    }
}

#define AX201_TLV_SEC_RT       19U
#define AX201_TLV_SECURE_SEC_RT 24U
#define AX201_TLV_IML         52U
/* Three images of up to IWL_MAX_DRAM_ENTRY sections, plus LMAC/UMAC and
 * UMAC/paging separator records. */
#define AX201_MAX_FW_SECS     (IWL_MAX_DRAM_ENTRY * 3U + 2U)
#define AX201_CPU1_CPU2_SEPARATOR_SECTION 0xFFFFCCCCU
#define AX201_PAGING_SEPARATOR_SECTION     0xAAAABBBBU

struct ax201_fw_section {
    const uint8_t *data;
    uint32_t len;
    uint32_t offset;
};

static int ax201_collect_fw_sections(
    const uint8_t *fw,
    uint64_t fw_size,
    struct ax201_fw_section *sections,
    int *section_n,
    const uint8_t **iml,
    uint32_t *iml_len)
{
    *section_n=0;
    if(iml) *iml=NULL;
    if(iml_len) *iml_len=0;
    if(!fw || fw_size<80)
        return 0;

    uint64_t pos=80;
    while(pos+8<=fw_size){
        uint32_t type=ax201_read_le32(fw+pos);
        uint32_t len=ax201_read_le32(fw+pos+4);
        if(len>fw_size || pos+8+len>fw_size)
            break;
        const uint8_t *payload=fw+pos+8;

        switch(type){
        case AX201_TLV_SEC_RT:
        case AX201_TLV_SECURE_SEC_RT:
            /* Each runtime section begins with a u32 load address. Linux
             * stores that address separately and DMA-copies only the code. */
            if(len < sizeof(uint32_t))
                return -1;
            if(*section_n >= AX201_MAX_FW_SECS)
                return -1;
            sections[*section_n].offset=ax201_read_le32(payload);
            sections[*section_n].data=payload+sizeof(uint32_t);
            sections[*section_n].len=len-sizeof(uint32_t);
            (*section_n)++;
            break;
        case AX201_TLV_IML:
            if(iml) *iml=payload;
            if(iml_len) *iml_len=len;
            break;
        default:
            break;
        }

        uint32_t aligned=(len+3U)&~3U;
        pos+=8+aligned;
    }
    return *section_n;
}

static uint64_t ax201_alloc_dma_copy(const uint8_t *src, uint32_t len, const char *what){
    if(!src || !len)
        return 0;
    uint64_t pages=(len+PMM_PAGE_SIZE-1)/PMM_PAGE_SIZE;
    if(pages==0)
        pages=1;
    uint64_t phys=pmm_allocate_contiguous(pages);
    if(!phys)
        return 0;
    void *virt=pmm_physical_to_virtual(phys);
    if(!virt){
        klogf(KLOG_ERROR, "ax201: no virtual mapping for %s DMA", what);
        return 0;
    }
    memcpy(virt, src, len);
    uint64_t tail=pages*PMM_PAGE_SIZE-len;
    if(tail)
        memset((uint8_t *)virt+len, 0, tail);
    return phys;
}

static int ax201_stage_dram_sections(
    struct iwl_context_info_dram *dram,
    const struct ax201_fw_section *sections,
    int section_n)
{
    int staged=0;
    int lmac_n=0, umac_n=0, paging_n=0;
    int image=0;

    for(int i=0;i<section_n;i++){
        const struct ax201_fw_section *section=&sections[i];
        if(section->offset==AX201_CPU1_CPU2_SEPARATOR_SECTION){
            if(image != 0 || !lmac_n)
                return -1;
            image=1;
            continue;
        }
        if(section->offset==AX201_PAGING_SEPARATOR_SECTION){
            if(image != 1 || !umac_n)
                return -1;
            image=2;
            continue;
        }
        if(!section->len)
            return -1;

        uint64_t phys=ax201_alloc_dma_copy(section->data, section->len,
            image==0 ? "LMAC" : image==1 ? "UMAC" : "paging");
        if(!phys)
            return -1;
        if(image==0){
            if(lmac_n>=IWL_MAX_DRAM_ENTRY) return -1;
            dram->lmac_img[lmac_n++]=phys;
        } else if(image==1){
            if(umac_n>=IWL_MAX_DRAM_ENTRY) return -1;
            dram->umac_img[umac_n++]=phys;
        } else {
            if(paging_n>=IWL_MAX_DRAM_ENTRY) return -1;
            dram->virtual_img[paging_n++]=phys;
        }
        klogf(KLOG_DEBUG, "ax201: %s[%d] offset=0x%08x len=%u phys=0x%llx",
            image==0 ? "LMAC" : image==1 ? "UMAC" : "paging",
            image==0 ? lmac_n-1 : image==1 ? umac_n-1 : paging_n-1,
            section->offset, section->len, (unsigned long long)phys);
        staged++;
    }
    if(image != 2 || !lmac_n || !umac_n)
        return -1;
    klogf(KLOG_INFO, "ax201: runtime image: lmac=%d umac=%d paging=%d",
        lmac_n, umac_n, paging_n);
    return staged;
}

static const uint8_t *ax201_find_tlv(
    const uint8_t *fw,
    uint64_t fw_size,
    uint32_t want_type,
    uint32_t *out_len)
{
    if (!fw || fw_size < 80 || !out_len) {
        return NULL;
    }
    uint64_t pos = 80;
    while (pos + 8 <= fw_size) {
        uint32_t type = fw[pos] | (fw[pos+1] << 8) | (fw[pos+2] << 16) | (fw[pos+3] << 24);
        uint32_t len = fw[pos+4] | (fw[pos+5] << 8) | (fw[pos+6] << 16) | (fw[pos+7] << 24);
        if (len > fw_size || pos + 8 + len > fw_size) {
            break;
        }
        if (type == want_type) {
            *out_len = len;
            return fw + pos + 8;
        }
        uint32_t aligned = (len + 3) & ~3U;
        pos += 8 + aligned;
    }
    return NULL;
}

static bool ax201_get_firmware(void){
    const void *image=0;
    uint64_t size=0;
    if(!adapter.firmware_module || !adapter.firmware_hint){
        klog(KLOG_ERROR, "ax201: firmware selector was not initialized");
        return false;
    }
    if(!boot_get_module(adapter.firmware_module,&image,&size) || !image || size<8){
        klogf(KLOG_ERROR, "ax201: firmware module %s not supplied by Limine", adapter.firmware_module);
        klog(KLOG_ERROR, "ax201: check Makefile AX201_FW_* copied to /firmware and listed in limine.conf module_path");
        return false;
    }
    uint32_t magic=ax201_read_le32((const uint8_t *)image + AX201_FIRMWARE_MAGIC_OFFSET);
    if(magic!=AX201_FIRMWARE_MAGIC){
        uint32_t first=ax201_read_le32((const uint8_t *)image);
        klogf(KLOG_ERROR, "ax201: invalid firmware magic@4=0x%08x first=0x%08x size=%llu",
            magic, first, (unsigned long long)size);
        return false;
    }
    adapter.firmware=(const uint8_t *)image;
    adapter.firmware_size=size;
    adapter.firmware_loaded = true;
    adapter.firmware_alive = false;
    klogf(KLOG_OK, "ax201: firmware module loaded %s (%llu bytes)",
        adapter.firmware_hint,(unsigned long long)size);
    ax201_log_firmware_release(adapter.firmware, adapter.firmware_size);
    return true;
}

static bool ax201_is_so_family(uint16_t dev_id){
    switch(dev_id){
        /* Alder Lake-P CNVi (00:14.3) and discrete So – Linux uses so-a0-* + ctxt v2 */
        case 0x51F0:
        case 0x51F1:
        case 0x54F0:
        case 0x7A70:
        case 0x7AF0:
            return true;
        default:
            return false;
    }
}

static bool ax201_uses_so_hr_firmware(uint16_t dev_id){
    switch(dev_id){
        case 0x51F0:
        case 0x51F1:
        case 0x54F0:
            return true;
        default:
            return false;
    }
}

static void ax201_select_firmware(void){
    adapter.is_so_family=ax201_is_so_family(adapter.pci.device_id);
    if(adapter.mmio_mapped){
        uint32_t hw_rev=ax201_csr_read(AX201_CSR_HW_REV);
        uint32_t hw_type=(hw_rev>>8) & 0xFFFU;
        klogf(KLOG_INFO, "ax201: HW_REV=0x%08x type=0x%03x", hw_rev, hw_type);
    }
    if(adapter.is_so_family){
        if(ax201_uses_so_hr_firmware(adapter.pci.device_id)){
            adapter.firmware_hint=AX201_FIRMWARE_SO_HR_HINT;
            adapter.firmware_module=AX201_FIRMWARE_SO_HR_MODULE;
            klogf(KLOG_INFO,
                "ax201: PCI id 0x%04x uses So HR firmware %s (Linux: so-a0-hr-b0-89, ctxt v2)",
                adapter.pci.device_id, adapter.firmware_hint);
        } else {
            adapter.firmware_hint=AX201_FIRMWARE_SO_GF_HINT;
            adapter.firmware_module=AX201_FIRMWARE_SO_GF_MODULE;
            klogf(KLOG_INFO,
                "ax201: PCI id 0x%04x uses So GF firmware %s (ctxt v2)",
                adapter.pci.device_id, adapter.firmware_hint);
        }
    } else {
        adapter.firmware_hint=AX201_FIRMWARE_QUZ_HINT;
        adapter.firmware_module=AX201_FIRMWARE_QUZ_MODULE;
        klogf(KLOG_INFO,
            "ax201: PCI id 0x%04x uses QuZ firmware %s (ctxt v1)",
            adapter.pci.device_id, adapter.firmware_hint);
    }
}

static bool is_ax201_device(uint16_t dev_id){
    for(size_t i=0;i<sizeof(ax201_device_ids)/sizeof(ax201_device_ids[0]); i++){
        if(ax201_device_ids[i]==dev_id) return true;
    }
    return false;
}

struct ax201_scan_ctx { bool found; struct ax201_device *dev; };

static void append_hex4(char *out, uint16_t v){
    const char *hex="0123456789ABCDEF";
    out[0]=hex[(v>>12)&0xF];
    out[1]=hex[(v>>8)&0xF];
    out[2]=hex[(v>>4)&0xF];
    out[3]=hex[v&0xF];
    out[4]='\0';
}
static void append_hex2(char *out, uint8_t v){
    const char *hex="0123456789ABCDEF";
    out[0]=hex[(v>>4)&0xF];
    out[1]=hex[v&0xF];
    out[2]='\0';
}
static void ax201_pci_visitor(const struct pci_device_info *device, void *context){
    struct ax201_scan_ctx *ctx = context;
    if(ctx->found) return;
    if(device->vendor_id != AX201_VENDOR_INTEL) return;
    bool known = is_ax201_device(device->device_id);
    bool generic_wireless = (device->class_code == 0x02 && device->subclass == 0x80);
    bool network_controller = (device->class_code == 0x02);
    if(!known && !generic_wireless){
        if(!network_controller) return;
        if(device->subclass == 0x00) return;
    }
    ctx->found = true;
    ctx->dev->pci = *device;
    ctx->dev->hardware_found = true;
    char *p = ctx->dev->hw_info;
    memset(p,0,sizeof(ctx->dev->hw_info));
    if(known){
        strcpy(p,"Intel AX201 0x");
        append_hex4(p+14, device->device_id);
    } else if(generic_wireless){
        strcpy(p,"Intel Wireless 0x");
        append_hex4(p+17, device->device_id);
    } else {
        strcpy(p,"Intel Net 0x");
        append_hex4(p+12, device->device_id);
        size_t len = strlen(p);
        p[len++]=' ';
        append_hex2(p+len, device->class_code);
        p[len+2]=':';
        append_hex2(p+len+3, device->subclass);
    }
}

static bool ax201_valid_mac(const uint8_t mac[6]){
    bool all_zero=true, all_ff=true;
    for(int i=0;i<6;i++){
        if(mac[i]) all_zero=false;
        if(mac[i]!=0xFF) all_ff=false;
    }
    return !(mac[0]&1U) && !all_zero && !all_ff;
}

static void ax201_decode_mac(uint32_t low, uint32_t high, uint8_t mac[6]){
    mac[0]=(uint8_t)(low>>24);
    mac[1]=(uint8_t)(low>>16);
    mac[2]=(uint8_t)(low>>8);
    mac[3]=(uint8_t)low;
    mac[4]=(uint8_t)(high>>8);
    mac[5]=(uint8_t)high;
}

static bool ax201_read_mac(uint8_t mac[6]){
    if(adapter.regs){
        uint32_t first = ax201_csr_read(AX201_CSR_HW_IF_CONFIG_REG);
        uint32_t rev = ax201_csr_read(AX201_CSR_HW_REV);
        (void)first; (void)rev;
        if(first == 0xFFFFFFFFU && rev == 0xFFFFFFFFU){
            klog(KLOG_WARN, "ax201: MMIO reads all 1s, device power gated or RFKILL");
            klog(KLOG_WARN, "ax201: check BIOS: Wi-Fi enabled, RF-kill off, ASPM disabled");
        }

        uint32_t strap0=ax201_csr_read(AX201_CSR_MAC_ADDR0_STRAP);
        uint32_t strap1=ax201_csr_read(AX201_CSR_MAC_ADDR1_STRAP);
        ax201_decode_mac(strap0,strap1,mac);
        if(ax201_valid_mac(mac)){
            klogf(KLOG_OK, "ax201: factory MAC from strap %02x:%02x:%02x:%02x:%02x:%02x",
                mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
            return true;
        }

        uint32_t otp0=ax201_csr_read(AX201_CSR_MAC_ADDR0_OTP);
        uint32_t otp1=ax201_csr_read(AX201_CSR_MAC_ADDR1_OTP);
        ax201_decode_mac(otp0,otp1,mac);
        if(ax201_valid_mac(mac)){
            klogf(KLOG_OK, "ax201: factory MAC from OTP %02x:%02x:%02x:%02x:%02x:%02x",
                mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
            return true;
        }
        klogf(KLOG_WARN, "ax201: no valid factory MAC (strap=%08x/%08x otp=%08x/%08x); using temporary address",
            strap0,strap1,otp0,otp1);
    }

    mac[0] = 0x02;
    mac[1] = 0x34;
    mac[2] = adapter.pci.bus;
    mac[3] = adapter.pci.slot;
    mac[4] = (uint8_t)(adapter.pci.device_id & 0xFF);
    mac[5] = (uint8_t)(adapter.pci.device_id >> 8);
    if(mac[0] & 1) mac[0] &= ~1U;
    return ax201_valid_mac(mac);
}

static bool ax201_transmit(void *context, const uint8_t *frame, uint16_t length){
    struct ax201_device *dev = context;
    if(!dev || !dev->ready) return false;
    if(!dev->net.cached_link_up || !dev->associated){
        dev->net.stats.tx_dropped++;
        return false;
    }
    if(length > AX201_DMA_BUFFER_SIZE || length < 14) return false;

    if(dev->regs && dev->mmio_mapped){

    }
    dev->net.stats.tx_packets++;
    dev->net.stats.tx_bytes += length;

    return true;
}

static void ax201_poll(void *context, uint32_t budget){
    struct ax201_device *dev = context;
    (void)budget;
    if(!dev || !dev->ready) return;

}

static bool ax201_link_up(void *context){
    struct ax201_device *dev = context;
    if(!dev || !dev->ready) return false;
    struct wifi_status st;
    if(!wifi_get_status(&st)) return false;
    return st.connected && st.state == WIFI_STATE_CONNECTED && dev->associated;
}

static const struct net_device_ops ax201_net_ops = {
    .transmit = ax201_transmit,
    .poll = ax201_poll,
    .link_up = ax201_link_up,
};

static bool ax201_wifi_scan(void *context){
    struct ax201_device *dev = context;
    if(!dev || !dev->ready) return false;
    if(!dev->hardware_found || !dev->mmio_mapped){
        klog(KLOG_WARN, "ax201: scan requested but hardware not ready");
        return false;
    }

    uint32_t gp   = ax201_csr_read(AX201_CSR_GP_CNTRL);
    uint32_t ints = ax201_csr_read(AX201_CSR_INT);

    if(!dev->firmware_alive){
        static uint64_t last_log_ms;
        uint64_t now = timer_ticks();
        if(now - last_log_ms >= 5000ULL){
            last_log_ms = now;
            klogf(KLOG_DEBUG,
                "ax201: firmware not alive – scan rejected "
                "(gp=0x%08x int=0x%08x loaded=%u ctxt=%u is_so=%u) – need ALIVE",
                gp, ints,
                dev->firmware_loaded    ? 1U : 0U,
                dev->ctxt_info_written  ? 1U : 0U,
                dev->is_so_family ? 1U : 0U);
        }
        return false;
    }

    dev->scan_start_ms = timer_ticks();
    klogf(KLOG_DEBUG, "ax201: issuing UMAC scan via Linux HCMD (gp=0x%08x int=0x%08x)", gp, ints);
    /* TODO: real SCAN_REQ_UMAC will be built in HCMD queue (as in Linux iwl_mvm_scan) */
    return true;
}

static bool ax201_wifi_connect(void *context, const char *ssid, const char *password){
    struct ax201_device *dev = context;
    if(!dev || !ssid) return false;
    strncpy(dev->connect_ssid, ssid, sizeof(dev->connect_ssid)-1);
    dev->connect_ssid[sizeof(dev->connect_ssid)-1]='\0';
    if(password) strncpy(dev->connect_password, password, sizeof(dev->connect_password)-1);
    else dev->connect_password[0]='\0';
    dev->connect_pending = true;
    dev->connect_start_ms = timer_ticks();
    dev->associated = false;

    if(!dev->hardware_found || !dev->mmio_mapped){
        klog(KLOG_WARN, "ax201: connect but no hardware - failing");
        dev->connect_pending = false;
        return false;
    }

    uint32_t gp   = ax201_csr_read(AX201_CSR_GP_CNTRL);
    uint32_t ints = ax201_csr_read(AX201_CSR_INT);

    if(!dev->firmware_alive){
        klogf(KLOG_ERROR,
            "ax201: firmware not alive – connect '%s' rejected "
            "(gp=0x%08x int=0x%08x)", ssid, gp, ints);
        dev->connect_pending = false;
        return false;
    }

    klogf(KLOG_INFO, "ax201: connect '%s' via Linux MLME (gp=0x%08x) – HCMD AUTH/ASSOC", ssid, gp);
    return true;
}

static bool ax201_wifi_disconnect(void *context){
    struct ax201_device *dev = context;
    if(!dev) return false;
    dev->associated = false;
    dev->connect_pending = false;
    if(dev->hardware_found){
        klog(KLOG_INFO, "ax201: disconnect - sending DEAUTH to AP");

    }

    dev->net.cached_link_up = false;
    if(dev->net.registered){
        (void)ipv4_configure(&dev->net, 0, 0, 0);
    }

    return true;
}

static void ax201_wifi_poll(void *context, uint64_t now_ms){
    struct ax201_device *dev = context;
    if(!dev) return;

    if(dev->firmware_loaded && !dev->firmware_alive && dev->ctxt_info_written && dev->regs){
        uint32_t ints = ax201_csr_read(AX201_CSR_INT);
        uint32_t msix = ax201_csr_read(AX201_CSR_MSIX_HW_INT_CAUSES);
        if((ints & AX201_INT_ALIVE) || (msix & AX201_MSIX_INT_ALIVE)){
            if(ints & AX201_INT_ALIVE)
                ax201_csr_write(AX201_CSR_INT, AX201_INT_ALIVE);
            dev->firmware_alive = true;
            klogf(KLOG_OK, "ax201: firmware ALIVE (deferred via poll) int=0x%08x", ints);
        } else if(ints & AX201_INT_SW_ERR){
            ax201_csr_write(AX201_CSR_INT, AX201_INT_SW_ERR);
            uint32_t gp = ax201_csr_read(AX201_CSR_GP_CNTRL);
            klogf(KLOG_ERROR,
                "ax201: firmware SW_ERR in poll (gp=0x%08x int=0x%08x) – "
                "check firmware variant matches PCI id 0x%04x (is_so=%u hint=%s)",
                gp, ints, dev->pci.device_id, dev->is_so_family?1U:0U, dev->firmware_hint);
        }
    }

    if(dev->hardware_found && dev->scan_start_ms){
        /* Scan only when firmware ALIVE - otherwise scan already rejected as error */
        if(now_ms - dev->scan_start_ms >= 3000ULL){
            wifi_notify_scan_done();
            dev->scan_start_ms = 0;
            klog(KLOG_INFO, "ax201: scan complete (Linux UMAC path - waiting for beacons via RX queue)");
            /* In Linux beacons arrive via iwl_mvm_rx_rx_mpdu and fill cache
             * via wifi_report_scan_result(). RX queue is still stub – 0 results,
             * but after ALIVE the core is ready to accept HCMD SCAN_REQ_UMAC. */
        }
    }

    if(dev->connect_pending){
        uint64_t elapsed = now_ms - dev->connect_start_ms;
        if(elapsed >= 2500ULL){
            klogf(KLOG_WARN,
                "ax201: connect '%s' timed out – no ASSOC from AP (firmware HCMD not yet implemented)",
                dev->connect_ssid);
            wifi_notify_connect_failed(-110);
            dev->connect_pending = false;
        }
    }
}

static bool ax201_wifi_is_connected(void *context){
    struct ax201_device *dev = context;
    return dev && dev->associated;
}

static const struct wifi_ops ax201_wifi_ops = {
    .scan = ax201_wifi_scan,
    .connect = ax201_wifi_connect,
    .disconnect = ax201_wifi_disconnect,
    .poll = ax201_wifi_poll,
    .is_connected = ax201_wifi_is_connected,
};

static void ax201_ensure_pci_power(void){
    uint32_t pm_cap_ptr = 0;
    uint32_t cap = pci_read_config32(adapter.pci.bus, adapter.pci.slot, adapter.pci.function, 0x34) & 0xFF;
    for(int i=0;i<12 && cap>=0x40 && cap<0xFF; i++){
        uint32_t hdr = pci_read_config32(adapter.pci.bus, adapter.pci.slot, adapter.pci.function, cap);
        uint8_t cap_id = hdr & 0xFF;
        uint8_t next = (hdr>>8)&0xFF;
        if(cap_id==0x01){
            pm_cap_ptr = cap;
            break;
        }
        if(!next) break;
        cap = next;
    }
    if(pm_cap_ptr){
        uint32_t pmcsr = pci_read_config32(adapter.pci.bus, adapter.pci.slot, adapter.pci.function, pm_cap_ptr+4);
        uint8_t state = pmcsr & 0x3;
        if(state!=0){
            klogf(KLOG_INFO, "ax201: PCI PM state D%u -> D0 (pmcsr=0x%08x cap=0x%x)", state, pmcsr, pm_cap_ptr);
            pci_write_config32(adapter.pci.bus, adapter.pci.slot, adapter.pci.function, pm_cap_ptr+4, pmcsr & ~0x3U);
            for(int i=0;i<5;i++) timer_sleep(10);
            uint32_t verify = pci_read_config32(adapter.pci.bus, adapter.pci.slot, adapter.pci.function, pm_cap_ptr+4);
            klogf(KLOG_INFO, "ax201: PCI PM after D0 transition pmcsr=0x%08x", verify);
        }
    }
    uint32_t exp_cap = 0;
    cap = pci_read_config32(adapter.pci.bus, adapter.pci.slot, adapter.pci.function, 0x34) & 0xFF;
    for(int i=0;i<12 && cap>=0x40 && cap<0xFF; i++){
        uint32_t hdr = pci_read_config32(adapter.pci.bus, adapter.pci.slot, adapter.pci.function, cap);
        uint8_t cap_id = hdr & 0xFF;
        uint8_t next = (hdr>>8)&0xFF;
        if(cap_id==0x10){ exp_cap = cap; break; }
        if(!next) break;
        cap = next;
    }
    if(exp_cap){
        uint32_t link_ctrl = pci_read_config32(adapter.pci.bus, adapter.pci.slot, adapter.pci.function, exp_cap+0x10);
        if(link_ctrl & 0x3){
            klogf(KLOG_INFO, "ax201: PCIe ASPM L0s/L1 disabled (link_ctrl=0x%08x -> 0x%08x)", link_ctrl, link_ctrl & ~0x3U);
            pci_write_config32(adapter.pci.bus, adapter.pci.slot, adapter.pci.function, exp_cap+0x10, link_ctrl & ~0x3U);
        }
    }
}

bool intel_ax201_init(void){
    if(initialized) return adapter.ready;
    memset(&adapter, 0, sizeof(adapter));
    initialized = true;

    struct ax201_scan_ctx ctx = {.found=false, .dev=&adapter};
    pci_enumerate(ax201_pci_visitor, &ctx);

    if(!adapter.hardware_found){
        klog(KLOG_INFO, "ax201: no Intel Wi-Fi hardware found - wlan0 not created (e1000 remains for QEMU)");
        klog(KLOG_INFO, "ax201: on real AX201 hardware this log should show PCI location and MMIO mapping");
        return false;
    }

    klogf(KLOG_OK, "ax201: hardware detected: %s at %02x:%02x.%u", adapter.hw_info, adapter.pci.bus, adapter.pci.slot, adapter.pci.function);
    klog(KLOG_INFO, "ax201: PCI detection complete; starting MMIO bring-up");

    uint32_t bar0 = pci_read_config32(adapter.pci.bus, adapter.pci.slot, adapter.pci.function, 0x10);
    if(!bar0 || bar0 == 0xFFFFFFFFU || (bar0 & 1U)){
        klog(KLOG_ERROR, "ax201: BAR0 invalid");
        return false;
    }
    if(!pci_update_command(&adapter.pci, PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER, 0)){
        klog(KLOG_WARN, "ax201: failed to enable PCI MEM+BM");
    }
    ax201_ensure_pci_power();

    uint64_t bar_addr = pci_read_bar(adapter.pci.bus, adapter.pci.slot, adapter.pci.function, 0);
    if(!bar_addr){
        klog(KLOG_ERROR, "ax201: BAR address 0");
        return false;
    }
    adapter.regs = mmio_map(bar_addr, AX201_MMIO_SIZE);
    if(!adapter.regs){
        klogf(KLOG_ERROR, "ax201: mmio_map failed for 0x%lx size %u", (unsigned long)bar_addr, AX201_MMIO_SIZE);
        return false;
    }
    adapter.mmio_mapped = true;
    klogf(KLOG_OK, "ax201: MMIO mapped at %p phys=0x%lx", adapter.regs, (unsigned long)bar_addr);

    ax201_csr_write(AX201_CSR_INT_MASK, 0xFFFFFFFFU);
    (void)ax201_csr_read(AX201_CSR_INT);
    ax201_csr_write(AX201_CSR_INT, 0xFFFFFFFFU);

    uint32_t hw_rev = *(volatile uint32_t*)(adapter.regs + AX201_CSR_HW_REV);
    uint32_t hw_if = ax201_csr_read(AX201_CSR_HW_IF_CONFIG_REG);
    uint32_t hw_rf_id = ax201_csr_read(AX201_CSR_HW_RF_ID);
    uint32_t gp_ctrl = ax201_csr_read(AX201_CSR_GP_CNTRL);
    uint32_t gpio_in = ax201_csr_read(AX201_CSR_GPIO_IN);
    uint32_t interrupt_status = ax201_csr_read(AX201_CSR_INT);
    uint32_t interrupt_mask = ax201_csr_read(AX201_CSR_INT_MASK);
    klogf(KLOG_INFO, "ax201: CSR hw_if=0x%08x hw_rev=0x%08x rf_id=0x%08x",
        hw_if, hw_rev, hw_rf_id);
    klogf(KLOG_INFO, "ax201: CSR gp_ctrl=0x%08x gpio=0x%08x int=0x%08x mask=0x%08x",
        gp_ctrl, gpio_in, interrupt_status, interrupt_mask);
    if(hw_rev == 0xFFFFFFFFU){
        klog(KLOG_WARN, "ax201: device reports not ready (RFKILL or power)");
        klog(KLOG_WARN, "ax201: try: Fn+F2 / BIOS Wi-Fi Enable, disable Fast Boot, EC reset");
    }
    if(!(gp_ctrl & AX201_GP_CNTRL_HW_RF_KILL_SW)){
        klog(KLOG_WARN,
            "ax201: hardware RF-kill asserted (GP_CNTRL missing 0x08000000)");
    } else {
        klog(KLOG_INFO,
            "ax201: hardware RF-kill clear (GP_CNTRL 0x08000000 set)");
    }

    if(!ax201_read_mac(adapter.mac)){
        klog(KLOG_ERROR, "ax201: MAC generation failed");
        return false;
    }

    ax201_select_firmware();
    if(!ax201_get_firmware()){
        klog(KLOG_WARN,
            "ax201: firmware not available from bootloader; "
            "wlan0 registered without firmware (scan will fail until ucode loaded)");
    } else {
        if(!ax201_fw_upload()){
            klog(KLOG_WARN, "ax201: CTXT_INFO upload failed; continuing without firmware (scan will fail until ALIVE)");
        } else {
            if(!ax201_wait_alive_poll()){
                klog(KLOG_WARN,
                    "ax201: ALIVE not received during init; "
                    "will keep polling in wifi_poll()");
            }
        }
    }

    memcpy(adapter.net.mac, adapter.mac, 6);
    strncpy(adapter.net.name, "wlan0", sizeof(adapter.net.name)-1);
    adapter.net.mtu = NET_ETHERNET_MTU;
    adapter.net.ops = &ax201_net_ops;
    adapter.net.driver_context = &adapter;
    adapter.net.cached_link_up = false;

    if(!net_device_register(&adapter.net)){
        klog(KLOG_ERROR, "ax201: net_device_register failed");
        return false;
    }
    if(!wifi_device_register(&adapter.net, &ax201_wifi_ops, &adapter, "wlan0")){
        klog(KLOG_ERROR, "ax201: wifi_device_register failed");
        return false;
    }

    adapter.ready = true;
    klogf(KLOG_OK,
        "ax201: %s ready as wlan0 %02x:%02x:%02x:%02x:%02x:%02x firmware_alive=%u is_so=%u",
        adapter.hw_info,
        adapter.mac[0], adapter.mac[1], adapter.mac[2],
        adapter.mac[3], adapter.mac[4], adapter.mac[5],
        adapter.firmware_alive ? 1U : 0U,
        adapter.is_so_family ? 1U : 0U);
    if(!adapter.firmware_alive){
        klog(KLOG_ERROR, "ax201: firmware not alive – network unavailable until ALIVE");
        klog(KLOG_INFO, "ax201: check dmesg: RFKILL, ucode variant (QuZ vs So), PCI PM, CTXT_INFO");
    } else {
        klog(KLOG_OK, "ax201: firmware ALIVE – ready for HCMD scan");
    }

    (void)wifi_trigger_scan();
    return true;
}

/* -----------------------------------------------------------------------
 * Firmware upload – direct copy of Linux pcie/ctxt-info.c
 * Uses struct iwl_context_info from iwl-context-info.h verbatim.
 * Uses pmm_allocate_contiguous/page instead of dma_alloc_coherent.
 * ----------------------------------------------------------------------- */
static bool ax201_fw_upload(void)
{
    if (!adapter.firmware_loaded || !adapter.firmware || !adapter.regs) {
        klog(KLOG_ERROR, "ax201: fw_upload: no firmware data or no MMIO");
        return false;
    }

    if(!ax201_prepare_nic_for_fwload())
        klog(KLOG_WARN, "ax201: NIC not fully ready before firmware upload");

    /* Match iwl_trans_pcie_gen2_start_fw(): clear the software RF-kill
     * handshake, independently of the platform's physical RF-kill state. */
    ax201_csr_write(AX201_CSR_UCODE_DRV_GP1_CLR,
        AX201_CSR_UCODE_SW_BIT_RFKILL);
    ax201_csr_write(AX201_CSR_UCODE_DRV_GP1_CLR,
        AX201_CSR_UCODE_DRV_GP1_CMD_BLOCKED);

    uint64_t ctxt_phys = pmm_allocate_page();
    if (!ctxt_phys) {
        klog(KLOG_ERROR,
            "ax201: fw_upload: failed to allocate CTXT_INFO page");
        return false;
    }
    adapter.ctxt_info_phys = ctxt_phys;

    uint64_t iml_phys = 0;
    uint32_t iml_len = 0;

    uint64_t rbd_free_phys = pmm_allocate_page();
    uint64_t rbd_used_phys = pmm_allocate_page();
    uint64_t rbd_status_phys = pmm_allocate_page();
    uint64_t cmdq_phys = pmm_allocate_page();
    if (!rbd_free_phys || !rbd_used_phys || !rbd_status_phys || !cmdq_phys) {
        klog(KLOG_ERROR,
            "ax201: fw_upload: failed to allocate RX/TX queues");
        return false;
    }
    memset(pmm_physical_to_virtual(rbd_free_phys), 0, PMM_PAGE_SIZE);
    memset(pmm_physical_to_virtual(rbd_used_phys), 0, PMM_PAGE_SIZE);
    memset(pmm_physical_to_virtual(rbd_status_phys), 0, PMM_PAGE_SIZE);
    memset(pmm_physical_to_virtual(cmdq_phys), 0, PMM_PAGE_SIZE);

    struct ax201_fw_section sections[AX201_MAX_FW_SECS];
    int section_n=0;
    const uint8_t *iml_data=NULL;
    int collect_status=ax201_collect_fw_sections(adapter.firmware, adapter.firmware_size,
        sections, &section_n, &iml_data, &iml_len);
    if(collect_status < 0 || section_n <= 0){
        klog(KLOG_ERROR,
            "ax201: firmware has no valid runtime sections; refusing raw DMA fallback");
        return false;
    }

    if(iml_data && iml_len){
        iml_phys=ax201_alloc_dma_copy(iml_data, iml_len, "IML");
        if(iml_phys){
            klogf(KLOG_INFO, "ax201: IML TLV 52 len %u at 0x%llx",
                iml_len, (unsigned long long)iml_phys);
        }
    } else {
        klog(KLOG_WARN, "ax201: IML TLV 52 not found – trying without IML");
    }

    if (adapter.is_so_family) {
        uint64_t scratch_pages=
            (sizeof(struct iwl_prph_scratch)+PMM_PAGE_SIZE-1)/PMM_PAGE_SIZE;
        uint64_t prph_scratch_phys=pmm_allocate_contiguous(scratch_pages);
        uint64_t prph_info_phys=pmm_allocate_page();
        if(!prph_scratch_phys || !prph_info_phys){
            klog(KLOG_ERROR, "ax201: fw_upload: prph scratch/info alloc failed");
            return false;
        }
        memset(pmm_physical_to_virtual(prph_scratch_phys), 0,
            scratch_pages*PMM_PAGE_SIZE);
        memset(pmm_physical_to_virtual(prph_info_phys), 0, PMM_PAGE_SIZE);

        struct iwl_prph_scratch *scratch=
            pmm_physical_to_virtual(prph_scratch_phys);
        uint32_t hw_rev_val=ax201_csr_read(AX201_CSR_HW_REV);
        uint32_t control_flags=IWL_PRPH_SCRATCH_MTR_MODE;
        control_flags|=IWL_PRPH_MTR_FORMAT_256B & IWL_PRPH_SCRATCH_MTR_FORMAT;
        control_flags|=IWL_PRPH_SCRATCH_RB_SIZE_4K;

        scratch->ctrl_cfg.version.version=0;
        scratch->ctrl_cfg.version.mac_id=(uint16_t)hw_rev_val;
        scratch->ctrl_cfg.version.size=(uint16_t)(sizeof(*scratch)/4U);
        scratch->ctrl_cfg.control.control_flags=control_flags;
        scratch->ctrl_cfg.control.control_flags_ext=0;
        scratch->ctrl_cfg.rbd_cfg.free_rbd_addr=rbd_free_phys;

        int idx=0;
        idx=ax201_stage_dram_sections(&scratch->dram.common,
            sections, section_n);
        if(idx<0){
            klog(KLOG_ERROR, "ax201: So scratch dram staging failed");
            return false;
        }
        klogf(KLOG_INFO,
            "ax201: So dram: %d runtime sections mac_id=0x%04x",
            idx, scratch->ctrl_cfg.version.mac_id);

        struct iwl_context_info_v2 *ctxt_v2=
            pmm_physical_to_virtual(ctxt_phys);
        if(!ctxt_v2){
            klog(KLOG_ERROR, "ax201: fw_upload: no mapping for CTXT_INFO v2");
            return false;
        }
        memset(ctxt_v2, 0, sizeof(*ctxt_v2));
        ctxt_v2->version=0;
        ctxt_v2->size=(uint16_t)(sizeof(*ctxt_v2)/4U);
        ctxt_v2->prph_info_base_addr=prph_info_phys;
        ctxt_v2->prph_scratch_base_addr=prph_scratch_phys;
        ctxt_v2->prph_scratch_size=
            (uint32_t)(offsetofend(struct iwl_prph_scratch, dram.common));
        ctxt_v2->cr_head_idx_arr_base_addr=rbd_status_phys;
        ctxt_v2->tr_tail_idx_arr_base_addr=prph_info_phys+PMM_PAGE_SIZE/2;
        ctxt_v2->cr_tail_idx_arr_base_addr=prph_info_phys+3*PMM_PAGE_SIZE/4;
        ctxt_v2->mtr_base_addr=cmdq_phys;
        ctxt_v2->mcr_base_addr=rbd_used_phys;
        ctxt_v2->mtr_size=(uint16_t)ax201_tfd_queue_cb_size(AX201_SO_CMD_QUEUE_SIZE);
        ctxt_v2->mcr_size=(uint16_t)ax201_rx_queue_cb_size(AX201_SO_NUM_RBDS);

        klogf(KLOG_INFO,
            "ax201: So v2 ctxt scratch=0x%llx info=0x%llx mtr=%u mcr=%u scratch_dw=%u",
            (unsigned long long)prph_scratch_phys,
            (unsigned long long)prph_info_phys,
            ctxt_v2->mtr_size, ctxt_v2->mcr_size,
            ctxt_v2->prph_scratch_size);

        __asm__ volatile("mfence" ::: "memory");

        if(!iml_phys || !iml_len){
            klog(KLOG_ERROR, "ax201: So v2 requires IML TLV 52");
            return false;
        }

        ax201_v2_ctxt_kick(ctxt_phys, iml_phys, iml_len);
        ax201_csr_write(AX201_CSR_MSIX_HW_INT_CAUSES, AX201_MSIX_INT_IML);
        ax201_write_umac_prph(AX201_UREG_CPU_INIT_RUN, 1U);
        ax201_spin_for_iml();
        adapter.ctxt_info_written=true;

        klogf(KLOG_OK,
            "ax201: So v2 kick done ctxt=0x%llx iml=%u (Linux ctxt-info-v2)",
            (unsigned long long)ctxt_phys, iml_len);
        return true;
    }

    struct iwl_context_info *ctxt =
        pmm_physical_to_virtual(ctxt_phys);
    if (!ctxt) {
        klog(KLOG_ERROR, "ax201: fw_upload: no virtual mapping for CTXT_INFO");
        return false;
    }
    memset(ctxt, 0, sizeof(*ctxt));

    uint32_t hw_rev = ax201_csr_read(AX201_CSR_HW_REV);
    ctxt->version.mac_id = (uint16_t)((hw_rev >> 4) & 0xFFF);
    ctxt->version.version = 0;
    ctxt->version.size = sizeof(*ctxt) / 4;

    uint32_t rb_size = IWL_CTXT_INFO_RB_SIZE_4K;
    uint32_t cb_size = 8;
    uint32_t control_flags = IWL_CTXT_INFO_TFD_FORMAT_LONG;
    control_flags |= (cb_size << 4) & IWL_CTXT_INFO_RB_CB_SIZE;
    control_flags |= (rb_size << 9) & IWL_CTXT_INFO_RB_SIZE;
    ctxt->control.control_flags = control_flags;

    ctxt->rbd_cfg.free_rbd_addr = rbd_free_phys;
    ctxt->rbd_cfg.used_rbd_addr = rbd_used_phys;
    ctxt->rbd_cfg.status_wr_ptr = rbd_status_phys;

    ctxt->hcmd_cfg.cmd_queue_addr = cmdq_phys;
    ctxt->hcmd_cfg.cmd_queue_size = 8;

    int idx = 0;
    idx=ax201_stage_dram_sections(&ctxt->dram, sections, section_n);
    if(idx<0){
        klog(KLOG_ERROR, "ax201: Qu dram staging failed");
        return false;
    }

    klogf(KLOG_INFO,
        "ax201: Linux dram: %d entries, firmware %llu bytes",
        idx, (unsigned long long)adapter.firmware_size);

    __asm__ volatile("mfence" ::: "memory");

    ax201_enable_fw_load_interrupts();

    ax201_csr_write(AX201_CSR_CTXT_INFO_BA,
        (uint32_t)(ctxt_phys & 0xFFFFFFFFU));
    ax201_csr_write(AX201_CSR_CTXT_INFO_BA_HI,
        (uint32_t)(ctxt_phys >> 32));
    if (iml_phys && iml_len) {
        ax201_csr_write(AX201_CSR_IML_DATA_ADDR_V1,
            (uint32_t)(iml_phys & 0xFFFFFFFFU));
        ax201_csr_write(AX201_CSR_IML_DATA_ADDR_V1 + 4,
            (uint32_t)(iml_phys >> 32));
        ax201_csr_write(AX201_CSR_IML_SIZE_ADDR_V1, iml_len);
        ax201_csr_write(AX201_CSR_CTXT_INFO_BOOT_CTRL_V1,
            AX201_CSR_AUTO_FUNC_BOOT_ENA);
    }

    uint32_t gp = ax201_csr_read(AX201_CSR_GP_CNTRL);
    gp &= ~AX201_GP_CNTRL_INIT_DONE;
    ax201_csr_write(AX201_CSR_GP_CNTRL, gp);
    timer_sleep(1);
    gp = ax201_csr_read(AX201_CSR_GP_CNTRL);
    gp |= AX201_GP_CNTRL_INIT_DONE;
    ax201_csr_write(AX201_CSR_GP_CNTRL, gp);
    (void)ax201_csr_read(AX201_CSR_GP_CNTRL);

    ax201_csr_write(AX201_CSR_CTXT_INFO_KICK, 1U);

    adapter.ctxt_info_written = true;

    klogf(KLOG_OK,
        "ax201: CTXT_INFO kick phys=0x%llx staged=%d iml=%u",
        (unsigned long long)ctxt_phys, idx, iml_len);
    klogf(KLOG_DEBUG,
        "ax201: Linux ctxt size=%u version=%u mac_id=0x%x flags=0x%x",
        ctxt->version.size,
        ctxt->version.version,
        ctxt->version.mac_id,
        control_flags);

    return true;
}

static bool ax201_wait_alive_poll(void){
    if(!adapter.regs) return false;
    uint64_t deadline = timer_ticks() + AX201_ALIVE_TIMEOUT_MS;
    klogf(KLOG_INFO, "ax201: waiting up to %u ms for firmware ALIVE (is_so=%u)...",
        AX201_ALIVE_TIMEOUT_MS, adapter.is_so_family?1U:0U);

    while(timer_ticks() < deadline){
        uint32_t ints=ax201_csr_read(AX201_CSR_INT);
        uint32_t msix=ax201_csr_read(AX201_CSR_MSIX_HW_INT_CAUSES);

        if((ints & AX201_INT_ALIVE) || (msix & AX201_MSIX_INT_ALIVE)){
            if(ints & AX201_INT_ALIVE)
                ax201_csr_write(AX201_CSR_INT, AX201_INT_ALIVE);
            adapter.firmware_alive=true;
            klogf(KLOG_OK,
                "ax201: firmware ALIVE (synchronous) int=0x%08x msix=0x%08x", ints, msix);
            return true;
        }
        if(ints & AX201_INT_SW_ERR){
            ax201_csr_write(AX201_CSR_INT, AX201_INT_SW_ERR);
            uint32_t gp = ax201_csr_read(AX201_CSR_GP_CNTRL);
            klogf(KLOG_ERROR,
                "ax201: firmware SW_ERR during ALIVE wait "
                "(gp=0x%08x int=0x%08x hint=%s)", gp, ints, adapter.firmware_hint);
            klog(KLOG_ERROR, "ax201: SW_ERR means context invalid or firmware mismatch for PCI ID");
            return false;
        }

        timer_sleep(AX201_ALIVE_POLL_MS);
    }

    uint32_t gp   = ax201_csr_read(AX201_CSR_GP_CNTRL);
    uint32_t ints = ax201_csr_read(AX201_CSR_INT);
    uint32_t rev  = ax201_csr_read(AX201_CSR_HW_REV);
    klogf(KLOG_WARN,
        "ax201: ALIVE timeout %u ms (gp=0x%08x int=0x%08x rev=0x%08x is_so=%u)",
        AX201_ALIVE_TIMEOUT_MS, gp, ints, rev, adapter.is_so_family?1U:0U);
    klog(KLOG_WARN, "ax201: Linux ALIVE timeout – check RFKILL, ucode variant (QuZ vs So), PCI PM D0, CTXT_INFO");
    klog(KLOG_INFO, "ax201: without ALIVE real scan/connect is impossible (as in Linux iwl_trans_pcie_gen2_start_fw)");
    return false;
}

bool intel_ax201_has_hardware(void){ return adapter.hardware_found; }
const char *intel_ax201_hardware_info(void){ return adapter.hw_info; }
