# ROS Framework Hardwares

## IIWA 7 Hardware Interface
1. Here we don't discuss the IIWA operation over the Java side of the robot.
2. Configure the robot and IIWA IP address and should be in the same network.
3. Also configure the `ROS_IP` and `ROS_MASTER_URI`
4. Before lauching the iiwa control run `roscore`to set `/iiwa/publishJointStates` is `true`in the parameter server. Or edit the launch file to do the same.
5. `roslaunch iiwa_moveit moveit_planning_execution.launch sim:=<true|false>` to run the ros iiwa controller and move_group. False if we need to run harware. 

## Panda Hardware interface
1. libfranka requires real-time ubunkernel](https://frankaemika.github.io/docs/installation.html#setting-up-the-real-time-kernel)
2. All source code must be compiled with [`-DCMAKE_BUILD_TYPE=Release`](https://frankaemika.github.io/docs/troubleshooting.html#troubleshooting) for *libfranka* and *franka_ros*. For binary installation this is not required.
3. Test the libfranka and FCI is properly interfaced
    * `./echo_robot_state <fci_ip>` or `rosrun libfranka echo_robot_state <fci_ip>`
    * `rosrun libfranka communication_test <fci_ip>`
4. For ROS control first launch [franka control](https://frankaemika.github.io/docs/franka_ros.html#franka-control)
   * ```roslaunch franka_control franka_control robot_ip:=<fci_ip> load_gripper:=<true|false>```
5. Sate of the robot can be seen throught the topic
   * `rostopic echo /franka_state_controller/franka_state` 
   * Message is `uint8 robot_mode`
   * When `robot_mode = 2` then the robot is completly operational.
6. If the robot mode changes during the collision or user pressing the button, then franka controller will go into the error mode. This can be resolved by publishing the following topic.
   * `rostopic pub -1 /franka_control/error_recovery/goal franka_control/ErrorRecoveryActionGoal "{}"`