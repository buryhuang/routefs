#include <iostream>
#include <sstream>
#include <string>

#include "leveldb/db.h"
#include "log.h"

#include "stats.h"

using namespace std;

leveldb::DB* _stats = NULL;

void * stats_hdl()
{
	return (void *)_stats;
}

int stats_init()
{
	// Set up database connection information and open database
	leveldb::Options options;
	options.create_if_missing = true;
	
	leveldb::Status status = leveldb::DB::Open(options, STATS_DB, &_stats);
	
	if (false == status.ok())
	{
	    cerr << "Unable to open post-processing database "<< STATS_DB << endl;
	    cerr << status.ToString() << endl;
	    return -1;
	}
	
	return 0;
}

int stats_set(const char * obj, const char * state)
{
	// Add 256 values to the database
	leveldb::WriteOptions writeOptions;
	_stats->Put(writeOptions, obj, state);
	
	return 0;
}

int stats_get(const char * obj, string &state)
{
	// Add 256 values to the database
	leveldb::ReadOptions readOptions;
	leveldb::Status status = _stats->Get(readOptions, obj, &state);
	
	if (false == status.ok())
	{
		return -1;
	}
	
	return 0;
}

int stats_del(const char * obj)
{
	// Add 256 values to the database
	leveldb::WriteOptions writeOptions;
	_stats->Delete(writeOptions, obj);
	
	return 0;
}

int stats_list(const char * prefix, vector<string>& obj_list)
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
	
	// Iterate over each item in the database and print them
	leveldb::Iterator* it = _stats->NewIterator(leveldb::ReadOptions());
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

int stats_dump_to_log()
{
	// Iterate over each item in the database and print them
	leveldb::Iterator* it = _stats->NewIterator(leveldb::ReadOptions());
	
	log_msg(LOG_LEVEL_ERROR, "\nstats_dump_to_log: starting dumping \n");
	
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
	
	log_msg(LOG_LEVEL_ERROR, "\nstats_dump_to_log: finished dumping\n");

	return 0;
}