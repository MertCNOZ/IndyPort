#pragma once
// The game's addresses the port's hooks and helpers use, for each release: USA (IndyET.exe, assets/default.xbe) and
// Europe / PAL (IndyET_EU.exe, INDY_EU, assets_eu/default.xbe). The PAL release is the same code linked a little
// differently; its addresses were found with tools/map_release.py (code by masked byte matching, data through the
// instructions that load it), the structure offsets inside the game's objects are the same in both.
//
// A hook names the function it replaces: X86_FUNC(FN_X) for the replacement, X86_EXTERN_FUNC(ORIG(FN_X)) for the
// original it calls.

#define INDY_CAT_(a, b) a##b
#define INDY_CAT(a, b) INDY_CAT_(a, b)
#define ORIG(fn) INDY_CAT(__imp__, fn)

#ifdef INDY_EU
// functions
#define FN_FAR_CLIP sub_00323CB0      // the level's far clip (draw distance)
#define FN_PROJECTION sub_00323400    // the camera's projection and culling planes (widescreen, fov)
#define FN_TEXTBOX_FRAME sub_0022D680 // the text box's per frame method (name entry)
#define FN_TEXTBOX_KEY sub_0022D0A0   // the text box's key handler
#define FN_CAMERA_3RD sub_0010B250    // the third person camera's update
#define FN_CAMERA_VECTOR sub_0010B090 // its flat camera to Indy vector
#define FN_INPUT_GATHER sub_00103FA0  // the player's input, camera pair in its second argument
#define FN_CAMERA_1ST sub_00108340    // the first person camera's update
// return addresses inside the third person camera's update
constexpr uint32_t kRetCameraVector = 0x10B403u;  // after it calls FN_CAMERA_VECTOR
constexpr uint32_t kRetInputGather = 0x10B2E9u;   // after it calls FN_INPUT_GATHER
constexpr uint32_t kTextboxFrame = 0x22D680u;     // FN_TEXTBOX_FRAME, pushed as a return address
// data
constexpr uint32_t kHalfAngleH = 0x6D7EF0u;       // the projection's horizontal half angle
constexpr uint32_t kHalfAngleV = 0x6D7EF4u;       // and vertical
constexpr uint32_t kTextboxVtable = 0x58E608u;    // the text box class
constexpr uint32_t kGameSettingsPtr = 0x6B9CA8u;  // the game's settings object
constexpr uint32_t kD3DDevicePtr = 0x4A9C08u;     // D3D_pDevice
#else
#define FN_FAR_CLIP sub_00323A10
#define FN_PROJECTION sub_00323160
#define FN_TEXTBOX_FRAME sub_0022DCE0
#define FN_TEXTBOX_KEY sub_0022D700
#define FN_CAMERA_3RD sub_0010B360
#define FN_CAMERA_VECTOR sub_0010B1A0
#define FN_INPUT_GATHER sub_001040B0
#define FN_CAMERA_1ST sub_00108450
constexpr uint32_t kRetCameraVector = 0x10B513u;
constexpr uint32_t kRetInputGather = 0x10B3F9u;
constexpr uint32_t kTextboxFrame = 0x22DCE0u;
constexpr uint32_t kHalfAngleH = 0x6D75F0u;
constexpr uint32_t kHalfAngleV = 0x6D75F4u;
constexpr uint32_t kTextboxVtable = 0x58DD00u;
constexpr uint32_t kGameSettingsPtr = 0x6B93A8u;
constexpr uint32_t kD3DDevicePtr = 0x4A9668u;
#endif
