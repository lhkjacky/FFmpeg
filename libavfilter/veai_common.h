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

int ff_veai_checkDevice(int deviceIndex, AVFilterContext* ctx);
int ff_veai_checkScale(int scale, AVFilterContext* ctx);
int ff_veai_checkModel(char* modelName, ModelType modelType, AVFilterContext* ctx);
void ff_veai_handleLogging(void);
void* ff_veai_verifyAndCreate(AVFilterLink *inlink, AVFilterLink *outlink, char *processorName, char* modelName, ModelType modelType,
                            int deviceIndex, int extraThreads, int scale, int canDownloadModels, float *pParameters, int parameterCount, AVFilterContext* ctx);
void ff_veai_prepareIOBufferInput(IOBuffer* ioBuffer, AVFrame *in, FrameType frameType, int isFirst);
AVFrame* ff_veai_prepareIOBufferOutput(AVFilterLink *outlink, IOBuffer* ioBuffer);

#endif
