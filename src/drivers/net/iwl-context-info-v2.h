/* Copied from Linux drivers/net/wireless/intel/iwlwifi/iwl-context-info-v2.h */
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifndef __packed
#define __packed __attribute__((packed))
#endif

#define IWL_PRPH_SCRATCH_RB_SIZE_4K 0x04
#define IWL_PRPH_SCRATCH_MTR_MODE 0x01
#define IWL_PRPH_SCRATCH_MTR_FORMAT 0x02
#define IWL_PRPH_MTR_FORMAT_256B 0x02

struct iwl_prph_scratch_version {
    uint16_t mac_id;
    uint16_t version;
    uint16_t size;
    uint16_t reserved;
} __packed;

struct iwl_context_info_v2 {
    uint16_t version;
    uint16_t size;
    uint32_t config;
    uint64_t prph_info_base_addr;
    uint64_t prph_scratch_base_addr;
    uint32_t prph_scratch_size;
    uint32_t reserved0;
    uint64_t cr_head_idx_arr_base_addr;
    uint64_t tr_tail_idx_arr_base_addr;
    uint64_t cr_tail_idx_arr_base_addr;
    uint64_t tr_head_idx_arr_base_addr;
    uint16_t cr_idx_arr_size;
    uint16_t tr_idx_arr_size;
    uint32_t reserved1;
    uint64_t mtr_base_addr;
    uint64_t mcr_base_addr;
    uint16_t mtr_size;
    uint16_t mcr_size;
    uint32_t reserved2[4];
} __packed;

struct iwl_prph_scratch_ctrl_cfg {
    struct iwl_prph_scratch_version version;
    uint32_t control_flags;
    uint32_t reserved;
    uint64_t rbd_cfg_free;
    uint64_t rbd_cfg_used;
    uint64_t rbd_cfg_status;
    uint64_t reserved2[8];
} __packed;

struct iwl_prph_scratch {
    struct iwl_prph_scratch_ctrl_cfg ctrl_cfg;
    uint8_t reserved[1024];
} __packed;
