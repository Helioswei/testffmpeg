#include "./common.h"


#define LOG(ret, message)  if ((ret) < 0){  \
	av_log(NULL, AV_LOG_ERROR, message);  \
	exit(1);	\
} \


void SaveFrame(AVFrame *pFrame, int width, int height, int iFrame){
	FILE *pFile;
	char szFilename [32];
	int y;

	//open file
	sprintf(szFilename, "frame%d.ppm",iFrame);
	pFile = fopen(szFilename, "wb");
	if (!pFile)
		exit(1);

	//write header
	fprintf(pFile, "P6\n%d %d\n255\n", width, height);

	//write pixel data
	for (y = 0; y < height; y++)
		fwrite(pFrame->data[0]+y*pFrame -> linesize[0], 1, width*3,pFile);

	// close file
	fclose(pFile);




}
int main (int argc, const char *argv[]){
	AVFormatContext *pFormatCtx = NULL;
	int videoIndex,ret;
	AVCodecContext *pCodecCtx;
	AVCodec *pCodec;
	AVFrame *pFrame;
	AVFrame *pFrameRGB;
	AVPacket packet;
	int frameFinished;
	int numBytes;
	uint8_t *buffer;

	char *inputFile = "/root/source_video/codetest.avi";

	//1,初始化
	av_register_all();
	av_log_set_level(AV_LOG_DEBUG);
	//2.1，打开输入的文件,只会读文件头，并不会填充流的信息
	ret = avformat_open_input(&pFormatCtx, inputFile, NULL, NULL);
	LOG(ret, "Can not to open input file");

	//2.2,获取文件中的流信息
	ret = avformat_find_stream_info(pFormatCtx, NULL);
	LOG(ret, "Can not to find stream in input file");	

	av_dump_format(pFormatCtx, -1, inputFile, 0);

	//3，找到视频流的索引，这个可以有两种方法
	//3.1，通过循环的方式来查找
	for(int i = 0; i < pFormatCtx -> nb_streams; i ++){
		if (pFormatCtx -> streams[i] -> codec ->codec_type == AVMEDIA_TYPE_VIDEO){
			videoIndex = i;
			break;
		}
	}
	//3.2,直接通过函数来查找
	/*AVCodec *dec = NULL;
	ret = av_find_best_stream(pFormatCtx,AVMEDIA_TYPE_VIDEO,-1, -1, &dec, NULL);
	LOG(ret, "Can not to find the video Index");
	videoIndex = ret;*/

	//4,打开解码的解码器
	pCodecCtx = pFormatCtx -> streams[videoIndex] -> codec;
	pCodec = avcodec_find_decoder(pCodecCtx -> codec_id);
	if (!pCodec){
		return -1;
	}
	ret = avcodec_open2(pCodecCtx, pCodec, NULL);
	LOG(ret, "Failed to avcodec open");
	
	// Allocate video frame
	pFrame = av_frame_alloc();
	if (!pFrame)
		return -1;
	// Allocate an AVFrame structure
	pFrameRGB = av_frame_alloc();
	if (!pFrameRGB)
		return -1;
	//调用avcodec_alloc_frame分配帧，因为最后我们会将图像写成24-bitRGB的ppm文件
	//所以这里需要两个AVFrame，pFrame用于存储解码后的数据，平FrameRGB用于存储转换后的数据
	numBytes = avpicture_get_size(AV_PIX_FMT_RGB24, pCodecCtx -> width, pCodecCtx -> height);
	//分配空间
	buffer = (uint8_t *)av_malloc(numBytes*sizeof(uint8_t));
	//调用avpicture_fill将pFrameRGB跟buffer指向的内存关联起来
	avpicture_fill( (AVPicture*)pFrameRGB, buffer, AV_PIX_FMT_RGB24, pCodecCtx -> width, pCodecCtx -> height);
	
	//获取图像，从文件中读取视频帧并解码得到图像了
	int i = 0;
	while( av_read_frame(pFormatCtx, &packet) >= 0){
		if (packet.stream_index == videoIndex){
			//Decode video frame
			avcodec_decode_video2(pCodecCtx,pFrame,&frameFinished, &packet);
			//Did we get a video frame?
			if (frameFinished){
				//Convert the image from its native format to RGB
				struct SwsContext *img_convert_ctx = NULL;
				img_convert_ctx = sws_getCachedContext(img_convert_ctx, pCodecCtx ->width,pCodecCtx->height,pCodecCtx -> pix_fmt,
						pCodecCtx -> width, pCodecCtx ->height, AV_PIX_FMT_RGB24, SWS_BILINEAR, NULL, NULL, NULL);
				
				if (!img_convert_ctx){
					cout << "Cannot initialize sws conversion context \n" << endl;
					exit(1);
				}	
				sws_scale(img_convert_ctx, (const uint8_t* const *)pFrame ->data,pFrame -> linesize,0,
						pCodecCtx -> height, pFrameRGB -> data, pFrameRGB ->linesize );
				if (i++ < 5)
					SaveFrame(pFrameRGB, pCodecCtx -> width, pCodecCtx ->height,i);
			}
		}
		av_free_packet(&packet);
	}
	av_free(buffer);
	av_free(pFrameRGB);
	av_free(pFrame);
	avcodec_close(pCodecCtx);
	avformat_close_input(&pFormatCtx);
	return 0;

}
