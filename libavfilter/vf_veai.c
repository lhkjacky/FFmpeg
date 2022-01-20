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

typedef struct VEAIContext {
    const AVClass *class;
    char *model;
    int device, scale, extraThreads;
    int canDownloadModels;
    double preBlur, noise, details, halo, blur, compression;
    void* pFrameProcessor;
} VEAIContext;

#define OFFSET(x) offsetof(VEAIContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption veai_options[] = {
    { "model", "Model short name", OFFSET(model), AV_OPT_TYPE_STRING, {.str="aaa-9"}, .flags = FLAGS },
    { "scale",  "Output scale",  OFFSET(scale),  AV_OPT_TYPE_INT, {.i64=1}, 0, 10, FLAGS, "scale" },
    { "device",  "Device index (Auto: -2, CPU: -1, GPU0: 0, ...)",  OFFSET(device),  AV_OPT_TYPE_INT, {.i64=-2}, -2, 8, FLAGS, "device" },
    { "threads",  "Number of extra threads to use on device",  OFFSET(extraThreads),  AV_OPT_TYPE_INT, {.i64=0}, 0, 3, FLAGS, "extraThreads" },
    { "download",  "Enable model downloading",  OFFSET(canDownloadModels),  AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS, "canDownloadModels" },
    { "preblur",  "Adjusts both the antialiasing and deblurring strength relative to the amount of aliasing and blurring in the input video. \nNegative values are better if the input video has aliasing artifacts such as moire patterns or staircasing. Positive values are better if the input video has more lens blurring than aliasing artifacts. ",  OFFSET(scale),  AV_OPT_TYPE_DOUBLE, {.dbl=1}, -0.99, 0.99, FLAGS, "preblur" },
    { "noise",  "Removes ISO noise from the input video. Higher values remove more noise but may also remove fine details. \nNote that this value is relative to the amount of noise found in the input video - higher values on videos with low amounts of ISO noise may introduce more artifacts.",  OFFSET(scale),  AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 0.99, FLAGS, "noise" },
    { "details",  "Used to recover fine texture and detail lost due to in-camera noise suppression. \nThis value is relative to the amount of noise suppression in the camera used for the input video, and higher values may introduce artifacts if the input video has little to no in-camera noise suppression.",  OFFSET(scale),  AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 0.99, FLAGS, "details" },
    { "halo",  "Increase this if the input video has halo or ring artifacts around strong edges caused by oversharpening. \nThis value is relative to the amount of haloing artifacts in the input video, and has a \"sweet spot\". Values that are too high for the input video may cause additional artifacts to appear.",  OFFSET(scale),  AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 0.99, FLAGS, "halo" },
    { "blur",  "Additional sharpening of the video. Use this if the input video looks too soft. \nThe value set should be relative to the amount of softness in the input video - if the input video is already sharp, higher values will introduce more artifacts.",  OFFSET(scale),  AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 0.99, FLAGS, "blur" },
    { "compression",  "Reduces compression artifacts from codec encoding, such as blockiness or mosquito noise. Higher values are best for low bitrate videos.\nNote that the value should be relative to the amount of compression artifacts in the input video - higher values on a video with few compression artifacts will introduce more artifacts into the output.",  OFFSET(scale),  AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 0.99, FLAGS, "compression" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(veai);

static av_cold int init(AVFilterContext *ctx) {
  VEAIContext *veai = ctx->priv;
  av_log(NULL, AV_LOG_WARNING, "Here init with params: %s %d %d %lf %lf %lf %lf %lf %lf\n", veai->model, veai->scale, veai->device,
        veai->preBlur, veai->noise, veai->details, veai->halo, veai->blur, veai->compression);

  return veai->pFrameProcessor == NULL;
}

static int config_props(AVFilterLink *outlink) {
    AVFilterContext *ctx = outlink->src;
    VEAIContext *veai = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    float parameter_values[6] = {veai->preBlur, veai->noise, veai->details, veai->halo, veai->blur, veai->compression};
    VideoProcessorInfo info;
    info.modelName = veai->model;
    info.scale = veai->scale;
    info.deviceIndex = veai->device;
    info.extraThreadCount = veai->extraThreads;
    info.canDownloadModel = veai->canDownloadModels;
    info.inputWidth = inlink->w;
    info.inputHeight = inlink->h;
    info.timebase = av_q2d(inlink->time_base);
    info.framerate = av_q2d(inlink->frame_rate);
    outlink->w = inlink->w;
    outlink->h = inlink->h;
    memcpy(info.modelParameters, parameter_values, sizeof(info.modelParameters));
    veai->pFrameProcessor = veai_create(&info);
    av_log(NULL, AV_LOG_WARNING, "Here Init model with params: %s %d %d %d %lf %lf %lf %lf %lf %lf\n", veai->model, veai->scale, veai->device, veai->extraThreads,
          veai->preBlur, veai->noise, veai->details, veai->halo, veai->blur, veai->compression);
    return veai->pFrameProcessor == NULL;
}


static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_BGR48,
    AV_PIX_FMT_NONE
};

static int filter_frame(AVFilterLink *inlink, AVFrame *in) {
    AVFilterContext *ctx = inlink->dst;
    VEAIContext *veai = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    IOBuffer ioBuffer;
    static int count = 1;
    //av_log(NULL, AV_LOG_WARNING, "Handling frame %d %lf\n", count++, TS2T(in->pts, inlink->time_base));
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
    if (veai_upscaler_process(veai->pFrameProcessor,  &ioBuffer)) {
        av_log(NULL, AV_LOG_ERROR, "The processing has failed");
        av_frame_free(&in);
        return AVERROR(ENOSYS);
    }
    //memcpy(out->data[0], in->data[0], in->linesize[0]*inlink->h);
    av_frame_copy_props(out, in);
    out->pts = ioBuffer.outputTS;
    av_log(NULL, AV_LOG_WARNING, "Handling frame %d %lf %lf\n", count++, TS2T(in->pts, inlink->time_base), TS2T(ioBuffer.outputTS, outlink->time_base));
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx) {
    VEAIContext *veai = ctx->priv;
    //veai->pFrameProcessor = veaiCreateFrameProcessor();
}

static const AVFilterPad veai_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad veai_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
    },
};

const AVFilter ff_vf_veai = {
    .name          = "veai",
    .description   = NULL_IF_CONFIG_SMALL("Apply Video Enhance AI models."),
    .priv_size     = sizeof(VEAIContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(veai_inputs),
    FILTER_OUTPUTS(veai_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .priv_class    = &veai_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
