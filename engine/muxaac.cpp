#include "./common.h"


void adts_header(char *szAdtsHeader, int dataLen){

    int audio_object_type = 2;
    int sampling_frequency_index = 7;
    int channel_config = 2;

    int adtsLen = dataLen + 7;

    szAdtsHeader[0] = 0xff;         //syncword:0xfff                          高8bits
    szAdtsHeader[1] = 0xf0;         //syncword:0xfff                          低4bits
    szAdtsHeader[1] |= (0 << 3);    //MPEG Version:0 for MPEG-4,1 for MPEG-2  1bit
    szAdtsHeader[1] |= (0 << 1);    //Layer:0                                 2bits 
    szAdtsHeader[1] |= 1;           //protection absent:1                     1bit

    szAdtsHeader[2] = (audio_object_type - 1)<<6;            //profile:audio_object_type - 1                      2bits
    szAdtsHeader[2] |= (sampling_frequency_index & 0x0f)<<2; //sampling frequency index:sampling_frequency_index  4bits 
    szAdtsHeader[2] |= (0 << 1);                             //private bit:0                                      1bit
    szAdtsHeader[2] |= (channel_config & 0x04)>>2;           //channel configuration:channel_config               高1bit

    szAdtsHeader[3] = (channel_config & 0x03)<<6;     //channel configuration:channel_config      低2bits
    szAdtsHeader[3] |= (0 << 5);                      //original：0                               1bit
    szAdtsHeader[3] |= (0 << 4);                      //home：0                                   1bit
    szAdtsHeader[3] |= (0 << 3);                      //copyright id bit：0                       1bit  
    szAdtsHeader[3] |= (0 << 2);                      //copyright id start：0                     1bit
    szAdtsHeader[3] |= ((adtsLen & 0x1800) >> 11);           //frame length：value   高2bits

    szAdtsHeader[4] = (uint8_t)((adtsLen & 0x7f8) >> 3);     //frame length:value    中间8bits
    szAdtsHeader[5] = (uint8_t)((adtsLen & 0x7) << 5);       //frame length:value    低3bits
    szAdtsHeader[5] |= 0x1f;                                 //buffer fullness:0x7ff 高5bits
    szAdtsHeader[6] = 0xfc;
}

int main(int argc, char * argv[]){
	int ret;
	AVFormatContext *fmt_ctx = NULL;
	char *in_file = "/root/source_video/video1080p.mp4";	
	char *out_file = "test.aac";
	int audio_index = -1;
	ret = avformat_open_input(&fmt_ctx, in_file, NULL, NULL);

	if (ret < 0){
		cout << "Could not open the input file" << endl;
		return -1;
	}

	ret = avformat_find_stream_info(fmt_ctx, NULL);
    if (ret < 0){
		cout << "Could not find stream " << endl;
		return -1;
	}	
	ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	if (ret < 0){
		cout << "Could not find best stream" << endl;
		return -1;
	}
	audio_index = ret;

	FILE *dst_fd = fopen(out_file, "wb");
	if (!dst_fd){
		cout << "Could not open output file" <<  endl;
		return -1;
	}
	int len;
	AVPacket pkt;
	av_init_packet(&pkt);
	pkt.data = NULL;
	pkt.size = 0;
	while(av_read_frame(fmt_ctx, &pkt) >= 0){
		cout << "read successfully" << endl;
		if (pkt.stream_index == audio_index){
        char adts_header_buf[7];
        adts_header(adts_header_buf, pkt.size);
        fwrite(adts_header_buf, 1, 7, dst_fd);
		len = fwrite(pkt.data,1, pkt.size, dst_fd);
			if (len != pkt.size){
				cout << "write data failed" << endl;
			}
		
		}
		av_packet_unref(&pkt);
	
	}
	avformat_close_input(&fmt_ctx);
	if (dst_fd){
		fclose(dst_fd);
	}
	return 0;
}
