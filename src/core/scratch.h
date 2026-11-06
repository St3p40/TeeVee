#pragma once
#include <stdlib.h>

// A temporary heap buffer for one request (JSON replies, upload chunks). The big static buffers these used to be cost
// 35 KB of the board's scarce static RAM for the whole run; this holds the memory only while a request is handled.
struct Scratch {
    char* p;
    size_t n;
    explicit Scratch(size_t size) : p((char*)malloc(size)), n(size) {}
    ~Scratch() { free(p); }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    explicit operator bool() const { return p != nullptr; }
};
