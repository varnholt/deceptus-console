#include <stddef.h>

void *memset(void *destination, int value, size_t count)
{
   unsigned char *output;
   output = (unsigned char *)destination;
   while (count)
   {
      *output++ = (unsigned char)value;
      --count;
   }
   return destination;
}

void *memcpy(void *destination, const void *source, size_t count)
{
   unsigned char *output;
   const unsigned char *input;
   output = (unsigned char *)destination;
   input = (const unsigned char *)source;
   while (count)
   {
      *output++ = *input++;
      --count;
   }
   return destination;
}

extern int main(void);

/* entry point called by the AmigaDOS Hunk loader. */
int __attribute__((used, section(".text.startup"))) _start(void)
{
   return main();
}
