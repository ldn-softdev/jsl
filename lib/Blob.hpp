/*
 * Created by Dmitry Lyssenko.
 *
 * Blob class and SERDES interface (requires c++14 or above)
 *
 * Serdes interface provides serialization/deserialization ability for arbitrary
 * defined user classes. A user class ensures serialization/deserialization operations
 * by inclusion of SERDES macro as a public methods and enumerating all class
 * members which needs to be SERDES'ed.
 *
 * Blob class caters a byte-vector which holds serialized data.
 * There 2 basic interfaces to serialize data into the Blob and de-serialize (restore
 * data) from the blob:
 *
 *      Blob b;
 *      b.append(x, y, etc);
 *      b.restore(x, y, etc);
 *
 * What types of data that can be serialized/deserialized?
 *  1. any fundamental data (bool, char, int, double, etc)
 *  2. C-type arrays and STL containers of the above types
 *  3. any user-defined data classes which are defined with SERDES interface
 *     - that includes recurrent data structures and those with (recursive) pointers
 *  4. data held by (dynamic) pointers is handled through user's callbacks
 *
 * Blob class features 2 constructors (in addition to default's):
 *  - Constructor with data structures to be serialized:
 *
 *      Blob b(x, y, z);            // which is equal to: Blob b; b.append(x, y, z);
 *
 *  - Constructor with iterators, the iterator must be a byte-type, it's particularly
 *    handy with istream_iterator's to load up data into the blob from external
 *    sources:
 *
 *      ifstream fs(file_name, ios::binary);                // input file with serialized data
 *      Blob b( istream_iterator<uint8_t>{fs>>noskipws},    // construct blob from istream
 *              istream_iterator<uint8_t>{} );
 *
 *  - Constructor with iterators and target data structure where blob needs
 *    to be de-serialized:
 *
 * Other Blob methods:
 *      reset()         // reset blob's state: requires in between append and restore
 *      clear()         // clears blob entirely
 *      offset()        // returns current offset (after next append/restore operations)
 *      size()          // returns size of the blob itself (not size of the Blob object)
 *      empty()         // check if blob is empty (e.g. after clear())
 *      data()          // returns blob's data (string of serialized bytes)
 *      store()         // returns container (vector) of blob's data
 *      [c]begin()      // [c]begin() and [c]end() methods are re-published from
 *      [c]end()        // from store, for user convenience
 *
 *
 * SERDES interface explained:
 *
 *  User class becomes SERDES'able when SERDES macro is included inside class definition
 *  as the public method, e.g.:
 *
 *  class SomeClass {
 *   public:
 *      ...
 *      SERDES(SomeClass, i_, s_, v_, ...)  // enumerate all what needs to be SERDES'ed
 *
 *   private:
 *      int                             i_;
 *      std::sting                      s_;
 *      std::vector<SerdesableClass>    v_;
 *      ...
 *  };
 *
 *  ... // once a class defined like that, the class becomes SERDES'able:
 *  SomeClass x;
 *  Blob b(x);                          // same as: Blob b; b.append(x);
 *
 * SERDES macro declares 2 public methods and a constructor for the host class, which
 * let restoring class object from the blob:
 *
 *      serialize(...);
 *      deserialize(...);
 *
 * - serialize() accepts data types by const reference, while deserialize()
 *   does by reference, thus in order to enumerate SERDES'able object by a call,
 *   two methods must be provided: one for serialize() and one for deserialize()
 *   methods, e.g.:
 *
 *  class SomeClass {
 *   public:
 *      ...
 *      int             get_i(void) const { return i; }     // serialize requires const qualifier
 *      int &           get_i(void) { return &i; }          // used in deserialize (no const)
 *
 *      SERDES(SomeClass, get_i(), ...)     // enumerate all what needs to be serdes'ed
 *
 *   private:
 *      int                             i_;
 *      ...
 *  };
 *
 *  if Blob is available for SomeClass, then data could be reinstated at the construction
 *  time:
 *
 *      Blob  b;
 *      // ... assume b was read from a file here, containing SomeClass data;
 *      SomeClass x(b);                 // this constructor is provided by SERDES interface
 *
 *
 * SERDES interface handling pointers explained:
 *
 *  When class handles dynamic resources via pointers (though consider that to be an 
 *  obsolete and generally bad practice), then SERDES needs pointer-provider method:
 *  pointer-provider method is void type, while accepts reference to Blob:
 *
 *  class ResourceHandler {
 *   public:
 *      ...
 *      SERDES(ResourceHandler, x_, &ResourceHandler::ptr_provider)
 *
 *      void            ptr_provider(Blob &b) const {           // for serialize (const qualifier)
 *                       if(b.append(ptr_ != nullptr))          // store state of pointer
 *                        b.append_raw(ptr1_, b.append(strlen(ptr_)));
 *                      }
 *
 *      void            ptr_provider(Blob &b) {                 // de-serialize (restore) provider 
 *                       bool is_saved;                         // check if ptr was actually saved
 *                       size_t size;
 *                       if(b.restore(is_saved)) {
 *                        ptr_ = new char[b.restore(size)]; 
 *                        b.restore_raw(ptr_, size);
 *                       }
 *                      }
 *
 *   private:
 *      someClass           x_;
 *      char []             ptr_;
 *  };
 *
 * SERDES itself does not handle any data pointed by pointers, instead it let user's
 * callback(s) (a.k.a pointer providers) take care of serialization (appending) and
 * de-deserialization (restoring) of data held by pointers
 *
 *
 * IMPORTANT: SERDES interface requires host class to have a default constructor
 *            (or one with a default argument), thus if none is declared, forces 
 *            an explicit default one (it could be private though).
 *
 *
 * File operations with Blob:
 *
 * Blob provides ostream operator:
 *
 *      Blob b(x, y, z);
 *      std::ofstream bf(file, std::ios::binary);
 *      bf << b;
 *
 * as well as istream's:
 *
 *      Blob b;
 *      std::ifstream bf(file, std::ios::binary);
 *      bf >> std::noskipws >> b;
 *
 *
 * for more usage examples see "gt_blob.cpp"
 *
 */

#pragma once

#include <vector>
#include <utility>      // std::forward, std::move,
#include <cstdint>      // uint8_t, ...
#include <type_traits>
#include "extensions.hpp"



// SERDES is a simplest form of interface allowing serialization/deserialization
// of any fundamental types of data, SERDES'able classes (those, defined with SERDES
// interface), arrays and containers of afore mentioned data types as well as
// data stored by pointers
// declaration syntax:
//      SERDES(MyClass, a, b, c, etc)
//
// SERDES macro requires presence of a default constructor
#define __SERDES_APPEND__(ARG) __blob__.append(ARG);
#define __SERDES_RESTORE__(ARG) __blob__.restore(ARG);
#define SERDES(CLASS, Args...) \
             CLASS(Blob &&__blob__): CLASS() { deserialize(__blob__); } \
             CLASS(Blob &__blob__): CLASS() { deserialize(__blob__); } \
        void serialize(Blob &__blob__) const { \
         __blob__.__push_host__(this); \
         MACRO_TO_ARGS(__SERDES_APPEND__, Args) \
         __blob__.__pop_appended__(); \
        } \
        void deserialize(Blob &__blob__) { \
         __blob__.__push_host__(this); \
         MACRO_TO_ARGS(__SERDES_RESTORE__, Args) \
         __blob__.__pop_restored__(); \
        }



class Blob{
  // dump blob into output stream
  friend std::ostream & operator<<(std::ostream &os, const Blob & self) {
                         os.write(reinterpret_cast<const char*>(self.data()), self.size());
                         return os;
                        }
  // read into blob from input stream
  // blob size is unknown (until parsed), so will read until end of stream
  friend std::istream & operator>>(std::istream &is, Blob & self) {
                         size_t file_size, is_pos = is.tellg();
                         file_size = is.seekg(0, std::ios_base::end).tellg();
                         self.store().resize(file_size - is_pos);
                         is.seekg(is_pos)
                           .read(reinterpret_cast<char*>(self.data()), file_size - is_pos);
                         return is;
                        }

 public:

    typedef unsigned int uint;

    #define THROWREASON \
                inconsistent_data_while_appending, \
                inconsistent_data_while_restoring
    ENUMSTR(ThrowReason, THROWREASON)


                        Blob(void) = default;                   // DC

                        // constructors to dump target data into blob:
                        // Blob(src1, src2, ...);
    template<typename... Args>
    explicit            Blob(Args & ... args)
                         { append(args...); }

                        // constructor to restore Blob
                        // handy to use with istream_iterator's:
    template<class Iter>
                        Blob(Iter first, Iter last)
                         { while(first != last) store().push_back(*first++); }


    // User interface to work with Blob:
                        // reset will clear all data structures except blob itself,
                        // as if it was just read
    Blob &              reset(void)
                         { /*vp_.clear(); mpu_.clear(); mpl_.clear();*/ offset_=0; return *this; }
    Blob &              clear(void) { blob_.clear(); return reset(); }
    size_t              offset(void) const { return offset_; }
    size_t              size(void) const { return blob_.size(); }
    bool                empty(void) const { return blob_.empty(); }

    uint8_t *           data(void) { return blob_.data(); }
    const uint8_t *     data(void) const { return blob_.data(); }
 std::vector<uint8_t> & store(void) { return blob_; }
    const std::vector<uint8_t> &
                        store(void) const { return blob_; }

                        // republish container's iterators
    auto                begin(void) { return store().begin(); }
    auto                begin(void) const { return store().begin(); }
    auto                cbegin(void) const { return store().cbegin(); }
    auto                end(void) { return store().end(); }
    auto                end(void) const { return store().end(); }
    auto                cend(void) const { return store().cend(); }

                        // these methods have to be public, but should not be used by user
    void                __push_host__(const void *ptr) { cptr_.push_back(ptr); }
    void                __push_host__(void *ptr) { vptr_.push_back(ptr); }
    void                __pop_appended__(void) { if(not cptr_.empty()) cptr_.pop_back(); }
    void                __pop_restored__(void) { if(not vptr_.empty()) vptr_.pop_back(); }


    //
    // ... APPEND [to the blob] methods
    //
                        // generic, data-type agnostic append
    void                append_raw(const void *ptr, size_t s)
                         { for(size_t i = 0; i < s; ++i) blob_.push_back(((uint8_t*)ptr)[i]); }

                        // 0. variadic append
    template<typename T, typename... Args>
    void                append(const T & first, const Args &... rest)
                         { append(first); append(rest...); }

                        // 1a. atomic (fundamental) type append
    template<typename T>
    typename std::enable_if<std::is_fundamental<T>::value, const T &>::type
                        append(const T & v) { append_raw(&v, sizeof(T)); return v; }

                        // 1b. SERDES class append (custom class must provide serialize(Blob &))
    template<typename T>
    typename std::enable_if<std::is_member_function_pointer<decltype(& T::serialize)>::value,
                            const T &>::type
                        append(const T & v) { v.serialize(*this); return v; }

                        // 1c. atomic (enum) type append
    template<typename T>
    typename std::enable_if<std::is_enum<T>::value, const T &>::type
                        append(const T & v) { append_raw(&v, sizeof(T)); return v; }

                        // 2. containers
                        // 2a. basic string append
    const std::basic_string<char> &
                        append(const std::basic_string<char> & c)
                         { appendCntr_(c.size()); for(char v: c) append(v); return c; }

                        // 2b. native arrays append
    template<typename T>
    typename std::enable_if<std::is_array<T>::value, const T &>::type
                        append(const T & v) {
                         for(int i=0, s=sizeof(v)/sizeof(v[0]); i<s; ++i) append(v[i]);
                         return v;
                        }

                        // 2c. trivial containers - single data allocator append
    template<template<typename, typename> class Container, typename T, typename Alloc>
    typename std::enable_if<std::is_member_function_pointer<decltype(& Alloc::allocate)>::value,
                            const Container<T, Alloc> &>::type
                        append(const Container<T, Alloc> & c)
                         { appendCntr_(c.size()); for(const T &v: c) append(v); return c; }

                        // 2d. complex container append: map
    template<template<typename, typename, typename, typename> class Map,
             typename K, typename V, typename C, typename A>
    typename std::enable_if<std::is_member_function_pointer<decltype(& A::allocate)>::value,
                            const Map<K, V, C, A> &>::type
                        append(const Map<K, V, C, A> & c) {
                         appendCntr_(c.size());
                         for(auto &v: c)
                          { append(v.first); append(v.second); }
                         return c;
                        }

                        // 3. pointers
                        // 3a. pointers are callbacks of const member methods
    template<typename T>
    typename std::enable_if<std::is_member_function_pointer<void(T::*)(Blob &) const>::value,
                            void>::type
                        append(void (T::*cb)(Blob &) const)
                         { (static_cast<const T*>(cptr_.back())->*cb)(*this); }


    //
    // ... RESTORE [from the blob] methods
    //
                        // generic, data-type agnostic restore
    void                restore_raw(char * ptr, size_t s) {
                         for(size_t i = 0; i < s; ++i)
                          *static_cast<char*>(ptr++) = blob_.at(offset_++);
                        }

                        // 0. variadic restore
    template<typename T, typename... Args>
    void                restore(T & first, Args &... rest)
                         { restore(first); restore(rest...); }

                        // 1a. atomic (fundamental) restore
    template<typename T>
    typename std::enable_if<std::is_fundamental<T>::value, T &>::type
                        restore(T &r) {
                         r = *reinterpret_cast<T*>(&blob_.at(offset_));
                         offset_ += sizeof(T);
                         return r;
                        }

                        // 1b. SERDES class restore (custom class must provide deserialize(Blob &))
    template<typename T>
    typename std::enable_if<std::is_member_function_pointer<decltype(& T::deserialize)>::value,
                            T &>::type
                        restore(T & v) { v.deserialize(*this); return v; }

                        // 1c. atomic (enum) restore
    template<typename T>
    typename std::enable_if<std::is_enum<T>::value, T &>::type
                        restore(T &r) {
                         r = *reinterpret_cast<T*>(&blob_.at(offset_));
                         offset_ += sizeof(T);
                         return r;
                        }

                        // 2. containers
                        // 2a. basic string restore
    std::basic_string<char> &
                        restore(std::basic_string<char> & c)
                         { c.resize(restoreCntr_()); for(auto &v: c) restore(v); return c; }

                        // 2b. native arrays restore
    template<typename T>
    typename std::enable_if<std::is_array<T>::value, T &>::type
                        restore(T &r) {
                         for(int i=0, s=sizeof(r)/sizeof(r[0]); i<s; ++i)
                          restore(r[i]);
                         return r;
                        }

                        // 2c. trivial container restore: vector/list/dequeue/array
    template<template<typename, typename> class Container, typename Alloc, typename T>
    typename std::enable_if<std::is_member_function_pointer<decltype(& Alloc::allocate)>::value,
                            Container<T, Alloc> &>::type
                        restore(Container<T, Alloc> & c)
                         { c.resize(restoreCntr_()); for(auto &v: c) restore(v); return c; }

                        // 2d. complex container restore: map
    template<template<typename, typename, typename, typename> class Map,
             typename K, typename V, typename C, typename A>
    Map<K, V, C, A> &   restore(Map<K, V, C, A> & c) {
                         size_t l = restoreCntr_();
                         for(size_t i = 0; i < l; ++i)
                          { K k; V v; restore(k); restore(v);
                            c.emplace(std::move(k), std::move(v)); }
                         return c;
                        }

                        // 3. pointers
                        // 3a. pointers are callbacks of non-const member methods
    template<typename T>
    typename std::enable_if<std::is_member_function_pointer<void(T::*)(Blob &)>::value,
                            void>::type
                        restore(void (T::*cb)(Blob &))
                         { (static_cast<T*>(vptr_.back())->*cb)(*this); }

    EXCEPTIONS(ThrowReason)

 protected:
    size_t              offset_{0};
   std::vector<uint8_t> blob_;

 private:
std::vector<const void*>cptr_;                                  // host pointers for append.
    std::vector<void*>  vptr_;                                  // host pointers for restore

    // save up some space when serializing counters. Don't attempt moving counter
    // size to bits-space of the counter itself - it will break then either of
    // endianess.
    // any further optimization of serialized space must be done with compression
    // algorithms
    void                appendCntr_(size_t s) {
                         uint8_t cs = counterSize_(s);
                         append(cs);
                         switch(cs) {
                          case 0: append(static_cast<uint8_t>(s)); break;
                          case 1: append(static_cast<uint16_t>(s)); break;
                          case 2: append(static_cast<uint32_t>(s)); break;
                          case 3: append(static_cast<uint64_t>(s)); break;
                          default: throw EXP(inconsistent_data_while_appending);
                         }
                        }
    size_t              restoreCntr_(void) {
                         uint8_t cs;
                         restore(cs);
                         switch(cs) {
                          case 0: { uint8_t cnt; restore(cnt); return cnt; }
                          case 1: { uint16_t cnt; restore(cnt); return cnt; }
                          case 2: { uint32_t cnt; restore(cnt); return cnt; }
                          case 3: { uint64_t cnt; restore(cnt); return cnt; }
                          default: throw EXP(inconsistent_data_while_restoring);
                         }
                        }
    uint8_t             counterSize_(size_t cntr) {
                         #if ( __WORDSIZE == 64 )
                          static size_t bound[3]{1ul<<8, 1ul<<16, 1ul<<32};
                         #endif
                         #if ( __WORDSIZE == 32 )
                          static size_t bound[2]{1ul<<8, 1ul<<16};
                         #endif
                         for(int i=sizeof(bound)/sizeof(bound[0])-1; i>=0; --i)
                          if(cntr >= bound[i])
                           return i+1;
                         return 0;
                        }

};

STRINGIFY(Blob::ThrowReason, THROWREASON)
#undef THROWREASON

















