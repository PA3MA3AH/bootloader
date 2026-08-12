#include "keyboard.h"
#include "io.h"

#define PS2_DATA_PORT   0x60
#define PS2_STATUS_PORT 0x64

#define STATUS_OUTPUT_FULL 0x01
#define KEYBOARD_BUFFER_SIZE 256

static int shift_pressed = 0;
static int ctrl_pressed  = 0;
static int alt_pressed   = 0;
static int e0_prefix     = 0;

static volatile uint32_t g_kbd_head = 0;
static volatile uint32_t g_kbd_tail = 0;
static char g_kbd_buffer[KEYBOARD_BUFFER_SIZE];

/* --------------------------------------------------------------------------
 * Scancode set 1 → ASCII maps
 * -------------------------------------------------------------------------- */

static const char scancode_set1_map[128] = {
    [0x01] = 0,
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
    [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
    [0x0A] = '9', [0x0B] = '0',
    [0x0C] = '-', [0x0D] = '=',
    [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
    [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
    [0x18] = 'o', [0x19] = 'p',
    [0x1A] = '[', [0x1B] = ']',
    [0x1C] = '\n',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f',
    [0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
    [0x26] = 'l',
    [0x27] = ';', [0x28] = '\'', [0x29] = '`',
    [0x2B] = '\\',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v',
    [0x30] = 'b', [0x31] = 'n', [0x32] = 'm',
    [0x33] = ',', [0x34] = '.', [0x35] = '/',
    [0x39] = ' '
};

static const char scancode_set1_shift_map[128] = {
    [0x01] = 0,
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
    [0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*',
    [0x0A] = '(', [0x0B] = ')',
    [0x0C] = '_', [0x0D] = '+',
    [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R',
    [0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I',
    [0x18] = 'O', [0x19] = 'P',
    [0x1A] = '{', [0x1B] = '}',
    [0x1C] = '\n',
    [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F',
    [0x22] = 'G', [0x23] = 'H', [0x24] = 'J', [0x25] = 'K',
    [0x26] = 'L',
    [0x27] = ':', [0x28] = '"', [0x29] = '~',
    [0x2B] = '|',
    [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V',
    [0x30] = 'B', [0x31] = 'N', [0x32] = 'M',
    [0x33] = '<', [0x34] = '>', [0x35] = '?',
    [0x39] = ' '
};

/* --------------------------------------------------------------------------
 * Ring buffer
 * -------------------------------------------------------------------------- */

static void keyboard_buffer_clear(void) {
    g_kbd_head = 0;
    g_kbd_tail = 0;
}

static void keyboard_buffer_push(char ch) {
    uint32_t next = (g_kbd_head + 1U) % KEYBOARD_BUFFER_SIZE;
    if (next == g_kbd_tail) return;
    g_kbd_buffer[g_kbd_head] = ch;
    g_kbd_head = next;
}

int keyboard_buffer_has_data(void) {
    return g_kbd_head != g_kbd_tail;
}

char keyboard_buffer_getchar(void) {
    if (g_kbd_head == g_kbd_tail) return 0;
    char ch = g_kbd_buffer[g_kbd_tail];
    g_kbd_tail = (g_kbd_tail + 1U) % KEYBOARD_BUFFER_SIZE;
    return ch;
}

char keyboard_buffer_peek(void) {
    if (g_kbd_head == g_kbd_tail) return 0;
    return g_kbd_buffer[g_kbd_tail];
}

uint8_t keyboard_get_modifiers(void) {
    uint8_t m = 0;
    if (ctrl_pressed)  m |= MOD_CTRL;
    if (alt_pressed)   m |= MOD_ALT;
    if (shift_pressed) m |= MOD_SHIFT;
    return m;
}

/* --------------------------------------------------------------------------
 * Init / low-level
 * -------------------------------------------------------------------------- */

void keyboard_init(void) {
    shift_pressed = 0;
    ctrl_pressed  = 0;
    alt_pressed   = 0;
    e0_prefix     = 0;
    keyboard_buffer_clear();
    while (keyboard_has_data()) (void)inb(PS2_DATA_PORT);
}

int keyboard_has_data(void) {
    return (inb(PS2_STATUS_PORT) & STATUS_OUTPUT_FULL) != 0;
}

uint8_t keyboard_read_scancode(void) {
    while (!keyboard_has_data()) __asm__ __volatile__("pause");
    return inb(PS2_DATA_PORT);
}

/* --------------------------------------------------------------------------
 * Scancode → ASCII / keycode translation
 *
 * Order of processing:
 *   1. E0 prefix detection
 *   2. E0-prefixed keys  (right Ctrl/Alt, arrow keys)
 *   3. Non-E0 modifier press/release
 *   4. Function keys + Escape
 *   5. Key-release filtering
 *   6. Ctrl+letter pre-translation
 *   7. Normal / shifted character lookup
 * -------------------------------------------------------------------------- */

char keyboard_scancode_to_ascii(uint8_t scancode) {
    /* 1. E0 prefix */
    if (scancode == 0xE0) {
        e0_prefix = 1;
        return 0;
    }

    /* 2. E0-prefixed scancodes */
    if (e0_prefix) {
        e0_prefix = 0;

        /* Right Ctrl */
        if (scancode == 0x1D) { ctrl_pressed = 1; return 0; }
        if (scancode == 0x9D) { ctrl_pressed = 0; return 0; }

        /* Right Alt */
        if (scancode == 0x38) { alt_pressed = 1; return 0; }
        if (scancode == 0xB8) { alt_pressed = 0; return 0; }

        /* Arrow keys */
        switch (scancode) {
            case 0x48: return (char)KEY_ARROW_UP;
            case 0x50: return (char)KEY_ARROW_DOWN;
            case 0x4B: return (char)KEY_ARROW_LEFT;
            case 0x4D: return (char)KEY_ARROW_RIGHT;
            default:   return 0;
        }
    }

    /* 3. Non-E0 modifier press / release */
    /* Left Shift */
    if (scancode == 0x2A || scancode == 0x36) { shift_pressed = 1; return 0; }
    if (scancode == 0xAA || scancode == 0xB6) { shift_pressed = 0; return 0; }

    /* Left Ctrl */
    if (scancode == 0x1D) { ctrl_pressed = 1; return 0; }
    if (scancode == 0x9D) { ctrl_pressed = 0; return 0; }

    /* Left Alt */
    if (scancode == 0x38) { alt_pressed = 1; return 0; }
    if (scancode == 0xB8) { alt_pressed = 0; return 0; }

    /* 4. Function keys (non-E0) */
    switch (scancode) {
        case 0x3B: return (char)KEY_F1;
        case 0x3C: return (char)KEY_F2;
        case 0x3D: return (char)KEY_F3;
        case 0x3E: return (char)KEY_F4;
        case 0x3F: return (char)KEY_F5;
        case 0x40: return (char)KEY_F6;
        case 0x41: return (char)KEY_F7;
        case 0x42: return (char)KEY_F8;
        case 0x43: return (char)KEY_F9;
        case 0x44: return (char)KEY_F10;
        case 0x57: return (char)KEY_F11;
        case 0x58: return (char)KEY_F12;
        case 0x01: return (char)KEY_ESCAPE;   /* was 0 (discarded) */
        default:   break;
    }

    /* 5. Key-release scancode (bit 7 set) -- already handled Ctrl/Alt/Shift
       releases above, so anything else with bit 7 is just ignored. */
    if (scancode & 0x80) return 0;

    /* 6. Ctrl+letter pre-translation: produce 0x01–0x1A */
    if (ctrl_pressed && scancode < 128) {
        char base = scancode_set1_map[scancode];
        if (base >= 'a' && base <= 'z')
            return (char)(base - 'a' + 1);
    }

    /* 7. Normal or shifted character */
    if (shift_pressed)
        return scancode_set1_shift_map[scancode];
    return scancode_set1_map[scancode];
}

/* --------------------------------------------------------------------------
 * IRQ handler + blocking getchar
 * -------------------------------------------------------------------------- */

void keyboard_irq_handler(void) {
    while (keyboard_has_data()) {
        uint8_t scancode = inb(PS2_DATA_PORT);
        char ch = keyboard_scancode_to_ascii(scancode);
        if (ch != 0) keyboard_buffer_push(ch);
    }
}

char keyboard_getchar(void) {
    for (;;) {
        char ch = keyboard_buffer_getchar();
        if (ch != 0) return ch;
        __asm__ __volatile__("pause");
    }
}
