# Installing Raz

Raz releases provide a Windows MSI installer, a portable toolchain archive, and the `razup` toolchain manager. The MSI and portable archive install the same production command image, runtime library, standard library sources, license files, and integrity manifest.

## Windows MSI

The MSI installs Raz under `C:\Program Files\Raz` by default.

The installer includes an **Add Raz to PATH** feature. It is selected by default and can be turned off from the installer feature-selection screen. When enabled, the installer appends the Raz `bin` directory to the system `PATH`. Removing Raz through Windows Installer removes the Raz entry without replacing unrelated `PATH` entries.

After installation, open a new terminal and run:

```text
raz --version
raz doctor
razup --version
```

For unattended installation with the default features:

```text
msiexec /i raz-1.0.0-windows-x64.msi /qn
```

To install without changing `PATH`:

```text
msiexec /i raz-1.0.0-windows-x64.msi /qn ADDLOCAL=RazToolchain
```

## Portable Windows archive

Extract the Windows archive and either run Raz directly from `bin`, or use the included PowerShell installer:

```powershell
.\install.ps1
```

The portable installer defaults to the current user's `PATH`. The location and PATH scope can be selected explicitly:

```powershell
.\install.ps1 -InstallDir C:\Tools\Raz -PathScope User
.\install.ps1 -InstallDir C:\Tools\Raz -PathScope Machine
.\install.ps1 -InstallDir C:\Tools\Raz -PathScope None
```

Machine PATH changes require an elevated PowerShell session.

The installed copy includes `uninstall.ps1`, which removes the Raz PATH entry recorded by the portable installer and then removes the installed files.


## Razup toolchain manager

`razup` manages versioned Raz toolchains independently of the package manager.
The default per-user root is `~/.razup`; set `RAZUP_HOME` to use another
location.

Common commands:

```text
razup install stable
razup install nightly
razup install 1.0.0
razup default stable
razup update
razup toolchain list
razup show
razup env
razup uninstall 1.0.0
```

`stable` resolves `stable.txt` from the latest non-prerelease GitHub Release,
while `nightly` uses the repository channel pointer. A versioned installation
resolves its immutable GitHub Release asset directly. `RAZUP_CHANNEL_BASE` can
redirect named-channel lookup to a mirror or test fixture. Every archive is
checked against its SHA-256 digest before any files are installed.

The managed layout is:

```text
~/.razup/
  current/
  downloads/
  toolchains/
  settings.txt
  toolchains.txt
```

`razup env` prints the selected toolchain's `bin` directory. Add that directory
to `PATH` when using `razup` as the active toolchain selector. The Windows MSI
and portable installer retain their own selectable PATH behavior for a direct
installation.

Official binary packaging and channel publication are maintained in the separate
`raz-language/installer` repository. Stable and pinned `razup` toolchains are
published from that repository; compiler and standard-library sources remain
in `raz-language/raz`.

## Release layout

A redistributable toolchain uses this layout:

```text
raz-<version>-<platform>/
  bin/
    raz
    razc
    razup
  lib/
    raz_runtime
  share/
    raz/
      library/
  licenses/
  README.md
  VERSION
  manifest.sha256
```

On Windows, executable and library file names use the native `.exe` and `.lib` suffixes.

`manifest.sha256` contains a SHA-256 digest for every file in the portable toolchain and can be used to verify archive contents before installation.
