#ifndef __NIO_VIDEO_SPLICER_H__
#define __NIO_VIDEO_SPLICER_H__
#include <string.h>

#include "NIOType.h"
#include "NIOThread.h"

#define USE_AV_FILTER

#ifdef __cplusplus
extern "C" {
#endif

#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libswresample/swresample.h>

#ifdef USE_AV_FILTER
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#else
#include <libswscale/swscale.h>
#endif // USE_AV_FILTER

#ifdef __cplusplus
}
#endif

#ifdef USE_AV_FILTER
typedef struct Rect {
    int left;
    int top;
    int right;
    int bottom;
} Rect;
#endif // USE_AV_FILTER

typedef struct FileInfoStruct {
    const char*         path;
    int                 width;
    int                 height;
    int64_t             duration;
    int                 eof;
    AVFrame             *audioFrame;
    AVFrame             *videoFrame;
    AVStream            *audioStream;
    AVStream            *videoStream;
    AVFormatContext     *formatContext;
    int                 needFlushAudioCodec;
    int                 audioCodecEof;
    AVCodecContext      *audioCodecCtx;
    int                 needFlushVideoCodec;
    int                 videoCodecEof;
    AVCodecContext      *videoCodecCtx;
#ifdef USE_AV_FILTER
    AVFilterContext     *buffersink_ctx;
    AVFilterContext     *buffersrc_ctx;
    AVFilterGraph       *videoFilter;
#else
    SwsContext          *videoScalerCtx;
#endif
    int64_t             audioPTS;
    AVAudioFifo         *audioFifo;
    SwrContext          *audioResampleCtx;
}FileInfoStruct;

class NIOVideoSplicer {
    public:
        NIOVideoSplicer(int inCount, void *input, const char* output, int (*cb)(int, int, int, void *));
        virtual ~NIOVideoSplicer();

        int Start();
#ifdef USE_AV_FILTER
        int SetWatermark(const char *path, int left, int top, int right, int bottom);
#endif // USE_AV_FILTER
        int Stop();

    private:
        static int WorkLoop(void *data);

    private:
        int NeedDoTranscoding(NIOVideoSplicer *splicer);
        int OpenInput(NIOVideoSplicer *splicer);
        int OpenOutput(NIOVideoSplicer *splicer);
        int OpenInputDecoder(FileInfoStruct *input, enum AVMediaType type);
        int Remuxing(NIOVideoSplicer *splicer);
        int ReadPacket(NIOVideoSplicer *splicer, AVPacket *pkt);
        int AudioTranscoding(NIOVideoSplicer *splicer, AVPacket *in_pkt, AVPacket *out_pkt);
        int AudioResampling(NIOVideoSplicer *splicer, AVFrame *in_frame, AVFrame *out_frame);
        int VideoTranscoding(NIOVideoSplicer *splicer, AVPacket *in_pkt, AVPacket *out_pkt);
        int CreateAudioResampleContext(FileInfoStruct *input, FileInfoStruct *output);
#ifdef USE_AV_FILTER
        int CreateVideoFilter(NIOVideoSplicer *splicer, FileInfoStruct *input, FileInfoStruct *output);
#else
        int CreateVideoScaler(FileInfoStruct *input, FileInfoStruct *output);
#endif
        int MuxingPacket(NIOVideoSplicer *splicer, AVPacket *out_pkt);
        int FinishProcess(NIOVideoSplicer *splicer, int nRC);
        void CloseFileInfoStruct(FileInfoStruct *infoStruct, int output);
        void AdjustTimestamp(NIOVideoSplicer *splicer, AVPacket *pkt);
        int CheckCodecParameters(int type, AVCodecParameters *in, AVCodecParameters *out);

    private:
        NIOThread* workThread;
        int (*callback)(int, int, int, void *);

        // FFMPEG related
    private:
        int             inputCount;
        int             currentDemuxIndex;
        const char      **inputFileList;
        const char      *outputFile;
#ifdef USE_AV_FILTER
        const char      *watermarkFile;
        Rect            watermarkRect;
#endif // USE_AV_FILTER
        FileInfoStruct *inputStruct;
        FileInfoStruct *outputStruct;
        int             needAudioTranscoding;
        int             needVideoTranscoding;

        int64_t         outputDuration;
        int64_t         audioPTSStride;
        int64_t         audioDTSStride;
        int64_t         videoPTSStride;
        int64_t         videoDTSStride;

        int64_t         audioPrePTS;
        int64_t         audioPreDTS;
        int64_t         videoPrePTS;
        int64_t         videoPreDTS;

        // Info callback related
    private:
        int64_t         lastCallbackPTS;
};
#endif //__NIO_VIDEO_SPLICER_H__
