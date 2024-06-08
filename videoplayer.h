#ifndef VIDEOPLAYER_H
#define VIDEOPLAYER_H

#include <QObject>
#include <QQuickPaintedItem>
#include <QImage>
#include <QTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QDebug>
#include <QPainter>
#include <QtMultimedia>
#include <QWaitCondition>
#include <QThread>
#include <QString>
#include <chrono>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}
struct AudioData{
    QByteArray buffer;
    qint64 pts;
    qint64 duration;
};

class AudioThread : public QThread
{
    Q_OBJECT
    QML_ELEMENT
public:
    AudioThread(QObject *parent = nullptr);
    ~AudioThread();

    void run() override;

    void cleanQueue();

    void setCustomTimebase(qint64 *timebase);

    qint64 audioTimeLine=0;

    void conditionWakeAll();

    void pause();
    void resume();
    int init_filters(const char *filters_descr);


    void stop();

    void deleteAudioSink();


    void initAudioThread();
    QAudioFormat::SampleFormat ffmpegToQtSampleFormat(AVSampleFormat ffmpegFormat);
signals:
    void audioFrameReady(qint64 pts);
    void audioProcessed();
    void sendAudioTimeLine(qint64 timeLine);
private slots:
    void processAudio();
public slots:
    void handleAudioPacket(AVPacket *packet);
    void receiveAudioParameter(AVFormatContext *format_Ctx,AVCodecContext *audioCodec_Ctx,int *audioStream_Index);
    void setPlaybackSpeed(double speed);

private:

    AVFormatContext *formatCtx = nullptr;
    int *audioStreamIndex = nullptr;
    // 音频编解码器上下文
    AVCodecContext *audioCodecCtx;
    // 音频重采样上下文
    SwrContext *swrCtx;
    // 音频输出设备
    QAudioSink *audioSink;
    // 音频设备输入/输出接口
    QIODevice *audioIODevice;
    QMutex mutex;
    QWaitCondition condition;
    bool shouldStop = false;

    qint64 audioClock = 0; /**< 音频时钟 */
    qint64 *audioTimebase=nullptr;
    bool pauseFlag=false;
    QQueue<AudioData> audioData;
    QQueue<AVPacket*> packetQueue;

    AVFilterContext *buffersink_ctx=nullptr;
    AVFilterContext *buffersrc_ctx=nullptr;
    AVFilterGraph *filter_graph=nullptr;
    qint64 originalPts=0;

    double playbackSpeed=2.0;
    char filters_descr[64]={0};
    int data_size=0;

    QMediaDevices *outputDevices=nullptr;
    QAudioDevice outputDevice;
    QAudioFormat format;

    QTimer *timer;
    bool timerFlag=false;

};


class VideoPlayer : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int videoWidth READ videoWidth  NOTIFY videoWidthChanged)
    Q_PROPERTY(int videoHeight READ videoHeight NOTIFY videoHeightChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(qint64 position READ position WRITE setPosition NOTIFY positionChanged)

public:
    VideoPlayer(QQuickItem *parent = nullptr);
    ~VideoPlayer();
    Q_INVOKABLE bool loadFile(const QString &fileName);
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void setPosi(qint64 position);
    Q_INVOKABLE void audioSpeed(qreal speed);

    int videoWidth() const {
        return m_videoWidth;
    }
    int videoHeight() const{
        return m_videoHeight;
    }
    qint64 duration() const{
        return m_duration;
    }
    qint64 position() const{
        return m_position;
    }
    void setPosition(int p);

    void cleanVideoPacketQueue();

    qint64 turnPoint=0;

    void delay(int milliseconds);
signals:
    void videoWidthChanged();
    void videoHeightChanged();
    void durationChanged(qint64 duration);
    void positionChanged(qint64 position);
    void deliverPacketToAudio(AVPacket *deliverPacket);
    void sendAudioParameter(AVFormatContext *formatCtx,AVCodecContext *audioCodecCtx,int *audioStreamIndex);
    void sendSpeed(double speed);

protected:
    void paint(QPainter *painter) override;
public slots:
    void receiveAudioTimeLine(qint64 timeLine);

private slots:
    void onTimeout();
private:
    void cleanup();
    void decodeVideo();

    AVFormatContext *formatCtx = nullptr;
    AVCodecContext *videoCodecCtx = nullptr;
    SwsContext *swsCtx = nullptr;
    AVCodecContext *audioCodecCtx=nullptr;
    SwrContext *swrCtx=nullptr;


    QImage currentImage;
    QTimer *timer = nullptr;
    QTimer *syncTimer=nullptr;
    int videoStreamIndex = -1;
    int audioStreamIndex = -1;
    AudioThread *audioThread = nullptr;
    AVPacket *audioPacket=nullptr;
    qint64 audioClock = 0; /**< 音频时钟 */
    qint64 videoClock = 0; /**< 视频时钟 */
    QMutex mutex;
    double audioPts=0;
    QQueue<AVFrame*> videoQueue;
    QQueue<AVPacket*> videoPacketQueue;


    int m_videoWidth=0;
    int m_videoHeight=0;
    qint64 m_duration=0;
    qint64 m_position=0;

    qint64 customTimebase=0;

};


#endif // VIDEOPLAYER_H
