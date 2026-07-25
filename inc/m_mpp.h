#ifndef	_M_MPP_H
#define _M_MPP_H
#include "rk_mpi.h"
#include "rk_type.h"
#include "mpp_frame.h"
#include "mpp_packet.h"
#include "mpp_buffer.h"
#include "rk_mpi_cmd.h"
#include "rk_mpi.h"

#include "mpp_env.h"
#include "mpp_mem.h"
#include "mpp_time.h"
#include "mpp_debug.h"
#include "mpp_common.h"

#include <stdio.h>
MPP_RET mpp(MppCtx *ctx, MppApi **mpi);



#endif