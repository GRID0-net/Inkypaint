/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#ifndef SNX_FS_TREE_H
#define SNX_FS_TREE_H

#include "common/filesystem/host.h"
#include <stdio.h>

#define FS_TREE_PATH_MAX 4096

typedef enum {
    FS_TREE_OK = 0,
    FS_TREE_NOTFOUND,
    FS_TREE_EXISTS,
    FS_TREE_NOTEMPTY,
    FS_TREE_INVALID,
    FS_TREE_TOOLONG,
    FS_TREE_IO,
    FS_TREE_NOMEM,
    FS_TREE_READONLY,
    FS_TREE_NOTDELETABLE,
    FS_TREE_NOTRENAMABLE
} FsTreeResult;

typedef struct FsNode {
    char* name;
    struct FsNode *parent, *children, *next;
    uint64_t size;
    int is_dir, is_virtual, readonly, attached;
    uint8_t* virtual_data;
    /* fd >= 0: a RomFS/ExeFS file opened at startup and mapped read-only and shared (plat_file_share) for the
       whole run, its contents at map (NULL when empty). Such nodes are readonly, so fd, map and size never change */
    int fd;
    const uint8_t* map;
    unsigned refs; /* open handles; attached nodes are additionally owned by the tree */
    char* host;    /* a graft's top (fs_tree_graft): the host folder its subtree is in, else NULL */
} FsNode;

typedef struct {
    char* base;   /* absolute game data directory, captured once at initialization */
    FsNode* root; /* the data directory; its child "root" represents the guest's / */
    PMutex lock;
} FsTree;

/* Initialization is the only operation that enumerates or probes host entries. A failed scan leaves no tree.
   Symlinks, junctions, hardlinked regular files, and non-file/non-directory entries are rejected during scanning.
   All other calls and direct node access require tree->lock to be held. Children are sorted by strcmp(name).
   Close open handles before destroy; retain/release keep removed entries alive while their handles exist. */
FsTreeResult fs_tree_init(FsTree* tree, const char* directory);
/* fs_tree_init with the data directory's own entries named in skipped (NULL-terminated, or NULL) left out of the index,
   as if they weren't there: folders read apart from it (a game's mods) */
FsTreeResult fs_tree_init_skipping(FsTree* tree, const char* directory, const char* const* skipped);
/* Startup only: name, a child of the data directory it doesn't have, indexed from another host directory, as
   fs_tree_init indexes its own and with the same rejections, and readonly. The game's RomFS kept in tmpfs */
FsTreeResult fs_tree_graft(FsTree* tree, const char* name, const char* directory);
/* Startup only: directory's entries laid over the data directory's, read-only, as another folder's: a file over the
   entry of its name, a folder's entries over the folder of its name. Of its top-level entries, only those named in
   whole or merged (each NULL-terminated, or NULL) are taken: one in whole replaces the data directory's and
   everything under it, one in merged is laid over it. Any other is left out, the data directory's own kept writable */
FsTreeResult fs_tree_overlay(FsTree* tree, const char* directory, const char* const* whole,
                             const char* const* merged);
/* Startup: merge one host directory into a named top-level directory. Writable imports
   keep their own host paths, so configuration updates persist in the owning mod. */
FsTreeResult fs_tree_overlay_directory(FsTree* tree, const char* name, const char* directory, int writable);
void fs_tree_destroy(FsTree* tree);
/* With no scope, accepts drive:/path, /path (under root/), or paths relative to the data directory.
   A scope makes paths relative to that directory; drive prefixes and traversal above scope are rejected. */
FsTreeResult fs_tree_lookup(FsTree* tree, FsNode* scope, const char* path, FsNode** out);
FsTreeResult fs_tree_path(FsTree* tree, const FsNode* node, char* out, size_t capacity);
/* Caller holds lock and enforces rights. Unbuffered, keeping separately opened handles coherent. */
FILE* fs_tree_open(FsTree* tree, FsNode* node, const char* mode);
/* Resize a real (non-virtual) file: truncate or grow zero-filled. Caller
   holds lock; node->size follows the host file. */
FsTreeResult fs_tree_resize(FsTree* tree, FsNode* node, uint64_t size);
/* Host modification time (UTC Unix milliseconds) of an indexed node. Caller
   holds lock. Virtual nodes have no host backing and return FS_TREE_INVALID. */
FsTreeResult fs_tree_mtime(FsTree* tree, const FsNode* node, uint64_t* unix_ms);
FsTreeResult fs_tree_mkdirs(FsTree* tree, const char* relative_path, FsNode** out);
FsTreeResult fs_tree_create(FsTree* tree, FsNode* scope, const char* path, int is_dir, uint64_t size, FsNode** out);
FsTreeResult fs_tree_remove(FsTree* tree, FsNode* scope, const char* path, int is_dir, int recursive, int keep);
FsTreeResult fs_tree_rename(FsTree* tree, FsNode* scope, const char* src, const char* dst, int is_dir);
/* Copies the contents. Missing parent directories are virtual, writable placeholders. Injected files are readonly. */
FsTreeResult fs_tree_add_virtual(FsTree* tree, const char* relative_path, const void* data, uint64_t size,
                                 FsNode** out);
/* Startup only: opens and maps every regular file under node (plat_file_share) and makes the subtree readonly, so no
   mapped file is ever truncated, replaced or renamed. A file that can't be mapped keeps fd -1 and is read on demand. */
FsTreeResult fs_tree_share(FsTree* tree, FsNode* node, unsigned* shared, unsigned* failed);
/* Startup only: the subtree readonly, as fs_tree_share makes it, with no file opened: each is read on demand */
FsTreeResult fs_tree_seal(FsTree* tree, FsNode* node);
void fs_tree_retain(FsNode* node);
void fs_tree_release(FsNode* node);

#endif
