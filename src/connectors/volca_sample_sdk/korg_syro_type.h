#ifndef KORG_SYRO_TYPE_H__
#define KORG_SYRO_TYPE_H__

#ifndef _MSC_VER

#include <stdint.h>

#else	// #ifndef _MSC_VER

#ifndef uint8_t
typedef unsigned char uint8_t;
#endif
#ifndef int8_t
typedef signed char int8_t;
#endif

#ifndef uint16_t
typedef unsigned short uint16_t;
#endif
#ifndef int16_t
typedef short int16_t;
#endif

#ifndef uint32_t
typedef unsigned long uint32_t;
#endif
#ifndef int32_t
typedef long int32_t;
#endif

#endif	// #ifndef _MSC_VER

#if !defined(__cplusplus) && (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 202000)
#ifndef bool
typedef int bool;
#endif
#ifndef true
#define true (1)
#endif
#ifndef false
#define false (0)
#endif
#endif	// !defined(__cplusplus) && (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 202000)

#endif	// #ifndef KORG_SYRO_VOLCASAMPLE_H__
