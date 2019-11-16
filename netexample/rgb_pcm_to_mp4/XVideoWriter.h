/*******************************************************************************
**                                                                            **
**                     Jiedi(China nanjing)Ltd.                               **
**	               �������Ĳܿ����˴��������Ϊѧϰ�ο�                       **
*******************************************************************************/

/*****************************FILE INFOMATION***********************************
**
** Project       : FFmpeg
** Description   : FFMPEG��Ŀ����ʾ��
** Contact       : xiacaojun@qq.com
**        ����   : http://blog.csdn.net/jiedichina
**		��Ƶ�γ� : http://edu.csdn.net/lecturer/lecturer_detail?lecturer_id=961
**                 http://edu.51cto.com/lecturer/index/user_id-12016059.html
**                 http://study.163.com/u/xiacaojun
**                 https://jiedi.ke.qq.com/
**   FFmpeg����Ƶ����ʵս �γ�Ⱥ ��651163356
**   ΢�Ź��ں�  : jiedi2007
**		ͷ����	 : �Ĳܿ�
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
	//��Ƶ�������
	int inWidth = 848;
	int inHeight = 480;
	int inPixFmt = 30;   //AV_PIX_FMT_BGRA

	//��Ƶ�������
	int inSampleRate = 44100;
	int inChannels = 2;
	XSAMPLEFMT inSampleFmt = X_S16;
	
	//��Ƶ�������
	int vBitrate = 4000000;
	int outWidth = 848;
	int outHeight = 480;
	int outFPS = 25;

	//��Ƶ�������
	int aBitrate = 64000;
	int outChannels = 2;
	int outSampleRate = 44100;
	XSAMPLEFMT outSampleFmt = X_FLATP;

	int nb_sample = 1024; //���������ÿ֡����ÿͨ����������


	virtual bool Init(const char * file) = 0;
	virtual void Close() = 0;
	virtual bool AddVideoStream() = 0;
	virtual bool AddAudioStream() = 0;
	virtual AVPacket * EncodeVideo(const unsigned char *rgb) = 0;
	virtual AVPacket * EncodeAudio(const unsigned char *pcm) = 0;
	virtual bool WriteHead() = 0;

	//���ͷ�pkt�Ŀռ�
	virtual bool WriteFrame(AVPacket *pkt) = 0;

	virtual bool WriteEnd() = 0;
	virtual bool IsVideoBefor() = 0;

	static XVideoWriter * Get(unsigned short index = 0);
	~XVideoWriter();
	std::string filename;

protected:
	XVideoWriter();
};

