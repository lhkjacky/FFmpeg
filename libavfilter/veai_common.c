#include "veai_common.h"

int veai_checkDevice(int deviceIndex) {
  char devices[1024];
  int device_count = veai_device_list(devices, 1024);
  if(deviceIndex < -2 || deviceIndex > device_count ) {
      av_log(NULL, AV_LOG_ERROR, "Invalid value %d for device, device should be in the following list:\n-2 : AUTO \n-1 : CPU\n%s\n%d : ALL GPUs\n", deviceIndex, devices, device_count);
      return AVERROR(EINVAL);
  }
  return 0;
}

int veai_checkScale(int scale) {
  if(scale != 1 && scale != 2 && scale !=4 ) {
      av_log(NULL, AV_LOG_ERROR, "Invalid value %d for scale, only 1,2,4 allowed for scale\n", scale);
      return AVERROR(EINVAL);
  }
  return 0;
}

void veai_handleLogging() {
  int logLevel = av_log_get_level();
  if(!(logLevel == AV_LOG_DEBUG || logLevel == AV_LOG_VERBOSE)) {
    veai_disable_logging();
  }
}

int veai_checkModel(char* modelName, ModelType modelType) {
  av_log(NULL, AV_LOG_DEBUG, "Checking value %s for model, model should be in the following list\n", modelName);
  char modelString[10024];
  int modelStringSize = veai_model_list(modelName, modelType, modelString, 10024);

  if(modelStringSize > 0) {
      av_log(NULL, AV_LOG_ERROR, "Invalid value %s for model, model should be in the following list:\n%s\n", modelName, modelString);
      return AVERROR(EINVAL);
  } else if(modelStringSize < 0) {
    av_log(NULL, AV_LOG_ERROR, "Some other error:%s\n", modelString);
    return AVERROR(EINVAL);
  }
  return 0;
}

void* veai_verifyAndCreate(AVFilterLink *inlink, AVFilterLink *outlink, char *processorName, char* modelName, ModelType modelType,
                            int deviceIndex, int extraThreads, int scale, int canDownloadModels, float *pParameters, int parameterCount) {
  veai_handleLogging();
  if(veai_checkModel(modelName, modelType) || veai_checkDevice(deviceIndex) || veai_checkScale(scale)) {
    return NULL;
  }
  VideoProcessorInfo info;
  info.basic.processorName = processorName;
  info.basic.modelName = modelName;
  info.basic.scale = scale;
  info.basic.deviceIndex = deviceIndex;
  info.basic.extraThreadCount = extraThreads;
  info.basic.canDownloadModel = canDownloadModels;
  info.basic.inputWidth = inlink->w;
  info.basic.inputHeight = inlink->h;
  info.basic.timebase = av_q2d(inlink->time_base);
  info.basic.framerate = av_q2d(inlink->frame_rate);
  if(pParameters != NULL && parameterCount > 0) {
    memcpy(info.modelParameters, pParameters, sizeof(float)*parameterCount);
  }
  outlink->w = inlink->w*scale;
  outlink->h = inlink->h*scale;
  av_log(NULL, AV_LOG_DEBUG, "Here Config props model with params: %s %s %d %d %d %d %d %d %lf %lf\n", info.basic.processorName, info.basic.modelName, info.basic.scale, info.basic.deviceIndex,
          info.basic.extraThreadCount, info.basic.canDownloadModel, info.basic.inputWidth, info.basic.inputHeight, info.basic.timebase, info.basic.framerate);
  return veai_create(&info);
}

void veai_prepareIOBufferInput(IOBuffer* ioBuffer, AVFrame *in, FrameType frameType, int isFirst) {
  ioBuffer->inputBuffer = in->data[0];
  ioBuffer->inputLinesize = in->linesize[0];
  ioBuffer->inputTS = in->pts;
  ioBuffer->frameType = frameType | (isFirst ? FrameTypeStart : FrameTypeNone);
}

AVFrame* veai_prepareIOBufferOutput(AVFilterLink *outlink, IOBuffer* ioBuffer) {
  AVFrame* out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
  if (!out) {
      av_log(NULL, AV_LOG_ERROR, "The processing has failed, unable to create output buffer of size:%dx%d\n", outlink->w, outlink->h);
      return NULL;
  }
  ioBuffer->outputBuffer = out->data[0];
  ioBuffer->outputLinesize = out->linesize[0];
  return out;
}
