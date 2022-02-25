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

typedef struct VEAICPEContext {
    const AVClass *class;
    char *model;
    int device, extraThreads;
    int canDownloadModels;
    void* pFrameProcessor;
    int firstFrame;
    unsigned int counter;
} VEAICPEContext;

#define OFFSET(x) offsetof(VEAICPEContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption veai_cpe_options[] = {
    { "model", "Model short name", OFFSET(model), AV_OPT_TYPE_STRING, {.str="cam-1"}, .flags = FLAGS },
    { "device",  "Device index (Auto: -2, CPU: -1, GPU0: 0, ...)",  OFFSET(device),  AV_OPT_TYPE_INT, {.i64=-2}, -2, 8, FLAGS, "device" },
    { "threads",  "Number of extra threads to use on device",  OFFSET(extraThreads),  AV_OPT_TYPE_INT, {.i64=0}, 0, 3, FLAGS, "extraThreads" },
    { "download",  "Enable model downloading",  OFFSET(canDownloadModels),  AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS, "canDownloadModels" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(veai_cpe);

static av_cold int init(AVFilterContext *ctx) {
  VEAICPEContext *veai = ctx->priv;
  av_log(NULL, AV_LOG_DEBUG, "Here init with params: %s %d\n", veai->model, veai->device);
  veai->firstFrame = 1;
  veai->counter = 0;
  return 0;
}

static int config_props(AVFilterLink *outlink) {
    AVFilterContext *ctx = outlink->src;
    VEAICPEContext *veai = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    int logLevel = av_log_get_level();

    if(!(logLevel == AV_LOG_DEBUG || logLevel == AV_LOG_VERBOSE)) {
        veai_disable_logging();
    }
    char devices[1024];
    int device_count = veai_device_list(devices, 1024);
    if(veai->device < -2 || veai->device > device_count ) {
        av_log(NULL, AV_LOG_ERROR, "Invalid value %d for device, device should be in the following list:\n-2 : AUTO \n-1 : CPU\n%s\n%d : ALL GPUs\n", veai->device, devices, device_count);
        return AVERROR(EINVAL);
    }
    char modelString[10024];
    int modelStringSize = veai_model_list(veai->model, 3, modelString, 10024);
    if(modelStringSize > 0) {
        av_log(NULL, AV_LOG_ERROR, "Invalid value %s for model, model should be in the following list:\n%s\n", veai->model, modelString);
        return AVERROR(EINVAL);
    } else if(modelStringSize < 0) {
      av_log(NULL, AV_LOG_ERROR, "%s\n", modelString);
      return AVERROR(EINVAL);
    }
    VideoProcessorInfo info;
    info.basic.processorName = "cpe";
    info.basic.modelName = veai->model;
    info.basic.scale = 1;
    info.basic.deviceIndex = veai->device;
    info.basic.extraThreadCount = veai->extraThreads;
    info.basic.canDownloadModel = veai->canDownloadModels;
    info.basic.inputWidth = inlink->w;
    info.basic.inputHeight = inlink->h;
    info.basic.timebase = av_q2d(inlink->time_base);
    info.basic.framerate = av_q2d(inlink->frame_rate);

    outlink->w = inlink->w;
    outlink->h = inlink->h;

    veai->pFrameProcessor = veai_create(&info);
    av_log(NULL, AV_LOG_DEBUG, "Here Config props model with params: %s %d %d\n", veai->model, veai->device, veai->extraThreads);
    return veai->pFrameProcessor == NULL ? AVERROR(EINVAL) : 0;
}

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_BGR48,
    AV_PIX_FMT_NONE
};

static int filter_frame(AVFilterLink *inlink, AVFrame *in) {
    AVFilterContext *ctx = inlink->dst;
    VEAICPEContext *veai = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    IOBuffer ioBuffer;
    av_log(NULL, AV_LOG_DEBUG, "About to filter frame %s %lf %lf %d\n", veai->model, TS2T(in->pts, inlink->time_base));
    ioBuffer.inputBuffer = in->data[0];
    ioBuffer.inputLinesize = in->linesize[0];
    ioBuffer.inputTS = in->pts;
    if(veai->pFrameProcessor == NULL) {
        av_log(NULL, AV_LOG_ERROR, "The processing has failed, frame processor has not been created");
        return AVERROR(ENOSYS);
    }

    float transform[4] = {0,0,0,0};
    ioBuffer.outputBuffer = transform;
    ioBuffer.outputLinesize = sizeof(float)*4;
    ioBuffer.frameType = FrameTypeNormal;
    int ignoreValue = veai->firstFrame;
    if(veai->firstFrame) {
      ioBuffer.frameType = ioBuffer.frameType | FrameTypeStart;
      veai->firstFrame = 0;
    }
    if (veai_process(veai->pFrameProcessor,  &ioBuffer)) {
        av_log(NULL, AV_LOG_ERROR, "The processing has failed");
        av_frame_free(&in);
        return AVERROR(ENOSYS);
    }
    if(ignoreValue)
      return ff_filter_frame(outlink, in);
    av_log(NULL, AV_LOG_ERROR, "%u CPE: %f\t%f\t%f\t%f\n", veai->counter++, transform[0], transform[1], transform[2], transform[3]);
    return ff_filter_frame(outlink, in);
}

static av_cold void uninit(AVFilterContext *ctx) {
    VEAICPEContext *veai = ctx->priv;
    float transform[4] = {0,0,0,0};
    av_log(NULL, AV_LOG_ERROR, "%u CPE: %f\t%f\t%f\t%f\n", veai->counter++, transform[0], transform[1], transform[2], transform[3]);
    av_log(NULL, AV_LOG_DEBUG, "Uninit called for %s %u\n", veai->model, veai->pFrameProcessor);
    if(veai->pFrameProcessor)
        veai_destroy(veai->pFrameProcessor);
}

static const AVFilterPad veai_cpe_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad veai_cpe_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
    },
};

const AVFilter ff_vf_veai_cpe = {
    .name          = "veai_cpe",
    .description   = NULL_IF_CONFIG_SMALL("Apply Video Enhance AI upscale models, parameters will only be applied to appropriate models"),
    .priv_size     = sizeof(VEAICPEContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(veai_cpe_inputs),
    FILTER_OUTPUTS(veai_cpe_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .priv_class    = &veai_cpe_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
