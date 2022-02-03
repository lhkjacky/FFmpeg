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
#include "veai.h"

#define PLANE_R 0x4
#define PLANE_G 0x1
#define PLANE_B 0x2
#define PLANE_Y 0x1
#define PLANE_U 0x2
#define PLANE_V 0x4
#define PLANE_A 0x8

enum FilterMode {
    MODE_WIRES,
    MODE_COLORMIX,
    MODE_CANNY,
    NB_MODE
};

struct plane_info {
    uint8_t  *tmpbuf;
    uint16_t *gradients;
    char     *directions;
    int      width, height;
};

typedef struct VEAIParamContext {
    const AVClass *class;
    char *model;
    int device;
    int canDownloadModels;
    void* pFrameProcessor;
    int firstFrame;
} VEAIParamContext;

#define OFFSET(x) offsetof(VEAIParamContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption veai_param_options[] = {
    { "model", "Model short name", OFFSET(model), AV_OPT_TYPE_STRING, {.str="aaa-9"}, .flags = FLAGS },
    { "device",  "Device index (Auto: -2, CPU: -1, GPU0: 0, ...)",  OFFSET(device),  AV_OPT_TYPE_INT, {.i64=-2}, -2, 8, FLAGS, "device" },
    { "download",  "Enable model downloading",  OFFSET(canDownloadModels),  AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS, "canDownloadModels" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(veai_param);

static av_cold int init(AVFilterContext *ctx) {
  VEAIParamContext *veai = ctx->priv;
  av_log(NULL, AV_LOG_WARNING, "Here init with params: %s %d\n", veai->model, veai->device);
  veai->firstFrame = 1;
  return veai->pFrameProcessor == NULL;
}

static int config_props(AVFilterLink *outlink) {
    AVFilterContext *ctx = outlink->src;
    VEAIParamContext *veai = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    float parameter_values[6] = {0,0,0,0,0,0};
    VideoProcessorInfo info;
    info.modelName = veai->model;
    info.scale = 1;
    info.deviceIndex = veai->device;
    info.extraThreadCount = 0;
    info.canDownloadModel = veai->canDownloadModels;
    info.inputWidth = inlink->w;
    info.inputHeight = inlink->h;
    info.timebase = av_q2d(inlink->time_base);
    info.framerate = av_q2d(inlink->frame_rate);
    outlink->w = inlink->w;
    outlink->h = inlink->h;
    memcpy(info.modelParameters, parameter_values, sizeof(info.modelParameters));
    veai->pFrameProcessor = veai_create(&info);
    av_log(NULL, AV_LOG_WARNING, "Here Init model with params: %s %d\n", veai->model, veai->device);
    return veai->pFrameProcessor == NULL ? AVERROR(ENOSYS) : 0;
}


static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_BGR48,
    AV_PIX_FMT_NONE
};

static int filter_frame(AVFilterLink *inlink, AVFrame *in) {
    AVFilterContext *ctx = inlink->dst;
    VEAIParamContext *veai = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    IOBuffer ioBuffer;
    static int count = 1;
    av_log(NULL, AV_LOG_VERBOSE, "Handling frame %d %lf\n", count++, TS2T(in->pts, inlink->time_base));
    ioBuffer.inputBuffer = in->data[0];
    ioBuffer.inputLinesize = in->linesize[0];
    ioBuffer.inputTS = in->pts;
    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }

    ioBuffer.outputBuffer = out->data[0];
    ioBuffer.outputLinesize = out->linesize[0];
    ioBuffer.frameType = FrameTypeNormal;
    if(veai->firstFrame) {
      ioBuffer.frameType = ioBuffer.frameType | FrameTypeStart;
      veai->firstFrame = 0;
    }
    if (veai_upscaler_process(veai->pFrameProcessor,  &ioBuffer)) {
        av_log(NULL, AV_LOG_ERROR, "The processing has failed");
        av_frame_free(&in);
        return AVERROR(ENOSYS);
    }
    av_frame_copy_props(out, in);
    out->pts = ioBuffer.outputTS;
    av_log(NULL, AV_LOG_VERBOSE, "Handling frame BBB %d %lf %lf\n", count++, TS2T(in->pts, inlink->time_base), TS2T(ioBuffer.outputTS, outlink->time_base));
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx) {
    VEAIParamContext *veai = ctx->priv;
    veai_destroy(veai->pFrameProcessor);
}

static const AVFilterPad veai_param_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad veai_param_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
    },
};

const AVFilter ff_vf_veai_param = {
    .name          = "veai_param",
    .description   = NULL_IF_CONFIG_SMALL("Apply Video Enhance AI models."),
    .priv_size     = sizeof(VEAIParamContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(veai_param_inputs),
    FILTER_OUTPUTS(veai_param_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .priv_class    = &veai_param_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
