#include <stdio.h>
#include <string.h>

#include <fnmatch.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "../utils.h"
#include "../debug.h"
#include "dirtranslator.h"
#include "gemdos.h"

DirTranslator::DirTranslator()
{

}

DirTranslator::~DirTranslator()
{
    clear();
}

void DirTranslator::clear(void)
{
    std::map<std::string, FilenameShortener *>::iterator it;

    for(it = mapPathToShortener.begin(); it != mapPathToShortener.end(); ++it) {        // go through the map
        FilenameShortener *fs = it->second;                                             // get the filename shortener
        delete fs;                                                                      // and delete it
    }

    mapPathToShortener.clear();
}

void DirTranslator::shortToLongPath(std::string &rootPath, std::string &shortPath, std::string &longPath)
{
    #define MAX_DIR_NESTING     64
    static char longName[MAX_FILENAME_LEN];

    std::string strings[MAX_DIR_NESTING];
    int start = 0, pos;
    unsigned int i, found = 0;

    // replace all possible atari path separators to host path separators
    for(i=0; i<shortPath.length(); i++) {
        if(shortPath[i] == ATARIPATH_SEPAR_CHAR) {
            shortPath[i] = HOSTPATH_SEPAR_CHAR;
        }
    }

    // first split the string by separator
    while(1) {
        pos = shortPath.find(HOSTPATH_SEPAR_CHAR, start);

        if(pos == -1) {                             // not found?
            strings[found] = shortPath.substr(start);    // copy in the rest
            found++;
            break;
        }

        strings[found] = shortPath.substr(start, (pos - start));
        found++;

        start = pos + 1;

        if(found >= MAX_DIR_NESTING) {              // sanitize possible overflow
            break;
        }
    }

    // now convert all the short names to long names
    bool res;
    std::string pathPart = rootPath;

    for(i=0; i<found; i++) {
        std::map<std::string, FilenameShortener *>::iterator it;
        it = mapPathToShortener.find(pathPart);

        FilenameShortener *fs;
        if(it != mapPathToShortener.end()) {            // already got the shortener
            fs = it->second;
        } else {                                        // don't have the shortener yet
            fs = createShortener(pathPart);
        }

        res = fs->shortToLongFileName((char *) strings[i].c_str(), longName);   // try to convert the name

        if(res) {                               // if there was a long version of the file name, replace the short one
            strings[i] = longName;
        }

        Utils::mergeHostPaths(pathPart, strings[i]);   // build the path slowly
    }

    // and finally - put the string back together
    std::string final = "";

    for(i=0; i<found; i++) {
        if(strings[i].length() != 0) {      // not empty string?
            Utils::mergeHostPaths(final, strings[i]);
        }
    }

    longPath = final;
}

bool DirTranslator::longToShortFilename(std::string &longHostPath, std::string &longFname, std::string &shortFname)
{
    std::map<std::string, FilenameShortener *>::iterator it;
    it = mapPathToShortener.find(longHostPath);     // find the shortener for that host path

    FilenameShortener *fs;
    if(it != mapPathToShortener.end()) {            // already got the shortener
        fs = it->second;
    } else {                                        // don't have the shortener yet
        fs = createShortener(longHostPath);
    }

    char shortName[32];                             // try to shorten the name
    bool res = fs->longToShortFileName((char *) longFname.c_str(), shortName);   // try to convert the name from long to short

    if(res) {                                       // name shortened - store it
        shortFname = shortName;
    } else {                                        // failed to shorten - clear it
        shortFname.clear();
    }

    return res;
}

FilenameShortener *DirTranslator::createShortener(std::string &path)
{
    FilenameShortener *fs = new FilenameShortener();
    mapPathToShortener.insert( std::pair<std::string, FilenameShortener *>(path, fs) );

	DIR *dir = opendir((char *) path.c_str());						// try to open the dir
	
    if(dir == NULL) {                                 				// not found?
        return fs;
    }
	
    char shortName[14];
	
	while(1) {                                                  	// while there are more files, store them
		struct dirent *de = readdir(dir);							// read the next directory entry
	
		if(de == NULL) {											// no more entries?
			break;
		}
	
		if(de->d_type != DT_DIR && de->d_type != DT_REG) {			// not 	a file, not a directory?
			Debug::out("TranslatedDisk::createShortener -- skipped %s because the type %d is not supported!", de->d_name, de->d_type);
			continue;
		}

		fs->longToShortFileName(de->d_name, shortName);
    }

	closedir(dir);	
    return fs;
}

bool DirTranslator::buildGemdosFindstorageData(TFindStorage *fs, std::string hostSearchPathAndWildcards, BYTE findAttribs)
{
	std::string hostPath, searchString;

    Utils::splitFilenameFromPath(hostSearchPathAndWildcards, hostPath, searchString);

    // then build the found files list
	DIR *dir = opendir(hostPath.c_str());							// try to open the dir
	
    if(dir == NULL) {                                 				// not found?
        return false;
    }

    // initialize find storage in case anything goes bad
    fs->count       = 0;
    fs->fsnextStart = 0;

	while(1) {                                                  	// while there are more files, store them
		struct dirent *de = readdir(dir);							// read the next directory entry
	
		if(de == NULL) {											// no more entries?
			break;
		}
	
		if(de->d_type != DT_DIR && de->d_type != DT_REG) {			// not a file, not a directory?
			Debug::out("TranslatedDisk::onFsfirst -- skipped %s because the type %d is not supported!", de->d_name, de->d_type);
			continue;
		}

		// check the current name against searchString using fnmatch
		int ires = fnmatch((char *) searchString.c_str(), (char *) hostPath.c_str(), FNM_PATHNAME);
		
		if(ires != 0) {
			continue;
		}

		// finnaly append to the find storage
		appendFoundToFindStorage(hostPath, fs, de, findAttribs);

        if(fs->count >= fs->maxCount) {         					// avoid buffer overflow
            break;
        }
    }

	closedir(dir);
	return true;
}

void DirTranslator::appendFoundToFindStorage(std::string &hostPath, TFindStorage *fs, struct dirent *de, BYTE findAttribs)
{
    // TODO: verify on ST that the find attributes work like this

    // TOS 1.04 searches with findAttribs set to 0x10, that's INCLUDE DIRs

    // first verify if the file attributes are OK
	// TODO: do support for checking the READ ONLY flag on linux
	bool isReadOnly = false;
//    if((found->dwFileAttributes & FILE_ATTRIBUTE_READONLY)!=0   && (findAttribs & FA_READONLY)==0)  // is read only, but not searching for that
//        return;

// attribute not supported on linux
//    if((found->dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)!=0     && (findAttribs & FA_HIDDEN)==0)    // is hidden, but not searching for that
//        return;

// attribute not supported on linux
//    if((found->dwFileAttributes & FILE_ATTRIBUTE_SYSTEM)!=0     && (findAttribs & FA_SYSTEM)==0)    // is system, but not searching for that
//        return;

	bool isDir = (de->d_type == DT_DIR);
    if(isDir  && (findAttribs & FA_DIR)==0)       // is dir, but not searching for that
        return;

//    // this one is now disabled as on Win almost everything has archive bit set, and thus TOS didn't show any files
//    if((found->dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE)!=0    && (findAttribs & FA_ARCHIVE)==0)   // is archive, but not searching for that
//        return;

    //--------
    // add this file
    DWORD addr  = fs->count * 23;               // calculate offset
    BYTE *buf   = &(fs->buffer[addr]);          // and get pointer to this location

    BYTE atariAttribs;								// convert host to atari attribs
    Utils::attributesHostToAtari(isReadOnly, isDir, atariAttribs);

	std::string fullEntryPath 	= hostPath;
	std::string longFname		= de->d_name;
	Utils::mergeHostPaths(fullEntryPath, longFname);
	
	int res;
	struct stat attr;
    res = stat(fullEntryPath.c_str(), &attr);		// get the file status
	
	if(res != 0) {
		Debug::out("TranslatedDisk::appendFoundToFindStorage -- stat() failed");
		return;		
	}
	
	tm *time = localtime(&attr.st_mtime);			// convert time_t to tm structure
	
    WORD atariTime = Utils::fileTimeToAtariTime(time);
    WORD atariDate = Utils::fileTimeToAtariDate(time);
	
    std::string shortFname;

    res = longToShortFilename(hostPath, longFname, shortFname);

    if(!res) {
        return;
    }

    // now convert the short 'FILE.C' to 'FILE    .C  '
    char shortFnameExtended[14];
    FilenameShortener::extendWithSpaces((char *) shortFname.c_str(), shortFnameExtended);

    // GEMDOS File Attributes
    buf[0] = atariAttribs;

    // GEMDOS Time
    buf[1] = atariTime >> 8;
    buf[2] = atariTime &  0xff;

    // GEMDOS Date
    buf[3] = atariDate >> 8;
    buf[4] = atariDate &  0xff;

    // File Length
    buf[5] = attr.st_size >>  24;
    buf[6] = attr.st_size >>  16;
    buf[7] = attr.st_size >>   8;
    buf[8] = attr.st_size & 0xff;

    // Filename -- d_fname[14]
    memset(&buf[9], 0, 14);                                         // first clear the mem
    strncpy((char *) &buf[9], (char *) shortFnameExtended, 14);     // then copy only valid part of the string, max 14 chars

    fs->count++;
}


