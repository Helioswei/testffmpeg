#include "./common.h"

#define LOG(ret, message) if ((ret) < 0) { \
	av_log(NULL, AV_LOG_ERROR, message); \
	return ret;  \
		} \

static AVFormatContext *ifmt_ctx;
static AVFormatContext *ofmt_ctx;

typedef struct StreamContext{
	AVCodecContext *dec_ctx;
	AVCodecContext *enc_ctx;
}StreamContext;

static StreamContext *stream_ctx;

void init(){
	av_log_set_level(AV_LOG_DEBUG);
}

static int open_input_file(const char * filename){
	int ret;
	ifmt_ctx = NULL;
	//1,打开输入的文件
	ret = avformat_open_input(&ifmt_ctx, filename, NULL, NULL);
	LOG(ret, "Cannot open the input file\n");

	//2,查找输入文件中的流信息
	ret = avformat_find_stream_info(ifmt_ctx, NULL);
	LOG(ret, "Cannot find the stream infomation\n");

	//3,给steam_ctx 开辟空间
	stream_ctx = (StreamContext *)av_mallocz_array(ifmt_ctx ->nb_streams, sizeof(*stream_ctx));
	if (!stream_ctx){
		return AVERROR(ENOMEM);
	}

	//4,解码的初始化的一些工作
	for (int i = 0; i < ifmt_ctx -> nb_streams; i ++){
		AVStream *stream = ifmt_ctx -> streams[i];
		//4.1,查找解码器
		AVCodec *dec = avcodec_find_decoder(stream -> codecpar -> codec_id);
		if (!dec){
			av_log(NULL, AV_LOG_ERROR, "Failed to find decoder from stream %u\n", i);
			return AVERROR_DECODER_NOT_FOUND;
		}
		//4.2,初始化解码上下文
		AVCodecContext *codec_ctx = avcodec_alloc_context3(dec);
		if (!codec_ctx){
			av_log(NULL, AV_LOG_ERROR, "Failed to allocate the decoder context for stream\n");
			return AVERROR(ENOMEM);
		}
		ret = avcodec_parameters_to_context(codec_ctx, stream -> codecpar);	
		LOG(ret, "Failed to copy decoder parameters to input decoder context\n");
		if (stream -> codecpar -> codec_type == AVMEDIA_TYPE_VIDEO || stream -> codecpar -> codec_type == AVMEDIA_TYPE_AUDIO){
			if (codec_ctx ->codec_type == AVMEDIA_TYPE_VIDEO)
				codec_ctx -> framerate = av_guess_frame_rate(ifmt_ctx, stream, NULL);
			/*open decoder*/
			ret = avcodec_open2(codec_ctx, dec, NULL);
			LOG(ret, "Failed to open decoder for stream");
		}
		stream_ctx[i].dec_ctx = codec_ctx;
	}
	av_dump_format(ifmt_ctx, 0, filename, 0);
	return 0;

}

static int open_output_file(const char * filename){
	int ret;
	AVStream *out_stream;
	AVStream *in_stream;
	AVCodecContext *dec_ctx, *enc_ctx;
	AVCodec *encoder;

	ofmt_ctx = NULL;
	avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, filename);
	if (!ofmt_ctx){
		av_log(NULL, AV_LOG_ERROR, "Cannot create output context\n");
		return AVERROR_UNKNOWN;
	}
	//设置输出文件的流信息以及编码信息
	for(int i = 0; i < ifmt_ctx -> nb_streams; i++){
		out_stream = avformat_new_stream(ofmt_ctx, NULL);
		if (!out_stream){
			av_log(NULL, AV_LOG_ERROR, "Failed to allocating output stream\n");
			return AVERROR_UNKNOWN;
		}
		in_stream = ifmt_ctx -> streams[i];
		dec_ctx = stream_ctx[i].dec_ctx;
		if (dec_ctx -> codec_type == AVMEDIA_TYPE_VIDEO || dec_ctx -> codec_type == AVMEDIA_TYPE_AUDIO){
			/*In this example ,we transcode to same properties (picture size,sample rate etc.) changed for output streams easily usng filters*/		
			if (dec_ctx -> codec_type == AVMEDIA_TYPE_VIDEO){
				encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
				if (!encoder){
					av_log(NULL, AV_LOG_ERROR, "Necessary encoder not found\n");
					return AVERROR_UNKNOWN;
				}
				enc_ctx = avcodec_alloc_context3(encoder);

				enc_ctx -> codec_id = encoder -> id;
				enc_ctx -> codec_type = AVMEDIA_TYPE_VIDEO;
				enc_ctx -> bit_rate = 400000;
				enc_ctx -> bit_rate_tolerance = 4000000;

				enc_ctx -> height = dec_ctx -> height;
				enc_ctx -> width = dec_ctx -> width;
				//帧率的基本单位，我们用分数来表示
				enc_ctx -> time_base.den = dec_ctx -> time_base.den;
				//enc_ctx -> time_base = (AVRational){1, 25};
				enc_ctx -> time_base = dec_ctx -> time_base;
				enc_ctx -> time_base.num = 1;
				//每30帧插入1个I帧，I帧越少，视频越小
				enc_ctx -> gop_size = 30;
				//enc_ctx -> has_b_frames = 0;
				//两个非B帧之间允许出现多少个B帧
				//设置0表示不使用B帧
				//B帧越多，图片越小
				enc_ctx -> max_b_frames = 0;
				//enc_ctx -> me_subpel_quality = 0;
				//enc_ctx -> refs = 1;
				//enc_ctx -> scenechange_threshold = 0;
				//enc_ctx -> trellis = 0;
				//enc_ctx -> height = 640;
				//enc_ctx -> width = 320;
				//enc_ctx -> sample_aspect_ratio = dec_ctx -> sample_aspect_ratio;
				/*take first format from list of supported formats*/
				if (encoder -> pix_fmts)
					enc_ctx -> pix_fmt = encoder -> pix_fmts[0];
				else 
					enc_ctx -> pix_fmt = dec_ctx -> pix_fmt;
				/*video time_base can be set to whatever is handy and supported by encoder*/
				//enc_ctx -> time_base = (AVRational){1, 25};
				//enc_ctx -> framerate = (AVRational){25, 1};	
			} else {
				/*in this example,we choose transcode to same codec*/
				encoder = avcodec_find_encoder(dec_ctx -> codec_id);
				if (!encoder){
					av_log(NULL, AV_LOG_ERROR, "Necessary encoder not found\n");
					return AVERROR_UNKNOWN;
				}
				enc_ctx = avcodec_alloc_context3(encoder);
				if (!enc_ctx){
					av_log(NULL, AV_LOG_ERROR, "Failed to allocate the encoder context\n");
					return AVERROR(ENOMEM);
				}
				enc_ctx -> sample_rate = dec_ctx -> sample_rate;
				enc_ctx -> channel_layout = dec_ctx -> channel_layout;
				enc_ctx -> channels = av_get_channel_layout_nb_channels(enc_ctx -> channel_layout);
				/*take first format from list of supported formats*/
				enc_ctx -> sample_fmt = encoder -> sample_fmts[0];
				enc_ctx -> time_base = (AVRational){1, enc_ctx -> sample_rate};
			}	
			if (ofmt_ctx -> oformat -> flags & AVFMT_GLOBALHEADER)
				enc_ctx -> flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
			/*Third parameter can be used to pass settings to encoder*/
			ret = avcodec_open2(enc_ctx, encoder, NULL);
			if (ret < 0){
				av_log(NULL, AV_LOG_ERROR, "Cannot open video encoder for stream\n");
				return ret;
			}
			//注意：设置完编码的参数之后，必须将编码的信息和out_stream -> codecpar相连接
			ret = avcodec_parameters_from_context(out_stream -> codecpar, enc_ctx);
			if (ret < 0){
				av_log(NULL, AV_LOG_ERROR, "Failed to copy encoder parameters to output stream");
				return ret;
			}
			out_stream -> time_base = enc_ctx -> time_base;
			stream_ctx[i].enc_ctx = enc_ctx;
		
		} else if (dec_ctx -> codec_type == AVMEDIA_TYPE_UNKNOWN){
			av_log(NULL, AV_LOG_ERROR, "Elementary stream is of unknown type,cannot proceed\n");
			return AVERROR_INVALIDDATA;
		} else {
			/*if this stream must be remuxed*/
			ret = avcodec_parameters_copy(out_stream -> codecpar, in_stream -> codecpar);
			LOG(ret, "Copying parameters for stream failed\n");
			out_stream -> time_base = in_stream -> time_base;
		}
	}
	av_dump_format(ofmt_ctx, 0, filename, 1);

	if (!(ofmt_ctx -> oformat -> flags & AVFMT_NOFILE)){
		ret = avio_open(&ofmt_ctx -> pb, filename, AVIO_FLAG_WRITE);
		LOG(ret, "Could not open output file");
	}
	/*init muxer, write output file header*/
	ret = avformat_write_header(ofmt_ctx, NULL);
	LOG(ret, "Error occurred when opening output file\n");
	return 0;
}

static int encode_write_frame(AVFrame *filt_frame, int stream_index, int *got_frame){
	int ret;
	int got_frame_local;
	AVPacket enc_pkt;

	int (*enc_func)(AVCodecContext*, AVPacket *, const AVFrame *, int *) = (ifmt_ctx -> streams[stream_index]-> codecpar -> codec_type == AVMEDIA_TYPE_VIDEO)?avcodec_encode_video2:avcodec_encode_audio2;

	if (!got_frame)
		got_frame = &got_frame_local;
	
	av_log(NULL, AV_LOG_INFO, "Encoding frame\n");

	/*encode frame*/
	enc_pkt.data = NULL;
	enc_pkt.size = 0;
	av_init_packet(&enc_pkt);
	//编码了,编码之前设置时间
	//filt_frame -> pts = filt_frame -> best_effort_timestamp;
	ret = enc_func(stream_ctx[stream_index].enc_ctx, &enc_pkt, filt_frame, got_frame);
	av_frame_free(&filt_frame);
	if (ret < 0)
		return ret;
	if (!(*got_frame))
		return 0;
	/*prepare packet for muxing*/
	enc_pkt.stream_index = stream_index;
	//av_packet_rescale_ts(&enc_pkt, stream_ctx[stream_index].enc_ctx -> time_base, ofmt_ctx -> streams[stream_index] -> time_base);
	enc_pkt.pts = av_rescale_q_rnd(enc_pkt.pts,ifmt_ctx -> streams[stream_index]->time_base, ofmt_ctx->streams[stream_index]-> time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
	enc_pkt.dts = av_rescale_q_rnd(enc_pkt.dts,ifmt_ctx -> streams[stream_index]->time_base, ofmt_ctx->streams[stream_index]-> time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
	enc_pkt.duration = av_rescale_q(enc_pkt.duration, ifmt_ctx -> streams[stream_index] ->time_base, ofmt_ctx -> streams[stream_index] -> time_base);
	enc_pkt.pos = -1;
	av_log(NULL, AV_LOG_DEBUG, "Muxing frame\n");
	ret = av_interleaved_write_frame(ofmt_ctx, &enc_pkt);
	return ret;
}


int main(int argc, char **argv){
	int ret ;
	AVPacket packet;
	packet.data = NULL;
	packet.size = 0;
	AVFrame *frame = NULL;
	enum AVMediaType type;
	int stream_index;
	int i;
	int got_frame = 0;
	int got_output = 0;
	int (*dec_func)(AVCodecContext*, AVFrame*, int *, const AVPacket *);
	//const char * input_file = "/root/source_video/video1080p.mp4";
	//const char * output_file = "/root/transcode.mp4";
	const char * input_file = "/root/source_video/codetest.avi";
	const char * output_file = "/root/transcode.mp4";
	ret = open_input_file(input_file);
	if (ret < 0)
		goto end;
	ret = open_output_file(output_file);
	if (ret < 0)
		goto end;

	/*read all packets*/
	static int count = 0;
	while (true){
		count ++;
		cout << "count" << count << endl;
		ret = av_read_frame(ifmt_ctx, &packet);
		if (ret < 0){
			av_log(NULL, AV_LOG_INFO, "Cannot to read frame\n");
			ret = 0;
			break;
		}
		stream_index = packet.stream_index;
		type = ifmt_ctx -> streams[packet.stream_index] -> codecpar -> codec_type;
		av_log(NULL, AV_LOG_INFO, "Demuxer gave frame of stream_index\n");
		frame = av_frame_alloc();
		if (!frame){
			ret = AVERROR(ENOMEM);
			break;
		}
		//av_packet_rescale_ts(&packet, ifmt_ctx -> streams[stream_index] -> time_base, stream_ctx[stream_index].dec_ctx -> time_base);
		//解码
		dec_func =(type == AVMEDIA_TYPE_VIDEO)?avcodec_decode_video2:avcodec_decode_audio4;		
		ret = dec_func(stream_ctx[stream_index].dec_ctx, frame, &got_frame, &packet);
		if (ret < 0){
			av_frame_free(&frame);
			av_log(NULL, AV_LOG_ERROR, "Decodeing failed\n");
			break;
		}	
		if (got_frame){
			ret = encode_write_frame(frame, stream_index, &got_output);
			//注意在encode_write_frame中frame已经被释放了，所以下面不应该在释放，否则会出错
			//av_frame_free(&frame);
			if (ret < 0)
				goto end;
		} else {
			av_frame_free(&frame);
		}
	
	
	
	}

	av_packet_unref(&packet);

	av_write_trailer(ofmt_ctx);


end:
	if (!packet.data)
		av_packet_unref(&packet);
	//if (&frame)
		//av_frame_free(&frame);

	for (int i = 0; i < ifmt_ctx -> nb_streams; i++){
		avcodec_free_context(&stream_ctx[i].dec_ctx);
		if (ofmt_ctx && ofmt_ctx -> nb_streams >i && ofmt_ctx ->streams[i] && stream_ctx[i].enc_ctx)
			avcodec_free_context(&stream_ctx[i].enc_ctx);
	}
	av_free(stream_ctx);
	avformat_close_input(&ifmt_ctx);
	if (ofmt_ctx && !(ofmt_ctx -> oformat -> flags & AVFMT_NOFILE))
		avio_closep(&ofmt_ctx ->pb);
	avformat_free_context(ofmt_ctx);

	if (ret < 0)
		av_log(NULL, AV_LOG_ERROR, "Error occurred");

	return ret?1:0;
}
