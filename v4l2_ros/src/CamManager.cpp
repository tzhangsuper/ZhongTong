#include "CamManager.h"
#include <fstream>

CameraManager::CameraManager(ros::NodeHandle& nh, const std::string& config_file_path)
    : nh_(nh), initialized_(false) {
    
    if (!loadConfiguration(config_file_path)) {
        ROS_ERROR("Failed to load camera configuration");
        return;
    }
    
    if (!validateConfiguration()) {
        ROS_ERROR("Camera configuration validation failed");
        return;
    }
    
    ROS_INFO("Camera Manager created with %zu cameras", camera_configs_.size());
}

CameraManager::~CameraManager() {
    stopAllCameras();
}

bool CameraManager::initialize() {
    if (initialized_) {
        ROS_WARN("Camera Manager already initialized");
        return true;
    }
    
    // 为每个相机配置创建CameraCapture实例
    for (const auto& config : camera_configs_) {
        auto camera = std::make_unique<CameraCapture>(config, nh_);
        
        if (!camera->initialize()) { // 创建缓冲区和参数初始化
            ROS_ERROR("Failed to initialize camera: %s", config.device_path.c_str());
            return false;
        }
        
        cameras_.push_back(std::move(camera));
    }
    
    initialized_ = true;
    ROS_INFO("All cameras initialized successfully");
    return true;
}

bool CameraManager::startAllCameras() {
    if (!initialized_) {
        ROS_ERROR("Camera Manager not initialized");
        return false;
    }
    
    bool all_started = true;
    
    for (auto& camera : cameras_) {
        if (!camera->startCapture()) {
            ROS_ERROR("Failed to start camera capture");
            all_started = false;
        }
    }
    
    if (all_started) {
        ROS_INFO("All cameras started successfully");
    } else {
        ROS_ERROR("Some cameras failed to start");
    }
    
    return all_started;
}

void CameraManager::stopAllCameras() {
    for (auto& camera : cameras_) {
        if (camera) {
            camera->stopCapture();
        }
    }
    ROS_INFO("All cameras stopped");
}

void CameraManager::printStatus() {
    ROS_INFO("Camera Manager Status:");
    ROS_INFO("  Initialized: %s", initialized_ ? "Yes" : "No");
    ROS_INFO("  Total cameras: %zu", cameras_.size());
    
    for (size_t i = 0; i < cameras_.size(); ++i) {
        if (cameras_[i]) {
            ROS_INFO("  Camera %zu (%s): %s", 
                     i, 
                     camera_configs_[i].device_path.c_str(),
                     cameras_[i]->isRunning() ? "Running" : "Stopped");
        }
    }
}

bool CameraManager::loadConfiguration(const std::string& config_file_path) {
    /*
        填充私有变量 camera_configs_, 获取参数
    */
    try {
        // 检查文件是否存在
        std::ifstream file(config_file_path);
        if (!file.good()) {
            ROS_ERROR("Configuration file not found: %s", config_file_path.c_str());
            return false;
        }
        
        YAML::Node config = YAML::LoadFile(config_file_path);
        
        if (!config["cameras"]) {
            ROS_ERROR("No 'cameras' section found in configuration file");
            return false;
        }
        
        const YAML::Node& cameras_node = config["cameras"];
        
        for (const auto& camera_node : cameras_node) {
            CameraConfig cam_config;
            
            // 必须的参数
            if (!camera_node["device_path"]) {
                ROS_ERROR("Missing 'device_path' for camera configuration");
                return false;
            }
            cam_config.device_path = camera_node["device_path"].as<std::string>();
            
            if (!camera_node["topic_name"]) {
                ROS_ERROR("Missing 'topic_name' for camera: %s", cam_config.device_path.c_str());
                return false;
            }
            cam_config.topic_name = camera_node["topic_name"].as<std::string>();
            
            // 可选参数，提供默认值
            cam_config.width = camera_node["width"].as<int>(1920);
            cam_config.height = camera_node["height"].as<int>(1080);
            cam_config.pixel_format = camera_node["pixel_format"].as<std::string>("UYVY");
            cam_config.fps = camera_node["fps"].as<int>(30);
            cam_config.frame_id = camera_node["frame_id"].as<std::string>("camera_" + 
                                  std::to_string(camera_configs_.size()));
            
            camera_configs_.push_back(cam_config);
            
            ROS_INFO("Loaded camera config: %s -> %s (%dx%d@%dfps, %s)", 
                     cam_config.device_path.c_str(),
                     cam_config.topic_name.c_str(),
                     cam_config.width,
                     cam_config.height,
                     cam_config.fps,
                     cam_config.pixel_format.c_str());
        }
        
        return true;
        
    } catch (const YAML::Exception& e) {
        ROS_ERROR("YAML parsing error: %s", e.what());
        return false;
    } catch (const std::exception& e) {
        ROS_ERROR("Configuration loading error: %s", e.what());
        return false;
    }
}

bool CameraManager::validateConfiguration() {
    if (camera_configs_.empty()) {
        ROS_ERROR("No camera configurations loaded");
        return false;
    }
    
    // 检查设备路径和话题名称的唯一性
    std::set<std::string> device_paths;
    std::set<std::string> topic_names;
    
    for (const auto& config : camera_configs_) {
        // 检查设备路径唯一性
        if (device_paths.find(config.device_path) != device_paths.end()) {
            ROS_ERROR("Duplicate device path: %s", config.device_path.c_str());
            return false;
        }
        device_paths.insert(config.device_path);
        
        // 检查话题名称唯一性
        if (topic_names.find(config.topic_name) != topic_names.end()) {
            ROS_ERROR("Duplicate topic name: %s", config.topic_name.c_str());
            return false;
        }
        topic_names.insert(config.topic_name);
        
        // 检查分辨率
        if (config.width <= 0 || config.height <= 0) {
            ROS_ERROR("Invalid resolution for %s: %dx%d", 
                     config.device_path.c_str(), config.width, config.height);
            return false;
        }
        
        // 检查帧率
        if (config.fps <= 0 || config.fps > 120) {
            ROS_ERROR("Invalid FPS for %s: %d", config.device_path.c_str(), config.fps);
            return false;
        }
        
        // 检查设备文件是否存在
        if (access(config.device_path.c_str(), F_OK) != 0) {
            ROS_WARN("Camera device may not exist: %s", config.device_path.c_str());
        }
    }
    
    ROS_INFO("Configuration validation passed for %zu cameras", camera_configs_.size());
    return true;
}