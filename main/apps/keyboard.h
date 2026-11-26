// keyboard.h - round on-screen keyboard (a screen of its own)
#pragma once

typedef struct {
    const char *title;
    const char *initial;
    int maxlen;
    void (*done)(const char *text, void *user);   // text = NULL when cancelled
    void *user;
} kb_arg_t;

void keyboard_open(kb_arg_t *a);   // a must stay valid until done() is called
