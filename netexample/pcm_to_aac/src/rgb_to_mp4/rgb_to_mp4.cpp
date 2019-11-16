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
extern "C"
{
	#include <libavformat/avformat.h>
	#include <libswscale/swscale.h>
}

#include <iostream>
using namespace std;
#pragma comment(lib,"avformat.lib")
#pragma comment(lib,"avcodec.lib")
#pragma comment(lib,"avutil.lib")
#pragma comment(lib,"swscale.lib")

int main()
{
	char infile[] = "out.rgb";
	char outfile[] = "rgb.mp4";
	//muxer,demuters
	av_register_all();
	avcodec_register_all();


	FILE *fp = fopen(infile, "rb");
	if (!fp)
	{
		cout << infile << " open failed!" << endl;
		getchar();
		return -1;
	}

	int width = 848;
	int height = 480;
	int fps = 25;

	//1 create codec
	AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
	if (!codec)
	{
		cout << " avcodec_find_encoder AV_CODEC_ID_H264 failed!" << endl;
		getchar();
		return -1;
	}
	AVCodecContext *c = avcodec_alloc_context3(codec);
	if (!c)
	{
		cout << " avcodec_alloc_context3  failed!" << endl;
		getchar();
		return -1;
	}
	//ѹ��������
	c->bit_rate = 400000000;

	c->width = width;
	c->height = height;
	c->time_base = { 1, fps };
	c->framerate = { fps, 1 };
	
	//�������С���ؼ�֡
	c->gop_size = 50;


	c->max_b_frames = 0;

	c->pix_fmt = AV_PIX_FMT_YUV420P;
	c->codec_id = AV_CODEC_ID_H264;
	c->thread_count = 8;
	
	//ȫ�ֵı�����Ϣ
	c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	int ret = avcodec_open2(c, codec, NULL);
	if (ret < 0)
	{
		cout << " avcodec_open2  failed!" << endl;
		getchar();
		return -1;
	}
	cout << "avcodec_open2 success!" << endl;

	//2 create out context
	AVFormatContext *oc = NULL;
	avformat_alloc_output_context2(&oc, 0, 0, outfile);

	//3 add video stream
	AVStream *st = avformat_new_stream(oc, NULL);
	//st->codec = c;
	st->id = 0;
	st->codecpar->codec_tag = 0;
	avcodec_parameters_from_context(st->codecpar, c);

	cout << "===============================================" << endl;
	av_dump_format(oc, 0, outfile, 1);
	cout << "===============================================" << endl;

	//4 rgb to yuv
	SwsContext *ctx = NULL;
	ctx = sws_getCachedContext(ctx,
		width, height, AV_PIX_FMT_BGRA,
		width, height, AV_PIX_FMT_YUV420P,
		SWS_BICUBIC,
		NULL, NULL, NULL
		);
	//����ռ�
	unsigned char *rgb = new unsigned char[width*height * 4];

	//����Ŀռ�
	AVFrame *yuv = av_frame_alloc();
	yuv->format = AV_PIX_FMT_YUV420P;
	yuv->width = width;
	yuv->height = height;
	ret = av_frame_get_buffer(yuv, 32);

	if (ret < 0)
	{
		cout << " av_frame_get_buffer  failed!" << endl;
		getchar();
		return -1;
	}


	//5 wirte mp4 head
	ret = avio_open(&oc->pb, outfile, AVIO_FLAG_WRITE);
	if (ret < 0)
	{
		cout << " avio_open  failed!" << endl;
		getchar();
		return -1;
	}
	ret = avformat_write_header(oc, NULL);
	if (ret < 0)
	{
		cout << " avformat_write_header  failed!" << endl;
		getchar();
		return -1;
	}
	int p = 0;
	for (;;)
	{
		int len = fread(rgb, 1, width*height * 4, fp);
		if (len <= 0)
		{
			break;
		}
		uint8_t *indata[AV_NUM_DATA_POINTERS] = { 0 };
		indata[0] = rgb;
		int inlinesize[AV_NUM_DATA_POINTERS] = { 0 };
		inlinesize[0] = width * 4;

		int h = sws_scale(ctx, indata, inlinesize, 0, height,
			yuv->data, yuv->linesize
			);
		if (h <= 0)
			break;

		//6 encode frame
		yuv->pts = p;
		//yuv->pict_type = AV_PICTURE_TYPE_I;
		p = p + 3600;
		ret = avcodec_send_frame(c, yuv);
		if (ret != 0)
		{
			continue;
		}
		AVPacket pkt;
		av_init_packet(&pkt);
		ret = avcodec_receive_packet(c, &pkt);
		if (ret != 0)
			continue;

		//av_write_frame(oc, &pkt);
		//av_packet_unref(&pkt);
		av_interleaved_write_frame(oc, &pkt);

		cout << "<"<<pkt.size<<">";
	}
	
	//д����Ƶ����
	av_write_trailer(oc);

	//�ر���Ƶ���io
	avio_close(oc->pb);

	//�����װ���������
	avformat_free_context(oc);

	//�رձ�����
	avcodec_close(c);

	//���������������
	avcodec_free_context(&c);

	//������Ƶ�ز���������
	sws_freeContext(ctx);


	cout << "======================end=========================" << endl;





























	delete rgb;
	getchar();
	return 0;
}