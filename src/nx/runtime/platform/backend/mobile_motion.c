/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
#include "nx/runtime/platform/mobile_motion.h"
#include "nx/runtime/platform/mobile_motion_math.h"
#include "nx/runtime/platform/log.h"
#include "snx_settings.h"
#ifdef SDL_PLATFORM_IOS
static struct {
    SDL_Sensor *accel, *gyro;
    int attempted, initialized;
} g_mobile_motion;
static void close_sensors(void) {
    if (g_mobile_motion.accel) SDL_CloseSensor(g_mobile_motion.accel);
    if (g_mobile_motion.gyro) SDL_CloseSensor(g_mobile_motion.gyro);
    g_mobile_motion.accel = g_mobile_motion.gyro = NULL;
    if (g_mobile_motion.initialized) SDL_QuitSubSystem(SDL_INIT_SENSOR);
    g_mobile_motion.initialized = g_mobile_motion.attempted = 0;
}
#endif
void mobile_motion_suspend(void) {
#ifdef SDL_PLATFORM_IOS
    close_sensors();
#endif
}
void mobile_motion_sample(SDL_Window* window, int active, PadMotion* out) {
#ifdef SDL_PLATFORM_IOS
    if (!active || !window || !g_settings.device_motion) {
        close_sensors();
        return;
    }
    if (!g_mobile_motion.attempted) {
        g_mobile_motion.attempted = 1;
        if (!SDL_InitSubSystem(SDL_INIT_SENSOR)) {
            WARN("mobile motion: sensors unavailable: %s\n", SDL_GetError());
            return;
        }
        g_mobile_motion.initialized = 1;
        int n = 0;
        SDL_SensorID* ids = SDL_GetSensors(&n);
        for (int i = 0; ids && i < n; i++) {
            SDL_SensorType type = SDL_GetSensorTypeForID(ids[i]);
            if (type == SDL_SENSOR_ACCEL && !g_mobile_motion.accel) g_mobile_motion.accel = SDL_OpenSensor(ids[i]);
            if (type == SDL_SENSOR_GYRO && !g_mobile_motion.gyro) g_mobile_motion.gyro = SDL_OpenSensor(ids[i]);
        }
        SDL_free(ids);
        LOG("mobile motion: accelerometer %s, gyroscope %s\n",
            g_mobile_motion.accel ? "ready" : "unavailable", g_mobile_motion.gyro ? "ready" : "unavailable");
    }
    if (!g_mobile_motion.accel || !g_mobile_motion.gyro) return;
    SDL_DisplayOrientation orientation = SDL_GetCurrentDisplayOrientation(SDL_GetDisplayForWindow(window));
    if (orientation != SDL_ORIENTATION_LANDSCAPE && orientation != SDL_ORIENTATION_LANDSCAPE_FLIPPED) return;
    float a[3], w[3];
    PadMotion motion = {0};
    if (SDL_GetSensorData(g_mobile_motion.accel, a, 3) && SDL_GetSensorData(g_mobile_motion.gyro, w, 3) &&
        mobile_motion_convert(a, w, orientation == SDL_ORIENTATION_LANDSCAPE_FLIPPED,
                              motion.acceleration, motion.angular_velocity)) {
        motion.valid = 1;
        *out = motion;
    }
#else
    (void)window;
    (void)active;
    (void)out;
#endif
}
