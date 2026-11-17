/* d4tune.c -- the game's Performance Tuning screens over the loaded world.
 *
 * Tune Car between Garage and Options in career free roam's pause menu opens
 * the original mode picker.  An optional setting also enables F5 while
 * driving.  The original dyno BUN, screen factories, car/profile code,
 * category locks and slider handlers do all tuning.  Dyno Run, Test Run and
 * Quit Tuning are omitted from the dyno menu; Save Profile remains, and Back
 * offers to save a changed setup before returning to the loaded world.
 *
 * Addresses and layouts are for SPEED2.EXE v1.2 NA.  The original dyno entry
 * at 0x5309E0 resolves the driven car, resets 0x839C40, calls 0x50F800 and
 * then 0x526970, which changes race parameters and starts a world load.  This
 * feature reproduces only the first three operations.  It loads
 * Global\InGamePerformanceTuning.bun exactly as the world's dyno path at
 * 0x57F334 does, then atomically replaces UI_Pause.fng with
 * UI_PerformanceTuning_Setting.fng.
 *
 * A non-FF previous profile makes the picker's normal accept path go to
 * UI_PerformanceDyno_MAIN instead of the dyno chart.  On exit the current
 * setup is stored in the visit copy and all five setups are put back into the
 * car record through 0x50FC20/0x50FAE0.  The game's ordinary profile save
 * remains responsible for writing the record to disk. */
#include "cpu.h"
#include "game.h"
#include "guest.h"
#include "kbd.h"
#include "setting.h"
#include <stdio.h>
#include <strings.h>

void dispatch_patch(uint32_t a, void (*f)(CPU *));
uint32_t gheap_alloc(int heap, uint32_t n, int zero);
long autotest_frame(void);
uint32_t language_add_text(const char *name, const char *text);
int dikbd_ours(unsigned dik, const char *what);
uint32_t garage_find(uint32_t db, uint32_t h);
int coop_in(void);
int coop_racing_together(void);
int career_fe_active(const char *name);
void dyno_dispatch(CPU *c);
void sub_0055a740(CPU *c);

#define DYNO               0x00839c40u
#define D_TUNED            0x900u
#define D_KIND             0x9fcu
#define G_DYNO_SOURCE      0x0083a640u
#define G_DYNO_CONTEXT     0x0083a644u
#define G_TUNE_CATEGORY    0x0083a648u
#define G_TUNE_KIND        0x0083a63cu
#define G_TUNE_RIDE        0x0083a540u
#define G_TUNE_BUSY        0x0083a655u
#define G_PLAYER1          SPLIT_PLAYERS_INDEX

#define FN_DYNO_RESET      0x0050f9e0u
#define FN_DYNO_CLOSE      0x0050f970u
#define FN_DYNO_INIT       0x0050f800u
#define FN_DYNO_STORE      0x0050fc20u
#define FN_DYNO_PUT_IN     0x0050fae0u
#define FN_DYNO_CHANGED    0x0050fd10u
#define FN_RECORD_SPECIAL  0x00503640u
#define FN_RECORD_TABLE    0x00503510u
#define FN_RECORD_CUSTOM   0x005035c0u
#define FN_BUNDLE_NEW      0x0057cef0u
#define FN_BUNDLE_START    0x0057bd70u
#define FN_BUNDLE_CALLBACK 0x00571050u /* cdecl (object): decrement object +0xC8 */
#define FN_SCREEN_STATE    0x0050b8c0u /* cdecl (package name, state): set the screen registration's input state */
#define FN_FE_SWITCH       0x00537980u /* cdecl (from package, to package, argument) */
#define FN_FE_REMOVE       0x005379a0u
#define FN_FE_STRING       0x00537a10u /* cdecl (package name, object hash): its FE string object */
#define FN_FE_SET_LABEL    0x00495460u /* cdecl (package name, object hash, label hash) */
#define FN_DIALOG          0x00558790u /* cdecl (from package, style, labels/messages, first, text) */
#define FN_MODAL_ON        0x00538c00u
#define FN_MODAL_OFF       0x00538ca0u
#define FN_MC_JOB          0x004a3100u /* cdecl (from package, completion package, job, 0, 0, 0, 0) */
#define FN_PAUSE           0x005f4230u /* thiscall race coordinator (player owner): pause and open UI_Pause */
#define FN_RESUME          0x005f4240u /* thiscall race coordinator (1 race, 2 free roam): resume */
#define FN_SETTING_MSG     0x0055a740u
#define FN_DYNO_MSG        0x005647e0u
#define FN_ADD_ITEM        0x0050dab0u
#define FN_DELETE          0x00575630u

#define S_BUNDLE           0x007a0cb4u /* "Global\\InGamePerformanceTuning.bun" */
#define S_HELP_BAR         0x00790000u /* "UI_PC_Help_Bar.fng" */
#define S_PAUSE            0x0079070cu /* "UI_Pause.fng" */
#define S_DYNO_MAIN        0x007906f0u /* "UI_PerformanceDyno_MAIN.fng" */
#define S_SETTING          0x00799b08u
#define S_MASTER           0x00799bb4u
#define S_SLIDERS          0x00799b50u
#define S_DRIVETRAIN       0x00799b2cu
#define S_GRAPH            0x00799b94u
#define S_NOS              0x00799b74u
#define S_DIALOG_STYLE     0x00783fe5u

#define VT_REPLAY          0x007954f0u
#define VT_DYNO_MAIN       0x0079d30cu
#define ITEM_BYTES         0x48u
#define IT_GREY            0x20u
#define IT_LABEL           0x2cu
#define ICON_TUNE_SETTINGS 0xd4060fc4u
#define LBL_GARAGE         0x453890e1u /* MU_PAUSE_QUIT_TO_CRIB, the pause menu's Garage */
#define LBL_DYNO_RUN       0x436c91d4u /* TEST_PAUSE_DYNO_TEST */
#define LBL_TEST_RUN       0xf62234efu /* TEST_PAUSE_TEST_TRACK */
#define LBL_SAVE_PROFILE   0x410258c3u
#define LBL_QUIT_TUNING    0xde5cdfb2u
#define MSG_ACCEPT         0x0c407210u
#define MSG_BACK           0x911ab364u
#define MSG_QUIT_YES       0x30ed2368u
#define MSG_SAVE_NOW_YES   0x24dd40cfu
#define MSG_SAVE_NOW_NO    0x1fab5998u
#define MSG_DIALOG_BACK    0x34dc1bcfu
#define MSG_SAVE_DONE      0x251bf83eu
#define LBL_YES            0x0000f770u
#define LBL_NO             0x0000061cu
#define LBL_SAVE_NOW_ASK   0x71851654u
#define OBJ_SETTING_TITLE  0x95ad096bu /* Setting's "Options" string, stock label "Performance Tuning" */
#define OBJ_DYNO_TITLE     0x42adb44cu /* Dyno Main's "HEADER_TEXT" string */
#define LBL_DYNO_SHOP      0x8a24d87au
#define DIK_F5             0x3fu
#define R_OWNED            0x7f5u
#define UI_TIMEOUT_FRAMES  3600
#define SAVE_GRACE_FRAMES  10
#define G_MC_JOB           0x00836514u
#define MC_JOB_PROFILE     0x51u
#define MC_PACKAGE         "MC_Main.fng"

enum { DT_IDLE, DT_LOADING, DT_QUEUED, DT_ACTIVE };
enum { DX_NONE, DX_ASK_SAVE, DX_WAIT_SAVE };
static struct {
    int state, pending, from_pause, seen_main, label_ready, close_pending, close_keep, bundle_ready, f5_enabled;
    int pause_requested, pause_owned, pause_replaced;
    long at;
    uint32_t bundle, bundle_mark, label, record, close_package;
    uint32_t exit_owner, exit_package;
    uint32_t key_seq;
    int key_init, key_down;
    int exit_state;
    long exit_at;
    const char *close_why;
} D;
static const uint32_t tuning_packages[] = { S_NOS, S_GRAPH, S_DRIVETRAIN, S_SLIDERS, S_MASTER, S_SETTING, S_DYNO_MAIN };

static void ret_this(CPU *c, uint32_t bytes){ POP32(c->eip); c->esp += bytes; }

static uint32_t driven_handle(void){
    return MR32(G_GAMEMODE) == 1u ? guest_rd32(G_CAREER_CAR) : guest_rd32(G_QR_CAR);
}

/* Resolve the record through the same two branches as 0x530A5B.  garage_find
 * also sees the runtime's extended career slots, so use it as a final lookup. */
static uint32_t driven_record(CPU *c){
    uint32_t h = driven_handle(), db = G_FECARS;
    if (!h) return 0;
    uint32_t special = gcall_main(c, FN_RECORD_SPECIAL, db, 1, &h);
    uint32_t rec = gcall_main(c, special ? FN_RECORD_TABLE : FN_RECORD_CUSTOM, db, 1, &h);
    if (!rec) rec = garage_find(db, h);
    return guest_ok(rec, R_OWNED) ? rec : 0;
}

static int network_game(void){
    uint32_t gm = guest_rd32(G_GAMEMODE);
    return gm == 4u || gm == 8u || gm == 0x200u || MR8(G_NET_ONLINE) ||
           (int32_t)MR32(G_NET_CLIENT) > 0 || (int32_t)MR32(G_NET_HOST) > 0 ||
           coop_in() || coop_racing_together();
}

static const char *not_now(int pause_item){
    if (D.state != DT_IDLE || D.pending) return "already open";
    if (MR32(G_FLOW) != 6u) return "not in the world";
    if (network_game()) return "a network or split-screen race";
    if (!guest_rd32(G_FENG)) return "no front end";
    if (MR32(G_GAMEMODE) == 1u && !MR8(G_PROFILE_NAME + 0x20u)) return "no profile loaded";
    if (!pause_item){
        uint32_t race = guest_rd32(G_RACE);
        if (!guest_ok(race, 3u) || MR32(race) != 4u) return "not driving";
    }
    return garage_find(G_FECARS, driven_handle()) ? 0 : "no owned car record";
}

static uint32_t top_name(void){
    uint32_t fe = guest_rd32(G_FENG), mgr = fe ? guest_rd32(fe + 8u) : 0;
    uint32_t top = mgr ? guest_rd32(mgr + 0xe4u) : 0;
    return top ? guest_rd32(top + 0x0cu) : 0;
}

static int top_is(const char *name){
    char b[64];
    guest_read_str(top_name(), b, sizeof b);
    return b[0] && !strcasecmp(b, name);
}

static int hud_on_top(void){
    uint32_t pl = guest_rd32(G_PLAYER1), hud = guest_ok(pl, 0x9a7u) ? guest_rd32(pl + 0x9a4u) : 0;
    char top[64], own[64];
    guest_read_str(top_name(), top, sizeof top);
    guest_read_str(hud ? guest_rd32(hud + 0x10u) : 0, own, sizeof own);
    return top[0] && !strcasecmp(top, own);
}

static void package_remove(CPU *c, uint32_t name){
    gcall_main(c, FN_FE_REMOVE, 0, 1, &name);
}

static void screen_state(CPU *c, uint32_t name, uint32_t state){
    uint32_t a[2] = { name, state };
    gcall_main(c, FN_SCREEN_STATE, 0, 2, a);
}

static int apply_setup(CPU *c){
    uint32_t kind = guest_rd32(G_TUNE_KIND);
    if (!D.record || guest_rd32(DYNO) != D.record || !guest_rd32(DYNO + D_TUNED) || kind >= 5u) return 0;
    gcall_main(c, FN_DYNO_STORE, DYNO, 1, &kind);
    gcall_main(c, FN_DYNO_PUT_IN, DYNO, 1, &kind);
    return 1;
}

static int setup_changed(CPU *c){
    return D.record && guest_rd32(DYNO) == D.record && (gcall_main(c, FN_DYNO_CHANGED, DYNO, 0, 0) & 0xffu);
}

static void finish(CPU *c, int keep, const char *why){
    if (D.state == DT_IDLE) return;
    if (keep) apply_setup(c);
    if (D.close_package) screen_state(c, D.close_package, 0u);
    else if (D.pause_replaced) screen_state(c, S_SETTING, 0u);
    if (D.pause_replaced || D.pause_owned) screen_state(c, S_PAUSE, 0u);
    gcall_main(c, FN_DYNO_CLOSE, DYNO, 0, 0);
    MR32(G_TUNE_KIND) = 0xffu;
    MR32(G_TUNE_CATEGORY) = 0u;
    MR32(G_TUNE_RIDE) = 0u;
    MR8(G_TUNE_BUSY) = 0;
    if ((D.pause_replaced || (D.pause_requested && D.pause_owned)) && MR32(G_FLOW) == 6u){
        uint32_t coordinator = guest_rd32(G_RACESTATUS), resume = MR32(G_RACETYPE) == 0u ? 2u : 1u;
        if (coordinator) gcall_main(c, FN_RESUME, coordinator, 1, &resume);
    }
    for (unsigned i = 0; i < sizeof tuning_packages / sizeof tuning_packages[0]; i++) package_remove(c, tuning_packages[i]);
    if (D.pause_replaced || D.pause_owned) package_remove(c, S_HELP_BAR);
    if (D.pause_owned && !D.pause_replaced) package_remove(c, S_PAUSE);
    fprintf(stderr, "[d4tune] f=%ld closed (%s)%s\n", autotest_frame(), why, keep ? ", setups put into the car" : "");
    D.state = DT_IDLE; D.pending = 0; D.from_pause = 0; D.seen_main = 0; D.record = 0;
    D.pause_requested = 0; D.pause_owned = 0; D.pause_replaced = 0;
    D.close_pending = 0; D.close_keep = 0; D.close_package = 0; D.close_why = 0;
    D.exit_state = DX_NONE; D.exit_owner = 0; D.exit_package = 0; D.exit_at = 0;
}

static void request_close(uint32_t package, int keep, const char *why){
    D.close_pending = 1; D.close_keep = keep; D.close_package = package; D.close_why = why;
}

static void ask_save(CPU *c, uint32_t owner, uint32_t package){
    uint32_t a[9] = { package, S_DIALOG_STYLE, LBL_YES, LBL_NO, MSG_SAVE_NOW_YES,
                      MSG_SAVE_NOW_NO, MSG_DIALOG_BACK, 1u, LBL_SAVE_NOW_ASK };
    gcall(c, FN_DIALOG, 0, 9, a);
    gcall(c, FN_MODAL_ON, owner, 0, 0);
    D.exit_state = DX_ASK_SAVE; D.exit_owner = owner; D.exit_package = package;
    fprintf(stderr, "[d4tune] f=%ld Back: asking whether to save the changed setup\n", autotest_frame());
}

static int exit_message(CPU *c, uint32_t msg, uint32_t fallback, void (*stock)(CPU *)){
    if (D.exit_state == DX_ASK_SAVE){
        if (msg == MSG_DIALOG_BACK){
            gcall(c, FN_MODAL_OFF, D.exit_owner, 0, 0);
            D.exit_state = DX_NONE; D.exit_owner = 0; D.exit_package = 0;
            ret_this(c, 0x10u);
            return 1;
        }
        if (msg == MSG_SAVE_NOW_NO){
            uint32_t package = D.exit_package ? D.exit_package : fallback;
            apply_setup(c);
            D.exit_state = DX_NONE;
            request_close(package, 0, "Back, profile save declined");
            ret_this(c, 0x10u);
            return 1;
        }
        if (msg == MSG_SAVE_NOW_YES){
            uint32_t package = D.exit_package ? D.exit_package : fallback;
            apply_setup(c);
            uint32_t a[7] = { package, package, MC_JOB_PROFILE, 0u, 0u, 0u, 0u };
            D.exit_state = DX_WAIT_SAVE; D.exit_at = autotest_frame();
            gcall(c, FN_MC_JOB, 0, 7, a);
            fprintf(stderr, "[d4tune] f=%ld Back: saving the profile\n", D.exit_at);
            ret_this(c, 0x10u);
            return 1;
        }
    }
    if (D.exit_state == DX_WAIT_SAVE && msg == MSG_SAVE_DONE){
        uint32_t package = D.exit_package ? D.exit_package : fallback;
        stock(c);
        D.exit_state = DX_NONE;
        request_close(package, 0, "Back, profile save complete");
        return 1;
    }
    return 0;
}

static int back(CPU *c, uint32_t package, const char *unchanged){
    uint32_t owner = c->ecx, actual = guest_ok(owner, 8u) ? guest_rd32(owner + 4u) : 0u;
    if (!setup_changed(c)) request_close(actual ? actual : package, 0, unchanged);
    else if (actual) ask_save(c, owner, actual);
    else request_close(package, 1, "Back, no package for the save question");
    ret_this(c, 0x10u);
    return 1;
}

static int begin(CPU *c, int from_pause){
    const char *why = not_now(from_pause);
    if (why){ D.from_pause = 0; fprintf(stderr, "[d4tune] f=%ld not opened (%s)\n", autotest_frame(), why); return 0; }
    D.record = driven_record(c);
    gcall_main(c, FN_DYNO_RESET, DYNO, 0, 0);
    MR32(G_DYNO_SOURCE) = MR32(G_GAMEMODE) == 1u ? 0u : 1u;
    MR32(G_DYNO_CONTEXT) = 1u;
    uint32_t init[2] = { 0u, D.record };
    gcall_main(c, FN_DYNO_INIT, DYNO, 2, init);
    if (guest_rd32(DYNO) != D.record){
        fprintf(stderr, "[d4tune] f=%ld dyno initialization rejected record 0x%08x\n", autotest_frame(), D.record);
        D.state = DT_LOADING;
        finish(c, 0, "initialization failed");
        return 0;
    }

    /* A non-FF previous kind makes Setting's stock accept path open the dyno
     * menu rather than the chart.  The selected kind replaces this one. */
    MR32(G_TUNE_KIND) = 0u;
    D.pause_requested = from_pause;
    if (!D.bundle){
        uint32_t a[5] = { S_BUNDLE, 3u, 0u, 0u, 0u };
        D.bundle_mark = gheap_alloc(1, 0xd0u, 1);
        if (!guest_ok(D.bundle_mark, 0xcbu)){
            D.state = DT_LOADING;
            finish(c, 0, "no memory for the tuning bundle marker");
            return 0;
        }
        MR32(D.bundle_mark + 0xc8u) = 1u;
        D.bundle = gcall_main(c, FN_BUNDLE_NEW, 0, 5, a);
        if (!D.bundle){
            D.state = DT_LOADING;
            finish(c, 0, "the tuning bundle could not be created");
            return 0;
        }
        /* The loader calls 0x571050 with bundle_mark after this bundle, not
         * merely some concurrent resource load, has completed. */
        uint32_t start[2] = { FN_BUNDLE_CALLBACK, D.bundle_mark };
        gcall_main(c, FN_BUNDLE_START, D.bundle, 2, start);
    }
    if (!from_pause){
        uint32_t player = guest_rd32(G_PLAYER1), owner = guest_ok(player, 0xbu) ? guest_rd32(player + 8u) : 0;
        uint32_t coordinator = guest_rd32(G_RACESTATUS);
        if (!owner || !coordinator){
            D.state = DT_LOADING;
            finish(c, 0, "the game could not pause");
            return 0;
        }
        gcall_main(c, FN_PAUSE, coordinator, 1, &owner);
        D.pause_requested = 1;
        D.pause_owned = 1;
    }
    D.state = DT_LOADING; D.from_pause = from_pause; D.seen_main = 0; D.at = autotest_frame();
    fprintf(stderr, "[d4tune] f=%ld loading the stock tuning screens for record 0x%08x\n", D.at, D.record);
    return 1;
}

static void switch_to_setting(CPU *c){
    if (!top_is("UI_Pause.fng")) return;
    uint32_t a[3] = { S_PAUSE, S_SETTING, 0u };
    gcall_main(c, FN_FE_SWITCH, 0, 3, a);
    D.pause_replaced = 1;
    D.state = DT_QUEUED; D.at = autotest_frame();
    fprintf(stderr, "[d4tune] f=%ld pause menu replaced by mode selection\n", D.at);
}

static int f5_pressed(void){
    uint32_t head = kbd_ev_head();
    if (!D.key_init){ D.key_init = 1; D.key_seq = head; return 0; }
    int press = 0;
    for (; D.key_seq != head; D.key_seq++){
        KbdEv e;
        int r = kbd_ev_get(D.key_seq, &e);
        if (r == 0) break;
        if (r > 0 && e.dik == DIK_F5){
            if (e.down && !D.key_down && dikbd_ours(DIK_F5, "Tune Car")) press = 1;
            D.key_down = e.down;
        }
    }
    return press;
}

void d4tune_frame(CPU *c){
    if (!D.label_ready){ D.label = language_add_text("D4TUNE_TUNE_CAR", "Tune Car"); D.label_ready = D.label != 0; }
    if (D.f5_enabled && f5_pressed() && D.state == DT_IDLE && !D.pending){
        if (!hud_on_top()) fprintf(stderr, "[d4tune] f=%ld F5: not opened (a screen is over the HUD)\n", autotest_frame());
        else { D.from_pause = 0; D.pending = 1; }
    }
    if (D.close_pending){ finish(c, D.close_keep, D.close_why ? D.close_why : "closed"); return; }
    if (D.exit_state == DX_WAIT_SAVE && autotest_frame() - D.exit_at >= SAVE_GRACE_FRAMES &&
        !MR32(G_MC_JOB) && !career_fe_active(MC_PACKAGE)){
        uint32_t package = D.exit_package ? D.exit_package : S_DYNO_MAIN;
        D.exit_state = DX_NONE;
        request_close(package, 0, "Back, profile save finished");
        return;
    }
    if (D.pending && D.state == DT_IDLE){ int pause = D.from_pause; D.pending = 0; begin(c, pause); }
    if (D.state == DT_LOADING){
        long waited = autotest_frame() - D.at;
        if (!D.bundle_ready && waited > 0 && D.bundle_mark && guest_rd32(D.bundle_mark + 0xc8u) == 0u) D.bundle_ready = 1;
        if (D.bundle_ready) switch_to_setting(c);
        if (D.state == DT_LOADING && waited > UI_TIMEOUT_FRAMES){
            D.bundle = 0; D.bundle_mark = 0;
            finish(c, 0, D.bundle_ready ? "the pause menu could not be replaced" : "the tuning bundle did not load");
        }
    }
    if (D.state == DT_QUEUED){
        uint32_t name = S_SETTING;
        if (gcall_main(c, FN_FE_PACKAGE, 0, 1, &name)){
            uint32_t find[2] = { S_SETTING, OBJ_SETTING_TITLE };
            if (gcall_main(c, FN_FE_STRING, 0, 2, find)){
                uint32_t title[3] = { S_SETTING, OBJ_SETTING_TITLE, D.label };
                gcall_main(c, FN_FE_SET_LABEL, 0, 3, title);
                D.state = DT_ACTIVE;
                fprintf(stderr, "[d4tune] f=%ld the Tune Car mode selection is active\n", autotest_frame());
            }
        }
        if (D.state == DT_QUEUED && autotest_frame() - D.at > UI_TIMEOUT_FRAMES){
            D.bundle = 0; D.bundle_mark = 0; D.bundle_ready = 0;
            finish(c, 0, "the tuning screen did not load");
        }
    }
    if (D.state == DT_ACTIVE && top_is("UI_PerformanceDyno_MAIN.fng")){
        D.seen_main = 1;
        uint32_t find[2] = { S_DYNO_MAIN, OBJ_DYNO_TITLE };
        uint32_t object = gcall_main(c, FN_FE_STRING, 0, 2, find);
        if (object && guest_rd32(object + 0x60u) == LBL_DYNO_SHOP){
            uint32_t title[3] = { S_DYNO_MAIN, OBJ_DYNO_TITLE, D.label };
            gcall_main(c, FN_FE_SET_LABEL, 0, 3, title);
        }
    }
    if (D.state != DT_IDLE && D.exit_state != DX_WAIT_SAVE && MR32(G_FLOW) != 6u) finish(c, 1, "the world ended");
}

/* pausemenu.c's AddOption patch calls this before the stock AddOption. */
int d4tune_before_add(CPU *c, uint32_t menu, uint32_t it){
    if (D.state == DT_IDLE || !guest_ok(menu, 3u) || MR32(menu) != VT_DYNO_MAIN || !guest_ok(it, IT_LABEL + 3u)) return 0;
    uint32_t label = MR32(it + IT_LABEL);
    if (label != LBL_DYNO_RUN && label != LBL_TEST_RUN && label != LBL_QUIT_TUNING) return 0;
    gcall(c, FN_DELETE, 0, 1, &it);
    ret_this(c, 4u);
    return 1;
}

/* pausemenu.c calls this after it, so Tune Car is inserted immediately after Garage and
 * before the constructor adds Options. */
void d4tune_after_add(CPU *c, uint32_t menu, uint32_t it){
    if (!D.label || MR32(G_GAMEMODE) != 1u || MR32(G_RACETYPE) != 0u ||
        !guest_ok(it, IT_LABEL + 3u) || MR32(it + IT_LABEL) != LBL_GARAGE) return;
    uint32_t n = ITEM_BYTES, tune = gcall(c, FN_NEW, 0, 1, &n);
    if (!guest_ok(tune, ITEM_BYTES)) return;
    uint32_t a[3] = { ICON_TUNE_SETTINGS, D.label, 0u };
    gcall(c, FN_ITEM, tune, 3, a);
    MR32(tune) = VT_REPLAY;
    const char *why = not_now(1);
    MR8(tune + IT_GREY) = why ? 1 : 0;
    gcall(c, FN_ADD_ITEM, menu, 1, &tune);
    fprintf(stderr, "[d4tune] f=%ld pause menu: Tune Car%s%s%s\n", autotest_frame(), why ? " (greyed out: " : "",
            why ? why : "", why ? ")" : "");
}

/* pausemenu.c's Replay-action patch calls this first. */
int d4tune_pause_accept(CPU *c){
    uint32_t it = c->ecx, msg = MR32(c->esp + 8u);
    if (!D.label || !guest_ok(it, IT_LABEL + 3u) || MR32(it) != VT_REPLAY || MR32(it + IT_LABEL) != D.label) return 0;
    if (msg == MSG_ACCEPT && !MR8(it + IT_GREY)){
        D.from_pause = 1; D.pending = 1;
        fprintf(stderr, "[d4tune] f=%ld pause menu: Tune Car\n", autotest_frame());
    }
    ret_this(c, 0x14u);
    return 1;
}

static void setting_dispatch(CPU *c){
    uint32_t msg = MR32(c->esp + 4u);
    if (D.state != DT_IDLE && exit_message(c, msg, S_SETTING, sub_0055a740)) return;
    if (D.state != DT_IDLE && msg == MSG_BACK && D.exit_state == DX_NONE)
        if (back(c, S_SETTING, D.seen_main ? "Mode Select Back, unchanged" : "Initial Mode Select Back, unchanged")) return;
    sub_0055a740(c);
}

static void d4tune_dyno_dispatch(CPU *c){
    uint32_t msg = MR32(c->esp + 4u);
    if (D.state != DT_IDLE) D.seen_main = 1;
    if (D.state != DT_IDLE && exit_message(c, msg, S_DYNO_MAIN, dyno_dispatch)) return;
    if (D.state != DT_IDLE && msg == MSG_BACK && D.exit_state == DX_NONE)
        if (back(c, S_DYNO_MAIN, "Dyno Main Back, unchanged")) return;
    if (D.state != DT_IDLE && msg == MSG_QUIT_YES){
        uint32_t package = guest_ok(c->ecx, 8u) ? guest_rd32(c->ecx + 4u) : 0u;
        request_close(package ? package : S_DYNO_MAIN, 1, msg == MSG_BACK ? "Dyno Main Back" : "Quit Tuning");
        ret_this(c, 0x10u);
        return;
    }
    dyno_dispatch(c);
}

void d4tune_install(void){
    D.f5_enabled = setting_bool("NFSU2_D4TUNE_F5", 0);
    dispatch_patch(FN_SETTING_MSG, setting_dispatch);
    dispatch_patch(FN_DYNO_MSG, d4tune_dyno_dispatch);
}
