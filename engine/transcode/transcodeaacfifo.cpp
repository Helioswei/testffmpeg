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
	(*frame) -> channel_layout = enc_ctx -> channel_layout;
	(*frame) -> nb_samples = frame_size;
	(*frame) -> sample_rate = enc_ctx -> sample_rate;

	error = av_frame_get_buffer(*frame, 0);
	if (error < 0){
		av_log(NULL, AV_LOG_ERROR,"Could not allocate output frame samples");
		av_frame_free(frame);
		return error;
	}
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
static int init_input_frame(AVFrame **frame)
{
    if (!(*frame = av_frame_alloc())) {
        av_log(NULL, AV_LOG_ERROR, "Could not allocate input frame\n");
        return AVERROR(ENOMEM);
    }
    return 0;
}
static int decode_audio_frame(AVFrame *frame,
                              AVFormatContext *input_format_context,
                              AVCodecContext *input_codec_context,
                              int *data_present, int *finished)
{
    /* Packet used for temporary storage. */
    AVPacket input_packet;
    int error;
    init_packet(&input_packet);
	static int count = 0;
	ofstream myout;
	myout.open("/root/debug",ios::app);
    /* Read one audio frame from the input file into a temporary packet. */
    if ((error = av_read_frame(input_format_context, &input_packet)) < 0) {
        /* If we are at the end of the file, flush the decoder below. */
        if (error == AVERROR_EOF){
            *finished = 1;
		}
        else {
            av_log(NULL, AV_LOG_ERROR, "Could not read frame\n");
            return error;
        }
		cout << "finished : " << *finished << endl;
		
    }
	if (input_packet.stream_index != audioIndex){
		error = 0;
		goto cleanup;
	}
	//av_packet_rescale_ts(&input_packet, input_format_context -> streams[audioIndex] -> time_base, input_codec_context -> time_base);
    /* Send the audio frame stored in the temporary packet to the decoder.
     * The input audio stream decoder is used to do this. */
    if ((error = avcodec_send_packet(input_codec_context, &input_packet)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not send packet for decoding\n");
        return error;
    }

    /* Receive one frame from the decoder. */
    error = avcodec_receive_frame(input_codec_context, frame);
    /* If the decoder asks for more data to be able to decode a frame,
     * return indicating that no data is present. */
    if (error == AVERROR(EAGAIN)) {
        error = 0;
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
		//cout << frame -> data[1] << endl;
		//myout << frame -> pts << endl;
		//mypts = frame -> best_effort_timestamp;
        goto cleanup;
    }

cleanup:
    av_packet_unref(&input_packet);
    return error;

}
static int convert_samples(const uint8_t **input_data,
                           uint8_t **converted_data, const int frame_size,
                           SwrContext *resample_context)
{
    int error;

    /* Convert the samples using the resampler. */
    if ((error = swr_convert(resample_context,
                             converted_data, frame_size,
                             input_data    , frame_size)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not convert input samples\n");
        return error;
    }

    return 0;
}

static int read_decode_convert_and_store(AVAudioFifo *fifo,
                                         AVFormatContext *input_format_context,
                                         AVCodecContext *input_codec_context,
                                         AVCodecContext *output_codec_context,
                                         SwrContext *resampler_context,
                                         int *finished)
{
    /* Temporary storage of the input samples of the frame read from the file. */
    AVFrame *input_frame = NULL;
	AVFrame *output_frame = NULL;
    /* Temporary storage for the converted input samples. */
    //uint8_t **converted_input_samples = NULL;
    int data_present = 0;
    int ret = AVERROR_EXIT;
	uint8_t * m_ain[32];

    /* Initialize temporary storage for one input frame. */
    if (init_input_frame(&input_frame) < 0)
        goto cleanup;
    /* Decode one frame worth of audio samples. */
    if (decode_audio_frame(input_frame, input_format_context,
                           input_codec_context, &data_present, finished) < 0)
        goto cleanup;
    /* If we are at the end of the file and there are no more samples
     * in the decoder which are delayed, we are actually finished.
     * This must not be treated as an error. */
    if (*finished) {
        ret = 0;
        goto cleanup;
    }
    /* If there is decoded data, convert and store it. */
    if (data_present) {
		if (av_sample_fmt_is_planar(input_codec_context -> sample_fmt)){
			for(int i = 0; i < input_frame -> channels;i++)
				m_ain[i] = input_frame -> data[i];
		} else{
			m_ain[0] = input_frame -> data[0];
		}
		//对音频进行重新采样
		if (init_output_frame(&output_frame, output_codec_context, output_codec_context->frame_size) < 0)
			goto cleanup;

		int len  = swr_convert(resampler_context,output_frame -> data, output_frame -> nb_samples,
								(const uint8_t **)m_ain, input_frame -> nb_samples);
	
		//if (output_frame -> frame_size <= fifo_size){
		//	int len  = swr_convert(resampler_context,output_frame -> data, output_frame -> nb_samples,
	//							NULL, 0);
		//}	
		 if (len < 0){
			av_log(NULL, AV_LOG_ERROR, "Could not convert input samples\n");
			goto cleanup;
		 }
		 //将重采样后的数据放入队列中,注意在这里len和input->nb_samples的取值，必须使用len的，重采样后的！
		 //！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！
        if (add_samples_to_fifo(fifo, (uint8_t **)output_frame -> data,len) < 0)
        //if (add_samples_to_fifo(fifo, (uint8_t **)output_frame -> data,input_frame -> nb_samples) < 0)
            goto cleanup;
		//重采样中可能有缓存	
		while(swr_get_out_samples(resampler_context,0) >= output_frame -> nb_samples){
			//有缓存，则取出缓存
			AVFrame *output_frame;
			if (init_output_frame(&output_frame, output_codec_context, output_codec_context->frame_size) < 0)
				goto cleanup;
			int len  = swr_convert(resampler_context,output_frame -> data, output_frame -> nb_samples,
								NULL, 0);
			if (len < 0){
				av_log(NULL, AV_LOG_ERROR, "Could not convert input cache samples\n");
				goto cleanup;
			}
			if (add_samples_to_fifo(fifo, (uint8_t **) output_frame -> data, len) < 0)
				goto cleanup;
			av_frame_free(&output_frame);
		
		}
		
    }
    ret = 0;

cleanup:
    av_frame_free(&input_frame);
	if (output_frame)
		av_frame_free(&output_frame);
    return ret;
}



static int encode_audio_frame(AVFrame *frame, AVFormatContext *ofmt_ctx, AVCodecContext *enc_ctx, int *data_preset){
	AVPacket output_packet;
	int error;
	ofstream myout;
	myout.open("/root/debug",ios::app);
	init_packet(&output_packet);
	if (frame){
		frame -> pts = mypts;
		mypts+= frame -> nb_samples;
		myout << frame -> pts << endl;	
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
	//av_packet_rescale_ts(&output_packet, enc_ctx -> time_base, ofmt_ctx ->streams[0] -> time_base);
	//getchar();	
	if (*data_preset&&(error = av_write_frame(ofmt_ctx,&output_packet)) < 0){
		av_log(NULL, AV_LOG_ERROR, "Could not write frame\n");
		goto cleanup;
	}
	
cleanup:
	av_packet_unref(&output_packet);
	return error;	
}

static int load_encode_and_write(AVAudioFifo *fifo,
                                 AVFormatContext *output_format_context,
                                 AVCodecContext *output_codec_context)
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
    if (encode_audio_frame(output_frame, output_format_context,
                           output_codec_context, &data_written) < 0) {
        av_frame_free(&output_frame);
        return AVERROR_EXIT;
    }
    av_frame_free(&output_frame);
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
	char * filename = "/root/source_video/video/flv.flv";
	//char * filename = "/root/source_video/codetest.avi";
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
		av_log(NULL, AV_LOG_ERROR, "swr_alloc_set_opts failed\n");		  return -1;
	}
	ret = swr_init(actx);
	if (ret < 0){
		av_log(NULL, AV_LOG_ERROR, "swr_init failed\n");	
		return ret;
	}
	//初始化重采样后存储的AVFrame
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


	//初始化一个先进先出的缓存队列
	AVAudioFifo *fifo = av_audio_fifo_alloc(enc_ctx -> sample_fmt,enc_ctx -> channels, enc_ctx -> frame_size);
	while(true){
		//获取编码每帧的最大取样数
		const int output_frame_size = enc_ctx -> frame_size;
		int finished = 0;
		while( av_audio_fifo_size(fifo) < output_frame_size){
			if (read_decode_convert_and_store(fifo, ifmt_ctx,dec_ctx,enc_ctx,actx,&finished) < 0)
				goto cleanup;
			if(finished)	
				break;
		}
		while(av_audio_fifo_size(fifo) >= output_frame_size || (finished&& av_audio_fifo_size(fifo) > 0)){
			//说明帧已经存在了队列中，现在去出来进行编码，并且写入文件
			if(load_encode_and_write(fifo, ofmt_ctx, enc_ctx) < 0)
				goto cleanup;	
			av_log(NULL, AV_LOG_DEBUG, "load_encode_and_write\n");	
		}
		if (finished){
			int data_written;
			do{
				data_written = 0;
				if (encode_audio_frame(NULL, ofmt_ctx,enc_ctx,&data_written) < 0)
					goto cleanup;
				av_log(NULL, AV_LOG_DEBUG, "flush encoder data\n");	
				
			}while(data_written);
			break;
		}
	}
	
	
	ret = av_write_trailer(ofmt_ctx);	
	if (ret < 0){
		av_log(NULL, AV_LOG_ERROR, "error when av_write_trailer\n");
	}
	
	//av_frame_free(&areframe);
	
cleanup:
	if (fifo)
		av_audio_fifo_free(fifo);
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
