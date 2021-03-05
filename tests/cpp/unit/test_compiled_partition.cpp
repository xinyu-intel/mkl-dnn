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

#include <gtest/gtest.h>

#include "interface/ir.hpp"
#include "interface/partition.hpp"

#include "unit_test_common.hpp"
#include "utils.hpp"

namespace utils = dnnl::graph::tests::unit::utils;

TEST(compiled_partition, relu) {
    impl::engine_t &eng = get_engine();

    size_t operator_num = get_dnnl_kernel_registry().get_register_kernels_num();
    ASSERT_NE(operator_num, 0);

    impl::node_t relu_node(impl::op_kind::ReLU);
    relu_node.set_attr<std::string>("backend", "dnnl");

    const impl::logical_tensor_t lt_in = utils::logical_tensor_init(
            /* tid= */ 1, {1, 1, 3, 3}, impl::data_type::f32);
    const impl::logical_tensor_t lt_out
            = utils::logical_tensor_init(/* tid= */ 2, {1, 1, 3, 3},
                    impl::data_type::f32, impl::layout_type::any);

    relu_node.add_input_tensors({lt_in});
    relu_node.add_output_tensors({lt_out});

    impl::partition_t p;
    p.init(&relu_node, eng.kind());

    impl::compiled_partition_t cp(p);
    ASSERT_EQ(p.id(), cp.src_partition().id());

    std::vector<const impl::logical_tensor_t *> lt_inputs {&lt_in};
    std::vector<const impl::logical_tensor_t *> lt_outputs {&lt_out};
    impl::status_t status = p.compile(&cp, lt_inputs, lt_outputs, &eng);
    ASSERT_EQ(status, impl::status::success);
    impl::logical_tensor_t query_in_lt, query_out_lt;
    impl::status_t status_query
            = cp.query_logical_tensor(lt_out.id, &query_out_lt);
    ASSERT_EQ(status_query, impl::status::success);
    ASSERT_EQ(query_out_lt.layout_type, impl::layout_type::opaque);

    size_t size_in = 0, size_out = 0;
    cp.query_logical_tensor(lt_in.id, &query_in_lt);
    size_in = impl::logical_tensor_wrapper(query_in_lt).size();
    size_out = impl::logical_tensor_wrapper(query_out_lt).size();
    ASSERT_EQ(size_in, 9 * sizeof(float));
    ASSERT_EQ(size_in, size_out);

    size_t ele_num_in = size_in / sizeof(float);
    test::vector<float> data_in(ele_num_in);
    test::vector<float> data_out(ele_num_in);
    for (size_t i = 0; i < ele_num_in; i++) {
        data_in[i] = static_cast<float>(i) - static_cast<float>(ele_num_in / 2);
    }

    impl::tensor_t t_in(lt_in, data_in.data()),
            t_out(query_out_lt, data_out.data());

    std::vector<impl::tensor_t> t_inputs, t_outputs;
    t_inputs.emplace_back(t_in);
    t_outputs.emplace_back(t_out);

    impl::stream_t &strm = get_stream();
    EXPECT_SUCCESS(cp.execute(&strm, t_inputs, t_outputs));

    std::unique_ptr<float[]> ref_out(new float[ele_num_in]);
    for (size_t i = 0; i < ele_num_in; i++) {
        ref_out[i] = (i < ele_num_in / 2)
                ? 0.0f
                : static_cast<float>(i) - static_cast<float>(ele_num_in / 2);
    }

    for (size_t i = 0; i < ele_num_in; i++) {
        ASSERT_FLOAT_EQ(ref_out[i], data_out[i]);
    }
}

TEST(compiled_partition, search_required_inputs_outputs) {
    impl::engine_t &eng = get_engine();

    size_t operator_num = get_dnnl_kernel_registry().get_register_kernels_num();
    ASSERT_NE(operator_num, 0);

    impl::node_t relu_node(impl::op_kind::ReLU);
    relu_node.set_attr<std::string>("backend", "dnnl");

    impl::logical_tensor_t lt_in = utils::logical_tensor_init(
            /* tid= */ 1, {1, 1, 3, 3}, impl::data_type::f32);
    impl::logical_tensor_t lt_out = utils::logical_tensor_init(/* tid= */ 2,
            {1, 1, 3, 3}, impl::data_type::f32, impl::layout_type::any);

    relu_node.add_input_tensors({lt_in});
    relu_node.add_output_tensors({lt_out});

    impl::partition_t p;
    p.init(&relu_node, eng.kind());

    impl::compiled_partition_t cp(p);
    ASSERT_EQ(p.id(), cp.src_partition().id());

    impl::logical_tensor_t lt_in_additional1 = utils::logical_tensor_init(
            /* tid= */ 3, {1, 1, 3, 3}, impl::data_type::f32);
    impl::logical_tensor_t lt_in_additional2 = utils::logical_tensor_init(
            /* tid= */ 4, {1, 1, 3, 3}, impl::data_type::f32);
    impl::logical_tensor_t lt_out_additional1
            = utils::logical_tensor_init(/* tid= */ 5, {1, 1, 3, 3},
                    impl::data_type::f32, impl::layout_type::any);
    impl::logical_tensor_t lt_out_additional2
            = utils::logical_tensor_init(/* tid= */ 6, {1, 1, 3, 3},
                    impl::data_type::f32, impl::layout_type::any);

    // in/outputs list have to contain required logical tensor
    std::vector<const impl::logical_tensor_t *> lt_inputs_wrong {
            &lt_in_additional1, &lt_in_additional2}; // no required
    std::vector<const impl::logical_tensor_t *> lt_outputs_wrong {
            &lt_out_additional1, &lt_out_additional2}; // no required

    // compile function return a miss_ins_outs error, since it can't find
    // required inputs and outputs from the given arguments
    impl::status_t status
            = p.compile(&cp, lt_inputs_wrong, lt_outputs_wrong, &eng);
    ASSERT_EQ(status, impl::status::miss_ins_outs);

    // in/outputs list can contain more logical tensors than required
    std::vector<const impl::logical_tensor_t *> lt_inputs_correct {
            &lt_in_additional1, /* required */ &lt_in, &lt_in_additional2};
    std::vector<const impl::logical_tensor_t *> lt_outputs_correct {
            &lt_out_additional1, &lt_out_additional2, /* required */ &lt_out};

    // compile function will search its required inputs and outputs by itself
    status = p.compile(&cp, lt_inputs_correct, lt_outputs_correct, &eng);
    ASSERT_EQ(status, impl::status::success);

    //query logical_tensor to get its layout
    impl::logical_tensor_t query_lt_in, query_lt_out;
    ASSERT_EQ(cp.query_logical_tensor(lt_out.id, &query_lt_out),
            impl::status::success);
    ASSERT_EQ(query_lt_out.layout_type, impl::layout_type::opaque);

    size_t size_in = 0, size_out = 0;
    cp.query_logical_tensor(lt_in.id, &query_lt_in);
    size_in = impl::logical_tensor_wrapper(query_lt_in).size();
    size_out = impl::logical_tensor_wrapper(query_lt_out).size();
    ASSERT_EQ(size_in, 9 * sizeof(float));
    ASSERT_EQ(size_in, size_out);

    size_t ele_num_in = size_in / sizeof(float);
    size_t ele_num_out = size_out / sizeof(float);
    test::vector<float> data_in(ele_num_in);
    test::vector<float> data_out(ele_num_out);
    for (size_t i = 0; i < ele_num_in; i++) {
        data_in[i] = static_cast<float>(i) - static_cast<float>(ele_num_in / 2);
    }

    impl::tensor_t t_in(lt_in, data_in.data()),
            t_out(query_lt_out, data_out.data());
    impl::tensor_t t_in_additional1(lt_in_additional1, nullptr),
            t_in_additional2(lt_in_additional2, nullptr);
    impl::tensor_t t_out_additional1(lt_out_additional1, nullptr),
            t_out_additional2(lt_out_additional2, nullptr);

    // when submit, in/outputs tensor's order must be same as compile
    // funcstion's in/outputs logical tensor
    std::vector<impl::tensor_t> t_inputs_correct {
            t_in_additional1, t_in, t_in_additional2};
    std::vector<impl::tensor_t> t_outputs_correct {
            t_out_additional1, t_out_additional2, t_out};

    impl::stream_t &strm = get_stream();
    EXPECT_SUCCESS(cp.execute(&strm, t_inputs_correct, t_outputs_correct));

    test::vector<float> ref_out(ele_num_in);
    for (size_t i = 0; i < ele_num_in; i++) {
        ref_out[i] = (i < ele_num_in / 2)
                ? 0.0f
                : static_cast<float>(i) - static_cast<float>(ele_num_in / 2);
    }

    for (size_t i = 0; i < ele_num_in; i++) {
        ASSERT_FLOAT_EQ(ref_out[i], data_out[i]);
    }

    // if in/outputs tensor's order is not same as compile
    // function's in/outputs logical tensor, we can also execute
    std::vector<impl::tensor_t> t_inputs_wrong {
            t_in_additional1, t_in_additional2, t_in};
    std::vector<impl::tensor_t> t_outputs_wrong {
            t_out_additional1, t_out, t_out_additional2};

    EXPECT_SUCCESS(cp.execute(&strm, t_inputs_wrong, t_outputs_wrong));
}

TEST(compiled_partition, allow_repeated_inputs) {
    impl::engine_t &eng = get_engine();

    size_t operator_num = get_dnnl_kernel_registry().get_register_kernels_num();
    ASSERT_NE(operator_num, 0);

    impl::node_t n(impl::op_kind::Multiply);
    n.set_attr<std::string>("backend", "dnnl");

    impl::logical_tensor_t lt_in1 = utils::logical_tensor_init(
            /* tid= */ 1, {1, 1, 3, 3}, impl::data_type::f32);
    impl::logical_tensor_t lt_out = utils::logical_tensor_init(/* tid= */ 2,
            {1, 1, 3, 3}, impl::data_type::f32, impl::layout_type::any);

    // repeated inputs
    n.add_input_tensors({lt_in1, lt_in1});
    n.add_output_tensors({lt_out});

    impl::partition_t p;
    p.init(&n, eng.kind());

    impl::compiled_partition_t cp(p);

    // only one input
    std::vector<const impl::logical_tensor_t *> lt_ins {&lt_in1};
    std::vector<const impl::logical_tensor_t *> lt_outs {&lt_out};

    impl::status_t status = p.compile(&cp, lt_ins, lt_outs, &eng);
    ASSERT_EQ(status, impl::status::success);

    impl::logical_tensor_t query_lt_out;
    ASSERT_EQ(cp.query_logical_tensor(lt_out.id, &query_lt_out),
            impl::status::success);
    ASSERT_EQ(query_lt_out.layout_type, impl::layout_type::opaque);

    size_t size_in = 0, size_out = 0;
    size_in = impl::logical_tensor_wrapper(lt_in1).size();
    size_out = impl::logical_tensor_wrapper(query_lt_out).size();
    ASSERT_EQ(size_in, 9 * sizeof(float));
    ASSERT_EQ(size_in, size_out);

    test::vector<float> data_in {
            1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f};
    test::vector<float> data_out(data_in.size());
    test::vector<float> ref_out {
            1.0f, 4.0f, 9.0f, 16.0f, 25.0f, 36.0f, 49.0f, 64.0f, 81.0f};

    impl::tensor_t t_in1(lt_in1, data_in.data());
    impl::tensor_t t_out(query_lt_out, data_out.data());

    // only one input
    std::vector<impl::tensor_t> t_ins {t_in1};
    std::vector<impl::tensor_t> t_outs {t_out};

    impl::stream_t &strm = get_stream();
    EXPECT_SUCCESS(cp.execute(&strm, t_ins, t_outs));

    for (size_t i = 0; i < ref_out.size(); i++) {
        ASSERT_FLOAT_EQ(ref_out[i], data_out[i]);
    }
}

TEST(compiled_partition, not_allow_repeated_inputs) {
    impl::engine_t &eng = get_engine();

    size_t operator_num = get_dnnl_kernel_registry().get_register_kernels_num();
    ASSERT_NE(operator_num, 0);

    impl::node_t n(impl::op_kind::MatMul);
    n.set_attr<std::string>("backend", "dnnl");

    impl::logical_tensor_t lt_in1 = utils::logical_tensor_init(
            /* tid= */ 1, {1, 1, 3, 3}, impl::data_type::f32);
    impl::logical_tensor_t lt_out = utils::logical_tensor_init(/* tid= */ 2,
            {1, 1, 3, 3}, impl::data_type::f32, impl::layout_type::any);

    // repeated inputs
    n.add_input_tensors({lt_in1, lt_in1});
    n.add_output_tensors({lt_out});

    impl::partition_t p;
    p.init(&n, eng.kind());

    impl::compiled_partition_t cp(p);

    // only one input
    std::vector<const impl::logical_tensor_t *> lt_ins {&lt_in1};
    std::vector<const impl::logical_tensor_t *> lt_outs {&lt_out};

    impl::status_t status = p.compile(&cp, lt_ins, lt_outs, &eng);
    ASSERT_EQ(status, impl::status::miss_ins_outs);
}
