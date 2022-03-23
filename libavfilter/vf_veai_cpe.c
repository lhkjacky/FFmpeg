/*
 * Copyright (c) 2012-2014 Clément Bœsch <u pkh me>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Video Enhance AI filter
 *
 * @see https://www.topazlabs.com/video-enhance-ai
 */

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/avutil.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "tvai.h"
#include "tvai_common.h"

typedef struct TVAICPEContext {
    const AVClass *class;
    char *model, *filename;
    int device, extraThreads;
    int canDownloadModels;
    void* pFrameProcessor;
    unsigned int counter;
} TVAICPEContext;

#define OFFSET(x) offsetof(TVAICPEContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption tvai_cpe_options[] = {
    { "model", "Model short name", OFFSET(model), AV_OPT_TYPE_STRING, {.str="cpe-1"}, .flags = FLAGS },
    { "filename", "CPE output filename", OFFSET(filename), AV_OPT_TYPE_STRING, {.str="cpe.json"}, .flags = FLAGS },
    { "device",  "Device index (Auto: -2, CPU: -1, GPU0: 0, ...)",  OFFSET(device),  AV_OPT_TYPE_INT, {.i64=-2}, -2, 8, FLAGS, "device" },
    { "threads",  "Number of extra threads to use on device",  OFFSET(extraThreads),  AV_OPT_TYPE_INT, {.i64=0}, 0, 3, FLAGS, "extraThreads" },
    { "download",  "Enable model downloading",  OFFSET(canDownloadModels),  AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS, "canDownloadModels" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(tvai_cpe);

static av_cold int init(AVFilterContext *ctx) {
  TVAICPEContext *tvai = ctx->priv;
  av_log(ctx, AV_LOG_DEBUG, "Here init with params: %s %d\n", tvai->model, tvai->device);
  tvai->counter = 0;
  return 0;
}

static int config_props(AVFilterLink *outlink) {
    AVFilterContext *ctx = outlink->src;
    TVAICPEContext *tvai = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    VideoProcessorInfo info;
    info.options[0] = tvai->filename;
    if(ff_tvai_verifyAndSetInfo(&info, inlink, outlink, (char*)"cpe", tvai->model, ModelTypeCamPoseEstimation, tvai->device, tvai->extraThreads, 1, tvai->canDownloadModels, NULL, 0, ctx)) {
      return AVERROR(EINVAL);
    }
    tvai->pFrameProcessor = tvai_create(&info);
    return tvai->pFrameProcessor == NULL ? AVERROR(EINVAL) : 0;
}

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_BGR48,
    AV_PIX_FMT_NONE
};

static int filter_frame(AVFilterLink *inlink, AVFrame *in) {
    AVFilterContext *ctx = inlink->dst;
    TVAICPEContext *tvai = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    IOBuffer ioBuffer;
    ff_tvai_prepareIOBufferInput(&ioBuffer, in, FrameTypeNormal, tvai->counter==0);

    float transform[4] = {0,0,0,0};
    ioBuffer.output.pBuffer = (unsigned char *)transform;
    ioBuffer.output.lineSize = sizeof(float)*4;

    if(tvai->pFrameProcessor == NULL || tvai_process(tvai->pFrameProcessor,  &ioBuffer)) {
        av_log(ctx, AV_LOG_ERROR, "The processing has failed");
        av_frame_free(&in);
        return AVERROR(ENOSYS);
    }
    av_log(ctx, AV_LOG_DEBUG, "%u CPE: %f\t%f\t%f\t%f\n", tvai->counter++, transform[0], transform[1], transform[2], transform[3]);
    return ff_filter_frame(outlink, in);
}

static int request_frame(AVFilterLink *outlink) {
    AVFilterContext *ctx = outlink->src;
    TVAICPEContext *tvai = ctx->priv;
    int ret = ff_request_frame(ctx->inputs[0]);
    if (ret == AVERROR_EOF) {
        int i, n = tvai_remaining_frames(tvai->pFrameProcessor);
        for(i=0;i<n;i++) {
            TVAIBuffer oBuffer;
            float transform[4] = {0,0,0,0};
            oBuffer.pBuffer = (unsigned char *)transform;
            oBuffer.lineSize = sizeof(float)*4;
            if(tvai->pFrameProcessor == NULL || tvai_process_last(tvai->pFrameProcessor, &oBuffer)) {
                av_log(ctx, AV_LOG_ERROR, "The post flight processing has failed");
                return AVERROR(ENOSYS);
            }
        }
        av_log(ctx, AV_LOG_DEBUG, "End of file reached %s %d\n", tvai->model, tvai->pFrameProcessor == NULL);
    }

    return ret;
}

static av_cold void uninit(AVFilterContext *ctx) {
    TVAICPEContext *tvai = ctx->priv;
    float transform[4] = {0,0,0,0};
    av_log(ctx, AV_LOG_ERROR, "%u CPE: %f\t%f\t%f\t%f\n", tvai->counter++, transform[0], transform[1], transform[2], transform[3]);
    av_log(ctx, AV_LOG_DEBUG, "Uninit called for %s %u\n", tvai->model, tvai->pFrameProcessor);
    if(tvai->pFrameProcessor)
        tvai_destroy(tvai->pFrameProcessor);
}

static const AVFilterPad tvai_cpe_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad tvai_cpe_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
        .request_frame = request_frame,
    },
};

const AVFilter ff_vf_tvai_cpe = {
    .name          = "tvai_cpe",
    .description   = NULL_IF_CONFIG_SMALL("Apply Video Enhance AI upscale models, parameters will only be applied to appropriate models"),
    .priv_size     = sizeof(TVAICPEContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(tvai_cpe_inputs),
    FILTER_OUTPUTS(tvai_cpe_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .priv_class    = &tvai_cpe_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
