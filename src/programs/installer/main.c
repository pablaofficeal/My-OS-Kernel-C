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
static bool installation_committed;
static struct install_log install_log_snapshot;

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

static void signed_number_text(char *buffer, int32_t value){
    char magnitude[24];
    uint64_t absolute=value<0 ? (uint64_t)(-(int64_t)value) : (uint64_t)value;
    number_text(magnitude,absolute,"");
    uint32_t output=0;
    if(value<0) buffer[output++]='-';
    for(uint32_t index=0;magnitude[index];index++)
        buffer[output++]=magnitude[index];
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
    pc_draw_text(panel_x+panel_width-250,108,
                 installation_committed
                    ? "Installer locked until reboot"
                    : "Esc: cancel  Enter: install",
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
    if(!installation_committed)
        draw_button(panel_x+28,button_y,150,"Cancel",COLOR_DANGER);
    draw_button(panel_x+panel_width-218,button_y,190,"Install",COLOR_SUCCESS);
    pc_display_end_update();
}

static void draw_install_confirmation(int32_t pressed_button){
    pc_display_begin_update();
    pc_display_clear(COLOR_BACKGROUND);
    uint32_t panel_x=display.width>700 ? (display.width-700)/2 : 20;
    uint32_t panel_width=display.width>740 ? 700 : display.width-40;
    uint32_t panel_y=display.height>380 ? (display.height-380)/2 : 20;
    pc_draw_rect(panel_x,panel_y,panel_width,340,COLOR_PANEL);
    pc_draw_text(panel_x+30,panel_y+30,"Confirm disk erase",COLOR_TEXT,COLOR_PANEL);
    pc_draw_text(panel_x+30,panel_y+62,
                 "The selected disk will be erased completely.",
                 COLOR_DANGER,COLOR_PANEL);
    pc_draw_rect(panel_x+30,panel_y+100,panel_width-60,92,COLOR_CARD);
    pc_draw_text(panel_x+46,panel_y+116,disks[selected_disk].name,
                 COLOR_TEXT,COLOR_CARD);
    pc_draw_text(panel_x+146,panel_y+116,disks[selected_disk].model,
                 COLOR_TEXT,COLOR_CARD);
    char size[32];
    number_text(size,
                (disks[selected_disk].sector_count
                 *disks[selected_disk].sector_size)/(1024*1024),
                " MiB");
    pc_draw_text(panel_x+46,panel_y+150,size,COLOR_MUTED,COLOR_CARD);
    pc_draw_text(panel_x+30,panel_y+214,
                 "This operation cannot be undone.",COLOR_DANGER,COLOR_PANEL);
    uint32_t button_y=panel_y+270;
    draw_button(panel_x+30,button_y,150,"Back",
                pressed_button==0 ? COLOR_ACCENT : COLOR_CARD);
    draw_button(panel_x+panel_width-230,button_y,200,
                "Erase & Install",
                pressed_button==1 ? COLOR_ACCENT : COLOR_DANGER);
    pc_display_end_update();
}

static bool confirm_installation(void){
    uint32_t panel_x=display.width>700 ? (display.width-700)/2 : 20;
    uint32_t panel_width=display.width>740 ? 700 : display.width-40;
    uint32_t panel_y=display.height>380 ? (display.height-380)/2 : 20;
    uint32_t button_y=panel_y+270;
    uint32_t confirm_x=panel_x+panel_width-230;
    int32_t pressed_button=-1;
    struct mouse_state mouse={0};
    bool previous_down=false;
    if(pc_mouse_get(&mouse)) previous_down=(mouse.buttons&1)!=0;
    uint32_t redraw_ticks=0;
    draw_install_confirmation(pressed_button);
    for(;;){
        int32_t key=pc_try_getchar();
        if(key==27 || key=='q' || key=='Q') return false;
        if(key=='y' || key=='Y' || key=='\r' || key=='\n') return true;
        if(pc_mouse_get(&mouse)){
            bool button_down=(mouse.buttons&1)!=0;
            if(button_down && !previous_down){
                if(inside(mouse.x,mouse.y,panel_x+30,button_y,150,40))
                    pressed_button=0;
                else if(inside(mouse.x,mouse.y,confirm_x,button_y,200,40))
                    pressed_button=1;
                else
                    pressed_button=-1;
                draw_install_confirmation(pressed_button);
            }
            if(!button_down && previous_down){
                int32_t released_button=pressed_button;
                pressed_button=-1;
                draw_install_confirmation(pressed_button);
                if(released_button==0
                   && inside(mouse.x,mouse.y,panel_x+30,button_y,150,40))
                    return false;
                if(released_button==1
                   && inside(mouse.x,mouse.y,confirm_x,button_y,200,40))
                    return true;
            }
            previous_down=button_down;
        }
        if(++redraw_ticks>=15){
            draw_install_confirmation(pressed_button);
            redraw_ticks=0;
        }
        pc_sleep(20);
    }
}

static void progress_panel_layout(uint32_t *panel_x, uint32_t *panel_y,
                                  uint32_t *panel_width,
                                  uint32_t *panel_height){
    *panel_width=display.width>800 ? 760 : display.width-40;
    *panel_height=display.height>600 ? 540 : display.height-40;
    *panel_x=(display.width-*panel_width)/2;
    *panel_y=(display.height-*panel_height)/2;
}

static void append_current_stage(struct install_log *log,
                                 const struct install_status *status){
    if(!status->stage[0]) return;
    if(log->count>0
       && pc_strcmp(log->entries[log->count-1].stage,status->stage)==0) return;
    if(log->count==INSTALL_LOG_CAPACITY){
        for(uint32_t index=1;index<INSTALL_LOG_CAPACITY;index++)
            log->entries[index-1]=log->entries[index];
        log->count--;
    }
    struct install_log_entry *entry=&log->entries[log->count++];
    entry->progress=status->progress;
    pc_copy(entry->stage,status->stage,sizeof(entry->stage));
}

static void draw_progress(const struct install_status *status){
    pc_display_begin_update();
    pc_display_clear(COLOR_BACKGROUND);
    uint32_t panel_x,panel_y,panel_width,panel_height;
    progress_panel_layout(&panel_x,&panel_y,&panel_width,&panel_height);
    pc_draw_rect(panel_x,panel_y,panel_width,panel_height,COLOR_PANEL);
    pc_draw_text(panel_x+30,panel_y+28,"Installing PureC OS",COLOR_TEXT,COLOR_PANEL);
    pc_draw_text(panel_x+30,panel_y+62,"Current operation:",COLOR_MUTED,COLOR_PANEL);
    pc_draw_text(panel_x+170,panel_y+62,status->stage,COLOR_TEXT,COLOR_PANEL);
    pc_draw_text(panel_x+30,panel_y+86,"Target disk:",COLOR_MUTED,COLOR_PANEL);
    pc_draw_text(panel_x+130,panel_y+86,disks[selected_disk].name,
                 COLOR_TEXT,COLOR_PANEL);
    uint32_t bar_x=panel_x+30;
    uint32_t bar_y=panel_y+112;
    uint32_t bar_width=panel_width-60;
    pc_draw_rect(bar_x,bar_y,bar_width,40,COLOR_CARD);
    uint32_t progress=status->progress>100 ? 100 : status->progress;
    pc_draw_rect(bar_x,bar_y,(bar_width*progress)/100,40,
                 status->state==3 ? COLOR_DANGER : COLOR_ACCENT);
    char percent[16];
    number_text(percent,progress,"%");
    pc_draw_text(panel_x+panel_width/2-12,bar_y+15,percent,COLOR_TEXT,
                 progress ? COLOR_ACCENT : COLOR_CARD);
    pc_display_end_update();

    install_log_snapshot.count=0;
    if(pc_strcmp(status->stage,"Preparing installation")!=0)
        (void)pc_install_log(&install_log_snapshot);
    append_current_stage(&install_log_snapshot,status);
    pc_display_begin_update();
    pc_draw_text(panel_x+30,panel_y+174,"Live installation log",
                 COLOR_TEXT,COLOR_PANEL);
    uint32_t log_top=panel_y+202;
    uint32_t footer_top=panel_y+panel_height-104;
    if(footer_top>log_top+12){
        pc_draw_rect(panel_x+30,log_top,panel_width-60,
                     footer_top-log_top,COLOR_CARD);
        uint32_t visible=(footer_top-log_top-12)/20;
        if(visible>install_log_snapshot.count)
            visible=install_log_snapshot.count;
        uint32_t first=install_log_snapshot.count-visible;
        for(uint32_t row=0;row<visible;row++){
            struct install_log_entry *entry=
                &install_log_snapshot.entries[first+row];
            char entry_percent[16];
            number_text(entry_percent,entry->progress,"%");
            uint32_t y=log_top+8+row*20;
            uint32_t color=row+1==visible ? COLOR_ACCENT : COLOR_MUTED;
            pc_draw_text(panel_x+44,y,entry_percent,color,COLOR_CARD);
            pc_draw_text(panel_x+94,y,entry->stage,color,COLOR_CARD);
        }
    }
    pc_draw_text(panel_x+30,panel_y+panel_height-82,
                 status->state==3
                    ? "Installation stopped. The disk was not completed."
                    : "Installation is active. Do not power off the machine.",
                 status->state==3 ? COLOR_DANGER : COLOR_MUTED,COLOR_PANEL);
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

static void draw_failure_screen(const struct install_status *status){
    draw_progress(status);
    uint32_t panel_x,panel_y,panel_width,panel_height;
    progress_panel_layout(&panel_x,&panel_y,&panel_width,&panel_height);
    pc_display_begin_update();
    pc_draw_rect(panel_x+20,panel_y+18,panel_width-40,62,COLOR_DANGER);
    pc_draw_text(panel_x+38,panel_y+43,"INSTALLATION FAILED",
                 COLOR_BACKGROUND,COLOR_DANGER);
    pc_draw_rect(panel_x+20,panel_y+panel_height-96,
                 panel_width-40,82,COLOR_PANEL);
    pc_draw_text(panel_x+30,panel_y+panel_height-86,
                 "Installation stopped. Press Enter or click Retry.",
                 COLOR_DANGER,COLOR_PANEL);
    char result[24];
    signed_number_text(result,status->result);
    pc_draw_text(panel_x+30,panel_y+panel_height-50,"Error code:",
                 COLOR_MUTED,COLOR_PANEL);
    pc_draw_text(panel_x+126,panel_y+panel_height-50,result,
                 COLOR_DANGER,COLOR_PANEL);
    draw_button(panel_x+panel_width-180,panel_y+panel_height-54,
                150,"Retry",COLOR_DANGER);
    pc_display_end_update();
}

static void wait_after_failure(const char *stage, int result){
    struct install_status status={0};
    status.state=3;
    status.progress=100;
    status.result=result;
    pc_copy(status.stage,stage,sizeof(status.stage));
    draw_failure_screen(&status);

    uint32_t panel_x,panel_y,panel_width,panel_height;
    progress_panel_layout(&panel_x,&panel_y,&panel_width,&panel_height);
    uint32_t button_x=panel_x+panel_width-180;
    uint32_t button_y=panel_y+panel_height-54;
    bool mouse_armed=false;
    struct mouse_state previous={0};
    uint32_t redraw_ticks=0;
    for(;;){
        int32_t key=pc_try_getchar();
        if(key=='\r' || key=='\n') return;
        struct mouse_state mouse;
        if(pc_mouse_get(&mouse)){
            bool button_down=(mouse.buttons&1)!=0;
            if(!button_down) mouse_armed=true;
            if(mouse_armed && button_down && !(previous.buttons&1)
               && inside(mouse.x,mouse.y,button_x,button_y,150,40)){
                return;
            }
            previous=mouse;
        }
        if(++redraw_ticks>=15){
            draw_failure_screen(&status);
            redraw_ticks=0;
        }
        pc_sleep(20);
    }
}

static void draw_completion_screen(const struct install_status *status){
    draw_progress(status);
    uint32_t panel_x,panel_y,panel_width,panel_height;
    progress_panel_layout(&panel_x,&panel_y,&panel_width,&panel_height);
    pc_display_begin_update();
    pc_draw_rect(panel_x+20,panel_y+18,panel_width-40,62,COLOR_SUCCESS);
    pc_draw_text(panel_x+38,panel_y+43,"PUREC OS INSTALLED SUCCESSFULLY",
                 COLOR_BACKGROUND,COLOR_SUCCESS);
    pc_draw_rect(panel_x+20,panel_y+panel_height-96,
                 panel_width-40,82,COLOR_PANEL);
    pc_draw_text(panel_x+30,panel_y+panel_height-86,
                 "Installation complete. Remove the ISO, then reboot.",
                 COLOR_SUCCESS,COLOR_PANEL);
    draw_button(panel_x+panel_width-180,panel_y+panel_height-54,
                150,"Reboot",COLOR_SUCCESS);
    pc_display_end_update();
}

static void wait_after_success(const struct install_status *status){
    uint32_t panel_x,panel_y,panel_width,panel_height;
    progress_panel_layout(&panel_x,&panel_y,&panel_width,&panel_height);
    uint32_t button_x=panel_x+panel_width-180;
    uint32_t button_y=panel_y+panel_height-54;
    bool mouse_armed=false;
    struct mouse_state previous={0};
    uint32_t redraw_ticks=0;
    draw_completion_screen(status);
    for(;;){
        int32_t key=pc_try_getchar();
        if(key=='r' || key=='R' || key=='\r' || key=='\n')
            (void)pc_syscall(SYS_REBOOT,0,0,0);
        struct mouse_state mouse;
        if(pc_mouse_get(&mouse)){
            bool button_down=(mouse.buttons&1)!=0;
            if(!button_down) mouse_armed=true;
            if(mouse_armed && button_down && !(previous.buttons&1)
               && inside(mouse.x,mouse.y,button_x,button_y,150,40)){
                (void)pc_syscall(SYS_REBOOT,0,0,0);
            }
            previous=mouse;
        }
        if(++redraw_ticks>=15){
            draw_completion_screen(status);
            redraw_ticks=0;
        }
        pc_sleep(20);
    }
}

static void run_installation(bool start_job){
    struct install_status status={0};
    if(start_job){
        int32_t start_result=pc_install_start(disks[selected_disk].name,
                                              disks[selected_disk].serial);
        if(start_result<0){
            wait_after_failure("Cannot start installation",start_result);
            return;
        }
    }
    if(!pc_install_status(&status)){
        wait_after_failure("Cannot resume installation",5);
        return;
    }
    draw_progress(&status);
    uint32_t previous_progress=UINT32_MAX;
    uint32_t redraw_ticks=0;
    for(;;){
        if(!pc_install_status(&status)){
            wait_after_failure("Cannot read installation status",5);
            return;
        }
        if(status.progress!=previous_progress || ++redraw_ticks>=10){
            draw_progress(&status);
            previous_progress=status.progress;
            redraw_ticks=0;
        }
        if(status.state==3){
            wait_after_failure(status.stage,status.result ? status.result : 6);
            return;
        }
        if(status.state==2) break;
        pc_sleep(30);
    }
    status.progress=96;
    pc_copy(status.stage,"Writing system configuration",sizeof(status.stage));
    draw_progress(&status);
    if(!finish_configuration()){
        wait_after_failure("Cannot write system configuration",7);
        return;
    }
    status.progress=100;
    status.state=2;
    pc_copy(status.stage,"Installation complete",sizeof(status.stage));
    wait_after_success(&status);
}

static void start_selected_installation(void){
    if(!confirm_installation()){
        draw_disk_selection();
        return;
    }
    installation_committed=true;
    run_installation(true);
    draw_disk_selection();
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
    struct install_status existing_status={0};
    if(pc_install_status(&existing_status) && existing_status.state!=0){
        installation_committed=true;
        run_installation(false);
    }
    draw_disk_selection();
    struct mouse_state previous={0};
    bool mouse_armed=false;
    for(;;){
        int32_t key=pc_try_getchar();
        if(!installation_committed
           && (key==27 || key=='q' || key=='Q')) return 0;
        if(key=='w' || key=='W'){
            if(selected_disk>0) selected_disk--;
            draw_disk_selection();
        } else if(key=='s' || key=='S'){
            if(selected_disk+1<(int32_t)visible_disk_count()) selected_disk++;
            draw_disk_selection();
        } else if(key=='\r' || key=='\n'){
            start_selected_installation();
            previous=(struct mouse_state){0};
            mouse_armed=false;
            continue;
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
            if(!installation_committed
               && inside(mouse.x,mouse.y,panel_x+28,button_y,150,40)) return 0;
            if(inside(mouse.x,mouse.y,panel_x+panel_width-218,button_y,190,40)){
                start_selected_installation();
                previous=(struct mouse_state){0};
                mouse_armed=false;
                continue;
            }
        }
        previous=mouse;
        pc_sleep(16);
    }
}

void _start(void){
    pc_exit(installer_main());
}
