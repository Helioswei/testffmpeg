#OBJS= avideowriter.o  media.o
#OBJS= avbase.o  media.o
#OBJS= mediaoperate.o  testmedia.o
#CONSOLENAME=ts_media
OBJS= ./util/stringdeal.o ./iomgm/simples3.o  basicapplication.o mediaoperate.o videoapplication.o videotool.o
CONSOLENAME=videotool
#OBJS=filtervideo.o 
#CONSOLENAME=ts_filtervideo
#OBJS=filterencvideo.o 
#CONSOLENAME=ts_filterencvideo
#OBJS=readvideo.o 
#CONSOLENAME=ts_readvideo
#OBJS=transcodeencoderall1.o 
#CONSOLENAME=ts_transcodeenall1
#OBJS=transcodeaacfifo.o 
#CONSOLENAME=ts_transcodeaacfifo
#OBJS=transcode.o 
#CONSOLENAME=ts_transcode
#OBJS=toimage.o 
#CONSOLENAME=ts_toimage
#OBJS=muxaacout.o 
#CONSOLENAME=ts_muxaacout
#OBJS=encodevideo.o
#CONSOLENAME=ts_encodevideo
#OBJS=encodeaudio.o
#CONSOLENAME=ts_encodeaudio
#OBJS=combinevideo.o
#CONSOLENAME=ts_combinevideo
###定义FFmpeg路径，用于方便头文件和动态库的查找
FFMPEGPATH=$(HELIOS)/deps/video
POCOPATH=$(HELIOS)/deps/poco
S3PATH=$(HELIOS)/deps/libs3

###定义g++编译时头文件查找的编译路径
IFLAGS= -g -I $(FFMPEGPATH)/include  -I $(POCOPATH)/include -I $(S3PATH)/include


### 链接目标文件
### -L  用于指定libavformat.so所在的目录
LFLAGS= -L $(FFMPEGPATH)/lib -L $(POCOPATH)/lib  -L $(S3PATH)/lib/x64/centos7      
### -lavformat  链接库文件 libavformat.so
####定义程序运行时的查找路径
RUNPATH=-Wl,-rpath=$(FFMPEGPATH)/lib -Wl,-rpath=$(POCOPATH)/lib -Wl,-rpath=$(S3PATH)/lib/x64/centos7 
###编译可执行文件
$(CONSOLENAME):$(OBJS)
	g++ -std=c++11 -o $(CONSOLENAME) $(OBJS)    $(LFLAGS)  \
	   	-lavformat -lavcodec -lavutil -lavfilter -lswresample -lswscale  \
		-lPocoFoundation -lPocoUtil  \
		-ls3 \
		$(RUNPATH) 
###编译源文件
### $<表示所有的依赖目标集也就是tsavfmtcont.cpp    $@ 表示目标集合也就是 tsavfmtcont.o
$(OBJS):%.o:%.cpp 
	g++ -std=c++11 -c  $(IFLAGS) $< $(RUNPATH) -fPIC -shared -o $@ 


.PHONY:clean

clean:
	-rm -f $(CONSOLENAME) $(OBJS)

