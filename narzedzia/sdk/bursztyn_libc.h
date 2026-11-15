#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Deklaracje funkcji z Bursztyn libc
size_t strlen(const char* str);
int strcmp(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, size_t n);
bool zaczyna_sie_od(const char* str, const char* prefix);
void int_do_str(int wartosc, char* bufor);