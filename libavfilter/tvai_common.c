#include "tvai_common.h"

int ff_tvai_checkDevice(int deviceIndex, AVFilterContext* ctx) {
  char devices[1024];
  int device_count = tvai_device_list(devices, 1024);
  if(deviceIndex < -2 || deviceIndex > device_count ) {
      av_log(ctx, AV_LOG_ERROR, "Invalid value %d for device, device should be in the following list:\n-2 : AUTO \n-1 : CPU\n%s\n%d : ALL GPUs\n", deviceIndex, devices, device_count);
      return AVERROR(EINVAL);
  }
  return 0;
}

int ff_tvai_checkScale(int scale, AVFilterContext* ctx) {
  if(scale != 1 && scale != 2 && scale !=4 ) {
      av_log(ctx, AV_LOG_ERROR, "Invalid value %d for scale, only 1,2,4 allowed for scale\n", scale);
      return AVERROR(EINVAL);
  }
  return 0;
}

void ff_tvai_handleLogging() {
  int logLevel = av_log_get_level();
  tvai_set_logging(logLevel == AV_LOG_DEBUG || logLevel == AV_LOG_VERBOSE);
}

int ff_tvai_checkModel(char* modelName, ModelType modelType, AVFilterContext* ctx) {
  char modelString[10024];
  int modelStringSize = tvai_model_list(modelName, modelType, modelString, 10024);
  if(modelStringSize > 0) {
      av_log(ctx, AV_LOG_ERROR, "Invalid value %s for model, model should be in the following list:\n%s\n", modelName, modelString);
      return AVERROR(EINVAL);
  } else if(modelStringSize < 0) {
    av_log(ctx, AV_LOG_ERROR, "Some other error:%s\n", modelString);
    return AVERROR(EINVAL);
  }
  return 0;
}

int ff_tvai_verifyAndSetInfo(VideoProcessorInfo* info, AVFilterLink *inlink, AVFilterLink *outlink, char *processorName, char* modelName, ModelType modelType,
                            int deviceIndex, int extraThreads, int vram, int scale, int canDownloadModels, float *pParameters, int parameterCount, AVFilterContext* ctx) {
  ff_tvai_handleLogging();
  if(ff_tvai_checkModel(modelName, modelType, ctx) || ff_tvai_checkDevice(deviceIndex, ctx) || ff_tvai_checkScale(scale, ctx)) {
    return 1;
  }
  info->basic.processorName = processorName;
  info->basic.modelName = modelName;
  info->basic.scale = scale;
  info->basic.device.index = deviceIndex;
  info->basic.device.extraThreadCount = extraThreads;
  info->basic.device.maxMemory = vram;
  info->basic.canDownloadModel = canDownloadModels;
  info->basic.inputWidth = inlink->w;
  info->basic.inputHeight = inlink->h;
  info->basic.timebase = av_q2d(inlink->time_base);
  info->basic.framerate = av_q2d(inlink->frame_rate);
  if(pParameters != NULL && parameterCount > 0) {
    memcpy(info->modelParameters, pParameters, sizeof(float)*parameterCount);
  }
  outlink->w = inlink->w*scale;
  outlink->h = inlink->h*scale;
  outlink->time_base = inlink->time_base;
  outlink->frame_rate = inlink->frame_rate;
  outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;
  av_log(ctx, AV_LOG_DEBUG, "Output size set to: %d %d\n", outlink->w, outlink->h);
  av_log(ctx, AV_LOG_DEBUG, "Here Config props model with params: %s %s %d %d %d %d %d %d %lf %lf\n", info->basic.processorName, info->basic.modelName, info->basic.scale, info->basic.device.index,
          info->basic.device.extraThreadCount, info->basic.canDownloadModel, info->basic.inputWidth, info->basic.inputHeight, info->basic.timebase, info->basic.framerate);
  return 0;
}

void* ff_tvai_verifyAndCreate(AVFilterLink *inlink, AVFilterLink *outlink, char *processorName, char* modelName, ModelType modelType,
                            int deviceIndex, int extraThreads, int vram, int scale, int canDownloadModels, float *pParameters, int parameterCount, AVFilterContext* ctx) {
  VideoProcessorInfo info;
  if(ff_tvai_verifyAndSetInfo(&info, inlink, outlink, processorName, modelName, modelType, deviceIndex, extraThreads, vram, scale, canDownloadModels, pParameters, parameterCount, ctx))
    return NULL;
  return tvai_create(&info);
}

void ff_tvai_prepareBufferInput(TVAIBuffer* ioBuffer, AVFrame *in) {
  ioBuffer->pBuffer = in->data[0];
  ioBuffer->lineSize = in->linesize[0];
  ioBuffer->pts = in->pts;
}

AVFrame* ff_tvai_prepareBufferOutput(AVFilterLink *outlink, TVAIBuffer* oBuffer) {
  AVFrame* out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
  if (!out) {
      av_log(NULL, AV_LOG_ERROR, "The processing has failed, unable to create output buffer of size:%dx%d\n", outlink->w, outlink->h);
      return NULL;
  }
  oBuffer->pBuffer = out->data[0];
  oBuffer->lineSize = out->linesize[0];
  return out;
}

int ff_tvai_handlePostFlight(void* pProcessor, AVFilterLink *outlink, AVFrame *in, AVFilterContext* ctx) {
    tvai_end_stream(pProcessor);
    int i, n = 0;//tvai_queued_frames(pProcessor);
    for(i=0;i<n;i++) {
        TVAIBuffer oBuffer;
        AVFrame *out = ff_tvai_prepareBufferOutput(outlink, &oBuffer);
        if(pProcessor == NULL || out == NULL ||tvai_process(pProcessor, &oBuffer, 0)) {
            av_log(ctx, AV_LOG_ERROR, "The processing has failed");
            av_frame_free(&in);
            return AVERROR(ENOSYS);
        }
        av_frame_copy_props(out, in);
        out->pts = oBuffer.pts;
        if(oBuffer.pts < 0) {
          av_frame_free(&out);
          av_log(ctx, AV_LOG_DEBUG, "Ignoring frame %lf\n", TS2T(oBuffer.pts, outlink->time_base));
          //return AVERROR(ENOSYS);
          continue;
        }
        av_log(ctx, AV_LOG_DEBUG, "Finished processing frame %lf\n", TS2T(oBuffer.pts, outlink->time_base));
        int code = ff_filter_frame(outlink, out);
        if(code) {
          return code;
        }
    }
    return 0;
}

int ff_tvai_handleQueue(void* pProcessor, AVFilterLink *outlink, AVFrame *in, AVFilterContext* ctx) {
    tvai_end_stream(pProcessor);
    int i, n = 0;//tvai_queued_frames(pProcessor);
    for(i=0;i<n;i++) {
        TVAIBuffer oBuffer;
        AVFrame *out = ff_tvai_prepareBufferOutput(outlink, &oBuffer);
        if(pProcessor == NULL || out == NULL ||tvai_process(pProcessor, &oBuffer, 0)) {
            av_log(ctx, AV_LOG_ERROR, "The processing has failed");
            av_frame_free(&in);
            return AVERROR(ENOSYS);
        }
        av_frame_copy_props(out, in);
        out->pts = oBuffer.pts;
        if(oBuffer.pts < 0) {
          av_frame_free(&out);
          av_log(ctx, AV_LOG_DEBUG, "Ignoring frame %lf\n", TS2T(oBuffer.pts, outlink->time_base));
          //return AVERROR(ENOSYS);
          continue;
        }
        av_log(ctx, AV_LOG_DEBUG, "Finished processing frame %lf\n", TS2T(oBuffer.pts, outlink->time_base));
        int code = ff_filter_frame(outlink, out);
        if(code) {
          return code;
        }
    }
    return 0;
}

int ff_tvai_estimateParam(AVFilterContext* ctx, void* pProcessor, AVFrame* in, int isFirstFrame, float *parameters) {
    IOBuffer ioBuffer;
    ff_tvai_prepareBufferInput(&ioBuffer, in);
    ioBuffer.output.pBuffer = (unsigned char *)parameters;
    ioBuffer.output.lineSize = sizeof(float)*TVAI_MAX_PARAMETER_COUNT;
    if(pProcessor == NULL || tvai_process(pProcessor,  &ioBuffer, 0)) {
        av_log(NULL, AV_LOG_ERROR, "The processing has failed");
        av_frame_free(&in);
        return AVERROR(ENOSYS);
    }
    if(ioBuffer.output.pts < 0) {
        av_log(ctx, AV_LOG_DEBUG, "Ignoring frame %lld\n", ioBuffer.output.pts);
        return 1;
    }
    av_log(ctx, AV_LOG_WARNING, "Parameter values:[");
    for(int i=0;i<TVAI_MAX_PARAMETER_COUNT;i++) {
        av_log(ctx, AV_LOG_WARNING, " %f,", parameters[i]);
    }
    av_log(ctx, AV_LOG_WARNING, "]\n");
    return 0;
}
