
#include <stdio.h>
#include <assert.h>

int main(int argc, char** argv) {

  FILE* devnull = fopen("/dev/null", "r+");
  assert(NULL != devnull);

  char msg[] = "hello world";
  char buf[1024];

  size_t bytes;
  bytes = fwrite(msg, 1, sizeof(msg), devnull);
  if (feof(devnull)) { printf("fwrite said EOF\n"); }
  if (ferror(devnull)) { printf("fwrite said error\n"); }
  printf("tried to write %lu bytes and sent %lu\n", sizeof(msg), bytes);
  assert(sizeof(msg) == bytes);
  
  bytes = fread(buf, 1, sizeof(msg), devnull);
  if (feof(devnull)) { printf("fread said EOF\n"); }
  if (ferror(devnull)) { printf("fread said error\n"); }
  printf("tried to read %lu bytes and received %lu\n", sizeof(msg), bytes);
  assert(0 == bytes);
}
