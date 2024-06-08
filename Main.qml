import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Dialogs
import ffmpegAudioThread

Window {
    id:window001
    visible: true
    width: 900
    height: 600
    title: "Video Player"
    color:"black"

    function formatTime(milliseconds) {
        var seconds = Math.floor(milliseconds / 1000);
        var minutes = Math.floor(seconds / 60);
        var hours = Math.floor(minutes / 60);
        seconds = seconds % 60;
        minutes = minutes % 60;
        // 使用padStart来确保数字总是显示两位
        var formattedTime = hours.toString().padStart(2, '0') + ":" +
                            minutes.toString().padStart(2, '0') + ":" +
                            seconds.toString().padStart(2, '0');
        return formattedTime;
    }

    FileDialog{
        id:fileDialog
        onAccepted: {
            console.log(fileDialog.selectedFile)
            if (videoPlayer.loadFile(fileDialog.selectedFile)) {
                videoPlayer.play();
            }
        }
    }

    Row {
        anchors.fill: parent
        spacing: 10

            VideoPlayer {
                id: videoPlayer
                width: parent.width * 0.90
                height: parent.height

                onVideoWidthChanged: {
                    window001.width=videoPlayer.videoWidth

                }
                onVideoHeightChanged: {
                    window001.height=videoPlayer.videoHeight

                }
                onDurationChanged: {
                    slider.to=videoPlayer.duration

                }
                onPositionChanged: {

                    if(!slider.pressed){

                        slider.value=videoPlayer.position

                    }
                }

                Slider{
                    id:slider
                    width:videoPlayer.width
                    height:20
                    anchors.bottom:parent.bottom
                    from: 0
                    to:videoPlayer.duration
                    value:videoPlayer.position
                    visible:true
                    opacity: 0
                    onValueChanged: {
                        if(slider.pressed){

                           var intValue=Math.floor(slider.value)

                           videoPlayer.setPosi(intValue)
                        }
                    }
                    Keys.onPressed: {
                        if(event.key===Qt.Key_Escape){
                            window001.visibility=Window.Windowed
                            window001.width=videoPlayer.videoWidth
                            window001.height=videoPlayer.videoHeight
                            videoPlayer.width=window001.width * 0.90
                            videoPlayer.height=window001.height

                       }
                    }
                    MouseArea{
                        id:mouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onEntered: {

                            slider.opacity=1
                        }
                        onExited:{

                            slider.opacity=0
                        }
                        onClicked: {
                            var newPosition=mouse.x/width;
                            slider.value=slider.from+newPosition*(slider.to-slider.from);
                            var intValue=Math.floor(slider.value)
                            videoPlayer.setPosi(intValue)

                        }
                    }
                    Label{
                        id:valueLabel
                        text:formatTime(slider.value)
                        color: "white"
                        x:slider.leftPadding+slider.visualPosition*(slider.width-width)
                        y:slider.topPadding-height
                    }
                }
            }

        Column {
            id: column
            width: parent.width * 0.1
            height: parent.height
            spacing: 10
            anchors.verticalCenter: parent.verticalCenter

            Button {
                text: "开始"
                onClicked: {
                    fileDialog.open()
                }
            }
            Button {
                text: "暂停"
                onClicked: videoPlayer.pause()
            }
            Slider{
                id:playbackSpeedSlider
                from:0.5
                to:2.0
                value:1.0
                stepSize: 0.1
                orientation: Qt.Vertical
                onValueChanged: {
                    playbackSpeedLabel.text="速度:"+playbackSpeedSlider.value.toFixed(1)
                    videoPlayer.audioSpeed(playbackSpeedSlider.value.toFixed(1))
                }
            }
            Label{
                id:playbackSpeedLabel
                color:"white"
                text: "速度:1.0"
            }
            Button{
                text:"全屏"
                onClicked: {
                    if(window001.visibility===Window.FullScreen){
                        window001.visibility=Window.Windowed
                        window001.width=videoPlayer.videoWidth
                        window001.height=videoPlayer.videoHeight
                        videoPlayer.width=window001.width * 0.90
                        videoPlayer.height=window001.height

                    }else{
                        window001.visibility=Window.FullScreen

                        var v_width=videoPlayer.videoWidth
                        var v_height=videoPlayer.videoHeight

                        videoPlayer.width=1920

                        videoPlayer.height=1920*(v_height/v_width)

                    }
                }
            }
            Keys.onPressed: {
                if(event.key===Qt.Key_Escape){
                    window001.visibility=Window.Windowed
                    window001.width=videoPlayer.videoWidth
                    window001.height=videoPlayer.videoHeight
                    videoPlayer.width=window001.width * 0.90
                    videoPlayer.height=window001.height
                }
            }
        }
    }
}
