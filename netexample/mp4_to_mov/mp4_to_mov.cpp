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
}

#include <iostream>
using namespace std;
#pragma comment(lib,"avformat.lib")
#pragma comment(lib,"avcodec.lib")
#pragma comment(lib,"avutil.lib")

int main()
{
	char infile[] = "test.mp4";
	char outfile[] = "test.mov";
	//muxer,demuters
	av_register_all();

	//1 open input file
	AVFormatContext *ic = NULL;
	avformat_open_input(&ic, infile, 0,0);
	if (!ic)
	{
		cout << "avformat_open_input failed!" << endl;
		getchar();
	}

	///2 create output context
	AVFormatContext *oc = NULL;
	avformat_alloc_output_context2(&oc, NULL, NULL, outfile);
	if (!oc)
	{
		cerr << "avformat_alloc_output_context2 " << outfile << " failed!" << endl;
		getchar();
		return -1;
	}

	///3 add the stream
	AVStream *videoStream = avformat_new_stream(oc, NULL);
	AVStream *audioStream = avformat_new_stream(oc, NULL);

	///4 copy para
	avcodec_parameters_copy(videoStream->codecpar, ic->streams[0]->codecpar);
	avcodec_parameters_copy(audioStream->codecpar, ic->streams[1]->codecpar);

	videoStream->codecpar->codec_tag = 0;
	audioStream->codecpar->codec_tag = 0;

	av_dump_format(ic, 0, infile, 0);
	cout << "================================================" << endl;
	av_dump_format(oc, 0, outfile, 1);


	///5 open out file io,write head
	int ret = avio_open(&oc->pb, outfile, AVIO_FLAG_WRITE);
	if (ret < 0)
	{
		cerr << "avio open failed!" << endl;
		getchar();
		return -1;
	}
	ret = avformat_write_header(oc, NULL);
	if (ret < 0)
	{
		cerr << "avformat_write_header failed!" << endl;
		getchar();
	}
	AVPacket pkt;
	for (;;)
	{
		int re = av_read_frame(ic, &pkt);
		if (re < 0)
			break;
		pkt.pts = av_rescale_q_rnd(pkt.pts,
			ic->streams[pkt.stream_index]->time_base,
			oc->streams[pkt.stream_index]->time_base,
			(AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX)
			);
		pkt.dts = av_rescale_q_rnd(pkt.dts,
			ic->streams[pkt.stream_index]->time_base,
			oc->streams[pkt.stream_index]->time_base,
			(AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX)
			);
		pkt.pos = -1;
		pkt.duration = av_rescale_q_rnd(pkt.duration,
			ic->streams[pkt.stream_index]->time_base,
			oc->streams[pkt.stream_index]->time_base,
			(AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX)
			);

		av_write_frame(oc, &pkt);
		av_packet_unref(&pkt);
		cout << ".";
	}

	av_write_trailer(oc);
	avio_close(oc->pb);
	cout << "================end================" << endl;




	getchar();
	return 0;
}