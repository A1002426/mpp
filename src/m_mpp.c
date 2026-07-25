#include "m_mpp.h"

MPP_RET mpp(MppCtx *ctx, MppApi **mpi)
{
    MPP_RET ret;
    ret = mpp_create(ctx, mpi);
    if (ret != MPP_OK) {
        printf("[ERR] mpp_create dec fail %d\n", ret);
        return ret;
    }
    // 初始化解码器 MJPEG
    ret = mpp_init(*ctx, MPP_CTX_DEC, MPP_VIDEO_CodingMJPEG);
    if (ret != MPP_OK) {
        printf("[ERR] mpp_init dec mjpeg fail %d\n", ret);
        return ret;
    }
    
    return MPP_OK;
}

