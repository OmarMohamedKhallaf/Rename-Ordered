#include <errno.h>
#include <stdio.h>
#include <utime.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>

#define EPRINTF(...) fprintf(stderr, __VA_ARGS__)

static char *get_directory_path(int argc, char const * const *argv);

static char const *basename(char const *path);

static long count_dir_files(char const *dir_path);

static char const *get_file_extension(char const *file_path);

int main(int argc, char const * const *argv) {
    DIR *dir;
    size_t alloc_size;
    char *dir_path, new_filename_fmt[30];    // filename_fmt = "%s/%0_ld%s" where "_" is a length of a long number, "%s/" dir_path and "%s" file extension
    char *old_filename, *new_filename;
    char tmp_dir_path[] = "./.renamed-files";
    long num_files, zero_pad, cnt;
    struct dirent *dir_ent;
    struct stat ent_stat;
    dir_path = get_directory_path(argc, argv);
    if ((num_files = count_dir_files(dir_path)) == -1) {
        EPRINTF("%s - %s | failed to count files in directory '%s': %s\n",
                __FILE__, __func__, dir_path, strerror(errno));
        goto FREE_DIR_PATH_ERROR;
    }
    for (zero_pad = 1; num_files / 10 != 0; ++zero_pad)
        num_files /= 10;
    sprintf(new_filename_fmt, "%s%ld%s", "%s/%0", zero_pad, "ld%s");
    // allocate space of the file names
    alloc_size = strlen(dir_path);
    alloc_size = alloc_size > strlen(tmp_dir_path) ? alloc_size : strlen(tmp_dir_path);
    if ((old_filename = malloc(alloc_size + 1 + 255 + 1)) == NULL) {
        EPRINTF("%s - %s | failed to allocate space: %s\n",
                __FILE__, __func__, strerror(errno));
        goto FREE_DIR_PATH_ERROR;
    }
    if ((new_filename = malloc(alloc_size + 1 + 255 + 1)) == NULL) {
        EPRINTF("%s - %s | failed to allocate space for new_filename: %s\n",
                __FILE__, __func__, strerror(errno));
        goto FREE_OLD_PATH_ERROR;
    }
    // create tmp directory to save files with their new names to avoid overwriting
    // if file name already exists in the target directory
    if (mkdir(tmp_dir_path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) != 0) {
        EPRINTF("%s - %s | failed to create tmp file: %s\n",
                __FILE__, __func__, strerror(errno));
        goto FREE_NEW_PATH_ERROR;
    }
    // loop through each entity in the directory and rename them in tmp directory
    if ((dir = opendir(dir_path)) == NULL) {
        EPRINTF("%s - %s | failed to open directory '%s': %s\n",
                __FILE__, __func__, dir_path, strerror(errno));
        goto FREE_NEW_PATH_ERROR;
    }
    cnt = 0;
    errno = 0;
    while ((dir_ent = readdir(dir)) != NULL) {
        if (dir_ent->d_name[0] == '.')
            continue;
        sprintf(old_filename, "%s/%s", dir_path, dir_ent->d_name);
        stat(old_filename, &ent_stat);      // can't fail because we did the same when counting
        if (! S_ISREG(ent_stat.st_mode))
            continue;
        sprintf(new_filename, new_filename_fmt, tmp_dir_path, cnt, get_file_extension(dir_ent->d_name));
        if (rename(old_filename, new_filename) != 0)
            fprintf(stderr, "failed to rename '%s' to '%s': %s\n", old_filename, new_filename, strerror(errno));
        ++cnt;
        errno = 0;
    }
    if (errno != 0) {
        fprintf(stderr, "failed to read directory '%s': %s\n", dir_path, strerror(errno));
        goto CLOSE_DIR_ERROR;
    }
    closedir(dir);
    // get the file back from the tmp directory
    if ((dir = opendir(tmp_dir_path)) == NULL) {
        fprintf(stderr, "failed to open '%s': %s\n", dir_path, strerror(errno));
        goto FREE_NEW_PATH_ERROR;
    }
    errno = 0;
    while ((dir_ent = readdir(dir)) != NULL) {
        if (dir_ent->d_name[0] == '.')
            continue;
        sprintf(old_filename, "%s/%s", tmp_dir_path, dir_ent->d_name);
        sprintf(new_filename, "%s/%s", dir_path, dir_ent->d_name);
        if (rename(old_filename, new_filename) != 0)
            fprintf(stderr, "failed to rename '%s' to '%s': %s\n", old_filename, new_filename, strerror(errno));
        if (utime(new_filename, NULL) != 0)
            fprintf(stderr, "failed to change timestamps for '%s': %s\n", new_filename, strerror(errno));
    }
    if (rmdir(tmp_dir_path) != 0)
        fprintf(stderr, "failed to remove tmp directory '%s': %s\n", tmp_dir_path, strerror(errno));
    free(new_filename);
    free(old_filename);
    if (dir_path != argv[1])
        free(dir_path);
    return EXIT_SUCCESS;
CLOSE_DIR_ERROR:
    closedir(dir);
FREE_NEW_PATH_ERROR:
    free(new_filename);
FREE_OLD_PATH_ERROR:
    free(old_filename);
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
