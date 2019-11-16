#include "../common.h"

using namespace std;

#define LOG(ret,error) \
    av_log(NULL, AV_LOG_INFO,error ",Error code: %d,%s\n",ret,av_err2str(ret)) \ 

static AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
static AVBufferRef *hw_device_ctx  = NULL;
static AVCodecContext *decoder_ctx = NULL, *encoder_ctx = NULL;
static int video_stream = -1;
static AVStream *ost;
static int initialized = 0;

void initLog()
{
    av_log_set_level(AV_LOG_INFO);
}


static enum AVPixelFormat get_vaapi_format(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts)
{
    const enum AVPixelFormat *p;
    for(p = pix_fmts; *p != AV_PIX_FMT_NONE; p++){
        if (*p == AV_PIX_FMT_VAAPI)
            return *p;
    }



}
static int open_input_file(const string filename)
{
    int ret;
    AVCodec *decoder = NULL;
    AVStream *video = NULL;

    ret = avformat_open_input(&ifmt_ctx, filename.c_str(), NULL, NULL);
    if(0 > ret){
        LOG(ret, "Cannot open input file");
        return ret;
    }
    ret = avformat_find_stream_info(ifmt_ctx, NULL);
    if (0 > ret){
        LOG(ret, "Cannot find input stream informat");
        return ret;
    }
    
    ret = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder,0);
    if (0 > ret){
        LOG(ret, "Cannot find a video stream in the input file");
        return ret;
    }
    video_stream = ret;
    decoder_ctx = avcodec_alloc_context3(decoder);
    if (NULL == decoder_ctx)
        return AVERROR(ENOMEM);

    video = ifmt_ctx -> streams[video_stream];

    ret = avcodec_parameters_to_context(decoder_ctx, video -> codecpar);
    if (0 > ret){
        LOG(ret, "avcodec_parameters_to_context error");
        return ret;
    }
    decoder_ctx -> hw_device_ctx = av_buffer_ref(hw_device_ctx); 
    if (NULL == decoder_ctx ->hw_device_ctx){
        LOG(-1, "A hardware device reference create failed");
        return AVERROR(ENOMEM);
    }
    decoder_ctx -> get_format = get_vaapi_format;

    ret = avcodec_open2(decoder_ctx, decoder, NULL);
    if (0 > ret){
        LOG(ret, "Failed to open codec for decoding");
    }
    return ret;
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
        if (0 > ret)
            break;
        enc_pkt.stream_index = 0;
        av_packet_rescale_ts(&enc_pkt, ifmt_ctx -> streams[video_stream] -> time_base,ofmt_ctx -> streams[0] -> time_base);
        ret = av_interleaved_write_frame(ofmt_ctx, &enc_pkt);
        if (0 > ret){
            LOG(ret, "Error during writing data to output file");
            return ret;
        }
    }
end:
    if (AVERROR_EOF == ret)
        return 0;
    ret = ((ret == AVERROR(EAGAIN)) ? 0 : -1);
    return ret;
}

static int dec_enc(AVPacket *pkt, AVCodec *enc_codec)
{
    AVFrame *frame;
    int ret = 0;
    ret = avcodec_send_packet(decoder_ctx, pkt);
    if (0 > ret){
        LOG(ret, "Error during decoding");
        return ret;
    }
    while(0 <= ret){
        if (!(frame = av_frame_alloc()))
            return AVERROR(ENOMEM);
        ret = avcodec_receive_frame(decoder_ctx, frame);
        if (AVERROR(EAGAIN) == ret || AVERROR_EOF == ret){
            av_frame_free(&frame);
            return 0;
        }else if(0 > ret){
            LOG(ret, "Error while decoding");
            goto fail;
        }

        if (0 == initialized){
            //we need to ref hw_frames_ctx of decoder to initialize encoder's codec,
            //only after we get a decoded frame, can we obtain its hw_frames_ctx
            encoder_ctx -> hw_frames_ctx = av_buffer_ref(decoder_ctx -> hw_frames_ctx);
            if (NULL == encoder_ctx -> hw_frames_ctx){
                ret = AVERROR(ENOMEM);
                goto fail;
            }

            //set AVCodecContext Parameters for encoder, here we keep them stay the same as decoder
            //now the sample can't handle resolution change case
            encoder_ctx -> time_base = av_inv_q(decoder_ctx ->framerate);
            encoder_ctx -> pix_fmt = AV_PIX_FMT_VAAPI;
            encoder_ctx -> width = decoder_ctx -> width;
            encoder_ctx -> height = decoder_ctx -> height;
        
            ret = avcodec_open2(encoder_ctx, enc_codec, NULL);
            if (0 > ret){
                LOG(ret, "Failed to open encode codec");
                goto fail;
            }

            if (!(ost = avformat_new_stream(ofmt_ctx, enc_codec))){
                LOG(-1, "Failed to allocate stream for output format");
                ret = AVERROR(ENOMEM);
                goto fail;
            }
            ost -> time_base = encoder_ctx -> time_base;
            ret = avcodec_parameters_from_context(ost -> codecpar , encoder_ctx);
            if (0 > ret){
                LOG(ret, "Failed to copy the stream parameters");
                goto fail;
            }
            ret = avformat_write_header(ofmt_ctx, NULL);
            if (0 > ret){
                LOG(ret, "Error while writing stream header");
                goto fail;
            }
            initialized = 1;

        }

        ret = encode_write(frame);
        if (0 > ret)
            LOG(ret, "Error during encoding and writing");
fail:
       av_frame_free(&frame);
      if (0 > ret)
         return ret; 
    
    }
    return 0;

}

int main(int argc, char* argv[])
{
    int ret;

    initLog();

    string filename("/root/source_media/flv.flv");
    string dec("h264_vaapi");
    string outputName("/root/vna.mp4");
    AVPacket dec_pkt;
    AVCodec *enc_codec;

    ret = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_VAAPI, NULL, NULL, 0);
    if (0 > ret){
        LOG(ret, "Failed to create a VAAPI device");
        return -1;
    }


    ret = open_input_file(filename);
    if (0 > ret)
        goto end;
    
    if (!(enc_codec = avcodec_find_encoder_by_name(dec.c_str()))){
        LOG(-1,"Could not find encoder");
        ret = -1;
        goto end;
    }

    ret = avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, outputName.c_str());
    if (0 > ret){
        LOG(ret, "Failed to deduce output format from file extension");
        goto end;
    }

    if (!(encoder_ctx = avcodec_alloc_context3(enc_codec))){
        ret = AVERROR(ENOMEM);
        goto end;
    }

    ret = avio_open(&ofmt_ctx -> pb, outputName.c_str(), AVIO_FLAG_WRITE);
    if (0 > ret){
        LOG(ret, "Cannot open output file");
        goto end;
    }

    while(ret >= 0){
        ret = av_read_frame(ifmt_ctx, &dec_pkt);
        if (0 > ret)
            break;
        if (video_stream = dec_pkt.stream_index)
            ret = dec_enc(&dec_pkt, enc_codec);
        av_packet_unref(&dec_pkt);
    
    }

    //flush decoder
    dec_pkt.data = NULL;
    dec_pkt.size = 0;
    ret = dec_enc(&dec_pkt, enc_codec);
    av_packet_unref(&dec_pkt);

    //flush encoder
    ret = encode_write(NULL);
    av_write_trailer(ofmt_ctx);
end:
    avformat_close_input(&ifmt_ctx);
    avformat_close_input(&ofmt_ctx);
    avcodec_free_context(&decoder_ctx);
    avcodec_free_context(&encoder_ctx);
    av_buffer_unref(&hw_device_ctx);
    return ret;

}
