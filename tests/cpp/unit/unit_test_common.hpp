/*******************************************************************************
* Copyright 2020-2021 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#ifndef UNIT_TEST_COMMON_HPP
#define UNIT_TEST_COMMON_HPP

#include <memory>
#include <vector>

#include "interface/backend.hpp"
#include "interface/engine.hpp"
#include "interface/stream.hpp"

#include "backend/dnnl/backend.hpp"
#include "backend/dnnl/common.hpp"

#if DNNL_GRAPH_WITH_SYCL
#include <CL/sycl.hpp>
#endif

namespace impl = dnnl::graph::impl;
namespace dnnl_impl = impl::dnnl_impl;

static impl::kernel_registry &get_dnnl_kernel_registry() {
#ifdef _WIN32
    static bool dnnl_enabled = impl::backend_manager::register_backend("dnnl",
            &impl::backend_manager::create_backend<dnnl_impl::dnnl_backend>);
    if (!dnnl_enabled) { throw std::exception("cannot init dnnl backend."); };
#endif // _WIN32
    return std::dynamic_pointer_cast<dnnl_impl::dnnl_backend>(
            impl::backend_manager::get_backend("dnnl"))
            ->get_kernel_registry();
}

#if DNNL_GRAPH_WITH_SYCL
cl::sycl::device &get_device();
cl::sycl::context &get_context();
void *sycl_alloc(size_t n, const void *dev, const void *ctx);
void sycl_free(void *ptr, const void *ctx);
#endif // DNNL_GRAPH_WITH_SYCL

impl::engine_t &get_engine(dnnl::graph::impl::engine_kind_t engine_kind
        = dnnl::graph::impl::engine_kind::any_engine);

impl::stream_t &get_stream();

namespace test {

#if DNNL_GRAPH_WITH_SYCL
constexpr size_t usm_alignment = 16;

template <typename T>
using AllocatorBase = cl::sycl::usm_allocator<T, cl::sycl::usm::alloc::shared,
        usm_alignment>;
#else
template <typename T>
using AllocatorBase = std::allocator<T>;
#endif // DNNL_GRAPH_WITH_SYCL

template <typename T>
class TestAllocator : public AllocatorBase<T> {
public:
#if DNNL_GRAPH_WITH_SYCL
    TestAllocator() : AllocatorBase<T>(get_context(), get_device()) {}
#else
    TestAllocator() : AllocatorBase<T>() {}
#endif

    template <typename U>
    struct rebind {
        using other = TestAllocator<U>;
    };
};

template <typename T>
#if DNNL_GRAPH_WITH_SYCL
using vector = std::vector<T, TestAllocator<T>>;
#else
using vector = std::vector<T>;
#endif /* _WIN32 */
} // namespace test

#endif
