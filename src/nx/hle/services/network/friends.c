/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors
   The GRID0+ commands ported from GRID0-Citrosis's src/core/hle/service/friend/friend.cpp:
     SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project (GPL-2.0-or-later)
     SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project (GPL-2.0-or-later)
   friend, the console's friend list: with GRID0+ the account's friends, which the server keeps (grid0.c), and
   none without. A console's friends sysmodule keeps a copy of the list synced in the background: grid0.c's cache is
   that copy, so a command never waits on the network, but for the first list of a session (Splatoon 3 asks once, at
   startup, and keeps what it got) and a friend's picture, which the game doesn't ask for again either.

   Splatoon 3 takes its friend list from NPLN, by network service account id, then asks this service about each id:
   the record (FriendImpl) carries the presence, whose same-application flag and key-value storage are how the game
   finds a friend it can join */
#include "nx/hle/services/network/network.h"
#include "nx/hle/services/network/grid0.h"
#include "nx/hle/hle.h"
#include "hle/services/network/friends.swipc.inc"

#define FRIEND_NO_NOTIFICATIONS RC(124, 15) /* account's NoNotifications, which friends answers too */
#define FRIEND_RECORD           0x200       /* nn::friends::detail::FriendImpl */
#define FRIEND_PRESENCE         0xE0        /* nn::friends::detail::UserPresenceImpl */
#define FRIEND_FIRST_WAIT_MS    2000
#define FRIEND_IMAGE_WAIT_MS    1500

/* ============================================================ IServiceCreator */
static void service_destroy(SvcObj* o) { kobj_unref(&((FriendService*)o->data)->done->o); }
static int nn_friends_detail_ipc_IServiceCreator_CreateFriendService(SvcObj* self, SvcObj** service) {
    SvcObj* o = nn_friends_detail_ipc_IFriendService_new_data(g_hle.friends.services, g_hle.friends.service_next,
                                                              "friend services");
    FriendService* f = o->data;
    (void)self;
    /* signaled from the start too: an asynchronous call's session asks for it first, then makes its one call */
    f->done = owned_event("friend completion");
    f->done->signaled = 1;
    o->destroy = service_destroy;
    *service = o;
    R_SUCCEED();
}
static int nn_friends_detail_ipc_IServiceCreator_CreateNotificationService(SvcObj* self, const nn_account_Uid* uid,
                                                                           SvcObj** service) {
    (void)self, (void)uid;
    *service = nn_friends_detail_ipc_INotificationService_new();
    R_SUCCEED();
}
static int nn_friends_detail_ipc_IServiceCreator_CreateDaemonSuspendSessionService(SvcObj* self, SvcObj** service) {
    (void)self;
    *service = nn_friends_detail_ipc_IDaemonSuspendSessionService_new();
    R_SUCCEED();
}

/* ============================================================ IFriendService */
/* every command's end: the SDK's asynchronous calls wait for the event before they read a result, and never
   signalling it left Splatoon 3 waiting forever, never asking for the list (Citrosis, after citron-nextendo) */
static int done(SvcObj* self, int rc) {
    kevent_signal(((FriendService*)self->data)->done);
    return rc;
}
static int nn_friends_detail_ipc_IFriendService_GetCompletionEvent(SvcObj* self, u32* event) {
    *event = ev_handle(((FriendService*)self->data)->done);
    R_SUCCEED();
}

/* a friend's record: its uid (the NSA id and a console's high half), the NSA id, the nickname, the presence (the uid
   again, the last time online, the status, whether it plays this game, the game's key-value storage), the favourite
   flag and whether the record is valid */
static void record(const Grid0Friend* f, u8* r) {
    memset(r, 0, FRIEND_RECORD);
    snx_st_le64(r + 0x00, f->nsa_id);
    snx_st_le64(r + 0x08, 0x1100000000000000ull);
    snx_st_le64(r + 0x10, f->nsa_id);
    memcpy(r + 0x18, f->nickname, strlen(f->nickname) < 0x20 ? strlen(f->nickname) : 0x20);
    memcpy(r + 0x40, r, 0x10);
    snx_st_le64(r + 0x50, 0x7FFFFFFFFFFFFFFFull);
    snx_st_le32(r + 0x58, f->status);
    /* a friend in this game is one the game asks about joining; one in another isn't */
    r[0x5C] = f->status == 2 && f->title_id && f->title_id == g_hle.kernel.title_id;
    memcpy(r + 0x60, f->field, f->field_size);
    r[0x120] = f->favorite;
    r[0x128] = 1;
}
/* the list, the first of the session waited for. Returns the count, into the heap's list (the caller frees it) */
static u32 friends_now(Grid0Friend** list, int wait_ms) {
    *list = (Grid0Friend*)malloc(GRID0_FRIENDS_MAX * sizeof **list);
    return *list ? grid0_friends(*list, GRID0_FRIENDS_MAX, wait_ms) : 0;
}

static int nn_friends_detail_ipc_IFriendService_GetFriendListIds(SvcObj* self, i32 offset, const nn_account_Uid* uid,
                                                                 const nn_friends_detail_ipc_SizedFriendFilter* filter,
                                                                 u64 pid, IpcBuf ids, i32* count) {
    Grid0Friend* list;
    u32 n = friends_now(&list, FRIEND_FIRST_WAIT_MS), i, out = 0;
    (void)uid, (void)filter, (void)pid;
    for (i = offset > 0 ? (u32)offset : 0; i < n && (out + 1) * 8 <= ids.size; i++, out++)
        snx_st_le64(GP(ids.addr + out * 8), list[i].nsa_id);
    free(list);
    *count = (i32)out;
    LOG("friend: GetFriendListIds(offset %d): %u\n", offset, out);
    return done(self, 0);
}
static int nn_friends_detail_ipc_IFriendService_GetFriendList(SvcObj* self, i32 offset, const nn_account_Uid* uid,
                                                              const nn_friends_detail_ipc_SizedFriendFilter* filter,
                                                              u64 pid, IpcBuf friends, i32* count) {
    Grid0Friend* list;
    u32 n = friends_now(&list, FRIEND_FIRST_WAIT_MS), i, out = 0;
    (void)uid, (void)filter, (void)pid;
    for (i = offset > 0 ? (u32)offset : 0; i < n && (out + 1) * FRIEND_RECORD <= friends.size; i++, out++)
        record(&list[i], GP(friends.addr + out * FRIEND_RECORD));
    free(list);
    *count = (i32)out;
    LOG("friend: GetFriendList(offset %d): %u\n", offset, out);
    return done(self, 0);
}
/* a record for each id asked about, in its order: the caller pairs them by position, so an id that isn't a friend
   stays an invalid record (all zeros) rather than being left out */
static int nn_friends_detail_ipc_IFriendService_UpdateFriendInfo(SvcObj* self, const nn_account_Uid* uid, u64 pid,
                                                                 IpcBuf ids, IpcBuf friends) {
    Grid0Friend* list;
    u32 n = friends_now(&list, 0), i, j, wanted = (u32)(ids.size / 8);
    (void)uid, (void)pid;
    for (i = 0; i < wanted && (i + 1) * FRIEND_RECORD <= friends.size; i++) {
        u64 id = snx_ld_le64(GP(ids.addr + i * 8));
        u8* r = GP(friends.addr + i * FRIEND_RECORD);
        memset(r, 0, FRIEND_RECORD);
        for (j = 0; j < n; j++)
            if (list[j].nsa_id == id) {
                record(&list[j], r);
                break;
            }
    }
    free(list);
    return done(self, 0);
}
static int nn_friends_detail_ipc_IFriendService_GetFriendProfileImage(SvcObj* self, const nn_account_Uid* uid,
                                                                      nn_account_NetworkServiceAccountId id,
                                                                      IpcBuf image, u32* size) {
    (void)uid;
    *size = image.addr ? grid0_friend_image(id, GP(image.addr), (u32)image.size, FRIEND_IMAGE_WAIT_MS) : 0;
    return done(self, 0);
}
static int nn_friends_detail_ipc_IFriendService_CheckFriendListAvailability(SvcObj* self, const nn_account_Uid* uid,
                                                                            u8* available) {
    (void)uid;
    *available = (u8)grid0_on();
    return done(self, 0);
}
static int nn_friends_detail_ipc_IFriendService_EnsureFriendListAvailable(SvcObj* self, const nn_account_Uid* uid) {
    (void)uid;
    return done(self, 0);
}
static int nn_friends_detail_ipc_IFriendService_DeclareOpenOnlinePlaySession(SvcObj* self, const nn_account_Uid* uid) {
    (void)uid;
    return done(self, 0);
}
static int nn_friends_detail_ipc_IFriendService_DeclareCloseOnlinePlaySession(SvcObj* self, const nn_account_Uid* uid) {
    (void)uid;
    return done(self, 0);
}
/* the status and the game's key-value storage are what a friend needs to see this player online and join them */
static int nn_friends_detail_ipc_IFriendService_UpdateUserPresence(SvcObj* self, const nn_account_Uid* uid, u64 pid,
                                                                   IpcBuf presence) {
    (void)uid, (void)pid;
    if (presence.addr && presence.size >= FRIEND_PRESENCE) {
        const u8* p = GP(presence.addr);
        grid0_presence(snx_ld_le32(p + 0x18), g_hle.kernel.title_id, p + 0x20, FRIEND_PRESENCE - 0x20);
    }
    return done(self, 0);
}
/* the players met: kept nowhere, as a console's play history is nothing the game reads back */
static int nn_friends_detail_ipc_IFriendService_AddPlayHistory(SvcObj* self, const nn_account_Uid* uid, u64 pid,
                                                               IpcBuf key, IpcBuf name, IpcBuf my_name) {
    (void)uid, (void)pid, (void)key, (void)name, (void)my_name;
    return done(self, 0);
}
static int nn_friends_detail_ipc_IFriendService_GetFriendCount(SvcObj* self, const nn_account_Uid* uid,
                                                               const nn_friends_detail_ipc_SizedFriendFilter* filter,
                                                               u64 pid, i32* count) {
    Grid0Friend* list;
    (void)uid, (void)filter, (void)pid;
    *count = (i32)friends_now(&list, 0);
    free(list);
    return done(self, 0);
}

/* ============================================================ INotificationService */
static int nn_friends_detail_ipc_INotificationService_GetEvent(SvcObj* self, u32* event) {
    (void)self;
    return new_event(event, "friend notification", 0);
}
static int nn_friends_detail_ipc_INotificationService_Pop(SvcObj* self,
                                                          nn_friends_detail_ipc_SizedNotificationInfo* info) {
    (void)self, (void)info;
    R_THROW(FRIEND_NO_NOTIFICATIONS);
}

SvcObj* new_friend(void) { return nn_friends_detail_ipc_IServiceCreator_new(); }
