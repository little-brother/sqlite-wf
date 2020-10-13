#ifndef __SESSION_H__
#define __SESSION_H__

namespace session {
	bool run(int id, const char* dbpath, int parent_sid, int argc, const char** values);
}
#endif
