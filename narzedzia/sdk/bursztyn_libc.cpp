#include "bursztyn_libc.h"

int strlen(const char* str) {
    int len = 0; 
    while (str[len] != '\0') len++; 
    return len;
}

bool strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 == *(const unsigned char*)s2;
}

bool strncmp(const char* s1, const char* s2, int n) {
    while (n && *s1 && (*s1 == *s2)) { s1++; s2++; n--; }
    return n == 0 || *(const unsigned char*)s1 == *(const unsigned char*)s2;
}

bool zaczyna_sie_od(const char* str, const char* prefix) {
    while (*prefix) {
        if (*str != *prefix) return false;
        str++; prefix++;
    }
    return true;
}

void int_do_str(int wartosc, char* bufor) {
    if (wartosc == 0) { bufor[0] = '0'; bufor[1] = '\0'; return; }
    int i = 0; char temp[16];
    while (wartosc > 0) { temp[i++] = (wartosc % 10) + '0'; wartosc /= 10; }
    int j = 0; while (i > 0) { bufor[j++] = temp[--i]; }
    bufor[j] = '\0';
}