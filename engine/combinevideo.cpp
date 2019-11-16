#include "./common.h"

#define LOG(ret, message)                    \
    if ((ret) < 0) {                         \
	av_log(NULL, AV_LOG_ERROR, message); \
	return ret;                          \
    }
typedef struct FileInfoStruct{
	const char* path;
	int width;
	int height;
	int64_t duration;
	int eof;
	AVFrame *audioFrame;
	AVFrame *videoFrame;

	AVStream *audioStream;
	AVStream *videoStream;

	AVFormatContext *formatContext;

	int audioCodecEof;
	AVCodecContext *audioCodecCtx;
	
	int videoCodecEof;
	AVCodecContext *videoCodecCtx;
#ifdef USE_AV_FILTER
	AVFilterContext *buffersink_ctx;
	AVFilterContext *buffersrc_ctx;
	AVFilterGraph *videoFilter;
#else
	SwsContext *videoScalerCtx;
#endif
	int64_t audioPTS;
	SwrContext *audioResampleCtx;
}FileInfoStruct;

AVFormatContext *ofmt_ctx;
FileInfoStruct *inputStruct = new FileInfoStruct;
FileInfoStruct *inputStruct1 = new FileInfoStruct;
FileInfoStruct *outputStruct = new FileInfoStruct;

int64_t outputDuration = 0;

int64_t audioPTSStride = 0;
int64_t audioDTSStride = 0;
int64_t videoPTSStride = 0;
int64_t videoDTSStride = 0;

int64_t audioPrePTS = 0;
int64_t audioPreDTS = 0;
int64_t videoPrePTS = 0;
int64_t videoPreDTS = 0;

int currentDemuxIndex = 0;
int64_t lastCallbackPTS = 0;
void init() {
    av_log_set_level(AV_LOG_DEBUG);
    av_register_all();
}


int OpenInputDecoder(FileInfoStruct *input, enum AVMediaType type);

int OpenInput( char *filename, FileInfoStruct *fileinfo){
	int nRC = 0;
	AVCodec *dec = NULL, *enc = NULL;
	//设置PTSStride，不知道为啥？？？？？
	audioPTSStride = audioPrePTS + 10240;
	audioDTSStride = audioPreDTS + 10240;
	videoPTSStride = videoPrePTS + 10000;
	videoDTSStride = videoPreDTS + 10000;	

	fileinfo -> path = filename;
	if (NULL == fileinfo -> path){
		av_log(NULL, AV_LOG_ERROR, "fileinfo -> path is NULL\n");
		nRC = -1;
		goto ERROR;
	}
	//1，打开输入文件的文件头
	nRC = avformat_open_input(&fileinfo -> formatContext, fileinfo -> path, NULL, NULL);
	if (nRC < 0){
		av_log(NULL, AV_LOG_ERROR, "Could not open input file '%s' nRC %d!!!\n", fileinfo -> path, nRC);
	}
	
	//2，查找视频流信息
	nRC = av_find_best_stream(fileinfo -> formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
	if (nRC < 0){
		av_log(NULL, AV_LOG_ERROR, "Cannot find video stream in %s\n", fileinfo -> path);
		avformat_close_input(&fileinfo -> formatContext);
		fileinfo -> formatContext = NULL;
		goto ERROR;
	} else {
		fileinfo -> videoStream = fileinfo -> formatContext -> streams[nRC];
	}
	//3，查找音频流信息
	nRC = av_find_best_stream(fileinfo -> formatContext, AVMEDIA_TYPE_AUDIO, -1, -1, &dec, 0);
	if (nRC < 0){
		av_log(NULL, AV_LOG_ERROR, "Cannot find audio stream in %s\n", fileinfo -> path);
		avformat_close_input(&fileinfo -> formatContext);
		fileinfo -> formatContext = NULL;
		goto ERROR;
	} else {
		fileinfo -> audioStream = fileinfo -> formatContext -> streams[nRC];
	}
	//4,存储视频的播放时长,转换为秒为单位
	if (avformat_find_stream_info(fileinfo -> formatContext, NULL) < 0){
		av_log(NULL, AV_LOG_ERROR, "find stream failed \n");
	}
	fileinfo -> duration = av_rescale(fileinfo -> formatContext -> duration, 1000, AV_TIME_BASE);
	outputDuration += fileinfo -> duration;
	av_log(NULL, AV_LOG_INFO, "Debug ..... outputDuration %lld inputDuration %lld duration %lld \n", outputDuration, fileinfo -> duration, fileinfo -> formatContext -> duration);

	nRC = 0;
	av_dump_format(fileinfo -> formatContext , 0, fileinfo -> path, 0);

	//5,对音频进行解码设置
	nRC = OpenInputDecoder(fileinfo, AVMEDIA_TYPE_AUDIO);
	if (nRC < 0){
		av_log(NULL, AV_LOG_ERROR, "Cannot open inputStruct audio decoder!\n");
		goto ERROR;
	}
	fileinfo -> audioFrame = av_frame_alloc();
	if (NULL == fileinfo -> audioFrame){
		av_log(NULL, AV_LOG_ERROR, "Cannot alloc input frame for audio decoder!\n");
		goto ERROR;
	}
	//6,对视频进行解码设置
	nRC = OpenInputDecoder(fileinfo, AVMEDIA_TYPE_VIDEO);
	if (nRC < 0){
		av_log(NULL, AV_LOG_ERROR, "Cannot open inputStruct video decoder!\n");
		goto ERROR;
	}
	fileinfo -> videoFrame = av_frame_alloc();
	if (NULL == fileinfo -> videoFrame){
		av_log(NULL, AV_LOG_ERROR, "Cannot alloc input frame for video decoder!\n");
		goto ERROR;
	}

ERROR:
	return nRC;
}



int OpenInputDecoder(FileInfoStruct *input, enum AVMediaType type){
	if (NULL == input){
		av_log(NULL, AV_LOG_ERROR, "OpenInputDecoder width NULL parameter!\n");
		return -1;
	}
	int nRC = 0;
	AVDictionary *opts = NULL;
	AVCodecContext **dec_ctx;
	AVStream *st = NULL;
	AVCodec *dec = NULL;

	if (AVMEDIA_TYPE_AUDIO == type){
		st = input -> audioStream;
		dec_ctx = &input -> audioCodecCtx;
	} else if (AVMEDIA_TYPE_VIDEO == type){
		st = input -> videoStream;
		dec_ctx = &input -> videoCodecCtx;
	}

	if (NULL != *dec_ctx){
		av_log(NULL, AV_LOG_INFO, "%s video decoder opened already\n", input -> path);
		return 0;
	}

	if (NULL == st){
		av_log(NULL, AV_LOG_ERROR, "OpenInputDecoder Cannot find input stream!\n");
		return -1;
	}
	dec = avcodec_find_decoder(st -> codecpar -> codec_id);
	if (NULL == dec){
		av_log(NULL, AV_LOG_ERROR, "Failed to find %s codec\n", av_get_media_type_string(type));
		return AVERROR(EINVAL);
	}

	*dec_ctx = avcodec_alloc_context3(dec);
	if (NULL == *dec_ctx){
		av_log(NULL, AV_LOG_ERROR, "Failed to allocate the %s codec context\n", av_get_media_type_string(type));
		return AVERROR(ENOMEM);
	}

	nRC = avcodec_parameters_to_context(*dec_ctx, st -> codecpar);
	if (nRC < 0){
		av_log(NULL, AV_LOG_ERROR, "Failed to copy %s codec parameters to decoder context\n", av_get_media_type_string(type));
		return nRC;
	}

	av_dict_set(&opts, "refcounted_frames", "0", 0);
	nRC = avcodec_open2(*dec_ctx, dec, &opts);
	if (nRC < 0){
		av_log(NULL, AV_LOG_ERROR, "Failed to open %s codec\n", av_get_media_type_string(type));
		return nRC;
	}
	return 0;
}

int OpenOutput(char *filename, FileInfoStruct *fileinfo){
	int nRC = 0;
	AVCodec *enc = NULL;

	if (0 == currentDemuxIndex){
		audioPTSStride = 0;
		audioDTSStride = 0;
		videoPTSStride = 0;
		videoDTSStride = 0;
	}
	fileinfo -> path = filename;
	// open output below
	avformat_alloc_output_context2(&fileinfo -> formatContext , NULL, NULL, fileinfo -> path);
	if (NULL == fileinfo -> formatContext){
		av_log(NULL, AV_LOG_ERROR, "Cannot open output %s \n", fileinfo -> path);
		nRC = -1;
		goto ERROR;
	}
	//Audio stream
	fileinfo -> audioStream = avformat_new_stream(fileinfo -> formatContext, NULL);
	if (NULL == fileinfo -> audioStream){
		av_log(NULL, AV_LOG_ERROR, "Cannot new aduio stream for output!");
		nRC = -1;
		goto ERROR;
	}
	enc = avcodec_find_encoder(AV_CODEC_ID_AAC);
	if(NULL == enc){
		av_log(NULL, AV_LOG_ERROR, "Cannot alloc aduio AAC encoder context!\n");
		nRC = -1;
		goto ERROR;
	}else {
		fileinfo -> audioCodecCtx = avcodec_alloc_context3(enc);
		if (NULL == fileinfo -> audioCodecCtx){
			av_log(NULL, AV_LOG_ERROR, "Cannot alloc audio AAC encoder context!");
			nRC = -1;
			goto ERROR;
		}
		fileinfo -> audioCodecCtx -> channels = 2;
		fileinfo -> audioCodecCtx -> channel_layout = av_get_default_channel_layout(fileinfo -> audioCodecCtx -> channels);
		fileinfo -> audioCodecCtx -> sample_fmt = enc -> sample_fmts[0];
		fileinfo -> audioCodecCtx -> sample_rate = 44100;
		fileinfo -> audioCodecCtx -> bit_rate = 64000; 
		fileinfo -> audioCodecCtx -> strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL ;
		fileinfo -> audioCodecCtx -> time_base.den = 44100;
		fileinfo -> audioCodecCtx -> time_base.num = 1;

		if (NULL != fileinfo -> formatContext -> oformat && fileinfo -> formatContext ->oformat -> flags & AVFMT_GLOBALHEADER)
			fileinfo -> audioCodecCtx -> flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		nRC = avcodec_open2(fileinfo -> audioCodecCtx , enc, NULL);
		if (0 > nRC){
			av_log(NULL, AV_LOG_ERROR, "Cannot open audio output codec  \n");
			goto ERROR;
		}
		nRC = avcodec_parameters_from_context(fileinfo -> audioStream -> codecpar, fileinfo -> audioCodecCtx);
		if (0 > nRC){
			av_log(NULL, AV_LOG_ERROR, "Could not initialize audio stream parameters , nRC %d", nRC);
			goto ERROR;
		}
		//初始化audioFrame
		fileinfo -> audioFrame = av_frame_alloc();
		if (NULL == fileinfo -> audioFrame){
			av_log(NULL, AV_LOG_ERROR, "Cannot alloc frame for audio \n");
			goto ERROR;
		}
		fileinfo -> audioFrame -> nb_samples = outputStruct -> audioCodecCtx -> frame_size;
		fileinfo -> audioFrame -> channel_layout = outputStruct -> audioCodecCtx -> channel_layout; 
		fileinfo -> audioFrame -> format = outputStruct -> audioCodecCtx -> sample_fmt; 
		fileinfo -> audioFrame -> sample_rate  = outputStruct -> audioCodecCtx -> sample_rate; 
		nRC = av_frame_get_buffer(fileinfo -> audioFrame, 0);
		if (0 > nRC){
			av_log(NULL, AV_LOG_ERROR, "Cannot get buffer for audio %d \n", nRC);
			goto ERROR;
		}
	}

	fileinfo -> audioStream -> codecpar -> codec_tag = 0;

	//video stream
	fileinfo -> videoStream = avformat_new_stream(fileinfo -> formatContext, NULL);
	if (NULL == fileinfo -> videoStream){
		av_log(NULL, AV_LOG_ERROR, "Cannot new audio stream for output!");
		nRC = -1;
		goto ERROR;
	}
	enc = avcodec_find_encoder(AV_CODEC_ID_H264);
	if (NULL != enc){
		fileinfo -> videoCodecCtx = avcodec_alloc_context3(enc);
		if (NULL == fileinfo -> videoCodecCtx){
			av_log(NULL, AV_LOG_ERROR, "Cannot alloc video encdeor!\n");	
			nRC = -1;
			goto ERROR;
		}
		fileinfo -> videoCodecCtx -> height = 320;
		fileinfo -> videoCodecCtx -> width = 640;
		fileinfo -> videoCodecCtx -> sample_aspect_ratio = inputStruct -> videoCodecCtx ->sample_aspect_ratio;
		fileinfo -> videoCodecCtx -> pix_fmt = AV_PIX_FMT_YUV420P;
		fileinfo -> videoCodecCtx -> time_base = av_inv_q (inputStruct -> videoStream -> r_frame_rate);
		if (NULL != fileinfo -> formatContext -> oformat && fileinfo -> formatContext -> oformat -> flags & AVFMT_GLOBALHEADER)
			fileinfo -> videoCodecCtx -> flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		nRC = avcodec_open2(fileinfo -> videoCodecCtx, enc, NULL);
		if (0 > nRC){
			av_log(NULL,  AV_LOG_ERROR, "Cannot open output encoder\n");
			avcodec_free_context(&fileinfo -> videoCodecCtx);
			fileinfo ->videoCodecCtx = NULL;
			goto ERROR;
		}

		nRC = avcodec_parameters_from_context(fileinfo -> videoStream -> codecpar, fileinfo -> videoCodecCtx);
		if (nRC < 0){
			av_log(NULL, AV_LOG_ERROR, "Cannot alloc video encoder!\n");
			nRC = -1;
			goto ERROR;
		}

	}else {
		av_log(NULL, AV_LOG_ERROR, "Cannot find video encoder id %d\n",fileinfo -> videoCodecCtx -> codec_id);	
		nRC = -1;
		goto ERROR;
	}
	fileinfo -> videoFrame = av_frame_alloc();
	if (NULL == fileinfo -> videoFrame){
		av_log(NULL, AV_LOG_ERROR, "Cannot alloc frame for video svaler output!\n");
		goto ERROR;
	}
	fileinfo -> videoFrame -> format = fileinfo -> videoCodecCtx -> pix_fmt;
	fileinfo -> videoFrame -> width = fileinfo -> videoCodecCtx -> width;
	fileinfo -> videoFrame -> height = fileinfo -> videoCodecCtx -> height;
	nRC = av_frame_get_buffer(fileinfo -> videoFrame, 32);
	if (0 > nRC){
		av_log(NULL, AV_LOG_ERROR, "Cannot get buffer for video frame \n");
		goto ERROR;
	}

	fileinfo -> videoStream -> codecpar -> codec_tag = 0;
	av_dump_format(fileinfo -> formatContext, 0, fileinfo -> path, 1);

	if (!(fileinfo -> formatContext -> flags & AVFMT_NOFILE)){
		nRC = avio_open(&fileinfo -> formatContext ->pb, fileinfo -> path, AVIO_FLAG_WRITE);

		if (0 > nRC){
			av_log(NULL, AV_LOG_ERROR, "Could not open output file %s\n", fileinfo -> path);
			goto ERROR;
		}
	}

ERROR :
	return nRC;

}

int ReadPacket(FileInfoStruct *fileinfo, AVPacket *pkt){
	if (NULL == pkt){
		av_log(NULL, AV_LOG_ERROR, "ReadPacket with NUll pkt");
		return -1;
	}
	int nRC = 0;
	int64_t pts2ms;
	if (0 != inputStruct -> eof || 0 != inputStruct1 -> eof){
		nRC = AVERROR_EOF;
		goto ERROR;
	}
	nRC = av_read_frame(fileinfo ->formatContext, pkt);

	if(0 > nRC){
		//nRC < 0说明读取packet失败
		if (AVERROR_EOF == nRC){
			av_log(NULL, AV_LOG_ERROR, "av_read_frame reached EOS:");
			fileinfo -> eof = 1;
			pkt -> stream_index = -1;//Dropped this stream packet
		}else {
			av_log(NULL, AV_LOG_ERROR, "av_read_frame error \n");
		}
		goto ERROR;
	}

	if (pkt -> stream_index == fileinfo -> audioStream -> index){
		pkt -> stream_index = outputStruct -> audioStream -> index;
	} else if ( pkt -> stream_index == fileinfo -> videoStream ->index){
		pts2ms = pkt -> pts * av_q2d(fileinfo -> videoStream -> time_base)*1000;
		pkt -> stream_index = outputStruct -> videoStream ->index;
	}else {
		pkt -> stream_index = -1;//Dropped this stream packet
	}

ERROR:
	//Callback to App side to inform progress
	//这些不懂什么意思
	if (fileinfo -> duration > 0){
		if ((pts2ms - lastCallbackPTS) * 100 / fileinfo -> duration > 1){
			lastCallbackPTS = pts2ms;
		}
		if (AVERROR_EOF == nRC){
			lastCallbackPTS = 0;
		}
	
	}
	return nRC;
}

void AdjustTimestamp(FileInfoStruct *fileinfo,AVPacket *pkt){
	if (NULL == fileinfo || NULL == pkt){
		av_log(NULL, AV_LOG_ERROR, "AdjustTimestamp with NUll parameter splicer %p pkt %p",fileinfo,pkt);
		return;
	}

	AVStream *inStream, *outStream;

	if (pkt -> stream_index == outputStruct -> audioStream ->index){
		inStream = fileinfo ->audioStream;
		outStream = outputStruct -> audioStream;
	} else if (pkt -> stream_index == outputStruct -> videoStream -> index){
		inStream = fileinfo -> videoStream;
		outStream = outputStruct -> videoStream;
	}

	pkt -> pts = av_rescale_q_rnd(pkt-> pts, inStream -> time_base, outStream -> time_base, (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
	pkt -> dts = av_rescale_q_rnd(pkt-> dts, inStream -> time_base, outStream -> time_base, (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));

	if (pkt -> stream_index == outputStruct -> audioStream -> index){
		pkt -> pts += audioPTSStride;
		pkt -> dts += audioDTSStride;
	} else {
		pkt -> pts += videoPTSStride;
		pkt -> dts += videoDTSStride;
	}

	return;


}
int AudioTranscoding(FileInfoStruct *fileinfo ,AVPacket *in_pkt, AVPacket *out_pkt)
{
	if (NULL == fileinfo || NULL == in_pkt || NULL == out_pkt){
		av_log(NULL, AV_LOG_ERROR,"AudioTranscoding  with NUll parameter");
		return -1;
	}

	int nRC = 0;
	AVPacket **pkt = &in_pkt;
	
	//解码
	if (0 < (*pkt) -> size && NULL != (*pkt) -> data){
		int got_frame;
		nRC = avcodec_decode_audio4(fileinfo -> audioCodecCtx, fileinfo -> audioFrame, &got_frame, *pkt);
		if (0 > nRC){
			av_log(NULL, AV_LOG_ERROR, "Decoding audio failed\n");
			goto ERROR;
		}
		if (got_frame)	{
			//解码成功进行编码
			int got_output;
			nRC = avcodec_encode_audio2(outputStruct -> audioCodecCtx, out_pkt, fileinfo ->audioFrame, &got_output);	
			if (0 > nRC){
				av_log(NULL, AV_LOG_ERROR , "Encoder audio failed\n");
				goto ERROR;
			}
			if (got_output){
				av_log(NULL, AV_LOG_INFO, "Encoder audio success\n");
			}
		
		
		}
	
	}
	out_pkt -> stream_index = outputStruct -> audioStream -> index;
ERROR:
	return nRC;
}


int VideoTranscoding(FileInfoStruct *fileinfo, AVPacket *in_pkt, AVPacket *out_pkt)
{
	if (NULL == fileinfo || NULL == in_pkt || NULL == out_pkt){
		av_log(NULL, AV_LOG_ERROR , "VideoTranscoding with NULL parameter\n");
		return -1;
	}

	int nRC = 0;
	int got_frame;
	AVPacket **pkt = &in_pkt;
	//解码
	if (NULL == *pkt || (0 < (*pkt) -> size && NULL != (*pkt) -> data)){
		nRC = avcodec_decode_video2(fileinfo -> videoCodecCtx, fileinfo -> videoFrame, &got_frame, *pkt);
		if (NULL == *pkt)
			av_log(NULL, AV_LOG_ERROR, "Decoder video avcode_decode_video2  with null");
		if (0 > nRC){
			//说明解码失败
			av_log(NULL,AV_LOG_ERROR, "Decoder video Error while sending a packet to the decoder \n");
			goto ERROR;
		}
		if (got_frame){
			//解码成功进行编码
			int got_packet = 0;
			outputStruct -> videoFrame = fileinfo -> videoFrame;
			nRC = avcodec_encode_video2(outputStruct -> videoCodecCtx,out_pkt,outputStruct -> videoFrame, &got_packet);
			if (0 > nRC){
				av_log(NULL, AV_LOG_ERROR, "Encodeing video failed\n");
				goto ERROR;	
			}	
			if (got_packet){
				av_log(NULL, AV_LOG_INFO, "Encode video success\n");
			}
		
		
		}
	}
//	nRC = avcodec_send_frame(outputStruct -> videoCodecCtx, fileinfo -> videoFrame);
//	if (AVERROR(EAGAIN) == nRC){
//		nRC = 0;
//		av_log(NULL, AV_LOG_ERROR, "Encoder video avcodec_send_frame reveived EAGAIN\n");
//		goto ERROR;
//	
//	} else if ( AVERROR_EOF == nRC){
//		av_log(NULL, AV_LOG_ERROR, "Encoder video  avcodec_send_frame received AVERROR_EOF");
//		nRC = 0;
//	} else if (0 > nRC){
//		av_log(NULL, AV_LOG_ERROR, "Encoder video Error while sending a frame to video encoder nRC");
//		goto ERROR;
//	}
//	
//	nRC = avcodec_receive_packet(outputStruct -> videoCodecCtx, out_pkt);
//	if (AVERROR(EAGAIN) == nRC){
//		nRC = 0;
//		av_log(NULL, AV_LOG_ERROR, "Encoder video avcodec_receive_packet reveived EAGAIN\n");
//		goto ERROR;
//	
//	} else if ( AVERROR_EOF == nRC){
//		av_log(NULL, AV_LOG_ERROR, "Encoder video  avcodec_receive_packet received AVERROR_EOF");
//		nRC = 0;
//	} else if (0 > nRC){
//		av_log(NULL, AV_LOG_ERROR, "Encoder video Error while sending a frame to video encoder nRC %s",nRC);
//		goto ERROR;
//	}
	out_pkt -> stream_index = outputStruct -> videoStream -> index;

ERROR:
	return nRC;

}

int MuxingPacket(AVPacket *pkt){
	int nRC = av_interleaved_write_frame(outputStruct -> formatContext, pkt);
   if (0 > nRC)	{
		av_log(NULL, AV_LOG_ERROR, "Error during muxing pkt\n");
		goto ERROR;
   }

ERROR:
   return nRC;

}

int Remuxing(){
	if (NULL == inputStruct || NULL == inputStruct1){
		av_log(NULL, AV_LOG_ERROR, "Remuxing without inpuyt struct");
		return -1;
	}
	if (NULL == outputStruct || NULL == outputStruct -> formatContext){
		av_log(NULL, AV_LOG_ERROR, "Remuxing without output struct!");
		return -1;
	}
	int nRC = 0, readReturn = 0, pkt_type = 0;

	AVPacket in_pkt;
	AVPacket out_pkt;
	AVPacket *mux_pkt = &out_pkt;
	av_init_packet(&in_pkt);
	av_init_packet(&out_pkt);

	while(true){
		//问题？会不会永远读的都是第一个packet
		nRC = ReadPacket(inputStruct,&in_pkt);
		if (0 > nRC && AVERROR_EOF != nRC){
			goto ERROR;
		}

		if (-1 == in_pkt.stream_index){
			nRC = 0;
			av_packet_unref(&in_pkt);
			//说明这个packet不能用，可以继续读下一个packet
			continue;
		}
		if (in_pkt.stream_index == outputStruct -> audioStream -> index){
			pkt_type = AVMEDIA_TYPE_AUDIO;
		} else if (in_pkt.stream_index == outputStruct -> videoStream -> index){
			pkt_type = AVMEDIA_TYPE_VIDEO;
		} else{
			pkt_type = AVMEDIA_TYPE_UNKNOWN;
		}

		if (nRC >= 0){
			//说明这个packet是好的，然后进行时间戳的转换
			AdjustTimestamp(inputStruct, &in_pkt);
			if (in_pkt.stream_index == outputStruct -> audioStream -> index){
				audioPrePTS = in_pkt.pts;
				audioPreDTS = in_pkt.dts;
			}else {
				videoPrePTS = in_pkt.pts;
				videoPreDTS = in_pkt.dts;
			}
		
		}
		readReturn = nRC;
		//获得了包，设置了pts，dts，并且记录了，然后转码
//		if(AVMEDIA_TYPE_AUDIO == pkt_type || AVERROR_EOF == readReturn){
//			//音频转码
//			nRC = AudioTranscoding(inputStruct,&in_pkt,&out_pkt);
//			pkt_type = AVMEDIA_TYPE_AUDIO;
//			av_packet_unref(&in_pkt);
//			if (nRC < 0 && AVERROR_EOF != nRC ){
//				//当前包转码失败
//				goto ERROR;
//			}
//			if (out_pkt.size > 0 && nRC != AVERROR_EOF)
//				goto MUXING;
//		}
		if (AVMEDIA_TYPE_VIDEO == pkt_type || AVERROR_EOF == readReturn){
			//视频转码
			nRC = VideoTranscoding(inputStruct,&in_pkt, &out_pkt);
			pkt_type = AVMEDIA_TYPE_VIDEO;
			av_packet_unref(&in_pkt);
			if (nRC < 0 && AVERROR_EOF != nRC)
				goto ERROR;
		}
		//转码完成，进行文件写写入
MUXING:
		if (AVMEDIA_TYPE_VIDEO == pkt_type || AVMEDIA_TYPE_AUDIO == pkt_type)
			mux_pkt = &in_pkt;
		if (0 < mux_pkt -> size){
			nRC = MuxingPacket(mux_pkt);
			av_packet_unref(mux_pkt);
		}
		mux_pkt = NULL;

	}

ERROR:
		return nRC;
}
int main(int argc, char **argv) {
    int nRC = 0;
    char * input_file = "/root/source_video/codetest.avi";
    //char * input_file1 = "/root/source_video/video/flv.flv";
    char * input_file1 = "/root/source_video/video/test.mp4";
    char *output_file = "/root/transcode.mp4";
    init();
    //打开输入的文件
	nRC = OpenInput(input_file1, inputStruct1);
	if (nRC < 0){
		av_log(NULL, AV_LOG_ERROR, "Start error during OpenInput nRC %d, the file is %s\n", nRC, input_file1);
		return nRC;
	}
    //打开输入的文件
	nRC = OpenInput(input_file, inputStruct);
	if (nRC < 0){
		av_log(NULL, AV_LOG_ERROR, "Start error during OpenInput nRC %d, the file is %s\n", nRC, input_file);
		return nRC;
	}
	//打开输出的文件
	nRC = OpenOutput(output_file, outputStruct);
	if (0 > nRC){
		av_log(NULL, AV_LOG_ERROR, "Start error during OpenOutput nRC %d \n", nRC);
		return nRC;
	}
	nRC = Remuxing();
	if (0 > nRC){
		av_log(NULL, AV_LOG_ERROR, "Start error during Remuxing nRc %d\n", nRC);
		return nRC;
	}
	return nRC;
}
