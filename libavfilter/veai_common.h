#ifndef VEAI_COMMON_H
#define VEAI_COMMON_H

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/avutil.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "veai_data.h"
#include "veai.h"

void veai_test(AVFilterContext* ctx);
int veai_checkDevice(int deviceIndex, AVFilterContext* ctx);
int veai_checkScale(int scale, AVFilterContext* ctx);
int veai_checkModel(char* modelName, ModelType modelType, AVFilterContext* ctx);
void veai_handleLogging(void);
void* veai_verifyAndCreate(AVFilterLink *inlink, AVFilterLink *outlink, char *processorName, char* modelName, ModelType modelType,
                            int deviceIndex, int extraThreads, int scale, int canDownloadModels, float *pParameters, int parameterCount, AVFilterContext* ctx);
void veai_prepareIOBufferInput(IOBuffer* ioBuffer, AVFrame *in, FrameType frameType, int isFirst);
AVFrame* veai_prepareIOBufferOutput(AVFilterLink *outlink, IOBuffer* ioBuffer);

#endif
