#include <png.h> /* libpng のヘッダファイル */
#include <string.h>
// #include <stdlib.h>

void func(png_structp png_ptr, int length, const void *user_data) {
  png_charp chunkdata;
  // char *chunkdata;
  chunkdata = (png_charp)png_malloc(png_ptr, length + 1);
  // chunkdata = (char *)malloc(sizeof(char)*16);
  /* ... */
  memcpy(chunkdata, user_data, length);
  /* ... */
}
