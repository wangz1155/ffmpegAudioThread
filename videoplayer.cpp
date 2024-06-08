#include "videoplayer.h"
#include <QDebug>


AudioThread::AudioThread(QObject *parent)
    : QThread(parent),
    audioCodecCtx(nullptr),
    swrCtx(nullptr),
    audioSink(nullptr),
    audioIODevice(nullptr),
    buffersink_ctx(nullptr),
    buffersrc_ctx(nullptr),
    filter_graph(nullptr),
    shouldStop(false),
    pauseFlag(false),
    playbackSpeed(1.0),
    data_size(0){

}

AudioThread::~AudioThread() {
    shouldStop=true;
    condition.wakeAll();
    wait();
}

//设置播放速度
void AudioThread::setPlaybackSpeed(double speed)
{
    playbackSpeed=speed;
    qDebug()<<"playbackSpeed"<<playbackSpeed;

    QMutexLocker locker(&mutex);

    if(filter_graph!=nullptr){

        qWarning() << "无法初始化";
        avfilter_graph_free(&filter_graph);
        timerFlag=true;
        snprintf(filters_descr, sizeof(filters_descr), "atempo=%.1f", playbackSpeed);

        if (init_filters(filters_descr) < 0) {
            qWarning() << "无法初始化滤镜图表";
            return;
        }
    }

}

void AudioThread::pause() {
    QMutexLocker locker(&mutex);
    pauseFlag=true;
}
void AudioThread::resume() {
    QMutexLocker locker(&mutex);
    if(pauseFlag){
        pauseFlag=false;
    }
    condition.wakeAll();
}
void AudioThread::stop() {
    QMutexLocker locker(&mutex);
    shouldStop = true;
    condition.wakeAll();
}

//暂停和清除音频队列，待完善
void AudioThread::deleteAudioSink()
{

    pause();

    cleanQueue();

}

//接收音频放入队列
void AudioThread::handleAudioPacket(AVPacket *packet) {
    QMutexLocker locker(&mutex);
    packetQueue.enqueue(packet);
    condition.wakeOne();
}

//接收主进程传递的参数
void AudioThread::receiveAudioParameter(AVFormatContext *format_Ctx, AVCodecContext *audioCodec_Ctx, int *audioStream_Index)
{
    formatCtx=format_Ctx;
    audioCodecCtx=audioCodec_Ctx;
    audioStreamIndex=audioStream_Index;
}

void AudioThread::conditionWakeAll(){
    condition.wakeAll();
}

//清除音频队列
void AudioThread::cleanQueue(){
    QMutexLocker locker(&mutex);
    while(!packetQueue.isEmpty()){
        AVPacket *packet=packetQueue.dequeue();
        av_packet_unref(packet);
        av_packet_free(&packet);
    }
    while(!audioData.isEmpty()){
        audioData.dequeue();
    }

    locker.unlock();
}

//滤镜初始化
int AudioThread::init_filters(const char *filters_descr) {
    char args[512];
    int ret = 0;
    const AVFilter *buffersrc  = avfilter_get_by_name("abuffer");
    const AVFilter *buffersink = avfilter_get_by_name("abuffersink");
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs  = avfilter_inout_alloc();

    filter_graph = avfilter_graph_alloc();
    if (!outputs || !inputs || !filter_graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    if (!audioCodecCtx->channel_layout)
        audioCodecCtx->channel_layout =
            av_get_default_channel_layout(audioCodecCtx->channels);
    snprintf(args, sizeof(args),
             "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%"PRIx64,
             audioCodecCtx->time_base.num, audioCodecCtx->time_base.den, audioCodecCtx->sample_rate,
             av_get_sample_fmt_name(audioCodecCtx->sample_fmt), audioCodecCtx->channel_layout);
    ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                                       args, nullptr, filter_graph);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_ERROR, "Cannot create buffer source\n");
        goto end;
    }

    ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                                       nullptr, nullptr, filter_graph);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_ERROR, "Cannot create buffer sink\n");
        goto end;
    }


    outputs->name       = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = nullptr;

    inputs->name       = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = nullptr;

    if ((ret = avfilter_graph_parse_ptr(filter_graph, filters_descr,
                                        &inputs, &outputs, nullptr)) < 0)
        goto end;
    if ((ret = avfilter_graph_config(filter_graph, nullptr)) < 0)
        goto end;

end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    if(ret<0&& filter_graph){
        avfilter_graph_free(&filter_graph);
        filter_graph=nullptr;

    }

    return ret;
}

//音频播放
void AudioThread::processAudio()
{
    AudioData audioDataTemp;
    if (shouldStop) {
        quit();
        return;
    }
    //QMutexLocker locker(&mutex);
    if (pauseFlag) {
        return;
    }

    int bytesFree = audioSink->bytesFree();
    if (!audioData.isEmpty() && bytesFree >= audioData.head().buffer.size()) {
        AudioData dataTemp = audioData.dequeue();
        audioTimeLine = dataTemp.pts + dataTemp.duration + audioSink->bufferSize() / data_size * dataTemp.duration;
        emit sendAudioTimeLine(audioTimeLine);
        qDebug() << "audioTimeLine" << audioTimeLine;
        audioIODevice->write(dataTemp.buffer);
    } else {
        qDebug() << "duration_error";
    }

    if (packetQueue.isEmpty()) {

        qDebug() << "packetQueue.isEmpty()" ;
        return;
        //condition.wait(&mutex);
        if (shouldStop) return;
    }
    if (!packetQueue.isEmpty()) {
        AVPacket *packet = packetQueue.dequeue();

        AVFrame *frame = av_frame_alloc();
        if (!frame) {
            qWarning() << "无法分配音频帧";
            return;
        }

        int ret = avcodec_send_packet(audioCodecCtx, packet);
        if (ret < 0) {
            qWarning() << "无法发送音频包到解码器";
            av_packet_unref(packet);
            av_packet_free(&packet);
            av_frame_free(&frame);
            return;
        }

        while (ret >= 0) {
            ret = avcodec_receive_frame(audioCodecCtx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                av_frame_free(&frame);
                break;
            } else if (ret < 0) {
                qWarning() << "无法接收解码后的音频帧";
                av_frame_free(&frame);
                break;
            }

            originalPts = frame->pts * av_q2d(formatCtx->streams[*audioStreamIndex]->time_base) * 1000;

            if (av_buffersrc_add_frame(buffersrc_ctx, frame) < 0) {
                qWarning() << "无法将音频帧送入滤镜链";
                av_frame_free(&frame);
                break;
            }

            while (true) {
                AVFrame *filt_frame = av_frame_alloc();
                if (!filt_frame) {
                    qWarning() << "无法分配滤镜后的音频帧";
                    break;
                }

                ret = av_buffersink_get_frame(buffersink_ctx, filt_frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    av_frame_free(&filt_frame);
                    break;
                } else if (ret < 0) {
                    qWarning() << "无法从滤镜链获取处理后的音频帧";
                    av_frame_free(&filt_frame);
                    break;
                }
                if (!filt_frame || filt_frame->nb_samples <= 0) {
                    qWarning() << "滤镜数据nb_samples<=0";
                    av_frame_free(&filt_frame);
                    continue;
                }
                if (!filt_frame->data[0][0]) {
                    qWarning() << "滤镜数据为空";
                    av_frame_free(&filt_frame);
                    continue;
                }
                if (filt_frame->extended_data == nullptr) {
                    qWarning() << "滤镜数据extended_data";
                    av_frame_free(&filt_frame);
                    continue;
                }

                data_size = av_samples_get_buffer_size(nullptr, filt_frame->channels,
                                                       filt_frame->nb_samples,
                                                       (AVSampleFormat)filt_frame->format, 1);
                if (data_size < 0) {
                    qWarning() << "无法获取缓冲区大小";
                    av_frame_unref(filt_frame);
                    break;
                }


                audioDataTemp.duration = ((filt_frame->nb_samples * 1000) / filt_frame->sample_rate);
                audioDataTemp.pts = originalPts;
                qDebug() << "audioDataTemp.duration" << audioDataTemp.duration;
                qDebug() << "audioDataTemp.pts" << audioDataTemp.pts;

                audioDataTemp.buffer = QByteArray((char*)filt_frame->data[0], data_size);
                audioData.enqueue(audioDataTemp);
                qDebug() << "audioData.size()" << audioData.size();



                av_frame_free(&filt_frame);
            }

            av_frame_free(&frame);
        }

        av_packet_unref(packet);
        av_packet_free(&packet);
    }

    emit audioProcessed();

}

//返回音频类型
QAudioFormat::SampleFormat AudioThread::ffmpegToQtSampleFormat(AVSampleFormat ffmpegFormat) {
    switch (ffmpegFormat) {
    case AV_SAMPLE_FMT_U8:   return QAudioFormat::UInt8;
    case AV_SAMPLE_FMT_S16:  return QAudioFormat::Int16;
    case AV_SAMPLE_FMT_S32:  return QAudioFormat::Int32;
    case AV_SAMPLE_FMT_FLT:  return QAudioFormat::Float;
    case AV_SAMPLE_FMT_DBL:  // Qt没有直接对应的64位浮点格式
    case AV_SAMPLE_FMT_U8P:  // 平面格式
    case AV_SAMPLE_FMT_S16P: // 平面格式
    case AV_SAMPLE_FMT_S32P: // 平面格式
    case AV_SAMPLE_FMT_FLTP: // 平面格式
    case AV_SAMPLE_FMT_DBLP: // 平面格式unknown
    default: return QAudioFormat::Float;
    }
}

//初始化音频
void AudioThread::initAudioThread(){

    if(filter_graph!=nullptr){
        avfilter_graph_free(&filter_graph);
    }

    timerFlag=true;
    snprintf(filters_descr, sizeof(filters_descr), "atempo=%.1f", playbackSpeed);


    outputDevices=new QMediaDevices();
    outputDevice=outputDevices->defaultAudioOutput();
    //format=outputDevice.preferredFormat();

    format.setSampleRate(audioCodecCtx->sample_rate);
    format.setChannelCount(audioCodecCtx->channels);
    //format.setSampleFormat(QAudioFormat::Float);
    format.setSampleFormat(ffmpegToQtSampleFormat(audioCodecCtx->sample_fmt));


    audioSink = new QAudioSink(outputDevice, format);
    audioIODevice =audioSink->start();


    if (init_filters(filters_descr) < 0) {
        qWarning() << "无法初始化滤镜图表";
        return;
    }

}

//初始化音频，开始timer
void AudioThread::run() {

    timerFlag=true;
    snprintf(filters_descr, sizeof(filters_descr), "atempo=%.1f", playbackSpeed);


    outputDevices=new QMediaDevices();
    outputDevice=outputDevices->defaultAudioOutput();
    //format=outputDevice.preferredFormat();

    format.setSampleRate(audioCodecCtx->sample_rate);
    format.setChannelCount(audioCodecCtx->channels);
    //format.setSampleFormat(QAudioFormat::Float);
    format.setSampleFormat(ffmpegToQtSampleFormat(audioCodecCtx->sample_fmt));


    audioSink = new QAudioSink(outputDevice, format);
    audioIODevice =audioSink->start();


    if (init_filters(filters_descr) < 0) {
        qWarning() << "无法初始化滤镜图表";
        return;
    }

    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &AudioThread::processAudio);
    timer->start(10); // 每10ms触发一次
    exec();
    avfilter_graph_free(&filter_graph);

}

VideoPlayer::VideoPlayer(QQuickItem *parent)
    : QQuickPaintedItem(parent),
    formatCtx(nullptr),
    videoCodecCtx(nullptr),
    swsCtx(nullptr),
    audioCodecCtx(nullptr),
    swrCtx(nullptr),
    timer(new QTimer(this)),
    customTimebase(0),
    audioThread(new AudioThread(this)) {
    connect(timer, &QTimer::timeout, this, &VideoPlayer::onTimeout);
    connect(this,&VideoPlayer::deliverPacketToAudio,audioThread,&AudioThread::handleAudioPacket);
    connect(audioThread,&AudioThread::sendAudioTimeLine,this,&VideoPlayer::receiveAudioTimeLine);
    connect(this,&VideoPlayer::sendAudioParameter,audioThread,&AudioThread::receiveAudioParameter);
    connect(this,&VideoPlayer::sendSpeed,audioThread,&AudioThread::setPlaybackSpeed);
    avformat_network_init();
    av_register_all(); // 注册所有编解码器
    avfilter_register_all();
}

VideoPlayer::~VideoPlayer() {
    stop();
    audioThread->quit();
    audioThread->wait();
    delete audioThread;

}


//打开视频文件，如果打开成功，qml中执行 play（）；文件选择用的 qml
bool VideoPlayer::loadFile(const QString &fileName) {
    stop();
    formatCtx = avformat_alloc_context();
    if (avformat_open_input(&formatCtx, fileName.toStdString().c_str(), nullptr, nullptr) != 0) {
        qWarning() << "无法打开文件";
        return false;
    }

    if (avformat_find_stream_info(formatCtx, nullptr) < 0) {
        qWarning() << "无法获取流信息";
        return false;
    }

    for (unsigned int i = 0; i < formatCtx->nb_streams; ++i) {
        if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = i;
        } else if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioStreamIndex = i;
        }
    }

    if (videoStreamIndex == -1) {
        qWarning() << "未找到视频流";
        return false;
    }

    if (audioStreamIndex == -1) {
        qWarning() << "未找到音频流";
        return false;
    }

    AVCodec *videoCodec = avcodec_find_decoder(formatCtx->streams[videoStreamIndex]->codecpar->codec_id);
    if (!videoCodec) {
        qWarning() << "未找到视频解码器";
        return false;
    }

    videoCodecCtx = avcodec_alloc_context3(videoCodec);
    avcodec_parameters_to_context(videoCodecCtx, formatCtx->streams[videoStreamIndex]->codecpar);
    if (avcodec_open2(videoCodecCtx, videoCodec, nullptr) < 0) {
        qWarning() << "无法打开视频解码器";
        return false;
    }

    swsCtx = sws_getContext(videoCodecCtx->width, videoCodecCtx->height, videoCodecCtx->pix_fmt,
                            videoCodecCtx->width, videoCodecCtx->height, AV_PIX_FMT_RGB24,
                            SWS_BILINEAR, nullptr, nullptr, nullptr);



    AVCodec *audioCodec = avcodec_find_decoder(formatCtx->streams[audioStreamIndex]->codecpar->codec_id);
    if (!audioCodec) {
        qWarning() << "未找到音频解码器";
        return false;
    }

    audioCodecCtx = avcodec_alloc_context3(audioCodec);
    if(avcodec_parameters_to_context(audioCodecCtx, formatCtx->streams[audioStreamIndex]->codecpar)<0){
        qWarning() << "无法复制音频解码器上下文";
        return false;
    }

    if (avcodec_open2(audioCodecCtx, audioCodec, nullptr) < 0)
    {
        qWarning() << "无法打开音频解码器";
        return false;
    }

    emit sendAudioParameter(formatCtx,audioCodecCtx,&audioStreamIndex);

    if(!audioThread->isRunning()){
        audioThread->start();
    }else{
        audioThread->initAudioThread();
    }
    audioThread->resume();

    m_duration=formatCtx->duration / AV_TIME_BASE *1000;

    emit durationChanged(m_duration);

    customTimebase=0;

    return true;
}

void VideoPlayer::play() {
    if (!timer->isActive()) {
       timer->start(1000 / 150);//用150是保证2倍数时,数据量足够，避免出现卡顿。
    }
}

void VideoPlayer::pause() {

    if (timer->isActive()) {
        timer->stop();
        audioThread->pause();
    }else{
        timer->start();
        audioThread->resume();
    }

}

void VideoPlayer::stop() {
    if (timer->isActive()) {
        timer->stop();
    }
    cleanup();
}

//绘制视频
void VideoPlayer::paint(QPainter *painter) {
    if (!currentImage.isNull()) {
        painter->drawImage(boundingRect(), currentImage);
    }
}

//接收音频时间线，调整视频时间线。
void VideoPlayer::receiveAudioTimeLine(qint64 timeLine)
{
    customTimebase=timeLine+15;
}

//视频队列清空
void VideoPlayer::cleanVideoPacketQueue(){
    //QMutexLocker locker(&mutex);
    while(!videoPacketQueue.isEmpty()){
        //packetQueue.dequeue();
        AVPacket *packet=videoPacketQueue.dequeue();
        av_packet_unref(packet);
        av_packet_free(&packet);
    }
    //locker.unlock();
}

//定义了Q_PROPERTY(qint64 position READ position WRITE setPosition NOTIFY positionChanged) 必须要有
void VideoPlayer::setPosition(int p){
    /*
    qint64 position=p;
    QMutexLocker locker(&mutex);
    if(av_seek_frame(formatCtx,-1,position*AV_TIME_BASE/1000,AVSEEK_FLAG_ANY)<0){
        qWarning()<<"无法跳转到指定位置";
        return;
    }
    avcodec_flush_buffers(videoCodecCtx);
    avcodec_flush_buffers(audioCodecCtx);
    m_position=position;
    emit positionChanged(m_position);*/
}

//查找定位，用于进度条拖拽。
void VideoPlayer::setPosi(qint64 position){


    if (timer->isActive()) {
        timer->stop();
    }else{

    }
    audioThread->deleteAudioSink();

    qint64 target_ts=position*1000;

    avcodec_flush_buffers(videoCodecCtx);
    avcodec_flush_buffers(audioCodecCtx);

    cleanVideoPacketQueue();

    //if(av_seek_frame(formatCtx,-1,target_ts,AVSEEK_FLAG_BACKWARD)<0){
    if(avformat_seek_file(formatCtx,-1,INT64_MIN,target_ts,INT64_MAX,AVSEEK_FLAG_BACKWARD)){   //这方法查找更准确
        qWarning()<<"无法跳转到指定位置";
        return;
    }

    if (timer->isActive()) {

    }else{
        timer->start();
    }

    audioThread->resume();

    m_position=position;
    customTimebase=position;
    //turnPoint=position;
    emit positionChanged(m_position);

}

//发送速度参数给音频滤镜
void VideoPlayer::audioSpeed(qreal speed)
{
    double s=speed;
    emit sendSpeed(s);

}

//主线程延迟标准程序
void VideoPlayer::delay(int milliseconds) {
    QTime dieTime = QTime::currentTime().addMSecs(milliseconds);
    while (QTime::currentTime() < dieTime) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }
}

//定时器，定时执行内容
void VideoPlayer::onTimeout() {
    AVPacket *packet=av_packet_alloc();
    if(!packet) return;

    if (av_read_frame(formatCtx, packet) >= 0) {
        if (packet->stream_index == videoStreamIndex) {
            videoPacketQueue.enqueue(packet);

            decodeVideo();
        } else if (packet->stream_index == audioStreamIndex) {
            AVPacket *audioPacket=av_packet_alloc();
            if(!audioPacket){
                av_packet_unref(packet);
                av_packet_free(&packet);
                return;
            }
            av_packet_ref(audioPacket,packet);

            emit deliverPacketToAudio(audioPacket);
        }
    }else{
        decodeVideo();
    }
}


//解码视频，并刷新
void VideoPlayer::decodeVideo() {

    if(videoPacketQueue.isEmpty()){
        return;
    }

    AVPacket *packet=videoPacketQueue.first();
    qint64 videoPts=packet->pts*av_q2d(formatCtx->streams[videoStreamIndex]->time_base)*1000;//转换为毫秒

    if(videoPts>(customTimebase)){
        return;
    }else{
        packet=videoPacketQueue.dequeue();
    }

    AVFrame *frame = av_frame_alloc();
    if (!frame) {
        qWarning() << "无法分配视频帧";
        av_packet_unref(packet);
        av_packet_free(&packet);
        return;
    }

    int ret = avcodec_send_packet(videoCodecCtx, packet);
    if (ret < 0) {
        qWarning() << "无法发送视频包到解码器";
        av_frame_free(&frame);
        av_packet_unref(packet);
        av_packet_free(&packet);
        return;
    }

    ret = avcodec_receive_frame(videoCodecCtx, frame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        av_frame_free(&frame);
        av_packet_unref(packet);
        av_packet_free(&packet);
        return;
    } else if (ret < 0) {
        qWarning() << "无法接收解码后的视频帧";
        av_frame_free(&frame);
        av_packet_unref(packet);
        av_packet_free(&packet);
        return;
    }

    av_packet_unref(packet);
    av_packet_free(&packet);


    m_position=customTimebase;            //以音频轴更新视频轴
    emit positionChanged(m_position);


    if(m_videoWidth!=frame->width||m_videoHeight!=frame->height){
        m_videoWidth=frame->width;
        m_videoHeight=frame->height;
        emit videoWidthChanged();
        emit videoHeightChanged();
    }
    // 缩放视频帧
    AVFrame *rgbFrame = av_frame_alloc();
    if (!rgbFrame) {
        qWarning() << "无法分配RGB视频帧";
        av_frame_free(&frame);
        return;
    }
    rgbFrame->format = AV_PIX_FMT_RGB24;
    rgbFrame->width = videoCodecCtx->width;
    rgbFrame->height = videoCodecCtx->height;
    ret = av_frame_get_buffer(rgbFrame, 0);
    if (ret < 0) {
        qWarning() << "无法分配RGB视频帧数据缓冲区";
        av_frame_free(&frame);
        av_frame_free(&rgbFrame);
        return;
    }
    sws_scale(swsCtx, frame->data, frame->linesize, 0, videoCodecCtx->height,
              rgbFrame->data, rgbFrame->linesize);

    // 将RGB视频帧转换为QImage
    currentImage = QImage(rgbFrame->data[0], rgbFrame->width, rgbFrame->height, rgbFrame->linesize[0], QImage::Format_RGB888).copy();


    // 释放视频帧
    av_frame_free(&frame);
    av_frame_free(&rgbFrame);

    update();
}

//清除，用于开始下一个新文件
void VideoPlayer::cleanup() {
    if (swsCtx) {
        sws_freeContext(swsCtx);
        swsCtx = nullptr;
    }
    if (videoCodecCtx) {
        avcodec_free_context(&videoCodecCtx);
        videoCodecCtx = nullptr;
    }
    if (swrCtx) {
        swr_free(&swrCtx);
        swrCtx = nullptr;
    }
    if (audioCodecCtx) {
        avcodec_free_context(&audioCodecCtx);
        audioCodecCtx = nullptr;
    }


    if (formatCtx) {
        avformat_close_input(&formatCtx);
        formatCtx = nullptr;
    }


    while(!videoQueue.isEmpty()){
        AVFrame *frame;
        frame=videoQueue.dequeue();
        av_frame_free(&frame);
    }

    audioThread->deleteAudioSink();
    cleanVideoPacketQueue();

    m_position=0;
    m_duration=0;
    emit positionChanged(m_position);
    emit durationChanged(m_duration);

}
