#include "../drivers/screen.h"
#include "../drivers/keyboard.h"
#include "../fs/fat16.h"
#include "../lib/string.h"
#include "../lib/memory.h"

#define HEXEDIT_BUFFER_SIZE 8192
#define DISPLAY_LINES 20
#define BYTES_PER_LINE 16

static unsigned char hex_buffer[HEXEDIT_BUFFER_SIZE];
static unsigned int buffer_size = 0;
static unsigned int cursor_pos = 0;
static unsigned int file_offset = 0;
static char filename[32] = "";
static int modified = 0;
static int mode = 0; // 0=hex, 1=ascii, 2=assembly

// Assembly instruction table
typedef struct {
    unsigned char opcode;
    const char *mnemonic;
    int length;
} asm_instruction_t;

static asm_instruction_t asm_table[] = {
    {0x90, "NOP", 1}, {0x50, "PUSH AX", 1}, {0x58, "POP AX", 1},
    {0xB8, "MOV AX,", 3}, {0xBB, "MOV BX,", 3}, {0xB9, "MOV CX,", 3},
    {0xBA, "MOV DX,", 3}, {0x04, "ADD AL,", 2}, {0x80, "ADD BYTE,", 3},
    {0x74, "JE", 2}, {0x75, "JNE", 2}, {0xEB, "JMP", 2},
    {0xCD, "INT", 2}, {0xC3, "RET", 1}, {0xE8, "CALL", 3},
    {0x3C, "CMP AL,", 2}, {0x88, "MOV BYTE,", 3}, {0x8A, "MOV AL,", 3},
    {0xA0, "MOV AL,[", 3}, {0xA2, "MOV [,AL", 3}, {0xF4, "HLT", 1}
};

void hexedit_display_help() {
    printf("=== Hex Editor Commands ===\n");
    printf("F1 - Help\n");
    printf("F2 - Save file\n");
    printf("F3 - Switch mode (Hex/ASCII/Assembly)\n");
    printf("F5 - Goto offset\n");
    printf("F6 - Find bytes\n");
    printf("F7 - Assemble code\n");
    printf("F8 - Fill pattern\n");
    printf("ESC - Exit\n");
    printf("WASD - Navigate\n");
    printf("0-9,A-F - Edit hex values\n");
    printf("===========================\n");
}

void hexedit_display_header() {
    printf("Hex Editor - %s | Size: %d bytes | Mode: ", 
           filename[0] ? filename : "[Untitled]", buffer_size);
    
    switch(mode) {
        case 0: printf("Hex"); break;
        case 1: printf("ASCII"); break;
        case 2: printf("Assembly"); break;
    }
    
    if (modified) printf(" | MODIFIED");
    printf("\n");
    
    printf("Offset   ");
    for (int i = 0; i < BYTES_PER_LINE; i++) {
        printf(" %02X", i);
    }
    printf("  ASCII\n");
    printf("--------");
    for (int i = 0; i < BYTES_PER_LINE; i++) {
        printf("---");
    }
    printf("  -----------------\n");
}

void hexedit_display_hex_line(int line) {
    unsigned int offset = file_offset + line * BYTES_PER_LINE;
    
    if (offset >= buffer_size) {
        printf("        ");
        for (int i = 0; i < BYTES_PER_LINE; i++) {
            printf("   ");
        }
        printf("  \n");
        return;
    }
    
    // Display offset
    printf("%08X ", offset);
    
    // Display hex bytes
    for (int i = 0; i < BYTES_PER_LINE; i++) {
        unsigned int pos = offset + i;
        if (pos >= buffer_size) {
            printf("   ");
        } else {
            if (pos == cursor_pos) {
                printf("[%02X", hex_buffer[pos]);
            } else {
                printf(" %02X", hex_buffer[pos]);
            }
        }
    }
    
    printf("  ");
    
    // Display ASCII
    for (int i = 0; i < BYTES_PER_LINE; i++) {
        unsigned int pos = offset + i;
        if (pos >= buffer_size) {
            printf(" ");
        } else {
            unsigned char c = hex_buffer[pos];
            if (c >= 32 && c < 127) {
                if (pos == cursor_pos) {
                    printf("[%c", c);
                } else {
                    printf("%c", c);
                }
            } else {
                printf(".");
            }
        }
    }
    
    printf("\n");
}

void hexedit_display_assembly_line(int line) {
    unsigned int offset = file_offset + line * 8; // Show 8 instructions per line
    
    if (offset >= buffer_size) {
        printf("\n");
        return;
    }
    
    printf("%08X: ", offset);
    
    int instructions_displayed = 0;
    unsigned int current_offset = offset;
    
    while (instructions_displayed < 8 && current_offset < buffer_size) {
        // Simple disassembly
        unsigned char opcode = hex_buffer[current_offset];
        const char *mnemonic = "DB";
        int length = 1;
        
        // Look up in instruction table
        for (int i = 0; i < sizeof(asm_table)/sizeof(asm_table[0]); i++) {
            if (asm_table[i].opcode == opcode) {
                mnemonic = asm_table[i].mnemonic;
                length = asm_table[i].length;
                break;
            }
        }
        
        // Display instruction
        printf("%s", mnemonic);
        
        // Display operands for multi-byte instructions
        if (length > 1 && current_offset + 1 < buffer_size) {
            if (length == 2) {
                printf(" %02X", hex_buffer[current_offset + 1]);
            } else if (length == 3 && current_offset + 2 < buffer_size) {
                unsigned short word = (hex_buffer[current_offset + 2] << 8) | hex_buffer[current_offset + 1];
                printf(" %04X", word);
            }
        }
        
        printf("; ");
        
        current_offset += length;
        instructions_displayed++;
        
        if (current_offset >= buffer_size) break;
    }
    
    printf("\n");
}

void hexedit_display() {
    clear_screen();
    hexedit_display_header();
    
    for (int line = 0; line < DISPLAY_LINES; line++) {
        if (mode == 2) {
            hexedit_display_assembly_line(line);
        } else {
            hexedit_display_hex_line(line);
        }
    }
    
    printf("\nCommands: F1=Help F2=Save F3=Mode F5=Goto F6=Find F7=Assemble F8=Fill ESC=Exit\n");
}

int hexedit_load_file(const char *name) {
    if (name[0] == '\0') {
        // New file
        buffer_size = 0;
        strcpy(filename, "");
        return 1;
    }
    
    file_t *file = fat16_open(name, 0);
    if (!file) {
        printf("Error: Cannot open file %s\n", name);
        return 0;
    }
    
    buffer_size = file->size;
    if (buffer_size > HEXEDIT_BUFFER_SIZE) {
        buffer_size = HEXEDIT_BUFFER_SIZE;
        printf("Warning: File truncated to %d bytes\n", HEXEDIT_BUFFER_SIZE);
    }
    
    int bytes_read = fat16_read(file, (char*)hex_buffer, buffer_size);
    if (bytes_read != buffer_size) {
        printf("Error reading file\n");
        fat16_close(file);
        return 0;
    }
    
    strcpy(filename, name);
    fat16_close(file);
    printf("Loaded %s (%d bytes)\n", filename, buffer_size);
    return 1;
}

int hexedit_save_file() {
    if (filename[0] == '\0') {
        printf("Enter filename: ");
        char newname[32];
        readline(newname, sizeof(newname));
        
        if (newname[0] == '\0') {
            printf("Save cancelled\n");
            return 0;
        }
        strcpy(filename, newname);
    }
    
    file_t *file = fat16_open(filename, 1);
    if (!file) {
        printf("Error: Cannot create file %s\n", filename);
        return 0;
    }
    
    int bytes_written = fat16_write(file, (char*)hex_buffer, buffer_size);
    if (bytes_written != buffer_size) {
        printf("Error writing file\n");
        fat16_close(file);
        return 0;
    }
    
    fat16_close(file);
    modified = 0;
    printf("Saved %s (%d bytes)\n", filename, buffer_size);
    return 1;
}

void hexedit_goto_offset() {
    printf("Enter offset (hex): ");
    char input[16];
    readline(input, sizeof(input));
    
    unsigned int offset = 0;
    for (int i = 0; input[i] != '\0'; i++) {
        char c = input[i];
        offset *= 16;
        if (c >= '0' && c <= '9') offset += c - '0';
        else if (c >= 'A' && c <= 'F') offset += c - 'A' + 10;
        else if (c >= 'a' && c <= 'f') offset += c - 'a' + 10;
    }
    
    if (offset < buffer_size) {
        cursor_pos = offset;
        file_offset = (offset / BYTES_PER_LINE) * BYTES_PER_LINE;
    } else {
        printf("Offset out of range\n");
    }
}

void hexedit_find_bytes() {
    printf("Enter hex bytes to find (e.g., '90 CD 21'): ");
    char input[64];
    readline(input, sizeof(input));
    
    unsigned char search_bytes[32];
    int search_len = 0;
    char *token = input;
    
    // Parse hex bytes
    while (*token && search_len < 32) {
        while (*token == ' ') token++;
        if (*token == '\0') break;
        
        if (token[0] >= '0' && token[0] <= '9') search_bytes[search_len] = token[0] - '0';
        else if (token[0] >= 'A' && token[0] <= 'F') search_bytes[search_len] = token[0] - 'A' + 10;
        else if (token[0] >= 'a' && token[0] <= 'f') search_bytes[search_len] = token[0] - 'a' + 10;
        else break;
        
        search_bytes[search_len] <<= 4;
        
        if (token[1] >= '0' && token[1] <= '9') search_bytes[search_len] |= token[1] - '0';
        else if (token[1] >= 'A' && token[1] <= 'F') search_bytes[search_len] |= token[1] - 'A' + 10;
        else if (token[1] >= 'a' && token[1] <= 'f') search_bytes[search_len] |= token[1] - 'a' + 10;
        else break;
        
        search_len++;
        token += 2;
    }
    
    if (search_len == 0) {
        printf("No valid bytes entered\n");
        return;
    }
    
    // Search for pattern
    for (unsigned int i = cursor_pos + 1; i <= buffer_size - search_len; i++) {
        int found = 1;
        for (int j = 0; j < search_len; j++) {
            if (hex_buffer[i + j] != search_bytes[j]) {
                found = 0;
                break;
            }
        }
        
        if (found) {
            cursor_pos = i;
            file_offset = (i / BYTES_PER_LINE) * BYTES_PER_LINE;
            printf("Found at offset %08X\n", i);
            return;
        }
    }
    
    printf("Pattern not found\n");
}

void hexedit_assemble_code() {
    printf("Assembly at offset %08X\n", cursor_pos);
    printf("Enter assembly (e.g., 'MOV AX,1234' or 'NOP'): ");
    char input[64];
    readline(input, sizeof(input));
    
    // Simple assembler
    if (strncmp(input, "NOP", 3) == 0) {
        hex_buffer[cursor_pos++] = 0x90;
        modified = 1;
    }
    else if (strncmp(input, "RET", 3) == 0) {
        hex_buffer[cursor_pos++] = 0xC3;
        modified = 1;
    }
    else if (strncmp(input, "HLT", 3) == 0) {
        hex_buffer[cursor_pos++] = 0xF4;
        modified = 1;
    }
    else if (strncmp(input, "INT", 3) == 0) {
        // Parse INT instruction
        char *param = input + 3;
        while (*param == ' ') param++;
        
        unsigned char int_num = 0;
        if (*param >= '0' && *param <= '9') {
            int_num = (*param - '0') * 10;
            if (param[1] >= '0' && param[1] <= '9') {
                int_num += param[1] - '0';
            }
        }
        
        hex_buffer[cursor_pos++] = 0xCD;
        hex_buffer[cursor_pos++] = int_num;
        modified = 1;
    }
    else if (strncmp(input, "MOV AX,", 7) == 0) {
        // Parse MOV AX,imm16
        char *param = input + 7;
        unsigned short value = 0;
        
        for (int i = 0; param[i] != '\0' && i < 4; i++) {
            value <<= 4;
            if (param[i] >= '0' && param[i] <= '9') value |= param[i] - '0';
            else if (param[i] >= 'A' && param[i] <= 'F') value |= param[i] - 'A' + 10;
            else if (param[i] >= 'a' && param[i] <= 'f') value |= param[i] - 'a' + 10;
        }
        
        hex_buffer[cursor_pos++] = 0xB8;
        hex_buffer[cursor_pos++] = value & 0xFF;
        hex_buffer[cursor_pos++] = (value >> 8) & 0xFF;
        modified = 1;
    }
    else {
        printf("Unknown instruction or syntax error\n");
    }
}

void hexedit_fill_pattern() {
    printf("Fill from %08X to ", cursor_pos);
    char input[16];
    readline(input, sizeof(input));
    
    unsigned int end_offset = 0;
    for (int i = 0; input[i] != '\0'; i++) {
        char c = input[i];
        end_offset *= 16;
        if (c >= '0' && c <= '9') end_offset += c - '0';
        else if (c >= 'A' && c <= 'F') end_offset += c - 'A' + 10;
        else if (c >= 'a' && c <= 'f') end_offset += c - 'a' + 10;
    }
    
    if (end_offset <= cursor_pos || end_offset >= HEXEDIT_BUFFER_SIZE) {
        printf("Invalid range\n");
        return;
    }
    
    printf("Fill with byte (hex): ");
    readline(input, sizeof(input));
    
    unsigned char fill_byte = 0;
    if (input[0] >= '0' && input[0] <= '9') fill_byte = (input[0] - '0') << 4;
    else if (input[0] >= 'A' && input[0] <= 'F') fill_byte = (input[0] - 'A' + 10) << 4;
    else if (input[0] >= 'a' && input[0] <= 'f') fill_byte = (input[0] - 'a' + 10) << 4;
    
    if (input[1] >= '0' && input[1] <= '9') fill_byte |= input[1] - '0';
    else if (input[1] >= 'A' && input[1] <= 'F') fill_byte |= input[1] - 'A' + 10;
    else if (input[1] >= 'a' && input[1] <= 'f') fill_byte |= input[1] - 'a' + 10;
    
    for (unsigned int i = cursor_pos; i <= end_offset && i < HEXEDIT_BUFFER_SIZE; i++) {
        hex_buffer[i] = fill_byte;
    }
    
    if (end_offset >= buffer_size) {
        buffer_size = end_offset + 1;
    }
    
    modified = 1;
    printf("Filled %d bytes with %02X\n", end_offset - cursor_pos + 1, fill_byte);
}

void hexedit_handle_input() {
    int function_key_mode = 0; // 0 = normal, 1 = function key
    
    while (1) {
        hexedit_display();
        
        char c = getchar();
        
        // Handle function keys (F1-F8)
        if (c == 0) {
            function_key_mode = 1;
            continue;
        }
        
        if (function_key_mode) {
            function_key_mode = 0;
            switch (c) {
                case 0x3B: // F1
                    hexedit_display_help();
                    printf("Press any key to continue...");
                    getchar();
                    break;
                case 0x3C: // F2
                    hexedit_save_file();
                    printf("Press any key to continue...");
                    getchar();
                    break;
                case 0x3D: // F3
                    mode = (mode + 1) % 3;
                    break;
                case 0x3F: // F5
                    hexedit_goto_offset();
                    printf("Press any key to continue...");
                    getchar();
                    break;
                case 0x40: // F6
                    hexedit_find_bytes();
                    printf("Press any key to continue...");
                    getchar();
                    break;
                case 0x41: // F7
                    hexedit_assemble_code();
                    printf("Press any key to continue...");
                    getchar();
                    break;
                case 0x42: // F8
                    hexedit_fill_pattern();
                    printf("Press any key to continue...");
                    getchar();
                    break;
            }
            continue;
        }
        
        // Handle normal keys
        switch (c) {
            case 27: // ESC
                if (modified) {
                    printf("Unsaved changes! Save? (y/n): ");
                    c = getchar();
                    if (c == 'y' || c == 'Y') {
                        hexedit_save_file();
                    }
                }
                return;
                
            // Navigation
            case 'w': // Up
            case 'W':
                if (cursor_pos >= BYTES_PER_LINE) {
                    cursor_pos -= BYTES_PER_LINE;
                    if (cursor_pos < file_offset) {
                        file_offset -= BYTES_PER_LINE;
                    }
                }
                break;
                
            case 's': // Down  
            case 'S':
                if (cursor_pos + BYTES_PER_LINE < HEXEDIT_BUFFER_SIZE) {
                    cursor_pos += BYTES_PER_LINE;
                    if (cursor_pos >= file_offset + (DISPLAY_LINES * BYTES_PER_LINE)) {
                        file_offset += BYTES_PER_LINE;
                    }
                }
                break;
                
            case 'a': // Left
                if (cursor_pos > 0) cursor_pos--;
                break;
                
            case 'd': // Right
                if (cursor_pos < buffer_size - 1) cursor_pos++;
                break;
                
            // Hex digit input
            default:
                if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')) {
                    unsigned char nibble;
                    if (c >= '0' && c <= '9') nibble = c - '0';
                    else if (c >= 'A' && c <= 'F') nibble = c - 'A' + 10;
                    else nibble = c - 'a' + 10;
                    
                    if (mode == 0) { // Hex mode
                        // Toggle between high and low nibble
                        static int high_nibble = 1;
                        if (high_nibble) {
                            hex_buffer[cursor_pos] = nibble << 4;
                        } else {
                            hex_buffer[cursor_pos] |= nibble;
                            if (cursor_pos < buffer_size - 1) cursor_pos++;
                        }
                        high_nibble = !high_nibble;
                        modified = 1;
                    } else if (mode == 1) { // ASCII mode
                        hex_buffer[cursor_pos] = c;
                        if (cursor_pos < buffer_size - 1) cursor_pos++;
                        modified = 1;
                    }
                }
                break;
        }
    }
}

void cmd_hexedit(char *args) {
    printf("=== PureC Hex Editor ===\n");
    printf("Machine Code & Assembly Editor\n");
    
    if (args[0] != '\0') {
        if (!hexedit_load_file(args)) {
            printf("Creating new file: %s\n", args);
            strcpy(filename, args);
            buffer_size = 0;
        }
    } else {
        printf("New file mode\n");
        buffer_size = 0;
        filename[0] = '\0';
    }
    
    cursor_pos = 0;
    file_offset = 0;
    modified = 0;
    mode = 0;
    
    hexedit_handle_input();
    printf("Hex editor closed\n");
}