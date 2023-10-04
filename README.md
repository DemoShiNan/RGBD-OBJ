# RGBD-OBJ

**Associated Publication:**
<!-- - **OA-SLAM: Leveraging Objects for Camera Relocalization in Visual SLAM.** Matthieu Zins, Gilles Simon, Marie-Odile Berger, *IEEE International Symposium on Mixed and Augmented Reality (ISMAR 2022).* [Paper](https://arxiv.org/abs/2209.08338) | [Video](https://youtu.be/L1HEL4kLJ3g) | [AR Demo](https://youtu.be/PXG_6LkbtgY) -->

- **Integrate depth information to enhance the robustness of object level SLAM.** Shinan Huang, Jianyong Chen,  (CGI 2023).


<!-- <p align="center">
<a href="https://youtu.be/PXG_6LkbtgY"> <img src="Doc/OA-SLAM_AR_demo.png" width="640"> </a>
</p>



<p align="center">
<img src="Doc/OA-SLAM.png" width="640">
</p>



<p align="center">
<a href="https://youtu.be/L1HEL4kLJ3g"> <img src="Doc/OA-SLAM_video.png" width="640"> </a>
</p>
 -->

# Installation

## Dependencies

- [Pangolin](https://github.com/stevenlovegrove/Pangolin) for visualization and user interface.
- [OpenCV](http://opencv.org) to manipulate images and features. Version >= 4 is required for live object detection. (tested with 4.6)
- [Eigen3](https://gitlab.com/libeigen/eigen) for linear algebra.
- [Dlib](https://github.com/davisking/dlib) for the Hungarian algorithm.
- [Protocol Buffers](https://github.com/protocolbuffers/protobuf) for Osmap.

Included in the *Thirdparty* folder:
- [DBoW2](https://github.com/dorian3d/DBoW2) for place recognition.
- [g2o](https://github.com/RainerKuemmerle/g2o) for graph-based non-linear optimization.
- [JSON](https://github.com/nlohmann/json) for I/O json files.
- [Osmap](https://github.com/AlejandroSilvestri/osmap) for map saving/loading. Modified version to handle objects.


# Data

Some test sequences are available in the [TUM-RGB dataset](https://vision.in.tum.de/data/datasets/rgbd-dataset).
In particular, we use the *fr2/desk* scene.

Our system takes object detections as input. We provide detections in JSON files for the sample data and for *fr2/desk* in the *Data* folder. They can be obtained from any object detector.
We used an off-the-shelf version of [YOLOv5](https://github.com/ultralytics/yolov5) for our custom scene and a fine-tuned version for *fr2/desk*.

The parameters for *fr2/desk* are in *Cameras/TUM2_rgbd_plane.yaml*.

# SLAM mode

SLAM includes a map viewer, an image viewer and a AR viewer.

Usage:
```
 ./oa-slam
      vocabulary_file
      camera_file
      path_to_image_sequence (.txt file listing the images or a folder with rgb.txt or 'webcam_id')
      detections_file (.json file with detections or .onnx yolov5 weights)
      categories_to_ignore_file (file containing the categories to ignore (one category_id per line))
      relocalization_mode ('points', 'objects' or 'points+objects')
      output_name  
      path_to_association
```

# License

OA-SLAM is released under a GPLv3 license. The code is based on [ORB-SLAM2](https://github.com/raulmur/ORB_SLAM2).



