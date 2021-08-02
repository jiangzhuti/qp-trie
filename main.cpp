#include <iostream>
#include <memory>
#include <cstring>
#include "Trie.hpp"

using namespace std;
using namespace jzt::qp;

struct S {
    std::string str;
    int a;
    operator std::string_view() const
    {
        return std::string_view(str);
    }
};

int main()
{
    S s;
    std::string str(s);
    Trie<std::string, false> t;
    std::cout << t.max_key_size() << std::endl;
    t.emplace("123", 1);
    t.emplace(std::string("abc"));
    auto it = t.find("1");
    std::cout << *it << std::endl;
    it = t.find("234");
    //std::cout << *it;
    t.remove("1");
    t.remove(std::string("abc"));
    t.emplace("233");
    s.str = "233";
    t.remove(s);
    std::string x("rrr");
    t.emplace(x);
    t.emplace(5, 'q');
    s.str = "rrr";
    bool ret;
    ret = t.contains(s);
    assert(false);
    s.str = "abcde";
    Trie<std::pair<S, int>, true> t2;
    t2.emplace(s, 1);
    s.str = "uvw";
    t2.emplace(std::move(s), 2);
    ret = t2.contains("uvw");

    t2.remove("uvw");
    s.str = "abcde";
    t2.remove(s);
    Trie<S, false> t3;

    s.str = "aaaa";
    t3.emplace(s);
    s.str = "aabb";
    t3.emplace(std::move(s));

    Trie<std::pair<const char*, int>, true> t4;
    t4.emplace("12345", 1);
    ret = t4.contains_prefix(std::string("12"));
    t4.emplace("abcxy", 5);
    t4.emplace("ab", 6);
    t4.emplace("ad", 7);
    t4.emplace("a", 8);
    auto it4 = t4.prefix("a");
    for (; it4 != t4.end(); it4++) {
        cout << it4->first << ", " << it4->second << endl;

    }
    Trie<std::pair<std::string, int>, true> t5;
    t5.emplace("123", 4);
    t5.emplace("1232", 4);
    Trie<std::pair<const char*, int>, true> t6;
    t6.emplace(strdup("123"), 4);
    t6.emplace(strdup("1233"), 4);
    auto it6 = t6.begin();
    while (it6 != t6.end()) {
        std::cout << it6->first << std::endl;
        free((void*)it6->first);
        ++it6;
    }
    return 0;
}
