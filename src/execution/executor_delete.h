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

class DeleteExecutor : public AbstractExecutor {
   private:
    TabMeta tab_;                   // 表的元数据
    std::vector<Condition> conds_;  // delete的条件
    RmFileHandle *fh_;              // 表的数据文件句柄
    std::vector<Rid> rids_;         // 需要删除的记录的位置
    std::string tab_name_;          // 表名称
    SmManager *sm_manager_;

   public:
    DeleteExecutor(SmManager *sm_manager, const std::string &tab_name, std::vector<Condition> conds,
                   std::vector<Rid> rids, Context *context) {
        sm_manager_ = sm_manager;
        tab_name_ = tab_name;
        tab_ = sm_manager_->db_.get_table(tab_name);
        fh_ = sm_manager_->fhs_.at(tab_name).get();
        conds_ = conds;
        rids_ = rids;
        context_ = context;

        // delete加IX锁
        if(context_ != nullptr) {
            context_->lock_mgr_->lock_IX_on_table_wait_time(context_->txn_, fh_->GetFd());
        }
    }


    // conds_条件已经在scan里用过了，现在直接用rids_即可
    std::unique_ptr<RmRecord> Next() override {
        
        for(auto rid : rids_) {
            // 1. 获取rec
            auto rec = fh_->get_record(rid, context_);
            
            // 2. 删除record
            // 将对table heap的delete操作写入日志
            Transaction* txn = context_->txn_;
            DeleteLogRecord* delete_log_rcd = new DeleteLogRecord(txn->get_transaction_id(), *rec, rid, tab_name_);
            delete_log_rcd->prev_lsn_ = txn->get_prev_lsn();
            context_->log_mgr_->add_log_to_buffer(delete_log_rcd);
            txn->set_prev_lsn(context_->log_mgr_->add_log_to_buffer(delete_log_rcd));

            // 删除record
            fh_->delete_record(rid, context_);

            // page的更新lsn
            fh_->update_page_lsn(delete_log_rcd->rid_.page_no,delete_log_rcd->lsn_);

            // 别忘了delete
            delete delete_log_rcd;

            // 将delete操作加入transaction的write_set里面，用于abort
            TableWriteRecord *write_record = new TableWriteRecord(WType::DELETE_TUPLE,tab_name_,rid, *rec);
            context_->txn_->append_table_write_record(write_record);
            
            // 3.删除index
            // 这里与index插入是相似的，都是对每一个的index，得到对应col的key，调用delete_entry(key,context_->txn_);        
            for(size_t i = 0; i < tab_.indexes.size(); ++i) {
                auto& index = tab_.indexes[i];
                auto ih = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
                char key[index.col_tot_len];
                int offset = 0;
                for(int j = 0; j < index.col_num; ++j) {
                    memcpy(key + offset, rec->data + index.cols[j].offset, index.cols[j].len);
                    offset += index.cols[j].len;
                }
                ih->delete_entry(key, context_->txn_);

                IndexWriteRecord *index_rcd = new IndexWriteRecord(WType::DELETE_TUPLE,tab_name_,rid,key,index.col_tot_len);
                context_->txn_->append_index_write_record(index_rcd);
            }

        }
        return nullptr;
    }

    Rid &rid() override { return _abstract_rid; }
};