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

typedef struct TVAIStbContext {
    const AVClass *class;
    char *model, *filename, *filler;
    int device, extraThreads;
    int canDownloadModels;
    void* pFrameProcessor;
    double smoothness;
    AVFrame* previousFrame;
} TVAIStbContext;

#define OFFSET(x) offsetof(TVAIStbContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption tvai_stb_options[] = {
    { "model", "Model short name", OFFSET(model), AV_OPT_TYPE_STRING, {.str="ref-1"}, .flags = FLAGS },
    { "device",  "Device index (Auto: -2, CPU: -1, GPU0: 0, ...)",  OFFSET(device),  AV_OPT_TYPE_INT, {.i64=-2}, -2, 8, FLAGS, "device" },
    { "threads",  "Number of extra threads to use on device",  OFFSET(extraThreads),  AV_OPT_TYPE_INT, {.i64=0}, 0, 3, FLAGS, "extraThreads" },
    { "download",  "Enable model downloading",  OFFSET(canDownloadModels),  AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS, "canDownloadModels" },
    { "filename", "CPE output filename", OFFSET(filename), AV_OPT_TYPE_STRING, {.str="cpe.json"}, .flags = FLAGS },
    { "filler", "Filler output path", OFFSET(filler), AV_OPT_TYPE_STRING, {.str="./"}, .flags = FLAGS },
    { "smoothness", "Amount of smoothness to be applied on the camera trajectory to stabilize the video",  OFFSET(smoothness),  AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0.0, 16.0, FLAGS, "smoothness" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(tvai_stb);

static av_cold int init(AVFilterContext *ctx) {
  TVAIStbContext *tvai = ctx->priv;
  av_log(ctx, AV_LOG_VERBOSE, "Here init with params: %s %d %s %s %lf\n", tvai->model, tvai->device, tvai->filename, tvai->filler, tvai->smoothness);
  tvai->previousFrame = NULL;
  return 0;
}

static int config_props(AVFilterLink *outlink) {
  AVFilterContext *ctx = outlink->src;
  TVAIStbContext *tvai = ctx->priv;
  AVFilterLink *inlink = ctx->inputs[0];
  VideoProcessorInfo info;
  info.options[0] = tvai->filename;
  info.options[1] = tvai->filler;
  float smoothness = tvai->smoothness;
  if(ff_tvai_verifyAndSetInfo(&info, inlink, outlink, (char*)"st", tvai->model, ModelTypeStabilization, tvai->device, tvai->extraThreads, 1, tvai->canDownloadModels, &smoothness, 1, ctx)) {
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
    TVAIStbContext *tvai = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    IOBuffer ioBuffer;
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
      av_log(ctx, AV_LOG_DEBUG, "Ignoring frame %s %lf %lf\n", tvai->model, its, TS2T(ioBuffer.output.timestamp, outlink->time_base));
      return 0;
    }
    av_log(ctx, AV_LOG_DEBUG, "Finished processing frame %s %lf %lf\n", tvai->model, its, TS2T(ioBuffer.output.timestamp, outlink->time_base));
    return ff_filter_frame(outlink, out);
}

static int request_frame(AVFilterLink *outlink) {
    AVFilterContext *ctx = outlink->src;
    TVAIStbContext *tvai = ctx->priv;
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
    TVAIStbContext *tvai = ctx->priv;
    av_log(ctx, AV_LOG_DEBUG, "Uninit called for %s %d\n", tvai->model, tvai->pFrameProcessor == NULL);
    if(tvai->pFrameProcessor)
        tvai_destroy(tvai->pFrameProcessor);
}

static const AVFilterPad tvai_stb_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad tvai_stb_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
        .request_frame = request_frame,
    },
};

const AVFilter ff_vf_tvai_stb = {
    .name          = "tvai_stb",
    .description   = NULL_IF_CONFIG_SMALL("Apply Video Enhance AI stabilization models"),
    .priv_size     = sizeof(TVAIStbContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(tvai_stb_inputs),
    FILTER_OUTPUTS(tvai_stb_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .priv_class    = &tvai_stb_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
