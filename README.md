
QP-Trie来自于 https://fanf.livejournal.com/137283.html 

原始实现只支持`\0`结尾的数据，所以一个key不会是另一个key的前缀。参考 https://github.com/sdleffler/qp-trie-rs 进行改进，支持任意字符串数据。

## 一些实现细节，与原始C版本以及`qp-trie-rs`的对比

1. 在`qp-trie-rs`中, `head`叶子节点位于数组最开始，导致了就算没有`head`叶子节点，也要额外留出数组下标0的位置。本实现改为在数组末尾存放，减少一部分空间消耗。
2. 使用了c++17的`variant`代替`union`。跟原始C语言版比起来，每个节点增加了额外字节消耗(`variant`中用于标识类型的字段)
3. 我之前仿照`qp-trie-rs`，在`branch`中用一个`vector`存放`twigs`。这样会额外引入16字节的空间消耗。后来还是用了原版c实现的方式，自己搞了个简单的动态数组，为了简化实现，从位域`index`中借用10比特作为动态数组的`size`和`capacity`。这样只能存储最长2^36字节的字符串，不过也够用了。现在一个`branch`固定消耗16字节，和C
版本一样
4. 由于使用了`variant`，节点不再是`trivially_copyable`，所以动态数组不能用`realloc`以及`memove`，与C版本比性能会有一定损失
5. 原始C语言版本只支持C风格字符串`char*`作为key。这里为了通用性，只要是满足`std::is_convertible<std::string_view, T>`的类型`T`都可以作为key。比如`std::string`或者是实现了`operator std::string_view()`的自定义类型。但是代价是增加了叶子结点的大小(由于实际存储的`Node`是`variant<Leaf, Branch>`，所有节点大小都会增加)。
6. 据上所述，以`char*`作为key时，节点大小最小(在我的机器上：`sizeof(Leaf)==8`, `sizeof(Branch)==16`, `sizeof(Node)==max(8, 16)+8==24`)
7. `char*`为key和原版比起来，存在一个问题：在内部逻辑中，统一用`std::string_view`处理key，会根据key构造一些`std::string_view`的临时对象。`char*`转`string_view`会调用`strlen`，所以和`std::string`作为key相比，性能会有一定损失。
8. 使用了hat-trie(https://github.com/Tessil/hat-trie)的测试程序进行初步测试。数据用了wikipedia 20200801的标题集合, shuf打乱顺序。和hat-trie作者给出的结果类似，qp-trie在这种大量前缀概率高的短字符串场景下表现不理想。

    a. 和哈希表类结构相比： 插入耗时是`std::unordered_map`和`htrie_map`的3倍左右，查询耗时是他们的5倍左右，内存消耗比`std::unordered_map`的略低，是`hat-trie`的3倍多。原因是这种大量前缀概率高的短字符串场景下，qp-trie的压缩数组带来的空间收益不明显，而且动态数组会频繁扩容，产生大量的小内存的申请和释放。由于前缀概率高，导致分支层次变得很深，相比哈希表，一次查询会引起更多次寻址

    b. 和平衡树类结构相比： 插入和查询性能比`std::map`略高，内存消耗大致类似。

    没有测试前缀查找功能，不过从原理上看，qp-trie的前缀查找性能应该会有不错的表现。

## TODO

- [x] 实现迭代器（目前好像有bug）
- [x] 实现非递归遍历，优化前缀查找性能
- [x] 实现KV功能
- [ ] `set`和`map`类型定义
- [ ] `Trie`的拷贝构造和移动构造
- [ ] 更详细的benchmark
