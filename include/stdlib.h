#ifndef REDIX_STDLIB_H
#define REDIX_STDLIB_H

void *malloc(long n);
void *realloc(void *p, long n);
void *calloc(long n, long size);
void free(void *p);
void exit(int status);
int atoi(const char *s);

#endif
