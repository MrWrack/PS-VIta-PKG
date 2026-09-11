# VPK Manager v5 – PS Vita

Homebrew VPK manager with PC Quick Install and direct Custom Theme transfer.

## Vita controls
- **X** – install selected VPK from `ux0:/download/`
- **Triangle** – delete selected VPK
- **Square** – refresh list
- **R** – wait for a VPK from the PC on port **1338**, then install it
- **L** – wait for a custom theme from the PC on port **1339**, unpack it to `ux0:/data/vpk_manager/theme/`, and apply it
- **START** – theme menu
- **O** – back/exit

## Custom Theme format
The theme directory can contain:
- `background.png` (recommended 960x544)
- `theme.ini`

Example `theme.ini`:
```ini
name=My Custom Theme
bg=#071018
panel=#101820
accent=#00FF88
text=#FFFFFF
selected=#22CCFF
```

## Send a theme from PC
1. Put Vita and PC on the same Wi‑Fi.
2. In VPK Manager press **L**. The Vita waits on port **1339**.
3. Open **VPK Manager Theme Creator v2** on the PC.
4. Enter the Vita IP shown in VPK Manager.
5. Click **Send Theme to PS Vita**.
6. The Vita receives, extracts and immediately applies the custom theme.

The PC creator sends a small ZIP using the `THM1` protocol expected by the Vita receiver.

## Build
Requires VitaSDK + vita2d.
```bash
mkdir build && cd build
cmake ..
make
```
The generated output is `VPKManager.vpk`.

## GitHub Actions – get the actual VPK first
This project now includes `.github/workflows/build.yml`.

1. Upload the contents of this folder to a GitHub repository.
2. Open **Actions** → **Build VPK Manager**.
3. Choose **Run workflow**.
4. When the job finishes, download the artifact **VPKManager-v5-PS-Vita**.
5. Inside it is the file you install on the PS Vita: **`VPKManager.vpk`**.

The workflow uses the official/community VitaSDK Docker image and builds the same target configured by `CMakeLists.txt`.
