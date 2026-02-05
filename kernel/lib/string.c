#include <string.h>
#include <stdint.h>

/* Вычисление длины строки */
size_t strlen(const char* str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

/* Копирование строки */
char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++));
    return dest;
}

/* Безопасное копирование строки */
char* strncpy(char* dest, const char* src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

/* Конкатенация строк */
char* strcat(char* dest, const char* src) {
    char* d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

/* Безопасная конкатенация строк */
char* strncat(char* dest, const char* src, size_t n) {
    char* d = dest;
    while (*d) d++;
    
    size_t i = 0;
    while (i < n && src[i] != '\0') {
        d[i] = src[i];
        i++;
    }
    d[i] = '\0';
    return dest;
}

/* Сравнение строк */
int strcmp(const char* str1, const char* str2) {
    while (*str1 && (*str1 == *str2)) {
        str1++;
        str2++;
    }
    return *(unsigned char*)str1 - *(unsigned char*)str2;
}

/* Сравнение строк с ограничением длины */
int strncmp(const char* str1, const char* str2, size_t n) {
    if (n == 0) return 0;
    
    while (--n && *str1 && (*str1 == *str2)) {
        str1++;
        str2++;
    }
    return *(unsigned char*)str1 - *(unsigned char*)str2;
}

/* Поиск первого вхождения символа */
char* strchr(const char* str, int ch) {
    while (*str != (char)ch) {
        if (*str == '\0') return NULL;
        str++;
    }
    return (char*)str;
}

/* Поиск последнего вхождения символа */
char* strrchr(const char* str, int ch) {
    const char* last = NULL;
    while (*str) {
        if (*str == (char)ch) last = str;
        str++;
    }
    return (char*)last;
}

/* Поиск подстроки */
char* strstr(const char* haystack, const char* needle) {
    if (*needle == '\0') return (char*)haystack;
    
    for (; *haystack != '\0'; haystack++) {
        if (*haystack == *needle) {
            const char* h = haystack;
            const char* n = needle;
            
            while (*h && *n && (*h == *n)) {
                h++;
                n++;
            }
            
            if (*n == '\0') return (char*)haystack;
        }
    }
    
    return NULL;
}

/* Заполнение памяти значением */
void* memset(void* ptr, int value, size_t num) {
    uint8_t* p = (uint8_t*)ptr;
    while (num--) *p++ = (uint8_t)value;
    return ptr;
}

/* Копирование памяти */
void* memcpy(void* dest, const void* src, size_t num) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    
    /* Простое побайтовое копирование */
    while (num--) *d++ = *s++;
    
    return dest;
}

/* Безопасное копирование памяти (с учетом перекрытия) */
void* memmove(void* dest, const void* src, size_t num) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    
    if (d < s) {
        /* Копирование вперед */
        while (num--) *d++ = *s++;
    } else {
        /* Копирование назад для перекрывающихся областей */
        d += num;
        s += num;
        while (num--) *--d = *--s;
    }
    
    return dest;
}

/* Сравнение областей памяти */
int memcmp(const void* ptr1, const void* ptr2, size_t num) {
    const uint8_t* p1 = (const uint8_t*)ptr1;
    const uint8_t* p2 = (const uint8_t*)ptr2;
    
    while (num--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
}

/* Поиск символа в памяти */
void* memchr(const void* ptr, int value, size_t num) {
    const uint8_t* p = (const uint8_t*)ptr;
    while (num--) {
        if (*p == (uint8_t)value) return (void*)p;
        p++;
    }
    return NULL;
}

/* Дублирование строки */
/* char* strdup(const char* str) {
    size_t len = strlen(str) + 1;
    char* new_str = (char*)malloc(len);
    if (new_str) strcpy(new_str, str);
    return new_str;
} */

/* Безопасное дублирование строки */
/* char* strndup(const char* str, size_t n) {
    size_t len = strlen(str);
    if (len > n) len = n;
    
    char* new_str = (char*)malloc(len + 1);
    if (new_str) {
        strncpy(new_str, str, len);
        new_str[len] = '\0';
    }
    return new_str;
} */

/* Преобразование строки в число */
int atoi(const char* str) {
    int result = 0;
    int sign = 1;
    
    /* Пропуск пробелов */
    while (isspace(*str)) str++;
    
    /* Обработка знака */
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    
    /* Преобразование цифр */
    while (isdigit(*str)) {
        result = result * 10 + (*str - '0');
        str++;
    }
    
    return sign * result;
}

/* Преобразование строки в long */
long atol(const char* str) {
    long result = 0;
    int sign = 1;
    
    while (isspace(*str)) str++;
    
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    
    while (isdigit(*str)) {
        result = result * 10 + (*str - '0');
        str++;
    }
    
    return sign * result;
}

/* Преобразование числа в строку */
char* itoa(int value, char* str, int base) {
    char* rc;
    char* ptr;
    char* low;
    
    if (base < 2 || base > 36) {
        *str = '\0';
        return str;
    }
    
    rc = ptr = str;
    
    // Handle negative numbers
    if (value < 0 && base == 10) {
        *ptr++ = '-';
        rc = ptr;
        value = -value;
    }
    
    low = ptr;
    
    do {
        *ptr++ = "0123456789abcdefghijklmnopqrstuvwxyz"[value % base];
        value /= base;
    } while (value);
    
    *ptr-- = '\0';
    
    // Reverse the string
    while (low < ptr) {
        char tmp = *low;
        *low++ = *ptr;
        *ptr-- = tmp;
    }
    
    return rc;
}

char* utoa(unsigned int value, char* str, int base) {
    char* rc;
    char* ptr;
    char* low;
    
    if (base < 2 || base > 36) {
        *str = '\0';
        return str;
    }
    
    rc = ptr = str;
    low = ptr;
    
    do {
        *ptr++ = "0123456789abcdefghijklmnopqrstuvwxyz"[value % base];
        value /= base;
    } while (value);
    
    *ptr-- = '\0';
    
    // Reverse the string
    while (low < ptr) {
        char tmp = *low;
        *low++ = *ptr;
        *ptr-- = tmp;
    }
    
    return rc;
}

/* Функции для работы с символами */
int isdigit(int c) { return (c >= '0' && c <= '9'); }
int isalpha(int c) { return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isspace(int c) { return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'); }
int isupper(int c) { return (c >= 'A' && c <= 'Z'); }
int islower(int c) { return (c >= 'a' && c <= 'z'); }
int toupper(int c) { return (islower(c) ? (c - 'a' + 'A') : c); }
int tolower(int c) { return (isupper(c) ? (c - 'A' + 'a') : c); }

/* Токенизация строки (упрощенная версия) */
static char* strtok_save = NULL;

char* strtok(char* str, const char* delimiters) {
    if (str == NULL) {
        if (strtok_save == NULL) return NULL;
        str = strtok_save;
    }
    
    /* Пропуск начальных разделителей */
    while (*str != '\0' && strchr(delimiters, *str) != NULL) {
        str++;
    }
    
    if (*str == '\0') {
        strtok_save = NULL;
        return NULL;
    }
    
    char* token_start = str;
    
    /* Поиск следующего разделителя */
    while (*str != '\0' && strchr(delimiters, *str) == NULL) {
        str++;
    }
    
    if (*str == '\0') {
        strtok_save = NULL;
    } else {
        *str = '\0';
        strtok_save = str + 1;
    }
    
    return token_start;
}

/* Длина начального сегмента, содержащего только указанные символы */
size_t strspn(const char* str1, const char* str2) {
    size_t count = 0;
    while (*str1 && strchr(str2, *str1)) {
        str1++;
        count++;
    }
    return count;
}

/* Длина начального сегмента, не содержащего указанные символы */
size_t strcspn(const char* str1, const char* str2) {
    size_t count = 0;
    while (*str1 && !strchr(str2, *str1)) {
        str1++;
        count++;
    }
    return count;
}

/* Поиск первого вхождения любого символа из строки */
char* strpbrk(const char* str1, const char* str2) {
    while (*str1) {
        if (strchr(str2, *str1)) return (char*)str1;
        str1++;
    }
    return NULL;
}

/* Упрощенная функция для строк ошибок */
static const char* error_messages[] = {
    "No error",                 /* 0 */
    "Operation not permitted",  /* 1 */
    "No such file or directory",/* 2 */
    "Interrupted system call",  /* 3 */
    "Input/output error",       /* 4 */
    "Bad file descriptor",      /* 5 */
    "No child processes",       /* 6 */
    "Out of memory",            /* 7 */
    "Permission denied",        /* 8 */
    "Bad address",              /* 9 */
    "Try again",                /* 10 */
};

char* strerror(int errnum) {
    if (errnum < 0 || errnum >= (int)(sizeof(error_messages) / sizeof(error_messages[0]))) {
        return "Unknown error";
    }
    return (char*)error_messages[errnum];
}