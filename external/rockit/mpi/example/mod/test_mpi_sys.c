/* Copyright 2020 Rockchip Electronics Co. LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifdef DBG_MOD_ID
#undef DBG_MOD_ID
#endif
#define DBG_MOD_ID DBG_MOD_COMB1(RK_ID_SYS)

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "rk_debug.h"
#include "rk_mpi_sys.h"
#include "rk_mpi_ao.h"
#include "rk_mpi_adec.h"

#include "test_comm_argparse.h"

typedef enum rkTestVIMODE_E {
    TEST_SYS_MODE_BIND = 0,
    TEST_SYS_MODE_DUMPSYS = 1,
    TEST_SYS_MODE_FORCE_LOST_FRAME = 2,
    TEST_SYS_MODE_MMZ_RELEASE = 3,
} TEST_SYS_MODE_E;

typedef struct _rkTestSysCtx {
    RK_S32      s32LoopCount;
    RK_S32      s32DevId;
    RK_S32      s32SrcChnId;
    RK_S32      s32DstChnNum;
    TEST_SYS_MODE_E enMode;
    ADEC_CHN_ATTR_S *pstADecChnAttr;
} TEST_SYS_CTX_S;

RK_S32 test_ao_dev_init(TEST_SYS_CTX_S *pstCtx) {
#ifdef HAVE_API_MPI_AO
    AUDIO_DEV aoDevId = (AUDIO_DEV)pstCtx->s32DevId;
    AIO_ATTR_S aoAttr;

    memset(&aoAttr, 0, sizeof(AIO_ATTR_S));
    aoAttr.enBitwidth = AUDIO_BIT_WIDTH_16;
    aoAttr.soundCard.sampleRate = AUDIO_SAMPLE_RATE_44100;
    aoAttr.soundCard.channels = 2;
    aoAttr.soundCard.bitWidth = AUDIO_BIT_WIDTH_16;
    aoAttr.enSamplerate = AUDIO_SAMPLE_RATE_44100;
    aoAttr.enSoundmode = AUDIO_SOUND_MODE_MONO;
    aoAttr.u32FrmNum = 4;
    aoAttr.u32PtNumPerFrm = 1024;
    aoAttr.u32EXFlag = 0;
    aoAttr.u32ChnCnt = 2;
    memcpy(aoAttr.u8CardName, "hw:0,0", 7);
    RK_MPI_AO_SetPubAttr(aoDevId, &aoAttr);
    RK_MPI_AO_Enable(aoDevId);
#endif
    return RK_SUCCESS;
}

RK_S32 test_ao_dev_deinit(TEST_SYS_CTX_S *pstCtx) {
    RK_S32 s32Ret = RK_SUCCESS;
#ifdef HAVE_API_MPI_AO
    s32Ret =  RK_MPI_AO_Disable(pstCtx->s32DevId);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("failed to disable ao dev, err: %d", s32Ret);
        return RK_FAILURE;
    }
#endif
    return s32Ret;
}

ADEC_CHN_ATTR_S* test_adec_create_chn_attr() {
    ADEC_CHN_ATTR_S *pstChnAttr = (ADEC_CHN_ATTR_S *)
                                        (malloc(sizeof(ADEC_CHN_ATTR_S)));
    memset(pstChnAttr, 0, sizeof(ADEC_CHN_ATTR_S));
    ADEC_ATTR_CODEC_S *pstCodecAttr = &pstChnAttr->stCodecAttr;

    // attr of adec
    pstCodecAttr->u32Channels      = 2;
    pstCodecAttr->u32SampleRate    = 16000;
    pstCodecAttr->u32ExtraDataSize = 0;
    pstCodecAttr->pExtraData       = RK_NULL;

    // attr of chn
    pstChnAttr->enType          = RK_AUDIO_ID_ADPCM_G726;
    pstChnAttr->enMode          = ADEC_MODE_PACK;
    pstChnAttr->u32BufCount     = 4;
    pstChnAttr->u32BufSize      = 8192;

    return pstChnAttr;
}

void test_adec_destroy_chn_attr(ADEC_CHN_ATTR_S **ppstChnAttr) {
    ADEC_CHN_ATTR_S *pstChnAttr = *ppstChnAttr;
    if (pstChnAttr == RK_NULL) {
        return;
    }

    ADEC_ATTR_CODEC_S *pstCodecAttr = &pstChnAttr->stCodecAttr;
    if (pstCodecAttr->pExtraData != RK_NULL) {
        free(pstCodecAttr->pExtraData);
        pstCodecAttr->pExtraData = RK_NULL;
    }
    pstCodecAttr->u32ExtraDataSize = 0;

    free(pstChnAttr);
    *ppstChnAttr = RK_NULL;
}

RK_S32 test_adec_create_channel(TEST_SYS_CTX_S *pstCtx, RK_S32 s32ChnId) {
    RK_S32 s32Ret  = RK_SUCCESS;
#ifdef HAVE_API_MPI_ADEC
    ADEC_CHN AdChn = (ADEC_CHN)s32ChnId;
    ADEC_CHN_ATTR_S *pstChnAttr = test_adec_create_chn_attr();
    s32Ret = RK_MPI_ADEC_CreateChn(AdChn, pstChnAttr);
    if (s32Ret) {
        RK_LOGE("failed to create adec chn %d, err %d", AdChn, s32Ret);
        return RK_FAILURE;
    }
#endif
    return s32Ret;
}

RK_S32 test_adec_destroy_channel(TEST_SYS_CTX_S *pstCtx, RK_S32 s32ChnId) {
    RK_S32 s32Ret = RK_SUCCESS;
#ifdef HAVE_API_MPI_ADEC
    RK_S32 s32DevId = pstCtx->s32DevId;
    ADEC_CHN AdChn  = (ADEC_CHN)s32ChnId;
    s32Ret = RK_MPI_ADEC_DestroyChn(AdChn);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("failed to destroy adec channel(%d), err: %d", AdChn, s32Ret);
    }
#endif
    test_adec_destroy_chn_attr(&pstCtx->pstADecChnAttr);
    return s32Ret;
}

RK_S32 test_ao_enable_channel(TEST_SYS_CTX_S *pstCtx, RK_S32 s32ChnId) {
    RK_S32 s32Ret = RK_SUCCESS;
#ifdef HAVE_API_MPI_AO
    ADEC_CHN AdChn  = (ADEC_CHN)s32ChnId;
    s32Ret = RK_MPI_AO_EnableChn(pstCtx->s32DevId, AdChn);
    if (s32Ret != 0) {
        RK_LOGE("failed to enable ao chn %d, err %d", AdChn, s32Ret);
        return RK_FAILURE;
    }
#endif
    return s32Ret;
}

RK_S32 test_ao_disable_channel(TEST_SYS_CTX_S *pstCtx, RK_S32 s32ChnId) {
    RK_S32 s32Ret = RK_SUCCESS;
#ifdef HAVE_API_MPI_AO
    AO_CHN AoChn  = (AO_CHN)s32ChnId;
    s32Ret = RK_MPI_AO_DisableChn(pstCtx->s32DevId, AoChn);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("failed to disable ao channel(%d), err: %d", AoChn, s32Ret);
        return RK_FAILURE;
    }
#endif
    return s32Ret;
}

RK_S32 test_bind_adec_ao(TEST_SYS_CTX_S *pstCtx, RK_S32 s32SrcChnId, RK_S32 s32DstChnId) {
    RK_S32 s32Ret = RK_SUCCESS;
    RK_S32 s32DevId = pstCtx->s32DevId;
    MPP_CHN_S stSrcChn, stDstChn;

    stSrcChn.enModId = RK_ID_ADEC;
    stSrcChn.s32DevId = s32DevId;
    stSrcChn.s32ChnId = s32SrcChnId;

    stDstChn.enModId = RK_ID_AO;
    stDstChn.s32DevId = s32DevId;
    stDstChn.s32ChnId = s32DstChnId;
    s32Ret = RK_MPI_SYS_Bind(&stSrcChn, &stDstChn);

    return s32Ret;
}

RK_S32 test_unbind_adec_ao(TEST_SYS_CTX_S *pstCtx, RK_S32 s32SrcChnId, RK_S32 s32DstChnId) {
    RK_S32 s32Ret = RK_SUCCESS;
    RK_S32 s32DevId = pstCtx->s32DevId;

    MPP_CHN_S stSrcChn, stDstChn;
    stSrcChn.enModId = RK_ID_ADEC;
    stSrcChn.s32DevId = s32DevId;
    stSrcChn.s32ChnId = s32SrcChnId;

    stDstChn.enModId = RK_ID_AO;
    stDstChn.s32DevId = s32DevId;
    stDstChn.s32ChnId = s32DstChnId;
    s32Ret = RK_MPI_SYS_UnBind(&stSrcChn, &stDstChn);

    return s32Ret;
}

RK_S32 test_mpi_sys_get_bind_by_src(TEST_SYS_CTX_S *pstCtx, RK_S32 s32ChnId) {
    RK_S32 s32Ret = RK_SUCCESS;
    MPP_CHN_S stSrcChn;
    MPP_BIND_DEST_S pstBindDest;

    memset(&stSrcChn, 0, sizeof(MPP_CHN_S));
    memset(&pstBindDest, 0, sizeof(MPP_BIND_DEST_S));
    stSrcChn.enModId = RK_ID_ADEC;
    stSrcChn.s32DevId = pstCtx->s32DevId;
    stSrcChn.s32ChnId = s32ChnId;
    s32Ret = RK_MPI_SYS_GetBindbySrc(&stSrcChn, &pstBindDest);
    if (s32Ret == RK_SUCCESS) {
        for (RK_S32 i=0; i < pstBindDest.u32Num; i++) {
            MPP_CHN_S *pstDstChn = &pstBindDest.astMppChn[i];
            RK_LOGD("get dst channel(modId=%d, devId=%d, chnId=%d)",
                pstDstChn->enModId, pstDstChn->s32DevId, pstDstChn->s32ChnId);
        }
    } else {
        RK_LOGE("failed to RK_MPI_SYS_GetBindbySrc");
    }

    return s32Ret;
}

RK_S32 test_mpi_sys_get_bind_by_dest(TEST_SYS_CTX_S *pstCtx, RK_S32 s32ChnId) {
    RK_S32 s32Ret = RK_SUCCESS;
    MPP_CHN_S stSrcChn;
    MPP_CHN_S stDstChn;

    memset(&stSrcChn, 0, sizeof(MPP_CHN_S));
    memset(&stDstChn, 0, sizeof(MPP_CHN_S));
    stDstChn.enModId = RK_ID_AO;
    stDstChn.s32DevId = pstCtx->s32DevId;
    stDstChn.s32ChnId = s32ChnId;
    s32Ret = RK_MPI_SYS_GetBindbyDest(&stDstChn, &stSrcChn);
    if (s32Ret == RK_SUCCESS) {
        RK_LOGD("get src channel(modId=%d, devId=%d, chnId=%d)",
                stSrcChn.enModId, stSrcChn.s32DevId, stSrcChn.s32ChnId);
    } else {
        RK_LOGE("failed to RK_MPI_SYS_GetBindbyDest");
    }

    return s32Ret;
}

RK_S32 unit_test_mpi_sys_bind(TEST_SYS_CTX_S *pstCtx) {
    RK_S32 s32Ret = RK_SUCCESS;
    RK_S32 s32SrcChnId = pstCtx->s32SrcChnId;
    RK_S32 s32DstNumChn = pstCtx->s32DstChnNum;

    // init Ao device
    s32Ret = test_ao_dev_init(pstCtx);

    // create adec channel
    s32Ret = test_adec_create_channel(pstCtx, s32SrcChnId);
    if (s32Ret != RK_SUCCESS) {
        goto __FAILED_ADEC;
    }

    // enable ao channel
    for (RK_S32 s32DstChnId = 0; s32DstChnId < s32DstNumChn; s32DstChnId++) {
        s32Ret = test_ao_enable_channel(pstCtx, s32DstChnId);
        if (s32Ret != RK_SUCCESS) {
            goto __FAILED_AO;
        }
        // bind adec->ao
        s32Ret = test_bind_adec_ao(pstCtx, s32SrcChnId, s32DstChnId);
        if (s32Ret == RK_SUCCESS) {
            RK_LOGD("succeed to bind ADEC(%d) AO(%d)", s32SrcChnId, s32DstChnId);
        }
    }

    // test RK_MPI_SYS_GetBindbyDest/GetBindbySrc
    s32Ret = test_mpi_sys_get_bind_by_src(pstCtx, s32SrcChnId);
    for (RK_S32 s32DstChnId = 0; s32DstChnId < s32DstNumChn; s32DstChnId++) {
        s32Ret = test_mpi_sys_get_bind_by_dest(pstCtx, s32DstChnId);
    }

__FAILED_AO:
    for (RK_S32 s32DstChnId = 0; s32DstChnId < s32DstNumChn; s32DstChnId++) {
        // unbind adec->ao
        s32Ret = test_unbind_adec_ao(pstCtx, s32SrcChnId, s32DstChnId);
        if (s32Ret == RK_SUCCESS) {
            RK_LOGD("succeed to unbind ADEC(%d) AO(%d)", s32SrcChnId, s32DstChnId);
        }
        // disable ao channel
        test_ao_disable_channel(pstCtx, s32DstChnId);
    }

    test_adec_destroy_channel(pstCtx, s32SrcChnId);

 __FAILED_ADEC:
    // deinit Ao device
    s32Ret = test_ao_dev_deinit(pstCtx);

    return s32Ret;
}

RK_S32 unit_test_mpi_sys_dumpsys(TEST_SYS_CTX_S *pstCtx) {
    RK_S32 s32Ret = RK_SUCCESS;
    char *buf = (char *)malloc(100 * 1024);
    // init Ao device
    test_ao_dev_init(pstCtx);
    test_ao_enable_channel(pstCtx, 0);

    //RK_MPI_SYS_DumpSys("dumpsys ao", buf, 100 * 1024);
    //printf("%s\n", buf);
    RK_MPI_SYS_DumpSys("dumpsys cat /dev/mpi/valloc", buf, 100 * 1024);
    printf("%s\n", buf);
    free(buf);
    return s32Ret;
}

RK_S32 unit_test_mpi_sys_force_lost_frame(TEST_SYS_CTX_S *pstCtx) {
    RK_S32 s32Ret = RK_SUCCESS;
    return s32Ret;
}

RK_S32 unit_test_mpi_sys_mmz_release(TEST_SYS_CTX_S *pstCtx) {
    RK_S32 s32Ret = RK_SUCCESS;
    return s32Ret;
}

static const char *const usages[] = {
    "./rk_mpi_sys_test...",
    NULL,
};

int rk_mpi_sys_test(RK_S32 argc, const char **argv) {
    RK_S32 s32Ret = RK_SUCCESS;
    TEST_SYS_CTX_S stCtx;

    memset(&stCtx, 0, sizeof(TEST_SYS_CTX_S));
    stCtx.s32LoopCount = 1;
    stCtx.s32DevId = 0;
    stCtx.s32SrcChnId = 0;
    stCtx.s32DstChnNum = 1;
    stCtx.enMode = TEST_SYS_MODE_BIND;

    struct argparse_option options[] = {
        OPT_HELP(),
        OPT_GROUP("basic options:"),
        OPT_INTEGER('n', "loop_count", &(stCtx.s32LoopCount),
                    "loop running count. default(1)", NULL, 0, 0),
        OPT_INTEGER('\0', "device_id", &(stCtx.s32DevId),
                    "MODULE device id. default(0)", NULL, 0, 0),
        OPT_INTEGER('\0', "src_channel_id", &(stCtx.s32SrcChnId),
                    "source MODULE channel id. default(0)", NULL, 0, 0),
        OPT_INTEGER('\0', "dst_channel_count", &(stCtx.s32DstChnNum),
                    "the count of dst MODULE channel. default(1)", NULL, 0, 0),
        OPT_INTEGER('m', "mode", &(stCtx.enMode),
                    "test mode(default 0; \n\t"
                    "0:test bind api \n\t"
                    "1:test force lost frame api \n\t"
                    "2:test mmz release api)) \n\t"
                    ,NULL, 0, 0),
        OPT_END(),
    };

    struct argparse argparse;
    argparse_init(&argparse, options, usages, 0);
    argparse_describe(&argparse, "\nselect a test case to run.",
                                 "\nuse --help for details.");

    argc = argparse_parse(&argparse, argc, argv);
    if (argc < 0)
        return 0;
    RK_S32 loopCount = stCtx.s32LoopCount;
    do {
        printf("%s, %d\n", __func__, __LINE__);
        s32Ret = RK_MPI_SYS_Init();
        printf("%s, %d\n", __func__, __LINE__);
        if (s32Ret != RK_SUCCESS) {
            return s32Ret;
        }
        if (stCtx.enMode == TEST_SYS_MODE_BIND)
            s32Ret = unit_test_mpi_sys_bind(&stCtx);
        else if (stCtx.enMode == TEST_SYS_MODE_DUMPSYS)
            s32Ret = unit_test_mpi_sys_dumpsys(&stCtx);
        else if (stCtx.enMode == TEST_SYS_MODE_FORCE_LOST_FRAME)
            s32Ret = unit_test_mpi_sys_force_lost_frame(&stCtx);
        else if (stCtx.enMode == TEST_SYS_MODE_MMZ_RELEASE)
            s32Ret = unit_test_mpi_sys_mmz_release(&stCtx);
        if (s32Ret != RK_SUCCESS) {
            goto __FAILED;
        }
        printf("%s, %d\n", __func__, __LINE__);
        s32Ret = RK_MPI_SYS_Exit();
        if (s32Ret != RK_SUCCESS) {
            return s32Ret;
        }
        loopCount--;
        RK_LOGI("looping times %d", stCtx.s32LoopCount - loopCount);
    } while (loopCount > 0);
    RK_LOGI("test running ok.");
    return RK_SUCCESS;

__FAILED:
    RK_MPI_SYS_Exit();
    RK_LOGE("test running failed!");
    return s32Ret;
}

#ifdef OS_LINUX
int main(int argc, const char **argv) {
    return rk_mpi_sys_test(argc, argv);
}
#endif

#ifdef OS_RTT
#include <finsh.h>
MSH_CMD_EXPORT(rk_mpi_sys_test, rockit sys module test);
#endif