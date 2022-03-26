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
#include "veai_common.h"

typedef struct VEAIUpContext {
    const AVClass *class;
    char *model;
    int device, scale, extraThreads;
    int canDownloadModels;
    int estimateParamNthFrame, count, estimating;
    double preBlur, noise, details, halo, blur, compression;
    void* pFrameProcessor;
    void* pParamEstimator;
    AVFrame* previousFrame;
} VEAIUpContext;

#define OFFSET(x) offsetof(VEAIUpContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption veai_up_options[] = {
    { "model", "Model short name", OFFSET(model), AV_OPT_TYPE_STRING, {.str="amq-13"}, .flags = FLAGS },
    { "scale",  "Output scale",  OFFSET(scale),  AV_OPT_TYPE_INT, {.i64=1}, 0, 10, FLAGS, "scale" },
    { "device",  "Device index (Auto: -2, CPU: -1, GPU0: 0, ...)",  OFFSET(device),  AV_OPT_TYPE_INT, {.i64=-2}, -2, 8, FLAGS, "device" },
    { "threads",  "Number of extra threads to use on device",  OFFSET(extraThreads),  AV_OPT_TYPE_INT, {.i64=0}, 0, 3, FLAGS, "extraThreads" },
    { "download",  "Enable model downloading",  OFFSET(canDownloadModels),  AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS, "canDownloadModels" },
    { "estimate",  "Auto estimate parameter every Nth frame, 0 to disable auto parameter estimation",  OFFSET(estimateParamNthFrame),  AV_OPT_TYPE_INT, {.i64=0}, 0, 1000000, FLAGS, "estimateParamNthFrame" },
    { "preblur",  "Adjusts both the antialiasing and deblurring strength relative to the amount of aliasing and blurring in the input video. \nNegative values are better if the input video has aliasing artifacts such as moire patterns or staircasing. Positive values are better if the input video has more lens blurring than aliasing artifacts. ",  OFFSET(preBlur),  AV_OPT_TYPE_DOUBLE, {.dbl=0}, -1.0, 1.0, FLAGS, "preblur" },
    { "noise",  "Removes ISO noise from the input video. Higher values remove more noise but may also remove fine details. \nNote that this value is relative to the amount of noise found in the input video - higher values on videos with low amounts of ISO noise may introduce more artifacts.",  OFFSET(noise),  AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 1.0, FLAGS, "noise" },
    { "details",  "Used to recover fine texture and detail lost due to in-camera noise suppression. \nThis value is relative to the amount of noise suppression in the camera used for the input video, and higher values may introduce artifacts if the input video has little to no in-camera noise suppression.",  OFFSET(details),  AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 1.0, FLAGS, "details" },
    { "halo",  "Increase this if the input video has halo or ring artifacts around strong edges caused by oversharpening. \nThis value is relative to the amount of haloing artifacts in the input video, and has a \"sweet spot\". Values that are too high for the input video may cause additional artifacts to appear.",  OFFSET(halo),  AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 1.0, FLAGS, "halo" },
    { "blur",  "Additional sharpening of the video. Use this if the input video looks too soft. \nThe value set should be relative to the amount of softness in the input video - if the input video is already sharp, higher values will introduce more artifacts.",  OFFSET(blur),  AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 1.0, FLAGS, "blur" },
    { "compression",  "Reduces compression artifacts from codec encoding, such as blockiness or mosquito noise. Higher values are best for low bitrate videos.\nNote that the value should be relative to the amount of compression artifacts in the input video - higher values on a video with few compression artifacts will introduce more artifacts into the output.",  OFFSET(compression),  AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 1.0, FLAGS, "compression" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(veai_up);

static av_cold int init(AVFilterContext *ctx) {
  VEAIUpContext *veai = ctx->priv;
  av_log(ctx, AV_LOG_VERBOSE, "Here init with params: %s %d %d %lf %lf %lf %lf %lf %lf\n", veai->model, veai->scale, veai->device,
        veai->preBlur, veai->noise, veai->details, veai->halo, veai->blur, veai->compression);
  veai->previousFrame = NULL;
  veai->estimating = 0;
  veai->count = 0;
  return 0;
}

static int config_props(AVFilterLink *outlink) {
    AVFilterContext *ctx = outlink->src;
    VEAIUpContext *veai = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    float parameter_values[6] = {veai->preBlur, veai->noise, veai->details, veai->halo, veai->blur, veai->compression};

    veai->pFrameProcessor = ff_veai_verifyAndCreate(inlink, outlink, (char*)"up", veai->model, ModelTypeUpscaling, veai->device, veai->extraThreads,
                                                    veai->scale, veai->canDownloadModels, parameter_values, 6, ctx);
    veai->pParamEstimator = ff_veai_verifyAndCreate(inlink, outlink, (char*)"pe", "prap-3", ModelTypeParameterEstimation, veai->device, 0, 1, veai->canDownloadModels, NULL, 0, ctx);
    return (veai->pFrameProcessor == NULL || veai->pParamEstimator == NULL) ? AVERROR(EINVAL) : 0;
}

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_BGR48,
    AV_PIX_FMT_NONE
};

static int filter_frame(AVFilterLink *inlink, AVFrame *in) {
    AVFilterContext *ctx = inlink->dst;
    VEAIUpContext *veai = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    IOBuffer ioBuffer;
    if(veai->estimateParamNthFrame > 0 && (veai->count%veai->estimateParamNthFrame == 0 || veai->estimating)) {
        float parameters[VEAI_MAX_PARAMETER_COUNT] = {0};
        int result = ff_veai_estimateParam(ctx, veai->pParamEstimator, in, !veai->estimating, parameters);
        if(result == 0) {
            if(veai_parameter_update(veai->pFrameProcessor, parameters) != 0) {
              av_log(NULL, AV_LOG_ERROR, "Updating parameters has failed\n");
              av_frame_free(&in);
              return AVERROR(ENOSYS);
            }
            veai->estimating = 0;
        } else if(result != 1) {
            return result;
        } else {
            veai->estimating = 1;
        }
    }
    ff_veai_prepareIOBufferInput(&ioBuffer, in, FrameTypeNormal, veai->previousFrame == NULL);
    out = ff_veai_prepareBufferOutput(outlink, &ioBuffer.output);
    if(veai->pFrameProcessor == NULL || out == NULL || veai_process(veai->pFrameProcessor,  &ioBuffer)) {
        av_log(NULL, AV_LOG_ERROR, "The processing has failed\n");
        av_frame_free(&in);
        return AVERROR(ENOSYS);
    }
    double its = TS2T(in->pts, inlink->time_base);
    av_frame_copy_props(out, in);
    out->pts = ioBuffer.output.timestamp;
    if(veai->previousFrame)
      av_frame_free(&veai->previousFrame);
    veai->previousFrame = in;
    veai->count++;
    if(ioBuffer.output.timestamp < 0) {
      av_frame_free(&out);
      av_log(ctx, AV_LOG_DEBUG, "Ignoring frame %d %s %u %lf %lf\n", veai->count, veai->model, veai->scale, its, TS2T(ioBuffer.output.timestamp, outlink->time_base));
      return 0;
    }
    av_log(ctx, AV_LOG_WARNING, "Finished processing frame %d %s %u %lf %lf\n", veai->count, veai->model, veai->scale, its, TS2T(ioBuffer.output.timestamp, outlink->time_base));
    return ff_filter_frame(outlink, out);
}

static int request_frame(AVFilterLink *outlink) {
    AVFilterContext *ctx = outlink->src;
    VEAIUpContext *veai = ctx->priv;
    int ret = ff_request_frame(ctx->inputs[0]);
    if (ret == AVERROR_EOF) {
        if(ff_veai_handlePostFlight(veai->pFrameProcessor, outlink, veai->previousFrame, ctx)) {
          av_log(NULL, AV_LOG_ERROR, "The postflight processing has failed");
          av_frame_free(&veai->previousFrame);
          return AVERROR(ENOSYS);
        }
        av_frame_free(&veai->previousFrame);
        av_log(ctx, AV_LOG_DEBUG, "End of file reached %s %d\n", veai->model, veai->pFrameProcessor == NULL);
    }
    return ret;
}

static av_cold void uninit(AVFilterContext *ctx) {
    VEAIUpContext *veai = ctx->priv;
    av_log(ctx, AV_LOG_DEBUG, "Uninit called for %s %d\n", veai->model, veai->pFrameProcessor == NULL);
    if(veai->pFrameProcessor)
        veai_destroy(veai->pFrameProcessor);
    if(veai->pParamEstimator)
        veai_destroy(veai->pParamEstimator);

}

static const AVFilterPad veai_up_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad veai_up_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
        .request_frame = request_frame,
    },
};

const AVFilter ff_vf_veai_up = {
    .name          = "veai_up",
    .description   = NULL_IF_CONFIG_SMALL("Apply Video Enhance AI upscale models, parameters will only be applied to appropriate models"),
    .priv_size     = sizeof(VEAIUpContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(veai_up_inputs),
    FILTER_OUTPUTS(veai_up_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .priv_class    = &veai_up_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
