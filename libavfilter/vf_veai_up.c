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

typedef struct TVAIUpContext {
    const AVClass *class;
    char *model;
    int device, scale, extraThreads;
    int canDownloadModels;
    double preBlur, noise, details, halo, blur, compression;
    void* pFrameProcessor;
    AVFrame* previousFrame;
} TVAIUpContext;

#define OFFSET(x) offsetof(TVAIUpContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption tvai_up_options[] = {
    { "model", "Model short name", OFFSET(model), AV_OPT_TYPE_STRING, {.str="aaa-9"}, .flags = FLAGS },
    { "scale",  "Output scale",  OFFSET(scale),  AV_OPT_TYPE_INT, {.i64=1}, 0, 10, FLAGS, "scale" },
    { "device",  "Device index (Auto: -2, CPU: -1, GPU0: 0, ...)",  OFFSET(device),  AV_OPT_TYPE_INT, {.i64=-2}, -2, 8, FLAGS, "device" },
    { "threads",  "Number of extra threads to use on device",  OFFSET(extraThreads),  AV_OPT_TYPE_INT, {.i64=0}, 0, 3, FLAGS, "extraThreads" },
    { "download",  "Enable model downloading",  OFFSET(canDownloadModels),  AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS, "canDownloadModels" },
    { "preblur",  "Adjusts both the antialiasing and deblurring strength relative to the amount of aliasing and blurring in the input video. \nNegative values are better if the input video has aliasing artifacts such as moire patterns or staircasing. Positive values are better if the input video has more lens blurring than aliasing artifacts. ",  OFFSET(preBlur),  AV_OPT_TYPE_DOUBLE, {.dbl=0}, -0.99, 0.99, FLAGS, "preblur" },
    { "noise",  "Removes ISO noise from the input video. Higher values remove more noise but may also remove fine details. \nNote that this value is relative to the amount of noise found in the input video - higher values on videos with low amounts of ISO noise may introduce more artifacts.",  OFFSET(noise),  AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 0.99, FLAGS, "noise" },
    { "details",  "Used to recover fine texture and detail lost due to in-camera noise suppression. \nThis value is relative to the amount of noise suppression in the camera used for the input video, and higher values may introduce artifacts if the input video has little to no in-camera noise suppression.",  OFFSET(details),  AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 0.99, FLAGS, "details" },
    { "halo",  "Increase this if the input video has halo or ring artifacts around strong edges caused by oversharpening. \nThis value is relative to the amount of haloing artifacts in the input video, and has a \"sweet spot\". Values that are too high for the input video may cause additional artifacts to appear.",  OFFSET(halo),  AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 0.99, FLAGS, "halo" },
    { "blur",  "Additional sharpening of the video. Use this if the input video looks too soft. \nThe value set should be relative to the amount of softness in the input video - if the input video is already sharp, higher values will introduce more artifacts.",  OFFSET(blur),  AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 0.99, FLAGS, "blur" },
    { "compression",  "Reduces compression artifacts from codec encoding, such as blockiness or mosquito noise. Higher values are best for low bitrate videos.\nNote that the value should be relative to the amount of compression artifacts in the input video - higher values on a video with few compression artifacts will introduce more artifacts into the output.",  OFFSET(compression),  AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 0.99, FLAGS, "compression" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(tvai_up);

static av_cold int init(AVFilterContext *ctx) {
  TVAIUpContext *tvai = ctx->priv;
  av_log(ctx, AV_LOG_VERBOSE, "Here init with params: %s %d %d %lf %lf %lf %lf %lf %lf\n", tvai->model, tvai->scale, tvai->device,
        tvai->preBlur, tvai->noise, tvai->details, tvai->halo, tvai->blur, tvai->compression);
  tvai->previousFrame = NULL;
  return 0;
}

static int config_props(AVFilterLink *outlink) {
    AVFilterContext *ctx = outlink->src;
    TVAIUpContext *tvai = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    float parameter_values[6] = {tvai->preBlur, tvai->noise, tvai->details, tvai->halo, tvai->blur, tvai->compression};
    tvai->pFrameProcessor = ff_tvai_verifyAndCreate(inlink, outlink, (char*)"up", tvai->model, ModelTypeUpscaling, tvai->device, tvai->extraThreads,
                                                    tvai->scale, tvai->canDownloadModels, parameter_values, 6, ctx);
    return tvai->pFrameProcessor == NULL ? AVERROR(EINVAL) : 0;
}

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_BGR48,
    AV_PIX_FMT_NONE
};

static int filter_frame(AVFilterLink *inlink, AVFrame *in) {
    AVFilterContext *ctx = inlink->dst;
    TVAIUpContext *tvai = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    IOBuffer ioBuffer;
    static int count = 0;
    ff_tvai_prepareIOBufferInput(&ioBuffer, in, FrameTypeNormal, tvai->previousFrame == NULL);
    out = ff_tvai_prepareBufferOutput(outlink, &ioBuffer.output);
    if(tvai->pFrameProcessor == NULL || out == NULL || tvai_process(tvai->pFrameProcessor,  &ioBuffer)) {
        av_log(NULL, AV_LOG_ERROR, "The processing has failed");
        av_frame_free(&in);
        return AVERROR(ENOSYS);
    }
    double its = TS2T(in->pts, inlink->time_base);
    av_frame_copy_props(out, in);
    out->pts = ioBuffer.output.timestamp;
    if(tvai->previousFrame)
      av_frame_free(&tvai->previousFrame);
    tvai->previousFrame = in;
    if(ioBuffer.output.timestamp < 0) {
      av_frame_free(&out);
      av_log(ctx, AV_LOG_DEBUG, "Ignoring frame %d %s %u %lf %lf\n", count, tvai->model, tvai->scale, its, TS2T(ioBuffer.output.timestamp, outlink->time_base));
      return 0;
    }
    av_log(ctx, AV_LOG_DEBUG, "Finished processing frame %d %s %u %lf %lf\n", count++, tvai->model, tvai->scale, its, TS2T(ioBuffer.output.timestamp, outlink->time_base));
    return ff_filter_frame(outlink, out);
}

static int request_frame(AVFilterLink *outlink) {
    AVFilterContext *ctx = outlink->src;
    TVAIUpContext *tvai = ctx->priv;
    int ret = ff_request_frame(ctx->inputs[0]);
    if (ret == AVERROR_EOF) {
        if(ff_tvai_handlePostFlight(tvai->pFrameProcessor, outlink, tvai->previousFrame, ctx)) {
          av_log(NULL, AV_LOG_ERROR, "The postflight processing has failed");
          av_frame_free(&tvai->previousFrame);
          return AVERROR(ENOSYS);
        }
        av_frame_free(&tvai->previousFrame);
        av_log(ctx, AV_LOG_DEBUG, "End of file reached %s %d\n", tvai->model, tvai->pFrameProcessor == NULL);
    }
    return ret;
}

static av_cold void uninit(AVFilterContext *ctx) {
    TVAIUpContext *tvai = ctx->priv;
    av_log(ctx, AV_LOG_DEBUG, "Uninit called for %s %d\n", tvai->model, tvai->pFrameProcessor == NULL);
    if(tvai->pFrameProcessor)
        tvai_destroy(tvai->pFrameProcessor);
}

static const AVFilterPad tvai_up_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad tvai_up_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
        .request_frame = request_frame,
    },
};

const AVFilter ff_vf_tvai_up = {
    .name          = "tvai_up",
    .description   = NULL_IF_CONFIG_SMALL("Apply Video Enhance AI upscale models, parameters will only be applied to appropriate models"),
    .priv_size     = sizeof(TVAIUpContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(tvai_up_inputs),
    FILTER_OUTPUTS(tvai_up_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .priv_class    = &tvai_up_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
