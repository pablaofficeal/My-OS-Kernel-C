/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2017 Intel Deutschland GmbH
 * Copyright (C) 2018-2020, 2022 Intel Corporation
 * Copied verbatim from Linux v6.6 drivers/net/wireless/intel/iwlwifi/iwl-context-info.h
 * for PureC OS AX201 bring-up – original file: https://github.com/torvalds/linux
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifndef __packed
#define __packed __attribute__((packed))
#endif
#ifndef __le16
#define __le16 uint16_t
#endif
#ifndef __le32
#define __le32 uint32_t
#endif
#ifndef __le64
#define __le64 uint64_t
#endif

#define IWL_MAX_DRAM_ENTRY 64
#define CSR_CTXT_INFO_BA 0x40

enum iwl_context_info_flags {
    IWL_CTXT_INFO_AUTO_FUNC_INIT = 0x0001,
    IWL_CTXT_INFO_EARLY_DEBUG = 0x0002,
    IWL_CTXT_INFO_ENABLE_CDMP = 0x0004,
    IWL_CTXT_INFO_RB_CB_SIZE = 0x00f0,
    IWL_CTXT_INFO_TFD_FORMAT_LONG = 0x0100,
    IWL_CTXT_INFO_RB_SIZE = 0x1e00,
    IWL_CTXT_INFO_RB_SIZE_1K = 0x1,
    IWL_CTXT_INFO_RB_SIZE_2K = 0x2,
    IWL_CTXT_INFO_RB_SIZE_4K = 0x4,
    IWL_CTXT_INFO_RB_SIZE_8K = 0x8,
    IWL_CTXT_INFO_RB_SIZE_12K = 0x9,
    IWL_CTXT_INFO_RB_SIZE_16K = 0xa,
};

struct iwl_context_info_version {
    __le16 mac_id;
    __le16 version;
    __le16 size;
    __le16 reserved;
} __packed;

struct iwl_context_info_control {
    __le32 control_flags;
    __le32 reserved;
} __packed;

struct iwl_context_info_dram {
    __le64 umac_img[IWL_MAX_DRAM_ENTRY];
    __le64 lmac_img[IWL_MAX_DRAM_ENTRY];
    __le64 virtual_img[IWL_MAX_DRAM_ENTRY];
} __packed;

struct iwl_context_info_rbd_cfg {
    __le64 free_rbd_addr;
    __le64 used_rbd_addr;
    __le64 status_wr_ptr;
} __packed;

struct iwl_context_info_hcmd_cfg {
    __le64 cmd_queue_addr;
    uint8_t cmd_queue_size;
    uint8_t reserved[7];
} __packed;

struct iwl_context_info_dump_cfg {
    __le64 core_dump_addr;
    __le32 core_dump_size;
    __le32 reserved;
} __packed;

struct iwl_context_info_pnvm_cfg {
    __le64 platform_nvm_addr;
    __le32 platform_nvm_size;
    __le32 reserved;
} __packed;

struct iwl_context_info_early_dbg_cfg {
    __le64 early_debug_addr;
    __le32 early_debug_size;
    __le32 reserved;
} __packed;

struct iwl_context_info {
    struct iwl_context_info_version version;
    struct iwl_context_info_control control;
    __le64 reserved0;
    struct iwl_context_info_rbd_cfg rbd_cfg;
    struct iwl_context_info_hcmd_cfg hcmd_cfg;
    __le32 reserved1[4];
    struct iwl_context_info_dump_cfg dump_cfg;
    struct iwl_context_info_early_dbg_cfg edbg_cfg;
    struct iwl_context_info_pnvm_cfg pnvm_cfg;
    __le32 reserved2[16];
    struct iwl_context_info_dram dram;
    __le32 reserved3[16];
} __packed;

_Static_assert(sizeof(struct iwl_context_info) == 1792, "linux iwl_context_info size");
