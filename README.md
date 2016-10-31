# Caffe
[![Build Status](https://travis-ci.org/BVLC/caffe.svg?branch=master)](https://travis-ci.org/BVLC/caffe)
[![License](https://img.shields.io/badge/license-BSD-blue.svg)](LICENSE)

This is a merge of inte caffe at https://github.com/intel/caffe and SSD (Single Shot MultiBox Detector) at https://github.com/weiliu89/caffe/tree/ssd

## Note
It only targets for inference/scoring, training part is not merged.

## Building
Build procedure is the same as on bvlc-caffe-master branch. Both Make and CMake can be used.
When OpenMP is available will be used automatically.

## Running
1) Prepare/Download the trained model, for example, download from http://www.cs.unc.edu/%7Ewliu/projects/SSD/models_VGGNet_VOC0712_SSD_300x300.tar.gz

2) Run the SSD example:
./build/examples/ssd/ssd_detect.bin ./deploy.prototxt models/VGGNet/VOC0712/SSD_300x300/VGG_VOC0712_SSD_300x300_iter_60000.caffemodel images.txt 

Note: Please use the modified deploy.prototxt to get much better performance.
