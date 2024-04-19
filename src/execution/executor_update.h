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

class UpdateExecutor : public AbstractExecutor {
   private:
    TabMeta tab_;       // 表的元数据
    std::vector<Condition> conds_;  // 更新的条件
    RmFileHandle *fh_;      // 表的数据文件句柄
    std::vector<Rid> rids_;     // 更新的位置
    std::string tab_name_;      // 表名称
    std::vector<SetClause> set_clauses_; // 需要更新的字段和值
    SmManager *sm_manager_;

   public:
    UpdateExecutor(SmManager *sm_manager, const std::string &tab_name, std::vector<SetClause> set_clauses,
                   std::vector<Condition> conds, std::vector<Rid> rids, Context *context) {
        sm_manager_ = sm_manager;
        tab_name_ = tab_name;
        set_clauses_ = set_clauses;
        tab_ = sm_manager_->db_.get_table(tab_name);
        fh_ = sm_manager_->fhs_.at(tab_name).get();
        conds_ = conds;
        rids_ = rids;
        context_ = context;

        // update加IX锁
        if(context_ != nullptr) {
            context_->lock_mgr_->lock_IX_on_table_wait_time(context_->txn_, fh_->GetFd());
        }
    }
    std::unique_ptr<RmRecord> Next() override {
        for(auto &rid : rids_) {
            // 1. 取old record
            auto old_rec = fh_->get_record(rid, context_);

            // 2. 计算new record
            auto new_rec = std::make_unique<RmRecord>(fh_->get_file_hdr().record_size);
            memcpy(new_rec->data, old_rec->data, fh_->get_file_hdr().record_size);
            for(auto &set_clause: set_clauses_) {
                auto lhs_col = tab_.get_col(set_clause.lhs.col_name);
                // 将new rec中的相关值设置为新的值
                // 判断rhs是表达式，还是普通的value
                if(set_clause.is_val) {
                    memcpy(new_rec->data + lhs_col->offset, set_clause.rhs.raw->data, lhs_col->len);
                }else {
                    Value new_value;
                    // 取rhs col value
                    char *data = old_rec->data + lhs_col->offset;
                    switch (lhs_col->type)
                    {
                        case TYPE_INT:
                        {
                            int sum = 0;
                            sum += *reinterpret_cast<int *>(data);
                            sum += set_clause.rhs.int_val;
                            new_value.set_int(sum);
                            break;
                        }
                        case TYPE_FLOAT:
                        {
                            float sum = 0;
                            sum += *reinterpret_cast<float *>(data);
                            sum += set_clause.rhs.float_val;
                            new_value.set_float(sum);
                            break;
                        }
                        default:
                        {
                            throw InternalError("Invalid Type");
                            break;
                        }
                    }
                    new_value.init_raw(lhs_col->len);
                    memcpy(new_rec->data + lhs_col->offset, new_value.raw->data, lhs_col->len);
                }
            }
            
            // 3. 检查是否满足索引的一致性
            for(size_t i = 0; i < tab_.indexes.size(); ++i) {
                auto& index = tab_.indexes[i];
                // update之前和之后的key，如果相同，则跳过本次索引的检测（因为不检测也知道不会重复）
                char old_key[index.col_tot_len];
                char new_key[index.col_tot_len];
                int offset = 0;
                for(int j = 0; j < index.col_num; ++j) {
                    memcpy(old_key + offset, old_rec->data + index.cols[j].offset, index.cols[j].len);
                    memcpy(new_key + offset, new_rec->data + index.cols[j].offset, index.cols[j].len);
                    offset += index.cols[j].len;
                }
                // update之前和之后的key，如果相同，则跳过本次索引的检测（因为不检测也知道不会重复）
                if(memcmp(old_key, new_key, index.col_tot_len) == 0) {
                    continue;
                }
                auto ih = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
                if(ih->get_value(new_key, nullptr, context_->txn_)) {
                    throw InternalError("Non-unique index!");
                }
            }

            // 4. 满足一致性，update record 将update操作写入transaction里面
            Transaction* txn = context_->txn_;
            UpdateLogRecord* update_log_rcd = new UpdateLogRecord(txn->get_transaction_id(), *old_rec, *new_rec,rid,tab_name_);
            update_log_rcd->prev_lsn_ = txn->get_prev_lsn();
            txn->set_prev_lsn(context_->log_mgr_->add_log_to_buffer(update_log_rcd));
            // 对磁盘修改
            fh_->update_record(rid, new_rec->data, context_);
            // 更新page的lsn
            fh_->update_page_lsn(update_log_rcd->rid_.page_no,update_log_rcd->lsn_);
            //别忘了delete
            delete update_log_rcd;
            // 将update操作写入transaction的write set里面用于abort操作
            TableWriteRecord *write_rcd = new TableWriteRecord(WType::UPDATE_TUPLE,tab_name_,rid, *old_rec);
            context_->txn_->append_table_write_record(write_rcd);

            // 5. update index
            // 将rid插入在内存中更新后的新的record的cols对应key的index
            for(size_t i = 0; i < tab_.indexes.size(); ++i) {
                auto& index = tab_.indexes[i];
                // update之前和之后的key，如果相同，则跳过本次索引的更新（因为不用更新）
                char old_key[index.col_tot_len];
                char new_key[index.col_tot_len];
                int offset = 0;
                for(int j = 0; j < index.col_num; ++j) {
                    memcpy(old_key + offset, old_rec->data + index.cols[j].offset, index.cols[j].len);
                    memcpy(new_key + offset, new_rec->data + index.cols[j].offset, index.cols[j].len);
                    offset += index.cols[j].len;
                }
                // update之前和之后的key，如果相同，则跳过本次索引的更新（因为不用更新）
                if(memcmp(old_key, new_key, index.col_tot_len) == 0) {
                    continue;
                }
                // 先删除原来的old_key，再插入新的new_key
                auto ih = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
                ih->delete_entry(old_key, context_->txn_);
                ih->insert_entry(new_key, rid, context_->txn_);
                // 写入两条record
                IndexWriteRecord *delete_index_rcd = new IndexWriteRecord(WType::DELETE_TUPLE,tab_name_,rid, old_key,index.col_tot_len);
                context_->txn_->append_index_write_record(delete_index_rcd);
                IndexWriteRecord *insert_index_rcd = new IndexWriteRecord(WType::INSERT_TUPLE,tab_name_,rid, new_key,index.col_tot_len);
                context_->txn_->append_index_write_record(insert_index_rcd);
            }


            // for(size_t i = 0; i < tab_.indexes.size(); ++i) {
            //     auto& index = tab_.indexes[i];
            //     auto ih = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
            //     char key[index.col_tot_len];
            //     int offset = 0;
            //     for(size_t i = 0; i < index.col_num; ++i) {
            //         memcpy(key + offset, rec->data + index.cols[i].offset, index.cols[i].len);
            //         offset += index.cols[i].len;
            //     }
            //     ih->delete_entry(key, context_->txn_);

            //     IndexWriteRecord *index_rcd = new IndexWriteRecord(WType::DELETE_TUPLE,tab_name_,rid,key,index.col_tot_len);
            //     context_->txn_->append_index_write_record(index_rcd);
            // }

            // ！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！
            // 这里声明了new_rec表示新的rec，之前声明的rec为old_rec（恢复时用到）
            // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

            // 更新record file
           

            // 没有事务恢复部分，我们先用异常处理record和索引的一致性问题
            // try {
               
            // }catch(InternalError &error) {
            //     // 1. 恢复record
            //     fh_->update_record(rid, rec->data, context_);
            //     // 恢复索引
            //     // 2. 恢复所有的index
            //     for(size_t i = 0; i < tab_.indexes.size(); ++i) {
            //         auto& index = tab_.indexes[i];
            //         auto ih = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
            //         char key[index.col_tot_len];
            //         int offset = 0;
            //         for(size_t i = 0; i < index.col_num; ++i) {
            //             memcpy(key + offset, rec->data + index.cols[i].offset, index.cols[i].len);
            //             offset += index.cols[i].len;
            //         }
            //         ih->insert_entry(key, rid, context_->txn_);
            //     }

            //     // 3. 继续抛出异常
            //     throw InternalError("Non-unique index!");
            // }

        }
        return nullptr;
    }

    Rid &rid() override { return _abstract_rid; }
};