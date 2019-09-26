/**
 * Copyright (C) 2017 Social Robotics Lab, Yale University
 * Author: Alessandro Roncone
 * email:  alessandro.roncone@yale.edu
 * website: www.scazlab.yale.edu
 * Permission is granted to copy, distribute, and/or modify this program
 * under the terms of the GNU Lesser General Public License, version 2.1 or any
 * later version published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details
**/

#ifndef __ARM_CONTROLLER_H__
#define __ARM_CONTROLLER_H__

#include <map>
#include <thread>
#include <mutex>

#include "robot_interface/robot_interface.h"
#include "robot_interface/gripper.h"

#include "human_robot_collaboration_msgs/AskFeedback.h"

#define HAND_OVER_START  "handover_start"
#define HAND_OVER_READY  "handover_ready"
#define HAND_OVER_DONE   "handover_done"
#define HAND_OVER_WAIT   "handover_wait"

class ArmCtrl : public Gripper, public RobotInterface
{
private:
    // Substate of the controller (useful to keep track of
    // long actions that need multiple internal states, or
    // to store the error state of the controller in case of
    // unsuccessful actions
    std::string       sub_state;

    // High level action the controller is engaged in
    std::string          action;

    // Previous high level action (for complex actions)
    std::string     prev_action;

    // Vector of object ids the client is requesting the controller to act upon.
    // Among them, the controller will select those available in the DB, those
    // visible in the field of view and if there still is multiple choice,
    // it will randomly pick one of them
    std::vector<int> object_ids;

    // Selected Object ID the controller is acting upon (if the action is done with
    // respect to an object), among the list of requested objects
    int           sel_object_id;

    // Flag to know if the robot will try to recover from an error
    // or will wait the external planner to take care of that
    bool      internal_recovery;

    // Service to request actions to
    ros::ServiceServer  service;

    // Internal service used for multi-arm actions
    ros::ServiceServer service_other_limb;

    // Home configuration. Setting it in any of the children
    // of this class is mandatory (through the virtual method
    // called setHomeConfiguration() )
    Eigen::VectorXd home_conf;

    /**
     * Object database, which pairs an integer key, corresponding to the marker ID
     * placed on the object and read by ARuco, with a string that describes the object
     * itself in human terms.
     */
    std::map<int, std::string> object_db;

    // internal thread functionality
    std::thread arm_thread;

    // speed of the arm during some actions (e.g. pickup)
    double arm_speed;

    // Flag to know if the cuff button has been pressed
    bool cuff_button_pressed;

    // Vector of squish thresholds (NOT CURRENTLY USED)
    std::vector<double> squish_thresholds;

    // Position of the latest picked up object.
    Eigen::Vector3d pickedup_pos;

    /**
     * Provides basic functionalities for the object, such as a goHome and open.
     * For deeper, class-specific specialization, please modify doAction() instead.
     */
    void InternalThreadEntry();

    /**
     * Wrapper for Gripper:open() so that it can fit the action_db specifications
     * in terms of function signature.
     *
     * @return true/false if success/failure
     */
    bool openImpl() { return open(); }

protected:
    /*
     * Callback function for the upper (oval) CUFF OK button.
     * Specialized from RobotInterface::cuffUpperCb. Here it is used to receive
     * feedback from the user about the internal states of the hold action
     *
     * @param msg the topic message
     */
    virtual void cuffUpperCb(const intera_core_msgs::IODeviceStatus& msg);

    /**
     * Waits for the user to press the cuff button. Used in the hold action.
     *
     * @param _wait_time Time duration (in s) after which the method will return false
     * @return           true/false if button has been pressed.
     */
    bool waitForUserCuffUpperFb(double _wait_time = 60.0);

    /**
     * Pointer to the action prototype function, which does not take any
     * input argument and returns true/false if success/failure
     */
    typedef bool(ArmCtrl::*f_action)();

    /**
     * Action database, which pairs a string key, corresponding to the action name,
     * with its relative action, which is an f_action.
     *
     * Please be aware that, by default, if the user calls an action with the wrong
     * key or an action that is not available, the code will segfault. By C++
     * standard: operator[] returns (*((insert(make_pair(x, T()))).first)).second
     * Which means that if we are having a map of pointers to functions, a wrong key
     * will segfault the software. A layer of protection has been put in place to
     * avoid accessing a non-existing key (so this does not happen any more, but it
     * is still worth knowing).
     */
    std::map <std::string, f_action> action_db;

    /**
     * Recovers from errors during execution. It provides a basic interface,
     * but it is advised to specialize this function in the ArmCtrl's children.
     */
    virtual void recoverFromError();

    /**
     * Moves arm in a direction requested by the user, relative to the current
     * end-effector position
     *
     * @param dir  the direction of motion (left right up down forward backward)
     * @param dist the distance from the end-effector starting point
     *
     * @return true/false if success/failure
     */
    bool moveArm(std::string dir, double dist, std::string mode = "loose",
                                             bool disable_coll_av = false);

    /*
     * Moves arm to the requested pose , and checks if the pose has been achieved.
     * Specializes the RobotInterface::gotoPose method by setting the sub_state to
     * INV_KIN_FAILED if the method returns false.
     *
     * @param  requested pose (3D position + 4D quaternion for the orientation)
     * @param  mode (either loose or strict, it checks for the final desired position)
     * @return true/false if success/failure
     */
    bool goToPose(double px, double py, double pz,
                  double ox, double oy, double oz, double ow,
                  std::string mode="loose", bool disable_coll_av = false);

    /**
     * Placeholder for an action that has not been implemented (yet)
     *
     * @return false always
     */
    bool notImplemented();

    /**
     * Adds an object to the object database
     * @param  id the id of the object as read by ARuco
     * @param  n  its name as a string
     * @return    true/false if the insertion was successful or not
     */
    bool insertObject(int id, const std::string &n);

    /**
     * Adds an array of objects from an XmlRpcValue read from the parameter server.
     * It is encoded as an entire namespace of parameters using a YAML dictionary.
     * This is a valid parameter to set in your launch file:
     *
     * <rosparam param = "action_provider/objects_left">
     *   "left leg":      17
     *   "top":           21
     *   "central frame": 24
     *   "right leg":     26
     * </rosparam>
     *
     * @param  _param the XmlRpcValue read from the parameter server.
     * @return        true/false if the insertion was successful or not
     */
    bool insertObjects(XmlRpc::XmlRpcValue _params);

    /**
     * Removes an object from the database. If the object is not in the
     * database, the return value will be false.
     *
     * @param   id the object to be removed
     * @return     true/false if the removal was successful or not
     */
    bool removeObject(int id);

    /**
     * Gets an object's name from the object database
     *
     * @param    id the requested object's ID
     * @return      the associated string
     *              (empty string if object is not there)
     */
    std::string getObjectNameFromDB(int id);

    /**
     * Gets an object's ID from the object database
     *
     * @param   _name the requested object's name
     * @return      the associated id
     *              (-1 if object is not there)
     */
    int getObjectIDFromDB(std::string _name);

    /**
     * Checks if an object is available in the database
     *
     * @param  id the object to check for
     * @return    true/false if the object is available in the database
     */
    bool isObjectInDB(int id);

    /**
     * Checks if a set of objects is available in the database
     *
     * @param _objs The list of IDs of objects to choose from
     * @return      The list of IDs of objects that are available
     *              in the objectDB among those requested.
     */
    std::vector<int> areObjectsInDB(const std::vector<int> &_objs);

    /**
     * Chooses the object to act upon according to some rule. This method
     * needs to be specialized in any derived class because it is dependent
     * on the type of action and the type of sensory capabilities available.
     *
     * @param _objs The list of IDs of objects to choose from
     * @return      the ID of the chosen object (by default the ID of the
     *              first object will be chosen)
     */
    virtual int chooseObjectID(std::vector<int> _objs) { return _objs[0]; };

    /**
     * Prints the object database to screen.
     */
    void printObjectDB();

    /**
     * Converts the action database to a string.
     * @return the list of allowed actions, separated by a comma.
     */
    std::string objectDBToString();

    /**
     * Adds an action to the action database
     *
     * @param   a the action to be removed
     * @param   f a pointer to the action, in the form bool action()
     * @return    true/false if the insertion was successful or not
     */
    bool insertAction(const std::string &a, ArmCtrl::f_action f);

    /**
     * Removes an action from the database. If the action is not in the
     * database, the return value will be false.
     *
     * @param   a the action to be removed
     * @return    true/false if the removal was successful or not
     */
    bool removeAction(const std::string &a);

    /**
     * Calls an action from the action database
     *
     * @param    a the action to take
     * @return     true/false if the action called was successful or failed
     */
    bool callAction(const std::string &a);

    /**
     * This function wraps the arm-specific and task-specific actions.
     * For this reason, it has been implemented as virtual because it depends on
     * the child class.
     *
     * @param  s the state of the system BEFORE starting the action (when this
     *           method is called the state has been already updated to WORKING,
     *           so there is no way for the controller to recover it a part from
     *           this)
     * @param  a the action to do
     * @return   true/false if success/failure
     */
    virtual bool doAction(int s, std::string a);

    /**
     * Checks if an action is available in the database
     * @param             a the action to check for
     * @param  insertAction flag to know if the method has been called
     *                      inside insertAction (it only removes the
     *                      ROS_ERROR if the action is not in the DB)
     * @return   true/false if the action is available in the database
     */
    bool isActionInDB(const std::string &a, bool insertAction=false);

    /**
     * Prints the action database to screen.
     */
    void printActionDB();

    /**
     * Converts the action database to a string.
     * @return the list of allowed actions, separated by a comma.
     */
    std::string actionDBToString();

    /**
     * Publishes the high-level state of the controller (to be shown in the baxter display)
     */
    bool publishState();

    /**
     * Sets the previous action to (usually) the last action that has been requested.
     *
     * @param _prev_action the last action that has been requested
     * @return             true/false if success/failure
     */
    bool setPrevAction(const std::string& _prev_action);

    /**
     * Sets the sub state to a new sub state
     *
     * @param _sub_state the new sub state
     */
    virtual void setSubState(const std::string& _sub_state);

    /********************************************************************/
    /*                         HOME CAPABILITIES                        */
    /********************************************************************/
    /**
     * Home position with a specific joint configuration. This has
     * been introduced in order to force the arms to go to the home configuration
     * in always the same exact way, in order to clean the seed configuration in
     * case of subsequent inverse kinematics requests.
     *
     * @param  _disable_coll_av if to disable the collision avoidance while
     *                          performing the action or not
     * @return                  true/false if success/failure
     */
    bool homePoseStrict(bool _disable_coll_av = false);

    /**
     * Sets the joint-level configuration for the home position
     *
     * @param s0 First  shoulder joint
     * @param s1 Second shoulder joint
     * @param e0 First  elbow    joint
     * @param e1 Second elbow    joint
     * @param w0 First  wrist    joint
     * @param w1 Second wrist    joint
     * @param w2 Third  wrist    joint
     */
    void setHomeConf(double _s0, double _s1, double _e0, double _e1,
                                 double _w0, double _w1, double _w2);

    /**
     * Sets the joint-level configuration for the home position
     */
    virtual void setHomeConfiguration() { return; };

    /**
     * Sets the high-level configuration for the home position
     *
     * @param _loc the home position (either "pool" or "table")
     */
    void setHomeConfiguration(std::string _loc);

    /**
     * Goes to the home position, and "releases" the gripper
     *
     * @return        true/false if success/failure
     */
    bool goHome();

    bool testGripper();

    /********************************************************************/
    /*                        HOVER CAPABILITIES                        */
    /********************************************************************/
    /**
     * Hovers above table at a specific x-y position.
     *
     * @param  _height          the z-axis value of the end-effector position
     * @param  _mode            (loose/strict) it checks for the final desired position
     * @param  _disable_coll_av if to disable collision avoidance or not
     * @return                 true/false if success/failure
     */
    bool hoverAboveTable(double _height, std::string _mode = "loose",
                                     bool _disable_coll_av =  false);

    /**
     * Hovers above the pool of objects that is located at a specific x-y-z position.
     *
     * @param  _mode            (loose/strict) it checks for the final desired position
     * @param  _disable_coll_av if to disable collision avoidance or not
     * @return                 true/false if success/failure
     */
    bool hoverAbovePool(std::string     _mode = "loose",
                        bool _disable_coll_av =  false);

    /********************************************************************/
    /*                         HOLD CAPABILITIES                        */
    /********************************************************************/
    /**
     * Starts the hold behavior. Only available if gripper is electric.
     *
     * @return true/false if success/failure
     */
    virtual bool startHold();

    /**
     * Ends the hold behavior. Only available if gripper is electric.
     * @return true/false if success/failure
     */
    virtual bool endHold();

    /**
     * Holds the object. Only available if gripper is electric.
     *
     * @return true/false if success/failure
     */
    bool holdObject();

    /**
     * Reaches the hold position from which to initiate the holding behavior.
     * Right now two different hold positions are allowed: hold_leg and hold_top
     *
     * @return        true/false if success/failure
     */
    bool goHoldPose();

    /********************************************************************/
    /*                        PICKUP CAPABILITIES                       */
    /********************************************************************/

    /**
     * Retrieves an object from the pool of objects.
     *
     * @return true/false if success/failure
     */
    virtual bool getObject();

    /**
     * Selects the object for pickup, if there are more than one object to
     * choose from. Usually relies on perception in order to also look for
     * the objects that are actually present in the scene.
     *
     * @return true/false if success/failure
     */
    virtual bool selectObject4PickUp() { return true; };

    /**
     * Picks up the selected object by using ARuco's info on the tag
     *
     * @return true/false if success/failure
     */
    virtual bool pickUpObject() { return false; };

    /**
     * Passes an object to the human (or places it onto the workspace)
     *
     * @return true/false if success/failure
     */
    virtual bool passObject();

    /**
     * Moves an object to its final position during pass actions.
     *
     * @param  _human  if the object needs to be taken by an human
     * @return         true/false if success/failure
     */
    virtual bool moveObjectToPassPosition(bool &_human);

    /**
     * Combines getObject() and passObject() into a single action
     *
     * @return true/false if success/failure
     */
    bool getPassObject();

    /********************************************************************/
    /*                       CLEANUP CAPABILITIES                       */
    /********************************************************************/

    /**
     * Cleans up the selected object from the workspace
     *
     * @return true/false if success/failure
     */
    bool cleanUpObject();

    /**
     * Moves an object to its final position in the pool during cleanup actions.
     *
     * @return         true/false if success/failure
     */
    virtual bool moveObjectToPoolPosition() { return false; };

    /********************************************************************/
    /*                        SQUISH CAPABILITIES                       */
    /********************************************************************/

    /**
     * WARNING Not used since the API does not allow to change the squish
     * Stores initial squish thresholds to squish_thresholds, then
     * reduces and rewrites squish thresholds to aid in picking up tools
     */
    void reduceSquish();

    /**
     * WARNING Not used since the API does not allow to change the squish
     * Resets squish thresholds to original values stored in squish_thresholds
     */
    void resetSquish();

public:
    /**
     * Constructor
     */
    ArmCtrl(std::string        _name, std::string           _limb,
            bool   _use_robot = true, bool    _use_forces =  true,
            bool _use_trac_ik = true, bool _use_cart_ctrl = false);

    /*
     * Destructor
     */
    virtual ~ArmCtrl();

    /**
     * Starts thread that executes the control server.
     */
    bool startThread();

    /**
     * Callback for the service that requests actions
     * @param  req the action request
     * @param  res the action response (res.success either true or false)
     * @return     true always :)
     */
    bool serviceCb(human_robot_collaboration_msgs::DoAction::Request  &req,
                   human_robot_collaboration_msgs::DoAction::Response &res);

    /**
     * Callback for the service that lets the two limbs interact
     * @param  req the action request
     * @param  res the action response (res.success either true or false)
     * @return     true always :)
     */
    virtual bool serviceOtherLimbCb(human_robot_collaboration_msgs::AskFeedback::Request  &req,
                                    human_robot_collaboration_msgs::AskFeedback::Response &res);

    /* Self-explaining "setters" */
    virtual void setObjectID(int _obj)                { sel_object_id =  _obj; };
    virtual void setObjectIDs(std::vector<int> _objs) { object_ids    = _objs; };

    /**
     * Sets the action
     *
     * @param _action the new action
     * @return        true/false if success/failure
     */
    bool setAction(const std::string& _action);

    /*
     * Sets the internal state.
     *
     * @return true/false if success/failure
     */
    bool setState(int _state);

    /**
     * Sets the speed of the arm during some actions (e.g. pick up)
     *
     * @param  _arm_speed the new speed of the arm
     * @return            true/false if success/failure
     */
    bool setArmSpeed(double _arm_speed);

    /**
     * Sets the position of the picked up object to a new value
     *
     * @param  _pickedup_pos The new picked up object position
     * @return               true/false if success/failure
     */
    bool setPickedUpPos(const geometry_msgs::Point& _pickedup_pos);

    /**
     * Sets the position of the picked up object to a new value
     *
     * @param  _pickedup_pos The new picked up object position
     * @return               true/false if success/failure
     */
    bool setPickedUpPos(const Eigen::Vector3d& _pickedup_pos);

    /* Self-explaining "getters" */
    std::string        getSubState() { return         sub_state; };
    std::string          getAction() { return            action; };
    std::string      getPrevAction() { return       prev_action; };
    int                getObjectID() { return     sel_object_id; };
    std::vector<int>  getObjectIDs() { return        object_ids; };
    bool       getInternalRecovery() { return internal_recovery; };
    double             getArmSpeed() { return         arm_speed; };
    Eigen::Vector3d getPickedUpPos() { return      pickedup_pos; };
};

#endif
