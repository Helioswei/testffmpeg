#include "./avbase.h"
#include "./avideowriter.h"
using namespace Media;
#define PRINT (value) cout << #value << ":" << value << endl;

int main(int argc, char* argv[])
{
	string filename("/root/source_video/video1080p.mp4");
	AVBase avbase(filename);
	avbase.muxerMedia();
	//avbase.demuxerMedia();
	//avbase.getInfo();
	//avbase.convertMuxer();
	//avbase.testSome();
	//avbase.operateDir();
	//avbase.cutVideo();
	//avbase.waterMark();
	//
	//char outfile[] = "rgbpcm.mp4";
	//char rgbfile[] = "/root/source_video/video/test.rgb";
	//char pcmfile[] = "/root/source_video/video/test.pcm";
	//XVideoWriter *xw =  XVideoWriter::Get(0);  	
	//xw -> Init(outfile);
	//xw -> AddVideoStream();
	//xw -> AddAudioStream();
	//FILE *fp = fopen(rgbfile, "rb");
	//if (!fp){
	//	cout << "open rgbfile  failed" << endl;
	//	return -1;
	//}

	//FILE *fa = fopen(pcmfile, "rb");
	//if (!fa){
	//	cout << "open pcmfile failed" << endl;
	//	return -1;
	//}

	//int size = xw -> inWidth*xw ->inHeight *4;
	//unsigned char *rgb = new unsigned char[size];



	//int asize = xw -> nb_sample*xw ->inChannels *2;
	//unsigned char *pcm = new unsigned char[asize];
	
	//xw -> WriteHead();	

	//AVPacket *pkt = NULL;
	//int len = 0;
//	while(true){
//		if (xw -> IsVideoBefor()){
//			len = fread(rgb, 1, size, fp);
//			if (len <= 0)
//				break;
//			pkt = xw -> EncodeVideo(rgb);
//			if (pkt)
//				cout << ".";
//			else
//				cout << "-";
//			if (xw -> WriteFrame(pkt)){
//				cout << "+";
//			}
//		}else {
//			len = fread(pcm, 1, asize, fa);
//			if (len <= 0)
//				break;
//			pkt = xw -> EncodeAudio(pcm);
//			xw ->WriteFrame(pkt);
//		}
//	}
//	xw -> WriteEnd();
//	delete rgb;
//	rgb = NULL;
//	cout << "\n==================================End================================" << endl;
	return 0;

}


