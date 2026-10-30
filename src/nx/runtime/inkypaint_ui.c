/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
/* Splatoon 3 11.3.0's menus as nn::ui2d draws them, and the mouse on them (inkypaint_ui.h).

   The SDK's ui2d is linked into main, its classes known by their RTTI: Layout::Draw (vtable slot 12, main+0xa94344)
   draws the root pane at layout+0x18, its layout space 1920x1080 around the screen's center, y up. A pane
   (nn::ui2d::Pane, from its constructor at main+0xaa33e0 and FindPaneByName at main+0xaa48c0):
     +0x00 vtable    +0x08 link in the parent's list {prev, next}    +0x18 parent
     +0x20 children's list head {prev, next}, each child linked at child+8
     +0x30 translate (3 floats)  +0x3c rotate (3)  +0x48 scale (2)  +0x50 size (2)
     +0x58 flag (bit 0 visible)  +0x59 alpha  +0x5a global alpha  +0x5b base position (bits 0-1 x: center, left,
     right; bits 2-3 y: center, top, bottom)  +0x70 global matrix, 3 rows of 4 floats  +0xb0 name (24 + 1)
   A text box: its UTF-16 text at +0xd8, its length at +0x112 (TextBox::SetString, main+0xab0500).

   A menu is up while the squid cursor is drawn: a layout of its own (RootPane / L_Cursor_00 / N_Dummy_00), the squid
   on the button the menu's cursor is on. A button is a Parts pane (L_...) with an N_Cursor pane among its children,
   where the squid sits when the cursor is on it: the panes say which buttons are on the screen and where, and the
   squid which one is the cursor's. The game's button objects (Init main+0x48d84e0, SetState main+0x48d84b0) don't:
   most of a menu's are made before the menu opens and moving the cursor calls neither, and their touch mode (the
   TouchOn animations) is fed from the touch screen in two screens only. So a click here is the game's own
   navigation: the directional buttons pressed toward the button under the pointer, one step at a time, each step
   checked against where the squid went, then A */
#include "nx/runtime/inkypaint_ui.h"
#include "nx/runtime/inkypaint_framerate.h"
#include "nx/runtime/driver/inkypaint.h"
#include "nx/runtime/loader.h"
#include "nx/runtime/guest_jit.h"
#include "nx/runtime/options.h"
#include "nx/runtime/platform/log.h"
#include "nx/runtime/platform/plat.h"
#include "nx/hle/hle.h"
#include "nx/hle/kernel/kernel.h"
#include "nx/memory/mem.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define MAIN(off)   (0x8007000ull + (off))
#define LAYOUT_DRAW MAIN(0xa94344)
#define CENSUS_NS   40000000ull /* a frame and some: every layout the frame draws */
#define MAX_DEPTH   40
#define MAX_PANES   6000
#define ROOTS       32
#define ROOT_BUTTONS 192
#define SHOWN_NS    150000000ull  /* a layout drawn this recently is on the screen */
#define LOOK_NS     1000000000ull /* the layouts' buttons are looked for while the squid was drawn this recently */
#define SQUID_NEAR  150.0f        /* layout pixels: the squid bobs, and moves between buttons for a few frames */

/* the Npad's buttons the navigation presses */
#define PAD_A     (1ull << 0)
#define PAD_L     (1ull << 6)
#define PAD_R     (1ull << 7)
#define PAD_LEFT  (1ull << 12)
#define PAD_UP    (1ull << 13)
#define PAD_RIGHT (1ull << 14)
#define PAD_DOWN  (1ull << 15)
#define PRESS_NS  50000000ull /* a step's press: a frame of the game's at 30 frames a second, and some */
#define SETTLE_NS 70000000ull /* after it, for the game to move its cursor */
#define MAX_STEPS 16

typedef struct {
    u64 pane; /* identity only: the input thread never dereferences a saved pane */
    float box[4], center[2]; /* draw-thread snapshot of hit bounds and cursor anchor */
    char name[25];
    int tab; /* zero for a normal button, tab index + 1 otherwise */
} Button;
static GuestFn draw_original;
static int enabled;
static PMutex g_lock = PMUTEX_INIT;
static struct {
    struct {
        u64 root, ns; /* a layout drawn lately: its root pane, when */
        unsigned count;
        int active_tab; /* known X-menu content index, -1 when unavailable/transitioning */
        int lobby_list; /* rooms/friends browser, including its empty/loading state */
        int interaction; /* a visible PauseText A-button action guide */
        Button buttons[ROOT_BUTTONS]; /* the buttons shown in it then */
    } roots[ROOTS];
    u64 squid, squid_ns; /* cursor identity and the last visible draw */
    float squid_center[2];
    u64 focus;           /* the cursor's button (its pane) as cursor_button last found it */
    int menu;            /* the menu was up at the last pointer call */
} g_ui;
static struct {
    int requested; /* F9: the next draw starts a census */
    u64 until;     /* a census runs while plat_ns() is before this */
    unsigned number, layouts, panes;
    FILE* out;
} g_census;
static struct {
    u64 target;    /* the button (pane) being walked to, 0 for none */
    int confirm;   /* A once there: a click, not a hover */
    int steps;
    int failed[2]; /* an axis a press didn't move the cursor on: 0 across, 1 up and down */
    int axis;
    int tab_from; /* content index observed before a shoulder-button pulse */
    u64 press, until; /* the buttons held until then, and after them nothing until settle */
    u64 settle;
    u64 from;    /* the cursor's button when the step was pressed */
    u64 hovered; /* the button the pointer was last over */
} g_nav;

void inkypaint_ui_census_request(void) {
    __atomic_store_n(&g_census.requested, 1, __ATOMIC_RELEASE);
}

/* ---- guest memory, read as the game writes it from its own threads: every pointer is checked before use */
static float f32_at(u64 a) {
    u32 bits = gr32(a);
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}
static u8 gr8(u64 a) {
    return *(const u8*)GP(a);
}
static int guest_ok(u64 a, u64 size) {
    return a >= 0x1000 && !(a & 7) && mem_mapped_run(a, size) >= size;
}
static int pane_ok(u64 pane) {
    return guest_ok(pane, 0xe0);
}
/* a text box's text as UTF-8, at most n - 1 bytes */
static void text_of(u64 pane, char* s, size_t n) {
    u64 buf;
    unsigned len, i;
    size_t o = 0;
    s[0] = 0;
    if (!guest_ok(pane, 0x118)) return;
    buf = gr64(pane + 0xd8);
    len = gr16(pane + 0x112);
    if (!buf || len > 256 || buf & 1 || mem_mapped_run(buf, len * 2u) < len * 2u) return;
    for (i = 0; i < len && o + 4 < n; i++) {
        unsigned c = gr16(buf + i * 2u);
        if (c == '\n') c = '|';
        if (c < 0x80) s[o++] = (char)(c < 0x20 ? '?' : c);
        else if (c < 0x800) {
            s[o++] = (char)(0xc0 | c >> 6);
            s[o++] = (char)(0x80 | (c & 0x3f));
        } else if (c >= 0xd800 && c < 0xe000) s[o++] = '?';
        else {
            s[o++] = (char)(0xe0 | c >> 12);
            s[o++] = (char)(0x80 | (c >> 6 & 0x3f));
            s[o++] = (char)(0x80 | (c & 0x3f));
        }
    }
    s[o] = 0;
}
static void name_of(u64 pane, char name[25]) {
    memcpy(name, GP(pane + 0xb0), 24);
    name[24] = 0;
}
/* the pane's box through its global matrix, in the layout's space: x0 y0 x1 y1 */
static void box_of(u64 pane, float box[4]) {
    float w = f32_at(pane + 0x50), h = f32_at(pane + 0x54), x0, y0;
    u8 base = gr8(pane + 0x5b);
    int i;
    x0 = (base & 3) == 1 ? 0 : (base & 3) == 2 ? -w : -w / 2;
    y0 = (base >> 2 & 3) == 1 ? 0 : (base >> 2 & 3) == 2 ? h : h / 2;
    for (i = 0; i < 4; i++) {
        float x = x0 + (i & 1 ? w : 0), y = y0 - (i & 2 ? h : 0);
        float X = f32_at(pane + 0x70) * x + f32_at(pane + 0x74) * y + f32_at(pane + 0x7c);
        float Y = f32_at(pane + 0x80) * x + f32_at(pane + 0x84) * y + f32_at(pane + 0x8c);
        if (!i || X < box[0]) box[0] = X;
        if (!i || Y < box[1]) box[1] = Y;
        if (!i || X > box[2]) box[2] = X;
        if (!i || Y > box[3]) box[3] = Y;
    }
}

/* ---- the panes */
static int pane_seen(u64 pane) { /* shown, and not see-through: a menu's other tabs are alpha 0 above children */
    return (gr8(pane + 0x58) & 1) && gr8(pane + 0x59);
}
static void center_of(u64 pane, float c[2]) {
    float box[4];
    box_of(pane, box);
    c[0] = (box[0] + box[2]) / 2;
    c[1] = (box[1] + box[3]) / 2;
}
/* the child of a pane by name (or by the start of it), 0 for none */
static u64 child_named_visible(u64 pane, const char* want, size_t n, int visible) {
    u64 head = pane + 0x20, node;
    int count = 0;
    char name[25];
    for (node = gr64(head + 8); node != head && guest_ok(node, 16) && count < 256; node = gr64(node + 8), count++) {
        if (!pane_ok(node - 8)) return 0;
        name_of(node - 8, name);
        if ((!visible || pane_seen(node - 8)) && !strncmp(name, want, n)) return node - 8;
    }
    return 0;
}
static u64 child_named(u64 pane, const char* want, size_t n) {
    return child_named_visible(pane, want, n, 1);
}
/* Lobby_ListFriend_00 is shared by the lobby, Shoal and Grizzco. It uses highlighted
   rows and button guides rather than the separate squid cursor. Recognize its own
   pane hierarchy, not translated text or the presence of a friend/room row: those
   rows disappear in the empty and searching states. The add-room control identifies
   the layout even when that action is hidden; the visible title gates menu state. */
static int lobby_list_shown(u64 root) {
    u64 all = child_named(root, "N_All_00", sizeof "N_All_00");
    u64 rotate = all ? child_named(all, "N_Rotate_00", sizeof "N_Rotate_00") : 0;
    u64 title = rotate ? child_named(rotate, "T_Title_00", sizeof "T_Title_00") : 0;
    u64 controls = child_named_visible(root, "N_Rotate_02", sizeof "N_Rotate_02", 0);
    if (!title || !gr8(title + 0x5a) || !controls) return 0;
    return child_named(title, "T_Explain_00", sizeof "T_Explain_00") &&
           child_named_visible(controls, "L_AddRoom_00", sizeof "L_AddRoom_00", 0);
}
/* Visible descendants through wrappers. For a button anchor, stop at nested
   Parts panes so a container doesn't steal one of its child buttons' anchors. */
static u64 descendant_named(u64 pane, const char* want, size_t n, int parts, int depth, int* budget) {
    u64 head = pane + 0x20;
    if (depth > MAX_DEPTH) return 0;
    for (u64 node = gr64(head + 8); node != head && guest_ok(node, 16) && --*budget > 0; node = gr64(node + 8)) {
        u64 child = node - 8, found;
        char name[25];
        if (!pane_ok(child) || !pane_seen(child)) continue;
        name_of(child, name);
        if (!strncmp(name, want, n)) return child;
        if (!parts && name[0] == 'L' && name[1] == '_') continue;
        found = descendant_named(child, want, n, parts, depth + 1, budget);
        if (found) return found;
    }
    return 0;
}
static int valid_box(const float box[4]) {
    return isfinite(box[0]) && isfinite(box[1]) && isfinite(box[2]) && isfinite(box[3]) &&
           box[2] > box[0] && box[3] > box[1];
}
/* PauseText_00 supplies the button-and-text action guides (locker, terminal, etc.).
   Only its visible A variant counts. Other guides (X map, + equip, B back, tutorial
   controller drawings) must not change the gameplay face buttons. Snapshot here on
   the draw thread; input polling only reads this result, never the saved panes. */
static int interaction_prompt(u64 pane, int depth, int* budget) {
    char name[25];
    if (depth > MAX_DEPTH || --*budget < 0 || !pane_ok(pane) || !pane_seen(pane) || !gr8(pane + 0x5a)) return 0;
    name_of(pane, name);
    if (!strcmp(name, "W_BaseText_01")) {
        u64 align = child_named(pane, "N_Align_03", sizeof "N_Align_03");
        u64 text = align ? child_named(align, "T_Info_00_00", sizeof "T_Info_00_00") : 0;
        int key_budget = 128;
        u64 key = text ? descendant_named(align, "T_Btn_00", sizeof "T_Btn_00", 1, 0, &key_budget) : 0;
        if (key && gr8(key + 0x5a) && gr8(text + 0x5a)) {
            char button[8], description[200];
            float box[4];
            text_of(key, button, sizeof button);
            text_of(text, description, sizeof description);
            box_of(key, box);
            if (!strcmp(button, "A") && description[0] && valid_box(box) &&
                box[0] < 960 && box[2] > -960 && box[1] < 540 && box[3] > -540) return 1;
        }
    }
    u64 head = pane + 0x20;
    unsigned count = 0;
    for (u64 node = gr64(head + 8); node != head && guest_ok(node, 16) && *budget > 0 && count < 256;
         node = gr64(node + 8), count++)
        if (interaction_prompt(node - 8, depth + 1, budget)) return 1;
    return 0;
}

/* Snapshot button geometry while its layout is being drawn. B_Btn is the
   game's hit rectangle; Parts bounds also include decoration and can overlap
   several other buttons. Keep Parts bounds only when no hit rectangle exists. */
static void find_buttons(u64 pane, int depth, int* budget, unsigned* count, Button* out) {
    u64 head = pane + 0x20, node;
    for (node = gr64(head + 8); node != head && guest_ok(node, 16) && --*budget > 0; node = gr64(node + 8)) {
        u64 child = node - 8, anchor = 0;
        char name[25];
        if (!pane_ok(child) || !pane_seen(child)) continue;
        name_of(child, name);
        if (name[0] == 'L' && name[1] == '_' && *count < ROOT_BUTTONS) {
            int anchor_budget = 256;
            anchor = descendant_named(child, "N_Cursor", 8, 0, 0, &anchor_budget);
        }
        int tab = !strncmp(name, "L_Tab_", 6) && name[6] >= '0' && name[6] <= '9' &&
                  name[7] >= '0' && name[7] <= '9' && !name[8]
                      ? (name[6] - '0') * 10 + name[7] - '0' + 1 : 0;
        u64 hit = tab || anchor ? child_named(child, "B_Btn", 5) : 0;
        if ((anchor || (tab && hit)) && *count < ROOT_BUTTONS) {
            Button button = {0};
            button.tab = tab;
            button.pane = child;
            memcpy(button.name, name, sizeof button.name);
            box_of(hit ? hit : child, button.box);
            center_of(anchor ? anchor : hit, button.center);
            if (valid_box(button.box) && isfinite(button.center[0]) && isfinite(button.center[1]))
                out[(*count)++] = button;
        }
        if (depth < MAX_DEPTH) find_buttons(child, depth + 1, budget, count, out);
    }
}

/* Only the known X-menu content names establish L/R tab order. Never guess
   another screen's current tab from decoration or from the mouse's position. */
static int xmenu_active_tab(u64 root, const Button* buttons, unsigned count) {
    static const char* const contents[] = {
        "N_ContensMap_00", "N_ContensStage_00", "N_ContensPlayer_00", "N_ContensOption_00"
    };
    unsigned mask = 0;
    int selected = -1, alpha = 0;
    for (unsigned i = 0; i < count; i++)
        if (buttons[i].tab >= 1 && buttons[i].tab <= 4) mask |= 1u << (buttons[i].tab - 1);
    if (mask != 15) return -1;
    int budget = 8000;
    u64 container = descendant_named(root, "N_Contents_00", sizeof "N_Contents_00", 1, 0, &budget);
    if (!container) return -1;
    for (int i = 0; i < 4; i++) {
        u64 pane = child_named(container, contents[i], strlen(contents[i]) + 1);
        int a = pane ? gr8(pane + 0x5a) : 0;
        if (a > alpha) {
            selected = i;
            alpha = a;
        } else if (a && a == alpha)
            selected = -1; /* ambiguous crossfade */
    }
    return selected;
}

/* ---- the hook */
static void census_pane(u64 pane, int depth);
static void layout_draw(CPU* c, u64 entry) {
    u64 layout = c->x[0], root = guest_ok(layout, 0x20) ? gr64(layout + 0x18) : 0, now = plat_ns();
    int i, oldest = 0;
    pm_lock(&g_lock);
    if (root && pane_ok(root)) {
        int cursor_budget = 512;
        u64 cursor = pane_seen(root) ? child_named(root, "L_Cursor_", 9) : 0;
        if (!cursor && pane_seen(root)) cursor = descendant_named(root, "L_Cursor_", 9, 1, 0, &cursor_budget);
        u64 dummy = cursor ? child_named(cursor, "N_Dummy_00", 11) : 0;
        int tab_budget = 512;
        u64 tab_bar = pane_seen(root) ? descendant_named(root, "N_Tab_00", 9, 1, 0, &tab_budget) : 0;
        for (i = 0; i < ROOTS; i++) {
            if (g_ui.roots[i].root == root) break;
            if (g_ui.roots[i].ns < g_ui.roots[oldest].ns) oldest = i;
        }
        if (i == ROOTS) {
            i = oldest;
            g_ui.roots[i].count = 0;
        }
        g_ui.roots[i].root = root;
        g_ui.roots[i].ns = now;
        g_ui.roots[i].active_tab = -1;
        g_ui.roots[i].lobby_list = pane_seen(root) && lobby_list_shown(root);
        int interaction_budget = 512;
        g_ui.roots[i].interaction = g_settings.xbox_play_station_layout_in_menus &&
                                    interaction_prompt(root, 0, &interaction_budget);
        if (dummy) {
            g_ui.squid = dummy;
            center_of(dummy, g_ui.squid_center);
            if (isfinite(g_ui.squid_center[0]) && isfinite(g_ui.squid_center[1])) g_ui.squid_ns = now;
            g_ui.roots[i].count = 0;
        } else if (pane_seen(root) && (tab_bar || now - g_ui.squid_ns < LOOK_NS)) {
            int budget = 8000;
            g_ui.roots[i].count = 0;
            find_buttons(root, 0, &budget, &g_ui.roots[i].count, g_ui.roots[i].buttons);
            if (tab_bar) g_ui.roots[i].active_tab = xmenu_active_tab(root, g_ui.roots[i].buttons, g_ui.roots[i].count);
        } else
            g_ui.roots[i].count = 0;
    }
    if (__atomic_exchange_n(&g_census.requested, 0, __ATOMIC_ACQ_REL) && !g_census.until) {
        g_census.out = fopen("uicensus.txt", "a");
        g_census.until = g_census.out ? now + CENSUS_NS : 0;
        g_census.layouts = g_census.panes = 0;
        if (g_census.out) fprintf(g_census.out, "== census %u at %.2f s\n", ++g_census.number, (double)now / 1e9);
    }
    if (g_census.until && now < g_census.until && root && pane_ok(root)) {
        g_census.layouts++;
        fprintf(g_census.out, "-- layout %llx, root %.0fx%.0f\n", (unsigned long long)layout, f32_at(root + 0x50),
                f32_at(root + 0x54));
        census_pane(root, 1);
    } else if (g_census.until && now >= g_census.until) {
        fprintf(g_census.out, "== %u layouts, %u panes\n\n", g_census.layouts, g_census.panes);
        fclose(g_census.out);
        g_census.out = NULL;
        g_census.until = 0;
        LOG("ui: census %u written to uicensus.txt (%u layouts, %u panes)\n", g_census.number, g_census.layouts,
            g_census.panes);
    }
    pm_unlock(&g_lock);
    draw_original(c, entry);
}

/* ---- the census */
static void census_pane(u64 pane, int depth) {
    char name[25], text[200];
    float box[4];
    u64 head, node;
    if (depth > MAX_DEPTH || g_census.panes >= MAX_PANES || !pane_ok(pane)) return;
    if (!(gr8(pane + 0x58) & 1)) return; /* hidden, and everything under it */
    g_census.panes++;
    name_of(pane, name);
    box_of(pane, box);
    text[0] = 0;
    if (name[0] == 'T' && name[1] == '_') text_of(pane, text, sizeof text);
    fprintf(g_census.out, "%*s%s %.0f %.0f %.0f %.0f a=%u/%u%s%s%s%s\n", depth * 2, "", name, box[0], box[1], box[2],
            box[3], (unsigned)gr8(pane + 0x59), (unsigned)gr8(pane + 0x5a),
            pane == g_ui.focus ? " [the cursor's]" : "", text[0] ? " \"" : "", text, text[0] ? "\"" : "");
    head = pane + 0x20;
    unsigned siblings = 0;
    for (node = gr64(head + 8); node != head && guest_ok(node, 16) && siblings++ < MAX_PANES;
         node = gr64(node + 8))
        census_pane(node - 8, depth + 1);
}

/* ---- the pointer */
/* the squid cursor is on the screen: a menu is up. Under g_lock */
static int squid_shown(u64 now) {
    return g_ui.squid && now - g_ui.squid_ns < SHOWN_NS;
}
/* X-menu tabs and the lobby rooms/friends browser do not need a squid cursor. */
static int menu_shown(u64 now) {
    if (squid_shown(now)) return 1;
    for (int r = 0; r < ROOTS; r++)
        if (g_ui.roots[r].root && now - g_ui.roots[r].ns < SHOWN_NS &&
            (g_ui.roots[r].active_tab >= 0 || g_ui.roots[r].lobby_list)) return 1;
    return 0;
}
static int tab_active(u64 pane, u64 now) {
    for (int r = 0; r < ROOTS; r++) {
        if (now - g_ui.roots[r].ns >= SHOWN_NS) continue;
        for (unsigned i = 0; i < g_ui.roots[r].count; i++)
            if (g_ui.roots[r].buttons[i].pane == pane) return g_ui.roots[r].active_tab;
    }
    return -1;
}
/* the buttons on the screen, one at a time: *i from 0, NULL after the last. Under g_lock */
static const Button* next_button(int* r, unsigned* i, u64 now) {
    for (; *r < ROOTS; ++*r, *i = 0) {
        if (now - g_ui.roots[*r].ns >= SHOWN_NS) continue;
        while (*i < g_ui.roots[*r].count) {
            const Button* b = &g_ui.roots[*r].buttons[(*i)++];
            return b;
        }
    }
    return NULL;
}
static const Button* button_find(u64 pane, u64 now) {
    int r = 0;
    unsigned i = 0;
    const Button* b;
    while ((b = next_button(&r, &i, now)))
        if (b->pane == pane) return b;
    return NULL;
}
/* the cursor's button: the one whose N_Cursor pane the squid is nearest, 0 when it is on none of them. Under
   g_lock */
static u64 cursor_button(u64 now) {
    u64 best = 0;
    float sq[2], best_d = SQUID_NEAR * SQUID_NEAR;
    int r = 0;
    unsigned i = 0;
    const Button* b;
    if (squid_shown(now)) {
        memcpy(sq, g_ui.squid_center, sizeof sq);
        while ((b = next_button(&r, &i, now))) {
            float a[2], d;
            if (b->tab) continue; /* the squid identifies content buttons, not the active tab */
            memcpy(a, b->center, sizeof a);
            d = (a[0] - sq[0]) * (a[0] - sq[0]) + (a[1] - sq[1]) * (a[1] - sq[1]);
            if (d < best_d) {
                best = b->pane;
                best_d = d;
            }
        }
    }
    if (best != g_ui.focus && g_settings.ui_census) {
        char name[25] = "nothing";
        const Button* b = button_find(best, now);
        if (b) memcpy(name, b->name, sizeof name);
        LOG("ui: the cursor is on %s\n", name);
    }
    g_ui.focus = best;
    return best;
}
/* the button under (x, y): a button's pane is wider than what it draws, so of the panes the point is in, the one
   whose center is nearest. Under g_lock */
static u64 button_at(float x, float y, u64 now) {
    u64 best = 0;
    float best_d = 0;
    int r = 0;
    unsigned i = 0;
    const Button* b;
    while ((b = next_button(&r, &i, now))) {
        float box[4], d;
        memcpy(box, b->box, sizeof box);
        if (x < box[0] || x > box[2] || y < box[1] || y > box[3]) continue;
        d = (x - (box[0] + box[2]) / 2) * (x - (box[0] + box[2]) / 2) +
            (y - (box[1] + box[3]) / 2) * (y - (box[1] + box[3]) / 2);
        if (!best || d < best_d) {
            best = b->pane;
            best_d = d;
        }
    }
    return best;
}
/* what the menu's mouse knows, in the log (ui_census). Under g_lock */
static void diagnose(u64 now, const char* when) {
    unsigned shown = 0;
    float sq[2] = {0, 0};
    char name[25] = "nothing";
    int r = 0, squid = squid_shown(now);
    unsigned i = 0;
    while (next_button(&r, &i, now)) shown++;
    if (squid) memcpy(sq, g_ui.squid_center, sizeof sq);
    const Button* focused = button_find(g_ui.focus, now);
    if (focused) memcpy(name, focused->name, sizeof name);
    LOG("ui: %s: %u buttons shown, the squid %s at %.0f %.0f, on %s\n", when, shown, squid ? "shown" : "gone", sq[0],
        sq[1], name);
}
int inkypaint_ui_menu_active(void) {
    int shown;
    if (!enabled) return 0;
    pm_lock(&g_lock);
    shown = menu_shown(plat_ns());
    pm_unlock(&g_lock);
    return shown;
}
int inkypaint_ui_interaction_active(void) {
    int shown = 0;
    u64 now = plat_ns();
    if (!enabled) return 0;
    pm_lock(&g_lock);
    for (int r = 0; r < ROOTS; r++)
        if (g_ui.roots[r].root && now - g_ui.roots[r].ns < SHOWN_NS && g_ui.roots[r].interaction) {
            shown = 1;
            break;
        }
    pm_unlock(&g_lock);
    return shown;
}
static void nav_end(const char* why) {
    if (g_settings.ui_census && g_nav.target) LOG("ui: walk ends after %d steps: %s\n", g_nav.steps, why);
    g_nav.target = 0;
    g_nav.press = 0;
    g_nav.confirm = 0;
}
/* the target still a button on the screen. Under g_lock */
static int target_shown(u64 now) {
    int r = 0;
    unsigned i = 0;
    const Button* b;
    while ((b = next_button(&r, &i, now)))
        if (b->pane == g_nav.target) return 1;
    return 0;
}
/* the next step toward the target, from the cursor's button. Under g_lock */
static void nav_step(u64 now) {
    u64 focus = g_ui.focus;
    float f[2], t[2], dx, dy;
    int across;
    if (!target_shown(now)) return nav_end("the button is gone");
    const Button* target = button_find(g_nav.target, now);
    if (target && target->tab) {
        int active = tab_active(target->pane, now), wanted = target->tab - 1;
        if (wanted > 3 || active < 0) return nav_end("current tab is unknown");
        if (active == wanted) return nav_end("tab selected"); /* no A: changing tabs selects them */
        if (g_nav.steps >= MAX_STEPS || (g_nav.steps && active == g_nav.tab_from))
            return nav_end("tab did not change");
        g_nav.tab_from = active;
        g_nav.press = wanted > active ? PAD_R : PAD_L;
        g_nav.steps++;
        g_nav.until = now + PRESS_NS;
        g_nav.settle = g_nav.until + SETTLE_NS;
        if (g_settings.ui_census) LOG("ui: tab %d -> %d, pressing %s\n", active, wanted,
                                     g_nav.press == PAD_R ? "R" : "L");
        return;
    }
    if (focus == g_nav.target) {
        if (g_nav.confirm) {
            g_nav.press = PAD_A;
            g_nav.until = now + PRESS_NS;
            g_nav.settle = g_nav.until + SETTLE_NS;
            g_nav.confirm = 0;
            return;
        }
        return nav_end("there");
    }
    if (!focus) return nav_end("the squid is on no button");
    if (g_nav.steps >= MAX_STEPS) return nav_end("too many steps");
    if (g_nav.steps && focus == g_nav.from) g_nav.failed[g_nav.axis] = 1; /* that press moved nothing */
    else memset(g_nav.failed, 0, sizeof g_nav.failed); /* a new row may permit that axis */
    const Button *from = button_find(focus, now), *to = button_find(g_nav.target, now);
    if (!from || !to) return nav_end("layout changed");
    memcpy(f, from->center, sizeof f);
    memcpy(t, to->center, sizeof t);
    dx = t[0] - f[0];
    dy = t[1] - f[1];
    across = fabsf(dx) > fabsf(dy);
    if (g_nav.failed[across ? 0 : 1]) across = !across;
    if (g_nav.failed[across ? 0 : 1] || (across ? fabsf(dx) : fabsf(dy)) < 1) return nav_end("no way there");
    g_nav.axis = across ? 0 : 1;
    g_nav.press = across ? (dx > 0 ? PAD_RIGHT : PAD_LEFT) : (dy > 0 ? PAD_UP : PAD_DOWN);
    g_nav.from = focus;
    g_nav.steps++;
    g_nav.until = now + PRESS_NS;
    g_nav.settle = g_nav.until + SETTLE_NS;
}
static void nav_start(u64 target, int confirm, u64 now) {
    if (g_nav.target != target) {
        g_nav.steps = 0;
        memset(g_nav.failed, 0, sizeof g_nav.failed);
        /* Release the old direction before steering to another target. */
        g_nav.settle = g_nav.press ? now + SETTLE_NS : 0;
        g_nav.press = 0;
    }
    g_nav.target = target;
    g_nav.confirm = confirm;
}
u64 inkypaint_ui_pointer(float x, float y, int inside, int moved, int click) {
    u64 now = plat_ns(), out = 0, hovered = 0, focus;
    if (!enabled || !g_settings.mouse_in_menus) return 0;
    pm_lock(&g_lock);
    if (!menu_shown(now)) { /* no supported menu */
        if (g_ui.menu && g_settings.ui_census) LOG("ui: the menu is gone\n");
        g_ui.menu = 0;
        g_ui.focus = 0;
        memset(&g_nav, 0, sizeof g_nav);
        pm_unlock(&g_lock);
        return 0;
    }
    if (!inside) {
        nav_end("pointer inactive");
        g_nav.hovered = 0;
        pm_unlock(&g_lock);
        return 0;
    }
    focus = cursor_button(now);
    if (!g_ui.menu && g_settings.ui_census) {
        diagnose(now, "a menu is up");
        __atomic_store_n(&g_census.requested, 1, __ATOMIC_RELEASE); /* and what it draws, in uicensus.txt */
    }
    g_ui.menu = 1;
    if (inside && (moved || click)) hovered = button_at(x, y, now);
    if (click && g_settings.ui_census) {
        char name[25] = "no button";
        const Button* b = button_find(hovered, now);
        if (b) memcpy(name, b->name, sizeof name);
        LOG("ui: a click at %.0f %.0f, on %s\n", x, y, name);
        diagnose(now, "the click");
    }
    if (g_nav.press == PAD_A && focus != g_nav.target) nav_end("selection changed during confirmation");
    if (click && !hovered) nav_end("click outside buttons");
    if (click && hovered) {
        if (hovered == focus && !g_nav.target) { /* there already */
            g_nav.target = hovered;
            g_nav.confirm = 1;
            g_nav.settle = 0;
        } else
            nav_start(hovered, 1, now);
    } else if (moved && hovered && hovered != g_nav.hovered && hovered != focus && !g_nav.confirm &&
               !button_find(hovered, now)->tab)
        nav_start(hovered, 0, now); /* the pointer came onto another button: the cursor follows it */
    if (moved && inside) g_nav.hovered = hovered;
    if (g_nav.target) {
        if (g_nav.press && now < g_nav.until) out = g_nav.press;
        else if (now >= g_nav.settle) {
            if (g_nav.press == PAD_A) nav_end("pressed");
            else nav_step(now);
            if (g_nav.target && now < g_nav.until) out = g_nav.press;
        } else if (now >= g_nav.until)
            out = 0; /* let go, until the cursor has moved */
    }
    pm_unlock(&g_lock);
    return out;
}

static int hook(u64 at, u32 first, GuestFn fn, GuestFn* original, const char* what) {
    if (at < g_code_lo || at >= g_code_hi || gr32(at) != first) {
        WARN("ui: %s is not where this build has it\n", what);
        return 0;
    }
    jit_code_changed(at, 4);
    *original = jit_wait(at);
    if (!*original) {
        WARN("ui: could not compile %s\n", what);
        return 0;
    }
    g_disp[(at - g_code_lo) / 4] = fn;
    return 1;
}
void inkypaint_ui_init(void) {
    if (!(g_settings.ui_census || g_settings.mouse_in_menus || g_settings.keyboard_and_mouse_input ||
          g_settings.xbox_play_station_layout_in_menus || inkypaint_framerate_target()) || inkypaint_any_title() ||
        g_hle.kernel.title_id != INKYPAINT_TITLE_ID)
        return;
    /* ldr x8, [x0, #0x18]: the root pane */
    if (!hook(LAYOUT_DRAW, 0xf9400c08, layout_draw, &draw_original, "nn::ui2d::Layout::Draw")) return;
    enabled = 1;
    LOG("ui: menu detection and mouse navigation hooked for squid-cursor menus, X-menu tabs and lobby rooms/friends%s\n", g_settings.ui_census ? "; F9 writes the menus drawn to uicensus.txt" : "");
}
