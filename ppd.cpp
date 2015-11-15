#include <iostream>
#include <sstream>
#include <string>
#include <errno.h>
#include "log.h"
#include "store.h"
#include "objmap.h"
#include "postprocess.h"
#include "stats.h"
#include <unistd.h>

#include "leveldb/db.h"

#define PPD_LOGFILE "ppd.log"

using namespace std;

static pthread_t ppd_thread;

// Report errors to logfile and give -errno to caller
static int ppd_error(const char *str)
{
	int ret = -errno;
	printf("    ERROR %s: %s\n", str, strerror(errno));
	return ret;
}

void process_postprocess_file(const string path, const PP_ENTRY_T * pp_entry)
{
	log_msg(LOG_LEVEL_ERROR, "process_file: %s\n", path.c_str());

	int retstat = 0;
	// Then this is a non-dir file, need to get the real
	// destination

	string full_path;
	full_path = pp_entry->store_path[0];
	full_path += path;
	
	struct stat statbuf;
	log_msg(LOG_LEVEL_ERROR, "process_file:(fpath=\"%s\"), check for post-processing\n", full_path.c_str());
	retstat = lstat(full_path.c_str(), &statbuf);

	// @todo: now we only handle small files,
	// those < 16 KB
	// you know...one at a time
	if(retstat != -1) {
		std::string archor_path = pp_entry->store_path[0];
		if(archor_path == STORE_DATA_STAGING_SOURCE.store_name) {
			// Now, it's the staging/cache mode
			log_msg(LOG_LEVEL_ERROR, "process_file:(fpath=\"%s\"),post-processing:move from %s to %s\n",
				full_path.c_str(), archor_path.c_str(), STORE_DATA_STAGING_TARGET.store_name.c_str());

			retstat = store_migrate(path.c_str(), archor_path.c_str(), STORE_DATA_STAGING_TARGET.store_name.c_str(), STORE_DATA_STAGING_SOURCE.is_cached);
			
			if(retstat == 0) {
				// Update the database only after migration is successful
				#ifdef CACHE_MODE
				objmap_set(path.c_str(), STORE_DATA_STAGING_TARGET.store_name.c_str(), 2); // Only add to L2, not deleting one
				#else
				objmap_set(path.c_str(), STORE_DATA_STAGING_TARGET.store_name);
				#endif
				log_msg(LOG_LEVEL_ERROR, "process_file:(path=\"%s\"), post-process successfully migrated from %s to %s\n",
					path.c_str(),
					archor_path.c_str(),
					STORE_DATA_STAGING_TARGET.store_name.c_str());
				// Only now we can delete the item from queue
				log_msg(LOG_LEVEL_ERROR, "process_file:Done migration, removing from queue (path=\"%s\")\n", path.c_str());
				// @todo: better use a wrapper
				postprocess_del(path.c_str(), pp_entry);
			} else {
				retstat = ppd_error("ppd post-processing: migration failed");
			}
		} else if(archor_path == STORE_DATA_STAGING_TARGET.store_name) {
			// Now, it's the staging/cache mode
			log_msg(LOG_LEVEL_ERROR, "process_file:(fpath=\"%s\"),post-processing:promoting from %s to %s\n",
				full_path.c_str(), archor_path.c_str(), STORE_DATA_STAGING_SOURCE.store_name.c_str());

			// Never MOVE, COPY always in promotion
			retstat = store_migrate(path.c_str(), archor_path.c_str(), STORE_DATA_STAGING_SOURCE.store_name.c_str(), true);
			
			if(retstat == 0) {
				// Update the database only after migration is successful
				// Set L1 objmap
				objmap_set(path.c_str(), STORE_DATA_STAGING_SOURCE.store_name.c_str()); // Add to L1

				log_msg(LOG_LEVEL_ERROR, "process_file:(path=\"%s\"), post-process successfully promoted from %s to %s\n",
					path.c_str(),
					archor_path.c_str(),
					STORE_DATA_STAGING_SOURCE.store_name.c_str());
				// Only now we can delete the item from queue
				log_msg(LOG_LEVEL_ERROR, "process_file:Done promotion, removing from queue (path=\"%s\")\n", path.c_str());
				// @todo: better use a wrapper
				postprocess_del(path.c_str(), pp_entry);
			} else {
				retstat = ppd_error("ppd post-processing: promotion failed");
			}
		}
	} else {
		// Nothing to process, remove from queue
		printf("process_file:Nothing to process, removing from queue (path=\"%s\")\n", path.c_str());
		// @todo: better use a wrapper
		postprocess_del(path.c_str(), pp_entry);
	}
	printf("\n");
}

void process_postprocess_db(leveldb::DB* db) {
	// Iterate over each item in the database and print them
	leveldb::Iterator* it = db->NewIterator(leveldb::ReadOptions());
	
	for (it->SeekToFirst(); it->Valid(); it->Next())
	{
		cout << "Queue: "<< it->key().ToString() << " => " << it->value().ToString() << endl;
		PP_ENTRY_T pp_entry = *(reinterpret_cast<const PP_ENTRY_T *>(it->value().data()));
		process_postprocess_file(
			it->key().ToString(),
			&pp_entry
		);
	}
	
	if (false == it->status().ok())
	{
		cerr << "An error was found during the scan" << endl;
		cerr << it->status().ToString() << endl; 
	}
	
	delete it;
}

void process_postprocess_queue(const char * db_name)
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
	
	process_postprocess_db(db);
	
	// Close the database
	delete db;
}

void process_L1obj(leveldb::DB* db, const string path, const string store_path)
{
	printf("process_L1obj: %s => %s\n", path.c_str(), store_path.c_str());

	int retstat = 0;
	// Then this is a non-dir file, need to get the real
	// destination
	
	string full_path;
	full_path = store_path;
	full_path += path;

	struct stat statbuf;
	printf("process_L1obj: (fpath=\"%s\"), check for post-processing\n", full_path.c_str());
	retstat = lstat(full_path.c_str(), &statbuf);

	if(retstat != -1) {
		string obj_state;
		if(stats_get(path.c_str(), obj_state) == -1) {
			// Only when the object has not been accessed
			// And it's in L2 cache
			string L2obj_dest;
			if(objmap_get(path.c_str(), L2obj_dest, 2) != -1) {
				log_msg(LOG_LEVEL_ERROR, "process_L1obj: remove L1 file %s\n", full_path.c_str());
				retstat = unlink(full_path.c_str());
				if (retstat < 0)
				{
					retstat = ppd_error("process_L1obj: unlink");
				}
				log_msg(LOG_LEVEL_ERROR, "process_L1obj: Removing from queue (path=\"%s\")\n", path.c_str());
				// @todo: better use a wrapper
				db->Delete(leveldb::WriteOptions(), path.c_str());
			}
		}
	}
	printf("\n");
}

int process_L1obj_db() {
	leveldb::DB* L1obj = (leveldb::DB*)objmap_hdl(1);
	if(!L1obj) {
		if(objmap_init() == -1)
		{
			log_msg(LOG_LEVEL_ERROR, "\nprocess_L1obj_db: fail to init L1 objmap\n");
			return -1;
		}
	}

	// Iterate over each item in the database and print them
	leveldb::Iterator* it = L1obj->NewIterator(leveldb::ReadOptions());
	
	for (it->SeekToFirst(); it->Valid(); it->Next())
	{
	    cout << "Queue: "<< it->key().ToString() << " => " << it->value().ToString() << endl;
		process_L1obj(L1obj, it->key().ToString(), it->value().ToString());
	}
	
	if (false == it->status().ok())
	{
	    cerr << "An error was found during the scan" << endl;
	    cerr << it->status().ToString() << endl; 
	}
	
	delete it;
	
	return 0;
}

void * ppd_threadmain(void * arg)
{
	log_msg(LOG_LEVEL_ERROR, "ppd_threadmain: entry\n");

	while(1) {
		leveldb::DB* postprocess_db = (leveldb::DB*)postprocess_hdl();
		if(postprocess_db) {
			log_msg(LOG_LEVEL_DEBUG, "ppd_threadmain: process postprocess queue\n");
			process_postprocess_db(postprocess_db);
		}

		//leveldb::DB* L1obj = (leveldb::DB*)objmap_hdl(1);
		//if(L1obj) {
		//	log_msg(LOG_LEVEL_DEBUG, "ppd_threadmain: process stats queue\n");
		//	process_L1obj_db(L1obj);
		//}

		// @todo: for now all ppd tasks shares the same interval
		sleep(30);
	}

	log_msg(LOG_LEVEL_ERROR, "ppd_threadmain: end\n");
	
	return NULL;
}

void ppd_thread_start()
{
	//log_open(PPD_LOGFILE);

	log_msg(LOG_LEVEL_ERROR, "ppd_thread_start\n");
	if(pthread_create(&ppd_thread, NULL, ppd_threadmain, NULL)) {
		ppd_error("Error creating thread\n");
		return;
	}

	log_msg(LOG_LEVEL_ERROR, "ppd_thread_start: thread started\n");

	//log_msg(LOG_LEVEL_ERROR, "ppd_thread_start: wait for thread finish\n");
	/* wait for the thread to finish */
	//if(pthread_join(ppd_thread, NULL)) {
	//	ppd_error("Error joining thread\n");
	//	return;
	//}


}