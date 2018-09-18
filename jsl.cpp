#include <iostream>
#include <fstream>
#include <sstream>
#include <set>
#include <map>
#include <fstream>
#include "lib/getoptions.hpp"
#include "lib/Outable.hpp"
#include "lib/Json.hpp"
#include "lib/Sqlite.hpp"
#include "lib/dbg.hpp"

using namespace std;


#define VERSION "1.02"

#define ROW_LMT 2                                               // 2 bytes per autogen. column index
#define CLM_PFX Auto                                            // name prefix for auto-gen. columns
#define OPT_RDT -
#define OPT_GEN a
#define OPT_DBG d
#define OPT_EXP e
#define OPT_IGN i
#define OPT_IGS I
#define OPT_MAP m
#define OPT_MPS M
#define OPT_QET s
#define OPT_CLS u
#define ARG_DBF 0
#define ARG_TBL 1

// facilitate option materialization
#define STR(X) XSTR(X)
#define XSTR(X) #X
#define CHR(X) XCHR(X)
#define XCHR(X) *#X


#define RETURN_CODES \
        RC_OK, \
        RC_NO_TBL, \
        RC_ILL_QUOTING, \
        RC_END
ENUM(ReturnCodes, RETURN_CODES)

#define OFF_GETOPT RC_END                                       // offset for Getopt exceptions
#define OFF_JSL (OFF_GETOPT + Getopt::end_of_throw)             // offset for jsl exceptions



// sqlite_master table's record
struct MasterRecord {
    string              type;
    string              name;
    string              tbl_name;
    int                 rootpage;
    string              sql;

    SQLIO(type, name, tbl_name, rootpage, sql)
    OUTABLE(MasterRecord, type, name, tbl_name, rootpage, sql)
};



// PRAGMA table-info record
struct TableInfo {
    int                 cid;
    string              name;
    string              type;
    int                 not_null;
    string              default_value;
    int                 primary_key;

    SQLIO(cid, name, type, not_null, default_value, primary_key)
    COUTABLE(TableInfo, cid, name, type, not_null, primary_key)
};



struct SharedResource {
    Getopt              opt;
    Getopt              opr;                                    // option for remapped -m/-e values
    Json                json;                                   // source JSON
    string              tbl_name;                               // table to update in usere's db
    string              schema;                                 // table's schema
    vector<TableInfo>   table_info;                             // table's table_info pragma
    size_t              autokeys{0};                            // # of AUTOINCREMENTs in schema
    size_t              updates{0};                             // # of updates made into db
    set<string>         ignored;                                // ignored columns (-i, -I)

    ostream &           out(unsigned quiet)                     // demux /dev/null & std::cout
                         { return opt[CHR(OPT_QET)].hits()>=quiet? null_: std::cout; }
    DEBUGGABLE()

 private:
    ofstream            null_{"/dev/null"};
};


#define __REFX__(A) auto & A = __common_resource__.A;
#define REVEAL(X, ARGS...) \
        auto & __common_resource__ = X; \
        MACRO_TO_ARGS(__REFX__, ARGS)
// usage: REVEAL(cr, opt, DBG())


// forward declarations
class Vstr_maps;
void post_parse(SharedResource &r);
void parse_db(SharedResource &r);
void read_json(SharedResource &r);
void update_table(SharedResource &r);

string columns(SharedResource &r);
string value_placeholders(SharedResource &r);
void json_callback(SharedResource &r, Sqlite &db, Vstr_maps &row, const Jnode & node);
bool schema_generated(SharedResource &r, Sqlite &db, Vstr_maps &row, const Jnode & node);
string stringify(const Jnode &node);
string & trim_spaces(std::string &&str);
string generate_column_name(const Jnode &jn);
string maybe_quote(string str);


// row class declaration
typedef map<string, vector<string>> lbl_vstr_map;
typedef map<size_t, vector<string>> itn_vstr_map;
typedef map<string, size_t> lbl_opt;
typedef map<size_t, size_t> itn_opt;
class Vstr_maps {
 // the class facilitates a container for JSON values which to be dumped into sqlite
 // db (a full row). At the same time it's re-used to build columns definitions
 // when table needs to be auto-generated.
 // JSON values could be pointed either by JSON labels, or by walk-path (iterator)
 // thus, the container (de)multiplexes both container types
 // it also provides back-tracing of ordinal encounter of option -m to the mapped
 // container (vector)
 public:

                        Vstr_maps(void) = delete;
                        Vstr_maps(SharedResource &r): r_(r) {}

    void                book(const string &key, function<void(const Jnode &)> &&cb, size_t opt_cnt);
    size_t              size(void) const;
    void                clear(void);
    bool                complete(void) const;
    void                push(const Jnode &jn, const string &&value);
    size_t              backtrace_opt(const Jnode &jn) const;
 const vector<string> * value_by_position(size_t opt_cnt) const;
 const vector<string> & value_by_node(const Jnode &jn) const;

 protected:
    lbl_vstr_map        lbl_;                                   // mappings for lables
    itn_vstr_map        itr_;                                   // mapping for iterators
    lbl_opt             lon_;                                   // label to option counter map
    itn_opt             ion_;                                   // iterator to option counter map

    SharedResource &    r_;
};


void Vstr_maps::book(const string &key, function<void(const Jnode &)> &&cb, size_t on) {
 // book place either in itr or lbl: defined by key being walk-path or label
 try {
  Json::iterator it = r_.json.walk(key, Json::keep_cache);      // first try parse as a walk
  if(it == r_.json.end()) return;                               // walk failed - don't register
  r_.json.callback(move(it), move(cb));                         // plug itr callback here
  itr_[r_.json.itr_callbacks().size()-1];
  ion_[r_.json.itr_callbacks().size()-1] = on;                  // also record -m ordinal num
  DBG(r_, 0) DOUT(r_) << "booked iterator based callback: " << key << endl;
 }
 catch(stdException & e) {
  if(e.code() < Jnode::walk_offset_missing_closure) throw e;    // if failed with walk exception
  r_.json.callback(key, move(cb));
  lbl_[key];                                                    // then it's a label
  lon_[key] = on;                                               // it's required to trace prev. opt.
  DBG(r_, 0) DOUT(r_) << "booked label based holder: " << key << endl;
 }
}


size_t Vstr_maps::size(void) const {
 // calculate current size of all vectors in all maps
 size_t size = 0;
 for(auto &lbl_vec: lbl_) size += lbl_vec.second.size();
 for(auto &itn_vec: itr_) size += itn_vec.second.size();
 return size;
}


void Vstr_maps::clear(void) {
 // empty all map containers
 for(auto &lbl_vec: lbl_) lbl_vec.second.clear();
 for(auto &itn_vec: itr_) itn_vec.second.clear();
}


bool Vstr_maps::complete(void) const {
 for(auto &lbl_vec: lbl_)
  if(lbl_vec.second.empty()) return false;
 for(auto &itn_vec: itr_)
  if(itn_vec.second.empty()) return false;
 return true;
}


void Vstr_maps::push(const Jnode &jn, const string &&json_value) {
 // update string value into lbl or itr map
 if(jn.has_label())                                             // first check lbl mapping
  if(lbl_.count(jn.label()) == 1)
   { lbl_[jn.label()].push_back(move(json_value)); return; }
 for(auto &itn_vec: itr_) {                                     // then check itr mapping
  auto & iter = r_.json.itr_callbacks()[itn_vec.first].iter;
  if(&jn.value() == &iter->value())                             // &jn matches to pointer of itr
   { itn_vec.second.push_back(move(json_value)); return; }
 }

 cerr << "push() fail: json node is not back traceable, must be a bug" << endl;
 exit(RC_END);                                                  // should never reach here
}


size_t Vstr_maps::backtrace_opt(const Jnode &jn) const {
 // given Jnode (jn) back trace the option count that called it
 if(jn.has_label())                                             // first check lbl mapping
  if(lon_.count(jn.label()) == 1)
   return lon_.at(jn.label());

 for(auto &itn_vec: itr_) {                                     // then check itr vector
  auto & iter = r_.json.itr_callbacks()[itn_vec.first].iter;
  if(&jn.value() == &iter->value())                             // &jn matches to pointer of iter
   return ion_.at(itn_vec.first);
 }
 cerr << "backtrace_opt() fail: json node is not back traceable, must be a bug" << endl;
 exit(RC_END);                                                  // should never reach here
}


const vector<string> * Vstr_maps::value_by_position(size_t opt_cnt) const {
 // return recorded values for given option position (1st, 2nd etc)
 for(auto & lbl_cnt: lon_)
  if(lbl_cnt.second == opt_cnt)
   return &lbl_.at(lbl_cnt.first);

 for(auto & itr_num: ion_)
  if(itr_num.second == opt_cnt)
   return &itr_.at(itr_num.first);

 return nullptr;                                                // indicate 'not found' conditions
}


const vector<string> & Vstr_maps::value_by_node(const Jnode &jn) const {
 // return array of mapped values for given jnode
 if(jn.has_label())                                             // first check lbl mapping
  if(lbl_.count(jn.label()) == 1)
   return lbl_.at(jn.label());
 for(auto &itn_vec: itr_) {                                     // then check itr mapping
  auto & iter = r_.json.itr_callbacks()[itn_vec.first].iter;
  if(&jn.value() == &iter->value())                             // &jn matches to pointer of iter
   return itr_.at(itn_vec.first);
 }
 cerr << "value_by_node() fail: json node is not back traceable, must be a bug" << endl;
 exit(RC_END);                                                  // should never reach here
}







int main(int argc, char *argv[]) {

 SharedResource r;
 REVEAL(r, opt, json, tbl_name, updates, DBG())

 opt.prolog("\nJSON to Sqlite db dumper.\nVersion " VERSION \
            ", developed by Dmitry Lyssenko (ldn.softdev@gmail.com)\n");
 opt[CHR(OPT_GEN)].desc("auto-generate table schema from JSON values (if not in db yet)");
 opt[CHR(OPT_DBG)].desc("turn on debugs (multiple calls increase verbosity)");
 opt[CHR(OPT_EXP)].desc("expand followed mapping if it's a JSON array or object");
 opt[CHR(OPT_IGN)].desc("ignore a specified column").name("tbl_column");
 opt[CHR(OPT_IGS)].desc("ignore all listed columns (comma separated list)").name("header-list");
 opt[CHR(OPT_MAP)].desc("map a single label or walk-path onto a respective table column")
                  .name("label_walk");
 opt[CHR(OPT_MPS)].desc("map JSON labels (comma separated) to respective columns")
                  .name("label-list");
 opt[CHR(OPT_QET)].desc("run quietly (multiple calls reduce verbocity)");
 opt[CHR(OPT_CLS)].desc("sql update clause").bind("INSERT OR REPLACE").name("clause");
 opt[ARG_DBF].desc("sqlite db file").name("db_file");
 opt[ARG_TBL].desc("sqlite db table to update").name("table").bind("auto-selected first in db");
 opt.epilog("\nNote on -m and -M usage:\n\
 - option -m lets mapping a single label, while -M specifies a list of labels\n\
   over comma; option -M is expanded into respective number of -m options, the\n\
   order and relevance with other options is preserved\n\
 - option -e lets expanding a given label (if it's expandable): i.e. a label\n\
   may map onto a JSON array or object, if -e is not preceding -m, then mapped\n\
   entry will be stored away in db as a raw JSON string. if -e precedes the\n\
   mapping, then the mapped container will be expanded into respective db's\n\
   records; specifying -e in front of -M extends that behavior onto all listed\n\
   labels\n");

 // parse options
 try { opt.parse(argc,argv); }
 catch(stdException & e)
  { opt.usage(); return e.code() + OFF_GETOPT; }

 DBG().level(opt[CHR(OPT_DBG)])
      .use_ostream(cerr)
      .severity(json);
 post_parse(r);

 try {
  parse_db(r);
  if(tbl_name.empty() and opt[CHR(OPT_GEN)].hits() == 0)        // some wrong table name given
   { cerr << "error: no table " << opt[ARG_TBL] << " found in db" << endl; return RC_NO_TBL; }

  read_json(r);
  update_table(r);
  r.out(3) << "updated " << updates << " records into " << opt[ARG_DBF].str()
           << ", table: " << tbl_name << endl;
 }
 catch(stdException &e) {
  DBG(0) DOUT() << "exception raised by: " << e.where() << endl;
  cerr << opt.prog_name() << " exception: " << e.what() << endl;
  return e.code() + OFF_JSL;
 }

 return RC_OK;
}





void post_parse(SharedResource &r) {
 // deparse -m, -M, extend -I here
 REVEAL(r, opt, opr, ignored, DBG())

 opr[CHR(OPT_MAP)].bind();                                      // prepare option for remapping
 opr[CHR(OPT_EXP)];

 for(size_t i = 0, e = 0; i < opt.order().size(); ++i)          // move each -m and expand -M
  switch(opt.order(i).id()) {
   case CHR(OPT_EXP): e = 1; continue;
   case CHR(OPT_MAP):
    if(e-- > 0) opr[CHR(OPT_EXP)].hit();                        // insert -e if flag is raised
    opr[CHR(OPT_MAP)] = opt.order(i).str();                     // insert -m value
    continue;
   case CHR(OPT_MPS):
    for(size_t last{0}, found{0}; found != string::npos; last=found+1) {
     if(e > 0) opr[CHR(OPT_EXP)].hit();                         // insert -e if flag is raised
     found = opt.order(i).str().find(",", last);
     opr[CHR(OPT_MAP)] = trim_spaces(opt.order(i).str().substr(last, found-last));
    }
    --e;
  }

 for(size_t i = 0; i < opt.order().size(); ++i)                 // move each -i and expand -I
  switch(opt.order(i).id()) {
   case CHR(OPT_IGN): opr[CHR(OPT_IGN)] = opt.order(i).str(); continue;
   case CHR(OPT_IGS):
    for(size_t last{0}, found{0}; found != string::npos; last=found+1) {
     found = opt.order(i).str().find(",", last);
     opr[CHR(OPT_IGN)] = trim_spaces(opt.order(i).str().substr(last, found-last));
    }
  }

 if(opr[CHR(OPT_IGN)].hits() > 0)                               // build a set of ignored columns
  for(const auto &ign: opr[CHR(OPT_IGN)]) {
   ignored.insert(ign);
   DBG(0) DOUT() << "in updates will ignore column: " << ign << endl;
  }
}



void parse_db(SharedResource &r) {
 // read tables pragma and populate schema, ibl_name, table_info for selected table
 REVEAL(r, opt, opr, schema, tbl_name, table_info, DBG())

 Sqlite db;
 DBG().severity(db);
 vector<MasterRecord> master_tbl;                               // read here sqlite_master table

 db.open(opt[ARG_DBF].str(), (opt[CHR(OPT_GEN)].hits() > 0 and table_info.empty()?
                              SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE:
                              SQLITE_OPEN_READONLY))
   .compile("SELECT * FROM sqlite_master WHERE type='table';")
   .read(master_tbl);

 bool maps_given{ opr[CHR(OPT_MAP)].hits() != 0 };              // is -m given?
 for(auto &rec: master_tbl) {                                   // process all master's records
  DBG(3) DOUT() << rec << endl;
  if(tbl_name.empty()) {                                        // tbl_name not populated yet
   if(opt[ARG_TBL].hits() == 0)                                 // table name not passed in argument
    { tbl_name = rec.tbl_name; schema = rec.sql; }              // select then 1st table name
   else                                                         // table name was provided
    if(opt[ARG_TBL].str() == rec.tbl_name)                      // trying matching it then
     { tbl_name = rec.tbl_name; schema = rec.sql; }
  }

  if(tbl_name != rec.tbl_name)
   if(maps_given or opt[ARG_TBL].hits() > 0) continue;
  if(not maps_given)                                            // w/o '-m' - print table schema
   cout << "table [" << rec.tbl_name << "]:\nschema.. " << rec.sql << endl;
  table_info.clear();                                           // now read table_info PRAGMA
  db.compile("PRAGMA table_info(" + rec.tbl_name + ");").read(table_info);
  if(not maps_given) {                                          // w/o '-m' - print table_info
   for(auto &info_row: table_info) cout << info_row << endl;
   cout << (opt[ARG_TBL].hits() > 0? "": "\n");
  }
 }

 if(not maps_given) exit(RC_OK);                                // no further processing w/o -m
}



void read_json(SharedResource &r) {
 // read and parse json from stdin
 REVEAL(r, json, DBG())

 DBG(0) DOUT() << "reading json from <stdin>" << endl;
 json.raw().parse( string{istream_iterator<char>(cin>>noskipws),
                    istream_iterator<char>{}} );
}



void update_table(SharedResource &r) {
 // put callback on each mapped label and let callbacks do the job
 REVEAL(r, opt, opr, json, table_info, tbl_name, DBG())

 Sqlite db;
 DBG().severity(db);

 Vstr_maps row(r);
 auto cb = [&r, &row, &db](const Jnode & node){ json_callback(r, db, row, node); };

 size_t opt_cnt = 0;
 for(const auto &mapped_lbl: opr[CHR(OPT_MAP)])
  row.book(mapped_lbl, cb, ++opt_cnt);                          // create a holder for each label

 db.open(opt[ARG_DBF].str());
 r.out(2) << "table [" << opt[ARG_TBL].str() << "]:" << endl;
 if(not table_info.empty()) {                                   // update tbl only if -a not given
  db.begin_transaction()
    .compile(opt[CHR(OPT_CLS)].str() + " INTO " +
             tbl_name + columns(r) + " VALUES (" + value_placeholders(r) + ");");
  r.out(2) << "headers.. |";
  for(auto &info_row: table_info) r.out(2) << info_row.name << "|";
  r.out(2) << endl;
 }

 json.engage_callbacks().walk("<.^>R", Json::keep_cache);
}



string columns(SharedResource &r) {
 // generate columns string, exclude those with AUTOINCREMENT and in ignored set
 REVEAL(r, schema, table_info, autokeys, ignored, DBG())

 string str{" ("};
 string values{ schema, schema.find("(")+1 };                   // VALUE(... from schema
 DBG(1) DOUT() << "extracted values description: (" << values << endl;

 set<string> ignoring = move(ignored);
 size_t value_pos{0};
 for(auto &column: table_info) {
  size_t next_separator = values.find_first_of(",)", value_pos);
  string value{ values, value_pos, next_separator-value_pos };  // extract: <C-Name definition>,
  value_pos = next_separator + 1;

  if(ignoring.count(column.name) == 1)
   { ignored.insert(column.name); continue; }
  if(value.find("AUTOINCREMENT") != string::npos)               // if column is autoinc'ed
   { ++autokeys; continue; }                                    // don't include it into updates

  DBG(2) DOUT() << "compiling: " << trim_spaces(move(value)) << endl;
  str += maybe_quote(column.name) + ',';
 }

 str.pop_back();                                                // pop trailing comma
 str += ")";

 DBG(0) DOUT() << "compiled value string: " << str << endl;
 return str;
}



string value_placeholders(SharedResource &r) {
 // generate values placeholders (as many '?' as there updated parameters)
 REVEAL(r, table_info, autokeys, ignored, DBG())

 string str;
 for(size_t i=0; i<table_info.size() - autokeys - ignored.size(); ++i)
  str += "?,";

 str.pop_back();
 DBG(0) DOUT() << "placeholders: " << str << endl;
 return str;
}


void update_row(SharedResource &r, Vstr_maps &row,
                const Jnode & node, Vstr_maps *cschema=nullptr) {
 // this call is invoked from json_callback():
 // update row with given Json node (mind -e option), additionally build column definitions (-a)
 REVEAL(r, opr, DBG())

 auto m_order = opr[CHR(OPT_MAP)].order(row.backtrace_opt(node));
 bool expand = m_order == 0 or opr.order(m_order-1).id() != CHR(OPT_EXP)?
               false: true;                                     // current node has to be expanded?
 DBG(2) DOUT() << "expand json value? " << (expand? "yes": "no") << endl;

 if(node.is_atomic() or not expand) {                           // put a single value into a row
  row.push(node, stringify(node));                              // json iterables saved in row
  if(cschema == nullptr) return;                                // otherwise facilitate '-a' option
  cschema->push(node, maybe_quote(generate_column_name(node)) + " " +
                      (node.is_number() or node.is_bool()? "NUMERIC": "TEXT") );
 }
 else {                                                         // it's iterable requiring expansion
  string agg_column =  generate_column_name(node);
  for(auto &rec: node) {
   row.push(node, stringify(rec));
   if(cschema == nullptr) continue;
   string cname = agg_column + "_" + (node.is_array()? to_string(rec.index()): rec.label());
   cname = maybe_quote(cname);
   cname += rec.is_number() or rec.is_bool()? " NUMERIC": " TEXT";
   cschema->push(node, move(cname) );
  }
 }
}



void dump_row(SharedResource &r, Sqlite &db, Vstr_maps &row) {
 // dump row's data into the db
 REVEAL(r, opr, table_info, updates, DBG())

 vector<string> rout;                                           // prepare row for dumping to db
 for(size_t i=1; i<opr[CHR(OPT_MAP)].size(); ++i)
  if(row.value_by_position(i) != nullptr) {
   r.out(1) << " " << table_info[i-1].name;

   if(row.value_by_position(i)->size() > 1)
    r.out(1) << " .. " << table_info[i + row.value_by_position(i)->size()-2].name;

   for(auto &str: *row.value_by_position(i)) {                  // linearize row into an array
    rout.push_back( move(str) );
    r.out(1) << (&str == &row.value_by_position(i)->front() ? ": ":"|") << str;
   }
   r.out(1) << endl;
  }
 db << rout;
 r.out(1) << "-- flushed to db (row " << ++updates << ": " << row.size() << " values)" << endl;
 row.clear();
 DBG(2) DOUT() << "-- flushed to db (" << updates << ")" << endl;
}



void json_callback(SharedResource &r, Sqlite &db, Vstr_maps &row, const Jnode & node) {
 // build a row and dump it into database (also, facilitate -a option)
 REVEAL(r, opr, table_info, autokeys, ignored, DBG())

 DBG(2) DOUT() << (node.has_index()? "[" + to_string(node.index()) + "]":
                   (node.has_label()? node.label(): "root")) << ": " << node << endl;
 if(table_info.empty())                                         // need to generate schema (-a case)
  if(not schema_generated(r, db, row, node)) return;            // schema is't yet ready

 size_t full_size = table_info.size() - autokeys - ignored.size();
 if(row.size() > full_size) {                                   // if failed previously
  if(node.label() != opr[CHR(OPT_MAP)].str(1))                  // wait until first label come thru
   { DBG(1) DOUT() << "waiting for the first mapped label to come" << endl; return; }
  row.clear();                                                  // clean up the slate and start over
 }

 update_row(r, row, node);                                      // update row with given JSON node

 if(row.size() < full_size) return;                             // row is incomplete yet
 if(row.size() > full_size or not row.complete()) {             // row is bigger than required
  DBG(1) DOUT() << "inconsistent mappings occurred, skip dumping to DB" << endl;
  return;
 }

 dump_row(r, db, row);
}



bool schema_generated(SharedResource &r, Sqlite &db, Vstr_maps &row, const Jnode & node) {
 // return true if schema was generated, table_info read, etc
 REVEAL(r, opt, opr, DBG());
 static Vstr_maps cschema{row};                                 // static ok, given build only once

 if(row.value_by_node(node).empty()) {                          // otherwise it's a next row
  update_row(r, row, node, &cschema);                           // update and build column's schema
  if(DBG()(1))
   for(const auto &type: cschema.value_by_node(node))
    DOUT() << DBG_PROMPT(1) << "auto-defined column: " << type << endl;
  return false;
 }

 string schema = "CREATE TABLE " + opt[ARG_TBL].str() + " (";   // ok, ready to build schema
 bool primary_key = true;
 for(size_t i = 1; i < opr[CHR(OPT_MAP)].size(); ++i)
  for(auto &column_def: *cschema.value_by_position(i))
   { schema += column_def + (primary_key? " PRIMARY KEY": "") + ","; primary_key = false; }
 schema.pop_back();                                             // pop trailing ','
 schema += ");";
 DBG(1) DOUT() << "schema: " << schema << endl;

 db.compile(schema);
 r.out(2) << "generated schema.. " << schema << endl;
 parse_db(r);                                                   // populate now table_info PRAGMA
 db.begin_transaction()
   .compile(opt[CHR(OPT_CLS)].str() + " INTO " +
             opt[ARG_TBL].str() + columns(r) + " VALUES (" + value_placeholders(r) + ");");
 dump_row(r, db, row);
 return true;
}



string stringify(const Jnode &node) {
 // stringify node value

 if(node.is_bool())                                             // convert bool to 1/0
  return node.bul() == true? "1": "0";
 if(node.is_null())
  return "null";
 if(node.is_iterable()) {
  stringstream ss;
  ss << node;
  return ss.str();
 }
 return node.val();                                             // it's either a sting or number
}



string & trim_trailing_spaces(std::string &str) {
 // trim all trailing spaces
 return str.erase(str.find_last_not_of(" \t")+1);
}



string & trim_heading_spaces(std::string &str) {
 // trim all heading spaces
 return str.erase(0, str.find_first_not_of(" \t"));
}



string & trim_spaces(std::string &&str) {
 // trim all surrounding spaces
 return trim_heading_spaces( trim_trailing_spaces(str) );
}


string generate_column_name(const Jnode &jn) {
 // autogenerate column name for array, for node with labels return labels
 if(jn.has_label())
  return jn.label();

 static size_t col_num;                                         // sequenced column number
 std::stringstream ss;
 ss << STR(CLM_PFX) << std::hex << std::setfill('0') << std::setw(ROW_LMT * 2) << col_num++;
 return ss.str();
}


string maybe_quote(string str) {
 // quote string if it contains ` ` or `"` or `'`
 // able to quote only spaces and/or `"`, or spaces and/or `'`. combination of `'` + `"` is illegal
 bool space{false}, single{false}, dual{false};                 // space/ single quote/double quote
 for(auto c: str)
  switch(c) {
   case ' ' : space = true; break;
   case '\'': single = true; break;
   case '"' : dual = true; break;
  }
 if(single and dual)
   { cerr << "error: unsupported quoting in keyword: " << str << endl; exit(RC_ILL_QUOTING); }

 if(dual)
  return "'" + str + "'";
 if(single or space)
  return "\"" + str + "\"";
 return str;
}












