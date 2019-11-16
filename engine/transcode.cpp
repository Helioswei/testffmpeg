#include "./common.h"

#define LOG(ret, message) if ((ret) < 0) { \
	av_log(NULL, AV_LOG_ERROR, message); \
	return ret;  \
		} \

#define BLOG(ret, message,res) if ((ret)) { \
	av_log(NULL, AV_LOG_ERROR, message); \
	return res; \
	} \

static AVFormatContext *ifmt_ctx;
static AVFormatContext *ofmt_ctx;

typedef struct StreamContext{
	AVCodecContext *dec_ctx;
	AVCodecContext *enc_ctx;
}StreamContext;

static StreamContext *stream_ctx;

int64_t mypts = 0;
int64_t myvpts = 0;
int audioIndex = -1,videoIndex = -1;
int64_t videoDuration = 0;

void init_log(){
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
			if (codec_ctx ->codec_type == AVMEDIA_TYPE_VIDEO){
				codec_ctx -> framerate = av_guess_frame_rate(ifmt_ctx, stream, NULL);
				videoIndex = i;
			}
			if (codec_ctx -> codec_type == AVMEDIA_TYPE_AUDIO){
				audioIndex = i;
			}
			/*open decoder*/
			ret = avcodec_open2(codec_ctx, dec, NULL);
			LOG(ret, "Failed to open decoder for stream");
		}
		stream_ctx[i].dec_ctx = codec_ctx;
	}
	av_dump_format(ifmt_ctx, 0, filename, 0);
	videoDuration = ifmt_ctx -> duration/10000;//videoDuration/100 单位为秒
	return 0;

}

static void init_packet(AVPacket *packet){
	av_init_packet(packet);
	packet -> data = NULL;
	packet -> size = 0;
}

static int init_input_frame(AVFrame **frame){
	if(!(*frame = av_frame_alloc() )){
		av_log(NULL, AV_LOG_ERROR, "Could not allocate input frame\n");
		return AVERROR(ENOMEM);
	}
	return 0;
}
static int init_output_frame(AVFrame **frame, AVCodecContext *enc_ctx, int frame_size)
{
	int error;
	if(!(*frame = av_frame_alloc())){
		av_log(NULL, AV_LOG_ERROR, "Could not allocate output frame\n");
		return AVERROR_EXIT;
	}
	if (enc_ctx -> codec_type == AVMEDIA_TYPE_VIDEO){
		(*frame) -> format = enc_ctx -> pix_fmt;
		(*frame) -> width = enc_ctx -> width;
		(*frame) -> height = enc_ctx -> height;	
	} else if ( enc_ctx -> codec_type == AVMEDIA_TYPE_AUDIO){
		(*frame) -> nb_samples = frame_size;
		(*frame) -> channel_layout = enc_ctx -> channel_layout;
		(*frame) -> format = enc_ctx -> sample_fmt;
		(*frame) -> sample_rate = enc_ctx -> sample_rate;
	}

	error = av_frame_get_buffer(*frame, 0);
	if (error < 0){
		av_log(NULL, AV_LOG_ERROR,"Could not allocate output frame samples");
		av_frame_free(frame);
		return error;
	}
	return 0;
}

static int init_repixel(AVCodecContext *dec_ctx, AVCodecContext *enc_ctx, SwsContext **repixel_ctx){
	int error;
	*repixel_ctx = sws_getCachedContext(NULL,
			dec_ctx -> width, dec_ctx -> height, dec_ctx -> pix_fmt,//输入的数据格式
			enc_ctx -> width, enc_ctx -> height, enc_ctx -> pix_fmt,//输出的数据格式
			SWS_BICUBIC,
			NULL, NULL, NULL 
			); 

    if (!*repixel_ctx) {
         av_log(NULL, AV_LOG_ERROR, "Could not allocate repixel context\n");
         return AVERROR(ENOMEM);
    }
	return 0;
}
static int init_resampler(AVCodecContext *input_codec_context,
                          AVCodecContext *output_codec_context,
                          SwrContext **resample_context)
{
        int error;

        /*
         * Create a resampler context for the conversion.
         * Set the conversion parameters.
         * Default channel layouts based on the number of channels
         * are assumed for simplicity (they are sometimes not detected
         * properly by the demuxer and/or decoder).
         */
        *resample_context = swr_alloc_set_opts(NULL,
                                              av_get_default_channel_layout(output_codec_context->channels),
                                              output_codec_context->sample_fmt,
                                              output_codec_context->sample_rate,//输出格式
                                              av_get_default_channel_layout(input_codec_context->channels),
                                              input_codec_context->sample_fmt,
                                              input_codec_context->sample_rate,//输入格式
                                              0, NULL);
        if (!*resample_context) {
            av_log(NULL, AV_LOG_ERROR, "Could not allocate resample context\n");
            return AVERROR(ENOMEM);
        }
        /*
        * Perform a sanity check so that the number of converted samples is
        * not greater than the number of samples to be converted.
        * If the sample rates differ, this case has to be handled differently
        */
        //av_assert0(output_codec_context->sample_rate == input_codec_context->sample_rate);

        /* Open the resampler with the specified parameters. */
        if ((error = swr_init(*resample_context)) < 0) {
            av_log(NULL, AV_LOG_ERROR, "Could not open resample context\n");
            swr_free(resample_context);
            return error;
        }
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
			if (dec_ctx -> codec_type == AVMEDIA_TYPE_VIDEO){
				encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
				if (!encoder){
					av_log(NULL, AV_LOG_ERROR, "Necessary encoder not found\n");
					return AVERROR_UNKNOWN;
				}
				enc_ctx = avcodec_alloc_context3(encoder);
				BLOG(!enc_ctx, "Failed to allocate the encoder context\n",AVERROR(ENOMEM));
				
				enc_ctx -> codec_id = encoder -> id;
				enc_ctx -> codec_type = AVMEDIA_TYPE_VIDEO;
				enc_ctx -> me_range = 16;
				enc_ctx -> max_qdiff = 4;
				enc_ctx -> qmin = 10;
				enc_ctx -> qmax = 51;
				enc_ctx -> qcompress = 0.6;
				enc_ctx -> bit_rate = dec_ctx -> bit_rate;
				//enc_ctx -> bit_rate = 4000000;

				//enc_ctx -> height = dec_ctx -> height;
				//enc_ctx -> width = dec_ctx -> width;
				enc_ctx -> height = 480;
				enc_ctx -> width = 640;
				enc_ctx -> sample_aspect_ratio = dec_ctx -> sample_aspect_ratio;
				//帧率的基本单位，我们用分数来表示,手动设置可能不起作用
				enc_ctx -> time_base = dec_ctx -> time_base;
				enc_ctx -> framerate = dec_ctx -> framerate;
				//每30帧插入1个I帧，I帧越少，视频越小
				//enc_ctx -> gop_size = 30;
				//enc_ctx -> has_b_frames = 0;
				//两个非B帧之间允许出现多少个B帧
				//设置0表示不使用B帧
				//B帧越多，图片越小
				enc_ctx -> max_b_frames = 0;
				/*take first format from list of supported formats*/
				if (encoder -> pix_fmts)
					enc_ctx -> pix_fmt = encoder -> pix_fmts[0];
				else 
					enc_ctx -> pix_fmt = dec_ctx -> pix_fmt;
				/*video time_base can be set to whatever is handy and supported by encoder*/
				av_opt_set(enc_ctx -> priv_data, "preset", "slow",0);
			} else {
				encoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
				if (!encoder){
					av_log(NULL, AV_LOG_ERROR, "Necessary encoder not found\n");
					return AVERROR_UNKNOWN;
				}
				enc_ctx = avcodec_alloc_context3(encoder);
				if (!enc_ctx){
					av_log(NULL, AV_LOG_ERROR, "Failed to allocate the encoder context\n");
					return AVERROR(ENOMEM);
				}
				enc_ctx -> bit_rate = 64000;
				//如果设置成下面的，有问题，因为源可能为22050,
				//enc_ctx -> sample_rate = dec_ctx -> sample_rate;
				enc_ctx -> sample_rate = 44100;
				enc_ctx -> frame_size = 1024;
				//enc_ctx -> channel_layout = dec_ctx -> channel_layout;
				//enc_ctx -> channels = av_get_channel_layout_nb_channels(enc_ctx -> channel_layout);
				enc_ctx -> channels = dec_ctx -> channels;
				enc_ctx -> channel_layout = av_get_default_channel_layout(enc_ctx -> channels);
				/*take first format from list of supported formats*/
				enc_ctx -> sample_fmt = encoder -> sample_fmts[0];
				enc_ctx -> time_base = (AVRational){1, enc_ctx -> sample_rate};
			}	
			if (ofmt_ctx -> oformat -> flags & AVFMT_GLOBALHEADER)
				enc_ctx -> flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		
			/*Third parameter can be used to pass settings to encoder*/
			ret = avcodec_open2(enc_ctx, encoder, NULL);
			LOG(ret,"Cannot open encoder for stream\n");
			//同时设置codec_tag为0
			out_stream -> codecpar -> codec_tag = 0;
		
			//注意：设置完编码的参数之后，必须将编码的信息和out_stream -> codecpar相连接
			ret = avcodec_parameters_from_context(out_stream -> codecpar, enc_ctx);
			LOG(ret,"Failed to copy encoder parameters to output stream");
			
			//out_stream -> time_base = AVRational{1,1000};
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
	//移动moov到文件头
	AVDictionary *dict = NULL;
	av_dict_set(&dict, "movflags", "faststart",0);
	//av_dict_set(&dict, "movflags", "frag_keyframe+empty_moov",0);//vlc可以播放
	ret = avformat_write_header(ofmt_ctx, &dict);
	//ret = avformat_write_header(ofmt_ctx, NULL);
	LOG(ret, "Error occurred when opening output file\n");
	return 0;
}


static int init_converted_samples(uint8_t ***converted_input_samples,
                                  AVCodecContext *output_codec_context,
                                  int frame_size)
{
    int error;

    /* Allocate as many pointers as there are audio channels.
     * Each pointer will later point to the audio samples of the corresponding
     * channels (although it may be NULL for interleaved formats).
     */
    if (!(*converted_input_samples =(uint8_t **) calloc(output_codec_context->channels,sizeof(**converted_input_samples)))) {
        av_log(NULL, AV_LOG_ERROR, "Could not allocate converted input sample opinters\n");
        return AVERROR(ENOMEM);
    }

    /* Allocate memory for the samples of all channels in one consecutive
     * block for convenience. */
    if ((error = av_samples_alloc(*converted_input_samples, NULL,
                                  output_codec_context->channels,
                                  frame_size,
                                  output_codec_context->sample_fmt, 0)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not allocate converted input samples\n");
        av_freep(&(*converted_input_samples)[0]);
        free(*converted_input_samples);
        return error;
    }
    return 0;
}

static int init_fifo(AVAudioFifo **fifo, AVCodecContext *enc_ctx){
	if (!(*fifo = av_audio_fifo_alloc(enc_ctx -> sample_fmt, enc_ctx -> channels, enc_ctx -> frame_size))){
		av_log(NULL, AV_LOG_ERROR, "Could not allocate FIFO\n");
		return AVERROR(ENOMEM);
	}
	return 0;
}
static int add_samples_to_fifo(AVAudioFifo *fifo,
                               uint8_t **converted_input_samples,
                               const int frame_size)
{
    int error;

    /* Make the FIFO as large as it needs to be to hold both,
     * the old and the new samples. */
    if ((error = av_audio_fifo_realloc(fifo, av_audio_fifo_size(fifo) + frame_size)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not reaallocate FIFO\n");
        return error;
    }

    /* Store the new samples in the FIFO buffer. */
    if (av_audio_fifo_write(fifo, (void **)converted_input_samples,
                            frame_size) < frame_size) {
        av_log(NULL, AV_LOG_ERROR, "Could not write data to FIFO\n");
        return AVERROR_EXIT;
    }
    return 0;
}

static int decode_media_frame(AVPacket *input_packet,AVFrame *frame,
                              AVCodecContext *input_codec_context,
                              int *data_present, int *finished)
{
    int error;
	if (input_packet -> data == NULL && input_packet -> size == 0){
		av_log(NULL, AV_LOG_ERROR, "the parameter input packet is NULL\n");
		error = -1;
		goto cleanup;
	}
    /* Send the audio frame stored in the temporary packet to the decoder.
     * The input audio stream decoder is used to do this. */
    if ((error = avcodec_send_packet(input_codec_context, input_packet)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not send packet for decoding\n");
        return error;
    }

    /* Receive one frame from the decoder. */
    error = avcodec_receive_frame(input_codec_context, frame);
    /* If the decoder asks for more data to be able to decode a frame,
     * return indicating that no data is present. */
    if (error == AVERROR(EAGAIN)) {
        error = 1;//正确的应该设置为0，然后在程序中处理这个
        goto cleanup;
    /* If the end of the input file is reached, stop decoding. */
    } else if (error == AVERROR_EOF) {
        *finished = 1;
        error = 0;
        goto cleanup;
    } else if (error < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not decode frame\n");
        goto cleanup;
    /* Default case: Return decoded data. */
    } else {
        *data_present = 1;
		//mypts = frame -> best_effort_timestamp;
        goto cleanup;
    }

cleanup:
    av_packet_unref(input_packet);
    return error;

}

static int convert_samples(const uint8_t **input_data,
                           uint8_t **converted_data, const int inframe_size,
						   const int outframe_size,SwrContext *resample_context,int *cachenum)
{
    int error;

    /* Convert the samples using the resampler. */
    if ((error = swr_convert(resample_context,
                             converted_data, outframe_size,
                             input_data    , inframe_size)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not convert input samples\n");
        return error;
    }
	*cachenum= swr_get_out_samples(resample_context,0);

    return 0;
}

static int read_decode_convert_and_store(AVFrame *input_frame,AVAudioFifo *fifo,
										 AVCodecContext *input_codec_context,
                                         AVCodecContext *output_codec_context,
                                         SwrContext *resampler_context)
{
    /* Temporary storage for the converted input samples. */
    uint8_t **converted_input_samples = NULL;
    int ret = AVERROR_EXIT;
	int cachenum = -1;

        /* Initialize the temporary storage for the converted input samples. */
        if (init_converted_samples(&converted_input_samples, output_codec_context, input_frame->nb_samples) < 0)
            goto cleanup;

        /* Convert the input samples to the desired output sample format.
         * This requires a temporary storage provided by converted_input_samples. */
       if (convert_samples((const uint8_t**)input_frame->extended_data, converted_input_samples,
                            input_frame->nb_samples,input_frame -> nb_samples, resampler_context, &cachenum) < 0)
            goto cleanup;

        /* Add the converted input samples to the FIFO buffer for later processing. */
		//将重采样后的数据放入队列中,注意在这里len和input->nb_samples的取值，必须使用len的，重采样后的！      
		//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!                      
        if (add_samples_to_fifo(fifo, converted_input_samples,input_frame -> nb_samples) < 0)
   			goto cleanup;
		
		//在重采样中会有缓存，这个几乎在demo中都没有说明，
		//导致会有一些问题，
		while (cachenum >= input_frame -> nb_samples){
			 if (converted_input_samples) {
   			     av_freep(&converted_input_samples[0]);
   			     free(converted_input_samples);
   			 }
	        if (init_converted_samples(&converted_input_samples, output_codec_context, input_frame->nb_samples) < 0)
	            goto cleanup;
	
	       if (convert_samples(NULL, converted_input_samples,
	                            0,input_frame -> nb_samples, resampler_context, &cachenum) < 0)
	            goto cleanup;
	
	        if (add_samples_to_fifo(fifo, converted_input_samples,input_frame -> nb_samples) < 0)
	            goto cleanup;
		
		}
	   	ret = 0;
	
cleanup:
	if (converted_input_samples) {
		av_freep(&converted_input_samples[0]);
        free(converted_input_samples);
    }
    av_frame_free(&input_frame);

    return ret;
}


static int encode_media_frame(AVFrame *frame, AVFormatContext *ofmt_ctx, AVCodecContext *enc_ctx, int *data_preset,int stream_index){
	AVPacket output_packet;
	int error;
	init_packet(&output_packet);
	if (frame && stream_index == audioIndex){
		frame -> pts = mypts;
		mypts+= frame -> nb_samples;
	}
	if (frame && stream_index == videoIndex){
		frame->pts = myvpts;
		myvpts += enc_ctx->time_base.den / enc_ctx->framerate.num;
		//cout << myvpts << endl;
		/*计算公式，计算一帧的实际时间：1/fps,如25帧/s的话一帧的时间是1/25=0.04s
		 *在AVFrame中根据 AVFrame -> pts * av_q2d(AVCodecContext -> time_base) =
		 *1/fps
		 */
	}
	error = avcodec_send_frame(enc_ctx, frame);
	if (error == AVERROR_EOF){
		error = 0;
		goto cleanup;
	} else if (error < 0){
		av_log(NULL, AV_LOG_ERROR, "Could not send frame for encodeing\n");
		return error;
	}
	error = avcodec_receive_packet(enc_ctx, &output_packet);
	if(error == AVERROR(EAGAIN)){
		error = 0;
		goto cleanup;
	} else if (error == AVERROR_EOF){
		error = 0;
		goto cleanup;
	} else if (error < 0){
		av_log(NULL ,AV_LOG_ERROR, "Could not encode frame\n");
		goto cleanup;
	}else{
		*data_preset = 1;
	}
	//转换pts
	output_packet.stream_index = stream_index;
	av_packet_rescale_ts(&output_packet, enc_ctx -> time_base, ofmt_ctx ->streams[stream_index] -> time_base);
	if (enc_ctx -> codec_type == AVMEDIA_TYPE_VIDEO){
		int64_t pts2ms = output_packet.pts *av_q2d(ofmt_ctx -> streams[stream_index] -> time_base) * 100 ;//pts2ms/100 = s为单位
		//cout << pts2ms << "|" << videoDuration << endl;
		double progress = (double)pts2ms *100 / videoDuration;//转换百分数	
		//cout  <<  progress << endl;
		av_log(NULL, AV_LOG_INFO, "progress : %.2f%\n",progress);
	}
	//getchar();	
	if (*data_preset&&(error = av_interleaved_write_frame(ofmt_ctx,&output_packet)) < 0){
		av_log(NULL, AV_LOG_ERROR, "Could not write frame\n");
		goto cleanup;
	}
	
cleanup:
	av_packet_unref(&output_packet);
	return error;	
}

static int encode_audio_frame(AVFrame *frame, AVFormatContext *ofmt_ctx, AVCodecContext *enc_ctx, int *data_preset){
	AVPacket output_packet;
	int error;
	init_packet(&output_packet);
	if (frame){
		//frame -> pts = mypts;
		//mypts+= frame -> nb_samples;	
	}
	//cout << frame -> pts << endl;
	//getchar();
	error = avcodec_send_frame(enc_ctx, frame);
	if (error == AVERROR_EOF){
		error = 0;
		goto cleanup;
	} else if (error < 0){
		av_log(NULL, AV_LOG_ERROR, "Could not send frame for encodeing\n");
		return error;
	}
	error = avcodec_receive_packet(enc_ctx, &output_packet);
	if(error == AVERROR(EAGAIN)){
		error = 0;
		goto cleanup;
	} else if (error == AVERROR_EOF){
		error = 0;
		goto cleanup;
	} else if (error < 0){
		av_log(NULL ,AV_LOG_ERROR, "Could not encode frame\n");
		goto cleanup;
	}else{
		*data_preset = 1;
	}
	//转换pts
	cout << "old pts" << output_packet.pts << endl;
	cout << "old dts" << output_packet.dts << endl;
	//av_packet_rescale_ts(&output_packet, enc_ctx -> time_base, ofmt_ctx ->streams[0] -> time_base);
	//getchar();	
	if (*data_preset&&(error = av_interleaved_write_frame(ofmt_ctx,&output_packet)) < 0){
		av_log(NULL, AV_LOG_ERROR, "Could not write frame\n");
		goto cleanup;
	}
	
cleanup:
	av_packet_unref(&output_packet);
	return error;	
}

static int load_encode_and_write(AVAudioFifo *fifo,
                                 AVFormatContext *output_format_context,
                                 AVCodecContext *output_codec_context,int stream_index)
{
    /* Temporary storage of the output samples of the frame written to the file. */
    AVFrame *output_frame;
    /* Use the maximum number of possible samples per frame.
     * If there is less than the maximum possible frame size in the FIFO
     * buffer use this number. Otherwise, use the maximum possible frame size. */
    const int frame_size = FFMIN(av_audio_fifo_size(fifo),
                                 output_codec_context->frame_size);
    int data_written;

    /* Initialize temporary storage for one output frame. */
    if (init_output_frame(&output_frame, output_codec_context, frame_size) < 0)
        return AVERROR_EXIT;

    /* Read as many samples from the FIFO buffer as required to fill the frame.
     * The samples are stored in the frame temporarily. */
    if (av_audio_fifo_read(fifo, (void **)output_frame->data, frame_size) < frame_size) {
        av_log(NULL,AV_LOG_ERROR, "Could not read data from FIFO\n");
        av_frame_free(&output_frame);
        return AVERROR_EXIT;
    }

    /* Encode one frame worth of audio samples. */
    if (encode_media_frame(output_frame, output_format_context,
                           output_codec_context, &data_written,stream_index) < 0) {
        av_frame_free(&output_frame);
        return AVERROR_EXIT;
    }
    av_frame_free(&output_frame);
    return 0;
}

int main(int argc, char **argv){
	int ret ;
    ofstream  myout;
	myout.open("/root/debug",ios::app);
	
	//successful
	//const char * input_file = "/root/source_video/video1080p.mp4";
    //const char * input_file = "/root/source_video/video/video.mp4";
    //const char * input_file = "/root/source_video/codetest.avi";
   	const char * input_file = "/root/source_video/video/mkv.mkv";
    //const char * input_file = "/root/source_video/video/swf.swf";
    //const char *input_file = "/root/source_video/video/test.3gp";
	//const char * input_file = "/root/source_video/video/flv.flv";//有一帧返回EAGAIN,末位少了一帧
	
	//const char * input_file = "/root/video1/test.avi";//一小时
	//const char * input_file = "/root/video1/test1.ts";
	//const char * input_file = "/root/video1/test1.webm";
	//const char * input_file = "/root/video1/test1.wmv";
	//const char * input_file = "/root/video1/test1.mov";
	
	//const char * input_file = "/root/video2/avi.avi";
	//const char * input_file = "/root/video2/mpg.mpg";//视频少了几帧
	//const char * input_file = "/root/video2/rm.rm";//视频多了几帧
	//const char * input_file = "/root/video2/test.mp4";//视频丢帧
     
    //unsuccessful
    // const char * input_file = "/root/source_video/video/mov.mov";//有三路流，其中一路流识别不出来
    //const char *input_file = "/root/source_video/video/test.MXF";//vlc可以播放，但是暴风影音播放不了,leopard3.4可以
	//const char * input_file = "/root/source_video/video/asf.asf";//有5路流
    
	const char * output_file = "/root/transcode.mp4";
	SwrContext *resample_context = NULL;//音频重采样
	SwsContext *repixel_context = NULL;//视频像素转换
	AVAudioFifo *fifo = NULL;

	AVFrame *input_frame = NULL;

	enum AVMediaType type;
	int stream_index;
	int finished = 0;
	int data_preset = 0;
	ret = open_input_file(input_file);
	if (ret < 0)
		goto cleanup;
	
	ret = open_output_file(output_file);
	if (ret < 0)
		goto cleanup;

	if (audioIndex >= 0){
		if (init_resampler(stream_ctx[audioIndex].dec_ctx,stream_ctx[audioIndex].enc_ctx,&resample_context) < 0)
			goto cleanup;

		if (init_fifo(&fifo,stream_ctx[audioIndex].enc_ctx) < 0 )
			goto cleanup;
	}
	if (videoIndex >= 0){
		if (init_repixel(stream_ctx[videoIndex].dec_ctx, stream_ctx[videoIndex].enc_ctx,&repixel_context) < 0)
			goto cleanup;
	}
	while (true){
		AVPacket packet;
		init_packet(&packet);
		
		ret = av_read_frame(ifmt_ctx, &packet);
		if (ret < 0){
			if (ret == AVERROR_EOF){
				finished = 1;
				av_log(NULL, AV_LOG_INFO, "read frame AVERROR_EOF\n");
				break;	
			}
			else
				av_log(NULL, AV_LOG_INFO, "Cannot to read frame\n");
		}
		
		
		stream_index = packet.stream_index;
		type = ifmt_ctx -> streams[packet.stream_index] -> codecpar -> codec_type;
		//解码
		if (type == AVMEDIA_TYPE_VIDEO){
			cout << "video decode" << endl;
			AVFrame *input_frame = NULL;
			if (init_input_frame(&input_frame) < 0)
				goto cleanup;
		
			ret = decode_media_frame(&packet,input_frame,stream_ctx[stream_index].dec_ctx, &data_preset,&finished);
			if (ret == 1){
				av_frame_free(&input_frame);
				continue;
			}
			if (ret < 0){
				av_log(NULL, AV_LOG_ERROR, "Erroring when decode_media_frame\n");
				goto cleanup;
			}
			//input_frame -> pts = input_frame -> best_effort_timestamp;
			if (data_preset){
				//先进行像素的转换
				AVFrame *out_frame;
				if(init_output_frame(&out_frame, stream_ctx[stream_index].enc_ctx,0) < 0)
					goto cleanup;	
				int len = sws_scale(repixel_context,input_frame -> data, input_frame -> linesize,0,
						stream_ctx[stream_index].dec_ctx -> height,out_frame -> data, out_frame -> linesize);	
				if (len <=  0){
					av_log(NULL, AV_LOG_ERROR, "Erroring when video repixel\n");
					av_frame_free(&input_frame);
					av_frame_free(&out_frame);
					goto cleanup;
				}
				//out_frame -> pts = input_frame -> pts;
				//if (out_frame -> pts == AV_NOPTS_VALUE)
				//	out_frame -> pts = 0;
				//进行编码
				av_frame_free(&input_frame);
				int data_written;
				if (encode_media_frame(out_frame,ofmt_ctx,stream_ctx[stream_index].enc_ctx,&data_written,stream_index) < 0)
					goto cleanup;
				av_frame_free(&out_frame);
			}
		}else if(type == AVMEDIA_TYPE_AUDIO) {
			cout << "audio decode" << endl;
			AVFrame *input_frame = NULL;
			if (init_input_frame(&input_frame) < 0)
				goto cleanup;
			
			ret = decode_media_frame(&packet,input_frame,stream_ctx[stream_index].dec_ctx, &data_preset,&finished);
			if (ret == 1){
				av_frame_free(&input_frame);
				continue;
			}
			if (ret < 0){
				av_log(NULL, AV_LOG_ERROR, "Erroring when audio decode_media_frame\n");
				goto cleanup;
			}
			if (data_preset){
				//先进行音频的重采集
				if (read_decode_convert_and_store(input_frame,fifo,stream_ctx[stream_index].dec_ctx, stream_ctx[stream_index].enc_ctx,resample_context) < 0){
					goto cleanup;
				}
				const int output_frame_size = stream_ctx[stream_index].enc_ctx -> frame_size;
				while(av_audio_fifo_size(fifo) >= output_frame_size || (finished && av_audio_fifo_size(fifo) >0)){
					//编码
					if (load_encode_and_write(fifo,ofmt_ctx, stream_ctx[stream_index].enc_ctx, stream_index) < 0)
						goto cleanup;
				}	
			}
		}
	
	}
	if (finished){
		//flush
		int data_written;
		if (videoIndex >= 0){
			do{
				data_written = 0;
				if (encode_media_frame(NULL, ofmt_ctx,stream_ctx[videoIndex].enc_ctx,&data_written,videoIndex) < 0)
					goto cleanup;
				av_log(NULL, AV_LOG_INFO, "flush video encoder data\n");
			}while(data_written);
		}
		if (audioIndex >= 0){
			do{
				data_written = 0;
				if (encode_media_frame(NULL, ofmt_ctx,stream_ctx[audioIndex].enc_ctx,&data_written,audioIndex) < 0)
					goto cleanup;
				av_log(NULL, AV_LOG_INFO, "flush audio encoder data\n");
			}while(data_written);
		}
	}
	av_write_trailer(ofmt_ctx);
	ret = 0;

cleanup:
	if(fifo)
		av_audio_fifo_free(fifo);
	swr_free(&resample_context);
	sws_freeContext(repixel_context);

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
