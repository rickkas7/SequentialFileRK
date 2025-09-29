#include "SequentialFileRK.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>


static Logger _log("app.seqfile");

namespace {

// FIXME: MAX_PATH in libc is 4096, which is wrong
const size_t MAX_PATH_LEN = 255;

using ProcessCb = std::function<int(const char*, int)>;

// Workaround for https://github.com/littlefs-project/littlefs/commit/a5d614fbfbf19b8605e08c28a53bc69ea3179a3e
int findLeafEntry(char* pathBuf, size_t bufSize, size_t pathLen, bool* found = nullptr, ProcessCb process = ProcessCb()) {
    DIR* dir = opendir(pathBuf);
    CHECK_TRUE(dir, SYSTEM_ERROR_FILESYSTEM);
    NAMED_SCOPE_GUARD(closeDirGuard, {
        int r = closedir(dir);
        if (r < 0) {
            LOG(ERROR, "Failed to close directory handle: %d", errno);
        }
    });

    dirent* ent = nullptr;
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_type == DT_DIR && (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)) {
            continue;
        }
        pathBuf[pathLen++] = '/'; // May overwrite the term. null
        size_t n = strlcpy(pathBuf + pathLen, ent->d_name, bufSize - pathLen);
        if (n >= bufSize - pathLen) {
            return SYSTEM_ERROR_PATH_TOO_LONG;
        }
        closeDirGuard.dismiss();
        CHECK_TRUE(closedir(dir) == 0, SYSTEM_ERROR_FILESYSTEM);
        if (ent->d_type == DT_DIR) {
            CHECK(findLeafEntry(pathBuf, bufSize, pathLen + n));
        }
        if (!process) {
            break;
        } else {
            if (process(pathBuf, ent->d_type) < 0) {
                break;
            }
        }
    }
    CHECK_TRUE(ent, SYSTEM_ERROR_NOT_FOUND);
    if (found) {
        *found = (bool)ent;
    }
    return 0;
}
} // anonymous


SequentialFile::SequentialFile() {

}

SequentialFile::~SequentialFile() {

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
        if (sscanf(ent->d_name, pattern, &fileNum) == 1) {
            if (filenameExtension.length() == 0 || String(ent->d_name).endsWith(filenameExtension)) {
                // 
                if (preScanAddHook(ent->d_name)) {
                    if (fileNum > lastFileNum) {
                        lastFileNum = fileNum;
                    }
                    _log.trace("adding to queue %d %s", fileNum, ent->d_name);

                    queueMutexLock();
                    queue.push_back(fileNum); 
                    queueMutexUnlock();
                }
            }
        }
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
 
int SequentialFile::getFileFromQueue(bool remove) {
    int fileNum = 0;

    if (!scanDirCompleted) {
        scanDir();
    }

    queueMutexLock();
    if (!queue.empty()) {
        fileNum = queue.front();
        if (remove) {
            queue.pop_front();
        }
    }
    queueMutexUnlock();

    if (fileNum != 0) {
        _log.trace("getFileFromQueue returned %d", fileNum);
    }

    return fileNum;
}

int SequentialFile::removeSecondFileInQueue() {
    int fileNum = 0;

    if (!scanDirCompleted) {
        scanDir();
    }

    queueMutexLock();
    auto iter = queue.begin();
    if (iter != queue.end()) {
        if (++iter != queue.end()) {
            fileNum = *iter;
            queue.erase(iter);
        }
    }
    queueMutexUnlock();

    if (fileNum != 0) {
        _log.trace("removeSecondFileInQueue returned %d", fileNum);
    }

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
        char pathBuf[MAX_PATH_LEN + 1] = {};
        size_t pathLen = strlcpy(pathBuf, dirPath.c_str(), sizeof(pathBuf));
        if (pathLen >= sizeof(pathBuf)) {
            _log.trace("path %s over MAX_PATH_LEN", dirPath.c_str());
            return;
        }
        findLeafEntry(pathBuf, sizeof(pathBuf), pathLen, nullptr, [fileNum, this](const char* path, int type) {
            if (type == DT_REG) {
                int curFileNum;
                if (sscanf(basename(path), pattern.c_str(), &curFileNum) == 1) {
                    if (curFileNum == fileNum) {
                        unlink(path);
                        _log.trace("removed %s", path);
                    }
                }
            }
            return 0;
        });
    } else {
        String path = getPathForFileNum(fileNum); 
        unlink(path);
        _log.trace("removed %s", path.c_str());
    }
}

void SequentialFile::removeAll(bool removeDir) {
    char pathBuf[MAX_PATH_LEN + 1] = {};
    size_t pathLen = strlcpy(pathBuf, dirPath.c_str(), sizeof(pathBuf));
    if (pathLen >= sizeof(pathBuf)) {
        _log.trace("path %s over MAX_PATH_LEN", dirPath.c_str());
        return;
    }
    for (;;) {
        bool found = false;
        findLeafEntry(pathBuf, sizeof(pathBuf), pathLen, &found);
        if (!found) {
            break;
        }
        int r = unlink(pathBuf);
        if (r < 0) {
            _log.trace("failed to remove %s: %d", pathBuf, errno);
        }
        pathBuf[pathLen] = '\0'; // Reset to the base path
    }
    queueMutexLock();

    queue.clear();

    if (removeDir) {
        rmdir(dirPath);
    }
    lastFileNum = 0;
    scanDirCompleted = false;

    queueMutexUnlock();
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
String SequentialFile::getNameWithOptionalExt(const char *name, const char *ext) {
    String result = name;

    if (ext && *ext) {
        result += ".";
        result += ext;
    }

    return result;
}
