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
    /**
     * @brief Constructor
     * 
     * Often you will create this object as a global variable, though you can create it
     * with new. You normally do not allocate one as a stack variable as you want it
     * to maintain the queue in the object for efficiency.
     */
    SequentialFile();

    /**
     * @brief Constructor that takes a directory to use for the queue directory
     * 
     * This is equivalent to using the default constructor and calling withDirPath().
     */
    SequentialFile(const char *dirPath);

    /**
     * @brief Constructor that takes a directory to use for the queue directory and a filename extension
     * 
     * This is equivalent to using the default constructor and calling withDirPath() and withFilenameExtension().
     */
    SequentialFile(const char *dirPath, const char *ext);

    /**
     * @brief Destructor
     */
    virtual ~SequentialFile();

    /**
     * @brief Sets the directory to use for the queue directory. Required!
     * 
     * @param dirPath The path to use for the queue directory. 
     * 
     * Typically you either put the directory in /usr to make sure it doesn't conflict with
     * system usage. Thus you would pass in "/usr/myqueue" to use the directory "myqueue".
     * 
     * Note that this will only create one level of directories, so make sure you've first
     * created the parent directory (or directories) if you are using deeper nesting.
     * 
     * You can pass in a directory name that ends with a slash or not. Internally it's stored
     * with any trailing slash removed.
     */
    SequentialFile &withDirPath(const char *dirPath);

    /** 
     * @brief Gets the directory name of the queue directory
     * 
     * It will not end with a / regardless of what you pass in.
     */
    const char *getDirPath() const { return dirPath; };

    /**
     * @brief The filename to number pattern for sprintf/sscanf
     * 
     * The default is %08d.
     */
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
     * Passing false for allExtensions is more efficient if you are not using multiple files pere
     * queue element because it only has to unlink the one file. If you pass true, then the directory
     * needs to be iterated.
     */
    void removeFileNum(int fileNum, bool allExtensions);

    /**
     * @brief Remove all files on disk in the queue directory
     * 
     * @param removeDir Also remove the queue directory itself if true.
     * 
     * This removes all files, including those that do not match the filename pattern,
     * and all extensions.
     */
    void removeAll(bool removeDir);

    /**
     * @brief Gets the length of the queue (number of elements in the queue)
     */
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

    /**
     * @brief Gets an instance of SequentialFile for dirPath, or creates one if one does not yet exist
     * 
     * @param dirPath The path to use for the queue directory. 
     * 
     * Use of this is optional, however it's a good way to manage a queue shared by two different 
     * modules, a writer to the queue and a reader from the queue. 
     */
    static SequentialFile *getInstance(const char *dirPath);

    /**
     * @brief Gets an instance of SequentialFile for dirPath, or creates one if one does not yet exist
     * 
     * @param dirPath The path to use for the queue directory. 
     * 
     * @param ext The filename extension. This is not used for comparison, but is used for creating
     * a new instance. Assumption is that a queue directory will only use one extension, if it's
     * using an extension.
     */
    static SequentialFile *getInstance(const char *dirPath, const char *ext);


protected:
    /**
     * @brief Can be subclassed to examine a file before adding to the queue on scanDir
     * 
     * @return true to add to the queue or false to ignore
     * 
     * This class always returns true, but you can subclass this method and examine the
     * file to determine if you want to add it to the queue. For example, you could
     * determine if the file is valid, and discard partially written files this way.
     * 
     * Use getPathForFileNum() or getNameForFileNum() to get a path to the file. It
     * is safe to unlink() a file from this function.
     */
    virtual bool preScanAddHook(int fileNum) { return true; };

    /**
     * @brief Locks the mutex around accessing the queue
     * 
     * Note: The mutex is not a recursive mutex, so don't lock within a lock.
     */
    void queueMutexLock() const;

    /**
     * @brief Unlocks the mutex around accessing the queue
     */
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
