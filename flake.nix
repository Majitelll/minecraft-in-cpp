{
  description = "Minecraft in C++ - Vulkan/WebGL2 voxel engine";

  inputs = {
    nixpkgs.url     = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";

    # Submodule sources pinned to the exact commits in .gitmodules
    glfw-src = {
      url   = "github:glfw/glfw/8e15281d34a8b9ee9271ccce38177a3d812456f8";
      flake = false;
    };
    cglm-src = {
      url   = "github:recp/cglm/a4602f2d5f1f275c02ef608ef27d4021572d9d6c";
      flake = false;
    };
  };

  outputs = { self, nixpkgs, flake-utils, glfw-src, cglm-src }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in {
        packages.default = pkgs.stdenv.mkDerivation {
          pname   = "minecraft-cpp";
          version = "0.1.0";

          src = self;

          # Populate the submodule directories that nix omits from the source tree
          postUnpack = ''
            mkdir -p $sourceRoot/external
            cp -r ${glfw-src} $sourceRoot/external/glfw
            cp -r ${cglm-src} $sourceRoot/external/cglm
            chmod -R u+w $sourceRoot/external
          '';

          nativeBuildInputs = with pkgs; [
            cmake
            pkg-config
            shaderc.bin     # provides glslc (bin output, not default lib output)
            wayland-scanner # needed by GLFW's Wayland backend at configure time
            patchelf
            makeWrapper     # wraps the binary to set CWD for relative shader paths
          ];

          # Tell CMake exactly where glslc lives (PATH-based find_program also works
          # since shaderc.bin is in nativeBuildInputs, but explicit is more robust)
          cmakeFlags = [
            "-DGLSLC=${pkgs.shaderc.bin}/bin/glslc"
            "-DCGLM_SHARED=OFF"  # build cglm as static lib so it's not a runtime dep
          ];

          buildInputs = with pkgs; [
            vulkan-loader
            vulkan-headers
            vulkan-validation-layers
            libGL libGL.dev
            libx11 libx11.dev
            libxrandr libxrandr.dev
            libxinerama libxinerama.dev
            libxcursor libxcursor.dev
            libxi libxi.dev
            libxext libxext.dev
            libxkbcommon libxkbcommon.dev
            wayland
            libffi libffi.dev
            xorgproto
          ];

          installPhase = ''
            runHook preInstall
            mkdir -p $out/bin
            # Install the raw ELF as .game-unwrapped; the wrapper below provides `game`
            cp game $out/bin/.game-unwrapped
            cp -r shaders $out/bin/
            # Shader paths are resolved relative to the executable location.
            # Let the Vulkan loader discover system ICDs via XDG_DATA_DIRS
            # (/run/opengl-driver is populated by NixOS hardware.graphics.enable).
            # LD_LIBRARY_PATH is needed so NVIDIA's ICD .so can find its deps.
            makeWrapper $out/bin/.game-unwrapped $out/bin/game \
              --prefix LD_LIBRARY_PATH : /run/opengl-driver/lib \
              --prefix XDG_DATA_DIRS : /run/opengl-driver/share \
              --set-default VK_LAYER_PATH "${pkgs.vulkan-validation-layers}/share/vulkan/explicit_layer.d"
            runHook postInstall
          '';

          # Fix the RPATH on the raw ELF (the wrapper is a shell script, not an ELF).
          # preFixup runs before the forbidden-reference check in fixupPhase.
          postFixup = ''
            patchelf --set-rpath "${pkgs.lib.makeLibraryPath [
              pkgs.stdenv.cc.cc.lib
              pkgs.vulkan-loader
              pkgs.libxkbcommon
              pkgs.libx11
              pkgs.wayland
            ]}" $out/bin/.game-unwrapped
          '';
        };

        devShells.default = pkgs.mkShell {
          inputsFrom = [ self.packages.${system}.default ];

          packages = with pkgs; [
            gdb
            valgrind
            vulkan-tools   # vulkaninfo, vkcube
            renderdoc
          ];

          shellHook = ''
            export VK_LAYER_PATH="${pkgs.vulkan-validation-layers}/share/vulkan/explicit_layer.d"
            echo "minecraft-cpp dev shell ready"
            echo "Tip: cmake -S . -B build && cmake --build build -j\$(nproc)"
          '';
        };
      }
    );
}
