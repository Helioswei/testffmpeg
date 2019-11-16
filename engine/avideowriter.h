#include "./common.h"


enum XSAMPLEFMT
{
	X_S16 = 1,
	X_FLATP = 8

};
class XVideoWriter
{
	//工厂类
public:
	virtual bool Init(const char *file) = 0;//纯虚函数
	virtual void Close() = 0;//清理

	virtual bool AddVideoStream() = 0;
	virtual bool AddAudioStream() = 0;
	virtual AVPacket *EncodeVideo(const unsigned char *rgb) = 0;
	virtual AVPacket *EncodeAudio(const unsigned char *pcm) = 0;
	virtual bool WriteHead() = 0;
	//会释放packet的空间
	virtual bool WriteFrame(AVPacket *pkt) = 0;
	virtual bool WriteEnd() = 0;
	
	virtual bool IsVideoBefor() = 0;
	static XVideoWriter *Get(unsigned short  index = 0);
	~XVideoWriter();

	//视频输入参数
	int inWidth = 426;
	int inHeight = 240;
	int inPixFmt = AV_PIX_FMT_BGRA;// AV_PIX_FMT_BGRA,输入的视频帧格式
	string filename;
	//视频输出参数
	int vBitrate = 400000;
	int outWidth =720 ;
	int outHeight = 576;
	int outFPS = 25;
	//音频输出参数
	int aBitrate = 64000;
	int outChannels = 2;
	int outSampleRate = 44100;
	XSAMPLEFMT  outSampleFmt = X_FLATP;
	int nb_sample = 1024;
	//音频输入参数
	int inSampleRate = 44100;
	int inChannels = 2;
	XSAMPLEFMT  inSampleFmt = X_S16;

protected:
	XVideoWriter();


};

