#ifndef __SEQUENTIALFILERK_H
#define __SEQUENTIALFILERK_H

#include "Particle.h"

#include <deque>

/**
 * @brief Class for maintaining a directory of files as a queue with unique filenames
 * 
 * 
 */
class SequentialFile {
public:
    SequentialFile();
    virtual ~SequentialFile();

    SequentialFile &withDirPath(const char *dirPath);

    const char *getDirPath() const { return dirPath; };

    SequentialFile &withPattern(const char *pattern) { this->pattern = pattern; return *this; };

    const char *getPattern() const { return pattern; };
    
    /**
     * @brief Filename extension for queue files. (Default: no extension)
     * 
     * @param ext The filename extension. 
     */
    SequentialFile &withFilenameExtension(const char *ext) { this->filenameExtension = ext; return *this; };

    /**
     * @brief Scans the queue directory for files. Typically called during setup().
     */
    bool scanDir(void);

    /**
     * @brief Reserve a file number you will use to write data to
     * 
     * Use getPathForFileNum() to get the pathname to the file. Reservations are in-RAM only
     * so if the device reboots before you write the file, the reservation will be lost.
     */
    int reserveFile(void);

    /**
     * @brief Adds a previously reserved file to the queue
     * 
     * Use reserveFile() to get the next file number, addFileToQueue() to add it to the queue
     * and getFileFromQueue() to get an item from the queue.
     */
    void addFileToQueue(int fileNum);

    /**
     * @brief Gets a file from the queue
     * 
     * @return 0 if there are no items in the queue, or a fileNum for an item in the queue.
     * 
     * Use getPathForFileNum() to convert the number into a pathname for use with open().
     * 
     * The queue is stored in RAM, so if the device reboots before you delete the file it
     * will reappear in the queue when scanDir is called.
     */
    int getFileFromQueue(void);

    /**
     * @brief Uses pattern to create a filename given a fileNum
     * 
     * @param fileNum A file number, typically from reserveFile() or getFileFromQueue()
     * 
     * @param overrideExt If non-null, use this extension instead of the configured
     * filename extension.
     * 
     * The overrideExt is used when you create multiple files per queue entry fileNum,
     * for example a data file and a .sha1 hash for the file. Or other metadata.
     */
    String getNameForFileNum(int fileNum, const char *overrideExt = NULL);

    /**
     * @brief Gets a full pathname based on dirName and getNameForFileNum
     * 
     * @param fileNum A file number, typically from reserveFile() or getFileFromQueue()
     * 
     * @param overrideExt If non-null, use this extension instead of the configured
     * filename extension.
     * 
     * The overrideExt is used when you create multiple files per queue entry fileNum,
     * for example a data file and a .sha1 hash for the file. Or other metadata.
     */
    String getPathForFileNum(int fileNum, const char *overrideExt = NULL);

    /**
     * @brief Remove fileNum from the flash file system
     *
     * @param fileNum A file number, typically from reserveFile() or getFileFromQueue()
     * 
     * @param allExtensions If true, all files with that number regardless of extension are removed.
     * 
     */
    void removeFileNum(int fileNum, bool allExtensions);

    /**
     * @brief
     */
    void removeAll(bool removeDir);

    int getQueueLen() const;

    /**
     * @brief This class is not copyable
     */
    SequentialFile(const SequentialFile&) = delete;

    /**
     * @brief This class is not copyable
     */
    SequentialFile& operator=(const SequentialFile&) = delete;

    /**
     * @brief Creates the directory path if it does not exist
     * 
     * Note: This only creates the last element of the path if it does not
     * exist. If you use directories nested more than one level deep,
     * you'll need to call this separately to create parent directories!
     */
    static bool createDirIfNecessary(const char *path);

    /**
     * @brief Combines name and ext into a filename with an optional filename extension
     * 
     * @param name Filename (required)
     * 
     * @param ext Filename extension (optional). If you pass is NULL or an 
     * empty string, the dot and extension are not appended.
     * 
     * Only adds the . when ext is non-null and non-empty.
     */
    static String getNameWithOptionalExt(const char *name, const char *ext);

protected:
    virtual bool preScanAddHook(const char *name) { return true; };

    void queueMutexLock() const;

    void queueMutexUnlock() const;

protected:
    /**
     * @brief The path to the queue directory. Must be configured, using the top level directory is not allowed
     */
    String dirPath;
    String pattern = "%08d";
    String filenameExtension = "";
    bool scanDirCompleted = false;
    int lastFileNum = 0;
    mutable os_mutex_t queueMutex = 0;
    std::deque<int> queue;
};

#endif // __SEQUENTIALFILERK_H
