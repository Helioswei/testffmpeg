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
	//rgbתyuv

	//������Ƶ֡

	getchar();
	return 0;
}