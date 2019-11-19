#include "./common.h"

int main(int argc, char *argv[]){
	char in_file[] = "/root/source_video/video/test.pcm";
	char out_file[] = "pcm.aac";
	int ret;
	av_register_all();
	avcodec_register_all();

	//二进制打开文件
	FILE *fp = fopen(in_file, "rb");
	if (!fp){
		cout << in_file << "open failed" << endl;
		return -1;
	}
	//1,create codec
	AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
	if (!codec){
		cout << in_file << "avcodec_find_encoder  AV_CODEC_ID_H264 failed" << endl;
		return -1;
	}
	//2,create codec context
	AVCodecContext *c = avcodec_alloc_context3(codec);
	if (!c){
		cout << "avcodec_alloc_context3 failed" << endl;
		return -1;
	}
	//压缩比特率
	c -> bit_rate = 64000;
	c -> sample_rate = 44100;
	c -> sample_fmt = AV_SAMPLE_FMT_FLTP;//在mp4的封装格式中，音频aac必须使用这个采样格式
	c -> channel_layout = AV_CH_LAYOUT_STEREO;
	c -> channels = 2;

	//全局的编码信息
	c -> flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	//c -> thread_count = 1;//线程
	ret = avcodec_open2(c, codec, NULL);

	if (ret < 0){
		cout << "avcodec_open2 failed" << endl;
		return -1;
	}

	cout << "avcodec_open2 success" << endl;

	//2,create out context
	AVFormatContext *oc = NULL;
	avformat_alloc_output_context2(&oc, NULL, NULL , out_file);
	if (!oc){
		cout << "avformat_alloc_output_context2  failed" << endl;
		return -1;
	}

	//3,add video stream
	AVStream *st = avformat_new_stream(oc, NULL);
	if (!st){
		cout << "avformat_new_stream failed" << endl;
		return -1;
	}
	//st -> codec = c;
	//st -> id = 0;
	//编码信息在上面设置，这里设为o
	st -> codecpar -> codec_tag = 0;
	ret = avcodec_parameters_from_context(st -> codecpar, c);
	if (ret < 0){
		cout << "avcodec_parameters_from_context failed" << endl;
		return -1;
	}

	cout << "====================================" << endl;
	av_dump_format(oc, 0, out_file, 1);
	cout << "====================================" << endl;

	//5,wirte mp4 head
	ret = avio_open(&oc -> pb, out_file, AVIO_FLAG_WRITE);
	if (ret < 0){
		cout << "avio_open failed " << endl;
		return -1;
	}
	ret = avformat_write_header(oc, NULL);
	if (ret < 0){
		cout << "avformat_write_header failed" << endl;
		return -1;
	}


	//6,创建音频重采样上下文
	SwrContext *actx = NULL;
	actx = swr_alloc_set_opts(actx, 
			c -> channel_layout, c -> sample_fmt, c -> sample_rate,//输出格式
			AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_FLTP,44100,//输入格式
			0,0
			);

	if (!actx){
		cout << "swr_alloc_set_opts error" << endl;
		return -1;
	}

	ret = swr_init(actx);

	if (ret < 0){
		cout << "swr_init error" << endl;
		return -1;
	}

	//5,打开输入音频文件，进行重采样
	AVFrame *frame = av_frame_alloc();//未压缩的帧数据
	frame -> format = AV_SAMPLE_FMT_FLTP;
	frame -> channels = 2;
	frame -> channel_layout = AV_CH_LAYOUT_STEREO;
	frame -> nb_samples = 1024;//一帧音频存放的样本数量,帧率21.5
	ret = av_frame_get_buffer(frame,0);//创建音频的空间
	if (ret < 0){
		cout << "av_frame_get_buffer error" << endl;
		return -1;
	}
	int readSize = frame -> nb_samples*2;
	char *pcm = new char[readSize];
	while(true){
		int len = fread(pcm, 1, readSize, fp);
		if (len <= 0)
			break;
		//进行重新采样
		const uint8_t *data[1];
		data[0] = (uint8_t*)pcm;
		len = swr_convert(actx,
			   	frame -> data, frame -> nb_samples,//输出
				data, frame -> nb_samples//输入		
				);
		if (len <= 0)
			break;

		AVPacket pkt;
		av_init_packet(&pkt);
		//音频编码,对已经进行重新采样的音频进行编码
		ret = avcodec_send_frame(c, frame);
		if (ret != 0)
			continue;
		ret = avcodec_receive_packet(c, &pkt);
		if (ret != 0)
			continue;

		//音频封装入AAC文件
		pkt.stream_index = 0;
		//音频不存在不同步，所以可以设置为0，让其内部自动进行pts，dts的转换
		pkt.pts = 0;
		pkt.dts = 0;
		ret = av_interleaved_write_frame(oc, &pkt);
		cout << ".";
	}

	
	//写入视频的索引
	av_write_trailer(oc);
	//关闭视频的输出io
	avio_close(oc -> pb);

	//清理封装输出的上下文
	avformat_free_context(oc);

	//关闭编码器
	avcodec_close(c);
	//清理编码器上下文
	avcodec_free_context(&c);
	cout << "\n=======================end=========================" << endl;



	delete pcm;
	pcm = NULL;
	return 0;
}












