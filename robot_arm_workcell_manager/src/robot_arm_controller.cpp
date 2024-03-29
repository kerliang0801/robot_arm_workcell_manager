/*
 * Robot Arm Controller
 * Objective: Motion Task accoriding to input string or pose set request
 * Author: Tan You Liang
 */


#include "robot_arm_controller.hpp"


RobotArmController::RobotArmController(): nh_("~"){

    std::cout << std::endl << " Starting RobotArmController::RobotArmController() "<< std::endl;

    // Load yaml path via ros param
    loadParameters();
    
    std::cout << "ControlGroup::ControlGroup(" << group_name_ << ") enter" << std::endl;
    move_group_.reset( new moveit::planning_interface::MoveGroupInterface(group_name_));
    current_state_name_ = "";

    double t_0 = ros::Time::now().toSec();
    double dt = ros::Time::now().toSec() - t_0;
    double timeout = 10.0;
    
    while (dt < timeout){
        if (move_group_ && move_group_->getName() == group_name_)
            break;
        else
            move_group_.reset(new moveit::planning_interface::MoveGroupInterface(group_name_));  
    }

    if (!move_group_ || move_group_->getName() != group_name_){
        std::cout << "    initializing move_group failed for: " << group_name_ 
            << std::endl;
        load_complete_ = false;
        return;
    }

    move_group_->setNumPlanningAttempts(5);
    move_group_->setPlanningTime(20.0);
    std::cout << "ControlGroup::ControlGroup(" << group_name_ << ") completed." << std::endl;
    load_complete_ = true;
    
    ROS_INFO("RobotArmController::RobotArmController() completed!! \n");
}


RobotArmController::~RobotArmController(){
    std::cout << "Called Robot Arm Controller: " << " destructor" << std::endl;
}


//*
//* ------------------------------- Functions -------------------------------------
//*

// Get yaml path from ros param server, then load content via yaml-cpp
bool RobotArmController::loadParameters(){

    if (nh_.getParam("/group_name", group_name_)){
      ROS_INFO(" [PARAM] Got group_name param: %s", group_name_.c_str());
    }
    else{
      ROS_ERROR(" [PARAM] Failed to get param 'group_name'");
      return false;
    }

    std::string _yaml_path = "";
    if (nh_.getParam("/motion_target_yaml_path", _yaml_path)){
      ROS_INFO(" [PARAM] Got path param: %s", _yaml_path.c_str());
    }
    else{
      ROS_ERROR(" [PARAM] Failed to get param 'motion_target_yaml_path'");
      return false;
    }
    std::cout<<"[PARAM] YAML Path: "<<_yaml_path<<std::endl;


    try {
        NAMED_TARGET_CONFIG_ = YAML::LoadFile(_yaml_path);
    } 
    catch (std::exception& err){
        ROS_ERROR("exception in YAML LOADER: %s", err.what());
        return false;
    }
    ROS_INFO(" Motion Target YAML: Loading Completed! ");

    return true;
}


bool RobotArmController::moveToNamedTarget(const std::string& _target_name){
    
    std::string goal_type;
    double vel_factor;
    std::vector<double> goal_values;

    try {
        goal_type = NAMED_TARGET_CONFIG_["named_target"][_target_name]["type"].as<std::string>();
        goal_values = NAMED_TARGET_CONFIG_["named_target"][_target_name]["values"].as<std::vector<double>>();
        vel_factor = NAMED_TARGET_CONFIG_["named_target"][_target_name]["velFactor"].as<double>();
    } 
    catch (std::exception& err){
        ROS_ERROR("Exception in YAML LOADER: %s", err.what());
        return false;
    }
    
    ROS_INFO("Loaded Named Target: %s, type: %s, velFactor: %f, values: [ %f %f %f %f %f %f ]",  
        _target_name.c_str(), goal_type.c_str(), vel_factor,
        goal_values[0], goal_values[1], goal_values[2], goal_values[3], goal_values[4], goal_values[5]);

    // Joint Space Goal Mode
    if (goal_type.compare("joint_space_goal") == 0){
        std::vector<double> joints_target = {goal_values[0], goal_values[1], goal_values[2], goal_values[3], goal_values[4], goal_values[5]};
        return moveToJointsTarget(joints_target, vel_factor );
    }

    // Eef Pose Goal Mode
    if (goal_type.compare("eef_pose_goal") == 0){
        tf2::Quaternion quat;
        quat.setEuler( goal_values[3], goal_values[4], goal_values[5] );
        geometry_msgs::Pose target_pose;
        target_pose.position.x = goal_values[0];
        target_pose.position.y = goal_values[1];
        target_pose.position.z = goal_values[2];
        target_pose.orientation = tf2::toMsg(quat);
        return moveToEefTarget(target_pose, vel_factor);
    }

    ROS_ERROR("Invalid Motion Target, Please checked specified target in YAML");
    return false;
}


bool RobotArmController::moveToJointsTarget(const std::vector<double>& joints_target_values, double vel_factor){
    
    move_group_->setMaxVelocityScalingFactor(vel_factor);
    move_group_->setStartStateToCurrentState();
    move_group_->setJointValueTarget(joints_target_values);
    
    if (move_group_->plan(motion_plan_) != moveit::planning_interface::MoveItErrorCode::SUCCESS){
        ROS_ERROR("Joints Target motion planning: FAILED");
        return false;
    }

    ROS_INFO(" Executing Joint Space Motion...");
    move_group_->execute(motion_plan_);
    return true;
}


bool RobotArmController::moveToEefTarget(const geometry_msgs::Pose _eef_target_pose, double vel_factor ){

    const double jump_threshold = 0.0;
    const double eef_step = 0.01;

    move_group_->setMaxVelocityScalingFactor(vel_factor);
    
    // TODO: now only support cartesian, assume only one waypoint
    std::vector<geometry_msgs::Pose> waypoints;
    waypoints.push_back(_eef_target_pose);
    moveit_msgs::RobotTrajectory trajectory;
    double fraction = move_group_->computeCartesianPath(waypoints, eef_step, jump_threshold, trajectory);
    if (fraction != 1.0){
        ROS_ERROR("Eef Target motion plan: FAILED");
        return false;
    }

    // TODO: solve trajectory vs plan
    // motion_plan.start_state_ = *(move_group_->getCurrentState());
    motion_plan_.trajectory_ = trajectory;
    ROS_INFO(" Executing Cartesian Space Motion...");   
    move_group_->execute(motion_plan_);

    return true;
}



//////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ----------------------------------------------- MAIN ------------------------------------------------------
//////////////////////////////////////////////////////////////////////////////////////////////////////////////


int main(int argc, char** argv){
    std::cout << " YoYoYo, Robot Arm Controller is alive!!! =) " << std::endl;
    
    ros::init(argc, argv, "ur10_bot_controller", ros::init_options::NoSigintHandler);
    
    RobotArmController ur10_controller;
    ros::AsyncSpinner ros_async_spinner(1);
    ros_async_spinner.start();


    // *********************************************************************************
    // *************************** Starting of testing code ****************************

    std::cout<<" *********************** Starting to execute arm motion ********************" << std::endl;
    // NAMED!!!
    ur10_controller.moveToNamedTarget("home_position");
    std::cout<<" ---- Done Named Joint motion 1 -------" << std::endl;

    ur10_controller.moveToNamedTarget("pre_place_pose");
    std::cout<<" ---- Done Named Cartesian motion 1 -------" << std::endl;

    // test code!!!!!!!!!!!
    std::vector<double> joints_target_1 = {1, -1.7, 1.8, -0.1, 2.5, 0};
    ur10_controller.moveToJointsTarget(joints_target_1, 0.5);
    std::cout<<" ---- Done Joint motion 1 -------" << std::endl;

    std::vector<double> joints_target_2 = {-0.1, -1.56, 2.3, -0.8, 3.1, 0};
    ur10_controller.moveToJointsTarget(joints_target_2, 0.5);
    std::cout<<" ---- Done Joint motion 2 -------" << std::endl;
    
    geometry_msgs::Pose target_pose1;
    target_pose1.orientation.w = 1.0;
    target_pose1.position.x = 0.28;
    target_pose1.position.y = -0.2;
    target_pose1.position.z = 0.5;
    ur10_controller.moveToEefTarget(target_pose1, 1);
    std::cout<<" ---- Done Cartesian motion 1 -------" << std::endl;

    geometry_msgs::Pose target_pose2;
    target_pose2.orientation.w = 1.0;
    target_pose2.position.x = 0.28;
    target_pose2.position.y = -0.2;
    target_pose2.position.z = 0.7;
    ur10_controller.moveToEefTarget(target_pose2, 1);
    std::cout<<" ---- Done Cartesian motion 2 -------" << std::endl;

    ros::waitForShutdown();

    return 0;


}