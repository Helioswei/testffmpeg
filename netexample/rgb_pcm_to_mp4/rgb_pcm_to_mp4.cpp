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

#include "XVideoWriter.h"
#include <iostream>
using namespace std;
int main()
{
	char outfile[] = "rgbpcm.mp4";
	char rgbfile[] = "test.rgb";
	char pcmfile[] = "test.pcm";
	XVideoWriter *xw = XVideoWriter::Get(0);


	cout<<xw->Init(outfile);
	cout<<xw->AddVideoStream();
	xw->AddAudioStream();

	FILE *fp = fopen(rgbfile, "rb");
	if (!fp)
	{
		cout << "open " << rgbfile <<" failed!"<< endl;
		getchar();
		return -1;
	}
	FILE *fa = fopen(pcmfile, "rb");
	if (!fa)
	{
		cout << "open " << pcmfile << " failed!" << endl;
		getchar();
		return -1;
	}

	int size = xw->inWidth*xw->inHeight * 4;
	unsigned char *rgb = new unsigned char[size];

	int asize = xw->nb_sample*xw->inChannels * 2;
	unsigned char *pcm = new unsigned char[asize];

	xw->WriteHead();
	AVPacket *pkt = NULL;
	int len = 0;
	for (;;)
	{
		if (xw->IsVideoBefor())
		{
			len = fread(rgb, 1, size, fp);
			if (len <= 0)
				break;
			pkt = xw->EncodeVideo(rgb);
			if (pkt) cout << ".";
			else
			{
				cout << "-";
				continue; 
			}

			if (xw->WriteFrame(pkt))
			{
				cout << "+";
			}
		}
		else
		{
			len = fread(pcm, 1, asize, fa);
			if (len <= 0)break;
			pkt = xw->EncodeAudio(pcm);
			xw->WriteFrame(pkt);
		}

	}
	xw->WriteEnd();
	delete rgb;
	rgb = NULL;
	cout << "\n============================end============================" << endl;
	//rgb转yuv

	//编码视频帧

	getchar();
	return 0;
}