#ifndef DECEPTUS_SMD_STDINT_H
#define DECEPTUS_SMD_STDINT_H

/* sgdk supplies fixed-width aliases but no standard stdint header. */
#ifndef uint8_t
typedef char int8_t;
typedef unsigned char uint8_t;
#define __int8_t_defined 1
#endif
#ifndef uint16_t
typedef signed short int16_t;
typedef unsigned short uint16_t;
#define __int16_t_defined 1
#endif
#ifndef uint32_t
typedef signed long int32_t;
typedef unsigned long uint32_t;
#define __int32_t_defined 1
#endif
typedef char deceptus_smd_char_signed[(int8_t)-1 < 0 ? 1 : -1];
typedef char deceptus_smd_char_width[sizeof(int8_t) == 1 ? 1 : -1];
typedef char deceptus_smd_short_width[sizeof(int16_t) == 2 ? 1 : -1];
typedef char deceptus_smd_long_width[sizeof(int32_t) == 4 ? 1 : -1];

#endif
