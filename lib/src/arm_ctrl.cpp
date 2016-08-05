#include "robot_interface/arm_ctrl.h"

using namespace std;
using namespace geometry_msgs;
using namespace baxter_core_msgs;


ArmCtrl::ArmCtrl(string _name, string _limb) : ROSThread(_limb), Gripper(_limb),
                                               marker_id(-1), name(_name), action("")
{
    std::string other_limb = getLimb() == "right" ? "left" : "right";

    std::string service_name = "/"+name+"/service_"+_limb;
    service = _n.advertiseService(service_name, &ArmCtrl::serviceCb, this);
    ROS_INFO("[%s] Created service server with name : %s", getLimb().c_str(),
                                                        service_name.c_str());

    service_name = "/"+name+"/service_"+_limb+"_to_"+other_limb;
    service_other_limb = _n.advertiseService(service_name, &ArmCtrl::serviceOtherLimbCb,this);
    ROS_INFO("[%s] Created service server with name : %s", getLimb().c_str(),
                                                        service_name.c_str());
}

void ArmCtrl::InternalThreadEntry()
{
    std::string a =     getAction();
    int         s = int(getState());

    setState(WORKING);

    if (a == ACTION_HOME)
    {
        if (goHome())   setState(START);
    }
    else if (a == ACTION_RELEASE)
    {
        if (releaseObject())   setState(START);
    }
    else
    {
        if (!doAction(s, a))   setState(ERROR);
    }

    if (getState()==WORKING)
    {
        setState(ERROR);
    }

    pthread_exit(NULL);
    return;
}

bool ArmCtrl::serviceOtherLimbCb(baxter_collaboration::AskFeedback::Request  &req,
                                 baxter_collaboration::AskFeedback::Response &res)
{
    res.success = false;
    res.reply   = "not implemented";
    return true;
}

bool ArmCtrl::serviceCb(baxter_collaboration::DoAction::Request  &req,
                        baxter_collaboration::DoAction::Response &res)
{
    string action = req.action;
    int    ID     = req.object;

    ROS_INFO("[%s] Service request received. Action: %s object: %i",
                                                  getLimb().c_str(),
                                                action.c_str(), ID);
    // res.success = true;
    // return true;
    res.success = false;

    setAction(action);
    setMarkerID(ID);

    startInternalThread();
    ros::Duration(0.5).sleep();

    while( int(getState()) != START   &&
           int(getState()) != ERROR   &&
           int(getState()) != DONE    &&
           int(getState()) != PICK_UP   )
    {
        ros::spinOnce();
        ros::Rate(100).sleep();
    }

    if ( int(getState()) == START   ||
         int(getState()) == DONE    ||
         int(getState()) == PICK_UP   )
    {
        res.success = true;
    }

    ROS_INFO("[%s] Service reply with success: %s\n", getLimb().c_str(),
                                            res.success?"true":"false");
    return true;
}

bool ArmCtrl::moveArm(string dir, double dist, string mode, bool disable_coll_av)
{
    Point start = getPos();
    Point final = getPos();

    Quaternion ori = getOri();

    if      (dir == "backward") final.x -= dist;
    else if (dir == "forward")  final.x += dist;
    else if (dir == "right")    final.y -= dist;
    else if (dir == "left")     final.y += dist;
    else if (dir == "down")     final.z -= dist;
    else if (dir == "up")       final.z += dist;
    else                               return false;

    ros::Time t_start = ros::Time::now();

    bool finish = false;

    while(ros::ok())
    {
        if (disable_coll_av)    suppressCollisionAv();

        double t_elap = (ros::Time::now() - t_start).toSec();

        double px = start.x;
        double py = start.y;
        double pz = start.z;

        if (!finish)
        {
            if (dir == "backward" | dir == "forward")
            {
                int sgn = dir=="backward"?-1:+1;
                px = px + sgn * PICK_UP_SPEED * t_elap;

                if (dir == "backward")
                {
                    if (px < final.x) finish = true;
                }
                else if (dir == "forward")
                {
                    if (px > final.x) finish = true;
                }
            }
            if (dir == "right" | dir == "left")
            {
                int sgn = dir=="right"?-1:+1;
                py = py + sgn * PICK_UP_SPEED * t_elap;

                if (dir == "right")
                {
                    if (py < final.y) finish = true;
                }
                else if (dir == "left")
                {
                    if (py > final.y) finish = true;
                }
            }
            if (dir == "down" | dir == "up")
            {
                int sgn = dir=="down"?-1:+1;
                pz = pz + sgn * PICK_UP_SPEED * t_elap;

                if (dir == "down")
                {
                    if (px < final.z) finish = true;
                }
                else if (dir == "up")
                {
                    if (px > final.z) finish = true;
                }
            }
        }
        else
        {
            px = final.x;
            py = final.y;
            pz = final.z;
        }

        double ox = ori.x;
        double oy = ori.y;
        double oz = ori.z;
        double ow = ori.w;

        vector<double> joint_angles;
        if (!callIKService(px, py, pz, ox, oy, oz, ow, joint_angles)) return false;

        if (!goToPoseNoCheck(joint_angles))   return false;

        if(mode == "strict")
        {
            if(withinThres(getPos().x, final.x, 0.001) && 
               withinThres(getPos().y, final.y, 0.001) &&
               withinThres(getPos().z, final.z, 0.001)) break;
        }
        else if(mode == "loose")
        {
            if(withinThres(getPos().x, final.x, 0.01) && 
               withinThres(getPos().y, final.y, 0.01) &&
               withinThres(getPos().z, final.z, 0.01)) break;
        }

        ros::spinOnce();
        ros::Rate(100).sleep();
    }

    return true;
}

bool ArmCtrl::hoverAboveTable(double height, string mode, bool disable_coll_av)
{
    if (getLimb() == "right")
    {
        return ROSThread::goToPose(HOME_POS_R, height, VERTICAL_ORI_R,
                                                mode, disable_coll_av);
    }
    else if (getLimb() == "left")
    {
        if (height == Z_HIGH)
        {
            return ROSThread::goToPose(HOME_POS_L, height, VERTICAL_ORI_L,
                                                    mode, disable_coll_av);
        }

        while(ros::ok())
        {
            if (disable_coll_av)    suppressCollisionAv();

            JointCommand joint_cmd;
            joint_cmd.mode = JointCommand::POSITION_MODE;

            // joint_cmd.names
            setJointNames(joint_cmd);
            // joint_cmd.angles
            joint_cmd.command.push_back( 0.19673303604630432);      //'left_s0'
            joint_cmd.command.push_back(-0.870150601928001);        //'left_s1'
            joint_cmd.command.push_back(-1.0530778108833365);       //'left_e0'
            joint_cmd.command.push_back( 1.5577574900976376);       //'left_e1'
            joint_cmd.command.push_back( 0.6515583396543295);       //'left_w0'
            joint_cmd.command.push_back( 1.2463593901568986);       //'left_w1'
            joint_cmd.command.push_back(-0.1787087617886507);       //'left_w2'

            publish_joint_cmd(joint_cmd);

            ros::spinOnce();
            ros::Rate(100).sleep();
     
            if(hasPoseCompleted(HOME_POS_L, Z_LOW, VERTICAL_ORI_L))
            {
                return true;
            }
        }
    }
}

bool ArmCtrl::goHome()
{
    return hoverAboveTable(Z_LOW);
}

void ArmCtrl::recoverFromError()
{
    releaseObject();
    goHome();
}

ArmCtrl::~ArmCtrl()
{

}
