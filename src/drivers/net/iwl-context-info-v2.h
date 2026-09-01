/* Copied from Linux drivers/net/wireless/intel/iwlwifi/pcie/iwl-context-info-v2.h */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "iwl-context-info.h"

#ifndef __packed
#define __packed __attribute__((packed))
#endif

#define CSR_CTXT_INFO_BOOT_CTRL         0x0
#define CSR_CTXT_INFO_ADDR              0x118
#define CSR_IML_DATA_ADDR               0x120
#define CSR_IML_SIZE_ADDR               0x128

#define CSR_AUTO_FUNC_BOOT_ENA          (1u << 1)

#define IWL_PRPH_MTR_FORMAT_256B        0xC0000u
#define IWL_PRPH_SCRATCH_MTR_MODE       (1u << 17)
#define IWL_PRPH_SCRATCH_MTR_FORMAT     ((1u << 18) | (1u << 19))
#define IWL_PRPH_SCRATCH_RB_SIZE_4K     (1u << 16)

struct iwl_prph_scratch_version {
    uint16_t mac_id;
    uint16_t version;
    uint16_t size;
    uint16_t reserved;
} __packed;

struct iwl_prph_scratch_control {
    uint32_t control_flags;
    uint32_t control_flags_ext;
} __packed;

struct iwl_prph_scratch_pnvm_cfg {
    uint64_t pnvm_base_addr;
    uint32_t pnvm_size;
    uint32_t reserved;
} __packed;

struct iwl_prph_scratch_hwm_cfg {
    uint64_t hwm_base_addr;
    uint32_t hwm_size;
    uint32_t debug_token_config;
} __packed;

struct iwl_prph_scratch_rbd_cfg {
    uint64_t free_rbd_addr;
    uint32_t reserved;
} __packed;

struct iwl_prph_scratch_uefi_cfg {
    uint64_t base_addr;
    uint32_t size;
    uint32_t reserved;
} __packed;

struct iwl_prph_scratch_step_cfg {
    uint32_t mbx_addr_0;
    uint32_t mbx_addr_1;
} __packed;

struct iwl_prph_scratch_ctrl_cfg {
    struct iwl_prph_scratch_version version;
    struct iwl_prph_scratch_control control;
    struct iwl_prph_scratch_pnvm_cfg pnvm_cfg;
    struct iwl_prph_scratch_hwm_cfg hwm_cfg;
    struct iwl_prph_scratch_rbd_cfg rbd_cfg;
    struct iwl_prph_scratch_uefi_cfg reduce_power_cfg;
    struct iwl_prph_scratch_step_cfg step_cfg;
} __packed;

struct iwl_context_info_dram_fseq {
    struct iwl_context_info_dram common;
    uint64_t fseq_img[8];
} __packed;

struct iwl_prph_scratch {
    struct iwl_prph_scratch_ctrl_cfg ctrl_cfg;
    uint32_t fseq_override;
    uint32_t step_analog_params;
    uint32_t reserved[8];
    struct iwl_context_info_dram_fseq dram;
} __packed;

struct iwl_prph_info {
    uint32_t boot_stage_mirror;
    uint32_t ipc_status_mirror;
    uint32_t sleep_notif;
    uint32_t reserved;
} __packed;

struct iwl_context_info_v2 {
    uint16_t version;
    uint16_t size;
    uint32_t config;
    uint64_t prph_info_base_addr;
    uint64_t cr_head_idx_arr_base_addr;
    uint64_t tr_tail_idx_arr_base_addr;
    uint64_t cr_tail_idx_arr_base_addr;
    uint64_t tr_head_idx_arr_base_addr;
    uint16_t cr_idx_arr_size;
    uint16_t tr_idx_arr_size;
    uint64_t mtr_base_addr;
    uint64_t mcr_base_addr;
    uint16_t mtr_size;
    uint16_t mcr_size;
    uint16_t mtr_doorbell_vec;
    uint16_t mcr_doorbell_vec;
    uint16_t mtr_msi_vec;
    uint16_t mcr_msi_vec;
    uint8_t mtr_opt_header_size;
    uint8_t mtr_opt_footer_size;
    uint8_t mcr_opt_header_size;
    uint8_t mcr_opt_footer_size;
    uint16_t msg_rings_ctrl_flags;
    uint16_t prph_info_msi_vec;
    uint64_t prph_scratch_base_addr;
    uint32_t prph_scratch_size;
    uint32_t reserved;
} __packed;

#ifndef offsetofend
#define offsetofend(type, member) (offsetof(type, member) + sizeof(((type *)0)->member))
#endif
