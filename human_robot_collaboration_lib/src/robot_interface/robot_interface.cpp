#include "robot_interface/robot_interface.h"

#include <tf/transform_datatypes.h>

using namespace              std;
using namespace            Eigen;
using namespace intera_core_msgs;

/**************************************************************************/
/*                         RobotInterface                                 */
/**************************************************************************/
RobotInterface::RobotInterface(string _name, string _limb, bool _use_robot, bool _use_simulator, double _ctrl_freq,
                               bool _use_forces, bool _use_trac_ik, bool _use_cart_ctrl, bool _is_experimental) :
                               nh(_name), name(_name), limb(_limb), state(START), spinner(8), use_robot(_use_robot), use_simulator(_use_simulator),
                               use_forces(_use_forces), ir_ok(false), curr_range(0.0), curr_min_range(0.0), curr_max_range(0.0),
                               ik_solver(_limb, "stp_021808TP00080", _use_robot), use_trac_ik(_use_trac_ik), ctrl_freq(_ctrl_freq),
                               filt_force(0.0, 0.0, 0.0), filt_change(0.0, 0.0, 0.0), time_filt_last_updated(ros::Time::now()),
                               is_coll_av_on(false), is_coll_det_on(false), is_closing(false), use_cart_ctrl(_use_cart_ctrl),
                               is_ctrl_running(false), is_experimental(_is_experimental), ctrl_track_mode(false),
                               ctrl_mode(human_robot_collaboration_msgs::GoToPose::POSITION_MODE),
                               ctrl_check_mode("strict"), ctrl_type("pose"), print_level(0), rviz_pub(_name)
{
    // if (not _use_robot) return;

    if (getLimb()=="left")
    {
        nh.param<double>("force_threshold_left",  force_thres, FORCE_THRES_L);
        nh.param<double>("force_filter_variance_left", filt_variance, FORCE_FILT_VAR_L);
        nh.param<double>("relative_force_threshold_left", rel_force_thres, REL_FORCE_THRES_L);
    }
    else if (getLimb()=="right")
    {
        nh.param<double>("force_threshold_right", force_thres, FORCE_THRES_R);
        nh.param<double>("force_filter_variance_right", filt_variance, FORCE_FILT_VAR_R);
        nh.param<double>("relative_force_threshold_right", rel_force_thres, REL_FORCE_THRES_R);
    }

    nh.param<int> ("/print_level", print_level, 0);

    ROS_INFO_COND(print_level>=0, "[%s] Print Level set to %i", getLimb().c_str(), print_level);
    ROS_INFO_COND(print_level>=1, "[%s] Cartesian Controller %s enabled", getLimb().c_str(), use_cart_ctrl?"is":"is NOT");
    ROS_INFO_COND(print_level>=1 && use_cart_ctrl, "[%s] ctrlFreq set to %g [Hz]", getLimb().c_str(), getCtrlFreq());
    ROS_INFO_COND(print_level>=3, "[%s] Force Threshold : %g", getLimb().c_str(), force_thres);
    ROS_INFO_COND(print_level>=3, "[%s] Force Filter Variance: %g", getLimb().c_str(), filt_variance);
    ROS_INFO_COND(print_level>=3, "[%s] Relative Force Threshold: %g", getLimb().c_str(), rel_force_thres);

    joint_cmd_pub  = nh.advertise<JointCommand>("/robot/limb/" + getLimb() + "/joint_command", 200);
    coll_av_pub    = nh.advertise<std_msgs::Empty>("/robot/limb/" + getLimb() + "/suppress_collision_avoidance", 200);

    endpt_sub      = nh.subscribe("/robot/limb/" + getLimb() + "/endpoint_state",
                                   SUBSCRIBER_BUFFER, &RobotInterface::endpointCb, this);

    ir_sub         = nh.subscribe("/robot/range/" + getLimb() + "_hand_range/state",
                                   SUBSCRIBER_BUFFER, &RobotInterface::IRCb, this);

    cuff_sub_lower = nh.subscribe("/robot/digital_io/" + getLimb() + "_lower_button/state",
                                   SUBSCRIBER_BUFFER, &RobotInterface::cuffLowerCb, this);

    cuff_sub_upper = nh.subscribe("/robot/digital_io/" + getLimb() + "_upper_button/state",
                                   SUBSCRIBER_BUFFER, &RobotInterface::cuffUpperCb, this);

    jntstate_sub   = nh.subscribe("/robot/joint_states",
                                   SUBSCRIBER_BUFFER, &RobotInterface::jointStatesCb, this);

    if (_use_simulator == false)
    {
        coll_av_sub    = nh.subscribe("/robot/limb/" + getLimb() + "/collision_avoidance_state",
                                       SUBSCRIBER_BUFFER, &RobotInterface::collAvCb, this);

        coll_det_sub   = nh.subscribe("/robot/limb/" + getLimb() + "/collision_detection_state",
                                       SUBSCRIBER_BUFFER, &RobotInterface::collDetCb, this);
    }

    std::string topic = "/"+getName()+"/"+getLimb()+"/state";
    state_pub = nh.advertise<human_robot_collaboration_msgs::ArmState>(topic, SUBSCRIBER_BUFFER, true);
    ROS_INFO_COND(print_level>=1, "[%s] Created state publisher with name : %s", getLimb().c_str(), topic.c_str());

    if (use_cart_ctrl)
    {
        string topic = "/" + getName() + "/" + getLimb() + "/go_to_pose";
        ctrl_sub     = nh.subscribe(topic, SUBSCRIBER_BUFFER, &RobotInterface::ctrlMsgCb, this);
        ROS_INFO_COND(print_level>=1, "[%s] Created cartesian controller that listens to : %s", getLimb().c_str(), topic.c_str());
    }

    if (not use_trac_ik)
    {
        ik_client = nh.serviceClient<SolvePositionIK>("/ExternalTools/" + getLimb() +
                                                       "/PositionKinematicsNode/IKService");
    }

    spinner.start();

    if (use_cart_ctrl)
    {
        startThread();
        setState(START);
    }

    if (is_experimental)    ROS_WARN("[%s] Experimental mode enabled!", getLimb().c_str());
}

bool RobotInterface::startThread()
{
    ctrl_thread = std::thread(&RobotInterface::ThreadEntry, this);

    // joinable checks if thread identifies as active thread of execution
    return ctrl_thread.joinable();
}

void RobotInterface::ThreadEntry()
{
    ros::Rate r(ctrl_freq);

    while (ros::ok() && not isClosing())
    {
        // ROS_INFO("Time: %g", (ros::Time::now() - initTime).toSec());

        if (isCtrlRunning())
        {
            double time_elap = (ros::Time::now() - time_start).toSec();

            // Starting pose in terms of position and orientation
            // geometry_msgs::Point      p_s =    pose_start.position;
            geometry_msgs::Quaternion o_s = pose_start.orientation;

            // Desired  pose in terms of position and orientation
            geometry_msgs::Point      p_d =      pose_des.position;
            geometry_msgs::Quaternion o_d =   pose_des.orientation;

            if (!isPoseReached(p_d, o_d, ctrl_check_mode, getCtrlType()))
            {
                // Current pose to send to the IK solver.
                pose_curr = pose_des;

                /* POSITIONAL PART */
                Eigen::Vector3d pos_curr = particle->getCurrPoint();

                geometry_msgs::Point p_c;
                p_c.x = pos_curr[0];
                p_c.y = pos_curr[1];
                p_c.z = pos_curr[2];

                pose_curr.position = p_c;

                /* ORIENTATIONAL PART */
                // We use a spherical linear interpolation between o_s and o_d. The speed of the interpolation
                // depends on ARM_ROT_SPEED, which is fixed and defined in utils.h
                tf::Quaternion o_d_TF, o_s_TF;
                tf::quaternionMsgToTF(o_d, o_d_TF);
                tf::quaternionMsgToTF(o_s, o_s_TF);
                double angle     = o_s_TF.angleShortestPath(o_d_TF);
                double traj_time =            angle / ARM_ROT_SPEED;

                if (time_elap < traj_time)
                {
                    tf::Quaternion o_c_TF = o_s_TF.slerp(o_d_TF, time_elap / traj_time);
                    o_c_TF.normalize();
                    geometry_msgs::Quaternion o_c;
                    tf::quaternionTFToMsg(o_c_TF, o_c);

                    pose_curr.orientation = o_c;
                }

                // ROS_INFO("[%s] Current Pose: %s Time %g/%g", getLimb().c_str(), print(pose_curr).c_str(),
                //                                                                    time_elap, traj_time);

                if (!goToPoseNoCheck(pose_curr))
                {
                    ROS_WARN("[%s] desired configuration could not be reached.", getLimb().c_str());
                    setCtrlRunning(false);
                    setState(CTRL_FAIL);
                }

                if (hasCollidedIR("strict")) ROS_INFO_THROTTLE(2, "[%s] is colliding!", getLimb().c_str());
            }
            else if (not ctrl_track_mode)
            {
                ROS_INFO("[%s] Pose reached!\n", getLimb().c_str());
                particle -> stop();

                if (ctrl_mode == human_robot_collaboration_msgs::GoToPose::VELOCITY_MODE)
                {
                    goToJointConfNoCheck(Eigen::VectorXd::Constant(7, 0.0));
                }
                setCtrlRunning(false);
                setState(CTRL_DONE);
            }
        }

        r.sleep();
    }
    return;
}

void RobotInterface::setIsClosing(bool arg)
{
    std::lock_guard<std::mutex> lck(mtx_is_closing);
    is_closing = arg;
}

bool RobotInterface::isClosing()
{
    std::lock_guard<std::mutex> lck(mtx_is_closing);
    return is_closing;
}

bool RobotInterface::ok()
{
    bool res = ros::ok();
    res = res && getState() != KILLED && getState() != STOPPED;

    return res;
}

bool RobotInterface::getIKLimits(KDL::JntArray &ll, KDL::JntArray &ul)
{
    return ik_solver.getKDLLimits(ll,ul);
}

bool RobotInterface::setIKLimits(KDL::JntArray ll, KDL::JntArray ul)
{
    ik_solver.setKDLLimits(ll,ul);
    return true;
}

bool RobotInterface::initCtrlParams()
{
    time_start = ros::Time::now();
    pose_start = getPose();

    particle.reset(new LinearPointParticle(getName()+"/"+getLimb(), THREAD_FREQ, true));

    Eigen::Vector3d ps(pose_start.position.x, pose_start.position.y, pose_start.position.z);
    Eigen::Vector3d pd(  pose_des.position.x,   pose_des.position.y,   pose_des.position.z);

    LinearPointParticle *derived = dynamic_cast<LinearPointParticle*>(particle.get());
    derived->setupParticle(ps, pd, ARM_SPEED);

    return particle->isSet() && particle->start();
}

void RobotInterface::ctrlMsgCb(const human_robot_collaboration_msgs::GoToPose& _msg)
{
    if (int(getState()) != WORKING)
    {
        // Disabling the controller to prevent race conditions in creating new particle objects
        setCtrlRunning(false);

        // First, let's check if the type of the control command is allowed
        if (_msg.type == "stop")
        {
            ROS_INFO("[%s] Stopping cartesian controller server.", getLimb().c_str());
            setCtrlRunning(false);
            setState(CTRL_DONE);
            return;
        }

        if  (_msg.type ==   "position" || _msg.type ==       "pose" ||
             _msg.type == "relative_x" || _msg.type == "relative_y" || _msg.type == "relative_z")
        {
            if (_msg.type == "pose")
            {
                pose_des.position    = _msg.position;
                pose_des.orientation = _msg.orientation;
            }
            else
            {
                if (_msg.type == "position")
                {
                    pose_des.position = _msg.position;
                }
                else
                {
                    pose_des.position = getPos();
                }

                pose_des.orientation = getOri();
            }

            if (_msg.type == "relative_x")
            {
                pose_des.position.x += _msg.increment;
            }
            if (_msg.type == "relative_y")
            {
                pose_des.position.y += _msg.increment;
            }
            if (_msg.type == "relative_z")
            {
                pose_des.position.z += _msg.increment;
            }
        }
        else
        {
            ROS_ERROR("[%s] Requested command type %s not allowed!",
                                  getLimb().c_str(), _msg.type.c_str());
            return;
        }

        // Then, let's check if control mode is among the allowed options
        if (_msg.ctrl_mode != human_robot_collaboration_msgs::GoToPose::POSITION_MODE)
        {
            if (not is_experimental)
            {
                ROS_ERROR("[%s] As of now, the only tested control mode is POSITION_MODE. "
                          "To be able to use any other control mode, please set the "
                          "experimental flag in the constructor to true.", getLimb().c_str());
                return;
            }
            else
            {
                if (_msg.ctrl_mode == human_robot_collaboration_msgs::GoToPose::VELOCITY_MODE)
                {
                    ROS_WARN("[%s] Experimental VELOCITY_MODE enabled", getLimb().c_str());
                    ctrl_mode = human_robot_collaboration_msgs::GoToPose::VELOCITY_MODE;
                }
                else if (_msg.ctrl_mode == human_robot_collaboration_msgs::GoToPose::RAW_POSITION_MODE)
                {
                    ROS_WARN("[%s] Experimental RAW_POSITION_MODE enabled", getLimb().c_str());
                    ctrl_mode = human_robot_collaboration_msgs::GoToPose::RAW_POSITION_MODE;
                }
                else
                {
                    ROS_ERROR("[%s] Requested control mode %i not allowed!",
                                          getLimb().c_str(), _msg.ctrl_mode);
                    return;
                }
            }
        }

        ctrl_mode = _msg.ctrl_mode;

        // Finally, let's check if check mode is among the allowed options
        if (_msg.check_mode != "strict" && _msg.check_mode != "loose")
        {
            ctrl_check_mode = "strict";
            ROS_WARN("[%s] Requested check mode %s not allowed! Using strict by default",
                                              getLimb().c_str(), _msg.check_mode.c_str());
        }
        else
        {
            ctrl_check_mode = _msg.check_mode;
        }

        ctrl_track_mode = _msg.tracking_mode=="on"?true:false;

        if (initCtrlParams())
        {
            setCtrlRunning(true);
        }
        else
        {
            ROS_ERROR("[%s] Initialization of control parameters has failed!", getLimb().c_str());
            return;
        }

        ROS_INFO("[%s] Received new target pose: %s control mode: %i",
                 getLimb().c_str(), print(pose_des).c_str(), ctrl_mode);

        ROS_INFO("[%s] Check mode: %s Tracking_mode: %s",
                 getLimb().c_str(), ctrl_check_mode.c_str(), ctrl_track_mode==true?"ON":"OFF");
    }
    else
    {
        ROS_ERROR_THROTTLE(1, "[%s] Received new target control command, but the controller is already"
                              " in use through the high level interface!", getLimb().c_str());
    }

    return;
}

void RobotInterface::setCtrlRunning(bool _flag)
{
    // ROS_INFO("[%s] Setting is_ctrl_running to: %i", getLimb().c_str(), _flag);

    std::lock_guard<std::mutex> lck(mtx_ctrl);
    is_ctrl_running = _flag;

    if (_flag == true)
    {
        rviz_pub.start();
        setState(CTRL_RUNNING);
    }
    else
    {
        rviz_pub.stop();
        particle.reset();
        // setState(   CTRL_DONE);
    }

    return;
}

bool RobotInterface::isCtrlRunning()
{
    std::lock_guard<std::mutex> lck(mtx_ctrl);
    bool res = is_ctrl_running;

    // ROS_INFO("[%s] is_ctrl_running equal to: %s", getLimb().c_str(), res==true?"TRUE":"FALSE");

    return res;
}

void RobotInterface::collAvCb(const intera_core_msgs::CollisionAvoidanceState& _msg)
{
    if (_msg.collision_object.size()!=0)
    {
        is_coll_av_on =  true;

        string objects = "";
        for (size_t i = 0; i < _msg.collision_object.size(); ++i)
        {
            // Let's remove the first part of the collision object name for visualization
            // purposes, i.e. the part that says "collision_"
            objects = objects + " " + string(_msg.collision_object[i]).erase(0,10);
        }
        ROS_WARN_THROTTLE(1, "[%s] Collision avoidance with: %s",
                             getLimb().c_str(), objects.c_str());
    }
    else is_coll_av_on = false;

    return;
}

void RobotInterface::collDetCb(const intera_core_msgs::CollisionDetectionState& _msg)
{
    if (_msg.collision_state==true)
    {
        is_coll_det_on = true;

        ROS_WARN_THROTTLE(1, "[%s] Collision detected!", getLimb().c_str());
    }
    else is_coll_det_on = false;

    return;
}

void RobotInterface::jointStatesCb(const sensor_msgs::JointState& _msg)
{
    JointCommand joint_cmd;
    setJointNames(joint_cmd);

    if (_msg.name.size() >= joint_cmd.names.size())
    {
        // ROS_INFO("[%s] jointStatesCb", getLimb().c_str());
        std::lock_guard<std::mutex> lck(mtx_jnts);

        // cout << "Joint state ";
        // for (size_t i = 9; i < 16; ++i)
        // {
        //     cout << "[" << i << "] " << _msg.name[i] << " " << _msg.position[i] << "\t";
        // }
        // cout << endl;
        curr_jnts.name.clear();
        curr_jnts.position.clear();
        curr_jnts.velocity.clear();

        for (size_t i = 0; i < joint_cmd.names.size(); ++i)
        {
            for (size_t j = 0; j < _msg.name.size(); ++j)
            {
                if (joint_cmd.names[i] == _msg.name[j])
                {
                    curr_jnts.name.push_back(_msg.name[j]);
                    curr_jnts.position.push_back(_msg.position[j]);
                    curr_jnts.velocity.push_back(_msg.velocity[j]);
                }
            }
        }
    }

    return;
}

void RobotInterface::cuffLowerCb(const intera_core_msgs::DigitalIOState& _msg)
{
    if (_msg.state == intera_core_msgs::DigitalIOState::PRESSED)
    {
        // This if is placed because we couldn't have a ROS_INFO_COND_THROTTLE
        if (print_level >= 2)
        {
            ROS_INFO_THROTTLE(1, "Lower cuff button pressed!");
        }

        setState(KILLED);
    }

    return;
}

void RobotInterface::cuffUpperCb(const intera_core_msgs::DigitalIOState& _msg)
{
    if (_msg.state == intera_core_msgs::DigitalIOState::PRESSED)
    {
        // This if is placed because we couldn't have a ROS_INFO_COND_THROTTLE
        if (print_level >= 2)
        {
            ROS_INFO_THROTTLE(1, "Upper cuff button pressed!");
        }

        setState(KILLED);
    }

    return;
}

void RobotInterface::endpointCb(const intera_core_msgs::EndpointState& _msg)
{
    ROS_INFO_COND(print_level>=12, "endpointCb");
    tf::StampedTransform _transform;
    if (1)
    {
        try
        {
            tf_listener.lookupTransform("/base", "/stp_021808TP00080_tip", ros::Time(0), _transform);
            curr_pos.x = _transform.getOrigin().x();
            curr_pos.y = _transform.getOrigin().y();
            curr_pos.z = _transform.getOrigin().z();
            curr_ori.x = _transform.getRotation().x();
            curr_ori.y = _transform.getRotation().y();
            curr_ori.z = _transform.getRotation().z();
            curr_ori.w = _transform.getRotation().w();
        }
        catch (tf::TransformException ex)
        {
            //ROS_ERROR("%s", ex.what());
            curr_pos = _msg.pose.position;
            curr_ori = _msg.pose.orientation;
        }
        if (use_forces == true)
        {
            curr_wrench = _msg.wrench;
            filterForces();
        }
    }
    else
    {
        curr_pos = _msg.pose.position;
        curr_ori = _msg.pose.orientation;

        if (use_forces == true)
        {
            curr_wrench = _msg.wrench;
            filterForces();
        }
    }

    return;
}

void RobotInterface::IRCb(const sensor_msgs::Range& _msg)
{
    ROS_INFO_COND(print_level>=12, "IRCb");
    curr_range     = _msg.range;
    curr_max_range = _msg.max_range;
    curr_min_range = _msg.min_range;

    if (!ir_ok)
    {
        ir_ok = true;
    }

    return;
}

void RobotInterface::filterForces()
{
    double time_elap = ros::Time::now().toSec() - time_filt_last_updated.toSec();

    Vector3d  new_filt;
    Vector3d pred_filt;

    // initial attempt to update filter using a running average of
    // the forces on the arm (exponential moving average)
    new_filt[0] = (1 - FORCE_ALPHA) * filt_force[0] + FORCE_ALPHA * curr_wrench.force.x;
    new_filt[1] = (1 - FORCE_ALPHA) * filt_force[1] + FORCE_ALPHA * curr_wrench.force.y;
    new_filt[2] = (1 - FORCE_ALPHA) * filt_force[2] + FORCE_ALPHA * curr_wrench.force.z;

    for (int i = 0; i < 3; ++i)
    {
        // extrapolate a predicted new filter value using the
        // previous rate of change of the filter value:
        // new value = old value + rate of change * elapsed time
        pred_filt[i] = filt_force[i] + (filt_change[i] * time_elap);

        // update the rate of change of the filter using the new value from the initial attempt above
        filt_change[i] = (new_filt[i] - filt_force[i])/time_elap;

        // if the predicted filter value is very small or 0,
        // this is most likely the first time the filter is updated
        // (the filter values and rate of change start at 0),
        // so set the filter to the new value from the initial attempt above
        if (pred_filt[i] < FILTER_EPSILON)
        {
            filt_force[i] = new_filt[i];
        }
        else
        {
            // compare the initial attempt to the predicted filter value
            // if the relative difference is within a threshold defined in utils.h,
            // update the filter to the new value from the initial attempt
            // otherwise, the filter is not changed; this keeps the filter from changing
            // wildly while maintaining trends in the data
            if (abs((new_filt[i] - pred_filt[i])/pred_filt[i]) < filt_variance)
            {
                filt_force[i] = new_filt[i];
            }
        }
    }

    time_filt_last_updated = ros::Time::now();

}

bool RobotInterface::goToPoseNoCheck(geometry_msgs::Pose p)
{
    return goToPoseNoCheck(p.position, p.orientation);
}

bool RobotInterface::goToPoseNoCheck(geometry_msgs::Point p, geometry_msgs::Quaternion o)
{
    return goToPoseNoCheck(p.x, p.y, p.z, o.x, o.y, o.z, o.w);
}

bool RobotInterface::goToPoseNoCheck(double px, double py, double pz,
                                     double ox, double oy, double oz, double ow)
{
    VectorXd joint_angles;
    if (!computeIK(px, py, pz, ox, oy, oz, ow, joint_angles)) return false;

    return goToJointConfNoCheck(joint_angles);
}

bool RobotInterface::goToJointConfNoCheck(VectorXd joint_values)
{
    JointCommand     joint_cmd;
    joint_cmd.mode = ctrl_mode;

    setJointNames(joint_cmd);

    if (joint_cmd.mode == human_robot_collaboration_msgs::GoToPose::POSITION_MODE)
    {
        for (int i = 0; i < joint_values.size(); ++i)
        {
            joint_cmd.position.push_back(joint_values[i]);
        }
    }

    publishJointCmd(joint_cmd);

    return true;
}


bool RobotInterface::goToPose(double px, double py, double pz,
                              double ox, double oy, double oz, double ow,
                              string mode, bool disable_coll_av)
{
    VectorXd joint_angles;
    if (!computeIK(px, py, pz, ox, oy, oz, ow, joint_angles)) return false;

    ros::Rate r(800);
    while (RobotInterface::ok() && not isClosing())
    {
        if (disable_coll_av)
        {
            suppressCollisionAv();
        }
        else
        {
            if (is_coll_av_on == true)
            {
                ROS_ERROR("Collision Occurred! Stopping.");
                return false;
            }
        }

        if (!goToJointConfNoCheck(joint_angles))   return false;

        if (isPoseReached(px, py, pz, ox, oy, oz, ow, mode))
        {
            return true;
        }

        r.sleep();
    }

    return false;
}

bool RobotInterface::computeIK(geometry_msgs::Pose p, VectorXd& j)
{
    return computeIK(p.position, p.orientation, j);
}

bool RobotInterface::computeIK(geometry_msgs::Point p, geometry_msgs::Quaternion o,
                               VectorXd& j)
{
    return computeIK(p.x, p.y, p.z, o.x, o.y, o.z, o.w, j);
}

bool RobotInterface::computeIK(double px, double py, double pz,
                               double ox, double oy, double oz, double ow,
                               VectorXd& j)
{
    geometry_msgs::PoseStamped pose_stamp;
    pose_stamp.header.frame_id = "base";
    pose_stamp.header.stamp    = ros::Time::now();

    setPosition(   pose_stamp.pose, px, py, pz);
    setOrientation(pose_stamp.pose, ox, oy, oz, ow);

    j.resize(0);
    ros::Time start = ros::Time::now();
    float thresh_z = pose_stamp.pose.position.z + 0.01;

    while (RobotInterface::ok())
    {
        SolvePositionIK ik_srv;

        pose_stamp.header.stamp=ros::Time::now();

        //ik_srv.request.seed_mode=2;       // i.e. SEED_CURRENT
        ik_srv.request.seed_mode=0;         // i.e. SEED_AUTO

        ik_srv.request.pose_stamp.push_back(pose_stamp);
        ik_srv.request.seed_angles.push_back(getJointStates());

        ros::Time tn = ros::Time::now();

        //bool result = use_trac_ik?ik_solver.perform_ik(ik_srv):ik_client.call(ik_srv);
        bool result = ik_solver.perform_ik(ik_srv);

        if(result)
        {
            double te  = ros::Time::now().toSec()-tn.toSec();;
            if (te>0.010)
            {
                ROS_WARN_ONCE("\t\t\tTime elapsed in computing IK: %g",te);
            }

            if (ik_srv.response.result_type[0])
            {
                ROS_INFO_COND(print_level>=6, "Got solution!");

                j.resize(ik_srv.response.joints[0].position.size());

                for (size_t i = 0; i < ik_srv.response.joints[0].position.size(); ++i)
                {
                    j[i] = ik_srv.response.joints[0].position[i];
                }
                return true;
            }
            else
            {
                // if position cannot be reached, try a position with the same x-y coordinates
                // but higher z (useful when placing tokens)
                ROS_INFO_COND(print_level>=4, "[%s] IK solution not valid: %g %g %g",
                                                                   getLimb().c_str(),
                                                          pose_stamp.pose.position.x,
                                                          pose_stamp.pose.position.y,
                                                          pose_stamp.pose.position.z);
                pose_stamp.pose.position.z += 0.001;
            }
        }

        // if no solution is found within 50 milliseconds or no solution within the acceptable
        // z-coordinate threshold is found, then no solution exists and exit out of loop
        if ((ros::Time::now() - start).toSec() > 0.05 || pose_stamp.pose.position.z > thresh_z)
        {
            ROS_WARN("[%s] Did not find a suitable IK solution! Final Position %g %g %g",
                                                                       getLimb().c_str(),
                                                              pose_stamp.pose.position.x,
                                                              pose_stamp.pose.position.y,
                                                              pose_stamp.pose.position.z);
            return false;
        }
    }

    return false;
}

bool RobotInterface::hasCollidedIR(string mode)
{
    double thres = 0.0;

    if (getLimb() == "left")
    {
        if      (mode == "strict") thres = 0.050;
        else if (mode ==  "loose") thres = 0.067;
    }
    else if (getLimb() == "right")
    {
        if      (mode == "strict") thres = 0.089;
        else if (mode ==  "loose") thres = 0.110;
    }
    else
    {
        return false;
    }

    if (curr_range <= curr_max_range &&
        curr_range >= curr_min_range &&
        curr_range <= thres             ) return true;

    return false;
}

bool RobotInterface::hasCollidedCD()
{
    return is_coll_det_on;
}

bool RobotInterface::isPoseReached(geometry_msgs::Pose p, string mode, string type)
{
    return isPoseReached(p.position, p.orientation, mode, type);
}

bool RobotInterface::isPoseReached(geometry_msgs::Point p, geometry_msgs::Quaternion o,
                                   string mode, string type)
{
    return isPoseReached(p.x, p.y, p.z, o.x, o.y, o.z, o.w, mode, type);
}

bool RobotInterface::isPoseReached(double px, double py, double pz,
                                   double ox, double oy, double oz, double ow,
                                   string mode, string type)
{
    if (type == "pose" || type == "position")
    {
        if (!   isPositionReached(px, py, pz,     mode))  return false;
    }

    if (type == "pose" || type == "orientation")
    {
        if (!isOrientationReached(ox, oy, oz, ow, mode))  return false;
    }

    if (type != "pose" && type != "position" && type != "orientation")
    {
        ROS_ERROR("[%s] Type should be either pose, position or orientation."
                  " Received %s instead.", getLimb().c_str(), type.c_str());

        return false;
    }

    return true;
}

bool RobotInterface::isPositionReached(geometry_msgs::Point p, string mode)
{
    return isPositionReached(p.x, p.y, p.z, mode);
}

bool RobotInterface::isPositionReached(double px, double py, double pz, string mode)
{
    // ROS_INFO("[%s] Checking %s position. Error: %g %g %g", getLimb().c_str(),
    //               mode.c_str(), px-getPos().x, py-getPos().y, pz-getPos().z);

    if (mode == "strict")
    {
        if (abs(getPos().x-px) > 0.003) { return false; }
        if (abs(getPos().y-py) > 0.003) { return false; }
        if (abs(getPos().z-pz) > 0.003) { return false; }
    }
    else if (mode == "loose")
    {
        if (abs(getPos().x-px) > 0.010) { return false; }
        if (abs(getPos().y-py) > 0.010) { return false; }
        if (abs(getPos().z-pz) > 0.010) { return false; }
    }
    else
    {
        ROS_ERROR("[%s] Mode should be either strict or loose. Received %s instead.",
                                                    getLimb().c_str(), mode.c_str());
        return false;
    }

    return true;
}

bool RobotInterface::isOrientationReached(geometry_msgs::Quaternion q, string mode)
{
    return isOrientationReached(q.x, q.y, q.z, q.w, mode);
}

bool RobotInterface::isOrientationReached(double ox, double oy, double oz, double ow, string mode)
{
    tf::Quaternion des(ox, oy, oz, ow);
    tf::Quaternion cur;
    tf::quaternionMsgToTF(getOri(), cur);

    // ROS_INFO("[%s] Checking %s orientation. Curr %g %g %g %g Des %g %g %g %g Dot %g",
    //                                   getLimb().c_str(), mode.c_str(),
    //                                   getOri().x, getOri().y, getOri().z, getOri().w,
    //                                   ox, oy, oz, ow, des.dot(cur));

    if (mode == "strict")
    {
        if (abs(des.dot(cur)) < 0.98)  { return false; }
    }
    else if (mode == "loose")
    {
        if (abs(des.dot(cur)) < 0.95)  { return false; }
    }
    else
    {
        ROS_ERROR("[%s] Mode should be either strict or loose. Received %s instead.",
                                                    getLimb().c_str(), mode.c_str());
        return false;
    }

    return true;
}

bool RobotInterface::isConfigurationReached(VectorXd _dj, string _mode)
{
    if (_dj.size() < 7) { return false; }

    intera_core_msgs::JointCommand des_jnts;
    setJointNames(des_jnts);
    setJointCommands(_dj[0], _dj[1], _dj[2],
                     _dj[3], _dj[4], _dj[5], _dj[6], des_jnts);

    return isConfigurationReached(des_jnts, _mode);
}

bool RobotInterface::isConfigurationReached(intera_core_msgs::JointCommand _dj, string _mode)
{
    sensor_msgs::JointState cj = getJointStates();

    if (cj.position.size() < 7)    { return false; }

    ROS_INFO_COND(print_level>=6, "[%s] Checking configuration: Current %g %g %g %g %g %g %g"
                                                             "\tDesired %g %g %g %g %g %g %g",
                                                                            getLimb().c_str(),
                               cj.position[0], cj.position[1], cj.position[2], cj.position[3],
                                               cj.position[4], cj.position[5], cj.position[6],
                               _dj.position[0], _dj.position[1], _dj.position[2], _dj.position[3],
                                                _dj.position[4], _dj.position[5], _dj.position[6]);

    for (size_t i = 0; i < _dj.names.size(); ++i)
    {
        bool res = false;
        for (size_t j = 0; j < cj.name.size(); ++j)
        {
            if (_dj.names[i] == cj.name[j])
            {
                if (_mode == "strict")
                {
                    // It's approximatively half a degree
                    if (abs(_dj.position[i]-cj.position[j]) > 0.010) return false;
                }
                else if (_mode == "loose")
                {
                    // It's approximatively a degree
                    if (abs(_dj.position[i]-cj.position[j]) > 0.020) return false;
                }
                res = true;
            }
        }
        if (res == false)   return false;
    }

    return true;
}

void RobotInterface::setTracIK(bool _use_trac_ik)
{
    use_trac_ik = _use_trac_ik;
};

bool RobotInterface::setCtrlType(const std::string &_ctrl_type)
{
    if (_ctrl_type != "pose" && _ctrl_type != "position" && _ctrl_type != "orientation")
    {
        ROS_ERROR("[%s] Type should be either pose, position or orientation."
                  " Received %s instead.", getLimb().c_str(), _ctrl_type.c_str());

        return false;
    }

    ctrl_type = _ctrl_type;
    ROS_INFO_COND(print_level>=4, "[%s] Control type set to %s", getLimb().c_str(), ctrl_type.c_str());

    return true;
}

void RobotInterface::setJointNames(JointCommand& joint_cmd)
{
    joint_cmd.names.push_back(getLimb() + "_j0");
    joint_cmd.names.push_back(getLimb() + "_j1");
    joint_cmd.names.push_back(getLimb() + "_j2");
    joint_cmd.names.push_back(getLimb() + "_j3");
    joint_cmd.names.push_back(getLimb() + "_j4");
    joint_cmd.names.push_back(getLimb() + "_j5");
    joint_cmd.names.push_back(getLimb() + "_j6");
}

void RobotInterface::setJointCommands(double s0, double s1, double e0, double e1,
                                                 double w0, double w1, double w2,
                                      intera_core_msgs::JointCommand& joint_cmd)
{
    joint_cmd.position.push_back(s0);
    joint_cmd.position.push_back(s1);
    joint_cmd.position.push_back(e0);
    joint_cmd.position.push_back(e1);
    joint_cmd.position.push_back(w0);
    joint_cmd.position.push_back(w1);
    joint_cmd.position.push_back(w2);
}

double RobotInterface::relativeDiff(double a, double b)
{
    // returns the relative difference of a to b
    // 0.01 is added to b in case it is very small (this will not affect large values of b)
    return abs((a - b)/abs(b + 0.01));
}

bool RobotInterface::detectForceInteraction()
{
    // ROS_INFO("Filt Forces: %g, %g, %g", filt_force[0], filt_force[1], filt_force[2]);

    // compare the current force to the filter force. if the relative difference is above a
    // threshold defined in utils.h, return true

    if (relativeDiff(curr_wrench.force.x, filt_force[0]) > rel_force_thres ||
        relativeDiff(curr_wrench.force.y, filt_force[1]) > rel_force_thres ||
        relativeDiff(curr_wrench.force.z, filt_force[2]) > rel_force_thres)
    {
        ROS_INFO("Interaction: %g %g %g", curr_wrench.force.x, curr_wrench.force.y, curr_wrench.force.z);
        return true;
    }
    else
    {
        return false;
    }
}

bool RobotInterface::waitForForceInteraction(double _wait_time, bool disable_coll_av)
{
    ros::Time _init = ros::Time::now();

    ros::Rate r(100);
    while (RobotInterface::ok() && not isClosing())
    {
        if (disable_coll_av)          suppressCollisionAv();
        if (detectForceInteraction())           return true;

        r.sleep();

        if ((ros::Time::now()-_init).toSec() > _wait_time)
        {
            ROS_WARN("No force interaction has been detected in %gs!",_wait_time);
            return false;
        }
    }

    return false;
}

bool RobotInterface::waitForJointAngles(double _wait_time)
{
    ros::Time _init = ros::Time::now();

    ros::Rate r(100);
    while (RobotInterface::ok())
    {
        sensor_msgs::JointState _jnt_state = getJointStates();
        if (_jnt_state.position.size() > 0)      return true;

        r.sleep();

        if ((ros::Time::now()-_init).toSec() > _wait_time)
        {
            ROS_WARN("No joint angles received in %gs!",_wait_time);
            return false;
        }
    }

    return false;
}

sensor_msgs::JointState RobotInterface::getJointStates()
{
    sensor_msgs::JointState cj;

    std::lock_guard<std::mutex> lck(mtx_jnts);
    cj = curr_jnts;

    return   cj;
}

geometry_msgs::Pose RobotInterface::getPose()
{
    geometry_msgs::Pose res;

    res.position    = getPos();
    res.orientation = getOri();

    return res;
}

bool RobotInterface::setState(int _state)
{
    state.set(_state);

    // This if is placed because we couldn't have a ROS_INFO_COND_THROTTLE
    if (print_level >= 1)
    {
        ROS_INFO_THROTTLE(1, "[%s] State set to %s",
                             getLimb().c_str(), string(getState()).c_str());
    }


    // disable the cartesian controller server
    if (state == WORKING)
    {
        setCtrlRunning(false);
    }

    return publishState();
}

bool RobotInterface::publishState()
{
    human_robot_collaboration_msgs::ArmState msg;

    msg.state  = string(getState());

    state_pub.publish(msg);

    return true;
}

void RobotInterface::publishJointCmd(intera_core_msgs::JointCommand _cmd)
{
    // cout << "Joint Command: " << _cmd << endl;
    joint_cmd_pub.publish(_cmd);
}

void RobotInterface::suppressCollisionAv()
{
    coll_av_pub.publish(std_msgs::Empty());
}

RobotInterface::~RobotInterface()
{
    setIsClosing(true);
    if (ctrl_thread.joinable())
    {
        ctrl_thread.join();
    }
}

