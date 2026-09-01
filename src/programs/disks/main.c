#include "../../libgui/include/puregui.h"
#include "../../libgui/include/pguiw.h"
#include "../../libc/include/purec.h"

#define DISK_LIMIT 12
#define MIB (1024ULL*1024ULL)
#define GIB (1024ULL*1024ULL*1024ULL)

struct disk_app {
    struct storage_device_info disks[DISK_LIMIT];
    int32_t count;
    int32_t selected;
    bool confirm_format;
    char status[96];
    int32_t partition_count;    // новое: кол-во partitions (1-4)
    uint64_t sizes_gb[4];       // новые: размеры GB для каждого раздела
    bool custom_format;         // флаг кастомного формата
};

static char *append_text(char *out,const char *text){
    while(*text) *out++=*text++;
    *out='\0';
    return out;
}

static char *append_u64(char *out,uint64_t value){
    char reverse[24]; uint32_t count=0;
    do{ reverse[count++]=(char)('0'+value%10U); value/=10U; }
    while(value && count<sizeof(reverse));
    while(count) *out++=reverse[--count];
    *out='\0'; return out;
}

static void refresh(struct disk_app *app){
    app->count=pc_list_disks(app->disks,DISK_LIMIT);
    if(app->count<0) app->count=0;
    if(app->selected>=app->count) app->selected=app->count-1;
    if(app->selected<0 && app->count>0) app->selected=0;
    // reset custom format state when refreshing
    app->custom_format = false;
    app->partition_count = 1;
    for(int i=0;i<4;i++) app->sizes_gb[i] = 0;
}

static void disk_line(char *line,const struct storage_device_info *disk){
    char *out=append_text(line,disk->name);
    out=append_text(out,"  ");
    out=append_text(out,disk->model[0] ? disk->model : "Unknown model");
    out=append_text(out,"  ");
    uint64_t bytes=disk->sector_count*disk->sector_size;
    if(bytes>=GIB){ out=append_u64(out,bytes/GIB); out=append_text(out," GiB"); }
    else{ out=append_u64(out,bytes/MIB); out=append_text(out," MiB"); }
    out=append_text(out,disk->operational ? "  online" : "  offline");
    (void)append_text(out,disk->writable ? "  writable" : "  read-only");
}

static void draw(struct pg_window *window,struct disk_app *app,
                 const struct pg_event *event){
    pg_window_begin(window);
    pg_window_text(window,20,18,"Storage overview",window->theme.text);
    char summary[64]; char *out=append_text(summary,"Connected disks: ");
    out=append_u64(out,(uint32_t)app->count);
    uint64_t total=0;
    for(int32_t index=0;index<app->count;index++)
        total+=app->disks[index].sector_count*app->disks[index].sector_size;
    out=append_text(out,"    Total capacity: ");
    out=append_u64(out,total/GIB); (void)append_text(out," GiB");
    pg_window_text(window,20,40,summary,window->theme.muted_text);
    pg_window_text(window,20,72,"Connected devices",window->theme.text);
    uint32_t controls=window->client.height-88;
    uint32_t visible_rows=controls>94 ? (controls-94)/30U : 0;
    int32_t visible_count=app->count;
    if((uint32_t)visible_count>visible_rows) visible_count=(int32_t)visible_rows;
    for(int32_t index=0;index<visible_count;index++){
        struct pg_rect row={20,94+(uint32_t)index*30,
                            window->client.width-40,26};
        pg_window_rect(window,row,index==app->selected
            ? 0x45475A : 0x2B2D40);
        char line[120]; disk_line(line,&app->disks[index]);
        pg_window_text(window,row.x+8,row.y+9,line,window->theme.text);
        if(event && event->type==PG_EVENT_MOUSE_UP && event->button==1){
            int32_t x=event->x-(int32_t)window->client.x;
            int32_t y=event->y-(int32_t)window->client.y;
            if(x>=(int32_t)row.x && x<(int32_t)(row.x+row.width)
               && y>=(int32_t)row.y && y<(int32_t)(row.y+row.height)){
                app->selected=index;
                app->confirm_format=false;
                app->status[0]='\0';
            }
        }
    }
    if(app->selected>=0){
        const struct storage_device_info *disk=&app->disks[app->selected];
        char selected[96]; out=append_text(selected,"Selected: ");
        out=append_text(out,disk->name); out=append_text(out,"  serial: ");
        (void)append_text(out,disk->serial[0] ? disk->serial : "unavailable");
        pg_window_text(window,20,controls,selected,window->theme.text);
        // Partition count selector
        pg_window_text(window,20,controls+24,"Partitions: ",window->theme.muted_text);
        char part_str[8]; 
        uint32_t pc_val = app->partition_count;
        char rev[12]; int rlen=0;
        if(pc_val==0) rev[rlen++]='0'; else { char r2[12]; int l2=0; int v=pc_val; while(v>0){ r2[l2++]='0'+v%10; v/=10; } while(l2) rev[rlen++]=r2[--l2]; }
        for(int i=0;i<rlen;i++) part_str[i]=rev[i];
        part_str[rlen]='\0';
        pg_window_text(window, 140, controls+24, part_str, window->theme.text);
        // GB inputs for each partition (show only when custom_format)
        if(app->custom_format){
            pg_window_text(window,20,controls+48,"Size GB per partition:",window->theme.muted_text);
            for(int i=0;i<app->partition_count && i<4;i++){
                char lbl[8]; 
                uint64_t v = app->sizes_gb[i];
                char r[24]; uint32_t cl=0;
                do{ r[cl++]='0'+v%10U; v/=10U; }
                while(v && cl<sizeof(r));
                while(cl) lbl[cl-1]=r[--cl];
                lbl[cl]='\0';
                pg_window_text(window, 140+(i*50), controls+48, lbl, window->theme.text);
                pg_window_text(window, 160+(i*50), controls+48, "GB", window->theme.muted_text);
            }
        }
        // Format button
        const char *label = app->confirm_format ? "CONFIRM ERASE" : (app->custom_format ? "Apply Custom" : "Format FAT32");
        if(pg_button(window,(struct pg_rect){20,controls+24,152,30},label,event)){
            if(!disk->writable || !disk->operational || !disk->serial[0]){
                pc_copy(app->status,"Disk cannot be formatted safely.",
                        sizeof(app->status));
                app->confirm_format=false;
                app->custom_format=false;
            }else if(!app->confirm_format && !app->custom_format){
                app->confirm_format=true;
                pc_copy(app->status,
                    "Warning: confirmation permanently erases the selected disk.",
                    sizeof(app->status));
            }else if(app->custom_format){
                // custom format: call SYS_FAT32_FORMAT_CUSTOM
                // build request: device, partition_count, sizes_gb array
                struct {
                    char device[64];
                    uint32_t partition_count;
                    uint64_t sizes_gb[4];
                } req;
                pc_copy(req.device, disk->name, sizeof(req.device));
                req.partition_count = app->partition_count;
                for(int i=0;i<app->partition_count && i<4;i++) req.sizes_gb[i] = app->sizes_gb[i];
                int32_t result = (int32_t)pc_syscall(
                    SYS_FAT32_FORMAT_CUSTOM, (uint64_t)(uintptr_t)&req, 0, 0);
                app->confirm_format=false;
                app->custom_format=false;
                if(result==0) pc_copy(app->status,"Custom FAT32 format completed.",
                                    sizeof(app->status));
                else{
                    pc_copy(app->status,"Format failed, error ",sizeof(app->status));
                    char number[24]; char *number_out=number;
                    if(result<0) *number_out++='-';
                    (void)append_u64(number_out,result<0
                        ? (uint64_t)(-(int64_t)result) : (uint64_t)result);
                    append_text(app->status+pc_strlen(app->status),number);
                }
                refresh(app);
            }else{
                int32_t result=(int32_t)pc_syscall(
                    SYS_FAT32_FORMAT_FORCE,(uint64_t)(uintptr_t)disk->name,
                    (uint64_t)(uintptr_t)disk->serial,0);
                app->confirm_format=false;
                if(result==0) pc_copy(app->status,"FAT32 format completed.",
                                      sizeof(app->status));
                else{
                    pc_copy(app->status,"Format failed, error ",sizeof(app->status));
                    char number[24]; char *number_out=number;
                    if(result<0) *number_out++='-';
                    (void)append_u64(number_out,result<0
                        ? (uint64_t)(-(int64_t)result) : (uint64_t)result);
                    append_text(app->status+pc_strlen(app->status),number);
                }
                refresh(app);
            }
        }
    } // closes if(app->selected>=0)
    if(app->status[0]) pg_window_text(window,190,controls+35,app->status,
        app->confirm_format ? window->theme.danger : window->theme.muted_text);
    pg_window_end(window);
}

static int disks_main(void){
    struct pg_window window;
    struct pc_display_info display;
    if(!pc_display_get_info(&display) || !display.available) return 1;
    uint32_t width=display.width>780 ? 760 : display.width-20;
    uint32_t height=display.height>560 ? 520 : display.height-40;
    if(!pg_window_center(&window,"Disks",width,height)) return 1;
    struct disk_app app={.selected=-1};
    refresh(&app);
    struct pg_event event={0};
    draw(&window,&app,&event);
    uint32_t elapsed=0;
    while(pg_window_is_open(&window)){
        if(pg_window_poll_event(&window,&event)){
            if(event.type==PG_EVENT_CLOSE) break;
            if(event.type!=PG_EVENT_MOUSE_MOVE) draw(&window,&app,&event);
        }
        pc_sleep(20);
        elapsed+=20;
        if(elapsed>=1000){ elapsed=0; refresh(&app); draw(&window,&app,0); }
    }
    pg_window_close(&window);
    return 0;
}

void _start(void){ pc_exit(disks_main()); }