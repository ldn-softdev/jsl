/*
 * Created by Dmitry Lyssenko, last modified August 18, 2017.
 *
 * Blob class and serdes interface.
 *
 * Serdes interface provides serialization/de-serialization ability for arbitrary
 * defined user classes. A user class ensures serialization/deserialization operations
 * by inclusion of [M/R]SERDES macro as a public methods and enumerating all class
 * members which needs to be serdes'ed.
 *
 * Blob class caters a byte-vector which holds serialized data.
 * There 2 basic interfaces to serialize data into the Blob and de-serialize (restore:
 * data) from the blob:
 *
 *      b.append(x, y, etc);
 *      b.restore(x, y, etc);
 *
 * What types of data that can be serialized/deserialized?
 *  1. any fundamental data (bool, char, int, double, etc)
 *  2. C-type arrays and STL containers of the above types
 *  3. any user-defined data classes which are defined with [R/M]SERDES interface
 *     - that includes recurrent data structures and those with (recursive) pointers
 *
 * Blob class features 3 constructors (in addition to default's):
 *  - Constructor with data structure to be serialized:
 *
 *      Blob b(x, y, z);            // which is equal to Blob b; b.append(x, y, z);
 *
 *  - Constructor with iterator, the iterator must be a byte-type, it's particularly
 *    handy with istream_iterator's to load up data into the blob from external
 *    sources:
 *
 *      ifstream fs(file_name, ios::binary);                // input file with serialized data
 *      Blob b(istream_iterator<uint8_t>{fs>>noskipws},     // construct blob from istream
 *             istream_iterator<uint8_t>{});
 *
 *  - Constructor with iterators and target data structure where blob needs
 *    to be de-serialized:
 *
 *      ifstream fs(file_name, ios::binary);                // input file with serialized data
 *      Blob b(istream_iterator<uint8_t>{fs>>noskipws},     // construct blob from istream
 *             istream_iterator<uint8_t>{}, dst);           // and deserialize it into dst
 *
 *    this constructor accept only a single destination, in case user needs to
 *    restore (deserialize) the blob into multiple destinations, use prior constructor
 *    and restore blob via restore method: b.restore(dst1, dst2, ...);
 *
 * Other Blob methods:
 *      reset()         // reset blob's state: requires in between append and restore
 *      clear()         // clears blob entirely
 *      offset()        // returns current offset (after next append/restore operations)
 *      size()          // returns size of the blob itself (not size of the Blob object)
 *      empty()         // check if blob is empty (e.g. after clear())
 *      data()          // returns blob's data (string of serialized bytes)
 *      store()         // returns container (vector) of blob's data
 *
 *
 * SERDES (does not support pointers) interface explained:
 *
 *  User class becomes SERDES'able when SERDES macro is included inside class definition
 *  as the public method, e.g.:
 *
 *  class SomeClass {
 *   public:
 *      ...
 *      SERDES(i_, s_, v_, ...)         // enumerate all what needs to be serdes'ed
 *
 *   private:
 *      int                             i_;
 *      std::sting                      s_;
 *      std::vector<SerdesableClass>    v_;
 *      ...
 *  };
 *  ... // once defined like that, the class becomes SERDES'able:
 *  SomeClass x;
 *  Blob b(x);                          // same as: Blob b; b.append(x);
 *
 * SERDES macro declares 2 public methods:
 *
 *      serialize(...);
 *      deserialize(...);
 *
 * - serialize() accepts data types by value (or by const reference), while deserialize()
 *   does by reference(), thus in order to enumerate serdes'able object by a call,
 *   two methods must be provided: one for serialize() and one for deserialize()
 *   methods, e.g.:
 *
 *  class SomeClass {
 *   public:
 *      ...
 *      int             get_i(void) const { return i; }     // serialize requires const qualifier
 *      int &           get_i(void) { return &i; }          // used in deserialize (no const)
 *
 *      SERDES(get_i(), ...)            // enumerate all what needs to be serdes'ed
 *
 *   private:
 *      int                             i_;
 *      ...
 *  };
 *
 *
 * MSERDES interface explained:
 *
 *  When class handles resources (via pointers), then MSERDES has to be used
 *
 *  class ResourceHandler {
 *   public:
 *      ...
 *      MSERDES(MIND(x1_.name(), x2_.name()), ptr1_, ptr2_, x1_, x2_, ...)  // include pointers
 *
 *   private:
 *      someClass           x1_, x2_;
 *      const char *        ptr1_{x1_.name()}, ptr2_(x2_.name());
 *                          // assuming someClass has method name() returning const char *
 *  };
 *
 * MSERDES does not handle any data pointed by pointers, it only ensures actualization
 * of pointers during serialize() (blobl's append()) and deserialize() (blob's
 * restore()) operations.
 * thus, MIND(...) macro must enumerate all actual addresses (and in the same order)
 * as enlisted pointers. In the example above, addr1 is the address, which ptr1
 * is pointing to, addr2 is being held by ptr2.
 *
 * During serialization, MIND() operation occurs BEFORE serialization phase - it
 * builds a dictionary of addresses, assigns each address listed in the MIND()
 * a unique id (UID), whose value is then put into the blob (instead of the address)
 *
 * During deserialization, MIND() operation actually occurs AFTER deserialization.
 * Pointers are not restored then, just pointer's addresses are noted. Then later,
 * after MIND() builds the dictionary of addresses (to be restored into the pointers),
 * actual restorations of such addresses occurs (a.k.a. a remind() phase).
 *
 * Thus any addresses to be specified in MIND() macro have to be actual (i.e. exist
 * before serialization and deserialization event happen (that would limit the
 * application of MSERDES only to known/static pointer locations, or addresses
 * of data members that already have been restored).
 * To overcome such limitation, MIND() macro also accepts a linear container
 * (vector, list, deque - those whose iterator points to a pointer) of pointers
 * of type (void *) - the vector is returned by a pointer provider call (defined
 * by the user).
 * Pointer provider builds a container of pointers of (void *) type (order in the
 * container must match the order pointers enumerated as the MSERDES arguments)
 * - there pointers could be dynamically calculated and even data structures could
 * be created:
 *
 *  class ResourceHandler {
 *   public:
 *      ...
 *      MSERDES(MIND(ptr_provider_()), x_, ptr1_, ptr2_, ...)    // include pointers
 *
 *   private:
 *      someClass           x_;
 *      const char *        ptr1_, ptr2_;
 *
 *      std::vector<void*>   ptr_provider_() const              // provider for serialize()
 *                           { return std::vector<void*>{(void*)ptr1_, (void*)ptr2_}; }
 *      std::vector<void*>  ptr_provider_() {                   // provider for deserialize()
 *                           std::vector<void*> vp;
 *                           vp.push(...);                      // build/calculate addresses
 *                           ...                                // as required for restoration
 *                           return vp;
 *                          }
 *  };
 *
 * In fact, ptr_provider() could have recreated pointers directly and returned an empty
 * container, that way there would be no need enlisting pointers in the MSERDES macro,
 * compare:
 *
 * *  class ResourceHandler {
 *   public:
 *      ...
 *      MSERDES(MIND(ptr_provider_()), x_, ...)                 // no pointers included
 *
 *   private:
 *      someClass           x_;
 *      const char *        ptr1_, ptr2_;
 *
 *      std::vector<void*>   ptr_provider_() const              // provider for serialize call
 *                           { return std::vector<void*>{}; }
 *      std::vector<void*>  ptr_provider_() {                   // provider for deserialize()
 *                           ptr1_ = ...;                       // reinstate pointers directly
 *                           ptr2_ = ...;
 *                           return std::vector<void*>{};
 *                          }
 *  };
 *
 * RSERDES interface explained:
 *
 * RSERDES is used for serdes'ing a recurrent data structures with recursive pointer(s):
 *
 *
 *  class RecurisveClass {
 *   public:
 *      ...
 *      RSERDES(MIND(rp1_, rp2_), REMIND(rp1_, rp2_), s_, rp1_, rp2_, ...)
 *
 *   private:
 *      std::sting          s_;
 *      RecurisveClass *    rp1_, rp2_;
 *      ...
 *  };
 *
 * MIND() macro as before has to list all the pointers (or pointer_provider calls()),
 * including recursive pointer(s) to be serialized.
 * REMIND() macro only enlists recursive pointer(s) (pointers to own class).
 * there's no need to worry about actualization of recursive pointers though
 * - compiler knows the type of the data structure behind the pointer and will
 * take care of actualizing (recreating) the data for recursive pointer itself
 * - in a way, it's easier even than MSERDES interface (considering only recursive
 * pointers to be listed), because recursive pointer(s) just need to be listed
 * in both MIND and REMIND() macros to work and no worry about actualization of
 * those.
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
 * - also could read from istream using Blob constructors (see above).
 *
 * Though reading poses a dilemma if multiple blobs could be stored in a single
 * file: when reading, blob it does not know its size, so data will be read until
 * end of file.
 * Best approach is to store multiple blobs into a database.
 * If a storage for blobs has to be a file, then some extra handling is required:
 * - when first blob is read, restore all its targets, then offset() of the blob
 *   will tell the true size of the blob, then, based on the offset value, file's
 *   offset could be reinstated at the position of the next blob and operation
 *   repeated (alternatively remaining of 'unparsed' blob's data could be copied
 *   into a new blob)
 *
 */

#pragma once

#include <vector>
#include <set>
#include <map>
#include <utility>      // std::forward, std::move,
#include <cstdint>      // uint8_t, ...
#include <type_traits>
#include "enums.hpp"
//#include "dbg.hpp"


// return std::exception from all classes
class Jexcept: public std::exception {
 public:
    const char *        what() const noexcept { return mp_; }
    Jexcept &           operator()(const char *msg) { mp_ = msg; return *this; }
 private:
    const char *        mp_{nullptr};
};



// SERDES is a simplest form of interface allowing serialization/deserialization
// of any fundamental types of data, SERDES'able classes (those, defined with SERDES
// interface), arrays and containers of mentioned data types.
// This interface does not support pointers.
// declaration syntax:
//      SERDES(a, b, c, etc)
#define SERDES(Args...) \
        void serialize(Blob &__blob__) const { __blob__.append(Args); } \
        void deserialize(Blob &__blob__) { __blob__.restore(Args); }



// MSERDES is an extension of SERDES interface, in addition allowing user data
// to serialize/deserialize pointers.
// Declaration syntax:
//      MSERDES(MIND(add1, add2), x, ptr1, ptr2, etc)       //, or
//      MSERDES(MIND(ptr_provider(), etc), x, ptr1, ptr2, etc)
// in the above example add1 and add2 - are the references to data structures held
// in the respective pointers ptr1 and ptr2
#define MIND(MPTRS...) MPTRS
#define MSERDES(MPTRS, ARGS...) \
        void serialize(Blob &__blob__) const \
         { __blob__.mind(nullptr, MPTRS); __blob__.append(ARGS); } \
        void deserialize(Blob &__blob__) \
         { __blob__.restore(ARGS); __blob__.mind(nullptr, MPTRS); __blob__.remind(); }
// MSERDES interface does not serdes data pointed by pointers, it only takes care
// of the pointers themselves.
// MIND() must enumerate all actual data locations and the order must match
// the order of pointers enumerated for SERDER.
//
// Explanation of MSERDES idea:
// 1. MIND() section will build a dictionary of all data addresses: it will map
//    all data locations to own unique id (UID). Duplicated addresses (locations)
//    are allowed (but not processed - they represent the same, already allocated
//    UID), e.g:
//    MIND(&l1, &l2, &l3, &l1) will result in a container: [&l1, &l2, &l3], where
//    index of a location (address) represents its UID
// 2. when serializing data, building of locations dictionary occurs first (before
//    serialization. Upon encountering a pointer - search the pointer in the dictionary
//    and put into a blob its UID instead. If none found - throw (location is missed
//    in MIND()).
//    Dictionary (MIND) may contain more addresses (locations) than actually will
//    be used during serialization/deserialization.
// 3. when deserializing data and encountering a pointer, a pointer's location
//    (i.e. a pointer's address) is stowed away under the index which corresponds
//    to the restored UID. Because multiple pointers may point to the same data,
//    the stowing structure must be a container, e.g.:
//      restore(p1, p2, p3, p1), from above blob (in step 1) results in:
//      UID0: { pl1, pl4 }, UID1: { pl2 }, UID2: { pl3 }
// 4. MIND() phase occurs right after deserialization. same process as in step 1
// 5. rebuild (re-mind) pointers - occurs after mind() and only during deserialization:
//    using rebuilt pointers in step 4, restore all the pointers into their respective
//    locations (ordinal number = UID of the pointer), e.g., for above sequence
//    of pointer in mind:
//      [uid 0]: *pl1 = *pl4 = p1;
//      [uid 1]: *pl2 = p2;
//      [uid 2]: *pl3 = p3;
// nullptr in mind() call is not mandatory, but is required in case if user would
// use MSERDES before RSERDES interface in the same restore() operation



// RSERDES is an extension of MSERDES interface, in addition allowing recursive
// pointers. Provide in REMIND() section a list of recursive pointers. All of
// recursive pointers must be listed in MIND() section too, but exceptionally those
// don't have to be actual - REMIND() section will take care of their actualization
// e.g.:
//     RSERDES(MIND(add1, add2, ptr3), REMIND(ptr3), x1, x2, ptr1, ptr2, ptr3)
//
#define REMIND(RPTRS...) RPTRS
#define __SDSR_SX__(X) if(X) X->serialize(__blob__);
#define __SDSR_NX__(X) if(__blob__.is_actual(&X)) X = new remove_reference<decltype(*X)>::type;
#define __SDSR_DX__(X) if(X) X->deserialize_recursive(__blob__);

#define RSERDES(MPTRS, RPTRS, ARGS...) \
        void serialize(Blob &__blob__) const { \
         __blob__.mind(nullptr, MPTRS); __blob__.append(ARGS); \
         MACRO_TO_ARGS(__SDSR_SX__, RPTRS); \
        } \
        void deserialize(Blob &__blob__) { \
         __blob__.restore(ARGS); \
         MACRO_TO_ARGS(__SDSR_NX__, RPTRS); \
         __blob__.mind(nullptr, MPTRS); \
         MACRO_TO_ARGS(__SDSR_DX__, RPTRS); \
         __blob__.remind(); \
        } \
        void deserialize_recursive(Blob &__blob__) { \
         __blob__.restore(ARGS); \
         MACRO_TO_ARGS(__SDSR_NX__, RPTRS); \
         __blob__.mind(nullptr, MPTRS); \
         MACRO_TO_ARGS(__SDSR_DX__, RPTRS); \
        }

// RSERDES notes:
// 0. nullptr used in mind() as 1st argument ensures that nullptr always has UID0
// 1. restore args:
//    by the time mpl_ (storage for all pointer locations requiring restorations
//    - by their UIDs) is built, it looks like:
//      { UID0->set0{plA, plB, ...}, UIDN->setN{plM, plN, ...}, ... }
//    set0 in UID0 will contain all pointer locations {plA, plB, ...} which holds
//   'nullptr'
// 2. if any of recursive pointers are in mpl_[0] - don't create for those recursive
//    nodes, otherwise do it (it's a non-null pointer)
//
// CAUTION: if you find yourself using MSERDES or RSERDES interfaces, quite likely
//          your class design is bad. Modern C++ let getting away w/o using pointers
//          in most cases (no pointers => no memory leaks).



class Blob{
 public:
    typedef unsigned int uint;

    #define THROWREASON \
                UnaccountedPointer, \
                DuplicatePointerLocation, \
                MissingPointers, \
                DataCorruption
    ENUMSTR(ThrowReason, THROWREASON)

  friend std::ostream & operator<<(std::ostream &os, Blob & self) {
                         os.write(reinterpret_cast<const char*>(self.data()), self.size());
                         return os;
                        }
  // blob size is unknown (until parsed), so will read until end of stream
  friend std::istream & operator>>(std::istream &is, Blob & self) {
                         size_t file_size, is_pos(is.tellg());
                         file_size = is.seekg(0, std::ios_base::end).tellg();
                         self.store().resize(file_size - is_pos);
                         is.seekg(is_pos)
                           .read(reinterpret_cast<char*>(self.data()), file_size - is_pos);
                         return is;
                        }
                         //{  while(is.rdstate()==0) self.store().push_back(is.get()); return is; }

                        Blob(void) = default;                   // DC
                        Blob(const Blob &) = default;           // CC
                        Blob(Blob &&) = default;                // MC
    Blob &              operator=(const Blob &) = default;      // CA
    Blob &              operator=(Blob &&) = default;           // MA

                        // constructors to dump target data into blob:
                        // Blob(src1, src2, ...);
                        template<typename... Args>
    explicit            Blob(Args & ... rest) { append(rest...); }

                        // constructor to restore Blob
                        // handy to use with istream_iterator's:
                        template<class Iter> Blob(Iter first, Iter last)
                         { while(first != last) store().push_back(*first++); }

                        // constructor to restore Blob and immediately reinstate target data
                        // handy to use with istream_iterator's, may throw though:
                        template<class Iter, class T> Blob(Iter first, Iter last, T &t)
                         { while(first != last) store().push_back(*first++); restore(t); }



    // User interface to work with Blob:
                        // reset will clear all data structures except blob itself,
                        // as if it was just read from somewhere
    Blob &              reset(void)
                         { vp_.clear(); mpu_.clear(); mpl_.clear(); offset_=0; return *this; }
    Blob &              clear(void) { blob_.clear(); return reset(); }
    size_t              offset(void) const { return offset_; }
    size_t              size(void) const { return blob_.size(); }
    bool                empty(void) const { return blob_.size() == 0; }
    uint8_t *           data(void) { return blob_.data(); }
    const uint8_t *     data(void) const { return blob_.data(); }
 std::vector<uint8_t> & store(void) { return blob_; }

    // Interfaces facilitating SERDES macros
                        // all pointers have to be registered via method mind() -
                        // to build a dictionary of pointers
                        // this method processes a pointer value
    template<typename T, typename... Args>
    typename std::enable_if<std::is_pointer<T>::value, void>::type
                        mind(T ptr, Args&&... rest)
                         { mind_((void*)ptr); mind(std::forward<decltype(rest)>(rest)...); }
                        // this call is a nullptr enabler, which is required
                        // for implementation of RSERDES interface
    template<typename T, typename... Args>
    typename std::enable_if<std::is_null_pointer<T>::value, void>::type
                        mind(T ptr, Args&&... rest)
                         { mind_(nullptr); mind(std::forward<decltype(rest)>(rest)...); }
                        // this method processes a linear const container of pointers -
                        // returned by user's pointer provider
    template<template<typename, typename> class Container,
             typename T, typename Alloc, typename ...Args>
    typename std::enable_if<std::is_pointer<T>::value, void>::type
                        mind(Container<T, Alloc> && c, Args &&... rest)
                         { mind_(c); mind(std::forward<decltype(rest)>(rest)...); }

                        // rebuilds all pointers in vp_, into corresponding recorded
                        // locations in mpl_. this call is used only during deserialization
    void                remind(void) {
                         if(mpl_.empty()) return;
                         if(mpl_.size() > vp_.size())           // # of pointer locations cannot be
                          throw EXP(MissingPointers);           // higher than # of minded pointers
                         for(auto &spl: mpl_)
                          for(auto &pl: spl.second)
                           *(void**)pl = vp_[spl.first];
                         mpl_.clear();
                        }

                        // is_actual() is used by RSERDES interface only:
                        // mpl_[0] contains all pointer locations that later (during remind())
                        // will be assigned to a 'nullptr' value - such pointer locations are
                        // non-actual; all others are actual - which means that RSERDES must
                        // recreate recursive data structures for all such pointers
    template<typename T, typename... Args>
    typename std::enable_if<std::is_pointer<T>::value, bool>::type
                        is_actual(T ptr)
                         { return mpl_[0].find(ptr) == mpl_[0].end(); }

    //
    // ... APPEND [to the blob] methods
    //
                        // generic, data-type agnostic append
    void                append_raw(const void *ptr, size_t s)
                         { for(size_t i=0; i<s; ++i) blob_.push_back(((uint8_t*)ptr)[i]); }
                        // 0. variadic append
    template<typename T, typename... Args>
    Blob &              append(const T & first, const Args &... rest)
                         { append(first); append(rest...); return *this; }
                        // 1a. atomic (fundamental) type append
    template<typename T>
    typename std::enable_if<std::is_fundamental<T>::value, void>::type
                        append(T v) { append_raw(&v, sizeof(T)); }
                        // 1b. atomic (enum) type append
    template<typename T>
    typename std::enable_if<std::is_enum<T>::value, void>::type
                        append(T v) { append_raw(&v, sizeof(T)); }
                        // 1c. custom class append (custom class must provide serialize(Blob &))
    template<typename T>
    typename std::enable_if<std::is_member_function_pointer<decltype(& T::serialize)>::value,
                            void>::type
                        append(const T & v) { v.serialize(*this); }
                        // 2a. native arrays append
    template<typename T>
    typename std::enable_if<std::is_array<T>::value, void>::type
                        append(const T &v)
                         { for(int i=0, s=sizeof(v)/sizeof(v[0]); i<s; ++i) append(v[i]); }
                        // 2b. linear containers - single data allocator append
    template<template<typename, typename> class Container, typename T, typename Alloc>
    typename std::enable_if<std::is_member_function_pointer<decltype(& Alloc::allocate)>::value,
                            void>::type
                        append(const Container<T, Alloc> & c)
                         { appendCntr_(c.size()); for(const T &v: c) append(v); }
                        // 2c. basic string append
    void                append(const std::basic_string<char> & c)
                         { appendCntr_(c.size()); for(char v: c) append(v); }
                        // 2d. complex container append: map
    template<template<typename, typename, typename, typename> class Map,
             typename K, typename V, typename C, typename A>
    typename std::enable_if<std::is_member_function_pointer<decltype(& A::allocate)>::value,
                            void>::type
                        append(const Map<K, V, C, A> & c) {
                         appendCntr_(c.size());
                         for(auto &v: c)
                          { append(v.first); append(v.second); }
                        }
                        // 3a. pointer append
    template<typename T>
    typename std::enable_if<std::is_pointer<T>::value, void>::type
                        append(const T &v) {
                         auto p = mpu_.find((void*)v);
                         if(p == mpu_.end())
                          throw EXP(UnaccountedPointer);
                         append(p->second);                     // append idx (UID) of ptr
                        }

    //
    // ... RESTORE [from the blob] methods
    //
                        // 0. variadic restore
    template<typename T, typename... Args>
    Blob &              restore(T & first, Args &... rest)
                         { restore(first); restore(rest...); return *this; }
                        // 1a. atomic (fundamental) restore
    template<typename T>
    typename std::enable_if<std::is_fundamental<T>::value, void>::type
                        restore(T &r)
                         { r = *(T*)&blob_.at(offset_); offset_ += sizeof(T); }
                        // 1b. atomic (enum) restore
    template<typename T>
    typename std::enable_if<std::is_enum<T>::value, void>::type
                        restore(T &r)
                         { r = *(T*)&blob_.at(offset_); offset_ += sizeof(T); }
                        // 1c. custom class restore (custom class must provide deserialize(Blob &))
    template<typename T>
    typename std::enable_if<std::is_member_function_pointer<decltype(& T::deserialize)>::value,
                            void>::type
                        restore(T & v) { v.deserialize(*this); }
                        // 2a. native arrays restore
    template<typename T>
    typename std::enable_if<std::is_array<T>::value, void>::type
                        restore(T &r)
                         { for(int i=0, s=sizeof(r)/sizeof(r[0]); i<s; ++i) restore(r[i]); }
                        // 2b. linear container restore: vector/list/dequeue/array
    template<template<typename, typename> class Container, typename Alloc, typename T>
    typename std::enable_if<std::is_member_function_pointer<decltype(& Alloc::allocate)>::value,
                            void>::type
                        restore(Container<T, Alloc> & c)
                         { c.resize(restoreCntr_()); for(auto &v: c) restore(v); }
                        // 2c. basic string restore
    void                restore(std::basic_string<char> & c)
                         { c.resize(restoreCntr_()); for(auto &v: c) restore(v); }
                        // 2d. complex container restore: map
    template<template<typename, typename, typename, typename> class Map,
             typename K, typename V, typename C, typename A>
    void                restore(Map<K, V, C, A> & c) {
                         size_t l{restoreCntr_()};
                         for(size_t i=0; i<l; ++i)
                          { K k; V v; restore(k); restore(v); c.emplace(k, v); }
                        }
                        // 3. pointer restore: delay restoring, instead, stow away the location
                        // of the pointer holder; actual restoration of pointers occurs during
                        // remind(), once all data are restored and dictionary of pointers is built
    template<typename T>
    typename std::enable_if<std::is_pointer<T>::value, void>::type
                        restore(T &ptr) {
                         unsigned uid; restore(uid);
                         auto &spl = mpl_[uid];                 // get set of pointer locations
                         if(spl.size() != 0)
                          if(spl.find(&ptr) != spl.end())       // ptr was enumerated before
                           throw EXP(DuplicatePointerLocation); // deemed to be a programming error
                         spl.insert((void*)&ptr);
                        }

 protected:
    size_t              offset_{0};
   std::vector<uint8_t> blob_;

    // when RSERDES was introduced, it became apparent, that vector-based storing
    // of pointers and locations is sub-efficient, thus vectors were retired and
    // replaced with map:
    // mpl_: { UID0 -> {set4uid0}, ..., UIDn-> {pl1, pl2, ...}, ... }
    // vp_ now needs to be stored (for faster append and restore) as vector of
    // pointers addressed by UID (for re-mind)
                        // mpu_ is built by mind_() and used in append<pointer>()
  std::map<void*, uint> mpu_;                                       // maps pointer -> uid
                        // vp_ is built by mind_() and used in remind()
    std::vector<void*>  vp_;                                        // vector of pointers
                        // mpl_ is built by restore<pointer> and used in remind() and is_actual()
    std::map<uint, std::set<void*>>
                        mpl_;                                      // map of pointer locations

                        // mind methods create pointers dictionary
    void                mind(void) {}                               // variadic mind terminator
    void                mind_(void* ptr) {                          // build mpl_, vp_
                         if(mpu_.find(ptr) != mpu_.end()) return;   // duplicated ptr: do nothing
                         mpu_.emplace(ptr, vp_.size());
                         vp_.push_back(ptr);                        // idx of a ptr - is its UID!
                        }
                        // this one process return result from user's pointer-providers
    template<template<typename, typename> class Container, typename T, typename Alloc>
    void                mind_(const Container<T, Alloc> & c)
                         { for(auto v: c) mind_((void *)v); }

 private:
    // save up some space when serializing counters. Don't attempt moving counter
    // size to bits-space of the counter itself - it will break then either of
    // endianess.
    // any further optimization of serialized space must be done with compression
    // algorithms
    void                appendCntr_(size_t s) {
                         uint8_t cs = counterSize_(s);
                         append(cs);
                         switch(cs) {
                          case 0: append((uint8_t)s); break;
                          case 1: append((uint16_t)s); break;
                          case 2: append((uint32_t)s); break;
                          case 3: append((uint64_t)s); break;
                          default: throw EXP(DataCorruption);
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
                          default: throw EXP(DataCorruption);
                         }
                        }
    uint8_t             counterSize_(size_t cntr) {
                         static size_t bound[3]{1ul<<8, 1ul<<16, 1ul<<32};
                         for(int i=sizeof(bound)/sizeof(bound[0])-1; i>=0; --i)
                          if(cntr >= bound[i])
                           return i+1;
                         return 0;
                        }

    EXCEPTIONS(ThrowReason)

};

STRINGIFY(Blob::ThrowReason, THROWREASON)
#undef THROWREASON

















