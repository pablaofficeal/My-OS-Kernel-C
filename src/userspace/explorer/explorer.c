#include "explorer.h"

#include "../syscall.h"
#include "../display.h"
#include "../../drivers/mouse/ps2_mouse.h"
#include "../../fs/fs_types.h"
#include "../../kernel/syscall.h"
#include "../../lib/string.h"

#define EXPLORER_PATH_CAPACITY 128
#define EXPLORER_ENTRY_LIMIT 12
#define EXPLORER_NAME_CAPACITY FS_DIRECTORY_NAME_CAPACITY
#define EXPLORER_WIDTH 420
#define EXPLORER_HEIGHT 350
#define TITLE_HEIGHT 30
#define TOOLBAR_Y 38
#define LIST_Y 72
#define ROW_HEIGHT 20
#define FILE_PREVIEW_CAPACITY 1024

#define COLOR_BORDER 0x45475A
#define COLOR_WINDOW 0x1E1E2E
#define COLOR_TITLE 0x89B4FA
#define COLOR_TEXT 0xCDD6F4
#define COLOR_MUTED 0x9399B2
#define COLOR_BUTTON 0x313244
#define COLOR_SELECTED 0x585B70
#define COLOR_FOLDER 0xF9E2AF
#define COLOR_CLOSE 0xF38BA8
#define COLOR_OK 0xA6E3A1

enum explorer_edit_mode {
    EXPLORER_EDIT_NONE,
    EXPLORER_EDIT_CREATE_DIRECTORY,
    EXPLORER_EDIT_CREATE_FILE,
    EXPLORER_EDIT_RENAME
};

enum explorer_popup {
    EXPLORER_POPUP_NONE,
    EXPLORER_POPUP_NEW,
    EXPLORER_POPUP_ENTRY,
    EXPLORER_POPUP_DELETE_CONFIRM
};

static uint32_t window_x=34;
static uint32_t window_y=72;
static uint32_t window_width=EXPLORER_WIDTH;
static uint32_t window_height=EXPLORER_HEIGHT;
static bool visible;
static bool dragging;
static int32_t drag_offset_x;
static int32_t drag_offset_y;
static char current_path[EXPLORER_PATH_CAPACITY]="/";
static char previous_path[EXPLORER_PATH_CAPACITY]="/";
static struct fs_directory_entry entries[EXPLORER_ENTRY_LIMIT];
static uint32_t entry_count;
static int32_t selected_index=-1;
static enum explorer_edit_mode edit_mode;
static char edit_buffer[EXPLORER_NAME_CAPACITY];
static uint32_t edit_length;
static const char *status_text="";
static enum explorer_popup popup;
static uint32_t popup_x;
static uint32_t popup_y;
static uint8_t previous_buttons;
static bool viewing_file;
static char preview_name[EXPLORER_NAME_CAPACITY];
static char file_preview[FILE_PREVIEW_CAPACITY+1];
static uint32_t file_preview_length;

static uint32_t toolbar_button_width(void){
    return 28;
}

static uint32_t toolbar_button_x(uint32_t index){
    return window_x+10+index*34;
}

static uint32_t visible_row_count(void){
    uint32_t reserved=LIST_Y+48;
    if(window_height<=reserved) return 1;
    uint32_t rows=(window_height-reserved)/ROW_HEIGHT;
    return rows<EXPLORER_ENTRY_LIMIT ? rows : EXPLORER_ENTRY_LIMIT;
}

static bool point_inside(int32_t x, int32_t y, uint32_t left, uint32_t top,
                         uint32_t width, uint32_t height){
    return x>=(int32_t)left && y>=(int32_t)top
        && x<(int32_t)(left+width) && y<(int32_t)(top+height);
}

static void copy_text(char *destination, uint32_t capacity, const char *source){
    if(!capacity) return;
    uint32_t index=0;
    while(source[index] && index+1<capacity){
        destination[index]=source[index];
        index++;
    }
    destination[index]='\0';
}

static bool build_child_path(char output[EXPLORER_PATH_CAPACITY],
                             const char *name){
    uint32_t path_length=(uint32_t)strlen(current_path);
    uint32_t name_length=(uint32_t)strlen(name);
    bool root=path_length==1 && current_path[0]=='/';
    uint32_t required=path_length+name_length+(root ? 0U : 1U);
    if(required>=EXPLORER_PATH_CAPACITY) return false;
    memcpy(output,current_path,path_length);
    uint32_t offset=path_length;
    if(!root) output[offset++]='/';
    memcpy(&output[offset],name,name_length+1);
    return true;
}

static void refresh_entries(void){
    int64_t count=userspace_syscall(SYS_DIR_LIST,(uint64_t)current_path,
                                    (uint64_t)entries,EXPLORER_ENTRY_LIMIT);
    if(count<0){
        entry_count=0;
        selected_index=-1;
        status_text="Cannot read directory";
        return;
    }
    entry_count=(uint32_t)count;
    selected_index=-1;
    status_text=entry_count>visible_row_count() ? "More entries do not fit" : "";
}

static void draw_button(uint32_t index, const char *label){
    uint32_t x=toolbar_button_x(index);
    uint32_t width=toolbar_button_width();
    uint32_t y=window_y+TOOLBAR_Y;
    display_draw_rect(x,y,width,24,COLOR_BUTTON);
    display_draw_text_sized_at(x+5,y+8,label,COLOR_TEXT,COLOR_BUTTON,7);
}

static void draw_edit_box(void){
    uint32_t y=window_y+window_height-38;
    display_draw_rect(window_x+10,y,window_width-20,28,COLOR_BUTTON);
    if(edit_mode==EXPLORER_EDIT_NONE){
        display_draw_text_sized_at(window_x+16,y+9,status_text,COLOR_MUTED,
                               COLOR_BUTTON,8);
        return;
    }
    const char *label=edit_mode==EXPLORER_EDIT_CREATE_DIRECTORY
        ? "New folder: " : edit_mode==EXPLORER_EDIT_CREATE_FILE
        ? "New file: " : "Rename: ";
    display_draw_text_sized_at(window_x+16,y+9,label,COLOR_OK,COLOR_BUTTON,8);
    display_draw_text_sized_at(window_x+112,y+9,edit_buffer,COLOR_TEXT,COLOR_BUTTON,8);
    uint32_t cursor_x=window_x+112+edit_length*8;
    display_draw_rect(cursor_x,y+7,2,13,COLOR_TEXT);
}

static void draw_file_preview(uint32_t top, uint32_t height){
    display_draw_rect(window_x+10,top,window_width-20,height,COLOR_BUTTON);
    display_draw_text_sized_at(window_x+16,top+7,preview_name,COLOR_FOLDER,
                           COLOR_BUTTON,8);
    char line[46];
    uint32_t input=0;
    uint32_t y=top+26;
    while(input<file_preview_length && y+9<top+height){
        uint32_t length=0;
        while(input<file_preview_length && file_preview[input]!='\n'
              && length+1<sizeof(line)){
            char c=file_preview[input++];
            line[length++]=(c>=' ' && c<='~') ? c : '.';
        }
        if(input<file_preview_length && file_preview[input]=='\n') input++;
        line[length]='\0';
        display_draw_text_sized_at(window_x+16,y,line,COLOR_TEXT,COLOR_BUTTON,8);
        y+=12;
    }
}

static void draw_popup(void){
    if(popup==EXPLORER_POPUP_NONE) return;
    uint32_t height=popup==EXPLORER_POPUP_ENTRY ? 72 : 48;
    display_draw_rect(popup_x,popup_y,130,height,COLOR_BORDER);
    display_draw_rect(popup_x+1,popup_y+1,128,height-2,COLOR_WINDOW);
    if(popup==EXPLORER_POPUP_NEW){
        display_draw_text_sized_at(popup_x+8,popup_y+8,"New folder",COLOR_TEXT,COLOR_WINDOW,8);
        display_draw_text_sized_at(popup_x+8,popup_y+28,"New file",COLOR_TEXT,COLOR_WINDOW,8);
    } else if(popup==EXPLORER_POPUP_DELETE_CONFIRM){
        display_draw_text_sized_at(popup_x+8,popup_y+8,"Delete now",COLOR_CLOSE,COLOR_WINDOW,8);
        display_draw_text_sized_at(popup_x+8,popup_y+28,"Cancel",COLOR_TEXT,COLOR_WINDOW,8);
    } else {
        display_draw_text_sized_at(popup_x+8,popup_y+8,"Open",COLOR_TEXT,COLOR_WINDOW,8);
        display_draw_text_sized_at(popup_x+8,popup_y+28,"Rename",COLOR_TEXT,COLOR_WINDOW,8);
        display_draw_text_sized_at(popup_x+8,popup_y+48,"Delete",COLOR_CLOSE,COLOR_WINDOW,8);
    }
}

void explorer_window_draw(void){
    if(!visible) return;
    mouse_begin_framebuffer_update();
    display_draw_rect(window_x+5,window_y+5,window_width,window_height,0x11111B);
    display_draw_rect(window_x,window_y,window_width,window_height,COLOR_BORDER);
    display_draw_rect(window_x+1,window_y+1,window_width-2,
                  window_height-2,COLOR_WINDOW);
    display_draw_rect(window_x+1,window_y+1,window_width-2,TITLE_HEIGHT,COLOR_TITLE);
    display_draw_text_sized_at(window_x+12,window_y+10,"Files",COLOR_WINDOW,
                           COLOR_TITLE,9);
    display_draw_rect(window_x+window_width-27,window_y+6,18,18,COLOR_CLOSE);
    display_draw_text_sized_at(window_x+window_width-23,window_y+10,"x",
                           COLOR_WINDOW,COLOR_CLOSE,9);

    draw_button(0,"<");
    draw_button(1,"^");
    draw_button(2,"R");
    draw_button(3,"+");

    display_draw_text_sized_at(window_x+12,window_y+64,current_path,COLOR_MUTED,
                           COLOR_WINDOW,8);
    uint32_t list_top=window_y+LIST_Y;
    uint32_t row_count=visible_row_count();
    uint32_t list_height=row_count*ROW_HEIGHT;
    if(viewing_file){
        draw_file_preview(list_top,list_height);
        draw_edit_box();
        draw_popup();
        mouse_end_framebuffer_update();
        return;
    }
    display_draw_rect(window_x+10,list_top,window_width-20,list_height,COLOR_BUTTON);
    uint32_t drawn_entries=entry_count<row_count ? entry_count : row_count;
    for(uint32_t index=0;index<drawn_entries;index++){
        uint32_t row_y=list_top+index*ROW_HEIGHT;
        uint32_t background=(int32_t)index==selected_index
            ? COLOR_SELECTED : COLOR_BUTTON;
        if(background!=COLOR_BUTTON)
            display_draw_rect(window_x+10,row_y,window_width-20,ROW_HEIGHT,background);
        bool directory=(entries[index].attributes&FS_ATTRIBUTE_DIRECTORY)!=0;
        display_draw_text_sized_at(window_x+16,row_y+6,directory ? "[DIR]" : "FILE",
                               directory ? COLOR_FOLDER : COLOR_MUTED,
                               background,8);
        display_draw_text_sized_at(window_x+68,row_y+6,entries[index].name,
                               COLOR_TEXT,background,8);
    }
    if(!entry_count)
        display_draw_text_sized_at(window_x+16,list_top+8,"Directory is empty",
                               COLOR_MUTED,COLOR_BUTTON,8);
    draw_edit_box();
    draw_popup();
    mouse_end_framebuffer_update();
}

void explorer_open(uint32_t screen_width, uint32_t screen_height){
    window_width=screen_width>EXPLORER_WIDTH+20 ? EXPLORER_WIDTH : screen_width-20;
    window_height=screen_height>EXPLORER_HEIGHT+40 ? EXPLORER_HEIGHT : screen_height-40;
    if(window_x+window_width>screen_width) window_x=10;
    if(window_y+window_height>screen_height) window_y=34;
    visible=true;
    dragging=false;
    edit_mode=EXPLORER_EDIT_NONE;
    popup=EXPLORER_POPUP_NONE;
    viewing_file=false;
    current_path[0]='/';
    current_path[1]='\0';
    previous_path[0]='/';
    previous_path[1]='\0';
    previous_buttons=0;
    refresh_entries();
}

void explorer_window_close(void){
    visible=false;
    dragging=false;
    edit_mode=EXPLORER_EDIT_NONE;
    popup=EXPLORER_POPUP_NONE;
    viewing_file=false;
}

bool explorer_window_is_visible(void){ return visible; }

bool explorer_window_contains_point(int32_t x, int32_t y){
    return visible && point_inside(x,y,window_x,window_y,
                                   window_width,window_height);
}

static void navigate_up(void){
    if(viewing_file){
        viewing_file=false;
        status_text="";
        return;
    }
    uint32_t length=(uint32_t)strlen(current_path);
    if(length<=1) return;
    copy_text(previous_path,sizeof(previous_path),current_path);
    while(length>1 && current_path[length-1]!='/') length--;
    if(length>1) length--;
    current_path[length]='\0';
    refresh_entries();
}

static void open_selected(void){
    if(selected_index<0 || (uint32_t)selected_index>=entry_count){
        status_text="Select an entry";
        return;
    }
    struct fs_directory_entry *entry=&entries[selected_index];
    if(!(entry->attributes&FS_ATTRIBUTE_DIRECTORY)){
        char path[EXPLORER_PATH_CAPACITY];
        if(!build_child_path(path,entry->name)){
            status_text="Path is too long";
            return;
        }
        int64_t descriptor=userspace_syscall(SYS_FILE_OPEN,(uint64_t)path,0,0);
        if(descriptor<0){ status_text="Cannot open file"; return; }
        int64_t count=userspace_syscall(SYS_FILE_READ,(uint64_t)descriptor,
                                        (uint64_t)file_preview,
                                        FILE_PREVIEW_CAPACITY);
        (void)userspace_syscall(SYS_FILE_CLOSE,(uint64_t)descriptor,0,0);
        if(count<0){ status_text="Cannot read file"; return; }
        file_preview_length=(uint32_t)count;
        file_preview[file_preview_length]='\0';
        copy_text(preview_name,sizeof(preview_name),entry->name);
        viewing_file=true;
        popup=EXPLORER_POPUP_NONE;
        status_text=count==FILE_PREVIEW_CAPACITY ? "Preview truncated to 1 KiB" : "File preview";
        return;
    }
    char path[EXPLORER_PATH_CAPACITY];
    if(!build_child_path(path,entry->name)){
        status_text="Path is too long";
        return;
    }
    copy_text(previous_path,sizeof(previous_path),current_path);
    copy_text(current_path,sizeof(current_path),path);
    refresh_entries();
}

static void begin_edit(enum explorer_edit_mode mode){
    if(mode==EXPLORER_EDIT_RENAME){
        if(selected_index<0 || (uint32_t)selected_index>=entry_count){
            status_text="Select an entry to rename";
            return;
        }
        copy_text(edit_buffer,sizeof(edit_buffer),entries[selected_index].name);
        edit_length=(uint32_t)strlen(edit_buffer);
    } else {
        edit_buffer[0]='\0';
        edit_length=0;
    }
    edit_mode=mode;
    status_text="Enter confirms, Esc cancels";
}

static void navigate_back(void){
    if(viewing_file){ viewing_file=false; status_text=""; return; }
    char swap[EXPLORER_PATH_CAPACITY];
    copy_text(swap,sizeof(swap),current_path);
    copy_text(current_path,sizeof(current_path),previous_path);
    copy_text(previous_path,sizeof(previous_path),swap);
    refresh_entries();
}

static void delete_selected(void){
    if(selected_index<0 || (uint32_t)selected_index>=entry_count){
        status_text="Select an entry";
        return;
    }
    char path[EXPLORER_PATH_CAPACITY];
    if(!build_child_path(path,entries[selected_index].name)){
        status_text="Path is too long";
        return;
    }
    int64_t result=userspace_syscall(SYS_FILE_DELETE,(uint64_t)path,0,0);
    refresh_entries();
    status_text=result==0 ? "Deleted" : "Delete failed (folder must be empty)";
}

bool explorer_window_handle_mouse(int32_t x, int32_t y, uint8_t buttons,
                                  bool pressed, bool released,
                                  uint32_t screen_width,
                                  uint32_t screen_height){
    if(!visible) return false;
    bool right_pressed=(buttons&2) && !(previous_buttons&2);
    previous_buttons=buttons;
    if(pressed && point_inside(x,y,window_x+window_width-27,window_y+6,18,18)){
        explorer_window_close();
        return true;
    }
    if(pressed && point_inside(x,y,window_x,window_y,window_width,TITLE_HEIGHT)){
        dragging=true;
        drag_offset_x=x-(int32_t)window_x;
        drag_offset_y=y-(int32_t)window_y;
    }
    if(dragging && (buttons&1)){
        int32_t next_x=x-drag_offset_x;
        int32_t next_y=y-drag_offset_y;
        int32_t max_x=(int32_t)screen_width-(int32_t)window_width-6;
        int32_t max_y=(int32_t)screen_height-(int32_t)window_height-6;
        if(next_x<0) next_x=0;
        if(next_y<28) next_y=28;
        if(next_x>max_x) next_x=max_x;
        if(next_y>max_y) next_y=max_y;
        window_x=(uint32_t)next_x;
        window_y=(uint32_t)next_y;
    }
    if(released && dragging){
        dragging=false;
        return true;
    }
    if(right_pressed && edit_mode==EXPLORER_EDIT_NONE && !viewing_file){
        uint32_t list_top=window_y+LIST_Y;
        if(point_inside(x,y,window_x+10,list_top,window_width-20,
                        visible_row_count()*ROW_HEIGHT)){
            uint32_t index=((uint32_t)y-list_top)/ROW_HEIGHT;
            if(index<entry_count){
                selected_index=(int32_t)index;
                popup=EXPLORER_POPUP_ENTRY;
                popup_x=(uint32_t)x;
                popup_y=(uint32_t)y;
                if(popup_x+130>window_x+window_width) popup_x=window_x+window_width-134;
                if(popup_y+72>window_y+window_height) popup_y=window_y+window_height-76;
                return true;
            }
        }
    }
    if(!pressed || edit_mode!=EXPLORER_EDIT_NONE) return false;

    if(popup!=EXPLORER_POPUP_NONE){
        uint32_t popup_height=popup==EXPLORER_POPUP_ENTRY ? 72 : 48;
        if(point_inside(x,y,popup_x,popup_y,130,popup_height)){
            uint32_t action=((uint32_t)y-popup_y)/20;
            enum explorer_popup active=popup;
            popup=EXPLORER_POPUP_NONE;
            if(active==EXPLORER_POPUP_NEW){
                begin_edit(action==0 ? EXPLORER_EDIT_CREATE_DIRECTORY
                                     : EXPLORER_EDIT_CREATE_FILE);
            } else if(active==EXPLORER_POPUP_DELETE_CONFIRM){
                if(action==0) delete_selected();
            } else if(action==0) open_selected();
            else if(action==1) begin_edit(EXPLORER_EDIT_RENAME);
            else popup=EXPLORER_POPUP_DELETE_CONFIRM;
            return true;
        }
        popup=EXPLORER_POPUP_NONE;
    }

    uint32_t toolbar_y=window_y+TOOLBAR_Y;
    uint32_t button_width=toolbar_button_width();
    if(point_inside(x,y,toolbar_button_x(0),toolbar_y,button_width,24)) navigate_back();
    else if(point_inside(x,y,toolbar_button_x(1),toolbar_y,button_width,24)) navigate_up();
    else if(point_inside(x,y,toolbar_button_x(2),toolbar_y,button_width,24)) refresh_entries();
    else if(point_inside(x,y,toolbar_button_x(3),toolbar_y,button_width,24)){
        popup=EXPLORER_POPUP_NEW;
        popup_x=toolbar_button_x(3);
        popup_y=toolbar_y+26;
    }
    else {
        uint32_t list_top=window_y+LIST_Y;
        if(point_inside(x,y,window_x+10,list_top,window_width-20,
                        visible_row_count()*ROW_HEIGHT)){
            uint32_t index=((uint32_t)y-list_top)/ROW_HEIGHT;
            if(index<entry_count){
                if(selected_index==(int32_t)index){
                    open_selected();
                } else {
                    selected_index=(int32_t)index;
                    status_text="Click again to open; right-click for actions";
                }
            }
        }
    }
    return true;
}

static void submit_edit(void){
    if(!edit_length){
        status_text="Name cannot be empty";
        return;
    }
    int64_t result;
    const char *success_text;
    const char *failure_text;
    if(edit_mode==EXPLORER_EDIT_CREATE_DIRECTORY
       || edit_mode==EXPLORER_EDIT_CREATE_FILE){
        char path[EXPLORER_PATH_CAPACITY];
        if(!build_child_path(path,edit_buffer)){
            status_text="Path is too long";
            return;
        }
        bool directory=edit_mode==EXPLORER_EDIT_CREATE_DIRECTORY;
        result=userspace_syscall(directory ? SYS_DIR_CREATE : SYS_FILE_CREATE,
                                 (uint64_t)path,0,0);
        success_text=directory ? "Directory created" : "File created";
        failure_text="Create failed (8.3 names only)";
    } else {
        char path[EXPLORER_PATH_CAPACITY];
        if(selected_index<0 || (uint32_t)selected_index>=entry_count
           || !build_child_path(path,entries[selected_index].name)){
            status_text="Selected entry is unavailable";
            return;
        }
        result=userspace_syscall(SYS_FILE_RENAME,(uint64_t)path,
                                 (uint64_t)edit_buffer,0);
        success_text="Entry renamed";
        failure_text="Rename failed (8.3 names only)";
    }
    edit_mode=EXPLORER_EDIT_NONE;
    refresh_entries();
    status_text=result==0 ? success_text : failure_text;
}

bool explorer_window_handle_key(char key){
    if(!visible || edit_mode==EXPLORER_EDIT_NONE) return false;
    if(key==27){
        edit_mode=EXPLORER_EDIT_NONE;
        status_text="Cancelled";
    } else if(key=='\n' || key=='\r'){
        submit_edit();
    } else if(key=='\b' || key==127){
        if(edit_length) edit_buffer[--edit_length]='\0';
    } else if(key>=' ' && key<='~' && edit_length+1<sizeof(edit_buffer)){
        edit_buffer[edit_length++]=key;
        edit_buffer[edit_length]='\0';
    }
    explorer_window_draw();
    return true;
}
