#include "./avbase.h"

#define END(res, desc)        \
    if ((res) < 0) {          \
		cout << desc << endl; \
		goto end;             \
    } \
;
#define FAIL(res, message)                   \
    if ((res) < 0) {                         \
		av_log(NULL, AV_LOG_ERROR, message); \
		goto _fail;                        \
    } \
	 

using namespace Media;
AVBase::AVBase(const string filename) : _filename(filename) {}

void AVBase::muxerMedia() {
    AVOutputFormat *ofmt = NULL;
    AVFormatContext *infmtcont_a = NULL, *infmtcont_v = NULL,
		    *outfmtcont = NULL;
    AVPacket pkt;
    string inVideo("/root/codec.h264");
    string inAudio("/root/codec.aac");
    string outMedia("/root/codec.mp4");
    int res;
    int videoIndex = -1, videoIndex_out = -1;
    int audioIndex = -1, audioIndex_out = -1;
    int64_t cur_pts_v = 0, cur_pts_a = 0;
    int frame_index = 0;
    av_register_all();
    // 1.1 打开输入的视频文件
    res = avformat_open_input(&infmtcont_v, inVideo.data(), NULL, NULL);
    END(res, "open the input file video failed");
    // 1.2 打开输入的音频文件
    res = avformat_open_input(&infmtcont_a, inAudio.data(), NULL, NULL);
    END(res, "open the input file audio failed");

    // 2.1,open the video stream  pipe
    res = avformat_find_stream_info(infmtcont_v, NULL);
    END(res, "open the video stream pipe failed");
    // 2.2, open the audio stream pipe
    res = avformat_find_stream_info(infmtcont_a, NULL);
    END(res, "open the audio stream pipe failed");

    // 3.1,init the output file
    res = avformat_alloc_output_context2(&outfmtcont, NULL, NULL,
					 outMedia.data());
    END(res, "open the output file failed");

    // 3.2,connect the AVFormatContent with AVOutputContent
    ofmt = outfmtcont->oformat;

    // video
    for (int i = 0; i < infmtcont_v->nb_streams; i++) {
	AVStream *in_stream = infmtcont_v->streams[i];
	if (in_stream->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
	    AVStream *out_stream =
		avformat_new_stream(outfmtcont, in_stream->codec->codec);
	    videoIndex = i;
	    if (!out_stream) {
		cout << "create new stream failed" << endl;
		goto end;
	    }
	    videoIndex_out = out_stream->index;
	    // copy the setting of AVCodecContent
	    res = avcodec_copy_context(out_stream->codec, in_stream->codec);
	    END(res, "copy the setting failed");
	    // seperate the header
	    out_stream->codec->codec_tag = 0;
	    if (outfmtcont->oformat->flags & AVFMT_GLOBALHEADER) {
		out_stream->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		break;  //??????????
	    }
	}
    }
    // audio
    for (int i = 0; i < infmtcont_a->nb_streams; i++) {
	AVStream *in_stream = infmtcont_a->streams[i];
	if (in_stream->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
	    AVStream *out_stream =
		avformat_new_stream(outfmtcont, in_stream->codec->codec);
	    if (!out_stream) {
		cout << "create new stream pipe failed" << endl;
		goto end;
	    }
	    audioIndex = i;
	    audioIndex_out = out_stream->index;
	    // copy the setting of AVCodecContent
	    res = avcodec_copy_context(out_stream->codec, in_stream->codec);
	    END(res, "copy the setting failed");
	    // seperate the header
	    out_stream->codec->codec_tag = 0;
	    if (outfmtcont->oformat->flags & AVFMT_GLOBALHEADER) {
		out_stream->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		break;  //????
	    }
	}
    }

    // open the output file
    if (!(ofmt->flags & AVFMT_NOFILE)) {
	res = avio_open(&outfmtcont->pb, outMedia.data(), AVIO_FLAG_WRITE);
	END(res, "open the output file failed");
    }
    // write the file header
    res = avformat_write_header(outfmtcont, NULL);
    END(res, "write the file header failed");

    // write the frame to output file
    while (true) {
	AVFormatContext *tmpfmtcont = NULL;  //????
	AVStream *in_stream = NULL, *out_stream = NULL;
	int stream_index = 0;
	//解释说通过这个判断可以写入是视频packet还是音频packet，不过还是搞不懂,保证两种同步
	res = av_compare_ts(
	    cur_pts_v, infmtcont_v->streams[videoIndex]->time_base, cur_pts_a,
	    infmtcont_a->streams[audioIndex]->time_base);
	//如果视频的时间在音频前面
	if (res <= 0) {
	    tmpfmtcont = infmtcont_v;
	    stream_index = videoIndex_out;
	    if (av_read_frame(tmpfmtcont, &pkt) >= 0) {
		do {
		    in_stream = tmpfmtcont->streams[pkt.stream_index];
		    out_stream = outfmtcont->streams[stream_index];
		    if (pkt.stream_index == videoIndex) {
				cout << "source video  pts is " << pkt.pts << endl;
			if (pkt.pts == AV_NOPTS_VALUE) {
				cout << "source video pts == AV_NOPTS_VALUE " << endl;
			    // write pts
			    AVRational time_base1 = in_stream->time_base;
			    // duration between 2 frames (us)
			    int64_t calc_duration =
				(double)AV_TIME_BASE /
				av_q2d(in_stream->r_frame_rate);
			    // Parameters
			    pkt.pts =
				(double)(frame_index * calc_duration) /
				(double)(av_q2d(time_base1) * AV_TIME_BASE);
			    pkt.dts = pkt.pts;
			    pkt.duration =
				(double)calc_duration /
				(double)(av_q2d(time_base1) * AV_TIME_BASE);
			    frame_index++;
			}
			cout << "video convert pts is " << pkt.pts << " video convert dts is " << pkt.dts << endl;
			cur_pts_v = pkt.pts;
			getchar();
			break;
		    }
		} while (av_read_frame(tmpfmtcont, &pkt) >= 0);

	    } else {
		cout << "read frame failed" << endl;
		break;
	    }
	} else {
	    //如果音频的时间在视频的前面
	    tmpfmtcont = infmtcont_a;
	    stream_index = audioIndex_out;
	    if (av_read_frame(tmpfmtcont, &pkt) >= 0) {
		do {
		    in_stream = tmpfmtcont->streams[pkt.stream_index];
		    out_stream = outfmtcont->streams[stream_index];
		    if (pkt.stream_index == audioIndex) {
				cout << "source audio  pts is " << pkt.pts << endl;
			if (pkt.pts == AV_NOPTS_VALUE) {
				cout << "source audio pts == AV_NOPTS_VALUE " << endl;
			    // write PTS
			    AVRational time_base1 = in_stream->time_base;
			    // duration between 2 frames (us)
			    int64_t calc_duration =
				(double)AV_TIME_BASE /
				av_q2d(in_stream->r_frame_rate);
			    // Parameters
			    pkt.pts =
				(double)(frame_index) * calc_duration /
				(double)(av_q2d(time_base1) * AV_TIME_BASE);
			    pkt.dts = pkt.pts;
			    pkt.duration =
				(double)calc_duration /
				(double)(av_q2d(time_base1) * AV_TIME_BASE);
			    frame_index++;
			}
			cout << "audio  convert pts is " << pkt.pts << " audio  convert dts is " << pkt.dts << endl;
			cur_pts_a = pkt.pts;
			getchar();
			break;
		    }
		} while (av_read_frame(tmpfmtcont, &pkt) >= 0);
	    } else {
		cout << "read frame failed " << endl;
		break;
	    }
	}
    cout << "source pts is " << pkt.pts << " source dts is" << pkt.dts << " source duration is " << pkt.duration << endl;
	pkt.pts = av_rescale_q_rnd(
	    pkt.pts, in_stream->time_base, out_stream->time_base,
	    (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
	pkt.dts = av_rescale_q_rnd(
	    pkt.dts, in_stream->time_base, out_stream->time_base,
	    (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
	pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base,
				    out_stream->time_base);
	pkt.pos = -1;
    cout << "convert  pts is " << pkt.pts << " convert dts is" << pkt.dts << " convert  duration is " << pkt.duration << endl;
	pkt.stream_index = stream_index;
	getchar();
	// write packet to output file
	if (av_interleaved_write_frame(outfmtcont, &pkt) < 0) {
	    cout << "write packet to output file failed" << endl;
	    break;
	}
	av_free_packet(&pkt);
    }
    av_write_trailer(outfmtcont);

end:
    avformat_close_input(&infmtcont_a);
    avformat_close_input(&infmtcont_v);
    // close open output file
    if (outfmtcont && (ofmt->flags & AVFMT_NOFILE)) {
	avio_close(outfmtcont->pb);
    }
    avformat_free_context(outfmtcont);
}

void AVBase::getInfo() {
    av_register_all();
    //为AVFormatContext分配内存并且初始化
    AVFormatContext *fmtcont = avformat_alloc_context();
    int res = avformat_open_input(&fmtcont, _filename.data(), NULL, NULL);
    if (res < 0) {
	cout << "open file failed" << endl;
	abort();
    }
    if (avformat_find_stream_info(fmtcont, NULL) < 0) {
	cout << "find stream failed" << endl;
	abort();
    }
    int v_stream_idx = -1;
    int v_coded_width = -1;
    int v_coded_height = -1;
    cout << "streams :" << fmtcont->nb_streams << endl;
    for (int i = 0; i < fmtcont->nb_streams; i++) {
	AVStream *in_stream = fmtcont->streams[i];
	const AVCodec *codec = NULL;
	const AVCodecDescriptor *codecdes = NULL;
	if (in_stream->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
	    //视频流
	    v_stream_idx = i;
	    v_coded_width = in_stream->codec->coded_width;
	    v_coded_height = in_stream->codec->coded_height;
	    cout << "width:" << in_stream->codec->width << endl;
	    cout << "height:" << in_stream->codec->height << endl;
	    codec = in_stream->codec->codec;
	    if (codec) {
		cout << "codec name:" << codec->name << endl;
		if (codec->long_name)
		    cout << "codec describe:" << codec->long_name << endl;
	    } else if (codecdes =
			   avcodec_descriptor_get(in_stream->codec->codec_id)) {
		cout << "codec name else is" << codecdes->name << endl;
		cout << "codec describe:"
		     << (codecdes->long_name ? codecdes->long_name : "unknown")
		     << endl;
	    }
	}
	if (in_stream->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
	    //音频流
	    codec = in_stream->codec->codec;
	    if (codec) {
		cout << "audio codec name :" << codec->name << endl;
		if (codec->long_name)
		    cout << "audio codec desc:" << codec->long_name << endl;
	    } else if (codecdes =
			   avcodec_descriptor_get(in_stream->codec->codec_id)) {
		cout << "audio codec name else is " << codecdes->name << endl;
		if (codecdes->long_name)
		    cout << "audio codec desc else is " << codecdes->long_name
			 << endl;
	    }

	    cout << "采样率：" << in_stream->codec->sample_rate << endl;
	    cout << "声道：" << in_stream->codec->channels << endl;
	}
    }
    //获取文件的播放时长
    if (fmtcont->duration > 0) {
	cout << "视频的时长是：" << (fmtcont->duration / 10000) / 100.0 << endl;
    }
    //测
    avformat_free_context(fmtcont);
}

//分离视音频流的信息出来
void AVBase::demuxerMedia() {
    AVFormatContext *ifmtcont = NULL, *ofmtcont_v = NULL, *ofmtcont_a = NULL;
    AVOutputFormat *ofmt_a = NULL, *ofmt_v = NULL;
    AVPacket pkt;
    int res, i;
    int videoIndex = -1, audioIndex = -1;
    int frameIndex = 0;

    string outVideo("/root/codec.h264");
    string outAudio("/root/codec.aac");
    av_register_all();
    // 1,open the input file
    res = avformat_open_input(&ifmtcont, _filename.data(), NULL, NULL);
    if (res < 0) {
	cout << "open the input file failed" << endl;
	goto end;
    }
    // 2,open the stream pipe
    res = avformat_find_stream_info(ifmtcont, NULL);
    if (res < 0) {
	cout << "open the stream pipe failed" << endl;
	goto end;
    }
    // 3,open the video output file
    res = avformat_alloc_output_context2(&ofmtcont_v, NULL, NULL,
					 outVideo.data());
    if (res < 0) {
	cout << "open the video output file failed" << endl;
	goto end;
    }
    // 3.1,将AVOutputFormat和AVFormatContext联系起来
    ofmt_v = ofmtcont_v->oformat;
    // 4, open the audio output file
    res = avformat_alloc_output_context2(&ofmtcont_a, NULL, NULL,
					 outAudio.data());
    if (res < 0) {
	cout << "open the audio output file failed" << endl;
	goto end;
    }
    // 4.1,connect the AVOutputFormat with AVFormatContext
    ofmt_a = ofmtcont_a->oformat;

    // 5,
    for (int i = 0; i < ifmtcont->nb_streams; i++) {
	AVStream *in_stream = ifmtcont->streams[i];
	AVStream *out_stream = NULL;
	AVFormatContext *tmpfmtcont = NULL;
	if (in_stream->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
	    videoIndex = i;
	    out_stream =
		avformat_new_stream(ofmtcont_v, in_stream->codec->codec);
	    tmpfmtcont = ofmtcont_v;
	} else if (in_stream->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
	    audioIndex = i;
	    out_stream =
		avformat_new_stream(ofmtcont_a, in_stream->codec->codec);
	    tmpfmtcont = ofmtcont_a;
	} else {
	    break;
	}
	if (!out_stream) {
	    cout << "create stream pipe failed" << endl;
	    goto end;
	}
	res = avcodec_copy_context(out_stream->codec, in_stream->codec);
	if (res < 0) {
	    cout << "copy the setting of AVCodecContent failed" << endl;
	    goto end;
	}
	out_stream->codec->codec_tag = 0;
	//????????????不懂
	if (tmpfmtcont->oformat->flags & AVFMT_GLOBALHEADER) {
	    out_stream->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}
    }

    // open the output file
    if (!(ofmt_v->flags & AVFMT_NOFILE)) {
	res = avio_open(&ofmtcont_v->pb, outVideo.data(), AVIO_FLAG_WRITE);
	if (res < 0) {
	    cout << "open output video failed" << endl;
	    goto end;
	}
    }
    if (!(ofmt_a->flags & AVFMT_NOFILE)) {
	res = avio_open(&ofmtcont_a->pb, outAudio.data(), AVIO_FLAG_WRITE);
	if (res < 0) {
	    cout << "open output audio failed" << endl;
	    goto end;
	}
    }

    // wirte the file header
    res = avformat_write_header(ofmtcont_v, NULL);
    if (res < 0) {
	cout << "write the output video header failed" << endl;
	goto end;
    }
    res = avformat_write_header(ofmtcont_a, NULL);
    if (res < 0) {
	cout << "write the output audio failed" << endl;
	goto end;
    }
//??????
#if USE_H264BSR
    AVBitStreamFilterContext *h264bsfs =
	av_bitstream_filter_init("h264_mp4toannexb");
#endif
    while (true) {
	AVStream *in_stream, *out_stream;
	AVFormatContext *tmpfmtcont;
	res = av_read_frame(ifmtcont, &pkt);
	cout << "index" << pkt.stream_index << endl;
	cout << "pts " << pkt.pts << " dts " << pkt.dts << endl;
	if (res < 0) {
	    cout << "read frame failed" << endl;
	    break;
	}
	in_stream = ifmtcont->streams[pkt.stream_index];
	if (pkt.stream_index == videoIndex) {
	    out_stream = ofmtcont_v->streams[0];
	    tmpfmtcont = ofmtcont_v;
#if USE_H264BSR
	    av_bitstream_filter_filter(h264bsfs, in_stream->codec, pkt.size,
				       pkt.pts);
#endif
	} else if (pkt.stream_index == audioIndex) {
	    out_stream = ofmtcont_a->streams[0];
	    tmpfmtcont = ofmtcont_a;
	} else {
	    continue;
	}

	// convert PTS/DTS
	pkt.pts = av_rescale_q_rnd(
	    pkt.pts, in_stream->time_base, out_stream->time_base,
	    (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
	pkt.dts = av_rescale_q_rnd(
	    pkt.dts, in_stream->time_base, out_stream->time_base,
	    (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
	pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base,
				    out_stream->time_base);
	pkt.pos = -1;
	pkt.stream_index = 0;
	res = av_interleaved_write_frame(tmpfmtcont, &pkt);
	if (res < 0) {
	    cout << "write frame failed" << endl;
	    break;
	}
	av_free_packet(&pkt);
	frameIndex++;
    }
#if USE_H264BSR
    av_bitstream_filter_close(h264bsfs);
#endif
    // write file trailer
    av_write_trailer(ofmtcont_v);
    av_write_trailer(ofmtcont_a);
end:
    avformat_close_input(&ifmtcont);
    // close output
    if (ofmtcont_v && !(ofmt_v->flags & AVFMT_NOFILE))
	avio_close(ofmtcont_v->pb);
    if (ofmtcont_a && !(ofmt_a->flags & AVFMT_NOFILE))
	avio_close(ofmtcont_a->pb);
    avformat_free_context(ofmtcont_a);
    avformat_free_context(ofmtcont_v);
}
//转视频的封装格式，不编解码
void AVBase::convertMuxer() {
    AVOutputFormat *ofmt = NULL;
    AVFormatContext *ifmtcont = NULL, *ofmtcont = NULL;
    AVPacket pkt;
    string out_filename("/root/testcodec.flv");
    av_register_all();
    int res = -1;
    int frame_index = 0;
    // 1,打开输入文件，并写入AVFormatContext中
    res = avformat_open_input(&ifmtcont, _filename.data(), NULL, NULL);
    if (res < 0) {
	cout << "open file failed" << endl;
	goto end;
    }
    // 2,获取视音频，字幕流的信息
    res = avformat_find_stream_info(ifmtcont, NULL);
    if (res < 0) {
	cout << "find the stream failed" << endl;
	goto end;
    }
    // 3,初始化输出文件的AVFormatContext
    res = avformat_alloc_output_context2(&ofmtcont, NULL, NULL,
					 out_filename.data());
    if (res < 0 | !ofmtcont) {
	cout << "init output AVFormatcont failed" << endl;
	goto end;
    }
    // 4,
    ofmt = ofmtcont->oformat;
    // 5,从输入文件中读取媒体流
    for (int i = 0; i < ifmtcont->nb_streams; i++) {
	AVStream *in_stream = ifmtcont->streams[i];
	// 5.1初始化输出文件的流通道
	AVStream *out_stream =
	    avformat_new_stream(ofmtcont, in_stream->codec->codec);
	if (!out_stream) {
	    cout << "create stream path failed" << endl;
	    goto end;
	}
	// 5.2copy the AVCodecContext setting
	res = avcodec_copy_context(out_stream->codec, in_stream->codec);
	if (res < 0) {
	    cout << "copy the setting of AVCodecContext failed" << endl;
	    goto end;
	}
	out_stream->codec->codec_tag = 0;
	// Some formats want stream headers to be separate不懂？？？？？？
	if (ofmtcont->oformat->flags & AVFMT_GLOBALHEADER) {
	    out_stream->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}
    }

    // 6,open output file ????搞不懂为什么要判断
    if (!(ofmt->flags & AVFMT_NOFILE)) {
	res = avio_open(&ofmtcont->pb, out_filename.data(), AVIO_FLAG_WRITE);
	if (res < 0) {
	    cout << "open output file failed" << endl;
	    goto end;
	}
    }
    // 7,写媒体文件的头信息
    res = avformat_write_header(ofmtcont, NULL);
    if (res < 0) {
	cout << "write the file header failed" << endl;
	goto end;
    }
    // 8,
    // 数据包的读取和写入，输入与输出均已经打开，并与对应的AVFormatContext建立了关联，接下来可以从输入格式由读取数包，然后
    //将数据包写入到输出的文件，当然，随着输入的封装格式与输出的封装格式的差别化，时间戳也需要进行对应的计算改变
    while (true) {
	AVStream *in_stream = NULL, *out_stream = NULL;
	// 8.1 get an AVPacket
	res = av_read_frame(ifmtcont, &pkt);
	if (res < 0) {
	    cout << "get an avpacket failed" << endl;
	    break;
	}
	in_stream = ifmtcont->streams[pkt.stream_index];
	out_stream = ofmtcont->streams[pkt.stream_index];

	// convert PTS/DTS
	pkt.pts = av_rescale_q_rnd(
	    pkt.pts, in_stream->time_base, out_stream->time_base,
	    (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
	pkt.dts = av_rescale_q_rnd(
	    pkt.dts, in_stream->time_base, out_stream->time_base,
	    (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
	pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base,
				    out_stream->time_base);
	pkt.pos = -1;
	// write
	res = av_interleaved_write_frame(ofmtcont, &pkt);
	if (res < 0) {
	    cout << "wrire packet to file failed" << endl;
	    break;
	}
	av_free_packet(&pkt);
	cout << "write " << frame_index << "frames to output file" << endl;
	frame_index++;
    }
    av_write_trailer(ofmtcont);
end:
    avformat_close_input(&ifmtcont);
    // close output
    if (ofmtcont && !(ofmt->flags & AVFMT_NOFILE)) avio_close(ofmtcont->pb);
    avformat_free_context(ofmtcont);
}

// 改变视频的封装格式，并且转编码格式
/*
 *常用的数据结构，有
 *AVCodec 编码器结构体
 *AVCodecContext    编码器上下文
 *AVFrame   解码后的帧存放的结构体
 *结构体内存的分配与释放
 *av_frame_alloc/av_frame_free()
 *avcodec_alloc_context3/avcodec_free_context()
 *解码的步骤
 *1，查找解码器   avcodec_find_decoder_
 *2，打开解码器   avcodec_open2
 *
 * */
void AVBase::convertCodec() {}

//测试ffmpeg的一些函数
void AVBase::testSome() {
    //测试日志
    av_log_set_level(AV_LOG_DEBUG);
    av_log(NULL, AV_LOG_INFO, "Hello world:%s\n", "welcome");
    // 1,删除文件
    int res;
    // res = avpriv_io_delete("/root/w.txt");
    // 2,移动某个文件
    // source = /root/111.txt
    // dest = /root/222.txt
    // res = avpriv_io_move("/root/111.txt","/root/222.txt");
    // 3,操作目录重要结构体
    // 3.1 avio_open_dir()
    // 3.2 avio_read_dir()
    // 3.3 avio_close_dir()
    //还有两个重要的结构体
    // AVIODirContex,操作目录的上下文
    // AVIODirEntry ,目录项,用于存放文件名,文件大小等信息
    if (res < 0) {
	av_log(NULL, AV_LOG_ERROR, "failed to remove the file ");
    }
}

void AVBase::operateDir() {
    //设置日志的级别
    av_log_set_level(AV_LOG_INFO);
    int res;
    // AVIODirContext,存储目录的上下文
    AVIODirContext *iodircont = NULL;
    // AVIODirEntry
    AVIODirEntry *iodirentry = NULL;
    // 1,打开一个目录的结构
    string pathname("./");
    res = avio_open_dir(&iodircont, pathname.data(), NULL);
    if (res < 0) {
	av_log(NULL, AV_LOG_ERROR, "Failed to open the dir\n");
    }
    // 2,打开之后，读取目录中的内容，并且将读取到的值存放在AVIODirEntry
    while (true) {
	res = avio_read_dir(iodircont, &iodirentry);
	if (res) {
	    av_log(NULL, AV_LOG_ERROR, "Failed to read the dir");
	    goto _fail;
	}
	if (!iodirentry) {
	    av_log(NULL, AV_LOG_INFO, "iodirenter is null");
	    break;
	}
	av_log(NULL, AV_LOG_INFO, " %s, %d,the utf8 is %d\n", iodirentry->name,
	       iodirentry->size, iodirentry->utf8);
	avio_free_directory_entry(&iodirentry);
    }
_fail:
    avio_close_dir(&iodircont);
}

//剪切视频
void AVBase::cutVideo() {
    AVFormatContext *infmtcont = NULL, *outfmtcont = NULL;
    AVOutputFormat *ofmt = NULL;
    AVPacket pkt;
    av_register_all();
    int res;
    double startTime = 17.0, endTime = 40.0;
    string inputVideo("/root/source_video/codetest.avi");
    string outVideo("/root/cutVideo.avi");
    //根据流数量申请空间，并全部初始化为0
    map<int, int64_t> pts_start_from;
    map<int, int64_t> dts_start_from;

    // 1,设置av_log的级别
    av_log_set_level(AV_LOG_DEBUG);
    // 2,打开输入文件
    res = avformat_open_input(&infmtcont, inputVideo.data(), NULL, NULL);
    if (res < 0) {
	av_log(NULL, AV_LOG_ERROR, "Failed to open input file");
	goto _fail;
    }
    // 3,查找出入文件中的流
    res = avformat_find_stream_info(infmtcont, NULL);
    if (res < 0) {
	av_log(NULL, AV_LOG_ERROR, "Failed to find the input video stream");
	abort();
    }
    // 4.1初始化输入文件
    res = avformat_alloc_output_context2(&outfmtcont, NULL, NULL,
					 outVideo.data());
    // 4.2，关联AVOutputFormat
    ofmt = outfmtcont->oformat;

    // 5,创建输出文件的流，以及拷贝相关的编码设置
    for (int i = 0; i < infmtcont->nb_streams; i++) {
	AVStream *in_stream = NULL, *out_stream = NULL;
	in_stream = infmtcont->streams[i];
	// 5.1,初始化输出文件的流通道,新版本的方法
	AVCodec *c = avcodec_find_decoder(in_stream->codecpar->codec_id);
	out_stream = avformat_new_stream(outfmtcont, c);
	res =
	    avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
	if (res < 0) {
	    av_log(NULL, AV_LOG_ERROR, "Failed to copy the codec");
	    goto _fail;
	}
	//复制成功之后还需要设置codec_tag
	out_stream->codecpar->codec_tag = 0;
    }

    // 6,打开输出的文件
    if (!(ofmt->flags & AVFMT_NOFILE)) {
	res = avio_open(&outfmtcont->pb, outVideo.data(), AVIO_FLAG_WRITE);
	if (res < 0) {
	    av_log(NULL, AV_LOG_ERROR, "Failed to open the output file");
	    goto _fail;
	}
    }
    // 7,写入头文件
    res = avformat_write_header(outfmtcont, NULL);
    if (res < 0) {
	av_log(NULL, AV_LOG_ERROR, "Failed to write the header to file");
	goto _fail;
    }

    // 8,调到指定的帧
    res = av_seek_frame(infmtcont, -1, (int64_t)startTime * (AV_TIME_BASE),
			AVSEEK_FLAG_ANY);
    if (res < 0) {
	av_log(NULL, AV_LOG_ERROR, "Failed to av_seek_frame");
	goto _fail;
    }
    while (true) {
	AVStream *in_stream, *out_stream;
	// 1,读取输入文件中的数据
	res = av_read_frame(infmtcont, &pkt);
	if (res < 0) {
	    av_log(NULL, AV_LOG_ERROR, "Failed to read the frame");
	    goto _fail;
	}
	// 2
	in_stream = infmtcont->streams[pkt.stream_index];
	out_stream = outfmtcont->streams[pkt.stream_index];

	//当读取的时间超过了我们设置的要截取的时间，就退出循环
	av_log(NULL, AV_LOG_INFO, "pkt.pts is %f",
	       av_q2d(in_stream->time_base) * pkt.pts);
	if (av_q2d(in_stream->time_base) * pkt.pts > endTime) {
	    av_packet_unref(&pkt);
	    break;
	}
	//将截取后的每个流的起始dts，pts保存下来，作为开始时间，用来做后面的时间基转换
	//可以使用vector来存储或者map
	if (dts_start_from[pkt.stream_index] == 0) {
	    dts_start_from[pkt.stream_index] = pkt.dts;
	}
	if (pts_start_from[pkt.stream_index] == 0) {
	    pts_start_from[pkt.stream_index] = pkt.pts;
	}

	//时间基转换
	pkt.pts = av_rescale_q_rnd(
	    pkt.pts - pts_start_from[pkt.stream_index], in_stream->time_base,
	    out_stream->time_base,
	    (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
	pkt.dts = av_rescale_q_rnd(
	    pkt.dts - pts_start_from[pkt.stream_index], in_stream->time_base,
	    out_stream->time_base,
	    (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
	if (pkt.pts < 0) {
	    pkt.pts = 0;
	}
	if (pkt.dts < 0) {
	    pkt.dts = 0;
	}
	pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base,
				    out_stream->time_base);
	pkt.pos = -1;

	// 一帧视频的播放时间必须在解码时间点之后，当出现 pkt.pts < pkt.dts
	// 时会导致程序异常，
	// 所以我们丢掉有问题的，不会有太大的影响
	if (pkt.pts < pkt.dts) {
	    continue;
	}
	res = av_interleaved_write_frame(outfmtcont, &pkt);
	if (res < 0) {
	    av_log(NULL, AV_LOG_ERROR, "Failed to wwrite frame");
	    goto _fail;
	}
	av_packet_unref(&pkt);
    }
    //释放资源
    dts_start_from.clear();
    pts_start_from.clear();

    //写文件尾的信息
    av_write_trailer(outfmtcont);

_fail:
    avformat_close_input(&infmtcont);
    // close the output file
    if (outfmtcont && !(ofmt->flags & AVFMT_NOFILE)) avio_close(outfmtcont->pb);
    if (outfmtcont) {
	avformat_free_context(outfmtcont);
    }
    if (res < 0 && res != AVERROR_EOF) {
	cout << "Failed to this cut video,the res is :" << res << endl;
    }
}


void AVBase::waterMark() {
    //还是新打开输出的文件，定义变量等一系列东西
    AVFormatContext *ifmtcont = NULL, *ofmtcont = NULL;
    AVOutputFormat *ofmt = NULL;
    AVCodecContext *dec_ctx = NULL;
    int res, videoIndex = -1, got_frame = 0,num;
    AVPacket pkt;
    AVCodec *dec;
    AVFrame *frame = av_frame_alloc();
    AVFrame *filt_frame = av_frame_alloc();
    string inVideo("/root/source_video/codetest.avi");
    string outVideo("/root/waterMark.avi");
    av_register_all();
    avcodec_register_all();
    avfilter_register_all();
    // 1,可以定义为全局变量
    AVFilterContext *buffersink_ctx = NULL;
    AVFilterContext *buffersrc_ctx = NULL;
    AVFilterGraph *filter_graph = NULL;

    //这个局部的
    char args[512];
    const char *filter_descr = "drawtext=fontfile=/usr/share/fonts/fonts\\\\/simsun.ttc:fontsize=100:text=hello world:x=100:y=100";
    const AVFilter *buffersrc = NULL;
    const AVFilter *buffersink = NULL;
    AVFilterInOut *outputs = NULL;
    AVFilterInOut *inputs = NULL;
   enum AVPixelFormat pix_fmts[] = {AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE};
    //enum AVPixelFormat pix_fmts[] = {AV_PIX_FMT_YUV420p, AV_PIX_FMT_YUV420p};
    AVBufferSinkParams *buffersink_params;
	static int64_t last_pts = AV_NOPTS_VALUE;
    // 1，定义日志的级别
    av_log_set_level(AV_LOG_DEBUG);

    // 2,打开输入的文件
    res = avformat_open_input(&ifmtcont, inVideo.data(), NULL, NULL);
    FAIL(res, "Failed to open the input file");

    // 3,找到文件中的流通道
    res = avformat_find_stream_info(ifmtcont, NULL);
    FAIL(res, "Failed to find the stream of input file ");

    // 4,找到 视频流的信息
    res = av_find_best_stream(ifmtcont, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, NULL);
    FAIL(res, "Failed to find the video stream");
    videoIndex = res;
    dec_ctx = ifmtcont->streams[videoIndex]->codec;
    // 5, 初始化一个视音频编解码器的AVCodecContext
    res = avcodec_open2(dec_ctx, dec, NULL);
    FAIL(res, "Failed to open video decoder");

    // 6,初始化filters中的一些东西
    buffersrc = avfilter_get_by_name("buffer");
    buffersink = avfilter_get_by_name("buffersink");
    outputs = avfilter_inout_alloc();
    inputs = avfilter_inout_alloc();
    filter_graph = avfilter_graph_alloc();
    /* buffer video source: the decoded frames from the decoder will be inserted
     * here. */
	snprintf(args, sizeof(args),"video_size=%dx%d:pix_fmt=%d:time_base=%d%d:pixel_aspect=%d/%d",
			dec_ctx->width,dec_ctx->height,dec_ctx->pix_fmt,dec_ctx->time_base.num, dec_ctx ->time_base.den,
			dec_ctx->sample_aspect_ratio.num,dec_ctx->sample_aspect_ratio.den );
    res = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in", args,
				       NULL, filter_graph);
	FAIL(res, "Cannot create buffer source");
    /* buffer video sink: to terminate the filter chain. */
    buffersink_params = av_buffersink_params_alloc();
    buffersink_params->pixel_fmts = pix_fmts;
    res = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out", NULL,
				       buffersink_params, filter_graph);
    av_free(buffersink_params);
	FAIL(res, "Cannot create buffer sink");
    /* Endpoints for the filter graph. */
    outputs->name = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx = 0;
    outputs->next = NULL;
    inputs->name = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx = 0;
    inputs->next = NULL;
	res = avfilter_graph_parse_ptr(filter_graph, filter_descr, &inputs,&outputs, NULL);
	FAIL(res, "error");
	res = avfilter_graph_config(filter_graph, NULL);
	FAIL(res, "error");

	//7,初始化输出的文件
	res = avformat_alloc_output_context2(&ofmtcont, NULL, NULL, outVideo.c_str());
	FAIL(res, "Failed to init the output file");

	ofmt = ofmtcont->oformat;



	//7.2,编码信息
	for (int i= 0; i < ifmtcont -> nb_streams;i++){
		AVStream *in_stream = NULL, *out_stream = NULL;
		in_stream = ifmtcont->streams[i];
		AVCodec *c = avcodec_find_decoder(in_stream->codecpar->codec_id);
		out_stream = avformat_new_stream(ofmtcont, c);
		res = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
		FAIL(res, "Failed t copy the codec");	
		//复制成功之后还需要设置codec_tag
		out_stream->codecpar->codec_tag = 0;
	}

	//7.3，打开输出的文件
	res = avio_open2(&ofmtcont->pb,outVideo.c_str(), AVIO_FLAG_WRITE, NULL, NULL);
	FAIL(res, "Failed to open the output file");

	//7.4,写入头文件
	res = avformat_write_header(ofmtcont, NULL);
	FAIL(res, "Failed to write the header file");
	
	//7,读取所有的packet
	while(true){
		res = av_read_frame(ifmtcont, &pkt);
		if (res < 0){
			av_log(NULL, AV_LOG_ERROR, "Failed to read frame");
			break;
		}
		if (pkt.stream_index == videoIndex){
		//读取到了视频流，然后开始解码,以及进行过滤
			//avcodec_get_frame_defaults(frame);
			res = avcodec_decode_video2(dec_ctx,frame, &got_frame, &pkt);
			if (res < 0){
				av_log(NULL, AV_LOG_ERROR, "Failed to decodeing video\n");
				break;
			}
			if (got_frame){
				frame->pts = av_frame_get_best_effort_timestamp(frame);
				/*push the decoded frame into the filtergraph*/
				if (av_buffersrc_add_frame_flags(buffersrc_ctx,frame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0){
					av_log(NULL, AV_LOG_ERROR, "Failed to feed the filtergraph\n");
					break;
				}
				/*pull filtered frames from the filtergraph*/
				while(true){
					res = av_buffersink_get_frame(buffersink_ctx, filt_frame);
					if (res == AVERROR(EAGAIN) || res == AVERROR_EOF)
						break;
					if (res < 0)
						goto _fail;
					//display_frame,don't the meaning
					//display_frame(filt_frame, buffersink_ctx->inputs[0]->time_base);
					AVPacket tmppkt;
					//res = avcodec_encode_video2(ofmtcont->streams[videoIndex]->codec, &tmppkt,frame,&num);
					res = avcodec_send_frame(ofmtcont->streams[videoIndex]->codec,filt_frame);
					while (res > 0){
					 res = avcodec_receive_packet(ofmtcont->streams[videoIndex]->codec,&tmppkt);
					 if (res == AVERROR(EAGAIN) || res == AVERROR_EOF){
						break;
					 }
					if (av_interleaved_write_frame(ofmtcont, &tmppkt) < 0) {
					    cout << "write packet to output file failed" << endl;
					    break;
					}
					av_packet_unref(&tmppkt);
					}
					av_frame_unref(filt_frame);
				}	
			}
		}
		av_free_packet(&pkt);
	}

    av_write_trailer(ofmtcont);
_fail:
	avfilter_graph_free(&filter_graph);
	if (dec_ctx)
		avcodec_close(dec_ctx);
    avformat_close_input(&ifmtcont);
	av_frame_free(&frame);
	av_frame_free(&filt_frame);
    if (ofmtcont && !(ofmt->flags & AVFMT_NOFILE)) {
	avio_close(ofmtcont->pb);
    }
    avformat_free_context(ofmtcont);

    if (res < 0 && res != AVERROR_EOF) {
	av_log(NULL, AV_LOG_ERROR, "Faile to waterMark");
    }
}
