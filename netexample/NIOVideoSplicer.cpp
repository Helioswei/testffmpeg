#include "NIOType.h"
#include "NIOThread.h"
#include "NIOVideoSplicer.h"

NIOVideoSplicer::NIOVideoSplicer(int inCount, void *in, const char *output, int (*cb)(int, int, int, void *))
    : callback(cb)
    , workThread(NULL)
    , inputCount(inCount)
    , currentDemuxIndex(0)
    , inputFileList(NULL)
    , outputFile(NULL)
#ifdef USE_AV_FILTER
    , watermarkFile(NULL)
#endif // USE_AV_FILTER
    , inputStruct(NULL)
    , outputStruct(NULL)
    , needAudioTranscoding(0)
    , needVideoTranscoding(0)
    , outputDuration(0)
    , audioPTSStride(0)
    , audioDTSStride(0)
    , videoPTSStride(0)
    , videoDTSStride(0)
    , audioPrePTS(0)
    , audioPreDTS(0)
    , videoPrePTS(0)
    , videoPreDTS(0)
    , lastCallbackPTS(0)
{
    inputFileList = new const char*[inCount];
    if(NULL != inputFileList) {
        ALOGI("Input files:");
        for(int i = 0; i < inputCount; i ++) {
            inputFileList[i] = new char[INPUT_PATH_MAX_LEN];
            strncpy((char*)inputFileList[i], (char*)in + i * INPUT_PATH_MAX_LEN, INPUT_PATH_MAX_LEN);
            ALOGI("%s", inputFileList[i]);
        }
    }
    outputFile = new char[INPUT_PATH_MAX_LEN];
    if(NULL != outputFile) {
        strncpy((char*)outputFile, output, INPUT_PATH_MAX_LEN);
        ALOGI("Output file : %s", outputFile);
    }
}

NIOVideoSplicer::~NIOVideoSplicer()
{
    if(NULL != workThread) {
        workThread->Stop();
        workThread->WaitThread();
        workThread->DetachThread();
        delete workThread;
        workThread = NULL;
    }

    if(NULL != inputFileList) {
        for(int i = 0; i < inputCount; i ++) {
            if(NULL != inputFileList[i]) {
                delete [] inputFileList[i];
                inputFileList[i] = NULL;
            }
        }
        delete [] inputFileList;
        inputFileList = NULL;
    }

    if(NULL != outputFile) {
        delete [] outputFile;
        outputFile = NULL;
    }

    if(NULL != watermarkFile) {
        delete [] watermarkFile;
        watermarkFile = NULL;
    }
}

#ifdef USE_AV_FILTER
int NIOVideoSplicer::SetWatermark(const char *path, int left, int top, int right, int bottom)
{
    int len = sizeof(char) * strlen(path);

    ALOGI("SetWatermark path %s left %d top %d reight %d bottom %d", path, left, top, right, bottom);

    watermarkFile = new char[len + 1];
    memset((void*)watermarkFile, 0, len + 1);
    strncpy((char*)watermarkFile, path, len);

    watermarkRect.left = left;
    watermarkRect.top = top;
    watermarkRect.right = right;
    watermarkRect.bottom = bottom;

    return 0;
}
#endif // USE_AV_FILTER

int NIOVideoSplicer::Start()
{
    int nRC = 0;

    NeedDoTranscoding(this);

    nRC = OpenInput(this);
    if(0 > nRC) {
        ALOGE("Start error during OpenInput nRC %d", nRC);
        return nRC;
    }

    nRC = OpenOutput(this);
    if(0 > nRC) {
        ALOGE("Start error during OpenOutput nRC %d", nRC);
        return nRC;
    }

    if(NULL == workThread)
        workThread = new NIOThread("SplicerThread", -15);
    if(NULL == workThread) {
        ALOGE("Start with NULL work thread !");
        return -1;
    }

    return workThread->CreateThread(WorkLoop, this);
}

int NIOVideoSplicer::Stop()
{
    if(NULL != workThread) {
        workThread->Stop();
        workThread->WaitThread();
    }

    return 0;
}

//////////////////////////////////////////////// Private /////////////////////////////////////////////////////

int NIOVideoSplicer::WorkLoop(void *data)
{
    if(NULL == data) {
        ALOGE("WorkLoop with NULL parameter !");
        return -1;
    }

    NIOVideoSplicer *splicer = (NIOVideoSplicer*)data;

    return splicer->Remuxing(splicer);
}

int NIOVideoSplicer::NeedDoTranscoding(NIOVideoSplicer *splicer)
{
    if(NULL == splicer) {
        ALOGE("NeedDoTranscoding with splicer NULL !");
        return -1;
    }

    int nRC = 0;
    AVFormatContext *firstFormatContext = NULL, *nextFormatContext = NULL;
    AVFormatContext **formatContext = NULL;
    AVStream *firstAudioStream = NULL, *firstVideoStream = NULL;
    AVStream *audioStream = NULL, *videoStream = NULL;
    AVCodec *dec = NULL;

    const char **inputFileList = splicer->inputFileList;

    if(NULL == inputFileList) {
        ALOGE("NeedDoTranscoding with NULL input file list !");
        nRC = -1;
        goto END;
    }

#ifdef USE_AV_FILTER
    if(NULL != splicer->watermarkFile)
        splicer->needVideoTranscoding = 1;
#endif // USE_AV_FILTER

    for (int i = 0; i < splicer->inputCount; i ++) {
        if(0 == i)
            formatContext = &firstFormatContext;
        else
            formatContext = &nextFormatContext;

        if ((nRC = avformat_open_input(formatContext, inputFileList[i], 0, 0)) < 0) {
            ALOGE("Could not open input file '%s' nRC %d !!!", inputFileList[i], nRC);
            goto END;
        }

        if(0 >= splicer->needAudioTranscoding) {
            if (( nRC = av_find_best_stream(*formatContext, AVMEDIA_TYPE_AUDIO, -1, -1, &dec, 0)) < 0) {
                ALOGE("Cannot find audio steam in %s", inputFileList[i]);
                goto END;
            } else {
                if(0 == i)
                    firstAudioStream = (*formatContext)->streams[nRC];
                else
                    audioStream = (*formatContext)->streams[nRC];
            }
        }

        if(0 >= splicer->needVideoTranscoding) {
            if (( nRC = av_find_best_stream(*formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0)) < 0) {
                ALOGE("Cannot find video steam in %s", inputFileList[i]);
                goto END;
            } else {
                if(0 == i)
                    firstVideoStream = (*formatContext)->streams[nRC];
                else
                    videoStream = (*formatContext)->streams[nRC];
            }
        }

        if(NULL != firstAudioStream && NULL != audioStream)
            splicer->needAudioTranscoding = CheckCodecParameters(AVMEDIA_TYPE_AUDIO, firstAudioStream->codecpar, audioStream->codecpar);

        if(NULL != firstVideoStream && NULL != videoStream) {
            splicer->needVideoTranscoding = CheckCodecParameters(AVMEDIA_TYPE_VIDEO, firstVideoStream->codecpar, videoStream->codecpar);
        }

        if(NULL != nextFormatContext) {
            avformat_close_input(&nextFormatContext);
            nextFormatContext = NULL;
        }

        if(0 < splicer->needAudioTranscoding && 0 < splicer->needVideoTranscoding)
            break;
    }

 END:
   if(NULL != firstFormatContext) {
        avformat_close_input(&firstFormatContext);
        firstFormatContext = NULL;
    }

   if(NULL != nextFormatContext) {
        avformat_close_input(&nextFormatContext);
        nextFormatContext = NULL;
    }

    return nRC;
}

int NIOVideoSplicer::OpenInput(NIOVideoSplicer *splicer)
{
    if(NULL == splicer) {
        ALOGE("OpenInputOutput with NULL parameter !");
        return -1;
    }

    int nRC = 0;
    AVCodec *dec = NULL, *enc = NULL;

    const char **inputFileList = splicer->inputFileList;
    const char *output = splicer->outputFile;

    if(NULL == inputFileList) {
        ALOGE("OpenInputOutput with NULL inputFileList!");
        return -1;
    }

    splicer->audioPTSStride = splicer->audioPrePTS + 10240;
    splicer->audioDTSStride = splicer->audioPreDTS + 10240;
    splicer->videoPTSStride = splicer->videoPrePTS + 10000;
    splicer->videoDTSStride = splicer->videoPreDTS + 10000;

    if(NULL != splicer->inputStruct)
        delete splicer->inputStruct;
    splicer->inputStruct = new FileInfoStruct;
    if(NULL == splicer->inputStruct) {
        ALOGE("Cannot new input VideoInfoStruct list !");
        nRC = -1;
        goto ERROR;
    }
    memset(splicer->inputStruct, 0, sizeof(FileInfoStruct));

    splicer->inputStruct->path = inputFileList[splicer->currentDemuxIndex];
    if(NULL == splicer->inputStruct->path){
        ALOGE("NULL == inputFileList[%d]", splicer->currentDemuxIndex);
        nRC = -1;
        goto ERROR;
    }

    if ((nRC = avformat_open_input(&splicer->inputStruct->formatContext, splicer->inputStruct->path, 0, 0)) < 0) {
        ALOGE("Could not open input file '%s' nRC %d !!!", splicer->inputStruct->path, nRC);
        goto ERROR;
    }

    if (( nRC = av_find_best_stream(splicer->inputStruct->formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0)) < 0) {
        ALOGE("Cannot find video steam in %s", splicer->inputStruct->path);
        avformat_close_input(&splicer->inputStruct->formatContext);
        splicer->inputStruct->formatContext = NULL;
        goto ERROR;
    } else {
        splicer->inputStruct->videoStream = splicer->inputStruct->formatContext->streams[nRC];
    }

    if (( nRC = av_find_best_stream(splicer->inputStruct->formatContext, AVMEDIA_TYPE_AUDIO, -1, -1, &dec, 0)) < 0) {
        ALOGE("Cannot find audio steam in %s", splicer->inputStruct->path);
        avformat_close_input(&splicer->inputStruct->formatContext);
        splicer->inputStruct->formatContext = NULL;
        goto ERROR;
    } else {
        splicer->inputStruct->audioStream = splicer->inputStruct->formatContext->streams[nRC];
    }

    splicer->inputStruct->duration = av_rescale(splicer->inputStruct->formatContext->duration, 1000, AV_TIME_BASE);
    outputDuration += splicer->inputStruct->duration;
    ALOGI("Debug .... outputDuration %lld inputDuration %lld", outputDuration, splicer->inputStruct->duration);

    nRC = 0;
    av_dump_format(splicer->inputStruct->formatContext, 0, splicer->inputStruct->path, 0);

    if(0 < splicer->needAudioTranscoding) {
        nRC = splicer->OpenInputDecoder(splicer->inputStruct, AVMEDIA_TYPE_AUDIO);
        if(0 > nRC) {
            ALOGE("Cannot open inputStruct audio decoder !");
            goto ERROR;
        }

        splicer->inputStruct->audioFrame = av_frame_alloc();
        if(NULL == splicer->inputStruct->audioFrame) {
            ALOGE("Cannot alloc input frame for audio decoder !");
            goto ERROR;
        }
    }
    if(0 < splicer->needVideoTranscoding) {
        nRC = splicer->OpenInputDecoder(splicer->inputStruct, AVMEDIA_TYPE_VIDEO);
        if(0 > nRC) {
            ALOGE("Cannot open inputStruct video decoder !");
            goto ERROR;
        }

        splicer->inputStruct->videoFrame = av_frame_alloc();
        if(NULL == splicer->inputStruct->videoFrame) {
            ALOGE("Cannot alloc input frame for video decoder !");
            goto ERROR;
        }
    }

ERROR:

    return nRC;
}

int NIOVideoSplicer::OpenOutput(NIOVideoSplicer *splicer)
{
    if(NULL == splicer) {
        ALOGE("OpenInputOutput with NULL parameter !");
        return -1;
    }

    int nRC = 0;
    AVCodec *enc = NULL;

    const char *output = splicer->outputFile;

    if(NULL == output) {
        ALOGE("OpenOutput with NULL path !");
        return -1;
    }

    if(NULL == splicer->inputStruct->audioStream || NULL == splicer->inputStruct->videoStream) {
        ALOGE("Cannot open output wihtout input audio stream %p video stream %p", splicer->inputStruct->audioStream, splicer->inputStruct->videoStream);
        return -1;
    }

    if(0 == splicer->currentDemuxIndex) {
        splicer->audioPTSStride = 0;
        splicer->audioDTSStride = 0;
        splicer->videoPTSStride = 0;
        splicer->videoDTSStride = 0;
    }

    if(NULL == splicer->outputStruct) {
        splicer->outputStruct = new FileInfoStruct;
        if(NULL == splicer->outputStruct) {
            ALOGE("Cannot new output VideoInfoStruct !");
            nRC = -1;
            goto ERROR;
        }
        memset(splicer->outputStruct, 0, sizeof(FileInfoStruct));
    }

    // Open output below
    avformat_alloc_output_context2(&splicer->outputStruct->formatContext, NULL, NULL, output);
    if(NULL == splicer->outputStruct->formatContext) {
        ALOGE("Cannot open output %s", output);
        nRC = -1;
        goto ERROR;
    }

    // Audio stream
    splicer->outputStruct->audioStream = avformat_new_stream(splicer->outputStruct->formatContext, NULL);
    if(NULL == splicer->outputStruct->audioStream) {
        ALOGE("Cannot new audio stream for output !");
        nRC = -1;
        goto ERROR;
    }

    if(0 < splicer->needAudioTranscoding) {
        enc = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if(NULL == enc) {
            ALOGE("Cannot fine audio AAC encoder !");
            nRC = -1;
            goto ERROR;
        } else {
            splicer->outputStruct->audioCodecCtx = avcodec_alloc_context3(enc);
            if(NULL == splicer->outputStruct->audioCodecCtx) {
                ALOGE("Cannot alloc audio AAC encoder context!");
                nRC = -1;
                goto ERROR;
            } else {
                splicer->outputStruct->audioCodecCtx->channels       = OUTPUT_CHANNELS;
                splicer->outputStruct->audioCodecCtx->channel_layout = av_get_default_channel_layout(OUTPUT_CHANNELS);
                splicer->outputStruct->audioCodecCtx->sample_fmt     = enc->sample_fmts[0];
                splicer->outputStruct->audioCodecCtx->sample_rate    = OUTPUT_SAMPLE_RATE;
                splicer->outputStruct->audioCodecCtx->bit_rate       = OUTPUT_BIT_RATE;
                splicer->outputStruct->audioCodecCtx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
                splicer->outputStruct->audioCodecCtx->time_base.den = OUTPUT_SAMPLE_RATE;
                splicer->outputStruct->audioCodecCtx->time_base.num = 1;

                if (NULL != splicer->outputStruct->formatContext->oformat && splicer->outputStruct->formatContext->oformat->flags & AVFMT_GLOBALHEADER)
                    splicer->outputStruct->audioCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

                nRC = avcodec_open2(splicer->outputStruct->audioCodecCtx, enc, NULL);
                if(0 > nRC) {
                    ALOGE("Could not open audio output codec (error '%s')", av_err2str(nRC));
                    goto ERROR;
                }

                nRC = avcodec_parameters_from_context(splicer->outputStruct->audioStream->codecpar, splicer->outputStruct->audioCodecCtx);
                if (0 > nRC) {
                    ALOGE("Could not initialize audio stream parameters, nRC %d : %s", nRC, av_err2str(nRC));
                    goto ERROR;
                }

                ALOGI("Audio encoder channels %d channel_layout %lld sample_fmt %d, sample_rate %d bit_rate %d",
                        OUTPUT_CHANNELS, av_get_default_channel_layout(OUTPUT_CHANNELS), enc->sample_fmts[0], OUTPUT_SAMPLE_RATE, OUTPUT_BIT_RATE);

                splicer->outputStruct->audioFifo = av_audio_fifo_alloc(splicer->outputStruct->audioCodecCtx->sample_fmt, splicer->outputStruct->audioCodecCtx->channels, 1);
                if(NULL == splicer->outputStruct->audioFifo) {
                    ALOGE("Could not allocate audio FIFO");
                    nRC = -1;
                    goto ERROR;
                }

                splicer->outputStruct->audioFrame = av_frame_alloc();
                if(NULL == splicer->outputStruct->audioFrame) {
                    ALOGE("Cannot alloc frame for audio resample output !");
                    goto ERROR;
                }

                splicer->outputStruct->audioFrame->nb_samples     = splicer->outputStruct->audioCodecCtx->frame_size;
                splicer->outputStruct->audioFrame->channel_layout = splicer->outputStruct->audioCodecCtx->channel_layout;
                splicer->outputStruct->audioFrame->format         = splicer->outputStruct->audioCodecCtx->sample_fmt;
                splicer->outputStruct->audioFrame->sample_rate    = splicer->outputStruct->audioCodecCtx->sample_rate;

                nRC = av_frame_get_buffer(splicer->outputStruct->audioFrame, 0);
                if(0 > nRC) {
                    ALOGE("Cannot get buffer for audio resample output frame : %s !", av_err2str(nRC));
                    goto ERROR;
                }
            }
        }
    } else {
        nRC = avcodec_parameters_copy(splicer->outputStruct->audioStream->codecpar, splicer->inputStruct->audioStream->codecpar);
        if (0 > nRC) {
            ALOGE("Failed to copy codec parameters !!!");
            goto ERROR;
        }
    }

    splicer->outputStruct->audioStream->codecpar->codec_tag = 0;

    // Video stream
    splicer->outputStruct->videoStream = avformat_new_stream(splicer->outputStruct->formatContext, NULL);
    if(NULL == splicer->outputStruct->videoStream) {
        ALOGE("Cannot new audio stream for output !");
        nRC = -1;
        goto ERROR;
    }

    if(0 < splicer->needVideoTranscoding) {
        enc = avcodec_find_encoder(AV_CODEC_ID_H264);
        if(NULL != enc) {
            splicer->outputStruct->videoCodecCtx = avcodec_alloc_context3(enc);
            if(NULL != splicer->outputStruct->videoCodecCtx) {
                splicer->outputStruct->videoCodecCtx->height = splicer->inputStruct->videoCodecCtx->height;
                splicer->outputStruct->videoCodecCtx->width = splicer->inputStruct->videoCodecCtx->width;
                splicer->outputStruct->videoCodecCtx->sample_aspect_ratio = splicer->inputStruct->videoCodecCtx->sample_aspect_ratio;
                splicer->outputStruct->videoCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
                splicer->outputStruct->videoCodecCtx->time_base = av_inv_q(splicer->inputStruct->videoStream->r_frame_rate);

                if (NULL != splicer->outputStruct->formatContext->oformat && splicer->outputStruct->formatContext->oformat->flags & AVFMT_GLOBALHEADER)
                    splicer->outputStruct->videoCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

                ALOGI("Output encoder W * H = %d * %d pix_fmt %d den %d num %d!",
                        splicer->outputStruct->videoCodecCtx->width, splicer->outputStruct->videoCodecCtx->height,
                        splicer->outputStruct->videoCodecCtx->pix_fmt,
                        splicer->outputStruct->videoCodecCtx->time_base.den, splicer->outputStruct->videoCodecCtx->time_base.num);

                nRC = avcodec_open2(splicer->outputStruct->videoCodecCtx, enc, NULL);
                if(0 > nRC) {
                    ALOGE("Cannot open output encoder !");
                    avcodec_free_context(&splicer->outputStruct->videoCodecCtx);
                    splicer->outputStruct->videoCodecCtx = NULL;
                    goto ERROR;
                }

                nRC = avcodec_parameters_from_context(splicer->outputStruct->videoStream->codecpar, splicer->outputStruct->videoCodecCtx);
                if (0 > nRC) {
                    ALOGE("Could not initialize video stream parameters, nRC %d : %s", nRC, av_err2str(nRC));
                    goto ERROR;
                }

            } else {
                ALOGE("Cannot alloc video encdeor !");
                nRC = -1;
                goto ERROR;
            }
        } else {
            ALOGE("Cannot find video encoder id %d", splicer->inputStruct->videoCodecCtx->codec_id);
            nRC = -1;
            goto ERROR;
        }

        splicer->outputStruct->videoFrame = av_frame_alloc();
        if(NULL == splicer->outputStruct->videoFrame) {
            ALOGE("Cannot alloc frame for video scaler output !");
            goto ERROR;
        }

        splicer->outputStruct->videoFrame->format = splicer->outputStruct->videoCodecCtx->pix_fmt;
        splicer->outputStruct->videoFrame->width  = splicer->outputStruct->videoCodecCtx->width;
        splicer->outputStruct->videoFrame->height = splicer->outputStruct->videoCodecCtx->height;
        nRC = av_frame_get_buffer(splicer->outputStruct->videoFrame, 32);
        if(0 > nRC) {
            ALOGE("Cannot get buffer for video scaler output frame !");
            goto ERROR;
        }
    } else {
        nRC = avcodec_parameters_copy(splicer->outputStruct->videoStream->codecpar, splicer->inputStruct->videoStream->codecpar);
        if (0 > nRC) {
            ALOGE("Failed to copy codec parameters !!!");
            goto ERROR;
        }
    }
    splicer->outputStruct->videoStream->codecpar->codec_tag = 0;

    av_dump_format(splicer->outputStruct->formatContext, 0, output, 1);

    if (!(splicer->outputStruct->formatContext->flags & AVFMT_NOFILE)) {
        nRC = avio_open(&splicer->outputStruct->formatContext->pb, output, AVIO_FLAG_WRITE);
        if (0 > nRC) {
            ALOGE("Could not open output file '%s'", output);
            goto ERROR;
        }
    }

    nRC = avformat_write_header(splicer->outputStruct->formatContext, NULL);
    if(0 > nRC) {
        ALOGE("Error occurred when opening output file nRC %d : %s!!!", nRC, av_err2str(nRC));
        goto ERROR;
    }

ERROR:

    return nRC;
}

int NIOVideoSplicer::OpenInputDecoder(FileInfoStruct *input, enum AVMediaType type)
{
    if(NULL == input) {
        ALOGE("OpenInputDecoder with NULL parameter !");
        return -1;
    }

    int nRC = 0;
    AVDictionary *opts = NULL;
    AVCodecContext **dec_ctx;
    AVStream *st = NULL;
    AVCodec *dec = NULL;

    if(AVMEDIA_TYPE_AUDIO == type) {
        st = input->audioStream;
        dec_ctx = &input->audioCodecCtx;
    } else if(AVMEDIA_TYPE_VIDEO == type) {
        st = input->videoStream;
        dec_ctx = &input->videoCodecCtx;
    }

    if(NULL != *dec_ctx) {
        ALOGI("%s video decoder opened already", input->path);
        return 0;
    }

    if(NULL == st) {
        ALOGE("OpenInputDecoder Cannot find input stream !");
        return -1;
    }

    dec = avcodec_find_decoder(st->codecpar->codec_id);
    if (NULL == dec) {
        ALOGE("Failed to find %s codec", av_get_media_type_string(type));
        return AVERROR(EINVAL);
    }

    *dec_ctx = avcodec_alloc_context3(dec);
    if (NULL == *dec_ctx) {
        ALOGE("Failed to allocate the %s codec context", av_get_media_type_string(type));
        return AVERROR(ENOMEM);
    }

    if ((nRC = avcodec_parameters_to_context(*dec_ctx, st->codecpar)) < 0) {
        ALOGE("Failed to copy %s codec parameters to decoder context", av_get_media_type_string(type));
        return nRC;
    }

    av_dict_set(&opts, "refcounted_frames", "0", 0);
    if ((nRC = avcodec_open2(*dec_ctx, dec, &opts)) < 0) {
        ALOGE("Failed to open %s codec", av_get_media_type_string(type));
        return nRC;
    }

    return 0;
}

int NIOVideoSplicer::Remuxing(NIOVideoSplicer *splicer)
{
    if(NULL == splicer) {
        ALOGE("Remuxing with NULL parameter !");
        return -1;
    }

    if(NULL == splicer->inputStruct || NULL == splicer->inputStruct->formatContext) {
        ALOGE("Remuxing without input struct !");
        return -1;
    }

    if(NULL == splicer->outputStruct || NULL == splicer->outputStruct->formatContext) {
        ALOGE("Remuxing without output struct !");
        return -1;
    }

    int nRC = 0, readReturn = 0, pkt_type = 0;

    AVPacket in_pkt;
    AVPacket out_pkt;
    AVPacket *mux_pkt = &out_pkt;
    memset(&in_pkt, 0, sizeof(AVPacket));
    memset(&out_pkt, 0, sizeof(AVPacket));

    nRC = ReadPacket(splicer, &in_pkt);
    if(0 > nRC && AVERROR_EOF != nRC) {
        goto ERROR;
    }

    if(-1 == in_pkt.stream_index) {
        nRC = 0;
        av_packet_unref(&in_pkt);
        goto ERROR;
    }

    if(in_pkt.stream_index == splicer->outputStruct->audioStream->index)
        pkt_type = AVMEDIA_TYPE_AUDIO;
    else if (in_pkt.stream_index == splicer->outputStruct->videoStream->index)
        pkt_type = AVMEDIA_TYPE_VIDEO;
    else
        pkt_type = AVMEDIA_TYPE_UNKNOWN;

    if(0 <= nRC) {
        AdjustTimestamp(splicer, &in_pkt);

        if(in_pkt.stream_index == splicer->outputStruct->audioStream->index) {
            splicer->audioPrePTS = in_pkt.pts;
            splicer->audioPreDTS = in_pkt.dts;
        } else {
            splicer->videoPrePTS = in_pkt.pts;
            splicer->videoPreDTS = in_pkt.dts;
        }
    }
    readReturn = nRC;

    if((AVERROR_EOF == readReturn || AVMEDIA_TYPE_AUDIO == pkt_type) && 0 != splicer->needAudioTranscoding) {
        nRC = splicer->AudioTranscoding(splicer, &in_pkt, &out_pkt);
        pkt_type = AVMEDIA_TYPE_AUDIO;
        av_packet_unref(&in_pkt);
        if(0 > nRC && AVERROR_EOF != nRC) {
            goto ERROR;
        }
        if(AVERROR_EOF != nRC && 0 < out_pkt.size)
            goto MUXING;
    }
    if (((AVERROR_EOF == readReturn) || AVMEDIA_TYPE_VIDEO == pkt_type) && 0 != splicer->needVideoTranscoding) {
        nRC = splicer->VideoTranscoding(splicer, &in_pkt, &out_pkt);
        pkt_type = AVMEDIA_TYPE_VIDEO;
        av_packet_unref(&in_pkt);
        if(0 > nRC && AVERROR_EOF != nRC)
            goto ERROR;
    }

MUXING:

    if(0 != splicer->inputStruct->eof && splicer->currentDemuxIndex + 1 < splicer->inputCount
            && (0 == splicer->needAudioTranscoding || (0 != splicer->needAudioTranscoding && 0 != splicer->inputStruct->audioCodecEof))
            && (0 == splicer->needVideoTranscoding || (0 != splicer->needVideoTranscoding && 0 != splicer->inputStruct->videoCodecEof))) {
        splicer->currentDemuxIndex ++;

        splicer->CloseFileInfoStruct(splicer->inputStruct, TYPE_INPUT);

        nRC = splicer->OpenInput(splicer);
        ALOGI("%dth input file muxed form audio pts %lld dts %lld video pts %lld dts %lld nRC %d", splicer->currentDemuxIndex + 1
                , splicer->audioPTSStride, splicer->audioDTSStride, splicer->videoPTSStride, splicer->videoDTSStride, nRC);
        if(0 > nRC)
            goto ERROR;
    }

    if(splicer->currentDemuxIndex + 1 > splicer->inputCount) {
        if((0 == splicer->needAudioTranscoding || (0 != splicer->needAudioTranscoding && 0 != splicer->outputStruct->audioCodecEof))
                && (0 == splicer->needVideoTranscoding || (0 != splicer->needVideoTranscoding && 0 != splicer->outputStruct->videoCodecEof))) {
            ALOGI("Process Finished, current input %d input count %d, audio transcoding %d output eof %d, video transcoding %d output eof %d", splicer->currentDemuxIndex, splicer->inputCount
                    , splicer->needAudioTranscoding, splicer->outputStruct->audioCodecEof, splicer->needVideoTranscoding, splicer->outputStruct->videoCodecEof);
            nRC = AVERROR_EOF;
            goto ERROR;
        }
    }

    if((AVMEDIA_TYPE_AUDIO == pkt_type && 0 == splicer->needAudioTranscoding)
            || (AVMEDIA_TYPE_VIDEO == pkt_type && 0 == splicer->needVideoTranscoding)) {
        mux_pkt = &in_pkt;
    }

    if(0 < mux_pkt->size) {
        nRC = splicer->MuxingPacket(splicer, mux_pkt);
        av_packet_unref(mux_pkt);
    }
    mux_pkt = NULL;

ERROR:
    if(0 > nRC) {
        splicer->FinishProcess(splicer, nRC);
    }

    return nRC;
}

int NIOVideoSplicer::ReadPacket(NIOVideoSplicer *splicer, AVPacket *pkt)
{
    if(NULL == splicer || NULL == pkt) {
        ALOGE("ReadPacket with NULL parameter splicer %p pkt %p !", splicer, pkt);
        return -1;
    }

    int nRC = 0;
    int64_t pts2ms;
    AVFormatContext **formatContext  = &splicer->inputStruct->formatContext;
    if(NULL == *formatContext || 0 != splicer->inputStruct->eof) {
        nRC = AVERROR_EOF;
        goto ERROR;
    }

    nRC = av_read_frame(*formatContext, pkt);
    if(0 > nRC) {
        if(AVERROR_EOF == nRC) {
            ALOGI("av_read_frame reached EOS: %s", av_err2str(nRC));
            splicer->inputStruct->eof = 1;
            splicer->inputStruct->needFlushAudioCodec = 1;
            splicer->inputStruct->needFlushVideoCodec = 1;
            pkt->stream_index = -1; // Dropped this steam packet.
        } else {
            ALOGE("av_read_frame error %d : %s", nRC, av_err2str(nRC));
        }

        goto ERROR;
    }

    if(pkt->stream_index == splicer->inputStruct->audioStream->index) {
        pkt->stream_index = splicer->outputStruct->audioStream->index;
    } else if(pkt->stream_index == splicer->inputStruct->videoStream->index) {
        pts2ms = pkt->pts * av_q2d(splicer->inputStruct->videoStream->time_base) * 1000;
        pkt->stream_index = splicer->outputStruct->videoStream->index;
    } else
        pkt->stream_index = -1; // Dropped this steam packet.

ERROR:

    // Callback to APP side to inform progress
    if(NULL != splicer->callback && 0 < splicer->inputStruct->duration) {
        if((pts2ms - splicer->lastCallbackPTS) * 100 / splicer->inputStruct->duration > 1) {
            splicer->callback(NATIVE_INFO, 100 * pts2ms / splicer->inputStruct->duration, splicer->currentDemuxIndex + 1, NULL);
            splicer->lastCallbackPTS = pts2ms;
        }
        if(AVERROR_EOF == nRC) {
            splicer->callback(NATIVE_INFO, 100, splicer->currentDemuxIndex + 1, NULL);
            splicer->lastCallbackPTS = 0;
        }
    }

    return nRC;
}

int NIOVideoSplicer::AudioTranscoding(NIOVideoSplicer *splicer, AVPacket *in_pkt, AVPacket *out_pkt)
{
    if(NULL == splicer || NULL == splicer->inputStruct || NULL == in_pkt || NULL == out_pkt) {
        ALOGE("AudioTranscoding with NULL parameter splicer %p in_pkt %p out_pkt %p!", splicer, in_pkt, out_pkt);
        return -1;
    }

    int nRC = 0;
    AVPacket **pkt = &in_pkt;
    if(0 == splicer->needAudioTranscoding)
        return 0;

    if(NULL == splicer->inputStruct->audioCodecCtx) {
       nRC =  splicer->OpenInputDecoder(splicer->inputStruct, AVMEDIA_TYPE_AUDIO);
       if(0 > nRC)
            goto ERROR;
    }

    if(NULL == splicer->inputStruct->audioCodecCtx) {
        ALOGE("AudioTranscoding without audio codec context !");
        nRC = -1;
        goto ERROR;
    }

    if(0 != splicer->inputStruct->needFlushAudioCodec) {
        splicer->inputStruct->needFlushAudioCodec = 0;
        *pkt = NULL;
    }

    if(NULL == *pkt || (0 < (*pkt)->size && NULL != (*pkt)->data)) {
        nRC = avcodec_send_packet(splicer->inputStruct->audioCodecCtx, *pkt);
        if(NULL == *pkt)
            ALOGI("Decoder audio avcodec_send_packet with NULL, return %d", nRC);

        if(AVERROR(EAGAIN) == nRC) {
            nRC = 0;
            ALOGI("Decoder audio avcodec_send_packet received EAGAIN !");
            goto ERROR;
        } else if(AVERROR_EOF == nRC) {
            ALOGI("Decoder audio avcodec_send_packet received AVERROR_EOF");
            nRC = 0;
        } else if(0 > nRC) {
            ALOGE("Decoder audio Error while sending a packet to the decoder nRC %d : %s!", nRC, av_err2str(nRC));
            goto ERROR;
        }
    }

    if(0 == splicer->inputStruct->audioCodecEof) {
        nRC = avcodec_receive_frame(splicer->inputStruct->audioCodecCtx, splicer->inputStruct->audioFrame);
        if(AVERROR(EAGAIN) == nRC) {
            nRC = 0;
            ALOGI("Decoder audio avcodec_receive_frame received EAGAIN");
            goto ERROR;
        } else if (AVERROR_EOF == nRC) {
            ALOGI("Decoder audio avcodec_receive_frame received AVERROR_EOF");
            splicer->inputStruct->audioCodecEof = 1;
            if(splicer->currentDemuxIndex + 1 >= splicer->inputCount) {
                splicer->outputStruct->needFlushAudioCodec = 1;
                nRC = 0;
            } else {
                goto ERROR;
            }
        } else if (0 > nRC) {
            ALOGE("Decoder audio Error while receiving a frame from the decoder nRC %d : %s!", nRC, av_err2str(nRC));
            goto ERROR;
        }
    }

    if(NULL == splicer->inputStruct->audioResampleCtx) {
        nRC =  splicer->CreateAudioResampleContext(splicer->inputStruct, splicer->outputStruct);
        if(0 > nRC )
            goto ERROR;
    }
    if(NULL == splicer->inputStruct->audioResampleCtx) {
        ALOGE("Audio transcoding without audio resample context !");
        nRC = -1;
        goto ERROR;
    }

    splicer->outputStruct->audioFrame->nb_samples = splicer->outputStruct->audioCodecCtx->frame_size;
    nRC = AudioResampling(splicer, splicer->inputStruct->audioFrame, splicer->outputStruct->audioFrame);
    if(0 > nRC) {
        goto ERROR;
    }

    if(splicer->outputStruct->audioFrame->nb_samples > 0) {
        nRC = avcodec_send_frame(splicer->outputStruct->audioCodecCtx, splicer->outputStruct->audioFrame);
    } else if (0 == av_audio_fifo_size(splicer->outputStruct->audioFifo) && 0 != splicer->inputStruct->audioCodecEof && splicer->currentDemuxIndex + 1 >= splicer->inputCount) {
        nRC = avcodec_send_frame(splicer->outputStruct->audioCodecCtx, NULL);
        ALOGI("Encoder audio avcodec_send_frame with NULL, return %d", nRC);
    }
    if(AVERROR(EAGAIN) == nRC) {
        nRC = 0;
        ALOGI("Encoder audio avcodec_send_frame received EAGAIN");
        goto ERROR;
    } else if(AVERROR_EOF == nRC) {
        ALOGI("Encoder audio avcodec_send_frame received AVERROR_EOF");
        nRC = 0;
    } else if(0 > nRC) {
        ALOGE("Encoder audio Error while sending a frame to audio encoder nRC %d : %s!", nRC, av_err2str(nRC));
        goto ERROR;
    }

    nRC = avcodec_receive_packet(splicer->outputStruct->audioCodecCtx, out_pkt);
    if(AVERROR(EAGAIN) == nRC) {
        nRC = 0;
        ALOGI("Encoder audio avcodec_receive_packet received EAGAIN");
        goto ERROR;
    } else if(AVERROR_EOF == nRC) {
        ALOGI("Encoder audio avcodec_receive_packet received AVERROR_EOF");
        nRC = AVERROR_EOF;
        splicer->outputStruct->audioCodecEof = 1;
        goto ERROR;
    } else if(0 > nRC) {
        ALOGE("Encoder audio Error while sending a frame to audio encoder nRC %d : %s!", nRC, av_err2str(nRC));
        goto ERROR;
    }
    out_pkt->stream_index = splicer->outputStruct->audioStream->index;

ERROR:

    return nRC;
}

int NIOVideoSplicer::AudioResampling(NIOVideoSplicer *splicer, AVFrame *in_frame, AVFrame *out_frame)
{
    if(NULL == splicer || NULL == in_frame || NULL == out_frame) {
        ALOGE("AudioResampling error with NULL parameter(s) splicer %p in_frame %p out_frame %p", splicer, in_frame, out_frame);
        return -1;
    }

    if(NULL == splicer->inputStruct || NULL == splicer->outputStruct) {
        ALOGE("AudioResampling error without inputStruct %p outputStruct %p ", splicer->inputStruct, splicer->outputStruct);
        return -1;
    }

    if(NULL == splicer->inputStruct->audioResampleCtx || NULL == splicer->outputStruct->audioCodecCtx || NULL == splicer->outputStruct->audioFifo) {
        ALOGE("AudioResampling error without input audioResampleCtx %p out audioCodecCtx %p audioFifo %p",
                splicer->inputStruct->audioResampleCtx, splicer->outputStruct->audioCodecCtx, splicer->outputStruct->audioFifo);
        return -1;
    }

    int nRC = 0, storedSize = 0, neededSize = 0, convertedSize = 0;
    uint8_t* m_ain[SWR_CH_MAX];

    if(0 == splicer->inputStruct->audioCodecEof) {
        if (av_sample_fmt_is_planar(splicer->inputStruct->audioCodecCtx->sample_fmt)) {
            for (int i = 0; i < in_frame->channels; i++) {
                m_ain[i] = in_frame->data[i];
            }
        } else {
            m_ain[0] = in_frame->data[0];
        }

        convertedSize = swr_convert(splicer->inputStruct->audioResampleCtx, out_frame->data, out_frame->nb_samples, (const uint8_t**)m_ain, in_frame->nb_samples);
        if (0 > convertedSize) {
            ALOGE("Could not convert input samples convertedSize %d : %s", convertedSize, av_err2str(convertedSize));
            goto ERROR;
        }

        storedSize = av_audio_fifo_size(splicer->outputStruct->audioFifo);
        nRC = av_audio_fifo_realloc(splicer->outputStruct->audioFifo, storedSize + convertedSize);
        if(0 > nRC) {
            ALOGE("Could not reallocate audio FIFO, storedSize %d convertedSize %d nRC %d : %s", storedSize, convertedSize, nRC, av_err2str(nRC));
            goto ERROR;
        }

        nRC = av_audio_fifo_write(splicer->outputStruct->audioFifo, (void **)out_frame->data, convertedSize);
        if(nRC < convertedSize) {
            ALOGE("Could not write data to audio FIFO, nRC %d convertedSize %d : %s", nRC, convertedSize, av_err2str(nRC));
            goto ERROR;
        }
    }

    neededSize = splicer->outputStruct->audioCodecCtx->frame_size;
    storedSize = av_audio_fifo_size(splicer->outputStruct->audioFifo);

    if(0 >= storedSize) {
        out_frame->nb_samples = 0;
        nRC = 0;
        goto ERROR;
    } else if (storedSize < neededSize) {
        if (0 == splicer->inputStruct->audioCodecEof || (1 == splicer->inputStruct->audioCodecEof && splicer->currentDemuxIndex + 1 < splicer->inputCount)) {
            out_frame->nb_samples = 0;
            nRC = 0;
            goto ERROR;
        } else {
            neededSize = storedSize;
        }
    }

    nRC = av_audio_fifo_read(splicer->outputStruct->audioFifo, (void **)out_frame->data, neededSize);
    if(nRC < neededSize) {
        ALOGE("Read audio from FIFO need %d return %d : %s", neededSize, nRC, av_err2str(nRC));
        nRC = -1;
        goto ERROR;
    }
    out_frame->nb_samples = neededSize;
    out_frame->pts = splicer->outputStruct->audioPTS;
    splicer->outputStruct->audioPTS += out_frame->nb_samples;

ERROR:

    return nRC;
}

int NIOVideoSplicer::VideoTranscoding(NIOVideoSplicer *splicer, AVPacket *in_pkt, AVPacket *out_pkt)
{
    if(NULL == splicer || NULL == splicer->inputStruct || NULL == in_pkt || NULL == out_pkt) {
        ALOGE("VideoTranscoding with NULL parameter splicer %p in_pkt %p out_pkt %p!", splicer, in_pkt, out_pkt);
        return -1;
    }

    int nRC = 0;
    AVPacket **pkt = &in_pkt;
    if(0 == splicer->needVideoTranscoding)
        return 0;

    if(NULL == splicer->inputStruct->videoFrame || NULL == splicer->outputStruct->videoFrame) {
        ALOGE("Cannot do scale without temp frame, input %p output %p", splicer->inputStruct->videoFrame, splicer->outputStruct->videoFrame);
        return -1;
    }

    if(NULL == splicer->inputStruct->videoCodecCtx) {
       nRC =  splicer->OpenInputDecoder(splicer->inputStruct, AVMEDIA_TYPE_VIDEO);
       if(0 > nRC)
            goto ERROR;
    }

    if(NULL == splicer->inputStruct->videoCodecCtx) {
        ALOGE("VideoTranscoding without video codec context !");
        nRC = -1;
        goto ERROR;
    }

    if(0 != splicer->inputStruct->needFlushVideoCodec) {
        splicer->inputStruct->needFlushVideoCodec = 0;
        *pkt = NULL;
    }

    if(NULL == *pkt || (0 < (*pkt)->size && NULL != (*pkt)->data)) {
        nRC = avcodec_send_packet(splicer->inputStruct->videoCodecCtx, *pkt);
        if(NULL == *pkt)
            ALOGI("Decoder video avcodec_send_packet with NULL, return %d", nRC);

        if(AVERROR(EAGAIN) == nRC) {
            nRC = 0;
            ALOGI("Decoder video avcodec_send_packet received EAGAIN !");
            goto ERROR;
        } else if(AVERROR_EOF == nRC) {
            ALOGI("Decoder video avcodec_send_packet received AVERROR_EOF");
            nRC = 0;
        } else if(0 > nRC) {
            ALOGE("Decoder video Error while sending a packet to the decoder nRC %d : %s!", nRC, av_err2str(nRC));
            goto ERROR;
        }
    }

    if(0 == splicer->inputStruct->videoCodecEof) {
        nRC = avcodec_receive_frame(splicer->inputStruct->videoCodecCtx, splicer->inputStruct->videoFrame);
        if(AVERROR(EAGAIN) == nRC) {
            nRC = 0;
            ALOGI("Decoder video avcodec_receive_frame received EAGAIN");
            goto ERROR;
        } else if (AVERROR_EOF == nRC) {
            ALOGI("Decoder video avcodec_receive_frame received AVERROR_EOF");
            splicer->inputStruct->videoCodecEof = 1;
            if(splicer->currentDemuxIndex + 1 >= splicer->inputCount) {
                splicer->outputStruct->needFlushVideoCodec = 1;
                nRC = 0;
            } else {
                goto ERROR;
            }
        } else if (0 > nRC) {
            ALOGE("Decoder video Error while receiving a frame from the decoder nRC %d : %s!", nRC, av_err2str(nRC));
            goto ERROR;
        }
    }

#ifdef USE_AV_FILTER
    if(NULL == splicer->inputStruct->videoFilter) {
        nRC = splicer->CreateVideoFilter(splicer, splicer->inputStruct, splicer->outputStruct);
        if(0 > nRC)
            goto ERROR;
    }
    if(NULL == splicer->inputStruct->videoFilter) {
        ALOGE("Video transcoding without filter !");
        nRC = -1;
        goto ERROR;
    }
#else
    if(NULL == splicer->inputStruct->videoScalerCtx) {
        nRC =  splicer->CreateVideoScaler(splicer->inputStruct, splicer->outputStruct);
        if(0 > nRC )
            goto ERROR;
    }
    if(NULL == splicer->inputStruct->videoScalerCtx) {
        ALOGE("Video transcoding without scaler !");
        nRC = -1;
        goto ERROR;
    }
#endif // USE_AV_FILTER

    if(0 == splicer->inputStruct->videoCodecEof) {
#ifdef USE_AV_FILTER
        /* push the decoded frame into the filtergraph */
        nRC = av_buffersrc_add_frame_flags(splicer->inputStruct->buffersrc_ctx, splicer->inputStruct->videoFrame, AV_BUFFERSRC_FLAG_KEEP_REF);
        if(0 > nRC){
            ALOGE("Error while feeding the filtergraph, nRC %d : %s", nRC, av_err2str(nRC));
            goto ERROR;
        }

        nRC = av_buffersink_get_frame(splicer->inputStruct->buffersink_ctx, splicer->outputStruct->videoFrame);
        if (AVERROR(EAGAIN) == nRC) {
            nRC = 0;
            ALOGI("av_buffersink_get_frame return EAGAIN");
            goto ERROR;
        } else if (AVERROR_EOF == nRC) {
            nRC = 0;
            ALOGI("av_buffersink_get_frame return AVERROR_EOF");
            goto ERROR;
        } else if (0 > nRC) {
            ALOGI("av_buffersink_get_frame error, nRC %d : %s", nRC, av_err2str(nRC));
            goto ERROR;
        }
#else
        nRC = sws_scale(splicer->inputStruct->videoScalerCtx, (const uint8_t * const *)splicer->inputStruct->videoFrame->data, splicer->inputStruct->videoFrame->linesize, 0,
                splicer->inputStruct->videoFrame->height, splicer->outputStruct->videoFrame->data, splicer->outputStruct->videoFrame->linesize);
        if(0 > nRC) {
            ALOGE("sws_scale error, nRC %d : %s", nRC, av_err2str(nRC));
            goto ERROR;
        }
#endif // USE_AV_FILTER

        splicer->outputStruct->videoFrame->pts = splicer->inputStruct->videoFrame->pts;
    }

    if(0 == splicer->inputStruct->videoCodecEof || 0 != splicer->outputStruct->needFlushVideoCodec) {
        if(0 == splicer->outputStruct->needFlushVideoCodec) {
            nRC = avcodec_send_frame(splicer->outputStruct->videoCodecCtx, splicer->outputStruct->videoFrame);
        } else {
            nRC = avcodec_send_frame(splicer->outputStruct->videoCodecCtx, NULL);
            splicer->outputStruct->needFlushVideoCodec = 0;
            ALOGI("Encoder video avcodec_send_frame with NULL, return %d", nRC);
        }

        if(AVERROR(EAGAIN) == nRC) {
            nRC = 0;
            ALOGI("Encoder video avcodec_send_frame received EAGAIN");
            goto ERROR;
        } else if(AVERROR_EOF == nRC) {
            ALOGI("Encoder video avcodec_send_frame received AVERROR_EOF");
            nRC = 0;
        } else if(0 > nRC) {
            ALOGE("Encoder video Error while sending a frame to video encoder nRC %d : %s!", nRC, av_err2str(nRC));
            goto ERROR;
        }
    }

    nRC = avcodec_receive_packet(splicer->outputStruct->videoCodecCtx, out_pkt);
    if(AVERROR(EAGAIN) == nRC) {
        nRC = 0;
        ALOGI("Encoder video avcodec_receive_packet received EAGAIN");
        goto ERROR;
    } else if(AVERROR_EOF == nRC) {
        ALOGI("Encoder video avcodec_receive_packet received AVERROR_EOF");
        nRC = AVERROR_EOF;
        splicer->outputStruct->videoCodecEof = 1;
        goto ERROR;
    } else if(0 > nRC) {
        ALOGE("Encoder Error while sending a frame to video encoder nRC %d : %s!", nRC, av_err2str(nRC));
        goto ERROR;
    }
    out_pkt->stream_index = splicer->outputStruct->videoStream->index;

ERROR:

    return nRC;
}

int NIOVideoSplicer::MuxingPacket(NIOVideoSplicer *splicer, AVPacket *pkt)
{
    if(NULL == splicer || NULL == pkt){
        ALOGE("MuxingPacket with NULL parameter splicer %p pkt %p !", splicer, pkt);
        return -1;
    }

    if(NULL == splicer->outputStruct || NULL == splicer->outputStruct->formatContext) {
        ALOGE("MuxingPacket without output struct !");
        return -1;
    }

    if(NULL == splicer->outputStruct->audioStream || NULL == splicer->outputStruct->videoStream) {
        ALOGE("MuxingPacket without output audio %p video %p stream !", splicer->outputStruct->audioStream, splicer->outputStruct->videoStream);
        return -1;
    }

    int nRC = av_interleaved_write_frame(splicer->outputStruct->formatContext, pkt);
    if(0 > nRC){
        ALOGE("Error during muxing pkt : %s", av_err2str(nRC));
        goto ERROR;
    }

ERROR:

    return nRC;
}

int NIOVideoSplicer::CreateAudioResampleContext(FileInfoStruct *input, FileInfoStruct *output)
{
    int nRC = 0;

    if(NULL == input || NULL == output) {
        ALOGE("CreateAudioResampleContext with NULL parameter input %p output %p", input, output);
        return -1;
    }

    if(NULL == input->audioCodecCtx || NULL == output->audioCodecCtx) {
        ALOGE("CreateAudioResampleContext with NULL codec context input %p output %p", input->audioCodecCtx, output->audioCodecCtx);
        return -1;
    }

    if(NULL != input->audioResampleCtx)
        swr_free(&input->audioResampleCtx);

    input->audioResampleCtx = swr_alloc_set_opts(NULL,
                                              av_get_default_channel_layout(output->audioCodecCtx->channels),
                                              output->audioCodecCtx->sample_fmt,
                                              output->audioCodecCtx->sample_rate,
                                              av_get_default_channel_layout(input->audioCodecCtx->channels),
                                              input->audioCodecCtx->sample_fmt,
                                              input->audioCodecCtx->sample_rate,
                                              0, NULL);
    if(NULL == input->audioResampleCtx) {
        ALOGE("swr_alloc_set_opts error !");
        nRC = -1;
        goto ERROR;
    }

    nRC = swr_init(input->audioResampleCtx);
    if(0 > nRC) {
        ALOGE("swr_init error, nRC %d, msg %s", nRC, av_err2str(nRC));
        swr_free(&input->audioResampleCtx);
        input->audioResampleCtx = NULL;
        goto ERROR;
    }

ERROR:

    return nRC;
}

#ifdef USE_AV_FILTER

int NIOVideoSplicer::CreateVideoFilter(NIOVideoSplicer *splicer, FileInfoStruct *input, FileInfoStruct *output)
{
    if(NULL == splicer || NULL == input || NULL == output) {
        ALOGE("CreateVideoFilter wiht NULL parameter splicer %p input %p output %p", splicer, input, output);
        return -1;
    }

    if(NULL == input->videoCodecCtx || NULL == output->videoCodecCtx) {
        ALOGE("CreateVideoFilter wiht NULL codec context input %p output %p", input->videoCodecCtx, output->videoCodecCtx);
        return -1;
    }

    if(NULL == input->videoFrame || NULL == output->videoFrame) {
        ALOGI("NO input %p output %p frame !", input->videoFrame, output->videoFrame);
        return -1;
    }

    int nRC = 0;
    char args[512];
    char *filters_descr = args;
    AVRational time_base = input->videoStream->time_base;
    enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE };

    const AVFilter *buffersrc  = avfilter_get_by_name("buffer");
    const AVFilter *buffersink = avfilter_get_by_name("buffersink");
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs  = avfilter_inout_alloc();

    input->videoFilter = avfilter_graph_alloc();
    if (NULL == outputs || NULL == inputs || NULL == input->videoFilter) {
        ALOGE("CreateVideoFilter alloc filter error. output %p input %p filter %p", outputs, inputs, input->videoFilter);
        nRC = AVERROR(ENOMEM);
        goto ERROR;
    }

    snprintf(args, sizeof(args),
            "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
            input->videoCodecCtx->width, input->videoCodecCtx->height, input->videoCodecCtx->pix_fmt,
            time_base.num, time_base.den,
            input->videoCodecCtx->sample_aspect_ratio.num, input->videoCodecCtx->sample_aspect_ratio.den);

    nRC = avfilter_graph_create_filter(&input->buffersrc_ctx, buffersrc, "in", args, NULL, input->videoFilter);
    if (0 > nRC) {
        ALOGE("Cannot create buffer source, nRC %d : %s", nRC, av_err2str(nRC));
        goto ERROR;
    }

    nRC = avfilter_graph_create_filter(&input->buffersink_ctx, buffersink, "out", NULL, NULL, input->videoFilter);
    if (0 > nRC) {
        ALOGE("Cannot create buffer sink, nRC %d : %s", nRC, av_err2str(nRC));
        goto ERROR;
    }

    nRC = av_opt_set_int_list(input->buffersink_ctx, "pix_fmts", pix_fmts, AV_PIX_FMT_YUV420P, AV_OPT_SEARCH_CHILDREN);
    if (0 > nRC) {
        ALOGE("Cannot set output pixel format, nRC %d : %s", nRC, av_err2str(nRC));
        goto ERROR;
    }

    outputs->name       = av_strdup("in");
    outputs->filter_ctx = input->buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    inputs->name       = av_strdup("out");
    inputs->filter_ctx = input->buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;

    memset(args, 0, sizeof(args));

    if(NULL == splicer->watermarkFile)
        sprintf(args, "[in]scale=%d:%d[out]", output->videoCodecCtx->width, output->videoCodecCtx->height);
    else {
        sprintf(args, "movie=%s, scale=%d:%d [over1], [in][over1] overlay=%d:%d, scale=w=%d:h=%d [out]",
                splicer->watermarkFile,
                (splicer->watermarkRect.right - splicer->watermarkRect.left) * input->videoCodecCtx->width / output->videoCodecCtx->width,
                (splicer->watermarkRect.bottom - splicer->watermarkRect.top) * input->videoCodecCtx->height / output->videoCodecCtx->height,
                splicer->watermarkRect.left * input->videoCodecCtx->width / output->videoCodecCtx->width,
                splicer->watermarkRect.top * input->videoCodecCtx->height / output->videoCodecCtx->height,
                output->videoCodecCtx->width, output->videoCodecCtx->height);

        ALOGI("Watermark: %s,  left/top %d/%d w/h %d/%d, video scale %d/%d",
                splicer->watermarkFile,
                splicer->watermarkRect.left * input->videoCodecCtx->width / output->videoCodecCtx->width,
                splicer->watermarkRect.top * input->videoCodecCtx->height / output->videoCodecCtx->height,
                (splicer->watermarkRect.right - splicer->watermarkRect.left) * input->videoCodecCtx->width / output->videoCodecCtx->width,
                (splicer->watermarkRect.bottom - splicer->watermarkRect.top) * input->videoCodecCtx->height / output->videoCodecCtx->height,
                output->videoCodecCtx->width, output->videoCodecCtx->height);
    }

    nRC = avfilter_graph_parse_ptr(input->videoFilter, filters_descr, &inputs, &outputs, NULL);
    if(0 > nRC) {
        ALOGE("avfilter_graph_parse_ptr error, nRC %d : %s", nRC, av_err2str(nRC));
        goto ERROR;
    }

    nRC = avfilter_graph_config(input->videoFilter, NULL);
    if(0 > nRC) {
        ALOGE("avfilter_graph_config error, nRC %d : %s", nRC, av_err2str(nRC));
        goto ERROR;
    }

ERROR:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return nRC;
}

#else

int NIOVideoSplicer::CreateVideoScaler(FileInfoStruct *input, FileInfoStruct *output)
{
    if(NULL == input || NULL == output) {
        ALOGE("CreateVideoScaler wiht NULL parameter input %p output %p", input, output);
        return -1;
    }

    if(NULL == input->videoFrame || NULL == output->videoFrame) {
        ALOGI("NO input %p / output %p frame !", input->videoFrame, output->videoFrame);
        return -1;
    }

    int nRC = 0;

    if(NULL == input->videoScalerCtx) {
        ALOGI("Create scaler with input W * H = %d * %d pix_fmt %d output W * H = %d * %d pix_fmt %d",
                input->videoFrame->width, input->videoFrame->height, input->videoCodecCtx->pix_fmt,
                output->videoFrame->width, output->videoFrame->height, output->videoCodecCtx->pix_fmt);

        input->videoScalerCtx = sws_getContext(input->videoFrame->width, input->videoFrame->height, input->videoCodecCtx->pix_fmt,
                output->videoFrame->width, output->videoFrame->height, output->videoCodecCtx->pix_fmt, SWS_FAST_BILINEAR, NULL, NULL, NULL);
    }
    if(NULL == input->videoScalerCtx) {
        ALOGE("Cannot get video scaler !");
        nRC = -1;
        goto ERROR;
    }

ERROR:

    return nRC;
}
#endif // USE_AV_FILTER

int NIOVideoSplicer::FinishProcess(NIOVideoSplicer *splicer, int nRC)
{
    if(NULL == splicer)
        return -1;

    splicer->Stop();

    if(NULL != splicer->outputStruct) {
        splicer->CloseFileInfoStruct(splicer->outputStruct, TYPE_OUTPUT);
        delete splicer->outputStruct;
        splicer->outputStruct = NULL;
    }

    if(NULL != splicer->inputStruct) {
        if(NULL != splicer->inputStruct->formatContext) {
            CloseFileInfoStruct(splicer->inputStruct, TYPE_INPUT);
        }
        delete splicer->inputStruct;
        splicer->inputStruct = NULL;
    }

    if(NULL != splicer->callback) {
        if(AVERROR_EOF == nRC) {
            ALOGI("VideoSplicer finished !");
            splicer->callback(NATIVE_PROCESS_FINISHED, nRC, 0, NULL);
        } else
            splicer->callback(NATIVE_ERROR, nRC, 0, NULL);
    }

    return nRC;
}

void NIOVideoSplicer::CloseFileInfoStruct(FileInfoStruct *infoStruct, int type)
{
    if(NULL == infoStruct)
        return;

    if(NULL != infoStruct->audioCodecCtx) {
        avcodec_free_context(&infoStruct->audioCodecCtx);
        infoStruct->audioCodecCtx = NULL;
    }

    if(NULL != infoStruct->videoCodecCtx) {
        avcodec_free_context(&infoStruct->videoCodecCtx);
        infoStruct->videoCodecCtx = NULL;
    }

    if(NULL != infoStruct->audioFrame) {
        av_frame_free(&infoStruct->audioFrame);
        infoStruct->audioFrame = NULL;
    }

    if(NULL != infoStruct->videoFrame) {
        av_frame_free(&infoStruct->videoFrame);
        infoStruct->videoFrame = NULL;
    }

    if(NULL != infoStruct->formatContext) {
        if(TYPE_OUTPUT == type) {
            av_write_trailer(infoStruct->formatContext);

            av_dump_format(infoStruct->formatContext, 0, infoStruct->path, 1);

            if(0 != (infoStruct->formatContext->oformat->flags & AVFMT_NOFILE))
                avio_closep(&infoStruct->formatContext->pb);
            avformat_free_context(infoStruct->formatContext);
        } else
            avformat_close_input(&infoStruct->formatContext);

#ifdef USE_AV_FILTER
        if(NULL != infoStruct->videoFilter) {
            avfilter_graph_free(&infoStruct->videoFilter);
            infoStruct->videoFilter = NULL;
        }
#else
        if(NULL != infoStruct->videoScalerCtx) {
            sws_freeContext(infoStruct->videoScalerCtx);
            infoStruct->videoScalerCtx = NULL;
        }
#endif

        if(NULL != infoStruct->audioResampleCtx) {
            swr_free(&infoStruct->audioResampleCtx);
            infoStruct->audioResampleCtx = NULL;
        }

        if(NULL != infoStruct->audioFifo) {
            av_audio_fifo_free(infoStruct->audioFifo);
            infoStruct->audioFifo = NULL;
        }

        infoStruct->formatContext = NULL;
    }
}

void NIOVideoSplicer::AdjustTimestamp(NIOVideoSplicer *splicer, AVPacket *pkt)
{
    if(NULL == splicer || NULL == pkt) {
        ALOGE("AdjustTimestamp with NULL parameter splicer %p pkt %p", splicer, pkt);
        return;
    }

    AVStream *inStream, *outStream;
    if(pkt->stream_index == splicer->outputStruct->audioStream->index) {
        inStream = splicer->inputStruct->audioStream;
        outStream = splicer->outputStruct->audioStream;
    } else {
        inStream = splicer->inputStruct->videoStream;
        outStream = splicer->outputStruct->videoStream;
    }

    pkt->pts = av_rescale_q_rnd(pkt->pts,
            inStream->time_base,
            outStream->time_base,
            (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));

    pkt->dts = av_rescale_q_rnd(pkt->dts,
            inStream->time_base,
            outStream->time_base,
            (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));


    if(pkt->stream_index == splicer->outputStruct->audioStream->index) {
        pkt->pts += splicer->audioPTSStride;
        pkt->dts += splicer->audioDTSStride;

    } else {
        pkt->pts += splicer->videoPTSStride;
        pkt->dts += splicer->videoDTSStride;

    }

    return;
}

int NIOVideoSplicer::CheckCodecParameters(int type, AVCodecParameters *in, AVCodecParameters *out)
{
    if(NULL == in || NULL == out) {
        ALOGE("CheckCodecParameters %d wiht NULL parameter(s) in %p out %p", type, in, out);
        return -1;
    }

    if(AVMEDIA_TYPE_AUDIO == type) {
        if(in->codec_id != out->codec_id) {
            ALOGI("CheckCodecParameters Audio need transcoding with codec id in %d, out %d", in->codec_id, out->codec_id);
            return 1;
        }

        if(in->channels != out->channels) {
            ALOGI("CheckCodecParameters Audio need transcoding with channels in %d, out %d", in->channels, out->channels);
            return 1;
        }

        if(in->sample_rate != out->sample_rate) {
            ALOGI("CheckCodecParameters Audio need transcoding with sample rate in %d, out %d", in->sample_rate, out->sample_rate);
            return 1;
        }

        return 0;
    } else if(AVMEDIA_TYPE_VIDEO == type) {
        // Check encoder id here
        if(in->codec_id != out->codec_id) {
            ALOGI("CheckCodecParameters Video need transcoding with codec id in %d, out %d", in->codec_id, out->codec_id);
            return 1;
        }

        // Check resolution first.
        if(in->width != out->width || in->height != out->height) {
            ALOGI("CheckCodecParameters Video need transcoding with in W * H = %d * %d, out W * H = %d * %d", in->width, in->height, out->width, out->height);
            return 1;
        }
    }

    return 0;
}
