#include "SequentialFileRK.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <vector>

static Logger _log("app.seqfile");

class SequentialFileShared {
public:
    static SequentialFileShared &getInstance();

    void addObject(SequentialFile *obj);
    void removeObject(SequentialFile *obj);

    SequentialFile *findByPath(const char *dirPath);

protected:
    SequentialFileShared();
    virtual ~SequentialFileShared();

    std::vector<SequentialFile *>objects;
    mutable os_mutex_t objectsMutex = 0;

    static SequentialFileShared *_instance;
};

SequentialFileShared &SequentialFileShared::getInstance() {
    if (!_instance) {
        _instance = new SequentialFileShared();
    }
    return *_instance;
}

void SequentialFileShared::addObject(SequentialFile *obj) {
    if (!objectsMutex) {
        os_mutex_create(&objectsMutex);
    }
    os_mutex_lock(objectsMutex);
    objects.push_back(obj);
    os_mutex_unlock(objectsMutex);
}

void SequentialFileShared::removeObject(SequentialFile *obj) {
    os_mutex_lock(objectsMutex);
    for(auto iter = objects.begin(); iter != objects.end(); iter++) {
        if (*iter == obj) {
            objects.erase(iter);
            break;
        }
    }
    os_mutex_unlock(objectsMutex);
}
SequentialFile *SequentialFileShared::findByPath(const char *dirPath) {
    SequentialFile *result = 0;

    os_mutex_lock(objectsMutex);
    for(auto iter = objects.begin(); iter != objects.end(); iter++) {
        if (strcmp((*iter)->getDirPath(), dirPath) == 0) {
            result = *iter;
            break;
        }
    }
    os_mutex_unlock(objectsMutex);
}


SequentialFileShared::SequentialFileShared() {
}

SequentialFileShared::~SequentialFileShared() {

}


SequentialFileShared* SequentialFileShared::_instance;



SequentialFile::SequentialFile() {
    SequentialFileShared::getInstance().addObject(this);
}

SequentialFile::SequentialFile(const char *dirPath) {
    withDirPath(dirPath);
    SequentialFileShared::getInstance().addObject(this);
}

SequentialFile::SequentialFile(const char *dirPath, const char *ext) {
    withDirPath(dirPath);
    withFilenameExtension(ext);
    SequentialFileShared::getInstance().addObject(this);
}


SequentialFile::~SequentialFile() {

    SequentialFileShared::getInstance().removeObject(this);
}

SequentialFile &SequentialFile::withDirPath(const char *dirPath) { 
    this->dirPath = dirPath; 
    if (this->dirPath.endsWith("/")) {
        this->dirPath = this->dirPath.substring(0, this->dirPath.length() - 1);
    }
    return *this; 
};


bool SequentialFile::scanDir(void) {
    if (dirPath.length() <= 1) {
        // Cannot use an unconfigured directory or "/"!
        _log.error("unconfigured dirPath");
        return false;
    }

    if (!createDirIfNecessary(dirPath)) {
        return false;
    }

    _log.trace("scanning %s with pattern %s", dirPath.c_str(), pattern.c_str());

    DIR *dir = opendir(dirPath);
    if (!dir) {
        return false;
    }
    
    lastFileNum = 0;

    while(true) {
        struct dirent* ent = readdir(dir); 
        if (!ent) {
            break;
        }
        
        if (ent->d_type != DT_REG) {
            // Not a plain file
            continue;
        }
        
        int fileNum;
        if (sscanf(ent->d_name, pattern, &fileNum) != 1) {
            // Not a numeric file
            continue;
        }
        if (filenameExtension.length() != 0 && !String(ent->d_name).endsWith(filenameExtension)) {
            // Not a matching extension
            continue;
        }
        // 
        if (!preScanAddHook(fileNum)) {
            // Pre-scan add hook returned false (do not queue file)
            continue;
        }
                    
        if (fileNum > lastFileNum) {
            lastFileNum = fileNum;
        }
        _log.trace("adding to queue %d %s", fileNum, ent->d_name);

        queueMutexLock();
        queue.push_back(fileNum); 
        queueMutexUnlock();
    }
    closedir(dir);
    
    scanDirCompleted = true;
    return true;
}

int SequentialFile::reserveFile(void) {
    if (!scanDirCompleted) {
        scanDir();
    }

    return ++lastFileNum;
}

void SequentialFile::addFileToQueue(int fileNum) {
    if (!scanDirCompleted) {
        scanDir();
    }
    if (fileNum > lastFileNum) {
        lastFileNum = fileNum;
    }

    queueMutexLock();
    queue.push_back(fileNum); 
    queueMutexUnlock();
}
 
int SequentialFile::getFileFromQueue(void) {
    int fileNum = 0;

    if (!scanDirCompleted) {
        scanDir();
    }

    queueMutexLock();
    if (!queue.empty()) {
        fileNum = queue.front();
        queue.pop_front();
    }
    queueMutexUnlock();

    _log.trace("getFileFromQueue returned %d", fileNum);

    return fileNum;
}

String SequentialFile::getNameForFileNum(int fileNum, const char *overrideExt) {
    String name = String::format(pattern.c_str(), fileNum);

    return getNameWithOptionalExt(name, (overrideExt ? overrideExt : filenameExtension.c_str()));
}

String SequentialFile::getPathForFileNum(int fileNum, const char *overrideExt) {
    String result;
    result.reserve(dirPath.length() + pattern.length() + 4);

    // dirPath never ends with a "/" because withDirName() removes it if it was passed in
    result = dirPath + String("/") + getNameForFileNum(fileNum, overrideExt);

    return result;
}


void SequentialFile::removeFileNum(int fileNum, bool allExtensions) {
    if (allExtensions) {
        DIR *dir = opendir(dirPath);
        if (dir) {
            while(true) {
                struct dirent* ent = readdir(dir); 
                if (!ent) {
                    break;
                }
                
                if (ent->d_type != DT_REG) {
                    // Not a plain file
                    continue;
                }
                
                int curFileNum;
                if (sscanf(ent->d_name, pattern.c_str(), &curFileNum) == 1) {
                    if (curFileNum == fileNum) {
                        // dirPath never ends with a "/" because withDirName() removes it if it was passed in
                        String path = dirPath + String("/") + ent->d_name;
                        unlink(path);
                        _log.trace("removed %s", path.c_str());
                    }
                }
            }
            closedir(dir);
        }
    }
    else {
        String path = getPathForFileNum(fileNum); 
        unlink(path);
        _log.trace("removed %s", path.c_str());
    }
}

void SequentialFile::removeAll(bool removeDir) {
    DIR *dir = opendir(dirPath);
    if (dir) {
        while(true) {
            struct dirent* ent = readdir(dir); 
            if (!ent) {
                break;
            }
            
            if (ent->d_type != DT_REG) {
                // Not a plain file
                continue;
            }
            
            String path = dirPath + String("/") + ent->d_name;
            unlink(path);
            _log.trace("removed %s", path.c_str());
        }
        closedir(dir);
    }    
    queueMutexLock();
    queue.clear();
    queueMutexUnlock();


    if (removeDir) {
        rmdir(dirPath);
    }
}

int SequentialFile::getQueueLen() const {
    queueMutexLock();
    int size = queue.size();
    queueMutexUnlock();

    return size;
}


void SequentialFile::queueMutexLock() const {
    if (!queueMutex) {
        os_mutex_create(&queueMutex);
    }

    os_mutex_lock(queueMutex);
}

void SequentialFile::queueMutexUnlock() const {
    os_mutex_unlock(queueMutex);
}



// [static]
bool SequentialFile::createDirIfNecessary(const char *path) {
    struct stat statbuf;

    int result = stat(path, &statbuf);
    if (result == 0) {
        if ((statbuf.st_mode & S_IFDIR) != 0) {
            _log.info("%s exists and is a directory", path);
            return true;
        }

        _log.error("file in the way, deleting %s", path);
        unlink(path);
    }
    else {
        if (errno != ENOENT) {
            // Error other than file does not exist
            _log.error("stat filed errno=%d", errno);
            return false;
        }
    }

    // File does not exist (errno == 2)
    result = mkdir(path, 0777);
    if (result == 0) {
        _log.info("created dir %s", path);
        return true;
    }
    else {
        _log.error("mkdir failed errno=%d", errno);
        return false;
    }
}

// [static]
SequentialFile *SequentialFile::getInstance(const char *dirPath) {
    SequentialFile *result;
    
    result = SequentialFileShared::getInstance().findByPath(dirPath);
    if (!result) {
        result = new SequentialFile(dirPath);
    }
}


// [static]
SequentialFile *SequentialFile::getInstance(const char *dirPath, const char *ext) {
    SequentialFile *result;
    
    result = SequentialFileShared::getInstance().findByPath(dirPath);
    if (!result) {
        result = new SequentialFile(dirPath, ext);
    }
}



// [static]
String SequentialFile::getNameWithOptionalExt(const char *name, const char *ext) {
    String result = name;

    if (ext && *ext) {
        result += ".";
        result += ext;
    }

    return result;
}
