/*******************************************************************************
**                                                                            **
**                     Jiedi(China nanjing)Ltd.                               **
**	               创建：夏曹俊，此代码可用作为学习参考                       **
*******************************************************************************/

/*****************************FILE INFOMATION***********************************
**
** Project       : FFmpeg
** Description   : FFMPEG项目创建示例
** Contact       : xiacaojun@qq.com
**        博客   : http://blog.csdn.net/jiedichina
**		视频课程 : http://edu.csdn.net/lecturer/lecturer_detail?lecturer_id=961
**                 http://edu.51cto.com/lecturer/index/user_id-12016059.html
**                 http://study.163.com/u/xiacaojun
**                 https://jiedi.ke.qq.com/
**   FFmpeg音视频编码实战 课程群 ：651163356
**   微信公众号  : jiedi2007
**		头条号	 : 夏曹俊
** Creation date : 2017-05-17
**
*******************************************************************************/

#pragma once
#include <string>
class AVPacket;
enum XSAMPLEFMT
{
	X_S16 = 1,
	X_FLATP = 8
};

class XVideoWriter
{
public:
	//视频输入参数
	int inWidth = 848;
	int inHeight = 480;
	int inPixFmt = 30;   //AV_PIX_FMT_BGRA

	//音频输入参数
	int inSampleRate = 44100;
	int inChannels = 2;
	XSAMPLEFMT inSampleFmt = X_S16;
	
	//视频输出参数
	int vBitrate = 4000000;
	int outWidth = 848;
	int outHeight = 480;
	int outFPS = 25;

	//音频输出参数
	int aBitrate = 64000;
	int outChannels = 2;
	int outSampleRate = 44100;
	XSAMPLEFMT outSampleFmt = X_FLATP;

	int nb_sample = 1024; //输入输出的每帧数据每通道样本数量


	virtual bool Init(const char * file) = 0;
	virtual void Close() = 0;
	virtual bool AddVideoStream() = 0;
	virtual bool AddAudioStream() = 0;
	virtual AVPacket * EncodeVideo(const unsigned char *rgb) = 0;
	virtual AVPacket * EncodeAudio(const unsigned char *pcm) = 0;
	virtual bool WriteHead() = 0;

	//会释放pkt的空间
	virtual bool WriteFrame(AVPacket *pkt) = 0;

	virtual bool WriteEnd() = 0;
	virtual bool IsVideoBefor() = 0;

	static XVideoWriter * Get(unsigned short index = 0);
	~XVideoWriter();
	std::string filename;

protected:
	XVideoWriter();
};

