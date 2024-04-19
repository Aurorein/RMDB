/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "log_recovery.h"

/**
 * @description: analyze阶段，需要获得脏页表（DPT）和未完成的事务列表（ATT）
 */
void RecoveryManager::analyze() {
    
    // 开辟一个用于从磁盘读取log到内存的一个空间
    char log_buffer[LOG_BUFFER_SIZE];
    // 遍历到的buffer的下标
    int buffer_offset = 0;
    // 遍历到的磁盘上log文件的下标
    int file_offset = 0;
    // 每次读取的字节数
    int read_bytes;
    while(1){
        memset(log_buffer,0,LOG_BUFFER_SIZE);
        buffer_offset = 0;
        read_bytes = disk_manager_->read_log(log_buffer,LOG_BUFFER_SIZE,file_offset);
        
        if(read_bytes<0){
            throw InternalError("read log file error!");
        }

        while(buffer_offset+OFFSET_LOG_TOT_LEN+sizeof(int)<read_bytes){
            int log_size = *reinterpret_cast<uint32_t*>(log_buffer+buffer_offset+OFFSET_LOG_TOT_LEN);
            if(buffer_offset + log_size > read_bytes){

                break;
            }else{
                LogType cur_log_type = *reinterpret_cast<LogType*>(log_buffer+buffer_offset+OFFSET_LOG_TYPE);
                switch(cur_log_type){
                    case LogType::begin:{
                        BeginLogRecord *begin_log_rcd = new BeginLogRecord();
                        
                        begin_log_rcd->deserialize(log_buffer+buffer_offset);
                        lsn_offset_map_[begin_log_rcd->lsn_] = file_offset;
                        lsn_len_map_[begin_log_rcd->lsn_] = begin_log_rcd->log_tot_len_;

                        buffer_offset+=begin_log_rcd->log_tot_len_;
                        file_offset+=begin_log_rcd->log_tot_len_;
                        // 如果有begin，需要加入到undo map
                        undo_latest_lsn_map_[begin_log_rcd->log_tid_] = begin_log_rcd->lsn_;
                        
                        // 释放begin_log_rcd
                        delete begin_log_rcd;
                        break;
                    }
                    case LogType::commit:{
                        CommitLogRecord* commit_log_rcd = new CommitLogRecord();

                        commit_log_rcd->deserialize(log_buffer+buffer_offset);
                        lsn_offset_map_[commit_log_rcd->lsn_] = file_offset;
                        lsn_len_map_[commit_log_rcd->lsn_] = commit_log_rcd->log_tot_len_;
                        buffer_offset+=commit_log_rcd->log_tot_len_;
                        file_offset+=commit_log_rcd->log_tot_len_;

                        // 将该事务从undo map里面删掉
                        undo_latest_lsn_map_.erase(commit_log_rcd->log_tid_);
                        
                        // 释放commit_log_rcd
                        delete commit_log_rcd;
                        break;
                    }
                    case LogType::ABORT:{
                        AbortLogRecord* abort_log_rcd = new AbortLogRecord();

                        abort_log_rcd->deserialize(log_buffer+buffer_offset);
                        lsn_offset_map_[abort_log_rcd->lsn_] = file_offset;
                        lsn_len_map_[abort_log_rcd->lsn_] = abort_log_rcd->log_tot_len_;

                        buffer_offset += abort_log_rcd->log_tot_len_;
                        file_offset += abort_log_rcd->log_tid_;

                        // 将该事务从undo map里面删掉
                        undo_latest_lsn_map_.erase(abort_log_rcd->log_tid_);


                        // 释放abort log rcd
                        delete abort_log_rcd;

                        break;
                    }
                    case LogType::INSERT:{
                        InsertLogRecord* insert_log_rcd = new InsertLogRecord();

                        insert_log_rcd->deserialize(log_buffer+buffer_offset);
                        lsn_offset_map_[insert_log_rcd->lsn_] = file_offset;
                        lsn_len_map_[insert_log_rcd->lsn_] = insert_log_rcd->log_tot_len_;

                        buffer_offset += insert_log_rcd->log_tot_len_;
                        file_offset += insert_log_rcd->log_tot_len_;

                        // 加入redo list
                        // 加入undo map
                        undo_latest_lsn_map_[insert_log_rcd->log_tid_] = insert_log_rcd->lsn_;
                        redo_list_.emplace_back(insert_log_rcd->lsn_);

                        // 释放insert_log_rcd
                        delete insert_log_rcd;

                        break;
                    }
                    case LogType::DELETE:{
                        DeleteLogRecord* delete_log_rcd = new DeleteLogRecord();

                        delete_log_rcd->deserialize(log_buffer+buffer_offset);
                        lsn_offset_map_[delete_log_rcd->lsn_] = file_offset;
                        lsn_len_map_[delete_log_rcd->lsn_] = delete_log_rcd->log_tot_len_;

                        buffer_offset += delete_log_rcd->log_tot_len_;
                        file_offset += delete_log_rcd->log_tot_len_;

                        // 加入redo list
                        // 加入undo map
                        undo_latest_lsn_map_[delete_log_rcd->log_tid_] = delete_log_rcd->lsn_;
                        redo_list_.emplace_back(delete_log_rcd->lsn_);

                        // 释放delete_log_rcd
                        delete delete_log_rcd;

                        break;
                    }
                    case LogType::UPDATE:{
                        UpdateLogRecord* update_log_rcd = new UpdateLogRecord();

                        update_log_rcd->deserialize(log_buffer+buffer_offset);
                        lsn_offset_map_[update_log_rcd->lsn_] = file_offset;
                        lsn_len_map_[update_log_rcd->lsn_] = update_log_rcd->log_tot_len_;

                        buffer_offset += update_log_rcd->log_tot_len_;
                        file_offset += update_log_rcd->log_tot_len_;

                        // 加入redo list
                        // 加入undo map
                        undo_latest_lsn_map_[update_log_rcd->log_tid_] = update_log_rcd->lsn_;
                        redo_list_.emplace_back(update_log_rcd->lsn_);

                        // 释放update_log_rcd
                        delete update_log_rcd;

                        break;
                    }
                    case LogType::CREATE_TABLE:{
                        CreateTableRecord* ctb_log_rcd = new CreateTableRecord();

                        ctb_log_rcd->deserialize(log_buffer+buffer_offset);
                        lsn_offset_map_[ctb_log_rcd->lsn_] = file_offset;
                        lsn_len_map_[ctb_log_rcd->lsn_] = ctb_log_rcd->log_tot_len_;

                        buffer_offset += ctb_log_rcd->log_tot_len_;
                        file_offset += ctb_log_rcd->log_tot_len_;

                        // 加入redo list
                        // 加入undo map
                        undo_latest_lsn_map_[ctb_log_rcd->log_tid_] = ctb_log_rcd->lsn_;
                        redo_list_.emplace_back(ctb_log_rcd->lsn_);

                        // 释放ctb_log_rcd
                        delete ctb_log_rcd;

                        break;
                    }
                    default:break;
                }
                 
            }
        }

        if(read_bytes<LOG_BUFFER_SIZE){
            break;
        }
    }
}

/**
 * @description: 重做所有未落盘的操作
 */
void RecoveryManager::redo() {
    for(lsn_t lsn : redo_list_){
        int log_len = lsn_len_map_[lsn];
        char* log_buf = new char[log_len];
        memset(log_buf,0,log_len);
        disk_manager_->read_log(log_buf,log_len,lsn_offset_map_[lsn]);
        LogType log_type = *(LogType *)log_buf;
        switch(log_type){
            case LogType::INSERT:{
                InsertLogRecord* insert_log_rcd = new InsertLogRecord();

                insert_log_rcd->deserialize(log_buf);
                std::string tab_name(insert_log_rcd->table_name_,insert_log_rcd->table_name_size_);
                auto rm_file_hdl = sm_manager_->fhs_[tab_name].get();
                // 这个file_hdr没用到
                // auto fil_hdr = rm_file_hdl->get_file_hdr();

                // TODO 这样是对的嘛，如果这个时候数量要少的话，可以判断一定这个操作是没有执行过的？（因为日志是按顺序的）
                // if(rm_file_hdl->get_file_hdr().num_pages <= insert_log_rcd->rid_.page_no){
                //     rm_file_hdl->insert_record(insert_log_rcd->insert_value_.data,nullptr,insert_log_rcd->table_name_);
                // }else{
                //     auto page_hdl = rm_file_hdl->fetch_page_handle(insert_log_rcd->rid_.page_no);
                //     if(page_hdl.page->get_page_lsn() < insert_log_rcd->lsn_){
                //         rm_file_hdl->insert_record(insert_log_rcd->rid_,insert_log_rcd->insert_value_.data);
                //     }
                //     buffer_pool_manager_->unpin_page(page_hdl.page->get_page_id(),true);
                // }
                 if(rm_file_hdl->get_file_hdr().num_pages <= insert_log_rcd->rid_.page_no){
                    page_id_t new_page_no = rm_file_hdl->create_new_page_handle().page->get_page_id().page_no;
                    assert(new_page_no == insert_log_rcd->rid_.page_no);
                    rm_file_hdl->insert_record(insert_log_rcd->rid_,insert_log_rcd->insert_value_.data);
                }else{
                    auto page_hdl = rm_file_hdl->fetch_page_handle(insert_log_rcd->rid_.page_no);
                    if(page_hdl.page->get_page_lsn() < insert_log_rcd->lsn_){
                        rm_file_hdl->insert_record(insert_log_rcd->rid_,insert_log_rcd->insert_value_.data);
                    }
                    buffer_pool_manager_->unpin_page(page_hdl.page->get_page_id(),true);

                std::cout<<"redo insert "<<insert_log_rcd->lsn_<<std::endl;
                    
                }
                // rm_file_hdl->insert_record(insert_log_rcd->insert_value_.data,nullptr,insert_log_rcd->table_name_);
                
                
                // 释放insert log rcd的内存
                delete insert_log_rcd;

                break;
            }
            case LogType::DELETE:{
                DeleteLogRecord* delete_log_rcd = new DeleteLogRecord();

                delete_log_rcd->deserialize(log_buf);
                std::string tab_name(delete_log_rcd->table_name_,delete_log_rcd->table_name_size_);
                auto rm_file_hdl = sm_manager_->fhs_[tab_name].get();
                auto page_hdl = rm_file_hdl->fetch_page_handle(delete_log_rcd->rid_.page_no);
                if(page_hdl.page->get_page_lsn() < delete_log_rcd->lsn_){
                    rm_file_hdl->delete_record(delete_log_rcd->rid_,nullptr);
                }
                buffer_pool_manager_->unpin_page(page_hdl.page->get_page_id(),true);

                std::cout<<"redo delete "<<delete_log_rcd->lsn_<<std::endl;

                // 释放delete log rcd的内存
                delete delete_log_rcd;
                break;
            }
            case LogType::UPDATE:{
                UpdateLogRecord* update_log_rcd = new UpdateLogRecord();

                update_log_rcd->deserialize(log_buf);
                std::string tab_name(update_log_rcd->table_name_,update_log_rcd->table_name_size_);
                auto rm_file_hdl = sm_manager_->fhs_[tab_name].get();
                auto page_hdl = rm_file_hdl->fetch_page_handle(update_log_rcd->rid_.page_no);
                if(page_hdl.page->get_page_lsn() < update_log_rcd->lsn_){
                    rm_file_hdl->update_record(update_log_rcd->rid_,update_log_rcd->after_value_.data,nullptr);
                }
                buffer_pool_manager_->unpin_page(page_hdl.page->get_page_id(),true);

                std::cout<<"redo update "<<update_log_rcd->lsn_<<std::endl;

                // 释放update log rcd的内存
                delete update_log_rcd;
                break;
            }
            case LogType::CREATE_TABLE:{
                
                CreateTableRecord* ctb_log_rcd = new CreateTableRecord();

                ctb_log_rcd->deserialize(log_buf);
                // 如果有table则表示操作已经写过磁盘了
                if(!sm_manager_->db_.is_table(ctb_log_rcd->table_name_)){
                    sm_manager_->create_table(ctb_log_rcd->table_name_,ctb_log_rcd->col_defs_,nullptr);
                }
                

                // 将创建表的操作记入日志

                // 释放ctb_log_rcd的内存
                delete ctb_log_rcd;
                break;
            }
            default:break;
        }

    }
}

/**
 * @description: 回滚未完成的事务
 */
void RecoveryManager::undo() {
    for(auto it = undo_latest_lsn_map_.begin();it!= undo_latest_lsn_map_.end();++it){
        lsn_t lsn = it->second;
        while(lsn != INVALID_LSN){
            int log_len = lsn_len_map_[lsn];
            char* log_buf = new char[log_len];
            memset(log_buf,0,log_len);
            disk_manager_->read_log(log_buf,log_len,lsn_offset_map_[lsn]);
            LogType log_type = *(LogType *)log_buf;
            switch(log_type){
                case LogType::begin:{
                    BeginLogRecord* begin_log_rcd = new BeginLogRecord();
                    begin_log_rcd->deserialize(log_buf);
                    lsn = begin_log_rcd->prev_lsn_;

                    std::cout<<"undo begin "<<begin_log_rcd->lsn_<<std::endl;
                    break;
                }
                case LogType::INSERT:{
                    InsertLogRecord* insert_log_rcd = new InsertLogRecord();

                    insert_log_rcd->deserialize(log_buf);
                    std::string tab_name(insert_log_rcd->table_name_,insert_log_rcd->table_name_size_);
                    auto rm_file_hdl = sm_manager_->fhs_[tab_name].get();
                    rm_file_hdl->delete_record(insert_log_rcd->rid_,nullptr);
                    lsn = insert_log_rcd->prev_lsn_;

                    std::cout<<"undo insert "<<insert_log_rcd->lsn_<<std::endl;
                    break;

                }
                case LogType::DELETE:{
                    DeleteLogRecord* delete_log_rcd = new DeleteLogRecord();

                    delete_log_rcd->deserialize(log_buf);
                    std::string tab_name(delete_log_rcd->table_name_,delete_log_rcd->table_name_size_);
                    auto rm_file_hdl = sm_manager_->fhs_[tab_name].get();
                    rm_file_hdl->insert_record(delete_log_rcd->rid_,delete_log_rcd->delete_value_.data);

                    lsn = delete_log_rcd->prev_lsn_;

                    std::cout<<"undo delete "<<delete_log_rcd->lsn_<<std::endl;
                    break;
                }
                case LogType::UPDATE:{
                    UpdateLogRecord* update_log_rcd = new UpdateLogRecord();

                    update_log_rcd->deserialize(log_buf);

                    // 先将table name转化为string
                    std::string tab_name(update_log_rcd->table_name_,update_log_rcd->table_name_size_);
                    auto rm_file_hdl = sm_manager_->fhs_[tab_name].get();
                    rm_file_hdl->update_record(update_log_rcd->rid_, update_log_rcd->before_value_.data, nullptr);

                    lsn = update_log_rcd->prev_lsn_;

                    std::cout<<"undo update "<<update_log_rcd->lsn_<<std::endl;
                    break;
                }
                case LogType::CREATE_TABLE:{
                    CreateTableRecord* ctb_log_rcd = new CreateTableRecord();
                    
                    ctb_log_rcd->deserialize(log_buf);
                    sm_manager_->drop_table(ctb_log_rcd->table_name_,nullptr);

                    break;
                }
                default:break;
            }
        }
    }

}