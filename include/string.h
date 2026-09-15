#ifndef REDIX_STRING_H
#define REDIX_STRING_H

long strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, long n);
char *strcpy(char *d, const char *s);
char *strdup(const char *s);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
void *memcpy(void *d, const void *s, long n);

#endif
