#include <features.h>
#if defined(__GLIBC__) && __GLIBC__ <= 2
#if defined(__GLIBC_MINOR__)
#if __GLIBC_MINOR__ <= 10
#define _ATFILE_SOURCE
#else
#define _POSIX_C_SOURCE 200809L
#endif
#else
#define _ATFILE_SOURCE
#endif
#endif
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <utime.h>
#include <dirent.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>

#define EPRINTF(...) fprintf(stderr, __VA_ARGS__)

static char *get_directory_path(int argc, char const * const *argv);

static char const *basename(char const *path);

static long count_dir_files(char const *dir_path);

static char const *get_file_extension(char const *file_path);

static char *mktmpdir(const char *dir_path);

static int loop_through_dir(int (*user_func)(char const *, long, va_list), char const *dir_path, ...);

static int rename_in_tmp(char const *filename, long file_cnt, va_list list);

static int rename_from_tmp(char const *filename, long file_cnt, va_list list);

int main(int argc, char const * const *argv) {
    long num_files, zero_pad;
    char *dir_path, *old_filename, *new_filename, *tmp_dir_path;
    char new_filename_fmt[30];    // filename_fmt = "%s/%0_ld%s" where "_" is a length of a long number, "%s/" dir_path and "%s" file extension
    struct timespec cur_time;
    dir_path = get_directory_path(argc, argv);
    if ((num_files = count_dir_files(dir_path)) == -1) {
        EPRINTF("%s - %s | failed to count files in directory '%s': %s\n",
                __FILE__, __func__, dir_path, strerror(errno));
        goto FREE_DIR_PATH_ERROR;
    }
    for (zero_pad = 1; num_files / 10 != 0; ++zero_pad)
        num_files /= 10;
    sprintf(new_filename_fmt, "%s%ld%s", "%s/%0", zero_pad, "ld%s");
    // create tmp directory to save files with their new names to avoid overwriting
    // if file name already exists in the target directory
    if ((tmp_dir_path = mktmpdir(dir_path)) == NULL) {
        EPRINTF("%s - %s | failed to create tmp directory: %s\n",
                __FILE__, __func__, strerror(errno));
        goto FREE_DIR_PATH_ERROR;
    }
    // allocate space of the file names strlen(tmp_dir_path) > strlen(dir_path)
    if ((old_filename = malloc(strlen(tmp_dir_path) + 1 + 255 + 1)) == NULL) {
        EPRINTF("%s - %s | failed to allocate space: %s\n",
                __FILE__, __func__, strerror(errno));
        goto FREE_DIR_PATH_ERROR;
    }
    if ((new_filename = malloc(strlen(tmp_dir_path) + 1 + 255 + 1)) == NULL) {
        EPRINTF("%s - %s | failed to allocate space for new_filename: %s\n",
                __FILE__, __func__, strerror(errno));
        goto FREE_OLD_PATH_ERROR;
    }
    if (clock_gettime(CLOCK_REALTIME, &cur_time) != 0) {
        EPRINTF("%s - %s | failed to get current clock time: %s\n",
                __FILE__, __func__, strerror(errno));
        goto FREE_NEW_PATH_ERROR;
    }
    if (loop_through_dir(rename_in_tmp, dir_path, dir_path, tmp_dir_path,
            old_filename, new_filename, new_filename_fmt) != 0) {
        EPRINTF("%s - %s | failed to apply user function on directory '%s': %s\n",
                __FILE__, __func__, dir_path, strerror(errno));
        goto FREE_NEW_PATH_ERROR;
    }
    if (loop_through_dir(rename_from_tmp, tmp_dir_path, dir_path, tmp_dir_path,
                old_filename, new_filename, &cur_time) != 0) {
        EPRINTF("%s - %s | failed to apply user function on directory '%s': %s\n",
                __FILE__, __func__, dir_path, strerror(errno));
        goto FREE_NEW_PATH_ERROR;
    }
    free(new_filename);
    free(old_filename);
    if (rmdir(tmp_dir_path) != 0) {
        EPRINTF("%s - %s | failed to remove directory '%s': %s\n",
                __FILE__, __func__, tmp_dir_path, strerror(errno));
        goto FREE_TMP_DIR_PATH_ERROR;
    }
    free(tmp_dir_path);
    free(dir_path);
    return EXIT_SUCCESS;
FREE_NEW_PATH_ERROR:
    free(new_filename);
FREE_OLD_PATH_ERROR:
    free(old_filename);
   if (rmdir(tmp_dir_path) != 0)
        EPRINTF("%s - %s | failed to remove directory '%s': %s\n",
                __FILE__, __func__, tmp_dir_path, strerror(errno));
FREE_TMP_DIR_PATH_ERROR:
    free(tmp_dir_path);
FREE_DIR_PATH_ERROR:
    free(dir_path);
    return EXIT_FAILURE;
}

static char const *basename(char const *path) {
    char const *l_slash = path;
    while (*path != '\0')
#if defined(__WINDOWS__)
        if (*path++ == '\\')
            l_slash = path;
#else
    if (*path++ == '/')
        l_slash = path;
#endif
    return l_slash;
}

/*
 * counts the number of files only in a directory skipping hidden ones
 *
 * @param dir_path the directory path to read its files.
 * @returns the number of files in the given directory.
 */
static long count_dir_files(char const *dir_path) {
    DIR *dir;
    long cnt;
    char *full_path;
    struct dirent *ent;
    struct stat ent_stat;
    // allocated space = dir_path + '/' + max_file_name_length + '\0'
    if ((full_path = malloc(strlen(dir_path) + 1 + 255 + 1)) == NULL) {
        EPRINTF("%s - %s | failed to allocate space: %s\n",
                __FILE__, __func__,  strerror(errno));
        return -1L;
    }
    if ((dir = opendir(dir_path)) == NULL) {
        EPRINTF("%s - %s | failed to open directory '%s': %s\n",
                __FILE__, __func__, dir_path, strerror(errno));
        goto FREE_FULL_PATH_ERROR;
    }
    cnt = 0;
    errno = 0;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.')
            continue;
        sprintf(full_path, "%s/%s", dir_path, ent->d_name);  // cannot fail
        if (stat(full_path, &ent_stat) != 0) {
            EPRINTF("%s - %s | failed to get '%s' stats': %s\n",
                    __FILE__, __func__, full_path, strerror(errno));
            goto CLOSE_DIR_ERROR;
        }
        if (S_ISREG(ent_stat.st_mode))
            ++cnt;
        errno = 0;
    }
    if (errno != 0) {
        EPRINTF("%s - %s | failed to read directory '%s': %s\n",
                __FILE__, __func__,  dir_path, strerror(errno));
        goto CLOSE_DIR_ERROR;
    }
    closedir(dir);
    free(full_path);
    return cnt;
CLOSE_DIR_ERROR:
    closedir(dir);
FREE_FULL_PATH_ERROR:
    free(full_path);
    return -1L;
}

/*
 * Returns the file extension with the dot.
 * @param file_path the file path
 * @retuns the file extension
 */
static char const *get_file_extension(char const *file_path) {
    ssize_t len = strlen(file_path) - 1;
    while (len >= 0 && file_path[len] != '.')
        --len;
    if (len < 0)
        return "";
    else
        return &file_path[len];
}

/*
 * Gets the directory needed to rename its files. It may be given as argument
 * to the program or use the current directory as default.
 * @param argc the number of arguments given to the program in main function
 * @param argv the strings representing the program arguments
 * @returns a string the represents the path of the directory to operate on
 *          and it must be freed.
 */
static char *get_directory_path(int argc, char const * const *argv) {
    size_t str_len;
    char *dir_path;
    if (argc == 2) {
        str_len = strlen(argv[1]) + 1;
        if ((dir_path = malloc(sizeof(char) * str_len)) == NULL) {
            EPRINTF("%s - %s | failed to allocate memory: %s\n",
                    __FILE__, __func__, strerror(errno));
        }
        memcpy(dir_path, argv[1], str_len);
    }else if (argc > 2) {
        EPRINTF("usage: %s [dirctory]\n"
                "\trenames all files in a [directory] or current directory in numerical order\n",
                basename(argv[0]));
        exit(-1);
    } else {
        if ((dir_path = getcwd(NULL, 0)) == NULL) {
            EPRINTF("%s - %s | failed to open current directory: %s\n",
                    __FILE__, __func__,  strerror(errno));
            exit(-1);
        }
    }
    return dir_path;
}

/*
 * Creates a temporary directory in under the target directory that
 * we rename its files
 * @param dir_path the path of directory to rename its files
 * @returns a string representation of the created folder
 */
static char *mktmpdir(const char *dir_path) {
    long rand_num;
    char *tmp_dir_path;
    srand((unsigned) time(NULL));
    rand_num = rand();
    // allocate size is strlen(dir_path) + '/' + '.' + 'rand_num' + '\0'
    if ((tmp_dir_path = malloc(strlen(dir_path) + 1 + 1 + 20 + 1)) == NULL) {
        EPRINTF("%s - %s | failed to allocate memory: %s\n",
                __FILE__, __func__, strerror(errno));
        goto RETURN_ERROR;
    }
    sprintf(tmp_dir_path, "%s/.%ld", dir_path, rand_num);
    if (mkdir(tmp_dir_path, S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH) != 0) {
        EPRINTF("%s - %s | failed to create folder '%s': %s\n",
                __FILE__, __func__, tmp_dir_path, strerror(errno));
        goto FREE_TMP_DIR_PATH_ERROR;
    }
    return tmp_dir_path;
FREE_TMP_DIR_PATH_ERROR:
    free(tmp_dir_path);
RETURN_ERROR:
    return NULL;
}

/*
 * Loops through a directory and executes a given user function on files only in
 * that directory. The user function takes the file name as its first argument
 * ,the number of the file in no specific order and the other arguments are
 * varargs defined by the caller of this function.
 *
 * @param user_func the function to execute on each file in the directory
 * @param dir_path the directory to operate on
 * @param ... the arguments the user want to send to his own custom function
 * @returns 0 on success and -1 of failure
 */
static int loop_through_dir(int (*user_func)(char const *, long, va_list), char const *dir_path, ...) {
    DIR *dir;
    long cnt = 0;
    va_list list;
    char *full_path;
    struct dirent *dir_ent;
    struct stat ent_stat;
    if ((dir = opendir(dir_path)) == NULL) {
        EPRINTF("%s - %s | failed to open directory '%s': %s\n",
                __FILE__, __func__, dir_path, strerror(errno));
        goto RETURN_ERROR;
    }
    if ((full_path = malloc(strlen(dir_path) + 1 + 255 + 1)) == NULL) {
        EPRINTF("%s - %s | failed to allocate memory: %s\n",
                __FILE__, __func__, strerror(errno));
        goto CLOSE_DIR_ERROR;
    }
    errno = 0;
    while ((dir_ent = readdir(dir)) != NULL) {
        if (dir_ent->d_name[0] == '.')
            continue;
        sprintf(full_path, "%s/%s", dir_path, dir_ent->d_name);
        if (stat(full_path, &ent_stat) != 0) {
            EPRINTF("%s - %s | failed to get stats about '%s': %s\n",
                    __FILE__, __func__, full_path, strerror(errno));
            goto FREE_FULL_PATH;
        }
        if (! S_ISREG(ent_stat.st_mode))
            continue;
        va_start(list, dir_path);
        if ((*user_func)(dir_ent->d_name, cnt, list) != 0) {
            EPRINTF("%s - %s | failed to excute user function on file '%s'\n",
                    __FILE__, __func__, dir_ent->d_name);
            goto END_LIST_ERROR;
        }
        ++cnt;
        va_end(list);
        errno = 0;
    }
    if (errno != 0) {
        EPRINTF("%s - %s | failed to read directory '%s': %s\n",
                __FILE__, __func__, dir_path, strerror(errno));
        goto CLOSE_DIR_ERROR;
    }
    free(full_path);
    closedir(dir);
    return 0;
END_LIST_ERROR:
    va_end(list);
FREE_FULL_PATH:
    free(full_path);
CLOSE_DIR_ERROR:
    closedir(dir);
RETURN_ERROR:
    return -1;
}

static int rename_in_tmp(char const *filename, long file_cnt, va_list list) {
    char *dir_path, *tmp_dir_path, *new_filename_fmt, *new_filename, *old_filename;
    dir_path = va_arg(list, char *);
    tmp_dir_path = va_arg(list, char *);
    old_filename = va_arg(list, char *);
    new_filename = va_arg(list, char *);
    new_filename_fmt = va_arg(list, char *);
    sprintf(old_filename, "%s/%s", dir_path, filename);
    sprintf(new_filename, new_filename_fmt, tmp_dir_path, file_cnt, get_file_extension(filename));
    if (rename(old_filename, new_filename) != 0)
        EPRINTF("%s - %s | failed to rename '%s' => '%s': %s\n",
               __FILE__, __func__,  old_filename, new_filename, strerror(errno));
    return 0;
}

static int rename_from_tmp(char const *filename, long file_cnt, va_list list) {
    struct timespec *cur_time, mod_time[2];
    char *dir_path, *tmp_dir_path, *new_filename, *old_filename;
    dir_path = va_arg(list, char *);
    tmp_dir_path = va_arg(list, char *);
    old_filename = va_arg(list, char *);
    new_filename = va_arg(list, char *);
    cur_time = va_arg(list, struct timespec *);
    cur_time->tv_nsec += file_cnt;
    mod_time[0] = mod_time[1] = *cur_time;
    sprintf(old_filename, "%s/%s", tmp_dir_path, filename);
    sprintf(new_filename, "%s/%s", dir_path, filename);
    if (rename(old_filename, new_filename) != 0)
        EPRINTF("%s - %s | failed to rename '%s' => '%s': %s\n",
               __FILE__, __func__,  old_filename, new_filename, strerror(errno));
    else if (utimensat(AT_FDCWD, new_filename, mod_time, 0) != 0)
        EPRINTF("%s - %s | failed to update atime and mtime of file '%s': %s\n",
                __FILE__, __func__, new_filename, strerror(errno));
    return 0;
}
