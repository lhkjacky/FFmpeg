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
#include "veai_common.h"

typedef struct  {
    const AVClass *class;
    char *model;
    int device, extraThreads;
    double slowmo;
    int canDownloadModels;
    void* pFrameProcessor;
    unsigned int count;
    double fpsFactor;
} VEAIFIContext;

#define OFFSET(x) offsetof(VEAIFIContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption veai_fi_options[] = {
    { "model", "Model short name", OFFSET(model), AV_OPT_TYPE_STRING, {.str="chr-1"}, .flags = FLAGS },
    { "slowmo",  "Output fps",  OFFSET(slowmo),  AV_OPT_TYPE_DOUBLE, {.dbl=2.0}, 0.1, 16, FLAGS, "slowmo" },
    { "device",  "Device index (Auto: -2, CPU: -1, GPU0: 0, ...)",  OFFSET(device),  AV_OPT_TYPE_INT, {.i64=-2}, -2, 8, FLAGS, "device" },
    { "threads",  "Number of extra threads to use on device",  OFFSET(extraThreads),  AV_OPT_TYPE_INT, {.i64=0}, 0, 3, FLAGS, "extraThreads" },
    { "download",  "Enable model downloading",  OFFSET(canDownloadModels),  AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS, "canDownloadModels" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(veai_fi);

static av_cold int init(AVFilterContext *ctx) {
  VEAIFIContext *veai = ctx->priv;
  av_log(NULL, AV_LOG_DEBUG, "Here init with params: %s %d %d %lf\n", veai->model, veai->device, veai->extraThreads, veai->slowmo);
  veai->count = 0;
  return 0;
}

static int config_props(AVFilterLink *outlink) {
    AVFilterContext *ctx = outlink->src;
    VEAIFIContext *veai = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    veai->pFrameProcessor = ff_veai_verifyAndCreate(inlink, outlink, (char*)"fi", veai->model, ModelTypeFrameInterpolation, veai->device, veai->extraThreads, 1, veai->canDownloadModels, NULL, 0, ctx);
    return veai->pFrameProcessor == NULL ? AVERROR(EINVAL) : 0;
}


static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_BGR48,
    AV_PIX_FMT_NONE
};

static int filter_frame(AVFilterLink *inlink, AVFrame *in) {
    AVFilterContext *ctx = inlink->dst;
    VEAIFIContext *veai = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    IOBuffer ioBuffer;
    ff_veai_prepareIOBufferInput(&ioBuffer, in, FrameTypeNormal, veai->count == 0);
    if(veai->pFrameProcessor == NULL || veai_process(veai->pFrameProcessor,  &ioBuffer)) {
        av_log(NULL, AV_LOG_ERROR, "The processing has failed");
        av_frame_free(&in);
        return AVERROR(ENOSYS);
    }
    // while(position < framePos) {
    //     parameters["position"] = (position - (int)position);
    //
    //     // if(position < framePos - (1 - _fpsFactor * 0.3)) {
    //     //     qInfo() << "Using previous frame for" << oFrame.framePos << position;
    //     //     output = oFrame.outFrame;
    //     // } else if(position > framePos - _fpsFactor * 0.3) {
    //     //     qInfo() << "Using current frame for" << position;
    //     //     output = frame;
    //     // } else {
    //     //     AIPRINT_DURATION("Process" << QString::number(position, 'g', 5) << "Frame:", imgPTime, output = processEmpty(parameters))
    //     // }
    //     oFrame.framePos = position;
    //     oFrame.outFrame = output.clone();
    //     _lastFramePosition = position;
    //     position+=_fpsFactor;
    //
    // }
    av_frame_copy_props(out, in);
    out->pts = ioBuffer.outputTS;
    veai->count++;
    if(ioBuffer.outputTS < 0) {
      av_log(NULL, AV_LOG_DEBUG, "Ignoring frame %d %s %lf %lf\n", veai->count++, veai->model, TS2T(in->pts, inlink->time_base), TS2T(ioBuffer.outputTS, outlink->time_base));
      return 0;
    }
    av_log(NULL, AV_LOG_DEBUG, "Finished processing frame %d %s %lf %lf\n", veai->count++, veai->model, TS2T(in->pts, inlink->time_base), TS2T(ioBuffer.outputTS, outlink->time_base));
    return ff_filter_frame(outlink, out);
}

// void processManyFrames() {
//
//
//         double position = ceil((framePos - 1) / _fpsFactor) * _fpsFactor;
//         Mat block, output;
//         bool first = true;
//         auto oFrame = _frames.front();
//         if(_lastFramePosition >= position - _fpsFactor * 0.3) {
//             position+= _fpsFactor;
//         }
//
//         while(position < framePos) {
//             parameters["position"] = (position - (int)position);
//
//             if(position < framePos - (1 - _fpsFactor * 0.3)) {
//                 qInfo() << "Using previous frame for" << oFrame.framePos << position;
//                 output = oFrame.outFrame;
//             } else if(position > framePos - _fpsFactor * 0.3) {
//                 qInfo() << "Using current frame for" << position;
//                 output = frame;
//             } else {
//                 AIPRINT_DURATION("Process" << QString::number(position, 'g', 5) << "Frame:", imgPTime, output = processEmpty(parameters))
//             }
//             oFrame.framePos = position;
//             oFrame.outFrame = output.clone();
//             _lastFramePosition = position;
//             position+=_fpsFactor;
//
//         }
// }


static av_cold void uninit(AVFilterContext *ctx) {
    VEAIFIContext *veai = ctx->priv;
    if(veai->pFrameProcessor)
      veai_destroy(veai->pFrameProcessor);
}

static const AVFilterPad veai_fi_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad veai_fi_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
    },
};

const AVFilter ff_vf_veai_fi = {
    .name          = "veai_fi",
    .description   = NULL_IF_CONFIG_SMALL("Apply Video Enhance AI frame interpolation models."),
    .priv_size     = sizeof(VEAIFIContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(veai_fi_inputs),
    FILTER_OUTPUTS(veai_fi_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .priv_class    = &veai_fi_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
