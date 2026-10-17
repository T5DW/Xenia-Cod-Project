#pragma once

// A seeded list of Call of Duty: Ghosts (IW6) dvar names for console
// autocomplete. These are names only - they do nothing until the engine
// dvar functions are resolved (see dvar.cpp). Once sortedDvars/dvarCount are
// wired up, the live engine table is merged in and covers everything,
// including anything missing here.
namespace dvar_list
{
    constexpr const char* kGhostsDvars[] = {
        // --- com / system ---
        "com_maxfps", "com_maxframetime", "com_timescale", "developer",
        "developer_script", "logfile", "con_minicon", "monkeytoy",
        "com_introplayed", "com_playerProfile", "sys_quickboot",

        // --- renderer ---
        "r_fullscreen", "r_mode", "r_aspectratio", "r_vsync", "r_displayRefresh",
        "r_gamma", "r_brightness", "r_picmip", "r_texFilterAnisoMax",
        "r_multiGpu", "r_fovScale", "r_fog", "r_dof_enable", "r_distortion",
        "r_zFeather", "r_lodScaleRigid", "r_lodScaleSkinned", "r_lodBiasRigid",
        "r_lodBiasSkinned", "r_drawSun", "r_sky_intensity_factor0",
        "r_motionblur", "r_ssao", "r_ssaoStrength", "r_subsurfScatter",
        "r_fullbright", "r_showtris", "r_drawDecals", "r_depthPrepass",
        "r_normalMap", "r_specularMap", "r_outdoor", "r_lightTweakSunColor",
        "r_tonemap", "r_filmEnable", "r_filmContrast", "r_filmBrightness",

        // --- cg (client game / hud) ---
        "cg_fov", "cg_fovScale", "cg_fovMin", "cg_drawFPS", "cg_drawFPSLabels",
        "cg_draw2D", "cg_drawCrosshair", "cg_crosshairAlpha", "cg_crosshairDynamic",
        "cg_drawGun", "cg_gun_x", "cg_gun_y", "cg_gun_z", "cg_viewZSmoothingMax",
        "cg_hudDamageIconTime", "cg_drawViewpos", "cg_thirdPerson",
        "cg_thirdPersonRange", "cg_thirdPersonAngle", "cg_drawhud",
        "cg_drawCrosshairNames", "cg_subtitles", "cg_brass", "cg_blood",
        "cg_drawShellshock", "cg_laserForceOn", "cg_weaponCycleDelay",
        "cg_scoreboardPingText", "cg_hudGrenadeIconMaxRangeFrac",

        // --- player / movement / bg ---
        "player_sprintTime", "player_sprintSpeedScale", "player_sustainAmmo",
        "player_meleeHeight", "player_meleeRange", "player_meleeWidth",
        "player_breath_hold_time", "player_breath_fire_delay", "player_clipSizeMultiplier",
        "jump_height", "jump_slowdownEnable", "jump_stepSize", "jump_ladderPushVel",
        "bg_fallDamageMinHeight", "bg_fallDamageMaxHeight", "bg_bobMax",
        "bg_viewBobMax", "bg_weaponBobMax", "bg_swayMaxAngle", "bg_prone",
        "g_speed", "g_gravity", "g_knockback", "g_compassShowEnemies",
        "g_dropForwardSpeed", "g_playerEjection", "g_playerCollisionEjectSpeed",
        "g_inactivity", "g_friendlyNameDist", "g_fogColorWriteOverride",

        // --- ai ---
        "ai_corpseCount", "ai_friendlySuppression", "ai_debugAnims",
        "ai_disableSpawn", "ai_eventDist", "ai_meleeRange", "ai_showPaths",
        "ai_showGrenadeThrow", "ai_showLiteObjs", "ai_pathCornerDist",

        // --- physics ---
        "phys_gravity", "phys_gravity_ragdoll", "phys_bulletUpBias",
        "phys_bulletSpinScale", "ragdoll_enable", "ragdoll_max_simulating",
        "ragdoll_baseTime", "ragdoll_bullet_force",

        // --- sound ---
        "snd_enableReverb", "snd_volume", "snd_musicVolume", "snd_cinematicVolumeScale",
        "snd_drawInfo", "snd_errorOnMissingSound", "snd_slaveFxVolume",
        "snd_slaveMusicVolume", "snd_touchStreamFilesOnLoad",

        // --- client / input ---
        "cl_maxpackets", "cl_timeout", "cl_yawspeed", "cl_pitchspeed",
        "cl_freelook", "cl_mouseAccel", "cl_bypassMouseInput", "sensitivity",
        "in_mouse", "m_pitch", "m_yaw", "m_forward", "m_side", "m_filter",

        // --- ui / menu ---
        "ui_drawCrosshair", "ui_gametype", "ui_mapname", "ui_maxclients",
        "ui_hud_hardcore", "ui_smallFont", "ui_bigFont", "ui_textScale",
        "ui_showList", "ui_debugMode", "ui_allow_teamchange",

        // --- server / game rules (SP relevant subset) ---
        "sv_cheats", "sv_running", "sv_maxclients", "sv_pure", "sv_fps",
        "g_speed", "g_dropItemMaxDist", "g_deadChat", "scr_showcorpses",

        // --- scr_ (script) ---
        "scr_difficulty", "scr_hardcore", "scr_help", "scr_abort",

        // --- misc toggles people poke in SP ---
        "timescale", "notarget", "noclip", "god", "demigod", "ufo",
        "cg_drawTalk", "cg_drawSnapshot", "r_debugShader", "r_clear",
        "con_gameMsgWindow0LineCount", "cl_showsend", "cl_shownet",
    };
}
