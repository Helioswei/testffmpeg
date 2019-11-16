#include "./common.h"


int64_t mypts = 0;
int audioIndex = -1;
FILE *outfile = fopen("/root/test.pcm","wb");


static void init_packet(AVPacket *packet){
	av_init_packet(packet);
	packet -> data = NULL;
	packet -> size = 0;
}

static void init_log(){
	av_log_set_level(AV_LOG_DEBUG);
}

static int init_output_frame(AVFrame **frame, AVCodecContext *enc_ctx, int frame_size)
{
	int error;
	if(!(*frame = av_frame_alloc())){
		av_log(NULL, AV_LOG_ERROR, "Could not allocate output frame\n");
		return AVERROR_EXIT;
	}
	(*frame) -> format = enc_ctx -> sample_fmt;
	(*frame) -> channels = enc_ctx -> channels;
	//(*frame) -> channel_layout = enc_ctx -> channel_layout;
	(*frame) -> nb_samples = frame_size;
	//(*frame) -> sample_rate = enc_ctx -> sample_rate;

	error = av_frame_get_buffer(*frame, 0);
	if (error < 0){
		av_log(NULL, AV_LOG_ERROR,"Could not allocate output frame samples");
		av_frame_free(frame);
		return error;
	}
	return 0;
}





int main(int argc, char* argv[]){
	init_log();
	
	AVFormatContext *ifmt_ctx = NULL;
	AVFormatContext *ofmt_ctx = NULL;
	AVOutputFormat *ofmt = NULL; 	
	SwrContext *actx = NULL;
	AVFrame *areframe;
	AVCodec *dec;
	AVCodecContext *dec_ctx;
	int ret;
	//char * filename = "/root/source_video/video/flv.flv";
	char * filename = "/root/source_video/codetest.avi";
	char * outfile = "/root/transcode.mp4";
	//1，打开输入的文件
	ret = avformat_open_input(&ifmt_ctx, filename, NULL, NULL);
	//2,查找输入文件中的流信息
	ret = avformat_find_stream_info(ifmt_ctx, NULL);
	if (ret < 0){
		av_log(NULL, AV_LOG_ERROR, "Cannot fined the stream information \n");
	}
	//3,初始化音频解码器的相关内容
	ret = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, &dec, NULL);
	if (ret < 0){
		av_log(NULL, AV_LOG_ERROR,"Failed to find the audio stream");
	}
	audioIndex = ret;
	dec_ctx = ifmt_ctx -> streams[audioIndex] -> codec;
	//初始化解码器
	ret = avcodec_open2(dec_ctx, dec, NULL);
	if (ret < 0){
		av_log(NULL, AV_LOG_ERROR, "Failed to open video decoder\n");
	}

	//打开输出文件
	ret = avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, outfile);	
	if (ret < 0){
		av_log(NULL, AV_LOG_ERROR, "Failed to init the output file \n");
	}	
	//设置音频的流信息
	AVStream *astream = avformat_new_stream(ofmt_ctx, NULL);
	if (!astream){
		av_log(NULL, AV_LOG_ERROR, "Failed to allocating output stram\n");
	}
	AVCodec *enc = avcodec_find_encoder(AV_CODEC_ID_AAC);	
	if (!enc){
		av_log(NULL, AV_LOG_ERROR, "Necessary encoder not found\n");
	}	
	AVCodecContext *enc_ctx = avcodec_alloc_context3(enc);	
	if (!enc_ctx){
		av_log(NULL, AV_LOG_ERROR, "Failed o allocate the encoder context\n");
	}
	enc_ctx -> bit_rate = 64000;
	enc_ctx -> sample_rate = 44100;
	enc_ctx -> channel_layout = dec_ctx -> channel_layout;
	enc_ctx -> channels = av_get_channel_layout_nb_channels(enc_ctx -> channel_layout); 
	enc_ctx -> sample_fmt = enc -> sample_fmts[0];
	AVRational time_base = {1, enc_ctx -> sample_rate};
	enc_ctx -> time_base = time_base;
	enc_ctx -> frame_size = 1024;
	//初始化重采样
	actx = swr_alloc_set_opts(
			actx, enc_ctx -> channel_layout, enc_ctx -> sample_fmt,
			enc_ctx -> sample_rate,
			dec_ctx -> channel_layout,
			dec_ctx -> sample_fmt,
			dec_ctx -> sample_rate,
			0,0
			);
	if (!actx){
		av_log(NULL, AV_LOG_ERROR, "swr_alloc_set_opts failed\n");		  
		return -1;
	}
	ret = swr_init(actx);
	if (ret < 0){
		av_log(NULL, AV_LOG_ERROR, "swr_init failed\n");	
		return ret;
	}
	//初始化重采样后存储的AVFrame
//	areframe = av_frame_alloc();
//	areframe->format = enc_ctx->sample_fmt;
//	areframe->channels = enc_ctx->channels;
//	areframe->channel_layout = enc_ctx->channel_layout;
//	areframe->nb_samples = enc_ctx -> frame_size;
//	ret = av_frame_get_buffer(areframe, 0);
//	if (ret < 0) {
//		 av_log(NULL, AV_LOG_ERROR, "av_frame_get_buffer failed\n");
//		 return -1;
//	}
	if (ofmt_ctx -> oformat -> flags & AVFMT_GLOBALHEADER)
		enc_ctx -> flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	ret = avcodec_open2(enc_ctx, enc, NULL);
	if (ret < 0){
		av_log(NULL, AV_LOG_ERROR, "Cannot open video encoder for stream\n");
		return ret;
	}
	astream -> codecpar -> codec_tag = 0;
	ret = avcodec_parameters_from_context(astream -> codecpar, enc_ctx);
	if (ret < 0){
		av_log(NULL, AV_LOG_ERROR, "Failed to copy encoder parameters to output streams\n");
		return ret;
	}
	astream -> time_base = enc_ctx -> time_base;
	if (!(ofmt_ctx -> oformat -> flags & AVFMT_NOFILE)){
		ret = avio_open(&ofmt_ctx -> pb, outfile, AVIO_FLAG_WRITE);
		if (ret < 0){
			av_log(NULL, AV_LOG_ERROR, "Error occurred when opening output file\n");
			return ret;
		}
	}	

	ret = avformat_write_header(ofmt_ctx, NULL);
	if (ret < 0){
		av_log(NULL, AV_LOG_ERROR, "Error when write header\n");
		return ret;
	}

	AVPacket packet;
	packet.data = NULL;
	packet.size = 0;
	av_init_packet(&packet);
	//初始化一个先进先出的缓存队列
	while(true){
		ret = av_read_frame(ifmt_ctx, &packet);
		if(ret < 0){
			av_log(NULL, AV_LOG_ERROR, "Cannot not read frame\n");
			break;
		}
		if (packet.stream_index == audioIndex){
			//解码
			AVFrame *frame;
			frame = av_frame_alloc();
			if (!frame){
				ret = AVERROR(ENOMEM);
				return ret;
			}
			ret = avcodec_send_packet(dec_ctx, &packet);
			if (ret < 0){
				av_frame_free(&frame);
				av_log(NULL, AV_LOG_ERROR, "Decodeing audio failed\n");
				break;
			}
			ret = avcodec_receive_frame(dec_ctx,frame);
			if (ret < 0){
				av_frame_free(&frame);
				if (ret == AVERROR(EAGAIN)){
					av_log(NULL, AV_LOG_ERROR, "return AVERROR(EAGAIN)\n");
					continue;

				} else if (ret == AVERROR_EOF){
					av_log(NULL, AV_LOG_ERROR, "return AVERROR_EOF\n");
					continue;
				}else{
					av_log(NULL, AV_LOG_ERROR, "Cannot read frame\n");
					break;
				}
			}
			//重采样
			AVFrame *areframe;
			if (init_output_frame(&areframe, enc_ctx, 1024 ) < 0)
				break;
			int len = swr_convert(actx,areframe -> data, areframe -> nb_samples,(
						const uint8_t **)frame -> data,frame -> nb_samples);
			if (len <= 0){
				av_log(NULL, AV_LOG_ERROR, "resample is Failed\n");
				av_frame_free(&frame);
				break;
			}
			areframe -> pts = mypts;
			mypts += areframe -> nb_samples;
			if (areframe -> pts == AV_NOPTS_VALUE)
				areframe -> pts =0;
			av_frame_free(&frame);
			//编码
			AVPacket tmppkt;
			tmppkt.data = NULL;
			tmppkt.size = 0;
			av_init_packet(&tmppkt);
			ret = avcodec_send_frame(enc_ctx, areframe);
			if (ret < 0){
				av_frame_free(&areframe);
				av_packet_unref(&tmppkt);
				av_log(NULL, AV_LOG_ERROR, "Error sending the frame to encoder\n");
				break;
			}
			ret = avcodec_receive_packet(enc_ctx, &tmppkt);
			if (ret < 0){
				av_frame_free(&areframe);
				av_packet_unref(&tmppkt);
				if (ret == AVERROR(EAGAIN)){
					av_log(NULL, AV_LOG_ERROR, "return AVERROR(EAGAIN)\n");
					continue;

				} else if (ret == AVERROR_EOF){
					av_log(NULL, AV_LOG_ERROR, "return AVERROR_EOF\n");
					continue;
				}else{
					av_log(NULL, AV_LOG_ERROR, "Cannot read frame\n");
					break;
				}
			}
			av_frame_free(&areframe);
			av_log(NULL, AV_LOG_DEBUG, "Muxing packet write audio to file\n");
			ret = av_interleaved_write_frame(ofmt_ctx, &tmppkt);
			if (ret < 0){
				av_packet_unref(&tmppkt);
				av_log(NULL, AV_LOG_ERROR, "write audio frame failed\n");
				break;
			}
			av_packet_unref(&tmppkt);
			

		
		}
	}
	
	
	ret = av_write_trailer(ofmt_ctx);	
	if (ret < 0){
		av_log(NULL, AV_LOG_ERROR, "error when av_write_trailer\n");
	}
	
	av_packet_unref(&packet);
//	av_frame_free(&areframe);
	
cleanup:
	swr_free(&actx);
	if (enc_ctx)
		avcodec_free_context(&enc_ctx);
	if (dec_ctx)
		avcodec_free_context(&enc_ctx);
	if (ofmt_ctx){
		avio_closep(&ofmt_ctx ->pb);
		avformat_free_context(ofmt_ctx);
	}
	if (ifmt_ctx){
		avformat_close_input(&ifmt_ctx);
	}

	return ret;
}
