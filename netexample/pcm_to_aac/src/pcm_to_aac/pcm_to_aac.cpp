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

extern "C"
{
	#include <libavformat/avformat.h>
	#include <libswscale/swscale.h>
	#include <libswresample/swresample.h>
}

#include <iostream>
using namespace std;
#pragma comment(lib,"avformat.lib")
#pragma comment(lib,"avcodec.lib")
#pragma comment(lib,"avutil.lib")
#pragma comment(lib,"swscale.lib")
#pragma comment(lib,"swresample.lib")
int main()
{
	char infile[] = "16.wav";
	char outfile[] = "out.aac";
	//muxer,demuters
	av_register_all();
	avcodec_register_all();

	//1 打开音频编码器
	AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
	if (!codec)
	{
		cout << "avcodec_find_encoder error" << endl;
		getchar();
		return -1;
	}
	AVCodecContext *c = avcodec_alloc_context3(codec);
	if (!c)
	{
		cout << "avcodec_alloc_context3 error" << endl;
		getchar();
		return -1;
	}
	c->bit_rate = 64000;
	c->sample_rate = 44100;
	c->sample_fmt = AV_SAMPLE_FMT_FLTP;
	c->channel_layout = AV_CH_LAYOUT_STEREO;
	c->channels = 2;
	c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	int ret = avcodec_open2(c, codec, NULL);
	if (ret < 0)
	{
		cout << "avcodec_open2 error" << endl;
		getchar();
		return -1;
	}
	cout << "avcodec_open2 success!" << endl;

	//2 打开输出封装的上下文
	AVFormatContext *oc = NULL;
	avformat_alloc_output_context2(&oc, NULL,NULL,outfile);
	if (!oc)
	{
		cout << "avformat_alloc_output_context2 error" << endl;
		getchar();
		return -1;
	}

	AVStream *st = avformat_new_stream(oc, NULL);
	st->codecpar->codec_tag = 0;
	avcodec_parameters_from_context(st->codecpar, c);

	av_dump_format(oc, 0, outfile, 1);

	//3  open io ,write head
	ret = avio_open(&oc->pb, outfile, AVIO_FLAG_WRITE);
	if (ret < 0)
	{
		cout << "avio_open error" << endl;
		getchar();
		return -1;
	}
	ret = avformat_write_header(oc, NULL);



	//44100 16 2
	//4 创建音频重采样上下文
	SwrContext *actx = NULL;
	actx = swr_alloc_set_opts(actx,
		c->channel_layout, c->sample_fmt, c->sample_rate,	//输出格式
		AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S16,44100,		//输入格式
		0, 0);
	if (!actx)
	{
		cout << "swr_alloc_set_opts error" << endl;
		getchar();
		return -1;
	}
	ret = swr_init(actx);
	if (ret < 0)
	{
		cout << "swr_init error" << endl;
		getchar();
		return -1;
	}


	//5 打开输入音频文件，进行重采样
	AVFrame *frame = av_frame_alloc();
	frame->format = AV_SAMPLE_FMT_FLTP;
	frame->channels = 2;
	frame->channel_layout = AV_CH_LAYOUT_STEREO;
	frame->nb_samples = 1024;//一帧音频存放的样本数量
	ret = av_frame_get_buffer(frame, 0);
	if (ret < 0)
	{
		cout << "av_frame_get_buffer error" << endl;
		getchar();
		return -1;
	}

	int readSize = frame->nb_samples * 2 * 2;
	
	
	
	
	
	
	
	
	char *pcm = new char[readSize];

	FILE *fp = fopen(infile, "rb");

	for (;;)
	{
		int len = fread(pcm, 1, readSize, fp);
		if (len <= 0)break;
		const uint8_t *data[1];
		data[0] = (uint8_t*)pcm;

		len = swr_convert(actx, frame->data, frame->nb_samples,
			data, frame->nb_samples
			);
		if (len <= 0)
			break;
		

		AVPacket pkt;
		av_init_packet(&pkt);

		//6 音频编码
		ret = avcodec_send_frame(c, frame);
		if (ret != 0) continue;
		ret = avcodec_receive_packet(c, &pkt);
		if (ret != 0) continue;


		//7 音频封装如aac文件
		pkt.stream_index = 0;
		pkt.pts = 0;
		pkt.dts = 0;
		ret = av_interleaved_write_frame(oc, &pkt);


		cout << "[" << len << "]";



	
	
	}
	delete pcm;
	pcm = NULL;
		






	//写入视频索引
	av_write_trailer(oc);

	//关闭视频输出io
	avio_close(oc->pb);

	//清理封装输出上下文
	avformat_free_context(oc);

	//关闭编码器
	avcodec_close(c);

	//清理编码器上下文
	avcodec_free_context(&c);






















	cout << "======================end=========================" << endl;
	getchar();
	return 0;
}