#include <pthread.h>
#include <time.h>

//#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>

#include "leveldb/db.h"
#include "log.h"
#include "utils.h"

#include "postprocess.h"

using namespace std;

leveldb::DB* _postprocess = NULL;
pthread_mutex_t _postprocess_mutex = PTHREAD_MUTEX_INITIALIZER;
uint_least64_t _obj_id = 0;

const char * OBJ_GID_KEY = "__obj_gid__";

void * postprocess_hdl()
{
	return (void *)_postprocess;
}

/*
 * this function is intended to be used with lock acquired
 * do NOT acquire lock again.
 */
uint_least64_t get_obj_gid()
{
	//  * do NOT acquire lock again.
	// AutoLock lock(&_postprocess_mutex);

	_obj_id++;

	// Add 256 values to the database
	PP_ENTRY_T * pp_entry = new PP_ENTRY_T();
	
	pp_entry->state = 0;
	pp_entry->obj_id = _obj_id;
	
	// Add 256 values to the database
	leveldb::WriteOptions writeOptions;
	leveldb::Slice slice((char *)pp_entry, sizeof(PP_ENTRY_T));
	_postprocess->Put(writeOptions, OBJ_GID_KEY, slice);
	
	return _obj_id;
}

int postprocess_init()
{
	AutoLock lock(&_postprocess_mutex);

	// Set up database connection information and open database
	leveldb::Options options;
	options.create_if_missing = true;
	
	leveldb::Status status = leveldb::DB::Open(options, POSTPROCESS_DB, &_postprocess);
	
	if (false == status.ok())
	{
	    cerr << "Unable to open post-processing database "<< POSTPROCESS_DB << endl;
	    cerr << status.ToString() << endl;
	    return -1;
	}

	// Initialize/reload the global persistent variables
	leveldb::ReadOptions readOptions;
	std::string dbval;
	status = _postprocess->Get(readOptions, OBJ_GID_KEY, &dbval);
	
	if (true == status.ok())
	{
		leveldb::Slice slice = dbval; //string -> leveldb::Slice
		 const PP_ENTRY_T * pp_entry = reinterpret_cast<const PP_ENTRY_T *>(slice.data());
		_obj_id = pp_entry->obj_id;
	}
	
	log_msg(LOG_LEVEL_ERROR, "\npostprocess_init: postprocess obj_gid: %llu \n", _obj_id);
	
	return 0;
}

int postprocess_set(const char * obj, int state, std::string store_path1)
{
	AutoLock lock(&_postprocess_mutex);

	leveldb::ReadOptions readOptions;
	std::string dbval;
	leveldb::Status status = _postprocess->Get(readOptions, obj, &dbval);

	if (true != status.ok())
	{
		PP_ENTRY_T * pp_entry = new PP_ENTRY_T();
		
		pp_entry->state = state;
		pp_entry->store_path[0] = store_path1;
		pp_entry->obj_id = get_obj_gid();
		
		// Add 256 values to the database
		leveldb::WriteOptions writeOptions;
		leveldb::Slice slice((char *)pp_entry, sizeof(PP_ENTRY_T));
		_postprocess->Put(writeOptions, obj, slice);
	}

	return 0;
}

int postprocess_set(const char * obj, int state, std::string store_path1, std::string store_path2)
{
	AutoLock lock(&_postprocess_mutex);
	
	if(std::string(obj) == "/.ifsctl")
	{
		return 0;
	}

	leveldb::ReadOptions readOptions;
	std::string dbval;
	leveldb::Status status = _postprocess->Get(readOptions, obj, &dbval);

	if (true != status.ok())
	{
		PP_ENTRY_T * pp_entry = new PP_ENTRY_T();
		
		pp_entry->state = state;
		pp_entry->store_path[0] = store_path1;
		pp_entry->store_path[1] = store_path2;
		pp_entry->obj_id = get_obj_gid();
		
		// Add 256 values to the database
		leveldb::WriteOptions writeOptions;
		leveldb::Slice slice((char *)pp_entry, sizeof(PP_ENTRY_T));
		_postprocess->Put(writeOptions, obj, slice);
	}

	return 0;
}

int postprocess_get(const char * obj, const PP_ENTRY_T * &pp_entry)
{
	AutoLock lock(&_postprocess_mutex);

	if(std::string(obj) == "/.ifsctl")
	{
		return 0;
	}

	leveldb::ReadOptions readOptions;
	std::string dbval;
	leveldb::Status status = _postprocess->Get(readOptions, obj, &dbval);
	
	if (false == status.ok())
	{
		return -1;
	}
	
	leveldb::Slice slice = dbval; //string -> leveldb::Slice
	pp_entry = reinterpret_cast<const PP_ENTRY_T *>(slice.data());
	
	return 0;
}

/*
 * Not a typo.
 * Having pp_entry here is to avoid deleting updated objects, by checking gid in the pp_entry
 */
int postprocess_del(const char * obj, const PP_ENTRY_T *pp_entry)
{
	if(OBJ_GID_KEY == obj)
	{
		// @todo: there is a catagory of VARs, no hardcoded gid key only
		return 0;
	}

	AutoLock lock(&_postprocess_mutex);

	leveldb::ReadOptions readOptions;
	std::string dbval;
	leveldb::Status status = _postprocess->Get(readOptions, obj, &dbval);
	
	if (false == status.ok())
	{
		// Don't have to fail it since we are deleting this entry
		leveldb::Slice slice = dbval; //string -> leveldb::Slice
		const PP_ENTRY_T * this_pp_entry = reinterpret_cast<const PP_ENTRY_T *>(slice.data());
		if(this_pp_entry->obj_id != pp_entry->obj_id)
		{
			log_msg(LOG_LEVEL_ERROR, "\npostprocess_del: trying to delete an updated obj, ignoring. Origin objid[%llu], updated objid[%llu] \n", pp_entry->obj_id, this_pp_entry->obj_id);

			return 0;
		}
		delete pp_entry;
	}

	// Now remove the entry
	leveldb::WriteOptions writeOptions;
	_postprocess->Delete(writeOptions, obj);

	return 0;
}

int postprocess_list(const char * prefix, vector<string>& obj_list)
{
	AutoLock lock(&_postprocess_mutex);

	// Except for root "/"
	// the prefix comes without trailling seperator, for example:
	// "/this_is_a_folder"
	//
	// we need to return the basename of following:
	// /this_is_a_folder/this_is_file.txt
	// /this_is_file.txt

	log_msg(LOG_LEVEL_ERROR, "\npostprocess_list: generating postprocess_list \n");

	if(!prefix) {
		return -1;
	}
	// @todo: You think I don't know the performance here is poor..?
	
	// Iterate over each item in the database and print them
	leveldb::Iterator* it = _postprocess->NewIterator(leveldb::ReadOptions());

	for (it->SeekToFirst(); it->Valid(); it->Next())
	{
		string abs_path = it->key().ToString();
		string rel_path;
		if(abs_to_relative_path(prefix, abs_path, rel_path))
		{
			obj_list.push_back(rel_path);
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

int postprocess_dump_to_log()
{
	AutoLock lock(&_postprocess_mutex);

	// Iterate over each item in the database and print them
	leveldb::Iterator* it = _postprocess->NewIterator(leveldb::ReadOptions());
	
	log_msg(LOG_LEVEL_ERROR, "\npostprocess_dump_to_log: starting dumping \n");
	
	for (it->SeekToFirst(); it->Valid(); it->Next())
	{
		const PP_ENTRY_T * pp_entry = reinterpret_cast<const PP_ENTRY_T *>(it->value().data());

		std::ostringstream log_entry;
		log_entry << "[pp_entry]: ";
		log_entry << it->key().ToString();
		log_entry << " : ";
		log_entry << pp_entry->obj_id;
		log_entry << " : ";
		log_entry << pp_entry->store_path[0];
		cout << log_entry << endl;
		log_msg(LOG_LEVEL_ERROR, "%s\n", log_entry.str().c_str());
	}
	
	if (false == it->status().ok())
	{
	    cerr << "An error was found during the scan" << endl;
	    cerr << it->status().ToString() << endl; 
	}
	
	delete it;
	
	log_msg(LOG_LEVEL_ERROR, "\npostprocess_dump_to_log: finished dumping\n");

	return 0;
}
