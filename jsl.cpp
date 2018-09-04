#include <iostream>
#include <fstream>
#include <sstream>
#include <set>
#include <map>
#include "lib/getoptions.hpp"
#include "lib/Outable.hpp"
#include "lib/Json.hpp"
#include "lib/Sqlite.hpp"
#include "lib/dbg.hpp"

using namespace std;



#define VERSION "1.00"


#define OPT_RDT -
#define OPT_GEN a
#define OPT_DBG d
#define OPT_EXP e
#define OPT_IGN i
#define OPT_IGS I
#define OPT_MAP m
#define OPT_MPS M
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
    Json                json;                                   // source JSON
    string              tbl_name;                               // table to update in usere's db
    string              schema;                                 // table's schema
    vector<TableInfo>   table_info;                             // table's table_info pragma
    size_t              autokeys{0};                            // num. of AUTOINCREMENTs in schema
    size_t              updates{0};                             // number of updates made into db
    set<string>         ignored;                                // ignored columns (-i, -I)

    DEBUGGABLE()
};

#define __REFX__(A) auto & A = __common_resource__.A;
#define REVEAL(X, ARGS...) \
        auto & __common_resource__ = X; \
        MACRO_TO_ARGS(__REFX__, ARGS)
// usage: REVEAL(cr, opt, DBG())



// forward declarations
typedef map<string, vector<string>> vstr_map;
void post_parse(SharedResource &r);
void parse_db(SharedResource &r);
void read_json(SharedResource &r);
void update_table(SharedResource &r);
string columns(SharedResource &r);
string value_placeholders(SharedResource &r);
void json_callback(SharedResource &r, Sqlite &db, vstr_map &row, const Jnode & node);
bool schema_generated(SharedResource &r, Sqlite &db, vstr_map &row, const Jnode & node);
string stringify(const Jnode &node);
string & trim_spaces(std::string &&str);
string maybe_quote(string str);





int main(int argc, char *argv[]) {

 SharedResource r;
 REVEAL(r, opt, json, tbl_name, updates, DBG())

 opt.prolog("\nJSON to Sqlite db dumper.\nVersion " VERSION \
            ", developed by Dmitry Lyssenko (ldn.softdev@gmail.com)\n");
 opt[CHR(OPT_GEN)].desc("auto-generate table schema from JSON values (if it's not in db)");
 opt[CHR(OPT_DBG)].desc("turn on debugs (multiple calls increase verbosity)");
 opt[CHR(OPT_EXP)].desc("expand followed mapping if it's a JSON array or object");
 opt[CHR(OPT_IGN)].desc("ignore a specified column") .name("tbl_column");
 opt[CHR(OPT_IGS)].desc("ignore all listed columns (comma separated list)") .name("c-list");
 opt[CHR(OPT_MAP)].desc("map a single JSON label onto a respective table column")
                  .name("json_label");
 opt[CHR(OPT_MPS)].desc("map JSON labels (comma separated) to respective columns")
                  .name("label-list");
 opt[CHR(OPT_CLS)].desc("sql update clause").bind("INSERT OR REPLACE").name("clause");
 opt[ARG_DBF].desc("sqlite db file").name("db_file");
 opt[ARG_TBL].desc("sqlite db table to update").name("table").bind("auto-selected first in db");
 opt.epilog("\nNote on -m and -M usage:\n\
 - option -m lets mapping a single label, while -M specifies a list of labels\n\
   over comma; though -M is processed always after -m, i.e.: `-M \"b, c\" -m a'\n\
   is the same as `-m a -m b -m c'; options -i and -I share the same relation;\n\
   option -e instruct to expand given label (if it's expandable): i.e. a label\n\
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
   { cerr << "error: no table found in db" << endl; return RC_NO_TBL; }

  read_json(r);
  update_table(r);
  cout << "updated " << updates << " records into " << opt[ARG_DBF].str()
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
 // parse -M and -I here
 REVEAL(r, opt, ignored, DBG())

 if(opt[CHR(OPT_MPS)].hits() > 0)                               // break up & transfer -M to -m
  for(size_t last{0}, found{0}; found != string::npos; last=found+1) {
   found = opt[CHR(OPT_MPS)].str().find(",", last);
   opt[CHR(OPT_MAP)] = trim_spaces(opt[CHR(OPT_MPS)].str().substr(last, found-last));
   opt[CHR(OPT_MAP)].push_prior(opt[CHR(OPT_MPS)].prior());     // pass notion of a prior option
  }

 if(opt[CHR(OPT_IGS)].hits() > 0)                               // break up & transfer -I to -i
  for(size_t last{0}, found{0}; found != string::npos; last=found+1) {
   found = opt[CHR(OPT_IGS)].str().find(",", last);
   opt[CHR(OPT_IGN)] = trim_spaces(opt[CHR(OPT_IGS)].str().substr(last, found-last));
   opt[CHR(OPT_IGN)].push_prior(opt[CHR(OPT_IGS)].prior());     // pass notion of a prior option
  }

 if(opt[CHR(OPT_IGN)].hits() > 0)                               // build a set of ignored columns
  for(const auto &ign: opt[CHR(OPT_IGN)]) {
   ignored.insert(ign);
   DBG(0) DOUT() << "in updates will ignore column: " << ign << endl;
  }
}



void parse_db(SharedResource &r) {
 // read tables pragma and populate schema, ibl_name, table_info for selected table
 REVEAL(r, opt, schema, tbl_name, table_info, DBG())

 Sqlite db;
 DBG().severity(db);
 vector<MasterRecord> master_tbl;                               // read here sqlite_master table

 db.open(opt[ARG_DBF].str(), (opt[CHR(OPT_GEN)].hits() >=0 and table_info.empty()?
                              SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE:
                              SQLITE_OPEN_READONLY))
   .compile("SELECT * FROM sqlite_master WHERE type='table';")
   .read(master_tbl);

 bool maps_given{ opt[CHR(OPT_MAP)].hits() != 0 };              // is -m given?
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
  if(not maps_given)                                            // w/o '-m' - print table_info
   { for(auto &row: table_info) cout << row << endl; cout << endl; }
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
 REVEAL(r, opt, json, table_info, tbl_name, DBG())

 Sqlite db;
 DBG().severity(db);

 vstr_map row;
 auto cb = [&r, &row, &db](const Jnode & node){ json_callback(r, db, row, node); };

 for(const auto &mapped_lbl: opt[CHR(OPT_MAP)]) {
  json.callbacks().emplace(mapped_lbl, cb);                     // put callback on each mapped label
  row[mapped_lbl];                                              // create a holder for each label
  DBG(0) DOUT() << "placing callback for label: '" << mapped_lbl << "'" << endl;
 }

 db.open(opt[ARG_DBF].str());
 if(not table_info.empty())                                     // update tbl only if -a not given
  db.begin_transaction()
    .compile(opt[CHR(OPT_CLS)].str() + " INTO " +
             tbl_name + columns(r) + " VALUES (" + value_placeholders(r) + ");");

 json.walk("<.^>R");                                            // let all callbacks engage
}



string columns(SharedResource &r) {
 // generate columns string, exclude those with AUTOINCREMENT and in ignored set
 REVEAL(r, schema, table_info, autokeys, ignored, DBG())

 string str{" ("};
 string values{ schema, schema.find("(")+1 };                   // VALUE(... from schema
 DBG(1) DOUT() << "extracted values description: (" << values << endl;

 size_t value_pos{0};
 for(auto &column: table_info) {
  size_t next_separator = values.find_first_of(",)", value_pos);
  string value{ values, value_pos, next_separator-value_pos };  // extract: <C-Name definition>,  
  value_pos = next_separator + 1;

  if(ignored.count(column.name) == 1) continue;
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


size_t row_size(const vstr_map &row) {
 // return total sum of sizes of all vectors in row
 size_t size{0};
 for(auto &lbl_values: row)
  size += lbl_values.second.size();
 return size;
}



void update_row(SharedResource &r, vstr_map &row, const Jnode & node, vstr_map *cschema=nullptr) {
 // update row with given Json node (mind -e option), additionally build column definitions
 REVEAL(r, opt, DBG())

 bool extend{false};                                            // current node has to be extended?
 for(size_t i=1; i<opt[CHR(OPT_MAP)].size(); ++i)               // go thru all -m options
  if(opt[CHR(OPT_MAP)].str(i) == node.label()) {                // find index of -m with this label
   if(opt[CHR(OPT_MAP)].prior(i) == CHR(OPT_EXP)) extend = true;// if prior option was -e?
   break;
  }
 DBG(2) DOUT() << "extend json value: " << (extend? "yes": "no") << endl;

 if(node.is_atomic() or not extend) {                           // put a single value into a row
  row[node.label()].push_back( stringify(node) );               // json iterables saved in row
  if(cschema == nullptr) return;                                // non-null cschema facilitates '-a'
  (*cschema)[node.label()].push_back( maybe_quote(node.label()) + " " +
                                      (node.is_number() or node.is_bool()? "NUMERIC": "TEXT") );
 }
 else                                                           // it's iterable requiring expansion
  for(auto &rec: node) {
   row[node.label()].push_back( stringify(rec) );
   if(cschema == nullptr) continue;
   string cname = node.label() + "_" + (node.is_array()?
                                        to_string((*cschema)[node.label()].size()):
                                        rec.label());
   cname = maybe_quote(cname);
   cname += rec.is_number() or rec.is_bool()? " NUMERIC": " TEXT";
   (*cschema)[node.label()].push_back( move(cname) );
  }
}



void clear_row(vstr_map & row) {
 // clear all vectors in map;
 for(auto &vs: row) vs.second.clear();
}



void dump_row(SharedResource &r, Sqlite &db, vstr_map &row) {
 // dump row's data into the db
 REVEAL(r, opt, updates, DBG())

 vector<string> rout;                                           // prepare row for dumping to db
 for(const auto &lbl: opt[CHR(OPT_MAP)])                        // go by -m option order
  for(auto &str: row[lbl])                                      // linearize row into an array
   rout.push_back( move(str) );

 db << rout;
 clear_row(row);
 ++updates;
 DBG(2) DOUT() << "-- flushed to db (" << updates << ")" << endl;
}



void json_callback(SharedResource &r, Sqlite &db, vstr_map &row, const Jnode & node) {
 // build a row and dump it into database (also, facilitate -a option)
 REVEAL(r, opt, table_info, autokeys, ignored, DBG())

 DBG(2) DOUT() << node.label() << ": " << node << endl;
 if(table_info.empty())                                         // need to generate schema (-a case)
  if(not schema_generated(r, db, row, node)) return;            // schema is't yet ready

 size_t full_size = table_info.size() - autokeys - ignored.size();
 if(row_size(row) > full_size) {                                // if failed previously
  if(node.label() != opt[CHR(OPT_MAP)].str(1))                  // wait until first label come thru
   { DBG(1) DOUT() << "waiting for the first mapped label to come" << endl; return; }
  clear_row(row);                                               // clean up the slate and start over
 }

 update_row(r, row, node);                                      // update row with given JSON node

 if(row_size(row) < full_size) return;                          // row is incomplete yet
 if(row_size(row) > full_size or                                // row is bigger than required
    any_of(row.cbegin(), row.cend(), [](auto &rm){ return rm.second.empty(); }) ) {                                   // row is bigger that table width
  DBG(1) DOUT() << "inconsistent mappings occurred, skip dumping to DB" << endl;
  return;
 }

 dump_row(r, db, row);
}



bool schema_generated(SharedResource &r, Sqlite &db, vstr_map &row, const Jnode & node) {
 // return true if schema was generated, table_info read, etc
 REVEAL(r, opt, DBG());
 static vstr_map cschema;                                       // static ok, given build only once

 if(row[node.label()].empty()) {                                // otherwise it's a next row
  update_row(r, row, node, &cschema);                           // update and build column's schema
  if(DBG()(1))
   for(const auto &type: cschema[node.label()])
    DOUT() << DBG_PROMPT(1) << "auto-define column: " << type << endl;
  return false;
 }

 string schema = "CREATE TABLE " + opt[ARG_TBL].str() + " (";   // ok, ready to build schema
 bool primary{true};
 for(const auto &lbl: opt[CHR(OPT_MAP)])                        // go by option -m order
  for(auto &cd: cschema[lbl]) {
   schema += move(cd);
   if(primary)                                                  // first is always a primary key
    { schema += " PRIMARY KEY"; primary = false; }
   schema += ",";
  }
 schema.pop_back();                                             // pop trailing ','
 schema += ");";
 DBG(1) DOUT() << "schema: " << schema << endl;

 db.compile(schema);
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












