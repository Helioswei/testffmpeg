#include "./common.h"

int main(int argc, char *argv[]){
	char in_file[] = "/root/source_video/video/test.rgb";
	char out_file[] = "rgb.mp4";
	int ret;
	av_register_all();
	avcodec_register_all();

	//二进制打开文件
	FILE *fp = fopen(in_file, "rb");
	if (!fp){
		cout << in_file << "open failed" << endl;
		return -1;
	}
	int width = 426;
	int height = 240;
	int fps = 25;
	//1,create codec
	AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
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
	c -> bit_rate = 400000;

	c -> width = width;
	c -> height = height;
	c -> time_base = {1, fps};
	c -> framerate = {fps, 1};

	//画面组大小，关键帧
	c -> gop_size = 50;

	c -> max_b_frames = 0;
	c -> pix_fmt = AV_PIX_FMT_YUV420P;
	c -> codec_id = AV_CODEC_ID_H264;
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
	avformat_alloc_output_context2(&oc, 0, 0 , out_file);
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
	st -> id = 0;
	st -> codecpar -> codec_tag = 0;
	ret = avcodec_parameters_from_context(st -> codecpar, c);
	if (ret < 0){
		cout << "avcodec_parameters_from_context failed" << endl;
		return -1;
	}

	cout << "====================================" << endl;
	av_dump_format(oc, 0, out_file, 1);
	cout << "====================================" << endl;

	//4,rgb to yuv，创建的上下文
	SwsContext *ctx = NULL;
	ctx = sws_getCachedContext(ctx,
			width, height, AV_PIX_FMT_BGRA,
			width, height, AV_PIX_FMT_YUV420P,
			SWS_BICUBIC,
			NULL, NULL, NULL);

	//输入空间
	unsigned char *rgb =  new unsigned char[width*height *4];
	//输出空间
	AVFrame *yuv = av_frame_alloc();//用来存放非编码的数据，创建对象的空间，但是对于其本身的空间需要分配
	yuv -> format = AV_PIX_FMT_YUV420P;
	yuv -> width = width;
	yuv -> height = height;
	ret = av_frame_get_buffer(yuv, 0);//申请的空间，放在yuv->data下
	if (ret < 0){
		cout << "av_frame_get_buffer failed" << endl;
	    return -1;	
	}
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
	int p = 0;
	while(true){
		//每读一帧画面
		int len = fread(rgb, 1, width*height*4,fp);
		if (len <= 0) 
			break;
		//读取的rgba的非编码数据，先将其转化为yuv420p的
		uint8_t *indata[AV_NUM_DATA_POINTERS] = {0};
		indata[0] = rgb;
		int inlinesize[AV_NUM_DATA_POINTERS] = {0};
		inlinesize[0] = width * 4;
		
		int h = sws_scale(ctx, indata, inlinesize, 0, height,yuv -> data,yuv -> linesize);
		if (h <= 0)
			break;

		//6,encode frame，多线程编码
		yuv -> pts = p;
		p = p + 3600;//根据timebase算出,可能丢帧，编码前需要设置pts.90000/25=3600
		ret  = avcodec_send_frame(c, yuv);
		if (ret != 0){
			continue;
		
		}
		AVPacket pkt;
		av_init_packet(&pkt);
		ret = avcodec_receive_packet(c, &pkt);
		if (ret != 0)
			continue;

		//cout << "pkt.size" << pkt.size << endl;
		//7,写入文件中
		//av_write_frame(oc, pkt);
		//av_packet_unref(pkt);
		//自动处理pkt，并且按照dts排序
		av_interleaved_write_frame(oc, &pkt);
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
	//清理视频重采样上下文
	sws_freeContext(ctx);
	cout << "=======================end=========================" << endl;



	delete rgb;
	return 0;
}












