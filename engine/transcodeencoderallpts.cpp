#include "./common.h"

#define LOG(ret, message)                    \
    if ((ret) < 0) {                         \
	av_log(NULL, AV_LOG_ERROR, message); \
	return ret;                          \
    }

static AVFormatContext *ifmt_ctx;
static AVFormatContext *ofmt_ctx;
SwrContext *actx = NULL;
SwsContext *vctx = NULL;
AVFrame *areframe;
AVFrame *vreframe;
typedef struct StreamContext {
    AVCodecContext *dec_ctx;
    AVCodecContext *enc_ctx;
} StreamContext;

static StreamContext *stream_ctx;

int videoIndex = -1, audioIndex = -1;

void init() {
    av_log_set_level(AV_LOG_DEBUG);
    av_register_all();
}

static int open_input_file(const char *filename) {
    int ret;
    ifmt_ctx = NULL;
    // 1,打开输入的文件
    ret = avformat_open_input(&ifmt_ctx, filename, NULL, NULL);
    LOG(ret, "Cannot open the input file\n");

    // 2,查找输入文件中的流信息
    ret = avformat_find_stream_info(ifmt_ctx, NULL);
    LOG(ret, "Cannot find the stream infomation\n");

    // 3,给steam_ctx 开辟空间
    stream_ctx = (StreamContext *)av_mallocz_array(ifmt_ctx->nb_streams,
						   sizeof(*stream_ctx));
    if (!stream_ctx) {
	return AVERROR(ENOMEM);
    }

    // 4,解码的初始化的一些工作
    for (int i = 0; i < ifmt_ctx->nb_streams; i++) {
	AVStream *stream = ifmt_ctx->streams[i];
	// 4.1,查找解码器
	AVCodec *dec = avcodec_find_decoder(stream->codecpar->codec_id);
	if (!dec) {
	    av_log(NULL, AV_LOG_ERROR,
		   "Failed to find decoder from stream %u\n", i);
	    return AVERROR_DECODER_NOT_FOUND;
	}
	// 4.2,初始化解码上下文
	AVCodecContext *codec_ctx = avcodec_alloc_context3(dec);
	if (!codec_ctx) {
	    av_log(NULL, AV_LOG_ERROR,
		   "Failed to allocate the decoder context for stream\n");
	    return AVERROR(ENOMEM);
	}
	ret = avcodec_parameters_to_context(codec_ctx, stream->codecpar);
	LOG(ret,
	    "Failed to copy decoder parameters to input decoder context\n");
	if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO ||
	    stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
	    if (codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
		codec_ctx->framerate =
		    av_guess_frame_rate(ifmt_ctx, stream, NULL);
		videoIndex = i;
	    }
	    if (codec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
		audioIndex = i;
	    }
	    /*open decoder*/
	    ret = avcodec_open2(codec_ctx, dec, NULL);
	    LOG(ret, "Failed to open decoder for stream");
	}
	stream_ctx[i].dec_ctx = codec_ctx;
    }
    av_dump_format(ifmt_ctx, 0, filename, 0);
    return 0;
}

static int open_output_file(const char *filename) {
    int ret;
    AVStream *out_stream;
    AVStream *in_stream;
    AVCodecContext *dec_ctx, *enc_ctx;
    AVCodec *encoder;

    ofmt_ctx = NULL;
    //将输入的日志写入文件中
    // ofstream outfile;
    // outfile.open("/root/debug",ios::app);
    avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, filename);
    if (!ofmt_ctx) {
	av_log(NULL, AV_LOG_ERROR, "Cannot create output context\n");
	return AVERROR_UNKNOWN;
    }
    //设置输出文件的流信息以及编码信息
    for (int i = 0; i < ifmt_ctx->nb_streams; i++) {
	out_stream = avformat_new_stream(ofmt_ctx, NULL);
	if (!out_stream) {
	    av_log(NULL, AV_LOG_ERROR, "Failed to allocating output stream\n");
	    return AVERROR_UNKNOWN;
	}
	in_stream = ifmt_ctx->streams[i];
	dec_ctx = stream_ctx[i].dec_ctx;
	if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO ||
	    dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
	    if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
		encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
		if (!encoder) {
		    av_log(NULL, AV_LOG_ERROR, "Necessary encoder not found\n");
		    return AVERROR_UNKNOWN;
		}
		enc_ctx = avcodec_alloc_context3(encoder);
		if (!enc_ctx) {
		    av_log(NULL, AV_LOG_ERROR,
			   "Failed to allocate the encoder context\n");
		    return AVERROR(ENOMEM);
		}

		enc_ctx->codec_id = encoder->id;
		enc_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
		//enc_ctx->me_range = 16;
		//enc_ctx->max_qdiff = 4;
		//enc_ctx->bit_rate = dec_ctx->bit_rate;
		enc_ctx->bit_rate = 4000000;
		//enc_ctx->qcompress = 0.6;

		//enc_ctx->height = dec_ctx->height;
		//enc_ctx->width = dec_ctx->width;
		enc_ctx->height = 480;
		enc_ctx->width = 640;
		//enc_ctx->sample_aspect_ratio = dec_ctx->sample_aspect_ratio;
		//帧率的基本单位，我们用分数来表示
		//enc_ctx->time_base.den = 25;
		// enc_ctx -> time_base = dec_ctx -> time_base;
		enc_ctx->time_base = (AVRational) {1, 25};
		enc_ctx -> framerate = (AVRational){25, 1};
		// enc_ctx -> framerate = dec_ctx -> framerate;
		//enc_ctx->time_base.num = 1;
		//每30帧插入1个I帧，I帧越少，视频越小
		enc_ctx->gop_size = 250;
		// enc_ctx -> has_b_frames = 0;
		//两个非B帧之间允许出现多少个B帧
		//设置0表示不使用B帧
		// B帧越多，图片越小,b帧不设置简单一些
		enc_ctx->max_b_frames = 0;
		/*take first format from list of supported formats*/
		if (encoder->pix_fmts)
		    enc_ctx->pix_fmt = encoder->pix_fmts[0];
		else
		    enc_ctx->pix_fmt = dec_ctx->pix_fmt;
		/*video time_base can be set to whatever is handy and supported
		 * by encoder*/
		// enc_ctx -> time_base = (AVRational){1, 25};
		av_opt_set(enc_ctx->priv_data, "preset", "slow", 0);
		//初始化像素转换
		vctx = sws_getCachedContext(vctx, 
				dec_ctx -> width, dec_ctx -> height, dec_ctx -> pix_fmt, //输入的数据
				enc_ctx -> width, enc_ctx -> height, enc_ctx -> pix_fmt,//输出的参数设置
				SWS_BICUBIC,
				NULL, NULL, NULL 
				);
		//初始化像素转换后存储的AVFrame
		vreframe = av_frame_alloc();
		vreframe->format = enc_ctx->pix_fmt;
		vreframe -> width = enc_ctx -> width;
		vreframe -> height = enc_ctx -> height;
		ret = av_frame_get_buffer(vreframe, 0);
		if (ret < 0) {
		    av_log(NULL, AV_LOG_ERROR, "av_frame_get_buffer video  failed\n");
		    return -1;
		}
		
	    } else {
		/*in this example,we choose transcode to same codec*/
		encoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
		if (!encoder) {
		    av_log(NULL, AV_LOG_ERROR, "Necessary encoder not found\n");
		    return AVERROR_UNKNOWN;
		}
		enc_ctx = avcodec_alloc_context3(encoder);
		if (!enc_ctx) {
		    av_log(NULL, AV_LOG_ERROR,
			   "Failed to allocate the encoder context\n");
		    return AVERROR(ENOMEM);
		}
		enc_ctx->bit_rate = 64000;
		// enc_ctx -> sample_rate = dec_ctx -> sample_rate;
		//如果设置成dec_ctx -> sample_rate ,则有问题，因为源是22050
		enc_ctx->sample_rate = 44100;
		enc_ctx->channel_layout = dec_ctx->channel_layout;
		enc_ctx->channels =
		    av_get_channel_layout_nb_channels(enc_ctx->channel_layout);
		/*take first format from list of supported formats*/
		enc_ctx->sample_fmt = encoder->sample_fmts[0];
		//初始化重采样
		actx = swr_alloc_set_opts(
		    actx, enc_ctx->channel_layout, enc_ctx->sample_fmt,
		    enc_ctx->sample_rate,  //输出格式
		    dec_ctx->channel_layout, dec_ctx->sample_fmt,
		    dec_ctx->sample_rate,  //输入格式
		    0, 0);
		if (!actx) {
		    av_log(NULL, AV_LOG_ERROR, "swr_alloc_set_opts failed\n");
		    return -1;
		}
		ret = swr_init(actx);
		if (ret < 0) {
		    av_log(NULL, AV_LOG_ERROR, "swr_init Failed \n");
		    return ret;
		}
		//初始化重采样后存储的AVFrame
		areframe = av_frame_alloc();
		areframe->format = enc_ctx->sample_fmt;
		areframe->channels = enc_ctx->channels;
		areframe->channel_layout = enc_ctx->channel_layout;
		areframe->nb_samples = 1024;
		ret = av_frame_get_buffer(areframe, 0);
		if (ret < 0) {
		    av_log(NULL, AV_LOG_ERROR, "av_frame_get_buffer failed\n");
		    return -1;
		}
	    }
	    /*Third parameter can be used to pass settings to encoder*/
	    if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
		enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	    ret = avcodec_open2(enc_ctx, encoder, NULL);
	    if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR,
		       "Cannot open video encoder for stream\n");
		return ret;
	    }
	    out_stream->codecpar->codec_tag = 0;
	    //注意：设置完编码的参数之后，必须将编码的信息和out_stream ->
	    //codecpar相连接
	    ret =
		avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);
	    if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR,
		       "Failed to copy encoder parameters to output stream");
		return ret;
	    }
	    out_stream->time_base = in_stream->time_base;
	    // out_stream -> time_base = enc_ctx -> time_base;
	    stream_ctx[i].enc_ctx = enc_ctx;

	} else if (dec_ctx->codec_type == AVMEDIA_TYPE_UNKNOWN) {
	    av_log(NULL, AV_LOG_ERROR,
		   "Elementary stream is of unknown type,cannot proceed\n");
	    return AVERROR_INVALIDDATA;
	} else {
	    /*if this stream must be remuxed*/
	    ret = avcodec_parameters_copy(out_stream->codecpar,
					  in_stream->codecpar);
	    LOG(ret, "Copying parameters for stream failed\n");
	    out_stream->time_base = in_stream->time_base;
	}
    }
    av_dump_format(ofmt_ctx, 0, filename, 1);

    if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
	ret = avio_open(&ofmt_ctx->pb, filename, AVIO_FLAG_WRITE);
	LOG(ret, "Could not open output file");
    }
    /*init muxer, write output file header*/
    ret = avformat_write_header(ofmt_ctx, NULL);
    LOG(ret, "Error occurred when opening output file\n");
    return 0;
}
//encode_audio encode_video两者可以合并成一个
int encode_video(AVFrame *frame, int stream_index){
	int got_output = 0;
	AVPacket tmppkt;
	tmppkt.data = NULL;
	tmppkt.size = 0;
	av_init_packet(&tmppkt);
	int ret;
	static int64_t count = 0;
	//先将解码得到的AVFame数据，源数据进行重采样
	int len = sws_scale(vctx, frame -> data, frame -> linesize, 0, stream_ctx[stream_index].dec_ctx -> height,
			  vreframe -> data, vreframe -> linesize);
	if (len <= 0){
		av_log(NULL, AV_LOG_ERROR, "swr_convert failed \n");
		return -1;
	}
	vreframe -> pts = frame -> best_effort_timestamp;
	ret = avcodec_encode_video2(stream_ctx[stream_index].enc_ctx, &tmppkt, vreframe, &got_output);
	if (ret < 0){
		av_frame_free(&frame);
		av_packet_unref(&tmppkt);
		av_log(NULL, AV_LOG_ERROR, "Encoding video failed\n");
		return ret;
	}
	if (got_output){
		//写入文件
		tmppkt.stream_index = stream_index;
		tmppkt.pts = av_rescale_q_rnd(tmppkt.pts, ifmt_ctx -> streams[stream_index] -> time_base, ofmt_ctx -> streams[stream_index] -> time_base, (AVRounding)(AV_ROUND_NEAR_INF| AV_ROUND_PASS_MINMAX));
		tmppkt.dts = av_rescale_q_rnd(tmppkt.dts, ifmt_ctx -> streams[stream_index] -> time_base, ofmt_ctx -> streams[stream_index] -> time_base, (AVRounding)(AV_ROUND_NEAR_INF| AV_ROUND_PASS_MINMAX));
		tmppkt.duration = av_rescale_q(tmppkt.duration, ifmt_ctx -> streams[stream_index] -> time_base, ofmt_ctx -> streams[stream_index] -> time_base);
		tmppkt.pos = -1;
		//cout << "time" << tmppkt.pts * av_q2d(ofmt_ctx->streams[stream_index] -> time_base);
		count = count + tmppkt.pts* av_q2d(ofmt_ctx -> streams[stream_index] -> time_base);
		cout << count << endl;
		av_log(NULL, AV_LOG_DEBUG, "Muxing frame write video to file\n");
		ret = av_interleaved_write_frame(ofmt_ctx, &tmppkt);
		if(ret < 0){
			av_packet_unref(&tmppkt);
			av_log(NULL, AV_LOG_ERROR, "write audio frame failed\n");
			return 1;
		}
	}

	return 0;
}

int encode_audio(AVFrame *frame, int stream_index){
	int got_output = 0;
	AVPacket tmppkt;
	tmppkt.data = NULL;
	tmppkt.size = 0;
	av_init_packet(&tmppkt);
	int ret;
	//先将解码得到的AVFame数据，源数据进行重采样
	int len = swr_convert(actx, areframe -> data, areframe ->nb_samples,
			(const uint8_t **)frame ->data, frame -> nb_samples);	
	if (len <= 0){
		av_log(NULL, AV_LOG_ERROR, "swr_convert failed \n");
		return -1;
	}
	areframe -> pts = frame -> best_effort_timestamp;
	 ret = avcodec_encode_audio2(stream_ctx[stream_index].enc_ctx, &tmppkt, areframe, &got_output);
	if (ret < 0){
		av_frame_free(&frame);
		av_packet_unref(&tmppkt);
		av_log(NULL, AV_LOG_ERROR, "Encoding video failed\n");
		return ret;
	}
	if (got_output){
		//写入文件
		tmppkt.stream_index = stream_index;
		tmppkt.pts = av_rescale_q_rnd(tmppkt.pts, ifmt_ctx -> streams[stream_index] -> time_base, ofmt_ctx -> streams[stream_index] -> time_base, (AVRounding)(AV_ROUND_NEAR_INF| AV_ROUND_PASS_MINMAX));
		tmppkt.dts = av_rescale_q_rnd(tmppkt.dts, ifmt_ctx -> streams[stream_index] -> time_base, ofmt_ctx -> streams[stream_index] -> time_base, (AVRounding)(AV_ROUND_NEAR_INF| AV_ROUND_PASS_MINMAX));
		tmppkt.duration = av_rescale_q(tmppkt.duration, ifmt_ctx -> streams[stream_index] -> time_base, ofmt_ctx -> streams[stream_index] -> time_base);
		tmppkt.pos = -1;
		av_log(NULL, AV_LOG_DEBUG, "Muxing frame write audio to file\n");
		ret = av_interleaved_write_frame(ofmt_ctx, &tmppkt);
		if(ret < 0){
			av_packet_unref(&tmppkt);
			av_log(NULL, AV_LOG_ERROR, "write audio frame failed\n");
			return 1;
		}
	}

	return 0;
}

int main(int argc, char **argv) {
    int ret;
    AVPacket packet;
    packet.data = NULL;
    packet.size = 0;
    av_init_packet(&packet);
    AVFrame *frame = NULL;
    enum AVMediaType type;
    int stream_index;
    int i;
    int got_frame = 0;
	int64_t cur_pts_v = 0, cur_pts_a = 0;
    // const char * input_file = "/root/source_video/video1080p.mp4";
    // const char * input_file = "/root/source_video/codetest.avi";
    // const char * input_file = "/root/source_video/video/12.mpg";
    // const char * input_file = "/root/source_video/video/asf.asf";
    // const char * input_file = "/root/source_video/video/test.mp4";
     const char * input_file = "/root/source_video/video/flv.flv";
    // const char * input_file = "/root/source_video/video/mkv.mkv";
    // const char * input_file = "/root/source_video/video/mov.mov";
    // const char * input_file = "/root/source_video/video/swf.swf";
    //const char *input_file = "/root/source_video/video/test.3gp";
    //const char *input_file = "/root/source_video/video/test.MXF";
    const char *output_file = "/root/transcode.mp4";
    init();
    //打开输入的文件
    ret = open_input_file(input_file);
    if (ret < 0) goto end;

    //打开输出的文件
    ret = open_output_file(output_file);
    if (ret < 0) goto end;
	int comparepts;
    /*read all packets*/
    while (true) {
		cout << "cur_pts_v is " << cur_pts_v << " cur_pts_a is " << cur_pts_a << endl;
		comparepts = av_compare_ts(cur_pts_v, ifmt_ctx -> streams[videoIndex]-> time_base, cur_pts_a, ifmt_ctx -> streams[audioIndex] -> time_base);
		cout << "comparepts is " << comparepts << endl;
		if (comparepts <= 0){
			//处理视频，先解码，在编码，在写入文件
			ret = av_read_frame(ifmt_ctx, &packet);
			if (ret < 0){
				av_log(NULL, AV_LOG_ERROR, "Cannot to read frame\n");
				break;
			}
			stream_index = packet.stream_index;
			type = ifmt_ctx -> streams[packet.stream_index] -> codecpar -> codec_type;
			frame = av_frame_alloc();
			if (!frame){
				ret = AVERROR(ENOMEM);
				break;
			}
			if (AVMEDIA_TYPE_VIDEO == type){
				cur_pts_v = packet. pts;
				ret = avcodec_decode_video2(stream_ctx[stream_index].dec_ctx, frame,&got_frame, &packet);
				if (ret < 0){
					av_frame_free(&frame);
					av_log(NULL,AV_LOG_ERROR, "Decoding video failed\n");
					break;
				}
				if (got_frame){
					ret = encode_video(frame, stream_index);
					if (ret < 0){
						break;
					} else if (ret == 1){
						goto end;
					}
				} else {
					av_frame_free(&frame);
					av_log(NULL, AV_LOG_ERROR, "got_frame video is null\n");	
				
				}
			
			} else {
				continue;
			}
		
	
		} else {
			//处理音频
			ret = av_read_frame(ifmt_ctx, &packet);
			if (ret < 0){
				av_log(NULL, AV_LOG_ERROR, "Cannot to read frame\n");
				break;
			}
			stream_index = packet.stream_index;
			type = ifmt_ctx -> streams[packet.stream_index] -> codecpar -> codec_type;
			frame = av_frame_alloc();
			if (!frame){
				ret = AVERROR(ENOMEM);
				break;
			}
			if (AVMEDIA_TYPE_AUDIO == type){
				cur_pts_a = packet.pts;
				ret = avcodec_decode_audio4(stream_ctx[stream_index].dec_ctx, frame,&got_frame, &packet);
				if (ret < 0){
					av_frame_free(&frame);
					av_log(NULL,AV_LOG_ERROR, "Decoding video failed\n");
					break;
				}
				if (got_frame){
					ret = encode_audio(frame, stream_index);
					if (ret < 0)
						break;
					else if (ret == 1)
						goto end;
				} else {
					av_frame_free(&frame);
					av_log(NULL, AV_LOG_ERROR, "got_frame video is null\n");	
				
				}
			
			} else {
				continue;
			}	
		
		}
}
    av_frame_free(&areframe);
    av_frame_free(&vreframe);
    av_packet_unref(&packet);

    av_write_trailer(ofmt_ctx);

end:
    if (!packet.data) av_packet_unref(&packet);
    // if (&frame)
    // av_frame_free(&frame);

    for (int i = 0; i < ifmt_ctx->nb_streams; i++) {
	avcodec_free_context(&stream_ctx[i].dec_ctx);
	if (ofmt_ctx && ofmt_ctx->nb_streams > i && ofmt_ctx->streams[i] &&
	    stream_ctx[i].enc_ctx)
	    avcodec_free_context(&stream_ctx[i].enc_ctx);
    }
    av_free(stream_ctx);
    avformat_close_input(&ifmt_ctx);
    if (ofmt_ctx && !(ofmt_ctx->oformat->flags & AVFMT_NOFILE))
	avio_closep(&ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);

    if (ret < 0) av_log(NULL, AV_LOG_ERROR, "Error occurred");

    return ret ? 1 : 0;
}
