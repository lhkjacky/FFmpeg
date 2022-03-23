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
#include "tvai_common.h"

typedef struct TVAIParamContext {
    const AVClass *class;
    char *model;
    int device;
    int canDownloadModels;
    void* pFrameProcessor;
    int firstFrame;
} TVAIParamContext;

#define OFFSET(x) offsetof(TVAIParamContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption tvai_pe_options[] = {
    { "model", "Model short name", OFFSET(model), AV_OPT_TYPE_STRING, {.str="prap-2"}, .flags = FLAGS },
    { "device",  "Device index (Auto: -2, CPU: -1, GPU0: 0, ...)",  OFFSET(device),  AV_OPT_TYPE_INT, {.i64=-2}, -2, 8, FLAGS, "device" },
    { "download",  "Enable model downloading",  OFFSET(canDownloadModels),  AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS, "canDownloadModels" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(tvai_pe);

static av_cold int init(AVFilterContext *ctx) {
  TVAIParamContext *tvai = ctx->priv;
  av_log(NULL, AV_LOG_DEBUG, "Here init with params: %s %d\n", tvai->model, tvai->device);
  tvai->firstFrame = 1;
  return tvai->pFrameProcessor == NULL;
}

static int config_props(AVFilterLink *outlink) {
    AVFilterContext *ctx = outlink->src;
    TVAIParamContext *tvai = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];

    tvai->pFrameProcessor = ff_tvai_verifyAndCreate(inlink, outlink, (char*)"pe", tvai->model, ModelTypeParameterEstimation, tvai->device, 0, 1, tvai->canDownloadModels, NULL, 0, ctx);
    return tvai->pFrameProcessor == NULL ? AVERROR(EINVAL) : 0;
    return 0;
}


static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_BGR48,
    AV_PIX_FMT_NONE
};

static int filter_frame(AVFilterLink *inlink, AVFrame *in) {
    AVFilterContext *ctx = inlink->dst;
    TVAIParamContext *tvai = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    IOBuffer ioBuffer;
    int i;
    ff_tvai_prepareIOBufferInput(&ioBuffer, in, FrameTypeNormal, tvai->firstFrame);

    float parameters[TVAI_MAX_PARAMETER_COUNT] = {0};
    ioBuffer.output.pBuffer = (unsigned char *)parameters;
    ioBuffer.output.lineSize = sizeof(float)*TVAI_MAX_PARAMETER_COUNT;
    if(tvai->pFrameProcessor == NULL || tvai_process(tvai->pFrameProcessor,  &ioBuffer)) {
        av_log(NULL, AV_LOG_ERROR, "The processing has failed");
        av_frame_free(&in);
        return AVERROR(ENOSYS);
    }
    av_frame_free(&in);
    if(ioBuffer.output.timestamp < 0) {
      av_log(ctx, AV_LOG_DEBUG, "Ignoring frame %lf\n", TS2T(ioBuffer.output.timestamp, outlink->time_base));
      return 0;
    }
    av_log(ctx, AV_LOG_WARNING, "Parameter values:[");
    for(i=0;i<TVAI_MAX_PARAMETER_COUNT;i++) {
        av_log(ctx, AV_LOG_WARNING, " %f,", parameters[i]);
    }
    av_log(ctx, AV_LOG_WARNING, "]\n");
    tvai->firstFrame = 0;
    return ff_filter_frame(outlink, in);
}

static av_cold void uninit(AVFilterContext *ctx) {
    TVAIParamContext *tvai = ctx->priv;
    tvai_destroy(tvai->pFrameProcessor);
}

static const AVFilterPad tvai_pe_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad tvai_pe_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
    },
};

const AVFilter ff_vf_tvai_pe = {
    .name          = "tvai_pe",
    .description   = NULL_IF_CONFIG_SMALL("Apply Video Enhance AI models."),
    .priv_size     = sizeof(TVAIParamContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(tvai_pe_inputs),
    FILTER_OUTPUTS(tvai_pe_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .priv_class    = &tvai_pe_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
