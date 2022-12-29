#ifndef TVAI_COMMON_H
#define TVAI_COMMON_H

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/avutil.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "tvai_data.h"
#include "tvai.h"

int ff_tvai_checkDevice(int deviceIndex, AVFilterContext* ctx);
int ff_tvai_checkScale(int scale, AVFilterContext* ctx);
int ff_tvai_checkModel(char* modelName, ModelType modelType, AVFilterContext* ctx);
void ff_tvai_handleLogging(void);
int ff_tvai_verifyAndSetInfo(VideoProcessorInfo* info, AVFilterLink *inlink, AVFilterLink *outlink, char *processorName, char* modelName, ModelType modelType,
                            int deviceIndex, int extraThreads, int vram, int scale, int canDownloadModels, float *pParameters, int parameterCount, AVFilterContext* ctx);
void* ff_tvai_verifyAndCreate(AVFilterLink *inlink, AVFilterLink *outlink, char *processorName, char* modelName, ModelType modelType,
                            int deviceIndex, int extraThreads, int vram, int scale, int canDownloadModels, float *pParameters, int parameterCount, AVFilterContext* ctx);
void ff_tvai_prepareBufferInput(TVAIBuffer* ioBuffer, AVFrame *in);
AVFrame* ff_tvai_prepareBufferOutput(AVFilterLink *outlink, TVAIBuffer* oBuffer);
int ff_tvai_estimateParam(AVFilterContext* ctx, void* pProcessor, AVFrame* in, int isFirstFrame, float *parameters);
int ff_tvai_handlePostFlight(void* pProcessor, AVFilterLink *outlink, AVFrame *in, AVFilterContext* ctx);
int ff_tvai_handleQueue(void* pProcessor, AVFilterLink *outlink, AVFrame *in, AVFilterContext* ctx);

#endif
