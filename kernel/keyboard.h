#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

typedef enum {
    KEY_NONE = 0,
    KEY_F1 = 0x80,
    KEY_F2,
    KEY_F3,
    KEY_F4,
    KEY_F5,
    KEY_F6,
    KEY_F7,
    KEY_F8,
    KEY_F9,
    KEY_F10,
    KEY_F11,
    KEY_F12,
    KEY_ARROW_UP,
    KEY_ARROW_DOWN
} KEY_CODE;

void keyboard_init(void);

/* low-level helpers */
int keyboard_has_data(void);
uint8_t keyboard_read_scancode(void);
char keyboard_scancode_to_ascii(uint8_t scancode);

/* compatibility API */
char keyboard_getchar(void);

/* IRQ1 + ring buffer */
void keyboard_irq_handler(void);
int keyboard_buffer_has_data(void);
char keyboard_buffer_getchar(void);

#endif
