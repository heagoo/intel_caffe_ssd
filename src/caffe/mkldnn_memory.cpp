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

#ifdef MKLDNN_SUPPORTED
#include "caffe/mkldnn_memory.hpp"

namespace caffe {


template <typename Dtype>
MKLDNNMemoryDescriptorBase<Dtype>::MKLDNNMemoryDescriptorBase(shared_ptr<memory::primitive_desc> usr_memory_pd
                                                            , shared_ptr<memory::primitive_desc> prv_memory_pd
                                                            , Blob<Dtype>* blob
                                                            , MKLDNNLayer<Dtype>* mkldnn_layer)
                                    : name("MKLDNNMemoryDescriptorBase")
                                    , _reorder_usr2prv_pd(NULL), _reorder_prv2usr_pd(NULL), _reorder_extprv2prv_pd(NULL)
                                    ,_prv_memory(NULL), _internal_ptr(NULL), _usr_memory(NULL), _cpu_ptr(NULL)
                                    , _mkldnn_layer(NULL)
{
    set_usr_memory_pd(usr_memory_pd);
    set_prv_memory_pd(prv_memory_pd);
    set_mkldnn_layer(mkldnn_layer);
    this->_blob = blob;
    // !! TODO: check code below (there is error on second and other iterations without it) .
    if (_blob->data()->cpu_ptr())
        _blob->set_prv_data_descriptor(NULL);
}

template <typename Dtype>
void MKLDNNMemoryDescriptorBase<Dtype>::check_usr_with_prv_descriptors()
{
    CHECK(_usr_memory_pd);
    CHECK(_prv_memory_pd);
    int32_t ndims = _usr_memory_pd->desc().data.ndims;
    CHECK_EQ(ndims, _prv_memory_pd->desc().data.ndims)
            << "MKLDNNMemoryDescriptorBase: Usr and Prv memory must have same dimensions number";
    for (int32_t dim = 0; dim < ndims; ++dim) {
        CHECK_EQ(_usr_memory_pd->desc().data.dims[dim]
                , _prv_memory_pd->desc().data.dims[dim])
                << "MKLDNNMemoryDescriptorBase: Usr and Prv memory must have same dimensions";
    }
}

template <typename Dtype>
void MKLDNNMemoryDescriptorBase<Dtype>::create_reorder_descriptors()
{
    CHECK(_usr_memory_pd);
    CHECK(_prv_memory_pd);
    if ( *_usr_memory_pd != *_prv_memory_pd) {
        _reorder_usr2prv_pd = shared_ptr<reorder::primitive_desc>(
                new reorder::primitive_desc(*_usr_memory_pd, *_prv_memory_pd));
        _reorder_prv2usr_pd = shared_ptr<reorder::primitive_desc>(
                new reorder::primitive_desc(*_prv_memory_pd, *_usr_memory_pd));
    }
    if ( _extprv_memory_pd && *_prv_memory_pd != *_extprv_memory_pd) {
        _reorder_extprv2prv_pd = shared_ptr<reorder::primitive_desc>(
                new reorder::primitive_desc(*_extprv_memory_pd, *_prv_memory_pd));
    }
}


template <typename Dtype, bool is_diff>
 MKLDNNMemoryDescriptor<Dtype, is_diff>::MKLDNNMemoryDescriptor(shared_ptr<memory::primitive_desc> usr_memory_pd
                        , shared_ptr<memory::primitive_desc> prv_memory_pd
                        , Blob<Dtype>* blob, MKLDNNLayer<Dtype>* mkldnn_layer)
        : MKLDNNMemoryDescriptorBase<Dtype>(usr_memory_pd, prv_memory_pd, blob, mkldnn_layer)
{
    const Dtype* prv_ptr = is_diff ?  blob->prv_diff() : blob->prv_data();
    if (prv_ptr != NULL) {
        shared_ptr<MKLDNNMemoryDescriptor<Dtype, is_diff> > blob_prv_mkldnn_mem_descr = get_mkldnn_prv_descriptor<Dtype, is_diff>(blob);
        if (*blob_prv_mkldnn_mem_descr->prv_memory_pd() !=  *this->prv_memory_pd()) {
            set_extprv_memory_pd(blob_prv_mkldnn_mem_descr->prv_memory_pd());
        }
    }
}

template <typename Dtype, bool is_diff>
void MKLDNNMemoryDescriptor<Dtype, is_diff>::create_reorder_to_prv(void* cpu_ptr)
{
    CHECK(cpu_ptr);
    CHECK(this->_usr_memory_pd);
    CHECK(this->_prv_memory_pd);
    CHECK(this->_reorder_usr2prv_pd);
    if (this->_cpu_ptr == NULL)
        this->_cpu_ptr = cpu_ptr;
    else
        CHECK_EQ(this->_cpu_ptr, cpu_ptr);
    if(this->_usr_memory == NULL)
        this->_usr_memory.reset(new memory(*this->_usr_memory_pd, cpu_ptr));
    if(this->_reorder_usr2prv.aprimitive == NULL)
        this->_reorder_usr2prv.reset(new reorder(*this->_reorder_usr2prv_pd, *this->_usr_memory, *this->get_prv_memory()));
}

template <typename Dtype, bool is_diff>
void MKLDNNMemoryDescriptor<Dtype, is_diff>::convert_to_prv(void* cpu_ptr)
{
    CHECK(cpu_ptr);
    CHECK_EQ(this->_cpu_ptr, cpu_ptr);
    create_reorder_to_prv(cpu_ptr);
    VLOG(1) << "--- MKLDNNMemoryDescriptorBase<Dtype>::convert_to_prv --- " << this->name;
    this->_reorder_usr2prv.submit();;
}

template <typename Dtype, bool is_diff>
void MKLDNNMemoryDescriptor<Dtype, is_diff>::create_reorder_from_prv(void* cpu_ptr)
{
    CHECK(cpu_ptr);
    CHECK(this->_usr_memory_pd);
    CHECK(this->_prv_memory_pd);
    CHECK(this->_reorder_prv2usr_pd);
    if (this->_cpu_ptr == NULL)
        this->_cpu_ptr = cpu_ptr;
    else
        CHECK_EQ(this->_cpu_ptr, cpu_ptr);
    if(this->_usr_memory == NULL)
        this->_usr_memory.reset(new memory(*this->_usr_memory_pd, cpu_ptr));
    if(this->_reorder_prv2usr.aprimitive == NULL) {
        CHECK(this->aprimitive());
        this->_reorder_prv2usr.aprimitive.reset(new reorder(*this->_reorder_prv2usr_pd, *this->aprimitive(), *this->_usr_memory));
//        this->_reorder_prv2usr.aprimitive.reset(new reorder(*this->aprimitive(), *this->_usr_memory));
    }
}

template <typename Dtype, bool is_diff>
void MKLDNNMemoryDescriptor<Dtype, is_diff>::convert_from_prv(void* cpu_ptr)
{
    CHECK(cpu_ptr);
//    CHECK(this->mkldnn_layer());
    if(this->_reorder_prv2usr_pd == NULL)
        return;
    create_reorder_from_prv(cpu_ptr);
    VLOG(1) << "--- MKLDNNMemoryDescriptorBase<Dtype>::convert_from_prv --- " << this->name;
    this->_reorder_prv2usr.submit();
}

template <typename Dtype, bool is_diff>
void MKLDNNMemoryDescriptor<Dtype, is_diff>::create_reorder_from_extprv(shared_ptr<primitive> aprimitive)
{
    CHECK(aprimitive);
    CHECK(this->_extprv_memory_pd);
    CHECK(this->_prv_memory_pd);
    CHECK(this->_reorder_extprv2prv_pd);
    if(this->_reorder_extprv2prv.aprimitive == NULL)
        this->_reorder_extprv2prv.reset(new reorder(*this->_reorder_extprv2prv_pd, *aprimitive, *this->get_prv_memory()));
}

template <typename Dtype, bool is_diff>
void MKLDNNMemoryDescriptor<Dtype, is_diff>::convert_from_extprv(shared_ptr<primitive> aprimitive)
{
    CHECK(aprimitive);
    if(this->_reorder_extprv2prv_pd == NULL)
        return;
    create_reorder_from_extprv(aprimitive);
    VLOG(1) << "--- MKLDNNMemoryDescriptorBase<Dtype>::convert_from_extprv --- " << this->name;
    this->_reorder_extprv2prv.submit();;
}


template <typename Dtype, bool is_diff>
bool MKLDNNMemoryDescriptor<Dtype, is_diff>::on_to_cpu()
{
    CHECK(this->mkldnn_layer());
    if (StreamHolder::Instance().current_stream() != NULL && StreamHolder::Instance().current_stream()->ready()) {
        VLOG(1) << "- MKLDNNMemoryDescriptorBase<Dtype>::" << __FUNCTION__ << ": stream.wait() - " << this->name;
        StreamHolder::Instance().current_stream()->wait();
    }
    return true;
}

template <typename Dtype>
bool MKLDNNMemoryDescriptorBase<Dtype>::layout_compare(shared_ptr<PrvMemDescr> other)
{
    CHECK_EQ(other->get_descr_type(),
              PrvMemDescr::PRV_DESCR_MKLDNN);

    shared_ptr<MKLDNNMemoryDescriptorBase<Dtype> > other_descr =
        boost::static_pointer_cast<MKLDNNMemoryDescriptorBase<Dtype> >(other);

    return (*other_descr->prv_memory_pd() == *this->prv_memory_pd());
}

template <typename Dtype>
void MKLDNNMemoryDescriptorBase<Dtype>::convert_from_other(shared_ptr<PrvMemDescr> other)
{
    NOT_IMPLEMENTED;
}

template <typename Dtype, bool is_diff>
shared_ptr<primitive> MKLDNNMemoryDescriptor<Dtype, is_diff>::get_blob_prv_primitive(Blob<Dtype>* blob
                                            ,bool set_prv_ptr, bool convert
                                            ,MKLDNNMemoryDescriptor<Dtype,is_diff>* converted_in_fwd)
{
    if (!this->conversion_needed()) {
        return NULL; // TODO: may be CHECK ?
    }

    // Conversion is needed
    const Dtype* prv_ptr = is_diff ?  blob->prv_diff() : blob->prv_data();
    if (prv_ptr == NULL) {
        if (converted_in_fwd) {
            // TODO: use previously done conversion on forward - needed for training
            NOT_IMPLEMENTED;
        }
        if(convert)
            this->convert_to_prv(const_cast<Dtype*>(is_diff ? blob->cpu_diff() : blob->cpu_data()));
        else
            this->create_reorder_to_prv(const_cast<Dtype*>(is_diff ? blob->cpu_diff() : blob->cpu_data()));
        if (set_prv_ptr) {
            if (is_diff)
                blob->set_prv_diff_descriptor(this->get_shared_ptr(), true);
            else
                blob->set_prv_data_descriptor(this->get_shared_ptr(), true);
        }
        return this->reorder_usr2prv();
    } else {
        shared_ptr<MKLDNNMemoryDescriptor<Dtype, is_diff> > blob_prv_mkldnn_mem_descr = get_mkldnn_prv_descriptor<Dtype, is_diff>(blob);

        if (*blob_prv_mkldnn_mem_descr->prv_memory_pd() !=  *this->prv_memory_pd()) {
            // prv in blob and in this descrptor may have different layouts
            if(convert)
                this->convert_from_extprv(blob_prv_mkldnn_mem_descr->aprimitive());
            else
                this->create_reorder_from_extprv(blob_prv_mkldnn_mem_descr->aprimitive());
            return this->reorder_extprv2prv();
        } else if (blob_prv_mkldnn_mem_descr.get() != this) {
            VLOG(1) << "layout OK " << blob_prv_mkldnn_mem_descr->name << " == " << this->name;
        }
// TODO:    CHECK(blob_prv_mkldnn_mem_descr->mkldnn_primitive());
        return blob_prv_mkldnn_mem_descr->aprimitive();
    }
    NOT_IMPLEMENTED;
    return NULL;
}

template <typename Dtype, bool is_diff>
void MKLDNNMemoryDescriptor<Dtype, is_diff>::sync_blob_prv_data(Blob<Dtype>* blob, bool set_prv_ptr)
{
    get_blob_prv_primitive(blob, set_prv_ptr);
}

template <typename Dtype, bool is_diff>
void MKLDNNMemoryDescriptor<Dtype, is_diff>::sync_before_read(bool set_prv_ptr)
{
    // TODO: need to iptimize code
    get_blob_prv_primitive(this->_blob, set_prv_ptr);
}

template <typename Dtype, bool is_diff>
void MKLDNNMemoryDescriptor<Dtype, is_diff>::sync_before_write()
{
    // TODO: need to iptimize code
    this->_blob->set_prv_data_descriptor(this->get_shared_ptr(), this->conversion_needed() ? false : true);
}

template <typename Dtype, bool is_diff>
shared_ptr<primitive> MKLDNNMemoryDescriptor<Dtype, is_diff>::create_input(Blob<Dtype> * blob, bool set_prv_ptr)
{
    shared_ptr<mkldnn::primitive> pres;
    if (this->conversion_needed()) {
        pres = this->get_blob_prv_primitive(blob, set_prv_ptr, false);
    } else {
        pres.reset(new memory(*this->usr_memory_pd(), const_cast<Dtype*>(is_diff ?  blob->cpu_diff() : blob->cpu_data())));
    }
    return pres;
}

template <typename Dtype, bool is_diff>
shared_ptr<memory> MKLDNNMemoryDescriptor<Dtype, is_diff>::create_output_memory(Blob<Dtype> * blob)
{
    shared_ptr<memory> omem;
    if (this->conversion_needed()) {
        if(blob->get_prv_data_descriptor() != NULL) {
            shared_ptr<MKLDNNMemoryDescriptor<Dtype, is_diff> > current_descr = get_mkldnn_prv_descriptor<Dtype, is_diff>(blob);

            omem = current_descr->get_prv_memory();
            this->set_prv_memory(omem);
        } else {
            omem = this->get_prv_memory();
        }
    } else {
        omem.reset(new memory(*this->usr_memory_pd(), is_diff ? blob->mutable_cpu_diff() : blob->mutable_cpu_data()));
    }
    return omem;
}

template <typename Dtype, bool is_diff>
shared_ptr<primitive> MKLDNNMemoryDescriptor<Dtype, is_diff>::create_input(bool set_prv_ptr)
{
    // TODO: need to iptimize code
    return create_input(this->_blob, set_prv_ptr);
}

template <typename Dtype, bool is_diff>
shared_ptr<memory> MKLDNNMemoryDescriptor<Dtype, is_diff>::create_output_memory()
{
    // TODO: need to iptimize code
    shared_ptr<memory> omem = create_output_memory(this->_blob);
    this->_blob->set_prv_data_descriptor(this->get_shared_ptr(), this->conversion_needed() ? false : true);
    return omem;
}

template <typename Dtype, bool is_diff>
shared_ptr<MKLDNNMemoryDescriptor<Dtype, is_diff> > get_mkldnn_prv_descriptor(Blob<Dtype>* blob)
{
    shared_ptr<PrvMemDescr> blob_prv_mem_descriptor = is_diff ?
            (blob->get_prv_diff_descriptor()) : (blob->get_prv_data_descriptor());

    CHECK_EQ(blob_prv_mem_descriptor->get_descr_type(), PrvMemDescr::PRV_DESCR_MKLDNN);

    shared_ptr<MKLDNNMemoryDescriptor<Dtype, is_diff> > blob_prv_mkldnn_mem_descr =
            boost::static_pointer_cast<MKLDNNMemoryDescriptor<Dtype, is_diff> >(blob_prv_mem_descriptor);
    CHECK(blob_prv_mkldnn_mem_descr != NULL);
    return blob_prv_mkldnn_mem_descr;
}

template class MKLDNNMemoryDescriptor<double, true>;
template class MKLDNNMemoryDescriptor<float, true>;
template class MKLDNNMemoryDescriptor<float, false>;
template class MKLDNNMemoryDescriptor<double, false>;
template class MKLDNNMemoryDescriptorBase<float>;
template class MKLDNNMemoryDescriptorBase<double>;
}  // namespace caffe
#endif  // #ifdef MKLDNN_SUPPORTED
