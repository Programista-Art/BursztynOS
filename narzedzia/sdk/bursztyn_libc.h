#pragma once
#include <stdint.h>
#include <stdbool.h>

// Deklaracje funkcji z Bursztyn libc
int strlen(const char* str);
bool strcmp(const char* s1, const char* s2);
bool strncmp(const char* s1, const char* s2, int n);
bool zaczyna_sie_od(const char* str, const char* prefix);
void int_do_str(int wartosc, char* bufor);