<div align="center">
<img src="RMDB.jpg"  width=25%  /> 
</div>



全国大学生计算机系统能力大赛数据库管理系统赛道，以培养学生“数据库管理系统内核实现”能力为目标。本次比赛为参赛队伍提供数据库管理系统代码框架RMDB，参赛队伍在RMDB的基础上，设计和实现一个完整的关系型数据库管理系统，该系统要求具备运行TPC-C基准测试（TPC-C是一个面向联机事务处理的测试基准）常用负载的能力。

RMDB由中国人民大学数据库教学团队开发，平台、赛题和测试用例等得到了全国大学生计算机系统能力大赛数据库管理系统赛道技术委员会的支持和审核。系统能力大赛专家组和[101计划数据库系统课程](http://101.pku.edu.cn/courseDetails?id=DC767C683D697417E0555943CA7634DE)工作组给予了指导。

## 面试准备

### Q 讲一讲RMDB中实现的几个算子

1. 聚合算子

   在do_planner的时候，会和update、insert、delete生成算子一样先根据index_mode 生成scan算子，然后在外面套一个aggregate算子。因为是对所有查询出来数据的统计操作，可以直接采用物化模型，将查询出来的结果做统一存储。（流水线模型走的是for(beginTuple(); !is_end(); nextTuple())，物化模型走的是Next())。当时好像用流水线模型是超时的，原因分析可能是buffer pool的置换和虚函数调用耗时有关，因为查出来放在内存的数据会在buffer pool中给pin住，所以不会被置换出去。

2. sort算子

   sort算子属于select语句流水线模型中的一环，位于join算子和project算子之间。sort算子感觉必须要用物化视图的方式比较合适，在beginTuple()中已经通过(prev->beginTuple(); !prev->is_end(); prev->nextTuple())把所有元组都加载到内存中来了。通过自定义的compare比较器（传入orderBy的字段数组，从左到右逐一遍历比较，如果相等就continue比较下一个，否则就根据该字段是升序还是降序，返回true或者false）。sort算子的nextTuple()是让一个count值+1。当count到达limit之后，is_end变为true。next()函数是返回内存中排好序的数组的count下标的元素。

3. nestedloop_join算子

   ```c++
   void beginTuple() override {
           left_->beginTuple();
           right_->beginTuple();
           // 
           isend = false;
           find_next_valid_tuple();
       }
   ```

   find_next_valid_tuple()这个函数比较关键

   ```c++
   // 找到下一个符合fed_cond的tuple
       void find_next_valid_tuple() {
           assert(!is_end());
           while(!left_->is_end()) {
               // 取两边的record
               auto left_record = left_->Next();
               while(!right_->is_end()) {
                   auto right_record = right_->Next();
                   // 检查是否符合fed_cond
                   bool is_fit = true;
                   // 判断left_record和right_record是否满足join条件
                   ...... 此处省略若干行代码
                   if(is_fit) {
                       return ;
                   }else {
                       right_->nextTuple();
                   }
               }
               left_->nextTuple();
               right_->beginTuple();   
           }
           isend = true;
       }
   ```

   可以看出来，外层循环是!left->is_end()，内层循环是 !right->is_end()。如果left_record和right_record满足条件了直接return，否则right->nextTuple()。一旦right->is_end()了则内层当前循环结束，left->nextTuple()，right->beginTuple()。left->is_end()之后则isend变量值为true，结束。

   beginTuple和nextTuple都会调用find_next_Tuple找到一下组满足条件的record。这里有个细节需要注意，nextTuple()中本应该是调用一次right->nextTuple()，如果right已经是end了，则left->nextTuple()，重新right->beginTuple()。调用Next()的时候当然已经是指向下一个满足条件的left和right了，调用Next()得到左右record拼接起来返回即可。

4. block_nestedloop_join算子

   该算子本质上是nestedloop_join增加了两个基于Buffer Pool缓存之上的缓存，因为如果只使用一个Buffer Pool的话，缓存就这么一个且容量小，且因为频繁发生页的置换，所以速度会很慢。加了两个join buffer缓存后，left算子和right算子先填满整个buffer，然后直接在内存中进行join即可，减少了很多IO开销，当然这是一种用空间换时间的办法，在决赛中应该适当调整大小。在初赛中该题有正确性样例和时间样例限制。

   为了实现该算子增加了两个类：ExecutorWithJoinBuffer和JoinBuffer，ExecutorWithJoinBuffer内置孩子算子（left or right算子）和joinBuffer，作用只是为了装填一个Buffer，其beginBuffer()和nextBuffer()都是装填一个新的buffer。is_end()即join_buffer为空。JoinBuffer类控制比较容易，内置record数组，beginTuple，nextTuple和is_end都是用当前一个cur指针控制数组下标位置。

   下面是beginTuple()

   ```c++
   void beginTuple() override {
           // 1. 初始化 left blocks, right blocks, left join buffer, right join buffer
           left_blocks_->beginBuffer();
           right_blocks_->beginBuffer();
           left_join_buffer_ = left_blocks_->Next();
           right_join_buffer_ = std::move(right_blocks_->Next());
           (*left_join_buffer_)->beginTuple();
           (*right_join_buffer_)->beginTuple();
           // 2. 初始化 isend并开启循环寻找第一个符合要求的节点
           isend = false;
           find_next_valid_tuple();
       }
   ```

   再看find_next_valid_tuple()

   ```c++
   // 找到下一个符合fed_cond的tuple
       void find_next_valid_tuple() {
           // 1.开启循环体，四重循环
           while(!left_blocks_->is_end()) {
               while(!right_blocks_->is_end()) {
                   while(!(*left_join_buffer_)->is_end()) {
                       auto left_record = (*left_join_buffer_)->get_record();
                       while(!(*right_join_buffer_)->is_end()) {
                           // 寻找符合条件的tuple
                           auto right_record = (*right_join_buffer_)->get_record();
                          
                           ...... 省略若干行代码
                           // 如果符合要求，则返回
                           if(is_fit) {
                               return ;
                           }
                           (*right_join_buffer_)->nextTuple();
                       }
                       // right_join_buffer_遍历完毕
                       // left_join_buffer_+1, right_join buffer重新开始遍历
                       (*left_join_buffer_)->nextTuple();
                       (*right_join_buffer_)->beginTuple();
                   }
                   // left join buffer和right join buffer都遍历完毕
                   // left join buffer重新开始遍历，right join buffer刷新
                   (*left_join_buffer_)->beginTuple();
                   right_blocks_->nextBuffer();
                   right_join_buffer_ = right_blocks_->Next();
                   (*right_join_buffer_)->beginTuple();
               }
               // right blocks中的所有buffer都遍历过一遍了
               // left blocks + 1， right blocks重新遍历
               left_blocks_->nextBuffer();
               left_join_buffer_ = left_blocks_->Next();
               (*left_join_buffer_)->beginTuple();
               right_blocks_->beginBuffer();
               right_join_buffer_ = right_blocks_->Next();
               (*right_join_buffer_)->beginTuple();
           }
       }
   ```

   5. index_scan算子

      索引扫描算子相比于全表扫描算子，索引算子根据条件可以先确定一个范围。所以一开始先遍历conds的时候，可以根据conds的值初始化上下边界，比如 a > 10可以min更改10，IxScan算子根据边界初始化。

      beginTuple()和nextTuple()都是通过IxScan算子，while(!scan->is_end())找到下一个符合条件的元组，判断是否符合要求，符合要求就停止。

   6. index_scan_mode算子

      实现这个是因为针对决赛测试中的部分SQL，其where条件中第一个条件导致无法走索引（因为决赛当时的环境是跑固定的一堆SQL，所以所建立的索引也是给定的，不能自己创建）。所以为了让那些SQL走索引，而那些SQL恰好第一个条件的基数不高，所以实现了skip scan让它依然可以走索引。

      在判断是否可以选择走索引的时候，满足最左匹配原则（从左往右遍历符合条件）则走一般索引扫描，如果是满足从后往前连续都有索引，然后再往前又都没索引，则使用skip_scan算子。skip_scan算子相比于普通index_scan算子的区别就是要枚举前面没有索引部分的取值，剩余索引部分根据前面的范围填充，然后for(int i = min_a; i <= max_a; ++i) 每一个都创建一个IxScan扫描。采用了物化模型直接将符合条件的在beginTuple全查出来了，nextTuple只是得到下一个元组。

### Q 讲一讲B+ Tree存储引擎的实现

![B+tree](/B+tree.jpg)

首先看B+tree的节点结构

重点实现的函数有查找key，插入key和删除key

1. 查找key：
2. 插入key：先查找到该节点所在的叶子节点，给该叶子插入pair。如果插入后叶子节点的size不变则说明插入了重复的key（只支持唯一索引）。如果插入后的size已经超过了最大支持的size，则分裂该叶子节点。分裂即创建一个新节点放在它后面，如果分裂的是叶子节点还要维持其下一个节点next_leaf指针。其他操作是叶子节点和非叶子节点一样的（这也是选这种B+tree结构的好处）。然后将原节点分一半到新的节点中去。执行完split之后要接着进行insert_into_parent的操作，与insert的区别这里面会判断原节点是否已经是根节点（没有父节点了），如果是那样则创建新的根节点，其他跟insert是一样的，还是会继续递归向上尝试插入的。
3. 删除key：先查找到节点所在的叶子节点，删除对应key的pair。删除之后调用coalesce_or_redistribute判断是否要进行合并或者重分配，如果是根节点单独处理，否则判断如果删除后小于总结点数的1/2，则判断是进行合并还是重分配。获取其兄弟节点（默认前一个节点，如果是第一个节点则获取它的后一个节点），如果两个节点总和大于一个节点能承受的最大节点，那么从隔壁节点借一个pair过来仍然符合B+树的结构。如果两个节点总和小于的话，则可以将这两个节点合并。这里如果重分配的话不需要再在父节点继续递归了，如果是合并的话还要继续在父节点递归。

### Q 如何实现Buffer Pool的？

page有自己的page_id，这个是在刷盘的时候自增的。对于buffer pool，先来看看它的数据结构：

```c++
class BufferPoolManager {
   private:
    size_t pool_size_;      // buffer_pool中可容纳页面的个数，即帧的个数
    Page *pages_;           // buffer_pool中的Page对象数组，在构造空间中申请内存空间，在析构函数中释放，大小为BUFFER_POOL_SIZE
    std::unordered_map<PageId, frame_id_t, PageIdHash> page_table_; // 帧号和页面号的映射哈希表，用于根据页面的PageId定位该页面的帧编号
    std::list<frame_id_t> free_list_;   // 空闲帧编号的链表
}
```

对于pages是其固定的对象数组，其帧的个数固定的，为pool size。每个帧都有自己的帧号。每个page的page_id与frame_id的映射关系，由一个hash函数计算：(x.fd << 16) | x.page_no，通过将`fd`左移16位，可以将`fd`的高16位与`page_no`的低16位组合在一起，增加了哈希函数的散列性。这样可以降低不同`PageId`对象产生相同哈希值的概率。page_table即将pageId与frameId建立映射，以方便快速查找。

接下来是几个函数的实现：

1. fetch_page：首先查找page_table里面是否包含该page，如果包含直接取出来，并pin住（pin count++，因为可能有多个pin住）。如果没有，则需要从磁盘中读取目标页放到对应的page。这里需要找到有没有空闲的frame，如果有直接用该frame，如果没有则需要置换一个出去。具体置换使用相应的replacer。获取后将该page从磁盘读取数据，更新page_table固定目标页pin_count = 1。

2. update_page：如果是脏页则先刷盘，dirty置为false，然后更新page_table，pin_count变为0。
3. delete_page：page_table找不到该页直接返回。如果pin count没有减到0，返回false。然后写入磁盘，pin_count置为0，删除page_table，加入free_list。

### Q BitMap如何加速查找的？

bitMap用于快速查找一个page中的某个record。

来看一下如何初始化的：

```c++
// 初始化file header
        RmFileHdr file_hdr{};
        file_hdr.record_size = record_size;
        file_hdr.num_pages = 1;
        file_hdr.first_free_page_no = RM_NO_PAGE;
        // We have: sizeof(hdr) + (n + 7) / 8 + n * record_size <= PAGE_SIZE
        file_hdr.num_records_per_page =
            (BITMAP_WIDTH * (PAGE_SIZE - 1 - (int)sizeof(RmFileHdr)) + 1) / (1 + record_size * BITMAP_WIDTH);
        file_hdr.bitmap_size = (file_hdr.num_records_per_page + BITMAP_WIDTH - 1) / BITMAP_WIDTH;

        // 将file header写入磁盘文件（名为file name，文件描述符为fd）中的第0页
        // head page直接写入磁盘，没有经过缓冲区的NewPage，那么也就不需要FlushPage
        disk_manager_->write_page(fd, RM_FILE_HDR_PAGE, (char *)&file_hdr, sizeof(file_hdr));
```

所以bitmap是page->data的一部分，大小即page的records数量  + (BIT_MAP - 1)  / BITMAP_WIDTH（8）

BITMAP是一个类，作为rm_page_handle的友元类。其含有几个重要的函数：

```c++
static constexpr int BITMAP_WIDTH = 8;
static constexpr unsigned BITMAP_HIGHEST_BIT = 0x80u;  // 128 (2^7)

// pos位 置1
    static void set(char *bm, int pos) { bm[get_bucket(pos)] |= get_bit(pos); }

    // pos位 置0
    static void reset(char *bm, int pos) { bm[get_bucket(pos)] &= static_cast<char>(~get_bit(pos)); }

    // 如果pos位是1，则返回true
    static bool is_set(const char *bm, int pos) { return (bm[get_bucket(pos)] & get_bit(pos)) != 0; }

private:
    static int get_bucket(int pos) { return pos / BITMAP_WIDTH; }

    static char get_bit(int pos) { return BITMAP_HIGHEST_BIT >> static_cast<char>(pos % BITMAP_WIDTH); }
```

bitmap充分运用了每一个char一个字节的每一个bit，如果把每个字节作为一个桶，getBucket函数得到它在哪一个字节里面，get_bit()函数得到它在这个bit的哪一个位上，具体逻辑即10000000向右移动余数位。

set()函数让那一位或，因为相同得1。reset()即get_bit()取反&该字节，比如10110101 & 11101111即让第三位变为0，其他位不变。is_set()函数即&判断是否不全为0。

什么情境下会用到bitMap加速查找？在一个page里面插入一个新的record的时候要快速查找一个空闲的位置，这个时候就要用bitmap。再如普通扫描算子遍历record的时候也是根据bitMap为1的位来快速遍历的，避免了很多不需要的遍历。

### Q 锁管理器和多粒度锁如何实现的？

lock_manager结构

```c++
std::mutex latch_;      // 用于锁表的并发
    std::unordered_map<LockDataId, LockRequestQueue> lock_table_;   // 全局锁表

    // 加锁检查
    bool lock_check(Transaction *txn);
    // 释放锁检查
    bool unlock_check(Transaction *txn);
```

LockDataId即是具体的上锁对象，可以是表也可以是字段，字段的话要用rid来表示

log_manager维护了一个全局的LockDataId->上锁队列。

上锁队列的结构

```c++
class LockRequestQueue {
    public:
        std::list<LockRequest> request_queue_;  // 加锁队列
        std::condition_variable cv_;            // 条件变量，用于唤醒正在等待加锁的申请，在no-wait策略下无需使用
        GroupLockMode group_lock_mode_ = GroupLockMode::NON_LOCK;   // 加锁队列的锁模式
    };
```

2PL算法实现如下：通过事务状态控制，每次给表或者行加锁的时候先通过lock_check()做判断，一开始事务默认是default状态，如果是default状态就会改为growing状态，代表开始加锁，如果是growing状态返回true，代表正常加锁。如果是shrinking状态则抛出异常，因为解锁时不能加锁。如果事务已经是commit或者是abort状态则直接返回false。解锁也是一样的，GROWING抛异常，COMITTED和ABORTED返回false。

每次上锁先执行lock_check()，有6种函数：给字段上共享或排他锁，给表上共享或排他或者IX或者IS锁。根据锁的排斥矩阵确定是否可以获取锁成功，注意获取锁成功后会加入到事务的锁集合中。因为实现的是strict 2PL机制，所以在提交事务和回滚事务的时候需要一起释放所有的锁。

### Q 事务管理器是如何实现的？

循环接受客户端的char，得到当前SQL。每次执行前判断当前事务状态，如果是COMMITED或者ABORT就删除当前事务的内存，创建新的事务（调用transaction::begin）。 在执行SQL的时候catch异常，如果是Abort异常则捕获之后执行transaction::abort。如果txn_mode是false则说明是单条语句的，直接执行transcation::commit。

现在再来看事务管理器的几个函数：

1. begin:新建一个事务对象，一个事务对象包括锁对象集合，线程id，事务模式txn_mode，prev_lsn，事务状态，undo log(乱序文件和B+索引的)等信息。其中新创建出来的事务的状态位DEFAULT状态，prev_lsn = -1 , txn_mode为false。创建事务只要写入redo log。
2. commit：提交事务，首先根据strict 2 PL协议需要释放所有锁，写入redo log日志，并将日志缓存log buffer立即刷盘，修改状态。将undo_log清空。
3. abort：遍历所有的undo_log对乱序文件和索引文件的，开始逐条相反操作。当然根据strict 2 PL协议还是要释放所有的锁，并且写入redo log日志，并将log buffer立即落盘，然后修改事务状态为abort。最后将undo_log清空。

### Q redo log是如何实现的？

不同操作的redo log日志继承自相同的接口，包括了当前事务的lsn号，prev_lsn即事务的前一条日志创建的lsn号，当前事务的id。每个log有序列化和反序列化方法。 log_manager有一个全局的global lsn号用来全局递增的，他还有个日志缓冲区和已经持久化的lsn号，在log manager刷盘的时候更新lsn为global lsn - 1。

每次插入或者更新或者删除record的时候都会写入redo log记录，执行以下流程：

1. redo log的prev_lsn改为txn的prev_lsn
2. 将该日志通过log_manager刷入缓存，刷入的时候根据维护的全局global_lsn更新log的lsn。
3. 更新txn的prev_lsn为当前刚刷入缓存的lsn
4. 更新page的lsn为当前log的lsn

故障恢复的实现：

```c++
std::unordered_map<txn_id_t, lsn_t> undo_latest_lsn_map_;       // 要undo的事务的最后一条lsn
    std::vector<lsn_t> redo_list_;                                  // 需要redo的log集合
    std::unordered_map<lsn_t, int> lsn_offset_map_;                 // lsn与offset的对应map
    std::unordered_map<lsn_t, int> lsn_len_map_; 
```

故障恢复需要经历三个阶段：analyze() -> redo() -> undo()

1. analyze阶段：通过disk_manager读取log，对于每一个log解析出来，对于begin事务的日志则undo_latest_lsn_map的txn 的map，对于commit和abort都从undo_latest_map删除。对于其他插入更新删除等的日志都加入undo_latest_lsn_map和redo_list中。
2. redo阶段（重做未落盘的操作）：对于所有的redo_list的，对于该日志，找到该日志操作的page，如果该page的lsn小于当前日志的lsn，则重做，否则跳过即可。
3. undo阶段（回滚未完成的事务）：对于还存在于undo_map的log执行回滚操作（做相反的操作，这里用的是redo log做的相反操作而不是undo log）