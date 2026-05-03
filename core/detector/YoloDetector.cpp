#include "YoloDetector.h"
#include <algorithm>
#include <vector>
#include <string>
#include <opencv2/opencv.hpp>
#include <net.h>

namespace detector {

YoloDetector::YoloDetector() {
    // Initialize with default values
    conf_threshold_ = 0.3f;
    nms_threshold_ = 0.3f;
    input_width_ = 640;
    input_height_ = 640;
    model_loaded_ = false;
}

YoloDetector::~YoloDetector() {
    // Clean up
    if (model_loaded_) {
        yolo_net_.clear();
    }
}

bool YoloDetector::loadModel(const std::string& param_path, const std::string& bin_path) {
    if (model_loaded_) {
        yolo_net_.clear();
    }

    // Load model
    int ret = yolo_net_.load_param(param_path.c_str());
    if (ret != 0) {
        return false;
    }

    ret = yolo_net_.load_model(bin_path.c_str());
    if (ret != 0) {
        return false;
    }

    model_loaded_ = true;
    return true;
}

std::vector<Detection> YoloDetector::detect(const cv::Mat& image) {
    try {
        std::vector<Detection> detections;
        if (!model_loaded_) {
            std::cerr << "YoloDetector::detect: Model not loaded" << std::endl;
            return detections;
        }

        std::cerr << "YoloDetector::detect: Starting detection..." << std::endl;
        std::cerr << "YoloDetector::detect: Input image size - width=" << image.cols << ", height=" << image.rows << ", channels=" << image.channels() << std::endl;

        // Preprocess image
        std::cerr << "YoloDetector::detect: Preprocessing image..." << std::endl;
        ncnn::Mat in = preprocess(image);
        std::cerr << "YoloDetector::detect: Preprocessed image size - w=" << in.w << ", h=" << in.h << ", c=" << in.c << std::endl;

        std::cerr << "YoloDetector::detect: Creating extractor..." << std::endl;
        ncnn::Extractor ex = yolo_net_.create_extractor();
        
        // Enable light mode for faster inference
        ex.set_light_mode(true);
        
        std::cerr << "YoloDetector::detect: Setting input..." << std::endl;
        ex.input("in0", in);

        // Run inference
        std::cerr << "YoloDetector::detect: Running inference..." << std::endl;
        ncnn::Mat out;
        int ret = ex.extract("out0", out);
        std::cerr << "YoloDetector::detect: Inference completed, ret=" << ret << std::endl;
        
        if (ret != 0) {
            std::cerr << "YoloDetector::detect: Failed to extract output, ret=" << ret << std::endl;
            return detections;
        }

        std::cerr << "YoloDetector::detect: Output shape - w=" << out.w << ", h=" << out.h << ", c=" << out.c << std::endl;
        std::cerr << "YoloDetector::detect: Output total elements - " << out.total() << std::endl;

        // Process output
    
    // Check if output is empty
    if (out.w <= 0 || out.data == nullptr) {
        std::cerr << "YoloDetector::detect: Invalid output - w=" << out.w << ", data=" << out.data << std::endl;
        return detections;
    }
    
    // YOLOv5 output parsing based on Python script
    // Model output format: [8, 25200] for NCNN, but each detection is 8 values
    // The output is stored as [out.w=8, out.h=25200], so we need to process it correctly
    std::cerr << "YoloDetector::detect: Processing YOLOv5 output based on Python script..." << std::endl;
    
    // Get output dimensions
    int output_channels = out.w; // Should be 8 for this model
    int total_detections = out.h; // Should be 25200 for YOLOv5
    
    std::cerr << "YoloDetector::detect: Output channels=" << output_channels << ", total detections=" << total_detections << std::endl;
    
    float* data = (float*)out.data;
    
    // Image dimensions for scaling
    int img_width = image.cols;
    int img_height = image.rows;
    
    // Calculate scale factors: model input is 640x640, need to scale to original image size
    float scale_w = static_cast<float>(img_width) / input_width_;
    float scale_h = static_cast<float>(img_height) / input_height_;
    
    std::cerr << "YoloDetector::detect: Scale factors - scale_w=" << scale_w << ", scale_h=" << scale_h << std::endl;
    
    // Process each detection
    for (int i = 0; i < total_detections; i++) {
        try {
            // Calculate offset in the output data
            // For NCNN output [8, 25200], each detection is stored in consecutive memory
            int offset = i * output_channels;
            
            // Extract detection data - values are relative to 640x640 input
            float x_center = data[offset];       // Center x in 640x640 space
            float y_center = data[offset + 1];   // Center y in 640x640 space
            float box_width = data[offset + 2];  // Width in 640x640 space
            float box_height = data[offset + 3]; // Height in 640x640 space
            float obj_conf = data[offset + 4];   // Object confidence
            
            // Get class confidences (indices 5-7 for 3 classes)
            float cls_conf1 = data[offset + 5];
            float cls_conf2 = data[offset + 6];
            float cls_conf3 = data[offset + 7];
            
            // Find max class confidence and class ID
            float max_cls_conf = cls_conf1;
            int class_id = 0;
            
            if (cls_conf2 > max_cls_conf) {
                max_cls_conf = cls_conf2;
                class_id = 1;
            }
            
            if (cls_conf3 > max_cls_conf) {
                max_cls_conf = cls_conf3;
                class_id = 2;
            }
            
            // Calculate final confidence: obj_conf * max_class_conf
            float final_conf = obj_conf * max_cls_conf;
            
            // Skip low confidence detections
            if (final_conf < conf_threshold_) {
                continue;
            }
            
            // Convert coordinates from 640x640 space to original image space
            int x1 = static_cast<int>((x_center - box_width / 2) * scale_w);
            int y1 = static_cast<int>((y_center - box_height / 2) * scale_h);
            int x2 = static_cast<int>((x_center + box_width / 2) * scale_w);
            int y2 = static_cast<int>((y_center + box_height / 2) * scale_h);
            
            // Clip to image bounds
            x1 = std::max<int>(0, x1);
            y1 = std::max<int>(0, y1);
            x2 = std::min<int>(img_width - 1, x2);
            y2 = std::min<int>(img_height - 1, y2);
            
            // Check if bounding box is valid
            int width = x2 - x1;
            int height = y2 - y1;
            if (width <= 0 || height <= 0) {
                continue;
            }
            
            // Filter out small bounding boxes
            int min_box_area = 100; // Minimum box area in pixels
            if (width * height < min_box_area) {
                continue;
            }
            
            Detection det;
            det.bounding_box = cv::Rect(x1, y1, width, height);
            det.confidence = final_conf;
            det.class_id = class_id;
            det.class_name = (class_id >= 0 && class_id < static_cast<int>(class_names_.size()))
                             ? class_names_[class_id] : "unknown";
            
            detections.push_back(det);
            
            // Log detection details
            std::cerr << "YoloDetector::detect: Detection " << i << ": "
                    << "Center(x,y)=[" << x_center << ", " << y_center << "], "
                    << "Size(w,h)=[" << box_width << ", " << box_height << "], "
                    << "ObjConf=" << obj_conf << ", ClsConf=" << max_cls_conf << ", FinalConf=" << final_conf << ", "
                    << "ClassID=" << class_id << ", "
                    << "Bounding box=[" << width << "x" << height 
                    << " from (" << x1 << ", " << y1 << ")]" << std::endl;
            
        } catch (const std::exception& e) {
            std::cerr << "YoloDetector::detect: Exception processing detection " << i << ": " << e.what() << std::endl;
            continue;
        } catch (...) {
            std::cerr << "YoloDetector::detect: Unknown exception processing detection " << i << std::endl;
            continue;
        }
    }

        // Apply NMS if we have detections
        if (!detections.empty()) {
            std::cerr << "YoloDetector::detect: Applying NMS on " << detections.size() << " detections..." << std::endl;
            detections = applyNMS(detections);
            std::cerr << "YoloDetector::detect: NMS completed, remaining detections=" << detections.size() << std::endl;
        } else {
            std::cerr << "YoloDetector::detect: No detections to apply NMS on" << std::endl;
        }
        
        return detections;
    } catch (const std::exception& e) {
        std::cerr << "YoloDetector::detect: Exception in detect function: " << e.what() << std::endl;
        return std::vector<Detection>();
    } catch (...) {
        std::cerr << "YoloDetector::detect: Unknown exception in detect function" << std::endl;
        return std::vector<Detection>();
    }
}

cv::Mat YoloDetector::drawDetections(const cv::Mat& image, const std::vector<Detection>& detections) {
    cv::Mat result = image.clone();

    for (const auto& detection : detections) {
        // Draw bounding box
        cv::rectangle(result, detection.bounding_box, cv::Scalar(0, 255, 0), 2);

        // Draw label
        std::string label = detection.class_name + " " + std::to_string(detection.confidence);
        int baseLine;
        cv::Size label_size = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);
        cv::Point label_org(detection.bounding_box.x, detection.bounding_box.y - label_size.height - baseLine);
        cv::rectangle(result, label_org, cv::Point(detection.bounding_box.x + label_size.width, detection.bounding_box.y), cv::Scalar(0, 255, 0), -1);
        cv::putText(result, label, cv::Point(detection.bounding_box.x, detection.bounding_box.y - baseLine), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0), 1);
    }

    return result;
}

void YoloDetector::setConfidenceThreshold(float threshold) {
    conf_threshold_ = threshold;
}

void YoloDetector::setNmsThreshold(float threshold) {
    nms_threshold_ = threshold;
}

float YoloDetector::getConfidenceThreshold() const {
    return conf_threshold_;
}

float YoloDetector::getNmsThreshold() const {
    return nms_threshold_;
}

void YoloDetector::setInputSize(int width, int height) {
    input_width_ = width;
    input_height_ = height;
}

int YoloDetector::getInputWidth() const {
    return input_width_;
}

int YoloDetector::getInputHeight() const {
    return input_height_;
}

ncnn::Mat YoloDetector::preprocess(const cv::Mat& image) {
    // Resize image
    cv::Mat resized;
    cv::resize(image, resized, cv::Size(input_width_, input_height_));

    // Convert to RGB
    cv::Mat rgb;
    if (resized.channels() == 3) {
        cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);
    } else {
        cv::cvtColor(resized, rgb, cv::COLOR_GRAY2RGB);
    }

    // Convert to ncnn::Mat
    ncnn::Mat in = ncnn::Mat::from_pixels(rgb.data, ncnn::Mat::PIXEL_RGB, input_width_, input_height_);

    // Normalize
    const float mean_vals[3] = {0.0f, 0.0f, 0.0f};
    const float norm_vals[3] = {1/255.0f, 1/255.0f, 1/255.0f};
    in.substract_mean_normalize(mean_vals, norm_vals);

    return in;
}

std::vector<Detection> YoloDetector::applyNMS(std::vector<Detection>& detections) {
    std::vector<Detection> result;
    if (detections.empty()) {
        return result;
    }

    // Sort detections by confidence
    std::sort(detections.begin(), detections.end(), 
             [](const Detection& a, const Detection& b) {
                 return a.confidence > b.confidence;
             });

    std::vector<bool> suppressed(detections.size(), false);

    for (size_t i = 0; i < detections.size(); ++i) {
        if (suppressed[i]) continue;
        result.push_back(detections[i]);

        for (size_t j = i + 1; j < detections.size(); ++j) {
            if (suppressed[j]) continue;

            // Calculate IoU
            float x1 = std::max<float>(detections[i].bounding_box.x, detections[j].bounding_box.x);
            float y1 = std::max<float>(detections[i].bounding_box.y, detections[j].bounding_box.y);
            float x2 = std::min<float>(detections[i].bounding_box.x + detections[i].bounding_box.width, 
                              detections[j].bounding_box.x + detections[j].bounding_box.width);
            float y2 = std::min<float>(detections[i].bounding_box.y + detections[i].bounding_box.height, 
                              detections[j].bounding_box.y + detections[j].bounding_box.height);

            float intersection_area = std::max<float>(0.0f, x2 - x1) * std::max<float>(0.0f, y2 - y1);
            float union_area = detections[i].bounding_box.area() + detections[j].bounding_box.area() - intersection_area;
            float iou = intersection_area / union_area;

            if (iou > nms_threshold_) {
                suppressed[j] = true;
            }
        }
    }

    return result;
}



} // namespace detector
