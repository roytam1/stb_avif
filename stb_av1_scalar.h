/*
 * stb_av1_scalar.h - aggregate include for the scalar AV1 path.
 */
#ifndef STB_AV1_SCALAR_H
#define STB_AV1_SCALAR_H

#include <stddef.h>

#ifndef STBV_U8_DEFINED
typedef unsigned char stbv_u8;
#define STBV_U8_DEFINED 1
#endif
#ifndef STBV_U16_DEFINED
typedef unsigned short stbv_u16;
#define STBV_U16_DEFINED 1
#endif
#ifndef STBV_U32_DEFINED
typedef unsigned int stbv_u32;
#define STBV_U32_DEFINED 1
#endif
#ifndef STBV_I32_DEFINED
typedef signed int stbv_i32;
#define STBV_I32_DEFINED 1
#endif
#ifndef STBV_U64_DEFINED
# if defined(_MSC_VER)
typedef unsigned __int64 stbv_u64;
# else
typedef unsigned long long stbv_u64;
# endif
#define STBV_U64_DEFINED 1
#endif

#include "stb_av1_getbits.h"
#include "stb_av1_msac.h"
#include "stb_av1_cdf.h"
#include "stb_av1_partition.h"
#include "stb_av1_partition_decode.h"
#include "stb_av1_seqhdr.h"
#include "stb_av1_framehdr.h"
#include "stb_av1_tx.h"
#include "stb_av1_txstate.h"
#include "stb_av1_intra.h"
#include "stb_av1_state.h"
#include "stb_av1_coef.h"
#include "stb_av1_itx.h"
#include "stb_av1_ipred.h"
#include "stb_av1_leaf.h"
#include "stb_av1_tile.h"
#include "stb_av1_obu.h"
#include "stb_av1_tile_decode.h"

#endif
