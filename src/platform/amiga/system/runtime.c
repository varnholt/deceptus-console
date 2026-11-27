#include <stddef.h>

typedef unsigned long __attribute__((may_alias)) MemoryWord;

void* __attribute__((noinline, noclone)) memset(void* destination, int value, size_t count)
{
   unsigned char* output;
   MemoryWord* words;
   MemoryWord fill;
   output = (unsigned char*)destination;
   while (count && ((size_t)output & (sizeof(MemoryWord) - 1)))
   {
      *output++ = (unsigned char)value;
      --count;
   }
   fill = (unsigned char)value;
   fill |= fill << 8;
   fill |= fill << 16;
   words = (MemoryWord*)output;
   while (count >= sizeof(MemoryWord) * 4)
   {
      words[0] = fill;
      words[1] = fill;
      words[2] = fill;
      words[3] = fill;
      words += 4;
      count -= sizeof(MemoryWord) * 4;
   }
   while (count >= sizeof(MemoryWord))
   {
      *words++ = fill;
      count -= sizeof(MemoryWord);
   }
   output = (unsigned char*)words;
   while (count)
   {
      *output++ = (unsigned char)value;
      --count;
   }
   return destination;
}

void* __attribute__((noinline, noclone)) memcpy(void* destination, const void* source, size_t count)
{
   unsigned char* output;
   const unsigned char* input;
   MemoryWord* output_words;
   const MemoryWord* input_words;
   output = (unsigned char*)destination;
   input = (const unsigned char*)source;
   /* use native transfers for aligned sprite staging and struct copies. */
   if (((size_t)output & (sizeof(MemoryWord) - 1)) == ((size_t)input & (sizeof(MemoryWord) - 1)))
   {
      while (count && ((size_t)output & (sizeof(MemoryWord) - 1)))
      {
         *output++ = *input++;
         --count;
      }
      output_words = (MemoryWord*)output;
      input_words = (const MemoryWord*)input;
      while (count >= sizeof(MemoryWord) * 4)
      {
         output_words[0] = input_words[0];
         output_words[1] = input_words[1];
         output_words[2] = input_words[2];
         output_words[3] = input_words[3];
         output_words += 4;
         input_words += 4;
         count -= sizeof(MemoryWord) * 4;
      }
      while (count >= sizeof(MemoryWord))
      {
         *output_words++ = *input_words++;
         count -= sizeof(MemoryWord);
      }
      output = (unsigned char*)output_words;
      input = (const unsigned char*)input_words;
   }
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
