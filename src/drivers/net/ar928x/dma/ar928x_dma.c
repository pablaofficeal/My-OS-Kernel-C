#include "ar928x_dma.h"
#include "ar928x.h"
#include "kernel/diagnostics/klog.h"
#include "mm/pmm.h"
#include "lib/string.h"
void ar928x_dma_release(void){
    for(uint32_t i=0;i<AR928X_RING_COUNT;i++){
        if(g_ar928x.tx_buf_phys[i]) pmm_free_page(g_ar928x.tx_buf_phys[i]);
        if(g_ar928x.rx_buf_phys[i]) pmm_free_page(g_ar928x.rx_buf_phys[i]);
        g_ar928x.tx_buf_phys[i]=0; g_ar928x.rx_buf_phys[i]=0;
        g_ar928x.tx_bufs[i]=0; g_ar928x.rx_bufs[i]=0;
    }
    if(g_ar928x.tx_ring_phys) pmm_free_page(g_ar928x.tx_ring_phys);
    if(g_ar928x.rx_ring_phys) pmm_free_page(g_ar928x.rx_ring_phys);
    g_ar928x.tx_ring_phys=0; g_ar928x.rx_ring_phys=0;
    g_ar928x.tx_ring=0; g_ar928x.rx_ring=0;
}
bool ar928x_dma_allocate(void){
    g_ar928x.tx_ring_phys=pmm_allocate_page();
    g_ar928x.rx_ring_phys=pmm_allocate_page();
    if(!g_ar928x.tx_ring_phys || !g_ar928x.rx_ring_phys){ar928x_dma_release(); return false;}
    g_ar928x.tx_ring=pmm_physical_to_virtual(g_ar928x.tx_ring_phys);
    g_ar928x.rx_ring=pmm_physical_to_virtual(g_ar928x.rx_ring_phys);
    if(!g_ar928x.tx_ring || !g_ar928x.rx_ring){ar928x_dma_release(); return false;}
    memset(g_ar928x.tx_ring,0,PMM_PAGE_SIZE);
    memset(g_ar928x.rx_ring,0,PMM_PAGE_SIZE);
    for(uint32_t i=0;i<AR928X_RING_COUNT;i++){
        g_ar928x.tx_buf_phys[i]=pmm_allocate_page();
        g_ar928x.rx_buf_phys[i]=pmm_allocate_page();
        if(!g_ar928x.tx_buf_phys[i]||!g_ar928x.rx_buf_phys[i]){ar928x_dma_release(); return false;}
        g_ar928x.tx_bufs[i]=pmm_physical_to_virtual(g_ar928x.tx_buf_phys[i]);
        g_ar928x.rx_bufs[i]=pmm_physical_to_virtual(g_ar928x.rx_buf_phys[i]);
        if(!g_ar928x.tx_bufs[i]||!g_ar928x.rx_bufs[i]){ar928x_dma_release(); return false;}
        g_ar928x.rx_ring[i].ds_data=(uint32_t)g_ar928x.rx_buf_phys[i];
        g_ar928x.rx_ring[i].ds_link=0;
        if(i+1<AR928X_RING_COUNT) g_ar928x.tx_ring[i].ds_link=(uint32_t)g_ar928x.tx_ring_phys+(i+1)*sizeof(struct ar928x_desc);
        else g_ar928x.tx_ring[i].ds_link=(uint32_t)g_ar928x.tx_ring_phys;
    }
    klogf(KLOG_INFO,"ar928x: DMA rings tx=0x%llx rx=0x%llx",(unsigned long long)g_ar928x.tx_ring_phys,(unsigned long long)g_ar928x.rx_ring_phys);
    return true;
}
bool ar928x_dma_init_hw(void){
    if(!g_ar928x.regs || !g_ar928x.tx_ring_phys || !g_ar928x.rx_ring_phys) return false;
    return true;
}
