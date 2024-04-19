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
#include "common/join_buffer.h"

class LoadExecutor : public AbstractExecutor {
   private:
    TabMeta tab_;                   // 表的元数据
    std::string file_path_;         // 需要读取的数据文件路径
    RmFileHandle *fh_;              // 表的数据文件句柄
    std::string tab_name_;          // 表名称
    Rid rid_;                       // 插入的位置，由于系统默认插入时不指定位置，因此当前rid_在插入后才赋值
    SmManager *sm_manager_;

    // 用join buffer缓存数据
    std::unique_ptr<JoinBuffer> record_buffer_;
    // 文件句柄
    std::unique_ptr<std::ifstream> ifs_;

   public:
    LoadExecutor(SmManager *sm_manager, const std::string &tab_name, const std::string &file_path, Context *context) {
        sm_manager_ = sm_manager;
        tab_ = sm_manager_->db_.get_table(tab_name);
        file_path_ = file_path;
        tab_name_ = tab_name;
        fh_ = sm_manager_->fhs_.at(tab_name).get();
        context_ = context;
        ifs_ = std::make_unique<std::ifstream>(file_path_);

        std::vector<std::string> col_names;
        // 读取第一行数据(列名)
        std::string line;
        std::getline(*ifs_, line);
        std::stringstream ss(line);
        std::string token;
        while (std::getline(ss, token, ',')) {  // 使用逗号作为分隔符
            // 
            col_names.push_back(token);
        }
        // insert加X锁
        if(context_ != nullptr) {
            context_->lock_mgr_->lock_exclusive_on_table_wait_time(context_->txn_, fh_->GetFd());
        }
    };

    ~LoadExecutor() {
        ifs_->close();
    }
    std::unique_ptr<RmRecord> Next() override {
        // 1. 先把数据读取到内存
        std::vector<std::unique_ptr<RmRecord>> records;
        std::vector<Rid> rids;
        std::string line;
        while(std::getline(*ifs_, line)) {
            // 取一行数据后，生成record并插入
            auto rec = std::make_unique<RmRecord>(fh_->get_file_hdr().record_size);
            std::stringstream ss(line);
            for(auto &col : tab_.cols) {
                std::string token;
                std::getline(ss, token, ',');
                Value value;
                switch (col.type)
                {
                    case TYPE_INT: {
                        value.set_int(std::stoi(token));
                        break;
                    }
                    case TYPE_BIGINT: {
                        // 可能有bug，long int和int64_t还是有区别
                        value.set_bigint(std::stol(token));
                        break;
                    }
                    case TYPE_DATETIME: {
                        value.set_datetime(token);
                        break;
                    }
                    case TYPE_FLOAT: {
                        value.set_float(std::stof(token));
                        break;
                    }
                    case TYPE_STRING: {
                        value.set_str(token);
                        break;
                    }
                    default:{
                        throw InternalError("类型错误");
                        break;
                    }
                }
                value.init_raw(col.len);
                memcpy(rec->data + col.offset, value.raw->data, col.len);
            }
            records.push_back(std::move(rec));
        }

        // 不传context进去，因为已经加了表级的X锁
        fh_->massive_insert(&records, &rids, nullptr);
        assert(rids.size() == records.size());

        // 批量插入索引
        for(auto &index : tab_.indexes) {
            auto ih = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
            for(size_t i = 0; i < records.size(); i++) {
                char key[index.col_tot_len];
                int offset = 0;
                for(int j = 0; j < index.col_num; ++j) {
                    memcpy(key + offset, records[i]->data + index.cols[j].offset, index.cols[j].len);
                    offset += index.cols[j].len;
                }
                ih->insert_entry(key, rids[i], context_->txn_);
            }
        }
        // for(size_t i = 0; i < tab_.indexes.size(); ++i) {
        //     auto& index = tab_.indexes[i];
        //     auto ih = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
        //     // 获取keys
        //     std::vector<char *> keys;
        //     for(size_t j = 0; j < records.size(); j++) {
        //         char *key = new char[index.col_tot_len];
        //         int offset = 0;
        //         for(int k = 0; k < index.col_num; ++k) {
        //             memcpy(key + offset, records[j]->data + index.cols[k].offset, index.cols[k].len);
        //             offset += index.cols[k].len;
        //         }
        //         keys.push_back(key);
        //     }
        //     ih->massive_insert(keys, rids, context_->txn_);
        //     for(size_t i = 0; i < keys.size(); i++) {
        //         delete [] keys[i];
        //     }
        // }


        // rid_ = fh_->insert_record(rec.data, nullptr, tab_name_);


            // // 将插入操作写入日志
            // Transaction* txn = context_->txn_;
            // InsertLogRecord* insert_log_rcd = new InsertLogRecord(txn->get_transaction_id(),rec,rid_,tab_name_);
            // insert_log_rcd->prev_lsn_ = txn->get_prev_lsn();
            // txn->set_prev_lsn(context_->log_mgr_->add_log_to_buffer(insert_log_rcd));
            // // 更新page的lsn
            // fh_->update_page_lsn(rid_.page_no,insert_log_rcd->lsn_);
            // // 别忘了delete
            // delete insert_log_rcd;

            // load data不打事务日志了
            // // 加入transcation的write_set，用于abort
            // TableWriteRecord *write_rcd = new TableWriteRecord(WType::INSERT_TUPLE,tab_name_,rid_,rec);
            // context_->txn_->append_table_write_record(write_rcd);
            
            // // Insert into index
            // for(size_t i = 0; i < tab_.indexes.size(); ++i) {
            //     auto& index = tab_.indexes[i];
            //     auto ih = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
            //     char key[index.col_tot_len];
            //     int offset = 0;
            //     for(int i = 0; i < index.col_num; ++i) {
            //         memcpy(key + offset, rec.data + index.cols[i].offset, index.cols[i].len);
            //         offset += index.cols[i].len;
            //     }
            //     ih->insert_entry(key, rid_, context_->txn_);

            //     // IndexWriteRecord *index_rcd = new IndexWriteRecord(WType::INSERT_TUPLE,tab_name_,rid_,key,index.col_tot_len);
            //     // context_->txn_->append_index_write_record(index_rcd);
            // }

            return nullptr;
        }

    Rid &rid() override { return rid_; }
};