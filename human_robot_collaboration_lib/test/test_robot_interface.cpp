#include <gtest/gtest.h>

#include "robot_interface/robot_interface.h"

using namespace std;

// Declare a test
TEST(RobotInterfaceTest, testConstructorDefaultValues)
{
    RobotInterface ri("robot", "left");

    EXPECT_FALSE(ri.isCtrlRunning());
    EXPECT_EQ(START, int(ri.getState()));

    EXPECT_EQ("robot", ri.getName());
    EXPECT_EQ( "left", ri.getLimb());
    EXPECT_TRUE(ri.isRobotUsed());
    EXPECT_EQ(  100.0, ri.getCtrlFreq());
    EXPECT_TRUE(ri.useForces());
    EXPECT_TRUE(ri.useTracIK());
    EXPECT_TRUE(ri.useCartCtrl());
    EXPECT_FALSE(ri.isExperimental());

    ri.setTracIK(false);
    EXPECT_FALSE(ri.useTracIK());
}

TEST(RobotInterfaceTest, testConstructorCustomValues)
{
    string          name = "robot";
    string          limb =  "left";
    bool       use_robot =   false;
    double     ctrl_freq =    50.0;
    bool      use_forces =   false;
    bool     use_trac_ik =   false;
    bool   use_cart_ctrl =   false;
    bool is_experimental =   false;
    RobotInterface ri(name, limb, use_robot, ctrl_freq, use_forces,
                      use_trac_ik, use_cart_ctrl, is_experimental);

    EXPECT_FALSE(ri.isCtrlRunning());
    EXPECT_EQ(START, int(ri.getState()));

    EXPECT_EQ(           name, ri.getName());
    EXPECT_EQ(           limb, ri.getLimb());
    EXPECT_EQ(      use_robot, ri.isRobotUsed());
    EXPECT_EQ(      ctrl_freq, ri.getCtrlFreq());
    EXPECT_EQ(     use_forces, ri.useForces());
    EXPECT_EQ(    use_trac_ik, ri.useTracIK());
    EXPECT_EQ(  use_cart_ctrl, ri.useCartCtrl());
    EXPECT_EQ(is_experimental, ri.isExperimental());

    use_trac_ik = true;
    ri.setTracIK(use_trac_ik);
    EXPECT_EQ(use_trac_ik, ri.useTracIK());

}

TEST(RobotInterfaceTest, testPrivateMethods)
{
    string          name = "robot";
    string          limb =  "left";
    bool       use_robot =   false;
    double     ctrl_freq =    50.0;
    bool      use_forces =   false;
    bool     use_trac_ik =   false;
    bool   use_cart_ctrl =   false;
    bool is_experimental =   false;
    RobotInterface ri(name, limb, use_robot, ctrl_freq, use_forces,
                      use_trac_ik, use_cart_ctrl, is_experimental);

    EXPECT_FALSE(ri.isPoseReached(geometry_msgs::Pose(),  "loose",         "sdf"));
    EXPECT_FALSE(ri.isPoseReached(geometry_msgs::Pose(), "strict",         "sdf"));
    EXPECT_FALSE(ri.isPoseReached(geometry_msgs::Pose(),    "sdf",        "pose"));
    EXPECT_FALSE(ri.isPoseReached(geometry_msgs::Pose(),    "sdf",    "position"));
    EXPECT_FALSE(ri.isPoseReached(geometry_msgs::Pose(),    "sdf", "orientation"));

    std::string type = "pose";
    EXPECT_FALSE(ri.setCtrlType(""));
    EXPECT_EQ(ri.getCtrlType(), type);
    EXPECT_FALSE(ri.setCtrlType(" "));
    EXPECT_EQ(ri.getCtrlType(), type);
    EXPECT_FALSE(ri.setCtrlType("asdfa"));
    EXPECT_EQ(ri.getCtrlType(), type);
    EXPECT_TRUE(ri.setCtrlType(type));
    EXPECT_EQ(ri.getCtrlType(), type);

    type = "position";
    EXPECT_TRUE(ri.setCtrlType(type));
    EXPECT_EQ(ri.getCtrlType(), type);

    type = "orientation";
    EXPECT_TRUE(ri.setCtrlType(type));
    EXPECT_EQ(ri.getCtrlType(), type);
}


TEST(RobotInterfaceTest, testCallbacks)
{
    ros::NodeHandle       nh;
    RobotInterface ri("robot", "left");

    EXPECT_EQ(ri.getCurrRange(), 0.0);
    EXPECT_EQ(ri.getCurrMinRange(), 0.0);
    EXPECT_EQ(ri.getCurrMaxRange(), 0.0);

   
    // Create publisher
    ros::Publisher ir_pub = 
        nh.advertise<sensor_msgs::Range>("/robot/range/" + ri.getLimb() + "_hand_range/state", SUBSCRIBER_BUFFER);
   
    // Publish stuff
    sensor_msgs::Range msg;

    msg.range = 1.0;
    msg.min_range = 2.0;
    msg.max_range = 3.0;

    ros::Rate loop_rate(10);

    while(ros::ok() && (ri.getCurrRange() == 0.0))
    {
        ir_pub.publish(msg);
        //ROS_INFO("%s", msg.range);

        ros::spinOnce();

        loop_rate.sleep();
    
    }

    // Check that the stuff has beeen correctly parsed by a robot interface object
    EXPECT_EQ(ri.getCurrRange(), 1.0);
    EXPECT_EQ(ri.getCurrMinRange(), 2.0);
    EXPECT_EQ(ri.getCurrMaxRange(), 3.0);

}

// Run all the tests that were declared with TEST()
int main(int argc, char **argv)
{
    ros::init(argc, argv, "robot_interface_test");
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}


