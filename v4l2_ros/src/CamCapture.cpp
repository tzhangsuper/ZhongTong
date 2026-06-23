#include "CamCapture.h"
#include <sensor_msgs/image_encodings.h>

CameraCapture::CameraCapture(const CameraConfig& config, ros::NodeHandle& nh)
    : config_(config), nh_(nh), it_(nh), camera_fd_(-1), buffers_(nullptr),
      running_(false), should_stop_(false), frame_count_(0) {
    
    // 创建图像发布者
    image_pub_ = it_.advertise(config_.topic_name, 1);
    
    // 获取时间偏移
    epoch_offset_ms_ = getEpochTimeShift();
    
    printInfo("Camera " + config_.device_path + " initialized for topic " + config_.topic_name);
}

CameraCapture::~CameraCapture() {
    stopCapture();
    
    if (camera_fd_ >= 0) {
        close(camera_fd_);
    }
    
    if (buffers_) {
        for (int i = 0; i < V4L2_BUFFERS_NUM; i++) {
            if (buffers_[i].start != MAP_FAILED && buffers_[i].start != nullptr) {
                munmap(buffers_[i].start, buffers_[i].size);
            }
        }
        free(buffers_);
    }
}

bool CameraCapture::initialize() {
    if (!initializeCamera()) {
        printError("Failed to initialize camera");
        return false;
    }
    
    if (!prepareBuffers()) {
        printError("Failed to prepare buffers");
        return false;
    }
    
    printInfo("Camera initialization completed successfully");
    return true;
}

bool CameraCapture::startCapture() {
    if (running_) {
        printInfo("Camera is already running");
        return true;
    }
    
    if (!startStream()) {
        printError("Failed to start camera stream");
        return false;
    }
    
    should_stop_ = false;
    running_ = true;
    capture_thread_ = std::make_unique<std::thread>(&CameraCapture::captureLoop, this);
    
    printInfo("Camera capture started");
    return true;
}

void CameraCapture::stopCapture() {
    if (!running_) {
        return;
    }
    
    should_stop_ = true;
    
    if (capture_thread_ && capture_thread_->joinable()) {
        capture_thread_->join();
    }
    
    stopStream();
    running_ = false;
    
    printInfo("Camera capture stopped");
}

bool CameraCapture::initializeCamera() {
    // 打开相机设备
    camera_fd_ = open(config_.device_path.c_str(), O_RDWR);
    if (camera_fd_ == -1) {
        printError("Failed to open camera device " + config_.device_path + 
                  ": " + strerror(errno));
        return false;
    }
    
    // 获取像素格式
    pixel_format_ = getV4L2PixelFormat(config_.pixel_format);
    
    // 设置相机输出格式
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = config_.width;
    fmt.fmt.pix.height = config_.height;
    fmt.fmt.pix.pixelformat = pixel_format_;
    fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;
    
    if (ioctl(camera_fd_, VIDIOC_S_FMT, &fmt) < 0) {
        printError("Failed to set camera output format: " + std::string(strerror(errno)));
        return false;
    }
    
    // 验证实际设置的格式
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(camera_fd_, VIDIOC_G_FMT, &fmt) < 0) {
        printError("Failed to get camera output format: " + std::string(strerror(errno)));
        return false;
    }
    
    if (fmt.fmt.pix.width != config_.width ||
        fmt.fmt.pix.height != config_.height ||
        fmt.fmt.pix.pixelformat != pixel_format_) {
        
        printInfo("Desired format not supported, using: " +
                 std::to_string(fmt.fmt.pix.width) + "x" + std::to_string(fmt.fmt.pix.height));
        
        // 更新实际格式（但不修改config_，因为它是const引用）
        // config_.width = fmt.fmt.pix.width;  // 这行不能执行
        // config_.height = fmt.fmt.pix.height;
        pixel_format_ = fmt.fmt.pix.pixelformat;
    }
    
    // 获取并显示流参数
    struct v4l2_streamparm streamparm;
    memset(&streamparm, 0, sizeof(streamparm));
    streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(camera_fd_, VIDIOC_G_PARM, &streamparm);
    
    printInfo("Camera format: " + std::to_string(fmt.fmt.pix.width) + "x" + 
              std::to_string(fmt.fmt.pix.height) + ", stride: " + 
              std::to_string(fmt.fmt.pix.bytesperline) + ", imagesize: " + 
              std::to_string(fmt.fmt.pix.sizeimage));
    
    return true;
}

bool CameraCapture::prepareBuffers() {
    // 分配缓冲区数组
    buffers_ = (CameraBuffer*)malloc(V4L2_BUFFERS_NUM * sizeof(CameraBuffer));
    if (!buffers_) {
        printError("Failed to allocate buffer array");
        return false;
    }
    
    // 请求V4L2缓冲区
    struct v4l2_requestbuffers rb;
    memset(&rb, 0, sizeof(rb));
    rb.count = V4L2_BUFFERS_NUM;
    rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    rb.memory = V4L2_MEMORY_MMAP;
    
    if (ioctl(camera_fd_, VIDIOC_REQBUFS, &rb) < 0) {
        printError("Failed to request v4l2 buffers: " + std::string(strerror(errno)));
        return false;
    }
    
    if (rb.count != V4L2_BUFFERS_NUM) {
        printError("V4L2 buffer number is not as desired");
        return false;
    }
    
    // 映射缓冲区
    for (unsigned int i = 0; i < V4L2_BUFFERS_NUM; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.index = i;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        
        if (ioctl(camera_fd_, VIDIOC_QUERYBUF, &buf) < 0) {
            printError("Failed to query buffer: " + std::string(strerror(errno)));
            return false;
        }
        
        buffers_[i].size = buf.length;
        buffers_[i].start = (unsigned char*)mmap(NULL, buf.length,
                                               PROT_READ | PROT_WRITE,
                                               MAP_SHARED,
                                               camera_fd_, buf.m.offset);
        
        if (buffers_[i].start == MAP_FAILED) {
            printError("Failed to map buffer");
            return false;
        }
        
        // 将缓冲区入队
        if (ioctl(camera_fd_, VIDIOC_QBUF, &buf) < 0) {
            printError("Failed to enqueue buffer: " + std::string(strerror(errno)));
            return false;
        }
    }
    
    return true;
}

bool CameraCapture::startStream() {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(camera_fd_, VIDIOC_STREAMON, &type) < 0) {
        printError("Failed to start streaming: " + std::string(strerror(errno)));
        return false;
    }
    
    usleep(200000); // 200ms
    return true;
}

bool CameraCapture::stopStream() {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(camera_fd_, VIDIOC_STREAMOFF, &type) < 0) {
        printError("Failed to stop streaming: " + std::string(strerror(errno)));
        return false;
    }
    return true;
}

void CameraCapture::captureLoop() {
    struct pollfd fds[1];
    fds[0].fd = camera_fd_;
    fds[0].events = POLLIN;
    
    printInfo("Starting capture loop for " + config_.device_path);
    
    while (!should_stop_ && ros::ok()) {
        // 等待相机事件，超时5秒
        int poll_result = poll(fds, 1, 5000);
        
        if (poll_result < 0) {
            if (errno == EINTR) continue; // 被信号中断，继续
            printError("Poll failed: " + std::string(strerror(errno)));
            break;
        } else if (poll_result == 0) {
            printInfo("Poll timeout, continuing...");
            continue;
        }
        
        if (fds[0].revents & POLLIN) {
            struct v4l2_buffer v4l2_buf;
            memset(&v4l2_buf, 0, sizeof(v4l2_buf));
            v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            v4l2_buf.memory = V4L2_MEMORY_MMAP;
            
            // 出队缓冲区
            if (ioctl(camera_fd_, VIDIOC_DQBUF, &v4l2_buf) < 0) {
                printError("Failed to dequeue buffer: " + std::string(strerror(errno)));
                continue;
            }
            
            // 创建时间戳
            ros::Time timestamp = ros::Time::now();
            
            // 转换图像并发布
            try {
                cv::Mat image = convertFrame(buffers_[v4l2_buf.index], pixel_format_);
                if (!image.empty()) {
                    publishImage(image, timestamp);
                    frame_count_++;
                }
            } catch (const std::exception& e) {
                printError("Error processing frame: " + std::string(e.what()));
            }
            
            // 重新入队缓冲区
            if (ioctl(camera_fd_, VIDIOC_QBUF, &v4l2_buf) < 0) {
                printError("Failed to requeue buffer: " + std::string(strerror(errno)));
                break;
            }
        }
    }
    
    printInfo("Capture loop ended for " + config_.device_path);
}

unsigned int CameraCapture::getV4L2PixelFormat(const std::string& format) {
    if (format == "YUYV") return V4L2_PIX_FMT_YUYV;
    if (format == "YVYU") return V4L2_PIX_FMT_YVYU;
    if (format == "VYUY") return V4L2_PIX_FMT_VYUY;
    if (format == "UYVY") return V4L2_PIX_FMT_UYVY;
    if (format == "GREY") return V4L2_PIX_FMT_GREY;
    if (format == "ABGR32") return V4L2_PIX_FMT_ABGR32;
    if (format == "MJPEG") return V4L2_PIX_FMT_MJPEG;
    
    printError("Unsupported pixel format: " + format + ", using UYVY as default");
    return V4L2_PIX_FMT_UYVY;
}

cv::Mat CameraCapture::convertFrame(const CameraBuffer& buffer, unsigned int pixfmt) {
    cv::Size size(config_.width, config_.height);
    
    switch (pixfmt) {
        case V4L2_PIX_FMT_ABGR32: {
            return cv::Mat(size, CV_8UC4, buffer.start);
        }
        case V4L2_PIX_FMT_UYVY: {
            cv::Mat yuv_mat(size, CV_8UC2, buffer.start);
            cv::Mat bgr_mat;
            cv::cvtColor(yuv_mat, bgr_mat, cv::COLOR_YUV2BGR_UYVY);
            return bgr_mat;
        }
        case V4L2_PIX_FMT_YUYV: {
            cv::Mat yuv_mat(size, CV_8UC2, buffer.start);
            cv::Mat bgr_mat;
            cv::cvtColor(yuv_mat, bgr_mat, cv::COLOR_YUV2BGR_YUYV);
            return bgr_mat;
        }
        case V4L2_PIX_FMT_MJPEG: {
            std::vector<uchar> buffer_vec(buffer.start, buffer.start + buffer.size);
            return cv::imdecode(buffer_vec, cv::IMREAD_COLOR);
        }
        case V4L2_PIX_FMT_GREY: {
            return cv::Mat(size, CV_8UC1, buffer.start);
        }
        default: {
            printError("Unsupported pixel format for conversion");
            return cv::Mat();
        }
    }
}

void CameraCapture::publishImage(const cv::Mat& image, const ros::Time& timestamp) {
    if (image.empty()) {
        return;
    }
    
    std::string encoding;
    if (image.channels() == 1) {
        encoding = sensor_msgs::image_encodings::MONO8;
    } else if (image.channels() == 3) {
        encoding = sensor_msgs::image_encodings::BGR8;
    } else if (image.channels() == 4) {
        encoding = sensor_msgs::image_encodings::BGRA8;
    } else {
        printError("Unsupported image channels: " + std::to_string(image.channels()));
        return;
    }
    
    try {
        cv_bridge::CvImage cv_image;
        cv_image.header.stamp = timestamp;
        cv_image.header.frame_id = config_.frame_id;
        cv_image.encoding = encoding;
        cv_image.image = image;
        
        image_pub_.publish(cv_image.toImageMsg());
    } catch (cv_bridge::Exception& e) {
        printError("CV bridge exception: " + std::string(e.what()));
    }
}

long CameraCapture::getEpochTimeShift() {
    struct timeval epochtime;
    struct timespec vsTime;
    struct timespec realTime;

    gettimeofday(&epochtime, NULL);
    clock_gettime(CLOCK_MONOTONIC, &vsTime);
    clock_gettime(CLOCK_REALTIME, &realTime);

    long uptime_ms = vsTime.tv_sec * 1000 + (long)round(vsTime.tv_nsec / 1000000.0);
    long epoch_ms = epochtime.tv_sec * 1000 + (long)round(epochtime.tv_usec / 1000.0);
    long x_ms = realTime.tv_sec * 1000 + (long)round(realTime.tv_nsec / 1000000.0);

    printInfo("Epoch offset: " + std::to_string(epoch_ms - uptime_ms) + 
              " ms, Real mono offset: " + std::to_string(x_ms - uptime_ms) + " ms");
    
    return epoch_ms - uptime_ms;
}

void CameraCapture::printError(const std::string& message) {
    ROS_ERROR("[%s] %s", config_.device_path.c_str(), message.c_str());
}

void CameraCapture::printInfo(const std::string& message) {
    ROS_INFO("[%s] %s", config_.device_path.c_str(), message.c_str());
}