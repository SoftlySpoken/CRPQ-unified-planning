MillenniumDB
================================================================================
MillenniumDB is a graph oriented database management system developed by the [Millennium Institute for Foundational Research on Data (IMFD)](https://imfd.cl/).

The main objective of this project is to create a fully functional and easy-to-extend DBMS that serves as the basis for testing new techniques and algorithms related to databases and graphs. We aim to support multiple graph models. RDF/SPARQL support is fairly complete and we are also working on a variation of property graphs.

This project is still in active development and is not production ready yet, some functionality is missing and there may be bugs.



Table of Contents
================================================================================
- [SPARQL Support](#sparql-support)
- [Project Build](#project-build)
- [Using MillenniumDB](#using-millenniumdb)
- [Example](#example)


[SPARQL Support](#millenniumdb)
================================================================================
Currently Unsupported SPARQL Features
--------------------------------------------------------------------------------
- Updates other than `INSERT DATA` and `DELETE DATA`
- Named graphs
- The `FROM` clause
- The `GRAPH` keyword
- Regular expression flags other than `i`
- Property paths with Negated Property Sets

Deviations from the SPARQL Specification
--------------------------------------------------------------------------------
- **Language tag** (`@`) handling is **case sensitive** for `JOIN`s and related operators, but in **expressions** it is **case insensitive**.
- We do **not** store the exact **lexical representation** of numeric datatypes, only the **numeric value**. For example, `"01"^^xsd:integer` and `"1"^^xsd:integer` are identical in MillenniumDB.
    - This implies that expressions that work with the lexical representation may result in a different value. For example `STR(1e0)` should be `"1e0"` according to the standard, but MillenniumDB will evaluate it as `"1.0E0"`.
- We do not differentiate between `"0"^^xsd:boolean` and `false` / `"false"^^xsd:boolean` or between `"1"^^xsd:boolean` and `true` / `"true"^^xsd:boolean`.
- Our implementation uses **ECMAScript** regular expressions, not **Perl** regular expressions.
- The regular path expression `?s :P* ?o` won't return all the nodes in the database that appears as a subject or object in some triple as the standard says. Instead it will only return the nodes that appears as a subject in a triple with predicate `:P`.


[Project build](#millenniumdb)
================================================================================
MillenniumDB should be able to be built on any x86-64 Linux distribution.
On windows, Windows Subsystem for Linux (WSL) can be used.


Install Dependencies:
--------------------------------------------------------------------------------
MillenniumDB needs the following dependencies:
- GCC >= 8.1
- CMake >= 3.12
- Git
- libssl
- ncursesw and less for the CLI
- Python >= 3.8 with venv to run tests

On current Debian and Ubuntu based distributions they can be installed by running:
```bash
sudo apt update && sudo apt install git g++ cmake libssl-dev libncurses-dev locales less python3 python3-venv libomp-dev
```

The `en_US.UTF-8` locale also needs to be generated.\
On Ubuntu based distributions this can be done as follows:
```bash
sudo locale-gen en_US.UTF-8
```

On distributions without a patched locale-gen you can run:
```bash
sudo sed -i '/en_us.utf-8/Is/^# //g' /etc/locale.gen
sudo locale-gen
```

Or manually uncomment `en_US.UTF-8` in `/etc/locale.gen` and run:
```bash
sudo locale-gen
```


Clone the repository
--------------------------------------------------------------------------------
 Clone this repository, enter the repository root directory and set `MDB_HOME`:
```
git clone git@github.com:MillenniumDB/MillenniumDB-PreRelease.git
cd MillenniumDB-PreRelease
export MDB_HOME=$(pwd)
```


Install Boost
--------------------------------------------------------------------------------
Download [`boost_1_82_0.tar.gz`](https://boostorg.jfrog.io/artifactory/main/release/1.82.0/source/boost_1_82_0.tar.gz) using a browser or wget:
```bash
wget -q --show-progress https://boostorg.jfrog.io/artifactory/main/release/1.82.0/source/boost_1_82_0.tar.gz
```

and run the following in the directory where boost was downloaded:
```bash
tar -xf boost_1_82_0.tar.gz
mkdir -p $MDB_HOME/third_party/boost_1_82/include
mv boost_1_82_0/boost $MDB_HOME/third_party/boost_1_82/include
rm -r boost_1_82_0.tar.gz boost_1_82_0
```


Build the Project:
--------------------------------------------------------------------------------
Go back into the repository root directory and configure and build MillenniumDB:
```
cmake -B build/Release -D CMAKE_BUILD_TYPE=Release && cmake --build build/Release/
```
To use multiple cores during compilation (much faster) use the following command and replace `<n>` with the desired number of threads:
```
cmake -B build/Release -D CMAKE_BUILD_TYPE=Release && cmake --build build/Release/ -j <n>
```



[Using MillenniumDB](#millenniumdb)
================================================================================
MillenniumDB supports two database formats: RDF and QuadModel. A RDF database can only be queried with SPARQL and a QuadModel database can only be queried with MQL. In this document we will focus on RDF/SPARQL.


Creating a Database
--------------------------------------------------------------------------------
```
build/Release/bin/mdb-import <data-file> <db-directory> [--prefixes <prefixes-file>]
```
- `<data-file>` is the path to the file containing the data to import, in the [Turtle](https://www.w3.org/TR/turtle/) format.
- `<db-directory>` is the path of the directory where the new database will be created.
- `--prefixes <prefixes-file>` is an optional path to a prefixes file.

### Prefix Definitions
The optional prefixes file passed using the `--prefixes` option contains one prefix per line:

```
http://www.myprefix.com/
https://other.prefix.com/foo
https://other.prefix.com/bar
```

Using a prefix file is optional, but helps reduce the space occupied by IRIs in the database. MillenniumDB generates IDs for each prefix, and when importing IRIs into the database replaces any prefixes with IDs. For large databases this can save a significant amount of space. The total number of user defined prefixes cannot exceed 255.


Querying a Database
--------------------------------------------------------------------------------
We implement the typical client/server model, so in order to query a database, you need to have a server running and then send queries to it.

### Run the Server
To run the server use the following command, passing the `<db-directory>` where the database was created:
```
build/Release/bin/mdb-server <db-directory>
```

### Server parameters
When using large data, it is advisable to adjust some parameters for the best performance of the server.
Here we explain the parameters that `mdb-server` supports:

- `--threads`: Set how many thread workers the server will have. The server can execute up to that amount of queries at the same time.

- `--load-strings`: How many bytes of the string file (`strings.dat`) are preloaded into memory at startup, before the server is available.

- `--versioned-buffer`: Size of the buffer for shared structures that need MVCC (for now, just B+trees).

- `--private-buffer`: Size of the buffer that each worker have available for some temporal things (used in ORDER BY, GROUP BY, etc).

- `--unversioned-buffer`: Size of the buffer for shared structures that don't need MVCC (for now, just `str_hash.dat`).

- `--timeout`: How many seconds until the query might throw a timeout.

- `--port`: Specify the port to use (default is 8080)

- `--limit`: Specify a hard limit of how many results are returned in any query.


### Execute a Query
The MillenniumDB SPARQL server supports all three query operations specified in the [SPARQL 1.1 Protocol](https://www.w3.org/TR/2013/REC-sparql11-protocol-20130321/#query-operation):
- `query via GET`
- `query via URL-encoded POST`
- `query via POST directly`

We also provide a Python script that makes queries using the `SparqlWrapper` library.\
To use it you have to install `SparqlWrapper`:
```
pip3 install sparqlwrapper
```
You can then use the script to make queries as follows:
```
python3 scripts/sparql_query.py <query-file>
```
where `<query-file>` is the path to a file containing a query in SPARQL format.



[Example](#millenniumdb)
================================================================================
This is a step by step example of creating a database, running the server and making a query.\
To run this example MillenniumDB has to be [built](#project-build) first.


Create an Example Database
--------------------------------------------------------------------------------
From the repository root directory run the following command to create the example database:
```
build/Release/bin/mdb-import data/example-rdf-database.ttl data/example-rdf-database
```
That should have created the directory `data/example-rdf-database` containing a database initialized with the data from `data/example-rdf-database.ttl`.


Launch the Server
--------------------------------------------------------------------------------
The server can now be launched with the previously created database:
```
build/Release/bin/mdb-server data/example-rdf-database
```


Execute a Query
--------------------------------------------------------------------------------
If not already done previously, install SparqlWrapper:
```
pip3 install sparqlwrapper
```
Open another terminal and enter the repository root directory.\
Then run the following command to execute an example query:
```
python3 scripts/sparql_query.py data/example-sparql-query.rq
```
The query result should be shown in the terminal.


Remove the Database
--------------------------------------------------------------------------------
To remove the database that was created just delete the directory:
```
rm -r data/example-rdf-database
```
