#include <ros/ros.h>
#include <signal.h>
#include <thread>
#include <chrono>
#include "CamManager.h"

// 全局变量，用于信号处理
static std::unique_ptr<CameraManager> g_camera_manager = nullptr;
static bool g_shutdown_requested = false;

void signalHandler(int signal) {
    ROS_INFO("Received signal %d, shutting down...", signal);
    g_shutdown_requested = true;
    
    if (g_camera_manager) {
        g_camera_manager->stopAllCameras();
    }
    
    ros::shutdown();
}

int main(int argc, char** argv) {
    // 初始化ROS
    ros::init(argc, argv, "multi_camera_capture", ros::init_options::NoSigintHandler);
    ros::NodeHandle nh;
    ros::NodeHandle private_nh("~");
    
    // 注册信号处理器
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // 获取配置文件路径
    std::string config_file_path;
    private_nh.param<std::string>("config_file", config_file_path, 
                                  "$(find your_package_name)/config/cameras.yaml");
    
    ROS_INFO("Starting Multi-Camera Capture Node");
    ROS_INFO("Configuration file: %s", config_file_path.c_str());
    
    try {
        // 创建相机管理器，1. 加载config参数
        g_camera_manager = std::make_unique<CameraManager>(nh, config_file_path);
        
        // 初始化相机，1. 创建缓冲区 2. 初始化相机参数
        if (!g_camera_manager->initialize()) {
            ROS_ERROR("Failed to initialize camera manager");
            return -1;
        }
        
        // 启动所有相机，1.开启流 2.创建线程指针
        if (!g_camera_manager->startAllCameras()) {
            ROS_ERROR("Failed to start all cameras");
            return -1;
        }
        
        // 打印状态
        g_camera_manager->printStatus();
        
        ROS_INFO("All cameras started successfully. Running...");
        ROS_INFO("Press Ctrl+C to stop.");
        
        // 主循环
        ros::Rate loop_rate(1); // 1Hz status print
        int status_counter = 0;
        
        while (ros::ok() && !g_shutdown_requested) {
            ros::spinOnce();
            
            // 每30秒打印一次状态
            if (++status_counter >= 30) {
                g_camera_manager->printStatus();
                status_counter = 0;
            }
            
            loop_rate.sleep();
        }
        
    } catch (const std::exception& e) {
        ROS_ERROR("Exception in main: %s", e.what());
        return -1;
    }
    
    ROS_INFO("Shutting down Multi-Camera Capture Node");
    
    // 清理资源
    if (g_camera_manager) {
        g_camera_manager->stopAllCameras();
        g_camera_manager.reset();
    }
    
    ROS_INFO("Multi-Camera Capture Node shutdown complete");
    return 0;
}