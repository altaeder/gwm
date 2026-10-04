{
  description = "GalleryWM (gwm)";

  inputs =
  {
    nixpkgs.url = "nixpkgs/nixos-unstable";

    neuswc-gwm = 
    {
      url = "github:altaeder/neuswc-gwm";
    };
  };

  outputs = 
  { self, nixpkgs, neuswc-gwm, ... }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs 
      {
        inherit system;
      };
    in
    {
      packages.${system}.default = pkgs.stdenv.mkDerivation
      {
        pname = "gwm";
        version = "0.0";

        src = self;

        nativeBuildInputs = with pkgs;
        [
          pkg-config
          makeWrapper
        ];

        buildInputs = with pkgs;
        [
          neuswc-gwm.packages.${system}.default

          # Extra Dependencies
          libxkbcommon
          libinput
          libdrm
          libgbm
          libGL
          libGLU
          mesa
          mesa-gl-headers
          mesa_glu
          libglvnd
          xf86-video-amdgpu
          egl-gbm
          egl-wayland
          wayland
          xwayland
          libxcb
          libxcb-wm
        ];

        makeFlags =
        [
          "PREFIX=$(out)"
        ];

        postInstall = 
        ''
          makeWrapper \
            ${neuswc-gwm.packages.${system}.default}/bin/swc-launch \
            $out/bin/gwm-launch \
            --add-flags "$out/bin/gwm"
        '';
      };

      devShells.${system}.default = 
      pkgs.mkShell
      {
        packages = with pkgs;
        [
          gcc
          pkg-config
          neuswc-gwm.packages.${system}.default
          
          # Extra Dependencies
          libxkbcommon
          libinput
          libdrm
          libgbm
          libGL
          libGLU
          mesa
          mesa-gl-headers
          mesa_glu
          libglvnd
          xf86-video-amdgpu
          egl-gbm
          egl-wayland
          wayland
          xwayland
          libxcb
          libxcb-wm
        ];
      };
    };
}