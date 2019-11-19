#include "../common.h"

using namespace std;
#define USECUDA
#define LOG(ret,error) \
    av_log(NULL, AV_LOG_INFO,error ",Error code: %d,%s,%s:%d\n",ret,av_err2str(ret),__FILE__, __LINE__) \ 

static AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;

static AVCodecContext *decoder_ctx = NULL, *encoder_ctx = NULL;

static AVBufferRef *hw_device_ctx = NULL;

static int video_stream = -1;

static AVStream *iVStream = NULL;

static AVStream *oVStream = NULL;

static enum AVPixelFormat hw_pix_fmt;
//从config中取得hw_pix_fmt是AV_PIX_FMT_CUDA



void initLog()
{
    av_log_set_level(AV_LOG_INFO);
}


static int set_hwframe_ctx(AVCodecContext *ctx, AVBufferRef *hw_device_ctx)
{
    AVBufferRef *hw_frames_ref = NULL;
    AVHWFramesContext *frames_ctx = NULL;
    int err = 0;
    hw_frames_ref = av_hwframe_ctx_alloc(hw_device_ctx);
    if (NULL == hw_frames_ref){
        LOG(-1, "av_hwframe_ctx_alloc failed");
        return -1;
    }

    frames_ctx = (AVHWFramesContext *)(hw_frames_ref -> data);
    frames_ctx -> format = AV_PIX_FMT_CUDA;
    frames_ctx -> sw_format = AV_PIX_FMT_YUV420P;
    frames_ctx -> width = 640;
    frames_ctx -> height = 360;
    frames_ctx -> initial_pool_size = 20;
    err = av_hwframe_ctx_init(hw_frames_ref);
    if (0 > err){
        LOG(err, "Failed to initialize CUDA frame context");
        av_buffer_unref(&hw_frames_ref);
        return err;
    }
    ctx -> hw_frames_ctx = av_buffer_ref(hw_frames_ref);
    if (NULL == ctx -> hw_frames_ctx)
        err = AVERROR(ENOMEM);
    av_buffer_unref(&hw_frames_ref);
    return err;
}

static enum AVPixelFormat get_vaapi_format(AVCodecContext *ctx,const enum AVPixelFormat *pix_fmts)
{
    const enum AVPixelFormat *p;
    for(p = pix_fmts; *p != AV_PIX_FMT_NONE; p++){
        if(*p == hw_pix_fmt){
            return *p;
        }
    }
    LOG(-1, "Failed to get HW surface format");
    return AV_PIX_FMT_NONE;
}


static int open_input_file(const string filename)
{
    AVCodec *dec = NULL;
    int ret = 0;
    //打开输入的文件
    ret = avformat_open_input(&ifmt_ctx, filename.c_str(), NULL, NULL);
    if(0 > ret){
        LOG(ret, "Cannot open input file");
        return ret;
    }
    //查找输入文件中的流信息
    ret = avformat_find_stream_info(ifmt_ctx, NULL);
    if (0 > ret){
        LOG(ret, "Cannot find input stream informat");
        return ret;
    }
    //查找视频流的信息
    ret = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &dec,0);
    if (0 > ret){
        LOG(ret, "Cannot find a video stream in the input file");
        return ret;
    }
    video_stream = ret;
    iVStream = ifmt_ctx -> streams[ret];
#ifdef USECUDA
    //hw 检查是否有硬件支持
    ret = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_CUDA, NULL, NULL, 0);
    if (0 > ret){
        LOG(ret, "Failed to create a CUDA device");
        return -1;
    }
   //hw查找硬件的配置
    for(int i = 0; ; i++){
        const AVCodecHWConfig *config = avcodec_get_hw_config(dec, i);
        if(NULL == config){
            av_log(NULL, AV_LOG_ERROR, "Decoder %s does not support device type %s",  dec -> name, av_hwdevice_get_type_name(AV_HWDEVICE_TYPE_CUDA));
            return -1;
        }
        if (config -> methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX && config -> device_type == AV_HWDEVICE_TYPE_CUDA){
            hw_pix_fmt = config -> pix_fmt;
            break;
        }
    
    }
#endif
    dec = NULL;
    //查找解码的信息
    dec = avcodec_find_decoder(iVStream -> codecpar -> codec_id);
    if (NULL == dec){
        LOG(-1, "Failed to find video decoder");
        return -1;
    }
    //为解码上下文创建空间
    decoder_ctx = avcodec_alloc_context3(dec);
    if (NULL == decoder_ctx)
        return AVERROR(ENOMEM);

    //为解码上下文赋值
    ret = avcodec_parameters_to_context(decoder_ctx, iVStream -> codecpar);
    if (0 > ret){
        LOG(ret, "avcodec_parameters_to_context error");
        return ret;
    }
    decoder_ctx -> framerate = av_guess_frame_rate(ifmt_ctx, iVStream, NULL);
#ifdef USECUDA
    //hw添加硬件解码,硬件上下文拷贝一份然后赋值给解码器上下文的hw_device_ctx
    decoder_ctx -> hw_device_ctx = av_buffer_ref(hw_device_ctx);

    if (NULL == decoder_ctx -> hw_device_ctx){
        LOG(-1, "A hardware device reference create failed");
        return -1;
    }
    //hw添加函数,get_format 是一个函数，返回AV_PIX_FMT_CUDA,而不是AV_PIX_FMT_YUV420P 
    decoder_ctx -> get_format = get_vaapi_format;
#endif
    //打开解码器
    AVDictionary *opts = NULL;
    av_dict_set(&opts, "refcounted_frames", "0", 0);//frame 的分配和释放由ffmpeg自己控制
    
    //打开解码器
    ret = avcodec_open2(decoder_ctx, dec, &opts);
    if (0 > ret){
        LOG(ret, "Failed to open codec for decoding");
    }


}

static int open_output_file(const string filename)
{
    int ret;
    //打开输出的文件
    ret = avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, filename.c_str());
    if (0 > ret){
        LOG(ret, "Open output file failed");
        return ret;
    }

    //创建视频流
    oVStream = avformat_new_stream(ofmt_ctx, NULL);
    if (NULL == oVStream){
        LOG(-1, "Cannot new video stream for output");
        return -1;
    }

    //设置编码信息
    AVCodec *enc = NULL;
    //enc = avcodec_find_encoder_by_name("nvenc_h264");
#ifdef USECUDA 
    enc = avcodec_find_encoder_by_name("h264_nvenc");
#else 
    enc = avcodec_find_encoder(AV_CODEC_ID_H264);
#endif
    if (NULL == enc){
        LOG(-1, "Could not find encoder");
        return -1;
    }
    //创建空间
    encoder_ctx = avcodec_alloc_context3(enc);
    if (NULL == encoder_ctx){
        LOG(-1, "Cannot alloc video encoder context");
        return -1;
    }


    encoder_ctx -> time_base = av_inv_q(decoder_ctx -> framerate);
#ifdef USECUDA  
    encoder_ctx -> pix_fmt = hw_pix_fmt;
#else 
    encoder_ctx -> pix_fmt = enc -> pix_fmts[0];
#endif
    encoder_ctx -> width = decoder_ctx -> width;
    encoder_ctx -> height = decoder_ctx -> height;
    //encoder_ctx -> codec_id = enc -> id;
    //encoder_ctx -> codec_type = AVMEDIA_TYPE_VIDEO;
#ifdef USECUDA 
    //hw 添加硬件
    ret = set_hwframe_ctx(encoder_ctx, hw_device_ctx);
    if (0 > ret){
        LOG(ret, "Failed to set hwframe context\n");
        return ret;
    }
#endif

    //打开编码器
    ret = avcodec_open2(encoder_ctx, enc, NULL);
    if (0 > ret){
        LOG(ret, "Cannot open video output codec");
        return ret;
    }
    //
    oVStream -> time_base = encoder_ctx -> time_base;

    ret = avcodec_parameters_from_context(oVStream -> codecpar, encoder_ctx);
    if (0 > ret){
        LOG(ret, "Cannot initialize video stream parameters");
        return ret;
    }
    oVStream -> codecpar -> codec_tag = 0;
    
    
    
    //打开文件
    ret = avio_open(&ofmt_ctx -> pb, filename.c_str(), AVIO_FLAG_READ_WRITE);
    if(0 > ret){
        LOG(ret, "Could not open output file");
        return ret;
    }
    //写入文件头
    ret = avformat_write_header(ofmt_ctx, NULL);
    if (0 > ret){
        LOG(ret, "write header failed");
        return ret;
    }

    return 0;

}

static int encode_write(AVFrame *frame)
{
    int ret = 0;
    AVPacket enc_pkt;

    av_init_packet(&enc_pkt);
    enc_pkt.data = NULL;
    enc_pkt.size = 0;

    ret = avcodec_send_frame(encoder_ctx, frame);
    if (0 > ret){
        LOG(ret, "Error during encoding");
        goto end;
    }
    
    while(true){
        ret = avcodec_receive_packet(encoder_ctx, &enc_pkt);
        if (AVERROR(EAGAIN) == ret){
            ret = AVERROR(EAGAIN);
            LOG(ret, "THe avcodec_receive_packet return EAGAIN");
            break;
        }else if (AVERROR_EOF == ret){
            ret = AVERROR_EOF;
            LOG(ret, "The avcodec_receive_packet return AVERROR_EOF");
            break;
        }else if ( 0 > ret){
            LOG(ret, "Could not encoder frame");
            break;
        }
        enc_pkt.stream_index = 0;
        av_packet_rescale_ts(&enc_pkt, ifmt_ctx -> streams[video_stream] -> time_base,ofmt_ctx -> streams[0] -> time_base);
        ret = av_interleaved_write_frame(ofmt_ctx, &enc_pkt);
        if (0 > ret){
            LOG(ret, "Error during writing data to output file");
            return ret;
        }
        string log;
#ifdef USECUDA 
        log = "************************硬编码成功*************************";
#else
        log = "************************软编码成功*************************";
#endif
        cout << log << endl;
    }
end:
    if (AVERROR_EOF == ret)
        return 0;
    ret = ((ret == AVERROR(EAGAIN)) ? 0 : -1);
    return ret;
}

int main(int argc, char* argv[])
{
    
    av_log_set_level(AV_LOG_INFO);

    int ret;
    string inputFile;
    string outputFile;
    if (argc == 3){
        inputFile = argv[1];
        outputFile = argv[2];
    }else{
        inputFile = "/root/source_media/flv.flv";
        outputFile = "/opt/natr.mp4";
    }
    //string inputFile("/root/source_media/flv.flv");
    // string inputFile("/root/video/11.mkv");
    //string outputFile("/opt/natr.mp4");
  
    //打开输入文件  
    ret = open_input_file(inputFile);
    if (0 > ret){
        LOG(-1, "Error during open_input_file");
        return ret;
    }
    //打开输出文件
    ret = open_output_file(outputFile);
    if (0 > ret){
        LOG(-1, "Error during open_output_file");
        return ret;
    }

    //初始化AVPacket
    AVPacket packet;
    av_init_packet(&packet);
    packet.data = NULL;
    packet.size = 0;
    
    while(ret >= 0){
        ret = av_read_frame(ifmt_ctx, &packet);
        if (0 > ret){
            LOG(ret, "read packet from ifmt_ctx failed");
            break;
        }
        if(video_stream == packet.stream_index){
            //解码
            //计算当前时间
            double currTime = av_q2d(iVStream -> time_base) * packet.pts;
            cout << "currTime: " << currTime << endl;
            ret = avcodec_send_packet(decoder_ctx, &packet); 
            if (0 > ret){
                LOG(ret, "Error during avcodec_send_packet");
                return ret;
            }
            AVFrame *frame = NULL;
            frame = av_frame_alloc();
            if (NULL == frame)
                return AVERROR(ENOMEM);
            ret = avcodec_receive_frame(decoder_ctx, frame);
            if (AVERROR(EAGAIN) == ret){
                av_frame_free(&frame);
                ret = AVERROR(EAGAIN);
                LOG(ret, "avcodec_receive_frame return EAGAIN");
                ret = 0;
                //则继续进行解码，让其填充解码器
            }else if (AVERROR_EOF == ret){
                //说明没有了解码的内容
                av_frame_free(&frame);
                ret = AVERROR_EOF;
                LOG(ret, "avcodec_receive_frame return AVERROR_EOF");
                ret = 0;
            }else if (0 > ret){
                av_frame_free(&frame);
                LOG(ret, "Could not decode frame");
                return ret;
            }else {
                //解码成功
                cout << "解码成功,开始编码" << endl;
                ret = encode_write(frame);
                if (0 > ret){
                    LOG(ret, "Error during encoding and writing");
                    return ret;
                }
                av_frame_unref(frame);
            
            }
        }
        av_packet_unref(&packet); 
    
    
    }
    
    av_write_trailer(ofmt_ctx);
    cout << "ret: " << ret << endl;
    return ret;
}


