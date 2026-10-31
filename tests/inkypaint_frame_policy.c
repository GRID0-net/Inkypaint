/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
#include "nx/runtime/inkypaint_frame_policy.h"
#include "nx/runtime/inkypaint_frame_clock.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    assert(inkypaint_frame_render_cap(60) == 60);
    assert(inkypaint_frame_render_cap(100) == 100);
    assert(inkypaint_frame_render_cap(240) == 240);
    assert(inkypaint_frame_render_cap(0) == 120);
    assert(inkypaint_frame_render_cap(-1) == 120);
    assert(inkypaint_frame_render_cap(30) == 120);
    assert(inkypaint_frame_render_cap(241) == 120);
    const char* logic[] = {"Actor", "ActorAIGroupMgr::SetupJob", "ActorAIGroupMgr::Job", "StartBaseProc", "RunBaseProc",
        "Controller", "ControllerCheck", "GameUIFactory", "GameUIFactory::Update1st", "GameUIFactory::Update2ndMain",
        "GameUIFactory::Update2ndSub1", "GameUIFactory::Update2ndSub2", "GameNet:Recv", "GameNet:Send", "Banc",
        "Event", "Camera", "SoundModule", "Scene", "GameFrame", "Physics", "", "UnclassifiedNewModule"};
    const char* graphics[] = {"Graphics", "Resource", "GameGfx", "GameGfx:PostActorCalc", "GameGfx:UpdateDynamicLight",
        "EntryJobsBgGraphics"};
    for (unsigned role = 0x4c8; role <= 0x4e0; role += 8) {
        for (unsigned i = 0; i < sizeof logic / sizeof *logic; i++)
            assert(inkypaint_frame_simulation_job(role, logic[i], 0xcda24e0));
        for (unsigned i = 0; i < sizeof graphics / sizeof *graphics; i++)
            assert(!inkypaint_frame_simulation_job(role, graphics[i], 0xcda24e0));
        assert(!inkypaint_frame_simulation_job(role, "", 0xc7b56f0));
        assert(!inkypaint_frame_simulation_job(role, "", 0x9366800));
    }
    assert(!inkypaint_frame_capture(0, 1));
    assert(inkypaint_frame_capture(1, 1));
    assert(inkypaint_frame_capture(0, 0));
    for (unsigned hz = 90; hz <= 144; hz += 2) {
        InkypaintFrameClock clock = {0};
        unsigned updates = 0, renders = 0;
        for (unsigned f = 0; f < hz * 10; f++) {
            InkypaintFrameSample sample = inkypaint_frame_sample(&clock, (uint64_t)f * 1000000000ull / hz);
            updates += sample.steps;
            /* Every input consumer and stateful callback must see exactly the same
               step stream, irrespective of which graph owns it. Extra render
               callbacks must never consume an edge before its UI consumer. */
            int controller = !inkypaint_frame_simulation_job(0x4d0, "Controller", 0) || sample.steps;
            int menu = !inkypaint_frame_simulation_job(0x4c8, "GameUIFactory::Update2ndMain", 0) || sample.steps;
            int scene = !inkypaint_frame_simulation_job(0x4d8, "Event", 0) || sample.steps;
            assert(controller == menu && menu == scene);
            assert(controller == (sample.steps != 0));
            renders++;
        }
        assert(updates == 600 && renders == hz * 10);
    }
    puts("PASS render policy: all state/input fixed-step, graphics allowlist, immutable endpoints and fixed-rate cadence");
}
