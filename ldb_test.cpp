#include <iostream>
#include <sstream>
#include <string>
#include "objmap.h"
#include "postprocess.h"
#include "stats.h"

#include "leveldb/db.h"

using namespace std;

void dump_ldb(const char * db_name)
{
    // Set up database connection information and open database
    leveldb::DB* db;
    leveldb::Options options;
    options.create_if_missing = false;

    leveldb::Status status = leveldb::DB::Open(options, db_name, &db);

    if (false == status.ok())
    {
        cerr << "Unable to open/create test database "<< db_name << endl;
        cerr << status.ToString() << endl;
        return;
    }
    
    // Add 256 values to the database
    // leveldb::WriteOptions writeOptions;
    // for (unsigned int i = 0; i < 256; ++i)
    // {
    //     ostringstream keyStream;
    //     keyStream << "Key" << i;
    //     
    //     ostringstream valueStream;
    //     valueStream << "Test data value: " << i;
    //     
    //     db->Put(writeOptions, keyStream.str(), valueStream.str());
    // }
    
    // Iterate over each item in the database and print them
    leveldb::Iterator* it = db->NewIterator(leveldb::ReadOptions());
    
    for (it->SeekToFirst(); it->Valid(); it->Next())
    {
        cout << it->key().ToString() << " : " << it->value().ToString() << endl;
    }
    
    if (false == it->status().ok())
    {
        cerr << "An error was found during the scan" << endl;
        cerr << it->status().ToString() << endl; 
    }
    
    delete it;
    
    // Close the database
    delete db;
}

int main(int argc, char** argv)
{
	cout << "=========OBJMAP L1============" << endl;
	dump_ldb(OBJMAP_DB);
	cout << endl;

	cout << "=========OBJMAP L2============" << endl;
	dump_ldb(OBJMAP_DB2);
	cout << endl;

	cout << "=========POSTPROCESSING============" << endl;
	dump_ldb(POSTPROCESS_DB);
	cout << endl;

	cout << "=========STATS_DB============" << endl;
	dump_ldb(STATS_DB);
	cout << endl;

	return 0;
}