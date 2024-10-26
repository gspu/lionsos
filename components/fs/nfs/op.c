/*
 * Copyright 2023, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <nfsc/libnfs.h>
#include <nfsc/libnfs-raw.h>

#include <lions/fs/protocol.h>

#include "nfs.h"
#include "util.h"
#include "fd.h"

#define MAX_CONCURRENT_OPS FS_QUEUE_CAPACITY
#define CLIENT_SHARE_SIZE 0x4000000

struct fs_queue *command_queue;
struct fs_queue *completion_queue;
char *client_share;

char path_buffer[2][FS_MAX_PATH_LENGTH + 1];

struct continuation {
    uint64_t request_id;
    uint64_t data[4];
    struct continuation *next_free;
};

struct continuation continuation_pool[MAX_CONCURRENT_OPS];
struct continuation *first_free_cont;

void handle_initialise(fs_cmd_t cmd);
void handle_deinitialise(fs_cmd_t cmd);
void handle_open(fs_cmd_t cmd);
void handle_stat(fs_cmd_t cmd);
void handle_fsize(fs_cmd_t cmd);
void handle_close(fs_cmd_t cmd);
void handle_read(fs_cmd_t cmd);
void handle_write(fs_cmd_t cmd);
void handle_rename(fs_cmd_t cmd);
void handle_unlink(fs_cmd_t cmd);
void handle_truncate(fs_cmd_t cmd);
void handle_mkdir(fs_cmd_t cmd);
void handle_rmdir(fs_cmd_t cmd);
void handle_opendir(fs_cmd_t cmd);
void handle_closedir(fs_cmd_t cmd);
void handle_readdir(fs_cmd_t cmd);
void handle_fsync(fs_cmd_t cmd);
void handle_seekdir(fs_cmd_t cmd);
void handle_telldir(fs_cmd_t cmd);
void handle_rewinddir(fs_cmd_t cmd);

static void (*const cmd_handler[FS_NUM_COMMANDS])(fs_cmd_t cmd) = {
    [FS_CMD_INITIALISE] = handle_initialise,
    [FS_CMD_DEINITIALISE] = handle_deinitialise,
    [FS_CMD_FILE_OPEN] = handle_open,
    [FS_CMD_FILE_CLOSE] = handle_close,
    [FS_CMD_STAT] = handle_stat,
    [FS_CMD_FILE_READ] = handle_read,
    [FS_CMD_FILE_WRITE] = handle_write,
    [FS_CMD_FILE_SIZE] = handle_fsize,
    [FS_CMD_RENAME] = handle_rename,
    [FS_CMD_FILE_REMOVE] = handle_unlink,
    [FS_CMD_FILE_TRUNCATE] = handle_truncate,
    [FS_CMD_DIR_CREATE] = handle_mkdir,
    [FS_CMD_DIR_REMOVE] = handle_rmdir,
    [FS_CMD_DIR_OPEN] = handle_opendir,
    [FS_CMD_DIR_CLOSE] = handle_closedir,
    [FS_CMD_FILE_SYNC] = handle_fsync,
    [FS_CMD_DIR_READ] = handle_readdir,
    [FS_CMD_DIR_SEEK] = handle_seekdir,
    [FS_CMD_DIR_TELL] = handle_telldir,
    [FS_CMD_DIR_REWIND] = handle_rewinddir,
};

void reply(fs_cmpl_t cmpl) {
    assert(fs_queue_length_producer(completion_queue) != FS_QUEUE_CAPACITY);
    fs_queue_idx_empty(completion_queue, 0)->cmpl = cmpl;
    fs_queue_publish_production(completion_queue, 1);
    microkit_notify(CLIENT_CHANNEL);
}

void process_commands(void) {
    uint64_t command_count = fs_queue_length_consumer(command_queue);
    uint64_t completion_space = FS_QUEUE_CAPACITY - fs_queue_length_producer(completion_queue);
    // don't dequeue a command if we have no space to enqueue its completion
    uint64_t to_consume = MIN(command_count, completion_space);
    for (uint64_t i = 0; i < to_consume; i++) {
        fs_cmd_t cmd = fs_queue_idx_filled(command_queue, i)->cmd;
        if (cmd.type >= FS_NUM_COMMANDS) {
            reply((fs_cmpl_t){ .id = cmd.id, .status = FS_STATUS_INVALID_COMMAND, .data = {0} });
            continue;
        }
        cmd_handler[cmd.type](cmd);
    }
    fs_queue_publish_consumption(command_queue, to_consume);
}

void continuation_pool_init(void) {
    first_free_cont = &continuation_pool[0];
    for (int i = 0; i + 1 < MAX_CONCURRENT_OPS; i++) {
        continuation_pool[i].next_free = &continuation_pool[i+1];
    }
}

struct continuation *continuation_alloc(void) {
    struct continuation *cont = first_free_cont;
    if (cont != NULL) {
        first_free_cont = cont->next_free;
        cont->next_free = NULL;
    }
    return cont;
}

void continuation_free(struct continuation *cont) {
    assert(cont >= continuation_pool);
    assert(cont < continuation_pool + MAX_CONCURRENT_OPS);
    assert(cont->next_free == NULL);
    cont->next_free = first_free_cont;
    first_free_cont = cont;
}

void *get_buffer(fs_buffer_t buf) {
    if (buf.offset >= CLIENT_SHARE_SIZE
        || buf.size > CLIENT_SHARE_SIZE - buf.offset
        || buf.size == 0) {
        return NULL;
    }
    return (void *)(client_share + buf.offset);
}

char *copy_path(int slot, fs_buffer_t buf) {
    assert(0 <= slot && slot < 2);

    char *client_buf = get_buffer(buf);
    if (client_buf == NULL || buf.size > FS_MAX_PATH_LENGTH) {
        return NULL;
    }

    memcpy(path_buffer[slot], client_buf, buf.size);
    path_buffer[slot][buf.size] = '\0';
    return path_buffer[slot];
}

static void mount_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    struct continuation *cont = private_data;
    fs_cmpl_t cmpl = { .id = cont->request_id, .status = FS_STATUS_SUCCESS, .data = {0} };

    if (status != 0) {
        dlog("failed to connect to nfs server (%d): %s", status, data);
        cmpl.status = FS_STATUS_ERROR;
        goto fail;
    }

    dlog("connected to nfs server");

fail:
    continuation_free(cont);
    reply(cmpl);
}

void handle_initialise(fs_cmd_t cmd) {
    uint64_t status = FS_STATUS_ERROR;

    dlog("received initialise command");

    if (nfs != NULL) {
        dlog("duplicate initialise command from client");
        goto fail_duplicate;
    }

    nfs = nfs_init_context();
    if (nfs == NULL) {
        dlog("failed to init nfs context");
        goto fail_init;
    }

    struct continuation *cont = continuation_alloc();
    assert(cont != NULL);
    cont->request_id = cmd.id;

    /* Infinite retries */
    nfs_set_autoreconnect(nfs, -1);

    int err = nfs_mount_async(nfs, NFS_SERVER, NFS_DIRECTORY, mount_cb, cont);
    if (err) {
        dlog("failed to enqueue command");
        goto fail_mount;
    }

    return;

fail_mount:
    continuation_free(cont);
fail_init:
fail_duplicate:
    reply((fs_cmpl_t){ .id = cmd.id, .status = status, .data = {0} });
}

void handle_deinitialise(fs_cmd_t cmd) {
}

static void stat64_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    struct continuation *cont = private_data;
    fs_cmpl_t cmpl = { .id = cont->request_id, .status = FS_STATUS_SUCCESS, .data = {0} };
    void *buf = (void *)cont->data[0];

    if (status == 0) {
        memcpy(buf, data, sizeof (fs_stat_t));
    } else {
        dlogp(status != -ENOENT, "failed to stat file (%d): %s", status, data);
        cmpl.status = FS_STATUS_ERROR;
    }
    continuation_free(cont);
    reply(cmpl);
}

void handle_stat(fs_cmd_t cmd) {
    uint64_t status = FS_STATUS_ERROR;
    fs_cmd_params_stat_t params = cmd.params.stat;

    char *path = copy_path(0, params.path);
    if (path == NULL) {
        dlog("invalid path buffer provided");
        status = FS_STATUS_INVALID_PATH;
        goto fail_buffer;
    }

    void *buf = get_buffer(params.buf);
    if (buf == NULL || params.buf.size < sizeof (fs_stat_t)) {
        dlog("invalid output buffer provided");
        status = FS_STATUS_INVALID_BUFFER;
        goto fail_buffer;
    }

    struct continuation *cont = continuation_alloc();
    assert(cont != NULL);
    cont->request_id = cmd.id;
    cont->data[0] = (uint64_t)buf;

    int err = nfs_stat64_async(nfs, path, stat64_cb, cont);
    if (err) {
        dlog("failed to enqueue command");
        goto fail_enqueue;
    }

    return;

fail_enqueue:
    continuation_free(cont);
fail_buffer:
    reply((fs_cmpl_t){ .id = cmd.id, .status = status, .data = {0} });
}

void fsize_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    struct continuation *cont = private_data;
    fs_cmpl_t cmpl = { .id = cont->request_id, .status = FS_STATUS_SUCCESS, .data = {0} };
    fd_t fd = cont->data[0];

    if (status != 0) {
        dlog("failed to fstat file (fd=%lu) (%d): %s", fd, status, data);
        cmpl.status = FS_STATUS_ERROR;
        goto fail;
    }

    struct nfs_stat_64 *stat_buf = data;
    cmpl.data.file_size.size = stat_buf->nfs_size;
fail:
    fd_end_op(fd);
    continuation_free(cont);
    reply(cmpl);
}

void handle_fsize(fs_cmd_t cmd) {
    uint64_t status = FS_STATUS_ERROR;
    fs_cmd_params_file_size_t params = cmd.params.file_size;

    struct nfsfh *file_handle = NULL;
    int err = fd_begin_op_file(params.fd, &file_handle);
    if (err) {
        dlog("invalid fd: %d", params.fd);
        status = FS_STATUS_INVALID_FD;
        goto fail_begin;
    }

    struct continuation *cont = continuation_alloc();
    assert(cont != NULL);
    cont->request_id = cmd.id;
    cont->data[0] = params.fd;

    err = nfs_fstat64_async(nfs, file_handle, fsize_cb, cont);
    if (err) {
        dlog("failed to enqueue command");
        goto fail_enqueue;
    }

    return;

fail_enqueue:
    continuation_free(cont);
    fd_end_op(params.fd);
fail_begin:
    reply((fs_cmpl_t){ .id = cmd.id, .status = status, .data = {0} });
}

void open_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    struct continuation *cont = private_data;
    fs_cmpl_t cmpl = { .id = cont->request_id, .status = FS_STATUS_SUCCESS, .data = {0} };
    struct nfsfh *file = data;
    fd_t fd = cont->data[0];

    if (status == 0) {
        fd_set_file(fd, file);
        cmpl.data.file_open.fd = fd;
    } else {
        dlog("failed to open file (%d): %s\n", status, data);
        fd_free(fd);
        cmpl.status = FS_STATUS_ERROR;
    }
    continuation_free(cont);
    reply(cmpl);
}

void handle_open(fs_cmd_t cmd) {
    uint64_t status = FS_STATUS_ERROR;
    struct fs_cmd_params_file_open params = cmd.params.file_open;

    char *path = copy_path(0, params.path);
    if (path == NULL) {
        dlog("invalid path buffer provided");
        status = FS_STATUS_INVALID_PATH;
        goto fail_buffer;
    }

    fd_t fd;
    int err = fd_alloc(&fd);
    if (err) {
        dlog("no free fds");
        status = FS_STATUS_ALLOCATION_ERROR;
        goto fail_alloc;
    }

    struct continuation *cont = continuation_alloc();
    assert(cont != NULL);
    cont->request_id = cmd.id;
    cont->data[0] = fd;

    int posix_flags = 0;
    if (params.flags & FS_OPEN_FLAGS_READ_ONLY) {
        posix_flags |= O_RDONLY;
    }
    if (params.flags & FS_OPEN_FLAGS_WRITE_ONLY) {
        posix_flags |= O_WRONLY;
    }
    if (params.flags & FS_OPEN_FLAGS_READ_WRITE) {
        posix_flags |= O_RDWR;
    }
    if (params.flags & FS_OPEN_FLAGS_CREATE) {
        posix_flags |= O_CREAT;
    }

    err = nfs_open2_async(nfs, path, posix_flags, 0644, open_cb, cont);
    if (err) {
        dlog("failed to enqueue command");
        goto fail_enqueue;
    }

    return;

fail_enqueue:
    continuation_free(cont);
    fd_free(fd);
fail_alloc:
fail_buffer:
    reply((fs_cmpl_t){ .id = cmd.id, .status = status, .data = {0} });
}

void close_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    struct continuation *cont = private_data;
    fs_cmpl_t cmpl = { .id = cont->request_id, .status = FS_STATUS_SUCCESS, .data = {0} };
    fd_t fd = cont->data[0];
    struct nfsfh *fh = (struct nfsfh *)cont->data[1];

    if (status == 0) {
        fd_free(fd);
    } else {
        dlog("failed to close file: %d (%s)", status, nfs_get_error(nfs));
        fd_set_file(fd, fh);
        cmpl.status = FS_STATUS_ERROR;
    }
    continuation_free(cont);
    reply(cmpl);
}

void handle_close(fs_cmd_t cmd) {
    uint64_t status = FS_STATUS_ERROR;
    fs_cmd_params_file_close_t params = cmd.params.file_close;

    struct nfsfh *file_handle = NULL;
    int err = fd_begin_op_file(params.fd, &file_handle);
    if (err) {
        dlog("invalid fd: %d", params.fd);
        status = FS_STATUS_INVALID_FD;
        goto fail_begin;
    }
    fd_end_op(params.fd);

    err = fd_unset(params.fd);
    if (err) {
        dlog("fd has outstanding operations\n");
        status = FS_STATUS_OUTSTANDING_OPERATIONS;
        goto fail_unset;
    }

    struct continuation *cont = continuation_alloc();
    assert(cont != NULL);
    cont->request_id = cmd.id;
    cont->data[0] = params.fd;
    cont->data[1] = (uint64_t)file_handle;

    err = nfs_close_async(nfs, file_handle, close_cb, cont);
    if (err) {
        dlog("failed to enqueue command");
        goto fail_enqueue;
    }

    return;

fail_enqueue:
    continuation_free(cont);
    fd_set_file(params.fd, file_handle);
fail_unset:
fail_begin:
    reply((fs_cmpl_t){ .id = cmd.id, .status = status, .data = {0} });
}

void read_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    struct continuation *cont = private_data;
    fs_cmpl_t cmpl = { .id = cont->request_id, .status = FS_STATUS_SUCCESS, .data = {0} };
    fd_t fd = cont->data[0];

    if (status >= 0) {
        cmpl.data.file_read.len_read = status;
    } else {
        dlog("failed to read file: %d (%s)", status, data);
        cmpl.status = FS_STATUS_ERROR;
    }

    fd_end_op(fd);
    continuation_free(cont);
    reply(cmpl);
}

void handle_read(fs_cmd_t cmd) {
    uint64_t status = FS_STATUS_ERROR;
    fs_cmd_params_file_read_t params = cmd.params.file_read;

    char *buf = get_buffer(params.buf);
    if (buf == NULL) {
        dlog("invalid output buffer provided");
        status = FS_STATUS_INVALID_BUFFER;
        goto fail_buffer;
    }

    struct nfsfh *file_handle = NULL;
    int err = fd_begin_op_file(params.fd, &file_handle);
    if (err) {
        dlog("invalid fd: %d", params.fd);
        status = FS_STATUS_INVALID_FD;
        goto fail_begin;
    }

    struct continuation *cont = continuation_alloc();
    assert(cont != NULL);
    cont->request_id = cmd.id;
    cont->data[0] = params.fd;
    cont->data[1] = (uint64_t)buf;

    err = nfs_pread_async(nfs, file_handle, buf, params.buf.size, params.offset, read_cb, cont);
    if (err) {
        dlog("failed to enqueue command");
        goto fail_enqueue;
    }

    return;

fail_enqueue:
    continuation_free(cont);
    fd_end_op(params.fd);
fail_begin:
fail_buffer:
    reply((fs_cmpl_t){ .id = cmd.id, .status = status, .data = {0} });
}

void write_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    struct continuation *cont = private_data;
    fs_cmpl_t cmpl = { .id = cont->request_id, .status = FS_STATUS_SUCCESS, .data = {0} };
    fd_t fd = cont->data[0];

    if (status >= 0) {
        cmpl.data.file_write.len_written = status;
    } else {
        dlog("failed to write to file: %d (%s)", status, data);
        cmpl.status = FS_STATUS_ERROR;
    }

    fd_end_op(fd);
    continuation_free(cont);
    reply(cmpl);
}

void handle_write(fs_cmd_t cmd) {
    uint64_t status = FS_STATUS_ERROR;
    fs_cmd_params_file_write_t params = cmd.params.file_write;

    char *buf = get_buffer(params.buf);
    if (buf == NULL) {
        dlog("invalid output buffer provided");
        status = FS_STATUS_INVALID_BUFFER;
        goto fail_buffer;
    }

    struct nfsfh *file_handle = NULL;
    int err = fd_begin_op_file(params.fd, &file_handle);
    if (err) {
        dlog("invalid fd: %d", params.fd);
        status = FS_STATUS_INVALID_FD;
        goto fail_begin;
    }

    struct continuation *cont = continuation_alloc();
    assert(cont != NULL);
    cont->request_id = cmd.id;
    cont->data[0] = params.fd;

    err = nfs_pwrite_async(nfs, file_handle, buf, params.buf.size, params.offset, write_cb, cont);
    if (err) {
        dlog("failed to enqueue command");
        goto fail_enqueue;
    }

    return;

fail_enqueue:
    continuation_free(cont);
    fd_end_op(params.fd);
fail_begin:
fail_buffer:
    reply((fs_cmpl_t){ .id = cmd.id, .status = status, .data = {0} });
}

void rename_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    struct continuation *cont = private_data;
    fs_cmpl_t cmpl = { .id = cont->request_id, .status = FS_STATUS_SUCCESS, .data = {0} };
    if (status != 0) {
        dlog("failed to write to file: %d (%s)", status, data);
        cmpl.status = FS_STATUS_ERROR;
    }
    continuation_free(cont);
    reply(cmpl);
}

void handle_rename(fs_cmd_t cmd) {
    uint64_t status = FS_STATUS_ERROR;
    fs_cmd_params_rename_t params = cmd.params.rename;

    char *old_path = copy_path(0, params.old_path);
    char *new_path = copy_path(1, params.new_path);
    if (old_path == NULL || new_path == NULL) {
        dlog("invalid path buffer provided");
        status = FS_STATUS_INVALID_PATH;
        goto fail_buffer;
    }

    struct continuation *cont = continuation_alloc();
    assert(cont != NULL);
    cont->request_id = cmd.id;
    int err = nfs_rename_async(nfs, old_path, new_path, rename_cb, cont);
    if (err) {
        dlog("failed to enqueue command");
        goto fail_enqueue;
    }

    return;

fail_enqueue:
    continuation_free(cont);
fail_buffer:
    reply((fs_cmpl_t){ .id = cmd.id, .status = status, .data = {0} });
}

void unlink_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    struct continuation *cont = private_data;
    fs_cmpl_t cmpl = { .id = cont->request_id, .status = FS_STATUS_SUCCESS, .data = {0} };
    if (status != 0) {
        dlog("failed to unlink file");
        cmpl.status = FS_STATUS_ERROR;
    }
    continuation_free(cont);
    reply(cmpl);
}

void handle_unlink(fs_cmd_t cmd) {
    uint64_t status = FS_STATUS_ERROR;
    fs_cmd_params_file_remove_t params = cmd.params.file_remove;

    char *path = copy_path(0, params.path);
    if (path == NULL) {
        dlog("invalid path buffer provided");
        status = FS_STATUS_INVALID_PATH;
        goto fail_buffer;
    }

    struct continuation *cont = continuation_alloc();
    assert(cont != NULL);
    cont->request_id = cmd.id;
    int err = nfs_unlink_async(nfs, path, unlink_cb, cont);
    if (err) {
        dlog("failed to enqueue command");
        goto fail_enqueue;
    }

    return;

fail_enqueue:
    continuation_free(cont);
fail_buffer:
    reply((fs_cmpl_t){ .id = cmd.id, .status = status, .data = {0} });
}

void fsync_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    struct continuation *cont = private_data;
    fs_cmpl_t cmpl = { .id = cont->request_id, .status = FS_STATUS_SUCCESS, .data = {0} };
    fd_t fd = cont->data[0];
    if (status != 0) {
        dlog("fsync failed: %d (%s)", status, data);
        cmpl.status = FS_STATUS_ERROR;
    }
    fd_end_op(fd);
    continuation_free(cont);
    reply(cmpl);
}

void handle_fsync(fs_cmd_t cmd) {
    uint64_t status = FS_STATUS_ERROR;
    fs_cmd_params_file_sync_t params = cmd.params.file_sync;

    struct nfsfh *file_handle = NULL;
    int err = fd_begin_op_file(params.fd, &file_handle);
    if (err) {
        dlog("invalid fd (%d)", params.fd);
        status = FS_STATUS_INVALID_FD;
        goto fail_begin;
    }

    struct continuation *cont = continuation_alloc();
    assert(cont != NULL);
    cont->request_id = cmd.id;
    cont->data[0] = params.fd;

    err = nfs_fsync_async(nfs, file_handle, fsync_cb, cont);
    if (err) {
        dlog("failed to enqueue command");
        goto fail_enqueue;
    }

    return;

fail_enqueue:
    continuation_free(cont);
    fd_end_op(params.fd);
fail_begin:
    reply((fs_cmpl_t){ .id = cmd.id, .status = status, .data = {0} });
}

void truncate_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    struct continuation *cont = private_data;
    fs_cmpl_t cmpl = { .id = cont->request_id, .status = FS_STATUS_SUCCESS, .data = {0} };
    fd_t fd = cont->data[0];
    if (status != 0) {
        dlog("ftruncate failed: %d (%s)", status, data);
        cmpl.status = FS_STATUS_ERROR;
    }
    fd_end_op(fd);
    continuation_free(cont);
    reply(cmpl);
}

void handle_truncate(fs_cmd_t cmd) {
    uint64_t status = FS_STATUS_ERROR;
    fs_cmd_params_file_truncate_t params = cmd.params.file_truncate;

    struct nfsfh *file_handle = NULL;
    int err = fd_begin_op_file(params.fd, &file_handle);
    if (err) {
        dlog("invalid fd");
        status = FS_STATUS_INVALID_FD;
        goto fail_begin;
    }

    struct continuation *cont = continuation_alloc();
    assert(cont != NULL);
    cont->request_id = cmd.id;
    cont->data[0] = params.fd;

    err = nfs_ftruncate_async(nfs, file_handle, params.length, truncate_cb, cont);
    if (err) {
        dlog("failed to enqueue command");
        goto fail_enqueue;
    }

    return;

fail_enqueue:
    continuation_free(cont);
    fd_end_op(params.fd);
fail_begin:
    reply((fs_cmpl_t){ .id = cmd.id, .status = status, .data = {0} });
}

void mkdir_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    struct continuation *cont = private_data;
    fs_cmpl_t cmpl = { .id = cont->request_id, .status = FS_STATUS_SUCCESS, .data = {0} };
    if (status != 0) {
        dlog("failed to write to file: %d (%s)", status, data);
        cmpl.status = FS_STATUS_ERROR;
    }
    continuation_free(cont);
    reply(cmpl);
}

void handle_mkdir(fs_cmd_t cmd) {
    uint64_t status = FS_STATUS_ERROR;
    fs_cmd_params_dir_create_t params = cmd.params.dir_create;

    char *path = copy_path(0, params.path);
    if (path == NULL) {
        dlog("invalid path buffer provided");
        status = FS_STATUS_INVALID_PATH;
        goto fail_buffer;
    }

    struct continuation *cont = continuation_alloc();
    assert(cont != NULL);
    cont->request_id = cmd.id;

    int err = nfs_mkdir_async(nfs, path, mkdir_cb, cont);
    if (err) {
        dlog("failed to enqueue command");
        goto fail_enqueue;
    }

    return;

fail_enqueue:
    continuation_free(cont);
fail_buffer:
    reply((fs_cmpl_t){ .id = cmd.id, .status = status, .data = {0} });
}

void rmdir_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    struct continuation *cont = continuation_alloc();
    fs_cmpl_t cmpl = { .id = cont->request_id, .status = FS_STATUS_SUCCESS, .data = {0} };
    if (status != 0) {
        dlog("failed to write to file: %d (%s)", status, data);
        cmpl.status = FS_STATUS_ERROR;
    }
    continuation_free(cont);
    reply(cmpl);
}

void handle_rmdir(fs_cmd_t cmd) {
    uint64_t status = FS_STATUS_ERROR;
    fs_cmd_params_dir_remove_t params = cmd.params.dir_remove;

    char *path = copy_path(0, params.path);
    if (path == NULL) {
        dlog("invalid path buffer provided");
        status = FS_STATUS_INVALID_PATH;
        goto fail_buffer;
    }

    struct continuation *cont = continuation_alloc();
    assert(cont != NULL);
    cont->request_id = cmd.id;

    int err = nfs_rmdir_async(nfs, path, rmdir_cb, cont);
    if (err) {
        dlog("failed to enqueue command");
        goto fail_enqueue;
    }

    return;

fail_enqueue:
    continuation_free(cont);
fail_buffer:
    reply((fs_cmpl_t){ .id = cmd.id, .status = status, .data = {0} });
}

void opendir_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    struct continuation *cont = private_data;
    fs_cmpl_t cmpl = { .id = cont->request_id, .status = FS_STATUS_SUCCESS, .data = {0} };

    fd_t fd = cont->data[0];
    struct nfsdir *dir = data;

    if (status == 0) {
        fd_set_dir(fd, dir);
        cmpl.data.dir_open.fd = fd;
    } else {
        dlog("failed to open directory: %d (%s)", status, data);
        cmpl.status = FS_STATUS_ERROR;
        fd_free(fd);
    }

    continuation_free(cont);
    reply(cmpl);
}

void handle_opendir(fs_cmd_t cmd) {
    fs_cmd_params_dir_open_t params = cmd.params.dir_open;
    fs_cmpl_t cmpl = { .id = cmd.id, .status = FS_STATUS_ERROR, .data = {0} };

    char *path = copy_path(0, params.path);
    if (path == NULL) {
        dlog("invalid path buffer provided");
        cmpl.status = FS_STATUS_INVALID_PATH;
        goto fail_buffer;
    }

    fd_t fd;
    int err = fd_alloc(&fd);
    if (err) {
        dlog("no free fds");
        cmpl.status = FS_STATUS_ALLOCATION_ERROR;
        goto fail_alloc;
    }

    struct continuation *cont = continuation_alloc();
    assert(cont != NULL);
    cont->request_id = cmd.id;
    cont->data[0] = fd;

    err = nfs_opendir_async(nfs, path, opendir_cb, cont);
    if (err) {
        dlog("failed to enqueue command");
        cmpl.status = FS_STATUS_ERROR;
        goto fail_enqueue;
    }

    return;

fail_enqueue:
    continuation_free(cont);
    fd_free(fd);
fail_alloc:
fail_buffer:
    reply(cmpl);
}

void handle_closedir(fs_cmd_t cmd) {
    fs_cmd_params_dir_close_t params = cmd.params.dir_close;
    fs_cmpl_t cmpl = { .id = cmd.id, .status = FS_STATUS_SUCCESS, .data = {0} };

    struct nfsdir *dir_handle = NULL;
    int err = fd_begin_op_dir(params.fd, &dir_handle);
    if (err) {
        dlog("invalid fd (%d)", params.fd);
        cmpl.status = FS_STATUS_INVALID_FD;
        goto fail;
    }
    fd_end_op(params.fd);

    err = fd_unset(params.fd);
    if (err) {
        dlog("trying to close fd with outstanding operations");
        cmpl.status = FS_STATUS_OUTSTANDING_OPERATIONS;
        goto fail;
    }

    nfs_closedir(nfs, dir_handle);
    fd_free(params.fd);
fail:
    reply(cmpl);
}

void handle_readdir(fs_cmd_t cmd) {
    fs_cmd_params_dir_read_t params = cmd.params.dir_read;
    fs_cmpl_t cmpl = { .id = cmd.id, .status = FS_STATUS_SUCCESS, .data = {0} };

    char *buf = get_buffer(params.buf);
    if (buf == NULL || params.buf.size < FS_MAX_NAME_LENGTH) {
        dlog("invalid output buffer provided");
        cmpl.status = FS_STATUS_INVALID_BUFFER;
        goto fail_buffer;
    }

    struct nfsdir *dir_handle = NULL;
    int status = fd_begin_op_dir(params.fd, &dir_handle);
    if (status) {
        dlog("invalid fd (%d)", params.fd);
        cmpl.status = FS_STATUS_INVALID_FD;
        goto fail_begin;
    }

    struct nfsdirent *dirent = nfs_readdir(nfs, dir_handle);
    if (dirent == NULL) {
        cmpl.status = FS_STATUS_END_OF_DIRECTORY;
        goto end_of_dir;
    }

    uint64_t name_len = strlen(dirent->name);
    assert(name_len <= FS_MAX_NAME_LENGTH);
    memcpy(buf, dirent->name, name_len);
    cmpl.data.dir_read.path_len = name_len;

end_of_dir:
    fd_end_op(params.fd);
fail_begin:
fail_buffer:
    reply(cmpl);
}

void handle_seekdir(fs_cmd_t cmd) {
    fs_cmd_params_dir_seek_t params = cmd.params.dir_seek;
    fs_cmpl_t cmpl = { .id = cmd.id, .status = FS_STATUS_SUCCESS, .data = {0} };

    struct nfsdir *dir_handle = NULL;
    int err = fd_begin_op_dir(params.fd, &dir_handle);
    if (err) {
        dlog("invalid fd (%d)", params.fd);
        cmpl.status = FS_STATUS_INVALID_FD;
        goto fail;
    }
    nfs_seekdir(nfs, dir_handle, params.loc);
    fd_end_op(params.fd);

fail:
    reply(cmpl);
}

void handle_telldir(fs_cmd_t cmd) {
    fs_cmd_params_dir_tell_t params = cmd.params.dir_tell;
    fs_cmpl_t cmpl = { .id = cmd.id, .status = FS_STATUS_SUCCESS, .data = {0} };

    struct nfsdir *dir_handle = NULL;
    int err = fd_begin_op_dir(params.fd, &dir_handle);
    if (err) {
        dlog("invalid fd (%d)", params.fd);
        cmpl.status = FS_STATUS_INVALID_FD;
        goto fail;
    }
    cmpl.data.dir_tell.location = nfs_telldir(nfs, dir_handle);
    fd_end_op(params.fd);

fail:
    reply(cmpl);
}

void handle_rewinddir(fs_cmd_t cmd) {
    fs_cmd_params_dir_rewind_t params = cmd.params.dir_rewind;
    fs_cmpl_t cmpl = { .id = cmd.id, .status = FS_STATUS_SUCCESS, .data = {0} };

    struct nfsdir *dir_handle = NULL;
    int err = fd_begin_op_dir(params.fd, &dir_handle);
    if (err) {
        dlog("invalid fd (%d)", params.fd);
        cmpl.status = FS_STATUS_INVALID_FD;
        goto fail;
    }
    nfs_rewinddir(nfs, dir_handle);
    fd_end_op(params.fd);

fail:
    reply(cmpl);
}
