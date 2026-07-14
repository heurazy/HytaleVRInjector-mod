# Changelog

## [0.9.5] - 2026-07-14

### VR shadows and effects

- Restored the optional Hytale shadow, particle, water, and distortion passes in VR.
- Isolated Hytale's mono distortion field and reapplied it once per reconstructed eye to remove duplicated effects.
- Synchronized the VR view, projection, inverse view-projection, and projection reconstruction data used by Hytale's effect shaders.
- Corrected the independent SSAO/deferred-shadow temporal reprojection matrix so shadows remain anchored while the headset moves.
- Added `VR shadows`, `VR particles`, and `VR distortion effects` controls under Advanced options. They are enabled by default.

### Notes

- Restart Hytale completely before replacing an already injected hook DLL.
- These passes depend on Hytale's current OpenGL program layout and may require compatibility updates after a major renderer change.

## [0.9.0] - 2026-07-14

### AFW rendering

- Replaced alternating-eye presentation with AFW stereo reconstruction.
- Rendered one stable centered Hytale camera per frame, then reconstructed both SteamVR eye views from the game color buffer, linear scene depth, eye projections, and tracked poses.
- Added synchronized per-eye depth for hand occlusion and stabilized Noesis UI capture to reduce ghosting and flicker.
- Added an advanced `Resolution %` control from 50% to 200% while preserving the source aspect ratio and eye projections.

### VR comfort and interaction

- Aligned the in-game floor with SteamVR Standing space and compensated Hytale's native sneak camera movement.
- Set the default world scale to 130% and exposed world scale, hand depth tolerance, and VR resolution controls.
- Added physical jump and crouch detection. Physical block-hit interaction remains disabled while its reliability is improved.
- Fixed the X button sending the use key twice.

### Compatibility and reliability

- Added default SteamVR bindings for Oculus Touch, Valve Index, Vive, Vive Cosmos, Windows Mixed Reality, and HP Motion Controllers.
- Added validated hook-site scans and safer fallbacks for Hytale updates.
- Added clean render-hook shutdown and module unloading.
- Reduced release logging and redundant framebuffer work.
- Split shared validation, key-binding, depth, and render-resolution logic into testable helper modules.

### Notes

- AFW is a depth-based stereo reconstruction path, not native dual-camera rendering.
- Disable FXAA in Hytale, start SteamVR before injection, and keep Hytale focused for controls.
- Restart Hytale completely before replacing an already injected hook DLL.
