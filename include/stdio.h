#ifndef REDIX_STDIO_H
#define REDIX_STDIO_H

/* redix has no typedef so FILE is spelled void star */
int printf(const char *fmt, ...);
int fprintf(void *stream, const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int putchar(int c);
int puts(const char *s);
void *fopen(const char *path, const char *mode);
int fclose(void *stream);
int fseek(void *stream, long off, int whence);
long ftell(void *stream);
long fread(void *buf, long size, long n, void *stream);

#endif
