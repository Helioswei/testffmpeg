#include "./common.h"

int h264_mp4toannexb(AVFormatContext *fmt_ctx, AVPacket *in, FILE *dst_fd){
	AVPacket *out = NULL;
	AVPacket spspps_pkt;

	int len;
	uint8_t  unit_type;
	int32_t  nal_size;
	uint32_t cumul_size = 0;
	const uint8_t *buf;
	const uint8_t *buf_end;
	int buf_size;
	int ret = 0,i;

	out = av_packet_alloc();
	buf = in ->data;
	buf_size = in -> size;
	buf_end = 








}

int main(int argc, char * argv[]){
	int ret;
	AVFormatContext *fmt_ctx = NULL;
	char *in_file = "/root/source_video/video1080p.mp4";	
	char *out_file = "test.aac";
	int video_index = -1;
	ret = avformat_open_input(&fmt_ctx, in_file, NULL, NULL);

	if (ret < 0){
		cout << "Could not open the input file" << endl;
		return -1;
	}

	ret = avformat_find_stream_info(fmt_ctx, NULL);
    if (ret < 0){
		cout << "Could not find stream " << endl;
		return -1;
	}	
	ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	if (ret < 0){
		cout << "Could not find best stream" << endl;
		return -1;
	}
	video_index = ret;

	FILE *dst_fd = fopen(out_file, "wb");
	if (!dst_fd){
		cout << "Could not open output file" <<  endl;
		return -1;
	}
	int len;
	AVPacket pkt;
	av_init_packet(&pkt);
	pkt.data = NULL;
	pkt.size = 0;
	while(av_read_frame(fmt_ctx, &pkt) >= 0){
		cout << "read successfully" << endl;
		if (pkt.stream_index == video_index){
			h264_mp4toannexb(fmt_ctx,&pkt, dst_fd);	
		}
		av_packet_unref(&pkt);
	
	}
	avformat_close_input(&fmt_ctx);
	if (dst_fd){
		fclose(dst_fd);
	}
	return 0;
}
