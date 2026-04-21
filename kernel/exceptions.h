#ifndef EXCEPTIONS_H
#define EXCEPTIONS_H

#include "console.h"
#include "interrupts.h"

void exceptions_set_console(CONSOLE *con);
__attribute__((noreturn)) void handle_exception(INTERRUPT_FRAME *frame);

#endif
