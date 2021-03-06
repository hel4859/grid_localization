#include "dead_reckoning.h"
#include"stdio.h"
#include"math.h"

dead_reckoning::dead_reckoning()
{
    ros::NodeHandle pnh("~");			//定义私有节点句柄，用于传递参数  
    imu_sub_ = nh_.subscribe("/imu_torso/xsens/imu",1,&dead_reckoning::xsens_callback,this);   //imu
    pulse_sub = nh_.subscribe("/CanDecode_1/SpeedMilSteer",1,&dead_reckoning::odo_callback,this);  //odometry//// /RecvCAN_1/SpeedMilSteer  /CanDecode_1/SpeedMilSteer
    rtkgps_sub_ = nh_.subscribe("/fix",1,&dead_reckoning::rtk_callback,this);  //GPS
    initial_pose_sub = nh_.subscribe("/init_pose",1,&dead_reckoning::initPose_callback, this);

    //GPS & XY coordinate exchange
    gpsOrigin.latlon[0] = latMean;
    gpsOrigin.latlon[1] = lonMean;
    scale = cos(latMean*(Pi/180.0));// the latmean position
    //gpsOrigin.mercatorProj(scale);

    //EKF initialization
    XSENS_Diff= 1.36e-3;//1.36e-3//1.76e-6;
    speedCov=0.02;//0.05//0.1
    steerCov=0;
    GPSOBV_RO = Lu_Matrix(2,2);
    X0 = Lu_Matrix(3,1);
    P0 = Lu_Matrix(3,3);
    P0(0,0)=1,P0(1,1)=1,P0(2,2)=0.1;
    GPSINS_EKF.init(3,X0,P0);//
    //glResult_EKF.init(3,X0,P0);//grid_localization结果的EKF的 初始化


    lat=lon=0;
    uTcTime=0;
    initGpsCounter=0;  //counter for gps-ekf initiallizaiton
    angularVelocity=0;
    //CameraTrigger=-1;LastTrigger=-1;
    //LoopTimes=0;
    speed_time=0;
    firstGps_x=firstGps_y=0;
    gpsFirstGet = false;
    gpsCanGetSecond = false;

    //dead_reckoning related values
    yaw_angle = 0.0;//弧度
    roll_angle = 0.0;
    pitch_angle =0.0;
    yaw_angle_veolcity_sum =0;
    yaw_velocity_last = 0;
    pulse_sum =0;
    mileage_sum = 0;
    robot_pose_gps = CPose2D(0,0,0);
    robot_pose_inc = CPose2D(0,0,0); //CPose2D(-21729,66191,-0.9);
    poseIncr2D = CPose2D(0, 0, 0);
    tictacDR.Tic();
    ticPre = tictacDR.Tac();
    timePrevious = 0;
    ekf_result_ready = false;
    //firstMovement = true;
    firstGpsUpdate = false;

    firstTime = true;
    tictacDR.Tic();

    mileage_last = 0;
    mileage_present = 0;
    mileage_incr = 0;

    //for gps initiallizationf
    gpsSum_x = 0;
    gpsSum_y = 0;
    n280Orientation = 0;

    imu_orientation_yaw = 0;
    imu_orientation_pitch = 0;
    imu_orientation_roll = 0;
    ekf_init_flag = true;
}

void dead_reckoning::initPose_callback(const std_msgs::Float64MultiArray& msg)
{

}

void dead_reckoning::rtk_callback(const sensor_msgs::NavSatFix &msg)
{  
    gps_data = msg;
    //latitude or longitude to XY
    gpsPos.latlon[0] = gps_data.latitude;//gps纬度数据
    gpsPos.latlon[1] = gps_data.longitude;//gps经度数据
    //gpsPos.mercatorProj(scale, gpsOrigin);
    gpsPos.gps2meter(gpsPos.Ellipse_L0,
                     gpsPos.GPS_OriginX,
                     gpsPos.GPS_OriginY,
                     gpsPos.GPS_OffsetX,
                     gpsPos.GPS_OffsetY);

    poseGps2D = CPose2D(gpsPos.coordinate[0], gpsPos.coordinate[1],0);

    if(ekf_init_flag){
        ekf_init_flag = false;

        Lu_Matrix X0(3,1);
        Lu_Matrix P0(3,3);
        X0(0,0)=poseGps2D.x();
        X0(1,0)=poseGps2D.y();
        X0(2,0)=imu_orientation_yaw;
        if(X0(2,0)<0) X0(2,0) += 2*PI;
        P0(0,0)=1,P0(1,1)=1,P0(2,2)=0.1;
        GPSINS_EKF.init(3,X0,P0);
        glResult_EKF.init(3,X0,P0);
    }
    else {
        if(true)
//        {
//            GPSOBV_RO(0,0) = 0.001;
//            GPSOBV_RO(1,1) = 0.001;
//            GPSOBV_RO*=GPSOBV_RO;
//            if (pulse_sum>0.001) GPSINS_EKF.Obv_GPS_update(poseGps2D.x(),poseGps2D.y(),GPSOBV_RO);
//            if (!firstGpsUpdate) firstGpsUpdate = true;
//        }
        if(gps_data.position_covariance[0] > 0.00001)
        {
            if(gps_data.position_covariance[0]<1.4)
            {
                GPSOBV_RO(0,0) = gps_data.position_covariance[0] * 0.045;//0.05    //0.02//按照gps实际效果设定
                GPSOBV_RO(1,1) = gps_data.position_covariance[0] * 0.045;//0.05    //0.02//按照gps实际效果设定
            }
            else
            {
                GPSOBV_RO(0,0) = gps_data.position_covariance[0];//0.05    //0.02//按照gps实际效果设定
                GPSOBV_RO(1,1) = gps_data.position_covariance[0];//0.05    //0.02//按照gps实际效果设定
            }

            GPSOBV_RO*=GPSOBV_RO;
            //rtkGps_x = poseGps2D.x();
            //rtkGps_y = poseGps2D.y();
            if (pulse_sum>0.001) GPSINS_EKF.Obv_GPS_update(poseGps2D.x(),poseGps2D.y(),GPSOBV_RO);
            if (!firstGpsUpdate) firstGpsUpdate = true;
        }
    }
}

void dead_reckoning::xsens_callback(const sensor_msgs::Imu &msg)
{
    //ROS_INFO("i receive the IMU mesage!");
    timePresent = ros::Time::now().toSec();
    static double time_present =ros::Time::now().toSec();
    static double time_previous =ros::Time::now().toSec();
    static double time_delta=0.0;
    imu_data = msg;
    {
        time_present = ros::Time::now().toSec();
        time_delta = time_present - time_previous;
        time_previous = time_present;
        //ROS_INFO("%.10f",time_present);
    }

    imu_orientation_yaw = atan2(2*(imu_data.orientation.w * imu_data.orientation.y + imu_data.orientation.z * imu_data.orientation.x),
                          1-2*(imu_data.orientation.x * imu_data.orientation.x + imu_data.orientation.y * imu_data.orientation.y));
    imu_orientation_pitch = 0;
    imu_orientation_roll = 0;
    //ROS_INFO("yaw %.3f",imu_orien_yaw);

    angularVelocity = imu_data.angular_velocity.z - gyro_z_offset;
    yaw_angle_veolcity_sum += ((angularVelocity + yaw_velocity_last) / 2) * time_delta; //积分，单位是弧度/s
    //yaw_angle_veolcity_sum = angularVelocity * time_delta; //积分，单位是弧度/s
    yaw_velocity_last = angularVelocity;
}


void dead_reckoning::odo_callback(const can_msgs::SpeedMilSteer &msg )
{
    velocity = msg.Speed;
    mileage_present = msg.Mileage;
    if(!mileage_last)
        mileage_last = mileage_present;
    //velocity could be positive/negative
    if(velocity>=0)
        mileage_incr += (mileage_present - mileage_last);
    else
        mileage_incr -= (mileage_present - mileage_last);
    mileage_last = mileage_present;

    pulse_sum = mileage_incr;
    //if(!firstMovement)
    //{
    //    //if(pulse_temp!=0) firstMovement = true; //after driving
    //    if(pulse_sum>2) firstMovement = true; //after driving 2 meters
    //}
    //ROS_INFO("%i\t%i",pulse_data.data[0],pulse_data.data[1]);
}

void dead_reckoning::calculate_pose_inc()
{
    static double time_present =ros::Time::now().toSec();
    static double time_previous =ros::Time::now().toSec();
    static double time_delta=0.0;
    static int badCounter = 0;
    {
        time_present = ros::Time::now().toSec();
        time_delta = time_present - time_previous;
        time_previous = time_present;
    }

//    if(pulse_sum==0 && velocity>1){
//        badCounter++;
//        double odoIncr = velocity/3.6 * time_delta;
//        robot_pose_inc.x(odoIncr * cos(yaw_angle_veolcity_sum /2));
//        robot_pose_inc.y(odoIncr * sin(yaw_angle_veolcity_sum /2));
//        robot_pose_inc.phi(yaw_angle_veolcity_sum);
//        ROS_INFO("Odometry Error %i", badCounter);//. %.3f %.3f", robot_pose_inc.x(),robot_pose_inc.y());
//    }
//    else{
        robot_pose_inc.x(pulse_sum * cos(yaw_angle_veolcity_sum /2));
        robot_pose_inc.y(pulse_sum * sin(yaw_angle_veolcity_sum /2));
        robot_pose_inc.phi(yaw_angle_veolcity_sum);
//        badCounter = 0;
        //ROS_INFO("%.3f %.3f", robot_pose_inc.x(),robot_pose_inc.y());  
    //}
    //clear_sum();
    //printf("new pose is %f,  %f  %f\n",robot_pose_inc.x(),robot_pose_inc.y(),robot_pose_inc.phi());
}

//仅用GPS的情况，用于专门查看GPS数据结果，平时不用
void dead_reckoning::get_poseGps()
{
    //if FisrtTime
    if(firstTime){
        firstTime =false;
        poseGps2D_last = poseGps2D;
    }
    if(pulse_sum!=0){
        double dx, dy, phi;
        dx = poseGps2D.x()-poseGps2D_last.x();
        dy = poseGps2D.y()-poseGps2D_last.y();  
        phi = atan2(dy,dx);

        robot_pose_gps.x(poseGps2D.x());
        robot_pose_gps.y(poseGps2D.y());
        robot_pose_gps.phi(phi);
      
        poseGps2D_last = poseGps2D;
        clear_sum();//only used in this function
    }
}

void dead_reckoning::get_poseGps_ekf()
{
    static double time_present =ros::Time::now().toSec();
    static double time_previous =ros::Time::now().toSec();
    static double time_delta=0.0;
    {
        time_present = ros::Time::now().toSec();
        time_delta = time_present - time_previous;
        time_previous = time_present;
    }

    speed = pulse_sum/time_delta;
    speed_time = time_present;

    //if(firstGpsUpdate){
    if(firstGpsUpdate && pulse_sum>0.001){
        GPSINS_EKF.State_Predict(speed,angularVelocity,speed_time,speedCov,XSENS_Diff, pulse_sum, yaw_angle_veolcity_sum);
        //在ICP开始后才对结果进行滤波
        if(!notDoingIcpYet)
            glResult_EKF.State_Predict(speed,angularVelocity,speed_time,speedCov,XSENS_Diff, pulse_sum, yaw_angle_veolcity_sum);
    }

    Lu_Matrix state(3);
    state=GPSINS_EKF.getState();
    robot_poseGps_ekf.x(state(0,0));
    robot_poseGps_ekf.y(state(1,0));
    robot_poseGps_ekf.phi(state(2,0));
/*  //给robot_poseResult_ekf赋值无太大意义
    Lu_Matrix state(3);
    state=glResult_EKF.getState();
    robot_poseResult_ekf.x(state(0));
    robot_poseResult_ekf.y(state(1));
    robot_poseResult_ekf.phi(state(2));*/
    clear_sum();
}

void dead_reckoning::xy2latlon(double x,double y, double &lat, double &lon)
{
    gpsOut.coordinate[1] = y/1000;
    gpsOut.coordinate[0] = x/1000;
    gpsOut.mercatordeProj(scale, gpsOrigin);
    lat = gpsOut.latlon[0];
    lon = gpsOut.latlon[1];
}

//
void dead_reckoning::clear_sum()
{
    mileage_incr = 0;
    pulse_sum =0.0;
    yaw_angle_veolcity_sum =0.0;
}

