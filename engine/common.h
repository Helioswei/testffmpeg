#ifndef _COMMON_H
#define _COMMON_H

#include <iostream>
#include <fstream>
#include <thread>
#include <map>
#include <vector>
#include <string>


#include <unistd.h>
#include <math.h>
#include <cstdio>
#include <iomanip>
#include <time.h>
#include <sys/stat.h>
//包含ffmpeg的库
extern "C" {
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include "libavutil/log.h" //日志
#include "libavutil/error.h"//av_err2str,错误码转换
#include "libavutil/pixfmt.h"
#include "libavutil/avutil.h"
#include "libavutil/time.h"
#include "libavutil/opt.h"
#include "libavutil/mathematics.h"
#include "libavutil/imgutils.h"
#include "libavutil/frame.h"
#include "libavutil/audio_fifo.h"
#include "libavutil/mem.h"
#include "libavutil/rational.h"
#include "libavfilter/avfilter.h"
#include "libavfilter/buffersink.h"
#include "libavfilter/buffersrc.h"
#include "libswresample/swresample.h"
}

char av_error[AV_ERROR_MAX_STRING_SIZE] = {0};
#define av_err2str(errnum) \
    av_make_error_string(av_error, AV_ERROR_MAX_STRING_SIZE, errnum) \

#endif
