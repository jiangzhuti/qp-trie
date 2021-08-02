#ifndef TRIE_HPP
#define TRIE_HPP

#include <utility>
#include <vector>
#include <variant>
#include <string_view>
#include <optional>
#include <functional>
#include <type_traits>
#include <stack>
#include <new>
#include <algorithm>

#include <cassert>
#include <cstdlib>

namespace jzt {
namespace detail {

namespace trait {


template <class T>
struct is_pair : std::false_type {};

template <class T1, class T2>
struct is_pair<std::pair<T1, T2>> : std::true_type {};

}

namespace qp {


template <typename DataType, bool IsMap>
struct DataTraintsImpl
{
    typedef DataType key_type;
    typedef DataType mapped_type;
    typedef DataType value_type;

    template <class T>
    static const key_type& get_key(const T& value)
    {
        return value;
    }

    template <class T>
    static value_type& get_value(T& value)
    {
        return value;
    }
};

template <typename DataType>
struct DataTraintsImpl<DataType, true>
{
    typedef typename std::remove_cv<typename DataType::first_type>::type key_type;
    typedef typename DataType::second_type mapped_type;
    typedef DataType value_type;

    template <class T>
    static const key_type& get_key(const T& value)
    {
        return value.first;
    }

    template <class T>
    static value_type& get_value(T& value)
    {
        return value;
    }
};

template <typename DataType, bool IsMap>
struct DataTraints
{
    typedef DataTraintsImpl<DataType, IsMap> DataTraintsType;

    typedef typename DataTraintsType::key_type    key_type;
    typedef typename DataTraintsType::mapped_type mapped_type;
    typedef typename DataTraintsType::value_type  value_type;

    template <class T>
    static const key_type& get_key(const T& value)
    {
        return DataTraintsType::get_key(value);
    }

    template <class T>
    static value_type& get_value(T& value)
    {
        return DataTraintsType::get_value(value);
    }
};

using TwigIndexType = uint8_t;
using NybbleType = uint8_t;
using NybbleIndexType = uint64_t;
static constexpr uint8_t NybbleHead = 0xFF;

template <class DataType, bool IsMap>
struct Leaf
{
    using DataTraintsType = DataTraints<DataType, IsMap>;
    using key_type = typename DataTraintsType::key_type;
    using mapped_type = typename DataTraintsType::mapped_type;
    using value_type = typename DataTraintsType::value_type;

    static_assert (!IsMap || jzt::detail::trait::is_pair<DataType>::value, "DataType must be std::pair");

    static_assert (std::is_convertible_v<key_type, std::string_view>, "key_type must be able to converted to std::string_view");

    static_assert (std::is_nothrow_move_constructible_v<key_type>, "key_type must be nothrow move constructible");

    static_assert (!IsMap || std::is_nothrow_move_constructible_v<mapped_type>, "mapped_type must be nothrow move constructible");

    static_assert (std::is_nothrow_move_assignable_v<key_type>, "key_type must be nothrow move assignable");

    static_assert (!IsMap || std::is_nothrow_move_assignable_v<mapped_type>, "mapped_type must be nothrow move assignable");

    template <typename ...ValueArgs, std::enable_if_t<IsMap && std::is_constructible_v<value_type, ValueArgs...>, bool> = true>
    Leaf(ValueArgs&& ...args) : data(std::forward<ValueArgs>(args)...) {}

    //unused
    template <typename KeyArg, typename ...MappedArgs,
              std::enable_if_t<IsMap && std::is_constructible_v<key_type, KeyArg> && std::is_constructible_v<mapped_type, MappedArgs...>, bool> = true>
    Leaf(KeyArg&& key, MappedArgs&& ...args) : data(std::piecewise_construct, std::forward_as_tuple(key), std::forward_as_tuple(args)...) {}


    template <typename ...KeyArgs, std::enable_if_t<!IsMap && std::is_constructible_v<key_type, KeyArgs...>, bool> = true>
    Leaf(KeyArgs&& ...args) : data(std::forward<KeyArgs>(args)...) {}

    Leaf(Leaf&& leaf) : data(std::move(leaf.data)) {}
    Leaf(const Leaf& leaf) = delete;
    Leaf& operator= (Leaf&& leaf)
    {
        data = std::move(leaf.data);
        return *this;
    }

    const key_type& get_key() const
    {
        return DataTraintsType::get_key(data);
    }
    value_type& get_value()
    {
        return DataTraintsType::get_value(data);
    }
    DataType& get_data()
    {
        return data;
    }
    std::optional<NybbleIndexType> find_mismatch(std::string_view sv) const
    {
        std::string_view key(get_key());
        for (NybbleIndexType i = 0; i < sv.size() && i < key.size(); i++) {
            NybbleType diff = (uint8_t)sv[i] ^ (uint8_t)key[i];
            if (diff != 0) {
                if ((diff & 0x0F) == 0) {
                    return {1 + i * 2};
                } else {
                    return {i * 2};
                }
            }
        }
        if (sv.size() == key.size()) {
            return {};
        }
        return {std::min(sv.size(), key.size()) * 2};
    }
private:
    DataType data;
};

static NybbleType nybble_at(std::string_view key, NybbleIndexType ni)
{
    uint64_t bi = ni / 2;
    if (bi >= key.size()) return NybbleHead;
    uint8_t b = key[bi];
    if ((ni & 1) == 0) {
        return b & 0x0F;
    } else {
        return b >> 4;
    }
}

template <typename DataType, bool IsMap>
class Node;

template <typename DataType, bool IsMap>
class Branch {
    using NodeType = Node<DataType, IsMap>;
    using LeafType = Leaf<DataType, IsMap>;
    NodeType* twigs;
    uint64_t
        head : 1, //head flag
        capacity : 5, //twig capacity [0, 17]
        size : 5, //twig size [0, 16] + head => [0, 17]
        index : 37, //nybble index
        bitmap : 16; //twigs bitmap

private:

    template <typename ...Args, std::enable_if_t<std::is_constructible_v<NodeType, Args...>, bool> = true>
    void twig_init_one(Args&& ...args)
    {
        size = 1;
        capacity = 2;
        twigs = static_cast<NodeType*>(std::malloc(capacity * sizeof (NodeType)));
        if (twigs == nullptr) {
            throw std::bad_alloc();
        }
        new (&twigs[0]) NodeType(std::forward<Args>(args)...);
    }
    template <typename ...Args, std::enable_if_t<std::is_constructible_v<NodeType, Args...>, bool> = true>
    void twig_expand_emplace_at(TwigIndexType idx, Args&& ...args)
    {
        assert(capacity <= 17);
        int new_capacity = std::min((int)(capacity * 1.5), 17);
        NodeType* new_twigs = static_cast<NodeType*>(std::malloc(new_capacity * sizeof(NodeType)));
        if (new_twigs == nullptr) {
            throw std::bad_alloc();
        }
        int i = 0, j = 0;
        for (; i < idx; i++) {
            new (&new_twigs[j]) NodeType(std::move(twigs[i]));
            j++;
        }
        j++;
        for (; i < size; i++) {
            new (&new_twigs[j]) NodeType(std::move(twigs[i]));
            j++;
        }
        new (&new_twigs[idx]) NodeType(std::forward<Args>(args)...);
        for (i = 0; i < size; i++) {
            twigs[i].~NodeType();
        }
        std::free(twigs);
        twigs = new_twigs;
        size++;
        capacity = new_capacity;
    }
    template <typename ...Args, std::enable_if_t<std::is_constructible_v<NodeType, Args...>, bool> = true>
    void twig_emplace_at(TwigIndexType idx, Args&& ...args)
    {
        assert(size < 17);
        assert(idx <= size);
        if (size + 1 > capacity) {
            twig_expand_emplace_at(idx, std::forward<Args>(args)...);
        } else {
            if (idx < size) {
                int i = size;
                new (&twigs[i]) NodeType(std::move(twigs[i - 1]));
                i--;
                for (; i > idx; i--) {
                    twigs[i] = std::move(twigs[i - 1]);
                }
                twigs[idx] = NodeType(std::forward<Args>(args)...);
            } else {
                new (&twigs[idx]) NodeType(std::forward<Args>(args)...);
            }
            size++;
        }
    }
    template <typename ...Args, std::enable_if_t<std::is_constructible_v<NodeType, Args...>, bool> = true>
    void twig_emplace_back(Args&& ...args)
    {
        assert(size < 17);
        twig_emplace_at(size, std::forward<Args>(args)...);
    }

    void twig_erase_at(TwigIndexType idx)
    {
        assert(size > 1);
        assert(idx < size);
        for (int i = idx + 1; i < size; i++) {
            twigs[i - 1] = std::move(twigs[i]);
        }
        twigs[size - 1].~NodeType();
        size--;
    }
public:
    Branch(uint64_t i, LeafType&& leaf) : index(i), bitmap(0)
    {
        NybbleType n = nybble_at(leaf.get_key(), i);
        twig_init_one(std::move(leaf)); //init size. capacity, twigs
        if (n == NybbleHead) {
            head = true;
        } else {
            head = false;
            bitmap |= (1 << n);
        }
    }
    Branch(Branch&& branch) : twigs(branch.twigs), head(branch.head), capacity(branch.capacity), size(branch.size), index(branch.index), bitmap(branch.bitmap)
    {
        branch.twigs = nullptr;
    }
    Branch(const Branch& branch) = delete;

    Branch& operator= (Branch&& branch)
    {
        if (twigs != nullptr) {
            for (int i = 0; i < size; i++) {
                twigs[i].~NodeType();
            }
            std::free(twigs);
        }
        twigs = branch.twigs;
        head = branch.head;
        capacity = branch.capacity;
        size = branch.size;
        index = branch.index;
        bitmap = branch.bitmap;
        branch.twigs = nullptr;
        return *this;
    }
    ~Branch()
    {
        if (twigs != nullptr) {
            for (int i = 0; i < size; i++) {
                twigs[i].~NodeType();
            }
            std::free(twigs);
        }
    }

    NybbleIndexType nybble_index() const
    {
        return index;
    }
    bool has_head() const
    {
        return head;
    }
    NodeType* get_head()
    {
        assert(head);
        return &(twigs[size - 1]);
    }
    void remove_head()
    {
        assert(head);
        assert(size > 0);
        twigs[size - 1].~NodeType();
        size--;
        head = false;
    }
    NodeType* twig(TwigIndexType idx)
    {
        assert(idx < size);
        return &(twigs[idx]);
    }
    TwigIndexType twig_count() const
    {
        assert((int)__builtin_popcount(bitmap) + head == size);
        return size;
    }
    bool has_twig(NybbleType n)
    {
        assert(n != NybbleHead);
        return bitmap & (1 << n);
    }
    NybbleType twig_nybble(std::string_view key) const
    {
        return nybble_at(key, index);
    }
    TwigIndexType twig_index(NybbleType n) const
    {
        assert(n != NybbleHead);
        return __builtin_popcount(bitmap & ((1 << n) - 1));
    }

    void twig_insert(LeafType&& leaf, NybbleType n)
    {
        if (n == NybbleHead) {
            assert(!head);
            head = true;
            twig_emplace_back(std::move(leaf));
            return;
        }
        assert (!has_twig(n));
        TwigIndexType idx = twig_index(n);
        twig_emplace_at(idx, std::move(leaf));
        bitmap |= (1 << n);
    }
    void twig_insert(LeafType&& leaf)
    {
        NybbleType n = twig_nybble(leaf.get_key());
        return twig_insert(std::move(leaf), n);

    }
    void twig_insert(Branch&& new_branch, NybbleType n)
    {
        TwigIndexType idx = twig_index(n);
        twig_emplace_at(idx, std::move(new_branch));
        bitmap |= (1 << n);
    }
    void twig_remove(NybbleType n)
    {
        assert(has_twig(n));
        TwigIndexType idx = twig_index(n);
        twig_erase_at(idx);
        bitmap &= (~(1 << n));
    }
};

template <typename DataType, bool IsMap>
class Node
{
private:

    using LeafType = jzt::detail::qp::Leaf<DataType, IsMap>;
    using DataTraintsType = typename LeafType::DataTraintsType;
    using BranchType = jzt::detail::qp::Branch<DataType, IsMap>;

public:
    using key_type = typename LeafType::key_type;
    using mapped_type = typename LeafType::mapped_type;
    using value_type = typename LeafType::value_type;


    std::variant<LeafType, BranchType> v;

    //
    Node* find_similar(std::string_view key)
    {
        Node* node = this;
        while (node->is_branch()) {
            auto& branch = std::get<BranchType>(node->v);
            NybbleType n = branch.twig_nybble(key);
            if (n == NybbleHead) {
                if (branch.has_head()) {
                    node = branch.get_head();
                } else {
                    node = branch.twig(0);
                }
            } else if (branch.has_twig(n)) {
                TwigIndexType idx = branch.twig_index(n);
                node = branch.twig(idx);
            } else if (branch.has_head()) {
                node = branch.get_head();
            } else {
                node = branch.twig(0);
            }
        }
        return node;
    }
    template <typename ...Args>
    void leaf_burst(NybbleIndexType mismatch_index, Args... args)
    {
        auto& leaf = std::get<LeafType>(v);
        LeafType saved(std::move(leaf));
        BranchType branch(mismatch_index, LeafType(std::forward<Args>(args)...));
        branch.twig_insert(std::move(saved));
        v.template emplace<BranchType>(std::move(branch));
    }
public:
    bool is_leaf() const
    {
        return std::holds_alternative<LeafType>(v);
    }
    bool is_branch() const
    {
        return std::holds_alternative<BranchType>(v);
    }
    BranchType& get_branch()
    {
        return std::get<BranchType>(v);
    }
    LeafType& get_leaf()
    {
        return std::get<LeafType>(v);
    }

    Node* find(std::string_view key)
    {
        if (is_leaf()) {
            auto& leaf = std::get<LeafType>(v);
            if (leaf.get_key() == key) {
                return this;
            }
            return nullptr;
        }
        if (is_branch()) {
            Node* similar_node = find_similar(key);
            if (std::get<LeafType>(similar_node->v).get_key() == key) {
                return similar_node;
            }
        }
        return nullptr;
    }
    bool contains(std::string_view key)
    {
        if (is_leaf()) {
            auto& leaf = std::get<LeafType>(v);
            return (leaf.get_key() == key);
        }
        if (is_branch()) {
            Node* similar_node = find_similar(key);
            return (std::get<LeafType>(similar_node->v).get_key() == key);
        }
        return false;
    }
    bool contains_prefix(std::string_view prefix)
    {
        if (is_leaf()) {
            auto& leaf = std::get<LeafType>(v);
            std::string_view leaf_key(leaf.get_key());
            return (leaf_key.compare(0, prefix.size(), prefix) == 0);
        }
        if (is_branch()) {
            Node* similar_node = find_similar(prefix);
            std::string_view similar_key(std::get<LeafType>(similar_node->v).get_key());
            return (similar_key.compare(0, prefix.size(), prefix) == 0);
        }
        return false;
    }
    template <typename ...DataArgs>
    bool emplace(DataArgs... args) //todo. bool -> optional<iterator>
    {
        LeafType new_leaf(std::forward<DataArgs>(args)...);
        auto key_sv = std::string_view(new_leaf.get_key());
        if (is_leaf()) {
            auto& leaf = std::get<LeafType>(v);
            auto ni_opt = leaf.find_mismatch(key_sv);
            if (!ni_opt) {
                return false;
            }
            leaf_burst(*ni_opt, std::move(new_leaf));
            return true;
        }
        if (is_branch()) {
            Node* similar_node = find_similar(key_sv);
            LeafType& similar_leaf = std::get<LeafType>(similar_node->v);
            auto ni_opt = similar_leaf.find_mismatch(key_sv);
            if (!ni_opt) {
                return false;
            }
            Node* node = this;
            while (node->is_branch()) {
                auto& branch = std::get<BranchType>(node->v);
                NybbleIndexType branch_ni = branch.nybble_index();
                NybbleType n = branch.twig_nybble(key_sv);
                if (branch_ni < *ni_opt) {
                    TwigIndexType idx = branch.twig_index(n);
                    node = branch.twig(idx);
                    continue;
                }
                if (branch_ni == *ni_opt) {
                    branch.twig_insert(std::move(new_leaf));
                    return true;
                }
                if (branch_ni > *ni_opt) {
                    BranchType new_branch(*ni_opt, std::move(new_leaf));
                    new_branch.twig_insert(std::move(branch), nybble_at(similar_leaf.get_key(), *ni_opt));
                    node->v.template emplace<BranchType>(std::move(new_branch));
                    return true;
                }
            }
            node->leaf_burst(*ni_opt, std::move(new_leaf));
            return true;
        }
        return false;
    }

    Node* get_prefix(std::string_view prefix)
    {
        if (is_leaf()) {
            auto& leaf = std::get<LeafType>(v);
            std::string_view sv = leaf.get_key();
            if (sv.compare(0, prefix.size(), prefix) == 0) {
                return this;
            }
            return nullptr;
        }
        Node* node = this;
        bool has_leaf = false;
        if (is_branch()) {
            Node* similar_node = find_similar(prefix);
            std::string_view sv = std::get<LeafType>(similar_node->v).get_key();
            if (sv.compare(0, prefix.size(), prefix) != 0) {
                return nullptr;
            } else {
                has_leaf = true;
            }
            while (node->is_branch()) {
                auto& branch = std::get<BranchType>(node->v);
                if (branch.nybble_index() >= prefix.size() * 2) {
                    break;
                } else {
                    node = branch.twig(branch.twig_index(branch.twig_nybble(prefix)));
                }
            }
        }
        if (node->is_branch()) {
            return node;
        } else { //similar leaf
            if (has_leaf) {
                return node;
            }
            return nullptr;
        }
    }

    std::pair<bool/*ok*/, bool/*empty*/> remove(std::string_view key)
    {
        struct Parent {
            Node* node;
            NybbleType n;
            bool head;
        };
        bool empty = false;
        if (is_leaf()) {
            auto& leaf = std::get<LeafType>(v);
            std::string_view leaf_key(leaf.get_key());
            if (leaf_key == key) {
                empty = true;
                return {true, empty};
            }
            return {false, empty};
        }
        if (is_branch()) {
            Node* node = this;
            Parent parent;
            while (node->is_branch()) {
                auto& branch = std::get<BranchType>(node->v);
                NybbleType n = branch.twig_nybble(key);
                if (n == NybbleHead) {
                    if (branch.has_head()) {
                        parent.head = true;
                        parent.node = node;
                        node = branch.get_head();
                    } else {
                        return {false, empty};
                    }
                } else if (branch.has_twig(n)) {
                    parent.n = n;
                    parent.head = false;
                    parent.node = node;
                    TwigIndexType idx = branch.twig_index(n);
                    node = branch.twig(idx);
                } else {
                    return {false, empty};
                }
            }
            auto& leaf = std::get<LeafType>(node->v);
            std::string_view leaf_key(leaf.get_key());
            if (leaf_key != key) {
                return {false, empty};
            }
            auto& branch = std::get<BranchType>(parent.node->v);
            if (branch.twig_count() > 2) {
                if (parent.head) {
                    branch.remove_head();
                } else {
                    branch.twig_remove(parent.n);
                }
            } else {
                Node* another = nullptr;
                if (parent.head) {
                    another = branch.twig(0);
                } else {
                    TwigIndexType leaf_idx = branch.twig_index(parent.n);
                    assert(leaf_idx == 0 || leaf_idx == 1);
                    if (leaf_idx == 0) {
                        another = branch.twig(1);//maybe head
                    } else {
                        another = branch.twig(0);
                    }
                }
                Node saved(std::move(*another));
                parent.node->v.swap(saved.v);
            }
            return {true, empty};
        }
        return {false, empty};
    }
    //ctor
    Node(Node&& node) : v(std::move(node.v)) {}
    Node(const Node& node) = delete;

    Node& operator= (Node&& node)
    {
        v = std::move(node.v);

        return *this;
    }
    Node(BranchType&& branch) : v(std::move(branch)) {}
    Node(LeafType&& leaf) : v(std::move(leaf)) {}

    template <typename ...LeafArgs>
    Node(LeafArgs&& ...args) : v(LeafType(std::forward<LeafArgs>(args)...)) {}
};

template <typename NodeType>
struct IteratorBase
{
    std::stack<NodeType*> stk;

    NodeType* next_leaf()
    {
        if (stk.empty()) return nullptr;
        if (stk.top()->is_leaf()) stk.pop();
        NodeType* node = nullptr;
        while (!stk.empty()) {
            node = stk.top();
            if (node->is_leaf()) break;
            stk.pop();
            auto& branch = node->get_branch();
            auto sz = branch.twig_count();
            assert(sz >= 2);
            if (branch.has_head()) {
                sz -= 2;
                for (int i = sz; i >= 0; i-- ) {
                    stk.push(branch.twig(i));
                }
                stk.push(branch.get_head());
            } else {
                sz -= 1;
                for (int i = sz; i >= 0; i-- ) {
                    stk.push(branch.twig(i));
                }
            }
        }
        return node;
    }

    IteratorBase() = default;
    IteratorBase(const IteratorBase& o) : stk(o.stk) {}
    IteratorBase(IteratorBase&& o) : stk(o.stk) {}

    bool operator==(const IteratorBase& rhs) const
    {
        if (stk.empty() && rhs.stk.empty()) return true;
        if (!stk.empty() && !rhs.stk.empty()) return stk.top() == rhs.stk.top();
        return false;
    }
    bool operator!=(const IteratorBase& rhs) const
    {
        return !(operator==(rhs));
    }
};

} //namespace jzt::detail::qp

} //namespace jzt::detail

namespace qp {

template <typename NodeType>
class Iterator;

template <typename NodeType>
class ConstIterator;

template <typename NodeType>
class Iterator : public jzt::detail::qp::IteratorBase<NodeType>
{
private:
    using value_type = typename NodeType::value_type;
    using ConstIteratorType = ConstIterator<NodeType>;
    using IteratorBaseType = jzt::detail::qp::IteratorBase<NodeType>;
public:
    Iterator() {}
    Iterator(NodeType* root)
    {
        this->stk.push(root);
        if (root->is_branch()) {
            IteratorBaseType::next_leaf();
        }
    }
    Iterator(const Iterator& o) : IteratorBaseType(o)   {}
    Iterator(Iterator&& o) : IteratorBaseType(std::move(o)) {}

    Iterator& operator=(const Iterator& o)
    {
        this->stk = o.stk;
        return *this;
    }
    Iterator& operator=(Iterator&& o)
    {
        this->stk = std::move(o.stk);
        return *this;
    }

    value_type& operator*()
    {
        NodeType* node = nullptr;
        node = this->stk.top();
        return node->get_leaf().get_data();
    }
    value_type* operator->()
    {
        return &(operator*());
    }
    Iterator& operator++()
    {
        IteratorBaseType::next_leaf();
        return *this;
    }
    Iterator operator++(int)
    {
        Iterator tmp(*this);
        IteratorBaseType::next_leaf();
        return tmp;
    }
    using IteratorBaseType::operator==;
    using IteratorBaseType::operator!=;

};

template <typename NodeType>
class ConstIterator : public jzt::detail::qp::IteratorBase<NodeType>
{
private:
    using value_type = typename NodeType::value_type;
    using IteratorType = Iterator<NodeType>;
    using IteratorBaseType = jzt::detail::qp::IteratorBase<NodeType>;
public:
    ConstIterator() {}
    ConstIterator(NodeType* root)
    {
        this->stk.push(root);
        if (root->is_branch()) {
            IteratorBaseType::next_leaf();
        }
    }
    ConstIterator(const ConstIterator& o) : IteratorBaseType(o) {}
    ConstIterator(ConstIterator&& o) : IteratorBaseType(std::move(o)) {}

    ConstIterator& operator=(const ConstIterator& o)
    {
        this->stk = o.stk;
        return *this;
    }
    ConstIterator& operator=(ConstIterator&& o)
    {
        this->stk = std::move(o.stk);
        return *this;
    }

    const value_type& operator*()
    {
        NodeType* node = nullptr;
        node = this->stk.top();
        return node->get_leaf().get_data();
    }
    const value_type* operator->()
    {
        return &(operator*());
    }
    ConstIterator& operator++()
    {
        IteratorBaseType::next_leaf();
        return *this;
    }
    ConstIterator operator++(int)
    {
        ConstIterator tmp(*this);
        IteratorBaseType::next_leaf();
        return tmp;
    }
};

template <typename DataType, bool IsMap>
class Trie
{
private:
    using LeafType = jzt::detail::qp::Leaf<DataType, IsMap>;
    using NodeType = jzt::detail::qp::Node<DataType, IsMap>;
    using key_type = typename LeafType::key_type;
    using value_type = typename LeafType::value_type;
    using mapped_type = typename LeafType::mapped_type;

public:
    using IteratorType = Iterator<NodeType>;
    using ConstIteratorType = ConstIterator<NodeType>;

private:

    std::optional<NodeType> root;

    static inline const IteratorType iterator_end = {};

public:
    Trie() {}

    static uint64_t max_key_size()
    {
        return ((uint64_t)1 << 37);
    }
    IteratorType begin()
    {
        if (!root) {
            return {};
        }
        return IteratorType(&(root.value()));
    }
    ConstIteratorType cbegin()
    {
        if (!root) {
            return {};
        }
        return ConstIteratorType(&(root.value()));
    }
    const IteratorType& end() const
    {
        return iterator_end;
    }
    const ConstIteratorType& cend() const
    {
        return iterator_end;
    }
    template <typename ...Args, std::enable_if_t<std::is_constructible_v<LeafType, Args...>, bool> = true>
    void emplace(Args... args)
    {
        if (!root) {
            root.emplace(std::forward<Args>(args)...);
            return;
        }
        root->emplace(std::forward<Args>(args)...);
    }
    template <typename T, std::enable_if_t<std::is_convertible_v<T, std::string_view>, bool> = true>
    IteratorType find(const T& key)
    {
        if (!root) {
            return {};
        }
        std::string_view sv(key);
        NodeType* node = root->find(sv);
        if (node == nullptr) return {};
        return IteratorType(node);
    }
    template <typename T, std::enable_if_t<std::is_convertible_v<T, std::string_view>, bool> = true>
    ConstIteratorType find(const T& key) const
    {
        if (!root) {
            return {};
        }
        std::string_view sv(key);
        NodeType* node = root->find(sv);
        if (node == nullptr) return {};
        return ConstIteratorType(node);
    }
    template <typename T, std::enable_if_t<std::is_convertible_v<T, std::string_view>, bool> = true>
    IteratorType prefix(const T& prefix)
    {
        if (!root) {
            return {};
        }
        std::string_view sv(prefix);
        NodeType* node = root->get_prefix(sv);
        if (node == nullptr) return {};
        return IteratorType(node);
    }

    template <typename T, std::enable_if_t<std::is_convertible_v<T, std::string_view>, bool> = true>
    bool contains(const T& key)
    {
        if (!root) return false;
        std::string_view sv(key);
        return root->contains(sv);
    }

    template <typename T, std::enable_if_t<std::is_convertible_v<T, std::string_view>, bool> = true>
    bool contains_prefix(const T& prefix)
    {
        if (!root) return false;
        std::string_view sv(prefix);
        return root->contains_prefix(sv);
    }
    template <typename T, std::enable_if_t<std::is_convertible_v<T, std::string_view>, bool> = true>
    bool remove(const T& key)
    {
        if (!root) return false;
        std::string_view sv(key);
        auto ret = root->remove(sv);
        if (!ret.first) return false;
        if (ret.second) {
            root.reset();
        }
        return true;
    }

};

} //namespace jzt::qp
} //namespace jzt


#endif // TRIE_HPP

