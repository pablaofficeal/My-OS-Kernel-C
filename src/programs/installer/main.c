#include "../../libc/include/purec.h"
#include "../../fs/fs_types.h"

#define COLOR_BACKGROUND 0x181825
#define COLOR_PANEL 0x313244
#define COLOR_CARD 0x45475A
#define COLOR_TEXT 0xCDD6F4
#define COLOR_MUTED 0xA6ADC8
#define COLOR_ACCENT 0x89B4FA
#define COLOR_SUCCESS 0xA6E3A1
#define COLOR_DANGER 0xF38BA8
#define MAX_VISIBLE_DISKS 8

static struct storage_device_info disks[20];
static int32_t disk_count;
static int32_t selected_disk;
static struct pc_display_info display;

static uint32_t visible_disk_count(void){
    uint32_t capacity=display.height>280 ? (display.height-280)/54 : 1;
    if(capacity>MAX_VISIBLE_DISKS) capacity=MAX_VISIBLE_DISKS;
    if(capacity>(uint32_t)disk_count) capacity=(uint32_t)disk_count;
    return capacity;
}

static bool inside(int32_t x, int32_t y, uint32_t left, uint32_t top,
                   uint32_t width, uint32_t height){
    return x>=(int32_t)left && y>=(int32_t)top
        && x<(int32_t)(left+width) && y<(int32_t)(top+height);
}

static void number_text(char *buffer, uint64_t value, const char *suffix){
    char digits[21];
    uint32_t count=0;
    do {
        digits[count++]=(char)('0'+value%10);
        value/=10;
    } while(value);
    uint32_t output=0;
    while(count) buffer[output++]=digits[--count];
    while(*suffix) buffer[output++]=*suffix++;
    buffer[output]='\0';
}

static void draw_button(uint32_t x, uint32_t y, uint32_t width,
                        const char *label, uint32_t color){
    pc_draw_rect(x,y,width,40,color);
    pc_draw_text(x+16,y+15,label,COLOR_BACKGROUND,color);
}

static void draw_disk_selection(void){
    pc_display_begin_update();
    pc_display_clear(COLOR_BACKGROUND);
    uint32_t panel_x=display.width>820 ? (display.width-820)/2 : 20;
    uint32_t panel_width=display.width>860 ? 820 : display.width-40;
    pc_draw_rect(panel_x,36,panel_width,display.height-72,COLOR_PANEL);
    pc_draw_text(panel_x+28,62,"Install PureC OS",COLOR_TEXT,COLOR_PANEL);
    pc_draw_text(panel_x+28,88,
                 "Choose a target disk. Installation uses UEFI/GPT.",
                 COLOR_MUTED,COLOR_PANEL);
    pc_draw_text(panel_x+28,108,"All data on the selected disk will be erased.",
                 COLOR_DANGER,COLOR_PANEL);
    pc_draw_text(panel_x+panel_width-250,108,"Esc: cancel  Enter: install",
                 COLOR_MUTED,COLOR_PANEL);
    uint32_t visible=visible_disk_count();
    for(uint32_t index=0;index<visible;index++){
        uint32_t y=136+index*54;
        uint32_t color=(int32_t)index==selected_disk ? COLOR_ACCENT : COLOR_CARD;
        pc_draw_rect(panel_x+28,y,panel_width-56,44,color);
        pc_draw_text(panel_x+42,y+9,disks[index].name,COLOR_TEXT,color);
        pc_draw_text(panel_x+132,y+9,disks[index].model,COLOR_TEXT,color);
        char size[32];
        number_text(size,(disks[index].sector_count*disks[index].sector_size)
                    /(1024*1024)," MiB");
        pc_draw_text(panel_x+panel_width-150,y+9,size,COLOR_TEXT,color);
    }
    uint32_t button_y=display.height-104;
    draw_button(panel_x+28,button_y,150,"Cancel",COLOR_DANGER);
    draw_button(panel_x+panel_width-218,button_y,190,"Install",COLOR_SUCCESS);
    pc_display_end_update();
}

static void draw_progress(const struct install_status *status){
    pc_display_begin_update();
    pc_display_clear(COLOR_BACKGROUND);
    uint32_t panel_x=display.width>760 ? (display.width-760)/2 : 20;
    uint32_t panel_width=display.width>800 ? 760 : display.width-40;
    uint32_t panel_y=display.height>360 ? (display.height-360)/2 : 20;
    pc_draw_rect(panel_x,panel_y,panel_width,320,COLOR_PANEL);
    pc_draw_text(panel_x+30,panel_y+34,"Installing PureC OS",COLOR_TEXT,COLOR_PANEL);
    pc_draw_text(panel_x+30,panel_y+72,status->stage,COLOR_MUTED,COLOR_PANEL);
    uint32_t bar_x=panel_x+30;
    uint32_t bar_y=panel_y+126;
    uint32_t bar_width=panel_width-60;
    pc_draw_rect(bar_x,bar_y,bar_width,32,COLOR_CARD);
    uint32_t progress=status->progress>100 ? 100 : status->progress;
    pc_draw_rect(bar_x,bar_y,(bar_width*progress)/100,32,
                 status->state==3 ? COLOR_DANGER : COLOR_ACCENT);
    char percent[16];
    number_text(percent,progress,"%");
    pc_draw_text(panel_x+panel_width/2-12,bar_y+11,percent,COLOR_TEXT,
                 progress ? COLOR_ACCENT : COLOR_CARD);
    pc_draw_text(panel_x+30,panel_y+198,
                 "Do not power off or remove the target disk.",
                 COLOR_MUTED,COLOR_PANEL);
    pc_display_end_update();
}

static bool create_directory(const char *path){
    int64_t result=pc_syscall(SYS_MKDIR,(uint64_t)(uintptr_t)path,0,0);
    return result==0 || result==FS_ERROR_EXISTS;
}

static bool write_file(const char *path, const char *contents){
    return pc_syscall(SYS_FILE_WRITE,(uint64_t)(uintptr_t)path,
        (uint64_t)(uintptr_t)contents,pc_strlen(contents))>=0;
}

static bool finish_configuration(void){
    return create_directory("/etc") && create_directory("/home")
        && create_directory("/purec") && create_directory("/game")
        && write_file("/etc/hostname","purec-os\n")
        && write_file("/purec/install.cfg",
                      "version=0.4.0\ninstalled=1\ninstaller=gui-ring3\n")
        && write_file("/README","PureC OS installation complete.\n");
}

static int wait_after_failure(const char *stage, int result){
    struct install_status status={0};
    status.state=3;
    status.progress=100;
    status.result=result;
    pc_copy(status.stage,stage,sizeof(status.stage));
    draw_progress(&status);

    uint32_t panel_x=display.width>760 ? (display.width-760)/2 : 20;
    uint32_t panel_width=display.width>800 ? 760 : display.width-40;
    uint32_t panel_y=display.height>360 ? (display.height-360)/2 : 20;
    pc_display_begin_update();
    pc_draw_text(panel_x+30,panel_y+232,
                 "Installation stopped. Press Esc, Enter, or click Close.",
                 COLOR_DANGER,COLOR_PANEL);
    uint32_t button_x=panel_x+panel_width-180;
    uint32_t button_y=panel_y+262;
    draw_button(button_x,button_y,150,"Close",COLOR_DANGER);
    pc_display_end_update();

    bool mouse_armed=false;
    struct mouse_state previous={0};
    for(;;){
        int32_t key=pc_try_getchar();
        if(key==27 || key=='\r' || key=='\n') return result;
        struct mouse_state mouse;
        if(pc_mouse_get(&mouse)){
            bool button_down=(mouse.buttons&1)!=0;
            if(!button_down) mouse_armed=true;
            if(mouse_armed && button_down && !(previous.buttons&1)
               && inside(mouse.x,mouse.y,button_x,button_y,150,40)){
                return result;
            }
            previous=mouse;
        }
        pc_sleep(20);
    }
}

static int run_installation(void){
    if(pc_install_start(disks[selected_disk].name,
                        disks[selected_disk].serial)<0){
        return wait_after_failure("Cannot start installation",4);
    }
    struct install_status status={0};
    uint32_t previous_progress=UINT32_MAX;
    for(;;){
        if(!pc_install_status(&status))
            return wait_after_failure("Cannot read installation status",5);
        if(status.progress!=previous_progress){
            draw_progress(&status);
            previous_progress=status.progress;
        }
        if(status.state==3)
            return wait_after_failure(status.stage,
                                      status.result ? status.result : 6);
        if(status.state==2) break;
        pc_sleep(30);
    }
    status.progress=96;
    pc_copy(status.stage,"Writing system configuration",sizeof(status.stage));
    draw_progress(&status);
    if(!finish_configuration())
        return wait_after_failure("Cannot write system configuration",7);
    status.progress=100;
    status.state=2;
    pc_copy(status.stage,"Installation complete",sizeof(status.stage));
    draw_progress(&status);
    pc_display_begin_update();
    uint32_t completion_x=display.width>360 ? (display.width-360)/2 : 20;
    pc_draw_text(completion_x,display.height/2+110,
                 "Remove the ISO and reboot.",COLOR_SUCCESS,COLOR_PANEL);
    pc_display_end_update();
    pc_sleep(1800);
    return 0;
}

static int installer_main(void){
    if(!pc_display_get_info(&display) || !display.available) return 1;
    disk_count=pc_list_disks(disks,20);
    if(disk_count<=0){
        pc_display_begin_update();
        pc_display_clear(COLOR_BACKGROUND);
        pc_draw_text(40,60,"No disks found",COLOR_DANGER,COLOR_BACKGROUND);
        pc_display_end_update();
        pc_sleep(1500);
        return 2;
    }
    selected_disk=0;
    draw_disk_selection();
    struct mouse_state previous={0};
    bool mouse_armed=false;
    for(;;){
        int32_t key=pc_try_getchar();
        if(key==27 || key=='q' || key=='Q') return 0;
        if(key=='w' || key=='W'){
            if(selected_disk>0) selected_disk--;
            draw_disk_selection();
        } else if(key=='s' || key=='S'){
            if(selected_disk+1<(int32_t)visible_disk_count()) selected_disk++;
            draw_disk_selection();
        } else if(key=='\r' || key=='\n'){
            return run_installation();
        }
        struct mouse_state mouse;
        if(!pc_mouse_get(&mouse)){ pc_sleep(20); continue; }
        bool button_down=(mouse.buttons&1)!=0;
        if(!button_down) mouse_armed=true;
        bool pressed=mouse_armed && button_down && !(previous.buttons&1);
        if(pressed){
            mouse_armed=false;
            uint32_t panel_x=display.width>820 ? (display.width-820)/2 : 20;
            uint32_t panel_width=display.width>860 ? 820 : display.width-40;
            uint32_t visible=visible_disk_count();
            for(uint32_t index=0;index<visible;index++){
                if(inside(mouse.x,mouse.y,panel_x+28,136+index*54,
                          panel_width-56,44)){
                    selected_disk=(int32_t)index;
                    draw_disk_selection();
                }
            }
            uint32_t button_y=display.height-104;
            if(inside(mouse.x,mouse.y,panel_x+28,button_y,150,40)) return 0;
            if(inside(mouse.x,mouse.y,panel_x+panel_width-218,button_y,190,40))
                return run_installation();
        }
        previous=mouse;
        pc_sleep(16);
    }
}

void _start(void){
    pc_exit(installer_main());
}
