#include "bda_filesystem.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

#include <streams/file_stream.h>

struct RFILE {
    int handle;
    int64_t size;
};

void filestream_vfs_init(const struct retro_vfs_interface_info *vfs_info)
{
    (void)vfs_info;
}

RFILE *filestream_open(const char *path, unsigned mode, unsigned hints)
{
    RFILE *stream;
    int handle;
    int size;
    (void)hints;
    if (mode != RETRO_VFS_FILE_ACCESS_READ) {
        return 0;
    }
    handle = bda_fs_fopen_raw(path, "rb");
    if (!bda_fs_file_is_valid(handle)) {
        return 0;
    }
    size = bda_fs_seek_raw(handle, 0, BDA_SEEK_END);
    if (size < 0 || bda_fs_seek_raw(handle, 0, BDA_SEEK_SET) != 0) {
        (void)bda_fs_close_raw(handle);
        return 0;
    }
    stream = (RFILE *)malloc(sizeof(*stream));
    if (!stream) {
        (void)bda_fs_close_raw(handle);
        return 0;
    }
    stream->handle = handle;
    stream->size = size;
    return stream;
}

int64_t filestream_get_size(RFILE *stream)
{
    return stream ? stream->size : -1;
}

int64_t filestream_seek(RFILE *stream, int64_t offset, int seek_position)
{
    int whence;
    if (!stream || offset < INT_MIN || offset > INT_MAX) {
        return -1;
    }
    if (seek_position == RETRO_VFS_SEEK_POSITION_START) {
        whence = BDA_SEEK_SET;
    } else if (seek_position == RETRO_VFS_SEEK_POSITION_CURRENT) {
        whence = BDA_SEEK_CUR;
    } else if (seek_position == RETRO_VFS_SEEK_POSITION_END) {
        whence = BDA_SEEK_END;
    } else {
        return -1;
    }
    return bda_fs_seek_raw(stream->handle, (int)offset, whence);
}

int64_t filestream_read(RFILE *stream, void *buffer, int64_t length)
{
    if (!stream || length < 0 || length > INT_MAX) {
        return -1;
    }
    return bda_fs_read_raw(stream->handle, buffer, (bda_size_t)length);
}

int filestream_close(RFILE *stream)
{
    int result;
    if (!stream) {
        return -1;
    }
    result = bda_fs_close_raw(stream->handle);
    free(stream);
    return result;
}
