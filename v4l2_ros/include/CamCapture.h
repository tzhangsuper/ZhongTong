#ifndef CAMERA_CAPTURE_H
#define CAMERA_CAPTURE_H

#include <ros/ros.h>
#include <sensor_msgs/Image.h>
#include <image_transport/image_transport.h>
#include <cv_bridge/cv_bridge.h>

#include <opencv2/opencv.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>

#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include <mutex>

#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <poll.h>
#include <math.h>
#include <string.h>
#include <linux/videodev2.h>

#define V4L2_BUFFERS_NUM 4

struct CameraConfig {
    std::string device_path;      // 设备路径，如 "/dev/video0"
    std::string topic_name;       // ROS话题名称
    int width;                    // 图像宽度
    int height;                   // 图像高度
    std::string pixel_format;     // 像素格式 "UYVY", "YUYV", "MJPEG" etc.
    int fps;                      // 帧率
    std::string frame_id;         // ROS frame_id
};

struct CameraBuffer {
    unsigned char* start;
    unsigned int size;
    int dmabuff_fd;
};

class CameraCapture {
public:
    CameraCapture(const CameraConfig& config, ros::NodeHandle& nh);
    ~CameraCapture();
    
    bool initialize();
    bool startCapture();
    void stopCapture();
    bool isRunning() const { return running_; }
    
private:
    // V4L2 相关方法
    bool initializeCamera();
    bool prepareBuffers();
    bool startStream();
    bool stopStream();
    void captureLoop();
    
    // 像素格式转换
    unsigned int getV4L2PixelFormat(const std::string& format);
    cv::Mat convertFrame(const CameraBuffer& buffer, unsigned int pixfmt);
    
    // 工具方法
    void publishImage(const cv::Mat& image, const ros::Time& timestamp);
    void printError(const std::string& message);
    void printInfo(const std::string& message);
    
    // 配置参数
    CameraConfig config_;
    
    // V4L2 相关
    int camera_fd_;
    unsigned int pixel_format_;
    CameraBuffer* buffers_;
    
    // ROS 相关
    ros::NodeHandle& nh_;
    image_transport::ImageTransport it_;
    image_transport::Publisher image_pub_;
    
    // 线程控制
    std::unique_ptr<std::thread> capture_thread_;
    std::atomic<bool> running_;
    std::atomic<bool> should_stop_;
    
    // 统计信息
    std::atomic<int> frame_count_;
    
    // 互斥锁
    mutable std::mutex mutex_;
    
    // 时间戳相关
    long epoch_offset_ms_;
    long getEpochTimeShift();
};

#endif // CAMERA_CAPTURE_H