#include "explorer.h"

extern "C" {
#include "../../drivers/mouse/ps2_mouse.h"
#include "../../lib/string.h"
}

#include "../display.h"
#include "../fs.h"

namespace explorer {

constexpr uint32_t PATH_CAPACITY = 128;
constexpr uint32_t ENTRY_LIMIT = 12;
constexpr uint32_t NAME_CAPACITY = FS_DIRECTORY_NAME_CAPACITY;
constexpr uint32_t DEFAULT_WIDTH = 420;
constexpr uint32_t DEFAULT_HEIGHT = 350;
constexpr uint32_t TITLE_HEIGHT = 30;
constexpr uint32_t TOOLBAR_Y = 38;
constexpr uint32_t LIST_Y = 72;
constexpr uint32_t ROW_HEIGHT = 20;
constexpr uint32_t FILE_PREVIEW_CAPACITY = 1024;

constexpr uint32_t COLOR_BORDER = 0x45475A;
constexpr uint32_t COLOR_WINDOW = 0x1E1E2E;
constexpr uint32_t COLOR_TITLE = 0x89B4FA;
constexpr uint32_t COLOR_TEXT = 0xCDD6F4;
constexpr uint32_t COLOR_MUTED = 0x9399B2;
constexpr uint32_t COLOR_BUTTON = 0x313244;
constexpr uint32_t COLOR_SELECTED = 0x585B70;
constexpr uint32_t COLOR_FOLDER = 0xF9E2AF;
constexpr uint32_t COLOR_CLOSE = 0xF38BA8;
constexpr uint32_t COLOR_OK = 0xA6E3A1;

enum class EditMode : uint8_t {
    None,
    CreateDirectory,
    CreateFile,
    Rename
};

enum class Popup : uint8_t {
    None,
    New,
    Entry,
    DeleteConfirm
};

struct Rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;

    bool contains(int32_t point_x, int32_t point_y) const {
        return point_x >= static_cast<int32_t>(x)
            && point_y >= static_cast<int32_t>(y)
            && point_x < static_cast<int32_t>(x + width)
            && point_y < static_cast<int32_t>(y + height);
    }
};

class ExplorerWindow {
public:
    void open(uint32_t screen_width, uint32_t screen_height);
    void draw();
    void close();
    bool is_visible() const { return visible_; }
    bool contains_point(int32_t x, int32_t y) const;
    bool handle_mouse(int32_t x, int32_t y, uint8_t buttons,
                      bool pressed, bool released,
                      uint32_t screen_width, uint32_t screen_height);
    bool handle_key(char key);

private:
    uint32_t toolbar_button_width() const { return 28; }
    uint32_t toolbar_button_x(uint32_t index) const {
        return window_x_ + 10 + index * 34;
    }
    uint32_t visible_row_count() const;
    Rect bounds() const {
        return Rect{window_x_, window_y_, window_width_, window_height_};
    }

    void copy_text(char *destination, uint32_t capacity, const char *source);
    bool build_child_path(char output[PATH_CAPACITY], const char *name) const;
    void refresh_entries();
    void draw_button(uint32_t index, const char *label) const;
    void draw_edit_box() const;
    void draw_file_preview(uint32_t top, uint32_t height) const;
    void draw_popup() const;
    void navigate_up();
    void navigate_back();
    void open_selected();
    void begin_edit(EditMode mode);
    void delete_selected();
    void submit_edit();

    uint32_t window_x_ = 34;
    uint32_t window_y_ = 72;
    uint32_t window_width_ = DEFAULT_WIDTH;
    uint32_t window_height_ = DEFAULT_HEIGHT;
    bool visible_ = false;
    bool dragging_ = false;
    int32_t drag_offset_x_ = 0;
    int32_t drag_offset_y_ = 0;
    char current_path_[PATH_CAPACITY] = "/";
    char previous_path_[PATH_CAPACITY] = "/";
    fs_directory_entry entries_[ENTRY_LIMIT] = {};
    uint32_t entry_count_ = 0;
    int32_t selected_index_ = -1;
    EditMode edit_mode_ = EditMode::None;
    char edit_buffer_[NAME_CAPACITY] = {};
    uint32_t edit_length_ = 0;
    const char *status_text_ = "";
    Popup popup_ = Popup::None;
    uint32_t popup_x_ = 0;
    uint32_t popup_y_ = 0;
    uint8_t previous_buttons_ = 0;
    bool viewing_file_ = false;
    char preview_name_[NAME_CAPACITY] = {};
    char file_preview_[FILE_PREVIEW_CAPACITY + 1] = {};
    uint32_t file_preview_length_ = 0;
};

uint32_t ExplorerWindow::visible_row_count() const {
    constexpr uint32_t reserved = LIST_Y + 48;
    if(window_height_ <= reserved) return 1;
    uint32_t rows = (window_height_ - reserved) / ROW_HEIGHT;
    return rows < ENTRY_LIMIT ? rows : ENTRY_LIMIT;
}

void ExplorerWindow::copy_text(char *destination, uint32_t capacity,
                               const char *source) {
    if(!capacity) return;
    uint32_t index = 0;
    while(source[index] && index + 1 < capacity) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

bool ExplorerWindow::build_child_path(char output[PATH_CAPACITY],
                                      const char *name) const {
    uint32_t path_length = static_cast<uint32_t>(strlen(current_path_));
    uint32_t name_length = static_cast<uint32_t>(strlen(name));
    bool root = path_length == 1 && current_path_[0] == '/';
    uint32_t required = path_length + name_length + (root ? 0U : 1U);
    if(required >= PATH_CAPACITY) return false;
    memcpy(output, current_path_, path_length);
    uint32_t offset = path_length;
    if(!root) output[offset++] = '/';
    memcpy(&output[offset], name, name_length + 1);
    return true;
}

void ExplorerWindow::refresh_entries() {
    int64_t count = fs_list(current_path_, entries_, ENTRY_LIMIT);
    if(count < 0) {
        entry_count_ = 0;
        selected_index_ = -1;
        status_text_ = "Cannot read directory";
        return;
    }
    entry_count_ = static_cast<uint32_t>(count);
    selected_index_ = -1;
    status_text_ = entry_count_ > visible_row_count()
        ? "More entries do not fit" : "";
}

void ExplorerWindow::draw_button(uint32_t index, const char *label) const {
    uint32_t x = toolbar_button_x(index);
    uint32_t y = window_y_ + TOOLBAR_Y;
    display_draw_rect(x, y, toolbar_button_width(), 24, COLOR_BUTTON);
    display_draw_text_sized_at(x + 5, y + 8, label, COLOR_TEXT,
                               COLOR_BUTTON, 7);
}

void ExplorerWindow::draw_edit_box() const {
    uint32_t y = window_y_ + window_height_ - 38;
    display_draw_rect(window_x_ + 10, y, window_width_ - 20, 28,
                      COLOR_BUTTON);
    if(edit_mode_ == EditMode::None) {
        display_draw_text_sized_at(window_x_ + 16, y + 9, status_text_,
                                   COLOR_MUTED, COLOR_BUTTON, 8);
        return;
    }
    const char *label = edit_mode_ == EditMode::CreateDirectory
        ? "New folder: " : edit_mode_ == EditMode::CreateFile
        ? "New file: " : "Rename: ";
    display_draw_text_sized_at(window_x_ + 16, y + 9, label, COLOR_OK,
                               COLOR_BUTTON, 8);
    display_draw_text_sized_at(window_x_ + 112, y + 9, edit_buffer_,
                               COLOR_TEXT, COLOR_BUTTON, 8);
    uint32_t cursor_x = window_x_ + 112 + edit_length_ * 8;
    display_draw_rect(cursor_x, y + 7, 2, 13, COLOR_TEXT);
}

void ExplorerWindow::draw_file_preview(uint32_t top, uint32_t height) const {
    display_draw_rect(window_x_ + 10, top, window_width_ - 20, height,
                      COLOR_BUTTON);
    display_draw_text_sized_at(window_x_ + 16, top + 7, preview_name_,
                               COLOR_FOLDER, COLOR_BUTTON, 8);
    char line[46];
    uint32_t input = 0;
    uint32_t y = top + 26;
    while(input < file_preview_length_ && y + 9 < top + height) {
        uint32_t length = 0;
        while(input < file_preview_length_ && file_preview_[input] != '\n'
              && length + 1 < sizeof(line)) {
            char c = file_preview_[input++];
            line[length++] = c >= ' ' && c <= '~' ? c : '.';
        }
        if(input < file_preview_length_ && file_preview_[input] == '\n')
            input++;
        line[length] = '\0';
        display_draw_text_sized_at(window_x_ + 16, y, line, COLOR_TEXT,
                                   COLOR_BUTTON, 8);
        y += 12;
    }
}

void ExplorerWindow::draw_popup() const {
    if(popup_ == Popup::None) return;
    uint32_t height = popup_ == Popup::Entry ? 72 : 48;
    display_draw_rect(popup_x_, popup_y_, 130, height, COLOR_BORDER);
    display_draw_rect(popup_x_ + 1, popup_y_ + 1, 128, height - 2,
                      COLOR_WINDOW);
    if(popup_ == Popup::New) {
        display_draw_text_sized_at(popup_x_ + 8, popup_y_ + 8, "New folder",
                                   COLOR_TEXT, COLOR_WINDOW, 8);
        display_draw_text_sized_at(popup_x_ + 8, popup_y_ + 28, "New file",
                                   COLOR_TEXT, COLOR_WINDOW, 8);
    } else if(popup_ == Popup::DeleteConfirm) {
        display_draw_text_sized_at(popup_x_ + 8, popup_y_ + 8, "Delete now",
                                   COLOR_CLOSE, COLOR_WINDOW, 8);
        display_draw_text_sized_at(popup_x_ + 8, popup_y_ + 28, "Cancel",
                                   COLOR_TEXT, COLOR_WINDOW, 8);
    } else {
        display_draw_text_sized_at(popup_x_ + 8, popup_y_ + 8, "Open",
                                   COLOR_TEXT, COLOR_WINDOW, 8);
        display_draw_text_sized_at(popup_x_ + 8, popup_y_ + 28, "Rename",
                                   COLOR_TEXT, COLOR_WINDOW, 8);
        display_draw_text_sized_at(popup_x_ + 8, popup_y_ + 48, "Delete",
                                   COLOR_CLOSE, COLOR_WINDOW, 8);
    }
}

void ExplorerWindow::draw() {
    if(!visible_) return;
    mouse_begin_framebuffer_update();
    display_draw_rect(window_x_ + 5, window_y_ + 5, window_width_,
                      window_height_, 0x11111B);
    display_draw_rect(window_x_, window_y_, window_width_, window_height_,
                      COLOR_BORDER);
    display_draw_rect(window_x_ + 1, window_y_ + 1, window_width_ - 2,
                      window_height_ - 2, COLOR_WINDOW);
    display_draw_rect(window_x_ + 1, window_y_ + 1, window_width_ - 2,
                      TITLE_HEIGHT, COLOR_TITLE);
    display_draw_text_sized_at(window_x_ + 12, window_y_ + 10, "Files",
                               COLOR_WINDOW, COLOR_TITLE, 9);
    display_draw_rect(window_x_ + window_width_ - 27, window_y_ + 6, 18, 18,
                      COLOR_CLOSE);
    display_draw_text_sized_at(window_x_ + window_width_ - 23, window_y_ + 10,
                               "x", COLOR_WINDOW, COLOR_CLOSE, 9);

    draw_button(0, "<");
    draw_button(1, "^");
    draw_button(2, "R");
    draw_button(3, "+");

    display_draw_text_sized_at(window_x_ + 12, window_y_ + 64, current_path_,
                               COLOR_MUTED, COLOR_WINDOW, 8);
    uint32_t list_top = window_y_ + LIST_Y;
    uint32_t row_count = visible_row_count();
    uint32_t list_height = row_count * ROW_HEIGHT;
    if(viewing_file_) {
        draw_file_preview(list_top, list_height);
        draw_edit_box();
        draw_popup();
        mouse_end_framebuffer_update();
        return;
    }

    display_draw_rect(window_x_ + 10, list_top, window_width_ - 20,
                      list_height, COLOR_BUTTON);
    uint32_t drawn_entries = entry_count_ < row_count ? entry_count_ : row_count;
    for(uint32_t index = 0; index < drawn_entries; index++) {
        uint32_t row_y = list_top + index * ROW_HEIGHT;
        uint32_t background = static_cast<int32_t>(index) == selected_index_
            ? COLOR_SELECTED : COLOR_BUTTON;
        if(background != COLOR_BUTTON)
            display_draw_rect(window_x_ + 10, row_y, window_width_ - 20,
                              ROW_HEIGHT, background);
        bool directory = (entries_[index].attributes & FS_ATTRIBUTE_DIRECTORY)
            != 0;
        display_draw_text_sized_at(window_x_ + 16, row_y + 6,
                                   directory ? "[DIR]" : "FILE",
                                   directory ? COLOR_FOLDER : COLOR_MUTED,
                                   background, 8);
        display_draw_text_sized_at(window_x_ + 68, row_y + 6,
                                   entries_[index].name, COLOR_TEXT,
                                   background, 8);
    }
    if(!entry_count_)
        display_draw_text_sized_at(window_x_ + 16, list_top + 8,
                                   "Directory is empty", COLOR_MUTED,
                                   COLOR_BUTTON, 8);
    draw_edit_box();
    draw_popup();
    mouse_end_framebuffer_update();
}

void ExplorerWindow::open(uint32_t screen_width, uint32_t screen_height) {
    window_width_ = screen_width > DEFAULT_WIDTH + 20
        ? DEFAULT_WIDTH : screen_width - 20;
    window_height_ = screen_height > DEFAULT_HEIGHT + 40
        ? DEFAULT_HEIGHT : screen_height - 40;
    if(window_x_ + window_width_ > screen_width) window_x_ = 10;
    if(window_y_ + window_height_ > screen_height) window_y_ = 34;
    visible_ = true;
    dragging_ = false;
    edit_mode_ = EditMode::None;
    popup_ = Popup::None;
    viewing_file_ = false;
    current_path_[0] = '/';
    current_path_[1] = '\0';
    previous_path_[0] = '/';
    previous_path_[1] = '\0';
    previous_buttons_ = 0;
    refresh_entries();
}

void ExplorerWindow::close() {
    visible_ = false;
    dragging_ = false;
    edit_mode_ = EditMode::None;
    popup_ = Popup::None;
    viewing_file_ = false;
}

bool ExplorerWindow::contains_point(int32_t x, int32_t y) const {
    return visible_ && bounds().contains(x, y);
}

void ExplorerWindow::navigate_up() {
    if(viewing_file_) {
        viewing_file_ = false;
        status_text_ = "";
        return;
    }
    uint32_t length = static_cast<uint32_t>(strlen(current_path_));
    if(length <= 1) return;
    copy_text(previous_path_, sizeof(previous_path_), current_path_);
    while(length > 1 && current_path_[length - 1] != '/') length--;
    if(length > 1) length--;
    current_path_[length] = '\0';
    refresh_entries();
}

void ExplorerWindow::open_selected() {
    if(selected_index_ < 0 || static_cast<uint32_t>(selected_index_) >= entry_count_) {
        status_text_ = "Select an entry";
        return;
    }
    fs_directory_entry *entry = &entries_[selected_index_];
    if(!(entry->attributes & FS_ATTRIBUTE_DIRECTORY)) {
        char path[PATH_CAPACITY];
        if(!build_child_path(path, entry->name)) {
            status_text_ = "Path is too long";
            return;
        }
        int64_t descriptor = fs_open(path);
        if(descriptor < 0) {
            status_text_ = "Cannot open file";
            return;
        }
        int64_t count = fs_read(static_cast<int32_t>(descriptor),
                                file_preview_, FILE_PREVIEW_CAPACITY);
        (void)fs_close(static_cast<int32_t>(descriptor));
        if(count < 0) {
            status_text_ = "Cannot read file";
            return;
        }
        file_preview_length_ = static_cast<uint32_t>(count);
        file_preview_[file_preview_length_] = '\0';
        copy_text(preview_name_, sizeof(preview_name_), entry->name);
        viewing_file_ = true;
        popup_ = Popup::None;
        status_text_ = count == FILE_PREVIEW_CAPACITY
            ? "Preview truncated to 1 KiB" : "File preview";
        return;
    }
    char path[PATH_CAPACITY];
    if(!build_child_path(path, entry->name)) {
        status_text_ = "Path is too long";
        return;
    }
    copy_text(previous_path_, sizeof(previous_path_), current_path_);
    copy_text(current_path_, sizeof(current_path_), path);
    refresh_entries();
}

void ExplorerWindow::begin_edit(EditMode mode) {
    if(mode == EditMode::Rename) {
        if(selected_index_ < 0
           || static_cast<uint32_t>(selected_index_) >= entry_count_) {
            status_text_ = "Select an entry to rename";
            return;
        }
        copy_text(edit_buffer_, sizeof(edit_buffer_),
                  entries_[selected_index_].name);
        edit_length_ = static_cast<uint32_t>(strlen(edit_buffer_));
    } else {
        edit_buffer_[0] = '\0';
        edit_length_ = 0;
    }
    edit_mode_ = mode;
    status_text_ = "Enter confirms, Esc cancels";
}

void ExplorerWindow::navigate_back() {
    if(viewing_file_) {
        viewing_file_ = false;
        status_text_ = "";
        return;
    }
    char swap[PATH_CAPACITY];
    copy_text(swap, sizeof(swap), current_path_);
    copy_text(current_path_, sizeof(current_path_), previous_path_);
    copy_text(previous_path_, sizeof(previous_path_), swap);
    refresh_entries();
}

void ExplorerWindow::delete_selected() {
    if(selected_index_ < 0 || static_cast<uint32_t>(selected_index_) >= entry_count_) {
        status_text_ = "Select an entry";
        return;
    }
    char path[PATH_CAPACITY];
    if(!build_child_path(path, entries_[selected_index_].name)) {
        status_text_ = "Path is too long";
        return;
    }
    int64_t result = fs_delete(path);
    refresh_entries();
    status_text_ = result == 0 ? "Deleted" : "Delete failed (folder must be empty)";
}

bool ExplorerWindow::handle_mouse(int32_t x, int32_t y, uint8_t buttons,
                                  bool pressed, bool released,
                                  uint32_t screen_width,
                                  uint32_t screen_height) {
    if(!visible_) return false;
    bool right_pressed = (buttons & 2) && !(previous_buttons_ & 2);
    previous_buttons_ = buttons;
    if(pressed && Rect{window_x_ + window_width_ - 27, window_y_ + 6, 18, 18}.contains(x, y)) {
        close();
        return true;
    }
    if(pressed && Rect{window_x_, window_y_, window_width_, TITLE_HEIGHT}.contains(x, y)) {
        dragging_ = true;
        drag_offset_x_ = x - static_cast<int32_t>(window_x_);
        drag_offset_y_ = y - static_cast<int32_t>(window_y_);
    }
    if(dragging_ && (buttons & 1)) {
        int32_t next_x = x - drag_offset_x_;
        int32_t next_y = y - drag_offset_y_;
        int32_t max_x = static_cast<int32_t>(screen_width)
            - static_cast<int32_t>(window_width_) - 6;
        int32_t max_y = static_cast<int32_t>(screen_height)
            - static_cast<int32_t>(window_height_) - 6;
        if(next_x < 0) next_x = 0;
        if(next_y < 28) next_y = 28;
        if(next_x > max_x) next_x = max_x;
        if(next_y > max_y) next_y = max_y;
        window_x_ = static_cast<uint32_t>(next_x);
        window_y_ = static_cast<uint32_t>(next_y);
    }
    if(released && dragging_) {
        dragging_ = false;
        return true;
    }
    if(right_pressed && edit_mode_ == EditMode::None && !viewing_file_) {
        uint32_t list_top = window_y_ + LIST_Y;
        if(Rect{window_x_ + 10, list_top, window_width_ - 20,
                visible_row_count() * ROW_HEIGHT}.contains(x, y)) {
            uint32_t index = (static_cast<uint32_t>(y) - list_top) / ROW_HEIGHT;
            if(index < entry_count_) {
                selected_index_ = static_cast<int32_t>(index);
                popup_ = Popup::Entry;
                popup_x_ = static_cast<uint32_t>(x);
                popup_y_ = static_cast<uint32_t>(y);
                if(popup_x_ + 130 > window_x_ + window_width_)
                    popup_x_ = window_x_ + window_width_ - 134;
                if(popup_y_ + 72 > window_y_ + window_height_)
                    popup_y_ = window_y_ + window_height_ - 76;
                return true;
            }
        }
    }
    if(!pressed || edit_mode_ != EditMode::None) return false;

    if(popup_ != Popup::None) {
        uint32_t popup_height = popup_ == Popup::Entry ? 72 : 48;
        if(Rect{popup_x_, popup_y_, 130, popup_height}.contains(x, y)) {
            uint32_t action = (static_cast<uint32_t>(y) - popup_y_) / 20;
            Popup active = popup_;
            popup_ = Popup::None;
            if(active == Popup::New) {
                begin_edit(action == 0 ? EditMode::CreateDirectory
                                       : EditMode::CreateFile);
            } else if(active == Popup::DeleteConfirm) {
                if(action == 0) delete_selected();
            } else if(action == 0) {
                open_selected();
            } else if(action == 1) {
                begin_edit(EditMode::Rename);
            } else {
                popup_ = Popup::DeleteConfirm;
            }
            return true;
        }
        popup_ = Popup::None;
    }

    uint32_t toolbar_y = window_y_ + TOOLBAR_Y;
    uint32_t button_width = toolbar_button_width();
    if(Rect{toolbar_button_x(0), toolbar_y, button_width, 24}.contains(x, y))
        navigate_back();
    else if(Rect{toolbar_button_x(1), toolbar_y, button_width, 24}.contains(x, y))
        navigate_up();
    else if(Rect{toolbar_button_x(2), toolbar_y, button_width, 24}.contains(x, y))
        refresh_entries();
    else if(Rect{toolbar_button_x(3), toolbar_y, button_width, 24}.contains(x, y)) {
        popup_ = Popup::New;
        popup_x_ = toolbar_button_x(3);
        popup_y_ = toolbar_y + 26;
    } else {
        uint32_t list_top = window_y_ + LIST_Y;
        if(Rect{window_x_ + 10, list_top, window_width_ - 20,
                visible_row_count() * ROW_HEIGHT}.contains(x, y)) {
            uint32_t index = (static_cast<uint32_t>(y) - list_top) / ROW_HEIGHT;
            if(index < entry_count_) {
                if(selected_index_ == static_cast<int32_t>(index)) {
                    open_selected();
                } else {
                    selected_index_ = static_cast<int32_t>(index);
                    status_text_ = "Click again to open; right-click for actions";
                }
            }
        }
    }
    return true;
}

void ExplorerWindow::submit_edit() {
    if(!edit_length_) {
        status_text_ = "Name cannot be empty";
        return;
    }
    int64_t result;
    const char *success_text;
    const char *failure_text;
    if(edit_mode_ == EditMode::CreateDirectory
       || edit_mode_ == EditMode::CreateFile) {
        char path[PATH_CAPACITY];
        if(!build_child_path(path, edit_buffer_)) {
            status_text_ = "Path is too long";
            return;
        }
        bool directory = edit_mode_ == EditMode::CreateDirectory;
        result = directory ? fs_create_directory(path) : fs_create_file(path);
        success_text = directory ? "Directory created" : "File created";
        failure_text = "Create failed (8.3 names only)";
    } else {
        char path[PATH_CAPACITY];
        if(selected_index_ < 0
           || static_cast<uint32_t>(selected_index_) >= entry_count_
           || !build_child_path(path, entries_[selected_index_].name)) {
            status_text_ = "Selected entry is unavailable";
            return;
        }
        result = fs_rename(path, edit_buffer_);
        success_text = "Entry renamed";
        failure_text = "Rename failed (8.3 names only)";
    }
    edit_mode_ = EditMode::None;
    refresh_entries();
    status_text_ = result == 0 ? success_text : failure_text;
}

bool ExplorerWindow::handle_key(char key) {
    if(!visible_ || edit_mode_ == EditMode::None) return false;
    if(key == 27) {
        edit_mode_ = EditMode::None;
        status_text_ = "Cancelled";
    } else if(key == '\n' || key == '\r') {
        submit_edit();
    } else if(key == '\b' || key == 127) {
        if(edit_length_) edit_buffer_[--edit_length_] = '\0';
    } else if(key >= ' ' && key <= '~' && edit_length_ + 1 < sizeof(edit_buffer_)) {
        edit_buffer_[edit_length_++] = key;
        edit_buffer_[edit_length_] = '\0';
    }
    draw();
    return true;
}

static ExplorerWindow window;

}

extern "C" void explorer_open(uint32_t screen_width, uint32_t screen_height) {
    explorer::window.open(screen_width, screen_height);
}

extern "C" void explorer_window_draw(void) {
    explorer::window.draw();
}

extern "C" void explorer_window_close(void) {
    explorer::window.close();
}

extern "C" bool explorer_window_is_visible(void) {
    return explorer::window.is_visible();
}

extern "C" bool explorer_window_contains_point(int32_t x, int32_t y) {
    return explorer::window.contains_point(x, y);
}

extern "C" bool explorer_window_handle_mouse(int32_t x, int32_t y,
                                             uint8_t buttons, bool pressed,
                                             bool released,
                                             uint32_t screen_width,
                                             uint32_t screen_height) {
    return explorer::window.handle_mouse(x, y, buttons, pressed, released,
                                         screen_width, screen_height);
}

extern "C" bool explorer_window_handle_key(char key) {
    return explorer::window.handle_key(key);
}
