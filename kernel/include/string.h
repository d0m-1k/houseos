#pragma once

// #include <stddef.h>
#include <stdint.h>

typedef uint32_t size_t;

size_t strlen(const char* str);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t n);
char* strcat(char* dest, const char* src);
char* strncat(char* dest, const char* src, size_t n);
int strcmp(const char* str1, const char* str2);
int strncmp(const char* str1, const char* str2, size_t n);
char* strchr(const char* str, int ch);
char* strrchr(const char* str, int ch);
char* strstr(const char* haystack, const char* needle);
char* strtok(char* str, const char* delimiters);

void* memset(void* ptr, int value, size_t num);
void* memcpy(void* dest, const void* src, size_t num);
void* memmove(void* dest, const void* src, size_t num);
int memcmp(const void* ptr1, const void* ptr2, size_t num);
void* memchr(const void* ptr, int value, size_t num);

// char* strdup(const char* str);
// char* strndup(const char* str, size_t n);
size_t strspn(const char* str1, const char* str2);
size_t strcspn(const char* str1, const char* str2);
char* strpbrk(const char* str1, const char* str2);
char* strerror(int errnum);

int atoi(const char* str);
long atol(const char* str);
char* itoa(int value, char* str, int base);
char* ltoa(long value, char* str, int base);
char* utoa(unsigned int value, char* str, int base);

int isdigit(int c);
int isalpha(int c);
int isalnum(int c);
int isspace(int c);
int isupper(int c);
int islower(int c);
int toupper(int c);
int tolower(int c);