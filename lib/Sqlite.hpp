/*
 *  Created by Dmitry Lyssenko, last modified August 27, 2018
 *
 *
 * This is a simple lightweight sqlite3 wrapper (upon compiling don't forget to include
 * -lsqlite3).
 * the wrapper is meant to be written to support c++ idiomatic interface
 *
 * SYNOPSIS:
 * 1. create / open database object:
 *
 *  Sqlite db;              // or Sqlite db(filename, flags);
 *
 *  DBG().severity(db);     // class is DEBUGGABLE (see dbg.hpp)
 *  db.open("sql.db");      // open database file
 *
 *
 * 2. compile SQL statements
 *
 *  db.compile("CREATE TABLE IF NOT EXISTS a_table"
 *           " (Idx INTEGER PRIMARY KEY NOT NULL,"
 *           "  Description TEXT NOT NULL,"
 *           "  Rank REAL NOT NULL,"
 *           "  Data BLOB);");
 *  // SQL statements w/o parameters which do not facilitate READ or WRITE operations
 *  // are executed immediately, while for others the execution is deferred
 *
 *
 * 3. write SQL statements
 *
 *  db.compile("INSERT OR REPLACE INTO a_table VALUES (?,?,?,?)");
 *  db << 1 << "first line" << 0.1 << nullptr;
 *  db << 2 << "second line" << 0.2 << nullptr;
 *
 *  // write execution occurs automatically, when number of parameters passed to
 *  // the Sqlite object matches number of parameters in the compile SQL statement
 *  // also, no need recompiling SQL statement when it does not change in between
 *  // output operations
 *
 *
 * 4. read SQL statements
 *
 *  int idx;
 *  string str;
 *  double rank;
 *  Blob blob;
 *
 *  db.compile("SELECT * from a_table;");
 *  while(true) {
 *   db >> idx >> str >> rank >> blob;
 *   if(db.rc() != SQLITE_ROW) break;
 *   cout << "read " << idx << " string: " << str << ", rank: " << rank << endl;
 *  }
 *
 * Output:
 *
 *  read 1 string: first line, rank: 0.1
 *  read 2 string: second line, rank: 0.2
 *
 * // when reading NULL for any of parameters, following rule applies:
 * //  - numbers read value 0
 * //  - string is empty
 * //  - blob is empty (0 size)
 *
 *
 * 5. SQLIO interface:
 *
 *  // a custom class could be defined to support SQLIO interface, that way read/write
 *  // operations are simplified. Say, a following data struct is defined in a global
 *  // scope:
 *
 *  struct Row {
 *      int         idx;
 *      string      str;
 *      double      rank;
 *      Blob        blob;
 *                  SQLIO(idx, str, rank, blob)                 // struct is readable/writable
 *                  SERDES(idx, str, rank)                      // struct now is (de)serializable
 *                  COUTABLE(Row, idx, str, rank, blob.size())  // struct now is compact-outable
 *  };
 *
 *  // ... in main.c:
 *
 *   Row r{3, "third line", 0.3};
 *   r.blob.append(r);
 *
 *  db.compile("INSERT OR REPLACE INTO a_table VALUES (?,?,?,?)");
 *  db << r;
 *
 *  db.compile("SELECT * from a_table WHERE Idx==?");
 *  db << 3 >> r;
 *  cout << r << endl;
 *
 * Output:
 *
 *  Row.. idx:3, str:"third line", rank:0.3, blob.size():24
 *
 *
 * 6. Containers and SQLIO interface:
 *
 *  // entire table could be read at once into a container, if container's data
 *  // is defined with SQLIO interface:
 *
 *  vector<Row> v_row;
 *  db.compile("SELECT * from a_table;");
 *  db >> v_row;
 *
 *  for(auto &r: v_row)
 *   cout << r << endl;
 *
 * Output:
 *
 *  Row.. idx:1, str:"first line", rank:0.1, blob.size():0
 *  Row.. idx:2, str:"second line", rank:0.2, blob.size():0
 *  Row.. idx:3, str:"third line", rank:0.3, blob.size():24
 *
 */
#pragma once

#include <sqlite3.h>
#include <string>
#include <memory>
#include <type_traits>
#include "macrolib.h"
#include "extensions.hpp"
#include "Blob.hpp"
#include "dbg.hpp"



#define __SQLITE_OUT_X__(X) << X
#define __SQLITE_IN_X__(X) >> X

// SQLIO enables user-defined classes to become SQL readable/writable via
// << / >> operators
#define SQLIO(ARGS...) \
        Sqlite & __sqlite_db_out__(Sqlite &__db__) const \
         { return __db__ MACRO_TO_ARGS(__SQLITE_OUT_X__, ARGS); } \
        Sqlite & __sqlite_db_in__(Sqlite &__db__) \
         { return __db__ MACRO_TO_ARGS(__SQLITE_IN_X__, ARGS); }





class Sqlite {
    friend void         swap(Sqlite &l, Sqlite &r) {
                         using std::swap;                       // enable ADL
                         swap(l.dbp_, r.dbp_);
                         swap(l.ppStmt_, r.ppStmt_);
                         swap(l.headers_, r.headers_);
                         swap(l.htypes_, r.htypes_);
                         swap(l.hdtypes_, r.hdtypes_);
                         swap(l.rc_, r.rc_);
                         swap(l.ts_, r.ts_);
                         swap(l.pi_, r.pi_);
                         swap(l.pc_, r.pc_);
                         swap(l.ci_, r.ci_);
                         swap(l.cc_, r.cc_);
                         swap(l.sne_, r.sne_);
                         swap(l.lsql_, r.lsql_);
                        }

 public:
    #define THROWREASON \
                failed_opening_db, \
                could_not_begin_transaction, \
                could_not_destroy_sql_statement, \
                could_not_end_transaction, \
                could_not_compile_sql_statement, \
                must_not_recompile_while_in_transaction, \
                could_not_evaluate_sql_statement, \
                could_not_reset_compiled_ctatement, \
                could_not_bind_parameter, \
                could_not_clear_bindings, \
                EndOfRows
    ENUMSTR(ThrowReason, THROWREASON)


    #define THROWING \
                may_throw, \
                dont_throw
    ENUM(Throwing, THROWING)


    #define TRANSACTION \
                out_of_transaction, \
                in_transaction_precompiled, \
                in_transaction_compiled
    ENUMSTR(Transaction, TRANSACTION)


    #define DATATYPE \
                Illegal, \
                Integer, \
                Real, \
                Text, \
                Blob, \
                Null
    ENUMSTR(DataType, DATATYPE)


                        Sqlite(void) = default;                 // DC
                        Sqlite(const Sqlite &) = delete;        // CC: class is not copyable
                        Sqlite(Sqlite && other): Sqlite()       // MC: class is movable
                         { swap(*this, other); }
                        Sqlite(const std::string &filename,     // open constructor, may throw
                               int flags=SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE);
                       ~Sqlite(void) { if(dbp_) close(dont_throw); } // DD

    Sqlite &            operator=(const Sqlite & jn) = delete;  // CA: class is not copy assignable
    Sqlite &            operator=(Sqlite && other)              // MA: class move assignable
                         { swap(*this, other); return *this; }


    // file and transaction operations
    Sqlite &            open(const std::string &fn,
                             int flags=SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE);
    Sqlite &            close(Throwing = may_throw);
    sqlite3 **          dbp(void) { return & dbp_; };
    Sqlite &            begin_transaction(void);
    Sqlite &            end_transaction(Throwing = may_throw);
    Sqlite &            compile(const std::string &str);
    Sqlite &            reset(void);
    Sqlite &            finalize(void);
    int                 rc(void) { return rc_; }


    typedef std::vector<std::string> v_string;
    typedef std::vector<DataType> v_data;

    const v_string &    headers(void) { return headers_; }      // column names
    const v_data &      hdr_types(void) { return htypes_; }     // data types
    const v_string &    hdr_dcl_types(void) { return hdtypes_; }// column decltypes
    const char *        column_name(int i)
                         { return sqlite3_column_name(ppStmt_, i); }
    DataType            data_type(int i)
                         { return (DataType)sqlite3_column_type(ppStmt_, i); }
    const char *        column_decltype(int i)
                         { return sqlite3_column_decltype(ppStmt_, i); }
    int                 column_count(void)
                         { return sqlite3_column_count(ppStmt_); }
    Sqlite &            fill_headers(void)              // commands to fill headers upon next exec
                         { headers_.clear(); htypes_.clear(); hdtypes_.clear(); return *this; }


    // OUTPUT Storage Classes:
    template<typename T>
    Sqlite &            write(const T &val) { return operator<<(val); }
    Sqlite &            operator<<(std::nullptr_t x);           // NULL
    Sqlite &            operator<<(int64_t i);                  // INTEGER
    template<typename F>
        typename std::enable_if<std::is_floating_point<F>::value, Sqlite>::type &
                        operator<<(F d);                        // REAL
    Sqlite &            operator<<(const std::string & str);    // TEXT
    Sqlite &            operator<<(const class Blob &);         // BLOB

    template<typename T>                                        // custom types with SQLIO
        typename std::enable_if<
                    std::is_member_function_pointer<decltype(& T::__sqlite_db_out__)>::value,
                    Sqlite>::type &
                        operator<<(const T& v)
                         { return v.__sqlite_db_out__(*this); }

    template<template<typename, typename> class Container, typename T, typename Alloc>
    Sqlite &            operator<<(const Container<T, Alloc> & c)     // linear containers
                         { for(auto &r: c) *this << r; return *this; }

    template<template<typename, typename, typename, typename> class Map,
             typename Key, typename Val, typename Cmp, typename Alc>    // map container
    Sqlite &            operator<<(const Map<Key, Val, Cmp, Alc> & c)
                         { for(auto &r: c) *this << r.first << r.second; return *this; }

                        // ...add other containers as needed...

    // INPUT Storage Classes:
    template<typename T>
    Sqlite &            read(T &val) { return operator>>(val); }
    Sqlite &            operator>>(int64_t &i);                 // INTEGER
    Sqlite &            operator>>(int32_t &i)
                         { int64_t v; *this >> v; i = (int32_t)v; return *this; }
    Sqlite &            operator>>(int16_t &i)
                         { int64_t v; *this >> v; i = (int16_t)v; return *this; }
    Sqlite &            operator>>(int8_t &i)
                         { int64_t v; *this >> v; i = (int8_t)v; return *this; }
    Sqlite &            operator>>(double &i);                  // REAL
    Sqlite &            operator>>(std::string &i);             // TEXT
    Sqlite &            operator>>(class Blob &b);              // BLOB

    template<typename T>                                        // custom types with SQLIO
    typename
        std::enable_if<std::is_member_function_pointer<decltype(& T::__sqlite_db_in__)>::value,
                       Sqlite>::type &
                        operator>>(T& v)
                         { return v.__sqlite_db_in__(*this); }

    template<template<typename, typename> class Container, typename T, typename Alloc>
    Sqlite &            operator>>(Container<T, Alloc> & c) {   // linear containers
                         while(true) {
                          T value;
                          if((*this >> value).rc() != SQLITE_ROW) break;
                          c.push_back( std::move(value) );
                         }
                         return *this;
                        }

    // map container
    template<template<typename, typename, typename, typename> class Map,
                      typename Key, typename Val, typename Cmp, typename Alc>
    Sqlite &            operator>>(Map<Key, Val, Cmp, Alc> & c) {   // map container
                         while(true) {
                          Key key; Val value;
                          if((*this >> key).rc() != SQLITE_ROW) break;
                          if((*this >> value).rc() != SQLITE_ROW) break;
                          c.emplace(std::move(key), std::move(value));
                         }
                         return *this;
                        }
    DEBUGGABLE()

 protected:
    Sqlite &            exec_(void);
    void                maybeRecompileCachedSql_(void);

    sqlite3 *           dbp_{nullptr};                          // dp ptr
    sqlite3_stmt *      ppStmt_{nullptr};                       // pointer to a prepared statement

    v_string            headers_{std::string()};                // headers
    v_data              htypes_;                                // header types
    v_string            hdtypes_;                               // headers decl types
    int                 rc_{SQLITE_OK};                         // last result code

 private:
    Transaction         ts_{out_of_transaction};                // 0/1/2:  out/in_nosql/in_sql
    int                 pi_{1};                                 // parameter index
    int                 pc_{0};                                 // parameter count
    int                 ci_{0};                                 // column index
    int                 cc_{0};                                 // column count
    bool                sne_{false};                            // skip next exec_()
    std::string         lsql_;                                  // cached last sql statement

    EXCEPTIONS(ThrowReason)                                     // see "extensions.hpp"
};

STRINGIFY(Sqlite::ThrowReason, THROWREASON)
#undef THROWREASON
#undef THROWING

STRINGIFY(Sqlite::Transaction, TRANSACTION)
#undef TRANSACTION

STRINGIFY(Sqlite::DataType, DATATYPE)
#undef DATATYPE





Sqlite::Sqlite(const std::string &filename, int flags) {
 rc_ = sqlite3_open_v2(filename.c_str(), &dbp_, flags, nullptr);
 if(rc_ != SQLITE_OK)
  throw EXP(failed_opening_db);
}



Sqlite & Sqlite::open(const std::string &filename, int flags) {
 rc_ =  sqlite3_open_v2(filename.c_str(), & dbp_, flags, nullptr);
 DBG(0)
  DOUT() << "openning file/flags: " << filename << '/' << flags
         << ", tr/rc: " << ts_ << '/' << rc_ << std::endl;
 if(rc_ != SQLITE_OK)
  throw EXP(failed_opening_db);
 return *this;
}



Sqlite & Sqlite::close(Throwing throwing) {
 if(ts_ != out_of_transaction) end_transaction(throwing);
 finalize();
 const char * fn{nullptr};
 if(DBG()(0)) fn = sqlite3_db_filename(dbp_, nullptr);
 rc_ = sqlite3_close_v2(dbp_);
 DBG(0)
  DOUT() << "closed db '" << (fn?fn:"") << "', tr/rc: " << ts_ << '/' << rc_ << std::endl;
 dbp_ = nullptr;
 ppStmt_ = nullptr;
 return *this;
}



Sqlite & Sqlite::begin_transaction(void) {
 // 0 = out of transaction;
 // 1 = transaction is open but no SQL statement is compiled
 // 2 = transaction is open and SQL statement was compiled
 if(ts_ >= in_transaction_precompiled) return *this;            // should be called only once
 finalize();
 rc_ = sqlite3_exec(dbp_, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
 if(rc_ != SQLITE_OK) {
  DBG(1) DOUT() << "began transaction, tr/rc: " << ts_ << '/' << rc_ << std::endl;
  throw EXP(could_not_begin_transaction);
 }
 ts_ = in_transaction_precompiled;
 DBG(1) DOUT() << "began transaction, tr/rc: " << ts_ << '/' << rc_ << std::endl;
 return *this;
}



Sqlite & Sqlite::end_transaction(Throwing throwing) {
 // ends transaction upon successful return code and rolls back otherwise
 bool rolled{false};
 if(ts_ == in_transaction_compiled)
  finalize();
 if(rc() AMONG(SQLITE_OK, SQLITE_DONE, SQLITE_CONSTRAINT, SQLITE_ROW))                                      // good return codes
  rc_ = sqlite3_exec(dbp_, "END TRANSACTION", nullptr, nullptr, nullptr);
 else
  { rc_ = sqlite3_exec(dbp_, "ROLLBACK", nullptr, nullptr, nullptr); rolled = true; }

 ts_ = out_of_transaction;
 DBG(1)
  DOUT() << "ended transaction" << (rolled? "(via rollback)": "")
         << ", tr/rc: " << ts_ << '/' << rc_ << std::endl;

 if(rc_ != SQLITE_OK and throwing == may_throw)
  throw EXP(could_not_end_transaction);
 return *this;
}



Sqlite & Sqlite::compile(const std::string &sql) {
 // compile user sql statement
 if(ts_ == in_transaction_compiled)                             // considered to be a user error
  throw EXP(must_not_recompile_while_in_transaction);

 finalize();
 lsql_ = sql;                                                   // cache user sql statement
 DBG(1) DOUT() << "compiling SQL: " << lsql_ << std::endl;

 rc_ = sqlite3_prepare_v2(dbp_, sql.c_str(), -1, & ppStmt_, nullptr);
 DBG(2) DOUT() << "prepared statement, tr/rc: " << ts_ << '/' << rc_ << std::endl;
 if(rc_ != SQLITE_OK)
  throw EXP(could_not_compile_sql_statement);

 if(ts_ == in_transaction_precompiled) ts_ = in_transaction_compiled;
 pi_ = 1;
 pc_ = sqlite3_bind_parameter_count(ppStmt_);
 ci_ = 0;
 cc_ = column_count();
 DBG(2) DOUT() << "column/parameter count in compiled: " << cc_ << '/' << pc_ << std::endl;

 if(pc_!=0 or cc_!=0) return *this;                             // defer execution until R/W ops.
 DBG(2) DOUT() << "auto-executing SQL statement..." << std::endl;
 return exec_();                                                // auto exec statement
}



Sqlite & Sqlite::reset(void) {
 DBG(3) DOUT() << "done, tr/rc: " << ts_ <<'/'<< rc_ << std::endl;
 rc_ = sqlite3_reset(ppStmt_);
 ci_ = 0;                                                       // if sql statement is reset then
 if(pi_ != 1) {                                                 // these two also have to be reset
 int rc = sqlite3_clear_bindings(ppStmt_);
 DBG(3) DOUT() << "cleared binding, tr/rc: " << ts_ <<'/'<< rc << std::endl;
 if(rc != SQLITE_OK)
  { rc_ = rc; throw EXP(could_not_clear_bindings); }
  pi_ = 1;
 }
 return *this;
}



Sqlite & Sqlite::finalize(void) {
 if(ppStmt_ == nullptr) return *this;
 rc_ = sqlite3_finalize(ppStmt_);
 ppStmt_ = nullptr;
 DBG(3) DOUT() << "done, tr/rc: " << ts_ <<'/'<< rc_ << std::endl;
 // finalize only repeat the return code from the last operation, thus do not throw
 return *this;
}



Sqlite & Sqlite::operator<<(std::nullptr_t x) {
 maybeRecompileCachedSql_();
 rc_ = sqlite3_bind_null(ppStmt_, pi_);
 DBG(3)
  DOUT() << "created null parameter binding " << pi_
         << ", tr/rc: " << ts_ << '/' << rc_ << std::endl;
 if(rc_ != SQLITE_OK)
  throw EXP(could_not_bind_parameter);
 if(++pi_ > pc_)                                                // bound all parameters
  if(exec_().rc() == SQLITE_ROW) sne_ = true;                   // tell exec_ to skip next execution
  // EXPLANATION: when a statement carries bindings parameters and it's a read operation
  // at the same time (e.g.: SELECT * FROM a_table WHERE idx>=? AND idx <=?), parameters
  // are passed in the output operator '<<', but as the last parameter is passed, exec_()
  // call applies sqlite3_step() and the 1st row of data will be read (even before a user
  // read operator '>>' is executed. Thus true value sne_ tells exec_ call to skip the
  // next reading, because data is already read and await
 return *this;
}



Sqlite & Sqlite::operator<<(int64_t i) {
 maybeRecompileCachedSql_();
 rc_ = sqlite3_bind_int64(ppStmt_, pi_, i);
 DBG(3)
  DOUT() << "created integer parameter binding " << pi_
         << ", tr/rc: " << ts_ << '/' << rc_ << std::endl;
 if(rc_ != SQLITE_OK)
  throw EXP(could_not_bind_parameter);
 if(++pi_ > pc_)                                                // bound all parameters
  if(exec_().rc() == SQLITE_ROW) sne_ = true;                   // tell exec_ to skip next execution
 return *this;
}



template<typename F>
    typename std::enable_if<std::is_floating_point<F>::value, Sqlite>::type &
Sqlite::operator<<(F d) {
 maybeRecompileCachedSql_();
 rc_ = sqlite3_bind_double(ppStmt_, pi_, d);
 DBG(3)
  DOUT() << "created real parameter binding " << pi_
         << ", tr/rc: " << ts_ << '/' << rc_ << std::endl;
 if(rc_ != SQLITE_OK)
  throw EXP(could_not_bind_parameter);
 if(++pi_ > pc_)                                                // bound all parameters
  if(exec_().rc() == SQLITE_ROW) sne_ = true;                   // tell exec_ to skip next execution
 return *this;
}



Sqlite & Sqlite::operator<<(const std::string &str) {
 maybeRecompileCachedSql_();
 rc_ = sqlite3_bind_text(ppStmt_, pi_, str.c_str(), -1, SQLITE_TRANSIENT);
 DBG(3)
  DOUT() << "created text parameter binding " << pi_
         << ", tr/rc: " << ts_ << '/' << rc_ << std::endl;
 if(rc_ != SQLITE_OK)
  throw EXP(could_not_bind_parameter);
 if(++pi_ > pc_)                                                // bound all parameters
  if(exec_().rc() == SQLITE_ROW) sne_ = true;                   // tell exec_ to skip next execution
 return *this;
}



Sqlite & Sqlite::operator<<(const class Blob &blob) {
 maybeRecompileCachedSql_();
 rc_ = sqlite3_bind_blob(ppStmt_, pi_, (const void *)blob.data(), blob.size(), SQLITE_STATIC);
 DBG(3)
  DOUT() << "created blob parameter binding " << pi_
         << ", tr/rc: " << ts_ << '/' << rc_ << std::endl;
 if(rc_ != SQLITE_OK)
  throw EXP(could_not_bind_parameter);
 if(++pi_ > pc_)                                                // bound all parameters
  if(exec_().rc() == SQLITE_ROW) sne_ = true;                   // tell exec_ to skip next execution
 return *this;
}



Sqlite & Sqlite::operator>>(int64_t &i) {
 if(ci_ == 0) {                                                 // all columns read, read next row
  if(rc_ == SQLITE_DONE) return *this;                          // prior read returned SQLITE_DONE
  if(exec_().rc() != SQLITE_ROW) return *this;                  // some error
 }
 i = sqlite3_column_int(ppStmt_, ci_);
 DBG(3)
  DOUT() << "integer read from column " << ci_
         << ", tr/rc: " << ts_ << '/' << rc_ << std::endl;
 ci_ = (ci_+1) % cc_;
 return *this;
}



Sqlite & Sqlite::operator>>(double &d) {
 if(ci_ == 0) {                                                 // all columns read, read next row
  if(rc_ == SQLITE_DONE) return *this;                          // prior read returned SQLITE_DONE
  if(exec_().rc() != SQLITE_ROW) return *this;
 }
 d = sqlite3_column_double(ppStmt_, ci_);
 DBG(3)
  DOUT() << "real read from column " << ci_
         << ", tr/rc: " << ts_ << '/' << rc_ << std::endl;
 ci_ = (ci_+1) % cc_;
 return *this;
}



Sqlite & Sqlite::operator>>(std::string &str) {
 if(ci_ == 0) {                                                 // all columns read, read next row
  if(rc_ == SQLITE_DONE) return *this;                          // prior read returned SQLITE_DONE
  if(exec_().rc() != SQLITE_ROW) return *this;
 }
 const char *ptr = reinterpret_cast<const char *>(sqlite3_column_text(ppStmt_, ci_));
 str = ptr==nullptr? "": ptr;
 DBG(3)
  DOUT() << "text read from column " << ci_
         << ", tr/rc: " << ts_ << '/' << rc_ << std::endl;
 ci_ = (ci_+1) % cc_;
 return *this;
}



Sqlite & Sqlite::operator>>(class Blob &blob) {
 if(ci_ == 0) {                                                 // all columns read, read next row
  if(rc_ == SQLITE_DONE) return *this;                          // prior read returned SQLITE_DONE
  if(exec_().rc() != SQLITE_ROW) return *this;
 }
 blob.clear().
      append_raw(sqlite3_column_blob(ppStmt_, ci_), sqlite3_column_bytes(ppStmt_, ci_));
 DBG(3)
  DOUT() << "blob read from column " << ci_
         << ", tr/rc: " << ts_ <<'/'<< rc_ << std::endl;
 ci_ = (ci_+1) % cc_;
 return *this;
}





Sqlite & Sqlite::exec_(void) {
 // there are 3 ways exec_ can be run:
 // 1. when compiled SQL does not have any bindings (i.e. it's not a WRITE operation)
 //    and SQL does not have any return results (it's not a READ operation),
 //    e.g.: CREATE TABLE ...
 //    - then exec sequence is:
 //      sqlite3_step -> sqlite3_reset/sqlite3_finalize.
 // 2. when compiled SQL has bindings (WRITE expected)
 //    - then exec sequence is:
 //      sqlite3_step -> sqlite3_clear_bindings -> sqlite3_reset/sqlite3_finalize.
 // 3. when compiled SQL has columns returned (READ expected)
 //    - then exec sequence is:
 //      sqlite3_step -> sqlite3_column_* .... -> sqlite3_step -> ....;
 //      sqlite3_reset/sqlite3_finalize.

 if(sne_ == true) {
  DBG(3)
   DOUT() << "data read in the prior call, tr/rc: " << ts_ <<'/'<< rc_ << std::endl;
  sne_ = false;                                                 // ensure next read
  return *this;
 }                                                              // data was read in a prior call
 rc_ = sqlite3_step(ppStmt_);
 DBG(3) DOUT() << "stepped through, tr/rc: " << ts_ <<'/'<< rc_ << std::endl;
 if(not(rc() AMONG(SQLITE_DONE, SQLITE_CONSTRAINT, SQLITE_ROW)))// benign return codes
  throw EXP(could_not_evaluate_sql_statement);

 if(pc_ > 0 and pc_+1 == pi_) {                                 // sql statement was with params
  int rc = sqlite3_clear_bindings(ppStmt_);                     // and all bindings are done by now
  DBG(3) DOUT() << "cleared binding, tr/rc: " << ts_ <<'/'<< rc << std::endl;
  if(rc != SQLITE_OK)
   { rc_ = rc; throw EXP(could_not_clear_bindings); }
  ++pi_;                                                        // ensure clear bindings only once
 }

 if(headers_.empty())                                           // fill headers then
  for(int i=0; i<cc_; ++i) {
   const char *ptr = sqlite3_column_name(ppStmt_, i);
   headers_.push_back( ptr==nullptr? "": ptr );
   htypes_.push_back( (DataType)sqlite3_column_type(ppStmt_, i) );
   ptr = sqlite3_column_decltype(ppStmt_, i);
   hdtypes_.push_back( ptr==nullptr? "": ptr );

  }

 if(cc_ > 0) return *this;                                      // don't finalize/reset until DONE

 if(ts_ >= in_transaction_precompiled)
  { int rc = rc_; reset(); rc_ = rc; return *this; }            // preserve rc from sqlite3_step()

 int rc = rc_; finalize(); rc_ = rc;                            // preserve rc from sqlite3_step()
 return *this;
}




void Sqlite::maybeRecompileCachedSql_(void) {
 // auto-recompilation is handy for those cases, where SQL statement does not
 // change between writes (sql and parameters remain of the same type)
 // this routine not only decides on recompiling SQL statement (if out of transaction)
 // but also reset parameter index (pi_) when needed while in transaction:
 // - reset of pi_ normally is done in compile() call, but in transaction it's
 //   never called,
 // - thus this routine will keep track of pi_ and once it's above pc_, it'll get
 //   reset
 if(pi_ <= pc_) return;                                         // in the mids of binding
 if(pc_ == 0) return;                                           // no parameters in the last sql
 if(lsql_.empty()) return;                                      // there's no cached sql yet
 if(ts_ == in_transaction_compiled)                             // don't recompile while in t-action
  { if(pi_ > pc_) pi_ = 1; return; }                            // thou reset pi_ when needed
 DBG(3) DOUT() << "auto-recompiling" << std::endl;
 compile(lsql_);
}












