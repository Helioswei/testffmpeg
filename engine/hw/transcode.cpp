#include "../common.h"

using namespace std;

#define LOG(ret,error) \
    av_log(NULL, AV_LOG_INFO,error ",Error code: %d,%s,%s:%d\n",ret,av_err2str(ret),__FILE__, __LINE__) \ 

static AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;

static AVCodecContext *decoder_ctx = NULL, *encoder_ctx = NULL;

static AVBufferRef *hw_device_ctx = NULL;

static int video_stream = -1;

static AVStream *vstream;

static enum AVPixelFormat hw_pix_fmt;

static enum AVPixelFormat get_vaapi_format(AVCodecContext *ctx,const enum AVPixelFormat *pix_fmts)
{
    const enum AVPixelFormat *p;
    for(p = pix_fmts; *p != AV_PIX_FMT_NONE; p++){
        if(*p == hw_pix_fmt)
            return *p;
    }
    LOG(-1, "Failed to get HW surface format");
    return AV_PIX_FMT_NONE;
}

int main(int argc, char* argv[])
{
    
    av_log_set_level(AV_LOG_INFO);

    int ret;
    string inputFile("/root/source_media/flv.flv");
    string outputFile("/root/natr.mp4");

   

    AVCodec *dec = NULL;

    //打开输入的文件
    ret = avformat_open_input(&ifmt_ctx, inputFile.c_str(), NULL, NULL);
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
    vstream = ifmt_ctx -> streams[ret];
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

    dec = NULL;
    //查找解码的信息
    dec = avcodec_find_decoder(vstream -> codecpar -> codec_id);
    if (NULL == dec){
        LOG(-1, "Failed to find video decoder");
        return -1;
    }
    //为解码上下文创建空间
    decoder_ctx = avcodec_alloc_context3(dec);
    if (NULL == decoder_ctx)
        return AVERROR(ENOMEM);

    //为解码上下文赋值
    ret = avcodec_parameters_to_context(decoder_ctx, vstream -> codecpar);
    if (0 > ret){
        LOG(ret, "avcodec_parameters_to_context error");
        return ret;
    }
    decoder_ctx -> framerate = av_guess_frame_rate(ifmt_ctx, vstream, NULL);

    //hw添加硬件解码
    decoder_ctx -> hw_device_ctx = av_buffer_ref(hw_device_ctx);

    if (NULL == decoder_ctx -> hw_device_ctx){
        LOG(-1, "A hardware device reference create failed");
        return -1;
    }
    decoder_ctx -> get_format = get_vaapi_format;
    //打开解码器
    AVDictionary *opts = NULL;
    av_dict_set(&opts, "refcounted_frames", "0", 0);//frame 的分配和释放由ffmpeg自己控制
    //打开解码器
    ret = avcodec_open2(decoder_ctx, dec, &opts);
    if (0 > ret){
        LOG(ret, "Failed to open codec for decoding");
    }

    //初始化AVPacket
    while(ret >= 0){
    AVPacket packet;
    //av_init_packet(&packet);
    //packet.data = NULL;
    //packet.size = 0;
        ret = av_read_frame(ifmt_ctx, &packet);
        if (0 > ret){
            break;
        }
        if(video_stream == packet.stream_index){
            //解码
            ret = avcodec_send_packet(decoder_ctx, &packet); 
            if (0 > ret){
                LOG(ret, "Error during avcodec_send_packet");
                return ret;
            }
            while(0 <= ret){
                AVFrame *frame = NULL;
                frame = av_frame_alloc();
                if (NULL == frame)
                    return AVERROR(ENOMEM);
                ret = avcodec_receive_frame(decoder_ctx, frame);
                if (AVERROR(EAGAIN) == ret || AVERROR_EOF == ret){
                    av_frame_free(&frame);
                    //return 0;
                    cout << "avcodec_receive_frame return EAGAIN | AVERROR_EOF" << endl;
                    ret = 0;
                }else if ( 0 > ret){
                    LOG(ret, "Error while avcodec_receive_frame");
                    return ret;
                }else {
                    cout << "解码成功" << endl;
                }
            }
        }
        av_packet_unref(&packet); 
    
    
    }
    cout << "ret: " << ret << endl;
    return ret;
}


