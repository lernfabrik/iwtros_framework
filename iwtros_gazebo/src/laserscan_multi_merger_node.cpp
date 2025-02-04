/*** This code is copied from irq_laser_tool
 * and modified */
#include <ros/ros.h>
#include <string.h>
#include <tf/transform_listener.h>
#include <pcl_ros/transforms.h>
#include <laser_geometry/laser_geometry.h>
#include <pcl/conversions.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/io/pcd_io.h>
#include <sensor_msgs/PointCloud.h>
#include <sensor_msgs/PointCloud2.h>
#include <sensor_msgs/point_cloud_conversion.h> 
#include "std_msgs/Bool.h"
#include "sensor_msgs/LaserScan.h"
#include "pcl_ros/point_cloud.h"
#include <Eigen/Dense>
#include <dynamic_reconfigure/server.h>
#include <iwtros_gazebo/laserscan_multi_mergerConfig.h>

using namespace std;
using namespace pcl;
using namespace laserscan_multi_merger;

class LaserscanMerger
{
public:
    LaserscanMerger(std::string deactivatingTopic);
    void scanCallback(const sensor_msgs::LaserScan::ConstPtr& scan, std::string topic);
    void pointcloud_to_laserscan(Eigen::MatrixXf points, pcl::PCLPointCloud2 *merged_cloud);
    void reconfigureCallback(laserscan_multi_mergerConfig &config, uint32_t level);
    void deactivateBackScanCallback(const std_msgs::Bool::ConstPtr& detach);

private:
    ros::NodeHandle node_;
    laser_geometry::LaserProjection projector_;
    tf::TransformListener tfListener_;

    ros::Publisher point_cloud_publisher_;
    ros::Publisher laser_scan_publisher_;
    vector<ros::Subscriber> scan_subscribers;
    ros::Subscriber deactivation_subscriber_;
    vector<bool> clouds_modified;

    vector<pcl::PCLPointCloud2> clouds;
    vector<string> input_topics;

    void laserscan_topic_parser();

    double angle_min, tmp_angle_min;
    double angle_max, tmp_angle_max;
    double angle_increment;
    double time_increment;
    double scan_time;
    double range_min;
    double range_max;

    string destination_frame;
    string cloud_destination_topic;
    string scan_destination_topic;
    string laserscan_topics_front;
	string laserscan_topics_back;
};

void LaserscanMerger::reconfigureCallback(laserscan_multi_mergerConfig &config, uint32_t level)
{
	this->angle_min = config.angle_min;
	this->angle_max = config.angle_max;
	this->angle_increment = config.angle_increment;
	this->time_increment = config.time_increment;
	this->scan_time = config.scan_time;
	this->range_min = config.range_min;
	this->range_max = config.range_max;
}

void LaserscanMerger::laserscan_topic_parser()
{
	// Unsubscribe from previous topics
	for(int i=0; i<scan_subscribers.size(); ++i)
		scan_subscribers[i].shutdown();

	input_topics.push_back(laserscan_topics_front);
	input_topics.push_back(laserscan_topics_back);
	if(input_topics.size() > 0)
	{
		scan_subscribers.resize(input_topics.size());
		clouds_modified.resize(input_topics.size());
		clouds.resize(input_topics.size());
		ROS_INFO("Subscribing to topics\t%ld", scan_subscribers.size());
		for(int i=0; i<input_topics.size(); ++i)
		{
			scan_subscribers[i] = node_.subscribe<sensor_msgs::LaserScan> (input_topics[i].c_str(), 1, boost::bind(&LaserscanMerger::scanCallback,this, _1, input_topics[i]));
			clouds_modified[i] = false;
			cout << input_topics[i] << " ";
		}
	}
	else
		ROS_INFO("Not subscribed to any topic.");
}

LaserscanMerger::LaserscanMerger(std::string deactivatingTopic)
{
	ros::NodeHandle nh("~");

	nh.getParam("destination_frame", destination_frame);
	nh.getParam("cloud_destination_topic", cloud_destination_topic);
	nh.getParam("scan_destination_topic", scan_destination_topic);
    nh.getParam("laserscan_topics_front", laserscan_topics_front);
	 nh.getParam("laserscan_topics_back", laserscan_topics_back);

    this->laserscan_topic_parser();
    deactivation_subscriber_ = node_.subscribe<std_msgs::Bool> (deactivatingTopic.c_str(), 1, boost::bind(&LaserscanMerger::deactivateBackScanCallback, this, _1));

	point_cloud_publisher_ = node_.advertise<sensor_msgs::PointCloud2> (cloud_destination_topic.c_str(), 1, false);
	laser_scan_publisher_ = node_.advertise<sensor_msgs::LaserScan> (scan_destination_topic.c_str(), 1, false);

	tfListener_.setExtrapolationLimit(ros::Duration(0.1));
}

void LaserscanMerger::scanCallback(const sensor_msgs::LaserScan::ConstPtr& scan, std::string topic)
{
	sensor_msgs::PointCloud tmpCloud1,tmpCloud2;
	sensor_msgs::PointCloud2 tmpCloud3;

	/* std::string frame = scan->header.frame_id.c_str();
	std::string newFrame = frame.erase(0, 8);
	ROS_WARN(newFrame.c_str());*/
    // Verify that TF knows how to transform from the received scan to the destination scan frame
	tfListener_.waitForTransform(scan->header.frame_id.c_str(), destination_frame.c_str(), scan->header.stamp, ros::Duration(1));

	projector_.transformLaserScanToPointCloud(scan->header.frame_id, *scan, tmpCloud1, tfListener_);
	try
	{
		tfListener_.transformPointCloud(destination_frame.c_str(), tmpCloud1, tmpCloud2);
	}catch (tf::TransformException ex){ROS_ERROR("%s",ex.what());return;}

	for(int i=0; i<input_topics.size(); ++i)
	{
		if(topic.compare(input_topics[i]) == 0)
		{
			sensor_msgs::convertPointCloudToPointCloud2(tmpCloud2,tmpCloud3);
			pcl_conversions::toPCL(tmpCloud3, clouds[i]);
			clouds_modified[i] = true;
		}
	}	

    // Count how many scans we have
	int totalClouds = 0;
	for(int i=0; i<clouds_modified.size(); ++i)
		if(clouds_modified[i])
			++totalClouds;

    // Go ahead only if all subscribed scans have arrived
	if(totalClouds == clouds_modified.size())
	{
		pcl::PCLPointCloud2 merged_cloud = clouds[0];
		clouds_modified[0] = false;

		for(int i=1; i<clouds_modified.size(); ++i)
		{
			pcl::concatenatePointCloud(merged_cloud, clouds[i], merged_cloud);
			clouds_modified[i] = false;
		}
	
		point_cloud_publisher_.publish(merged_cloud);

		Eigen::MatrixXf points;
		getPointCloudAsEigen(merged_cloud,points);

		pointcloud_to_laserscan(points, &merged_cloud);
	}
}

void LaserscanMerger::pointcloud_to_laserscan(Eigen::MatrixXf points, pcl::PCLPointCloud2 *merged_cloud)
{
	sensor_msgs::LaserScanPtr output(new sensor_msgs::LaserScan());
	output->header = pcl_conversions::fromPCL(merged_cloud->header);
	output->header.frame_id = destination_frame.c_str();
	output->header.stamp = ros::Time::now();  //fixes #265
	output->angle_min = this->angle_min;
	output->angle_max = this->angle_max;
	output->angle_increment = this->angle_increment;
	output->time_increment = this->time_increment;
	output->scan_time = this->scan_time;
	output->range_min = this->range_min;
	output->range_max = this->range_max;

	uint32_t ranges_size = std::ceil((output->angle_max - output->angle_min) / output->angle_increment);
	output->ranges.assign(ranges_size, output->range_max + 1.0);

	for(int i=0; i<points.cols(); i++)
	{
		const float &x = points(0,i);
		const float &y = points(1,i);
		const float &z = points(2,i);

		if ( std::isnan(x) || std::isnan(y) || std::isnan(z) )
		{
			ROS_DEBUG("rejected for nan in point(%f, %f, %f)\n", x, y, z);
			continue;
		}

		double range_sq = y*y+x*x;
		double range_min_sq_ = output->range_min * output->range_min;
		if (range_sq < range_min_sq_) {
			ROS_DEBUG("rejected for range %f below minimum value %f. Point: (%f, %f, %f)", range_sq, range_min_sq_, x, y, z);
			continue;
		}

		double angle = atan2(y, x);
		if (angle < output->angle_min || angle > output->angle_max)
		{
			ROS_DEBUG("rejected for angle %f not in range (%f, %f)\n", angle, output->angle_min, output->angle_max);
			continue;
		}
		int index = (angle - output->angle_min) / output->angle_increment;


		if (output->ranges[index] * output->ranges[index] > range_sq)
			output->ranges[index] = sqrt(range_sq);
	}

	laser_scan_publisher_.publish(output);
}

void LaserscanMerger::deactivateBackScanCallback(const std_msgs::Bool::ConstPtr& detach){
    bool deactivate = detach->data;
    if(deactivate == true){
            scan_subscribers[1].shutdown();
            clouds_modified.resize(1);
            clouds.resize(1);
            this->tmp_angle_min = this->angle_min;
            this->tmp_angle_max = this->angle_max;
            this->angle_min = -2.09439992905;
            this->angle_max = 2.09439992905;
            ROS_INFO("Back scan is deactivated");
        }
        else if (deactivate == false){
            scan_subscribers[1] = node_.subscribe<sensor_msgs::LaserScan> (input_topics[1].c_str(), 1, boost::bind(&LaserscanMerger::scanCallback, this, _1, input_topics[1]));
            clouds_modified.resize(2);
            clouds.resize(2);
            this->angle_min = this->tmp_angle_min;
            this->angle_max = this->tmp_angle_max;
            ROS_INFO("Back scan is activated");
        }
}

int main(int argc, char** argv)
{
	ros::init(argc, argv, "laser_multi_merger");

    LaserscanMerger _laser_merger("/deactivateScan");

    dynamic_reconfigure::Server<laserscan_multi_mergerConfig> server;
    dynamic_reconfigure::Server<laserscan_multi_mergerConfig>::CallbackType f;

    f = boost::bind(&LaserscanMerger::reconfigureCallback,&_laser_merger, _1, _2);
	server.setCallback(f);

	ros::spin();

	return 0;
}
