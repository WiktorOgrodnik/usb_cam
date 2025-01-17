// Copyright 2014 Robert Bosch, LLC
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    * Redistributions of source code must retain the above copyright
//      notice, this list of conditions and the following disclaimer.
//
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in the
//      documentation and/or other materials provided with the distribution.
//
//    * Neither the name of the Robert Bosch, LLC nor the names of its
//      contributors may be used to endorse or promote products derived from
//      this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "usb_cam/usb_cam_node.hpp"
#include "usb_cam/utils.hpp"


namespace usb_cam
{

UsbCamNode::UsbCamNode(const rclcpp::NodeOptions & node_options)
: Node("usb_cam", node_options),
  m_camera(new usb_cam::UsbCam()),
  m_image_msg(new sensor_msgs::msg::Image()),
  m_image_publisher(std::make_shared<image_transport::CameraPublisher>(
      image_transport::create_camera_publisher(this, "image_raw",
      rclcpp::QoS {100}.get_rmw_qos_profile()))),
  m_camera_info_msg(new sensor_msgs::msg::CameraInfo()),
  m_service_capture(
    this->create_service<std_srvs::srv::SetBool>(
      "set_capture",
      std::bind(
        &UsbCamNode::service_capture,
        this,
        std::placeholders::_1,
        std::placeholders::_2,
        std::placeholders::_3)))
{
  // declare params
  this->declare_parameter("camera_name", "default_cam");
  this->declare_parameter("camera_info_url", "");
  this->declare_parameter("framerate", 30.0);
  this->declare_parameter("frame_id", "default_cam");
  this->declare_parameter("image_height", 480);
  this->declare_parameter("image_width", 640);
  this->declare_parameter("io_method", "mmap");
  this->declare_parameter("pixel_format", "yuyv");
  this->declare_parameter("video_device", "/dev/video0");
  this->declare_parameter("brightness", 50);  // 0-255, -1 "leave alone"
  this->declare_parameter("contrast", -1);    // 0-255, -1 "leave alone"
  this->declare_parameter("saturation", -1);  // 0-255, -1 "leave alone"
  this->declare_parameter("sharpness", -1);   // 0-255, -1 "leave alone"
  this->declare_parameter("gain", -1);        // 0-100?, -1 "leave alone"
  this->declare_parameter("auto_white_balance", true);
  this->declare_parameter("white_balance", 4000);
  this->declare_parameter("autoexposure", true);
  this->declare_parameter("exposure", 100);
  this->declare_parameter("autofocus", false);
  this->declare_parameter("focus", -1);  // 0-255, -1 "leave alone"

  get_ros_params();
  init();
  m_parameters_callback_handle = add_on_set_parameters_callback(
    std::bind(
      &UsbCamNode::parameters_callback, this,
      std::placeholders::_1));
}

UsbCamNode::~UsbCamNode()
{
  RCLCPP_WARN(this->get_logger(), "Shutting down");
  m_camera->shutdown();
}

void UsbCamNode::service_capture(
  const std::shared_ptr<rmw_request_id_t> request_header,
  const std::shared_ptr<std_srvs::srv::SetBool::Request> request,
  std::shared_ptr<std_srvs::srv::SetBool::Response> response)
{
  (void) request_header;
  if (request->data) {
    m_camera->start_capturing();
    response->message = "Start Capturing";
  } else {
    m_camera->stop_capturing();
    response->message = "Stop Capturing";
  }
}

void UsbCamNode::init()
{
  // configure the camera
  m_camera->configure();

  while (m_camera->parameters().frame_id == "") {
    RCLCPP_WARN_ONCE(
      this->get_logger(), "Required Parameters not set...waiting until they are set");
    get_ros_params();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }

  // load the camera info
  m_camera_info.reset(
    new camera_info_manager::CameraInfoManager(
      this, m_camera->parameters().camera_name, m_camera->parameters().camera_info_url));
  // check for default camera info
  if (!m_camera_info->isCalibrated()) {
    m_camera_info->setCameraName(m_camera->parameters().device_name);
    m_camera_info_msg->header.frame_id = m_camera->parameters().frame_id;
    m_camera_info_msg->width = m_camera->get_image_width();
    m_camera_info_msg->height = m_camera->get_image_height();
    m_camera_info->setCameraInfo(*m_camera_info_msg);
  }

  m_image_msg->header.frame_id = m_camera->parameters().frame_id;
  RCLCPP_INFO(
    this->get_logger(), "Starting '%s' (%s) at %dx%d via %s (%s) at %i FPS",
    m_camera->parameters().camera_name.c_str(), m_camera->parameters().device_name.c_str(),
    m_camera->parameters().image_width, m_camera->parameters().image_height,
    m_camera->parameters().io_method_name.c_str(),
    m_camera->parameters().pixel_format_name.c_str(), m_camera->parameters().framerate);

  RCLCPP_INFO(this->get_logger(), "This devices supproted formats:");
  for (auto fmt : m_camera->supported_formats()) {
    RCLCPP_INFO(
      this->get_logger(),
      "\t%s: %d x %d (%d Hz)",
      fmt.format.description,
      fmt.v4l2_fmt.width,
      fmt.v4l2_fmt.height,
      fmt.v4l2_fmt.discrete.denominator / fmt.v4l2_fmt.discrete.numerator);
  }

  m_camera->set_v4l2_params();

  // start the camera
  m_camera->start();

  // TODO(lucasw) should this check a little faster than expected frame rate?
  // TODO(lucasw) how to do small than ms, or fractional ms- std::chrono::nanoseconds?
  const int period_ms = 1000.0 / m_camera->parameters().framerate;
  m_timer = this->create_wall_timer(
    std::chrono::milliseconds(static_cast<int64_t>(period_ms)),
    std::bind(&UsbCamNode::update, this));
  RCLCPP_INFO_STREAM(this->get_logger(), "Timer triggering every " << period_ms << " ms");
}

void UsbCamNode::get_ros_params()
{
  auto parameters_client = std::make_shared<rclcpp::SyncParametersClient>(this);
  m_ros_parameters = parameters_client->get_parameters(
    {
      "camera_name", "camera_info_url", "frame_id", "framerate", "image_height", "image_width",
      "io_method", "pixel_format", "video_device", "brightness", "contrast",
      "saturation", "sharpness", "gain", "auto_white_balance", "white_balance", "autoexposure",
      "exposure", "autofocus", "focus"
    }
  );

  assign_ros_params(m_ros_parameters);
}

void UsbCamNode::assign_ros_params(const std::vector<rclcpp::Parameter> & parameters)
{
  usb_cam::parameters_t new_parameters{m_camera->parameters()};
  for (auto & parameter : parameters) {
    if (parameter.get_name() == "camera_name") {
      RCLCPP_INFO(this->get_logger(), "camera_name value: %s", parameter.value_to_string().c_str());
      new_parameters.camera_name = parameter.value_to_string();
    } else if (parameter.get_name() == "camera_info_url") {
      new_parameters.camera_info_url = parameter.value_to_string();
    } else if (parameter.get_name() == "frame_id") {
      new_parameters.frame_id = parameter.value_to_string();
    } else if (parameter.get_name() == "framerate") {
      RCLCPP_WARN(this->get_logger(), "framerate: %f", parameter.as_double());
      new_parameters.framerate = parameter.as_double();
    } else if (parameter.get_name() == "image_height") {
      new_parameters.image_height = parameter.as_int();
    } else if (parameter.get_name() == "image_width") {
      new_parameters.image_width = parameter.as_int();
    } else if (parameter.get_name() == "io_method") {
      new_parameters.io_method_name = parameter.value_to_string();
    } else if (parameter.get_name() == "pixel_format") {
      new_parameters.pixel_format_name = parameter.value_to_string();
    } else if (parameter.get_name() == "video_device") {
      new_parameters.device_name = parameter.value_to_string();
    } else if (parameter.get_name() == "brightness") {
      new_parameters.brightness = parameter.as_int();
    } else if (parameter.get_name() == "contrast") {
      new_parameters.contrast = parameter.as_int();
    } else if (parameter.get_name() == "saturation") {
      new_parameters.saturation = parameter.as_int();
    } else if (parameter.get_name() == "sharpness") {
      new_parameters.sharpness = parameter.as_int();
    } else if (parameter.get_name() == "gain") {
      new_parameters.gain = parameter.as_int();
    } else if (parameter.get_name() == "auto_white_balance") {
      new_parameters.auto_white_balance = parameter.as_bool();
    } else if (parameter.get_name() == "white_balance") {
      new_parameters.white_balance = parameter.as_int();
    } else if (parameter.get_name() == "autoexposure") {
      new_parameters.autoexposure = parameter.as_bool();
    } else if (parameter.get_name() == "exposure") {
      new_parameters.exposure = parameter.as_int();
    } else if (parameter.get_name() == "autofocus") {
      new_parameters.autofocus = parameter.as_bool();
    } else if (parameter.get_name() == "focus") {
      new_parameters.focus = parameter.as_int();
    } else {
      RCLCPP_WARN(this->get_logger(), "Invalid parameter name: %s", parameter.get_name().c_str());
    }
  }

  m_camera->assign_parameters(new_parameters);
}

bool UsbCamNode::take_and_send_image()
{
  // Only resize if required
  if (m_image_msg->data.size() != m_camera->get_image_size()) {
    m_image_msg->width = m_camera->get_image_width();
    m_image_msg->height = m_camera->get_image_height();
    m_image_msg->encoding = m_camera->get_pixel_format()->ros();
    m_image_msg->step = m_camera->get_image_step();
    if (m_image_msg->step == 0) {
      // Some formats don't have a linesize specified by v4l2
      // Fall back to manually calculating it step = size / height
      m_image_msg->step = m_camera->get_image_size() / m_image_msg->height;
    }
    m_image_msg->data.resize(m_camera->get_image_size());
  }

  // grab the image, pass image msg buffer to fill
  m_camera->get_image(reinterpret_cast<char *>(&m_image_msg->data[0]));

  auto stamp = m_camera->get_image_timestamp();
  m_image_msg->header.stamp.sec = stamp.tv_sec;
  m_image_msg->header.stamp.nanosec = stamp.tv_nsec;

  *m_camera_info_msg = m_camera_info->getCameraInfo();
  m_camera_info_msg->header = m_image_msg->header;
  m_image_publisher->publish(*m_image_msg, *m_camera_info_msg);
  return true;
}

rcl_interfaces::msg::SetParametersResult UsbCamNode::parameters_callback(
  const std::vector<rclcpp::Parameter> & parameters)
{
  RCLCPP_DEBUG(
    this->get_logger(),
    "Setting parameters for %s", m_camera->parameters().camera_name.c_str());
  m_timer->reset();
  assign_ros_params(parameters);
  m_camera->set_v4l2_params();
  rcl_interfaces::msg::SetParametersResult result;
  result.successful = true;
  result.reason = "success";
  return result;
}

void UsbCamNode::update()
{
  if (m_camera->is_capturing()) {
    // If the camera exposure longer higher than the framerate period
    // then that caps the framerate.
    // auto t0 = now();
    if (!take_and_send_image()) {
      RCLCPP_WARN_ONCE(this->get_logger(), "USB camera did not respond in time.");
    }
    // auto diff = now() - t0;
    // INFO(diff.nanoseconds() / 1e6 << " " << int(t0.nanoseconds() / 1e9));
  }
}
}  // namespace usb_cam


#include "rclcpp_components/register_node_macro.hpp"
RCLCPP_COMPONENTS_REGISTER_NODE(usb_cam::UsbCamNode)
