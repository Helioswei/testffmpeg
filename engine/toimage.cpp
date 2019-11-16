#include "./common.h"




int saveJpg(AVFrame *pFrame, char *out_name){
	int width = pFrame -> width;
	int height = pFrame -> height;
	AVCodecContext *pCodecCtx = NULL;

	AVFormatContext *pFormatCtx = avformat_alloc_context();

	//设置输出文件的格式
	pFormatCtx -> oformat = av_guess_format("mjpeg", NULL, NULL);

	//创建并初始化输出AVIOContext
	int res = avio_open(&pFormatCtx->pb, out_name, AVIO_FLAG_READ_WRITE);
	if (res < 0 ){
		cout << "Couldn't open output file ,the ret is " << res  << endl;
		return -1;
	
	}
	//构建一个新的stream
	AVStream  *pAVStream = avformat_new_stream(pFormatCtx, 0);
	if (!pAVStream)
		return -1;

	AVCodecParameters *parameters = pAVStream -> codecpar;
	parameters -> codec_id = pFormatCtx -> oformat -> video_codec;
	parameters -> codec_type = AVMEDIA_TYPE_VIDEO;
	parameters -> format = AV_PIX_FMT_YUVJ420P;
	parameters -> width = pFrame -> width;
	parameters -> height = pFrame -> height;
	//parameters -> width = 640;
	//parameters -> height = 320;
	
	AVCodec *pCodec = avcodec_find_encoder(pAVStream -> codecpar -> codec_id);	

	if (!pCodec){
		cout << "Could not find encoder" << endl;
		return -1;
	}

	pCodecCtx = avcodec_alloc_context3(pCodec);
	if (!pCodecCtx){
		cout << "Could not allocate video codec context" << endl;
		exit(1);
	}

	if ((avcodec_parameters_to_context(pCodecCtx, pAVStream -> codecpar)) < 0){
		cout << "Failed to copy codec  parameters to decoder context" << endl;
		return -1;
	}

	pCodecCtx -> time_base = (AVRational){1, 25};

	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0){
		cout << "Could not open codec" << endl;
		return -1;
	}

	int  ret = avformat_write_header(pFormatCtx, NULL);

	if (ret < 0){
		cout << "Write header failed"  << endl;
		return -1;
	}

	int y_size = width * height;

	//Encode
	//给AVPacket分配足够大的空间
	AVPacket  pkt;
	av_new_packet(&pkt, y_size *3);

	//编码数据
	ret = avcodec_send_frame(pCodecCtx, pFrame);
	if (ret < 0){
		cout << "Could not avcodec_send_frame" << endl;
		return -1;
	}


	//得到编码后的数据
	ret = avcodec_receive_packet(pCodecCtx, &pkt);

	if (ret < 0){
		cout << "Could not avcodec_receive packet" << endl;
		return -1;
	}

	ret = av_write_frame(pFormatCtx, &pkt);

	if (ret < 0){
		cout << "Could not av_write frame" << endl;
		return -1;
	}
	av_packet_unref(&pkt);

	//write trailer
	av_write_trailer(pFormatCtx);

	avcodec_close(pCodecCtx);
	avio_close(pFormatCtx -> pb);
	avformat_free_context(pFormatCtx);
	return 0;
}

int main(int argc, char *argv[]){
	int ret;
	const char *in_filename = "/root/source_video/codetest.avi", *out_filename = "/root/demo";

	AVFormatContext *fmt_ctx = NULL;

	const AVCodec *codec;
	AVCodecContext *codeCtx = NULL;

	AVStream *stream = NULL;
	
	int stream_index;

	AVPacket avpkt;

	int frame_count;

	AVFrame *frame;

	ret = avformat_open_input(&fmt_ctx, in_filename, NULL, NULL);
	if (ret < 0){
		cout << "Could not optn source file "<< endl;
		exit(1);
	}

	ret = avformat_find_stream_info(fmt_ctx, NULL);
	if (ret < 0){
		cout << "Could not find stream information" << endl;
		exit(1);
	}

	av_dump_format(fmt_ctx, 0, in_filename, 0);

	av_init_packet(&avpkt);
	avpkt.data = NULL;
	avpkt.size = 0;

	stream_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);

	stream = fmt_ctx -> streams[stream_index];

	//3
	codec = avcodec_find_decoder(stream -> codecpar -> codec_id);

	if (!codec){
		return -1;
	}

	codeCtx = avcodec_alloc_context3(NULL);
	if (!codeCtx){
		cout << "Could not allocate video codec context\n" << endl;
		exit(1);
	
	}


	ret = avcodec_parameters_to_context(codeCtx, stream -> codecpar);
	if (ret < 0){
		return ret;
	}

	avcodec_open2(codeCtx, codec, NULL);

	//初始化frame，解码后的数据
	frame = av_frame_alloc();

	if (!frame){
		cout << "Could not allocate video frame" << endl;
		exit(1);
	}

	frame_count = 0;
	char buf[1024];

	while (av_read_frame(fmt_ctx, &avpkt) >= 0){
		if (avpkt.stream_index == stream_index){
			int re = avcodec_send_packet(codeCtx, &avpkt);

			if (re < 0){
				continue;
			}
		
			//这里必须使用while，因为一次avcodec_receive_frame 可能服务接收到所有数据
			while(avcodec_receive_frame(codeCtx, frame) == 0){
				snprintf(buf,sizeof(buf), "%s/Demo-%d.jpg",out_filename,frame_count);
				cout << "解码成功：" << buf << endl;
				saveJpg(frame, buf);//保存为jpg图片
			
			}
			frame_count ++;
		}
	
		av_packet_unref(&avpkt);	
	}
}
