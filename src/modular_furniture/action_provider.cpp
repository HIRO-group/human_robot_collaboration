#include <stdio.h>

#include <ros/ros.h>
#include "robot_perception/cartesian_estimator_hsv.h"

int main(int argc, char ** argv)
{
    ros::init(argc, argv, "action_provider");
    ros::NodeHandle _n("action_provider");

    bool use_robot;
    _n.param<bool>("use_robot", use_robot, true);
    printf("\n");
    ROS_INFO("use_robot flag set to %s", use_robot==true?"true":"false");

    cv::Mat sizes(1, 2, CV_32FC1);
    sizes.at<float>(0,0) = 0.080;
    sizes.at<float>(0,1) = 0.020;

    hsvColorRange   blue(colorRange( 60, 130), colorRange(90, 256), colorRange( 10,256));
    hsvColorRange yellow(colorRange( 10,  60), colorRange(50, 116), colorRange(120,146));
    hsvColorRange    red(colorRange(160,  10), colorRange(70, 166), colorRange( 10, 66));

    std::vector<hsvColorRange> colors;
    colors.push_back(red);

    // printf("\n");
    CartesianEstimatorHSV ce_hsv("screwdriver_picker", sizes, colors);
    // printf("\n");
    // HoldCtrl  right_ctrl("action_provider","right", !use_robot);
    printf("\n");
    ROS_INFO("READY! Waiting for service messages..\n");

    ros::spin();
    return 0;
}

