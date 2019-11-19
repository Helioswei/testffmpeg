#ifndef _AVBASE_H
#define _AVBASE_H

#include <iostream>
#include <map>
#include <unistd.h>
//包含ffmpeg的库
extern "C" {
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include "libavutil/log.h" //日志
#include "libavutil/error.h"//av_err2str,错误码转换
#include "libavutil/pixfmt.h"
#include "libavutil/avutil.h"
#include "libavfilter/avfilter.h"
#include "libavfilter/buffersink.h"
#include "libavfilter/buffersrc.h"
}
using namespace std;
/*
 *常用的日志级别
 *AV_LOG_ERROR
 *AV_LOG_WARNING
 *AV_LOG_INFO
 *步骤：
 *av_log_set_level(AV_LOG_DEBUG)
 *av_log(NULL,AV_LOG_INFO<,"...%s\n",op)
 * */

namespace Media {

class AVBase {

public :
	AVBase(const string filename);
	~AVBase(){};
	void muxerMedia();
	//分离视音频流的信息出来
	void demuxerMedia();
	//获取音视频的信息
	void getInfo();
	//改变视频的封装格式，不改变编码
	void convertMuxer();
	//改变视频的封装格式，并且转编码格式
	void convertCodec();
	//测试ffmpeg的一些函数
	void testSome();
	//ffmpeg对目录的操作测试
	void operateDir();
	//剪切一段视频
	void cutVideo();
	//给视频添加水印?
	void waterMark();
	//截取关键帧
	void getkeyFrame();
public :
	string _filename;

};
}
#endif
