#include <iostream>
#include <fstream>
#include <vector>
#include <list>
#include <deque>
#include <map>
#include <string>
#include <string.h>
#include <gtest/gtest.h>
#include "lib/Blob.hpp"
#include "lib/Outable.hpp"

using namespace std;

/*
c++ -o u -Wall -std=c++14 -lgtest gt_blob.cpp
*/

#define FILE "gt_blob.bin"

enum Init { Preserve, Clear };



TEST(SERDES_test, test_append_restore_with_fundamentals) {
 bool                b1{true}, b2{false};
 char                c1{'c'}, c2{'\0'};
 wchar_t             w1{u'Ж'}, w2{'\0'};
 int                 i1{12345}, i2{0};
 long                l1{9876543210}, l2{0};
 float               f1{3.14}, f2{0};
 double              d1{3.14e100}, d2{0};
 long double         dd1{3.14e300}, dd2{0};
 enum { V1, V2, V3 } e1{V3}, e2{V1};

 Blob b(b1, c1, w1, i1, l1, f1, d1, dd1, e1);
 // or: b.append(b1, c1, w1, i1, l1, f1, d1, dd1, e1);
 b.restore(b2, c2, w2, i2, l2, f2, d2, dd2, e2);

 EXPECT_EQ(b1, b2);
 EXPECT_EQ(c1, c2);
 EXPECT_EQ(w1, w2);
 EXPECT_EQ(i1, i2);
 EXPECT_EQ(l1, l2);
 EXPECT_EQ(f1, f2);
 EXPECT_EQ(d1, d2);
 EXPECT_EQ(dd1, dd2);
 EXPECT_EQ(e1, e2);
}



class Fundamentals {
    // for SERDES tests of fundamental types:
    bool                b_{true};
    char                c_{'c'};
    wchar_t             w_{u'Ж'};
    int                 i_{12345};
    long                l_{9876543210};
    float               f_{3.14};
    double              d_{3.14e100};
    long double         dd_{3.14e300};
    enum { V1, V2, V3 } e_{V3};

 public:
                        Fundamentals(Init x = Preserve)
                         { if(x == Clear) nullify(); }

    void                nullify(void)
                         { b_= c_= w_= i_= l_= f_= d_= dd_= 0; e_= V1; }

    bool                operator==(const Fundamentals & r) const {
                         return b_== r.b_ and c_== r.c_ and w_== r.w_ and i_== r.i_ and
                                l_== r.l_ and d_== r.d_ and dd_== r.dd_ and e_== r.e_;
                        }

    SERDES(Fundamentals, b_, c_, w_, get_int(), l_, f_, d_, dd_, e_)

                        // just for demo purposes
    int                 get_int() const { return i_; }          // for serialize/append op.
    int &               get_int() { return i_; }                // for deserialize/restore op.
};

TEST(SERDES_test, serdessable_with_fundamentals) {
 Fundamentals src, dst(Clear);
 Blob b(src);
 b.restore(dst);
 EXPECT_EQ(dst, src);
}



class Serdesables {
    Fundamentals        f1_;

 public:
                        Serdesables(Init x = Preserve)
                         { if(x == Clear) nullify(); }

    void                nullify(void)
                         { f1_.nullify(); }

    bool                operator==(const Serdesables & r) const
                         { return f1_== r.f1_; }

    SERDES(Serdesables, f1_)
};

TEST(SERDES_test, nested_serdesables) {
 Serdesables src, dst(Clear);
 Blob b(src);
 b.restore(dst);
 EXPECT_EQ(dst, src);
}



class NativeArrays {
   static constexpr int size = 5;
    bool                b_[size]{true, false, true, false, true};
    char                c_[size]{'a', 'b', 'c', 'd', 'e'};
    int                 i_[size]{ 1, 2, 3, 4, 5};
    double              d_[size]{ 1.1, 2.2, 3.3, 4.4, 5.5};
    Serdesables         s_[size]{Preserve, Preserve, Preserve, Preserve, Preserve};

 public:
                        NativeArrays(Init x = Preserve)
                         { if(x == Clear) nullify(); }

    void                nullify(void) {
                         for(int j=0; j<size; ++j) {
                          b_[j] = c_[j] = i_[j] = d_[j] = 0;
                          s_[j].nullify();
                         }
                        }

    bool                operator==(const NativeArrays & r) const {
                         bool sb = true;
                         for(int j=0; j<size; ++j)
                          sb &= s_[j] == r.s_[j];
                         return sb and
                                memcmp(b_, r.b_, sizeof(b_)) == 0 and
                                memcmp(c_, r.c_, sizeof(c_)) == 0 and
                                memcmp(i_, r.i_, sizeof(i_)) == 0 and
                                memcmp(d_, r.d_, sizeof(d_)) == 0;
                        }
    SERDES(NativeArrays, b_, c_, i_, d_, s_)
};

TEST(SERDES_test, native_arrays) {
 NativeArrays src, dst(Clear);
 Blob b(src);
 b.restore(dst);
 EXPECT_EQ(dst, src);
}



class TrivialContainers {
    vector<int>         vi_{1, 2, 3, 4, 5};
    list<char>          lc_{'a', 'b', 'c', 'd'};
    string              s_{"Hello world!"};
    deque<double>       dd_{1.1, 2.2, 3.3, 4.4, 5.5};
   vector<Fundamentals> vf_{Preserve, Preserve, Preserve};

 public:
                        TrivialContainers(Init x = Preserve)
                         { if(x == Clear) nullify(); }

    void                nullify(void)
                         { vi_.clear(); lc_.clear(); s_.clear(); dd_.clear(); vf_.clear(); }

    bool                operator==(const TrivialContainers & r) const
                         { return vi_== r.vi_ and lc_== r.lc_ and
                                  s_== r.s_ and dd_== r.dd_ and vf_== r.vf_; }

    const char *        s_str(void) const { return s_.c_str(); } // needed in NoResourcePointers

    SERDES(TrivialContainers, vi_, lc_, s_, dd_, vf_)
};

TEST(SERDES_test, trivial_containers) {
 TrivialContainers src, dst(Clear);
 Blob b(src);
 b.restore(dst);
 EXPECT_EQ(dst, src);
}



class ComplexContainers {
    map<string, Serdesables>
                        mvs_{ {"filled", Serdesables(Preserve)},
                              {"empty", Serdesables(Clear)} };
    map<string, TrivialContainers>
                        mvc_{ {"empty", TrivialContainers(Clear)},
                              {"filled", TrivialContainers(Preserve)} };

 public:
                        ComplexContainers(Init x = Preserve)
                         { if(x == Clear) nullify(); }

    void                nullify(void)
                         { mvs_.clear(); mvc_.clear(); }

    bool                operator==(const ComplexContainers & r) const
                         { return mvs_== r.mvs_ and mvc_== r.mvc_; }

    SERDES(ComplexContainers, mvs_, mvc_)
};

TEST(SERDES_test, complex_containers) {
 ComplexContainers src, dst(Clear);
 Blob b(src);
 b.restore(dst);
 EXPECT_EQ(dst, src);
}



class NoResourcePointers {
    TrivialContainers   f1_{Preserve};
    TrivialContainers   f2_{Preserve};
    const char *        ptr1_{f1_.s_str()};
    const char *        ptr2_{f2_.s_str()};

 public:
                        NoResourcePointers(Init x = Preserve)
                         { if(x == Clear) nullify(); }

    void                nullify(void)
                         { f1_.nullify(); f2_.nullify(); ptr1_= ptr2_= nullptr; }

   const char * 	    cptr1(void) const { return ptr1_; }
   const char *         cptr2(void) const { return ptr2_; }

    bool                operator==(const NoResourcePointers & r) const {
                         return f1_== r.f1_ and strcmp(cptr1(), r.cptr1()) == 0 and
                                f2_== r.f2_ and strcmp(cptr2(), r.cptr2()) == 0;
                        }
    SERDES(NoResourcePointers, f1_, f2_, &NoResourcePointers::ptr_provider)

    void                ptr_provider(Blob &b) const {}
    void                ptr_provider(Blob &b) { ptr1_ = f1_.s_str(); ptr2_ = f2_.s_str(); }

};

TEST(SERDES_test, no_resource_handler_pointer) {
 NoResourcePointers src, dst(Clear);
 Blob b(src);
 b.restore(dst);
 ASSERT_NE(dst.cptr1(), nullptr) << " - after restoration pointer_1 should not be nullptr";
 ASSERT_NE(dst.cptr2(), nullptr) << " - after restoration pointer_2 should not be nullptr";
 EXPECT_EQ(dst, src);
}



class ResourcePointers {
    ComplexContainers   c_{Preserve};
std::unique_ptr<char[]> ptr1_;
    char *              ptr2_{nullptr};

 public:
                        ResourcePointers(Init x = Preserve) {
                         ptr1_.reset(new char[6] {"hello"});     // let's recreate the content
                         ptr2_ = new char[6] {"world"};
                         if(x == Clear) nullify();
                        }
                       ~ResourcePointers(void)
                         { nullify(); }

    void                nullify(void)
                         { c_.nullify(); delete [] ptr2_; ptr2_= nullptr; }

   const char *         cptr1(void) const { return ptr1_.get(); }
   const char *         cptr2(void) const { return ptr2_; }

    bool                operator==(const ResourcePointers & r) const {
                         return c_ == r.c_ and
                                strcmp(cptr1(), r.cptr1()) == 0 and
                                strcmp(cptr2(), r.cptr2()) == 0;
                        }
    SERDES(ResourcePointers, c_, &ResourcePointers::ptr_provider)

    void                ptr_provider(Blob &b) const {           // serialize (const qualifier)
                         if(b.append(ptr1_ != nullptr))
                          b.append_raw(ptr1_.get(), b.append(strlen(ptr1_.get())));
                         if(b.append(ptr2_ != nullptr))
                          b.append_raw(ptr2_, b.append(strlen(ptr2_)));
                        }

    void                ptr_provider(Blob &b) {                 // de-serialize provider
                         bool ptr_saved;
                         size_t size;
                         if(b.restore(ptr_saved)) {
                          ptr1_.reset(new char[b.restore(size)]);
                          b.restore_raw(ptr1_.get(), size);
                         }
                         if(b.restore(ptr_saved)) {
                          ptr2_ = new char[b.restore(size)];
                          b.restore_raw(ptr2_, size);
                         }
                        }
};


TEST(SERDES_test, resource_handler_pointer) {
 ResourcePointers src, dst(Clear);
 Blob b(src);
 b.restore(dst);
 ASSERT_NE(dst.cptr1(), nullptr) << " - after restoration pointer_1 should not be nullptr";
 ASSERT_NE(dst.cptr2(), nullptr) << " - after restoration pointer_2 should not be nullptr";
 EXPECT_EQ(dst, src);
}



class DataTree {
 public:
    int                 x{0};
    vector<DataTree>    v;

                        DataTree() = default;

    bool                operator!=(const DataTree & r) const { return not (*this == r); }
    bool                operator==(const DataTree & r) const { return v == r.v and x == r.x; }

    SERDES(DataTree, x, v)
};

TEST(SERDES_test, data_tree) {
 DataTree src, dst;

 // build some src tree now
 src.x = 120;
 src.v.resize(2);

 src.v[0].x = 123;
 src.v[1].x = 456;
 src.v[0].v.resize(2);

 src.v[0].v[0].x = 123123;
 src.v[0].v[1].x = 123456;
 src.v[0].v[1].v.resize(1);

 src.v[0].v[1].v[0].x = 123456123;

 EXPECT_NE(dst, src);

 Blob b(src);
 b.restore(dst);
 EXPECT_EQ(dst, src);
}



class DataTreePtr {
 public:
    int                 x{0};
    DataTreePtr *       l{nullptr};
    DataTreePtr *       r{nullptr};

                        DataTreePtr(void) = default;
                       ~DataTreePtr(void) { delete l; delete r; l = r = nullptr; }

    bool                operator!=(const DataTreePtr & r) const { return not (*this == r); }
    bool                operator==(const DataTreePtr & o) const {
                         if(l and not o.l) return false;
                         if(not l and o.l) return false;
                         if(r and not o.r) return false;
                         if(not r and o.r) return false;
                         return x == o.x and
                                (l? *l == *o.l: true) and
                                (r? *r == *o.r: true);
                        }

    SERDES(DataTreePtr, x, &DataTreePtr::ptr_provider);

    void                ptr_provider(Blob &b) const {
                         if(b.append(l != nullptr))
                          b.append(*l);
                         if(b.append(r != nullptr))
                          b.append(*r);
                        }

    void                ptr_provider(Blob &b) {
                         bool ptr_saved;
                         if(b.restore(ptr_saved)) {
                          l = new DataTreePtr;
                          b.restore(*l);
                         }
                         if(b.restore(ptr_saved)) {
                          r = new DataTreePtr;
                          b.restore(*r);
                         }
                        }
};

TEST(SERDES_test, data_tree_ptr) {
 DataTreePtr src, dst;

 // build some src tree now
 src.x = 120;                               /*          120             */
 src.l = new DataTreePtr;                   /*         /   \            */
 src.r = new DataTreePtr;

 src.l->x = 123;                            /*        123   456         */
 src.r->x = 456;                            /*       /   \              */
 src.l->l = new DataTreePtr;
 src.l->r = new DataTreePtr;

 src.l->l->x = 123123;                      /*   123123  123456         */
 src.l->r->x = 123456;                      /*                \         */
 src.l->r->r = new DataTreePtr;

 src.l->r->r->x = 123456123;                /*               123456123  */

 EXPECT_NE(dst, src);
 Blob b(src);
 b.restore(dst);
 EXPECT_EQ(dst, src);
}



TEST(SERDES_test, save_and_restore_via_file) {
 DataTree src1, src2;

 // build some src1 tree now
 src1.x = 0x42;
 src1.v.resize(2);

 src1.v[0].x = 0xAA;
 src1.v[1].x = 0x55;
 src1.v[0].v.resize(2);

 src1.v[0].v[0].x = 0xDEAF;
 src1.v[0].v[1].x = 0xFACE;
 src1.v[0].v[1].v.resize(1);

 src1.v[0].v[1].v[0].x = 0xFACADE;

 // build some src2 tree now
 src2.x = 0xAA;
 src2.v.resize(1);

 src2.v[0].x = 0xBB;
 src2.v[0].v.resize(2);

 src2.v[0].v[0].x = 0xBEAD;
 src2.v[0].v[1].x = 0xCAFE;

 // dump src into blob and blob to file
 Blob b1(src1), b2(src2);
  { ofstream blob_file(FILE, ios::binary);
    copy(b1.begin(), b1.end(), ostream_iterator<char>(blob_file<<noskipws));
    copy(b2.begin(), b2.end(), ostream_iterator<char>(blob_file)); }

 // restore into dst from file
 Blob b( istream_iterator<char>(ifstream{FILE, ios::binary}>>noskipws),
         istream_iterator<char>{} );
 DataTree dst1(b);
 DataTree dst2(b);

 EXPECT_EQ(dst1, src1);
 EXPECT_EQ(dst2, src2);
}





int main( int argc, char *argv[]) {

 testing::InitGoogleTest(&argc, argv);
 return RUN_ALL_TESTS();
}















