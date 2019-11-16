#include <iostream>

//包含FFmpeg的库
extern "C" {
//#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
}
using namespace std;

#define PRINT(value) cout << #value << ":" << value << endl;

int main(int argc, char* argv[]) {
    AVFormatContext* frmCont;  // AVFormatContext
    // 主要存储视音频封装格式中包含的数据

    const string filepath = "/root/source_video/264.mp4";

    av_register_all();  //初始化libavformat库和一些别的工作

    frmCont = avformat_alloc_context();  //分配formatcontext所需要的内存

    int res;
    res = avformat_open_input(&frmCont, filepath.data(), NULL, NULL);

    if (res != 0) {
	cout << "Couldn't open video file stream \n";
	return -1;
    }

    cout << "open video file successfully \n";

    int streamNum;  //视音频流的个数
    streamNum = frmCont->nb_streams;

    string filename;  //视频文件的名字
    filename = frmCont->filename;

    int64_t duration;  //视频的时间
    duration = frmCont->duration / 1000000;

    int bitrate;  //这个视频的码率
    bitrate = frmCont->bit_rate;

    int chapterNum;  //音频流的个数
    chapterNum = frmCont->nb_chapters;

    PRINT(streamNum);
    PRINT(filename);
    PRINT(duration);
    PRINT(bitrate);
    PRINT(chapterNum);

   // res = avformat_find_stream_info(frmCont, NULL);
   // if (res < 0) cout << "读取流的信息失败" << endl;

   // bitrate = frmCont->bit_rate;
   // PRINT(bitrate);
    //读取文件的信息之后，找到视频流和音频流
    // AVStream* avstream;

    // avstream = frmCont->streams;

    avformat_close_input(&frmCont);
    return 0;
}
