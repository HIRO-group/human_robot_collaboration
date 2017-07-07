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

#ifndef __PARTICLE_THREAD_H__
#define __PARTICLE_THREAD_H__

#include <mutex>
#include <thread>

#include <Eigen/Dense>

#include "robot_utils/rviz_publisher.h"

/**
 * Particle Thread object. Abstract class that implements the thread function and
 * all the related members. Needs to be specialized in any derived class.
 */
class ParticleThread
{
private:
    std::string   name; // Name of the object
    std::thread thread; // Thread to update the particle
    ros::Rate        r; // Rate of the thread (in Hz)

    bool           is_running; // Flag to know if the thread has been started
    std::mutex mtx_is_running; // Mutex to protect the thread running flag

    bool           is_closing;  // Flag to close the thread entry function
    std::mutex mtx_is_closing;  // Mutex to protect the thread close flag

    Eigen::VectorXd curr_pt; // Current position (or orientation) of the particle
    std::mutex  mtx_curr_pt; // Mutex to protect access to the current point

    bool rviz_visualization; // Flag to know if to publish to rviz or not

protected:
    ros::Time start_time; // When the thread started

    bool is_particle_set; // If the particle has been setup. Defaults to false, to be
                          // specialized in derived classes with proper function that set it to true
                          // (otherwise the thread will not start)

    RVIZPublisher  rviz_pub; // Publisher to publish the point to rviz

    /*
     * Function that will be spun out as a thread
     */
    void internalThread();

    /**
     * Updates the particle. To be specialized in derived classes.
     *
     * @param  _new_pt updated point
     * @return         true/false if success/failure
     */
    virtual bool updateParticle(Eigen::VectorXd& _new_pt) = 0;

    /**
     * Sets the current point as a marker for the RVIZPublisher to publish
     */
    virtual void setMarker();

public:
    /**
     * Constructor
     *
     * @param  _name               name of the object
     * @param  _thread_rate        period of the timer
     * @param  _rviz_visualization if to publish the current point to rviz as a marker
     */
    explicit ParticleThread(std::string _name = "particle_thread",
                            double _thread_rate = THREAD_FREQ,
                            bool _rviz_visualization = false);

    /**
     * Starts the particle thread
     *
     * @return true/false if thread created/thread already running
     */
    bool start();

    /**
     * Stops the particle thread
     *
     * @return true/false if success/failure
     */
    bool stop();

    /**
     * Gets if the thread needs to be closed or not
     *
     * @return if the thread needs to be closed or not
     */
    bool isClosing();

    /**
     * Sets if the thread needs to be closed or not
     *
     * @param _is_closing if the thread needs to be closed or not
     * @return            true/false if success/failure
     */
    bool setIsClosing(bool _is_closing);

    /**
     * Gets if the thread is running
     *
     * @return if the thread is running or not
     */
    bool isRunning();

    /**
     * Sets if the thread is running or not
     *
     * @param _is_running if the thread is running or not
     * @return            true/false if success/failure
     */
    bool setIsRunning(bool _is_running);

    /**
     * Gets the rate of the thread
     *
     * @return the rate of the thread
     */
    double getRate();

    /**
     * Gets the current position (or orientation) of the particle
     *
     * @return the current position (or orientation) of the particle
     */
    Eigen::VectorXd getCurrPoint();

    /**
     * Sets the current position (or orientation) of the particle to a new one
     *
     * @param  _curr_pt the new position (or orientation) of the particle
     * @return          true/false if success/failure
     */
    bool setCurrPoint(const Eigen::VectorXd& _curr_pt);

    /**
     * Gets the name of the object
     * @return the name of the object
     */
    std::string     getName() { return      name; };

    /**
     * Destructor
     */
    ~ParticleThread();
};

/**
 * Implementation of ParticleThread object. Does not do much (only for testing purposes)
 */
class ParticleThreadImpl : public ParticleThread
{
protected:
    /**
     * Updates the particle.
     *
     * @param  _new_pt updated point
     * @return         true/false if success/failure
     */
    bool updateParticle(Eigen::VectorXd& _new_pt);

public:
    /**
     * Constructor
     *
     * @param  _name               name of the object
     * @param  _thread_rate        period of the timer
     * @param  _rviz_visualization if to publish the current point to rviz as a marker
     */
    explicit ParticleThreadImpl(std::string _name = "particle_thread_impl",
                                double _thread_rate = THREAD_FREQ,
                                bool _rviz_visualization = false);

    /**
     * Destructor
     */
    ~ParticleThreadImpl() {};
};

/**
 * ParticleThread for a 3D Point following a straight trajectory from start to end.
 */
class LinearPointParticle : public ParticleThread
{
private:
    double speed;

    Eigen::Vector3d start_pt; // Start point of the trajectory
    std::mutex  mtx_start_pt; // Mutex to protect access to the start point

    Eigen::Vector3d   des_pt; // Desired point of the trajectory
    std::mutex    mtx_des_pt; // Mutex to protect access to the desired point

protected:
    /**
     * Updates the particle.
     *
     * @param  _new_pt updated point
     * @return         true/false if success/failure
     */
    bool updateParticle(Eigen::VectorXd& _new_pt);

    /**
     * Sets the current point and the desired target as markers for the RVIZPublisher to publish
     */
    void setMarker();

    /**
     * Gets the start position of the particle
     *
     * @return the start position of the particle
     */
    Eigen::VectorXd getStartPoint();

    /**
     * Sets the start position of the particle to a new one
     *
     * @param  _start_pt the new start position of the particle
     * @return           true/false if success/failure
     */
    bool setStartPoint(const Eigen::VectorXd& _start_pt);

    /**
     * Gets the desired final position of the particle
     *
     * @return the desired final position of the particle
     */
    Eigen::VectorXd getDesPoint();

    /**
     * Sets the desired final position of the particle to a new one
     *
     * @param  _des_pt the new desired position of the particle
     * @return         true/false if success/failure
     */
    bool setDesPoint(const Eigen::VectorXd& _des_pt);

public:
    /**
     * Constructor
     *
     * @param  _name               name of the object
     * @param  _thread_rate        period of the timer
     * @param  _rviz_visualization if to publish the current point to rviz as a marker
     */
    explicit LinearPointParticle(std::string _name = "particle_thread_impl",
                                 double _thread_rate = THREAD_FREQ,
                                 bool _rviz_visualization = false);

    /**
     * Sets the parameters of the particle
     *
     * @param  _start_pt The starting point of the particle
     * @param  _des_pt   The desired (final) point of the particle
     * @param  _speed    The particle speed
     * @return           true/false if success/failure
     */
    bool setupParticle(const Eigen::Vector3d& _start_pt,
                       const Eigen::Vector3d&   _des_pt,
                       double _speed);

    /**
     * Destructor
     */
    ~LinearPointParticle();
};

#endif