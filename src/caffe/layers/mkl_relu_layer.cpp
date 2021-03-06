/*
All modification made by Intel Corporation: © 2016 Intel Corporation

All contributions by the University of California:
Copyright (c) 2014, 2015, The Regents of the University of California (Regents)
All rights reserved.

All other contributions:
Copyright (c) 2014, 2015, the respective contributors
All rights reserved.
For the list of contributors go to https://github.com/BVLC/caffe/blob/master/CONTRIBUTORS.md


Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of Intel Corporation nor the names of its contributors
      may be used to endorse or promote products derived from this software
      without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifdef MKL2017_SUPPORTED
#include <algorithm>
#include <vector>

#include "caffe/layers/mkl_layers.hpp"

namespace caffe {
template <typename Dtype>
MKLReLULayer<Dtype>::~MKLReLULayer() {
    dnnDelete<Dtype>(reluFwd_);
    dnnDelete<Dtype>(reluBwd_);
}

template <typename Dtype>
void MKLReLULayer<Dtype>::LayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
//  CHECK_EQ(top[0]->shape(), bottom[0]->shape());
  size_t dim = bottom[0]->shape().size();
  size_t sizes[dim], strides[dim];
  for (size_t d = 0; d < dim; ++d) {
      sizes[d] = bottom[0]->shape()[dim - 1 - d];
      strides[d] = (d == 0) ? 1 : strides[d-1]*sizes[d-1];
  }

  // Names are for debugging only
  fwd_bottom_data_->name = "fwd_bottom_data   @ " + this->layer_param_.name();
  fwd_top_data_->name =    "fwd_top_data      @ " + this->layer_param_.name();
  bwd_bottom_diff_->name = "bwd_bottom_diff   @ " + this->layer_param_.name();
  bwd_top_diff_->name =    "bwd_top_diff      @ " + this->layer_param_.name();

  fwd_bottom_data_->create_user_layout(dim, sizes, strides);
  fwd_top_data_   ->create_user_layout(dim, sizes, strides);
  bwd_bottom_diff_->create_user_layout(dim, sizes, strides);
  bwd_top_diff_   ->create_user_layout(dim, sizes, strides);

  // "Lazy" allocation because here we don't know
  // what layout is used by neighbours.
  reluFwd_ = NULL;  // Will be allocated in a "lazy" way in first forward pass
  reluBwd_ = NULL;  // Will be allocated in a "lazy" way in first backward pass
}

template <typename Dtype>
void MKLReLULayer<Dtype>::Forward_cpu(const vector<Blob<Dtype>*>& bottom,
    const vector<Blob<Dtype>*>& top) {
  void* bottom_data =
    reinterpret_cast<void *>(const_cast<Dtype*>(bottom[0]->prv_data()));

  if (bottom_data) {
    if (reluFwd_ == NULL) {
      // first pass
      CHECK_EQ((bottom[0]->get_prv_data_descriptor())->get_descr_type(),
              PrvMemDescr::PRV_DESCR_MKL2017);
      shared_ptr<MKLData<Dtype> > mem_descr
        =  boost::static_pointer_cast<MKLData<Dtype> >
              (bottom[0]->get_prv_data_descriptor());
      CHECK(mem_descr != NULL);

      Dtype negative_slope = this->layer_param_.relu_param().negative_slope();
      dnnError_t e;
      e = dnnReLUCreateForward<Dtype>(&reluFwd_, NULL, mem_descr->layout_int,
              negative_slope);
      CHECK_EQ(e, E_SUCCESS);
      e = dnnReLUCreateBackward<Dtype>(&reluBwd_, NULL, mem_descr->layout_int,
              mem_descr->layout_int, negative_slope);
      CHECK_EQ(e, E_SUCCESS);

      DLOG(INFO) << "Using layout of " << mem_descr->name
              << " as input layout for " << this->layer_param_.name();
      // copy shared_ptr
      fwd_bottom_data_ = mem_descr;

      fwd_top_data_   ->create_internal_layout(reluFwd_, dnnResourceDst);
      bwd_top_diff_   ->create_internal_layout(reluFwd_, dnnResourceDst);
      bwd_bottom_diff_->create_internal_layout(reluFwd_, dnnResourceSrc);
    }
  } else {
    DLOG(INFO) << "Using cpu_data in MKLReLULayer.";
    bottom_data =
      reinterpret_cast<void *>(const_cast<Dtype*>(bottom[0]->cpu_data()));
    if (reluFwd_ == NULL) {
      // first pass
      dnnError_t e;
      Dtype negative_slope = this->layer_param_.relu_param().negative_slope();
      e = dnnReLUCreateForward<Dtype>(&reluFwd_, NULL,
              fwd_bottom_data_->layout_usr, negative_slope);
      CHECK_EQ(e, E_SUCCESS);
      e = dnnReLUCreateBackward<Dtype>(&reluBwd_, NULL,
              fwd_bottom_data_->layout_usr, fwd_bottom_data_->layout_usr,
              negative_slope);
      CHECK_EQ(e, E_SUCCESS);
    }
  }

  dnnError_t e;
  void* relu_res[dnnResourceNumber];
  relu_res[dnnResourceSrc] = bottom_data;

  if (fwd_top_data_->conversion_needed()) {
    if (bottom[0] == top[0]) {
//      top[0]->set_prv_data_descriptor(fwd_bottom_data_);
      DLOG(INFO) << "Using bottom as top (in-place) in mklReLU.";
    } else {
      top[0]->set_prv_data_descriptor(fwd_top_data_);
      DLOG(INFO) << "Using mutable_prv (out-of-place) in mklReLU.";
    }
    relu_res[dnnResourceDst] =
            reinterpret_cast<void *>(top[0]->mutable_prv_data());
  } else {
    relu_res[dnnResourceDst] =
            reinterpret_cast<void *>(top[0]->mutable_cpu_data());
    DLOG(INFO) << "Using cpu_data for top in mklReLU.";
  }

  e = dnnExecute<Dtype>(reluFwd_, relu_res);
  CHECK_EQ(e, E_SUCCESS);
}

template <typename Dtype>
void MKLReLULayer<Dtype>::Backward_cpu(const vector<Blob<Dtype>*>& top,
    const vector<bool>& propagate_down,
    const vector<Blob<Dtype>*>& bottom) {
  if (propagate_down[0]) {
    void* bottom_data =
        reinterpret_cast<void *>(const_cast<Dtype*>(bottom[0]->prv_data()));
    if (NULL == bottom_data) {
      bottom_data =
        reinterpret_cast<void *>(const_cast<Dtype*>(bottom[0]->cpu_data()));
    }

    dnnError_t e;
    void* relu_res[dnnResourceNumber];
    relu_res[dnnResourceSrc] = bottom_data;

    relu_res[dnnResourceDiffDst] = bwd_top_diff_->get_converted_prv(top[0],
            true);
    if (bwd_bottom_diff_->conversion_needed()) {
      if (NULL != bottom[0]->get_prv_data_descriptor()) {
        bottom[0]->set_prv_diff_descriptor(fwd_bottom_data_);
        DLOG(INFO) << "Using top as bottom (in-place) in mklReLU-backward.";
      } else {
        bottom[0]->set_prv_diff_descriptor(bwd_bottom_diff_);
        DLOG(INFO) << "Using top as bottom (in-place) in mklReLU-backward.";
      }
      relu_res[dnnResourceDiffSrc] = bottom[0]->mutable_prv_diff();
    } else {
      relu_res[dnnResourceDiffSrc] = bottom[0]->mutable_cpu_diff();
      DLOG(INFO) << "Using mutable_prv (out-of-place) in mklReLU-backward.";
    }

    e = dnnExecute<Dtype>(reluBwd_, relu_res);
    CHECK_EQ(e, E_SUCCESS);
  }
}

#ifdef CPU_ONLY
STUB_GPU(MKLReLULayer);
#else
template <typename Dtype>
void MKLReLULayer<Dtype>::Forward_gpu(const vector<Blob<Dtype>*>& bottom,
    const vector<Blob<Dtype>*>& top) {NOT_IMPLEMENTED;}
template <typename Dtype>
void MKLReLULayer<Dtype>::Backward_gpu(const vector<Blob<Dtype>*>& top,
    const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom)
  {NOT_IMPLEMENTED;}
#endif

INSTANTIATE_CLASS(MKLReLULayer);
}  // namespace caffe
#endif  // #ifdef MKL2017_SUPPORTED
