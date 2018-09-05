# jsl - JSON to Sqlite db dumper

offline tool to store regular JSON structures into Sqlite3 database

#### Linux and MacOS precompiled binaries are available for download:
- [macOS 64 bit](https://github.com/ldn-softdev/jsl/raw/master/jsl-macos-64.v1.00)
- [macOS 32 bit](https://github.com/ldn-softdev/jsl/raw/master/jsl-macos-32.v1.00)
- [linux 64 bit](https://github.com/ldn-softdev/jsl/raw/master/jsl-linux-64.v1.00)
- [linux 32 bit](https://github.com/ldn-softdev/jsl/raw/master/jsl-linux-32.v1.00)

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


Instead of listing each column individually, `-M` option lets listing all of the together over comma:
```
bash $ cat ab.json | jsl -M "Name, city, street address, state, postal code" sql.db ADDRESS_BOOK
```
all heading and trailing spaces in `-M` parameter around JSON labels will be stripped (if JSON contains such spacing, 
then use `-m` instead). Also, if both options are given, then option `-M` is always processed *after* `-m`, thus following two examples
are equal:
  - `jsl -M "b, c" -m a file.db TABLE`
  - `jsl -m a -m b -m c file.db TABLE`



##### 2. exclude table columns from the update (`-i` explained)

Let's rollback to the empty ADDRESS_BOOK table,
```
bash $ sqlite3 sql.db "DELETE FROM ADDRESS_BOOK;"
```
and this time try dumping all values except `State` column. For that purpose option `-i` comes handy:`
```
bash $ cat ab.json | jsl -m Name -M "city, street address, postal code" -i State sql.db ADDRESS_BOOK
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


Opotion `-i` has a similar counterpart `-I` letting listing multiple db table columns (though in this case, the order of listed parameters is irrelevant)


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
updated 3 records into sql.db, table: ADDRESS_BOOK
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


##### 4. expand JSON containers (`-e` explained)
if mapped JSON value (through `-m` or `-M`) is pointing to JSON array or object (a.k.a. iterable), then it's possible to subject 
the mapped iterable to the column update. The option `-e` does the trick, but it has to preceed every option `-m` where such 
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
updated 3 records into sql.db, table: ADDRESS_BOOK
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


##### 5. tables with `AUTOINCREMENT`'ed columns
  - if table definition caters any columns with keyword `AUTOINCREMENT`, such columns will be automatically ignored 
(i.e. no need listing it in `-i`, `-I`), obviously such columns won't be part of any mapping

That allows defining a rule for consitent and predictable mapping:

*A number of mapped JSON values (after all values expansions) plus number of ignored columns should match the number of
columns in the updated table minus those with 'AUTOINCREMENT'*


#### Planned enhancements:
1. add capability to specify in maping (`-m`, `-M`) json walk paths in addition to JSON labels 
(see [jtc](https://github.com/ldn-softdev/jtc) for walk path explanation)
2. add support for `SQL Foreign Key` to the tool (to build / make relational tables updates)


#### Enhancement requests are more than welcome: *ldn.softdev@gmail.com*



