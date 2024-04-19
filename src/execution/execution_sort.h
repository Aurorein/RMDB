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

class SortExecutor : public AbstractExecutor
{
private:
    std::unique_ptr<AbstractExecutor> prev_;
    ColMeta cols_; // 框架中只支持一个键排序，需要自行修改数据结构支持多个键排序
    size_t tuple_num_;
    bool is_desc_;
    std::vector<size_t> used_tuple;
    RmRecord current_tuple;
    std::vector<std::unique_ptr<RmRecord>> tuples_;

    // 增加order_cols，保存多个排序键
    std::vector<OrderByCol> order_cols_;
    // 增加limit_，限制输出元组个数
    int limit_;
    int count_;

public:
    SortExecutor(std::unique_ptr<AbstractExecutor> prev, std::vector<OrderByCol> order_cols, int limit)
    {
        prev_ = std::move(prev);
        // order_cols初始化
        order_cols_ = order_cols;

        // 增加limit，限制输出元组个数
        // !!!!!!：：如果limit == -1，则表示没有limit !!!
        limit_ = limit;
        count_ = 0;
        // 这两句我注释掉了，is_desc不需要用到了，包含在order_cols里了，但是cols_就是ColMeta还需要自己获取，需要补充一下这里的代码
        // cols_ = prev_->get_col_offset(sel_cols);
        // is_desc_ = is_desc;
        tuple_num_ = 0;
        used_tuple.clear();
    }

    // 自定义比较类

class RmCompare : AbstractExecutor {
    std::vector<OrderByCol> order_cols_;
    std::unique_ptr<AbstractExecutor> &prev_;
    RmCompare(std::vector<OrderByCol> order_cols, std::unique_ptr<AbstractExecutor> &prev) :
        order_cols_(order_cols), prev_(prev) { }
    bool operator()(const std::unique_ptr<RmRecord> &lhs, const std::unique_ptr<RmRecord> &rhs) {
        for (auto &tab_col : order_cols_)
        {
            
            auto cols = prev_->cols();
            auto col_meta = *get_col(cols, tab_col.tabcol);

            auto left_value = fetch_value(lhs, col_meta);
            auto right_value = fetch_value(rhs, col_meta);
            bool flag = compare_value(left_value, right_value, {OP_GE});

            // int flag = ix_compare(current_tuple.data+col_meta->offset, buf, col_meta->type, col_meta->len);
            // 后面符合条件的元组根据排序顺序 (降序 / 升序)
            if ((!flag && tab_col.is_desc) || (flag && !tab_col.is_desc))
            {
                return true;
            }else if(flag == 0){
                continue;
            }else{
                return false;
            }
        }
        return true;
    }

    Rid &rid() override { return _abstract_rid; }

};
    // bool rmcompare(const std::unique_ptr<RmRecord> &lhs, const std::unique_ptr<RmRecord> &rhs) {
    //     for (auto &tab_col : order_cols_)
    //     {
            
    //         auto cols = prev_->cols();
    //         auto col_meta = *get_col(cols, tab_col.tabcol);

    //         auto left_value = fetch_value(lhs, col_meta);
    //         auto right_value = fetch_value(rhs, col_meta);
    //         bool flag = compare_value(left_value, right_value, {OP_GE});

    //         // int flag = ix_compare(current_tuple.data+col_meta->offset, buf, col_meta->type, col_meta->len);
    //         // 后面符合条件的元组根据排序顺序 (降序 / 升序)
    //         if ((!flag && tab_col.is_desc) || (flag && !tab_col.is_desc))
    //         {
    //             return true;
    //         }else if(flag == 0){
    //             continue;
    //         }else{
    //             return false;
    //         }
    //     }
    //     return true;
    // }

    void beginTuple() override
    {

        // bool is_start = true; // 第一个符合条件的元组
        used_tuple.clear();
        // char *current; //  当前最适合的元组
        // size_t curr_index = 0, perfect_index = 0;
        for( prev_->beginTuple(); !prev_->is_end(); prev_->nextTuple()){

            auto tuple = prev_ -> Next();
            tuples_.push_back(std::move(tuple));
        }


        // 自定义比较类
        class RmCompare : AbstractExecutor {
        public:
            std::vector<OrderByCol> order_cols_;
            std::unique_ptr<AbstractExecutor> &prev_;
            RmCompare(std::vector<OrderByCol> order_cols, std::unique_ptr<AbstractExecutor> &prev) :
                order_cols_(order_cols), prev_(prev) { }
            bool operator()(const std::unique_ptr<RmRecord> &lhs, const std::unique_ptr<RmRecord> &rhs) {
                for (auto &tab_col : order_cols_)
                {
                    
                    auto cols = prev_->cols();
                    auto col_meta = *get_col(cols, tab_col.tabcol);

                    auto left_value = fetch_value(lhs, col_meta);
                    auto right_value = fetch_value(rhs, col_meta);
                    bool flag = compare_value(left_value, right_value, {OP_GT});
                    bool is_equal = compare_value(left_value, right_value, {OP_EQ});
                    // int flag = ix_compare(current_tuple.data+col_meta->offset, buf, col_meta->type, col_meta->len);
                    // 后面符合条件的元组根据排序顺序 (降序 / 升序)
                    if(is_equal) {
                        continue;
                    }else if((!flag && tab_col.is_desc) || (flag && !tab_col.is_desc))
                    {
                        return false;
                    }else{
                        return true;
                    }
                }
                return true;
            }

            Rid &rid() override { return _abstract_rid; }

            std::unique_ptr<RmRecord> Next() override { return std::make_unique<RmRecord>(); }
        };

        std::sort(tuples_.begin(), tuples_.end(), RmCompare(order_cols_, prev_));
        count_ = 0;
        // for (auto it = tuples_.begin();it != tuples_.end();it++)
        // {
        //     auto temp_tuple = *it;

        //     if (is_start)
        //     {
        //         // current = buf;
        //         perfect_index = curr_index;
        //         is_start = false;
        //         current_tuple = temp_tuple;
        //     }
        //     else
        //     {

        //         for (auto &tab_col : order_cols_)
        //         {
                    
        //             auto cols = prev_->cols();
        //             auto col_meta = get_col(cols, tab_col.tabcol);

        //             char *buf = temp_tuple.data + col_meta->offset;
        //             int flag = ix_compare(current_tuple.data+col_meta->offset, buf, col_meta->type, col_meta->len);
        //             // 后面符合条件的元组根据排序顺序 (降序 / 升序)
        //             if ((flag < 0 && tab_col.is_desc) || (flag > 0 && !tab_col.is_desc))
        //             {
        //                 // current = buf;
        //                 perfect_index = curr_index;
        //                 current_tuple = temp_tuple;
        //                 break;
        //             }else if(flag == 0){
        //                 continue;
        //             }else{
        //                 break;
        //             }
        //         }
        //     }            
        //     // auto temp_tuple = prev_->Next();
        //     // char *buf = temp_tuple->data + cols_.offset;
        //     // 如果是第一个符合条件的元组

        //     tuple_num_++;
        //     curr_index++;
        // }
        // used_tuple.emplace_back(perfect_index);
        
    }

    void nextTuple() override
    {
        count_++;
        // bool is_start = true;                     // 是否是第一个符合条件的元组
        // size_t curr_index = 0, perfect_index = 0; // 元组的索引号，当前最适合元组的索引号
        // // char *current;
        // for (auto it = tuples_.begin();it != tuples_.end();it++)
        // {
        //     // 检查当前的tuple不在used中
        //     if (std::find(used_tuple.begin(), used_tuple.end(), curr_index) != used_tuple.end())
        //     {
        //         curr_index++;
        //         continue;
        //     }
        //     auto temp_tuple = std::move(*it);
        //     // char *buf = temp_tuple->data + cols_.offset;
        //     if (is_start)
        //     {
        //         // 如果是第一个符合条件的元组
        //         // current = buf;
        //         perfect_index = curr_index;
        //         is_start = false;
        //         current_tuple = temp_tuple;
        //     }
        //     else
        //     {
        //         for (auto &tab_col : order_cols_)
        //         {
        //             auto cols = prev_->cols();
        //             auto col_meta = get_col(cols, tab_col.tabcol);

        //             char *buf = temp_tuple.data + col_meta->offset;
        //             int flag = ix_compare(current_tuple.data+col_meta->offset, buf, col_meta->type, col_meta->len);
        //             // 后面符合条件的元组根据排序顺序 (降序 / 升序)
        //             if ((flag < 0 && tab_col.is_desc) || (flag > 0 && !tab_col.is_desc))
        //             {
        //                 // current = buf;
        //                 perfect_index = curr_index;
        //                 current_tuple = temp_tuple;
        //                 break;
        //             }else if(flag == 0){
        //                 continue;
        //             }else{
        //                 break;
        //             }
        //         }
        //     }
        //     curr_index++;
        // }
        // tuple_num_--;
        // count_++;
        // used_tuple.emplace_back(perfect_index);
    }

    std::unique_ptr<RmRecord> Next() override
    {
        return std::move(tuples_[count_]);
    }

    Rid &rid() override { return _abstract_rid; }

    // 实现cols方法，上层需要调用
    const std::vector<ColMeta> &cols() const
    {
        return prev_->cols();
    };

    bool is_end() const override { 
        if(limit_ != -1 && count_ >= limit_) {
            return true;
        }
        return count_ >= (int)tuples_.size(); 
    };
};
