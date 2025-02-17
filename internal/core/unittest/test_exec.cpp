// Copyright (C) 2019-2020 Zilliz. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance
// with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations under the License

#include <boost/format.hpp>
#include <gtest/gtest.h>
#include <cstdint>
#include <memory>
#include <regex>
#include <vector>
#include <chrono>

#include "query/Expr.h"
#include "query/PlanImpl.h"
#include "query/PlanNode.h"
#include "query/generated/ExecPlanNodeVisitor.h"
#include "query/generated/ExprVisitor.h"
#include "query/generated/ShowPlanNodeVisitor.h"
#include "segcore/SegmentSealed.h"
#include "test_utils/AssertUtils.h"
#include "test_utils/DataGen.h"
#include "plan/PlanNode.h"
#include "exec/Task.h"
#include "exec/QueryContext.h"
#include "expr/ITypeExpr.h"
#include "exec/expression/Expr.h"

using namespace milvus;
using namespace milvus::exec;
using namespace milvus::query;
using namespace milvus::segcore;

class TaskTest : public testing::Test {
 protected:
    void
    SetUp() override {
        using namespace milvus;
        using namespace milvus::query;
        using namespace milvus::segcore;
        auto schema = std::make_shared<Schema>();
        auto vec_fid = schema->AddDebugField(
            "fakevec", DataType::VECTOR_FLOAT, 16, knowhere::metric::L2);
        auto bool_fid = schema->AddDebugField("bool", DataType::BOOL);
        field_map_.insert({"bool", bool_fid});
        auto bool_1_fid = schema->AddDebugField("bool1", DataType::BOOL);
        field_map_.insert({"bool1", bool_1_fid});
        auto int8_fid = schema->AddDebugField("int8", DataType::INT8);
        field_map_.insert({"int8", int8_fid});
        auto int8_1_fid = schema->AddDebugField("int81", DataType::INT8);
        field_map_.insert({"int81", int8_1_fid});
        auto int16_fid = schema->AddDebugField("int16", DataType::INT16);
        field_map_.insert({"int16", int16_fid});
        auto int16_1_fid = schema->AddDebugField("int161", DataType::INT16);
        field_map_.insert({"int161", int16_1_fid});
        auto int32_fid = schema->AddDebugField("int32", DataType::INT32);
        field_map_.insert({"int32", int32_fid});
        auto int32_1_fid = schema->AddDebugField("int321", DataType::INT32);
        field_map_.insert({"int321", int32_1_fid});
        auto int64_fid = schema->AddDebugField("int64", DataType::INT64);
        field_map_.insert({"int64", int64_fid});
        auto int64_1_fid = schema->AddDebugField("int641", DataType::INT64);
        field_map_.insert({"int641", int64_1_fid});
        auto float_fid = schema->AddDebugField("float", DataType::FLOAT);
        field_map_.insert({"float", float_fid});
        auto float_1_fid = schema->AddDebugField("float1", DataType::FLOAT);
        field_map_.insert({"float1", float_1_fid});
        auto double_fid = schema->AddDebugField("double", DataType::DOUBLE);
        field_map_.insert({"double", double_fid});
        auto double_1_fid = schema->AddDebugField("double1", DataType::DOUBLE);
        field_map_.insert({"double1", double_1_fid});
        auto str1_fid = schema->AddDebugField("string1", DataType::VARCHAR);
        field_map_.insert({"string1", str1_fid});
        auto str2_fid = schema->AddDebugField("string2", DataType::VARCHAR);
        field_map_.insert({"string2", str2_fid});
        auto str3_fid = schema->AddDebugField("string3", DataType::VARCHAR);
        field_map_.insert({"string3", str3_fid});
        schema->set_primary_field_id(str1_fid);

        auto segment = CreateSealedSegment(schema);
        size_t N = 1000000;
        num_rows_ = N;
        auto raw_data = DataGen(schema, N);
        auto fields = schema->get_fields();
        for (auto field_data : raw_data.raw_->fields_data()) {
            int64_t field_id = field_data.field_id();

            auto info = FieldDataInfo(field_data.field_id(), N, "/tmp/a");
            auto field_meta = fields.at(FieldId(field_id));
            info.channel->push(
                CreateFieldDataFromDataArray(N, &field_data, field_meta));
            info.channel->close();

            segment->LoadFieldData(FieldId(field_id), info);
        }
        segment_ = SegmentSealedSPtr(segment.release());
    }

    void
    TearDown() override {
    }

 public:
    SegmentSealedSPtr segment_;
    std::map<std::string, FieldId> field_map_;
    int64_t num_rows_{0};
};

TEST_F(TaskTest, UnaryExpr) {
    ::milvus::proto::plan::GenericValue value;
    value.set_int64_val(-1);
    auto logical_expr = std::make_shared<milvus::expr::UnaryRangeFilterExpr>(
        expr::ColumnInfo(field_map_["int64"], DataType::INT64),
        proto::plan::OpType::LessThan,
        value);
    std::vector<milvus::plan::PlanNodePtr> sources;
    auto filter_node = std::make_shared<milvus::plan::FilterBitsNode>(
        "plannode id 1", logical_expr, sources);
    auto plan = plan::PlanFragment(filter_node);
    auto query_context = std::make_shared<milvus::exec::QueryContext>(
        "test1",
        segment_.get(),
        MAX_TIMESTAMP,
        std::make_shared<milvus::exec::QueryConfig>(
            std::unordered_map<std::string, std::string>{}));

    auto start = std::chrono::steady_clock::now();
    auto task = Task::Create("task_unary_expr", plan, 0, query_context);
    int64_t num_rows = 0;
    int i = 0;
    for (;;) {
        auto result = task->Next();
        if (!result) {
            break;
        }
        num_rows += result->size();
    }
    auto cost = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - start)
                    .count();
    std::cout << "cost: " << cost << "us" << std::endl;
    EXPECT_EQ(num_rows, num_rows_);
}

TEST_F(TaskTest, LogicalExpr) {
    ::milvus::proto::plan::GenericValue value;
    value.set_int64_val(-1);
    auto left = std::make_shared<milvus::expr::UnaryRangeFilterExpr>(
        expr::ColumnInfo(field_map_["int64"], DataType::INT64),
        proto::plan::OpType::LessThan,
        value);
    auto right = std::make_shared<milvus::expr::UnaryRangeFilterExpr>(
        expr::ColumnInfo(field_map_["int64"], DataType::INT64),
        proto::plan::OpType::LessThan,
        value);

    auto top = std::make_shared<milvus::expr::LogicalBinaryExpr>(
        expr::LogicalBinaryExpr::OpType::And, left, right);
    std::vector<milvus::plan::PlanNodePtr> sources;
    auto filter_node = std::make_shared<milvus::plan::FilterBitsNode>(
        "plannode id 1", top, sources);
    auto plan = plan::PlanFragment(filter_node);
    auto query_context = std::make_shared<milvus::exec::QueryContext>(
        "test1",
        segment_.get(),
        MAX_TIMESTAMP,
        std::make_shared<milvus::exec::QueryConfig>(
            std::unordered_map<std::string, std::string>{}));

    auto start = std::chrono::steady_clock::now();
    auto task =
        Task::Create("task_logical_binary_expr", plan, 0, query_context);
    int64_t num_rows = 0;
    for (;;) {
        auto result = task->Next();
        if (!result) {
            break;
        }
        num_rows += result->size();
    }
    auto cost = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - start)
                    .count();
    std::cout << "cost: " << cost << "us" << std::endl;
    EXPECT_EQ(num_rows, num_rows_);
}

TEST(CompileInputs, and) {
    using namespace milvus;
    using namespace milvus::query;
    using namespace milvus::segcore;
    auto schema = std::make_shared<Schema>();
    auto vec_fid = schema->AddDebugField(
        "fakevec", DataType::VECTOR_FLOAT, 16, knowhere::metric::L2);
    auto int64_fid = schema->AddDebugField("int64", DataType::INT64);
    auto seg = CreateSealedSegment(schema);
    proto::plan::GenericValue val;
    val.set_int64_val(10);
    // expr: (int64_fid < 10 and int64_fid < 10) and (int64_fid < 10 and int64_fid < 10)
    auto expr1 = std::make_shared<expr::UnaryRangeFilterExpr>(
        expr::ColumnInfo(int64_fid, DataType::INT64),
        proto::plan::OpType::GreaterThan,
        val);
    auto expr2 = std::make_shared<expr::UnaryRangeFilterExpr>(
        expr::ColumnInfo(int64_fid, DataType::INT64),
        proto::plan::OpType::GreaterThan,
        val);
    auto expr3 = std::make_shared<expr::LogicalBinaryExpr>(
        expr::LogicalBinaryExpr::OpType::And, expr1, expr2);
    auto expr4 = std::make_shared<expr::UnaryRangeFilterExpr>(
        expr::ColumnInfo(int64_fid, DataType::INT64),
        proto::plan::OpType::GreaterThan,
        val);
    auto expr5 = std::make_shared<expr::UnaryRangeFilterExpr>(
        expr::ColumnInfo(int64_fid, DataType::INT64),
        proto::plan::OpType::GreaterThan,
        val);
    auto expr6 = std::make_shared<expr::LogicalBinaryExpr>(
        expr::LogicalBinaryExpr::OpType::And, expr1, expr2);
    auto expr7 = std::make_shared<expr::LogicalBinaryExpr>(
        expr::LogicalBinaryExpr::OpType::And, expr3, expr6);
    auto query_context = std::make_shared<milvus::exec::QueryContext>(
        DEAFULT_QUERY_ID, seg.get(), MAX_TIMESTAMP);
    auto exprs = milvus::exec::CompileInputs(expr7, query_context.get(), {});
    EXPECT_EQ(exprs.size(), 4);
    for (int i = 0; i < exprs.size(); ++i) {
        std::cout << exprs[i]->get_name() << std::endl;
        EXPECT_STREQ(exprs[i]->get_name().c_str(), "PhyUnaryRangeFilterExpr");
    }
}

TEST(CompileInputs, or_with_and) {
    using namespace milvus;
    using namespace milvus::query;
    using namespace milvus::segcore;
    auto schema = std::make_shared<Schema>();
    auto vec_fid = schema->AddDebugField(
        "fakevec", DataType::VECTOR_FLOAT, 16, knowhere::metric::L2);
    auto int64_fid = schema->AddDebugField("int64", DataType::INT64);
    auto seg = CreateSealedSegment(schema);
    proto::plan::GenericValue val;
    val.set_int64_val(10);
    {
        // expr: (int64_fid < 10 and int64_fid < 10) or (int64_fid < 10 and int64_fid < 10)
        auto expr1 = std::make_shared<expr::UnaryRangeFilterExpr>(
            expr::ColumnInfo(int64_fid, DataType::INT64),
            proto::plan::OpType::GreaterThan,
            val);
        auto expr2 = std::make_shared<expr::UnaryRangeFilterExpr>(
            expr::ColumnInfo(int64_fid, DataType::INT64),
            proto::plan::OpType::GreaterThan,
            val);
        auto expr3 = std::make_shared<expr::LogicalBinaryExpr>(
            expr::LogicalBinaryExpr::OpType::And, expr1, expr2);
        auto expr4 = std::make_shared<expr::UnaryRangeFilterExpr>(
            expr::ColumnInfo(int64_fid, DataType::INT64),
            proto::plan::OpType::GreaterThan,
            val);
        auto expr5 = std::make_shared<expr::UnaryRangeFilterExpr>(
            expr::ColumnInfo(int64_fid, DataType::INT64),
            proto::plan::OpType::GreaterThan,
            val);
        auto expr6 = std::make_shared<expr::LogicalBinaryExpr>(
            expr::LogicalBinaryExpr::OpType::And, expr1, expr2);
        auto query_context = std::make_shared<milvus::exec::QueryContext>(
            DEAFULT_QUERY_ID, seg.get(), MAX_TIMESTAMP);
        auto expr7 = std::make_shared<expr::LogicalBinaryExpr>(
            expr::LogicalBinaryExpr::OpType::Or, expr3, expr6);
        auto exprs =
            milvus::exec::CompileInputs(expr7, query_context.get(), {});
        EXPECT_EQ(exprs.size(), 2);
        for (int i = 0; i < exprs.size(); ++i) {
            std::cout << exprs[i]->get_name() << std::endl;
            EXPECT_STREQ(exprs[i]->get_name().c_str(), "and");
        }
    }
    {
        // expr: (int64_fid < 10 or int64_fid < 10) or (int64_fid < 10 and int64_fid < 10)
        auto expr1 = std::make_shared<expr::UnaryRangeFilterExpr>(
            expr::ColumnInfo(int64_fid, DataType::INT64),
            proto::plan::OpType::GreaterThan,
            val);
        auto expr2 = std::make_shared<expr::UnaryRangeFilterExpr>(
            expr::ColumnInfo(int64_fid, DataType::INT64),
            proto::plan::OpType::GreaterThan,
            val);
        auto expr3 = std::make_shared<expr::LogicalBinaryExpr>(
            expr::LogicalBinaryExpr::OpType::Or, expr1, expr2);
        auto expr4 = std::make_shared<expr::UnaryRangeFilterExpr>(
            expr::ColumnInfo(int64_fid, DataType::INT64),
            proto::plan::OpType::GreaterThan,
            val);
        auto expr5 = std::make_shared<expr::UnaryRangeFilterExpr>(
            expr::ColumnInfo(int64_fid, DataType::INT64),
            proto::plan::OpType::GreaterThan,
            val);
        auto expr6 = std::make_shared<expr::LogicalBinaryExpr>(
            expr::LogicalBinaryExpr::OpType::And, expr1, expr2);
        auto query_context = std::make_shared<milvus::exec::QueryContext>(
            DEAFULT_QUERY_ID, seg.get(), MAX_TIMESTAMP);
        auto expr7 = std::make_shared<expr::LogicalBinaryExpr>(
            expr::LogicalBinaryExpr::OpType::Or, expr3, expr6);
        auto exprs =
            milvus::exec::CompileInputs(expr7, query_context.get(), {});
        std::cout << exprs.size() << std::endl;
        EXPECT_EQ(exprs.size(), 3);
        for (int i = 0; i < exprs.size() - 1; ++i) {
            std::cout << exprs[i]->get_name() << std::endl;
            EXPECT_STREQ(exprs[i]->get_name().c_str(),
                         "PhyUnaryRangeFilterExpr");
        }
        EXPECT_STREQ(exprs[2]->get_name().c_str(), "and");
    }
    {
        // expr: (int64_fid < 10 or int64_fid < 10) and (int64_fid < 10 and int64_fid < 10)
        auto expr1 = std::make_shared<expr::UnaryRangeFilterExpr>(
            expr::ColumnInfo(int64_fid, DataType::INT64),
            proto::plan::OpType::GreaterThan,
            val);
        auto expr2 = std::make_shared<expr::UnaryRangeFilterExpr>(
            expr::ColumnInfo(int64_fid, DataType::INT64),
            proto::plan::OpType::GreaterThan,
            val);
        auto expr3 = std::make_shared<expr::LogicalBinaryExpr>(
            expr::LogicalBinaryExpr::OpType::Or, expr1, expr2);
        auto expr4 = std::make_shared<expr::UnaryRangeFilterExpr>(
            expr::ColumnInfo(int64_fid, DataType::INT64),
            proto::plan::OpType::GreaterThan,
            val);
        auto expr5 = std::make_shared<expr::UnaryRangeFilterExpr>(
            expr::ColumnInfo(int64_fid, DataType::INT64),
            proto::plan::OpType::GreaterThan,
            val);
        auto expr6 = std::make_shared<expr::LogicalBinaryExpr>(
            expr::LogicalBinaryExpr::OpType::And, expr1, expr2);
        auto query_context = std::make_shared<milvus::exec::QueryContext>(
            DEAFULT_QUERY_ID, seg.get(), MAX_TIMESTAMP);
        auto expr7 = std::make_shared<expr::LogicalBinaryExpr>(
            expr::LogicalBinaryExpr::OpType::And, expr3, expr6);
        auto exprs =
            milvus::exec::CompileInputs(expr7, query_context.get(), {});
        std::cout << exprs.size() << std::endl;
        EXPECT_EQ(exprs.size(), 3);
        EXPECT_STREQ(exprs[0]->get_name().c_str(), "or");
        for (int i = 1; i < exprs.size(); ++i) {
            std::cout << exprs[i]->get_name() << std::endl;
            EXPECT_STREQ(exprs[i]->get_name().c_str(),
                         "PhyUnaryRangeFilterExpr");
        }
    }
}