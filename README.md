# jsl - JSON to Sqlite db dumper

offline tool to store regular JSON structures into Sqlite3 database

#### Linux and MacOS precompiled binaries are available for download:
- [macOS 64 bit](https://github.com/ldn-softdev/jsl/raw/master/jsl-macos-64.v1.03)
- [macOS 32 bit](https://github.com/ldn-softdev/jsl/raw/master/jsl-macos-32.v1.03)
- [linux 64 bit](https://github.com/ldn-softdev/jsl/raw/master/jsl-linux-64.v1.03)
- [linux 32 bit](https://github.com/ldn-softdev/jsl/raw/master/jsl-linux-32.v1.03)

#### Compile and install instructions:

download `jsl-master.zip`, unzip it, descend into unzipped folder, compile using
an appropriate command, move compiled file into an install location.

1. here're the steps for *MacOS*:


say, `jsl-master.zip` has been downloaded to a folder and the terminal app is open in that
folder:
  - `unzip jsl-master.zip`
  - `cd jsl-master`
  - `c++ -o jsl -Wall -std=c++14 -Ofast -lsqlite3 jsl.cpp`
  - `sudo mv ./jsl /usr/local/bin/`

2. the steps for *Linux*:
  - `unzip jsl-master.zip`
  - `cd jsl-master`
  - `wget https://sqlite.org/2018/sqlite-amalgamation-3240000.zip`
  - `unzip sqlite-amalgamation-3240000.zip`
  - `gcc -O3 -c sqlite-amalgamation-3240000/sqlite3.c -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -ldl -lpthread -static`
  - `c++ -o jsl -Wall -std=gnu++14 sqlite3.o -static -ldl jsl.cpp`
  - `sudo mv ./jsl /usr/local/bin/`

#### Jump start usage guide:

##### 1. map JSON labels to sqlite table columns (`-m` explained)

Regular JSON structures could be mapped to respective columns in Sqlite3 db, following example illustrates that:


Say, sql.db contains a table `ADDRESS_BOOK`, with following columns defined:
```
bash $ sqlite3 sql.db -header "PRAGMA table_info(ADDRESS_BOOK);"
cid|name|type|notnull|dflt_value|pk
0|Name|TEXT|0||1
1|City|TEXT|0||0
2|Street|TEXT|0||0
3|State|TEXT|0||0
4|Zip|INEGER|0||0
bash $ 
```
And we want to dump into that table respecitve fields from this JSON in the file `ab.json`:
```
{
   "AddressBook": [
      {
         "Name": "John",
         "address": {
            "city": "New York",
            "postal code": 10012,
            "state": "NY",
            "street address": "599 Lafayette St"
         },
         "age": 25,
         "phoneNumbers": [
            {
               "number": "212 555-1234",
               "type": "mobile"
            }
         ],
         "spouse": null
      },
      {
         "Name": "Ivan",
         "address": {
            "city": "Seattle",
            "postal code": 98104,
            "state": "WA",
            "street address": "5423 Madison St"
         },
         "age": 31,
         "phoneNumbers": [
            {
               "number": "3 23 12334",
               "type": "home"
            },
            {
               "number": "6 54 12345",
               "type": "mobile"
            }
         ],
         "spouse": null
      },
      {
         "Name": "Jane",
         "address": {
            "city": "Denver",
            "postal code": 80206,
            "state": "CO",
            "street address": "6213 E Colfax Ave"
         },
         "age": 25,
         "phoneNumbers": [
            {
               "number": "+1 543 422-1231",
               "type": "office"
            }
         ],
         "spouse": null
      }
   ]
}
```
the way to achieve this using `jsl` would be:
```
bash $ cat ab.json | jsl -m Name -m city -m "street address" -m state -m "postal code" sql.db ADDRESS_BOOK
table [ADDRESS_BOOK]:
headers.. |Name|City|Street|State|Zip|
 Name: John
 City: New York
 Street: 599 Lafayette St
 State: NY
 Zip: 10012
-- flushed to db (row 1: 5 values)
 Name: Ivan
 City: Seattle
 Street: 5423 Madison St
 State: WA
 Zip: 98104
-- flushed to db (row 2: 5 values)
 Name: Jane
 City: Denver
 Street: 6213 E Colfax Ave
 State: CO
 Zip: 80206
-- flushed to db (row 3: 5 values)
updated 3 records into sql.db, table: ADDRESS_BOOK
bash $ 
bash $ sqlite3 sql.db -header "SELECT * FROM ADDRESS_BOOK;"
Name|City|Street|State|Zip
John|New York|599 Lafayette St|NY|10012
Ivan|Seattle|5423 Madison St|WA|98104
Jane|Denver|6213 E Colfax Ave|CO|80206
bash $
```
As follows from the example, option `-m` provides 1:1 mapping from JSON labels onto respecitve columns in db table
(order of `-m` follows the order of columns in the db table). A number of mapped labels must correspond to the number
of columns in the db table.


Instead of listing each column individually, `-M` option lets listing all of the mapped labels together over comma:
```
bash $ cat ab.json | jsl -M "Name, city, street address, state, postal code" sql.db ADDRESS_BOOK
```
all heading and trailing spaces in `-M` parameter around JSON labels will be stripped (if JSON contains such spacing, 
then use `-m` instead). Option `-M` internally is expanded into respective number of `-m` options. Options `-m`, `-M`
could be specified multiple times, preserving the order of each other respectively. E.g., following two examples
are equal:
  - `jsl -M "b, c" -m a -M "d, e" file.db TABLE`
  - `jsl -m b -m c -m a -m d -m e file.db TABLE`



##### 2. exclude table columns from the update (`-i` explained)

Let's rollback to the empty ADDRESS_BOOK table,
```
bash $ sqlite3 sql.db "DELETE FROM ADDRESS_BOOK;"
```
and this time try dumping all values except `State` column. For that purpose option `-i` comes handy:`
```
bash $ cat ab.json | jsl -m Name -M "city, street address, postal code" -i State sql.db ADDRESS_BOOK
table [ADDRESS_BOOK]:
headers.. |Name|City|Street|State|Zip|
 Name: John
 City: New York
 Street: 599 Lafayette St
 State: 10012
-- flushed to db (row 1: 4 values)
 Name: Ivan
 City: Seattle
 Street: 5423 Madison St
 State: 98104
-- flushed to db (row 2: 4 values)
 Name: Jane
 City: Denver
 Street: 6213 E Colfax Ave
 State: 80206
-- flushed to db (row 3: 4 values)
updated 3 records into sql.db, table: ADDRESS_BOOK
bash $ 
bash $ sqlite3 sql.db -header "SELECT * FROM ADDRESS_BOOK;"
Name|City|Street|State|Zip
John|New York|599 Lafayette St||10012
Ivan|Seattle|5423 Madison St||98104
Jane|Denver|6213 E Colfax Ave||80206
bash $ 
```
**NOTE**: *while option `-m` (`-M`) lists JSON labels (to be mapped onto the respecitve columns), `-i` option list db table column names;
a resulting number of mapped values (plus ignored columns) still should match the number of columns in the updated table*


Option `-i` has a similar counterpart `-I` letting listing multiple db table columns. Also, no need specifying column which is
ROWID - such column will be added to te list of ignored automatically.


##### 3. table auto-generation (`-a` explained)
Ok, this time let's start afresh with a clean slate:
```
bash $ sqlite3 sql.db "DROP TABLE ADDRESS_BOOK;"
bash $ sqlite3 sql.db "PRAGMA table_info(ADDRESS_BOOK);"
bash $ 
```
`jsl` is capable of auto-generating table from mapped JSON lables:
  - name of the column will be taken from the JSON label
  - type of the column will correspond to the mapped JSON type:
    - for JSON number and boolean values it will be `NUMERIC`
    - for other JSON values it will be TEXT
  - the order of columns in auto-generated table will be the same as they come in JSON (first come, first served)
  - first auto-generated column will be also auto-assigned `PRIMARY KEY`
```
bash $ cat ab.json | jsl -a -M "Name, age, city, postal code, state, street address" sql.db ADDRESS_BOOK
table [ADDRESS_BOOK]:
generated schema.. CREATE TABLE ADDRESS_BOOK (Name TEXT PRIMARY KEY,age NUMERIC,city TEXT,"postal code" NUMERIC,state TEXT,"street address" TEXT);
 Name: John
 age: 25
 city: New York
 postal code: 10012
 state: NY
 street address: 599 Lafayette St
-- flushed to db (row 1: 6 values)
 Name: Ivan
 age: 31
 city: Seattle
 postal code: 98104
 state: WA
 street address: 5423 Madison St
-- flushed to db (row 2: 6 values)
 Name: Jane
 age: 25
 city: Denver
 postal code: 80206
 state: CO
 street address: 6213 E Colfax Ave
-- flushed to db (row 3: 6 values)
updated 3 records into sql.db, table: ADDRESS_BOOK
bash $ 
bash $ sqlite3 sql.db "PRAGMA table_info(ADDRESS_BOOK);"
0|Name|TEXT|0||1
1|age|NUMERIC|0||0
2|city|TEXT|0||0
3|postal code|NUMERIC|0||0
4|state|TEXT|0||0
5|street address|TEXT|0||0
bash $ sqlite3 sql.db -header "SELECT * FROM ADDRESS_BOOK;"
Name|age|city|postal code|state|street address
John|25|New York|10012|NY|599 Lafayette St
Ivan|31|Seattle|98104|WA|5423 Madison St
Jane|25|Denver|80206|CO|6213 E Colfax Ave
bash $ 
```
Also, seeing table definition (i.e table_info PRAGMA) is possible with `jsl` if neither of `-m` or `-M` is given:
```
bash $ jsl sql.db ADDRESS_BOOK
table [ADDRESS_BOOK]:
schema.. CREATE TABLE ADDRESS_BOOK (Name TEXT PRIMARY KEY,age NUMERIC,city TEXT,"postal code" NUMERIC,state TEXT,"street address" TEXT)
TableInfo.. cid:0, name:"Name", type:"TEXT", not_null:0, primary_key:1 
TableInfo.. cid:1, name:"age", type:"NUMERIC", not_null:0, primary_key:0 
TableInfo.. cid:2, name:"city", type:"TEXT", not_null:0, primary_key:0 
TableInfo.. cid:3, name:"postal code", type:"NUMERIC", not_null:0, primary_key:0 
TableInfo.. cid:4, name:"state", type:"TEXT", not_null:0, primary_key:0 
TableInfo.. cid:5, name:"street address", type:"TEXT", not_null:0, primary_key:0 
bash $ 
```

*a table definition will be auto-generated only if the tabe is not yet defined, otherwise existing table defintion will be used
(i.e. `-a` will be ignored)*


There's another option for table auto-generation: `-A`, which takes a parameter - column name. Difference from `-a` is that
latter defines a 1st mapped column to be the primary key (whether it's TEXT or NUMERIC), while former, defines colum as 
`INTEGER PRIMARY KEY`, which in Sqlite db defines the column as ROWID - which generates the index automatically.
The implication is obvious:
  - repetitive execution of auto-generated tables with `-a` with the same set of source data (source JSON) will result in
  table rows being overwritten
  - the same execution of auto-generated tables with `-A` results in extending tables with each new execution (using 
  auto-generated ROWID index)


##### 4. expand JSON containers (`-e` explained)
if mapped JSON value (through `-m` or `-M`) is pointing to JSON array or object (a.k.a. iterable), then it's possible to subject 
the mapped iterable to the column update. The option `-e` does the trick, but it has to precede every option `-m` where such 
subjection is required. if option `-e` precedes `-M`, then it's applied to all listed labels.

Let's work with previously auto-generated table, but first clean it up:
```
bash $ sqlite3 sql.db "DELETE FROM ADDRESS_BOOK;"
bash $ sqlite3 sql.db "PRAGMA table_info(ADDRESS_BOOK);"
0|Name|TEXT|0||1
1|age|NUMERIC|0||0
2|city|TEXT|0||0
3|postal code|NUMERIC|0||0
4|state|TEXT|0||0
5|street address|TEXT|0||0
bash $ 
```
The order of entries: `city`, `postal code`, `state`, `street address`, is the same as in JSON processing engine 
(which is, btw, alphabetical, could be verified with [jtc](https://github.com/ldn-softdev/jtc) tool).
Thus, instead of enumerating each label individually, it's possible to map the label of the iterable - `address` and expand it:
```
bash $ cat ab.json | jsl -eM "Name, age, address" sql.db ADDRESS_BOOK
table [ADDRESS_BOOK]:
headers.. |Name|age|city|postal code|state|street address|
 Name: John
 age: 25
 city .. street address: New York|10012|NY|599 Lafayette St
-- flushed to db (row 1: 6 values)
 Name: Ivan
 age: 31
 city .. street address: Seattle|98104|WA|5423 Madison St
-- flushed to db (row 2: 6 values)
 Name: Jane
 age: 25
 city .. street address: Denver|80206|CO|6213 E Colfax Ave
-- flushed to db (row 3: 6 values)
updated 3 records into sql.db, table: ADDRESS_BOOK
bash $
bash $ sqlite3 sql.db -header "SELECT * FROM ADDRESS_BOOK;"
Name|age|city|postal code|state|street address
John|25|New York|10012|NY|599 Lafayette St
Ivan|31|Seattle|98104|WA|5423 Madison St
Jane|25|Denver|80206|CO|6213 E Colfax Ave
bash $ 
```
Effectively the label address got expaned (other were attempted too, but thier respective JSON values
were not iterables) into following labels (and respective columns): `city`, `postal code`, `state`, `street address`

Option `-e` is compatible with option `-a`, labels could be expanded during auto-generation too.


##### 5. Mapping JSON values using walk-path
Options `-m`, `-M` also allow specifying walk-path (refer to [jtc](https://github.com/ldn-softdev/jtc) for walk path explanation).
Desired mapping might not always be in JSON object, it could resides in arrays, in such case walk-path could be used instead of a label.
Say, we first extract desired values from our json (just for the same of walk-path usage):
```
bash $ cat ab.json | jtc -x[0][+0] -y[Name] -y[age] -y[address][+0] -jll
[
   {
      "Name": "John",
      "age": 25,
      "city": "New York",
      "postal code": 10012,
      "state": "NY",
      "street address": "599 Lafayette St"
   },
   {
      "Name": "Ivan",
      "age": 31,
      "city": "Seattle",
      "postal code": 98104,
      "state": "WA",
      "street address": "5423 Madison St"
   },
   {
      "Name": "Jane",
      "age": 25,
      "city": "Denver",
      "postal code": 80206,
      "state": "CO",
      "street address": "6213 E Colfax Ave"
   }
]
bash $ 
```
The above JSON could have been mapped using each label, but there is much easier way - via specifying a walk-path:
```
bash $ cat ab.json | jtc -x[0][+0] -y[Name] -y[age] -y[address][+0] -jll | jsl -e -m [+0] sql.db ADDRESS_BOOK
table [ADDRESS_BOOK]:
headers.. |Name|age|city|postal code|state|street address|
 Name .. street address: John|25|New York|10012|NY|599 Lafayette St
-- flushed to db (row 1: 6 values)
 Name .. street address: Ivan|31|Seattle|98104|WA|5423 Madison St
-- flushed to db (row 2: 6 values)
 Name .. street address: Jane|25|Denver|80206|CO|6213 E Colfax Ave
-- flushed to db (row 3: 6 values)
updated 3 records into sql.db, table: ADDRESS_BOOK
bash $ 
```
Each iteration of such walk-path points to a JSON object whithin root's array (which does not have a label). By specifying
option `-e` we instruct to expand such JSON object into corresponding table columns


#### Planned enhancements:
1. add capability to specify in maping (`-m`, `-M`) json walk paths in addition to JSON labels 
(see [jtc](https://github.com/ldn-softdev/jtc) for walk path explanation) - DONE
2. add support for `SQL Foreign Key` to the tool (to build / make relational tables updates)


#### Enhancement requests are more than welcome: *ldn.softdev@gmail.com*



