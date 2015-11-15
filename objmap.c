#include <iostream>
#include <sstream>
#include <string>

#include "log.h"
#include "leveldb/db.h"

#include "objmap.h"

using namespace std;

leveldb::DB* _objmap = NULL;

//#ifdef CACHE_MODE
leveldb::DB* _objmap_L2 = NULL;
//#endif

void * objmap_hdl(int level)
{
	if(level == 2)
	{
		return (void *)_objmap_L2;
	}

	return (void *)_objmap;
}

int objmap_init()
{
	// Set up database connection information and open database
	leveldb::Options options;
	options.create_if_missing = true;
	leveldb::Status status;
	
	status = leveldb::DB::Open(options, OBJMAP_DB, &_objmap);
	
	if (false == status.ok())
	{
	    cerr << "Unable to open objmap database "<< OBJMAP_DB << endl;
	    cerr << status.ToString() << endl;
	    return -1;
	}
	
//#ifdef CACHE_MODE
	status = leveldb::DB::Open(options, OBJMAP_DB2, &_objmap_L2);
	
	if (false == status.ok())
	{
	    cerr << "Unable to open objmap database "<< OBJMAP_DB2 << endl;
	    cerr << status.ToString() << endl;
	    return -1;
	}
//#endif

	return 0;
}

int objmap_set(const char * obj, const char * dest, int level)
{
	// Add 256 values to the database
	leveldb::WriteOptions writeOptions;
	leveldb::Status status;
	switch(level) {
		case 1:
			status = _objmap->Put(writeOptions, obj, dest);
			break;
		case 2:
			status = _objmap_L2->Put(writeOptions, obj, dest);
			break;
		default:
			return -1;
	}

	if (false == status.ok())
	{
		return -1;
	}

	return 0;
}

int objmap_get(const char * obj, string &destStr, int level)
{
	// Add 256 values to the database
	leveldb::ReadOptions readOptions;
	leveldb::Status status;
	
	switch(level) {
		case 1:
			status = _objmap->Get(readOptions, obj, &destStr);
			break;
		case 2:
			status = _objmap_L2->Get(readOptions, obj, &destStr);
			break;
		default:
			return -1;
	}
	
	if (false == status.ok())
	{
		return -1;
	}
	
	return 0;
}

int objmap_del(const char * obj, int level)
{
	// Add 256 values to the database
	leveldb::WriteOptions writeOptions;
	leveldb::Status status;

	switch(level) {
		case 1:
			status = _objmap->Delete(writeOptions, obj);
			break;
		case 2:
			status = _objmap_L2->Delete(writeOptions, obj);
			break;
		default:
			return -1;
	}
	
	if (false == status.ok())
	{
		return -1;
	}

	return 0;
}

int objmap_list(const char * prefix, vector<string>& obj_list, int level)
{
	// Except for root "/"
	// the prefix comes without trailling seperator, for example:
	// "/this_is_a_folder"
	//
	// we need to return the basename of following:
	// /this_is_a_folder/this_is_file.txt
	// /this_is_file.txt


	if(!prefix) {
		return -1;
	}
	// @todo: You think I don't know the performance here is poor..?
	
	leveldb::DB* objmap = NULL;
	switch(level) {
		case 1:
			objmap = _objmap;
			break;
		case 2:
			objmap = _objmap_L2;
			break;
		default:
			return -1;
	}

	// Iterate over each item in the database and print them
	leveldb::Iterator* it = objmap->NewIterator(leveldb::ReadOptions());
	size_t prefix_len = strlen(prefix);
	
	for (it->SeekToFirst(); it->Valid(); it->Next())
	{
		string keyStr = it->key().ToString();
		size_t preLoc = keyStr.find_last_of("/");
		
		if(preLoc != string::npos
			&& (
				(preLoc + 1) == prefix_len
				|| preLoc == prefix_len
			)
			&& (
				keyStr.substr(0, preLoc+1) == prefix // root
				|| keyStr.substr(0, preLoc) == prefix // non-root, no trailing
			)
		) {
			string leftStr = keyStr.substr(preLoc+1);
			obj_list.push_back(leftStr);
		}
	}
	
	if (false == it->status().ok())
	{
	    cerr << "An error was found during the scan" << endl;
	    cerr << it->status().ToString() << endl; 
	}
	
	delete it;
	
	return 0;
}

int objmap_dump_to_log(int level)
{
	// Iterate over each item in the database and print them
	leveldb::Iterator* it = NULL;
	if(level == 1)
	{
		it = _objmap->NewIterator(leveldb::ReadOptions());
	}
	else if(level == 2)
	{
		it = _objmap_L2->NewIterator(leveldb::ReadOptions());
		
	}
	if(!it)
	{
		log_msg(LOG_LEVEL_ERROR, "\nobjmap_dump_to_log: db level %d not open, returning\n", level);
		return -1;
	}
	
	log_msg(LOG_LEVEL_ERROR, "\nobjmap_dump_to_log: starting dumping level %d\n", level);
	
	for (it->SeekToFirst(); it->Valid(); it->Next())
	{
		std::string log_entry;
		log_entry = "";
		log_entry += it->key().ToString();
		log_entry += " : ";
		log_entry += it->value().ToString();
		log_entry += "\n";
	    cout << it->key().ToString() << " : " << it->value().ToString() << endl;
	    log_msg(LOG_LEVEL_ERROR, "%s", log_entry.c_str());
	}
	
	if (false == it->status().ok())
	{
	    cerr << "An error was found during the scan" << endl;
	    cerr << it->status().ToString() << endl; 
	}
	
	delete it;
	
	log_msg(LOG_LEVEL_ERROR, "\nobjmap_dump_to_log: finished dumping level %d\n", level);

	return 0;
}
