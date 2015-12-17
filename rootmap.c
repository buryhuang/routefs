#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "rootmap.h"
#include "log.h"

// Can be substituted to any other "HASH" function
#include "crc32.c"

using namespace std;

string TYPEMAP_DB;

TYPEVAL_T _typeval;
TYPEMAP_T _rootmap;

static int _mkdir(const char *dir) {
	char tmp[256];
	char *p = NULL;
	size_t len;
	int status = 0;

	snprintf(tmp, sizeof(tmp),"%s",dir);
	len = strlen(tmp);
	if(tmp[len - 1] == '/') {
		tmp[len - 1] = 0;
	}
	for(p = tmp + 1; *p; p++) {
		if(*p == '/') {
			*p = 0;
			status = mkdir(tmp, S_IRWXU);
			if (status != 0 && status != EEXIST) {
				return status;
			}
			*p = '/';
		}
	}
	status = mkdir(tmp, S_IRWXU);
	if (status != 0 && status != EEXIST) {
		return status;
	}

	return 0;
}

TYPE_T get_type(const char * hint)
{
	return crc32_str_nocase(hint);
}

int rootmap_list()
{
#if 0
	char *actual_file = "Wedding.xlsx";
	const char *magic_full;
	magic_t magic_cookie;
	/*MAGIC_MIME tells magic to return a mime of the file, but you can specify different things*/
	magic_cookie = magic_open(MAGIC_NONE);
	if (magic_cookie == NULL) {
		printf("unable to initialize magic library\n");
		return 1;
		}
	printf("Loading default magic database\n");
	if (magic_load(magic_cookie, NULL) != 0) {
		printf("cannot load magic database - %s\n", magic_error(magic_cookie));
		magic_close(magic_cookie);
		return 1;
	}
	magic_full = magic_file(magic_cookie, actual_file);
	printf("%s\n", magic_full);
	magic_close(magic_cookie);
#endif
	return 0;
}

int rootmap_add_type(const char * type, const char * dest)
{
	TYPE_T val = get_type(type);
	_typeval[type] = val;
	_rootmap[val] = dest;
	
	return 0;
}

int rootmap_init_default(const char * default_root)
{
	// Default rule
	rootmap_add_type(TYPE_DEFAULT, default_root);

	return 0;
}

int rootmap_init (const char * default_meta, const char * default_data)
{
	STORE_ROOT = default_meta;

	TYPEMAP_DB = STORE_ROOT + "/.type.map";

	string temp_str;

	ifstream file;
	file.open(TYPEMAP_DB.c_str());
	if(file.fail()) {
		cout<<"Failed to open "<<TYPEMAP_DB<<endl;
		cout<<"Using default map"<<endl;
		// Always get default first
		rootmap_init_default(default_data);
		return 0;
	}
	while (!file.eof())
	{
		file >> temp_str;
		//cout << "LOADING: " << temp_str << endl;
		size_t pos1 = temp_str.find(',');
		if(pos1 == string::npos) {
			cout<<"Invalid format, skipping: " << temp_str << endl;
			continue;
		}
		string dest_path = temp_str.substr(pos1+1);
		if(!store_is_valid_store(dest_path.c_str())) {
			cout<<"Invalid dest, skipping: " << temp_str << endl;
			continue;
		}

		string hint_str = temp_str.substr(0, pos1);
		cout << "ADDING: " << hint_str << " -> " << dest_path << endl;
		log_msg(LOG_LEVEL_ERROR, "rootmap_init LOADING %s -> %s\n", hint_str.c_str(), dest_path.c_str());
		rootmap_add_type(hint_str.c_str(), dest_path.c_str());
	}

	for (TYPEMAP_T::const_iterator iter = _rootmap.begin();
		 iter != _rootmap.end();
		 ++iter)
	{
		cout << "TYPE MAP: " <<hex<< iter->first << " -> " << iter->second << endl;
	}
	return 0;
}

const string rootmap_gettype_str()
{
	ostringstream oss; 
	for (TYPEVAL_T::const_iterator iter = _typeval.begin();
		 iter != _typeval.end();
		 ++iter)
	{
		oss <<iter->first << " -> " <<hex<< iter->second << endl;
	}
	return oss.str();
}

const string rootmap_getmap_str()
{
	ostringstream oss; 
	for (TYPEMAP_T::const_iterator iter = _rootmap.begin();
		 iter != _rootmap.end();
		 ++iter)
	{
		oss <<hex<< iter->first << " -> " << iter->second << endl;
	}
	return oss.str();
}

const char * rootmap_getdest(const char * hint)
{
	const char * dest = NULL;
	if(hint) {
		try {
			dest = (_rootmap.at(get_type(hint))).c_str();
		}
		catch(const std::exception& ex) {
			dest = _rootmap[get_type(TYPE_DEFAULT)].c_str();
		}
	} else {
		dest = _rootmap[get_type(TYPE_DEFAULT)].c_str();
	}

	return dest;
}

#ifdef TEST
int main (int argc, char **argv)
{
	rootmap_list();
	rootmap_init();

	return 0;
}
#endif