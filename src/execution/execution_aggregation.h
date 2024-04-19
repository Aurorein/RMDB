/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once
#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"
#include <climits>
#include <cfloat>

class AggregationExecutor : public AbstractExecutor
{
private:
        TabMeta tab_;                  // 表的元数据
        std::vector<Condition> conds_; // delete的条件
        RmFileHandle *fh_;             // 表的数据文件句柄
        AggreMeta aggre_meta_;
        std::string tab_name_; // 表名称
        std::vector<Rid> rids_;
        SmManager *sm_manager_;
        Value val_;
        // 对外输出的cols需要将offset修正为0
        std::vector<ColMeta> output_cols_;
        bool is_end_;

public:
        AggregationExecutor(SmManager *sm_manager, const std::string &tab_name, std::vector<Condition> conds, AggreMeta aggre_meta, std::vector<Rid> rids, Context *context)
        {
                aggre_meta_ = aggre_meta;
                sm_manager_ = sm_manager;
                tab_name_ = tab_name;
                tab_ = sm_manager_->db_.get_table(tab_name);
                fh_ = sm_manager_->fhs_.at(tab_name).get();
                conds_ = conds;
                rids_ = rids;
                context_ = context;
                if(aggre_meta_.op_ == AG_COUNT) {
                        ColMeta col_meta_ = {.tab_name = tab_name_, .name = "*", .type = TYPE_INT, .len = sizeof(int), .offset = 0, .index = false};
                        output_cols_.push_back(col_meta_);
                }else {
                        ColMeta col_meta_ = *tab_.get_col(aggre_meta_.tabcol_.col_name);
                        // 对外输出的col_meta需要修改offset字段
                        col_meta_.offset = 0;
                        output_cols_.push_back(col_meta_);
                }
                
                is_end_ = false;
        }

        // conds_条件已经在scan里用过了，现在直接用rids_即可
        std::unique_ptr<RmRecord> Next() override
        {
                return std::make_unique<RmRecord>(val_.raw->size, val_.raw->data);
        }

        Rid &rid() override { return _abstract_rid; }

        Value val(){return val_;}

        std::string getType() { return "AggregationExecutor"; };

        void beginTuple() override {


                switch (aggre_meta_.op_)
                {
                // 1. 如果是count()的情况
                case AG_COUNT: {
                        val_.set_int(rids_.size());
                        val_.init_raw(sizeof(int));
                        return ;
                }
                case AG_SUM:
                {       
                        int sum_int_value = 0;
                        float sum_float_value = 0;
                        auto col_meta = tab_.get_col(aggre_meta_.tabcol_.col_name);
                        int offset = col_meta->offset;
                        // int len = col_meta->len;
                        for(auto &rid: rids_){
                                auto rec = fh_->get_record(rid, nullptr);
                                char *data = rec->data + offset;
                                if(col_meta->type == TYPE_INT){
                                        int value = *reinterpret_cast<int *>(data);
                                        sum_int_value += value;
                                }else if(col_meta->type == TYPE_FLOAT){
                                        float value = *reinterpret_cast<float *>(data);
                                        sum_float_value += value;
                                }
                        }
                        switch(col_meta->type){
                                case TYPE_INT:{
                                        val_.set_int((int)sum_int_value);
                                        val_.init_raw(sizeof(int));
                                        break;
                                }
                                case TYPE_FLOAT:{
                                        val_.set_float(sum_float_value);
                                        val_.init_raw(sizeof(float));
                                        break;
                                }
                                case TYPE_STRING :
                                case TYPE_BIGINT :
                                case TYPE_DATETIME :
                                        throw RMDBError("In execution_aggregation, we don't implement string and bigint and datetime type");
                                default: 
                                        throw InvalidTypeError();
                        }

                        // return nullptr;
                        break;
                }
                case AG_MAX:
                {

                        auto col_meta = tab_.get_col(aggre_meta_.tabcol_.col_name);
                        int offset = col_meta->offset;
                        int len = col_meta->len;
                        switch(col_meta->type){
                                case TYPE_INT:{
                                        int max_value = INT_MIN;
                                        for(auto &rid: rids_){
                                                auto rec = fh_->get_record(rid, nullptr);
                                                char *data = rec->data + offset;
                                                int value = *reinterpret_cast<int *>(data);
                                                if(value > max_value){
                                                        max_value = value;
                                                }
                                        }
                                        val_.set_int(max_value);
                                        val_.init_raw(sizeof(int));
                                        // return nullptr;
                                        break;
                                }
                                case TYPE_FLOAT:{
                                       
                                        float max_value = FLT_MIN;
                                        for(auto &rid: rids_){
                                                auto rec = fh_->get_record(rid, nullptr);
                                                char *data = rec->data + offset;
                                                float value = *reinterpret_cast<float *>(data);
                                                if(value > max_value){
                                                        max_value = value;
                                                }
                                        }
                                        val_.set_float(max_value);
                                        val_.init_raw(sizeof(float));
                                        // return nullptr;
                                        break;
                                }
                                case TYPE_STRING:{
                                        auto rec_begin = fh_->get_record(*rids_.begin(), nullptr);
                                        std::string max_value(rec_begin->data+offset,len);
                                        for (auto &rid : rids_) {
                                                auto rec = fh_->get_record(rid, nullptr);
                                                char *data = rec->data + offset;
                                                std::string value(data, len);
                                                if (value > max_value) {
                                                        max_value.assign(value);
                                                }
                                        }
                                        
                                        val_.set_str(max_value);
                                        val_.init_raw(sizeof(max_value));

                                        // return nullptr;
                                        break;
                                }
                                case TYPE_BIGINT :
                                case TYPE_DATETIME :
                                        throw RMDBError("In execution_aggregation, we don't implement bigint and datetime type");
                                default: 
                                        throw InvalidTypeError();
                        }
                        break;
                }
                case AG_MIN:
                {
                        auto col_meta = tab_.get_col(aggre_meta_.tabcol_.col_name);
                        int offset = col_meta->offset;
                        int len = col_meta->len;
                        switch(col_meta->type){
                                case TYPE_INT:{
                                        int min_value = INT_MAX;
                                        for(auto &rid: rids_){
                                                auto rec = fh_->get_record(rid, nullptr);
                                                char *data = rec->data + offset;
                                                int value = *reinterpret_cast<int *>(data);
                                                if(value < min_value){
                                                        min_value = value;
                                                }
                                        }
                                        val_.set_int(min_value);
                                        val_.init_raw(sizeof(int));
                                        // return nullptr;
                                        break;

                                }
                                case TYPE_FLOAT:{
                                       
                                        float min_value = FLT_MAX;
                                        for(auto &rid: rids_){
                                                auto rec = fh_->get_record(rid, nullptr);
                                                char *data = rec->data + offset;
                                                float value = *reinterpret_cast<float *>(data);
                                                if(value < min_value){
                                                        min_value = value;
                                                }
                                        }
                                        val_.set_float(min_value);
                                        val_.init_raw(sizeof(float));
                                        // return nullptr;
                                        break;
                                }
                                case TYPE_STRING:{
                                        auto rec_begin = fh_->get_record(*rids_.begin(), nullptr);
                                        std::string min_value(rec_begin->data+offset,len);
                                        for (auto &rid : rids_) {
                                                auto rec = fh_->get_record(rid, nullptr);
                                                char *data = rec->data + offset;
                                                std::string value(data, len);
                                                if (value < min_value) {
                                                        min_value.assign(value);
                                                }
                                        }
                                        
                                        val_.set_str(min_value);
                                        val_.init_raw(sizeof(min_value));

                                        // return nullptr;
                                        break;
                                }
                                case TYPE_BIGINT :
                                case TYPE_DATETIME :
                                        throw RMDBError("In execution_aggregation, we don't implement bigint and datetime type");
                                default: 
                                        throw InvalidTypeError();

                        }
                        break;
                }
                }
        }

        void nextTuple() override {
        // assert(!prev_->is_end());
                is_end_ = true;
        }

        bool is_end() const override { 
                return is_end_; 
        };

        const std::vector<ColMeta> &cols() const {
                return output_cols_;
        };


};