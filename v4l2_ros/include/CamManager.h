#ifndef CAMERA_MANAGER_H
#define CAMERA_MANAGER_H

#include "CamCapture.h"
#include <ros/ros.h>
#include <yaml-cpp/yaml.h>
#include <vector>
#include <memory>

class CameraManager {
public:
    CameraManager(ros::NodeHandle& nh, const std::string& config_file_path);
    ~CameraManager();
    
    bool initialize();
    bool startAllCameras();
    void stopAllCameras();
    void printStatus();
    
private:
    bool loadConfiguration(const std::string& config_file_path);
    bool validateConfiguration();
    
    ros::NodeHandle& nh_;
    std::vector<CameraConfig> camera_configs_;
    std::vector<std::unique_ptr<CameraCapture>> cameras_;
    
    bool initialized_;
};

#endif // CAMERA_MANAGER_H