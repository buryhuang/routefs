#include <iostream>
#include <sstream>
#include <string>
#include <errno.h>
#include "log.h"
#include "store.h"
#include "objmap.h"
#include "postprocess.h"
#include "ppd.h"

#define PPDMAIN_LOGFILE "ppd_main.log"

int main(int argc, char** argv)
{
	log_open(PPDMAIN_LOGFILE);
	objmap_init();
	cout << "=========POSTPROCESSING============" << endl;
	process_postprocess_queue(POSTPROCESS_DB.c_str());
	cout << endl;
	
	return 0;
}
